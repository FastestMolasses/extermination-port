#!/usr/bin/env python3
"""Export the AREA00 level for both sub-states the route loads: geometry +
textures + GS materials, the static bank, the grid
collision, the cell directory, and check the background state and the
render-context bytes 001D8FD0 writes (docs/AREA00_ASSETS.md).

This is export_area01_level.py's pipeline, imported and run unchanged with
export_area01_common pointed at AREA00 (export_area00_common.configure):
the load map from the loader's descriptors and cursors, the bank walk
(001C6120's lookup), the display-list kick check, export_level.py's record
walker / strip builder / texture decode / GS material code and disc upload
replay, export_collision.py --node-class --verify-ram, the ORIGINAL
001A2370 cell-hull derivation, the background arm and the ORIGINAL 001D8FD0
ctx rebuild. Only the AREA00 differences are new here:

  * no dynamic list: 001D5370 walks *D_0028A5A4 (001D5BD0) only for the
    stages its switch names, and 0x0000 / 0x0001 are not among them; no
    capture draws a kernel-0x00237450 kick (check_kicks is run with an
    empty list, so one would fail the export);

  * two sub-states. Sub 0 (chunk04.n0: the arrival and a00_00..a00_07) and
    sub 1 (chunk04.n1: a00_08 and a00_09, after the switch). n0 and n1
    differ only in their last file (id 0x44), and in RAM the two loads
    differ from 0x15CE380 on: the static banks are different, so each sub
    gets its own zone EMDLs and bank (assets/area00/sub<N>/level/). The grid, the cell directory and the
    model bank lie below 0x15CE380; the exporter requires their bytes to
    be equal in both loads and exports them once;
  * the owners that re-transform cell hulls in AREA00 and the matrix each
    passes to 001A2370, read from their committed byte-identical C:
    0x825600 (func_overlay_AREA00_008255C0, four call sites) and 0x8263C0
    (func_overlay_AREA00_00826380): self + 0xD0; 0x219550 (pickup): node
    + 0xD0, as in AREA01 (export_area01_level.owner_matrix);
  * the directory's uid words. After the switch the uid 1 word carries
    bit 30 (0x80000228 -> 0xC0000228). The shaft door 0x823580
    (overlay_AREA00_func_00823540.c, byte-identical C) calls
    0019C6F0(2, 0) when D_0081075D is 0xFF, and 0019C6F0(2, 0), (0, 1)
    when D_0081075E is 0xFF (state 1, talk step 0). The ORIGINAL 0019C6F0
    runs in the EE interpreter over each capture's RAM with the directory
    set to the disc bytes, for exactly the calls that capture's two flag
    bytes select; the result must be the capture's uid words. The export
    keeps the disc words (the load-time image); the port re-applies the
    calls at run time through the translated 0019C6F0.

Output (ignored assets/area00/, disc-derived, never committed):
  sub<N>/level/NN_<file>.emdl (+ .gsmat.json)   geometry, textures, codes
  sub<N>/level/static_bank.emsc                 *D_0028A5A0
  sub<N>/level/level.json                       counts, hashes, verification
  area00.emcl                                   grid collision
  area00_cells.bin                              the cell directory (disc bytes)
  cells.json                                    the directory verification

Usage (port root, macOS arm64, pure Python):
  python3 tools/export_area00_level.py
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area00_common as A0  # noqa: E402

C = A0.configure(0)
import export_area01_level as L  # noqa: E402

D_0028A598, D_0028A59C, D_0028A5A0, D_0028A5A4, D_0028A5A8 = 0x28A598, 0x28A59C, 0x28A5A0, 0x28A5A4, 0x28A5A8
SUB_SPLIT = 0x15CE380            # the first resident address where the n0 and n1 loads differ

# 0019C6F0 (directory uid-word flag setter, NEARMISS C, logic faithful; the
# ORIGINAL instructions run here) and the shaft door that calls it
FLAG_SETTER = 0x19C6F0
FLAG_SETTER_CODE = ((0x19C6F0, 0x140),)
SHAFT_DOOR = 0x823580
D_0081075D, D_0081075E = 0x81075D, 0x81075E


def owner_matrix(ram, node):
    """The matrix address a live AREA00 owner passes to 001A2370 (None: the
    behaviour is not a known caller). From the committed C: 0x825600 and
    0x8263C0 call func_001A2370(self, self + 0xD0); 0x219550 passes node +
    0xD0 (export_area01_level.owner_matrix, NEARMISS C line 81)."""
    if C.u32(ram, node + 0x10) in (0x219550, 0x825600, 0x8263C0):
        return node + 0xD0
    return None


L.owner_matrix = owner_matrix          # derive_cell_image reads it by name


def door_calls(ram):
    """The 0019C6F0 calls the shaft door's talk step 0 makes, by its C
    (overlay_AREA00_func_00823540.c, state 1, +5 == 0), for this capture's
    D_0081075E / D_0081075D."""
    if ram[D_0081075E] == 0xFF:
        return ((2, 0), (0, 1))
    if ram[D_0081075D] == 0xFF:
        return ((2, 0),)
    return ()


def door_live(ram):
    return any(C.u32(ram, a + 0x10) == SHAFT_DOOR for _s, a in pool(ram))


def pool(ram):
    return [(s, L.POOL_BASE + s * L.POOL_STRIDE) for s in range(L.POOL_SLOTS)
            if ram[L.POOL_BASE + s * L.POOL_STRIDE]]


_WORDS = {}


def derive_words(elf, disc, cap):
    """(directory image, calls): the disc directory after the ORIGINAL
    0019C6F0 ran, in the EE interpreter over the capture's RAM and
    scratchpad with the directory set to `disc`, for the calls
    door_calls() selects. Raises ValueError when the calls are not empty
    and no shaft-door node is live, or when a call changes a byte outside
    the uid words, or when 0019C6F0's code in RAM differs from the ELF."""
    from test_coll_move_reference import FloatEE
    calls = door_calls(cap.ram)
    if not calls:
        return disc, calls
    if not door_live(cap.ram):
        raise ValueError(f'{cap.name}: flag calls {calls} without a live shaft door')
    for a, n in FLAG_SETTER_CODE:
        at = a - C.ELF_VADDR + C.ELF_OFFSET
        if cap.ram[a:a + n] != elf[at:at + n]:
            raise ValueError(f'{cap.name}: 0019C6F0 code in RAM differs from the ELF')
    key = (id(elf), id(cap.ram), id(cap.spad), disc, calls)
    hit = _WORDS.get(key)
    if hit is None:
        table = C.u32(cap.spad, L.SPAD_CELLS)
        ee = FloatEE(elf, cap.ram, cap.spad)
        ee.write(table, disc)
        for args in calls:
            ee.call(FLAG_SETTER, args)
        got = ee.read(table, len(disc))
        words = 4 + 4 * C.u32(disc, 0)
        if got[words:] != disc[words:]:
            raise ValueError(f'{cap.name}: 0019C6F0 changed a byte outside the uid words')
        hit = _WORDS[key] = (got, elf, cap.ram, cap.spad)
    return hit[0], calls


def verify_directory(elf, disc, caps):
    """Every byte of every capture's directory: the uid words through
    derive_words, then (export_area01_level.verify_cell_directory, per group
    of captures with the same derived words) the hulls, each equal to the
    disc or to the ORIGINAL 001A2370 derivation for its live owner.
    Returns (rows, problems, {capture: calls})."""
    groups, calls, problems = {}, {}, []
    for cap in caps:
        try:
            image, used = derive_words(elf, disc, cap)
        except ValueError as error:
            problems.append(str(error))
            continue
        calls[cap.name] = used
        groups.setdefault(image, []).append(cap)
    rows = []
    for image, group in groups.items():
        r, p = L.verify_cell_directory(elf, image, group)
        rows += r
        problems += p
    order = [c.name for c in caps]
    rows.sort(key=lambda r: order.index(r['capture']))
    return rows, problems, calls


def check_cells(image, caps, elf):
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
    rows, problems, calls = verify_directory(elf, disc, caps)
    if problems:
        raise SystemExit('cell directory: ' + '; '.join(problems))
    return table, count, hulls, disc, rows, calls


def export_sub(sub, el, elf, out_root, iso, report_shared):
    """Level, static bank, background and ctx block of one sub."""
    A0.configure(sub)
    caps = C.captures()
    lmap, info = C.build_load_map(caps[0])
    image = C.LoadedImage(lmap)
    buffers = {caps[0].name: info['rambuf']}
    for cap in caps[1:]:
        other, oinfo = C.build_load_map(cap)
        if other != lmap:
            raise SystemExit(f'{cap.name}: a different load map')
        buffers[cap.name] = oinfo['rambuf']
    if info['nested'] != f'chunk04.n{sub}':
        raise SystemExit(f'sub {sub}: nested block {info["nested"]}')
    cells_table = C.u32(caps[0].ram, D_0028A5A8)
    hit = image.locate(cells_table)
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
    objects = L.bank_objects(image.read, bank)
    # 001D5370 walks the dynamic list (001D5BD0) only for the stages its
    # switch names; 0x0000 / 0x0001 are not among them. D_0028A5A4 still
    # holds AREA01's list address (0x177A940, word 0 not a count here).
    # With no list, check_kicks refuses any kernel-0x00237450 kick, so the
    # captures must show none.
    dyn = []
    kicks, states, touched = L.check_kicks(caps, objects, dyn)
    if any(k['dynamic_kicks'] for k in kicks):
        raise SystemExit('a capture draws a dynamic-list kick')
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
    for old in level.glob('*.emdl'):
        old.unlink()
    for old in level.glob('*.gsmat.json'):
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
    stale = level / 'dynamic_objects.emsc'
    if stale.exists():
        stale.unlink()

    background = L.check_background(caps)
    if any(r['armed'] for r in background):
        raise SystemExit('a capture arms the background; run export_level.py --background for it')
    room = L.check_ctx_room_block(caps, elf)

    report = dict(
        area=C.AREA, sub=sub, captures=[c.name for c in caps],
        load_map=[dict(address=hex(a), file=l, file_offset=hex(o), bytes=s) for a, _p, o, s, l in lmap],
        load_info={k: (hex(v) if isinstance(v, int) else v) for k, v in info.items()},
        load_map_bytes_equal=load_rows, descriptor_buffer=buffers,
        static_bank=dict(address=hex(bank), objects=len(objects), grid=hex(grid), span=[hex(lo), hex(hi)],
                         sha256=C.sha(bank_bytes)),
        dynamic_list=dict(d_0028a5a4=hex(dyn_base), drawn=False,
                          note='stage 0x000N is not in 001D5370\'s dynamic-pass switch; no capture draws a '
                               'kernel-0x00237450 kick; D_0028A5A4 is the AREA01 value'),
        kicks=kicks, distinct_level_refs=touched,
        level_state=dict(prim=hex(prim), test_1=hex(el.GS_CLASS0_TEST), alpha_1=hex(el.GS_CLASS0_ALPHA),
                         tex1_1=hex(el.GS_CLASS0_TEX1), clamp_1=hex(el.GS_CLASS0_CLAMP)),
        textures=dict(source='level-load GS upload replayed from the disc image', transfers=transfers,
                      sections=sections, texture_comparisons_with_captures=tex_checked),
        zones=zone_rows, background=background, ctx_room_block=room, shared=report_shared)
    (level / 'level.json').write_text(json.dumps(report, indent=1, default=str) + '\n')
    print(f'sub {sub}: static bank {bank:#x}: {len(objects)} objects -> {len(zone_rows)} zone EMDLs; '
          f'{sum(r["level_kicks"] for r in kicks)} level kicks in {len(caps)} captures inside the bank; '
          f'{tex_checked} texture decodes equal the GS freezes; background not armed; ctx +0xA0..+0xFF '
          f'rebuilt by 001D8FD0 from room entry {sorted({r["room_entry"] for r in room})}')
    return image, caps, lmap


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=A0.OUT)
    ap.add_argument('--iso', type=Path, default=C.ISO_PATH,
                    help="the user's disc image (the level-load GS upload is replayed from it)")
    ap.add_argument('--skip-collision', action='store_true')
    args = ap.parse_args(argv)
    out = args.out.resolve()
    iso = args.iso.resolve()
    el = L.load_export_level()
    elf = C.read_elf()
    A0.read_overlay()                      # pins AREA00.BIN
    pairs = A0.all_captures()
    proof = A0.loaded_sub_proof(pairs)
    for cap, sub in pairs:
        rows = proof[cap.name]
        if not rows[sub] * 100 < rows[1 - sub]:
            raise SystemExit(f'{cap.name}: its sub-{sub} map does not fit RAM far better than the other ({rows})')
    shared = dict(loaded_sub_proof={k: {f'sub{s}': v for s, v in r.items()} for k, r in proof.items()})
    images = {}
    for sub in A0.SUBS:
        images[sub] = export_sub(sub, el, elf, out, iso, shared)

    # Shared below SUB_SPLIT: the grid, the directory and the model bank.
    A0.configure(0)
    image0, caps0, _ = images[0]
    image1, caps1, _ = images[1]
    caps = caps0 + caps1
    grid = C.u32(caps0[0].ram, D_0028A598)
    table = C.u32(caps0[0].ram, D_0028A5A8)
    models = C.u32(caps0[0].ram, D_0028A59C)
    for cap in caps:
        if (C.u32(cap.ram, D_0028A598), C.u32(cap.ram, D_0028A5A8), C.u32(cap.ram, D_0028A59C)) != \
                (grid, table, models):
            raise SystemExit(f'{cap.name}: grid / directory / model bank pointer differs')
    lo = min(a for a, *_ in image0.map if a >= 0x13E0B80)
    if image0.read(lo, SUB_SPLIT - lo) != image1.read(lo, SUB_SPLIT - lo):
        raise SystemExit('the n0 and n1 loads differ below 0x15CE380')
    if image0.read(SUB_SPLIT, 16) == image1.read(SUB_SPLIT, 16):
        raise SystemExit('the n0 and n1 loads do not differ at 0x15CE380')
    for name, addr in (('grid', grid), ('cell directory', table), ('model bank', models)):
        if not lo <= addr < SUB_SPLIT:
            raise SystemExit(f'{name} {addr:#x} not in the shared range')

    table, count, hulls, cells, cell_rows, calls = check_cells(image0, caps, elf)
    (out / 'area00_cells.bin').write_bytes(cells)
    cells_report = dict(address=hex(table), count=count, bytes=len(cells), sha256=C.sha(cells),
                        flag_uids={u: f for u, (_s, _e, f) in hulls.items() if f},
                        moved_uids=sorted({u for r in cell_rows for u in r['moved_hulls']}),
                        flag_calls={k: [list(c) for c in v] for k, v in calls.items()},
                        captures=cell_rows)
    coll = None
    if not args.skip_collision:
        files, grid_hdr, log = L.run_collision(image0, caps, out / 'area00.emcl')
        coll = dict(files=files, grid_header=grid_hdr, log=log.strip().splitlines(),
                    sha256=C.sha((out / 'area00.emcl').read_bytes()))
        print(f'area00.emcl: {files}; ' + ' '.join(l for l in log.splitlines() if l.startswith('wrote')))
    (out / 'cells.json').write_text(json.dumps(dict(cells=cells_report, collision=coll), indent=1,
                                               default=str) + '\n')
    print(f'cells {count} uids ({len(cells)} bytes), moved {cells_report["moved_uids"]}, '
          f'flag calls {sorted({tuple(map(tuple, v)) for v in cells_report["flag_calls"].values()})} '
          f'over {len(caps)} captures')
    return 0


if __name__ == '__main__':
    sys.exit(main())
