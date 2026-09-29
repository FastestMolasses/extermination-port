#!/usr/bin/env python3
"""Export the AREA04 sound banks and the registry of the captured sub
(docs/AREA04_ASSETS.md).

export_area01_sfx.bindings_from_captures and export_sfx_registry's resolver
and EMSR v2 writer, imported unchanged (the AREA01 / AREA00 / AREA02 lanes'
method): which banks the area uses is read from the sound driver's tables in
every capture (D_00281D50 handle table, D_0027C6C0 handle records), each
bound header must equal its container row except the 48 per-track channel
bytes +0x0A, and a slot whose header lacks the SShd magic is refused
(00119EA0 tests the magic word, +0xC, before it uses a bank: its NEARMISS
C), as group 3's in the earlier areas. The refusal reason is read from the
handle record (refusal_reasons): group 3 slot 0's header has no magic,
group 4 slot 1's handle record is freed (use word 0, header pointer 0). From export_area02_sfx it reuses, by import,
nested_block / block_bytes / area_container: the area container is upload
section 0 of the sub's nested descriptor (block offset 0 .. the section
size), assembled from the descriptor's own file list, none of it resident.

AREA04's own step: only sub 0 is captured, so one container and one
registry, scope (4, 0): the area-paged ids 0x3E8..0x5DB and 0x7D0..0x9C3
(001FB9F0's remap tables decide present or absent per id).

Output (ignored assets/area04/, disc-derived, never committed):
  sub0/sfx/area04_banks.bin            the sub's SShd container (+0 .. total)
  sub0/sfx/banks.json                  bindings and verification summary
  sub0/sfx/sfx_registry.emsr (+.json)  EMSR v2 for scope (4, 0)

Not verified here: SPU residency of the samples (no AREA04 SPU capture).

Usage (port root): python3 tools/export_area04_sfx.py
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area04_common as A4  # noqa: E402
import export_area02_sfx as E02  # noqa: E402  (sets AREA02 state on import)

C = A4.configure(0)                    # AREA04 from here on
S = E02.S                              # export_area01_sfx
X = S.X                                # export_sfx_registry


def container_name(sub):
    return f'area04_sub{sub}_banks.bin'


def refusal_reasons(caps, refused):
    """{(group, slot): why} for the refused slots, read from the sound
    driver's handle record D_0027C6C0 + 12 * handle (use word, header
    pointer, SPU address) in every capture: a record with use word 0 and
    header pointer 0 is a freed handle; otherwise the header lacks the SShd
    magic at +0xC. Every capture must give the same reason."""
    out = {}
    for (g, i) in sorted(refused):
        seen = set()
        for cap in caps:
            h = C.u32(cap.ram, S.D_00281D50 + 4 * (g * S.SLOTS + i))
            use, header = C.u32(cap.ram, S.D_0027C6C0 + 12 * h), C.u32(cap.ram, S.D_0027C6C0 + 12 * h + 4)
            if use == 0 and header == 0:
                seen.add(f'group {g} bank {i} = handle {h}, a freed handle record (use word 0, header pointer 0)')
            else:
                seen.add(f'group {g} bank {i} = handle {h}, header {header:#x} without SShd magic '
                         f'(use word {use})')
        if len(seen) != 1:
            raise SystemExit(f'group {g} slot {i}: the captures disagree on why it is refused: {sorted(seen)}')
        out[(g, i)] = seen.pop()
    return out


def export_sub(sub, out):
    A4.configure(sub)
    caps = C.captures()
    gdata = (C.DECOMP / X.GLOBAL_CONTAINER).read_bytes()
    gparsed = X.A.parse_container(gdata)
    label, data, parsed, names, first = E02.area_container(caps[0])
    for cap in caps[1:]:
        if E02.area_container(cap)[1] != data:
            raise SystemExit(f'{cap.name}: a different area container')
    name = container_name(sub)
    containers = {X.GLOBAL_CONTAINER: (gdata, gparsed), name: (data, parsed)}
    bound, refused, report = S.bindings_from_captures(caps, containers)
    by_name = {X.GLOBAL_CONTAINER: X.containers_by_type(X.GLOBAL_CONTAINER, gdata),
               name: X.containers_by_type(name, data)}
    groups = {}
    for (g, i), (key, _h, _hd, _sp) in sorted(bound.items()):
        bank = next(b for b in by_name[key[0]][g] if b.row == key[1])
        lst = groups.setdefault(g, [])
        if len(lst) != i:
            raise SystemExit(f'group {g}: slot {i} bound without slot {len(lst)}')
        lst.append(bank)
    if refused.get((3, 0)) is None:
        raise SystemExit('group 3 slot 0 is not the refused header it is in the earlier areas')
    why = {k: f'{v} in AREA04 sub {sub}' for k, v in refusal_reasons(caps, refused).items()}
    binding = {(C.AREA, sub): dict(groups=groups, binding=f'AREA04 sub {sub} captures (D_00281D50/D_0027C6C0)',
                                   refused=why)}
    ids = list(range(0x3E8, 0x5DC)) + list(range(0x7D0, 0x9C4))
    saved = X.area_bindings, X.scene_ids, X.census_report
    X.area_bindings = lambda _elf: binding
    X.scene_ids = lambda census=True: {(C.AREA, sub): ids}
    X.census_report = lambda entries: dict(scope=[C.AREA, sub], note='no first-level census for AREA04')
    sfx = out / f'sub{sub}/sfx'
    try:
        reg = X.export(sfx)
    finally:
        X.area_bindings, X.scene_ids, X.census_report = saved
    counts, reasons = {}, {}
    for e in reg['entries']:
        counts[e['state']] = counts.get(e['state'], 0) + 1
        if e['state'] == X.STATE_UNSUPPORTED:
            reasons[e['reason']] = reasons.get(e['reason'], 0) + 1
    (sfx / 'area04_banks.bin').write_bytes(data)
    summary = dict(
        sub=sub, captures=[c.name for c in caps], container=label, container_files=names,
        section0=[hex(first[0]), hex(first[1])], container_bytes=len(data), container_sha256=C.sha(data),
        banks=[dict(row=r, hd=hex(b['hd']), type=b['type'], body=hex(b['body_base']), body_bytes=b['body_size'])
               for r, b in enumerate(parsed['banks'])],
        bindings={f'{g}.{i}': f'{"global" if k[0] == X.GLOBAL_CONTAINER else "area"}#row{k[1]}'
                  for (g, i), (k, _h, _a, _s) in sorted(bound.items())},
        refused={f'{g}.{i}': h for (g, i), h in refused.items()},
        refused_why={f'{g}.{i}': w for (g, i), w in sorted(why.items())}, ids=len(ids),
        states=dict(audible=counts.get(1, 0), absent=counts.get(2, 0), unsupported=counts.get(3, 0)),
        unsupported_reasons=reasons, samples=len(reg['samples']), registry_sha256=reg['registry_sha256'],
        slots=report, not_verified='SPU residency of the samples (no AREA04 SPU capture)')
    (sfx / 'banks.json').write_text(json.dumps(summary, indent=1) + '\n')
    print(f"sub {sub}: container {label} ({len(data)} bytes over {names}, {len(parsed['banks'])} banks); "
          f"bindings {summary['bindings']}, refused {summary['refused']} in {len(caps)} captures; registry "
          f"{len(ids)} ids: {summary['states']} {reasons}, {len(reg['samples'])} samples")
    return summary


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=A4.OUT)
    args = ap.parse_args(argv)
    out = args.out.resolve()
    X.Elf()                                   # pins the boot ELF
    A4.read_overlay()
    for sub in A4.SUBS:
        export_sub(sub, out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
