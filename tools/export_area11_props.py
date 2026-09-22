#!/usr/bin/env python3
"""Restore original AREA11 switch/elevator models and indicator bindings.

Original callbacks: switch00159210 uses per-area04 and global child75;
elevator00827B10 uses per-area0F and child10. Record8 is Roger, not a
battery terminal. This removes that fabricated console from the normal
scene and replaces the old grate/elevator descriptions with proven data.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import sys

from export_pickup_lights import load_tool

ROOT = Path(__file__).resolve().parents[1]


def original_placement(overlay: bytes, index: int, behavior: int, model: int):
    if len(overlay) != 0x7800 or overlay[:4] != b'MWo3':
        raise ValueError('Expected original SCUS-97112 AREA11 overlay')
    arena = struct.unpack_from('<I', overlay, 8)[0]
    if arena != 0x823500:
        raise ValueError('Unexpected original overlay arena')
    at = 0x82A3C0 - arena + index * 40
    fields = struct.unpack_from('<HBBHHHH', overlay, at)
    actual_behavior = struct.unpack_from('<I', overlay, at + 36)[0]
    position = struct.unpack_from('<3f', overlay, at + 12)
    rotation = struct.unpack_from('<3f', overlay, at + 24)
    if (actual_behavior != behavior or fields[3] != model or
            rotation[0] != 0 or rotation[2] != 0):
        raise ValueError('Unexpected original prop placement')
    return dict(record=arena + at, behavior=behavior, model=model,
                position=position, yaw=rotation[1])


def exact_static_mesh(props, data: bytes, offset: int):
    """Preserve the original normal attributes; do not bake guessed light."""
    positions, normals, indices, bones, uvs, texture_ids = [], [], [], [], [], []
    textures = []
    texture_id = props.make_tex_of(textures, {})
    welded = {}
    for tex0, corners, parity in props.model_tris_slots(data, offset):
        texture = texture_id(tex0)
        triangle = []
        for position, attribute, uv, bone in corners:
            if bone:
                raise ValueError('Unexpected articulated prop geometry')
            key = (tuple(position), tuple(attribute[:3]), tuple(uv), texture)
            vertex = welded.get(key)
            if vertex is None:
                vertex = len(positions)
                welded[key] = vertex
                positions.append(key[0])
                normals.append(key[1])
                bones.append(0)
                uvs.append(key[2])
                texture_ids.append(texture)
            triangle.append(vertex)
        a, b, c = triangle
        indices.extend((c, b, a) if parity else (c, a, b))
    if not positions:
        raise ValueError('Original prop has no triangles')
    return [(positions, normals, indices, bones, uvs, texture_ids)], textures


def replace_block(lines, begin_prefix, directive, replacement):
    ends = [i for i, line in enumerate(lines) if directive(line)]
    if not ends:
        return
    if len(ends) != 1:
        raise ValueError('Ambiguous old prop binding')
    end = ends[0]
    starts = [i for i, line in enumerate(lines[:end]) if line.startswith(begin_prefix)]
    start = starts[-1] if starts else end
    lines[start:end + 1] = replacement


def update_manifest(path: Path, panel: dict, elevator: dict):
    lines = path.read_text().splitlines()
    pose = lambda row: ' '.join(format(x, '.9g') for x in (*row['position'], row['yaw']))
    replace_block(lines, '# Record 18 is the GATED GRATE',
                  lambda line: line.startswith('grate '), [
        '# Original switch00159210, per-area model04, placement record18.',
        '# Legacy manifest verb: static panel, without a guessed gate hull/slide.',
        'grate props/area_item_04.emdl ' + pose(panel)])
    replace_block(lines, '# INTERNAL ELEVATOR-CONTROL',
                  lambda line: line.startswith('pickup ') and
                  'props/area_internal_terminal.emdl' in line.split(), [])
    replace_block(lines, '# OUTSIDE BATTERY TERMINAL —',
                  lambda line: line.startswith('pickup ') and
                  'props/area_battery_terminal.emdl' in line.split(), [
        '# Record8 is the ordinary Roger actor8237E0 / resource47.',
        '# Its former static battery-console binding was a misidentification.'])
    replace_block(lines, '# OUTSIDE BATTERY TERMINAL examine',
                  lambda line: line.startswith('examine ') and
                  'battery_terminal' in line.split(), [
        '# Roger\'s original dialogue/actor behavior is not a battery-insert console.'])
    replace_block(lines, '# --- elevator (AREA11 opening',
                  lambda line: line.startswith('elevator '), [
        '# Elevator owner827B10: original per-area model0F, placement19.',
        '# Script helper828050 moves this actor and the player by +/-40 units.',
        'elevator props/area_elevator.emdl ' + pose(elevator)])
    begin, end = '# BEGIN ORIGINAL PROP INDICATORS', '# END ORIGINAL PROP INDICATORS'
    if begin in lines or end in lines:
        if lines.count(begin) != 1 or lines.count(end) != 1:
            raise ValueError('Invalid indicator block')
        first, last = lines.index(begin), lines.index(end)
        if first > last:
            raise ValueError('Reversed indicator block')
        del lines[first:last + 1]
    lines += [begin, 'prop_indicator panel props/item_75.emdl',
              'prop_indicator elevator props/area_indicator_10.emdl', end]
    path.write_text('\n'.join(lines) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp-root', type=Path, default=ROOT.parent/'Extermination')
    parser.add_argument('--gs', type=Path)
    parser.add_argument('--scene', type=Path, default=ROOT/'assets/scene_snow')
    args = parser.parse_args()
    decomp = args.decomp_root.resolve()
    sys.path.insert(0, str(decomp/'tools'))
    props = load_tool(decomp/'tools/export_props.py', '_area11_prop_export')
    overlay = (decomp/'extract/OVERLAY/AREA11.BIN').read_bytes()
    panel = original_placement(overlay, 18, 0x159210, 4)
    elevator = original_placement(overlay, 19, 0x827B10, 15)
    original_placement(overlay, 8, 0x8237E0, 0x47)  # ordinary Roger
    # Runtime D28A59C resolves to this directory in the concatenated pair.
    # Model0F spans the file boundary; loading f05 alone truncates it.
    area = ((decomp/'extract/chunk15/f05_id97.bin').read_bytes() +
            (decomp/'extract/chunk15/f06_id98.bin').read_bytes())
    library = (decomp/'extract/chunk27/f01_id37.bin').read_bytes()
    gs = args.gs or decomp/'build/startup-reference/opening_gs.bin'
    metadata = []
    for data, table, model, name in (
            (area, 0x5000, 4, 'area_item_04.emdl'),
            (area, 0x5000, 15, 'area_elevator.emdl'),
            (area, 0x5000, 16, 'area_indicator_10.emdl'),
            (library, 0, 0x75, 'item_75.emdl')):
        offset = props.table_entry_offset(data, table, model)
        sections, textures = exact_static_mesh(props, data, offset)
        entries, texels = props.lvl.build_texture_blob(None, textures, p2s=gs)
        if len(entries) != len(textures) or any(
                e['w'] != 1 << t['tw'] or e['h'] != 1 << t['th']
                for e, t in zip(entries, textures)):
            raise ValueError('Original prop texture was not resolved')
        output = args.scene/'props'/name
        output.parent.mkdir(parents=True, exist_ok=True)
        props.en.write_emdl(output, sections, [], [-1],
            [[props.en.mat_identity()]], 30.0, entries, texels, flags=0)
        metadata.append(dict(file=name, model=model, source_offset=offset,
                             vertices=len(sections[0][0]),
                             triangles=len(sections[0][2])//3))
    update_manifest(args.scene/'scene.txt', panel, elevator)
    print(json.dumps(dict(panel=panel, elevator=elevator, models=metadata), indent=2))


if __name__ == '__main__':
    main()
