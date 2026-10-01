#!/usr/bin/env python3
"""Export the sound banks and the registry of each area the a13 captures
load (AREA13 sub 0, and AREA19 sub 0 at the a13_05 arrival;
docs/AREA13_ASSETS.md).

export_area01_sfx.bindings_from_captures and export_sfx_registry's resolver
and EMSR v2 writer, imported unchanged (the AREA01 .. AREA06 lanes'
method): which banks the area uses is read from the sound driver's tables
in every capture (D_00281D50 handle table, D_0027C6C0 handle records),
each bound header must equal its container row except the 48 per-track
channel bytes +0x0A, and a slot whose header lacks the SShd magic is
refused (00119EA0 tests the magic word, +0xC, before it uses a bank: its
NEARMISS C). From export_area02_sfx it reuses block_bytes, and from
export_area04_sfx refusal_reasons.

This lane's own step: where the container is. 001FFCD0 state 4 runs
001FF590(0xAB, 0), which hands the descriptor's table entry 0 to the IOP
stream loader, only when that entry precedes the group-A sections (+0x0C,
the first group-A index, is 1); the nested block's state 8 does the same
with 0xAC. So the container is entry 0 of the descriptor whose +0x0C is 1:
AREA13's flat top descriptor (sector 17; entry 0 = block +0 .. +0xA6800),
and AREA19's nested block 0 (sector 23 +0x100; its top descriptor's +0x0C
is 0, its only entry the GS texel upload). None of it is resident. The
extracted files hold its bytes under shifted names (the file lists are
resident-relative), so banks.json names them as byte sources only. One
registry per target, scope (area, 0): the area-paged ids 0x3E8..0x5DB and
0x7D0..0x9C3 (001FB9F0's remap tables decide present or absent per id).

Output (ignored assets/area13/, disc-derived, never committed):
  sub0/sfx/area13_banks.bin, banks.json, sfx_registry.emsr (+.json)
  area19/sub0/sfx/area19_banks.bin, banks.json, sfx_registry.emsr (+.json)

Not verified here: SPU residency of the samples (no SPU capture).

Usage (port root): python3 tools/export_area13_sfx.py [--target area13|area19]
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area13_common as A13  # noqa: E402  (first: it keeps AREA01's nested load-map builder)
import export_area04_sfx as E04  # noqa: E402  (refusal_reasons; sets AREA04 state on import)

E02 = E04.E02                          # export_area02_sfx (block_bytes)
C = A13.configure('area13')            # this lane's state from here on
S = E04.S                              # export_area01_sfx
X = S.X                                # export_sfx_registry


def container_block(cap):
    """(chunk, [(path, block offset, span)], sections) of the descriptor
    whose table entry 0 is the IOP stream (its +0x0C = 1): the top one of a
    flat sector, else the capture's nested block (whose +0x0C must be 1
    while the top's is 0)."""
    sector, _note = A13.descriptor(cap)
    area, sub = cap.ram[C.D_00810700], cap.ram[C.D_00810700 + 1]
    top = sector[:C.DESCRIPTOR_SIZE]
    if C.u32(top, 0x18) == 0:
        desc, chunk = top, f'chunk{area + 4:02d}'
    else:
        desc = sector[C.NESTED_BASE + C.NESTED_SIZE * sub:C.NESTED_BASE + C.NESTED_SIZE * (sub + 1)]
        chunk = f'chunk{area + 4:02d}.n{sub}'
        if C.u16(top, 0x0C) != 0:
            raise SystemExit(f'{cap.name}: both the top and the nested descriptor carry a table entry 0')
    if C.u16(desc, 0x0C) != 1:
        raise SystemExit(f'{cap.name}: {chunk} +0x0C = {C.u16(desc, 0x0C)}: no IOP stream entry')
    sections = A13._sections(desc)
    return chunk, C._extract_files(chunk, A13._files(desc), C.u32(desc, 8)), sections, C.u32(desc, 0x14)


def area_container(cap):
    """(label, container bytes, parsed, [file names], section 0): table
    entry 0 of container_block, which must start at block offset 0, lie
    before the resident region and hold an SShd container that ends inside
    it."""
    chunk, files, sections, resident = container_block(cap)
    first = sections[0]
    if first[0] != 0 or first[1] > resident:
        raise SystemExit(f'{chunk} section 0 at {first[0]:#x} + {first[1]:#x}: not before the resident region')
    data = E02.block_bytes(files, 0, first[1])
    parsed = X.A.parse_container(data)
    if parsed is None or parsed['total'] > first[1]:
        raise SystemExit(f'{chunk}: section 0 is not an SShd container')
    names = [f'{p.parent.name}/{p.name}' for p, off, span in files if off < parsed['total']]
    return f'{chunk} +0x0', data[:parsed['total']], parsed, names, first


def export_target(name, out_root):
    A13.configure(name)
    t = A13.current()
    A13.read_overlay(t)
    caps = C.captures()
    gdata = (C.DECOMP / X.GLOBAL_CONTAINER).read_bytes()
    gparsed = X.A.parse_container(gdata)
    label, data, parsed, names, first = area_container(caps[0])
    for cap in caps[1:]:
        if area_container(cap)[1] != data:
            raise SystemExit(f'{cap.name}: a different area container')
    cname = f'{t.name}_sub{t.sub}_banks.bin'
    containers = {X.GLOBAL_CONTAINER: (gdata, gparsed), cname: (data, parsed)}
    bound, refused, report = S.bindings_from_captures(caps, containers)
    by_name = {X.GLOBAL_CONTAINER: X.containers_by_type(X.GLOBAL_CONTAINER, gdata),
               cname: X.containers_by_type(cname, data)}
    groups = {}
    for (g, i), (key, _h, _hd, _sp) in sorted(bound.items()):
        bank = next(b for b in by_name[key[0]][g] if b.row == key[1])
        lst = groups.setdefault(g, [])
        if len(lst) != i:
            raise SystemExit(f'group {g}: slot {i} bound without slot {len(lst)}')
        lst.append(bank)
    why = {k: f'{v} in {t.label} sub {t.sub}' for k, v in E04.refusal_reasons(caps, refused).items()}
    binding = {(C.AREA, t.sub): dict(groups=groups, refused=why,
                                     binding=f'{t.label} sub {t.sub} captures (D_00281D50/D_0027C6C0)')}
    ids = list(range(0x3E8, 0x5DC)) + list(range(0x7D0, 0x9C4))
    saved = X.area_bindings, X.scene_ids, X.census_report
    X.area_bindings = lambda _elf: binding
    X.scene_ids = lambda census=True: {(C.AREA, t.sub): ids}
    X.census_report = lambda entries: dict(scope=[C.AREA, t.sub], note=f'no first-level census for {t.label}')
    sfx = out_root / Path(*t.out.relative_to(A13.OUT).parts) / 'sfx'
    try:
        reg = X.export(sfx)
    finally:
        X.area_bindings, X.scene_ids, X.census_report = saved
    counts, reasons = {}, {}
    for e in reg['entries']:
        counts[e['state']] = counts.get(e['state'], 0) + 1
        if e['state'] == X.STATE_UNSUPPORTED:
            reasons[e['reason']] = reasons.get(e['reason'], 0) + 1
    (sfx / f'{t.name}_banks.bin').write_bytes(data)
    summary = dict(
        target=name, sub=t.sub, captures=[c.name for c in caps], container=label, container_byte_sources=names,
        container_byte_sources_note='extracted files as byte sources; their names read the resident-relative '
                                    'file list as block offsets and do not name what they hold',
        section0=[hex(first[0]), hex(first[1])], container_bytes=len(data), container_sha256=C.sha(data),
        banks=[dict(row=r, hd=hex(b['hd']), type=b['type'], body=hex(b['body_base']), body_bytes=b['body_size'])
               for r, b in enumerate(parsed['banks'])],
        bindings={f'{g}.{i}': f'{"global" if k[0] == X.GLOBAL_CONTAINER else "area"}#row{k[1]}'
                  for (g, i), (k, _h, _a, _s) in sorted(bound.items())},
        refused={f'{g}.{i}': h for (g, i), h in refused.items()},
        refused_why={f'{g}.{i}': w for (g, i), w in sorted(why.items())}, ids=len(ids),
        states=dict(audible=counts.get(1, 0), absent=counts.get(2, 0), unsupported=counts.get(3, 0)),
        unsupported_reasons=reasons, samples=len(reg['samples']), registry_sha256=reg['registry_sha256'],
        slots=report, not_verified='SPU residency of the samples (no SPU capture)')
    (sfx / 'banks.json').write_text(json.dumps(summary, indent=1) + '\n')
    print(f"{t.label}: container {label} ({len(data)} bytes, {len(parsed['banks'])} banks, byte sources {names}); "
          f"bindings {summary['bindings']}, refused {summary['refused']} ({summary['refused_why']}) in "
          f"{len(caps)} captures; registry {len(ids)} ids: {summary['states']} {reasons}, "
          f"{len(reg['samples'])} samples")
    return summary


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=A13.OUT)
    ap.add_argument('--target', choices=sorted(A13.TARGETS), action='append')
    args = ap.parse_args(argv)
    out = args.out.resolve()
    X.Elf()                                   # pins the boot ELF
    for name in args.target or ('area13', 'area19'):
        export_target(name, out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
