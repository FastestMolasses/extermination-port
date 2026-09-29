#!/usr/bin/env python3
"""Export the AREA04 level of the sub the route loads (sub 0): geometry +
textures + GS materials, the static bank, the dynamic-object list, the grid
collision, the cell directory, and check the background state and the
render-context bytes 001D8FD0 writes (docs/AREA04_ASSETS.md).

This is export_area01_level.py's pipeline, imported and run unchanged with
export_area01_common pointed at AREA04 (export_area04_common.configure):
the load map from the loader's descriptors and cursors, the bank walk
(001C6120's lookup), the dynamic-list walk, the display-list kick check
(level kernel kicks inside bank objects, kernel-0x00237450 kicks inside
dynamic-list entries), export_level.py's record walker / strip builder /
texture decode / GS material code and disc upload replay,
export_collision.py --node-class --verify-ram, the ORIGINAL 001A2370
cell-hull derivation (verify_cell_directory), the background arm and the
ORIGINAL 001D8FD0 ctx rebuild. From export_area02_level it reuses, by
import, run_collision / grid_extent (export_collision.py over the grid
section's resident bytes only). What is AREA04's own:

  * every AREA04 capture is sub 0 (chunk08.n0); the level files are written
    under assets/area04/sub0/ because sub 1 (chunk08.n1) is a different
    nested block, which no capture loads;
  * a dynamic list: D_0028A5A4 = 0x197D480 lies in the load map
    (chunk08.n0/f10_id6c), 54 entries, and the captures draw
    kernel-0x00237450 kicks from it, so level/dynamic_objects.emsc is
    written as AREA01's;
  * the grid (D_0028A598 = 0x1999C80) and the cell directory (0x19C0480)
    both lie in chunk08.n0/f13_id8e, so export_collision.py gets the grid
    section alone (export_area02_level.run_collision; AREA02_ASSETS.md
    finding 7);
  * the owners that re-transform cell hulls, and the matrix each passes to
    001A2370, read from the committed byte-identical C of the AREA04
    overlay (runtime = link name + 0x40):
    0x825510 (func_overlay_AREA04_008254D0), 0x825B00
    (func_overlay_AREA04_00825AC0) and 0x8260C0 (the reel,
    func_overlay_AREA04_00826080): self + 0xD0; 0x219550 (pickup): node +
    0xD0, as in AREA01; 0x156620 (the boot drum, NEARMISS C
    src/func_00156620.c; its state-2 path ends with the call at 0x156EF0,
    whose second argument the pinned ELF sets to self + 0xD0 in the
    instruction before it and whose first is self): self + 0xD0. Five
    drums own hulls 3..7 in every AREA04 capture, all in state 1, so no
    capture shows a drum-moved hull; the checker proves the drum's
    derivation with a planted knocked drum. No AREA04 overlay function calls 0019C6F0, and no
    capture's uid word differs from the disc; verify_cell_directory compares
    every byte of the directory, the uid words included.

Output (ignored assets/area04/, disc-derived, never committed):
  sub0/level/NN_<file>.emdl (+ .gsmat.json)   geometry, textures, codes
  sub0/level/static_bank.emsc                 *D_0028A5A0
  sub0/level/dynamic_objects.emsc             *D_0028A5A4
  sub0/level/level.json                       counts, hashes, verification
  sub0/area04.emcl (+ scene.txt)              grid collision
  sub0/area04_cells.bin                       the cell directory (disc bytes)
  sub0/cells.json                             the directory verification
  loaded_sub_proof.json                       per capture, rows of each nested map

Usage (port root, macOS arm64, pure Python):
  python3 tools/export_area04_level.py
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area04_common as A4  # noqa: E402
import export_area02_level as E02  # noqa: E402  (run_collision, grid_extent; sets AREA02 state on import)

C = A4.configure(0)                    # after the AREA02 import: AREA04 from here on
L = E02.L                              # export_area01_level

D_0028A598, D_0028A59C, D_0028A5A0, D_0028A5A4, D_0028A5A8 = 0x28A598, 0x28A59C, 0x28A5A0, 0x28A5A4, 0x28A5A8
PICKUP = 0x219550
# the boot drum behaviour: its state-2 path calls 001A2370(self, self +
# 0xD0) at 0x156EF0 (original instructions; NEARMISS C src/func_00156620.c)
DRUM = 0x156620
# the AREA04 overlay owners that call 001A2370(self, self + 0xD0) (committed
# byte-identical C, runtime addresses)
HULL_OWNERS = (0x825510, 0x825B00, 0x8260C0)
# every behaviour live in AREA04 that passes node + 0xD0 to 001A2370
NODE_D0_OWNERS = HULL_OWNERS + (PICKUP, DRUM)


def owner_matrix(ram, node):
    """The matrix address a live AREA04 owner passes to 001A2370 (None: the
    behaviour is not a known caller): node + 0xD0 for the pickup 0x219550,
    the drum 0x156620 and the three overlay owners HULL_OWNERS."""
    if C.u32(ram, node + 0x10) in NODE_D0_OWNERS:
        return node + 0xD0
    return None


def install():
    """Point the AREA01 level module at AREA04 (the import of
    export_area02_level installed AREA02's owner_matrix)."""
    A4.configure(0)
    L.owner_matrix = owner_matrix      # derive_cell_image reads it by name


install()


def check_cells(image, caps, elf):
    """(table, count, hulls, disc bytes, rows): the directory's disc bytes
    and export_area01_level.verify_cell_directory over every capture."""
    install()
    first = caps[0]
    table = C.u32(first.spad, L.SPAD_CELLS)
    if table != C.u32(first.ram, D_0028A5A8):
        raise SystemExit('scratchpad 0x70003250 != D_0028A5A8')
    hit = image.locate(table)
    if hit is None:
        raise SystemExit(f'cell directory {table:#x} outside the load map')
    disc = image.read(table, hit[0] + hit[3] - table)
    count, hulls, size = L.cell_directory(disc, 0)
    disc = disc[:size]
    for cap in caps:
        if C.u32(cap.spad, L.SPAD_CELLS) != table:
            raise SystemExit(f'{cap.name}: directory pointer differs')
    rows, problems = L.verify_cell_directory(elf, disc, caps)
    if problems:
        raise SystemExit('cell directory: ' + '; '.join(problems))
    return table, count, hulls, disc, rows


def export_sub(sub, el, elf, out_root, iso, skip_collision):
    """Level, static bank, dynamic list, collision, cells, background and ctx
    block of one sub."""
    A4.configure(sub)
    install()
    caps = C.captures()
    lmap, info = C.build_load_map(caps[0])
    image = C.LoadedImage(lmap)
    buffers = {caps[0].name: info['rambuf']}
    for cap in caps[1:]:
        other, oinfo = C.build_load_map(cap)
        if other != lmap:
            raise SystemExit(f'{cap.name}: a different load map')
        buffers[cap.name] = oinfo['rambuf']
    if info['nested'] != f'chunk08.n{sub}':
        raise SystemExit(f'sub {sub}: nested block {info["nested"]}')
    cells_table = C.u32(caps[0].ram, D_0028A5A8)
    hit = image.locate(cells_table)
    if hit is None:
        raise SystemExit(f'sub {sub}: cell directory {cells_table:#x} outside the load map')
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

    bank = C.u32(caps[0].ram, D_0028A5A0)
    dyn_base = C.u32(caps[0].ram, D_0028A5A4)
    for cap in caps:
        if (C.u32(cap.ram, D_0028A5A0), C.u32(cap.ram, D_0028A5A4)) != (bank, dyn_base):
            raise SystemExit(f'{cap.name}: bank pointers differ')
    if image.locate(dyn_base) is None:
        raise SystemExit(f'sub {sub}: D_0028A5A4 = {dyn_base:#x} outside the load map')
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

    out = out_root / f'sub{sub}'
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
        no_tex = sum(1 for t in b.tex if t == b.NO_TEX)
        zone_rows.append(dict(zone=path.name, source=label, objects=len(oids), first_object=min(oids),
                              last_object=max(oids), records=recs, vertices=len(b.pos),
                              triangles=len(b.tris) // 3, textures=len(b.tex_table),
                              untextured_vertices=no_tex, sha256=C.sha(path.read_bytes())))
        print(f'sub{sub}/level/{path.name}: {len(oids)} objects, {len(b.pos)} verts, '
              f'{len(b.tris) // 3} tris, {len(b.tex_table)} textures')

    lo, hi = bank, max(o + 0x40 + L.UNIT * n for _i, o, n in objects)
    bank_bytes = image.read(lo, hi - lo)
    (level / 'static_bank.emsc').write_bytes(C.emsc(lo, bank_bytes))
    dyn_bytes = image.read(dyn_base, 0x10 + L.DYN_ENTRY * len(dyn))
    (level / 'dynamic_objects.emsc').write_bytes(C.emsc(dyn_base, dyn_bytes))

    background = L.check_background(caps)
    if any(r['armed'] for r in background):
        raise SystemExit('a capture arms the background; run export_level.py --background for it')
    room = L.check_ctx_room_block(caps, elf)

    table, count, hulls, cells, cell_rows = check_cells(image, caps, elf)
    (out / 'area04_cells.bin').write_bytes(cells)
    cells_report = dict(address=hex(table), count=count, bytes=len(cells), sha256=C.sha(cells),
                        flag_uids={u: f for u, (_s, _e, f) in hulls.items() if f},
                        moved_uids=sorted({u for r in cell_rows for u in r['moved_hulls']}),
                        captures=cell_rows)
    coll = None
    if not skip_collision:
        files, grid_hdr, log = E02.run_collision(image, caps, out / 'area04.emcl', C.SCRATCH)
        coll = dict(files=files, grid_header=hex(grid_hdr), log=log.strip().splitlines(),
                    sha256=C.sha((out / 'area04.emcl').read_bytes()))
        print(f'sub{sub}/area04.emcl: {files}; ' + ' '.join(l for l in log.splitlines() if l.startswith('wrote')))
    (out / 'cells.json').write_text(json.dumps(dict(cells=cells_report, collision=coll), indent=1,
                                               default=str) + '\n')

    report = dict(
        area=C.AREA, sub=sub, captures=[c.name for c in caps],
        load_map=[dict(address=hex(a), file=l, file_offset=hex(o), bytes=s) for a, _p, o, s, l in lmap],
        load_info={k: (hex(v) if isinstance(v, int) else v) for k, v in info.items()},
        load_map_bytes_equal=load_rows, descriptor_buffer=buffers,
        static_bank=dict(address=hex(bank), objects=len(objects), grid=hex(grid), span=[hex(lo), hex(hi)],
                         sha256=C.sha(bank_bytes)),
        dynamic_list=dict(address=hex(dyn_base), entries=len(dyn), source=file_of(dyn_base),
                          sha256=C.sha(dyn_bytes),
                          note='kernel 0x00237450 with the entry +0x34 translation; exported raw, not as EMDL'),
        kicks=kicks, distinct_level_refs=touched,
        level_state=dict(prim=hex(prim), test_1=hex(el.GS_CLASS0_TEST), alpha_1=hex(el.GS_CLASS0_ALPHA),
                         tex1_1=hex(el.GS_CLASS0_TEX1), clamp_1=hex(el.GS_CLASS0_CLAMP)),
        textures=dict(source='level-load GS upload replayed from the disc image', transfers=transfers,
                      sections=sections, texture_comparisons_with_captures=tex_checked),
        zones=zone_rows, background=background, ctx_room_block=room)
    (level / 'level.json').write_text(json.dumps(report, indent=1, default=str) + '\n')
    print(f'sub {sub}: static bank {bank:#x}: {len(objects)} objects -> {len(zone_rows)} zone EMDLs; dynamic list '
          f'{dyn_base:#x} ({len(dyn)} entries); {sum(r["level_kicks"] for r in kicks)} level kicks and '
          f'{sum(r["dynamic_kicks"] for r in kicks)} dynamic kicks in {len(caps)} captures; '
          f'{tex_checked} texture decodes equal the GS freezes; background not armed; ctx +0xA0..+0xFF '
          f'rebuilt by 001D8FD0 from room entry {sorted({r["room_entry"] for r in room})}; cells {count} uids '
          f'({len(cells)} bytes), moved {cells_report["moved_uids"]}')
    return image, caps, lmap


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=A4.OUT)
    ap.add_argument('--iso', type=Path, default=C.ISO_PATH,
                    help="the user's disc image (the level-load GS upload is replayed from it)")
    ap.add_argument('--skip-collision', action='store_true')
    args = ap.parse_args(argv)
    out = args.out.resolve()
    iso = args.iso.resolve()
    el = L.load_export_level()
    elf = C.read_elf()
    A4.read_overlay()                      # pins AREA04.BIN
    pairs = A4.all_captures()
    proof = A4.loaded_sub_proof(pairs)
    for cap, sub in pairs:
        rows = proof[cap.name]
        if sub not in A4.SUBS or not all(rows[sub] * 100 < rows[s] for s in A4.ALL_SUBS if s != sub):
            raise SystemExit(f'{cap.name}: its sub-{sub} map does not fit RAM far better than the others ({rows})')
    out.mkdir(parents=True, exist_ok=True)
    (out / 'loaded_sub_proof.json').write_text(json.dumps(
        {k: {f'sub{s}': v for s, v in r.items()} for k, r in proof.items()}, indent=1) + '\n')
    for sub in A4.SUBS:
        export_sub(sub, el, elf, out, iso, args.skip_collision)
    return 0


if __name__ == '__main__':
    sys.exit(main())
