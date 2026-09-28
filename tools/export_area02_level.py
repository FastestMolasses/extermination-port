#!/usr/bin/env python3
"""Export the AREA02 level for both sub-states the route loads: geometry +
textures + GS materials, the static bank, the grid collision, the cell
directory, and check the background state and the render-context bytes
001D8FD0 writes (docs/AREA02_ASSETS.md).

This is export_area01_level.py's pipeline, imported and run unchanged with
export_area01_common pointed at AREA02 (export_area02_common.configure):
the load map from the loader's descriptors and cursors, the bank walk
(001C6120's lookup), the display-list kick check, export_level.py's record
walker / strip builder / texture decode / GS material code and disc upload
replay, export_collision.py --node-class --verify-ram, the ORIGINAL 001A2370
cell-hull derivation, the background arm and the ORIGINAL 001D8FD0 ctx
rebuild. What is AREA02's own:

  * everything is per sub. Sub 1 (chunk06.n1: the arrival a01r_03 and
    a02_s0) and sub 0 (chunk06.n0: a02_00 .. a02_04, loaded by the duct's
    sub change without an overlay reload) load different nested blocks at
    different cursors, and the grid, the cell directory, the static bank,
    the model bank and the area message bank all lie in the nested block.
    So every level file is written under assets/area02/sub<N>/;
  * no dynamic list: D_0028A5A4 still holds AREA01's list address
    (0x177A940, outside both load maps); check_kicks runs with an empty
    list, so any kernel-0x00237450 kick in a capture fails the export;
  * the grid file (n<N>/f02_id44) is only partly resident and also holds
    the cell directory, so export_collision.py gets exactly the grid
    section's resident bytes (run_collision), written to the scratch tree
    (build/area02/assets/sub<N>/grid_source.bin), not the whole disc files;
  * the owners that re-transform cell hulls, and the matrix each passes to
    001A2370, read from the committed byte-identical C:
    0x825100 (func_overlay_AREA02_008250C0): self + 0xD0;
    0x823930 with dispatch kind (+2 & 0x1F) 4 and +0x0D == 7 (the car:
    0x823930 -> 0x824020 -> 0x8242F0, func_overlay_AREA02_008242B0):
    *(self + 0x110) + 0x90; 0x219550 (pickup): node + 0xD0 (as in AREA01);
  * the directory's uid words. The 0019C6F0 callers in the overlay's C:
    the gate kind 14 at +0xB0 > 170 (0x823D70, func_overlay_AREA02_00823D30:
    (0x1D, 1), (0x1E, 1) in the frame its countdown ends, when it sets +4 =
    3), the kind-8 node of 0x823930 (0x8242F0 +6 case 3: (1, 1), then +4 =
    3), and, at state 0 only when D_00810761 is already set, the switch
    0x823980 ((0x1D, 1), (0x1E, 1)) and kinds 10 / 11 of 0x824020 ((1, 1)).
    flag_calls() derives the calls of each capture from its owners (see
    there); the ORIGINAL 0019C6F0 runs them in the EE interpreter over the
    capture's RAM and scratchpad with the directory set to the disc bytes,
    and the result must be the capture's uid words;
  * an orphaned hull: the car frees itself when it stops (a02_02 f487,
    FOURTH_LEVEL_ROUTE.md) and its pool slot's bone matrices are cleared, so
    its last 001A2370 run cannot be re-derived. Such a hull is accepted only
    under orphan_problems() (another capture derives the same hull from its
    live owner, every differing word is one the ORIGINAL 001A2370 writes
    (written_words), and the bytes are equal in every capture where it is
    orphaned); docs/AREA02_ASSETS.md lists it as a known gap.

Output (ignored assets/area02/, disc-derived, never committed):
  sub<N>/level/NN_<file>.emdl (+ .gsmat.json)   geometry, textures, codes
  sub<N>/level/static_bank.emsc                 *D_0028A5A0
  sub<N>/level/level.json                       counts, hashes, verification
  sub<N>/area02.emcl (+ scene.txt)              grid collision
  sub<N>/area02_cells.bin                       the cell directory (disc bytes)
  sub<N>/cells.json                             the directory verification

Usage (port root, macOS arm64, pure Python):
  python3 tools/export_area02_level.py
"""
from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area02_common as A2  # noqa: E402

C = A2.configure(0)
import export_area01_level as L  # noqa: E402

D_0028A598, D_0028A59C, D_0028A5A0, D_0028A5A4, D_0028A5A8 = 0x28A598, 0x28A59C, 0x28A5A0, 0x28A5A4, 0x28A5A8
D_00810761, D_008107E1 = 0x810761, 0x8107E1

# 0019C6F0 (directory uid-word flag setter, NEARMISS C, logic faithful; the
# ORIGINAL instructions run here)
FLAG_SETTER = 0x19C6F0
FLAG_SETTER_CODE = ((0x19C6F0, 0x140),)
DISPATCH, GATE, TURNTABLE, PICKUP = 0x823930, 0x823D70, 0x825100, 0x219550


def owner_matrix(ram, node):
    """The matrix address a live AREA02 owner passes to 001A2370 (None: the
    behaviour is not a known caller), from the committed C:
    0x825100 and 0x219550: node + 0xD0; 0x823930 with +2 & 0x1F == 4 and
    +0x0D == 7 (0x824020 -> 0x8242F0): *(node + 0x110) + 0x90."""
    behaviour = C.u32(ram, node + 0x10)
    if behaviour in (PICKUP, TURNTABLE):
        return node + 0xD0
    if behaviour == DISPATCH and ram[node + 2] & 0x1F == 4 and ram[node + 0x0D] == 7:
        return C.u32(ram, node + 0x110) + 0x90
    return None


L.owner_matrix = owner_matrix          # derive_cell_image reads it by name


def pool(ram):
    return [(s, L.POOL_BASE + s * L.POOL_STRIDE) for s in range(L.POOL_SLOTS)
            if ram[L.POOL_BASE + s * L.POOL_STRIDE]]


def placements_of(ram):
    """The current sub's placement records, walked over this RAM (001B6990's
    walk: D_0024D7C0[area][sub], 0x28 bytes, until s16 word 0 == 0xFF)."""
    area, sub = ram[0x810700], ram[0x810701]
    at = C.u32(ram, C.u32(ram, 0x24D7C0 + 4 * area) + 4 * sub)
    out = []
    while C.s16(ram, at + 0x28 * len(out)) != 0xFF:
        out.append(ram[at + 0x28 * len(out):at + 0x28 * (len(out) + 1)])
        if len(out) > 512:
            raise ValueError('placement table without an end record')
    return out


def live_in_state_1(ram, behaviour, index):
    """A live node spawned by placement `index` (behaviour and +0x9A) with +4 == 1."""
    return any(C.u32(ram, a + 0x10) == behaviour and ram[a + 0x9A] == index and ram[a + 4] == 1
               for _s, a in pool(ram))


def flag_calls(ram):
    """The 0019C6F0 calls the AREA02 sub-0 owners made in this load before
    the capture, from their committed C (the module docstring):
      * the load must have been made with D_00810761 clear, shown by the
        switch (0x823930, kind 9) live in state 1: its state 0 goes to
        state 3 instead when the byte is set (the load-time calls of the
        switch and of kinds 10 / 11 are then not modelled: ValueError);
      * each placement gate (0x823D70, +0x0D 14, x > 170) that is no longer
        live in state 1 has made (0x1D, 1), (0x1E, 1): it leaves state 1
        only in the frame it makes them;
      * each placement kind-8 node of 0x823930 (dispatch 4) that is no
        longer live in state 1 has made (1, 1), likewise.
    The calls only clear bit 30 of the word they select and select by bit
    29, so their order does not change the result. Returns a tuple."""
    if ram[0x810700] != A2.AREA or ram[0x810701] != 0:
        return ()
    if not any(C.u32(ram, a + 0x10) == DISPATCH and ram[a + 2] & 0x1F == 9 and ram[a + 4] == 1
               for _s, a in pool(ram)):
        raise ValueError('the switch 0x823930 kind 9 is not live in state 1: load-time flag calls '
                         '(D_00810761 set at load) are not modelled')
    calls = []
    for i, rec in enumerate(placements_of(ram)):
        behaviour = C.u32(rec, 0x24)
        if behaviour == GATE and rec[4] == 0x0E and struct.unpack_from('<f', rec, 0x0C)[0] > 170.0:
            if not live_in_state_1(ram, GATE, i):
                calls += [(0x1D, 1), (0x1E, 1)]
        elif behaviour == DISPATCH and rec[0] & 0x1F == 4 and rec[4] == 8:
            if not live_in_state_1(ram, DISPATCH, i):
                calls.append((1, 1))
    return tuple(calls)


_WORDS = {}


def derive_words(elf, disc, cap):
    """(directory image, calls): the disc directory after the ORIGINAL
    0019C6F0 ran, in the EE interpreter over the capture's RAM and
    scratchpad with the directory set to `disc`, for the calls flag_calls()
    selects. Raises ValueError when the calls cannot be derived, when a
    call changes a byte outside the uid words, when a call finds no record
    (returns 0), or when 0019C6F0's code in RAM differs from the ELF."""
    from test_coll_move_reference import FloatEE
    calls = flag_calls(cap.ram)
    if not calls:
        return disc, calls
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
        results = []
        for args in calls:
            ee.call(FLAG_SETTER, args)
            results.append(ee.r[2] & 0xFFFFFFFF)          # v0: 1 when a record acted
        got = ee.read(table, len(disc))
        words = 4 + 4 * C.u32(disc, 0)
        if got[words:] != disc[words:]:
            raise ValueError(f'{cap.name}: 0019C6F0 changed a byte outside the uid words')
        hit = _WORDS[key] = (got, results, elf, cap.ram, cap.spad)
    if any(r != 1 for r in hit[1]):
        raise ValueError(f'{cap.name}: a 0019C6F0 call {calls} returned {hit[1]} (no record acted)')
    return hit[0], calls


# Two generic rigid matrices (rotations about skew axes plus a translation),
# used only to learn which words of a hull 001A2370 writes: a written float
# lane changes under them, an unwritten word cannot.
_PROBE_MATRICES = (
    (0.36, 0.48, -0.8, 0.0, -0.8, 0.6, 0.0, 0.0, 0.48, 0.64, 0.6, 0.0, 17.25, -3.5, 101.125, 1.0),
    (0.6, 0.0, 0.8, 0.0, 0.64, 0.6, -0.48, 0.0, -0.48, 0.8, 0.36, 0.0, -250.5, 7.75, -12.0, 1.0),
)
SCRATCH_NODE, SCRATCH_MATRIX = 0x1C00000, 0x1C00400


def written_words(elf, disc, cap, uid, hulls):
    """{word offset in the hull} that the ORIGINAL 001A2370 writes when it
    re-transforms hull `uid` (the EE interpreter over the capture's RAM with
    the directory set to `disc`, a scratch node carrying the uid, each of
    the two _PROBE_MATRICES): the words that differ from the disc in
    either run. Raises ValueError when a run touches another hull."""
    from test_coll_move_reference import FloatEE
    s, e, _f = hulls[uid]
    table = C.u32(cap.spad, L.SPAD_CELLS)
    out = set()
    for m in _PROBE_MATRICES:
        ee = FloatEE(elf, cap.ram, cap.spad)
        ee.write(table, disc)
        node = bytearray(0x2F0)
        node[0] = 1
        struct.pack_into('<H', node, 0x0E, uid << 8)
        ee.write(SCRATCH_NODE, bytes(node))
        ee.write(SCRATCH_MATRIX, struct.pack('<16f', *m))
        ee.call(L.RETRANSFORM, (SCRATCH_NODE, SCRATCH_MATRIX))
        got = ee.read(table, len(disc))
        if got[:s] + got[e:] != disc[:s] + disc[e:]:
            raise ValueError(f'{cap.name}: the probe run of hull {uid} touched another hull')
        out |= {k for k in range(0, e - s, 4) if got[s + k:s + k + 4] != disc[s + k:s + k + 4]}
    return out


def orphan_problems(elf, cap, uid, data, disc, hulls, derived, orphans):
    """An orphaned hull (no live node carries its uid) whose bytes equal no
    derivation in another capture: some capture must derive the same hull
    from a live owner, every 32-bit word that differs from the disc must be
    a word the ORIGINAL 001A2370 writes (written_words), and the bytes must
    be equal in every capture where the hull is orphaned (`orphans`:
    [(name, uid, bytes)]). `cap` is the Capture."""
    s, e, _f = hulls[uid]
    if uid not in derived:
        return [f'{cap.name}: hull {uid} differs, no live node owns it, and no capture derives it']
    moved = {k for k in range(0, e - s, 4) if data[k:k + 4] != disc[s + k:s + k + 4]}
    try:
        written = written_words(elf, disc, cap, uid, hulls)
    except ValueError as error:
        return [str(error)]
    cap = cap.name
    out = []
    if not moved <= written:
        out.append(f'{cap}: hull {uid} orphan words {sorted(hex(k) for k in moved - written)[:6]} are words '
                   '001A2370 does not rewrite')
    if any(o != data for n, u, o in orphans if u == uid):
        out.append(f'{cap}: hull {uid} orphan bytes differ between the captures that orphan it')
    return out


def verify_directory(elf, disc, caps):
    """Every byte of every capture's directory: the uid words through
    derive_words, then per capture (export_area01_level.derive_cell_image)
    the hulls, each equal to the disc or to the ORIGINAL 001A2370
    derivation for its live owner; an orphaned hull equal to a derivation
    of it in another capture, or accepted by orphan_problems.
    Returns (rows, problems, {capture: calls})."""
    count, hulls, size = L.cell_directory(disc, 0)
    rows, problems, calls, derived, orphans, pending = [], [], {}, {}, [], []
    for cap in caps:
        table = C.u32(cap.spad, L.SPAD_CELLS)
        if size != len(disc) or C.s16(cap.spad, L.SPAD_CELL_COUNT) != count:
            problems.append(f'{cap.name}: directory count/size')
            continue
        if not L.retransform_code_equal(elf, cap.ram):
            problems.append(f'{cap.name}: 001A2370 code in RAM differs from the ELF')
            continue
        try:
            image, used = derive_words(elf, disc, cap)
        except ValueError as error:
            problems.append(str(error))
            continue
        calls[cap.name] = used
        img = cap.ram[table:table + size]
        expected, proofs = L.derive_cell_image(elf, image, cap, hulls)
        for uid, (kind, _node, _behaviour) in proofs.items():
            s, e, _f = hulls[uid]
            if kind == 'derived':
                derived.setdefault(uid, set()).add(img[s:e])
            elif kind == 'orphan':
                orphans.append((cap.name, uid, img[s:e]))
                pending.append((cap, uid, img[s:e], image))
                expected = expected[:s] + img[s:e] + expected[e:]     # proven below
            else:
                problems.append(f'{cap.name}: hull {uid} differs and no owner derivation reproduces it')
        bad = [k for k in range(size) if expected[k] != img[k]]
        if bad:
            problems.append(f'{cap.name}: directory bytes differ at {[hex(k) for k in bad[:8]]}')
        rows.append(dict(capture=cap.name, moved_hulls=sorted(proofs),
                         proofs={u: (f'001A2370(node {n:#x}, behaviour {b:#x})' if k == 'derived' else k)
                                 for u, (k, n, b) in sorted(proofs.items())}))
    for cap, uid, data, image in pending:
        if data in derived.get(uid, set()):
            continue
        problems += orphan_problems(elf, cap, uid, data, image, hulls, derived, orphans)
        for r in rows:
            if r['capture'] == cap.name:
                r['proofs'][uid] = 'orphan: 001A2370 output words only, equal in every orphaning capture'
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


def grid_extent(read, grid):
    hdr = read(grid, 0x28)
    offs = struct.unpack_from('<9I', hdr)
    nodes = struct.unpack_from('<h', hdr, 0x24)[0]
    return grid + max(offs[8] + 64 * nodes, offs[6] + 24 * nodes, offs[0] + 12 * offs[1],
                      offs[2] + 12 * offs[3], offs[4] + 2 * offs[5])


def run_collision(image, caps, out, scratch):
    """export_collision.py over exactly the grid section D_0028A598 names
    (header, pools, rank tables, nodes: [grid, grid_extent)), read from the
    resident image; the mapped files holding it must be contiguous in RAM.
    Only the section: the cell directory lies in the same file, and
    export_collision's cell-list scan takes it for a cell world (in sub 1
    it finds the 18-uid directory at 0x13E0E40 and would fold the actor
    hulls, at their disc positions, into the static EMCL).
    Returns (labels, grid, log)."""
    grid = C.u32(caps[0].ram, D_0028A598)
    end = grid_extent(image.read, grid)
    parts = [(a, s, l) for a, _p, _o, s, l in image.map if a < end and a + s > grid]
    if not parts or parts[0][0] > grid:
        raise SystemExit('grid section not inside mapped files')
    for (a, s, _l), (b, _t, _m) in zip(parts, parts[1:]):
        if a + s != b:
            raise SystemExit('the files holding the grid are not contiguous in RAM')
    scratch.mkdir(parents=True, exist_ok=True)
    src = scratch / 'grid_source.bin'
    src.write_bytes(image.read(grid, end - grid))
    cmd = [sys.executable, 'tools/export_collision.py', str(src.resolve()), '-o', str(Path(out).resolve()),
           '--node-class']
    for cap in caps:
        cmd += ['--verify-ram', str((cap.path / 'eeMemory.bin').resolve())]
    res = subprocess.run(cmd, cwd=C.DECOMP, capture_output=True, text=True)
    if res.returncode:
        raise SystemExit('export_collision.py failed:\n' + res.stdout + res.stderr)
    return [l for _a, _s, l in parts], grid, res.stdout


def export_sub(sub, el, elf, out_root, iso, skip_collision):
    """Level, static bank, collision, cells, background and ctx block of one sub."""
    A2.configure(sub)
    caps = C.captures()
    lmap, info = C.build_load_map(caps[0])
    image = C.LoadedImage(lmap)
    buffers = {caps[0].name: info['rambuf']}
    for cap in caps[1:]:
        other, oinfo = C.build_load_map(cap)
        if other != lmap:
            raise SystemExit(f'{cap.name}: a different load map')
        buffers[cap.name] = oinfo['rambuf']
    if info['nested'] != f'chunk06.n{sub}':
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
    if image.locate(dyn_base) is not None:
        raise SystemExit(f'sub {sub}: D_0028A5A4 = {dyn_base:#x} lies in the load map; a dynamic list is not modelled')
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
    stale = level / 'dynamic_objects.emsc'
    if stale.exists():
        stale.unlink()

    background = L.check_background(caps)
    if any(r['armed'] for r in background):
        raise SystemExit('a capture arms the background; run export_level.py --background for it')
    room = L.check_ctx_room_block(caps, elf)

    table, count, hulls, cells, cell_rows, calls = check_cells(image, caps, elf)
    (out / 'area02_cells.bin').write_bytes(cells)
    cells_report = dict(address=hex(table), count=count, bytes=len(cells), sha256=C.sha(cells),
                        flag_uids={u: f for u, (_s, _e, f) in hulls.items() if f},
                        moved_uids=sorted({u for r in cell_rows for u in r['moved_hulls']}),
                        flag_calls={k: [list(c) for c in v] for k, v in calls.items()},
                        captures=cell_rows)
    coll = None
    if not skip_collision:
        files, grid_hdr, log = run_collision(image, caps, out / 'area02.emcl', C.SCRATCH)
        coll = dict(files=files, grid_header=hex(grid_hdr), log=log.strip().splitlines(),
                    sha256=C.sha((out / 'area02.emcl').read_bytes()))
        print(f'sub{sub}/area02.emcl: {files}; ' + ' '.join(l for l in log.splitlines() if l.startswith('wrote')))
    (out / 'cells.json').write_text(json.dumps(dict(cells=cells_report, collision=coll), indent=1,
                                               default=str) + '\n')

    report = dict(
        area=C.AREA, sub=sub, captures=[c.name for c in caps],
        load_map=[dict(address=hex(a), file=l, file_offset=hex(o), bytes=s) for a, _p, o, s, l in lmap],
        load_info={k: (hex(v) if isinstance(v, int) else v) for k, v in info.items()},
        load_map_bytes_equal=load_rows, descriptor_buffer=buffers,
        static_bank=dict(address=hex(bank), objects=len(objects), grid=hex(grid), span=[hex(lo), hex(hi)],
                         sha256=C.sha(bank_bytes)),
        dynamic_list=dict(d_0028a5a4=hex(dyn_base), drawn=False,
                          note='D_0028A5A4 lies outside the load map (the AREA01 value); no capture draws a '
                               'kernel-0x00237450 kick'),
        kicks=kicks, distinct_level_refs=touched,
        level_state=dict(prim=hex(prim), test_1=hex(el.GS_CLASS0_TEST), alpha_1=hex(el.GS_CLASS0_ALPHA),
                         tex1_1=hex(el.GS_CLASS0_TEX1), clamp_1=hex(el.GS_CLASS0_CLAMP)),
        textures=dict(source='level-load GS upload replayed from the disc image', transfers=transfers,
                      sections=sections, texture_comparisons_with_captures=tex_checked),
        zones=zone_rows, background=background, ctx_room_block=room)
    (level / 'level.json').write_text(json.dumps(report, indent=1, default=str) + '\n')
    print(f'sub {sub}: static bank {bank:#x}: {len(objects)} objects -> {len(zone_rows)} zone EMDLs; '
          f'{sum(r["level_kicks"] for r in kicks)} level kicks in {len(caps)} captures inside the bank; '
          f'{tex_checked} texture decodes equal the GS freezes; background not armed; ctx +0xA0..+0xFF '
          f'rebuilt by 001D8FD0 from room entry {sorted({r["room_entry"] for r in room})}; cells {count} uids '
          f'({len(cells)} bytes), moved {cells_report["moved_uids"]}, flag calls '
          f'{sorted({tuple(map(tuple, v)) for v in cells_report["flag_calls"].values()})}')
    return image, caps, lmap


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=A2.OUT)
    ap.add_argument('--iso', type=Path, default=C.ISO_PATH,
                    help="the user's disc image (the level-load GS upload is replayed from it)")
    ap.add_argument('--skip-collision', action='store_true')
    ap.add_argument('--sub', type=int, choices=A2.SUBS, action='append',
                    help='export only this sub (repeatable; default: both)')
    args = ap.parse_args(argv)
    out = args.out.resolve()
    iso = args.iso.resolve()
    el = L.load_export_level()
    elf = C.read_elf()
    A2.read_overlay()                      # pins AREA02.BIN
    pairs = A2.all_captures()
    proof = A2.loaded_sub_proof(pairs)
    for cap, sub in pairs:
        rows = proof[cap.name]
        if not rows[sub] * 100 < rows[1 - sub]:
            raise SystemExit(f'{cap.name}: its sub-{sub} map does not fit RAM far better than the other ({rows})')
    (out).mkdir(parents=True, exist_ok=True)
    (out / 'loaded_sub_proof.json').write_text(json.dumps(
        {k: {f'sub{s}': v for s, v in r.items()} for k, r in proof.items()}, indent=1) + '\n')
    for sub in args.sub or A2.SUBS:
        export_sub(sub, el, elf, out, iso, args.skip_collision)
    return 0


if __name__ == '__main__':
    sys.exit(main())
