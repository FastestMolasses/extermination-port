#!/usr/bin/env python3
"""Export AREA15's level for sub 0 (a19d_20) and sub 1 (a15_01): geometry +
textures + GS state codes (one zone EMDL, the static bank id 0x44), the
static bank image, sub 0's dynamic list, the background, the grid
collision, the cell directory with its per-capture derivation, the
render-context block check and the loaded-sub proof (docs/AREA15_ASSETS.md).

Sub 1: export_area13_level.export_target (the AREA13 lane's nested-load
path) is imported and run UNCHANGED with export_area15_common's AREA15
target installed: sub 1's nested list relocates no id 0x45, and
D_0028A5A4 keeps its previous value (0x1B18440 in a15_00 and a15_01),
which is that lane's stale-pointer model.

Sub 0: its nested list relocates id 0x45 (chunk19.n0 +0x2F8000), so
D_0028A5A4 is the relocated id 0x45 (001FFCD0 state 11), which
export_area13_level.export_target refuses. export_dynamic is that
function's sequence with the AREA01 lane's dynamic-list step in place of
the stale-pointer step: the list *D_0028A5A4 (count, then 0x860-byte
entries from +0x10, export_area01_level.dynamic_entries) is exported as
level/dynamic_objects.emsc, and every kernel-0x00237450 kick must REF one
of its entries (export_area01_level.check_kicks with the list). Every other
step calls the same helpers (relocation_rows, the load-map compare, the
bank walk, the zones, the textures, the background, the ctx block,
check_cells with this lane's 001A2370 / 0019C6F0 rules, run_collision).

Output (ignored build/area15/assets/tree/part<sub>/, disc-derived, never
committed; export_area15_split.py places it under assets/area15/):
  sub<s>/level/00_id44.emdl (+ .gsmat.json), sub<s>/level/static_bank.emsc,
  sub0/level/dynamic_objects.emsc, sub<s>/level/level.json,
  sub<s>/background.embg (when the capture arms it), sub<s>/area15.emcl
  (+ scene.txt), sub<s>/area15_cells.bin, sub<s>/cells.json
and TREE/loaded_sub_proof.json (both captures).

Usage (port root, macOS arm64, pure Python):
  python3 tools/export_area15_level.py [--sub 0|1] [--skip-collision]
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area15_common as A15  # noqa: E402  (first: registers the AREA15 target)

A13, C, LV = A15.A13, A15.C, A15.LV
L, E22 = LV.L, LV.E22


def sub_proof(caps):
    """export_area13_common.loaded_sub_proof, each capture's own sub over
    100 times better (the AREA13 lane's rule)."""
    proof = A13.loaded_sub_proof(caps)
    for cap in caps:
        rows, sub = proof[cap.name], cap.ram[0x810701]
        if not all(rows[sub] * 100 < rows[s] for s in rows if s != sub):
            raise SystemExit(f'{cap.name}: its sub-{sub} map does not fit RAM far better ({rows})')
    return {k: {f'sub{s}': v for s, v in r.items()} for k, r in proof.items()}


def dynamic_list_problems(caps, rwhere):
    """Sub 0's id-0x45 rule: the nested list relocates id 0x45 and every
    capture's D_0028A5A4 is that relocated address."""
    out = []
    if 0x45 not in rwhere:
        return ['the descriptor relocates no id 0x45']
    for cap in caps:
        if C.u32(cap.ram, LV.D_0028A5A4) != rwhere[0x45]:
            out.append(f'{cap.name}: D_0028A5A4 = {C.u32(cap.ram, LV.D_0028A5A4):#x} is not the relocated id 0x45 '
                       f'{rwhere[0x45]:#x}')
    return out


def export_dynamic(el, elf, out_root, iso, skip_collision):
    """export_area13_level.export_target for a load whose nested list
    relocates id 0x45 (module docstring). Returns (image, caps, lmap)."""
    LV.install('area15')
    t = A13.current()
    caps = C.captures()
    lmap, info = C.build_load_map(caps[0])
    image = C.LoadedImage(lmap)
    buffers = {caps[0].name: info['rambuf']}
    for cap in caps[1:]:
        other, oinfo = C.build_load_map(cap)
        if other != lmap:
            raise SystemExit(f'{cap.name}: a different load map')
        buffers[cap.name] = oinfo['rambuf']
    rproblems, rwhere = LV.relocation_rows(caps)
    rproblems += dynamic_list_problems(caps, rwhere)
    if rproblems:
        raise SystemExit('relocations: ' + '; '.join(rproblems[:4]))
    for ident, word in ((0x41, LV.D_0028A594), (0x42, LV.D_0028A598), (0x43, LV.D_0028A59C),
                        (0x44, LV.D_0028A5A0), (0x46, LV.D_0028A5A8)):
        if rwhere.get(ident) != C.u32(caps[0].ram, word):
            raise SystemExit(f'D_{word:08X} is not the relocated id {ident:#x}')
    cells_table = C.u32(caps[0].ram, LV.D_0028A5A8)
    hit = image.locate(cells_table)
    if hit is None:
        raise SystemExit(f'cell directory {cells_table:#x} outside the load map')
    _count, _hulls, cells_size = L.cell_directory(image.read(cells_table, hit[0] + hit[3] - cells_table), 0)
    load_rows = {}
    for cap in caps:
        rows = C.compare_load_map(image, cap, allow=[(cells_table, cells_table + cells_size)])
        bad = [r for r in rows if r['unexpected_rows']]
        if bad:
            raise SystemExit(f'{cap.name}: load map differs from RAM: {bad}')
        load_rows[cap.name] = sum(r['bytes'] for r in rows)

    def file_of(address):
        hit = image.locate(address)
        if hit is None:
            raise SystemExit(f'{address:#x} outside the load map')
        return hit[4]

    bank = C.u32(caps[0].ram, LV.D_0028A5A0)
    dyn_base = C.u32(caps[0].ram, LV.D_0028A5A4)
    for cap in caps:
        if (C.u32(cap.ram, LV.D_0028A5A0), C.u32(cap.ram, LV.D_0028A5A4)) != (bank, dyn_base):
            raise SystemExit(f'{cap.name}: bank pointers differ')
    objects = L.bank_objects(image.read, bank)
    dyn = L.dynamic_entries(image.read, dyn_base)
    kicks, states, touched = L.check_kicks(caps, objects, dyn)
    prim = el.level_template_prim(el.BootElf(C.ELF_PATH))
    want_state = (prim, el.GS_CLASS0_TEST, el.GS_CLASS0_ALPHA, el.GS_CLASS0_TEX1, el.GS_CLASS0_CLAMP)
    if set(states) != {want_state}:
        raise SystemExit(f'level kick GS state {states} != class 0 {want_state}')
    ctx = C.u32(caps[0].ram, L.CTX_PTR) & 0x1FFFFFF
    grid = C.u32(caps[0].ram, ctx + 0x140)
    if not objects[0][1] <= grid < objects[1][1]:
        raise SystemExit('render ctx +0x140 is not inside bank object 0')
    ids = set()
    stride = C.u32(caps[0].ram, ctx + 0x148)
    for ix in range(32):
        for iz in range(32):
            for s in range(4):
                v = C.s32(caps[0].ram, grid + 4 * ((ix * stride + iz) * 4 + s))
                if v > 0:
                    ids.add(v)
    if ids != set(range(1, len(objects))):
        raise SystemExit(f'grid ids {len(ids)} do not name objects 1..{len(objects) - 1}')

    out = out_root / Path(*t.out.relative_to(A13.OUT).parts)
    rel = out.relative_to(out_root).as_posix()
    level = out / 'level'
    level.mkdir(parents=True, exist_ok=True)
    for old in list(level.glob('*.emdl')) + list(level.glob('*.gsmat.json')):
        old.unlink()
    gs_path, transfers, sections = L.texture_localmem(el, argparse.Namespace(iso=iso))
    zones = L.build_zones(el, image, objects, file_of)
    alphas = L.texture_alphas(el, image, objects[1:])
    zone_rows, tex_checked = [], 0
    for n, (label, (b, oids, bad, recs)) in enumerate(sorted(zones.items(),
                                                             key=lambda z: L.file_of_order(z[0], lmap))):
        if bad:
            raise SystemExit(f'{label}: {bad} records with a nonzero matrix slot')
        entries, blob = el.build_texture_blob(None, b.tex_table, gs_path)
        for cap in caps:
            if cap.gs is None:
                raise SystemExit(f'{cap.name}: no GS freeze')
            e2, b2 = el.build_texture_blob(None, b.tex_table, cap.gs)
            for x, y in zip(entries, e2):
                if blob[x['off']:x['off'] + 4 * x['w'] * x['h']] != b2[y['off']:y['off'] + 4 * y['w'] * y['h']]:
                    raise SystemExit(f'{label}: disc texture differs from {cap.name} GS freeze')
                tex_checked += 1
        stem = Path(label).name.split('.')[0]
        path = level / f'{n:02d}_{stem}.emdl'
        L.write_zone(el, path, b, entries, blob, prim, alphas, label)
        no_tex = sum(1 for x in b.tex if x == b.NO_TEX)
        zone_rows.append(dict(zone=path.name, source=label, objects=len(oids), first_object=min(oids),
                              last_object=max(oids), records=recs, vertices=len(b.pos),
                              triangles=len(b.tris) // 3, textures=len(b.tex_table),
                              untextured_vertices=no_tex, sha256=C.sha(path.read_bytes())))
        print(f'{rel}/level/{path.name}: {len(oids)} objects, {len(b.pos)} verts, '
              f'{len(b.tris) // 3} tris, {len(b.tex_table)} textures')

    lo, hi = bank, max(o + 0x40 + L.UNIT * n for _i, o, n in objects)
    bank_bytes = image.read(lo, hi - lo)
    (level / 'static_bank.emsc').write_bytes(C.emsc(lo, bank_bytes))
    dyn_bytes = image.read(dyn_base, 0x10 + L.DYN_ENTRY * len(dyn))
    (level / 'dynamic_objects.emsc').write_bytes(C.emsc(dyn_base, dyn_bytes))

    background = L.check_background(caps)
    bg = LV.export_background(el, caps, out)
    room = E22.check_ctx_block(caps, elf)
    LV.install('area15')

    table, count, hulls, cells, cell_rows, calls = LV.check_cells(image, caps, elf)
    (out / f'{t.name}_cells.bin').write_bytes(cells)
    drums = E22.drum_rows(caps, hulls, cells)
    if any(d['state'] != 1 for d in drums):
        raise SystemExit(f'a drum not in state 1: {[d for d in drums if d["state"] != 1]}')
    cells_report = dict(address=hex(table), count=count, bytes=len(cells), sha256=C.sha(cells),
                        flag_uids={u: f for u, (_s, _e, f) in hulls.items() if f},
                        moved_uids=sorted({u for r in cell_rows for u in r['moved_hulls']}),
                        flag_calls={k: [list(c) for c in v] for k, v in calls.items()},
                        drums=sorted({(d['uid'], d['state'], d['hull_flags']) for d in drums}),
                        captures=cell_rows)
    coll = None
    if not skip_collision:
        files, grid_hdr, log = LV.run_collision(image, caps, out / f'{t.name}.emcl', C.SCRATCH)
        coll = dict(files=files, grid_header=hex(grid_hdr), log=log.strip().splitlines(),
                    sha256=C.sha((out / f'{t.name}.emcl').read_bytes()))
        print(f'{rel}/{t.name}.emcl: {files}; ' + ' '.join(x for x in log.splitlines() if x.startswith('wrote')))
    (out / 'cells.json').write_text(json.dumps(dict(cells=cells_report, collision=coll), indent=1,
                                               default=str) + '\n')

    report = dict(
        target=t.name, area=C.AREA, sub=t.sub, captures=[c.name for c in caps],
        excluded_captures=[dict(capture=n, area_bytes=list(a)) for n, a in A13.excluded_captures(t)],
        load_map=[dict(address=hex(a), id=lab, bytes=s, extracted_source=f'{p.parent.name}/{p.name}',
                       source_offset=hex(o)) for a, p, o, s, lab in lmap],
        load_info={k: (hex(v) if isinstance(v, int) else v) for k, v in info.items()},
        load_map_bytes_equal=load_rows, descriptor_buffer=buffers,
        relocations={hex(k): hex(v) for k, v in sorted(rwhere.items())},
        dynamic_list=dict(address=hex(dyn_base), relocated_id='0x45', entries=len(dyn), sha256=C.sha(dyn_bytes),
                          previous_capture=LV.PREVIOUS[t.name].name,
                          previous_value=None if LV.previous_word(t.name, LV.D_0028A5A4) is None
                          else hex(LV.previous_word(t.name, LV.D_0028A5A4)),
                          note='D_0028A5A4 = the relocated id 0x45 (001FFCD0 state 11); kernel 0x00237450 with '
                               'the entry +0x34 translation; exported raw, not as EMDL'),
        static_bank=dict(address=hex(bank), objects=len(objects), grid=hex(grid), span=[hex(lo), hex(hi)],
                         sha256=C.sha(bank_bytes)),
        kicks=kicks, distinct_level_refs=touched,
        level_state=dict(prim=hex(prim), test_1=hex(el.GS_CLASS0_TEST), alpha_1=hex(el.GS_CLASS0_ALPHA),
                         tex1_1=hex(el.GS_CLASS0_TEX1), clamp_1=hex(el.GS_CLASS0_CLAMP)),
        textures=dict(source='level-load GS upload replayed from the disc image', transfers=transfers,
                      sections=sections, texture_comparisons_with_captures=tex_checked),
        zones=zone_rows, background=background, background_asset=bg, ctx_room_block=room)
    (level / 'level.json').write_text(json.dumps(report, indent=1, default=str) + '\n')
    print(f'{t.label} sub {t.sub}: static bank {bank:#x}: {len(objects)} objects -> {len(zone_rows)} zone EMDLs; '
          f'{sum(r["level_kicks"] for r in kicks)} level kicks, {sum(r["dynamic_kicks"] for r in kicks)} dynamic '
          f'kicks in {len(caps)} captures (dynamic list {dyn_base:#x}, {len(dyn)} entries); {tex_checked} texture '
          f'decodes equal the GS freezes; background {"exported, " + str(bg["size"]) if bg else "not armed"}; '
          f'ctx +0xA0..+0xFF rebuilt from room entries {sorted({r["room_entry"] for r in room})}; '
          f'cells {count} uids ({len(cells)} bytes), moved {cells_report["moved_uids"]}, flag calls '
          f'{sorted({tuple(map(tuple, v)) for v in calls.values()})}; drums {cells_report["drums"]}')
    return image, caps, lmap


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--tree', type=Path, default=A15.TREE)
    ap.add_argument('--iso', type=Path, default=C.ISO_PATH,
                    help="the user's disc image (the level-load GS upload is replayed from it)")
    ap.add_argument('--sub', type=int, choices=(0, 1), action='append', help='default both')
    ap.add_argument('--skip-collision', action='store_true')
    args = ap.parse_args(argv)
    tree = args.tree.resolve()
    A15.install(0)
    el = L.load_export_level()
    elf = C.read_elf()
    A13.read_overlay(A15.TARGET)                 # pins the overlay
    caps = []
    for s in args.sub or (0, 1):
        A15.install(s)
        A15.TARGET.out = tree / f'part{s}' / f'sub{s}'
        A13.OUT = tree / f'part{s}'
        got = C.captures()
        names = tuple(c.name for c in got)
        if names != A15.CAPTURE_NAMES[s]:
            raise SystemExit(f'AREA15 sub-{s} captures {names}, not the pinned {A15.CAPTURE_NAMES[s]}')
        caps += got
        A13.OUT.mkdir(parents=True, exist_ok=True)
        if s == 0:
            export_dynamic(el, elf, A13.OUT, args.iso.resolve(), args.skip_collision)
        else:
            LV.export_target('area15', el, elf, A13.OUT, args.iso.resolve(), args.skip_collision)
    if not args.sub:
        (tree / 'loaded_sub_proof.json').write_text(json.dumps(sub_proof(caps), indent=1) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
