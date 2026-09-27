#!/usr/bin/env python3
"""Check the exported AREA01 sub-0 assets (assets/area01/, docs/AREA01_ASSETS.md)
against the recorded original captures, and load each one through the port's
own loader.

Inputs (all local, all the user's): the assets written by
tools/export_area01_level.py, export_area01_tables.py and export_area01_sfx.py,
the extracted disc files, the pinned boot ELF and the AREA01 captures
(../Extermination/build/s87/route_a01/<beat>/ and route/15_level_exit/).
Missing inputs print SKIPPED and exit 0.

Checks (every comparison is exact):
  loaders   each file goes through the port loader of its format, compiled
            privately from src/ (em_model_load, em_collision_load,
            em_actor_roster_load, em_spawn_table_load + em_spawn_table_read,
            em_world_models_parse, em_script_image_load, em_sfx_registry_load,
            em_message_live_install); em_actor_cells_load's verdict on the
            AREA01 directory is printed (see docs/AREA01_ASSETS.md, bit 29)
  load map  in address order as built, no entry overlapping the next;
            every mapped disc byte equals RAM; the only allowance is the
            cell directory's own bytes (checked below), clipped per byte
  level     the static bank and dynamic list (EMSC header, full length)
            equal RAM at D_0028A5A0 / D_0028A5A4; every zone EMDL equals a
            rebuild from the bank (header, bone parents, clip block,
            positions, colours, UVs, bone slots, texture slots, indices,
            identity palette, GS codes, length) and its texels the GS freeze
            decode of EVERY capture of the run (each must have a freeze);
            every textured level-kernel kick REFs a bank
            object with exactly the class-0 GS state; no record has a
            matrix slot; one zone file per source file, named after it
  emcl      the whole file equals a rebuild from the RAM grid D_0028A598
            names (emcl_from_ram), byte for byte; D_0028A598 is pinned to
            0x17C7940, the grid block must lie in the load map and every
            node's index / edge-normal run inside its section; a grid whose
            float de-duplication would change a vertex's bits is refused
  cells     the file equals the disc bytes and ends where the directory
            ends; every capture's directory pointer equals the arrival's;
            every byte of every RAM directory equals the disc, or,
            for a moved hull, the ORIGINAL 001A2370 run for the live owner
            with that owner's matrix in the same capture; the moved set
            equals level.json
  tables    roster: the whole file equals the rebuild by the original's
            walks over the pinned ELF + overlay and over each capture's RAM;
            both EMSP files: the window set equals a walk over the pinned
            ELF, every window equals RAM; scripts and overlay data: extent
            from the pinned module, bytes equal the module and the arrival
            RAM; messages: every byte against RAM, counts from the ELF, bank
            sizes from the banks' own headers; world models (header, count,
            span = the bank's extent over RAM, wm_span); the static bank /
            dynamic list lengths from RAM; live
            placement nodes carry the record fields 001B6990 copies, and
            each capture has exactly 49 of them, 46 at rest
  sfx       area01_banks.bin equals the disc container over its full
            length; the bound bank headers in RAM equal its rows;
            sfx_registry.emsr equals an independent RAM re-derivation
  ctx       ORIGINAL 001D8FD0 rebuilds the captured ctx +0xA0..+0xFF from
            the area's room-table entry (flipping the entry changes it)
  canary    run_checks again over [the arrival, a planted copy of it]: each
            per-capture comparison is planted in the copy only (RAM,
            scratchpad or GS freeze) and must report for the copy and stay
            silent for the arrival; file plants (a copy of the tree under
            build/) only where the comparison is not per capture; a
            matrix-slot bit in the bank (its own check_level call); every
            port loader must refuse a broken copy of its file
  controls  a changed input in each compared section (file or RAM side) is
            detected by its comparator, including the far end (last
            vertex, index, record, window, span and bank byte) and the
            extents docs/AREA01_ASSETS.md lists (a window, record, entry or
            bank one unit short), plus the killing input of each named
            mutation survivor (docs, "Close-out"). The mutation claim is
            only the one the doc states: the listed mutants, not all mutations.
Unchecked side files: scene.txt, level/*.gsmat.json, world_models.json,
sfx/sfx_registry.json, sfx/banks.json, tables.json, level.json (except
cells.moved_uids).
Default: 4 captures (the arrival, a01_05, a01_s1, a01_s3); EM_TEST_FULL=1: all 12.
"""
from __future__ import annotations

import contextlib
import copy
import ctypes as CT
import io
import json
import os
import struct
import subprocess
import sys
import time
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area01_common as C  # noqa: E402
from reference_mode import FULL, MODE  # noqa: E402

# EM_AREA01_ASSETS: check another export tree (mutation sweeps); default assets/area01
A = Path(os.environ.get('EM_AREA01_ASSETS') or C.OUT).resolve()
BUILD = C.ROOT / 'build/area01/assets/test'
CANARY = BUILD / f'canary-{os.getpid()}'   # the canary's copy of the export tree (removed after each run)
QUICK = ('15_level_exit', 'a01_05_npc_bridge_talk', 'a01_s1_sentry_doc', 'a01_s3_fire_contact')
LOADER_SOURCES = ('src/game/em_actor_roster.c', 'src/game/em_spawn_table.c', 'src/game/em_script.c',
                  'src/game/em_owner_draw_original.c', 'src/game/em_collision.c', 'src/em_model.c',
                  'src/game/em_actor_collision.c', 'src/game/em_sfx_bank.c', 'src/game/em_message_live.c',
                  'src/game/em_message_service.c', 'src/game/em_message_draw_original.c',
                  'src/game/em_message_glyph_original.c')
NOOP_STUBS = {'em_frame_set_message_service'}   # a setter the message loader calls
FAILS = []


def check(cond, what):
    if not cond:
        FAILS.append(what)
        print('FAIL:', what)
    return cond


# ---------------------------------------------------------------------------
# Port loaders


def build_loaders():
    """The loaders from src/, linked with abort() stubs for every other
    symbol they reference (a stub that runs fails the test loudly). The
    library is rebuilt whenever any source, header or this file is newer."""
    BUILD.mkdir(parents=True, exist_ok=True)
    src = [str(C.ROOT / s) for s in LOADER_SOURCES]
    lib = BUILD / 'loaders.dylib'
    inputs = [Path(x) for x in src] + list((C.ROOT / 'src').rglob('*.h')) + [Path(__file__)]
    if lib.exists() and lib.stat().st_mtime > max(x.stat().st_mtime for x in inputs):
        return CT.CDLL(str(lib))
    probe = BUILD / 'probe.dylib'
    flags = ['-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC', '-I' + str(C.ROOT / 'src')]
    subprocess.run(['cc', *flags, '-Wl,-undefined,dynamic_lookup', *src, '-o', str(probe)], check=True)
    undefined = subprocess.run(['nm', '-u', str(probe)], check=True, capture_output=True, text=True).stdout
    names = sorted({n[1:] for n in undefined.split() if n.startswith('_em_')})
    stubs = BUILD / 'stubs.c'
    stubs.write_text('#include <stdlib.h>\n' + ''.join(
        f'void {n}(void);\nvoid {n}(void) {{ {"" if n in NOOP_STUBS else "abort();"} }}\n' for n in names))
    subprocess.run(['cc', *flags, *src, str(stubs), '-o', str(lib)], check=True)
    return CT.CDLL(str(lib))


def check_loaders(lib):
    big = lambda: CT.create_string_buffer(16 << 20)
    def rc(fn, *args):
        f = getattr(lib, fn)
        f.restype = CT.c_int
        return f(*args)
    p = lambda rel: str(A / rel).encode()
    out = {}
    out['roster'] = rc('em_actor_roster_load', big(), p('roster.emro')) == 0
    table = big()
    ok = rc('em_spawn_table_load', table, p('spawn_table.emsp')) == 0
    lib.em_spawn_table_read.restype = CT.c_void_p
    lib.em_spawn_table_read.argtypes = [CT.c_void_p, CT.c_uint32, CT.c_uint32]
    out['spawn'] = ok and bool(lib.em_spawn_table_read(table, 0x24B1A0, 10 * 0x30))
    doors = big()
    ok = rc('em_spawn_table_load', doors, p('door_destinations.emsp')) == 0
    out['doors (EMSP windows)'] = ok and bool(lib.em_spawn_table_read(doors, 0x24E140 + 4, 4)) and \
        bool(lib.em_spawn_table_read(doors, 0x24DFA0, 0x20))
    wm = (A / 'world_models.emwm').read_bytes()
    out['world models'] = rc('em_world_models_parse', big(), wm, CT.c_size_t(len(wm))) == 0
    for rel in ('area01_scripts/scripts.emsc', 'overlay_data.emsc', 'level/static_bank.emsc',
                'level/dynamic_objects.emsc'):
        out[rel] = rc('em_script_image_load', big(), p(rel)) == 1
    out['emcl'] = rc('em_collision_load', big(), p('area01.emcl')) == 0
    for z in sorted(str(x) for x in (A / 'level').glob('*.emdl')):
        out[Path(z).name] = rc('em_model_load', big(), z.encode()) == 0
    out['sfx registry'] = rc('em_sfx_registry_load', big(), p('sfx/sfx_registry.emsr')) == 1
    out['message data'] = rc('em_message_live_install', p('message_data.emmd')) == 1
    for name, ok in out.items():
        check(ok, f'port loader rejects {name}')
    cells = rc('em_actor_cells_load', big(), p('area01_cells.bin'))
    return out, cells


# ---------------------------------------------------------------------------
# Level


def emdl_parts(buf):
    bc, vc, ic, fc = struct.unpack_from('<4I', buf, 4)
    tc, flags, cc = struct.unpack_from('<3I', buf, 24)
    tex = 36 + 4 * bc
    verts = tex + 16 * tc + 16 * cc
    idx = verts + 40 * vc
    blob = idx + 4 * ic + 64 * fc * bc
    return dict(flags=flags, tc=tc, vc=vc, ic=ic, tex=tex, verts=verts, idx=idx, blob=blob)


IDENTITY = struct.pack('<16f', 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)


def texture_key(f):
    """What export_level.build_texture_blob decodes one texture from: its
    GS base, width, format, size and CLUT base."""
    return f['tbp0'], f['tbw'], f['psm'], f['tw'], f['th'], f['cbp']


_TEXELS = {}


def gs_texels(el, gs_path, tables):
    """Decode, from the GS freeze `gs_path`, every texture of `tables` not yet
    decoded from it (one build_texture_blob call per freeze; the 342 zone
    slots name 137 distinct textures). {(freeze, texture_key): RGBA}."""
    missing = {}
    for table in tables:
        for f in table:
            if (str(gs_path), texture_key(f)) not in _TEXELS:
                missing.setdefault(texture_key(f), f)
    if missing:
        fs = list(missing.values())
        entries, blob = el.build_texture_blob(None, fs, Path(gs_path))
        for f, x in zip(fs, entries):
            _TEXELS[(str(gs_path), texture_key(f))] = blob[x['off']:x['off'] + 4 * x['w'] * x['h']]
    return _TEXELS


_ZONE_EQ = {}


def zone_problems(el, name, buf, b, gs_caps, prim):
    """_zone_problems, memoised on its inputs (the canary re-checks the 11
    unchanged zone files). gs_caps = ((capture name, GS freeze path), ...)."""
    key = (name, bytes(buf), b, gs_caps, prim)      # b by identity; the key holds it alive
    if key not in _ZONE_EQ:
        _ZONE_EQ[key] = _zone_problems(el, name, buf, b, gs_caps, prim)
    return _ZONE_EQ[key]


def zone_equal(el, L, buf, b, gs_caps, prim):
    return not zone_problems(el, 'zone', buf, b, gs_caps, prim)


def _zone_problems(el, name, buf, b, gs_caps, prim):
    """The EMDL equals a rebuild (b = MeshBuilder over the bank objects),
    every byte of it: the header (one bone plus the identity slot, one
    frame, 30.0 fps -- the static-geometry constants write_emdl is given --
    the texture count, flags COLOR | GSMAT, one clip), the bone parents, the
    texture entries (w = 1 << tw, h = 1 << th, blob offset = the sum of the
    earlier textures' 4 * w * h bytes, GS code), the clip block, every
    vertex, the indices and the two identity palette matrices; the texel
    blob ends the file, and every texture's texels equal their decode from
    the GS freeze of EVERY capture in gs_caps. [] when equal; else one
    problem for the structure, or one per (texture, capture) texel
    difference."""
    rebuild = [f'{name} differs from a rebuild']
    e = emdl_parts(buf)
    if e['tc'] != len(b.tex_table) or e['vc'] != len(b.pos) or e['ic'] != len(b.tris):
        return rebuild
    head = struct.unpack_from('<4s4If3I', buf, 0)
    if head != (b'EMD3', 2, len(b.pos), len(b.tris), 1, 30.0, len(b.tex_table), 1 | el.EMDL_FLAG_GSMAT, 1):
        return rebuild
    if struct.unpack_from('<2i', buf, 36) != (-1, -1):
        return rebuild
    if struct.unpack_from('<3If', buf, e['tex'] + 16 * e['tc']) != (0, 0, 1, 30.0):
        return rebuild
    for i in range(e['vc']):
        v = struct.unpack_from('<8f2I', buf, e['verts'] + 40 * i)
        want = struct.unpack('<3f3f2f', struct.pack('<3f3f2f', *b.pos[i], *b.col[i], *b.uv[i]))
        bone = b.bone[i]
        slot = ((bone & 0xFFFFFF) if 0 <= (bone & 0xFFFFFF) < 1 else 1) | (bone & 0xFF000000) if bone >= 0 else 1
        if v[:8] != want or v[8] != slot or v[9] != (b.tex[i] if b.tex[i] != b.NO_TEX else 0xFFFFFFFF):
            return rebuild
    if list(struct.unpack_from(f"<{e['ic']}I", buf, e['idx'])) != list(b.tris):
        return rebuild
    if buf[e['idx'] + 4 * e['ic']:e['blob']] != IDENTITY * 2:
        return rebuild
    at, sizes = 0, []
    for i, f in enumerate(b.tex_table):
        w, h, off, code = struct.unpack_from('<4I', buf, e['tex'] + 16 * i)
        if (w, h, off) != (1 << f['tw'], 1 << f['th'], at):
            return rebuild
        want = el.gs_material_code(el.GS_CLASS0_TEST, prim, el.GS_CLASS0_ALPHA, f['key'],
                                   el.GS_CLASS0_TEX1, el.GS_CLASS0_CLAMP)
        if code != want:
            return rebuild
        sizes.append((off, 4 * w * h))
        at += 4 * w * h
    if len(buf) != e['blob'] + at:
        return rebuild
    out = []
    for cap_name, gs_path in gs_caps:
        texels = gs_texels(el, gs_path, [b.tex_table])
        for i, (f, (off, size)) in enumerate(zip(b.tex_table, sizes)):
            if buf[e['blob'] + off:e['blob'] + off + size] != texels[(gs_path, texture_key(f))]:
                out.append(f'{name}: texture {i} texels differ from the {cap_name} GS freeze')
    return out


def emsc_header_problems(label, blob):
    """An EMSC data window's header: magic, version 1, the entry word equal
    to the base (a data window has no entry point; the exporters write the
    base there), and a file length of exactly the header plus the window."""
    if len(blob) < 20:
        return [f'{label}: {len(blob)}-byte EMSC']
    magic, version, base, entry, length = struct.unpack_from('<4s4I', blob, 0)
    if (magic, version, entry) != (b'EMSC', 1, base) or len(blob) != 20 + length:
        return [f'{label}: EMSC header {(magic, version, hex(base), hex(entry), length)} / {len(blob)} bytes']
    return []


def emsc_problems(label, blob, caps, window, disc=None):
    """The EMSC header, the window's extent (base, length) = `window`, the
    window against `disc` (the module bytes, when given) and against RAM in
    every capture given."""
    out = emsc_header_problems(label, blob)
    base, d = emsc_window(blob)
    if (base, len(d)) != window:
        out.append(f'{label}: window {base:#x}+{len(d):#x}, not {window[0]:#x}+{window[1]:#x}')
    if disc is not None and d != disc:
        out.append(f'{label} differs from the disc overlay')
    for cap in caps:
        if cap.ram[base:base + len(d)] != d:
            out.append(f'{cap.name}: {label} differs from RAM')
    return out


def bank_end(ram, base):
    """Where the static bank at `base` ends: the last byte any object's
    units reach. 001C6120's lookup: object = base + (word[1 + id] >> 2 << 2)
    for the ids 0 .. word 0 - 1; an object's units (word 0 of the object,
    0x820 bytes each) start at +0x40."""
    objects = [base + (C.s32(ram, base + 4 + 4 * i) >> 2 << 2) for i in range(C.u32(ram, base))]
    return max((o + 0x40 + 0x820 * C.u32(ram, o) for o in objects), default=base)


def level_bank_problems(bank_img, dyn_img, caps):
    """The static bank and the dynamic list (EMSC windows) against RAM, over
    their full length, the pointers D_0028A5A0 / D_0028A5A4 to them, and
    their extents from RAM in every capture: the bank ends at bank_end, the
    list is its 16-byte head plus word 0 entries of 0x860 bytes."""
    out = emsc_header_problems('static bank', bank_img) + emsc_header_problems('dynamic list', dyn_img)
    base, _entry, length = struct.unpack_from('<3I', bank_img, 8)
    dbase, _e, dlen = struct.unpack_from('<3I', dyn_img, 8)
    if len(bank_img) != 20 + length or len(dyn_img) != 20 + dlen:
        out.append('static bank / dynamic list EMSC length')
    for cap in caps:
        if cap.ram[base:base + length] != bank_img[20:20 + length]:
            out.append(f'{cap.name}: static bank differs from RAM')
        if C.u32(cap.ram, 0x28A5A0) != base:
            out.append(f'{cap.name}: D_0028A5A0')
        if C.u32(cap.ram, 0x28A5A4) != dbase:
            out.append(f'{cap.name}: D_0028A5A4')
        if cap.ram[dbase:dbase + dlen] != dyn_img[20:20 + dlen]:
            out.append(f'{cap.name}: dynamic list differs from RAM')
        if base + length != bank_end(cap.ram, base):
            out.append(f'{cap.name}: static bank length {length:#x}, the objects end at +{bank_end(cap.ram, base) - base:#x}')
        if dlen != 0x10 + 0x860 * C.u32(cap.ram, dbase):
            out.append(f'{cap.name}: dynamic list length {dlen:#x} for {C.u32(cap.ram, dbase)} entries')
    return out


_ZONES = {}


def gs_captures(caps):
    """((capture name, GS freeze path), ...) of every capture of the run. A
    capture without a GS freeze stops the run: its texels could not be
    compared, and skipping it would pass them unchecked."""
    missing = [c.name for c in caps if c.gs is None]
    if missing:
        raise SystemExit(f'no GS freeze in {missing}')
    return tuple((c.name, str(c.gs)) for c in caps)


def level_gs_state(el, prim):
    """The class-0 state the zone GS codes are computed from."""
    return (prim, el.GS_CLASS0_TEST, el.GS_CLASS0_ALPHA, el.GS_CLASS0_TEX1, el.GS_CLASS0_CLAMP)


def gs_state_problems(states, want):
    """Every textured level kick of every capture must draw with exactly
    the state `want` (PRIM, TEST_1, ALPHA_1, TEX1_1, CLAMP_1)."""
    return [] if set(states) == {want} else [f'level GS state {states}']


def check_level(el, L, caps, image):
    bank_img = (A / 'level/static_bank.emsc').read_bytes()
    dyn_img = (A / 'level/dynamic_objects.emsc').read_bytes()
    for problem in level_bank_problems(bank_img, dyn_img, caps):
        check(False, problem)
    base, _entry, length = struct.unpack_from('<3I', bank_img, 8)
    bank = bank_img[20:20 + length]
    read = lambda a, n: bank[a - base:a - base + n]
    objects = L.bank_objects(read, base)
    dbase, _e, dlen = struct.unpack_from('<3I', dyn_img, 8)
    dyn = L.dynamic_entries(lambda a, n: dyn_img[20 + a - dbase:20 + a - dbase + n], dbase)
    kicks, states, _touched = L.check_kicks(caps, objects, dyn)
    prim = el.level_template_prim(el.BootElf(C.ELF_PATH))
    for problem in gs_state_problems(states, level_gs_state(el, prim)):
        check(False, problem)

    class BankImage:                     # the LoadedImage interface over the asset
        def read(self, a, n):
            return read(a, n)

    def file_of(a):
        return image.locate(a)[4]
    key = (bank_img, dyn_img)            # the rebuild depends only on these (the canary reuses it)
    if key not in _ZONES:
        _ZONES[key] = L.build_zones(el, BankImage(), objects, file_of)
    zones = _ZONES[key]
    # the texels against the GS freeze of every capture of the run
    gs_caps = gs_captures(caps)
    for _name, gs_path in gs_caps:
        gs_texels(el, gs_path, [z[0].tex_table for z in zones.values()])
    files = sorted(str(x) for x in (A / 'level').glob('*.emdl'))   # Path.glob: A may hold glob characters
    check(len(files) == len(zones), f'{len(files)} zone files for {len(zones)} source files')
    order = [l for _a, _p, _o, _s, l in image.map]
    by_label = sorted(zones.items(), key=lambda z: order.index(z[0]))
    first_zone = None
    for path, (label, (b, _ids, bad, _recs)) in zip(files, by_label):
        check(bad == 0, f'{label}: records with a matrix slot')
        buf = Path(path).read_bytes()
        check(Path(path).name.split('_', 1)[1].startswith(Path(label).name.split('.')[0]),
              f'{path}: zone order')
        for problem in zone_problems(el, Path(path).name, buf, b, gs_caps, prim):
            check(False, problem)
        if first_zone is None:
            first_zone = (buf, b)
    return dict(objects=len(objects), zones=len(files), kicks=sum(k['level_kicks'] for k in kicks),
                dynamic_kicks=sum(k['dynamic_kicks'] for k in kicks)), first_zone, gs_caps, prim


# ---------------------------------------------------------------------------
# Collision


def emcl_from_ram(ram):
    """The whole EMCL rebuilt from the RAM grid D_0028A598 names, written from
    the container layout (export_collision.py's module docstring) without
    running the exporter: header, bbox, the vertex pool (the grid vertices
    de-duplicated by float equality in first-use order), one 24-byte record
    per node (plane, first index, vertex count, set 4, attr +0x1A, class
    +0x1B), the node's s16 vertex indices remapped into the pool, the node's
    edge normals, then the EMRK (grid vertices, node words +0x00..+0x17,
    the 12 tables) and EMAX (node axes) sections. The grid has no cell
    n-gons in AREA01, so every polygon is a grid node."""
    base = C.u32(ram, 0x28A598)
    vert_off, vcnt, en_off, _encnt, il_off, _ilcnt, tab_off, form, node_off = struct.unpack_from('<9I', ram, base)
    n = C.s16(ram, base + 0x24)
    if form != 0xC:
        raise ValueError(f'grid form {form:#x}')
    graw = [ram[base + vert_off + 12 * i:base + vert_off + 12 * i + 12] for i in range(vcnt)]
    pool, lut, remap = [], {}, []
    for i, raw in enumerate(graw):
        key = struct.unpack('<3f', raw)
        if key not in lut:
            lut[key] = len(pool)
            pool.append(raw)
        remap.append(lut[key])
        # export_collision refuses a grid whose de-duplication would change a
        # vertex's bits (+0.0 and -0.0 are float-equal): so does the rebuild
        if pool[remap[-1]] != raw:
            raise ValueError(f'grid vertex {i}: the de-duplicated pool changes its bits')
    records, indices, edges = bytearray(), [], bytearray()
    for k in range(n):
        node = base + node_off + 64 * k
        count = ram[node + 0x18]
        iloff, enoff = struct.unpack_from('<2I', ram, node + 0x1C)
        records += ram[node + 0x24:node + 0x34] + struct.pack('<IBBBB', len(indices), count, 4,
                                                              ram[node + 0x1A], ram[node + 0x1B])
        for j in range(count):
            i = C.s16(ram, base + il_off + iloff + 2 * j)
            if not 0 <= i < vcnt:
                raise ValueError(f'node {k}: vertex index {i}')
            indices.append(remap[i])
            edges += ram[base + en_off + enoff + 12 * j:base + en_off + enoff + 12 * j + 12]
    floats = [struct.unpack('<3f', v) for v in pool]
    bbox = [min(v[a] for v in floats) for a in range(3)] + [max(v[a] for v in floats) for a in range(3)]
    out = bytearray(struct.pack('<4sIIIII', b'EMCL', 1, len(pool), n, len(indices), 0xF))
    out += struct.pack('<6f', *bbox) + b''.join(pool) + records
    out += struct.pack(f'<{len(indices)}H', *indices) + (b'\0\0' if len(indices) & 1 else b'')
    out += edges
    rank = bytearray(b'EMRK' + struct.pack('<4I', 1, n, 0, vcnt) + b''.join(graw))
    rank += b''.join(ram[base + node_off + 64 * k:base + node_off + 64 * k + 0x18] for k in range(n))
    rank += ram[base + tab_off:base + tab_off + 24 * n]
    rank += bytes(-len(rank) % 4)
    out += rank + b'EMAX' + struct.pack('<2I', 1, n)
    out += b''.join(ram[base + node_off + 64 * k + 0x34:base + node_off + 64 * k + 0x40] for k in range(n))
    return bytes(out)


# D_0028A598 (world-section directory word 0, the grid 00199C50 stages into
# the scratchpad) is 0x17C7940 = chunk05.n0/f12 + 0x19800 in all 12 captures.
# The code that fills the directory at level load is not identified
# (FINDINGS), so the checker pins the measured address and requires the
# whole grid block to lie in the load map outside the cell-directory
# allowance: then every grid byte emcl_from_ram reads through the header
# equals the disc in every capture that passes the load-map check.
GRID = 0x17C7940


def grid_problems(ram, image, allow):
    """D_0028A598 == GRID, and the grid's header, vertex pool, edge normals,
    index list, rank tables and node array (from the disc header at GRID)
    inside the load map and outside `allow` = (lo, hi)."""
    if C.u32(ram, 0x28A598) != GRID:
        return [f'D_0028A598 = {C.u32(ram, 0x28A598):#x}, not {GRID:#x}']
    hdr = image.read(GRID, 0x28)
    v, vcnt, en, encnt, il, ilcnt, tab, _form, node = struct.unpack_from('<9I', hdr)
    n = C.s16(hdr, 0x24)
    out = []
    for name, off, size in (('header', 0, 0x28), ('vertices', v, 12 * vcnt), ('edge normals', en, 12 * encnt),
                            ('indices', il, 2 * ilcnt), ('tables', tab, 24 * n), ('nodes', node, 64 * n)):
        lo, hi = GRID + off, GRID + off + size
        try:
            image.read(lo, size)
        except ValueError:
            out.append(f'grid {name} outside the load map')
        if lo < allow[1] and hi > allow[0]:
            out.append(f'grid {name} inside the cell-directory allowance')
    # every node's index and edge-normal run (count byte +0x18, offsets
    # +0x1C / +0x20, as emcl_from_ram reads them) inside its own section:
    # then every byte emcl_from_ram reads lies in the sections above
    try:
        nodes = image.read(GRID + node, 64 * n) if n > 0 else b''
    except ValueError:
        nodes = b''
    for k in range(len(nodes) // 64):
        count = nodes[64 * k + 0x18]
        iloff, enoff = struct.unpack_from('<2I', nodes, 64 * k + 0x1C)
        if iloff + 2 * count > 2 * ilcnt or enoff + 12 * count > 12 * encnt:
            out.append(f'grid node {k}: index / edge-normal run outside its section')
    return out


def emcl_sections(emcl):
    """[(name, start)] of the EMCL, for naming the first differing byte."""
    v, p, i = struct.unpack_from('<3I', emcl, 8)
    polys = 0x30 + 12 * v
    idx = polys + 24 * p
    edges = idx + 2 * i + (2 if i & 1 else 0)
    rank = edges + 12 * i
    return [('header', 0), ('bbox', 0x18), ('vertices', 0x30), ('polygons', polys), ('indices', idx),
            ('edge normals', edges), ('rank section', rank), ('end', len(emcl))]


def emcl_equal(emcl, ram):
    """None when the EMCL equals the independent RAM rebuild byte for byte,
    else where it first differs."""
    try:
        want = emcl_from_ram(ram)
    except (ValueError, struct.error) as error:
        return f'RAM grid: {error}'
    if emcl == want:
        return None
    k = next((j for j in range(min(len(emcl), len(want))) if emcl[j] != want[j]), min(len(emcl), len(want)))
    try:
        name = [n for n, at in emcl_sections(want) if at <= k][-1]
    except struct.error:
        name = '?'
    return f'first difference at +{k:#x} ({name}); sizes {len(emcl)} / {len(want)}'


def hulls_inside_table(L, cells):
    """The uids whose hull starts before the end of the count and uid words."""
    count, hulls, _size = L.cell_directory(cells, 0)
    return sorted(u for u, (s, _e, _f) in hulls.items() if s < 4 + 4 * count)


def cells_problems(L, elf, cells, image, caps):
    """The cell directory file against the disc bytes the load map names,
    and against every byte of every capture's RAM directory (the ORIGINAL
    001A2370 derivation for the re-transformed hulls, export_area01_level.
    verify_cell_directory). Returns ([problems], {uid: [captures]} moved)."""
    problems = []
    table = C.u32(caps[0].spad, L.SPAD_CELLS)
    try:
        disc = image.read(table, len(cells))
    except ValueError as error:
        return [f'cells: {error}'], {}
    if cells != disc:
        k = next(j for j in range(len(cells)) if cells[j] != disc[j])
        problems.append(f'area01_cells.bin differs from the disc bytes at +{k:#x}')
    try:
        # every hull after the count and uid words (the first starts exactly
        # there, at 4 + 4 * 38): so the directory check compares those words
        # with the disc in every capture, and no derivation may change them
        inside = hulls_inside_table(L, cells)
        if inside:
            problems.append(f'cells: hulls {inside} start inside the uid table')
        rows, more = L.verify_cell_directory(elf, cells, caps)
    except (AssertionError, IndexError, struct.error) as error:
        return problems + [f'cells: the directory does not parse ({error!r})'], {}
    moved = {}
    for r in rows:
        for u in r['moved_hulls']:
            moved.setdefault(u, []).append(r['capture'])
    return problems + more, moved


# ---------------------------------------------------------------------------
# Tables


D_0024D820, D_0024D7C0 = 0x24D820, 0x24D7C0


def roster_image(read):
    """The whole EMRO rebuilt by the original's walks over `read(address,
    size)` (a capture's RAM or the disc ELF + overlay): 001B6910 takes the
    group list D_0024D820[area][sub] and walks each group item until a
    0x2C-byte record whose s16 word 0 is -1, while the list's next word is
    non-zero; 001B6990 takes D_0024D7C0[area][sub] and walks 0x28-byte
    records until one whose s16 word 0 is 0xFF. Neither walk has a bound; a
    walk that leaves RAM or the disc image raises (a clean problem). Header: "EMRO", version 1,
    area, sub, group count, placement count, placement address, 0; then
    (address, records) per group, the group records, the placements."""
    u32 = lambda a: struct.unpack('<I', read(a, 4))[0]
    s16 = lambda a: struct.unpack('<h', read(a, 2))[0]
    groups = []
    registry = u32(D_0024D820 + 4 * C.AREA)
    if registry:
        q = u32(registry + 4 * C.SUB)
        while True:
            item, records = u32(q), []
            while s16(item + 0x2C * len(records)) != -1:
                records.append(read(item + 0x2C * len(records), 0x2C))
            groups.append((item, records))
            q += 4
            if not u32(q):
                break
    paddr, places = u32(u32(D_0024D7C0 + 4 * C.AREA) + 4 * C.SUB), []
    while s16(paddr + 0x28 * len(places)) != 0xFF:
        places.append(read(paddr + 0x28 * len(places), 0x28))
    out = bytearray(b'EMRO' + struct.pack('<IBBHIII', 1, C.AREA, C.SUB, len(groups), len(places), paddr, 0))
    out += b''.join(struct.pack('<II', a, len(r)) for a, r in groups)
    return bytes(out + b''.join(b''.join(r) for _a, r in groups) + b''.join(places))


def roster_equal(blob, ram):
    """Every byte of the EMRO equals the rebuild from this RAM (roster_image):
    the header, the group table and counts, every group and placement
    record, and the length."""
    return blob == roster_image(lambda a, n: ram[a:a + n])


def roster_nodes(T, blob, ram):
    """Live nodes that a placement record spawned keep the record's behaviour,
    +0x9A index, class/model byte +0x03, param +0x0D and +0x54 (001B6990).
    Every matched node is counted; ok is False when any of them lost a
    copied field. Returns (ok, (live, placed), [(record, node)])."""
    groups, count = C.u16(blob, 0x0A), C.u32(blob, 0x0C)
    at = 0x18 + 8 * groups + sum(0x2C * struct.unpack_from('<II', blob, 0x18 + 8 * g)[1] for g in range(groups))
    ok, live, placed, pairs = True, 0, 0, []
    for i in range(count):
        rec = blob[at + 0x28 * i:at + 0x28 * (i + 1)]
        if (C.s16(rec, 0) & 0xFF) == 0x0B:
            continue
        f, pos, rot = T.spawn_fields_placement(rec, i)
        for _slot, a in T.pool_nodes(ram):
            if ram[a + 0x10:a + 0x14] == f[0x10] and ram[a + 0x9A] == i:
                for off in (0x03, 0x0D, 0x54):
                    if ram[a + off:a + off + len(f[off])] != f[off]:
                        ok = False
                live += 1
                pairs.append((i, a))
                placed += ram[a + 0xB0:a + 0xBC] == pos and ram[a + 0xC0:a + 0xCC] == rot
    return ok, (live, placed), pairs


def emsp_windows(blob):
    n = C.u32(blob, 8)
    ranges = [struct.unpack_from('<II', blob, 16 + 8 * k) for k in range(n)]
    at, out = 16 + 8 * n, []
    for a, s in ranges:
        out.append((a, blob[at:at + s]))
        at += s
    return out


D_0024D650, D_0024E140 = 0x24D650, 0x24E140


def area_count(read):
    """The number of area numbers: the index of D_0024E140's first zero word
    (0x17: areas 0..0x16 each name a door row; the disc's OVERLAY directory
    holds AREA00..AREA22)."""
    return next(k for k in range(0x100) if not C.u32(read(D_0024E140 + 4 * k, 4), 0))


def spawn_layout(read):
    """[(address, size)] of the windows of spawn_table.emsp, walked over the
    pinned ELF (`read`, disc_reader) as
    001B07C0 / 001B0250 index the table (table = D_0024D650[area], entries =
    table[room], record = entries + entry * 0x30): D_0024D650[0 .. area
    count); each non-null area's room pointer array up to and including its
    first zero word, at most 4 words (the arrays lie 0x10 apart); each
    distinct room entry array, up to the next one in address order, the last
    one up to the lowest room pointer array or D_0024D650; windows that
    touch are one window."""
    u32 = lambda a: C.u32(read(a, 4), 0)
    n = area_count(read)
    ranges, tables, starts = [(D_0024D650, 4 * n)], [], set()
    for area in range(n):
        table = u32(D_0024D650 + 4 * area)
        if not table:
            continue
        rooms = []
        while len(rooms) < 4 and u32(table + 4 * len(rooms)):
            rooms.append(u32(table + 4 * len(rooms)))
        tables.append((table, 4 * min(4, len(rooms) + 1)))
        starts.update(rooms)
    starts = sorted(starts)
    floor = min([t for t, _s in tables] + [D_0024D650])
    ranges += tables + [(x, (starts[i + 1] if i + 1 < len(starts) else floor) - x) for i, x in enumerate(starts)]
    merged = []
    for a, size in sorted(ranges):
        if merged and a == merged[-1][0] + merged[-1][1]:
            merged[-1] = (merged[-1][0], merged[-1][1] + size)
        else:
            merged.append((a, size))
    return merged


def doors_layout(read):
    """[(address, size)] of door_destinations.emsp: the area's door row
    D_0024E140[area] (001BC150 indexes it), up to the next address the array
    names or the array itself, and the array D_0024E140[0 .. area count)."""
    n = area_count(read)
    ptrs = [C.u32(read(D_0024E140 + 4 * k, 4), 0) for k in range(n)]
    row = ptrs[C.AREA]
    end = min(x for x in set(ptrs) | {D_0024E140} if x > row)
    return sorted([(row, end - row), (D_0024E140, 4 * n)])


def emsp_problems(name, blob, caps, layout):
    """An EMSP file: the header (magic, version 1, the zero word +0x0C), a
    length of exactly the header, the range table and the windows, the
    window set (address and size of each) equal to `layout` (walked over
    the pinned ELF), and EVERY window against RAM in every capture."""
    out = []
    magic, version, n, zero = struct.unpack_from('<4s3I', blob, 0)
    windows = emsp_windows(blob)
    if (magic, version, zero) != (b'EMSP', 1, 0) or \
            len(blob) != 16 + 8 * n + sum(struct.unpack_from('<II', blob, 16 + 8 * k)[1] for k in range(n)):
        out.append(f'{name}: EMSP header / length')
    if [(a, len(d)) for a, d in windows] != layout:
        out.append(f'{name} windows {[(hex(a), len(d)) for a, d in windows]} != the ELF walk '
                   f'{[(hex(a), size) for a, size in layout]}')
    for cap in caps:
        for a, d in windows:
            if cap.ram[a:a + len(d)] != d:
                out.append(f'{cap.name}: {name} window {a:#x} differs')
    return out, windows


def wm_span(ram, table):
    """The bytes the model bank at `table` spans, by the original's reads:
    the table itself (word 0 = the model count, then one offset word per
    model) and, for each model at table + (word[1 + id] >> 2 << 2)
    (001C6120's lookup, the offset word signed), its 0x40-byte header and
    block data (from +0x40, header word 1 quadwords; the header's own end
    is never later) and its skeleton
    (header word 3 = its offset, word 2 records of 0x50 bytes), as the draw
    001CAA00 -> 001CA990 -> 001C7420 / 001CA940 reads them
    (export_world_models' docstring, docs/OWNER_DRAW.md). This is the
    exporter's span rule (model offset + skeleton + 0x50 * bones, the
    largest), written from the layout, not from model_record."""
    count = C.u32(ram, table)
    end = 4 + 4 * count
    for i in range(count):                # a read past RAM raises struct.error (a clean problem)
        off = C.s32(ram, table + 4 + 4 * i) >> 2 << 2
        m = table + off
        end = max(end, off + 0x40 + 16 * C.u32(ram, m + 4),
                  off + C.u32(ram, m + 0xC) + 0x50 * C.u32(ram, m + 8))
    return end


def world_models_problems(wm, caps):
    """world_models.emwm: magic, version 1, the table address (= D_0028A59C
    in every capture), the span length (= the file length - 32, and = the
    bank's extent wm_span over each capture's RAM), the model count (= the
    table's own word 0), the three zero words, and every span byte against
    RAM."""
    out = []
    magic, version, table, span, count, *pad = struct.unpack_from('<4s7I', wm)
    if (magic, version, pad) != (b'EMWM', 1, [0, 0, 0]) or len(wm) != 32 + span or \
            span < 4 or count != C.u32(wm, 32):
        out.append(f'world_models.emwm header {(magic, version, span, count, pad)} / {len(wm)} bytes')
    for cap in caps:
        if C.u32(cap.ram, 0x28A59C) != table or cap.ram[table:table + span] != wm[32:32 + span]:
            out.append(f'{cap.name}: world model bank differs')
        try:
            extent = wm_span(cap.ram, table)
        except struct.error as error:     # a model outside RAM
            extent = repr(error)
        if span != extent:
            shown = f'{extent:#x}' if isinstance(extent, int) else extent
            out.append(f'{cap.name}: world model span {span:#x}, the bank extends {shown}')
    return out, count


def emsc_window(blob):
    base, entry, length = struct.unpack_from('<3I', blob, 8)
    return base, blob[20:20 + length]


# message_data.emmd (export_message_data layout): the header, the colour,
# line-config and template blocks the exporter reads from the ELF, then the
# global and area records, the stream rows D_0026EC60 up to (not including)
# the -1 row, the global bank, the area bank
MSG_TABLES, MSG_STREAMS, MSG_CURSOR = 0x264DD0, 0x26EC60, 0x264D10
MSG_STREAM_NAMES, MSG_STREAM_LISTS = 0x275848, 0x264E40


def message_record_count(read, pointer):
    """A record table ends where the next table the original points at
    begins: every non-zero D_00264DD0 word (the global table and one per
    area), D_00275848[0..2) and D_00264E40[0 .. area count)
    (docs/MESSAGE_SERVICE.md); 8-byte records."""
    u32 = lambda a: C.u32(read(a, 4), 0)
    n = area_count(read)
    starts = {u32(MSG_TABLES + 4 * i) for i in range(1 + n)} | {u32(MSG_STREAM_NAMES + 4 * i) for i in range(2)} | \
        {u32(MSG_STREAM_LISTS + 4 * i) for i in range(n)}
    return (min(x for x in starts if x > pointer) - pointer) // 8


def message_bank_size(ram, bank):
    """A bank's extent from its own header, as the accessors 001FE460 /
    001FE480 / 001FE4B0 / 001FE4D0 reach it (docs/MESSAGE_DRAW.md): h = word
    0 + word 2; the bank is h + word(h) + word(h + 8) bytes."""
    h = C.u32(ram, bank) + C.u32(ram, bank + 8)
    return h + C.u32(ram, bank + h) + C.u32(ram, bank + h + 8)
MSG_BLOCKS = ((36, 0x26EC10, 64), (100, 0x264CD0, 20), (120, 0x264BF0, 20))


def messages_problems(mm, caps, elf):
    """Every byte of message_data.emmd against RAM: the header fields
    (magic, version 1, area 1, the cursor token 0x264D10, the counts and bank
    sizes that the file's own length must add up to), the three ELF blocks,
    the records at D_00264DD0[0] / [2], the stream rows (the RAM row after
    the last one must be the -1 row, and none before it), and both banks at
    *D_0028A4E8 / *D_0028A594. The record counts must equal what the ELF
    gives (message_record_count) and the bank sizes what each RAM bank's own
    header gives (message_bank_size)."""
    out = []
    magic, version, area, gcount, acount, rows, cursor, gbank, abank = struct.unpack_from('<4s8I', mm, 0)
    if (magic, version, area, cursor) != (b'EMMD', 1, C.AREA, MSG_CURSOR):
        out.append(f'message header {(magic, version, area, cursor)}')
    at = 140
    grec, arec = mm[at:at + 8 * gcount], mm[at + 8 * gcount:at + 8 * (gcount + acount)]
    srow = at + 8 * (gcount + acount)
    streams = mm[srow:srow + 16 * rows]
    banks = srow + 16 * rows
    if len(mm) != banks + gbank + abank:
        out.append(f'message_data.emmd length {len(mm)} != {banks + gbank + abank}')
        return out, acount, rows          # the counts do not describe this file; nothing to walk
    gptr = C.u32(elf, MSG_TABLES - C.ELF_VADDR + C.ELF_OFFSET)
    aptr = C.u32(elf, MSG_TABLES + 4 * (C.AREA + 1) - C.ELF_VADDR + C.ELF_OFFSET)
    for cap in caps:
        ram = cap.ram
        if (C.u32(ram, MSG_TABLES), C.u32(ram, MSG_TABLES + 4 * (C.AREA + 1))) != (gptr, aptr):
            out.append(f'{cap.name}: D_00264DD0 pointers')
        for fo, a, n in MSG_BLOCKS:
            if mm[fo:fo + n] != ram[a:a + n]:
                out.append(f'{cap.name}: message block {a:#x} differs')
        if ram[gptr:gptr + len(grec)] != grec or ram[aptr:aptr + len(arec)] != arec:
            out.append(f'{cap.name}: message records differ')
        ends = [k for k in range(rows + 1) if C.s32(ram, MSG_STREAMS + 16 * k) == -1]
        if ram[MSG_STREAMS:MSG_STREAMS + 16 * rows] != streams or ends != [rows]:
            out.append(f'{cap.name}: message stream rows differ (-1 rows at {ends})')
        g0, a0 = C.u32(ram, 0x28A4E8), C.u32(ram, 0x28A594)
        want = (message_record_count(disc_reader(), gptr), message_record_count(disc_reader(), aptr),
                message_bank_size(ram, g0), message_bank_size(ram, a0))
        if (gcount, acount, gbank, abank) != want:
            out.append(f'{cap.name}: message counts / bank sizes {(gcount, acount, gbank, abank)} != RAM {want}')
        if ram[g0:g0 + gbank] != mm[banks:banks + gbank] or \
                ram[a0:a0 + abank] != mm[banks + gbank:banks + gbank + abank]:
            out.append(f'{cap.name}: message banks differ')
    return out, acount, rows


def moved_problem(moved, level):
    """The moved-hull set this run derived against level.json's record."""
    want = level.get('cells', {}).get('moved_uids')
    return None if sorted(moved) == want else f're-transformed hulls {sorted(moved)} != level.json {want}'


def table_windows():
    """The EMSC windows of the overlay, from the pinned disc module and ELF:
    'overlay data' = the module's data section (MWo3 header: load address
    word 2, data size word 4;
    the module is loaded whole, so the data section is its last data-size
    bytes: load + module length - data size); 'scripts' = the first script
    chain entry (the lowest of export_area01_tables.SCRIPT_ENTRIES, the 14
    entries the overlay code starts chains at) up to the placement table
    D_0024D7C0[area][sub]."""
    import export_area01_tables as T
    read = disc_reader()
    ov = _DISC['overlay']
    load, data = C.u32(ov, 8), C.u32(ov, 0x10)
    lo = load + len(ov) - data
    first = min(T.SCRIPT_ENTRIES)
    places = C.u32(read(C.u32(read(D_0024D7C0 + 4 * C.AREA, 4), 0) + 4 * C.SUB, 4), 0)
    return {'overlay data': (lo, data), 'scripts': (first, places - first)}


TABLE_FILES = ('roster.emro', 'spawn_table.emsp', 'door_destinations.emsp', 'area01_scripts/scripts.emsc',
               'overlay_data.emsc', 'message_data.emmd', 'world_models.emwm')


def check_tables(T, caps, arrival, files=None):
    """Returns (report, roster bytes); a table that does not parse is a
    clean FAIL, not a crash. `files` = {relative path: bytes} replaces
    exported files (the controls)."""
    files = files or {}
    read = lambda rel: files[rel] if rel in files else (A / rel).read_bytes()
    rep = {}
    roster = read('roster.emro')
    try:
        return _check_tables(T, caps, arrival, rep, roster, read)
    except (SystemExit, ValueError, KeyError, IndexError, struct.error) as error:
        check(False, f'tables: {error!r}')
        return rep, roster


# Every one of the 12 AREA01 captures has 49 live nodes spawned by a
# placement record, 46 of them still at the record's position and rotation
# (docs/AREA01_ASSETS.md, Roster); the count is asserted, so an empty or
# unmatched pool cannot pass vacuously
ROSTER_LIVE = (49, 46)


_DISC = {}


# SHA-256 of the user's extract/OVERLAY/AREA01.BIN (a hash, not disc data):
# the overlay reference is as fixed as the pinned ELF
OVERLAY_SHA256 = '91269b993bba3a98c6608bc9013a2e2a177d92c2202869489f0de6fd1776f63a'


def pinned_overlay(ov):
    if C.sha(ov) != OVERLAY_SHA256:
        raise SystemExit(f'{C.OVERLAY_PATH}: not the pinned AREA01 overlay')
    return ov


def disc_reader():
    """read(address, size) over the pinned ELF and the pinned disc AREA01
    overlay (loaded whole at its header address)."""
    if 'read' not in _DISC:
        ov = pinned_overlay(C.read_overlay())
        _DISC['overlay'] = ov
        _DISC['read'] = C.static_reader(C.read_elf(), _DISC['overlay'])
    return _DISC['read']


def roster_problems(T, roster, caps):
    """([problems], [(live, placed)] per capture); a roster that does not
    parse is a problem, not a crash. The file must equal the rebuild from
    the disc (ELF + overlay) and from every capture's RAM."""
    out, lives = [], []
    try:
        if roster != roster_image(disc_reader()):
            out.append('roster.emro differs from the rebuild from the disc')
        for cap in caps:
            if not roster_equal(roster, cap.ram):
                out.append(f'{cap.name}: roster records differ from RAM')
            ok, live, _pairs = roster_nodes(T, roster, cap.ram)
            if not ok:
                out.append(f'{cap.name}: a live placement node lost a copied field')
            if live != ROSTER_LIVE:
                out.append(f'{cap.name}: placement nodes live / at the record position {live} != {ROSTER_LIVE}')
            lives.append(live)
    except (ValueError, IndexError, struct.error) as error:
        out.append(f'roster: {error!r}')
    return out, lives


def _check_tables(T, caps, arrival, rep, roster, read):
    problems, rep['placement_nodes_live'] = roster_problems(T, roster, caps)
    for problem in problems:
        check(False, problem)
    for name, layout in (('spawn_table.emsp', spawn_layout), ('door_destinations.emsp', doors_layout)):
        problems, windows = emsp_problems(name, read(name), caps, layout(disc_reader()))
        for problem in problems:
            check(False, problem)
        rep[name] = len(windows)
    # the scripts and overlay data are the load-time image: later beats show
    # words the game rewrote (docs/AREA01_ASSETS.md), so only the arrival;
    # both windows must also equal the module bytes on the disc
    windows = table_windows()
    for label, rel in (('scripts', 'area01_scripts/scripts.emsc'), ('overlay data', 'overlay_data.emsc')):
        lo, size = windows[label]
        disc = disc_reader()(lo, size)
        for problem in emsc_problems(label, read(rel), [arrival], (lo, size), disc):
            check(False, problem)
    base, d = emsc_window(read('area01_scripts/scripts.emsc'))
    chains = {e: len(T.walk_chain(d, base, e)) for e in T.SCRIPT_ENTRIES}
    rep['script_records'] = sum(chains.values())
    problems, acount, rows = messages_problems(read('message_data.emmd'), caps, C.read_elf())
    for problem in problems:
        check(False, problem)
    rep['message_area_records'], rep['message_stream_rows'] = acount, rows
    problems, rep['world_models'] = world_models_problems(read('world_models.emwm'), caps)
    for problem in problems:
        check(False, problem)
    return rep, roster


def bindings(S, caps, banks):
    """The capture binding (group, slot) -> ((container, row), handle, header,
    SPU base); raises on a header that matches no row (a clean FAIL)."""
    gname = S.X.GLOBAL_CONTAINER
    gdata = (C.DECOMP / gname).read_bytes()
    containers = {gname: (gdata, S.X.A.parse_container(gdata)),
                  'area01_banks.bin': (banks, S.X.A.parse_container(banks))}
    if containers['area01_banks.bin'][1] is None:
        raise ValueError('area01_banks.bin is not an SShd container')
    bound, refused, _rep = S.bindings_from_captures(caps, containers)
    return bound, refused, {gname: gdata, 'area01_banks.bin': banks}


def banks_problems(banks, info, lmap):
    """area01_banks.bin against the disc: the whole file equals the head of
    the nested block's first file (the descriptor's upload section 0), over
    the container's own total, every bank body inside it; the file's own
    word 0 (the container total) must equal its length."""
    path = next(p for _a, p, _o, _s, l in lmap if l.startswith(info['nested'] + '/f00_'))
    disc = path.read_bytes()
    total = C.u32(disc, 0)
    out = []
    if len(banks) != total or len(banks) < 4 or C.u32(banks, 0) != total or total > info['sections'][0][1]:
        out.append(f'area01_banks.bin: {len(banks)} bytes, container total {total:#x}, '
                   f'section 0 {info["sections"][0][1]:#x}')
    if banks != disc[:len(banks)]:
        k = next((j for j in range(min(len(banks), len(disc))) if banks[j] != disc[j]), len(banks))
        out.append(f'area01_banks.bin differs from {path.name} at +{k:#x}')
    img_off, nbanks = C.u32(disc, 0x10), C.u32(disc, 0x0C)
    body = img_off
    for i in range(nbanks):
        body += C.u32(disc, 0x20 + 16 * i)
    if body != total:
        out.append('bank bodies do not end at the container total')
    return out


# The registry, re-derived without export_sfx_registry.py: every table is
# read from the capture's RAM at the addresses the original reads (001FB9F0,
# 00119EA0, 001152D8, 00115850, 00117918, 001179E0), every sample from the
# disc bank body its tone names, decoded with the SPU ADPCM rule.
REC_REMAP = {0x3E8: (0x264A70, 0x264B30), 0x7D0: (0x264AD0, 0x264B90)}
D_00281D50, D_0027C6C0, D_0027F740 = 0x281D50, 0x27C6C0, 0x27F740
LADDER, PAN, SSHD = 0x241D70, 0x242630, 0x64685353
ADPCM = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))
REASON = dict(script=1, looping=2, sweep=3, modulation=4, noise=5, defaults=8, pitch=9, novoice=10, unbound=7)
OP = '<HBBBBHHHIHHBbBBBBBBI'


class Refuse(Exception):
    pass


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def record_001FB9F0(ram, sid, area, sub):
    """The area-paged branch of 001FB9F0 (the ids exported here)."""
    first = 0x3E8 if sid < 0x5DC else 0x7D0
    remaps, records = REC_REMAP[first]
    t = C.u32(ram, remaps + 4 * area)
    if not t or not C.u32(ram, t + 4 * sub):
        return None
    remap = ram[C.u32(ram, t + 4 * sub) + sid - first]
    if remap == 0xFF:
        return None
    t = C.u32(ram, records + 4 * area)
    if not t or not C.u32(ram, t + 4 * sub):
        return None
    return C.u32(ram, t + 4 * sub) + 4 * remap


def script_00119EA0(ram, handle, group, index):
    """(header, script address) or None (-1)."""
    if not (0 <= handle < 0x80 and 0 <= group < 0x80 and 0 <= index < 0x80):
        return None
    use, hdr = C.u32(ram, D_0027C6C0 + 12 * handle), C.u32(ram, D_0027C6C0 + 12 * handle + 4)
    if use != 1 or C.u32(ram, hdr + 0xC) != SSHD or C.u32(ram, hdr + 0x20) == 0xFFFFFFFF:
        return None
    blk = hdr + C.u32(ram, hdr + 0x1C)
    if C.u16(ram, blk) < group:
        return None
    x = C.u16(ram, blk + 2 * group + 2)
    if x == 0xFFFF:
        return None
    sel = x >> 1
    if C.u16(ram, blk + 2 * sel) < index:
        return None
    return hdr, blk + C.u16(ram, blk + 2 * (index + sel) + 2)


def events_001152D8(ram, at):
    """[(kind, tick, fields)] of one trigger script (running status, VLQ
    deltas, the track accumulator stepped by 0x1E0000 / D_0027F740[0x1D])."""
    step = 0x1E0000 // C.u16(ram, D_0027F740 + 2 * 0x1D)
    out, running, acc, tick, end = [], None, 0, 0, at + 0x1000
    while at < end:
        if ram[at] & 0x80:
            running = ram[at]
            at += 1
        if running is None:
            raise Refuse('script')
        if running == 0xFF:
            if ram[at:at + 2] != b'\x2f\x00':
                raise Refuse('script')
            out.append(('end', tick, ()))
            return out
        if running & 0xF0 == 0xA0:
            out.append(('a0', tick, tuple(ram[at:at + 3])))
            at += 3
        elif running & 0xF0 == 0xB0 and ram[at] == 0x41:
            out.append(('porta', tick, tuple(ram[at + 1:at + 5])))
            at += 5
        else:
            raise Refuse('script')
        delta = 0
        while True:
            byte = ram[at]
            at += 1
            delta = delta << 7 | byte & 0x7F
            if not byte & 0x80:
                break
        acc += delta << 12
        while acc > 0:
            acc -= step
            tick += 1
    raise Refuse('script')


_DECODED = {}


def adpcm(body, start):
    """(pcm, loop start frame or None, loop body) from the tone's first
    block through the first end-flag block (SPU ADPCM, zero history; a
    repeat end replays from the last loop-start block with the history
    carried, and must settle on its second pass). Memoised by the sample's
    own bytes."""
    end, loop = start, None
    while True:
        if end + 16 > len(body):
            raise Refuse('looping')
        if body[end + 1] & 4:
            loop = (end - start) // 16
        end += 16
        if body[end - 16 + 1] & 1:
            break
    raw = bytes(body[start:end])
    if (raw, loop) not in _DECODED:
        try:
            _DECODED[(raw, loop)] = _adpcm(raw, loop)
        except Refuse as why:
            _DECODED[(raw, loop)] = why
    got = _DECODED[(raw, loop)]
    if isinstance(got, Refuse):
        raise got
    return got


def _adpcm(raw, loop):
    repeat = raw[-16 + 1] & 2
    if repeat and loop is None:
        raise Refuse('looping')
    blocks = len(raw) // 16
    if any(raw[16 * k] & 0xF > 12 or raw[16 * k] >> 4 > 4 for k in range(blocks)):
        raise Refuse('script')

    def run(first, hist):
        h1, h2 = hist
        pcm = []
        for k in range(first, blocks):
            shift, (c1, c2) = raw[16 * k] & 0xF, ADPCM[raw[16 * k] >> 4]
            for i in range(28):
                nib = raw[16 * k + 2 + i // 2] >> (4 * (i & 1)) & 0xF
                nib -= 16 if nib > 7 else 0
                v = max(-32768, min(32767, (nib << (12 - shift)) + ((h1 * c1 + h2 * c2) >> 6)))
                h2, h1 = h1, v
                pcm.append(v)
        return pcm, (h1, h2)
    pcm, hist = run(0, (0, 0))
    if not repeat:
        return pcm, None, None
    body1, hist = run(loop, hist)
    if run(loop, hist)[0] != body1:
        raise Refuse('looping')
    return pcm, loop * 28, body1


def channel_defaults(chans):
    """The 48 per-channel state rows must agree in bytes 3, 0xC and 0xE (the
    scalar and defaults the registry folds into every voice); returns row 0."""
    if len(chans) != 48 or any(c[j] != chans[0][j] for c in chans for j in (3, 0xC, 0xE)):
        raise Refuse('defaults')
    return chans[0]


def bank_handle(ram, g, bi):
    """The handle word D_00281D50[g * 0x14 + bi] a record names, 0 outside
    6 groups x 0x14 slots."""
    return C.u32(ram, D_00281D50 + 4 * (g * 0x14 + bi)) if 0 <= g < 6 and 0 <= bi < 0x14 else 0


def pitch_00117918(ladder, note, center, fine):
    """The SPU pitch of a note: 00117918's ladder D_00241D70 step, then the
    32-bit product with 44100 over 48000 (truncated toward zero); a pitch
    outside 1..0x3FFF is refused."""
    if center <= note:
        d = note - center
        lad = s32(ladder[(d % 12) * 16 + fine + 0xD0] << (d // 12))
    else:
        d = center - note
        lad = ladder[(12 - d % 12) * 16 + fine + 0xD0] >> (d // 12 + 1)
    product = s32(lad * 44100)
    pitch = -(abs(product) // 48000) if product < 0 else product // 48000
    if not 0 < pitch <= 0x3FFF:
        raise Refuse('pitch')
    return pitch


def registry_from_ram(ram, bound, containers):
    """The whole EMSR v2 for (area, sub) of the capture, rebuilt."""
    area, sub = ram[0x810700], ram[0x810701]
    body_of = {}
    for (name, row), handle in {v[0]: v[1] for v in bound.values()}.items():
        data = containers[name]
        base = C.u32(data, 0x10) + sum(C.u32(data, 0x20 + 16 * i) for i in range(row))
        body_of[handle] = (name, data, base)
    ladder = [C.u16(ram, LADDER + 2 * i) for i in range(0x240)]
    samples, entries = {}, []
    for sid in list(range(0x3E8, 0x5DC)) + list(range(0x7D0, 0x9C4)):
        rec = record_001FB9F0(ram, sid, area, sub)
        if rec is None:
            entries.append((sid, 2, [], 0, 0))
            continue
        g, bi, sg, si = struct.unpack('4b', ram[rec:rec + 4])
        try:
            handle = bank_handle(ram, g, bi)
            if not handle and (g, bi) != (1, 0):
                raise Refuse('unbound')
            if C.u32(ram, C.u32(ram, D_0027C6C0 + 12 * handle + 4) + 0xC) != SSHD:
                entries.append((sid, 2, [], 0, 0))      # 00119EA0 refuses the header
                continue
            found = script_00119EA0(ram, handle, sg, si)
            if found is None:
                entries.append((sid, 2, [], 0, 0))
                continue
            hdr, at = found
            events = events_001152D8(ram, at)
            state = hdr + C.u32(ram, hdr + 0x20)
            programs = hdr + C.u32(ram, hdr + 0x24)
            velocities = hdr + C.u32(ram, hdr + 0x14)
            ch = channel_defaults([ram[state + 0x10 + 16 * t:state + 0x20 + 16 * t] for t in range(48)])
            ops = []
            for kind, tick, f in events:
                if kind == 'end':
                    ops.append(struct.pack(OP, tick, 4, *[0] * 18))
                elif kind == 'porta':
                    length, depth, prog, note = f
                    ops.append(struct.pack(OP, tick, 3, note, prog, *[0] * 12, length, depth, 0, 0))
                elif f[1] == 0:
                    ops.append(struct.pack(OP, tick, 2, f[0], f[2], *[0] * 16))
                else:
                    note, vel, prog = f
                    program = programs + C.u16(ram, programs + 2 + 2 * prog)
                    slot = note - ram[program + 6]
                    if slot < 0:
                        continue
                    tone = ram[program + 8 + 16 * slot:program + 24 + 16 * slot]
                    flags, fine = tone[15], struct.unpack('b', tone[3:4])[0]
                    if tone[10]:
                        raise Refuse('sweep')
                    if flags & 0x20:
                        raise Refuse('modulation')
                    if flags & 2:
                        raise Refuse('noise')
                    center = tone[2]
                    pitch = pitch_00117918(ladder, note, center, fine)
                    scalar = (ch[14] * ch[3] * tone[11] * ram[velocities + vel + 2] * ram[program + 1] *
                              ram[state]) >> 27
                    pan = C.u16(ram, PAN + 2 * (tone[12] >> 2))
                    name, data, base = body_of[handle]
                    start = base + (C.u16(tone, 4) << 3)
                    key = (name, start)
                    if key not in samples:
                        pcm, loop, body = adpcm(data, start)
                        samples[key] = (len(samples), pcm, loop, body)
                    ops.append(struct.pack(OP, tick, 1, note, prog, flags, samples[key][0], pitch, pan, scalar,
                                           C.u16(tone, 6), C.u16(tone, 8), center, fine, tone[13], tone[0],
                                           tone[1], 0, 0, 0, 0))
            if not any(o[2] == 1 for o in ops):
                raise Refuse('novoice')
            if len(ops) > 32:
                raise Refuse('script')
            entries.append((sid, 1, ops, 0, (g & 0xFF) << 8 | (bi & 0xFF)))
        except Refuse as why:
            entries.append((sid, 3, [], REASON[str(why)], 0))
    out = bytearray(struct.pack('<4sIIIII', b'EMSR', 2, len(samples), len(entries), 0x240, 0))
    out += struct.pack('<576H', *ladder)
    for sid, st, ops, reason, bank in entries:
        out += struct.pack('<IhhBBHHH', sid, area, sub, st, len(ops), reason, bank, 0) + b''.join(ops)
    for _i, pcm, loop, body in sorted(samples.values(), key=lambda x: x[0]):
        out += struct.pack('<II', len(pcm), 0xFFFFFFFF if loop is None else loop)
        out += struct.pack(f'<{len(pcm)}h', *pcm)
        if loop is not None:
            out += struct.pack(f'<{len(body)}h', *body)
    counts = {k: sum(1 for e in entries if e[1] == k) for k in (1, 2, 3)}
    return bytes(out), counts, len(samples), entries


def binding_problems(names, refused):
    """Exactly three bindings to area01_banks.bin (group 2 -> row 0, group 4
    -> rows 1 and 2) and group 3's header refused, in every capture."""
    out = []
    if sum(1 for n in names if 'area01_banks' in n) != 3:
        out.append(f'area bank bindings {names}')
    if 3 not in {g for g, _i in refused}:
        out.append('group 3 refused header')
    return out


def sfx_problems(S, caps, lmap, info, banks, emsr):
    """([problems], binding names, entry state counts)."""
    problems, names, counts = [], [], {}
    try:
        problems += banks_problems(banks, info, lmap)
        bound, refused, containers = bindings(S, caps, banks)
        names = sorted({f'{g}.{i}:{k[0]}#{k[1]}' for (g, i), (k, *_r) in bound.items()})
        problems += binding_problems(names, refused)
        for cap in caps:
            want, counts, _n, _entries = registry_from_ram(cap.ram, bound, containers)
            if emsr != want:
                k = next((j for j in range(min(len(emsr), len(want))) if emsr[j] != want[j]),
                         min(len(emsr), len(want)))
                problems.append(f'{cap.name}: sfx_registry.emsr differs from the RAM re-derivation at '
                                f'+{k:#x} (sizes {len(emsr)} / {len(want)})')
    except (SystemExit, ValueError, TypeError, KeyError, IndexError, StopIteration, struct.error) as error:
        problems.append(f'sfx: {error!r}')
    return problems, names, counts


# ---------------------------------------------------------------------------


def mutate(data, at):
    b = bytearray(data)
    b[at] ^= 0x01
    return bytes(b)


def load_map_problems(lmap):
    """The load map as build_load_map returns it must already be in address
    order (so the checker's LoadedImage.map, which sorts it, and the
    exporter's file_of_order, which uses this order, name the zones in the
    same order), and no entry may overlap the next one (so at most one
    entry holds any address, and LoadedImage.locate's answer does not
    depend on its search order)."""
    out = []
    if list(lmap) != sorted(lmap):
        out.append('load map not in address order: ' + ', '.join(m[4] for m in lmap))
    for x, y in zip(sorted(lmap), sorted(lmap)[1:]):
        if x[0] + x[3] > y[0]:
            out.append(f'load map entries {x[4]} and {y[4]} overlap')
    return out


def run_checks(K, caps):
    """Every real check, over the export tree A and `caps`; each problem
    goes to check(). Returns what the report and the controls use."""
    L, T, S, elf, image = K.L, K.T, K.S, K.elf, K.image
    V = dict(elf=elf, image=image)
    for problem in load_map_problems(K.lmap):
        check(False, problem)
    cells = V['cells'] = (A / 'area01_cells.bin').read_bytes()
    table = V['table'] = C.u32(K.arrival.spad, L.SPAD_CELLS)
    V['mapped'] = 0
    for cap in caps:
        # the allowance is the arrival's directory: every capture's directory
        # must be there (verify_cell_directory then compares each of its bytes)
        here = C.u32(cap.spad, L.SPAD_CELLS)
        check(here == table, f'{cap.name}: cell directory pointer {here:#x}, not the arrival\'s {table:#x}')
        rows = C.compare_load_map(image, cap, allow=[(table, table + len(cells))])
        check(all(r['unexpected_rows'] == 0 for r in rows), f'{cap.name}: load map differs from RAM')
        V['mapped'] = sum(r['bytes'] for r in rows)

    lv, first_zone, gs, prim = check_level(K.el, L, caps, image)
    V['level'], V['zone'] = lv, (*(first_zone or (None, None)), gs, prim)

    emcl = V['emcl'] = (A / 'area01.emcl').read_bytes()
    for cap in caps:
        for problem in grid_problems(cap.ram, image, (table, table + len(cells))):
            check(False, f'{cap.name}: {problem}')
        where = emcl_equal(emcl, cap.ram)
        check(where is None, f'{cap.name}: area01.emcl differs from the RAM grid rebuild: {where}')
    problems, moved = cells_problems(L, elf, cells, image, caps)
    for problem in problems:
        check(False, problem)
    V['moved'] = moved
    level = V['level_json'] = json.loads((A / 'level/level.json').read_text())
    problem = moved_problem(moved, level)
    check(problem is None, str(problem))

    V['tables'], V['roster'] = check_tables(T, caps, K.arrival)

    banks = V['banks'] = (A / 'sfx/area01_banks.bin').read_bytes()
    emsr = V['emsr'] = (A / 'sfx/sfx_registry.emsr').read_bytes()
    problems, V['sfx_names'], V['sfx_counts'] = sfx_problems(S, caps, K.lmap, K.info, banks, emsr)
    for problem in problems:
        check(False, problem)

    V['room'] = None
    try:
        V['room'] = L.check_ctx_room_block(caps, elf)
    except SystemExit as error:
        check(False, f'ctx room block: {error}')
    return V


CANARY_TEST_1 = 0x8153A0      # where the arrival's display lists take TEST_1 (both lists)
# Unused RAM (zero in all 12 captures): the canary's grid copy, whose address
# has GRID's low half (0x7940), so a pointer compare of the low half alone
# cannot report it; the canary's cell-directory copy; the controls' scratch
CANARY_GRID, GRID_BLOCK = 0x1C07940, 0x23B9C   # the grid block's length (header .. last node)
CANARY_CELLS = 0x1D00000
SCRATCH_RAM = 0x1C00000


# The canary's planted differences. File flips are in a copy of the export
# tree (symlinks plus the changed files, under BUILD/canary); RAM (and GS
# freeze) flips are in a copy of the arrival named 'canary', the SECOND
# capture of the run.
def canary(K, lib):
    """run_checks over [the arrival, a copy of it named 'canary'] and a tree
    with file flips, and check_loaders over a tree of broken files; every
    section must report its own planted difference.

    Every per-capture comparison listed in `want` below with a 'canary: '
    message has its plant in the canary copy only (RAM or its GS freeze),
    and the arrival must NOT report that message: a comparison that reads
    only the first capture's data fails the canary, even when it names the
    capture it is looping over. File plants are used only where the
    comparison is not per capture (zone rebuild, zone names and count, the
    moved set, the bank container), and none of them changes a per-capture
    comparison (the arrival's silence is checked). Returns the number of
    sections."""
    global A
    L, T = K.L, K.T
    arrival, real = K.arrival, A
    tree = CANARY

    def remove_tree():
        if tree.exists():
            for q in sorted(tree.rglob('*'), key=lambda x: -len(x.parts)):
                q.unlink() if not q.is_dir() or q.is_symlink() else q.rmdir()
            tree.rmdir()
    remove_tree()
    for q in real.rglob('*'):
        if q.is_file():
            d = tree / q.relative_to(real)
            d.parent.mkdir(parents=True, exist_ok=True)
            d.symlink_to(q)

    def plant(rel, data):
        (tree / rel).unlink(missing_ok=True)
        (tree / rel).write_bytes(data)
    # -- file plants (comparisons that are not per capture)
    zones = sorted(x.name for x in (real / 'level').glob('*.emdl'))
    zbuf = (real / f'level/{zones[-1]}').read_bytes()
    plant(f'level/{zones[-1]}', mutate(zbuf, emdl_parts(zbuf)['verts'] + 1))   # a position byte, not a texel
    renamed = zones[0][:3] + 'x' + zones[0][4:]          # 00_f00_... -> 00_x00_...: the name check
    (tree / f'level/{zones[0]}').rename(tree / f'level/{renamed}')
    # the third zone renamed in its id part only (NN_fII_idXX -> idXX + 1):
    # the name check compares the whole source file stem, not its fII part
    head, ident = zones[2][:-len('.emdl')].rsplit('_id', 1)
    id_renamed = f'{head}_id{int(ident, 16) + 1:02x}.emdl'
    (tree / f'level/{zones[2]}').rename(tree / f'level/{id_renamed}')
    plant('level/99_extra.emdl', (real / f'level/{zones[0]}').read_bytes())   # one file too many
    level = json.loads((real / 'level/level.json').read_text())
    level['cells']['moved_uids'] = level['cells']['moved_uids'][:-1]
    plant('level/level.json', json.dumps(level).encode())
    plant('sfx/area01_banks.bin', mutate((real / 'sfx/area01_banks.bin').read_bytes(), -1))
    # -- RAM plants, in the canary copy only
    bank = (real / 'level/static_bank.emsc').read_bytes()
    base, _e, length = struct.unpack_from('<3I', bank, 8)
    dbase = C.u32((real / 'level/dynamic_objects.emsc').read_bytes(), 8)
    ram = bytearray(arrival.ram)
    ram[base + length - 1] ^= 1                       # the static bank's last byte (a mapped byte too)
    objects = [base + (C.s32(ram, base + 4 + 4 * i) >> 2 << 2) for i in range(C.u32(ram, base))]
    last_obj = max(objects, key=lambda o: o + 0x820 * C.u32(ram, o))
    ram[last_obj] += 1                                # the last object one unit longer: bank_end
    ram[dbase] ^= 1                                   # the dynamic list count 12 -> 13: its bytes and length
    ram[0x28A5A0 + 2] ^= 1                            # D_0028A5A0, D_0028A5A4: byte 2
    ram[0x28A5A4 + 2] ^= 1
    ram[C.u32(ram, L.CTX_PTR) + 0xA4] ^= 1            # a ctx +0xA0..+0xFF byte
    ram[CANARY_TEST_1] ^= 2                           # TEST_1 of the level kicks' state
    ram[0x2755E0 + 7] ^= 1                            # the last spawn window's last byte
    ram[0x24DFA0 + 0x1F] ^= 1                         # the door row's last byte
    gb, ab = C.u32(ram, 0x28A4E8), C.u32(ram, 0x28A594)
    ram[gb + message_bank_size(ram, gb) - 1] ^= 1     # the global message bank's last byte
    ram[ab + 8] ^= 1                                  # the area bank's header word 2: its size
    ram[MSG_TABLES + 2] ^= 1                          # D_00264DD0[0]
    ram[0x264CD0 + 3] ^= 1                            # the line-config block
    ram[C.u32(K.elf, MSG_TABLES + 4 * (C.AREA + 1) - C.ELF_VADDR + C.ELF_OFFSET) + 5] ^= 1   # an area record
    ram[MSG_STREAMS + 16 * 3 + 5] ^= 1                # a stream row
    wm = (real / 'world_models.emwm').read_bytes()
    table = C.u32(wm, 8)
    ram[table + C.u32(wm, 12) - 1] ^= 1               # the world-model span's last byte
    models = [table + (C.s32(ram, table + 4 + 4 * i) >> 2 << 2) for i in range(C.u32(ram, table))]
    ram[max(models, key=lambda m: m + C.u32(ram, m + 0xC) + 0x50 * C.u32(ram, m + 8)) + 8] += 1   # one bone more
    roster = (real / 'roster.emro').read_bytes()
    groups = C.u16(roster, 0x0A)
    ga, gn = struct.unpack_from('<II', roster, 0x18 + 8 * (groups - 1))
    ram[ga + 0x2C * gn - 1] ^= 1                      # the last group record's last byte (0x829327)
    first_place = 0x18 + 8 * groups + sum(0x2C * struct.unpack_from('<II', roster, 0x18 + 8 * g)[1]
                                         for g in range(groups))
    _ok, _live, pairs = roster_nodes(T, roster, arrival.ram)
    ram[pairs[0][1] + 0x03] ^= 1                      # a placement node's copied +0x03
    at_rest = next(a for i, a in pairs[1:] if arrival.ram[a + 0xB0:a + 0xBC] ==
                   T.spawn_fields_placement(roster[first_place + 0x28 * i:first_place + 0x28 * (i + 1)], i)[1])
    ram[at_rest + 0xB1] ^= 1                          # another node moved off its record position
    ram[LADDER + 2 * 0x40] ^= 1                       # the pitch ladder: the registry re-derivation
    # the grid block copied to unused RAM, D_0028A598 pointed at the copy (the
    # grid pin), and a vertex byte of the copy flipped (the EMCL rebuild)
    ram[CANARY_GRID:CANARY_GRID + GRID_BLOCK] = ram[GRID:GRID + GRID_BLOCK]
    struct.pack_into('<I', ram, 0x28A598, CANARY_GRID)
    ram[CANARY_GRID + C.u32(ram, GRID) + 1] ^= 1
    # the cell directory copied to unused RAM, the scratchpad pointer moved
    # there (the pointer check), and uid 0's hull byte +0x19C flipped in the
    # copy (the RAM directory compare)
    cells = (real / 'area01_cells.bin').read_bytes()
    dtable = C.u32(arrival.spad, L.SPAD_CELLS)
    ram[CANARY_CELLS:CANARY_CELLS + len(cells)] = ram[dtable:dtable + len(cells)]
    ram[CANARY_CELLS + 0x19C] ^= 1
    spad = bytearray(arrival.spad)
    struct.pack_into('<I', spad, L.SPAD_CELLS, CANARY_CELLS)
    # the GS freeze: the texel block of the second zone's first texture
    # inverted (that zone is neither renamed nor file-planted, so its
    # verdict from the real run could be reused only by a memo that
    # ignores the freezes)
    zb0 = next(iter(_ZONES.values()))
    first_tex = sorted(zb0.items(), key=lambda z: [l for *_x, l in K.image.map].index(z[0]))[1][1][0].tex_table[0]
    gs = bytearray(arrival.gs.read_bytes())
    lm = len(gs) - 0x400000 - 84 + 256 * first_tex['tbp0']
    gs[lm:lm + 256] = bytes(x ^ 0xFF for x in gs[lm:lm + 256])
    plant('canary_gs.bin', bytes(gs))
    fake = copy.copy(arrival)
    fake.name, fake.ram, fake.spad, fake.gs = 'canary', bytes(ram), bytes(spad), tree / 'canary_gs.bin'
    zone0 = f'{zones[1]}: texture 0 texels differ from the canary GS freeze'
    want = {'load map': 'canary: load map differs from RAM',
            'cell directory pointer': 'canary: cell directory pointer',
            'static bank': 'canary: static bank differs from RAM',
            'static bank extent': 'canary: static bank length',
            'dynamic list': 'canary: dynamic list differs from RAM',
            'dynamic list extent': 'canary: dynamic list length',
            'D_0028A5A0': 'canary: D_0028A5A0',
            'D_0028A5A4': 'canary: D_0028A5A4',
            'GS state': 'level GS state',
            'zone texels': zone0,
            'zone EMDL': f'{zones[-1]} differs from a rebuild',
            'zone count': '13 zone files for 12 source files',
            'zone names': f'{renamed}: zone order',
            'zone id': f'{id_renamed}: zone order',
            'grid pointer': f'canary: D_0028A598 = {CANARY_GRID:#x}',
            'EMCL': 'canary: area01.emcl differs from the RAM grid rebuild',
            'cell RAM directory': 'canary: hull 0 differs',
            'moved set': 're-transformed hulls',
            'roster': 'canary: roster records differ from RAM',
            'placement node fields': 'canary: a live placement node lost a copied field',
            'placement node count': 'canary: placement nodes live / at the record position (49, 45)',
            'spawn windows': 'canary: spawn_table.emsp window 0x2755e0 differs',
            'door windows': 'canary: door_destinations.emsp window 0x24dfa0 differs',
            'message banks': 'canary: message banks differ',
            'message bank sizes': 'canary: message counts / bank sizes',
            'message pointers': 'canary: D_00264DD0 pointers',
            'message blocks': 'canary: message block 0x264cd0 differs',
            'message records': 'canary: message records differ',
            'message stream rows': 'canary: message stream rows differ',
            'world models': 'canary: world model bank differs',
            'world model span': 'canary: world model span',
            'sound bank': 'area01_banks.bin differs from',
            'registry': 'canary: sfx_registry.emsr differs from the RAM re-derivation',
            'ctx': 'ctx room block: canary:'}
    n = len(FAILS)
    try:
        A = tree
        with contextlib.redirect_stdout(io.StringIO()):
            run_checks(K, [arrival, fake])
        got = FAILS[n:]
        del FAILS[n:]
        # a matrix-slot bit ((w bits & 0x3FF) >> 3) in the first record of
        # the last bank object: the zone rebuild itself changes (a second
        # build_zones, ~0.3 s CPU), so it runs on its own, over the
        # arrival only (a bank file plant would reach every capture's bank
        # compare); every run since the close-out (it kills S24 and S26)
        el = K.el
        objs = L.bank_objects(lambda a, n: bank[20 + a - base:20 + a - base + n], base)
        _i, obj, units = objs[-1]
        unit = bank[20 + obj + 0x40 - base:20 + obj + 0x40 - base + L.UNIT * units]
        rec = next(r for r in el.walk_records(unit, 0, len(unit)) if r is not None)
        planted = bytearray(bank)
        planted[20 + obj + 0x40 - base + rec[0] + 0x3C] ^= 0x08
        plant('level/static_bank.emsc', bytes(planted))
        with contextlib.redirect_stdout(io.StringIO()):
            check_level(el, L, [arrival], K.image)
        got_slot = FAILS[n:]
        del FAILS[n:]
        # the loaders: every file the port loads, each one broken so that its
        # loader must refuse it; every loader verdict must report
        loader_want = plant_loader_canary(tree, plant)
        with contextlib.redirect_stdout(io.StringIO()), quiet_fds():
            check_loaders(lib)
        got_loader = FAILS[n:]
        del FAILS[n:]
    finally:
        A = real
        remove_tree()
    for section, text in want.items():
        check(any(text in x for x in got), f'canary: section {section} did not report "{text}"')
        quiet = text.replace('canary: ', f'{arrival.name}: ').replace('the canary GS', f'the {arrival.name} GS')
        if quiet != text:
            check(not any(quiet in x for x in got), f'canary: section {section} reported the arrival too')
    if got_slot is not None:
        check(any('records with a matrix slot' in x for x in got_slot), 'canary: section matrix slot did not report')
    for name in loader_want:
        check(f'port loader rejects {name}' in got_loader, f'canary: loader {name} did not report')
    return len(want) + (got_slot is not None) + len(loader_want)


@contextlib.contextmanager
def quiet_fds():
    """Silence the C loaders' own stdout / stderr diagnostics (the canary's
    broken files make them print)."""
    sys.stdout.flush()
    sys.stderr.flush()
    saved = [os.dup(1), os.dup(2)]
    null = os.open(os.devnull, os.O_WRONLY)
    try:
        os.dup2(null, 1)
        os.dup2(null, 2)
        yield
    finally:
        os.dup2(saved[0], 1)
        os.dup2(saved[1], 2)
        for fd in saved + [null]:
            os.close(fd)


def emsp_file(windows):
    """An EMSP v1 file of [(address, bytes)] windows."""
    return struct.pack('<4sIII', b'EMSP', 1, len(windows), 0) + \
        b''.join(struct.pack('<II', a, len(d)) for a, d in windows) + b''.join(d for _a, d in windows)


def plant_loader_canary(tree, plant):
    """Replace each loaded file in the canary tree by one its port loader must
    refuse; returns the check_loaders names that must report. The spawn
    file keeps every window but the one holding 0x24B1A0 (the load passes,
    the read fails); the door file keeps only the pointer array; an EMSC
    keeps its header with a 32-byte window (under one 64-byte record);
    every other file is cut to its first 16 bytes."""
    spawn = emsp_windows((tree / 'spawn_table.emsp').read_bytes())
    plant('spawn_table.emsp', emsp_file([(a, d) for a, d in spawn if not a <= 0x24B1A0 < a + len(d)]))
    doors = emsp_windows((tree / 'door_destinations.emsp').read_bytes())
    plant('door_destinations.emsp', emsp_file([(a, d) for a, d in doors if a == D_0024E140]))
    names = ['spawn', 'doors (EMSP windows)']
    for rel in ('area01_scripts/scripts.emsc', 'overlay_data.emsc', 'level/static_bank.emsc',
                'level/dynamic_objects.emsc'):
        blob = (tree / rel).read_bytes()
        base, entry = struct.unpack_from('<2I', blob, 8)
        plant(rel, C.emsc(base, blob[20:52], entry))
        names.append(rel)
    for rel, name in (('roster.emro', 'roster'), ('world_models.emwm', 'world models'), ('area01.emcl', 'emcl'),
                      ('sfx/sfx_registry.emsr', 'sfx registry'), ('message_data.emmd', 'message data')):
        plant(rel, (tree / rel).read_bytes()[:16])
        names.append(name)
    for z in sorted(tree.joinpath('level').glob('*.emdl')):     # every zone file there, the last included
        plant(f'level/{z.name}', z.read_bytes()[:16])
        names.append(z.name)
    return names


def emsc_blob(blob, base_shift=0, cut_head=0, cut_tail=0):
    """An EMSC variant with a consistent header: the window's first
    `cut_head` / last `cut_tail` bytes dropped, base and entry moved by
    `base_shift`."""
    base, entry, length = struct.unpack_from('<3I', blob, 8)
    return C.emsc(base + base_shift, blob[20 + cut_head:20 + length - cut_tail], entry + base_shift)


def mm_blob(mm, cut):
    """A message file variant with a consistent header: `cut` = one of
    gbank / abank (the bank's last byte), grec / arec (the table's last
    record), rows (the last stream row)."""
    magic, version, area, gc, ac, rows, cursor, gb, ab = struct.unpack_from('<4s8I', mm, 0)
    at = 140
    parts = {}
    for key, size in (('grec', 8 * gc), ('arec', 8 * ac), ('rows', 16 * rows), ('gbank', gb), ('abank', ab)):
        parts[key] = mm[at:at + size]
        at += size
    parts[cut] = parts[cut][:-{'grec': 8, 'arec': 8, 'rows': 16}.get(cut, 1)]
    head = struct.pack('<4s8I', magic, version, area, len(parts['grec']) // 8, len(parts['arec']) // 8,
                       len(parts['rows']) // 16, cursor, len(parts['gbank']), len(parts['abank']))
    return head + mm[len(head):140] + b''.join(parts[k] for k in ('grec', 'arec', 'rows', 'gbank', 'abank'))


def ram_copy(cap, name, edits):
    """A copy of `cap` named `name` whose RAM has the (address, bytes) edits."""
    ram = bytearray(cap.ram)
    for at, data in edits:
        ram[at:at + len(data)] = data
    fake = copy.copy(cap)
    fake.name, fake.ram = name, bytes(ram)
    return fake


def xor(ram, at, bit=1):
    return (at, bytes([ram[at] ^ bit]))


def extent_controls(K, V, cap):
    """Round-4 controls: every exported extent against its RAM / ELF / disc
    source (the review's A-TRUNC cases), the fields the round-4 sweep found
    unprobed, and the reference functions the registry and the EMCL rebuild
    use. Each must be reported (or, for the negative cases, accepted)."""
    L, T, S, elf, image = K.L, K.T, K.S, K.elf, K.image
    roster, emcl, cells, table = V['roster'], V['emcl'], V['cells'], V['table']
    miss = lambda what: f'control: {what} missed'
    # -- EMSP window sets (spawn: drop the last window, drop window 1, the
    # last window one byte short, the first four bytes short; doors: the
    # pointer array 0x17 -> 0x16 words, the row one record short)
    spawn = emsp_windows((A / 'spawn_table.emsp').read_bytes())
    doors = emsp_windows((A / 'door_destinations.emsp').read_bytes())
    for what, name, windows, layout in (
            ('spawn: last window dropped', 'spawn_table.emsp', spawn[:-1], spawn_layout),
            ('spawn: window 1 dropped', 'spawn_table.emsp', spawn[:1] + spawn[2:], spawn_layout),
            ('spawn: last window - 1', 'spawn_table.emsp', spawn[:-1] + [(spawn[-1][0], spawn[-1][1][:-1])],
             spawn_layout),
            ('spawn: first window - 4', 'spawn_table.emsp', [(spawn[0][0], spawn[0][1][:-4])] + spawn[1:],
             spawn_layout),
            ('doors: pointer array - 4', 'door_destinations.emsp', [doors[0], (doors[1][0], doors[1][1][:-4])],
             doors_layout),
            ('doors: row - 4', 'door_destinations.emsp', [(doors[0][0], doors[0][1][:-4]), doors[1]], doors_layout)):
        check(bool(emsp_problems(name, emsp_file(windows), [cap], layout(disc_reader()))[0]), miss(what))
    # a window byte that only RAM disagrees with (the RAM compare on its own)
    check(bool(emsp_problems('spawn_table.emsp', emsp_file(spawn),
                             [ram_copy(cap, 'spawn RAM', [xor(cap.ram, 0x2755E0 + 7)])],
                             spawn_layout(disc_reader()))[0]),
          miss('spawn window byte in RAM'))
    # -- EMSC windows (scripts - 4; overlay data - 4 and base + 4) and the
    # disc / RAM compares each on its own
    twin = table_windows()
    for label, rel in (('scripts', 'area01_scripts/scripts.emsc'), ('overlay data', 'overlay_data.emsc')):
        blob = (A / rel).read_bytes()
        disc = disc_reader()(*twin[label])
        variants = [('window - 4', emsc_blob(blob, cut_tail=4))]
        if label == 'overlay data':
            variants.append(('base + 4', emsc_blob(blob, base_shift=4, cut_head=4)))
        for what, bad in variants:
            check(bool(emsc_problems(label, bad, [cap], twin[label], disc)), miss(f'{label} {what}'))
        last = twin[label][0] + twin[label][1] - 1
        both = ram_copy(cap, f'{label} disc', [xor(cap.ram, last)])
        check(bool(emsc_problems(label, mutate(blob, len(blob) - 1), [both], twin[label], disc)),
              miss(f'{label}: file and RAM agree, the disc does not'))
        check(bool(emsc_problems(label, blob, [ram_copy(cap, f'{label} RAM', [xor(cap.ram, last)])],
                                 twin[label], disc)), miss(f'{label}: RAM byte'))
    # -- the static bank and dynamic list extents (- 1, - 4; the last entry
    # dropped with the count word still 12)
    bank_img = (A / 'level/static_bank.emsc').read_bytes()
    dyn_img = (A / 'level/dynamic_objects.emsc').read_bytes()
    for what, b1, b2 in (('static bank - 1', emsc_blob(bank_img, cut_tail=1), dyn_img),
                         ('static bank - 4', emsc_blob(bank_img, cut_tail=4), dyn_img),
                         ('dynamic list - 1', bank_img, emsc_blob(dyn_img, cut_tail=1)),
                         ('dynamic list last entry', bank_img, emsc_blob(dyn_img, cut_tail=0x860))):
        check(bool(level_bank_problems(b1, b2, [cap])), miss(what))
    # -- message counts and bank sizes (each part one unit short, header
    # consistent), the line-config block, D_00264DD0[0], the -1 row search
    mm = (A / 'message_data.emmd').read_bytes()
    for cut in ('gbank', 'abank', 'grec', 'arec', 'rows'):
        check(bool(messages_problems(mm_blob(mm, cut), [cap], elf)[0]), miss(f'message {cut} one short'))
    for fo, _a, size in MSG_BLOCKS:
        for at in (fo, fo + size - 2, fo + size - 1):
            check(bool(messages_problems(mutate(mm, at), [cap], elf)[0]), miss(f'message block byte +{at:#x}'))
    gp = C.u32(cap.ram, MSG_TABLES)
    check(bool(messages_problems(mm, [ram_copy(cap, 'gptr', [(MSG_TABLES, struct.pack('<I', gp + 8))])], elf)[0]),
          miss('RAM D_00264DD0[0] + 8'))
    gc, ac, rows = struct.unpack_from('<3I', mm, 12)
    srow = 140 + 8 * (gc + ac)
    for row, word, reported in ((0, 0xFFFFFFFF, True), (3, 0x0001FFFF, False)):
        bad = bytearray(mm)
        struct.pack_into('<I', bad, srow + 16 * row, word)
        got = messages_problems(bytes(bad), [ram_copy(cap, f'row {row}', [(MSG_STREAMS + 16 * row,
                                                                          struct.pack('<I', word))])], elf)[0]
        check(bool(got) == reported, miss(f'stream row {row} word {word:#x} in file and RAM ({got})'))
    # -- the roster: sub byte, a group record dropped (header consistent), a
    # placement dropped, RAM-only and disc-only record differences
    check(not roster_equal(mutate(roster, 0x09), cap.ram), miss('roster sub byte'))
    groups = C.u16(roster, 0x0A)
    table_ = [list(struct.unpack_from('<II', roster, 0x18 + 8 * g)) for g in range(groups)]
    last_record = table_[-1][0] + 0x2C * table_[-1][1] - 1      # the last group record's last byte in RAM
    at, recs = 0x18 + 8 * groups, []
    for _a, n in table_:
        recs.append(roster[at:at + 0x2C * n])
        at += 0x2C * n
    places = roster[at:]
    table_[-1][1] -= 1
    head = bytearray(roster[:0x18])
    dropped = bytes(head) + b''.join(struct.pack('<II', a, n) for a, n in table_) + \
        b''.join(recs[:-1]) + recs[-1][:-0x2C] + places
    check(not roster_equal(dropped, cap.ram), miss('roster: last group record dropped'))
    struct.pack_into('<I', head, 0x0C, C.u32(roster, 0x0C) - 1)
    check(not roster_equal(bytes(head) + roster[0x18:-0x28], cap.ram), miss('roster: last placement dropped'))
    last_group = 0x18 + 8 * groups + sum(len(r) for r in recs) - 1
    ram_side = ram_copy(cap, 'roster RAM', [xor(cap.ram, last_record)])
    check(not roster_equal(roster, ram_side.ram), miss('roster: a group record byte in RAM'))
    check(any('from the disc' in x for x in roster_problems(T, mutate(roster, last_group), [ram_side])[0]),
          miss('roster: file and RAM agree, the disc does not'))
    # -- placement nodes: the last byte of each copied field, the behaviour
    # word's high half, and the class byte (0x8B is processed, 0x0B skipped)
    first_place = 0x18 + 8 * groups + sum(len(r) for r in recs)
    _ok, _live, pairs = roster_nodes(T, roster, cap.ram)
    i, node = pairs[0]
    f, _pos, _rot = T.spawn_fields_placement(roster[first_place + 0x28 * i:first_place + 0x28 * (i + 1)], i)
    for off in (0x03, 0x0D, 0x54):
        at = node + off + len(f[off]) - 1
        check(not roster_nodes(T, roster, mutate(cap.ram, at))[0], miss(f'placement node +{at - node:#x}'))
    for off in (0x12, 0x13):
        got = roster_problems(T, roster, [ram_copy(cap, 'behaviour', [xor(cap.ram, node + off)])])[0]
        check(any('placement nodes live' in x for x in got), miss(f'node behaviour byte +{off:#x}'))
    for byte, want in ((0x8B, ROSTER_LIVE), (0x0B, (ROSTER_LIVE[0] - 1, None))):
        bad = bytearray(roster)
        bad[first_place + 0x28 * i] = byte
        ok, live, _p = roster_nodes(T, bytes(bad), cap.ram)
        check(ok and live[0] == want[0] and (want[1] is None or live == want),
              miss(f'placement record class byte {byte:#x} (live {live})'))
    # -- the sound references: the binding count, the handle bounds, the
    # tick step, the pitch branch and the 32-bit product
    names = sorted({x for x in ('2.0:area01_banks.bin#0', '4.0:area01_banks.bin#1', '4.1:area01_banks.bin#2')})
    check(not binding_problems(names, [(3, 0)]) and binding_problems(names + ['4.2:area01_banks.bin#1'], [(3, 0)])
          and binding_problems(names[:2], [(3, 0)]) and binding_problems(names, []), miss('area bank binding count'))
    check(bank_handle(cap.ram, 2, 0) != 0 and all(bank_handle(cap.ram, g, bi) == 0
                                                 for g, bi in ((1, 0x14), (6, 0), (-1, 0), (0, -1))),
          miss('bank handle bounds'))
    script = bytes([0xA0, 0x10, 0x20, 0x30, 0x60, 0xFF, 0x2F, 0x00])
    ram = ram_copy(cap, 'tempo', [(SCRATCH_RAM, script), (D_0027F740 + 2 * 0x1D, struct.pack('<H', 25))]).ram
    got = events_001152D8(ram, SCRATCH_RAM)
    # delta 0x60: 0x60 << 12 over the step 0x1E0000 // 25 = 78643 is 5.00001
    check(got[-1] == ('end', 6, ()), miss(f'the integer tick step at tempo 25 ({got})'))
    def pitch(ladder, note, center, fine):
        try:
            return pitch_00117918(ladder, note, center, fine)
        except Refuse:
            return None
    ladder = [1000 + k for k in range(0x240)]
    check(pitch(ladder, 60, 60, 3) == (ladder[0xD0 + 3] * 44100) // 48000, miss('pitch at note == center'))
    ladder[0xD0 + 3] = 50000
    check(pitch(ladder, 72, 60, 3) == 115032704 // 48000, miss('the 32-bit pitch product'))
    for lad, want in ((17833, None), (17832, 0x3FFF), (1, None)):     # 17833 * 44100 // 48000 = 0x4000
        ladder[0xD0 + 3] = lad
        check(pitch(ladder, 60, 60, 3) == want, miss(f'pitch bounds (ladder {lad})'))
    # the binding verdict through sfx_problems: group 4 slot 1's handle
    # cleared leaves two area-bank bindings
    banks = V['banks']
    one = ram_copy(cap, 'two bindings', [(D_00281D50 + 4 * (4 * 0x14 + 1), bytes(4))])
    check(any(x.startswith('area bank bindings') for x in sfx_problems(S, [one], K.lmap, K.info, banks, V['emsr'])[0]),
          miss('two area-bank bindings through sfx_problems'))
    # the channel defaults: every row, bytes 3 / 0xC / 0xE
    rows = [bytes(16)] * 48
    for t, j in ((47, 3), (47, 0xC), (1, 0xE)):
        bad = list(rows)
        bad[t] = bytes(16)[:j] + b'\x01' + bytes(16)[j + 1:]
        try:
            channel_defaults(bad)
            check(False, miss(f'channel {t} byte {j:#x} differs'))
        except Refuse:
            pass
    check(channel_defaults(rows) == bytes(16), miss('equal channel rows'))
    # the ADPCM header bounds: shift 12 and filter 4 decode, shift 13 and
    # filter 5 are refused
    for head, ok in ((0x0C, True), (0x4C, True), (0x0D, False), (0x5C, False)):
        try:
            _adpcm(bytes([head, 0x01]) + bytes(14), None)
            check(ok, miss(f'ADPCM header {head:#x} refused'))
        except Refuse:
            check(not ok, miss(f'ADPCM header {head:#x} accepted'))
    # a script longer than 0x100 bytes (60 events of 5 bytes) still parses
    long = bytes([0xA0]) + (bytes([0x10, 0x20, 0x30, 0x01]) + bytes([0xA0])) * 59 + \
        bytes([0x10, 0x20, 0x30, 0x01, 0xFF, 0x2F, 0x00])
    ram = ram_copy(cap, 'long', [(SCRATCH_RAM, long)]).ram
    try:
        check(len(events_001152D8(ram, SCRATCH_RAM)) == 61, miss('a 60-event script'))
    except Refuse:
        check(False, miss('a 60-event script refused'))
    # the overlay pin
    try:
        pinned_overlay(mutate(_DISC['overlay'], 0x100))
        check(False, miss('a changed overlay module accepted'))
    except SystemExit:
        pass
    # version words and zero words of every compared container
    wm = (A / 'world_models.emwm').read_bytes()
    for at in (4, 0x14, 0x18):
        check(bool(world_models_problems(mutate(wm, at), [cap])[0]), miss(f'world models +{at:#x}'))
    check(bool(world_models_problems(wm[:12] + bytes(4) + wm[16:32], [cap])[0]), miss('world models span 0'))
    for rel, layout in (('spawn_table.emsp', spawn_layout), ('door_destinations.emsp', doors_layout)):
        check(bool(emsp_problems(rel, mutate((A / rel).read_bytes(), 4), [cap], layout(disc_reader()))[0]),
              miss(f'{rel} version'))
    for label, rel in (('scripts', 'area01_scripts/scripts.emsc'), ('overlay data', 'overlay_data.emsc')):
        check(bool(emsc_header_problems(label, mutate((A / rel).read_bytes(), 4))), miss(f'{label} version'))
    check(bool(level_bank_problems(mutate(bank_img, 4), dyn_img, [cap])) and
          bool(level_bank_problems(bank_img, mutate(dyn_img, 4), [cap])), miss('bank / list version'))
    for at in range(140):                 # every header and block byte of the message file
        if not messages_problems(mutate(mm, at), [cap], elf)[0]:
            check(False, miss(f'message header byte +{at:#x}'))
            break
    # -- the grid: D_0028A598, the block inside the load map and outside the
    # directory allowance, the form word, a count word >= 0x8000 (negative
    # for the original's s16 read, so no node: refused, not read as 32768+
    # nodes), and the index pad of an odd index count
    allow = (table, table + len(cells))
    hdr = struct.unpack_from('<9I', cap.ram, GRID)
    nodes = GRID + hdr[8]
    check(bool(grid_problems(mutate(cap.ram, 0x28A598 + 1), image, allow)), miss('RAM D_0028A598'))
    check(bool(grid_problems(cap.ram, image, (nodes, nodes + 1))), miss('grid inside the allowance'))
    end = nodes + 64 * C.s16(cap.ram, GRID + 0x24)
    check(bool(grid_problems(cap.ram, image, (end - 1, end))), miss('the last node byte inside the allowance'))
    part = C.LoadedImage([m for m in K.lmap if not m[4].endswith('f13_id72.bin')])
    part.cache = image.cache
    check(any('outside the load map' in x for x in grid_problems(cap.ram, part, allow)), miss('grid outside the map'))
    check((emcl_equal(emcl, ram_copy(cap, 'form', [(GRID + 0x1C, struct.pack('<I', 0xD))]).ram) or '')
          .startswith('RAM grid'), miss('grid form 0xD'))
    n = C.s16(cap.ram, GRID + 0x24)
    ram = ram_copy(cap, 'count', [(GRID + 0x24, struct.pack('<H', 0x8000)),
                                  (nodes + 64 * n, bytes(64 * (0x8000 - n)))]).ram
    check((emcl_equal(emcl, ram) or '').startswith('RAM grid'), miss('grid count word 0x8000'))
    first = GRID + hdr[4] + C.u32(cap.ram, nodes + 0x1C)          # node 0's first s16 vertex index
    got = emcl_equal(emcl, ram_copy(cap, 'index', [(first, struct.pack('<h', hdr[1]))]).ram) or ''
    check(got.startswith('RAM grid: node 0: vertex index'), miss(f'a vertex index equal to the count ({got})'))
    ram = bytearray(cap.ram)            # node 0 one vertex longer: 3345 indices
    ram[nodes + 0x18] += 1
    rb = emcl_from_ram(bytes(ram))
    v, p, i = struct.unpack_from('<3I', rb, 8)
    edges = 0x30 + 12 * v + 24 * p + 2 * i + 2
    first_edge = GRID + hdr[2] + C.u32(ram, nodes + 0x20)
    check(i % 2 == 1 and rb[edges:edges + 12] == ram[first_edge:first_edge + 12], miss('the pad of an odd index count'))
    # -- LoadedImage.locate at every file seam: the next file, not the end
    # of the previous one
    seams = [(a, b) for a, b in zip(image.map, image.map[1:]) if a[0] + a[3] == b[0]]
    check(len(seams) > 0 and all(image.locate(b[0])[0] == b[0] for _a, b in seams), miss('locate at a file seam'))
    # -- zone EMDL: the second bone parent word, V of the first and last vertex
    zbuf, zb, gs, prim = V['zone']
    e = emdl_parts(zbuf)
    for what, at in (('bone parent 2', 40), ('bone parent 2 top byte', 43), ('V', e['verts'] + 28),
                     ('last vertex V', e['verts'] + 40 * (e['vc'] - 1) + 28)):
        check(not zone_equal(K.el, L, mutate(zbuf, at), zb, gs, prim), miss(f'zone {what}'))


class PatchedImage:
    """A LoadedImage whose reads see `patches` = [(address, bytes)] (a disc
    image with changed bytes, for the grid controls)."""

    def __init__(self, image, patches):
        self.image, self.patches = image, patches

    def read(self, a, n):
        out = bytearray(self.image.read(a, n))
        for at, data in self.patches:
            for j, x in enumerate(data):
                if a <= at + j < a + n:
                    out[at + j - a] = x
        return bytes(out)


def sweep2_controls(K, V, cap):
    """Round-5 controls: an input for each non-equivalent survivor of the
    second independent sweep (docs/AREA01_ASSETS.md), each on the
    comparator it weakened; each must be reported (or, where marked,
    accepted)."""
    L, T, elf, image = K.L, K.T, K.elf, K.image
    roster, emcl, cells, table = V['roster'], V['emcl'], V['cells'], V['table']
    miss = lambda what: f'control: {what} missed'
    # -- world models: the span one word longer (the RAM bytes after it,
    # header consistent: WM-GROW) and one word shorter; a table with no
    # model (span 4, count 0) is accepted
    wm = (A / 'world_models.emwm').read_bytes()
    wtable, span = C.u32(wm, 8), C.u32(wm, 12)
    for what, bad in (('span + 4 RAM bytes', wm[:12] + struct.pack('<I', span + 4) + wm[16:] +
                       cap.ram[wtable + span:wtable + span + 4]),
                      ('span - 4', wm[:12] + struct.pack('<I', span - 4) + wm[16:-4])):
        check(any('world model span' in x for x in world_models_problems(bad, [cap])[0]), miss(f'world models {what}'))
    empty = struct.pack('<4s7I', b'EMWM', 1, wtable, 4, 0, 0, 0, 0) + bytes(4)
    got = world_models_problems(empty, [ram_copy(cap, 'no models', [(wtable, bytes(4))])])[0]
    check(not got, miss(f'a model table without models accepted ({got})'))
    # wm_span over synthetic tables at 0x200, each extent term the largest in
    # turn: the header, the block data, the skeleton, an offset word with its
    # low bits set, and models below the table (signed offsets: the table's
    # own extent)
    def synth(count, offsets, header_at, qwc, bones, skel):
        r = bytearray(0x1000)
        struct.pack_into('<I', r, 0x200, count)
        for k, o in enumerate(offsets):
            struct.pack_into('<i', r, 0x204 + 4 * k, o)
        struct.pack_into('<3I', r, 0x200 + header_at + 4, qwc, bones, skel)
        return bytes(r)
    for what, r, want in (('header', synth(1, [0x10], 0x10, 0, 0, 0), 0x50),
                          ('block data', synth(1, [0x10], 0x10, 8, 0, 0x20), 0xD0),
                          ('skeleton', synth(1, [0x10], 0x10, 0, 2, 0x100), 0x1B0),
                          ('offset low bits', synth(1, [0x13], 0x10, 0, 2, 0x100), 0x1B0),
                          ('models below the table', synth(3, [-0x100] * 3, -0x100, 0, 0, 0), 0x10)):
        try:
            got = wm_span(r, 0x200)
        except struct.error as error:
            got = repr(error)
        check(got == want, miss(f'wm_span {what} ({got} != {want:#x})'))
    # a capture without a GS freeze stops the run
    try:
        gs_captures([cap, SimpleNamespace(name='no freeze', gs=None)])
        check(False, miss('a capture without a GS freeze accepted'))
    except SystemExit:
        pass
    # -- the four pointers, bytes 2 and 3 (the high half)
    bank_img = (A / 'level/static_bank.emsc').read_bytes()
    dyn_img = (A / 'level/dynamic_objects.emsc').read_bytes()
    allow = (table, table + len(cells))
    for byte in (2, 3):
        for name, at in (('D_0028A5A0', 0x28A5A0), ('D_0028A5A4', 0x28A5A4)):
            got = level_bank_problems(bank_img, dyn_img, [ram_copy(cap, 'ptr', [xor(cap.ram, at + byte)])])
            check(f'ptr: {name}' in got, miss(f'RAM {name} byte {byte}'))
        got = world_models_problems(wm, [ram_copy(cap, 'ptr', [xor(cap.ram, 0x28A59C + byte)])])[0]
        check('ptr: world model bank differs' in got, miss(f'RAM D_0028A59C byte {byte}'))
        got = grid_problems(ram_copy(cap, 'ptr', [xor(cap.ram, 0x28A598 + byte)]).ram, image, allow)
        check(any(x.startswith('D_0028A598 = ') for x in got), miss(f'RAM D_0028A598 byte {byte}'))
    # -- two captures, only the second one different: the roster RAM
    # rebuild and the dynamic list compare run per capture
    groups = C.u16(roster, 0x0A)
    ga, gn = struct.unpack_from('<II', roster, 0x18 + 8 * (groups - 1))
    got = roster_problems(T, roster, [cap, ram_copy(cap, 'second', [xor(cap.ram, ga + 0x2C * gn - 1)])])[0]
    check(got == ['second: roster records differ from RAM'], miss(f'the second capture\'s roster RAM ({got})'))
    dbase = C.u32(dyn_img, 8)
    got = level_bank_problems(bank_img, dyn_img, [cap, ram_copy(cap, 'second', [xor(cap.ram, dbase + 0x100)])])
    check(got == ['second: dynamic list differs from RAM'], miss(f'the second capture\'s dynamic list ({got})'))
    # -- the dynamic list count word 0x10001 with one entry, in the file and
    # RAM: 0x10001 entries, not 1
    one = struct.pack('<I', 0x10001) + dyn_img[24:20 + 0x10 + 0x860]
    got = level_bank_problems(bank_img, C.emsc(dbase, one), [ram_copy(cap, 'count', [(dbase, one)])])
    check(any('dynamic list length' in x for x in got), miss(f'dynamic list count 0x10001 ({got})'))
    # -- a negative object offset (-0xC, object 5, file and RAM; RAM there is
    # 0, so no units): 001C6120's >> 2 << 2 keeps the sign; accepted
    base = C.u32(bank_img, 8)
    neg = struct.pack('<i', -0xC)
    if C.u32(cap.ram, base - 0xC) == 0:
        b2 = bytearray(bank_img)
        b2[20 + 4 + 4 * 5:20 + 8 + 4 * 5] = neg
        try:
            got = level_bank_problems(bytes(b2), dyn_img, [ram_copy(cap, 'neg', [(base + 4 + 4 * 5, neg)])])
        except struct.error as error:
            got = [repr(error)]
        check(not got, miss(f'a negative object offset accepted ({got})'))
    else:
        check(False, miss('a negative object offset (RAM at base - 0xC is not 0)'))
    # -- placement records and nodes: word 0 0x010B is class 0x0B (skipped);
    # the rotation and position bytes of an at-rest node
    first_place = 0x18 + 8 * groups + sum(0x2C * struct.unpack_from('<II', roster, 0x18 + 8 * g)[1]
                                         for g in range(groups))
    _ok, _live, pairs = roster_nodes(T, roster, cap.ram)
    i, _node = pairs[0]
    bad = bytearray(roster)
    bad[first_place + 0x28 * i:first_place + 0x28 * i + 2] = b'\x0b\x01'
    check(roster_nodes(T, bytes(bad), cap.ram)[1][0] == ROSTER_LIVE[0] - 1, miss('placement record word 0 0x010B'))
    at_rest = next(a for i, a in pairs if cap.ram[a + 0xB0:a + 0xCC] ==
                   (lambda f: f[1] + cap.ram[a + 0xBC:a + 0xC0] + f[2])(
                       T.spawn_fields_placement(roster[first_place + 0x28 * i:first_place + 0x28 * (i + 1)], i)))
    for off in (0xB0, 0xBB, 0xC0, 0xC9, 0xCB):
        got = roster_problems(T, roster, [ram_copy(cap, 'rest', [xor(cap.ram, at_rest + off)])])[0]
        check(any('(49, 45)' in x for x in got), miss(f'at-rest node +{off:#x} ({got})'))
    # -- the grid: allowances over the first and the last byte of each
    # section report that section alone; allowances that only touch the
    # block's ends are accepted
    hdr = struct.unpack_from('<9I', cap.ram, GRID)
    n = C.s16(cap.ram, GRID + 0x24)
    sections = (('header', 0, 0x28), ('vertices', hdr[0], 12 * hdr[1]), ('edge normals', hdr[2], 12 * hdr[3]),
                ('indices', hdr[4], 2 * hdr[5]), ('tables', hdr[6], 24 * n), ('nodes', hdr[8], 64 * n))
    for name, off, size in sections:
        for at in (GRID + off, GRID + off + size - 1):
            got = [x for x in grid_problems(cap.ram, image, (at, at + 1)) if 'allowance' in x]
            check(got == [f'grid {name} inside the cell-directory allowance'], miss(f'grid allowance at {at:#x} ({got})'))
    end = max(GRID + off + size for _n, off, size in sections)
    for lo in (GRID - 1, end):
        got = grid_problems(cap.ram, image, (lo, lo + 1))
        check(not got, miss(f'an allowance touching the grid block at {lo:#x} accepted ({got})'))
    # -- every node's index / edge-normal run inside its section: the last
    # node's run one entry past the end is refused, ending at the end accepted
    last = GRID + hdr[8] + 64 * (n - 1)
    count = cap.ram[last + 0x18]
    for what, off, value, reported in (('index run one past', 0x1C, 2 * hdr[5] - 2 * count + 2, True),
                                       ('index run at the end', 0x1C, 2 * hdr[5] - 2 * count, False),
                                       ('edge run one past', 0x20, 12 * hdr[3] - 12 * count + 12, True),
                                       ('edge run at the end', 0x20, 12 * hdr[3] - 12 * count, False)):
        got = grid_problems(cap.ram, PatchedImage(image, [(last + off, struct.pack('<I', value))]), allow)
        check(any('run outside its section' in x for x in got) == reported, miss(f'grid {what} ({got})'))
    # -- the EMCL rebuild: +0.0 / -0.0 twins are refused (the pool would
    # change a vertex's bits), exact twins are merged; a vertex index -1 is
    # a bad RAM grid, not a difference
    x0 = GRID + hdr[0]
    v0 = struct.pack('<f', 0.0) + cap.ram[x0 + 4:x0 + 12]
    for sign, reported in ((-0.0, True), (0.0, False)):
        r = ram_copy(cap, 'twin', [(x0, v0), (x0 + 12, struct.pack('<f', sign) + v0[4:])]).ram
        try:
            emcl_from_ram(r)
            refused = False
        except ValueError as error:
            refused = 'changes its bits' in str(error)
        check(refused == reported, miss(f'grid vertex twins x = +0.0 / {sign!r}'))
    first = GRID + hdr[4] + C.u32(cap.ram, GRID + hdr[8] + 0x1C)
    got = emcl_equal(emcl, ram_copy(cap, 'index', [(first, struct.pack('<h', -1))]).ram) or ''
    check(got.startswith('RAM grid: node 0: vertex index -1'), miss(f'a vertex index -1 ({got})'))
    # -- a hull starting inside the uid table (synthetic one-uid directories
    # with an empty hull at +4, inside, and at +8, just after the table;
    # the real first hull starts exactly at the table's end, 4 + 4 * 38)
    got = [hulls_inside_table(L, struct.pack('<II', 1, at) + bytes(0x20)) for at in (4, 8)]
    check(got == [[0], []] and not hulls_inside_table(L, cells), miss(f'a hull inside the uid table ({got})'))
    got = cells_problems(L, elf, struct.pack('<II', 1, 4) + bytes(0x20), image, [cap])[0]
    check(any('start inside the uid table' in x for x in got), miss(f'the uid-table problem reported ({got[:2]})'))
    # -- the moved set compared as the list level.json records: order and
    # duplicates count
    moved = sorted(V['moved'])
    for what, want in (('order', [moved[1], moved[0]] + moved[2:]), ('duplicate', moved + moved[-1:])):
        check(moved_problem(V['moved'], {'cells': {'moved_uids': want}}) is not None, miss(f'moved set {what}'))
    # -- 00119EA0: only a +0x20 word of 0xFFFFFFFF refuses the header
    h = bank_handle(cap.ram, 2, 0)
    hdr_at = C.u32(cap.ram, D_0027C6C0 + 12 * h + 4)
    gi = next((g, k) for g in range(0x10) for k in range(0x10) if script_00119EA0(cap.ram, h, g, k) is not None)
    want = script_00119EA0(cap.ram, h, *gi)
    for word, result in ((0xFFFF, want), (0xFFFFFFFF, None)):
        r = ram_copy(cap, 'hdr', [(hdr_at + 0x20, struct.pack('<I', word))]).ram
        check(script_00119EA0(r, h, *gi) == result, miss(f'00119EA0 header +0x20 = {word:#x}'))
    # -- 001152D8: a B0 event must start with 0x41; the end event is FF 2F 00
    for what, data in (('B0 event 0x07', bytes([0xB0, 0x07, 0x10, 0x20, 0x30, 0x40, 0x00, 0xFF, 0x2F, 0x00])),
                       ('end FF 2F 01', bytes([0xA0, 0x10, 0x20, 0x30, 0x00, 0xFF, 0x2F, 0x01]))):
        try:
            events_001152D8(ram_copy(cap, 'script', [(SCRATCH_RAM, data)]).ram, SCRATCH_RAM)
            check(False, miss(f'script {what} accepted'))
        except Refuse:
            pass
    # -- ADPCM: a tone whose end block is the body's last 16 bytes decodes; a
    # repeating block whose loop does not settle is refused
    try:
        adpcm(bytes([0x00, 0x01]) + bytes(14), 0)
    except Refuse:
        check(False, miss('an end block that ends the body refused'))
    try:
        _adpcm(bytes([0x18, 0x07]) + b'\x11' * 14, 0)
        check(False, miss('an unsettled loop accepted'))
    except Refuse:
        pass
    # -- the container exactly as long as upload section 0 is inside it
    banks, info = V['banks'], K.info
    s0 = info['sections'][0]
    exact = dict(info, sections=[(s0[0], len(banks), *s0[2:])] + list(info['sections'][1:]))
    got = banks_problems(banks, exact, K.lmap)
    check(not got, miss(f'a container exactly upload section 0 long accepted ({got})'))
    # -- 001A2370 writing outside the hull the directory walk gives it: hull
    # 10 rebuilt as [extended box, compact box, compact box] (the walk sizes
    # 0x24, 0x14, 0x14; 001A2370 tests 0x800 on the first primitive only and
    # steps every box by 0x24, so it reads the third box at +0x10 of the
    # walk's third one and writes past the walk's end). The capture shows
    # the derived hull and the disc bytes after it: the derivation is not a
    # proof (it changed other bytes), so the hull must be reported.
    from test_coll_move_reference import FloatEE
    _count, hulls, _size = L.cell_directory(cells, 0)
    s, _e, _f = hulls[10]
    mod = bytearray(cells)
    struct.pack_into('<h', mod, s + 0x18, 3)
    struct.pack_into('<HH3ff3f', mod, s + 0x1C, 0x8800, 0, 0, 0, 0, 1.0, 1.0, 2.0, 3.0)
    struct.pack_into('<HH4f', mod, s + 0x1C + 0x24, 0x8000, 0, 0, 0, 0, 1.0)
    struct.pack_into('<HH3fHH', mod, s + 0x1C + 0x38, 0x8000, 0, 0, 0, 0, 0x8000, 0)
    mod = bytes(mod)
    e2 = L.cell_directory(mod, 0)[1][10][1]
    node = next(a for _s, a in T.pool_nodes(cap.ram)
                if C.u16(cap.ram, a + 0x0E) >> 8 == 10 and L.owner_matrix(cap.ram, a) is not None)
    ee = FloatEE(elf, cap.ram, cap.spad)
    ee.write(table, mod)
    ee.call(L.RETRANSFORM, (node, L.owner_matrix(cap.ram, node)))
    derived = ee.read(table, len(mod))
    outside = any(derived[k] != mod[k] for k in range(len(mod)) if not s <= k < e2)
    r = bytearray(cap.ram)
    r[table:table + len(mod)] = mod
    r[table + s:table + e2] = derived[s:e2]
    got = L.verify_cell_directory(elf, mod, [SimpleNamespace(name='overrun', ram=bytes(r), spad=cap.spad)])[1]
    check(outside and any(x.startswith('overrun: hull 10 differs and no owner derivation') for x in got),
          miss(f'a derivation that writes outside its hull accepted (outside {outside}, {got})'))
    # -- a derivation that changes only the hull's last byte is a proof:
    # hull 10 rebuilt as [extended box, compact mesh of one lane pair]
    # (walk sizes 0x24 and 0x2C); 001A2370 steps the mesh as extended, reads
    # its axis and lanes after the walk's end (the free bytes of the old
    # hull) and writes its lanes up to exactly the walk's end. The directory
    # is the call's own result with the last byte changed, so the call
    # changes that byte alone, and the capture shows the result.
    mod = bytearray(cells)
    p0 = s + 0x1C
    struct.pack_into('<h', mod, s + 0x18, 2)
    struct.pack_into('<HH3ff3f', mod, p0, 0x8800, 0, 0, 0, 0, 1.0, 1.0, 2.0, 3.0)
    struct.pack_into('<4B', mod, p0 + 0x24, 0x00, 0x10, 1, 0)
    for at, v in ((0x2C, (0.0, 0.0, 1.0)), (0x3C, (1.0, 1.0, 1.0)), (0x48, (0.0, 1.0, 0.0))):
        struct.pack_into('<3f', mod, p0 + 0x24 + at, *v)
    e2 = L.cell_directory(bytes(mod), 0)[1][10][1]

    def run(disc, ram=cap.ram, who=node):
        ee = FloatEE(elf, ram, cap.spad)
        ee.write(table, disc)
        ee.call(L.RETRANSFORM, (who, L.owner_matrix(ram, who)))
        return ee.read(table, len(disc))
    result = run(bytes(mod))
    disc = bytearray(result)
    disc[e2 - 1] ^= 1
    disc = bytes(disc)
    again = run(disc)
    r = bytearray(cap.ram)
    r[table:table + len(disc)] = again
    got = L.verify_cell_directory(elf, disc, [SimpleNamespace(name='last byte', ram=bytes(r), spad=cap.spad)])[1]
    only = [k for k in range(len(disc)) if again[k] != disc[k]] == [e2 - 1]
    check(only and not got, miss(f'a derivation changing only the hull\'s last byte refused (only {only}, {got})'))
    # -- two owners of different uids passing the same matrix (hull 10's
    # 0x826D40 owner made to pass hull 31's owner's node + 0xD0): each
    # derivation is its own, whatever the memo holds
    other = 0x7B1530
    if C.u16(cap.ram, other + 0x0E) >> 8 == 31 and L.owner_matrix(cap.ram, other) == other + 0xD0 and \
            C.u32(cap.ram, node + 0x10) == 0x826D40:
        r = bytearray(cap.ram)
        struct.pack_into('<I', r, node + 0x11C, other + 0xD0 - 0x90)
        r = bytes(r)
        derived = run(cells, r, node)
        s10, e10, _f = hulls[10]
        r = r[:table + s10] + derived[s10:e10] + r[table + e10:]
        got = L.verify_cell_directory(elf, cells, [SimpleNamespace(name='shared matrix', ram=r, spad=cap.spad)])[1]
        check(not got, miss(f'two owners passing one matrix ({got})'))
    else:
        check(False, miss('two owners passing one matrix (the arrival\'s owners moved)'))
    # -- every texture-key field names its own decode: zone 0's first
    # texture made two pages high (so its width in pages, tbw, matters),
    # and textures differing from it in one field, each decode to their own
    # texels
    zbuf, zb, gs_caps, _prim = V['zone']
    f, gs_path = dict(zb.tex_table[0]), gs_caps[0][1]
    f['th'] += 1
    mine = gs_texels(K.el, gs_path, [[f]])[(gs_path, texture_key(f))]
    for field, value in (('tbp0', f['tbp0'] + 32), ('tbw', f['tbw'] * 2), ('psm', 0x13), ('tw', f['tw'] - 1),
                         ('th', f['th'] - 1), ('cbp', f['cbp'] + 4)):
        f2 = dict(f, **{field: value})
        direct = K.el.build_texture_blob(None, [f2], Path(gs_path))[1]
        stored = gs_texels(K.el, gs_path, [[f2]])[(gs_path, texture_key(f2))]
        check(direct != mine and stored == direct, miss(f'the texture key field {field}'))


def sweep3_controls(K, V, cap):
    """Close-out controls: the killing input of each non-equivalent
    survivor of the third independent sweep and the final review
    (docs/AREA01_ASSETS.md, "Close-out"), on the comparator it weakened;
    each must be reported (or, where marked, accepted)."""
    L, elf, image = K.L, K.elf, K.image
    cells, table, emcl = V['cells'], V['table'], V['emcl']
    miss = lambda what: f'control: {what} missed'
    # -- the load map: an entry out of address order, and an entry one byte
    # into the next (the real map, checked in run_checks, has touching seams)
    lm = list(K.lmap)
    check(any('not in address order' in x for x in load_map_problems([lm[1], lm[0]] + lm[2:])),
          miss('a load map out of address order'))
    seam = next(k for k in range(len(lm) - 1) if lm[k][0] + lm[k][3] == lm[k + 1][0])
    over = lm[:seam] + [(*lm[seam][:3], lm[seam][3] + 1, lm[seam][4])] + lm[seam + 1:]
    check(load_map_problems(over) == [f'load map entries {lm[seam][4]} and {lm[seam + 1][4]} overlap'],
          miss('an overlapping load map'))
    # -- the static bank extent (S05, S07, S08): synthetic banks in unused
    # RAM, D_0028A5A0 pointed at them, the file = RAM
    bank_img = (A / 'level/static_bank.emsc').read_bytes()
    dyn_img = (A / 'level/dynamic_objects.emsc').read_bytes()
    sb = SCRATCH_RAM + 0x100
    # one object whose offset word -0x40 puts it (and its unit word) below
    # the bank: 1 unit in the first capture ends it exactly at the bank's
    # end; 0 units in the second leaves the bank too long there
    head = struct.pack('<Ii', 1, -0x40)
    bank = C.emsc(sb, head + bytes(0x820 - len(head)))
    two = [ram_copy(cap, name, [(sb, head), (sb - 0x40, struct.pack('<I', units)), (0x28A5A0, struct.pack('<I', sb))])
           for name, units in (('first', 1), ('second', 0))]
    got = level_bank_problems(bank, dyn_img, two)
    check(len(got) == 1 and got[0].startswith('second: static bank length'),
          miss(f'a bank longer than its objects in the second capture only ({got})'))
    # one object at +8 with unit word 0x10001; the length by its low half
    head = struct.pack('<IiI', 1, 8, 0x10001)
    length = 8 + 0x40 + 0x820
    bank = C.emsc(sb, head + bytes(length - len(head)))
    got = level_bank_problems(bank, dyn_img, [ram_copy(cap, 'units', [(sb, head), (0x28A5A0, struct.pack('<I', sb))])])
    check(any('static bank length' in x for x in got), miss(f'an object unit word 0x10001 ({got})'))
    # object 5's offset word + 1 (low bits: 001C6120's >> 2 << 2 ignores
    # them) in the file and RAM: accepted
    word = C.s32(bank_img, 20 + 4 + 4 * 5)
    b2 = bytearray(bank_img)
    struct.pack_into('<i', b2, 20 + 4 + 4 * 5, word + 1)
    base = C.u32(bank_img, 8)
    got = level_bank_problems(bytes(b2), dyn_img, [ram_copy(cap, 'low bits', [(base + 4 + 4 * 5,
                                                                             struct.pack('<i', word + 1))])])
    check(word & 3 == 0 and not got, miss(f'an object offset word with its low bits set accepted ({got})'))
    # -- the grid form word 0x10C (S18): refused as a bad RAM grid
    got = emcl_equal(emcl, ram_copy(cap, 'form', [(GRID + 0x1C, struct.pack('<I', 0x10C))]).ram) or ''
    check(got.startswith('RAM grid: grid form 0x10c'), miss(f'grid form 0x10C ({got})'))
    # -- 001A2370's code: the last byte of each range (S39) is reported as
    # the code check itself
    for a, n in L.RETRANSFORM_CODE:
        got = L.verify_cell_directory(elf, cells, [ram_copy(cap, 'code', [xor(cap.ram, a + n - 1)])])[1]
        check(got == ['code: 001A2370 code in RAM differs from the ELF'], miss(f'001A2370 code byte {a + n - 1:#x} ({got})'))
    # -- the render-context ELF blocks (S40): room entry 44 + 0x30, a byte
    # 001D8FD0 never reads for area 1, differs from the ELF
    at = L.ROOM_TABLE + L.ROOM_ENTRY * 44 + 0x30
    try:
        L.check_ctx_room_block([ram_copy(cap, 'room entry 44', [xor(cap.ram, at)])], elf)
        check(False, miss(f'room table byte {at:#x} accepted'))
    except SystemExit as error:
        check(str(error).startswith(f'{at:#x}: ELF'), miss(f'room table byte {at:#x} ({error})'))
    # -- the live-owner scan (S41, S42): hull 31's owner node moved to the
    # last pool slot with node byte 0 = 2 (live is any non-zero byte), its
    # matrix (node + 0xD0) moving with it; every other re-transformed hull
    # set back to the disc bytes (one derivation). Accepted, with the new
    # node as the proof
    owner, last = 0x7B1530, L.POOL_BASE + (L.POOL_SLOTS - 1) * L.POOL_STRIDE
    _c, hulls, _s = L.cell_directory(cells, 0)
    ram = bytearray(cap.ram)
    free = not ram[last]
    ram[last:last + L.POOL_STRIDE] = ram[owner:owner + L.POOL_STRIDE]
    ram[owner:owner + L.POOL_STRIDE] = bytes(L.POOL_STRIDE)
    ram[last] = 2
    for uid, (s, e, _f) in hulls.items():
        if uid != 31:
            ram[table + s:table + e] = cells[s:e]
    moved = SimpleNamespace(name='last slot', ram=bytes(ram), spad=cap.spad)
    rows, got = L.verify_cell_directory(elf, cells, [moved])
    check(free and C.u16(cap.ram, owner + 0x0E) >> 8 == 31 and not got and
          rows[0]['proofs'] == {31: f'001A2370(node {last:#x}, behaviour {C.u32(ram, last + 0x10):#x})'},
          miss(f'hull 31\'s owner in the last pool slot with byte 0 = 2 (free {free}, {got}, {rows[0]["proofs"]})'))
    # -- the sound references
    ram = cap.ram
    area, sub = cap.ram[0x810700], cap.ram[0x810701]
    sid = next(s for s in range(0x3E8, 0x5DC) if record_001FB9F0(ram, s, area, sub) is not None)
    # S31: a remap byte 0x80 is record 0x80, not the 0xFF sentinel
    page = C.u32(ram, C.u32(ram, REC_REMAP[0x3E8][0] + 4 * area) + 4 * sub)
    r = ram_copy(cap, 'remap', [(page + sid - 0x3E8, b'\x80')]).ram
    want = C.u32(r, C.u32(r, REC_REMAP[0x3E8][1] + 4 * area) + 4 * sub) + 4 * 0x80
    check(record_001FB9F0(r, sid, area, sub) == want, miss('a remap byte 0x80'))
    # S32: two loop-start blocks, then a repeating end block: the loop
    # starts at the last flagged block (frame 28)
    body = bytes([0x00, 0x04]) + b'\x12' * 14 + bytes([0x00, 0x04]) + b'\x34' * 14 + bytes([0x00, 0x03]) + bytes(14)
    got = adpcm(body, 0)[1]
    check(got == 28, miss(f'the last of two loop-start blocks ({got})'))
    # S34: handle 0x80 is out of range even when its entry is a valid one
    h = bank_handle(ram, 2, 0)
    gi = next((g, k) for g in range(0x10) for k in range(0x10) if script_00119EA0(ram, h, g, k) is not None)
    entry = ram[D_0027C6C0 + 12 * h:D_0027C6C0 + 12 * h + 12]
    check(script_00119EA0(ram_copy(cap, 'handle', [(D_0027C6C0 + 12 * 0x80, entry)]).ram, 0x80, *gi) is None,
          miss('handle 0x80'))
    # S36: a group word 0x8000 | x is an offset, only 0xFFFF is empty: the
    # words it selects are planted so the result is known
    hdr = C.u32(ram, D_0027C6C0 + 12 * h + 4)
    blk = hdr + C.u32(ram, hdr + 0x1C)
    x = C.u16(ram, blk + 2 * gi[0] + 2) | 0x8000
    sel = x >> 1
    r = ram_copy(cap, 'group', [(blk + 2 * gi[0] + 2, struct.pack('<H', x)), (blk + 2 * sel, struct.pack('<H', 0x7F)),
                                (blk + 2 * (gi[1] + sel) + 2, struct.pack('<H', 0x40))]).ram
    got = script_00119EA0(r, h, *gi)
    check(x != 0xFFFF and got == (hdr, blk + 0x40), miss(f'group word {x:#x} ({got})'))
    # S48: a well-formed portamento event B0 41 parses
    script = bytes([0xB0, 0x41, 1, 2, 3, 4, 0x00, 0xFF, 0x2F, 0x00])
    try:
        got = events_001152D8(ram_copy(cap, 'porta', [(SCRATCH_RAM, script)]).ram, SCRATCH_RAM)
    except Refuse as why:
        got = f'Refuse({why})'
    check(got == [('porta', 0, (1, 2, 3, 4)), ('end', 0, ())], miss(f'a B0 41 portamento event ({got})'))
    # S49: a record (1, 1) whose handle word is 0 is unbound (state 3,
    # reason 7); only (1, 0) plays without a handle
    rec = record_001FB9F0(ram, sid, area, sub)
    r = ram_copy(cap, 'unbound', [(rec, b'\x01\x01'), (D_00281D50 + 4 * (1 * 0x14 + 1), bytes(4))]).ram
    bound, _refused, containers = bindings(K.S, [cap], V['banks'])
    got = next(e for e in registry_from_ram(r, bound, containers)[3] if e[0] == sid)
    check(got[1] == 3 and got[3] == REASON['unbound'], miss(f'a record (1, 1) with handle 0 ({got})'))


def main():
    need = [A / 'level/level.json', A / 'tables.json', A / 'sfx/banks.json', C.ELF_PATH, C.OVERLAY_PATH]
    missing = [str(p) for p in need if not p.exists()]
    if missing or not C.ROUTE_A01.exists():
        print('area01 assets reference: SKIPPED, missing local inputs:', missing or [str(C.ROUTE_A01)])
        return 0
    t0 = time.time()
    import export_area01_level as L
    import export_area01_tables as T
    import export_area01_sfx as S
    el = L.load_export_level()
    allcaps = C.captures()
    caps = allcaps if FULL else [c for c in allcaps if c.name in QUICK] or allcaps[:3]
    arrival = next(c for c in allcaps if c.name == C.ARRIVAL.name)
    print(f'area01 assets reference ({MODE}): {len(caps)} of {len(allcaps)} captures')

    lib = build_loaders()
    loaded, cells_rc = check_loaders(lib)
    print(f'  loaders: {sum(loaded.values())}/{len(loaded)} files accepted by the port loaders; '
          f'em_actor_cells_load(area01_cells.bin) = {cells_rc} (uid 0 word has bit 29 set; docs/AREA01_ASSETS.md finding 1)')

    lmap, info = C.build_load_map(arrival)
    K = SimpleNamespace(el=el, L=L, T=T, S=S, elf=C.read_elf(), arrival=arrival, lmap=lmap, info=info,
                        image=C.LoadedImage(lmap))
    V = run_checks(K, caps)
    elf, image, roster, moved, level, table, cells, emcl, banks, emsr = (
        V[k] for k in ('elf', 'image', 'roster', 'moved', 'level_json', 'table', 'cells', 'emcl', 'banks', 'emsr'))
    rep, room = V['tables'], V['room']
    print(f"  load map: {len(lmap)} files, {V['mapped']} bytes equal RAM in each capture")
    lv = V['level']
    print(f"  level: {lv['objects']} bank objects -> {lv['zones']} zone EMDLs equal their rebuild; "
          f"{lv['kicks']} level kicks and {lv['dynamic_kicks']} dynamic kicks inside the exported bank/list")
    print(f'  collision: area01.emcl equals the RAM grid rebuild ({len(emcl)} bytes); area01_cells.bin equals '
          f'the disc, and every RAM directory byte equals it or the original 001A2370 derivation '
          f'(hulls {sorted(moved)})')
    print(f"  tables: roster/spawn ({rep.get('spawn_table.emsp')} EMSP windows)/doors "
          f"({rep.get('door_destinations.emsp')} windows)/scripts ({rep.get('script_records')} records)/overlay data/messages "
          f"({rep.get('message_area_records')} area records, {rep.get('message_stream_rows')} stream rows)/"
          f"world models ({rep.get('world_models')}) equal RAM; placement nodes live / at the record's "
          f"position+rotation {rep.get('placement_nodes_live')} (each must be {ROSTER_LIVE})")
    print(f"  sfx: area01_banks.bin equals the disc container; bound headers equal its rows {V['sfx_names']}; "
          f"sfx_registry.emsr equals the RAM re-derivation ({V['sfx_counts']})")
    if room:
        print(f'  ctx +0xA0..+0xFF: original 001D8FD0 rebuilt {room[0]["ctx_bytes_rebuilt"]} bytes in {len(room)} '
              f'capture(s) from room entry {sorted({r["room_entry"] for r in room})} '
              f'(D_008106C8 & 0x80 set in {sum(r["constant_branch"] for r in room)})')

    # the canary: the same run_checks, over one capture, with one planted
    # difference per section; each section must report its own (this proves
    # that every section's verdict reaches the exit code)
    if not FAILS:
        print(f'  canary: {canary(K, lib)} sections each reported their planted difference')

    # controls: one flipped byte must be caught by each comparator. They
    # start from the exported files, so they run only when those already
    # passed (a broken asset has failed above; its controls would be moot)
    if FAILS:
        print(f'  controls: skipped, {len(FAILS)} check(s) already failed')
    else:
        cap = arrival
        zbuf, zb, gs, prim = V['zone']
        groups = C.u16(roster, 0x0A)
        first_place = 0x18 + 8 * groups + sum(0x2C * struct.unpack_from('<II', roster, 0x18 + 8 * g)[1]
                                             for g in range(groups))
        for at in (first_place + 0x0C, len(roster) - 1, 0x14, 0x08):
            check(not roster_equal(mutate(roster, at), cap.ram), f'control: roster +{at:#x} mutation missed')
        check(bool(roster_problems(T, mutate(roster, 0x25), [cap])[0]), 'control: roster group-table mutation missed')
        check(not roster_equal(roster + b'\0', cap.ram), 'control: roster trailing byte missed')
        _ok, _live, pairs = roster_nodes(T, roster, cap.ram)
        node = pairs[0][1]
        for off in (0x03, 0x0D, 0x54):
            check(not roster_nodes(T, roster, mutate(cap.ram, node + off))[0],
                  f'control: placement node +{off:#x} mutation missed')
        # through roster_problems: the node still counts as live (49, 46), so
        # only the copied-field verdict can report it
        fake = SimpleNamespace(name='node field', ram=mutate(cap.ram, node + 0x03), spad=cap.spad)
        check(any('lost a copied field' in x for x in roster_problems(T, roster, [fake])[0]),
              'control: a placement node with a changed +0x03 passed roster_problems')
        mm = (A / 'message_data.emmd').read_bytes()
        gcount, acount, srows = struct.unpack_from('<3I', mm, 12)
        srow = 140 + 8 * (gcount + acount)
        for name, bad in (('last area record', mutate(mm, srow - 1)), ('stream row', mutate(mm, srow + 16 * 17 + 3)),
                          ('last stream row', mutate(mm, srow + 16 * srows - 1)), ('cursor', mutate(mm, 24)),
                          ('colour block', mutate(mm, 36 + 5)), ('template block', mutate(mm, 120 + 19)),
                          ('stream count', mutate(mm, 20)), ('trailing byte', mm + b'\0'),
                          ('last bank byte', mutate(mm, len(mm) - 1))):
            check(bool(messages_problems(bad, [cap], elf)[0]), f'control: message {name} mutation missed')
        # the RAM side: the -1 row after the last stream row, and a -1 row
        # planted before it
        for name, at in (('-1 row', MSG_STREAMS + 16 * srows), ('early -1 row', MSG_STREAMS + 16 * 5),
                         ('table pointer', MSG_TABLES + 4 * (C.AREA + 1))):
            ram = bytearray(cap.ram)
            ram[at:at + 4] = b'\xff\xff\xff\xff' if name == 'early -1 row' else b'\0\0\0\0'
            fake = SimpleNamespace(name=name, ram=bytes(ram), spad=cap.spad)
            check(bool(messages_problems(mm, [fake], elf)[0]), f'control: message RAM {name} mutation missed')
        # check_tables end to end (the path the real check takes): the last
        # byte of each table file flipped, and a truncated world-model file,
        # which must be a clean FAIL, not a crash
        for rel, bad in [(rel, mutate((A / rel).read_bytes(), -1)) for rel in TABLE_FILES] + \
                [('world_models.emwm', b'EMWM')]:
            n = len(FAILS)
            with contextlib.redirect_stdout(io.StringIO()):
                check_tables(T, [cap], arrival, {rel: bad})
            caught = len(FAILS) > n
            del FAILS[n:]
            check(caught, f'control: check_tables accepted {rel} with a flipped last byte / truncated')
        check(moved_problem(sorted(moved)[:-1], level) is not None and
              moved_problem(moved, {'cells': {'moved_uids': sorted(moved)[1:]}}) is not None,
              'control: a wrong moved-hull set accepted')
        # load map: single RAM bytes, in a map cut down to the file that holds
        # them: the first and the last byte after the cell directory, and the
        # last byte of the first and of the last mapped file
        end = table + len(cells)
        for name, at in (('directory end + 0', end), ('directory end + 11', end + 11),
                         ('first file last byte', lmap[0][0] + lmap[0][3] - 1),
                         ('last file last byte', lmap[-1][0] + lmap[-1][3] - 1),
                         # close-out (F17): a file's first 16-byte row
                         ('first file first byte', lmap[0][0]), ('last file first byte', lmap[-1][0])):
            part = SimpleNamespace(map=[m for m in lmap if m[0] <= at < m[0] + m[3]], _data=image._data)
            fake = SimpleNamespace(name='load map control', ram=mutate(cap.ram, at), spad=cap.spad)
            rows = C.compare_load_map(part, fake, allow=[(table, table + len(cells))])
            check(len(part.map) == 1 and rows[0]['unexpected_rows'] == 1 and not table <= at < end,
                  f'control: load map {name} mutation missed')
        bank_img = (A / 'level/static_bank.emsc').read_bytes()
        dyn_img = (A / 'level/dynamic_objects.emsc').read_bytes()
        for name, b1, b2 in (('static bank 3/4', mutate(bank_img, 20 + (len(bank_img) - 20) * 3 // 4), dyn_img),
                             ('static bank last byte', mutate(bank_img, len(bank_img) - 1), dyn_img),
                             ('dynamic list last byte', bank_img, mutate(dyn_img, len(dyn_img) - 1)),
                             ('static bank trailing byte', bank_img + b'\0', dyn_img)):
            check(bool(level_bank_problems(b1, b2, [cap])), f'control: {name} mutation missed')
        v, p = struct.unpack_from('<2I', emcl, 8)
        sections = dict(emcl_sections(emcl))
        for at in (0x30 + 12 * v + 4, 0x289, 0xA126, sections['edge normals'] + 5, sections['rank section'] + 0x40,
                   0x1A, len(emcl) - 1):
            check(emcl_equal(mutate(emcl, at), cap.ram) is not None, f'control: EMCL +{at:#x} mutation missed')
        check(emcl_equal(emcl + b'\0\0\0\0', cap.ram) is not None, 'control: EMCL trailing bytes missed')
        # the ctx block: a captured byte near each end of +0xA0..+0xFF
        ctx = C.u32(cap.ram, L.CTX_PTR)
        for off in (0xA0, 0xA4, 0xFC, 0xFF):
            fake = SimpleNamespace(name=f'ctx +{off:#x}', ram=mutate(cap.ram, ctx + off), spad=cap.spad)
            try:
                L.check_ctx_room_block([fake], elf)
                check(False, f'control: captured ctx +{off:#x} mutation missed')
            except SystemExit:
                pass
        # the cell controls mutate the disc directory (the asset may be the
        # thing under test)
        table = C.u32(arrival.spad, L.SPAD_CELLS)
        cells = image.read(table, L.cell_directory(image.read(table, len(cells)), 0)[2])
        _c, hulls, _s = L.cell_directory(cells, 0)
        ctl = [c for c in caps if c.name != 'a01_s1_sentry_doc'][:1]
        for at in (0x19C, 0x8, hulls[10][0] + 0x40, hulls[34][0] + 0x04, hulls[37][0] + 0x20, len(cells) - 1):
            check(bool(cells_problems(L, elf, mutate(cells, at), image, ctl)[0]),
                  f'control: cells +{at:#x} mutation missed')
        # the file against the disc on its own: RAM made to agree with a
        # flipped last byte must still fail on the disc comparison
        fake = SimpleNamespace(name=arrival.name, ram=mutate(arrival.ram, table + len(cells) - 1), spad=arrival.spad)
        check(any('disc bytes' in x for x in cells_problems(L, elf, mutate(cells, len(cells) - 1), image, [fake])[0]),
              'control: a file byte that RAM agrees with but the disc does not was accepted')
        # the RAM side on its own (verify_cell_directory, no file-vs-disc check):
        # a source byte of a re-transformed hull, and single RAM bytes of a
        # capture at uid 0's hull, the directory's last byte, a hull word, the
        # count word, the freed-owner hull (a01_s1) and 001A2370's code
        s1 = next((c for c in caps if c.name == 'a01_s1_sentry_doc'), None)
        check(bool(L.verify_cell_directory(elf, mutate(cells, hulls[10][1] - 2), ctl)[1]),
              'control: hull 10 source-lane mutation missed by the 001A2370 derivation')
        ram_cases = [('uid 0 hull', arrival, table + 0x19C), ('last byte', arrival, table + len(cells) - 1),
                     ('directory word uid 5', arrival, table + 4 + 4 * 5), ('count word', arrival, table),
                     ('001A2370 code', arrival, L.RETRANSFORM + 0x40)]
        if s1 is not None:
            ram_cases.append(('freed-owner hull 34', s1, table + hulls[34][1] - 2))
        for name, base, at in ram_cases:
            # the capture's own name (the messages name it); the code case
            # stops before any derivation
            fake = SimpleNamespace(name=base.name, ram=mutate(base.ram, at), spad=base.spad)
            others = [c for c in ctl if c.name != base.name]
            check(bool(L.verify_cell_directory(elf, cells, others + [fake])[1]),
                  f'control: RAM {name} mutation missed')
        for bit in (0x01, 0x02):             # the count one above and two below
            spad = bytearray(arrival.spad)
            spad[L.SPAD_CELL_COUNT] ^= bit
            fake = SimpleNamespace(name=arrival.name, ram=arrival.ram, spad=bytes(spad))
            check(bool(L.verify_cell_directory(elf, cells, [fake])[1]), f'control: scratchpad count ^{bit} missed')
        # the room branch: a capture whose D_008106C8 has bit 0x80 and whose ctx
        # bytes hold the constant-branch output must be refused (the rebuild does
        # not read the room entry there)
        import test_effect_manager_reference as EM
        ram = bytearray(arrival.ram)
        ram[0x8106C8] |= 0x80
        ctx = C.u32(ram, L.CTX_PTR)
        oracle = EM.Oracle(elf, bytearray(ram), bytearray(arrival.spad))
        oracle.run(0x1D8FD0, (), stack=0x70003F00)
        ram[ctx + L.CTX_LO:ctx + L.CTX_HI] = oracle.ram[ctx + L.CTX_LO:ctx + L.CTX_HI]
        try:
            L.check_ctx_room_block([SimpleNamespace(name='constant branch', ram=bytes(ram), spad=arrival.spad)], elf)
            check(False, 'control: constant-branch ctx block accepted as room-table data')
        except SystemExit:
            pass
        for name, bad_banks, bad_emsr in (('registry middle', banks, mutate(emsr, len(emsr) // 2)),
                                          ('bank last byte', mutate(banks, len(banks) - 1), emsr),
                                          ('bank truncated', banks[:-16], emsr),
                                          ('registry entry', banks, mutate(emsr, 24 + 2 * 0x240 + 5)),
                                          ('bank body', mutate(banks, 0x20000), emsr),
                                          ('bank byte 0', mutate(banks, 0), emsr),
                                          ('bank header row', mutate(banks, 0x40), emsr)):
            check(bool(sfx_problems(S, caps[:1], lmap, info, bad_banks, bad_emsr)[0]),
                  f'control: {name} mutation missed')
        # the registry is re-derived for EVERY capture: a second capture whose
        # pitch ladder D_00241D70 differs must be reported on its own
        fake = copy.copy(arrival)
        fake.name, fake.ram = 'second capture', mutate(arrival.ram, LADDER + 2 * 0x40)
        check(any(x.startswith('second capture: sfx_registry.emsr differs')
                  for x in sfx_problems(S, [arrival, fake], lmap, info, banks, emsr)[0]),
              'control: a second capture\'s registry was not re-derived')
        # the container must lie inside upload section 0 of the descriptor
        s0 = info['sections'][0]
        short = dict(info, sections=[(*s0[:1], len(banks) - 1, *s0[2:])] + list(info['sections'][1:]))
        check(bool(banks_problems(banks, short, lmap)), 'control: a container longer than upload section 0 accepted')
        e = emdl_parts(zbuf)
        for name, at in (('texel', e['blob'] + 1), ('position', e['verts'] + 1), ('colour', e['verts'] + 12),
                         ('UV', e['verts'] + 40 * 7 + 24), ('bone slot', e['verts'] + 32), ('texture slot', e['verts'] + 36),
                         ('index', e['idx'] + 4), ('last vertex', e['verts'] + 40 * (e['vc'] - 1) + 1),
                         ('last vertex texture slot', e['verts'] + 40 * e['vc'] - 4),
                         ('last index', e['idx'] + 4 * (e['ic'] - 1)), ('fps', 0x14), ('flags', 0x1C), ('bone parent', 36),
                         ('texture entry offset', e['tex'] + 16 + 8), ('GS code', e['tex'] + 12),
                         ('texture width', e['tex']), ('texture height', e['tex'] + 4),
                         ('clip block', e['tex'] + 16 * e['tc'] + 12), ('palette', e['idx'] + 4 * e['ic'] + 64 + 20),
                         ('last byte', len(zbuf) - 1),
                         # close-out: the GS code's top byte, clip block word 0, the first palette matrix
                         ('GS code top byte', e['tex'] + 15), ('clip block word 0', e['tex'] + 16 * e['tc']),
                         ('first palette matrix', e['idx'] + 4 * e['ic'])):
            check(not zone_equal(el, L, mutate(zbuf, at), zb, gs, prim), f'control: zone {name} mutation missed')
        check(not zone_equal(el, L, zbuf + b'\0', zb, gs, prim), 'control: zone trailing byte missed')
        check(zone_equal(el, L, zbuf, zb, gs, prim), 'control: the unchanged zone file after the zone controls not equal')
        for i in range(e['tc']):
            w, h, off = struct.unpack_from('<3I', zbuf, e['tex'] + 16 * i)
            if zbuf[e['blob'] + off:e['blob'] + off + 4 * w * h] != \
                    zbuf[e['blob'] + (off ^ 4):e['blob'] + (off ^ 4) + 4 * w * h]:
                bad = bytearray(zbuf)
                bad[e['tex'] + 16 * i + 8] ^= 4
                check(not zone_equal(el, L, bytes(bad), zb, gs, prim),
                      f'control: zone texture {i} offset shifted by one texel missed')
                break
        else:
            check(False, 'control: no texture whose one-texel shift changes its texels')
        # the vertex floats are compared as the float32 the file stores: a
        # rebuild coordinate a quarter float32 ulp away (exact in the host
        # double) writes the same bytes and must compare equal (the memo key
        # holds each builder, so its identity is never reused).
        k = max(range(3), key=lambda j: abs(zb.pos[0][j]))
        x = zb.pos[0][k]
        ulp = abs(struct.unpack('<f', struct.pack('<I', struct.unpack('<I', struct.pack('<f', x))[0] + 1))[0] - x)
        near = copy.copy(zb)
        near.pos = list(zb.pos)
        near.pos[0] = tuple(v + ulp / 4 if j == k else v for j, v in enumerate(zb.pos[0]))
        check(near.pos[0][k] != x and zone_equal(el, L, zbuf, near, gs, prim),
              'control: a rebuild coordinate within float32 rounding of the file was not equal')
        # the SPU ADPCM decode saturates at the s16 limits (synthetic blocks:
        # filter 1, shift 0, end flag, every nibble +7 or -8)
        up = _adpcm(bytes([0x10, 0x01]) + b'\x77' * 14, None)[0]
        down = _adpcm(bytes([0x10, 0x01]) + b'\x88' * 14, None)[0]
        check(max(up) == 32767 and up.count(32767) > 1 and min(down) == -32768 and down.count(-32768) > 1,
              'control: the ADPCM decode does not saturate at 32767 / -32768')
        # the table comparators, each at its far end: the last byte of the
        # last window / record / span, plus header words and trailing bytes
        ngroup = sum(struct.unpack_from('<II', roster, 0x18 + 8 * g)[1] for g in range(groups))
        first_group = struct.unpack_from('<II', roster, 0x18)[1]
        for at in (0x18 + 8 * groups + 0x2C * ngroup - 1, 0x18 + 8 * groups + 0x2C * first_group - 1):
            check(not roster_equal(mutate(roster, at), cap.ram), f'control: roster group record +{at:#x} mutation missed')
        ram = bytearray(cap.ram)                      # an empty actor pool
        for slot in range(T.POOL_SLOTS):
            ram[T.POOL_BASE + slot * T.POOL_STRIDE] = 0
        check(bool(roster_problems(T, roster, [SimpleNamespace(name='empty pool', ram=bytes(ram), spad=cap.spad)])[0]),
              'control: an empty actor pool passed the placement-node check')
        at_rest = next(a for i, a in pairs if cap.ram[a + 0xB0:a + 0xBC] ==
                       T.spawn_fields_placement(roster[first_place + 0x28 * i:first_place + 0x28 * (i + 1)], i)[1])
        check(bool(roster_problems(T, roster, [SimpleNamespace(name='moved node', ram=mutate(cap.ram, at_rest + 0xB1),
                                                               spad=cap.spad)])[0]),
              'control: a placement node moved off its record position passed')
        for name in ('spawn_table.emsp', 'door_destinations.emsp'):
            blob = (A / name).read_bytes()
            windows = emsp_windows(blob)
            first_end = 16 + 8 * len(windows) + len(windows[0][1]) - 1
            cases = [('last byte', mutate(blob, len(blob) - 1)), ('first window last byte', mutate(blob, first_end)),
                     ('header +0x0C', mutate(blob, 0x0C)), ('trailing byte', blob + b'\0')]
            if name == 'spawn_table.emsp':
                at = 16 + 8 * len(windows)
                for a, d in windows:
                    if a <= 0x24B1A0 < a + len(d):
                        cases.append(('area 1 row', mutate(blob, at + 0x24B1A0 - a + 5)))
                    at += len(d)
            layout = spawn_layout if name == 'spawn_table.emsp' else doors_layout
            for what, bad in cases:
                check(bool(emsp_problems(name, bad, [cap], layout(disc_reader()))[0]),
                      f'control: {name} {what} mutation missed')
        twin = table_windows()
        for label, rel in (('scripts', 'area01_scripts/scripts.emsc'), ('overlay data', 'overlay_data.emsc')):
            blob = (A / rel).read_bytes()
            disc = disc_reader()(*twin[label])
            for what, bad in (('last byte', mutate(blob, len(blob) - 1)), ('first byte', mutate(blob, 20)),
                              ('middle', mutate(blob, 20 + (len(blob) - 20) // 2)), ('entry word', mutate(blob, 12)),
                              ('trailing byte', blob + b'\0')):
                check(bool(emsc_problems(label, bad, [cap], twin[label], disc)),
                      f'control: {label} {what} mutation missed')
        wm = (A / 'world_models.emwm').read_bytes()
        for what, bad in (('last span byte', mutate(wm, len(wm) - 1)), ('first span byte', mutate(wm, 32)),
                          ('trailing byte', wm + b'\0'), ('table word', mutate(wm, 8)), ('count', mutate(wm, 16)),
                          ('zero word', mutate(wm, 0x1C))):
            check(bool(world_models_problems(bad, [cap])[0]), f'control: world models {what} mutation missed')
        fake = SimpleNamespace(name='D_0028A59C', ram=mutate(cap.ram, 0x28A59C + 1), spad=cap.spad)
        check(bool(world_models_problems(wm, [fake])[0]), 'control: RAM D_0028A59C mutation missed')
        gbank = C.u32(mm, 28)
        banks_at = srow + 16 * srows
        for name, at in (('last global record', 140 + 8 * gcount - 1), ('first global record', 140),
                         ('global bank middle', banks_at + gbank // 2), ('global bank last byte', banks_at + gbank - 1),
                         ('area bank first byte', banks_at + gbank)):
            check(bool(messages_problems(mutate(mm, at), [cap], elf)[0]), f'control: message {name} mutation missed')
        want = level_gs_state(el, prim)
        for field in range(5):
            other = tuple(v ^ 1 if k == field else v for k, v in enumerate(want))
            check(bool(gs_state_problems({other: 1}, want)) and bool(gs_state_problems({want: 5, other: 1}, want)),
                  f'control: level GS state field {field} mutation missed')
        # the level bank pointers on the RAM side, and the file's own base /
        # entry words
        for name, at in (('D_0028A5A0', 0x28A5A0), ('D_0028A5A4', 0x28A5A4)):
            fake = SimpleNamespace(name=name, ram=mutate(cap.ram, at + 1), spad=cap.spad)
            check(bool(level_bank_problems(bank_img, dyn_img, [fake])), f'control: RAM {name} mutation missed')
        for name, b1, b2 in (('static bank base word', mutate(bank_img, 9), dyn_img),
                             ('static bank entry word', mutate(bank_img, 13), dyn_img),
                             ('dynamic list base word', bank_img, mutate(dyn_img, 9))):
            check(bool(level_bank_problems(b1, b2, [cap])), f'control: {name} mutation missed')
        # the cell directory file one disc-equal byte too long: the file-vs-
        # disc compare runs over the file's own length, so only the directory
        # size catches it
        extra = image.read(table + len(cells), 1)
        check(bool(cells_problems(L, elf, cells + extra, image, ctl)[0]),
              'control: area01_cells.bin plus the next disc byte accepted')
        # two captures, one with a corrupted live owner matrix (hull 31's
        # owner node 0x7B1530, behaviour 0x8261A0, matrix node + 0xD0): the
        # owner is live, so its hull must be derived in THAT capture; a
        # derivation of the same hull in another capture must not excuse it
        owner = 0x7B1530
        ram = bytearray(arrival.ram)
        ram[owner + 0xD0 + 3] ^= 0x40
        fake = SimpleNamespace(name='owner matrix control', ram=bytes(ram), spad=arrival.spad)
        got = L.verify_cell_directory(elf, cells, [arrival, fake])[1]
        check(L.owner_matrix(arrival.ram, owner) == owner + 0xD0 and
              any(x.startswith('owner matrix control: hull 31 differs and no owner derivation') for x in got),
              f'control: a live owner whose matrix does not derive its hull was excused ({got})')
        extent_controls(K, V, cap)
        sweep2_controls(K, V, cap)
        sweep3_controls(K, V, cap)
        print('  controls: flipped bytes in the roster (header, sub byte, first and last record, group table, '
              'the last byte of each group, a group record and a placement dropped, RAM-only and disc-only '
              'differences, trailing byte), placement node fields (first and last byte of each, the behaviour '
              'word high half, the class byte 0x8B / 0x0B), an empty pool, a moved node, every EMSP file (last byte, '
              'first window end, area 1 row, header, trailing byte, window sets: a window dropped or short), '
              'scripts / overlay data (bytes, header words, window extents, disc-only and RAM-only differences), '
              'world models, the message file (header, every block\'s ends, records, stream rows, banks, each '
              'count and bank size one short, D_00264DD0[0], the -1 row search at row 0, a 0x0001FFFF row that is '
              'not -1), D_0028A5A0 / D_0028A5A4, the static bank and dynamic list extents, D_0028A598 and the grid '
              'block, the grid form, count and index pad, the cell file plus one disc byte, a corrupted live owner '
              'matrix beside a clean capture, a second capture\'s registry, the binding count, handle bounds, '
              'tick step, pitch branch and product, upload section 0, D_0028A59C, the level GS state, float32 '
              'rounding, ADPCM saturation, the moved-hull set, the load map and its file seams, EMCL (7 places, '
              'trailing bytes), ctx +0xA0 / +0xA4 / +0xFC / +0xFF, the cell file (6 places), RAM directory bytes, '
              '001A2370 code, scratchpad count, the room branch, registry, bank body/header/last byte/length, and '
              '25 zone EMDL fields are detected; round 5: the world-model span (grown, shrunk, no models, each '
              'extent term), the four pointers\' high bytes, second-capture-only roster and dynamic-list '
              'differences, the dynamic count 0x10001, a negative object offset, record word 0x010B, at-rest '
              'rotation and position bytes, grid allowances at every section end, node runs past their '
              'sections, +0.0 / -0.0 vertex twins, index -1, the moved-set order, 00119EA0 +0x20, B0 / end '
              'events, ADPCM end block and unsettled loop, upload section 0 exact, a GS-less capture, a '
              '001A2370 overrun, a last-byte-only derivation, a shared owner matrix and the texture key fields; '
              'close-out: a zone GS code\'s top byte, clip word 0 and first palette matrix, a file\'s first load-map '
              'row, a load map out of order or overlapping, a bank too long in the second capture only, a unit word '
              '0x10001, offset low bits (accept), grid form 0x10C, the last byte of each 001A2370 code range, a '
              'room-table byte, an owner in the last pool slot with byte 0 = 2 (accept), remap 0x80, two loop '
              'starts, handle 0x80, group word 0x8000 | x, a B0 41 event and an unbound (1, 1) record')

    dt = time.time() - t0
    if FAILS:
        print(f'area01 assets reference: FAILED ({len(FAILS)}) in {dt:.1f} s')
        return 1
    print(f'area01 assets reference: OK ({MODE}, {dt:.1f} s)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
