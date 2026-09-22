#!/usr/bin/env python3
"""Export the original AREA11 auxiliary lamp list and color presets.

Generated assets remain local and ignored. This table is selected by
001F6D60/001F6E40 independently of the nearby steam actor and primary lamps.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp-root', type=Path, default=ROOT.parent/'Extermination')
    parser.add_argument('--out', type=Path, default=ROOT/'assets/scene_snow')
    parser.add_argument('--reference-ee', type=Path)
    args = parser.parse_args()
    sys.path.insert(0, str(args.decomp_root/'tools'))
    from audio_export import ElfImage
    path = args.decomp_root/'config/SCUS_971.12'
    sha = hashlib.sha256(path.read_bytes()).hexdigest()
    if sha != 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a':
        raise ValueError('Expected original SCUS-97112 executable')
    elf = ElfImage(path)
    table, records, provenance = 0x25d5a0, [], []
    for i in range(33):
        record = elf.read(table+i*40,40)
        if struct.unpack_from('<h',record)[0] < 0: break
        if i == 32: raise ValueError('Original lamp list exceeds pool')
        preset = struct.unpack_from('<h',record,4)[0]
        if not 0 <= preset < 3: raise ValueError('Unverified light preset')
        position = record[12:24]+struct.pack('<f',1)
        color = elf.read(0x26eb70+preset*16,16)
        records.append(struct.pack('<I',1)+position+color)
        provenance.append({'record':hex(table+i*40),'preset':preset,
                           'color_address':hex(0x26eb70+preset*16),
                           'position':struct.unpack('<4f',position),
                           'unscaled_color':struct.unpack('<4f',color)})
    if len(records) != 1: raise ValueError('Expected one original AREA11 auxiliary lamp')
    if args.reference_ee:
        ram = args.reference_ee.read_bytes()
        context = struct.unpack_from('<I',ram,0x275670)[0]
        position = records[0][4:20]
        color = struct.unpack('<4f',records[0][20:36])
        if ram[context+0x230:context+0x240] != position:
            raise ValueError('Captured original light position differs')
        if ram[context+0x240:context+0x250] != struct.pack('<4f',*(v*128 for v in color)):
            raise ValueError('Captured original light color/intensity differs')
    args.out.mkdir(parents=True,exist_ok=True)
    payload = struct.pack('<4sIII',b'EMLP',1,0x0b00,len(records))+b''.join(records)
    (args.out/'point_lights.emlp').write_bytes(payload)
    manifest = args.out/'scene.txt'
    if manifest.exists():
        lines = [line for line in manifest.read_text().splitlines()
                 if not line.startswith('pointlights ')]
        # Remove the obsolete claim that the steam actor owns a guessed light.
        begin = next((i for i,line in enumerate(lines) if line.startswith('# --- steam / FX emitter')),None)
        end = next((i for i,line in enumerate(lines) if line.startswith('# --- end steam / FX emitter')),None)
        if begin is not None and end is not None and begin < end:
            steam = [line for line in lines[begin:end+1] if line.startswith('steam ')]
            lines[begin:end+1] = ['# Legacy steam audio/FX, separate from the original auxiliary lamp.']+steam
        lines.append('pointlights point_lights.emlp')
        manifest.write_text('\n'.join(lines)+'\n')
    report = {'original_elf_sha256':sha,'selector':'001F6D60',
              'registration':'001F6E40 -> 001F6640 -> 001D7FA0',
              'area_key':'0x0b00','records':provenance,
              'asset_sha256':hashlib.sha256(payload).hexdigest(),
              'radius':'none: 001D8340 clamps squared distance to1 before inverse-square fold',
              'animation':'001D7C30 type1 bounded two-angle random walk; intensity/color constant'}
    (args.out/'point_lights_source.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__': main()
