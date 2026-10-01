#!/usr/bin/env python3
"""Export the level of each area the a13 captures load (AREA13 sub 0, and
AREA19 sub 0 at the a13_05 arrival): geometry + textures + GS materials,
the static bank, the grid collision, the cell directory, and check the
background state and the render-context bytes 001D8FD0 / 001D1C50 write
(docs/AREA13_ASSETS.md).

This is export_area01_level.py's pipeline, imported and run unchanged with
export_area01_common pointed at the target (export_area13_common.configure):
the bank walk (001C6120's lookup), the display-list kick check,
export_level.py's record walker / strip builder / texture decode / GS
material code and disc upload replay, export_collision.py --node-class
--verify-ram, the ORIGINAL 001A2370 cell-hull derivation
(derive_cell_image) and the background arm. From export_area02_level:
run_collision (the grid section alone), derive_words (the ORIGINAL 0019C6F0
over the capture, with this module's flag_calls installed),
pool; from export_area22_level:
check_ctx_block (the ORIGINAL 001D8FD0 then 001D1C50) and drum_rows; from
export_area06_level: derive_scaled_hull (the ORIGINAL 0x219F50 run whole on
a live 0x219870 node) and SCALED_CODE. What is this lane's own:

  * the load maps (export_area13_common): AREA13 flat (the AREA22 builder),
    AREA19 a top block with sections + a nested block (build_load_map_both);
    every row is labelled by relocation id, so each static bank is ONE zone
    EMDL (00_id44);
  * neither descriptor relocates id 0x45: D_0028A5A4 keeps the value it had
    in the AREA04 captures before the lift (0x1980000), which lies inside
    both load maps by coincidence. No capture draws a kernel-0x00237450
    kick (check_kicks with an empty list fails on one), so no dynamic list
    is exported; the previous-area value is recorded (stale_d_0028a5a4);
  * the owners that re-transform cell hulls, read from original code: the
    pickup 0x219550 and the drum 0x156620 (node + 0xD0); the outdoor
    creatures 0x824BB0 (AREA13, byte-identical C
    func_overlay_AREA13_00824B70.c) and 0x827DD0 (AREA19, byte-identical C
    func_overlay_AREA19_00827D90.c), twins of AREA01's 0x826D40, whose
    four 001A2370 calls pass *(D_00275B40 + 0xC) + 0x90 with D_00275B40 =
    node + 0x110 while a behaviour runs, so *(node + 0x11C) + 0x90 (the
    AREA01 lane's reading of the same code); and 0x219870, whose hull is
    re-transformed by 0x219F50 with a scaled scratchpad copy, derived by
    running the ORIGINAL 0x219F50 (the AREA06 lane's derive_scaled_hull);
  * the uid words (0019C6F0, flag_calls below): AREA13's [47] 0x8293A0
    (byte-identical C func_overlay_AREA13_00829360.c) and AREA19's [9]
    0x825C70 (byte-identical C func_overlay_AREA19_00825C30.c).

Output (ignored assets/area13/, disc-derived, never committed):
  sub0/...                 AREA13 sub 0
  area19/sub0/...          AREA19 sub 0 (the a13_05 arrival only)
each with
  level/00_id44.emdl (+ .gsmat.json)   geometry, textures, codes
  level/static_bank.emsc               *D_0028A5A0
  level/level.json                     counts, hashes, verification
  <area>.emcl (+ scene.txt)            grid collision (area13.emcl / area19.emcl)
  <area>_cells.bin                     the cell directory (disc bytes)
  cells.json                           the directory verification
and area19/loaded_sub_proof.json (per capture, rows of each nested map).

Usage (port root, macOS arm64, pure Python):
  python3 tools/export_area13_level.py [--target area13|area19]
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area13_common as A13  # noqa: E402  (first: it keeps AREA01's nested load-map builder)
import export_area06_level as E06  # noqa: E402  (derive_scaled_hull; sets AREA06 state on import)
import export_area02_level as E02  # noqa: E402  (already imported by E06)
import export_area22_level as E22  # noqa: E402  (already imported by E06)

C = A13.configure('area13')            # after the imports: this lane's state from here on
L = E02.L                              # export_area01_level

D_0028A594, D_0028A598, D_0028A59C, D_0028A5A0, D_0028A5A4, D_0028A5A8 = \
    0x28A594, 0x28A598, 0x28A59C, 0x28A5A0, 0x28A5A4, 0x28A5A8
PICKUP, DRUM = 0x219550, 0x156620
# the outdoor creatures (twins of AREA01 0x826D40): *(node + 0x11C) + 0x90
CREATURES = {'area13': 0x824BB0, 'area19': 0x827DD0}
SCALED_OWNER, SCALED_CALLER = E06.SCALED_OWNER, E06.SCALED_CALLER        # 0x219870, 0x219F50
# the 0019C6F0 caller of each target's sub 0, from its byte-identical C
FLAG_OWNERS = {'area13': 0x8293A0, 'area19': 0x825C70}
# the D_0028A5A4 word of the last capture before each target's load (the
# area it came from): AREA04's a04b_03 for AREA13, AREA13's a13_04 for AREA19
PREVIOUS = {'area13': A13.ROUTE_A04B / 'a04b_03_reader', 'area19': A13.ROUTE_A13 / 'a13_04_hatch'}


def target_name():
    return A13.current().name


def owner_matrix(ram, node):
    """The matrix address a live owner passes to 001A2370 (None: not a
    matrix-address caller): node + 0xD0 for the pickup and the drum,
    *(node + 0x11C) + 0x90 for the target's creature. 0x219870 is None here:
    its matrix is the scratchpad copy 0x219F50 makes (derive_scaled_hull)."""
    behaviour = C.u32(ram, node + 0x10)
    if behaviour in (PICKUP, DRUM):
        return node + 0xD0
    if behaviour == CREATURES[target_name()]:
        return C.u32(ram, node + 0x11C) + 0x90
    return None


def flag_calls(ram):
    """The 0019C6F0 calls that decide the uid words of this load, from the
    owner's byte-identical C (only calls whose result the capture shows):
      * AREA13 [47] 0x8293A0: state 0 calls (0x1F, 1), (0x20, 0) when
        D_008107F4 bit 0x40 is clear and (0x1F, 0), (0x20, 1) when it is
        set; state 1 calls (0x1F, 0), (0x20, 1) only with the bit set. So a
        live node in state 1 with the bit clear made (0x1F, 1), (0x20, 0)
        last. The bit set is not modelled (ValueError): the node could then
        have made either pair last;
      * AREA19 [9] 0x825C70: state 0 calls (0x21, 0) when D_00810776 is
        0xFF (going to state 2), else (0x21, 1) (going to state 1); state 1
        calls (0x21, 0) only on its way to state 2. So a live node in state
        1 made (0x21, 1) last, one in state 2 made (0x21, 0) last.
    Any other state, count or byte: ValueError. Outside the target: no call."""
    t = A13.current()
    if ram[0x810700] != t.area or ram[0x810701] != t.sub:
        return ()
    owner = FLAG_OWNERS[t.name]
    nodes = [a for _s, a in E02.pool(ram) if C.u32(ram, a + 0x10) == owner]
    if t.name == 'area13':
        if len(nodes) != 1 or ram[nodes[0] + 4] != 1 or ram[0x8107F4] & 0x40:
            raise ValueError(f'[47] (0x8293A0) is not one live node in state 1 with D_008107F4 bit 0x40 clear: '
                             f'{[(hex(a), ram[a + 4]) for a in nodes]}, D_008107F4 {ram[0x8107F4]:#x}')
        return ((0x1F, 1), (0x20, 0))
    if len(nodes) != 1 or ram[nodes[0] + 4] not in (1, 2):
        raise ValueError(f'[9] (0x825C70) is not one live node in state 1 or 2: '
                         f'{[(hex(a), ram[a + 4]) for a in nodes]}')
    return ((0x21, 1),) if ram[nodes[0] + 4] == 1 else ((0x21, 0),)


def install(target=None):
    """Point the AREA01 level module and export_area02_level's word
    derivation at the target (the imports installed AREA06's)."""
    A13.configure(target or A13.current())
    L.owner_matrix = owner_matrix      # derive_cell_image reads it by name
    E02.flag_calls = flag_calls        # derive_words reads it by name


install('area13')


def verify_directory(elf, disc, caps):
    """Every byte of every capture's directory: the uid words through the
    ORIGINAL 0019C6F0 (export_area02_level.derive_words with flag_calls),
    then per capture the hulls (export_area01_level.derive_cell_image with
    owner_matrix), a hull of a live 0x219870 node through the ORIGINAL
    0x219F50 (export_area06_level.derive_scaled_hull), an orphaned hull
    (no live node carries its uid) equal, byte for byte, to a derivation of
    it in another capture of the run. Returns (rows, problems, calls)."""
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
                for a, n in E06.SCALED_CODE):
            problems.append(f'{cap.name}: 001A2370 / 0x219F50 code in RAM differs from the ELF')
            continue
        try:
            image, used = E02.derive_words(elf, disc, cap)
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
                    hull = E06.derive_scaled_hull(elf, image, cap, n, hulls)
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
    for cap, uid, data, _image in pending:
        row = next(r for r in rows if r['capture'] == cap.name)
        if data in derived.get(uid, set()):
            row['proofs'][uid] = 'orphan: equal to a derivation of it in another capture'
            continue
        # no written-words fallback (export_area02_level.orphan_problems, which
        # the AREA06 beam needed): every orphan of these captures equals a
        # derivation in another capture, and the fallback would accept a
        # changed byte of a word 001A2370 writes
        problems.append(f'{cap.name}: hull {uid} differs, no live node owns it, and it equals no derivation of '
                        'it in another capture')
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
    """(problems, {id: address}) of every capture (A13.relocation_problems)."""
    problems, where = [], None
    for cap in caps:
        p, w = A13.relocation_problems(cap)
        problems += p
        if where is not None and w != where:
            problems.append(f'{cap.name}: relocated addresses differ from the first capture')
        where = w
    return problems, where


def previous_word(target, at):
    """The word at `at` in the last capture before the target's load (None
    when that capture is absent)."""
    path = PREVIOUS[target] / 'eeMemory.bin'
    if not path.exists():
        return None
    with open(path, 'rb') as f:
        f.seek(at)
        return struct.unpack('<I', f.read(4))[0]


# export_collision.py run with one change to its header scan: the grid of
# AREA13 has its rank tables (header +0x18, twelve s16[N] tables) at a
# halfword-aligned offset (0x270FE), which find_grid_header's "every offset
# 4-aligned" signature rejects. The original names that halfword-aligned
# address itself: the loader leaves grid + tab_off + 2 * N * k in the
# scratchpad words 0x70003210 + 4k, which --verify-ram compares in every
# capture. The relaxed scan is tried only when the tool's own finds nothing,
# only at offset 0 (run_collision's source file starts at the header), and
# keeps every other signature test.
_COLLISION_RUNNER = r"""
import struct, sys
sys.path.insert(0, 'tools')
import export_collision as X
strict = X.find_grid_header
def find(d):
    o = strict(d)
    if o is not None or len(d) < 0x28:
        return o
    v = struct.unpack_from('<9I', d, 0)
    (n,) = struct.unpack_from('<h', d, 0x24)
    offs = (v[0], v[2], v[4], v[6], v[8])
    if v[7] != 0xC or not 0 < n < 0x8000 or any(x == 0 or x >= len(d) for x in offs):
        return None
    if any(x & 3 for x in (v[0], v[2], v[4], v[8])) or v[6] & 1 or list(offs) != sorted(offs):
        return None
    if v[1] != (v[2] - v[0]) // 12 or v[5] != (v[6] - v[4]) // 2 or v[8] + 64 * n > len(d):
        return None
    print('grid header at 0 with the rank tables at the halfword-aligned offset %#x' % v[6])
    return 0
X.find_grid_header = find
sys.exit(X.main(sys.argv[1:]))
"""


def run_collision(image, caps, out, scratch):
    """export_area02_level.run_collision's inputs (the grid section alone,
    read from the resident image) through export_collision.py with the
    relaxed header scan above. Returns (labels, grid, log)."""
    import subprocess
    grid = C.u32(caps[0].ram, D_0028A598)
    end = E02.grid_extent(image.read, grid)
    parts = [(a, s, lab) for a, _p, _o, s, lab in image.map if a < end and a + s > grid]
    if not parts or parts[0][0] > grid:
        raise SystemExit('grid section not inside mapped files')
    for (a, s, _l), (b, _t, _m) in zip(parts, parts[1:]):
        if a + s != b:
            raise SystemExit('the files holding the grid are not contiguous in RAM')
    scratch.mkdir(parents=True, exist_ok=True)
    src = scratch / 'grid_source.bin'
    src.write_bytes(image.read(grid, end - grid))
    cmd = [sys.executable, '-c', _COLLISION_RUNNER, str(src.resolve()), '-o', str(Path(out).resolve()),
           '--node-class']
    for cap in caps:
        cmd += ['--verify-ram', str((cap.path / 'eeMemory.bin').resolve())]
    import subprocess
    res = subprocess.run(cmd, cwd=C.DECOMP, capture_output=True, text=True)
    if res.returncode:
        raise SystemExit('export_collision.py failed:\n' + res.stdout + res.stderr)
    if 'grid section @0x0:' not in res.stdout or 'wrote' not in res.stdout:
        raise SystemExit('export_collision.py did not export the grid:\n' + res.stdout)
    return [lab for _a, _s, lab in parts], grid, res.stdout


def export_background(el, caps, out):
    """The level background when the captures arm it (render ctx +0x174
    bits 0 and 1, 001C1F50 at the area load): export_level.export_background
    (the decomp's --background path, imported) run on EVERY capture, each
    run checking the disc-replayed texels against that capture's GS freeze;
    every run must write the same asset, which becomes background.embg.
    All captures arm it or none does (a mix is refused). Returns the
    summary (None when no capture arms it; a stale file is removed)."""
    import contextlib
    import io
    armed = [r['armed'] for r in L.check_background(caps)]
    path = out / el.BG_ASSET
    if not any(armed):
        if path.exists():
            path.unlink()
        return None
    if not all(armed):
        raise SystemExit(f'the background is armed in {sum(armed)} of {len(caps)} captures; not modelled')
    disc = el.BackgroundDisc(None, str(C.ISO_PATH))
    blobs, log = {}, ''
    for cap in caps:
        scene = C.SCRATCH / 'background' / cap.name
        if scene.exists():
            for q in scene.iterdir():
                q.unlink()
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            el.export_background(scene, C.ELF_PATH, cap.path / 'eeMemory.bin', cap.gs, C.AREA, C.SUB, disc)
        log = buf.getvalue()
        blobs[cap.name] = (scene / el.BG_ASSET).read_bytes()
        for q in scene.iterdir():                       # scratch: the asset and its scene.txt
            q.unlink()
        scene.rmdir()
    if (C.SCRATCH / 'background').exists() and not any((C.SCRATCH / 'background').iterdir()):
        (C.SCRATCH / 'background').rmdir()
    first = blobs[caps[0].name]
    if any(b != first for b in blobs.values()):
        raise SystemExit('the background assets of the captures differ: '
                         f'{sorted(k for k, b in blobs.items() if b != first)}')
    path.write_bytes(first)
    return dict(file=path.name, bytes=len(first), sha256=C.sha(first), captures_equal=sorted(blobs),
                texels_equal_gs_freeze=sorted(blobs), tex0=hex(struct.unpack_from('<Q', first, 16)[0]),
                size=list(struct.unpack_from('<2I', first, 8)),
                log=[x for x in log.splitlines() if x.startswith('background:') and 'manifest' not in x])


def export_target(target, el, elf, out_root, iso, skip_collision):
    """Level, static bank, collision, cells, background and ctx block of one
    target. Returns (image, caps, lmap)."""
    install(target)
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
    rproblems, rwhere = relocation_rows(caps)
    if rproblems:
        raise SystemExit('relocations: ' + '; '.join(rproblems[:4]))
    if 0x45 in rwhere:
        raise SystemExit('the descriptor relocates id 0x45; the stale-pointer model does not apply')
    for ident, word in ((0x41, D_0028A594), (0x42, D_0028A598), (0x43, D_0028A59C), (0x44, D_0028A5A0),
                        (0x46, D_0028A5A8)):
        if rwhere.get(ident) != C.u32(caps[0].ram, word):
            raise SystemExit(f'D_{word:08X} is not the relocated id {ident:#x}')
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
    previous = previous_word(target, D_0028A5A4)
    if previous is not None and previous != dyn_base:
        raise SystemExit(f'D_0028A5A4 {dyn_base:#x} is not the previous capture\'s {previous:#x}')
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
    stale = level / 'dynamic_objects.emsc'
    if stale.exists():
        stale.unlink()

    background = L.check_background(caps)
    bg = export_background(el, caps, out)
    room = E22.check_ctx_block(caps, elf)
    install(target)

    table, count, hulls, cells, cell_rows, calls = check_cells(image, caps, elf)
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
        files, grid_hdr, log = run_collision(image, caps, out / f'{t.name}.emcl', C.SCRATCH)
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
        stale_d_0028a5a4=dict(value=hex(dyn_base), previous_capture=PREVIOUS[target].name,
                              previous_value=None if previous is None else hex(previous),
                              inside_load_map=image.locate(dyn_base) is not None,
                              note='id 0x45 is not in the relocation list; the word keeps the previous '
                                   'area\'s value; no capture draws a kernel-0x00237450 kick'),
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
          f'{sum(r["level_kicks"] for r in kicks)} level kicks in {len(caps)} captures, no dynamic kick '
          f'(D_0028A5A4 {dyn_base:#x} stale); {tex_checked} texture decodes equal the GS freezes; background '
          f'{"exported, " + str(bg["size"]) if bg else "not armed"}; ctx +0xA0..+0xFF rebuilt from room entries {sorted({r["room_entry"] for r in room})}; '
          f'cells {count} uids ({len(cells)} bytes), moved {cells_report["moved_uids"]}, flag calls '
          f'{sorted({tuple(map(tuple, v)) for v in calls.values()})}; drums {cells_report["drums"]}')
    return image, caps, lmap


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=A13.OUT)
    ap.add_argument('--iso', type=Path, default=C.ISO_PATH,
                    help="the user's disc image (the level-load GS upload is replayed from it)")
    ap.add_argument('--target', choices=sorted(A13.TARGETS), action='append',
                    help='export only this target (repeatable; default both)')
    ap.add_argument('--skip-collision', action='store_true')
    args = ap.parse_args(argv)
    out = args.out.resolve()
    iso = args.iso.resolve()
    el = L.load_export_level()
    elf = C.read_elf()
    out.mkdir(parents=True, exist_ok=True)
    for name in args.target or ('area13', 'area19'):
        A13.read_overlay(A13.TARGETS[name])            # pins the overlay
        if name == 'area19':
            install('area19')
            caps = C.captures()
            proof = A13.loaded_sub_proof(caps)
            for cap in caps:
                rows = proof[cap.name]
                sub = cap.ram[0x810701]
                if not all(rows[sub] * 100 < rows[s] for s in rows if s != sub):
                    raise SystemExit(f'{cap.name}: its sub-{sub} map does not fit RAM far better ({rows})')
            dest = out / 'area19'
            dest.mkdir(parents=True, exist_ok=True)
            (dest / 'loaded_sub_proof.json').write_text(json.dumps(
                {k: {f'sub{s}': v for s, v in r.items()} for k, r in proof.items()}, indent=1) + '\n')
        export_target(name, el, elf, out, iso, args.skip_collision)
    return 0


if __name__ == '__main__':
    sys.exit(main())
