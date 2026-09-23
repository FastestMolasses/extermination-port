#!/usr/bin/env python3
"""Export the AREA11 overlay's truck/director scripts and director quads.

Copies two ranges of the user's own AREA11 overlay (extract/OVERLAY/AREA11.BIN,
MWo3, loaded whole at its header address 0x823500) into ignored EMSC files,
the format em_script_image_load reads ("EMSC", u32 1, base, entry, length,
then the bytes; docs/SCRIPT_HOST_WORKERS.md):

  assets/scene_snow/area11_scripts/scripts.emsc         0x8292C0..0x82A3C0
      truck camera preview 0x8292C0, director beats 0x8294C0 / 0x829A40 /
      0x829CC0 / 0x829E80
  assets/scene_snow/area11_scripts/director_quads.emsc  0x82ABE0..0x82ACA0
      the director's quads 0x82ABE0 / 0x82AC20 / 0x82AC60 (4 XYZW vertices each)

Nothing is rewritten. With the captured first-control RAM present
(../Extermination/build/startup-reference/playable_ee.bin), both ranges are
checked byte for byte against the loaded overlay there. The outputs hold
disc-derived data and stay ignored (never committed).
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
OVERLAY_BASE, OVERLAY_SIZE = 0x823500, 0x7800
SCRIPTS = (0x8292C0, 0x82A3C0)
QUADS = (0x82ABE0, 0x82ACA0)
ENTRIES = {0x8292C0: 'truck camera preview', 0x8294C0: 'director beat 0',
           0x829A40: 'director beat 1', 0x829CC0: 'director beat 2',
           0x829E80: 'director beat 3'}
OUT = ROOT / 'assets/scene_snow/area11_scripts'
RAM = DECOMP / 'build/startup-reference/playable_ee.bin'


def emsc(base, data):
    return struct.pack('<4s4I', b'EMSC', 1, base, base, len(data)) + data


def chain(data, base, entry):
    """Record addresses from entry to its stop record (jumps followed)."""
    pc, walked = entry, []
    for _ in range(64):
        assert base <= pc < base + len(data) and (pc - base) % 64 == 0, ('chain leaves range', hex(pc))
        walked.append(pc)
        flags = struct.unpack_from('<I', data, pc - base)[0]
        if flags & 0x80000000:
            return walked
        pc = struct.unpack_from('<I', data, pc - base + 4)[0] if flags & 0x40000000 else pc + 64
    raise AssertionError(('no stop record within 64 records', hex(entry)))


def export(overlay):
    """(scripts.emsc bytes, director_quads.emsc bytes, report)."""
    if len(overlay) != OVERLAY_SIZE or overlay[:4] != b'MWo3':
        raise ValueError('expected the original SCUS-97112 AREA11 overlay')
    if struct.unpack_from('<I', overlay, 8)[0] != OVERLAY_BASE:
        raise ValueError('unexpected overlay load base')
    scripts = overlay[SCRIPTS[0] - OVERLAY_BASE:SCRIPTS[1] - OVERLAY_BASE]
    quads = overlay[QUADS[0] - OVERLAY_BASE:QUADS[1] - OVERLAY_BASE]
    records = {}
    for entry, name in ENTRIES.items():
        walked = chain(scripts, SCRIPTS[0], entry)
        records[f'{entry:08X}'] = dict(name=name, records=len(walked))
    # The five chains tile the range exactly: every record belongs to one.
    covered = sum(r['records'] for r in records.values())
    if covered * 64 != len(scripts):
        raise ValueError('the script chains do not tile 0x8292C0..0x82A3C0')
    report = dict(scripts=dict(base=SCRIPTS[0], end=SCRIPTS[1], entries=records),
                  quads=dict(base=QUADS[0], end=QUADS[1], count=3))
    return emsc(SCRIPTS[0], scripts), emsc(QUADS[0], quads), report


def verify_ram(overlay, ram):
    """Both ranges equal the captured RAM (before any script ran there)."""
    for lo, hi in (SCRIPTS, QUADS):
        if overlay[lo - OVERLAY_BASE:hi - OVERLAY_BASE] != ram[lo:hi]:
            raise ValueError(f'{lo:08X}..{hi:08X} differs from the captured RAM')


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--overlay', type=Path, default=DECOMP / 'extract/OVERLAY/AREA11.BIN')
    parser.add_argument('--out', type=Path, default=OUT)
    parser.add_argument('--ram', type=Path, default=RAM,
                        help='captured RAM with the overlay loaded (checked when present)')
    args = parser.parse_args()
    overlay = args.overlay.read_bytes()
    scripts, quads, report = export(overlay)
    if args.ram.is_file():
        verify_ram(overlay, args.ram.read_bytes())
        report['ram_check'] = dict(path=str(args.ram), result='byte-identical')
    args.out.mkdir(parents=True, exist_ok=True)
    for name, blob in (('scripts.emsc', scripts), ('director_quads.emsc', quads)):
        (args.out / name).write_bytes(blob)
        report[name] = dict(size=len(blob), sha256=hashlib.sha256(blob).hexdigest())
    receipt = ROOT / 'build/area11_scripts/export.json'
    receipt.parent.mkdir(parents=True, exist_ok=True)
    receipt.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Exported {len(ENTRIES)} AREA11 overlay scripts ({(SCRIPTS[1] - SCRIPTS[0]) // 64} records)'
          f' and 3 director quads' + (' (checked against the captured RAM)' if 'ram_check' in report else ''))


if __name__ == '__main__':
    main()
