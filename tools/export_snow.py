#!/usr/bin/env python3
"""Export original AREA11 snow parameters, VU lookup and resident texture.

All outputs are generated locally from the user's original ELF/save state and
must remain ignored. The 80 scalar lookup values are original VIF upload data,
not recomputed sine values. The GS state must contain AREA11 textures.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp-root', type=Path, default=ROOT.parent / 'Extermination')
    parser.add_argument('--gs', type=Path, required=True)
    parser.add_argument('--reference-vu', type=Path)
    parser.add_argument('--reference-ee', type=Path,
                        help='required when --gs is a bare GS freeze instead of a save state')
    parser.add_argument('--out', type=Path, default=ROOT / 'assets/scene_snow')
    args = parser.parse_args()
    sys.path.insert(0, str(args.decomp_root / 'tools'))
    import export_native as native
    from audio_export import ElfImage
    if args.reference_ee:
        ram = args.reference_ee.read_bytes()
    elif args.gs.suffix.lower() == '.p2s':
        from parse_pcsx2_state import extract_zstd_entry
        with zipfile.ZipFile(args.gs) as archive:
            info = archive.getinfo('eeMemory.bin')
            ram = archive.read(info) if info.compress_type != 93 else None
        if ram is None:
            ram = extract_zstd_entry(args.gs, 'eeMemory.bin')
    else:
        raise ValueError('Supply --reference-ee with a bare GS freeze')
    if len(ram) != 32 * 1024 * 1024 or ram[0x810700:0x810702] != b'\x0b\0':
        raise ValueError('Expected original AREA11/sub0 reference state')
    weather_flags = struct.unpack_from('<I', ram, 0x8106C8)[0] & 0x0e000070
    if weather_flags != 0x10:
        raise ValueError('Unverified AREA11 weather branch')
    original = args.decomp_root / 'config/SCUS_971.12'
    if hashlib.sha256(original.read_bytes()).hexdigest() != 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a':
        raise ValueError('Expected original SCUS-97112 executable')
    elf = ElfImage(original)
    descriptor = bytearray(elf.read(0x255170, 9 * 16))
    lookup = elf.read(0x2342BC, 80 * 4)
    rows = elf.read(0x255200, 6 * 48)
    count, _, flags, kind = struct.unpack_from('<4I', descriptor, 128)
    if (count, flags, kind) != (0, 30, 2):
        raise ValueError('Unexpected original snowfall descriptor')
    # 001E67C0 assigns the default count before area-specific overrides.
    candidates = [elf.u32(at) & 65535 for at in range(0x1E67C0, 0x1E6824, 4)
                  if elf.u32(at) >> 16 == 0x2403]
    if candidates != [20]:
        raise ValueError('Unexpected original AREA11 particle count assignment')
    count = candidates[0]
    struct.pack_into('<I', descriptor, 128, count)
    if args.reference_vu:
        vu = args.reference_vu.read_bytes()
        if lookup != b''.join(vu[i * 16:i * 16 + 4] for i in range(80)):
            raise ValueError('Original VIF scalar lookup differs from captured VU')
    key = struct.unpack_from('<Q', descriptor, 112)[0] & native.TEX0_KEY_MASK
    texture = native.tex0_fields(key)
    texture['key'] = key
    if texture['psm'] not in (0x13, 0x14):
        raise ValueError('Unsupported original snow texture format')
    entries, texels = native.build_texture_blob(None, [texture], p2s=args.gs)
    entry = entries[0]
    if not 1 < entry['w'] <= 256 or not 1 < entry['h'] <= 256:
        raise ValueError('Unexpected snow texture dimensions')
    args.out.mkdir(parents=True, exist_ok=True)
    payload = struct.pack('<4s4I', b'EMSN', 1, 9, 80, 6) + descriptor + lookup + rows
    (args.out / 'snow.emsn').write_bytes(payload)
    texture_payload = struct.pack('<4s3I', b'EMTX', 1, entry['w'], entry['h']) + texels
    (args.out / 'snow.emtx').write_bytes(texture_payload)
    manifest = args.out / 'scene.txt'
    if manifest.exists():
        lines = manifest.read_text().splitlines()
        lines = [line for line in lines if not line.startswith('weather ')]
        lines.append(f'weather {weather_flags:#x} snow.emsn snow.emtx')
        manifest.write_text('\n'.join(lines) + '\n')
    report = dict(descriptor_address=0x255170, lookup_address=0x2342BC,
                  rows_address=0x255200, particles_per_tile=count, tile_count=108,
                  flags=flags, kind=kind, weather_flags=weather_flags, texture=texture,
                  parameters_sha256=hashlib.sha256(payload).hexdigest(),
                  texture_sha256=hashlib.sha256(texture_payload).hexdigest(),
                  lookup_compared_to_vu=bool(args.reference_vu))
    output = ROOT / 'build/weather_reference'
    output.mkdir(parents=True, exist_ok=True)
    (output / 'export.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Exported original snow: {108 * count} particles, {entry["w"]}x{entry["h"]} texture')


if __name__ == '__main__':
    main()
