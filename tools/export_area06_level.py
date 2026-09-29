#!/usr/bin/env python3
"""Export the AREA06 level of the sub the route loads (sub 0): geometry +
textures + GS materials, the static bank, the dynamic-object list, the grid
collision, the cell directory, and check the background state and the
render-context bytes 001D8FD0 / 001D1C50 write (docs/AREA06_ASSETS.md).

This is export_area01_level.py's pipeline, imported and run unchanged with
export_area01_common pointed at AREA06 (export_area06_common.configure):
the bank walk (001C6120's lookup), the dynamic-list walk, the display-list
kick check, export_level.py's record walker / strip builder / texture decode
/ GS material code and disc upload replay, export_collision.py --node-class
--verify-ram, the ORIGINAL 001A2370 cell-hull derivation (derive_cell_image)
and the background arm. From export_area02_level it reuses, by import,
run_collision / grid_extent (export_collision.py over the grid section
only), derive_words (the ORIGINAL 0019C6F0 over the capture, with AREA06's
flag_calls installed) and orphan_problems / written_words; from
export_area22_level check_ctx_block (the ORIGINAL 001D8FD0 and then the
ORIGINAL 001D1C50) and drum_rows. What is AREA06's own:

  * the load map is labelled by relocation id (export_area06_common.
    build_load_map_ids, AREA22_ASSETS.md finding 8). The static bank
    D_0028A5A0 is the start of id 0x44, so the bank is ONE zone EMDL
    (00_id44), whatever extracted files hold its bytes; every relocated word
    D_0028A490[id] of both lists equals its cursor + offset in every capture
    (relocation_problems);
  * a dynamic list: D_0028A5A4 = relocation id 0x45, drawn by
    kernel-0x00237450 kicks; level/dynamic_objects.emsc is written as
    AREA04's;
  * the owners that re-transform cell hulls, all read from original code:
    the pickup 0x219550 and the drum 0x156620 (node + 0xD0, as in AREA22),
    the beam 0x824560 (func_overlay_AREA06_00824520, byte-identical C: its
    three 001A2370 calls pass self + 0xD0), and the node 0x219870 (g[11]),
    whose hull is re-transformed once, at its spawn, by 0x219F50 with the
    scratchpad copy 0x700036A0 of node + 0xD0 whose third row is scaled by a
    length 0x219F50 computes (byte-matched C src/func_00219F50.c). That one is
    derived by running the ORIGINAL 0x219F50 on the node in the EE
    interpreter over the capture (derive_scaled_hull), not by a matrix
    model;
  * the uid words: [9] (0x823580, overlay_AREA06_func_00823540.c,
    byte-identical C) calls 0019C6F0(0xD, 1) when its sub-state +5 goes to
    1 and 0019C6F0(0xD, 0) when it goes back to 0 (its only calls; the ELF
    has none). flag_calls() gives the last call from the live node's +5, and
    the ORIGINAL 0019C6F0 runs it over the capture (export_area02_level.
    derive_words);
  * the beam's hull after the collapse: the beam reloads its pose and takes
    uid 27 (+0x0F 0x1C -> 0x1B, a06_04 on), whose hull lacks the extended
    bit 0x800 001A2370 requires, so hull 28 keeps the bytes of the beam's
    last falling frame with no live owner. It is accepted only under
    export_area02_level.orphan_problems (another capture derives hull 28
    from the live beam, every differing word is one the ORIGINAL 001A2370
    writes, and the bytes are equal in every capture that orphans it); a
    known gap (the falling pose is not in any capture).

Output (ignored assets/area06/, disc-derived, never committed):
  sub0/level/00_id44.emdl (+ .gsmat.json)     geometry, textures, codes
  sub0/level/static_bank.emsc                 *D_0028A5A0
  sub0/level/dynamic_objects.emsc             *D_0028A5A4
  sub0/level/level.json                       counts, hashes, verification
  sub0/area06.emcl (+ scene.txt)              grid collision
  sub0/area06_cells.bin                       the cell directory (disc bytes)
  sub0/cells.json                             the directory verification
  loaded_sub_proof.json                       per capture, rows of each nested map

Usage (port root, macOS arm64, pure Python):
  python3 tools/export_area06_level.py
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area06_common as A6  # noqa: E402  (first: it keeps AREA01's nested load-map builder)
import export_area02_level as E02  # noqa: E402  (sets AREA02 state on import)
import export_area22_level as E22  # noqa: E402  (sets AREA22 state on import)

C = A6.configure(0)                    # after the imports: AREA06 from here on
L = E02.L                              # export_area01_level

D_0028A594, D_0028A598, D_0028A59C, D_0028A5A0, D_0028A5A4, D_0028A5A8 = \
    0x28A594, 0x28A598, 0x28A59C, 0x28A5A0, 0x28A5A4, 0x28A5A8
PICKUP = 0x219550
# the boot drum behaviour: its state-2 path calls 001A2370(self, self +
# 0xD0) at 0x156EF0 (original instructions; NEARMISS C src/func_00156620.c)
DRUM = 0x156620
# the beam [11] (func_overlay_AREA06_00824520, byte-identical C, runtime
# 0x824560): its three 001A2370 calls pass self + 0xD0
BEAM = 0x824560
HULL_OWNERS = (BEAM,)
NODE_D0_OWNERS = HULL_OWNERS + (PICKUP, DRUM)
# 0x219870 calls 0x219F50 in its state 0 only (NEARMISS C src/func_00219870.c);
# 0x219F50 calls 001A2370(self, 0x700036A0) with a scaled copy of self + 0xD0
SCALED_OWNER, SCALED_CALLER = 0x219870, 0x219F50
# [9], func overlay_AREA06_func_00823540 (runtime 0x823580): 0019C6F0(0xD, 1)
# on entering sub-state 1, 0019C6F0(0xD, 0) on returning to sub-state 0
FLAG_OWNER, FLAG_KEY = 0x823580, 0xD


def owner_matrix(ram, node):
    """The matrix address a live AREA06 owner passes to 001A2370 (None: the
    behaviour is not a matrix-address caller): node + 0xD0 for the pickup
    0x219550, the drum 0x156620 and the beam 0x824560. 0x219870 is None
    here: its matrix is a scaled scratchpad copy made inside 0x219F50, which
    derive_scaled_hull runs whole."""
    if C.u32(ram, node + 0x10) in NODE_D0_OWNERS:
        return node + 0xD0
    return None


def flag_calls(ram):
    """The 0019C6F0 call that decides the uid words of this AREA06 sub-0
    load, from [9]'s byte-identical C: its sub-state +5 is set to 1 in the
    frame it calls (0xD, 1) and back to 0 in the frame it calls (0xD, 0).
    So a live [9] in state 1 with +5 = 1 made (0xD, 1) last; with +5 = 0 it
    made (0xD, 0) last or no call yet, and (0xD, 0) only sets bit 30, which
    the disc word already carries (derive_flag_image checks that the two
    readings give the same image). Not complete: [9]'s state 0 (its spawn)
    sets +5 = 0 without calling 0019C6F0, so a respawn after (0xD, 1) in the
    same load leaves bit 30 clear with +5 = 0; this model then derives the
    disc word and the checker reports a directory mismatch (it fails
    closed). No capture has that history. Any other state of the node:
    ValueError. Outside AREA06 sub 0: no call."""
    if ram[0x810700] != A6.AREA or ram[0x810701] != 0:
        return ()
    nodes = [a for _s, a in E02.pool(ram) if C.u32(ram, a + 0x10) == FLAG_OWNER]
    if len(nodes) != 1 or ram[nodes[0] + 4] != 1 or ram[nodes[0] + 5] not in (0, 1):
        raise ValueError(f'[9] (0x823580) is not one live node in state 1 with sub-state 0 or 1: '
                         f'{[(hex(a), ram[a + 4], ram[a + 5]) for a in nodes]}')
    return ((FLAG_KEY, ram[nodes[0] + 5]),)


def install():
    """Point the AREA01 level module and export_area02_level's word
    derivation at AREA06 (the imports installed AREA02's and AREA22's)."""
    A6.configure(0)
    L.owner_matrix = owner_matrix      # derive_cell_image reads it by name
    E02.flag_calls = flag_calls        # derive_words reads it by name


install()


def derive_flag_image(elf, disc, cap):
    """(image, calls): the uid words after the ORIGINAL 0019C6F0 ran the
    call flag_calls() gives (export_area02_level.derive_words). With +5 = 0
    the image must also equal the disc (no call made yet): ValueError when
    it does not."""
    install()
    image, calls = E02.derive_words(elf, disc, cap)
    if calls and calls[0][1] == 0 and image != disc:
        raise ValueError(f'{cap.name}: 0019C6F0(0xD, 0) changes the disc directory; "no call yet" and '
                         '"(0xD, 0) last" disagree')
    return image, calls


_SCALED = {}


def derive_scaled_hull(elf, disc, cap, node, hulls):
    """The directory the ORIGINAL 0x219F50 leaves when it runs on `node` (a
    live 0x219870 node) in the EE interpreter over the capture's RAM and
    scratchpad, with the directory set to `disc`: its hull bytes, or b''
    when the run touched another hull or the node's uid has no hull."""
    from test_coll_move_reference import FloatEE
    uid = cap.ram[node + 0x0F]
    if uid not in hulls:
        return b''
    s, e, _f = hulls[uid]
    key = (id(elf), id(cap.ram), id(cap.spad), node, disc, s, e)
    hit = _SCALED.get(key)
    if hit is None:
        table = C.u32(cap.spad, L.SPAD_CELLS)
        ee = FloatEE(elf, cap.ram, cap.spad)
        ee.write(table, disc)
        ee.call(SCALED_CALLER, (node,))
        got = ee.read(table, len(disc))
        hit = _SCALED[key] = (got[s:e] if got[:s] + got[e:] == disc[:s] + disc[e:] else b'', elf, cap.ram, cap.spad)
    return hit[0]


SCALED_CODE = ((0x219F50, 0x230),)    # 0x219F50's own instructions must be the ELF's in RAM


def verify_directory(elf, disc, caps):
    """Every byte of every capture's directory: the uid words through the
    ORIGINAL 0019C6F0 (derive_flag_image), then per capture the hulls
    (export_area01_level.derive_cell_image with AREA06's owner_matrix), a
    hull of a live 0x219870 node through the ORIGINAL 0x219F50
    (derive_scaled_hull), an orphaned hull equal to a derivation of it in
    another capture or accepted by export_area02_level.orphan_problems.
    Returns (rows, problems, {capture: calls})."""
    install()
    count, hulls, size = L.cell_directory(disc, 0)
    rows, problems, calls, derived, orphans, pending = [], [], {}, {}, [], []
    for cap in caps:
        table = C.u32(cap.spad, L.SPAD_CELLS)
        if size != len(disc) or C.s16(cap.spad, L.SPAD_CELL_COUNT) != count:
            problems.append(f'{cap.name}: directory count/size')
            continue
        if not L.retransform_code_equal(elf, cap.ram) or not all(
                cap.ram[a:a + n] == elf[a - C.ELF_VADDR + C.ELF_OFFSET:a - C.ELF_VADDR + C.ELF_OFFSET + n]
                for a, n in SCALED_CODE):
            problems.append(f'{cap.name}: 001A2370 / 0x219F50 code in RAM differs from the ELF')
            continue
        try:
            image, used = derive_flag_image(elf, disc, cap)
        except ValueError as error:
            problems.append(str(error))
            continue
        calls[cap.name] = used
        img = cap.ram[table:table + size]
        expected, proofs = L.derive_cell_image(elf, image, cap, hulls)
        expected = bytearray(expected)
        for uid, (kind, node, behaviour) in sorted(proofs.items()):
            s, e, _f = hulls[uid]
            if kind == 'underived':
                for n in [a for _s, a in E02.pool(cap.ram) if cap.ram[a + 0x0F] == uid
                          and C.u32(cap.ram, a + 0x10) == SCALED_OWNER]:
                    hull = derive_scaled_hull(elf, image, cap, n, hulls)
                    if hull and hull == img[s:e]:
                        expected[s:e] = hull
                        kind, node, behaviour = proofs[uid] = ('scaled', n, SCALED_OWNER)
                        break
            if kind in ('derived', 'scaled'):
                derived.setdefault(uid, set()).add(img[s:e])
            elif kind == 'orphan':
                orphans.append((cap.name, uid, img[s:e]))
                pending.append((cap, uid, img[s:e], image))
                expected[s:e] = img[s:e]                               # proven below
            else:
                problems.append(f'{cap.name}: hull {uid} differs and no owner derivation reproduces it')
        bad = [k for k in range(size) if expected[k] != img[k]]
        if bad:
            problems.append(f'{cap.name}: directory bytes differ at {[hex(k) for k in bad[:8]]}')
        rows.append(dict(capture=cap.name, moved_hulls=sorted(proofs),
                         proofs={u: (f'001A2370(node {n:#x}, behaviour {b:#x})' if k == 'derived' else
                                     f'0x219F50(node {n:#x})' if k == 'scaled' else k)
                                 for u, (k, n, b) in sorted(proofs.items())}))
    for cap, uid, data, image in pending:
        row = next(r for r in rows if r['capture'] == cap.name)
        if data in derived.get(uid, set()):
            row['proofs'][uid] = 'orphan: equal to a derivation of it in another capture'
            continue
        more = E02.orphan_problems(elf, cap, uid, data, image, hulls, derived, orphans)
        problems += more
        if not more:
            row['proofs'][uid] = 'orphan: 001A2370 output words only, equal in every orphaning capture'
    return rows, problems, calls


def check_cells(image, caps, elf):
    """(table, count, hulls, disc bytes, rows, calls): the directory's disc
    bytes and verify_directory over every capture."""
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
    rows, problems, calls = verify_directory(elf, disc, caps)
    if problems:
        raise SystemExit('cell directory: ' + '; '.join(problems))
    return table, count, hulls, disc, rows, calls


def relocation_rows(caps):
    """(problems, {id: address}) of every capture (A6.relocation_problems)."""
    problems, where = [], None
    for cap in caps:
        p, w = A6.relocation_problems(cap)
        problems += p
        if where is not None and w != where:
            problems.append(f'{cap.name}: relocated addresses differ from the first capture')
        where = w
    return problems, where


def export_sub(sub, el, elf, out_root, iso, skip_collision):
    """Level, static bank, dynamic list, collision, cells, background and ctx
    block of one sub."""
    A6.configure(sub)
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
    if info['nested'] != f'chunk10.n{sub}':
        raise SystemExit(f'sub {sub}: nested block {info["nested"]}')
    rproblems, rwhere = relocation_rows(caps)
    if rproblems:
        raise SystemExit('relocations: ' + '; '.join(rproblems[:4]))
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
    if (rwhere.get(0x44), rwhere.get(0x45)) != (bank, dyn_base):
        raise SystemExit('D_0028A5A0 / D_0028A5A4 are not the relocated ids 0x44 / 0x45')
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
    with_setup = [c for c in caps if C.u32(c.ram, E22.D_008106C8) & 0x80]
    room = E22.check_ctx_block(caps, elf)

    table, count, hulls, cells, cell_rows, calls = check_cells(image, caps, elf)
    (out / 'area06_cells.bin').write_bytes(cells)
    drums = E22.drum_rows(caps, hulls, cells)
    if any(d['state'] != 1 for d in drums):
        raise SystemExit(f'a drum not in state 1: {[d for d in drums if d["state"] != 1]}')
    cells_report = dict(address=hex(table), count=count, bytes=len(cells), sha256=C.sha(cells),
                        flag_uids={u: f for u, (_s, _e, f) in hulls.items() if f},
                        moved_uids=sorted({u for r in cell_rows for u in r['moved_hulls']}),
                        flag_calls={k: [list(c) for c in v] for k, v in calls.items()},
                        drums=drums, captures=cell_rows)
    coll = None
    if not skip_collision:
        files, grid_hdr, log = E02.run_collision(image, caps, out / 'area06.emcl', C.SCRATCH)
        coll = dict(files=files, grid_header=hex(grid_hdr), log=log.strip().splitlines(),
                    sha256=C.sha((out / 'area06.emcl').read_bytes()))
        print(f'sub{sub}/area06.emcl: {files}; ' + ' '.join(l for l in log.splitlines() if l.startswith('wrote')))
    (out / 'cells.json').write_text(json.dumps(dict(cells=cells_report, collision=coll), indent=1,
                                               default=str) + '\n')

    report = dict(
        area=C.AREA, sub=sub, captures=[c.name for c in caps],
        excluded_captures=[dict(capture=n, area_bytes=list(a)) for n, a in A6.excluded_captures()],
        load_map=[dict(address=hex(a), id=l, bytes=s, extracted_source=f'{p.parent.name}/{p.name}',
                       source_offset=hex(o)) for a, p, o, s, l in lmap],
        load_info={k: (hex(v) if isinstance(v, int) else v) for k, v in info.items()},
        load_map_bytes_equal=load_rows, descriptor_buffer=buffers,
        relocations={hex(k): hex(v) for k, v in sorted(rwhere.items())},
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
        zones=zone_rows, background=background, ctx_room_block=room,
        spawn_bit_0x80_captures=[c.name for c in with_setup])
    (level / 'level.json').write_text(json.dumps(report, indent=1, default=str) + '\n')
    print(f'sub {sub}: static bank {bank:#x}: {len(objects)} objects -> {len(zone_rows)} zone EMDLs; dynamic list '
          f'{dyn_base:#x} ({len(dyn)} entries); {sum(r["level_kicks"] for r in kicks)} level kicks and '
          f'{sum(r["dynamic_kicks"] for r in kicks)} dynamic kicks in {len(caps)} captures; '
          f'{tex_checked} texture decodes equal the GS freezes; background not armed; ctx +0xA0..+0xFF '
          f'rebuilt by 001D8FD0 + 001D1C50 from room entries {sorted({r["room_entry"] for r in room})}; '
          f'cells {count} uids ({len(cells)} bytes), moved {cells_report["moved_uids"]}, flag calls '
          f'{sorted({tuple(map(tuple, v)) for v in calls.values()})}; drums '
          f'{sorted({(d["uid"], d["state"], d["hull_flags"]) for d in drums})}')
    return image, caps, lmap


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=A6.OUT)
    ap.add_argument('--iso', type=Path, default=C.ISO_PATH,
                    help="the user's disc image (the level-load GS upload is replayed from it)")
    ap.add_argument('--skip-collision', action='store_true')
    args = ap.parse_args(argv)
    out = args.out.resolve()
    iso = args.iso.resolve()
    el = L.load_export_level()
    elf = C.read_elf()
    A6.read_overlay()                      # pins AREA06.BIN
    pairs = A6.all_captures()
    proof = A6.loaded_sub_proof(pairs)
    for cap, sub in pairs:
        rows = proof[cap.name]
        if sub not in A6.SUBS or not all(rows[sub] * 100 < rows[s] for s in A6.ALL_SUBS if s != sub):
            raise SystemExit(f'{cap.name}: its sub-{sub} map does not fit RAM far better than the others ({rows})')
    out.mkdir(parents=True, exist_ok=True)
    (out / 'loaded_sub_proof.json').write_text(json.dumps(
        {k: {f'sub{s}': v for s, v in r.items()} for k, r in proof.items()}, indent=1) + '\n')
    for sub in A6.SUBS:
        export_sub(sub, el, elf, out, iso, args.skip_collision)
    return 0


if __name__ == '__main__':
    sys.exit(main())
