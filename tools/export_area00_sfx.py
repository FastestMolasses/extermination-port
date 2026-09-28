#!/usr/bin/env python3
"""Export the AREA00 sound banks and the registry of each sub
(docs/AREA00_ASSETS.md).

export_area01_sfx.bindings_from_captures and export_sfx_registry's resolver
and EMSR v2 writer, imported unchanged (the AREA01 lane's method): which banks
the area uses is read from the sound driver's tables in every capture
(D_00281D50 handle table, D_0027C6C0 handle records), each bound header must
equal its container row except the 48 per-track channel bytes +0x0A, and
group 3's header (no SShd magic) is refused, as in AREA11 and AREA01.

AREA00's own step: the area container. The nested descriptor's upload
section 0 starts at block offset 0 (+0 .. +0x8B000), inside the block's
first file, which is not resident at all in AREA00 (the resident region
starts at +0x173800), so it is not in the load map; the file is found from
the nested descriptor's own file list (the entry at block offset 0). In
every capture of both subs the bindings are: group 1 -> the global container
rows 0..2 (handles 0..2), group 2 -> area row 0 (handle 4), group 4 -> area
rows 1 and 2 (handles 5, 6). chunk04.n0 and chunk04.n1 carry byte-identical
first files, so one container serves both subs.

The registry is resolved per sub, for scope (0, sub): the area-paged ids
0x3E8..0x5DB and 0x7D0..0x9C3 (001FB9F0's remap tables decide present or
absent per id).

Output (ignored assets/area00/, disc-derived, never committed):
  sfx/area00_banks.bin                   the SShd container (+0 .. total)
  sfx/banks.json                         bindings and verification summary
  sub<N>/sfx/sfx_registry.emsr (+.json)  EMSR v2 for scope (0, N)

Not verified here: SPU residency of the samples (no AREA00 SPU capture).

Usage (port root): python3 tools/export_area00_sfx.py
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area00_common as A0  # noqa: E402

C = A0.configure(0)
import export_area01_sfx as S  # noqa: E402

X = S.X


def area_container(cap):
    """(relative name, container bytes, parsed, info): upload section 0 of
    the capture's nested descriptor, read from the block file that holds
    block offset 0 (the descriptor's file list), checked to be an SShd
    container that ends inside section 0."""
    _lmap, info = C.build_load_map(cap)
    first = info['sections'][0]
    if first[0] != 0:
        raise SystemExit(f'nested section 0 at {first[0]:#x}, not the block start')
    top = C.iso_descriptor(C.AREA + 4)
    if top is None:
        raise SystemExit('the disc image is needed for the nested descriptor')
    sub = cap.ram[0x810701]
    nested = top[C.NESTED_BASE + C.NESTED_SIZE * sub:C.NESTED_BASE + C.NESTED_SIZE * (sub + 1)]
    nfirst, ncount = C.u16(nested, 0x0C), C.u16(nested, 0x0E)
    files = C._files(nested, 0x20 + 8 * (nfirst + ncount), C.u32(nested, 0x1C))
    (path, _off, size), = [f for f in C._extract_files(info['nested'], files, C.u32(nested, 8)) if f[1] == 0]
    if size < first[1]:
        raise SystemExit(f'{path} ({size:#x} bytes) does not hold section 0 ({first[1]:#x})')
    data = path.read_bytes()[:first[1]]
    parsed = X.A.parse_container(data)
    if parsed is None or parsed['total'] > first[1]:
        raise SystemExit(f'{path}: section 0 is not an SShd container')
    return str(path.relative_to(C.DECOMP)), data[:parsed['total']], parsed, info


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=A0.OUT)
    args = ap.parse_args(argv)
    out = args.out.resolve()
    X.Elf()                                   # pins the boot ELF
    gdata = (C.DECOMP / X.GLOBAL_CONTAINER).read_bytes()
    gparsed = X.A.parse_container(gdata)
    summary = dict(subs={})
    container = None
    for sub in A0.SUBS:
        A0.configure(sub)
        caps = C.captures()
        rel, data, parsed, _info = area_container(caps[0])
        if container is None:
            container = (rel, data, parsed)
        elif data != container[1]:
            raise SystemExit(f'sub {sub}: the area container differs from sub 0\'s')
        name = container[0]                   # one container for both subs (equal bytes)
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
            raise SystemExit('group 3 slot 0 is not the refused header it is in AREA11 / AREA01')
        binding = {(C.AREA, sub): dict(groups=groups, binding='AREA00 captures (D_00281D50/D_0027C6C0)',
                                       refused={(3, 0): 'group 3 bank 0 = handle 3, header without SShd '
                                                        'magic in AREA00'})}
        ids = list(range(0x3E8, 0x5DC)) + list(range(0x7D0, 0x9C4))
        saved = X.area_bindings, X.scene_ids, X.census_report
        X.area_bindings = lambda _elf: binding
        X.scene_ids = lambda census=True: {(C.AREA, sub): ids}
        X.census_report = lambda entries: dict(scope=[C.AREA, sub], note='no first-level census for AREA00')
        try:
            reg = X.export(out / f'sub{sub}/sfx')
        finally:
            X.area_bindings, X.scene_ids, X.census_report = saved
        counts, reasons = {}, {}
        for e in reg['entries']:
            counts[e['state']] = counts.get(e['state'], 0) + 1
            if e['state'] == X.STATE_UNSUPPORTED:
                reasons[e['reason']] = reasons.get(e['reason'], 0) + 1
        summary['subs'][sub] = dict(
            captures=[c.name for c in caps],
            bindings={f'{g}.{i}': f'{k[0]}#row{k[1]}' for (g, i), (k, _h, _a, _s) in sorted(bound.items())},
            refused={f'{g}.{i}': h for (g, i), h in refused.items()}, ids=len(ids),
            states=dict(audible=counts.get(1, 0), absent=counts.get(2, 0), unsupported=counts.get(3, 0)),
            unsupported_reasons=reasons, samples=len(reg['samples']), registry_sha256=reg['registry_sha256'],
            slots=report)
        print(f"sub {sub}: bindings {summary['subs'][sub]['bindings']} in {len(caps)} captures; registry "
              f"{len(ids)} ids: {summary['subs'][sub]['states']}, {len(reg['samples'])} samples")
    rel, data, parsed = container
    (out / 'sfx').mkdir(parents=True, exist_ok=True)
    (out / 'sfx/area00_banks.bin').write_bytes(data)
    summary.update(container=rel, container_bytes=len(data), container_sha256=C.sha(data),
                   banks=[dict(row=r, hd=hex(b['hd']), type=b['type'], body=hex(b['body_base']),
                               body_bytes=b['body_size']) for r, b in enumerate(parsed['banks'])],
                   not_verified='SPU residency of the samples (no AREA00 SPU capture)')
    (out / 'sfx/banks.json').write_text(json.dumps(summary, indent=1) + '\n')
    print(f"area banks {rel} ({len(data)} bytes, {len(parsed['banks'])} banks)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
