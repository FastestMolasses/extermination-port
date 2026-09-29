#!/usr/bin/env python3
"""Export the AREA22 sound banks and the registry (docs/AREA22_ASSETS.md).

export_area01_sfx.bindings_from_captures and export_sfx_registry's resolver
and EMSR v2 writer, imported unchanged (the AREA01 / AREA00 / AREA02 /
AREA04 lanes' method): which banks the area uses is read from the sound
driver's tables in every capture (D_00281D50 handle table, D_0027C6C0
handle records), each bound header must equal its container row except the
48 per-track channel bytes +0x0A, and a slot whose header lacks the SShd
magic is refused (00119EA0 tests the magic word, +0xC, before it uses a
bank: its NEARMISS C). From export_area02_sfx it reuses block_bytes, and
from export_area04_sfx refusal_reasons (the reason of each refused slot,
read from its handle record in every capture).

AREA22's own step: the level block has no nested descriptor, so the area
container is upload section 0 of the flat top descriptor (INDEX.IDX sector
26, block offset 0 .. the section size), assembled from the extracted
chunk26 files as contiguous slices of the block; none of it is resident
(the resident region starts at +0xD9800). The descriptor's file list
describes only the resident region (its offsets are resident-relative, see
export_area22_common.build_load_map_flat), so no relocation id owns these
bytes; the extracted files that hold them are named in banks.json only as
byte sources. One registry, scope (0x16, 0):
the area-paged ids 0x3E8..0x5DB and 0x7D0..0x9C3 (001FB9F0's remap tables
decide present or absent per id).

Output (ignored assets/area22/, disc-derived, never committed):
  sub0/sfx/area22_banks.bin            the SShd container (+0 .. total)
  sub0/sfx/banks.json                  bindings and verification summary
  sub0/sfx/sfx_registry.emsr (+.json)  EMSR v2 for scope (0x16, 0)

Not verified here: SPU residency of the samples (no AREA22 SPU capture).

Usage (port root): python3 tools/export_area22_sfx.py
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area22_common as A22  # noqa: E402
import export_area04_sfx as E04  # noqa: E402  (refusal_reasons; sets AREA04 state on import)

E02 = E04.E02                          # export_area02_sfx (block_bytes)
C = A22.configure()                    # AREA22 from here on
S = E04.S                              # export_area01_sfx
X = S.X                                # export_sfx_registry

CONTAINER = 'area22_sub0_banks.bin'


def area_container(cap):
    """(label, container bytes, parsed, [file names], section 0): upload
    section 0 of the flat descriptor, which must start at block offset 0,
    lie before the resident region, and hold an SShd container that ends
    inside it."""
    _lmap, info = C.build_load_map(cap)
    chunk, files, sections = A22.top_files()
    if chunk != info['chunk'] or sections != info['sections']:
        raise SystemExit(f'{cap.name}: descriptor differs from the load map\'s')
    first = sections[0]
    if first[0] != 0 or first[1] > info['resident']:
        raise SystemExit(f'section 0 at {first[0]:#x} + {first[1]:#x}: not before the resident region')
    data = E02.block_bytes(files, 0, first[1])
    parsed = X.A.parse_container(data)
    if parsed is None or parsed['total'] > first[1]:
        raise SystemExit(f'{chunk}: section 0 is not an SShd container')
    names = [p.name for p, off, span in files if off < parsed['total']]
    return f'{chunk} +0x0', data[:parsed['total']], parsed, names, first


def export(out):
    A22.configure()
    caps = C.captures()
    gdata = (C.DECOMP / X.GLOBAL_CONTAINER).read_bytes()
    gparsed = X.A.parse_container(gdata)
    label, data, parsed, names, first = area_container(caps[0])
    for cap in caps[1:]:
        if area_container(cap)[1] != data:
            raise SystemExit(f'{cap.name}: a different area container')
    containers = {X.GLOBAL_CONTAINER: (gdata, gparsed), CONTAINER: (data, parsed)}
    bound, refused, report = S.bindings_from_captures(caps, containers)
    by_name = {X.GLOBAL_CONTAINER: X.containers_by_type(X.GLOBAL_CONTAINER, gdata),
               CONTAINER: X.containers_by_type(CONTAINER, data)}
    groups = {}
    for (g, i), (key, _h, _hd, _sp) in sorted(bound.items()):
        bank = next(b for b in by_name[key[0]][g] if b.row == key[1])
        lst = groups.setdefault(g, [])
        if len(lst) != i:
            raise SystemExit(f'group {g}: slot {i} bound without slot {len(lst)}')
        lst.append(bank)
    if refused.get((3, 0)) is None:
        raise SystemExit('group 3 slot 0 is not the refused header it is in the earlier areas')
    why = {k: f'{v} in AREA22' for k, v in E04.refusal_reasons(caps, refused).items()}
    binding = {(C.AREA, 0): dict(groups=groups, binding='AREA22 captures (D_00281D50/D_0027C6C0)', refused=why)}
    ids = list(range(0x3E8, 0x5DC)) + list(range(0x7D0, 0x9C4))
    saved = X.area_bindings, X.scene_ids, X.census_report
    X.area_bindings = lambda _elf: binding
    X.scene_ids = lambda census=True: {(C.AREA, 0): ids}
    X.census_report = lambda entries: dict(scope=[C.AREA, 0], note='no first-level census for AREA22')
    sfx = out / 'sub0/sfx'
    try:
        reg = X.export(sfx)
    finally:
        X.area_bindings, X.scene_ids, X.census_report = saved
    counts, reasons = {}, {}
    for e in reg['entries']:
        counts[e['state']] = counts.get(e['state'], 0) + 1
        if e['state'] == X.STATE_UNSUPPORTED:
            reasons[e['reason']] = reasons.get(e['reason'], 0) + 1
    (sfx / 'area22_banks.bin').write_bytes(data)
    summary = dict(
        sub=0, captures=[c.name for c in caps], container=label, container_byte_sources=names,
        section0=[hex(first[0]), hex(first[1])], container_bytes=len(data), container_sha256=C.sha(data),
        banks=[dict(row=r, hd=hex(b['hd']), type=b['type'], body=hex(b['body_base']), body_bytes=b['body_size'])
               for r, b in enumerate(parsed['banks'])],
        bindings={f'{g}.{i}': f'{"global" if k[0] == X.GLOBAL_CONTAINER else "area"}#row{k[1]}'
                  for (g, i), (k, _h, _a, _s) in sorted(bound.items())},
        refused={f'{g}.{i}': h for (g, i), h in refused.items()},
        refused_why={f'{g}.{i}': w for (g, i), w in sorted(why.items())}, ids=len(ids),
        states=dict(audible=counts.get(1, 0), absent=counts.get(2, 0), unsupported=counts.get(3, 0)),
        unsupported_reasons=reasons, samples=len(reg['samples']), registry_sha256=reg['registry_sha256'],
        slots=report, not_verified='SPU residency of the samples (no AREA22 SPU capture)')
    (sfx / 'banks.json').write_text(json.dumps(summary, indent=1) + '\n')
    print(f"container {label} ({len(data)} bytes over {names}, {len(parsed['banks'])} banks); "
          f"bindings {summary['bindings']}, refused {summary['refused']} in {len(caps)} captures; registry "
          f"{len(ids)} ids: {summary['states']} {reasons}, {len(reg['samples'])} samples")
    return summary


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=A22.OUT)
    args = ap.parse_args(argv)
    out = args.out.resolve()
    X.Elf()                                   # pins the boot ELF
    A22.read_overlay()
    export(out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
