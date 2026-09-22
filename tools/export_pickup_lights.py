#!/usr/bin/env python3
"""Export AREA11's original pickup child meshes and explicit owner bindings.

00219550 creates model73 through 001C5570(parent,color,model,1). Its
001C5680 child draws through 001CABA0: an unlit, additive mesh, never a
billboard. Placement and persistence belong to the original deferred item
record. Inputs and generated assets remain local and ignored.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import math
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
BEGIN = '# BEGIN ORIGINAL PICKUP LIGHTS'
END = '# END ORIGINAL PICKUP LIGHTS'


def load_tool(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def pickup_records(read) -> list[dict]:
    """001B6910/001B6660: area11, sub0, null-ended groups of 44-byte rows."""
    u32 = lambda address: struct.unpack('<I', read(address, 4))[0]
    sub_table = u32(0x24D820 + 11 * 4)
    groups = u32(sub_table)
    records = []
    for group in range(64):
        address = u32(groups + group * 4)
        if not address:
            break
        for index in range(256):
            at = address + index * 44
            row = read(at, 44)
            condition = struct.unpack_from('<h', row)[0]
            if condition == -1:
                break
            behavior = struct.unpack_from('<I', row, 40)[0]
            if behavior != 0x219550:
                continue
            if condition != 1 or row[4] != 0x84 or row[8] != 0x72:
                raise ValueError('Unsupported original pickup condition or model')
            position = struct.unpack_from('<3f', row, 16)
            rotation = struct.unpack_from('<3f', row, 28)
            if (not row[2] or rotation[0] or rotation[2] or
                    not all(math.isfinite(v) for v in (*position, *rotation))):
                raise ValueError('Unsupported original pickup transform or UID')
            records.append(dict(address=at, uid=(11 << 8) | row[2],
                                position=position, yaw=rotation[1]))
        else:
            raise ValueError('Unterminated pickup records')
    else:
        raise ValueError('Unterminated pickup groups')
    if not records or len({r['uid'] for r in records}) != len(records):
        raise ValueError('Missing or duplicate original pickup owners')
    return records


def write_bindings(path: Path, records: list[dict], model: str,
                   color: tuple[float, ...]) -> None:
    lines = path.read_text().splitlines()
    if BEGIN in lines or END in lines:
        if lines.count(BEGIN) != 1 or lines.count(END) != 1:
            raise ValueError('Invalid generated light block')
        start, end = lines.index(BEGIN), lines.index(END)
        if end < start:
            raise ValueError('Reversed generated light block')
        del lines[start:end + 1]
    for rec in records:
        matches = [line.split() for line in lines
                   if line.startswith('pickup ') and
                   int(line.split()[6], 0) == rec['uid']]
        if len(matches) != 1:
            raise ValueError(f"Missing native pickup owner {rec['uid']:#x}")
        values = tuple(map(float, matches[0][2:6]))
        if any(abs(a - b) > 0.0001 for a, b in
               zip(values, (*rec['position'], rec['yaw']))):
            raise ValueError('Native pickup transform differs from original record')
    fields = ' '.join(format(v, '.9g') for v in color)
    lines += [BEGIN, '# 00219550 child: model73 / 001C5680 / GS additive RGB.']
    lines += [f"pickup_light {r['uid']:#06x} {model} {fields}" for r in records]
    lines += [END]
    path.write_text('\n'.join(lines) + '\n')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp-root', type=Path, default=ROOT.parent/'Extermination')
    parser.add_argument('--gs', type=Path)
    parser.add_argument('--scene', type=Path, default=ROOT/'assets/scene_snow')
    args = parser.parse_args()
    decomp = args.decomp_root.resolve()
    sys.path.insert(0, str(decomp/'tools'))
    props = load_tool(decomp/'tools/export_props.py', '_pickup_light_props')
    audio = load_tool(decomp/'tools/audio_export.py', '_pickup_light_elf')
    elf = audio.ElfImage(decomp/'config/SCUS_971.12')
    overlay = (decomp/'extract/OVERLAY/AREA11.BIN').read_bytes()
    if len(overlay) != 0x7800 or overlay[:4] != b'MWo3':
        raise ValueError('Expected original AREA11 overlay')
    arena = struct.unpack_from('<I', overlay, 8)[0]
    if arena != 0x823500:
        raise ValueError('Unexpected original overlay arena')

    def read(address, count):
        if arena <= address and address + count <= arena + len(overlay):
            return overlay[address - arena:address - arena + count]
        return elf.read(address, count)

    records = pickup_records(read)
    # The color's nonzero components are materialized by LUI in the
    # original owner initializer; read their literal operands from ELF.
    def lui_float(address):
        instruction = elf.u32(address)
        if instruction >> 26 != 15:
            raise ValueError('Unexpected color initialization instruction')
        return struct.unpack('<f', struct.pack('<I', (instruction & 0xFFFF) << 16))[0]

    color = (0.0, lui_float(0x219678), 0.0, lui_float(0x21968C))
    model_instruction = elf.u32(0x2196A0)
    if model_instruction >> 26 != 9 or (model_instruction >> 21) & 31:
        raise ValueError('Unexpected child model initialization')
    model_id = model_instruction & 0xFFFF
    if model_id != 0x73 or color != (0.0, 1.0, 0.0, 0.25):
        raise ValueError('Unverified pickup child variant')
    library = (decomp/'extract/chunk27/f01_id37.bin').read_bytes()
    offset = props.table_entry_offset(library, 0, model_id)
    if any(slot != 0 for _, corners, _ in props.model_tris_slots(library, offset)
           for _, _, _, slot in corners):
        raise ValueError('Unexpected articulated pickup light')
    sections, textures = props.build_blob_mesh(library, offset)
    # Lighting mode1 zeroes normal rows; no guessed normal lighting is
    # baked into these vertices. The additive draw uses only texture/tint.
    sections[0][1][:] = [(1.0, 1.0, 1.0)] * len(sections[0][0])
    gs = args.gs or decomp/'build/startup-reference/opening_gs.bin'
    entries, texels = props.lvl.build_texture_blob(None, textures, p2s=gs)
    if len(entries) != 1 or entries[0]['w'] != 16 or entries[0]['h'] != 16:
        raise ValueError('Original pickup light texture was not resolved')
    relative = f'props/item_{model_id:02x}.emdl'
    output = args.scene/relative
    output.parent.mkdir(parents=True, exist_ok=True)
    props.en.write_emdl(output, sections, [], [-1],
                        [[props.en.mat_identity()]], 30.0, entries, texels, flags=1)
    write_bindings(args.scene/'scene.txt', records, relative, color)
    print(json.dumps(dict(model=model_id, library_offset=offset,
                          vertices=len(sections[0][0]),
                          triangles=len(sections[0][2])//3,
                          color=color, owners=records), indent=2))


if __name__ == '__main__':
    main()
