#!/usr/bin/env python3
"""Export the AREA02 sound banks and the registry of each sub
(docs/AREA02_ASSETS.md).

export_area01_sfx.bindings_from_captures and export_sfx_registry's resolver
and EMSR v2 writer, imported unchanged (the AREA01 / AREA00 lanes' method):
which banks the area uses is read from the sound driver's tables in every
capture (D_00281D50 handle table, D_0027C6C0 handle records), each bound
header must equal its container row except the 48 per-track channel bytes
+0x0A, and a header without SShd magic is refused (00119EA0 tests the magic
word, +0xC, before it uses a bank: its NEARMISS C), as group 3's in AREA11,
AREA01 and AREA00.

AREA02's own steps:
  * the area container is upload section 0 of the sub's nested descriptor
    (block offset 0 .. the section size). In sub 0 the SShd container
    (0x9A410 bytes) is longer than the block's first file (0x28800), so the
    block bytes are assembled from the descriptor's own file list
    (id << 24 | block offset) over the extracted files, in block order;
    none of them is resident (the resident region starts at +0x14B000 in
    sub 0 and +0x82000 in sub 1), so they are not in the load map;
  * the subs load different containers: sub 0 three banks (group 2 -> area
    row 0, group 4 -> area rows 1 and 2), sub 1 one bank (group 2 -> area
    row 0). In the sub-1 captures the group-4 handles 5 and 6 are still in
    use but their headers lie inside resident level data and carry no SShd
    magic, so bindings_from_captures refuses them with group 3's header;
    the registry resolves ids that reach them as absent ("00119EA0 returns
    -1"), as it does for group 3.

The registry is resolved per sub, for scope (2, sub): the area-paged ids
0x3E8..0x5DB and 0x7D0..0x9C3 (001FB9F0's remap tables decide present or
absent per id).

Output (ignored assets/area02/, disc-derived, never committed):
  sub<N>/sfx/area02_banks.bin            the sub's SShd container (+0 .. total)
  sub<N>/sfx/banks.json                  bindings and verification summary
  sub<N>/sfx/sfx_registry.emsr (+.json)  EMSR v2 for scope (2, N)

Not verified here: SPU residency of the samples (no AREA02 SPU capture).

Usage (port root): python3 tools/export_area02_sfx.py
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area02_common as A2  # noqa: E402

C = A2.configure(0)
import export_area01_sfx as S  # noqa: E402

X = S.X


def nested_block(sub):
    """(chunk name, [(path, block offset, span)], section list) of the sub's
    nested descriptor, read from the disc image's INDEX.IDX sector."""
    top = C.iso_descriptor(C.AREA + 4)
    if top is None:
        raise SystemExit('the disc image is needed for the nested descriptor')
    if C.u32(top, 0) != C.AREA + 4:
        raise SystemExit('INDEX.IDX sector word is not the area descriptor')
    nested = top[C.NESTED_BASE + C.NESTED_SIZE * sub:C.NESTED_BASE + C.NESTED_SIZE * (sub + 1)]
    nfirst, ncount = C.u16(nested, 0x0C), C.u16(nested, 0x0E)
    files = C._files(nested, 0x20 + 8 * (nfirst + ncount), C.u32(nested, 0x1C))
    chunk = f'chunk{C.AREA + 4:02d}.n{sub}'
    sections = [(C.u32(nested, 0x20 + 8 * k), C.u32(nested, 0x24 + 8 * k)) for k in range(nfirst + ncount)]
    return chunk, C._extract_files(chunk, files, C.u32(nested, 8)), sections


def block_bytes(files, lo, hi):
    """Block bytes [lo, hi) assembled from the file list (each file holds
    its descriptor span from its block offset)."""
    out = bytearray()
    for path, off, span in files:
        a, b = max(lo, off), min(hi, off + span)
        if a < b:
            if len(out) != a - lo:
                raise SystemExit(f'block bytes {lo:#x}..{hi:#x} have a gap before {path.name}')
            out += path.read_bytes()[a - off:b - off]
    if len(out) != hi - lo:
        raise SystemExit(f'block bytes {lo:#x}..{hi:#x} not covered by the file list')
    return bytes(out)


def area_container(cap):
    """(label, container bytes, parsed, [file names], section 0): upload
    section 0 of the capture's nested descriptor, which must start at block
    offset 0 and hold an SShd container that ends inside it."""
    _lmap, info = C.build_load_map(cap)
    sub = cap.ram[0x810701]
    chunk, files, sections = nested_block(sub)
    if chunk != info['nested'] or sections != info['sections']:
        raise SystemExit(f'{cap.name}: nested descriptor differs from the load map\'s')
    first = sections[0]
    if first[0] != 0:
        raise SystemExit(f'nested section 0 at {first[0]:#x}, not the block start')
    data = block_bytes(files, 0, first[1])
    parsed = X.A.parse_container(data)
    if parsed is None or parsed['total'] > first[1]:
        raise SystemExit(f'{chunk}: section 0 is not an SShd container')
    names = [p.name for p, off, span in files if off < parsed['total']]
    return f'{chunk} +0x0', data[:parsed['total']], parsed, names, first


def export_sub(sub, out):
    A2.configure(sub)
    caps = C.captures()
    gdata = (C.DECOMP / X.GLOBAL_CONTAINER).read_bytes()
    gparsed = X.A.parse_container(gdata)
    label, data, parsed, names, first = area_container(caps[0])
    for cap in caps[1:]:
        if area_container(cap)[1] != data:
            raise SystemExit(f'{cap.name}: a different area container')
    name = f'area02_sub{sub}_banks.bin'
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
        raise SystemExit('group 3 slot 0 is not the refused header it is in AREA11 / AREA01 / AREA00')
    why = {k: f'group {k[0]} bank {k[1]} = handle {h}, header without SShd magic in AREA02 sub {sub}'
           for k, h in refused.items()}
    binding = {(C.AREA, sub): dict(groups=groups, binding=f'AREA02 sub {sub} captures (D_00281D50/D_0027C6C0)',
                                   refused=why)}
    ids = list(range(0x3E8, 0x5DC)) + list(range(0x7D0, 0x9C4))
    saved = X.area_bindings, X.scene_ids, X.census_report
    X.area_bindings = lambda _elf: binding
    X.scene_ids = lambda census=True: {(C.AREA, sub): ids}
    X.census_report = lambda entries: dict(scope=[C.AREA, sub], note='no first-level census for AREA02')
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
    (sfx / 'area02_banks.bin').write_bytes(data)
    summary = dict(
        sub=sub, captures=[c.name for c in caps], container=label, container_files=names,
        section0=[hex(first[0]), hex(first[1])], container_bytes=len(data), container_sha256=C.sha(data),
        banks=[dict(row=r, hd=hex(b['hd']), type=b['type'], body=hex(b['body_base']), body_bytes=b['body_size'])
               for r, b in enumerate(parsed['banks'])],
        bindings={f'{g}.{i}': f'{"global" if k[0] == X.GLOBAL_CONTAINER else "area"}#row{k[1]}'
                  for (g, i), (k, _h, _a, _s) in sorted(bound.items())},
        refused={f'{g}.{i}': h for (g, i), h in refused.items()}, ids=len(ids),
        states=dict(audible=counts.get(1, 0), absent=counts.get(2, 0), unsupported=counts.get(3, 0)),
        unsupported_reasons=reasons, samples=len(reg['samples']), registry_sha256=reg['registry_sha256'],
        slots=report, not_verified='SPU residency of the samples (no AREA02 SPU capture)')
    (sfx / 'banks.json').write_text(json.dumps(summary, indent=1) + '\n')
    print(f"sub {sub}: container {label} ({len(data)} bytes over {names}, {len(parsed['banks'])} banks); "
          f"bindings {summary['bindings']}, refused {summary['refused']} in {len(caps)} captures; registry "
          f"{len(ids)} ids: {summary['states']} {reasons}, {len(reg['samples'])} samples")
    return summary


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=A2.OUT)
    args = ap.parse_args(argv)
    out = args.out.resolve()
    X.Elf()                                   # pins the boot ELF
    A2.read_overlay()
    for sub in A2.SUBS:
        export_sub(sub, out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
