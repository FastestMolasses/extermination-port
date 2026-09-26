#!/usr/bin/env python3
"""export_roger_banks.py - the resources Roger's owner reads at run time.

Roger (AREA11 overlay 008237E0, record 0x7A8830) and the equipment node that
rides on him (001C5C90, record 0x7A8B20) are bound over original record
bytes (em_area11_roger.c, docs/ROGER_ACTOR_ORIGINAL.md "Binding"). Their
original routines follow EE addresses: the resource table D_0028A490 (the
words 008237E0 / 001B10B0 / 001BA8E0 / the area scripts index), Roger's clip
banks (+0x40: bank 0x4A at rest, bank 0x96 in the encounter, which also holds
the camera track and the player's encounter clip), his model (+0x44, the
byte 001C6150 reads) and the equipment's model (D_0028A56C's entry 0x6B,
001B1020). This tool writes those original bytes at their original
addresses, from the user's own extracted disc:

  * the resource table: D_0028A490[0 .. 0xC0) (0x300 bytes), the load
    addresses the engine gives its resource files. The table is runtime data;
    it is taken from the playable capture and must be identical in every
    AREA11 route capture;
  * extract/chunk15/f12_id44.bin from +0x41000 to its end at
    D_0028A490[0x4A] - 0x10E000 + 0x41000 (bank 0x96 at +0x41000, bank 0x4A
    at +0x10E000, the +0x58 word at +0x104800). The file's earlier bytes
    (+0x39B46..+0x3E0C3) are rewritten at run time and are not exported;
  * extract/chunk15/f18_id94.bin whole at D_0028A490[0x47] - 0x35000 (the
    model 0x47 at +0x35000 and the face resource 0x88 at +0x86000);
  * extract/chunk27/f01_id37.bin's table head (D_0028A56C = D_0028A490[0x37]:
    the count word and the 126 entry words) and the equipment models it
    indexes (header, blocks and skeleton records): 0x6B (Roger's 001C5C90)
    and every id the player equipment's 0018A8D0 can bind (0x2F; 0x30,
    0x40, 0x6D; 0x31..0x3D; 0x6A; em_equipment_live.c, census L28).

Every region is checked byte for byte against RAM at its address in every
AREA11 capture (default: playable_ee.bin and route beats 00..14).

Output (disc-derived: git-ignored assets/ only; nothing is embedded here):
  assets/scene_snow/roger/resources.emrs, little-endian:
    0x00 'EMRS', u32 version 1, u32 table address (0x28A490), u32 table words N,
    u32 region count R, 3 x u32 0
    0x20 N table words
    then R regions: u32 address, u32 size, u32 writable (0), u32 0, size bytes
  build/roger_banks/export.json: addresses, sizes and SHA-256s only.

Runs natively on arm64 macOS (pure Python).

Usage (port root):
  python3 tools/export_roger_banks.py
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
TABLE, TABLE_WORDS = 0x0028A490, 0xC0
BANK_4A_AT, BANK_96_AT, MODEL_AT, FACE_AT = 0x10E000, 0x41000, 0x35000, 0x86000
GLOBAL_TABLE_INDEX = 0x37
# Roger's equipment 001C5C90 (0x6B) and the ids 0018A8D0 maps (flavour, variant)
# to: 0x2F; 0x30 / 0x40 / 0x6D; 0x32, 0x33, 0x34, 0x35, 0x36, 0x31, 0x37, 0x38 and
# 0x39..0x3D; 0x6A. Each span is one region, from its first model's header to
# its last model's skeleton end (the file bytes between are exported too and
# checked like the rest).
EQUIPMENT_SPANS = ((0x2F, 0x3D), (0x40, 0x40), (0x6A, 0x6D))


def u32(b, a): return struct.unpack_from('<I', b, a)[0]


def default_captures():
    route = DECOMP / 'build/s87/route'
    playable = DECOMP / 'build/startup-reference/playable_ee.bin'
    return ([playable] if playable.exists() else []) + sorted(
        p for p in route.glob('*/eeMemory.bin') if not p.parent.name.startswith('15'))


def block_model_size(data: bytes, off: int) -> int:
    """Bytes of the block model at `off`: header, blocks, skeleton records."""
    blocks, qwc, bones, skel = struct.unpack_from('<4I', data, off)
    if blocks < 1 or qwc != blocks * 0x82 or bones > 0xFF or skel != 0x40 + 16 * qwc:
        raise SystemExit(f'the equipment model at +{off:#x} is not a block model')
    return skel + 0x50 * bones


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--extract', type=Path, default=DECOMP / 'extract')
    ap.add_argument('--out', type=Path, default=ROOT / 'assets/scene_snow/roger/resources.emrs')
    ap.add_argument('--verify-ram', type=Path, action='append', default=None)
    args = ap.parse_args(argv)
    captures = args.verify_ram if args.verify_ram is not None else default_captures()
    if not captures:
        raise SystemExit('no AREA11 capture to take D_0028A490 from')
    first = captures[0].read_bytes()
    table = [u32(first, TABLE + 4 * i) for i in range(TABLE_WORDS)]

    f12 = (args.extract / 'chunk15/f12_id44.bin').read_bytes()
    f18 = (args.extract / 'chunk15/f18_id94.bin').read_bytes()
    f37 = (args.extract / 'chunk27/f01_id37.bin').read_bytes()
    base12 = table[0x4A] - BANK_4A_AT
    if table[0x96] != base12 + BANK_96_AT:
        raise SystemExit('D_0028A490[0x96] is not bank 0x96 of the file that holds bank 0x4A')
    if table[0x4D] - base12 >= len(f12) or table[0x4D] < base12 + BANK_96_AT:
        raise SystemExit('D_0028A490[0x4D] lies outside the exported bank region')
    base18 = table[0x47] - MODEL_AT
    if table[0x88] != base18 + FACE_AT:
        raise SystemExit("D_0028A490[0x88] is not the face resource of Roger's model file")
    global_table = table[GLOBAL_TABLE_INDEX]
    count = u32(f37, 0)
    if not 0 < count < 0x400 or max(last for _first, last in EQUIPMENT_SPANS) >= count:
        raise SystemExit(f'chunk27/f01_id37.bin: table word 0 = {count}')
    regions = [
        (base12 + BANK_96_AT, f12[BANK_96_AT:]),
        (base18, f18),
        (global_table, f37[:4 + 4 * count]),
    ]
    for first, last in EQUIPMENT_SPANS:
        offsets = [struct.unpack_from('<i', f37, 4 + 4 * kind)[0] >> 2 << 2 for kind in range(first, last + 1)]
        if offsets != sorted(offsets):
            raise SystemExit(f'the equipment models {first:#x}..{last:#x} are not in file order')
        start = offsets[0]
        end = offsets[-1] + block_model_size(f37, offsets[-1])
        for off in offsets:
            if off + block_model_size(f37, off) > end:
                raise SystemExit(f'the equipment model at +{off:#x} leaves its span')
        regions.append((global_table + start, f37[start:end]))
    ordered = sorted(regions)
    for (a, da), (b, _db) in zip(ordered, ordered[1:]):
        if a + len(da) > b:
            raise SystemExit(f'regions {a:#x} and {b:#x} overlap')

    checked = []
    for path in captures:
        ram = path.read_bytes()
        words = [u32(ram, TABLE + 4 * i) for i in range(TABLE_WORDS)]
        if words != table:
            diff = next(i for i in range(TABLE_WORDS) if words[i] != table[i])
            raise SystemExit(f'{path}: D_0028A490[{diff:#x}] differs from the first capture')
        for address, data in regions:
            if ram[address:address + len(data)] != data:
                diff = next(i for i in range(len(data)) if ram[address + i] != data[i])
                raise SystemExit(f'{path}: RAM differs from the region at {address:#x} at +{diff:#x}')
        checked.append(str(path.relative_to(DECOMP) if path.is_relative_to(DECOMP) else path))

    out = bytearray(struct.pack('<4s7I', b'EMRS', 1, TABLE, TABLE_WORDS, len(regions), 0, 0, 0))
    out += struct.pack(f'<{TABLE_WORDS}I', *table)
    for address, data in regions:
        out += struct.pack('<4I', address, len(data), 0, 0) + data
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(bytes(out))
    report = dict(output=str(args.out.relative_to(ROOT)), bytes=len(out),
                  sha256=hashlib.sha256(bytes(out)).hexdigest(),
                  regions=[dict(address=f'{a:08X}', size=len(d), sha256=hashlib.sha256(d).hexdigest())
                           for a, d in regions],
                  captures=checked)
    receipt = ROOT / 'build/roger_banks'
    receipt.mkdir(parents=True, exist_ok=True)
    (receipt / 'export.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'roger banks: {len(regions)} regions, table {TABLE_WORDS} words, {len(out)} bytes -> '
          f'{args.out} (checked against {len(checked)} captures)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
