#!/usr/bin/env python3
"""Export the AREA01 sub-0 level: geometry + textures, GS materials, the
dynamic-object list, collision (grid EMCL + cell directory), and check the
background state and the render-context bytes 001D8FD0 writes against the
captures (docs/AREA01_ASSETS.md).

What the original draws as the level (read from the byte-matched / NEARMISS C
under ../Extermination/src, not from labels):
  * 001D5370 walks the grid at render ctx +0x140 (32 x 32 cells, 4 ids each)
    and, for every id, takes object 001C6120(*D_0028A5A0, id) of the static
    bank; 001D4FB0 -> 001D4F30 REFs the object's units (word +0 of the
    object, 0x820 bytes each, from object +0x40) to the level kernel
    0x00237180 (clip kernel 0x00239C90).
  * For stage 0x0100 / 0x0101 001D5370 also calls 001D5BD0, which walks the
    list *D_0028A5A4 (count, then 0x860-byte entries from +0x10) through
    001D4FC0 / 001D5170: kernel 0x00237450 with the entry's +0x34 translation.
So the level geometry is every object of the static bank. Each object's
records are walked with export_level.py's walker and strip builder and
written as one EMDL per source file (NN_<file>.emdl), the format the AREA11
zones use; the texels come from the area's level-load GS upload replayed
from the user's disc (export_level.background_disc_localmem) and must equal
every capture's GS freeze for every texture; the GS state codes are written
the way export_level.py --gs-materials writes the AREA11 zones.

Checks (each one fails the export):
  * the load map equals every capture byte for byte (the cell directory's
    re-transformed hulls excepted and checked separately);
  * every textured level-kernel kick in both display lists of every capture
    REFs units inside a bank object, with PRIM / TEST_1 / ALPHA_1 / TEX1_1 /
    CLAMP_1 equal to the class-0 constants the materials use;
  * every kick of kernel 0x00237450 REFs an entry of the dynamic list;
  * every record of every bank object has matrix slot 0 (world space);
  * the grid EMCL: export_collision.py --node-class --verify-ram over every
    capture (D_0028A598 header, pools, nodes, tables, scratchpad pointers);
  * the cell directory (scratchpad 0x70003250 / 0x7000324C): every byte in
    every capture equals the disc bytes, except each hull whose captured
    bytes differ: that hull must equal the ORIGINAL 001A2370 run (EE
    interpreter) over the disc hull with the matrix its live owner's own
    code passes (owner_matrix); a hull whose owner was freed must equal a
    derivation of the same hull in another capture;
  * background: render ctx +0x174 bits 0/1 (001C1F50's arm) in every capture;
  * ctx +0xA0..+0xFF: the ORIGINAL 001D8FD0 executed over each capture with
    those bytes overwritten rebuilds exactly the captured bytes; it read the
    area's room-table entry (flipping it changes the rebuild) on the
    non-constant branch (D_008106C8 & 0x80 clear); the global
    assets/render_context.emrc blocks are compared with every capture too.

Output (ignored assets/area01/, disc-derived, never committed):
  level/NN_<file>.emdl, level/NN_<file>.gsmat.json   geometry, textures, codes
  level/static_bank.emsc     the bank *D_0028A5A0 (directory + grid + objects)
  level/dynamic_objects.emsc the list *D_0028A5A4
  area01.emcl                grid collision (export_collision.py)
  area01_cells.bin           the cell directory (em_actor_cells_load layout)
  level/level.json           counts, hashes and the verification summary

Usage (port root, macOS arm64, pure Python):
  python3 tools/export_area01_level.py
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area01_common as C  # noqa: E402

D_0028A598, D_0028A5A0, D_0028A5A4 = 0x28A598, 0x28A5A0, 0x28A5A4
UNIT = 0x820
DYN_ENTRY = 0x860
LEVEL_KERNEL, CLIP_KERNEL, DYN_KERNEL = 0x237180, 0x239C90, 0x237450
SPAD_CELLS, SPAD_CELL_COUNT = 0x3250, 0x324C
CTX_PTR, CTX_FLAGS = 0x275670, 0x174
CTX_LO, CTX_HI = 0xA0, 0x100
POOL_BASE, POOL_STRIDE, POOL_SLOTS, ACTOR_HEAD = 0x7A5640, 0x2F0, 0x100, 0x275BC0


def load_export_level():
    name = '_a01_export_level'
    mod = sys.modules.get(name)
    if mod is None:
        sys.path.insert(0, str(C.DECOMP / 'tools'))
        spec = importlib.util.spec_from_file_location(name, C.DECOMP / 'tools/export_level.py')
        mod = importlib.util.module_from_spec(spec)
        sys.modules[name] = mod
        spec.loader.exec_module(mod)
    return mod


# ---------------------------------------------------------------------------
# Bank walk


def bank_objects(read, bank):
    """[(id, object address, units)] of the static bank at `bank`, through
    001C6120's lookup (word 1 + id, >> 2 << 2, added to the bank)."""
    count = C.u32(read(bank, 4), 0)
    if not 0 < count < 0x8000:
        raise SystemExit(f'static bank {bank:#x}: count {count}')
    words = read(bank + 4, 4 * count)
    out = []
    for i in range(count):
        obj = bank + (struct.unpack_from('<i', words, 4 * i)[0] >> 2 << 2)
        out.append((i, obj, C.u32(read(obj, 4), 0)))
    return out


def dynamic_entries(read, base):
    count = C.u32(read(base, 4), 0)
    if not 0 <= count < 0x100:
        raise SystemExit(f'dynamic list {base:#x}: count {count}')
    return [(k, base + 0x10 + DYN_ENTRY * k) for k in range(count)]


# ---------------------------------------------------------------------------
# Display-list checks (the walker of test_level_material_reference.py)


def check_kicks(captures, objects, dyn):
    from test_level_material_reference import (Frame, LIST_HEADS, GS, template_prim)
    ranges = sorted((o + 0x40, o + 0x40 + UNIT * n) for _i, o, n in objects[1:])
    starts = [lo for lo, _ in ranges]
    import bisect

    def in_bank(a, n):
        k = bisect.bisect_right(starts, a) - 1
        return k >= 0 and a + n <= ranges[k][1]
    dyn_ranges = [(e + 0x40, e + 0x40 + UNIT) for _k, e in dyn]
    report, states, touched = [], {}, set()
    for cap in captures:
        n_level = n_dyn = 0
        for head in LIST_HEADS:
            for k in Frame(cap.ram, head).kicks:
                kern, (addr, qwc) = k['kernel'], k['ref']
                if kern in (LEVEL_KERNEL, CLIP_KERNEL):
                    prim = template_prim(k['tpl'], 0x3FC if kern == LEVEL_KERNEL else 0x3F9)
                    if prim is None or not (prim >> 4) & 1:
                        continue          # untextured shadow-volume boxes
                    if k['tid'] != 3 or not in_bank(addr, 16 * qwc):
                        raise SystemExit(f'{cap.name}: level kick REF {addr:#x}+{16 * qwc:#x} '
                                         'outside every bank object')
                    gs = k['gs']
                    st = (template_prim(k['tpl'], 0x3FC), gs[GS['TEST_1']], gs[GS['ALPHA_1']],
                          gs[GS['TEX1_1']], gs[GS['CLAMP_1']])
                    states[st] = states.get(st, 0) + 1
                    touched.add(addr)
                    n_level += 1
                elif kern == DYN_KERNEL:
                    if not any(lo <= addr and addr + 16 * qwc <= hi for lo, hi in dyn_ranges):
                        raise SystemExit(f'{cap.name}: kernel 0x237450 REF {addr:#x} outside the dynamic list')
                    n_dyn += 1
        report.append(dict(capture=cap.name, level_kicks=n_level, dynamic_kicks=n_dyn))
    return report, states, len(touched)


# ---------------------------------------------------------------------------
# Geometry + textures


def texture_localmem(el, args):
    """GS local memory after the area's level-load uploads, replayed from the
    disc image, written as a freeze-shaped file for build_texture_blob."""
    disc = el.BackgroundDisc(None, str(args.iso))
    lm, transfers, sections = el.background_disc_localmem(disc, C.AREA, C.SUB)
    C.SCRATCH.mkdir(parents=True, exist_ok=True)
    path = C.SCRATCH / 'disc_upload_gs.bin'
    path.write_bytes(bytes(425) + lm + bytes(84))          # gs_vram.localmem_base: len - 4 MB - 84
    return path, transfers, sections


def build_zones(el, image, objects, file_of):
    """{label: (MeshBuilder, [object ids], slot-bit violations, records)}"""
    zones = {}
    for ident, obj, units in objects[1:]:
        label = file_of(obj + 0x40)
        b, ids, bad, recs = zones.get(label, (None, [], 0, 0))
        if b is None:
            b = el.MeshBuilder()           # light None: a normal-carrying record faults (H18)
        data = image.read(obj + 0x40, UNIT * units)
        stream = []
        for r in el.walk_records(data, 0, len(data)):
            if r is not None:
                recs += 1
                if (r[2] & 0x3FF) >> 3:
                    bad += 1
            stream.append(r if r is None else r[1:])
        b.add_stream(stream, None)
        ids.append(ident)
        zones[label] = (b, ids, bad, recs)
    return zones


def texture_alphas(el, image, objects):
    """level_texture_alphas over every object's units (strips break at every
    object, as the kernel runs one REF per object)."""
    out = {}
    for _i, obj, units in objects:
        for key, alphas in el.level_texture_alphas(image.read(obj + 0x40, UNIT * units)).items():
            out.setdefault(key, set()).update(alphas)
    return out


def write_zone(el, path, b, tex_entries, tex_blob, prim, alphas, source):
    sections = [(b.pos, b.col, b.tris, b.bone, b.uv, b.tex)]
    el.en.write_emdl(path, sections, [], [-1], [[el.en.mat_identity()]], 30.0,
                     tex_entries, tex_blob, flags=1)
    buf = bytearray(path.read_bytes())
    f_off, t_off, tc, _v_off, _vc, _i_off, _ic = el._emdl_layout(buf)
    if tc != len(b.tex_table):
        raise SystemExit(f'{path.name}: texture count {tc} != {len(b.tex_table)}')
    records = []
    for i, f in enumerate(b.tex_table):
        key = f['key']
        tcc, tfx = (key >> 34) & 1, (key >> 35) & 3
        af = sorted(alphas.get(key, ()))
        if tcc != 1 or (tfx == 0 and af != [128]) or (tfx == 2 and af != [0]) or tfx not in (0, 2):
            raise SystemExit(f'{path.name} texture {i} (TCC {tcc}, TFX {tfx}, Af {af}) is outside '
                             'the decoded material set')
        code = el.gs_material_code(el.GS_CLASS0_TEST, prim, el.GS_CLASS0_ALPHA, key,
                                   el.GS_CLASS0_TEX1, el.GS_CLASS0_CLAMP)
        struct.pack_into('<I', buf, t_off + 16 * i + 12, code)
        records.append({'index': i, 'tex0': f'0x{key:016X}', 'tcc': tcc, 'tfx': tfx, 'rgbaq_a': af,
                        'prim': f'0x{prim:03X}', 'test_1': f'0x{el.GS_CLASS0_TEST:X}',
                        'alpha_1': f'0x{el.GS_CLASS0_ALPHA:X}', 'tex1_1': f'0x{el.GS_CLASS0_TEX1:X}',
                        'clamp_1': f'0x{el.GS_CLASS0_CLAMP:X}', 'code': f'0x{code:08X}'})
    struct.pack_into('<I', buf, f_off, struct.unpack_from('<I', buf, f_off)[0] | el.EMDL_FLAG_GSMAT)
    path.write_bytes(bytes(buf))
    path.with_suffix('.gsmat.json').write_text(json.dumps({
        'zone': path.name, 'source': source,
        'kernel': '0x00237180 (clip 0x00239C90)',
        'env_packet': '001D0F20 arena+0xBA0+0x5A0 (D_00815360), set 1 class 0',
        'template': 'D_002514D0 -> D_00816440 -> VU1 dmem 0x3FC',
        'textures': records}, indent=1) + '\n')


# ---------------------------------------------------------------------------
# Collision


def cell_directory(data, base):
    """(count, {uid: (start, end, flag bits)}, size). A word's low 29 bits are
    the hull offset: the AREA01 directory also sets bit 29 (uid 0), which
    the original's 0x3FFFFFFF mask keeps (docs/AREA01_ASSETS.md)."""
    from test_actor_collision_reference import prim_size
    count = C.u32(data, base)
    size, hulls = 4 + 4 * count, {}
    for uid in range(count):
        word = C.u32(data, base + 4 + 4 * uid)
        if not word:
            continue
        off = word & 0x1FFFFFFF
        at = base + off + 0x1C
        for _ in range(C.s16(data, base + off + 0x18)):
            at += prim_size(data, at)
        hulls[uid] = (off, at - base, word >> 29)
        size = max(size, at - base)
    return count, hulls, size


def live_owners(ram):
    """{uid (+0x0E >> 8): [(slot, behaviour)]} of the live pool nodes."""
    out = {}
    for slot in range(POOL_SLOTS):
        a = POOL_BASE + slot * POOL_STRIDE
        if ram[a]:
            out.setdefault(C.u16(ram, a + 0x0E) >> 8, []).append((slot, C.u32(ram, a + 0x10)))
    return out


# 001A2370 (hull re-transform) and the two leaf routines it calls; the
# captured RAM must hold the pinned ELF's instructions there.
RETRANSFORM = 0x1A2370
RETRANSFORM_CODE = ((0x1A2370, 0x768), (0x1026A0, 0x2C), (0x102738, 0x24))


def owner_matrix(ram, node):
    """The matrix address a live owner passes to 001A2370, read from the
    original call sites (None: the behaviour is not a known caller):
      * 0x219550 (pickup, NEARMISS C line 81; the original passes the node
        + 0xD0 in its second argument register) and 0x8261A0 (byte-identical
        C of 0x826200 / 0x826440: self + 0xD0);
      * 0x826D40 (original code, four call sites): *(D_00275B40 + 0xC) +
        0x90, and while a behaviour runs D_00275B40 = node + 0x110
        (001CB590 -> anim_bone_array_setup, compiled C), so
        *(node + 0x11C) + 0x90 -- the AREA11 0x825940 form."""
    behaviour = C.u32(ram, node + 0x10)
    if behaviour in (0x219550, 0x8261A0):
        return node + 0xD0
    if behaviour == 0x826D40:
        return C.u32(ram, node + 0x11C) + 0x90
    return None


def retransform_code_equal(elf, ram):
    return all(ram[a:a + n] == elf[a - C.ELF_VADDR + C.ELF_OFFSET:a - C.ELF_VADDR + C.ELF_OFFSET + n]
               for a, n in RETRANSFORM_CODE)


_DERIVED = {}


def derive_cell_image(elf, disc, cap, hulls):
    """The directory image the ORIGINAL 001A2370 leaves in `cap`: start from
    the disc bytes, and for every hull whose captured bytes differ run
    001A2370 (EE interpreter over the capture's RAM with the directory set
    to the disc bytes) for each live owner of that uid, with the matrix its
    own code passes. Returns (expected image, {uid: proof}); a proof is
    ('derived', node, behaviour) or ('orphan', None, None) when no live
    owner exists (the caller must prove an orphan from another capture).
    A hull no owner derivation reproduces keeps its disc bytes in the
    expected image, so the comparison fails on it."""
    from test_coll_move_reference import FloatEE
    table = C.u32(cap.spad, SPAD_CELLS)
    img = cap.ram[table:table + len(disc)]
    expected = bytearray(disc)
    proofs = {}
    nodes = {}
    for slot in range(POOL_SLOTS):
        a = POOL_BASE + slot * POOL_STRIDE
        if cap.ram[a]:
            nodes.setdefault(C.u16(cap.ram, a + 0x0E) >> 8, []).append(a)
    for uid, (s, e, _f) in sorted(hulls.items()):
        if img[s:e] == disc[s:e]:
            continue
        proofs[uid] = ('orphan', None, None) if uid not in nodes else ('underived', None, None)
        for node in nodes.get(uid, ()):
            matrix = owner_matrix(cap.ram, node)
            if matrix is None:
                continue
            # the run reads the ELF, this capture's RAM and scratchpad, the
            # directory and (node, matrix); (s, e) follow from the directory
            # and the node's uid. The memo key holds every one of them (the
            # byte images by identity: the entry keeps them alive, so an
            # identity is never reused while the entry exists)
            key = (id(elf), id(cap.ram), id(cap.spad), node, matrix, disc)
            hit = _DERIVED.get(key)
            if hit is None:
                ee = FloatEE(elf, cap.ram, cap.spad)
                ee.write(table, disc)
                ee.call(RETRANSFORM, (node, matrix))
                got = ee.read(table, len(disc))
                hit = _DERIVED[key] = ((got[s:e] if got[:s] + got[e:] == disc[:s] + disc[e:]
                                        else b''),     # b'': the call touched another hull
                                       elf, cap.ram, cap.spad)
            hull = hit[0]
            if hull == img[s:e]:
                expected[s:e] = hull
                proofs[uid] = ('derived', node, C.u32(cap.ram, node + 0x10))
                break
    return bytes(expected), proofs


def verify_cell_directory(elf, disc, caps):
    """Every byte of the directory in every capture: the disc bytes, except
    the hulls the ORIGINAL 001A2370 re-transformed, which must equal the
    disc hull re-transformed with the live owner's matrix. A hull whose
    owner was freed (no live node carries its uid) must equal, byte for
    byte, a derivation of the same hull in another capture. Returns
    (rows, [problems])."""
    count, hulls, size = cell_directory(disc, 0)
    rows, problems, derived, orphans = [], [], {}, []
    for cap in caps:
        table = C.u32(cap.spad, SPAD_CELLS)
        if size != len(disc) or C.s16(cap.spad, SPAD_CELL_COUNT) != count:
            problems.append(f'{cap.name}: directory count/size')
            continue
        if not retransform_code_equal(elf, cap.ram):
            problems.append(f'{cap.name}: 001A2370 code in RAM differs from the ELF')
            continue
        img = cap.ram[table:table + size]
        expected, proofs = derive_cell_image(elf, disc, cap, hulls)
        for uid, (kind, node, behaviour) in proofs.items():
            s, e, _f = hulls[uid]
            if kind == 'derived':
                derived.setdefault(uid, set()).add(img[s:e])
            elif kind == 'orphan':
                orphans.append((cap.name, uid, img[s:e]))
                expected = expected[:s] + img[s:e] + expected[e:]   # proven below
            else:
                problems.append(f'{cap.name}: hull {uid} differs and no owner derivation reproduces it')
        bad = [k for k in range(size) if expected[k] != img[k]]
        if bad:
            problems.append(f'{cap.name}: directory bytes differ at {[hex(k) for k in bad[:8]]}')
        rows.append(dict(capture=cap.name, moved_hulls=sorted(proofs),
                         proofs={u: (f'001A2370(node {n:#x}, behaviour {b:#x})' if k == 'derived' else k)
                                 for u, (k, n, b) in sorted(proofs.items())}))
    for name, uid, data in orphans:
        if data not in derived.get(uid, set()):
            problems.append(f'{name}: hull {uid} differs, no live node owns it, and it equals no '
                            '001A2370 derivation of it in another capture')
    return rows, problems


def check_cells(image, captures, elf):
    first = captures[0]
    table = C.u32(first.spad, SPAD_CELLS)
    if table != C.u32(first.ram, 0x28A5A8):
        raise SystemExit('scratchpad 0x70003250 != D_0028A5A8')
    hit = image.locate(table)
    if hit is None:
        raise SystemExit(f'cell directory {table:#x} outside the load map')
    tail = hit[0] + hit[3] - table
    disc = image.read(table, tail)
    count, hulls, size = cell_directory(disc, 0)
    disc = disc[:size]
    for cap in captures:
        if C.u32(cap.spad, SPAD_CELLS) != table:
            raise SystemExit(f'{cap.name}: directory pointer differs')
    rows, problems = verify_cell_directory(elf, disc, captures)
    if problems:
        raise SystemExit('cell directory: ' + '; '.join(problems))
    return table, count, hulls, disc, rows


def run_collision(image, captures, out):
    """export_collision.py over the files that hold the grid section D_0028A598
    names (its header and pools; the concatenation is the resident order)."""
    grid = C.u32(captures[0].ram, D_0028A598)
    hdr = image.read(grid, 0x28)
    offs = struct.unpack_from('<9I', hdr)
    nodes = struct.unpack_from('<h', hdr, 0x24)[0]
    end = grid + max(offs[8] + 64 * nodes, offs[6] + 24 * nodes, offs[0] + 12 * offs[1],
                     offs[2] + 12 * offs[3], offs[4] + 2 * offs[5])
    files = [(a, p) for a, p, off, size, _l in image.map if a < end and a + size > grid and off == 0]
    if not files or files[0][0] > grid:
        raise SystemExit('grid section not inside whole mapped files')
    # absolute paths: the tool runs with cwd = the decomp tree, so a
    # relative --out must not be resolved there
    cmd = [sys.executable, 'tools/export_collision.py', *[str(p.resolve()) for _a, p in files],
           '-o', str(Path(out).resolve()),
           '--node-class']
    for cap in captures:
        cmd += ['--verify-ram', str((cap.path / 'eeMemory.bin').resolve())]
    res = subprocess.run(cmd, cwd=C.DECOMP, capture_output=True, text=True)
    if res.returncode:
        raise SystemExit('export_collision.py failed:\n' + res.stdout + res.stderr)
    return [p.name for _a, p in files], grid, res.stdout


# ---------------------------------------------------------------------------
# Background and the render-context room block


def check_background(captures):
    rows = []
    for cap in captures:
        ctx = C.u32(cap.ram, CTX_PTR) & 0x1FFFFFF
        flags = C.u32(cap.ram, ctx + CTX_FLAGS)
        rows.append(dict(capture=cap.name, ctx=ctx, flags=flags, armed=flags & 3 == 3))
    return rows


ROOM_TABLE, ROOM_ENTRY, ROOM_ENTRIES = 0x251C50, 0x78, 0x2D


def check_ctx_room_block(captures, elf):
    """The render context bytes +0xA0..+0xFF that 001D8FD0 (camera/transform
    update, committed C) rebuilds. Its C: p = 001D7B30() (the first 0x78-byte
    entry of D_00251C50 whose word 0 equals 0xF00 when 001D2910(8) is
    non-zero, else (D_00810700 << 8 | D_00810701), else the table base);
    when 001B0070() (= D_008106C8) has bit 0x80 it passes the constants
    0.0 / 110.0 and 0, 0, 0, else p +4 / +8 (0021B970: ctx +0xB8 / +0xBC)
    and p +0xC / +0x10 / +0x14 (0021BA80: one packed 24-bit word). The
    ORIGINAL instructions run over each capture with +0xA0..+0xFF
    overwritten and must rebuild the captured bytes; D_008106C8 & 0x80 is
    recorded, and flipping the area's room entry +4..+0x17 must change the
    rebuild (the dependency on the room table is real, not the constants).
    The render_context.emrc blocks are compared with the capture too."""
    import test_effect_manager_reference as EM
    import export_render_context as ERC
    emrc = C.ROOT / 'assets/render_context.emrc'
    blocks = [(a, ERC.elf_block(elf, a, n)) for a, n in ERC.BLOCKS]
    # The global asset is another lane's export; report whether it holds the
    # blocks compared here (a stale or re-scoped asset is not this lane's to fix).
    emrc_fresh = emrc.exists() and ERC.serialize(blocks) == emrc.read_bytes()

    def rebuild(cap, flip=()):
        ram = bytearray(cap.ram)
        ctx = C.u32(ram, CTX_PTR)
        ram[ctx + CTX_LO:ctx + CTX_HI] = bytes([0x5A]) * (CTX_HI - CTX_LO)
        for at in flip:
            ram[at] ^= 0x40
        oracle = EM.Oracle(elf, ram, bytearray(cap.spad))
        oracle.run(0x1D8FD0, (), stack=0x70003F00)
        return bytes(oracle.ram[ctx + CTX_LO:ctx + CTX_HI])

    rows = []
    for cap in captures:
        compared = ERC.verify(blocks, cap.ram)
        ctx = C.u32(cap.ram, CTX_PTR)
        want = bytes(cap.ram[ctx + CTX_LO:ctx + CTX_HI])
        got = rebuild(cap)
        if got != want:
            diff = [hex(CTX_LO + i) for i in range(len(want)) if got[i] != want[i]]
            raise SystemExit(f'{cap.name}: original 001D8FD0 does not rebuild ctx +0xA0..+0xFF: {diff[:8]}')
        key = cap.ram[0x810700] << 8 | cap.ram[0x810701]
        entry = next((k for k in range(ROOM_ENTRIES) if C.u32(cap.ram, ROOM_TABLE + ROOM_ENTRY * k) == key), None)
        if entry is None:
            raise SystemExit(f'{cap.name}: no room entry {key:#x}')
        at = ROOM_TABLE + ROOM_ENTRY * entry
        changed = sum(1 for x, y in zip(rebuild(cap, range(at + 4, at + 0x18)), want) if x != y)
        if not changed:
            raise SystemExit(f'{cap.name}: the rebuild does not read room entry {entry} (key {key:#x})')
        rows.append(dict(capture=cap.name, emrc_bytes_equal=compared, ctx_bytes_rebuilt=CTX_HI - CTX_LO,
                         d_008106c8=hex(C.u32(cap.ram, 0x8106C8)),
                         constant_branch=bool(C.u32(cap.ram, 0x8106C8) & 0x80),
                         room_entry=entry, room_key=hex(key), bytes_changed_by_room_entry=changed,
                         emrc_asset_equals_blocks=emrc_fresh))
    return rows


# ---------------------------------------------------------------------------


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=C.OUT)
    ap.add_argument('--iso', type=Path, default=C.ISO_PATH,
                    help="the user's disc image (the level-load GS upload is replayed from it)")
    ap.add_argument('--skip-collision', action='store_true')
    args = ap.parse_args(argv)
    el = load_export_level()
    elf = C.read_elf()
    caps = C.captures()
    lmap, info = C.build_load_map(caps[0])
    image = C.LoadedImage(lmap)
    buffers = {}
    for cap in caps[1:]:
        other, oinfo = C.build_load_map(cap)
        if other != lmap:
            raise SystemExit(f'{cap.name}: a different load map')
        buffers[cap.name] = oinfo['rambuf']
    cells_table = C.u32(caps[0].ram, 0x28A5A8)
    hit = image.locate(cells_table)
    _count, _hulls, cells_size = cell_directory(image.read(cells_table, hit[0] + hit[3] - cells_table), 0)
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
    objects = bank_objects(image.read, bank)
    dyn = dynamic_entries(image.read, dyn_base)
    kicks, states, touched = check_kicks(caps, objects, dyn)
    prim = el.level_template_prim(el.BootElf(C.ELF_PATH))
    want_state = (prim, el.GS_CLASS0_TEST, el.GS_CLASS0_ALPHA, el.GS_CLASS0_TEX1, el.GS_CLASS0_CLAMP)
    if set(states) != {want_state}:
        raise SystemExit(f'level kick GS state {states} != class 0 {want_state}')

    # object 0 is the grid block itself (render ctx +0x140 points into it)
    ctx = C.u32(caps[0].ram, CTX_PTR) & 0x1FFFFFF
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

    out = args.out.resolve()
    args.iso = args.iso.resolve()
    level = out / 'level'
    level.mkdir(parents=True, exist_ok=True)
    gs_path, transfers, sections = texture_localmem(el, args)
    zones = build_zones(el, image, objects, file_of)
    alphas = texture_alphas(el, image, objects[1:])
    zone_rows, tex_checked = [], 0
    for n, (label, (b, oids, bad, recs)) in enumerate(sorted(zones.items(), key=lambda z: file_of_order(z[0], lmap))):
        if bad:
            raise SystemExit(f'{label}: {bad} records with a nonzero matrix slot')
        entries, blob = el.build_texture_blob(None, b.tex_table, gs_path)
        for cap in caps:
            if cap.gs is None:
                continue
            e2, b2 = el.build_texture_blob(None, b.tex_table, cap.gs)
            for x, y in zip(entries, e2):
                if blob[x['off']:x['off'] + 4 * x['w'] * x['h']] != b2[y['off']:y['off'] + 4 * y['w'] * y['h']]:
                    raise SystemExit(f'{label}: disc texture differs from {cap.name} GS freeze')
                tex_checked += 1
        stem = Path(label).name.split('.')[0]
        path = level / f'{n:02d}_{stem}.emdl'
        write_zone(el, path, b, entries, blob, prim, alphas, label)
        no_tex = sum(1 for t in b.tex if t == b.NO_TEX)
        zone_rows.append(dict(zone=path.name, source=label, objects=len(oids), first_object=min(oids),
                              last_object=max(oids), records=recs, vertices=len(b.pos),
                              triangles=len(b.tris) // 3, textures=len(b.tex_table),
                              untextured_vertices=no_tex, sha256=C.sha(path.read_bytes())))
        print(f'{path.name}: {len(oids)} objects, {len(b.pos)} verts, {len(b.tris) // 3} tris, '
              f'{len(b.tex_table)} textures')

    lo, hi = bank, max(o + 0x40 + UNIT * n for _i, o, n in objects)
    bank_bytes = image.read(lo, hi - lo)
    (level / 'static_bank.emsc').write_bytes(C.emsc(lo, bank_bytes))
    dyn_bytes = image.read(dyn_base, 0x10 + DYN_ENTRY * len(dyn))
    (level / 'dynamic_objects.emsc').write_bytes(C.emsc(dyn_base, dyn_bytes))

    table, count, hulls, cells, cell_rows = check_cells(image, caps, elf)
    (out / 'area01_cells.bin').write_bytes(cells)
    flagged = {u: f for u, (_s, _e, f) in hulls.items() if f & 1}
    coll = None
    if not args.skip_collision:
        files, grid_hdr, log = run_collision(image, caps, out / 'area01.emcl')
        coll = dict(files=files, grid_header=grid_hdr, log=log.strip().splitlines(),
                    sha256=C.sha((out / 'area01.emcl').read_bytes()))
        print(f'area01.emcl: {files}; ' + ' '.join(l for l in log.splitlines() if l.startswith('wrote')))

    background = check_background(caps)
    if any(r['armed'] for r in background):
        raise SystemExit('a capture arms the background; run export_level.py --background for it')
    room = check_ctx_room_block(caps, elf)

    report = dict(
        area=C.AREA, sub=C.SUB, load_map=[dict(address=hex(a), file=l, file_offset=hex(o), bytes=s)
                                          for a, _p, o, s, l in lmap],
        load_info={k: (hex(v) if isinstance(v, int) else v) for k, v in info.items()},
        load_map_bytes_equal=load_rows, descriptor_buffer=buffers,
        static_bank=dict(address=hex(bank), objects=len(objects), grid=hex(grid), span=[hex(lo), hex(hi)],
                         sha256=C.sha(bank_bytes)),
        dynamic_list=dict(address=hex(dyn_base), entries=len(dyn), sha256=C.sha(dyn_bytes),
                          note='kernel 0x00237450 with the entry +0x34 translation; exported raw, not as EMDL'),
        kicks=kicks, distinct_level_refs=touched,
        level_state=dict(prim=hex(prim), test_1=hex(el.GS_CLASS0_TEST), alpha_1=hex(el.GS_CLASS0_ALPHA),
                         tex1_1=hex(el.GS_CLASS0_TEX1), clamp_1=hex(el.GS_CLASS0_CLAMP)),
        textures=dict(source='level-load GS upload replayed from the disc image', transfers=transfers,
                      sections=sections, texture_comparisons_with_captures=tex_checked),
        zones=zone_rows,
        cells=dict(address=hex(table), count=count, bytes=len(cells), sha256=C.sha(cells),
                   bit29_uids=sorted(flagged),
                   moved_uids=sorted({u for r in cell_rows for u in r['moved_hulls']}),
                   captures=cell_rows),
        collision=coll, background=background, ctx_room_block=room)
    (level / 'level.json').write_text(json.dumps(report, indent=1, default=str) + '\n')
    print(f'static bank {bank:#x}: {len(objects)} objects -> {len(zone_rows)} zone EMDLs; '
          f'{sum(r["level_kicks"] for r in kicks)} level kicks in {len(caps)} captures inside the bank; '
          f'{tex_checked} texture decodes equal the GS freezes; cells {count} uids '
          f'({len(cells)} bytes); background not armed in any capture; ctx +0xA0..+0xFF rebuilt by 001D8FD0 '
          f'from room entry {sorted({r["room_entry"] for r in room})} in {len(room)} captures')
    return 0


def file_of_order(label, lmap):
    for k, (_a, _p, _o, _s, l) in enumerate(lmap):
        if l == label:
            return k
    return len(lmap)


if __name__ == '__main__':
    sys.exit(main())
