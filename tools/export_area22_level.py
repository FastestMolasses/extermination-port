#!/usr/bin/env python3
"""Export the AREA22 level: geometry + textures + GS materials, the static
bank, the grid collision, the cell directory, and check the background
state and the render-context bytes 001D8FD0 writes (docs/AREA22_ASSETS.md).

This is export_area01_level.py's pipeline, imported and run unchanged with
export_area01_common pointed at AREA22 (export_area22_common.configure):
the bank walk (001C6120's lookup), the display-list kick check,
export_level.py's record walker / strip builder / texture decode / GS
material code and disc upload replay, export_collision.py --node-class
--verify-ram, the ORIGINAL 001A2370 cell-hull derivation
(verify_cell_directory), the background arm and the ORIGINAL 001D8FD0 ctx
rebuild. From export_area02_level it reuses, by import, run_collision /
grid_extent (export_collision.py over the grid section's resident bytes
only). What is AREA22's own:

  * the load map is the flat one (export_area22_common.build_load_map_flat:
    no nested block; 001FFCD0 state 5 puts the block's resident region at
    D_0028A73C). There is one sub-state, and the files are written under
    assets/area22/sub0/ so that the layout matches the earlier areas;
  * the descriptor's relocation list (id << 24 | offset, applied by
    001FFCD0 state 7 as D_0028A490[id] = D_0028A73C + offset) names the ids
    0x43 (D_0028A59C, the model bank), 0x44 (D_0028A5A0, the static bank),
    0x42 (D_0028A598, the grid), 0x46 (D_0028A5A8, the cell directory),
    0x72, 0x71 and 0x73, and NOT 0x45 (D_0028A5A4, the dynamic list) nor
    0x41 (D_0028A594, the area message bank). Those two words keep the
    AREA04 values (0x197D480, 0x152E740): D_0028A5A4 lies outside the load
    map, so there is no dynamic list (check_kicks runs with an empty list
    and any kernel-0x00237450 kick fails the export, as for AREA02);
  * the load map is labelled by relocation id (chunk26/idXX, the list's
    offsets read resident-relative, as 001FFCD0 state 7 applies them); the
    extracted chunk26/fII_idXX.bin files are only the byte source (their
    names read the offsets as block offsets and are shifted by 0xD9800).
    So the whole static bank (objects 1..480 and object 0) lies in id 0x44
    and there is ONE zone EMDL, 00_id44;
  * the grid (id 0x42, 0x152E500) is followed directly by the cell
    directory (id 0x46, 0x1539D00), so export_collision.py gets the grid
    section alone (export_area02_level.run_collision), which stops its
    cell-list scan from taking the directory for a cell world;
  * the owners that re-transform cell hulls: AREA22's overlay has no owner
    code (its text is the entry pad and the area init, SIXTH_LEVEL_ROUTE.md
    section 2.1), so the callers of 001A2370 live in AREA22 are the boot
    ones: the pickup 0x219550 (node + 0xD0, as in AREA01) and the drum
    0x156620 (node + 0xD0: its state-2 path calls 001A2370(self, self +
    0xD0) at 0x156EF0; NEARMISS C src/func_00156620.c). The drum is in
    `owner_matrix` here (the A04ASSETS lead note: the earlier owner lists
    omit it). The three drums [14]..[16] own hulls 0..2, whose first prim
    carries 0x4000 without the extended bit 0x800 that 001A2370 requires,
    so a knocked drum leaves its hull as loaded; every drum is in state 1
    in every capture. The third boot caller, 0x219F50 (reached only from
    0x219870), passes a scratchpad copy that owner_matrix does not model;
    no AREA22 capture has a live 0x219870 node (the checker fails on one).

Output (ignored assets/area22/, disc-derived, never committed):
  sub0/level/00_id44.emdl (+ .gsmat.json)     geometry, textures, codes
  sub0/level/static_bank.emsc                 *D_0028A5A0
  sub0/level/level.json                       counts, hashes, verification
  sub0/area22.emcl (+ scene.txt)              grid collision
  sub0/area22_cells.bin                       the cell directory (disc bytes)
  sub0/cells.json                             the directory verification

Usage (port root, macOS arm64, pure Python):
  python3 tools/export_area22_level.py
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area22_common as A22  # noqa: E402
import export_area02_level as E02  # noqa: E402  (run_collision, grid_extent; sets AREA02 state on import)

C = A22.configure()                    # after the AREA02 import: AREA22 from here on
L = E02.L                              # export_area01_level

D_0028A490 = 0x28A490                  # the relocation targets, D_0028A490[id]
D_0028A594, D_0028A598, D_0028A59C, D_0028A5A0, D_0028A5A4, D_0028A5A8 = \
    0x28A594, 0x28A598, 0x28A59C, 0x28A5A0, 0x28A5A4, 0x28A5A8
PICKUP = 0x219550
# the boot drum behaviour: its state-2 path calls 001A2370(self, self +
# 0xD0) at 0x156EF0 (original instructions; NEARMISS C src/func_00156620.c)
DRUM = 0x156620
# every behaviour live in AREA22 that passes node + 0xD0 to 001A2370
NODE_D0_OWNERS = (PICKUP, DRUM)


def owner_matrix(ram, node):
    """The matrix address a live AREA22 owner passes to 001A2370 (None: the
    behaviour is not a known caller): node + 0xD0 for the pickup 0x219550
    and the drum 0x156620."""
    if C.u32(ram, node + 0x10) in NODE_D0_OWNERS:
        return node + 0xD0
    return None


def install():
    """Point the AREA01 level module at AREA22 (the import of
    export_area02_level installed AREA02's owner_matrix)."""
    A22.configure()
    L.owner_matrix = owner_matrix      # derive_cell_image reads it by name


install()


def relocations(top=None):
    """{id: offset} of the descriptor's relocation words (+0x1C entries
    after the section table), which 001FFCD0 state 7 applies as
    D_0028A490[id] = D_0028A73C + offset."""
    if top is None:
        top = C.iso_descriptor(C.AREA + 4)
    first, count = C.u16(top, 0x0C), C.u16(top, 0x0E)
    return {i: o for i, o in C._files(top, 0x20 + 8 * (first + count), C.u32(top, 0x1C))}


def relocation_problems(caps, top=None):
    """Every relocated word D_0028A490[id] equals D_0028A73C + offset in
    every capture; returns (problems, {id: address})."""
    rel = relocations(top)
    out, where = [], {}
    for cap in caps:
        base = C.u32(cap.ram, C.D_0028A73C)
        for ident, off in sorted(rel.items()):
            got = C.u32(cap.ram, D_0028A490 + 4 * ident)
            where[ident] = base + off
            if got != base + off:
                out.append(f'{cap.name}: D_0028A490[{ident:#x}] = {got:#x}, not D_0028A73C + {off:#x}')
    return out, where


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


RENDER_SETUP = 0x1D1C50               # per-frame render setup (NEARMISS C, logic recovered)
ROOM_REBUILD = 0x1D8FD0               # room fog block at a room change (committed C)
D_008106C4, D_008106C6, D_008106C7, D_008106C8 = 0x8106C4, 0x8106C6, 0x8106C7, 0x8106C8


def ctx_rebuild(cap, elf, flip=(), setup=True):
    """The render context bytes +0xA0..+0xFF after the ORIGINAL 001D8FD0 and
    then (setup) the ORIGINAL 001D1C50, run over the capture with the block
    overwritten (0x5A) and each (address, mask) of `flip` applied (XOR)."""
    import test_effect_manager_reference as EM
    ram = bytearray(cap.ram)
    ctx = C.u32(ram, L.CTX_PTR)
    ram[ctx + L.CTX_LO:ctx + L.CTX_HI] = bytes([0x5A]) * (L.CTX_HI - L.CTX_LO)
    for at, mask in flip:
        ram[at] ^= mask
    oracle = EM.Oracle(elf, ram, bytearray(cap.spad))
    oracle.run(ROOM_REBUILD, (), stack=0x70003F00)
    if setup:
        oracle.run(RENDER_SETUP, (), stack=0x70003F00)
    return bytes(oracle.ram[ctx + L.CTX_LO:ctx + L.CTX_HI])


def check_ctx_block(caps, elf):
    """The render context bytes +0xA0..+0xFF (three 0x20-byte fog records).
    export_area01_level.check_ctx_room_block runs 001D8FD0 alone; in AREA22
    that fails from door [7] on: spawn entries 1, 3, 4 and 5 carry bit 0x80 in
    word +0x1C (D_008106C8 = 0x108080), and on that bit the per-frame
    001D1C50 (its C: D_008106C4 == 0, D_008106C8 & 0x80, D_008106C7 == 0
    and not (0015D2F0() == 2 and D_008106C6 == 2)) calls 0021B970(0, 50) and
    0021BA80(8, 8, 0x15), which rewrite +0xA0..+0xBF and copy them to +0xC0
    (0021B900), leaving 001D8FD0's +0xE0 record. So the ORIGINAL 001D8FD0
    and then the ORIGINAL 001D1C50 run over each capture with the block
    overwritten, and must rebuild every captured byte; 001D8FD0 alone must
    not (from door [7] on) or must (the arrival: 001D1C50's other branch
    leaves the values); flipping the area's room entry +4..+0x17 must change
    the rebuild. The render_context.emrc blocks are compared with RAM too."""
    import export_render_context as ERC
    emrc = C.ROOT / 'assets/render_context.emrc'
    blocks = [(a, ERC.elf_block(elf, a, n)) for a, n in ERC.BLOCKS]
    emrc_fresh = emrc.exists() and ERC.serialize(blocks) == emrc.read_bytes()
    rows = []
    for cap in caps:
        compared = ERC.verify(blocks, cap.ram)
        ctx = C.u32(cap.ram, L.CTX_PTR)
        want = bytes(cap.ram[ctx + L.CTX_LO:ctx + L.CTX_HI])
        got = ctx_rebuild(cap, elf)
        if got != want:
            diff = [hex(L.CTX_LO + i) for i in range(len(want)) if got[i] != want[i]]
            raise SystemExit(f'{cap.name}: original 001D8FD0 + 001D1C50 do not rebuild ctx +0xA0..+0xFF: '
                             f'{diff[:8]}')
        alone = ctx_rebuild(cap, elf, setup=False)
        by_setup = sum(1 for x, y in zip(alone, want) if x != y)
        key = cap.ram[0x810700] << 8 | cap.ram[0x810701]
        entry = next((k for k in range(L.ROOM_ENTRIES) if C.u32(cap.ram, L.ROOM_TABLE + L.ROOM_ENTRY * k) == key),
                     None)
        if entry is None:
            raise SystemExit(f'{cap.name}: no room entry {key:#x}')
        at = L.ROOM_TABLE + L.ROOM_ENTRY * entry
        changed = sum(1 for x, y in zip(ctx_rebuild(cap, elf, [(a, 0x40) for a in range(at + 4, at + 0x18)]), want)
                      if x != y)
        flag = C.u32(cap.ram, D_008106C8)
        by_flag = sum(1 for x, y in zip(ctx_rebuild(cap, elf, [(D_008106C8, 0x80)]), want) if x != y)
        by_c7 = sum(1 for x, y in zip(ctx_rebuild(cap, elf, [(D_008106C7, 1)]), want) if x != y)
        if flag & 0x80:
            # the constant branches of both routines: the room entry is not
            # read, and the bit and 001D1C50's D_008106C7 test decide
            if changed or not by_flag or not by_c7:
                raise SystemExit(f'{cap.name}: bit 0x80 set, but the rebuild depends on room entry {entry} '
                                 f'({changed}) / not on the bit ({by_flag}) / not on D_008106C7 ({by_c7})')
        elif not changed or by_setup:
            raise SystemExit(f'{cap.name}: the rebuild does not read room entry {entry} (key {key:#x}), or '
                             f'001D1C50 changes {by_setup} bytes of 001D8FD0\'s')
        rows.append(dict(capture=cap.name, emrc_bytes_equal=compared, ctx_bytes_rebuilt=L.CTX_HI - L.CTX_LO,
                         d_008106c8=hex(flag), bit_0x80=bool(flag & 0x80),
                         bytes_001d1c50_changes=by_setup, room_entry=entry, room_key=hex(key),
                         bytes_changed_by_room_entry=changed, bytes_changed_by_bit_0x80=by_flag,
                         bytes_changed_by_d_008106c7=by_c7, emrc_asset_equals_blocks=emrc_fresh,
                         d_008106c4_c6_c7=[cap.ram[D_008106C4], cap.ram[D_008106C6], cap.ram[D_008106C7]]))
    return rows


def drum_rows(caps, hulls, disc):
    """Per capture, the live drums: node, uid, state, and the extended bit
    0x800 of their hull's first prim (+0x18 + 4)."""
    out = []
    for cap in caps:
        for slot in range(L.POOL_SLOTS):
            a = L.POOL_BASE + slot * L.POOL_STRIDE
            if cap.ram[a] and C.u32(cap.ram, a + 0x10) == DRUM:
                uid = cap.ram[a + 0x0F]
                s = hulls[uid][0] if uid in hulls else None
                out.append(dict(capture=cap.name, node=hex(a), uid=uid, state=cap.ram[a + 4],
                                hull_flags=hex(C.u16(disc, s + 0x18 + 4)) if s is not None else None,
                                extended_bit=bool(s is not None and C.u16(disc, s + 0x18 + 4) & 0x800)))
    return out


def export(el, elf, out_root, iso, skip_collision):
    """Level, static bank, collision, cells, background and ctx block."""
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
    rproblems, rwhere = relocation_problems(caps)
    if rproblems:
        raise SystemExit('relocations: ' + '; '.join(rproblems[:4]))
    for ident in (0x41, 0x45):
        if ident in rwhere:
            raise SystemExit(f'the descriptor relocates id {ident:#x}; the stale-pointer model does not apply')
    cells_table = C.u32(caps[0].ram, D_0028A5A8)
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

    bank = C.u32(caps[0].ram, D_0028A5A0)
    dyn_base = C.u32(caps[0].ram, D_0028A5A4)
    for cap in caps:
        if (C.u32(cap.ram, D_0028A5A0), C.u32(cap.ram, D_0028A5A4)) != (bank, dyn_base):
            raise SystemExit(f'{cap.name}: bank pointers differ')
    if image.locate(dyn_base) is not None:
        raise SystemExit(f'D_0028A5A4 = {dyn_base:#x} lies in the load map; a dynamic list is not modelled')
    objects = L.bank_objects(image.read, bank)
    kicks, states, touched = L.check_kicks(caps, objects, [])
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

    out = out_root / 'sub0'
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
        print(f'sub0/level/{path.name}: {len(oids)} objects, {len(b.pos)} verts, '
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
    room = check_ctx_block(caps, elf)

    table, count, hulls, cells, cell_rows = check_cells(image, caps, elf)
    (out / 'area22_cells.bin').write_bytes(cells)
    drums = drum_rows(caps, hulls, cells)
    if any(d['extended_bit'] for d in drums):
        raise SystemExit('a drum hull carries the extended bit 0x800; the at-rest model does not apply')
    cells_report = dict(address=hex(table), count=count, bytes=len(cells), sha256=C.sha(cells),
                        flag_uids={u: f for u, (_s, _e, f) in hulls.items() if f},
                        moved_uids=sorted({u for r in cell_rows for u in r['moved_hulls']}),
                        drums=drums, captures=cell_rows)
    coll = None
    if not skip_collision:
        files, grid_hdr, log = E02.run_collision(image, caps, out / 'area22.emcl', C.SCRATCH)
        coll = dict(files=files, grid_header=hex(grid_hdr), log=log.strip().splitlines(),
                    sha256=C.sha((out / 'area22.emcl').read_bytes()))
        print(f'sub0/area22.emcl: {files}; ' + ' '.join(l for l in log.splitlines() if l.startswith('wrote')))
    (out / 'cells.json').write_text(json.dumps(dict(cells=cells_report, collision=coll), indent=1,
                                               default=str) + '\n')

    report = dict(
        area=C.AREA, sub=0, captures=[c.name for c in caps],
        excluded_captures=[dict(capture=n, area_bytes=list(a)) for n, a in A22.excluded_captures()],
        load_map=[dict(address=hex(a), id=l, bytes=s, extracted_source=p.name, source_offset=hex(o))
                  for a, p, o, s, l in lmap],
        load_info={k: (hex(v) if isinstance(v, int) else v) for k, v in info.items()},
        load_map_bytes_equal=load_rows, descriptor_buffer=buffers,
        relocations={hex(k): hex(v) for k, v in sorted(rwhere.items())},
        stale_pointers=dict(d_0028a594=hex(C.u32(caps[0].ram, D_0028A594)), d_0028a5a4=hex(dyn_base),
                            note='ids 0x41 and 0x45 are not in the descriptor relocation list; both words '
                                 'keep the previous area\'s values'),
        static_bank=dict(address=hex(bank), objects=len(objects), grid=hex(grid), span=[hex(lo), hex(hi)],
                         sha256=C.sha(bank_bytes)),
        dynamic_list=dict(d_0028a5a4=hex(dyn_base), drawn=False,
                          note='D_0028A5A4 lies outside the load map (not relocated at the AREA22 load); no '
                               'capture draws a kernel-0x00237450 kick'),
        kicks=kicks, distinct_level_refs=touched,
        level_state=dict(prim=hex(prim), test_1=hex(el.GS_CLASS0_TEST), alpha_1=hex(el.GS_CLASS0_ALPHA),
                         tex1_1=hex(el.GS_CLASS0_TEX1), clamp_1=hex(el.GS_CLASS0_CLAMP)),
        textures=dict(source='level-load GS upload replayed from the disc image', transfers=transfers,
                      sections=sections, texture_comparisons_with_captures=tex_checked),
        zones=zone_rows, background=background, ctx_room_block=room)
    (level / 'level.json').write_text(json.dumps(report, indent=1, default=str) + '\n')
    print(f'static bank {bank:#x}: {len(objects)} objects -> {len(zone_rows)} zone EMDLs; '
          f'{sum(r["level_kicks"] for r in kicks)} level kicks in {len(caps)} captures inside the bank; '
          f'{tex_checked} texture decodes equal the GS freezes; background not armed; ctx +0xA0..+0xFF '
          f'rebuilt by 001D8FD0 + 001D1C50 from room entry {sorted({r["room_entry"] for r in room})}; cells {count} uids '
          f'({len(cells)} bytes), moved {cells_report["moved_uids"]}; drums '
          f'{sorted({(d["uid"], d["state"], d["hull_flags"]) for d in drums})}')
    return image, caps, lmap


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=A22.OUT)
    ap.add_argument('--iso', type=Path, default=C.ISO_PATH,
                    help="the user's disc image (the level-load GS upload is replayed from it)")
    ap.add_argument('--skip-collision', action='store_true')
    args = ap.parse_args(argv)
    out = args.out.resolve()
    iso = args.iso.resolve()
    el = L.load_export_level()
    elf = C.read_elf()
    A22.read_overlay()                     # pins AREA22.BIN
    out.mkdir(parents=True, exist_ok=True)
    export(el, elf, out, iso, args.skip_collision)
    return 0


if __name__ == '__main__':
    sys.exit(main())
