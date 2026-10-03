#!/usr/bin/env python3
"""Export the AREA01 sub-0 sound banks and their registry (docs/AREA01_ASSETS.md).

Which banks the area uses is read from the captures, not from labels: the
sound driver's handle table D_00281D50 (group * 0x14 + index -> handle) and
the handle records D_0027C6C0 (12 bytes: in use, EE header address, SPU base).
In every AREA01 capture group 1 names handles 0..2 (the global container
extract/chunk00/f05_id05.bin, as in AREA11), group 2 handle 4 and group 4
handles 5 and 6, whose headers lie in RAM at the loader's nested cursor minus
0x19B0: the SShd container at the head of the nested block's section 0 (the
descriptor's upload-table entry 0, extract/chunk05.n0/f00_id44.bin
+0..+0x74C10). Each bound header must equal its container row byte for byte
except the 48 per-track channel bytes +0x0A the driver rewrites (the rule
tools/test_area11_sfx_reference.py applies to AREA11). Group 3 names handle 3
whose header has no SShd magic (00119EA0 refuses it), exactly as in AREA11.

The registry is export_sfx_registry.py's own resolver and EMSR v2 writer,
imported unchanged, with these (area, sub) = (1, 0) bindings and the area-paged
ids 0x3E8..0x5DB and 0x7D0..0x9C3 (001FB9F0's remap tables D_00264A70 /
D_00264AD0 decide present or absent per id).

Output (ignored assets/area01/sfx/, disc-derived, never committed):
  sfx_registry.emsr, sfx_registry.json   EMSR v2 for scope (1, 0)
  area01_banks.bin                       the SShd container bytes (+0..total)
  banks.json                             bindings and the verification summary

Not verified here: SPU residency of the samples (no AREA01 SPU capture).

Usage (port root): python3 tools/export_area01_sfx.py
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area01_common as C  # noqa: E402
import export_sfx_registry as X  # noqa: E402

D_00281D50, D_0027C6C0 = 0x281D50, 0x27C6C0
GROUPS, SLOTS = 6, 0x14
SSHD = 0x64685353


def area_container(caps):
    """(relative name, container bytes) from the nested descriptor's
    upload-table entry 0 (the section the loader streams to the sound
    driver), checked to be an SShd container."""
    lmap, info = C.build_load_map(caps[0])
    first = info['sections'][0]
    if first[0] != 0:
        raise SystemExit(f'nested section 0 at {first[0]:#x}, not the block start')
    # the block's first file carries section 0
    path = next(p for _a, p, _o, _s, l in lmap if l.startswith(info['nested'] + '/f00_'))
    data = path.read_bytes()[:first[1]]
    parsed = X.A.parse_container(data)
    if parsed is None:
        raise SystemExit(f'{path}: section 0 is not an SShd container')
    rel = str(path.relative_to(C.DECOMP))
    return rel, data[:parsed['total']], parsed


def bindings_from_captures(caps, containers):
    """{(group, index): (container name, row)} bound in every capture, with
    the header comparison. `containers` = {name: (data, parsed)}."""
    rows = {}
    for name, (data, parsed) in containers.items():
        for row, bank in enumerate(parsed['banks']):
            following = [b['hd'] for b in parsed['banks'] if b['hd'] > bank['hd']]
            end = min(following + [parsed['img_off']])
            state = bank['hd'] + C.u32(data, bank['hd'] + 0x20)
            mutable = {state + 0x10 + 16 * t + 0xA - bank['hd'] for t in range(48)}
            rows[(name, row)] = (data[bank['hd']:end], mutable, bank['type'])
    bound, refused, report = None, {}, []
    for cap in caps:
        here = {}
        for g in range(GROUPS):
            for i in range(SLOTS):
                h = C.u32(cap.ram, D_00281D50 + 4 * (g * SLOTS + i))
                if not h and not (g, i) in ((1, 0),):
                    continue
                use, header, spu = (C.u32(cap.ram, D_0027C6C0 + 12 * h + 4 * k) for k in range(3))
                if C.u32(cap.ram, header + 0xC) != SSHD:
                    refused[(g, i)] = h
                    continue
                match = []
                for key, (want, mutable, typ) in rows.items():
                    got = cap.ram[header:header + len(want)]
                    if typ == g and all(a == b or k in mutable for k, (a, b) in enumerate(zip(got, want))):
                        match.append(key)
                if len(match) != 1 or use != 1:
                    raise SystemExit(f'{cap.name}: group {g} slot {i} handle {h} header {header:#x} '
                                     f'matches {match}')
                here[(g, i)] = (match[0], h, header, spu)
        if bound is None:
            bound = here
        elif {k: v[0] for k, v in bound.items()} != {k: v[0] for k, v in here.items()}:
            raise SystemExit(f'{cap.name}: a different bank binding')
        report.append(dict(capture=cap.name, slots={f'{g}.{i}': dict(bank=f'{m[0]}#row{m[1]}', handle=h,
                                                                    header=hex(hd), spu=hex(sp))
                                                   for (g, i), (m, h, hd, sp) in sorted(here.items())}))
    return bound, refused, report


def area_binding(caps):
    """The (1, 0) binding export_sfx_registry.py's resolver takes, from the
    captures `caps`, with the container, its parse and the capture report:
    (binding, rel, data, parsed, bound, refused, report). Also used by
    export_sfx_registry.py for the level exit's arrival scope (the first
    level's own AREA01 captures)."""
    rel, data, parsed = area_container(caps)
    gdata = (C.DECOMP / X.GLOBAL_CONTAINER).read_bytes()
    gparsed = X.A.parse_container(gdata)
    containers = {X.GLOBAL_CONTAINER: (gdata, gparsed), rel: (data, parsed)}
    bound, refused, report = bindings_from_captures(caps, containers)
    groups = {}
    by_name = {X.GLOBAL_CONTAINER: X.containers_by_type(X.GLOBAL_CONTAINER, gdata),
               rel: X.containers_by_type(rel, data)}
    for (g, i), (key, _h, _hd, _sp) in sorted(bound.items()):
        name, row = key
        bank = next(b for b in by_name[name][g] if b.row == row)
        lst = groups.setdefault(g, [])
        if len(lst) != i:
            raise SystemExit(f'group {g}: slot {i} bound without slot {len(lst)}')
        lst.append(bank)
    if refused.get((3, 0)) is None:
        raise SystemExit('group 3 slot 0 is not the refused header it is in AREA11')
    binding = {(C.AREA, C.SUB): dict(groups=groups, binding='AREA01 captures (D_00281D50/D_0027C6C0)',
                                     refused={(3, 0): 'group 3 bank 0 = handle 3, header without SShd '
                                                      'magic in AREA01'})}
    return binding, rel, data, parsed, bound, refused, report


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=C.OUT / 'sfx')
    args = ap.parse_args(argv)
    args.out = args.out.resolve()
    caps = C.captures()
    X.Elf()                                   # pins the boot ELF
    binding, rel, data, parsed, bound, refused, report = area_binding(caps)
    ids = [i for i in list(range(0x3E8, 0x5DC)) + list(range(0x7D0, 0x9C4))]
    saved = X.area_bindings, X.scene_ids, X.census_report
    X.area_bindings = lambda _elf: binding
    X.scene_ids = lambda census=True: {(C.AREA, C.SUB): ids}
    X.census_report = lambda entries: dict(scope=[C.AREA, C.SUB], note='no first-level census for AREA01')
    try:
        reg = X.export(args.out)
    finally:
        X.area_bindings, X.scene_ids, X.census_report = saved
    (args.out / 'area01_banks.bin').write_bytes(data)
    counts = {}
    for e in reg['entries']:
        counts[e['state']] = counts.get(e['state'], 0) + 1
    reasons = {}
    for e in reg['entries']:
        if e['state'] == X.STATE_UNSUPPORTED:
            reasons[e['reason']] = reasons.get(e['reason'], 0) + 1
    summary = dict(container=rel, container_bytes=len(data), container_sha256=C.sha(data),
                   banks=[dict(row=r, hd=hex(b['hd']), type=b['type'], body=hex(b['body_base']),
                               body_bytes=b['body_size']) for r, b in enumerate(parsed['banks'])],
                   bindings={f'{g}.{i}': f'{k[0]}#row{k[1]}' for (g, i), (k, _h, _a, _s) in sorted(bound.items())},
                   refused={f'{g}.{i}': h for (g, i), h in refused.items()},
                   ids=len(ids), states=dict(audible=counts.get(1, 0), absent=counts.get(2, 0),
                                             unsupported=counts.get(3, 0)),
                   unsupported_reasons=reasons, samples=len(reg['samples']),
                   registry_sha256=reg['registry_sha256'], captures=report,
                   not_verified='SPU residency of the samples (no AREA01 SPU capture)')
    (args.out / 'banks.json').write_text(json.dumps(summary, indent=1) + '\n')
    print(f"area banks {rel} ({len(data)} bytes, {len(parsed['banks'])} banks); bindings "
          f"{summary['bindings']} in {len(caps)} captures; registry {len(ids)} ids: {summary['states']}, "
          f"{len(reg['samples'])} samples")
    return 0


if __name__ == '__main__':
    sys.exit(main())
