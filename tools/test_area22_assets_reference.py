#!/usr/bin/env python3
"""Check the exported AREA22 assets (assets/area22/, docs/AREA22_ASSETS.md)
against the recorded original captures, and load each one through the port's
own loader.

Inputs (all local, all the user's): the assets written by
tools/export_area22_level.py, export_area22_tables.py and export_area22_sfx.py,
the extracted disc files, the pinned boot ELF and AREA22 overlay, and the
AREA22 captures (the arrival ../Extermination/build/s87/route_a04/
a04_05_progression_exit/ and build/s87/route_a22/a22_00, a22_01,
a22_s0..a22_s2; a22_02 and a22_s3 end in other areas). Missing inputs print
SKIPPED and exit 0.

The comparators are the AREA01 checker's (tools/test_area01_assets_reference.py,
imported unchanged, with export_area01_common pointed at AREA22 and its
module constants GRID / BUILD / OVERLAY_SHA256 set for AREA22):
zone_problems, emsc_header_problems, gs_captures, gs_state_problems,
bank_end, emcl_equal, grid_problems, hulls_inside_table, roster_image /
roster_equal / roster_nodes, emsp_problems with spawn_layout / doors_layout,
message_record_count / message_bank_size / MSG_BLOCKS, world_models_problems,
registry_from_ram, load_map_problems, build_loaders. The directory
derivation is export_area01_level.verify_cell_directory with AREA22's
owner_matrix (export_area22_level, the drum included). AREA22's own checks
are written here: the flat load map and the relocation words, the stale
D_0028A594 / D_0028A5A4, the level without a dynamic list, the message file
without an area table, the overlay data without script chains, the spawn
word of the last room load, and the ctx block by 001D8FD0 + 001D1C50.

Checks (every comparison is exact):
  load map  in address order, no overlap, the same map from every capture's
            descriptor (INDEX.IDX sector 26 SHA-256 pinned); every row
            labelled chunk26/idXX lies inside [D_0028A490[XX], the next
            relocated address) and the rows tile D_0028A73C .. D_0028A740
            (label_problems); every mapped disc byte equals RAM except the cell
            directory's own bytes (checked below); every relocation word
            D_0028A490[id] = D_0028A73C + offset; ids 0x41 / 0x45 not
            relocated, D_0028A594 / D_0028A5A4 pinned to the AREA04 values
  level     sub0/level/static_bank.emsc equals RAM at D_0028A5A0 over its
            extent; no dynamic-list file and D_0028A5A4 outside the map;
            every textured level kick REFs a bank object with the class-0 GS
            state; each zone EMDL equals a rebuild from the bank, byte for
            byte, and its texels the GS freeze decode of every capture
  emcl      sub0/area22.emcl equals the RAM grid rebuild in every capture;
            D_0028A598 pinned, the grid block inside the load map and
            outside the directory allowance
  cells     sub0/area22_cells.bin equals the disc bytes and ends where the
            directory ends; every byte of every capture's directory: the
            disc, the ORIGINAL 001A2370 run for the live owner's matrix, or
            an orphan equal to another capture's derivation; the moved set
            per capture equals cells.json; the 001A2370 call sites of the
            ELF and the module equal RETRANSFORM_SITES and no capture has a
            live 0x219870 node
  tables    sub0/roster.emro = the rebuild by the original's walks over the
            pinned ELF + overlay and over every capture's RAM, (live, at
            rest) per capture pinned, every live placement node keeps its
            copied fields; both EMSP files; D_008106C8 = +0x1C of the load
            entry; no func_001BA1A0 call in the AREA22 C; overlay data
            equal to the module and to RAM in every capture;
            sub0/message_data.emmd (every byte, no area table); world models
            and every model owner's +0x44, the bound count pinned
  sfx       sub0/sfx/area22_banks.bin = upload section 0's container on the
            disc; the capture bindings pinned; sub0/sfx/sfx_registry.emsr =
            the RAM re-derivation of every capture
  ctx       ORIGINAL 001D8FD0 + 001D1C50 rebuild ctx +0xA0..+0xFF
  loaders   each file through its port loader (compiled privately from src/)
  canary    run_checks again over [a capture, a planted copy of it] and a
            copy of the export tree with file plants; each section must
            report its plant for the copy and stay silent for the original;
            every port loader must refuse a broken copy of its file
  controls  a changed input per comparator, file side and RAM side, and the
            accept cases listed in docs/AREA22_ASSETS.md
Default: 3 captures (the arrival, a22_s0, a22_s1); EM_TEST_FULL=1: all 6.
"""
from __future__ import annotations

import contextlib
import copy
import ctypes as CT
import io
import json
import os
import re
import struct
import sys
import time
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area22_common as A22  # noqa: E402
import export_area22_level as E1  # noqa: E402
import export_area22_tables as E2  # noqa: E402
import export_area22_sfx as E3  # noqa: E402

C = A22.configure()
E1.install()
import test_area01_assets_reference as T01  # noqa: E402
from reference_mode import FULL, MODE  # noqa: E402

L, T, S = E1.L, E2.T, E3.S
A = Path(os.environ.get('EM_AREA22_ASSETS') or A22.OUT).resolve()
BUILD = C.ROOT / 'build/area22/assets/test'
CANARY = BUILD / f'canary-{os.getpid()}'
QUICK = ('a04_05_progression_exit', 'a22_s0_pickup', 'a22_s1_ladder_reader')
T01.BUILD = BUILD
T01.OVERLAY_SHA256 = A22.OVERLAY_SHA256
# D_0028A598 = D_0028A73C + the id-0x42 relocation offset 0x1F7000, measured
# in every capture (the relocation check ties it to the descriptor)
GRID = 0x152E500
T01.GRID = GRID
FAILS = T01.FAILS
check = T01.check

# (live, at the record's position and rotation) placement nodes per capture
# (T01.roster_nodes); measured, asserted exactly
ROSTER_LIVE = {'a04_05_progression_exit': (21, 21), 'a22_00_door7': (21, 21), 'a22_01_corridor': (21, 21),
               'a22_s0_pickup': (21, 21), 'a22_s1_ladder_reader': (21, 21), 'a22_s2_door10_locked': (21, 21)}
# model owners bound to a bank model per capture; asserted exactly
MODEL_OWNERS = {'a04_05_progression_exit': 19, 'a22_00_door7': 19, 'a22_01_corridor': 19, 'a22_s0_pickup': 19,
                'a22_s1_ladder_reader': 19, 'a22_s2_door10_locked': 19}
# the capture bindings, (group, slot) -> (container, row); 'area' =
# area22_banks.bin; and the refused slots
BINDING = {(1, 0): ('global', 0), (1, 1): ('global', 1), (1, 2): ('global', 2),
           (2, 0): ('area', 0), (4, 0): ('area', 1), (4, 1): ('area', 2)}
REFUSED = {(3, 0)}
SPAWN_ROWS = E2.SPAWN_ROWS
DOOR_ROW = E2.DOOR_ROW
LOAD_ENTRY = E2.LOAD_ENTRY
# the previous area's words the AREA22 load does not relocate (ids 0x41, 0x45)
STALE = {E1.D_0028A594: 0x152E740, E1.D_0028A5A4: 0x197D480}
STALE_SOURCE = A22.ROUTE_A04 / 'a04_04_conveyor'     # an AREA04 capture holding the same words
# SHA-256 of INDEX.IDX sector 26 as the user's disc image gives it (a hash,
# not disc data): the relocation list, the sections and the resident offset
# every check reads are as fixed as the pinned ELF
DESCRIPTOR_SHA256 = '6578e36b7df8695f9823aef19fb9e4f65de8caeef6dac72b8f27062ce84aa146'
LABEL = re.compile(r'chunk26/id([0-9a-f]{2})')
CANARY_TEST_1 = 0x8153A0      # where the level kicks take TEST_1 (0x5000D), as in the earlier areas
DRUMS = 3


def key(cap):
    """The capture name the pinned tables use (a planted copy keeps its source's)."""
    return getattr(cap, 'key', cap.name)


def sub_path(rel):
    return A / 'sub0' / rel


# ---------------------------------------------------------------------------
# Load map


def stale_problems(caps, top=None):
    """D_0028A594 and D_0028A5A4 keep the values STALE pins (the descriptor
    `top`, default the disc sector, relocates neither id 0x41 nor 0x45)."""
    out = []
    rel = E1.relocations(top)
    for ident in (0x41, 0x45):
        if ident in rel:
            out.append(f'the descriptor relocates id {ident:#x}')
    for cap in caps:
        for at, want in STALE.items():
            if C.u32(cap.ram, at) != want:
                out.append(f'{cap.name}: D_{at:08X} = {C.u32(cap.ram, at):#x}, not the unrelocated {want:#x}')
    return out


def label_problems(lmap, cap, top):
    """Every load-map row is labelled chunk26/idXX by the original's own
    reading of the relocation list: the rows tile D_0028A73C .. D_0028A740
    without a gap, and a row labelled idXX lies inside [D_0028A490[XX], the
    next relocated address or D_0028A740), all read from the capture's RAM.
    (The extracted files' names read the list as block offsets and are
    shifted by the resident offset; a map labelled by them fails here.)"""
    out, ram = [], cap.ram
    rows = sorted(lmap)
    lo, hi = C.u32(ram, C.D_0028A73C), C.u32(ram, C.D_0028A740)
    if not rows or rows[0][0] != lo or rows[-1][0] + rows[-1][3] != hi:
        out.append(f'{cap.name}: the load map does not span D_0028A73C .. D_0028A740')
    for x, y in zip(rows, rows[1:]):
        if x[0] + x[3] != y[0]:
            out.append(f'{cap.name}: a gap in the load map at {x[0] + x[3]:#x}')
    starts = sorted(C.u32(ram, E1.D_0028A490 + 4 * i) for i in E1.relocations(top))
    for a, _p, _o, n, label in rows:
        m = LABEL.fullmatch(label)
        if m is None:
            out.append(f'{cap.name}: load map label {label} is not a relocation id')
            continue
        ident = int(m.group(1), 16)
        start = C.u32(ram, E1.D_0028A490 + 4 * ident)
        end = min([x for x in starts if x > start], default=hi)
        if not start <= a < a + n <= end:
            out.append(f'{cap.name}: load map row {a:#x}+{n:#x} labelled {label} lies outside '
                       f'D_0028A490[{ident:#x}] {start:#x}..{end:#x}')
    return out


def load_problems(K, caps, cells_len):
    out = list(T01.load_map_problems(K.lmap))
    image = K.image
    table = C.u32(K.first.spad, L.SPAD_CELLS)
    for cap in caps:
        try:
            A22.configure()
            lmap, _info = C.build_load_map(cap)
        except SystemExit as error:
            out.append(f'{cap.name}: load map: {error}')
            continue
        if lmap != K.lmap:
            out.append(f'{cap.name}: a different load map')
        if C.u32(cap.spad, L.SPAD_CELLS) != table:
            out.append(f'{cap.name}: cell directory pointer')
        rows = C.compare_load_map(image, cap, allow=[(table, table + cells_len)])
        if any(r['unexpected_rows'] for r in rows):
            out.append(f'{cap.name}: load map differs from RAM')
        out += E1.relocation_problems([cap], K.top)[0]
        out += label_problems(K.lmap, cap, K.top)
    out += stale_problems(caps, K.top)
    return out


# ---------------------------------------------------------------------------
# Level


_ZONES = {}


def bank_problems(bank_img, caps):
    """The static bank's EMSC header, and against every capture: D_0028A5A0,
    every byte over its length, and its length = bank_end over RAM."""
    out = T01.emsc_header_problems('static bank', bank_img)
    base, _entry, length = struct.unpack_from('<3I', bank_img, 8)
    if len(bank_img) != 20 + length:
        out.append('static bank EMSC length')
    bank = bank_img[20:20 + length]
    for cap in caps:
        if C.u32(cap.ram, E1.D_0028A5A0) != base:
            out.append(f'{cap.name}: D_0028A5A0')
        if cap.ram[base:base + length] != bank:
            out.append(f'{cap.name}: static bank differs from RAM')
        try:
            end = T01.bank_end(cap.ram, base)
        except struct.error:
            end = None
        if end != base + length:
            out.append(f'{cap.name}: static bank length {length:#x}, the objects end at {end}')
    return out


def level_problems(K, caps):
    """The static bank, the absence of a dynamic list, the kicks and the zone
    EMDLs. Returns (problems, summary)."""
    out, el, image = [], K.el, K.image
    lvl = sub_path('level')
    bank_img = (lvl / 'static_bank.emsc').read_bytes()
    out += bank_problems(bank_img, caps)
    base, _entry, length = struct.unpack_from('<3I', bank_img, 8)
    bank = bank_img[20:20 + length]
    if (lvl / 'dynamic_objects.emsc').exists():
        out.append('sub0: a dynamic-list file (no capture draws one)')
    for cap in caps:
        if image.locate(C.u32(cap.ram, E1.D_0028A5A4)) is not None:
            out.append(f'{cap.name}: D_0028A5A4 lies in the load map')
    read = lambda a, n: bank[a - base:a - base + n]
    try:
        objects = L.bank_objects(read, base)
        kicks, states, _touched = L.check_kicks(caps, objects, [])
    except (SystemExit, struct.error, ValueError) as error:
        return out + [f'level: {error}'], {}
    prim = el.level_template_prim(el.BootElf(C.ELF_PATH))
    out += T01.gs_state_problems(states, T01.level_gs_state(el, prim))

    class BankImage:
        def read(self, a, n):
            return read(a, n)

    zkey = bank_img
    if zkey not in _ZONES:
        _ZONES[zkey] = L.build_zones(el, BankImage(), objects, lambda a: image.locate(a)[4])
    zones = _ZONES[zkey]
    try:
        gs_caps = T01.gs_captures(caps)
    except SystemExit as error:
        return out + [str(error)], {}
    files = sorted(str(x) for x in lvl.glob('*.emdl'))
    if len(files) != len(zones):
        out.append(f'sub0: {len(files)} zone files for {len(zones)} source files')
    order = [l for _a, _p, _o, _s, l in image.map]
    for path, (label, (b, _ids, bad, _recs)) in zip(files, sorted(zones.items(), key=lambda z: order.index(z[0]))):
        if bad:
            out.append(f'{label}: records with a matrix slot')
        name = Path(path).name
        if name.split('_', 1)[1] != Path(label).name.split('.')[0] + '.emdl':
            out.append(f'sub0/{name}: zone name')
        out += T01.zone_problems(el, f'sub0/{name}', Path(path).read_bytes(), b, gs_caps, prim)
    return out, dict(objects=len(objects), zones=len(files), kicks=sum(k['level_kicks'] for k in kicks),
                     dynamic_kicks=sum(k['dynamic_kicks'] for k in kicks))


# ---------------------------------------------------------------------------
# Collision and cells


def emcl_problems(K, emcl, caps, cells_len):
    out = []
    T01.GRID = GRID
    for cap in caps:
        table = C.u32(cap.spad, L.SPAD_CELLS)
        for problem in T01.grid_problems(cap.ram, K.image, (table, table + cells_len)):
            out.append(f'{cap.name}: {problem}')
        where = T01.emcl_equal(emcl, cap.ram)
        if where is not None:
            out.append(f'{cap.name}: sub0/area22.emcl differs from the RAM grid rebuild: {where}')
    return out


def cells_problems(K, cells, caps, record):
    """The directory file against the disc, and every capture's RAM
    directory through export_area01_level.verify_cell_directory (the
    ORIGINAL 001A2370 for the hulls, AREA22's owner_matrix). `record` =
    sub0/cells.json."""
    out = []
    E1.install()
    table = C.u32(K.first.spad, L.SPAD_CELLS)
    try:
        disc = K.image.read(table, len(cells))
    except ValueError as error:
        return [f'cells: {error}']
    if cells != disc:
        k = next(j for j in range(len(cells)) if cells[j] != disc[j])
        out.append(f'sub0/area22_cells.bin differs from the disc bytes at +{k:#x}')
    try:
        _count, _hulls, size = L.cell_directory(cells, 0)
        if size != len(cells):
            out.append(f'sub0/area22_cells.bin is {len(cells)} bytes, the directory {size}')
        inside = T01.hulls_inside_table(L, cells)
        if inside:
            out.append(f'cells: hulls {inside} start inside the uid table')
        rows, problems = L.verify_cell_directory(K.elf, cells, caps)
    except (AssertionError, IndexError, struct.error, ValueError) as error:
        return out + [f'cells: the directory does not parse ({error!r})']
    out += problems
    union = sorted({u for x in record['cells']['captures'] for u in x['moved_hulls']})
    if record['cells']['moved_uids'] != union:
        out.append(f'sub0/cells.json moved_uids {record["cells"]["moved_uids"]} is not the union of its rows {union}')
    for r in rows:
        cap = next(c for c in caps if c.name == r['capture'])
        want = next((x['moved_hulls'] for x in record['cells']['captures'] if x['capture'] == key(cap)), None)
        if want is None:
            out.append(f'{r["capture"]}: not recorded in cells.json')
        elif [int(u) for u in r['moved_hulls']] != [int(u) for u in want]:
            out.append(f'{r["capture"]}: re-transformed hulls {r["moved_hulls"]} != cells.json {want}')
    return out


# every call of 001A2370 in the pinned boot ELF and the AREA22 overlay (jal;
# no j, no address-as-data word): the drum 0x156620 (0x156EF0), the pickup
# 0x219550 (0x219668), 0x219F50 (0x21A104; its matrix is the scratchpad copy
# 0x700036A0; its only caller is 0x219870). The AREA22 overlay holds none.
RETRANSFORM_SITES = (0x156EF0, 0x219668, 0x21A104)
SCRATCH_MATRIX_CALLERS = (0x219870,)


def word_sites(blob, base, word):
    """The 4-aligned addresses in `blob` (loaded at `base`) holding `word`."""
    pat, out, k = struct.pack('<I', word), [], blob.find(struct.pack('<I', word))
    while k >= 0:
        if k % 4 == 0:
            out.append(base + k)
        k = blob.find(pat, k + 1)
    return out


def retransform_census_problems(elf, ov, caps):
    """The 001A2370 call sites equal RETRANSFORM_SITES (so owner_matrix
    names every caller), and no capture has a live node of a behaviour
    whose matrix is a scratchpad copy (SCRATCH_MATRIX_CALLERS)."""
    text = elf[C.ELF_OFFSET:C.ELF_OFFSET + 0x175B00]
    load = C.u32(ov, 8)
    sites = []
    for word in ((3 << 26) | (L.RETRANSFORM >> 2), (2 << 26) | (L.RETRANSFORM >> 2), L.RETRANSFORM):
        sites += word_sites(text, C.ELF_VADDR, word) + word_sites(ov, load, word)
    out = []
    if tuple(sorted(sites)) != RETRANSFORM_SITES:
        out.append(f'001A2370 call sites {[hex(a) for a in sorted(sites)]} != the census')
    for cap in caps:
        for _s, a in T.pool_nodes(cap.ram):
            if C.u32(cap.ram, a + 0x10) in SCRATCH_MATRIX_CALLERS:
                out.append(f'{cap.name}: live node {a:#x} of {C.u32(cap.ram, a + 0x10):#x}, whose 001A2370 '
                           f'matrix is a scratchpad copy owner_matrix does not model')
    return out


def script_problems(calls, record_chains):
    """No func_001BA1A0 call in the AREA22 overlay C, and none in tables.json."""
    out = []
    if calls:
        out.append(f'the AREA22 overlay C starts script chains: {calls}')
    if record_chains:
        out.append('tables.json scripts chains are not empty')
    return out


# ---------------------------------------------------------------------------
# Tables


def roster_problems(roster, caps):
    out = []
    try:
        disc = T01.roster_image(T01.disc_reader())
        if roster != disc:
            out.append('sub0/roster.emro differs from the rebuild from the disc')
        for cap in caps:
            if not T01.roster_equal(disc, cap.ram):
                out.append(f'{cap.name}: roster records differ from RAM')
            ok, live, _pairs = T01.roster_nodes(T, disc, cap.ram)
            if not ok:
                out.append(f'{cap.name}: a live placement node lost a copied field')
            if live != ROSTER_LIVE.get(key(cap)):
                out.append(f'{cap.name}: placement nodes live / at the record position {live} != '
                           f'{ROSTER_LIVE.get(key(cap))}')
    except (ValueError, IndexError, struct.error) as error:
        out.append(f'roster: {error!r}')
    return out


def window_problems(label, blob, window, disc, caps, record):
    """The overlay-data EMSC window: header, extent = `window`, bytes = the
    module, and every word against every capture's RAM (AREA22 has no chain
    record, so no word may differ); tables.json must record none."""
    out = T01.emsc_header_problems(label, blob)
    base, d = T01.emsc_window(blob)
    if (base, len(d)) != window:
        return out + [f'{label}: window {base:#x}+{len(d):#x}, not {window[0]:#x}+{window[1]:#x}']
    if d != disc:
        out.append(f'{label} differs from the disc overlay')
    for cap in caps:
        words = E2.run_time_words(disc, window[0], cap.ram)
        if words:
            out.append(f'{cap.name}: {label}: {len(words)} words differ from the module ({words[0]:#x}); no chain '
                       f'record holds a run-time word')
    if record:
        out.append(f'tables.json {label} run-time words {record}')
    return out


def messages_problems(mm, caps, elf):
    """Every byte of message_data.emmd against RAM: the header (magic,
    version 1, area 0x16, no area records, the cursor token, no area bank,
    the global count and bank size its own length must add up to), the three
    ELF blocks, the global records at D_00264DD0[0], D_00264DD0[0x17] = 0 in
    the ELF and in RAM, the stream rows (the RAM row after the last one must
    be the -1 row), and the global bank at *D_0028A4E8. The global count must
    equal message_record_count and the bank size message_bank_size."""
    out = []
    magic, version, area, gcount, acount, rows, cursor, gbank, abank = struct.unpack_from('<4s8I', mm, 0)
    if (magic, version, area, cursor) != (b'EMMD', 1, C.AREA, T01.MSG_CURSOR):
        out.append(f'message header {(magic, version, area, cursor)}')
    if (acount, abank) != (0, 0):
        out.append(f'message area records / bank {acount} / {abank}, not 0 / 0 (D_00264DD0[0x17] is 0)')
    at = 140
    grec = mm[at:at + 8 * gcount]
    srow = at + 8 * (gcount + acount)
    streams = mm[srow:srow + 16 * rows]
    banks = srow + 16 * rows
    if len(mm) != banks + gbank + abank:
        return out + [f'message_data.emmd length {len(mm)} != {banks + gbank + abank}']
    gptr = C.u32(elf, T01.MSG_TABLES - C.ELF_VADDR + C.ELF_OFFSET)
    aptr = C.u32(elf, T01.MSG_TABLES + 4 * (C.AREA + 1) - C.ELF_VADDR + C.ELF_OFFSET)
    if aptr != 0:
        out.append(f'ELF D_00264DD0[0x17] = {aptr:#x}')
    for cap in caps:
        ram = cap.ram
        if (C.u32(ram, T01.MSG_TABLES), C.u32(ram, T01.MSG_TABLES + 4 * (C.AREA + 1))) != (gptr, 0):
            out.append(f'{cap.name}: D_00264DD0 pointers')
        for fo, a, n in T01.MSG_BLOCKS:
            if mm[fo:fo + n] != ram[a:a + n]:
                out.append(f'{cap.name}: message block {a:#x} differs')
        if ram[gptr:gptr + len(grec)] != grec:
            out.append(f'{cap.name}: message records differ')
        ends = [k for k in range(rows + 1) if C.s32(ram, T01.MSG_STREAMS + 16 * k) == -1]
        if ram[T01.MSG_STREAMS:T01.MSG_STREAMS + 16 * rows] != streams or ends != [rows]:
            out.append(f'{cap.name}: message stream rows differ (-1 rows at {ends})')
        g0 = C.u32(ram, 0x28A4E8)
        want = (T01.message_record_count(T01.disc_reader(), gptr), T01.message_bank_size(ram, g0))
        if (gcount, gbank) != want:
            out.append(f'{cap.name}: message count / bank size {(gcount, gbank)} != RAM {want}')
        if ram[g0:g0 + gbank] != mm[banks:banks + gbank]:
            out.append(f'{cap.name}: message banks differ')
    return out


def model_binding_problems(wm, caps):
    """Every model owner (not a NO_MODEL_BEHAVIOURS node) whose +0x44 names a
    model of the bank: +0x44 = 001C6120(table, +0x0D), the bone count +0x0C
    = the model's, every bone slot set; the bound count pinned."""
    out = []
    table = C.u32(wm, 8)
    for cap in caps:
        ram = cap.ram
        try:
            count = C.u32(ram, table)
            models = {table + (C.s32(ram, table + 4 + 4 * i) >> 2 << 2): i for i in range(count)}
            bound = 0
            for slot, a in E2.E02.model_owner_nodes(ram):
                m = C.u32(ram, a + 0x44)
                if m not in models:
                    continue
                bound += 1
                if models[m] != (ram[a + 0x0D] & 0x7FFF) and \
                        table + (C.s32(ram, table + 4 + 4 * (ram[a + 0x0D] & 0x7FFF)) >> 2 << 2) != m:
                    out.append(f'{cap.name} slot {slot}: +0x44 != 001C6120(table, +0x0D)')
                if ram[a + 0x0C] != C.u32(ram, m + 8) or not all(C.u32(ram, a + 0x110 + 4 * k)
                                                                 for k in range(ram[a + 0x0C])):
                    out.append(f'{cap.name} slot {slot}: bone count / slots')
            if bound != MODEL_OWNERS.get(key(cap)):
                out.append(f'{cap.name}: {bound} model owners bound, not {MODEL_OWNERS.get(key(cap))}')
        except struct.error as error:
            out.append(f'{cap.name}: model bindings {error!r}')
    return out


def spawn_entry_problems(caps):
    """D_008106C8 = word +0x1C of the spawn record of the capture's last room
    load (LOAD_ENTRY), the records read from the pinned ELF; the entry byte
    D_00810702 inside the rows; sub 0."""
    out = []
    dread = T01.disc_reader()
    _table, entries, count = E2.spawn_rows(dread)
    for cap in caps:
        sub, room, load = cap.ram[0x810701], cap.ram[0x810702], LOAD_ENTRY.get(key(cap))
        if sub != 0:
            out.append(f'{cap.name}: sub {sub}, not 0')
            continue
        if room >= count:
            out.append(f'{cap.name}: spawn entry {room} outside the {count} records')
            continue
        if load is None:
            out.append(f'{cap.name}: no recorded load entry')
            continue
        want = C.u32(dread(entries + 0x30 * load + 0x1C, 4), 0)
        got = C.u32(cap.ram, T.D_008106C8)
        if got != want:
            out.append(f'{cap.name}: D_008106C8 {got:#x} != +0x1C of spawn record {load} ({want:#x})')
    return out


def tables_problems(K, caps, files=None):
    files = files or {}
    read = lambda rel: files[rel] if rel in files else (A / rel).read_bytes()
    out = []
    try:
        out += roster_problems(read('sub0/roster.emro'), caps)
        for name, layout in (('spawn_table.emsp', T01.spawn_layout), ('door_destinations.emsp', T01.doors_layout)):
            out += T01.emsp_problems(name, read(name), caps, layout(T01.disc_reader()))[0]
        out += spawn_entry_problems(caps)
        rec = json.loads(read('tables.json'))
        out += script_problems(E2.c_script_calls(), rec['scripts']['chains'])
        ov = A22.read_overlay()
        olo, ohi = E2.E02.overlay_data_window(ov)
        odisc = ov[olo - C.OVERLAY_ARENA:ohi - C.OVERLAY_ARENA]
        out += window_problems('overlay data', read('overlay_data.emsc'), (olo, ohi - olo), odisc, caps,
                               rec['overlay_data']['run_time_words'])
        out += messages_problems(read('sub0/message_data.emmd'), caps, K.elf)
        wm = read('sub0/world_models.emwm')
        out += T01.world_models_problems(wm, caps)[0]
        out += model_binding_problems(wm, caps)
    except (SystemExit, ValueError, KeyError, IndexError, struct.error) as error:
        out.append(f'tables: {error!r}')
    return out


# ---------------------------------------------------------------------------
# Sound


def binding_problem(got, refused):
    """True unless the bindings are exactly BINDING and the refused set REFUSED."""
    return got != BINDING or set(refused) != REFUSED


def sfx_problems(K, caps, banks, registry):
    out = []
    try:
        gname = S.X.GLOBAL_CONTAINER
        gdata = (C.DECOMP / gname).read_bytes()
        A22.configure()
        _label, disc, _parsed, _names, first = E3.area_container(K.first)
        if banks != disc:
            k = next((j for j in range(min(len(banks), len(disc))) if banks[j] != disc[j]), min(len(banks), len(disc)))
            out.append(f'sub0/sfx/area22_banks.bin differs from the disc container at +{k:#x} '
                       f'({len(banks)} / {len(disc)})')
        total = C.u32(banks, 0) if len(banks) >= 4 else -1
        if len(banks) != total or total > first[1]:
            out.append(f'sub0/sfx/area22_banks.bin: {len(banks)} bytes, container total {total:#x}')
        parsed = S.X.A.parse_container(banks)
        if parsed is None:
            return out + ['sub0/sfx/area22_banks.bin is not an SShd container']
        name = E3.CONTAINER
        containers = {gname: (gdata, S.X.A.parse_container(gdata)), name: (banks, parsed)}
        bound, refused, _rep = S.bindings_from_captures(caps, containers)
        got = {k: ('global' if v[0][0] == gname else 'area', v[0][1]) for k, v in bound.items()}
        if binding_problem(got, refused):
            return out + [f'bank bindings {got}, refused {sorted(refused)}']
        for cap in caps:
            want, _counts, _n, _e = T01.registry_from_ram(cap.ram, bound, {gname: gdata, name: disc})
            if registry != want:
                k = next((j for j in range(min(len(registry), len(want))) if registry[j] != want[j]),
                         min(len(registry), len(want)))
                out.append(f'{cap.name}: sub0/sfx/sfx_registry.emsr differs from the RAM re-derivation at +{k:#x}')
    except (SystemExit, ValueError, TypeError, KeyError, IndexError, StopIteration, struct.error) as error:
        out.append(f'sfx: {error!r}')
    return out


# ---------------------------------------------------------------------------


def ctx_problems(K, caps):
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            return [], E1.check_ctx_block(caps, K.elf)
    except SystemExit as error:
        return [f'ctx block: {error}'], []


def run_checks(K, caps):
    """Every real check over the export tree A; returns (problems, summary)."""
    P, V = [], {}
    cells = sub_path('area22_cells.bin').read_bytes()
    record = json.loads(sub_path('cells.json').read_text())
    P += load_problems(K, caps, len(cells))
    p, V['level'] = level_problems(K, caps)
    P += p
    P += emcl_problems(K, sub_path('area22.emcl').read_bytes(), caps, len(cells))
    P += cells_problems(K, cells, caps, record)
    P += sfx_problems(K, caps, sub_path('sfx/area22_banks.bin').read_bytes(),
                      sub_path('sfx/sfx_registry.emsr').read_bytes())
    P += tables_problems(K, caps)
    P += retransform_census_problems(K.elf, A22.read_overlay(), caps)
    p, V['room'] = ctx_problems(K, caps)
    P += p
    return P, V


# ---------------------------------------------------------------------------
# Port loaders


def doors_loader_ok(lib, path):
    """em_spawn_table_load accepts the door file and serves both windows
    the door code reads: the pointer array 0x24E140 (0x17 words) and the
    row DOOR_ROW (0x10 bytes)."""
    lib.em_spawn_table_read.restype = CT.c_void_p
    lib.em_spawn_table_read.argtypes = [CT.c_void_p, CT.c_uint32, CT.c_uint32]
    lib.em_spawn_table_load.restype = CT.c_int
    doors = CT.create_string_buffer(16 << 20)
    ok = lib.em_spawn_table_load(doors, str(path).encode()) == 0
    return ok and bool(lib.em_spawn_table_read(doors, 0x24E140, 4 * 0x17)) and \
        bool(lib.em_spawn_table_read(doors, DOOR_ROW, 0x10))


def check_loaders(lib, tree):
    big = lambda: CT.create_string_buffer(16 << 20)

    def rc(fn, *args):
        f = getattr(lib, fn)
        f.restype = CT.c_int
        return f(*args)
    p = lambda rel: str(tree / rel).encode()
    out = {}
    lib.em_spawn_table_read.restype = CT.c_void_p
    lib.em_spawn_table_read.argtypes = [CT.c_void_p, CT.c_uint32, CT.c_uint32]
    table = big()
    ok = rc('em_spawn_table_load', table, p('spawn_table.emsp')) == 0
    out['spawn'] = ok and bool(lib.em_spawn_table_read(table, SPAWN_ROWS[0], SPAWN_ROWS[1] * 0x30))
    out['doors'] = doors_loader_ok(lib, tree / 'door_destinations.emsp')
    for rel in ('overlay_data.emsc', 'sub0/level/static_bank.emsc'):
        out[rel] = rc('em_script_image_load', big(), p(rel)) == 1
    wm = (tree / 'sub0/world_models.emwm').read_bytes()
    out['sub0/world models'] = rc('em_world_models_parse', big(), wm, CT.c_size_t(len(wm))) == 0
    out['sub0/emcl'] = rc('em_collision_load', big(), p('sub0/area22.emcl')) == 0
    out['sub0/roster.emro'] = rc('em_actor_roster_load', big(), p('sub0/roster.emro')) == 0
    for z in sorted(str(x) for x in (tree / 'sub0/level').glob('*.emdl')):
        out[f'sub0/{Path(z).name}'] = rc('em_model_load', big(), z.encode()) == 0
    out['sub0/sfx registry'] = rc('em_sfx_registry_load', big(), p('sub0/sfx/sfx_registry.emsr')) == 1
    out['sub0/cells'] = rc('em_actor_cells_load', big(), p('sub0/area22_cells.bin')) == 0
    with T01.quiet_fds():
        message = rc('em_message_live_install', p('sub0/message_data.emmd'))
    return out, message


def message_findings_ok(rc, diag):
    """The loader verdicts docs/AREA22_ASSETS.md records: the message file
    (no area records, no area bank) is refused, and a diagnostic copy with
    one zero area record and the global bank repeated as an area bank loads
    (so nothing else in the file is refused)."""
    return rc == 0 and diag == 1


def message_diagnostic(lib, mm):
    """em_message_live_install over a diagnostic copy of the message file:
    area count 1 (one all-zero record) and the global bank appended as the
    area bank. Never an asset: written to the test build tree and removed."""
    BUILD.mkdir(parents=True, exist_ok=True)
    magic, version, area, gc, _ac, rows, cursor, gb, _ab = struct.unpack_from('<4s8I', mm, 0)
    at = 140 + 8 * gc
    gbank = mm[at + 16 * rows:at + 16 * rows + gb]
    blob = struct.pack('<4s8I', magic, version, area, gc, 1, rows, cursor, gb, gb) + mm[36:at] + bytes(8) + \
        mm[at:at + 16 * rows] + gbank + gbank
    path = BUILD / f'msgdiag-{os.getpid()}.emmd'
    path.write_bytes(blob)
    f = lib.em_message_live_install
    f.restype = CT.c_int
    try:
        with T01.quiet_fds():
            return f(str(path).encode())
    finally:
        path.unlink()


# ---------------------------------------------------------------------------
# Canary


def ram_copy(cap, name, edits=(), gs=None):
    """A copy of `cap` named `name` (keeping its pinned-table key). Without
    edits it shares the capture's own RAM and scratchpad objects, so the
    derivation memos (keyed on their identity) are reused."""
    fake = copy.copy(cap)
    fake.name, fake.key = name, key(cap)
    if edits:
        ram = bytearray(cap.ram)
        for at, data in edits:
            ram[at:at + len(data)] = data
        fake.ram = bytes(ram)
    if gs is not None:
        fake.gs = gs
    return fake


def flip(ram, at, bit=1):
    return (at, bytes([ram[at] ^ bit]))


def node_of(ram, behaviour, index=None, uid=None):
    for _s, a in T.pool_nodes(ram):
        if C.u32(ram, a + 0x10) == behaviour and (index is None or ram[a + 0x9A] == index) and \
                (uid is None or ram[a + 0x0F] == uid):
            return a
    return None


def canary_plants(K, cap, tree, tag):
    """RAM plants for the canary copy of `cap` -> (copy, {section: text})."""
    ram = cap.ram
    edits, want = [], {}
    bank = sub_path('level/static_bank.emsc').read_bytes()
    base, _e, length = struct.unpack_from('<3I', bank, 8)
    objects = [base + (C.s32(ram, base + 4 + 4 * i) >> 2 << 2) for i in range(C.u32(ram, base))]
    # the byte before the first object (after the offset table): the bank's
    # last byte is VIF data a level kick REFs (its STCYCL), which the kick
    # walker cannot parse once changed
    pad = min(objects) - 1
    assert pad >= base + 4 + 4 * len(objects)
    edits.append(flip(ram, pad))
    want['load map'] = f'{tag}: load map differs from RAM'
    want['static bank'] = f'{tag}: static bank differs from RAM'
    last = max(objects, key=lambda o: o + 0x820 * C.u32(ram, o))
    edits.append((last, bytes([ram[last] + 1])))
    want['static bank extent'] = f'{tag}: static bank length'
    edits.append(flip(ram, E1.D_0028A5A0 + 2))
    want['D_0028A5A0'] = f'{tag}: D_0028A5A0'
    edits.append((E1.D_0028A5A4, struct.pack('<I', base)))    # D_0028A5A4 into the load map
    want['D_0028A5A4'] = f'{tag}: D_0028A5A4 lies in the load map'
    edits.append(flip(ram, E1.D_0028A594 + 1))
    want['stale D_0028A594'] = f'{tag}: D_0028A594 = '
    edits.append(flip(ram, E1.D_0028A490 + 4 * 0x72 + 1))
    want['relocation word'] = f'{tag}: D_0028A490[0x72]'
    edits.append(flip(ram, CANARY_TEST_1, 2))      # TEST_1 of the level kicks' state
    want['GS state'] = 'level GS state'
    ctx = C.u32(ram, L.CTX_PTR)
    edits.append(flip(ram, ctx + 0xA4))
    want['ctx'] = f'ctx block: {tag}:'
    table = C.u32(cap.spad, L.SPAD_CELLS)
    cells = sub_path('area22_cells.bin').read_bytes()
    _count, hulls, _size = L.cell_directory(cells, 0)
    s0, _e0, _f0 = hulls[min(hulls)]
    edits.append(flip(ram, table + s0 + 0x20))
    want['cell hull'] = f'{tag}: hull {min(hulls)} differs'
    edits.append((table + 8 + 3, bytes([ram[table + 8 + 3] ^ 0x40])))    # uid 1's word, bit 30
    want['uid word'] = f'{tag}: directory bytes differ at'
    edits.append(flip(ram, GRID + C.u32(ram, GRID) + 1))
    want['EMCL'] = f'{tag}: sub0/area22.emcl differs from the RAM grid rebuild'
    roster = sub_path('roster.emro').read_bytes()
    paddr = C.u32(roster, 0x10)
    edits.append(flip(ram, paddr + 0x28 * C.u32(roster, 0x0C) - 1))
    want['roster'] = f'{tag}: roster records differ from RAM'
    want['overlay data'] = f'{tag}: overlay data: '
    _ok, _live, pairs = T01.roster_nodes(T, roster, ram)
    edits.append(flip(ram, pairs[0][1] + 0x03))
    want['placement node fields'] = f'{tag}: a live placement node lost a copied field'
    edits.append(flip(ram, DOOR_ROW + 0x0F))
    want['door windows'] = f'{tag}: door_destinations.emsp window {DOOR_ROW:#x} differs'
    rows, n = SPAWN_ROWS
    edits.append(flip(ram, rows + n * 0x30 - 1))   # the last byte of the area's spawn rows
    want['spawn windows'] = f'{tag}: spawn_table.emsp window'
    gptr = C.u32(K.elf, T01.MSG_TABLES - C.ELF_VADDR + C.ELF_OFFSET)
    edits.append(flip(ram, gptr + 5))
    want['message records'] = f'{tag}: message records differ'
    edits.append((T01.MSG_TABLES + 4 * (C.AREA + 1), struct.pack('<I', gptr)))
    want['message area pointer'] = f'{tag}: D_00264DD0 pointers'
    wm = sub_path('world_models.emwm').read_bytes()
    edits.append(flip(ram, C.u32(wm, 8) + C.u32(wm, 12) - 1))
    want['world models'] = f'{tag}: world model bank differs'
    edits.append(flip(ram, T01.LADDER + 2 * 0x40))
    want['registry'] = f'{tag}: sub0/sfx/sfx_registry.emsr differs'
    # the matrix of the first hull this capture shows re-transformed by a
    # live pickup (cells.json), one byte flipped
    rec = json.loads(sub_path('cells.json').read_text())
    moved = next((r['moved_hulls'] for r in rec['cells']['captures'] if r['capture'] == key(cap)), [])
    for uid in moved:
        owner = next((a for _s, a in T.pool_nodes(ram) if ram[a + 0x0F] == int(uid)
                      and C.u32(ram, a + 0x10) == E1.PICKUP), None)
        if owner is not None:
            edits.append(flip(ram, E1.owner_matrix(ram, owner) + 0x33, 0x04))
            want['owner matrix'] = f'{tag}: hull {uid} differs and no owner derivation reproduces it'
            break
    wtab = C.u32(wm, 8)
    offs = [wtab + (C.s32(ram, wtab + 4 + 4 * i) >> 2 << 2) for i in range(C.u32(ram, wtab))]
    slot, owner = next((s, a) for s, a in E2.E02.model_owner_nodes(ram) if C.u32(ram, a + 0x44) in offs
                       and (ram[a + 0x0D] ^ 1) < len(offs) and offs[ram[a + 0x0D] ^ 1] != C.u32(ram, a + 0x44))
    edits.append(flip(ram, owner + 0x0D))
    want['model binding'] = f'{tag} slot {slot}: +0x44 != 001C6120(table, +0x0D)'
    edits.append(flip(ram, T.D_008106C8 + 1))
    want['spawn entry word'] = f'{tag}: D_008106C8 '
    gs = bytearray(cap.gs.read_bytes())
    zb = next(iter(next(iter(_ZONES.values())).values()))[0]
    first_tex = zb.tex_table[0]
    at = len(gs) - 0x400000 - 84 + 256 * first_tex['tbp0']
    gs[at:at + 256] = bytes(x ^ 0xFF for x in gs[at:at + 256])
    gpath = tree / f'{tag}_gs.bin'
    gpath.write_bytes(bytes(gs))
    want['zone texels'] = f'texture 0 texels differ from the {tag} GS freeze'
    return ram_copy(cap, tag, edits, gs=gpath), want


def mutate(data, at, bit=1):
    b = bytearray(data)
    b[at] ^= bit
    return bytes(b)


def canary(K, lib, cap):
    """run_checks over [capture, planted copy] and a tree with file plants;
    each section must report its plant for the copy and stay silent for the
    original. Returns the number of sections."""
    global A
    real, tree = A, CANARY

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
        (tree / rel).parent.mkdir(parents=True, exist_ok=True)
        (tree / rel).write_bytes(data)
    fake, want = canary_plants(K, cap, tree, 'canary0')
    # the quiet check (the original capture must not report the section):
    # not for the uid word, whose text the cells-file plant below also
    # produces for the original
    originals = {k: cap.name for k in want if k != 'uid word'}
    zones = sorted(x.name for x in (real / 'sub0/level').glob('*.emdl'))
    plant('sub0/level/00_a_extra.emdl', (real / f'sub0/level/{zones[0]}').read_bytes())
    want['zone count'] = f'sub0: {len(zones) + 1} zone files for {len(zones)} source files'
    want['zone name'] = 'sub0/00_a_extra.emdl: zone name'
    rec = json.loads((real / 'sub0/cells.json').read_text())
    row = next(r for r in rec['cells']['captures'] if r['capture'] == key(cap))
    row['moved_hulls'] = row['moved_hulls'][:-1]
    plant('sub0/cells.json', json.dumps(rec).encode())
    want['moved set'] = f'{cap.name}: re-transformed hulls'
    plant('sub0/area22_cells.bin', mutate((real / 'sub0/area22_cells.bin').read_bytes(), -1))
    want['cells file'] = 'sub0/area22_cells.bin differs from the disc bytes'
    plant('sub0/sfx/area22_banks.bin', mutate((real / 'sub0/sfx/area22_banks.bin').read_bytes(), 0x20000))
    want['sound bank'] = 'sub0/sfx/area22_banks.bin differs from the disc container'
    plant('sub0/roster.emro', mutate((real / 'sub0/roster.emro').read_bytes(), 0x09))
    want['roster file'] = 'sub0/roster.emro differs from the rebuild from the disc'
    od = (real / 'overlay_data.emsc').read_bytes()
    plant('overlay_data.emsc', mutate(od, len(od) - 1))
    want['overlay data file'] = 'overlay data differs from the disc overlay'
    mm = (real / 'sub0/message_data.emmd').read_bytes()
    plant('sub0/message_data.emmd', mutate(mm, len(mm) - 1))
    want['message file'] = f'{cap.name}: message banks differ'
    tab = json.loads((real / 'tables.json').read_text())
    tab['overlay_data']['run_time_words'][key(cap)] = ['0x823600']
    tab['scripts']['chains'] = {'00823600': 1}
    plant('tables.json', json.dumps(tab).encode())
    want['overlay run-time set'] = 'tables.json overlay data run-time words'
    want['script chains'] = 'tables.json scripts chains are not empty'
    n = len(FAILS)
    try:
        A = tree
        with contextlib.redirect_stdout(io.StringIO()):
            got, _v = run_checks(K, [cap, fake])
        # the zone file itself (its own pass: a structure difference stops
        # zone_problems before the texels, which the pass above plants)
        zbuf = (real / f'sub0/level/{zones[-1]}').read_bytes()
        plant(f'sub0/level/{zones[-1]}', mutate(zbuf, T01.emdl_parts(zbuf)['verts'] + 1))
        want['zone EMDL'] = f'sub0/{zones[-1]} differs from a rebuild'
        plant('sub0/level/dynamic_objects.emsc', C.emsc(STALE[E1.D_0028A5A4], bytes(16)))
        want['dynamic list file'] = 'sub0: a dynamic-list file'
        (tree / 'sub0/level/00_a_extra.emdl').unlink()
        got2 = level_problems(K, [cap])[0]
        loader_want = plant_loader_canary(tree, plant)
        with contextlib.redirect_stdout(io.StringIO()), T01.quiet_fds():
            loaded, _message = check_loaders(lib, tree)
    finally:
        A = real
        remove_tree()
    del FAILS[n:]
    for section, text in want.items():
        check(any(text in x for x in got + got2), f'canary: section {section} did not report "{text}"')
        if section in originals:
            quiet = text.replace('canary0', originals[section])
            if quiet != text:
                check(not any(quiet in x for x in got), f'canary: section {section} reported the original too')
    for name in loader_want:
        check(not loaded.get(name, True), f'canary: loader {name} accepted a broken file')
    return len(want) + len(loader_want)


def plant_loader_canary(tree, plant):
    """Replace each loaded file in the tree by one its loader must refuse;
    returns the check_loaders names. The spawn file loses the last byte of
    the area's rows; the door file keeps only the pointer array; an EMSC
    keeps its header with a 32-byte window; the cells file loses its last
    byte; every other file is cut to its first 16 bytes."""
    spawn = T01.emsp_windows((tree / 'spawn_table.emsp').read_bytes())
    end0 = SPAWN_ROWS[0] + 0x30 * SPAWN_ROWS[1]
    plant('spawn_table.emsp', T01.emsp_file([(a, d[:end0 - 1 - a] if a <= SPAWN_ROWS[0] < a + len(d) else d)
                                             for a, d in spawn]))
    doors = T01.emsp_windows((tree / 'door_destinations.emsp').read_bytes())
    plant('door_destinations.emsp', T01.emsp_file([(a, d) for a, d in doors if a == T01.D_0024E140]))
    names = ['spawn', 'doors']
    for rel in ('overlay_data.emsc', 'sub0/level/static_bank.emsc'):
        blob = (tree / rel).read_bytes()
        base, entry = struct.unpack_from('<2I', blob, 8)
        plant(rel, C.emsc(base, blob[20:52], entry))
        names.append(rel)
    for rel, name in (('sub0/roster.emro', 'sub0/roster.emro'), ('sub0/world_models.emwm', 'sub0/world models'),
                      ('sub0/area22.emcl', 'sub0/emcl'), ('sub0/sfx/sfx_registry.emsr', 'sub0/sfx registry')):
        plant(rel, (tree / rel).read_bytes()[:16])
        names.append(name)
    cells = (tree / 'sub0/area22_cells.bin').read_bytes()
    plant('sub0/area22_cells.bin', cells[:-1])
    names.append('sub0/cells')
    for z in sorted(tree.joinpath('sub0/level').glob('*.emdl')):
        plant(f'sub0/level/{z.name}', z.read_bytes()[:16])
        names.append(f'sub0/{z.name}')
    return names


# ---------------------------------------------------------------------------
# Controls


def controls(K, caps, lib):
    """One changed input per comparator (file side and RAM side), and the
    accept cases. Returns the number of controls."""
    global A
    n = 0

    def expect(problems, what, accept=False):
        nonlocal n
        n += 1
        check(bool(problems) != accept, f'control: {what} {"rejected" if accept else "missed"}')

    def expect_text(problems, text, what):
        expect([p for p in problems if text in p], what)
    by_name = {c.name: c for c in caps}
    first = caps[0]
    later = by_name.get('a22_s1_ladder_reader') or caps[-1]
    record = json.loads((A / 'sub0/cells.json').read_text())
    cells = (A / 'sub0/area22_cells.bin').read_bytes()
    count = C.u32(cells, 0)
    table = C.u32(first.spad, L.SPAD_CELLS)
    _c, hulls, _s = L.cell_directory(cells, 0)
    # cells: the file side
    for at in (0, 4, 4 + 4 * count, len(cells) - 1):
        expect(cells_problems(K, mutate(cells, at), [first], record), f'cells file +{at:#x}')
    expect_text(cells_problems(K, cells + b'\0', [first], record), 'bytes, the directory', 'cells size check')
    # every moved hull's last byte, and the pickups' matrices
    for uid in record['cells']['moved_uids']:
        s, e, _f = hulls[int(uid)]
        fake = ram_copy(first, f'hull {uid}', [flip(first.ram, table + e - 1)])
        expect(L.verify_cell_directory(K.elf, cells, [fake])[1], f'moved hull {uid} last byte')
    tried = 0
    for _s0, node in T.pool_nodes(first.ram):
        if C.u32(first.ram, node + 0x10) != E1.PICKUP or first.ram[node + 0x0F] not in hulls:
            continue
        s, e, _f = hulls[first.ram[node + 0x0F]]
        if first.ram[table + s:table + e] == cells[s:e]:
            continue
        tried += 1
        fake = ram_copy(first, f'matrix {node:#x}', [flip(first.ram, node + 0xD0 + 0x32, 0x10)])
        expect(L.verify_cell_directory(K.elf, cells, [fake])[1], f'pickup {node:#x} matrix (node + 0xD0)')
    check(tried == 4, f'control: {tried} pickups with a moved hull in the arrival, not 4')
    # the orphan: a22_s0 freed the pickup g[2] (hull 21) and reused its node;
    # alone it has no derivation, with a capture of the live pickup it is
    # accepted (the orphan rule)
    s0 = by_name.get('a22_s0_pickup')
    if s0 is not None:
        expect_text(L.verify_cell_directory(K.elf, cells, [s0])[1], 'no live node owns it',
                    'the orphaned hull 21 with no derivation elsewhere')
        expect(L.verify_cell_directory(K.elf, cells, [first, s0])[1], 'the orphaned hull 21 with the arrival',
               accept=True)
        s21, e21, _f = hulls[21]
        fake = ram_copy(s0, 'orphan changed', [flip(s0.ram, table + e21 - 1)])
        expect_text(L.verify_cell_directory(K.elf, cells, [first, fake])[1], 'equals no',
                    'an orphaned hull that equals no derivation')
    # the drums (0x156620 passes drum + 0xD0 to 001A2370 on its state-2
    # path). Their AREA22 hulls' first prim carries 0x4000 without the
    # extended bit 0x800 that 001A2370 requires, so the ORIGINAL leaves every
    # drum hull as it is for any matrix. With the bit planted in a copy of
    # the directory, the ORIGINAL does move the hull, and
    # verify_cell_directory accepts the result only because owner_matrix
    # names the drum; the same hull with the matrix at rest is rejected.
    from test_coll_move_reference import FloatEE
    drums = [a for _s1, a in T.pool_nodes(first.ram)
             if C.u32(first.ram, a + 0x10) == E1.DRUM and first.ram[a + 0x0F] in hulls]
    check(len(drums) == DRUMS, f'control: {len(drums)} live drums owning a hull, not {DRUMS}')

    def run_original(ram, directory, node):
        ee = FloatEE(K.elf, bytes(ram), first.spad)
        ee.write(table, directory)
        ee.call(L.RETRANSFORM, (node, node + 0xD0))
        return ee.read(table, len(directory))
    for drum in drums:
        ds, _de, _f = hulls[first.ram[drum + 0x0F]]
        kram = bytearray(first.ram)
        kram[drum + 0xD0 + 0x32] ^= 0x10              # translation x changed
        check(C.u16(cells, ds + 0x18 + 4) & 0x800 == 0 and run_original(kram, cells, drum) == cells,
              f'control: drum {drum:#x}: its hull has no 0x800 bit and stays as on disc')
    drum = drums[0]
    ds, de, _f = hulls[first.ram[drum + 0x0F]]
    ext = bytearray(cells)
    struct.pack_into('<H', ext, ds + 0x18 + 4, C.u16(cells, ds + 0x18 + 4) | 0x800)
    ext = bytes(ext)
    knock = [flip(first.ram, drum + 0xD0 + 0x32, 0x10)]
    kram = bytearray(first.ram)
    kram[knock[0][0]] = knock[0][1][0]
    got = run_original(kram, ext, drum)
    check(got[ds:de] != ext[ds:de] and got[:ds] == ext[:ds] and got[de:] == ext[de:],
          'control: with the 0x800 bit the knocked drum moves exactly its own hull')
    fake = ram_copy(first, 'knocked drum', knock + [(table + ds, bytes(got[ds:de]))])
    expect(L.verify_cell_directory(K.elf, ext, [fake])[1], 'a knocked drum\'s extended hull (001A2370, drum + 0xD0)',
           accept=True)
    fake = ram_copy(first, 'drum hull, matrix at rest', [(table + ds, bytes(got[ds:de]))])
    expect(L.verify_cell_directory(K.elf, ext, [fake])[1], 'a drum hull moved without its matrix')
    saved = L.owner_matrix
    try:
        L.owner_matrix = lambda ram, node: node + 0xD0 if C.u32(ram, node + 0x10) == E1.PICKUP else None
        fake = ram_copy(first, 'knocked drum, no drum owner', knock + [(table + ds, bytes(got[ds:de]))])
        expect(L.verify_cell_directory(K.elf, ext, [fake])[1], 'the knocked drum with an owner list without the drum')
    finally:
        L.owner_matrix = saved
        E1.install()
    # the uid words: bit 30 of uid 1 changed, and the 001A2370 code in RAM
    fake = ram_copy(first, 'uid word', [(table + 8 + 3, bytes([first.ram[table + 8 + 3] ^ 0x40]))])
    expect_text(L.verify_cell_directory(K.elf, cells, [fake])[1], 'directory bytes differ', 'uid 1 word bit 30')
    fake = ram_copy(first, 'retransform code', [flip(first.ram, L.RETRANSFORM + 0x767)])
    expect(L.verify_cell_directory(K.elf, cells, [fake])[1], '001A2370 code in RAM')
    # owner_matrix unit controls
    ram = bytearray(first.ram)
    node = 0x1C00000
    for b, want in ((0x219550, node + 0xD0), (0x156620, node + 0xD0), (0x219870, None), (0x219F50, None),
                    (0x15AFA0, None), (0x825510, None), (0x8260C0, None), (0x8261A0, None), (0x826D40, None)):
        ram[node:node + 0x2F0] = bytes(0x2F0)
        struct.pack_into('<I', ram, node + 0x10, b)
        expect(E1.owner_matrix(bytes(ram), node) != want, f'owner matrix {b:#x}', accept=True)
    # the 001A2370 caller census: an extra call site, and a live 0x219870
    ov = A22.read_overlay()
    expect(retransform_census_problems(K.elf, ov, [first]), 'the 001A2370 census', accept=True)
    extra = bytearray(ov)
    struct.pack_into('<I', extra, len(ov) - 4, (3 << 26) | (L.RETRANSFORM >> 2))
    expect_text(retransform_census_problems(K.elf, bytes(extra), [first]), 'call sites', 'an extra 001A2370 call')
    fake = ram_copy(first, 'scratch owner', [(node_of(first.ram, E1.DRUM) + 0x10, struct.pack('<I', 0x219870))])
    expect_text(retransform_census_problems(K.elf, ov, [fake]), 'scratchpad copy', 'a live 0x219870 node')
    # the overlay data: every word, no chain
    olo, ohi = E2.E02.overlay_data_window(ov)
    odisc = ov[olo - C.OVERLAY_ARENA:ohi - C.OVERLAY_ARENA]
    rec_t = json.loads((A / 'tables.json').read_text())
    od = (A / 'overlay_data.emsc').read_bytes()
    for at in (olo, ohi - 1, E2.PLACEMENTS + 0x10, E2.NEST_GROUP[0] + 0x10):
        fake = ram_copy(first, f'overlay word {at:#x}', [flip(first.ram, at, 0x80)])
        expect_text(window_problems('overlay data', od, (olo, len(odisc)), odisc, [fake],
                                    rec_t['overlay_data']['run_time_words']), 'differ from the module',
                    f'an overlay-data byte at {at:#x}')
    expect(window_problems('overlay data', od, (olo, len(odisc)), odisc, caps, rec_t['overlay_data']['run_time_words']),
           'the overlay data in every capture', accept=True)
    for name, bad in (('first byte', mutate(od, 20)), ('last byte', mutate(od, len(od) - 1)),
                      ('base moved', T01.emsc_blob(od, base_shift=4, cut_head=4)), ('trailing byte', od + b'\0'),
                      ('four bytes short', T01.emsc_blob(od, cut_tail=4))):
        expect(tables_problems(K, [first], {'overlay_data.emsc': bad}), f'overlay data {name}')
    # the script chains: none in the C (accepted); a planted call (rejected)
    expect(script_problems(E2.c_script_calls(), rec_t['scripts']['chains']), 'no chain in the C', accept=True)
    expect_text(script_problems([('x.c', 1)], {}), 'starts script chains', 'a func_001BA1A0 call in the C')
    # every table file's last byte
    for rel in ('sub0/roster.emro', 'spawn_table.emsp', 'door_destinations.emsp', 'sub0/message_data.emmd',
                'sub0/world_models.emwm'):
        expect(tables_problems(K, [first], {rel: mutate((A / rel).read_bytes(), -1)}), f'{rel} last byte')
    expect(tables_problems(K, [first], {'sub0/world_models.emwm': b'EMWM'}), 'truncated world models')
    # the message file
    mm = (A / 'sub0/message_data.emmd').read_bytes()
    for cut in ('grec', 'gbank', 'rows'):
        expect(tables_problems(K, [first], {'sub0/message_data.emmd': T01.mm_blob(mm, cut)}),
               f'message file one {cut} unit short')
    withrec = T01.mm_blob(mm, 'grec')
    magic, version, area, gc, _ac, rows_, cursor, gb, ab = struct.unpack_from('<4s8I', withrec, 0)
    moved_rec = struct.pack('<4s8I', magic, version, area, gc, 1, rows_, cursor, gb, ab) + withrec[36:]
    expect_text(messages_problems(moved_rec, [first], K.elf), 'not 0 / 0', 'a global record counted as an area record')
    m_gc, m_rows, m_gb = C.u32(mm, 12), C.u32(mm, 20), C.u32(mm, 28)
    with_abank = struct.pack('<4s8I', b'EMMD', 1, C.AREA, m_gc, 0, m_rows, T01.MSG_CURSOR, m_gb, m_gb) + \
        mm[36:] + mm[len(mm) - m_gb:]
    expect_text(messages_problems(with_abank, [first], K.elf), 'not 0 / 0', 'an area bank appended (abank != 0)')
    expect_text(messages_problems(mutate(mm, 8), [first], K.elf), 'message header', 'the area word')
    fake = ram_copy(first, 'area pointer', [(T01.MSG_TABLES + 4 * (C.AREA + 1), struct.pack('<I', 0x26F790))])
    expect_text(messages_problems(mm, [fake], K.elf), 'D_00264DD0 pointers', 'an area table pointer in RAM')
    g0 = C.u32(first.ram, 0x28A4E8)
    fake = ram_copy(first, 'global bank', [flip(first.ram, g0 + 0x40)])
    expect_text(messages_problems(mm, [fake], K.elf), 'message banks differ', 'a global bank byte in RAM')
    fake = ram_copy(first, 'stream end', [(T01.MSG_STREAMS + 16 * rows_, bytes(4))])
    expect_text(messages_problems(mm, [fake], K.elf), 'stream rows', 'the -1 stream row cleared')
    expect(messages_problems(mm, caps, K.elf), 'the message file in every capture', accept=True)
    # every ELF block's last byte, file side and RAM side (a comparator that
    # dropped a block's final byte passes the rest)
    for bfo, baddr, blen in T01.MSG_BLOCKS:        # not `n`: expect() counts in it
        expect_text(messages_problems(mutate(mm, bfo + blen - 1), [first], K.elf), f'message block {baddr:#x}',
                    f'the last byte of message block {baddr:#x} in the file')
        fake = ram_copy(first, f'message block {baddr:#x}', [flip(first.ram, baddr + blen - 1)])
        expect_text(messages_problems(mm, [fake], K.elf), f'message block {baddr:#x}',
                    f'the last byte of message block {baddr:#x} in RAM')
    # a trailing byte: the length check itself must report it
    expect_text(messages_problems(mm + b'\0', [first], K.elf), f'length {len(mm) + 1} != {len(mm)}',
                'a trailing byte on the message file')
    # the stale pointers
    expect(stale_problems(caps, K.top), 'the unrelocated words in every capture', accept=True)
    # a descriptor that relocates id 0x41 or 0x45 (its first list entry's id
    # replaced in a copy): the stale model must not apply
    first_entry = 0x20 + 8 * (C.u16(K.top, 0x0C) + C.u16(K.top, 0x0E))
    for ident in (0x41, 0x45):
        planted = bytearray(K.top)
        planted[first_entry + 3] = ident
        expect_text(stale_problems(caps[:1], bytes(planted)), f'relocates id {ident:#x}',
                    f'a descriptor relocating id {ident:#x}')
    for at in STALE:
        fake = ram_copy(first, f'stale {at:#x}', [flip(first.ram, at + 1)])
        expect_text(stale_problems([fake]), f'D_{at:08X}', f'D_{at:08X} changed')
    # the roster
    roster = (A / 'sub0/roster.emro').read_bytes()
    _ok, _live, pairs = T01.roster_nodes(T, roster, first.ram)
    fake = ram_copy(first, 'node moved', [flip(first.ram, pairs[1][1] + 0xB1)])
    expect(roster_problems(roster, [fake]), 'a placement node moved off its record')
    fake = ram_copy(first, 'no pool', [(L.POOL_BASE, bytes(L.POOL_STRIDE * L.POOL_SLOTS))])
    expect(roster_problems(roster, [fake]), 'an empty actor pool')
    for off in (0x03, 0x0D, 0x54):
        fake = ram_copy(first, f'+{off:#x}', [flip(first.ram, pairs[0][1] + off)])
        expect_text(roster_problems(roster, [fake]), 'lost a copied field', f'a placement node +{off:#x}')
    fake = ram_copy(first, 'group record', [flip(first.ram, E2.GROUPS[0][0] + 0x2C * E2.GROUPS[0][1] - 1)])
    expect_text(roster_problems(roster, [fake]), 'roster records differ', 'the last deferred-group byte')
    # model bindings
    wm = (A / 'sub0/world_models.emwm').read_bytes()
    wtab = C.u32(wm, 8)
    offs = {wtab + (C.s32(first.ram, wtab + 4 + 4 * i) >> 2 << 2) for i in range(C.u32(wm, 32))}
    node = next(a for _s2, a in E2.E02.model_owner_nodes(first.ram) if C.u32(first.ram, a + 0x44) in offs)
    expect(model_binding_problems(wm, [ram_copy(first, 'model id', [flip(first.ram, node + 0x0D)])]), 'model id +0x0D')
    expect(model_binding_problems(wm, [ram_copy(first, 'bones', [flip(first.ram, node + 0x0C)])]), 'bone count')
    multi = next((a for _s2, a in E2.E02.model_owner_nodes(first.ram) if first.ram[a + 0x0C] >= 2 and
                  C.u32(first.ram, a + 0x44) in offs), None)
    if multi is not None:
        last_slot = multi + 0x110 + 4 * (first.ram[multi + 0x0C] - 1)
        expect_text(model_binding_problems(wm, [ram_copy(first, 'bone slot', [(last_slot, bytes(4))])]),
                    'bone count / slots', 'the last bone slot of a multi-bone owner cleared')
    expect_text(model_binding_problems(wm, [ram_copy(first, 'unbound', [(node + 0x44, bytes(4))])]),
                'model owners bound, not', 'one bound owner fewer')
    # the spawn entry word: the load entry, not the entry byte
    expect(spawn_entry_problems(caps), 'D_008106C8 of each capture\'s load entry', accept=True)
    if 'a22_s1_ladder_reader' in by_name:
        s1 = by_name['a22_s1_ladder_reader']
        saved = dict(LOAD_ENTRY)
        try:
            LOAD_ENTRY['a22_s1_ladder_reader'] = s1.ram[0x810702]
            expect_text(spawn_entry_problems([s1]), '+0x1C of spawn record 3',
                        'a22_s1 with the entry byte (3) as the load entry')
        finally:
            LOAD_ENTRY.clear()
            LOAD_ENTRY.update(saved)
    expect_text(spawn_entry_problems([ram_copy(first, 'entry', [(0x810702, bytes([SPAWN_ROWS[1]]))])]), 'outside the',
                'the entry byte one past the rows')
    expect_text(spawn_entry_problems([ram_copy(first, 'sub 1', [(0x810701, b'\x01')])]), 'sub 1, not 0',
                'a capture whose sub byte is 1')
    unrec = ram_copy(first, 'unrecorded')
    unrec.key = 'unrecorded'
    expect_text(spawn_entry_problems([unrec]), 'no recorded load entry', 'an unrecorded capture')
    # the door loader: both windows must be served
    dw = T01.emsp_windows((A / 'door_destinations.emsp').read_bytes())
    BUILD.mkdir(parents=True, exist_ok=True)
    dpath = BUILD / f'doors-{os.getpid()}.emsp'
    try:
        for keep, what, ok in ((lambda a: True, 'the door file', True),
                               (lambda a: a != T01.D_0024E140, 'a door file without the pointer array', False),
                               (lambda a: a != DOOR_ROW, 'a door file without the row', False)):
            dpath.write_bytes(T01.emsp_file([(a, d) for a, d in dw if keep(a)]))
            with T01.quiet_fds():
                got = doors_loader_ok(lib, dpath)
            expect(not got, what, accept=ok)
    finally:
        dpath.unlink(missing_ok=True)
    # the loader-finding verdict
    expect(not message_findings_ok(0, 1), 'the recorded loader verdicts', accept=True)
    for rc, d in ((1, 1), (0, 0), (1, 0)):
        expect(not message_findings_ok(rc, d), f'loader verdicts {rc} / {d} taken as the finding')
    # the load map
    lmap = K.lmap
    for entry in (lmap[0], lmap[-1]):
        fake = ram_copy(first, 'mapped byte', [flip(first.ram, entry[0] + entry[3] - 1)])
        expect(load_problems(K, [fake], len(cells)), f'last byte of {entry[4]}')
        fake = ram_copy(first, 'mapped byte', [flip(first.ram, entry[0])])
        expect(load_problems(K, [fake], len(cells)), f'first byte of {entry[4]}')
    for at in (table - 1, table + len(cells)):
        if K.image.locate(at) is not None:
            expect(load_problems(K, [ram_copy(first, 'edge', [flip(first.ram, at)])], len(cells)),
                   f'the mapped byte {at:#x} next to the directory')
    fake = ram_copy(first, 'cursor', [(C.D_0028A740, struct.pack('<I', C.u32(first.ram, C.D_0028A740) + 0x800))])
    expect_text(load_problems(K, [fake], len(cells)), 'D_0028A740', 'the second cursor off the resident length')
    both = C.u32(first.ram, C.D_0028A73C) + 0x800
    fake = ram_copy(first, 'both cursors', [(C.D_0028A73C, struct.pack('<I', both)),
                                            (C.D_0028A740, struct.pack('<I', C.u32(first.ram, C.D_0028A740) + 0x800))])
    expect_text(load_problems(K, [fake], len(cells)), 'a different load map', 'both cursors moved')
    expect_text(load_problems(K, [ram_copy(first, 'sub byte', [(0x810701, b'\1')])], len(cells)), 'no nested block',
                'a capture whose sub byte is 1')
    # the labels: the map as built (accepted in every capture); labelled by
    # the extracted files' ids (the list read as block offsets), a row
    # moved to its neighbour id, and a dropped row (rejected)
    for cap in caps:
        expect(label_problems(lmap, cap, K.top), f'the id labels in {cap.name}', accept=True)
    extracted = [(a, pth, o, n, 'chunk26/id' + pth.name.split('_id')[1][:2]) for a, pth, o, n, _l in lmap]
    expect_text(label_problems(extracted, first, K.top), 'lies outside D_0028A490',
                'the map labelled by the extracted file names')
    k = next(i for i, r in enumerate(lmap) if r[4] == 'chunk26/id42')
    shifted = list(lmap)
    shifted[k] = (*lmap[k][:4], 'chunk26/id44')
    expect_text(label_problems(shifted, first, K.top), 'labelled chunk26/id44', 'the grid row labelled id 0x44')
    expect_text(label_problems(lmap[:k] + lmap[k + 1:], first, K.top), 'a gap', 'a row dropped from the map')
    expect_text(label_problems(lmap[:-1], first, K.top), 'does not span', 'the last row dropped')
    fake = ram_copy(first, 'relocation', [flip(first.ram, E1.D_0028A490 + 4 * 0x71)])
    expect_text(load_problems(K, [fake], len(cells)), 'D_0028A490[0x71]', 'a relocation word')
    fake = ram_copy(first, 'spad pointer')
    sp = bytearray(first.spad)
    struct.pack_into('<I', sp, L.SPAD_CELLS, C.u32(sp, L.SPAD_CELLS) + 0x10)
    fake.spad = bytes(sp)
    expect_text(load_problems(K, [fake], len(cells)), 'cell directory pointer', 'the scratchpad directory pointer')
    # the level: zone file bytes, the GS state, the bank file
    zfile = sorted((A / 'sub0/level').glob('*.emdl'))[0]
    zbuf = zfile.read_bytes()
    b = next(z for zz in _ZONES.values() for lab, z in zz.items() if lab == 'chunk26/id44')[0]
    gs_caps = T01.gs_captures([first])
    prim = K.el.level_template_prim(K.el.BootElf(C.ELF_PATH))
    parts = T01.emdl_parts(zbuf)
    for name, bad in (('position', mutate(zbuf, parts['verts'] + 1)), ('texel', mutate(zbuf, parts['blob'] + 7)),
                      ('last index', mutate(zbuf, parts['blob'] - 128 - 1)), ('GS code', mutate(zbuf, parts['tex'] + 15)),
                      ('last texel', mutate(zbuf, len(zbuf) - 1)), ('trailing byte', zbuf + b'\0')):
        expect(T01.zone_problems(K.el, 'control', bad, b, gs_caps, prim), f'zone {name}')
    expect(T01.zone_problems(K.el, 'control', zbuf, b, gs_caps, prim), 'the zone file itself', accept=True)
    expect(T01.gs_state_problems({(1, 2, 3, 4, 5): 1}, T01.level_gs_state(K.el, prim)), 'level GS state')
    bank = (A / 'sub0/level/static_bank.emsc').read_bytes()
    base, _e, length = struct.unpack_from('<3I', bank, 8)
    expect_text(bank_problems(bank, [ram_copy(first, 'bank end', [flip(first.ram, base + length - 1)])]),
                'static bank differs from RAM', 'the static bank\'s last byte')
    for name, bb in (('bank trailing byte', bank + b'\0'), ('bank one byte short', T01.emsc_blob(bank, cut_tail=1)),
                     ('bank base word', mutate(bank, 8)), ('bank version', mutate(bank, 4))):
        expect(bank_problems(bb, [first]), f'level files: {name}')
    # sound
    banks = sub_path('sfx/area22_banks.bin').read_bytes()
    reg = sub_path('sfx/sfx_registry.emsr').read_bytes()
    for name, bad in (('byte 0', mutate(banks, 0)), ('last byte', mutate(banks, -1)), ('truncated', banks[:-1]),
                      ('row +0x50', mutate(banks, 0x50))):
        expect(sfx_problems(K, [first], bad, reg), f'area22_banks.bin {name}')
    expect_text(sfx_problems(K, [first], banks + b'\0', reg), 'container total', 'container length')
    expect(sfx_problems(K, [first], banks, mutate(reg, len(reg) // 2)), 'registry middle byte')
    expect(sfx_problems(K, [first], banks, mutate(reg, len(reg) - 1)), 'registry last byte')
    handle_slot = 0x281D50 + 4 * (4 * 0x14 + 1)
    expect(sfx_problems(K, [ram_copy(first, 'binding', [(handle_slot, b'\0\0\0\0')])], banks, reg),
           'group 4 slot 1 unbound')
    second = next(c for c in caps if c is not first)
    expect_text(sfx_problems(K, [first, ram_copy(second, 'binding 2', [(handle_slot, b'\0\0\0\0')])], banks, reg),
                'a different bank binding', 'group 4 slot 1 unbound in the second capture only')
    why = E3.E04.refusal_reasons(caps, REFUSED)
    expect(not ('without SShd magic' in why[(3, 0)]), 'the refusal reason (group 3: no magic)', accept=True)
    good = dict(BINDING)
    expect(binding_problem(good, set(REFUSED)), 'the pinned bindings', accept=True)
    expect(binding_problem({**good, (2, 0): ('area', 1)}, set(REFUSED)), 'another row')
    expect(binding_problem({k: v for k, v in good.items() if k != (4, 1)}, set(REFUSED)), 'a binding missing')
    expect(binding_problem(good, set()), 'nothing refused')
    expect(binding_problem(good, set(REFUSED) | {(4, 1)}), 'an extra refused slot')
    # EMCL
    emcl = (A / 'sub0/area22.emcl').read_bytes()
    T01.GRID = GRID
    for at in (0x30 + 1, len(emcl) // 2, len(emcl) - 1):
        expect(T01.emcl_equal(mutate(emcl, at), first.ram), f'EMCL +{at:#x}')
    expect(T01.emcl_equal(emcl + b'\0', first.ram), 'EMCL trailing byte')
    fake = ram_copy(first, 'grid pointer', [flip(first.ram, E1.D_0028A598 + 1)])
    expect(emcl_problems(K, emcl, [fake], len(cells)), 'D_0028A598')
    fake = ram_copy(first, 'grid node', [flip(first.ram, GRID + C.u32(first.ram, GRID + 0x20) + 0x26)])
    expect(emcl_problems(K, emcl, [fake], len(cells)), 'a grid node plane byte')
    fake = ram_copy(first, 'allowance on the grid')
    sp = bytearray(first.spad)
    struct.pack_into('<I', sp, L.SPAD_CELLS, GRID)
    fake.spad = bytes(sp)
    expect_text(emcl_problems(K, emcl, [fake], len(cells)), 'inside the cell-directory allowance',
                'the grid inside the directory allowance')
    fake = ram_copy(first, 'allowance over the grid start')
    sp = bytearray(first.spad)
    struct.pack_into('<I', sp, L.SPAD_CELLS, GRID - 0x10)
    fake.spad = bytes(sp)
    expect_text(emcl_problems(K, emcl, [fake], len(cells)), 'grid header inside the cell-directory allowance',
                'a directory allowance starting 0x10 below the grid')
    # ctx: a byte of each record, 001D1C50's branch byte, and 001D8FD0 alone
    ctx = C.u32(first.ram, L.CTX_PTR)
    for cap in (first, later):
        for off in (0xA0, 0xC0, 0xFF):
            expect(ctx_problems(K, [ram_copy(cap, 'ctx', [flip(cap.ram, ctx + off)])])[0], f'{cap.name} ctx +{off:#x}')
    expect_text(ctx_problems(K, [ram_copy(later, 'c7', [flip(later.ram, E1.D_008106C7)])])[0], 'do not rebuild',
                'D_008106C7 set (001D1C50\'s other value)')
    alone = E1.ctx_rebuild(later, K.elf, setup=False)
    check(alone != bytes(later.ram[ctx + 0xA0:ctx + 0x100]),
          f'control: {later.name}: 001D8FD0 alone rebuilds the block (001D1C50 would be untested)')
    n += 1
    # cells.json against its own rows, an unrecorded capture, clean copies
    fake = ram_copy(first, 'unrecorded')
    fake.key = 'unrecorded'
    expect_text(cells_problems(K, cells, [fake], record), 'not recorded in cells.json', 'an unrecorded capture')
    bad = json.loads(json.dumps(record))
    bad['cells']['moved_uids'] = bad['cells']['moved_uids'][:-1]
    expect_text(cells_problems(K, cells, [first], bad), 'is not the union of its rows', 'cells.json moved_uids')
    clean = [ram_copy(c, f'clean {c.name}') for c in (first, later)]
    expect(cells_problems(K, cells, clean, record), 'clean copies (cells)', accept=True)
    expect(tables_problems(K, clean), 'clean copies (tables)', accept=True)
    # a bank record carrying a matrix slot (bit 3 of its w word), in the bank
    # file and in RAM alike, is reported by level_problems' guard
    real = A
    at = None
    for _i, o, units in L.bank_objects(lambda a, k: first.ram[a:a + k], base)[1:]:
        r = next((r for r in K.el.walk_records(first.ram[o + 0x40:o + 0x40 + L.UNIT * units]) if r), None)
        if r is not None:
            at = o + 0x40 + r[0] + 0x3C
            break
    tree = BUILD / f'slot-{os.getpid()}'
    (tree / 'sub0/level').mkdir(parents=True, exist_ok=True)
    zlinks = sorted((real / 'sub0/level').glob('*.emdl'))
    try:
        (tree / 'sub0/level/static_bank.emsc').write_bytes(mutate(bank, 20 + at - base, 0x08))
        for z in zlinks:
            (tree / f'sub0/level/{z.name}').symlink_to(z)
        A = tree
        got, _v = level_problems(K, [ram_copy(first, 'matrix slot', [flip(first.ram, at, 0x08)])])
    finally:
        A = real
        for q in [tree / f'sub0/level/{z.name}' for z in zlinks] + [tree / 'sub0/level/static_bank.emsc']:
            q.unlink(missing_ok=True)
        for q in (tree / 'sub0/level', tree / 'sub0', tree):
            q.rmdir()
    for zkey in [z for z in _ZONES if z != bank]:
        del _ZONES[zkey]
    expect_text(got, 'records with a matrix slot', 'a bank record with a matrix slot')
    return n


# ---------------------------------------------------------------------------


def main():
    need = [A / 'sub0/level/level.json', A / 'tables.json', A / 'sub0/cells.json', A / 'sub0/sfx/banks.json',
            C.ELF_PATH, A22.OVERLAY_PATH, C.ISO_PATH]
    missing = [str(p) for p in need if not p.exists()]
    if missing or not A22.ROUTE_A22.exists() or not A22.ARRIVAL.exists():
        print('area22 assets reference: SKIPPED, missing local inputs:', missing or [str(A22.ROUTE_A22)])
        return 0
    t0 = time.time()
    el = L.load_export_level()
    A22.configure()
    E1.install()
    every = A22.all_captures()
    excluded = A22.excluded_captures()
    check(sorted(n for n, _a in excluded) == ['a22_02_progression_exit', 'a22_s3_door6_back'],
          f'excluded captures {excluded}')
    caps = every if FULL else [c for c in every if c.name in QUICK]
    print(f'area22 assets reference ({MODE}): {len(caps)} of {len(every)} captures (all sub 0; excluded by area '
          f'byte: {", ".join(n for n, _a in excluded)})')
    K = SimpleNamespace(el=el, elf=C.read_elf(), first=every[0], top=C.iso_descriptor(C.AREA + 4))
    check(K.top is not None and C.sha(K.top) == DESCRIPTOR_SHA256,
          'INDEX.IDX sector 26 is not the pinned AREA22 descriptor')
    K.lmap, K.info = C.build_load_map(K.first)
    K.image = C.LoadedImage(K.lmap)
    if (STALE_SOURCE / 'eeMemory.bin').exists():
        with open(STALE_SOURCE / 'eeMemory.bin', 'rb') as f:
            f.seek(E1.D_0028A594)
            w594 = struct.unpack('<I', f.read(4))[0]
            f.seek(E1.D_0028A5A4)
            w5a4 = struct.unpack('<I', f.read(4))[0]
        check((w594, w5a4) == (STALE[E1.D_0028A594], STALE[E1.D_0028A5A4]),
              f'{STALE_SOURCE.name}: D_0028A594 / D_0028A5A4 {w594:#x} / {w5a4:#x} are not the AREA22 values')
    lib = T01.build_loaders()
    loaded, message_rc = check_loaders(lib, A)
    for name, ok in loaded.items():
        check(ok, f'port loader rejects {name}')
    diag = message_diagnostic(lib, (A / 'sub0/message_data.emmd').read_bytes())
    check(message_findings_ok(message_rc, diag), f'em_message_live_install: {message_rc}, diagnostic copy {diag}')
    print(f'  loaders: {sum(loaded.values())}/{len(loaded)} files accepted by the port loaders; finding: '
          f'em_message_live_install(sub0/message_data.emmd) = {message_rc} (no area records; the diagnostic '
          f'copy with one: {diag})')

    problems, V = run_checks(K, caps)
    for p in problems:
        check(False, p)
    lv = V.get('level', {})
    print(f"  load map + level: {lv.get('objects')} bank objects -> {lv.get('zones')} zone EMDLs; "
          f"{lv.get('kicks')} level kicks inside them, {lv.get('dynamic_kicks')} dynamic kicks")
    print(f'  collision, cells, tables, sfx: every comparison over the run\'s captures ({len(problems)} problems)')
    if V['room']:
        print(f"  ctx +0xA0..+0xFF: original 001D8FD0 + 001D1C50 rebuilt the captured bytes in {len(V['room'])} "
              f"capture(s); 001D1C50 changed {sorted({r['bytes_001d1c50_changes'] for r in V['room']})} bytes")
    if FAILS:
        print(f'  canary and controls: skipped, {len(FAILS)} check(s) already failed')
    else:
        print(f'  canary: {canary(K, lib, caps[0])} sections each reported their planted difference')
        if not FAILS:
            print(f'  controls: {controls(K, caps, lib)} changed inputs, each caught (or accepted where marked)')
    print(f'area22 assets reference: {"FAIL" if FAILS else "PASS"} ({len(FAILS)} failures, '
          f'{time.time() - t0:.1f} s)')
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
