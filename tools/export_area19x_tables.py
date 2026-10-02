#!/usr/bin/env python3
"""Export AREA19 sub 0's tables over the a19b captures (the second AREA19
load, entry 10): the roster, the spawn table, the door destinations, the
overlay scripts and data section, the message records + area bank, the
world model bank, and the creature resources the 0x82A590 group's 0012E3A0
creature binds in a19b_02 (relocation ids 0x70 and 0x71)
(docs/AREA19X_ASSETS.md).

export_area13_tables.export_target (the AREA13 lane's AREA19 path) is
imported and run UNCHANGED; export_area19x_common re-points it at the three
captures and the scratch tree and adds [43]'s model rule (model 0x25 after
counter 0x1D bit 0, +0x0D unchanged).

The creature (export_creature): 0012E560 (src/func_0012E560.c, NEARMISS;
the readable C) binds the model through 001B10B0(self, 0x70, 0x71) when
+0x0D has bits 0 and 0x80 (src/func_001B10B0.c, byte-identical:
+0x44 = D_0028A490[0x70] via 001CA6E0, +0x40 = D_0028A490[0x71], +0x0C =
001C6150(model)). Each id's resident bytes are written as an EMSC address
window (em_script_image_load's format) from the load map (the disc
files), and checked equal to every capture's RAM; id 0x70 must parse as a
block model (export_world_models.model_record) whose bone count is the
node's +0x0C, and every live 0012E3A0 node with +0x0D bits 0x81 must hold
those words.

Output (ignored build/area19x/assets/tree/, disc-derived, never committed):
  sub0/roster.emro, sub0/message_data.emmd, sub0/world_models.emwm (+ .json)
  spawn_table.emsp, door_destinations.emsp, scripts.emsc, overlay_data.emsc,
  tables.json; sub0/creature/id70.emsc, id71.emsc, creature.json

Usage (port root, macOS arm64, pure Python): python3 tools/export_area19x_tables.py
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area19x_common as X19  # noqa: E402  (first: re-points the AREA13 lane's AREA19 target)
import export_area13_tables as TB  # noqa: E402
import export_world_models as W  # noqa: E402  (model_record)

A13, C = X19.A13, X19.C
CREATURE = 0x12E3A0                    # the behaviour (its spawn 0012E560)
MODEL_ID, SECOND_ID = 0x70, 0x71       # 001B10B0(self, 0x70, 0x71): +0x0D bits 0 and 0x80
D_0028A490 = A13.D_0028A490


def creature_nodes(ram):
    """[(slot, node)] of live 0012E3A0 nodes whose +0x0D has bits 0 and 0x80."""
    return [(s, a) for s, a in TB.E02._POOL_NODES(ram)
            if C.u32(ram, a + 0x10) == CREATURE and ram[a + 0x0D] & 0x81 == 0x81]


def creature_problems(cap, record):
    """Each creature_nodes node of `cap` against `record` (the model's bone
    count) and the capture's relocation words."""
    out = []
    ram = cap.ram
    model, second = C.u32(ram, D_0028A490 + 4 * MODEL_ID), C.u32(ram, D_0028A490 + 4 * SECOND_ID)
    for slot, a in creature_nodes(ram):
        if C.u32(ram, a + 0x44) != model:
            out.append(f'{cap.name} slot {slot}: +0x44 {C.u32(ram, a + 0x44):#x} != D_0028A490[0x70] {model:#x}')
        if C.u32(ram, a + 0x40) != second:
            out.append(f'{cap.name} slot {slot}: +0x40 {C.u32(ram, a + 0x40):#x} != D_0028A490[0x71] {second:#x}')
        if ram[a + 0x0C] != record['bones'] or not all(C.u32(ram, a + 0x110 + 4 * k) for k in range(ram[a + 0x0C])):
            out.append(f'{cap.name} slot {slot}: bone count {ram[a + 0x0C]} / slots (model {record["bones"]})')
    return out


def export_creature(caps, out):
    """Write sub0/creature/ (id70.emsc, id71.emsc, creature.json); returns
    the report."""
    lmap, info = C.build_load_map(caps[0])
    image = C.LoadedImage(lmap)
    spans = {name: (address, size) for name, address, size in info['ids']}
    folder = out / 'creature'
    folder.mkdir(parents=True, exist_ok=True)
    report = dict(behaviour=hex(CREATURE), binding='001B10B0(self, 0x70, 0x71) from 0012E560 (+0x0D bits 0x81)',
                  ids={}, captures=[c.name for c in caps], nodes={})
    record = None
    for ident in (MODEL_ID, SECOND_ID):
        address, size = spans[f'id{ident:02x}']
        data = bytes(image.read(address, size))
        for cap in caps:
            if C.u32(cap.ram, D_0028A490 + 4 * ident) != address:
                raise SystemExit(f'{cap.name}: D_0028A490[{ident:#x}] is not the load map address {address:#x}')
            if cap.ram[address:address + size] != data:
                raise SystemExit(f'{cap.name}: id {ident:#x} differs from RAM')
        row = dict(address=hex(address), bytes=size, sha256=C.sha(data), source=image.locate(address)[4])
        if ident == MODEL_ID:
            blocks, qwc, bones, skel, nbytes, radius = W.model_record(data, 0, ident)
            if nbytes > size:
                raise SystemExit(f'id {ident:#x}: the model ({nbytes} bytes) overruns the id ({size})')
            record = dict(blocks=blocks, qwc=qwc, bones=bones, skeleton=hex(skel), bytes=nbytes,
                          radius_bits=hex(radius))
            row['model'] = record
        (folder / f'id{ident:02x}.emsc').write_bytes(C.emsc(address, data))
        report['ids'][f'0x{ident:02x}'] = row
    problems = [p for cap in caps for p in creature_problems(cap, record)]
    if problems:
        raise SystemExit('; '.join(problems))
    report['nodes'] = {cap.name: [hex(a) for _s, a in creature_nodes(cap.ram)] for cap in caps}
    if not any(report['nodes'].values()):
        raise SystemExit('no capture holds a 0012E3A0 node bound to ids 0x70 / 0x71')
    (folder / 'creature.json').write_text(json.dumps(report, indent=1) + '\n')
    return report


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=X19.TREE)
    args = ap.parse_args(argv)
    X19.install()
    out = args.out.resolve()
    report = TB.export_target('area19', out)
    if tuple(report['captures']) != X19.CAPTURE_NAMES:
        raise SystemExit(f'AREA19 captures {report["captures"]}, not the pinned {X19.CAPTURE_NAMES}')
    X19.install()
    crt = export_creature(C.captures(), out / 'sub0')
    print(f"creature: ids {sorted(crt['ids'])}, model {crt['ids']['0x70']['model']['bones']} bones / "
          f"{crt['ids']['0x70']['model']['blocks']} blocks; nodes {crt['nodes']}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
