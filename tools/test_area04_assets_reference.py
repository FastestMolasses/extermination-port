#!/usr/bin/env python3
"""Check the exported AREA04 assets (assets/area04/, docs/AREA04_ASSETS.md)
against the recorded original captures, and load each one through the port's
own loader.

Inputs (all local, all the user's): the assets written by
tools/export_area04_level.py, export_area04_tables.py and export_area04_sfx.py,
the extracted disc files, the pinned boot ELF and AREA04 overlay, and the
AREA04 captures (the arrival ../Extermination/build/s87/route_a02/
a02_05_progression_exit/ and build/s87/route_a04/a04_00..a04_04,
a04_s0..a04_s3; every one is sub 0). Missing inputs print SKIPPED and exit 0.

The comparators are the AREA01 checker's (tools/test_area01_assets_reference.py,
imported unchanged, with export_area01_common pointed at AREA04 and its
module constants GRID / BUILD / OVERLAY_SHA256 set for AREA04):
level_bank_problems (the static bank and the dynamic list), zone_problems,
emsc_header_problems, gs_captures, gs_state_problems, emcl_equal,
grid_problems, hulls_inside_table, roster_image / roster_equal /
roster_nodes, emsp_problems with spawn_layout / doors_layout,
messages_problems, world_models_problems, registry_from_ram,
load_map_problems, build_loaders. The directory derivation is
export_area01_level.verify_cell_directory with AREA04's owner_matrix
(export_area04_level). AREA04's own checks are written here: the sub-1
nested block against RAM, the [47] model-kind rule for the placement nodes
(its committed C rewrites +0x0D), the run-time words of the script and
overlay-data windows, the model bindings, and the sound bindings.

Checks (every comparison is exact):
  load map  in address order, no overlap, the same map from every
            capture's descriptors; every mapped disc byte equals RAM except
            the cell directory's own bytes (checked below); the sub-1 nested
            files do not fit RAM
  level     sub0/level/static_bank.emsc and dynamic_objects.emsc equal RAM
            at D_0028A5A0 / D_0028A5A4 over their extents; every textured
            level kick REFs a bank object with the class-0 GS state, every
            kernel-0x00237450 kick REFs a dynamic-list entry; each zone EMDL
            equals a rebuild from the bank, byte for byte, and its texels the
            GS freeze decode of every capture
  emcl      sub0/area04.emcl equals the RAM grid rebuild (emcl_from_ram) in
            every capture; D_0028A598 pinned, the grid block inside the load
            map and outside the directory allowance
  cells     sub0/area04_cells.bin equals the disc bytes and ends where the
            directory ends; every byte of every capture's directory: the
            disc, or the ORIGINAL 001A2370 run for the live owner's matrix;
            the moved set per capture equals cells.json; the 001A2370
            call sites of the ELF and the module equal RETRANSFORM_SITES
            (so owner_matrix names every caller) and no capture has a live
            0x219870 node (its matrix is a scratchpad copy)
  tables    sub0/roster.emro = the rebuild by the original's walks over the
            pinned ELF + overlay and over every capture's RAM, (live, at
            rest) per capture pinned, every live placement node keeps its
            copied fields (+0x0D of 0x823EE0 by its C's rule); both EMSP
            files (window set from the ELF walk, every window against RAM);
            D_008106C8 = +0x1C of the current spawn record; scripts and
            overlay data: extent, bytes = the pinned module, RAM equal except
            the run-time words, each in a reached chain record, the set per
            capture = tables.json; the chain set = the C's func_001BA1A0
            data symbols = tables.json's chains; sub0/message_data.emmd (every byte);
            world models and every model owner's +0x44 = 001C6120(table,
            +0x0D) with its bone count and bone slots, the bound count pinned
  sfx       sub0/sfx/area04_banks.bin = upload section 0's container on the
            disc; the capture bindings pinned (every capture must bind the
            same banks); sub0/sfx/sfx_registry.emsr =
            the RAM re-derivation of every capture
  ctx       ORIGINAL 001D8FD0 rebuilds ctx +0xA0..+0xFF from room entry
            0x0400 (flipping the entry changes it)
  loaders   each file through its port loader (compiled privately from src/)
  canary    run_checks again over [a capture, a planted copy of it] and a
            copy of the export tree with file plants; each section must
            report its plant for the copy and stay silent for the original;
            every port loader must refuse a broken copy of its file
  controls  a changed input per comparator, file side and RAM side, and the
            accept cases listed in docs/AREA04_ASSETS.md
Default: 3 captures (the arrival, a04_02, a04_s2); EM_TEST_FULL=1: all 10.
"""
from __future__ import annotations

import contextlib
import copy
import ctypes as CT
import io
import json
import os
import struct
import sys
import time
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area04_common as A4  # noqa: E402
import export_area04_level as E1  # noqa: E402
import export_area04_tables as E2  # noqa: E402
import export_area04_sfx as E3  # noqa: E402

C = A4.configure(0)
E1.install()
import test_area01_assets_reference as T01  # noqa: E402
from reference_mode import FULL, MODE  # noqa: E402

L, T, S = E1.L, E2.T, E3.S
A = Path(os.environ.get('EM_AREA04_ASSETS') or A4.OUT).resolve()
BUILD = C.ROOT / 'build/area04/assets/test'
CANARY = BUILD / f'canary-{os.getpid()}'
QUICK = ('a02_05_progression_exit', 'a04_02_console', 'a04_s2_pickup')
T01.BUILD = BUILD
T01.OVERLAY_SHA256 = A4.OVERLAY_SHA256
# D_0028A598, measured in every capture (chunk08.n0/f13_id8e + 0x1800); as
# for AREA01 / AREA00 / AREA02 the code that fills it at load is not
# identified, so the measured address is pinned and the grid block must
# lie in the map
GRID = 0x1999C80
T01.GRID = GRID
FAILS = T01.FAILS
check = T01.check

# (live, at the record's position and rotation) placement nodes per capture
# (T01.roster_nodes); measured, asserted exactly
ROSTER_LIVE = {'a02_05_progression_exit': (89, 89), 'a04_00_door45_event': (87, 87),
               'a04_01_door40': (87, 87), 'a04_02_console': (87, 86), 'a04_03_back_to_hall': (87, 86),
               'a04_04_conveyor': (87, 86), 'a04_s0_door45_locked': (87, 87), 'a04_s1_reel_blocks': (87, 87),
               'a04_s2_pickup': (87, 87), 'a04_s3_door42_locked': (87, 87)}
# model owners bound to a bank model per capture; asserted exactly
MODEL_OWNERS = {'a02_05_progression_exit': 74, 'a04_00_door45_event': 74, 'a04_01_door40': 74,
                'a04_02_console': 73, 'a04_03_back_to_hall': 73, 'a04_04_conveyor': 73,
                'a04_s0_door45_locked': 74, 'a04_s1_reel_blocks': 74, 'a04_s2_pickup': 73,
                'a04_s3_door42_locked': 74}
# the capture bindings, (group, slot) -> (container, row); 'area' = the
# sub's area04_banks.bin; and the refused slots
BINDING = {(1, 0): ('global', 0), (1, 1): ('global', 1), (1, 2): ('global', 2),
           (2, 0): ('area', 0), (4, 0): ('area', 1)}
REFUSED = {(3, 0), (4, 1)}
SPAWN_ROWS = E2.SPAWN_SUB0
DOOR_ROW = E2.DOOR_ROW
CANARY_TEST_1 = 0x8153A0      # where the level kicks take TEST_1 (0x5000D), as in AREA01 / AREA00 / AREA02
# [47]'s behaviour (func_overlay_AREA04_00823EA0, byte-identical C) rewrites
# its +0x0D from the 8-byte table 0x827530 indexed by D_00810701: the
# first kind (+0) in state 0 while D_00810764 != 0xFF, the second kind (+4)
# when sub-state 1 leaves for state 2
KIND_OWNER, KIND_TABLE = 0x823EE0, 0x827530


def key(cap):
    """The capture name the pinned tables use (a planted copy keeps its source's)."""
    return getattr(cap, 'key', cap.name)


def sub_path(rel):
    return A / 'sub0' / rel


# ---------------------------------------------------------------------------
# Load map


def load_problems(K, caps, cells_len):
    out = list(T01.load_map_problems(K.lmap))
    image = K.image
    table = C.u32(K.first.spad, L.SPAD_CELLS)
    for cap in caps:
        try:
            A4.configure(0)
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
        # the sub-1 nested files against this RAM
        nested = [m for m in K.other.map if '.n' in m[4]]
        if all(K.other.read(a, n) == cap.ram[a:a + n] for a, _p, _o, n, _l in nested):
            out.append(f'{cap.name}: the sub-1 load also fits RAM')
    return out


# ---------------------------------------------------------------------------
# Level


_ZONES = {}


def level_files_problems(bank_img, dyn_img, caps):
    """T01.level_bank_problems (the static bank and the dynamic list against
    RAM over their extents); a header whose base names no RAM is a problem,
    not a crash."""
    try:
        return T01.level_bank_problems(bank_img, dyn_img, caps)
    except (struct.error, IndexError) as error:
        return [f'static bank / dynamic list: {error!r}']


def level_problems(K, caps):
    """The static bank and the dynamic list (T01.level_bank_problems), the
    kicks and the zone EMDLs. Returns (problems, summary)."""
    out, el, image = [], K.el, K.image
    lvl = sub_path('level')
    bank_img = (lvl / 'static_bank.emsc').read_bytes()
    dyn_img = (lvl / 'dynamic_objects.emsc').read_bytes()
    out += level_files_problems(bank_img, dyn_img, caps)
    base, _entry, length = struct.unpack_from('<3I', bank_img, 8)
    bank = bank_img[20:20 + length]
    dbase, _e, dlen = struct.unpack_from('<3I', dyn_img, 8)
    read = lambda a, n: bank[a - base:a - base + n]
    try:
        objects = L.bank_objects(read, base)
        dyn = L.dynamic_entries(lambda a, n: dyn_img[20 + a - dbase:20 + a - dbase + n], dbase)
        kicks, states, _touched = L.check_kicks(caps, objects, dyn)
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
    return out, dict(objects=len(objects), zones=len(files), dynamic_entries=len(dyn),
                     kicks=sum(k['level_kicks'] for k in kicks), dynamic_kicks=sum(k['dynamic_kicks'] for k in kicks))


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
            out.append(f'{cap.name}: sub0/area04.emcl differs from the RAM grid rebuild: {where}')
    return out


def cells_problems(K, cells, caps, record):
    """The directory file against the disc, and every capture's RAM
    directory through export_area01_level.verify_cell_directory (the
    ORIGINAL 001A2370 for the hulls, AREA04's owner_matrix). `record` =
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
        out.append(f'sub0/area04_cells.bin differs from the disc bytes at +{k:#x}')
    try:
        _count, _hulls, size = L.cell_directory(cells, 0)
        if size != len(cells):
            out.append(f'sub0/area04_cells.bin is {len(cells)} bytes, the directory {size}')
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


# every call of 001A2370 in the pinned boot ELF and the AREA04 overlay (jal;
# no j, no address-as-data word, no lui/addiu pair builds it): the drum
# 0x156620 (0x156EF0), the pickup 0x219550 (0x219668), 0x219F50 (0x21A104,
# its matrix is the scratchpad copy 0x700036A0; 0x219F50's only caller is
# 0x219870, at 0x2198E4, and no AREA04 capture has a live 0x219870 node),
# and the three overlay owners (runtime addresses): 0x825510 x3, 0x825B00
# x2, 0x8260C0 x3
RETRANSFORM_SITES = (0x156EF0, 0x219668, 0x21A104, 0x8255E8, 0x8256E4, 0x825724, 0x825BAC, 0x825D10,
                     0x82616C, 0x826514, 0x826578)
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
        for _s, a in E2.T.pool_nodes(cap.ram):
            if C.u32(cap.ram, a + 0x10) in SCRATCH_MATRIX_CALLERS:
                out.append(f'{cap.name}: live node {a:#x} of {C.u32(cap.ram, a + 0x10):#x}, whose 001A2370 '
                           f'matrix is a scratchpad copy owner_matrix does not model')
    return out


C_CALL = r'func_001BA1A0\(\s*\w+\s*,\s*\(?[^,()]*\)?\s*&?D_overlay_AREA04_([0-9A-Fa-f]{8})'


def c_script_entries():
    """Every chain the AREA04 overlay's committed C starts: the data symbol
    of each func_001BA1A0 call in src/overlays/AREA04/ (the data names are
    runtime addresses)."""
    import re
    out = set()
    for f in sorted((C.DECOMP / 'src/overlays/AREA04').glob('*.c')):
        out.update(int(m, 16) for m in re.findall(C_CALL, f.read_text()))
    return out


def script_entry_problems(entries, record_chains, c_entries):
    """The chain set the checker walks (E2.SCRIPT_ENTRIES) equals the C's
    and tables.json's."""
    out = []
    if set(entries) != c_entries:
        out.append(f'script entries {sorted(hex(e) for e in set(entries) ^ c_entries)} differ from the C')
    if {int(k, 16) for k in record_chains} != c_entries:
        out.append('tables.json scripts chains differ from the C')
    return out


# ---------------------------------------------------------------------------
# Tables


def kind_allowed(ram, node, rec_param):
    """The +0x0D values [47]'s C can leave in `node` (KIND_OWNER): in state
    0 or 1 the first kind 0x827530[8 * sub] (state 0 sets it while
    D_00810764 != 0xFF, and state 1 does not change it), in state 2 the
    record's value (state 0 went straight to 2), the first kind (sub-state
    0 left on D_00810764 == 0xFF) or the second kind 0x827534[8 * sub]
    (sub-state 1's switch). State 3 is set by other code and its only
    action is 001AFC10 (which clears +0x0C..+0x0F and frees the node), so
    a live node caught in state 3 still carries the kind of the state it
    left: any of the three. Other state values have no case in the C. The
    kind table is read from RAM (the overlay-data window check compares it
    with the disc)."""
    sub = ram[0x810701]
    first, second = ram[KIND_TABLE + 8 * sub], ram[KIND_TABLE + 4 + 8 * sub]
    state = ram[node + 4]
    if state == 0:
        return {rec_param, first}
    if state == 1:
        return {first}
    if state in (2, 3):
        return {rec_param, first, second}
    return set()


def placements_at(blob):
    """The offset of the placement records in a roster blob."""
    groups = C.u16(blob, 0x0A)
    return 0x18 + 8 * groups + sum(0x2C * struct.unpack_from('<II', blob, 0x18 + 8 * g)[1] for g in range(groups))


def roster_node_problems(blob, ram):
    """(problems, (live, placed)): T01.roster_nodes' pairs, each live
    placement node keeping +0x03 and +0x54, and +0x0D as the record gives
    it, or, for KIND_OWNER, a value kind_allowed permits."""
    _ok, live, pairs = T01.roster_nodes(T, blob, ram)
    at = placements_at(blob)
    out = []
    for i, a in pairs:
        rec = blob[at + 0x28 * i:at + 0x28 * (i + 1)]
        f, _pos, _rot = T.spawn_fields_placement(rec, i)
        for off in (0x03, 0x54):
            if ram[a + off:a + off + len(f[off])] != f[off]:
                out.append(f'placement[{i}] node {a:#x} +{off:#x}')
        if C.u32(ram, a + 0x10) == KIND_OWNER:
            if ram[a + 0x0D] not in kind_allowed(ram, a, rec[4]):
                out.append(f'placement[{i}] node {a:#x} +0xd {ram[a + 0x0D]} outside [47]\'s kinds')
        elif ram[a + 0x0D] != rec[4]:
            out.append(f'placement[{i}] node {a:#x} +0xd')
    return out, live


def roster_problems(roster, caps):
    out = []
    try:
        disc = T01.roster_image(T01.disc_reader())
        if roster != disc:
            out.append('sub0/roster.emro differs from the rebuild from the disc')
        for cap in caps:
            if not T01.roster_equal(disc, cap.ram):
                out.append(f'{cap.name}: roster records differ from RAM')
            lost, live = roster_node_problems(disc, cap.ram)
            if lost:
                out.append(f'{cap.name}: a live placement node lost a copied field ({lost[:2]})')
            if live != ROSTER_LIVE.get(key(cap)):
                out.append(f'{cap.name}: placement nodes live / at the record position {live} != '
                           f'{ROSTER_LIVE.get(key(cap))}')
    except (ValueError, IndexError, struct.error) as error:
        out.append(f'roster: {error!r}')
    return out


def window_problems(label, blob, window, disc, caps, record, records):
    """An EMSC data window: header, extent = `window`, bytes = the module,
    and against every capture's RAM except its run-time words, which must
    each lie in a reached chain record (E2.run_time_words) and equal
    tables.json's list."""
    out = T01.emsc_header_problems(label, blob)
    base, d = T01.emsc_window(blob)
    if (base, len(d)) != window:
        return out + [f'{label}: window {base:#x}+{len(d):#x}, not {window[0]:#x}+{window[1]:#x}']
    if d != disc:
        out.append(f'{label} differs from the disc overlay')
    for cap in caps:
        words, problems = E2.run_time_words(disc, window[0], cap.ram, records)
        out += [f'{cap.name}: {label}: {p}' for p in problems]
        want = record.get(key(cap), [])
        if [hex(a) for a in words] != want:
            out.append(f'{cap.name}: {label} run-time words {len(words)} != tables.json {len(want)}')
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
    """D_008106C8 = word +0x1C of the capture's current spawn record (area 4,
    sub D_00810701, entry D_00810702), the records read from the pinned ELF."""
    out = []
    dread = T01.disc_reader()
    table = C.u32(dread(T.D_0024D650 + 4 * C.AREA, 4), 0)
    for cap in caps:
        sub, room = cap.ram[0x810701], cap.ram[0x810702]
        if sub != 0:
            out.append(f'{cap.name}: sub {sub}, not the captured sub 0')
            continue
        entries = C.u32(dread(table, 4), 0)
        count = (C.u32(dread(table + 4, 4), 0) - entries) // 0x30
        if room >= count:
            out.append(f'{cap.name}: spawn entry {room} outside the {count} sub-0 records')
            continue
        want = C.u32(dread(entries + 0x30 * room + 0x1C, 4), 0)
        got = C.u32(cap.ram, T.D_008106C8)
        if got != want:
            out.append(f'{cap.name}: D_008106C8 {got:#x} != +0x1C of spawn record {room} ({want:#x})')
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
        out += script_entry_problems(E2.SCRIPT_ENTRIES, rec['scripts']['chains'], c_script_entries())
        ov = A4.read_overlay()
        olo, ohi = E2.E02.overlay_data_window(ov)
        odisc = ov[olo - C.OVERLAY_ARENA:ohi - C.OVERLAY_ARENA]
        records = E2.chain_records(odisc, olo)
        slo, shi = E2.scripts_window(ov)
        sdisc = ov[slo - C.OVERLAY_ARENA:shi - C.OVERLAY_ARENA]
        out += window_problems('scripts', read('area04_scripts/scripts.emsc'), (slo, shi - slo), sdisc, caps,
                               rec['scripts']['run_time_words'], records)
        out += window_problems('overlay data', read('overlay_data.emsc'), (olo, ohi - olo), odisc, caps,
                               rec['overlay_data']['run_time_words'], records)
        out += T01.messages_problems(read('sub0/message_data.emmd'), caps, K.elf)[0]
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
        A4.configure(0)
        _label, disc, _parsed, _names, first = E3.E02.area_container(K.first)
        if banks != disc:
            k = next((j for j in range(min(len(banks), len(disc))) if banks[j] != disc[j]), min(len(banks), len(disc)))
            out.append(f'sub0/sfx/area04_banks.bin differs from the disc container at +{k:#x} '
                       f'({len(banks)} / {len(disc)})')
        total = C.u32(banks, 0) if len(banks) >= 4 else -1
        if len(banks) != total or total > first[1]:
            out.append(f'sub0/sfx/area04_banks.bin: {len(banks)} bytes, container total {total:#x}')
        parsed = S.X.A.parse_container(banks)
        if parsed is None:
            return out + ['sub0/sfx/area04_banks.bin is not an SShd container']
        name = E3.container_name(0)
        containers = {gname: (gdata, S.X.A.parse_container(gdata)), name: (banks, parsed)}
        bound, refused, _rep = S.bindings_from_captures(caps, containers)
        got = {k: ('global' if v[0][0] == gname else 'area', v[0][1]) for k, v in bound.items()}
        if binding_problem(got, refused):
            return out + [f'bank bindings {got}, refused {sorted(refused)}']
        for cap in caps:
            # the samples from the disc container (the file is compared with it above)
            want, _counts, _n, _e = T01.registry_from_ram(cap.ram, bound, {gname: gdata, name: disc})
            if registry != want:
                k = next((j for j in range(min(len(registry), len(want))) if registry[j] != want[j]),
                         min(len(registry), len(want)))
                out.append(f'{cap.name}: sub0/sfx/sfx_registry.emsr differs from the RAM re-derivation at +{k:#x}')
    except (SystemExit, ValueError, TypeError, KeyError, IndexError, StopIteration, struct.error) as error:
        out.append(f'sfx: {error!r}')
    return out


# ---------------------------------------------------------------------------


def run_checks(K, caps):
    """Every real check over the export tree A; returns (problems, summary)."""
    P, V = [], {}
    cells = sub_path('area04_cells.bin').read_bytes()
    record = json.loads(sub_path('cells.json').read_text())
    P += load_problems(K, caps, len(cells))
    p, V['level'] = level_problems(K, caps)
    P += p
    P += emcl_problems(K, sub_path('area04.emcl').read_bytes(), caps, len(cells))
    P += cells_problems(K, cells, caps, record)
    P += sfx_problems(K, caps, sub_path('sfx/area04_banks.bin').read_bytes(),
                      sub_path('sfx/sfx_registry.emsr').read_bytes())
    P += tables_problems(K, caps)
    P += retransform_census_problems(K.elf, A4.read_overlay(), caps)
    V['room'] = []
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            V['room'] += L.check_ctx_room_block(caps, K.elf)
    except SystemExit as error:
        P.append(f'ctx room block: {error}')
    return P, V


# ---------------------------------------------------------------------------
# Port loaders


def doors_loader_ok(lib, path):
    """em_spawn_table_load accepts the door file and serves both windows
    the door code reads: the pointer array 0x24E140 (0x17 words) and the
    row DOOR_ROW (0x28 bytes)."""
    lib.em_spawn_table_read.restype = CT.c_void_p
    lib.em_spawn_table_read.argtypes = [CT.c_void_p, CT.c_uint32, CT.c_uint32]
    lib.em_spawn_table_load.restype = CT.c_int
    doors = CT.create_string_buffer(16 << 20)
    ok = lib.em_spawn_table_load(doors, str(path).encode()) == 0
    return ok and bool(lib.em_spawn_table_read(doors, 0x24E140, 4 * 0x17)) and \
        bool(lib.em_spawn_table_read(doors, DOOR_ROW, 0x28))


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
    for rel in ('area04_scripts/scripts.emsc', 'overlay_data.emsc', 'sub0/level/static_bank.emsc',
                'sub0/level/dynamic_objects.emsc'):
        out[rel] = rc('em_script_image_load', big(), p(rel)) == 1
    wm = (tree / 'sub0/world_models.emwm').read_bytes()
    out['sub0/world models'] = rc('em_world_models_parse', big(), wm, CT.c_size_t(len(wm))) == 0
    out['sub0/emcl'] = rc('em_collision_load', big(), p('sub0/area04.emcl')) == 0
    out['sub0/roster.emro'] = rc('em_actor_roster_load', big(), p('sub0/roster.emro')) == 0
    for z in sorted(str(x) for x in (tree / 'sub0/level').glob('*.emdl')):
        out[f'sub0/{Path(z).name}'] = rc('em_model_load', big(), z.encode()) == 0
    out['sub0/sfx registry'] = rc('em_sfx_registry_load', big(), p('sub0/sfx/sfx_registry.emsr')) == 1
    with T01.quiet_fds():
        out['sub0/message data'] = rc('em_message_live_install', p('sub0/message_data.emmd')) == 1
    cells = rc('em_actor_cells_load', big(), p('sub0/area04_cells.bin'))
    return out, cells


def cells_findings_ok(rc, diag):
    """The loader verdicts docs/AREA04_ASSETS.md records: the directory is
    refused (uid 0's word carries bit 29, as in AREA01 / AREA00 / AREA02)
    and loads once bit 29 is cleared in every uid word."""
    return rc == -1 and diag == 0


def cells_diagnostic(lib, tree, cells_bytes=None):
    """em_actor_cells_load over a copy of sub0/area04_cells.bin with bit 29
    cleared in every uid word (nothing else changed)."""
    BUILD.mkdir(parents=True, exist_ok=True)
    g = lib.em_actor_cells_load
    g.restype = CT.c_int
    cells = bytearray(cells_bytes if cells_bytes is not None else (tree / 'sub0/area04_cells.bin').read_bytes())
    for uid in range(C.u32(cells, 0)):
        struct.pack_into('<I', cells, 4 + 4 * uid, C.u32(cells, 4 + 4 * uid) & ~0x20000000)
    path = BUILD / f'diag-{os.getpid()}.bin'
    path.write_bytes(bytes(cells))
    try:
        return g(CT.create_string_buffer(16 << 20), str(path).encode())
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
    for _s, a in E2.T.pool_nodes(ram):
        if C.u32(ram, a + 0x10) == behaviour and (index is None or ram[a + 0x9A] == index) and \
                (uid is None or ram[a + 0x0F] == uid):
            return a
    return None


def gap_word(odisc, olo, records):
    """The first overlay-data word in no reached chain record."""
    return next(a for a in range(olo, olo + len(odisc), 4) if not any(r <= a < r + 0x40 for r in records))


def canary_plants(K, cap, tree, tag):
    """RAM plants for the canary copy of `cap` -> (copy, {section: text})."""
    ram = cap.ram
    edits, want = [], {}
    bank = sub_path('level/static_bank.emsc').read_bytes()
    base, _e, length = struct.unpack_from('<3I', bank, 8)
    edits.append(flip(ram, base + length - 1))
    want['load map'] = f'{tag}: load map differs from RAM'
    want['static bank'] = f'{tag}: static bank differs from RAM'
    objects = [base + (C.s32(ram, base + 4 + 4 * i) >> 2 << 2) for i in range(C.u32(ram, base))]
    last = max(objects, key=lambda o: o + 0x820 * C.u32(ram, o))
    edits.append((last, bytes([ram[last] + 1])))
    want['static bank extent'] = f'{tag}: static bank length'
    edits.append(flip(ram, E1.D_0028A5A0 + 2))
    want['D_0028A5A0'] = f'{tag}: D_0028A5A0'
    dyn = sub_path('level/dynamic_objects.emsc').read_bytes()
    dbase, _de, dlen = struct.unpack_from('<3I', dyn, 8)
    edits.append(flip(ram, dbase))                 # the list's count word (its entries are REF'd VIF data)
    want['dynamic list'] = f'{tag}: dynamic list differs from RAM'
    want['dynamic list extent'] = f'{tag}: dynamic list length'
    edits.append(flip(ram, CANARY_TEST_1, 2))      # TEST_1 of the level kicks' state
    want['GS state'] = 'level GS state'
    ctx = C.u32(ram, L.CTX_PTR)
    edits.append(flip(ram, ctx + 0xA4))
    want['ctx'] = f'ctx room block: {tag}:'
    table = C.u32(cap.spad, L.SPAD_CELLS)
    cells = sub_path('area04_cells.bin').read_bytes()
    _count, hulls, _size = L.cell_directory(cells, 0)
    s0, _e0, _f0 = hulls[min(hulls)]
    edits.append(flip(ram, table + s0 + 0x20))
    want['cell hull'] = f'{tag}: hull {min(hulls)} differs'
    edits.append((table + 8 + 3, bytes([ram[table + 8 + 3] ^ 0x40])))    # uid 1's word, bit 30
    want['uid word'] = f'{tag}: directory bytes differ at'
    grid = GRID
    edits.append(flip(ram, grid + C.u32(ram, grid) + 1))
    want['EMCL'] = f'{tag}: sub0/area04.emcl differs from the RAM grid rebuild'
    roster = sub_path('roster.emro').read_bytes()
    paddr = C.u32(roster, 0x10)
    edits.append(flip(ram, paddr + 0x28 * C.u32(roster, 0x0C) - 1))
    want['roster'] = f'{tag}: roster records differ from RAM'
    _ok, _live, pairs = T01.roster_nodes(T, roster, ram)
    edits.append(flip(ram, pairs[0][1] + 0x03))
    want['placement node fields'] = f'{tag}: a live placement node lost a copied field'
    edits.append(flip(ram, DOOR_ROW + 0x27))
    want['door windows'] = f'{tag}: door_destinations.emsp window {DOOR_ROW:#x} differs'
    rows, n = SPAWN_ROWS
    edits.append(flip(ram, rows + n * 0x30 - 1))   # the last byte of the sub-0 spawn rows
    want['spawn windows'] = f'{tag}: spawn_table.emsp window'
    edits.append(flip(ram, min(E2.SCRIPT_ENTRIES) + 0x10))   # word +0x10 of the first chain's first record
    want['script words'] = f'{tag}: scripts run-time words'
    ov = A4.read_overlay()
    olo, ohi = E2.E02.overlay_data_window(ov)
    odisc = ov[olo - C.OVERLAY_ARENA:ohi - C.OVERLAY_ARENA]
    gap = gap_word(odisc, olo, E2.chain_records(odisc, olo))
    edits.append(flip(ram, gap))
    want['overlay gap word'] = f'{tag}: overlay data: {gap:#x}: rewritten word outside every reached chain record'
    area_records = C.u32(K.elf, T01.MSG_TABLES + 4 * (C.AREA + 1) - C.ELF_VADDR + C.ELF_OFFSET)
    edits.append(flip(ram, area_records + 5))
    want['message records'] = f'{tag}: message records differ'
    wm = sub_path('world_models.emwm').read_bytes()
    edits.append(flip(ram, C.u32(wm, 8) + C.u32(wm, 12) - 1))
    want['world models'] = f'{tag}: world model bank differs'
    edits.append(flip(ram, T01.LADDER + 2 * 0x40))
    want['registry'] = f'{tag}: sub0/sfx/sfx_registry.emsr differs'
    # the matrix of the first hull this capture shows re-transformed by a
    # live overlay owner (cells.json), one byte flipped
    rec = json.loads(sub_path('cells.json').read_text())
    moved = next((r['moved_hulls'] for r in rec['cells']['captures'] if r['capture'] == key(cap)), [])
    for uid in moved:
        owner = next((a for _s, a in E2.T.pool_nodes(ram) if ram[a + 0x0F] == int(uid)
                      and C.u32(ram, a + 0x10) in E1.HULL_OWNERS), None)
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
    plant('sub0/area04_cells.bin', mutate((real / 'sub0/area04_cells.bin').read_bytes(), -1))
    want['cells file'] = 'sub0/area04_cells.bin differs from the disc bytes'
    plant('sub0/sfx/area04_banks.bin', mutate((real / 'sub0/sfx/area04_banks.bin').read_bytes(), 0x20000))
    want['sound bank'] = 'sub0/sfx/area04_banks.bin differs from the disc container'
    plant('sub0/roster.emro', mutate((real / 'sub0/roster.emro').read_bytes(), 0x09))
    want['roster file'] = 'sub0/roster.emro differs from the rebuild from the disc'
    sc = (real / 'area04_scripts/scripts.emsc').read_bytes()
    plant('area04_scripts/scripts.emsc', mutate(sc, len(sc) - 1))
    want['scripts file'] = 'scripts differs from the disc overlay'
    tab = json.loads((real / 'tables.json').read_text())
    words = tab['overlay_data']['run_time_words'].get(key(cap), [])
    tab['overlay_data']['run_time_words'][key(cap)] = words[1:] if words else ['0x826600']
    plant('tables.json', json.dumps(tab).encode())
    want['overlay run-time set'] = f'{cap.name}: overlay data run-time words'
    dyn = (real / 'sub0/level/dynamic_objects.emsc').read_bytes()
    n = len(FAILS)
    try:
        A = tree
        with contextlib.redirect_stdout(io.StringIO()):
            got, _v = run_checks(K, [cap, fake])
        # the zone file itself and the dynamic-list file (their own pass: a
        # structure difference stops zone_problems before the texels, which
        # the pass above plants)
        zbuf = (real / f'sub0/level/{zones[-1]}').read_bytes()
        plant(f'sub0/level/{zones[-1]}', mutate(zbuf, T01.emdl_parts(zbuf)['verts'] + 1))
        want['zone EMDL'] = f'sub0/{zones[-1]} differs from a rebuild'
        plant('sub0/level/dynamic_objects.emsc', mutate(dyn, 20 + 0x40))
        want['dynamic list file'] = f'{cap.name}: dynamic list differs from RAM'
        (tree / 'sub0/level/00_a_extra.emdl').unlink()
        got2 = level_problems(K, [cap])[0]
        loader_want = plant_loader_canary(tree, plant)
        with contextlib.redirect_stdout(io.StringIO()), T01.quiet_fds():
            loaded, _cells = check_loaders(lib, tree)
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
    the sub-0 rows (its window cut short); the door file keeps only the
    pointer array; an EMSC keeps its header with a 32-byte window; every
    other file is cut to its first 16 bytes."""
    spawn = T01.emsp_windows((tree / 'spawn_table.emsp').read_bytes())
    end0 = SPAWN_ROWS[0] + 0x30 * SPAWN_ROWS[1]
    plant('spawn_table.emsp', T01.emsp_file([(a, d[:end0 - 1 - a] if a <= SPAWN_ROWS[0] < a + len(d) else d)
                                             for a, d in spawn]))
    doors = T01.emsp_windows((tree / 'door_destinations.emsp').read_bytes())
    plant('door_destinations.emsp', T01.emsp_file([(a, d) for a, d in doors if a == T01.D_0024E140]))
    names = ['spawn', 'doors']
    for rel in ('area04_scripts/scripts.emsc', 'overlay_data.emsc', 'sub0/level/static_bank.emsc',
                'sub0/level/dynamic_objects.emsc'):
        blob = (tree / rel).read_bytes()
        base, entry = struct.unpack_from('<2I', blob, 8)
        plant(rel, C.emsc(base, blob[20:52], entry))
        names.append(rel)
    for rel, name in (('sub0/message_data.emmd', 'sub0/message data'), ('sub0/roster.emro', 'sub0/roster.emro'),
                      ('sub0/world_models.emwm', 'sub0/world models'), ('sub0/area04.emcl', 'sub0/emcl'),
                      ('sub0/sfx/sfx_registry.emsr', 'sub0/sfx registry')):
        plant(rel, (tree / rel).read_bytes()[:16])
        names.append(name)
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
    moved = by_name.get('a04_02_console') or caps[-1]              # the reel rolled down and was placed
    record = json.loads((A / 'sub0/cells.json').read_text())
    cells = (A / 'sub0/area04_cells.bin').read_bytes()
    count = C.u32(cells, 0)
    table = C.u32(first.spad, L.SPAD_CELLS)
    _c, hulls, _s = L.cell_directory(cells, 0)
    # cells: the file side
    for at in (0, 4, 4 + 4 * count, len(cells) - 1):
        expect(cells_problems(K, mutate(cells, at), [first], record), f'cells file +{at:#x}')
    expect_text(cells_problems(K, cells + b'\0', [first], record), 'bytes, the directory', 'cells size check')
    # every moved hull's last byte, and each overlay owner's matrix
    for uid in record['cells']['moved_uids']:
        s, e, _f = hulls[int(uid)]
        fake = ram_copy(moved, f'hull {uid}', [flip(moved.ram, table + e - 1)])
        expect(L.verify_cell_directory(K.elf, cells, [fake])[1], f'moved hull {uid} last byte')
    tried = 0
    for behaviour in E1.NODE_D0_OWNERS:
        node = node_of(moved.ram, behaviour)
        if node is None or moved.ram[node + 0x0F] not in hulls:
            continue
        s, e, _f = hulls[moved.ram[node + 0x0F]]
        if moved.ram[table + s:table + e] == cells[s:e]:
            continue                   # this owner's hull is not moved here (the drums): nothing derives it
        tried += 1
        fake = ram_copy(moved, f'matrix {behaviour:#x}', [flip(moved.ram, node + 0xD0 + 0x32, 0x10)])
        expect(L.verify_cell_directory(K.elf, cells, [fake])[1], f'owner matrix of {behaviour:#x} (node + 0xD0)')
    check(tried == 4, f'control: {tried} owners with a moved hull, not the 3 overlay owners and the pickup')
    # the drums (0x156620 passes drum + 0xD0 to 001A2370 on its state-2
    # path). Their AREA04 hulls' first prim lacks the extended bit 0x800
    # that 001A2370 requires, so the ORIGINAL leaves every drum hull as it
    # is for any matrix: a knocked drum (matrix changed) keeps the disc
    # bytes. With the bit planted in a copy of the directory, the ORIGINAL
    # does move the hull, and verify_cell_directory accepts the result only
    # because owner_matrix names the drum; the same hull with the matrix at
    # rest is rejected.
    from test_coll_move_reference import FloatEE
    drums = [a for _s, a in E2.T.pool_nodes(moved.ram)
             if C.u32(moved.ram, a + 0x10) == E1.DRUM and moved.ram[a + 0x0F] in hulls]
    check(len(drums) == 5, f'control: {len(drums)} live drums owning a hull, not 5')

    def run_original(ram, directory, node):
        ee = FloatEE(K.elf, bytes(ram), moved.spad)
        ee.write(table, directory)
        ee.call(L.RETRANSFORM, (node, node + 0xD0))
        return ee.read(table, len(directory))
    for drum in drums:
        ds, _de, _f = hulls[moved.ram[drum + 0x0F]]
        kram = bytearray(moved.ram)
        kram[drum + 0xD0 + 0x32] ^= 0x10              # translation x changed
        check(C.u16(cells, ds + 0x18 + 4) & 0x800 == 0 and run_original(kram, cells, drum) == cells,
              f'control: drum {drum:#x}: its hull has no 0x800 bit and stays as on disc')
    drum = drums[0]
    ds, de, _f = hulls[moved.ram[drum + 0x0F]]
    ext = bytearray(cells)
    struct.pack_into('<H', ext, ds + 0x18 + 4, C.u16(cells, ds + 0x18 + 4) | 0x800)
    ext = bytes(ext)
    knock = [flip(moved.ram, drum + 0xD0 + 0x32, 0x10)]
    kram = bytearray(moved.ram)
    kram[knock[0][0]] = knock[0][1][0]
    got = run_original(kram, ext, drum)
    check(got[ds:de] != ext[ds:de] and got[:ds] == ext[:ds] and got[de:] == ext[de:],
          'control: with the 0x800 bit the knocked drum moves exactly its own hull')
    fake = ram_copy(moved, 'knocked drum', knock + [(table + ds, bytes(got[ds:de]))])
    expect(L.verify_cell_directory(K.elf, ext, [fake])[1], 'a knocked drum\'s extended hull (001A2370, drum + 0xD0)',
           accept=True)
    fake = ram_copy(moved, 'drum hull, matrix at rest', [(table + ds, bytes(got[ds:de]))])
    expect(L.verify_cell_directory(K.elf, ext, [fake])[1], 'a drum hull moved without its matrix')
    # a hull whose owner is freed: the orphan rule (no other capture derives
    # a different byte image)
    reel = node_of(moved.ram, 0x8260C0)
    fake = ram_copy(moved, 'reel freed', [(reel, b'\0')])
    expect_text(L.verify_cell_directory(K.elf, cells, [fake])[1], 'no live node owns it',
                'a moved hull whose owner is freed, with no derivation elsewhere')
    expect(L.verify_cell_directory(K.elf, cells, [moved, fake])[1], 'the freed reel\'s hull with a02 live copy',
           accept=True)
    # the uid words: bit 30 of uid 1 changed, and the 001A2370 code in RAM
    fake = ram_copy(moved, 'uid word', [(table + 8 + 3, bytes([moved.ram[table + 8 + 3] ^ 0x40]))])
    expect_text(L.verify_cell_directory(K.elf, cells, [fake])[1], 'directory bytes differ', 'uid 1 word bit 30')
    fake = ram_copy(moved, 'retransform code', [flip(moved.ram, L.RETRANSFORM + 0x767)])
    expect(L.verify_cell_directory(K.elf, cells, [fake])[1], '001A2370 code in RAM')
    # owner_matrix unit controls
    ram = bytearray(moved.ram)
    node = 0x1C00000
    for b, want in ((0x825510, node + 0xD0), (0x825B00, node + 0xD0), (0x8260C0, node + 0xD0),
                    (0x219550, node + 0xD0), (0x156620, node + 0xD0), (0x219870, None), (0x219F50, None),
                    (0x825880, None), (0x823B90, None), (0x826D40, None)):
        ram[node:node + 0x2F0] = bytes(0x2F0)
        struct.pack_into('<I', ram, node + 0x10, b)
        expect(E1.owner_matrix(bytes(ram), node) != want, f'owner matrix {b:#x}', accept=True)
    # the 001A2370 caller census: an extra call site, and a live 0x219870
    ov = A4.read_overlay()
    expect(retransform_census_problems(K.elf, ov, [first]), 'the 001A2370 census', accept=True)
    extra = bytearray(ov)
    struct.pack_into('<I', extra, len(ov) - 4, (3 << 26) | (L.RETRANSFORM >> 2))
    expect_text(retransform_census_problems(K.elf, bytes(extra), [first]), 'call sites', 'an extra 001A2370 call')
    fake = ram_copy(first, 'scratch owner', [(node_of(first.ram, E1.DRUM) + 0x10, struct.pack('<I', 0x219870))])
    expect_text(retransform_census_problems(K.elf, ov, [fake]), 'scratchpad copy', 'a live 0x219870 node')
    # the run-time words
    olo, ohi = E2.E02.overlay_data_window(ov)
    odisc = ov[olo - C.OVERLAY_ARENA:ohi - C.OVERLAY_ARENA]
    records = E2.chain_records(odisc, olo)
    gap = gap_word(odisc, olo, records)
    fake = ram_copy(first, 'gap word', [flip(first.ram, gap)])
    expect(E2.run_time_words(odisc, olo, fake.ram, records)[1], f'a rewritten word outside the chains ({gap:#x})')
    fake = ram_copy(first, 'last chain', [flip(first.ram, max(E2.SCRIPT_ENTRIES) + 0x10)])
    expect(E2.run_time_words(odisc, olo, fake.ram, records)[1], 'a word of the last chain', accept=True)
    last_rec = max(records)
    fake = ram_copy(first, 'last record end', [flip(first.ram, last_rec + 0x3F)])
    expect(E2.run_time_words(odisc, olo, fake.ram, records)[1], 'the last byte of the last reached record',
           accept=True)
    fake = ram_copy(first, 'after last record', [flip(first.ram, last_rec + 0x40)])
    expect(E2.run_time_words(odisc, olo, fake.ram, records)[1], 'the first byte after the last reached record')
    fake = ram_copy(first, 'placement word', [flip(first.ram, E2.PLACEMENTS[0] + 0x10)])
    expect(E2.run_time_words(odisc, olo, fake.ram, records)[1], 'a placement-table word (between the chains)')
    # the window's last word (0x82C8FC), outside every chain record
    lastw = olo + len(odisc) - 4
    check(not any(r <= lastw < r + 0x40 for r in records), 'control: the last overlay-data word is in a chain')
    fake = ram_copy(first, 'last window word', [flip(first.ram, lastw + 3, 0x80)])
    expect_text(E2.run_time_words(odisc, olo, fake.ram, records)[1], f'{lastw:#x}: rewritten word outside',
                'the last overlay-data word')
    rec_t = json.loads((A / 'tables.json').read_text())
    expect_text(window_problems('overlay data', (A / 'overlay_data.emsc').read_bytes(), (olo, len(odisc)), odisc,
                                [fake], rec_t['overlay_data']['run_time_words'], records),
                'run-time words', 'the last overlay-data word against tables.json')
    # the chain set: pinned to the C and to tables.json; a word of chain
    # 0x828BE0's first record is a run-time word
    centries = c_script_entries()
    expect(script_entry_problems(E2.SCRIPT_ENTRIES, rec_t['scripts']['chains'], centries), 'the chain set',
           accept=True)
    expect(len(centries) != 20, 'twenty chains in the C', accept=True)
    drop = tuple(e for e in E2.SCRIPT_ENTRIES if e != 0x828BE0)
    expect_text(script_entry_problems(drop, rec_t['scripts']['chains'], centries), 'differ from the C',
                'the checker\'s chains without 0x828BE0')
    fewer = {k: v for k, v in rec_t['scripts']['chains'].items() if int(k, 16) != 0x828BE0}
    expect_text(script_entry_problems(E2.SCRIPT_ENTRIES, fewer, centries), 'tables.json scripts chains',
                'tables.json without chain 0x828BE0')
    fake = ram_copy(first, 'chain 828BE0', [flip(first.ram, 0x828BE0 + 0x10)])
    expect(E2.run_time_words(odisc, olo, fake.ram, records)[1], 'a word of chain 0x828BE0', accept=True)
    # the windows
    sc = (A / 'area04_scripts/scripts.emsc').read_bytes()
    for name, bad in (('first byte', mutate(sc, 20)), ('last byte', mutate(sc, len(sc) - 1)),
                      ('entry word', mutate(sc, 12)), ('trailing byte', sc + b'\0'),
                      ('four bytes short', T01.emsc_blob(sc, cut_tail=4))):
        expect(tables_problems(K, [first], {'area04_scripts/scripts.emsc': bad}), f'scripts {name}')
    od = (A / 'overlay_data.emsc').read_bytes()
    for name, bad in (('first byte', mutate(od, 20)), ('last byte', mutate(od, len(od) - 1)),
                      ('base moved', T01.emsc_blob(od, base_shift=4, cut_head=4))):
        expect(tables_problems(K, [first], {'overlay_data.emsc': bad}), f'overlay data {name}')
    tab = json.loads((A / 'tables.json').read_text())
    words = tab['scripts']['run_time_words'][key(first)]
    swapped = dict(tab['scripts']['run_time_words'])
    swapped[key(first)] = [hex(int(words[0], 16) + 4)] + words[1:]
    slo, shi = E2.scripts_window(ov)
    expect(window_problems('scripts', sc, (slo, shi - slo), ov[slo - C.OVERLAY_ARENA:shi - C.OVERLAY_ARENA],
                           [first], swapped, records), 'a run-time word replaced by its neighbour')
    # every table file's last byte
    for rel in ('sub0/roster.emro', 'spawn_table.emsp', 'door_destinations.emsp', 'sub0/message_data.emmd',
                'sub0/world_models.emwm'):
        expect(tables_problems(K, [first], {rel: mutate((A / rel).read_bytes(), -1)}), f'{rel} last byte')
    expect(tables_problems(K, [first], {'sub0/world_models.emwm': b'EMWM'}), 'truncated world models')
    mm = (A / 'sub0/message_data.emmd').read_bytes()
    for cut in ('arec', 'abank', 'rows'):
        expect(tables_problems(K, [first], {'sub0/message_data.emmd': T01.mm_blob(mm, cut)}),
               f'message file one {cut} unit short')
    # the roster and the [47] kind rule
    roster = (A / 'sub0/roster.emro').read_bytes()
    _ok, _live, pairs = T01.roster_nodes(T, roster, first.ram)
    fake = ram_copy(first, 'node moved', [flip(first.ram, pairs[1][1] + 0xB1)])
    expect(roster_problems(roster, [fake]), 'a placement node moved off its record')
    fake = ram_copy(first, 'no pool', [(L.POOL_BASE, bytes(L.POOL_STRIDE * L.POOL_SLOTS))])
    expect(roster_problems(roster, [fake]), 'an empty actor pool')
    k47 = node_of(first.ram, KIND_OWNER, index=47)
    later = next((c for c in caps if c is not first), first)
    l47 = node_of(later.ram, KIND_OWNER, index=47)
    expect(roster_problems(roster, [first, later]), '[47] kinds in state 1 (first) and state 2 (second)',
           accept=True)
    sub = first.ram[0x810701]
    k_first, k_second = first.ram[KIND_TABLE + 8 * sub], first.ram[KIND_TABLE + 4 + 8 * sub]
    check((k_first, k_second) == (8, 9), f'control: [47] kinds {k_first} / {k_second}, not 8 / 9')
    # [47]'s record param equals its second kind (9): so in states 2 and 3
    # {record, first, second} = {record, first}, and a rule that drops the
    # second kind there cannot be told apart by any AREA04 input
    i47 = next(i for i, a in pairs if a == k47)
    check(roster[placements_at(roster) + 0x28 * i47 + 4] == k_second,
          'control: [47]\'s record param is not its second kind')
    for val, state, what in ((9, 1, 'state 1 with the second kind'), (7, 2, 'state 2 with a kind outside the table'),
                             (7, 0, 'state 0 with a kind outside the table'),
                             (7, 3, 'state 3 with a kind outside the table'),
                             (8, 4, 'state 4 (no case in the C) with the first kind')):
        fake = ram_copy(first, what, [(k47 + 0x0D, bytes([val])), (k47 + 4, bytes([state]))])
        expect_text(roster_problems(roster, [fake]), 'lost a copied field', f'[47] {what}')
    # accepted: state 2 left from sub-state 0 on D_00810764 == 0xFF (the
    # first kind stays), state 0 after a tick that did not leave it (the
    # first kind written), state 3 with each kind it can inherit
    for val, state, what in ((8, 2, 'state 2 with the first kind'), (8, 0, 'state 0 with the first kind'),
                             (9, 3, 'state 3 with the second kind'), (8, 3, 'state 3 with the first kind')):
        fake = ram_copy(first, what, [(k47 + 0x0D, bytes([val])), (k47 + 4, bytes([state]))])
        expect([p for p in roster_problems(roster, [fake]) if 'lost a copied field' in p], f'[47] {what}',
               accept=True)
    # in state 1 the node must carry the first kind the table gives (in
    # state 2 the second kind equals the record's param, so the table's
    # second kind cannot show there)
    fake = ram_copy(first, 'kind table', [flip(first.ram, KIND_TABLE)])
    expect_text(roster_problems(roster, [fake]), 'lost a copied field', '[47] with its first kind changed in RAM')
    fake = ram_copy(later, '+0x54', [flip(later.ram, l47 + 0x54)])
    expect_text(roster_problems(roster, [fake]), 'lost a copied field', 'a placement node +0x54')
    other = next(a for i, a in pairs if C.u32(first.ram, a + 0x10) != KIND_OWNER)
    fake = ram_copy(first, '+0x0d', [flip(first.ram, other + 0x0D)])
    expect_text(roster_problems(roster, [fake]), 'lost a copied field', 'a placement node +0x0D')
    # model bindings
    wm = (A / 'sub0/world_models.emwm').read_bytes()
    wtab = C.u32(wm, 8)
    offs = {wtab + (C.s32(first.ram, wtab + 4 + 4 * i) >> 2 << 2) for i in range(C.u32(wm, 32))}
    node = next(a for _s, a in E2.E02.model_owner_nodes(first.ram) if C.u32(first.ram, a + 0x44) in offs)
    expect(model_binding_problems(wm, [ram_copy(first, 'model id', [flip(first.ram, node + 0x0D)])]), 'model id +0x0D')
    expect(model_binding_problems(wm, [ram_copy(first, 'bones', [flip(first.ram, node + 0x0C)])]), 'bone count')
    multi = next(a for _s, a in E2.E02.model_owner_nodes(first.ram) if first.ram[a + 0x0C] >= 2 and
                 C.u32(first.ram, a + 0x44) in offs)
    last_slot = multi + 0x110 + 4 * (first.ram[multi + 0x0C] - 1)
    expect_text(model_binding_problems(wm, [ram_copy(first, 'bone slot', [(last_slot, bytes(4))])]),
                'bone count / slots', 'the last bone slot of a multi-bone owner cleared')
    expect_text(model_binding_problems(wm, [ram_copy(first, 'unbound', [(node + 0x44, bytes(4))])]),
                'model owners bound, not', 'one bound owner fewer')
    # the spawn entry
    expect_text(spawn_entry_problems([ram_copy(first, 'entry', [(0x810702, b'\x0d')])]), 'outside the',
                'the spawn entry one past the sub-0 records')
    expect(spawn_entry_problems([ram_copy(first, 'entry 12', [(0x810702, b'\x0c')])]),
           'another spawn entry (12) than D_008106C8 names')
    expect_text(spawn_entry_problems([ram_copy(first, 'sub 1', [(0x810701, b'\x01')])]), 'not the captured sub 0',
                'a capture whose sub byte is 1')
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
    expect(not cells_findings_ok(-1, 0), 'the recorded loader verdicts', accept=True)
    for rc, d in ((0, 0), (-1, -1), (0, -1)):
        expect(not cells_findings_ok(rc, d), f'loader verdicts {rc} / {d} taken as the finding')
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
    fake = ram_copy(first, 'cursor', [(0x28A740, struct.pack('<I', C.u32(first.ram, 0x28A740) + 0x800))])
    expect_text(load_problems(K, [fake], len(cells)), 'a different load map', 'a moved load cursor')
    expect(load_problems(K, [ram_copy(first, 'sub byte', [(0x810701, b'\1')])], len(cells)),
           'a capture whose sub byte names sub 1')
    planted = [(a, K.other.read(a, n)) for a, _p, _o, n, l in K.other.map if '.n' in l]
    expect_text(load_problems(K, [ram_copy(first, 'sub1 image', planted)], len(cells)), 'also fits RAM',
                'the sub-1 nested image in RAM')
    for one, which in ((planted[-1:], 'last'), (planted[:1], 'first')):
        expect([p for p in load_problems(K, [ram_copy(first, 'one sub1 file', one)], len(cells))
                if 'also fits RAM' in p], f'only the {which} sub-1 nested file in RAM', accept=True)
    fake = ram_copy(first, 'spad pointer')
    sp = bytearray(first.spad)
    struct.pack_into('<I', sp, L.SPAD_CELLS, C.u32(sp, L.SPAD_CELLS) + 0x10)
    fake.spad = bytes(sp)
    expect_text(load_problems(K, [fake], len(cells)), 'cell directory pointer', 'the scratchpad directory pointer')
    # the level: zone file bytes, the GS state, the bank and list files
    zfile = sorted((A / 'sub0/level').glob('*.emdl'))[0]
    zbuf = zfile.read_bytes()
    b = next(z for zz in _ZONES.values() for lab, z in zz.items() if lab.endswith('f01_id44.bin'))[0]
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
    dyn = (A / 'sub0/level/dynamic_objects.emsc').read_bytes()
    base, _e, length = struct.unpack_from('<3I', bank, 8)
    dbase, _de, dlen = struct.unpack_from('<3I', dyn, 8)
    expect_text(level_files_problems(bank, dyn, [ram_copy(first, 'bank end', [flip(first.ram, base + length - 1)])]),
                'static bank differs from RAM', 'the static bank\'s last byte')
    for name, bb, dd in (('bank trailing byte', bank + b'\0', dyn), ('bank one byte short', T01.emsc_blob(bank, cut_tail=1), dyn),
                         ('bank base word', mutate(bank, 8), dyn), ('bank version', mutate(bank, 4), dyn),
                         ('list last byte', bank, mutate(dyn, len(dyn) - 1)),
                         ('list one byte short', bank, T01.emsc_blob(dyn, cut_tail=1)),
                         ('list last entry', bank, T01.emsc_blob(dyn, cut_tail=L.DYN_ENTRY)),
                         ('list base word', bank, mutate(dyn, 9))):
        expect(level_files_problems(bb, dd, [first]), f'level files: {name}')
    fake = ram_copy(first, 'dyn count', [(dbase, struct.pack('<I', C.u32(first.ram, dbase) + 1))])
    expect_text(level_files_problems(bank, dyn, [fake]), 'dynamic list', 'the dynamic list count + 1 in RAM')
    fake = ram_copy(first, 'dyn pointer', [(E1.D_0028A5A4, struct.pack('<I', dbase + L.DYN_ENTRY))])
    expect_text(level_files_problems(bank, dyn, [fake]), 'D_0028A5A4', 'D_0028A5A4 moved')
    # sound
    banks = sub_path('sfx/area04_banks.bin').read_bytes()
    reg = sub_path('sfx/sfx_registry.emsr').read_bytes()
    for name, bad in (('byte 0', mutate(banks, 0)), ('last byte', mutate(banks, -1)), ('truncated', banks[:-1]),
                      ('row +0x50', mutate(banks, 0x50))):
        expect(sfx_problems(K, [first], bad, reg), f'area04_banks.bin {name}')
    expect_text(sfx_problems(K, [first], banks + b'\0', reg), 'container total', 'container length')
    expect(sfx_problems(K, [first], banks, mutate(reg, len(reg) // 2)), 'registry middle byte')
    expect(sfx_problems(K, [first], banks, mutate(reg, len(reg) - 1)), 'registry last byte')
    handle_slot = 0x281D50 + 4 * (4 * 0x14 + 0)
    expect(sfx_problems(K, [ram_copy(first, 'binding', [(handle_slot, b'\0\0\0\0')])], banks, reg),
           'group 4 slot 0 unbound')
    second = next(c for c in caps if c is not first)
    expect_text(sfx_problems(K, [first, ram_copy(second, 'binding 2', [(handle_slot, b'\0\0\0\0')])], banks, reg),
                'a different bank binding', 'group 4 slot 0 unbound in the second capture only')
    why = E3.refusal_reasons(caps, REFUSED)
    expect(not ('without SShd magic' in why[(3, 0)] and 'freed handle record' in why[(4, 1)]),
           'the refusal reasons (group 3: no magic, group 4 slot 1: freed)', accept=True)
    h6 = C.u32(first.ram, S.D_00281D50 + 4 * (4 * S.SLOTS + 1))
    fake = ram_copy(first, 'handle 6 used', [(S.D_0027C6C0 + 12 * h6, struct.pack('<I', 1))])
    expect_text([E3.refusal_reasons([fake], REFUSED)[(4, 1)]], 'without SShd magic', 'a handle record in use')
    good = dict(BINDING)
    expect(binding_problem(good, set(REFUSED)), 'the pinned bindings', accept=True)
    expect(binding_problem({**good, (2, 0): ('area', 1)}, set(REFUSED)), 'another row')
    expect(binding_problem({k: v for k, v in good.items() if k != (1, 2)}, set(REFUSED)), 'a binding missing')
    expect(binding_problem(good, set()), 'nothing refused')
    expect(binding_problem(good, set(REFUSED) | {(5, 0)}), 'an extra refused slot')
    expect(binding_problem(good, set(sorted(REFUSED)[:-1])), 'a refused slot missing')
    # EMCL
    emcl = (A / 'sub0/area04.emcl').read_bytes()
    T01.GRID = GRID
    for at in (0x30 + 1, len(emcl) // 2, len(emcl) - 1):
        expect(T01.emcl_equal(mutate(emcl, at), first.ram), f'EMCL +{at:#x}')
    expect(T01.emcl_equal(emcl + b'\0', first.ram), 'EMCL trailing byte')
    fake = ram_copy(first, 'grid pointer', [flip(first.ram, E1.D_0028A598 + 1)])
    expect(emcl_problems(K, emcl, [fake], len(cells)), 'D_0028A598')
    fake = ram_copy(first, 'grid node', [flip(first.ram, GRID + C.u32(first.ram, GRID + 0x20) + 0x26)])
    expect(emcl_problems(K, emcl, [fake], len(cells)), 'a grid node plane byte')
    # the directory allowance moved over the grid (the scratchpad pointer at
    # the grid): the RAM rebuild still equals the file, only grid_problems
    # sees that the grid bytes would be excused from the load-map check
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
    # ctx
    ctx = C.u32(first.ram, L.CTX_PTR)
    for off in (0xA0, 0xFF):
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                L.check_ctx_room_block([ram_copy(first, 'ctx', [flip(first.ram, ctx + off)])], K.elf)
            caught = []
        except SystemExit as error:
            caught = [str(error)]
        expect(caught, f'ctx +{off:#x}')
    # cells.json against its own rows, an unrecorded capture, clean copies
    fake = ram_copy(first, 'unrecorded')
    fake.key = 'unrecorded'
    expect_text(cells_problems(K, cells, [fake], record), 'not recorded in cells.json', 'an unrecorded capture')
    bad = json.loads(json.dumps(record))
    bad['cells']['moved_uids'] = bad['cells']['moved_uids'][:-1]
    expect_text(cells_problems(K, cells, [first], bad), 'is not the union of its rows', 'cells.json moved_uids')
    clean = [ram_copy(c, f'clean {c.name}') for c in (first, moved)]
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
        (tree / 'sub0/level/dynamic_objects.emsc').symlink_to(real / 'sub0/level/dynamic_objects.emsc')
        for z in zlinks:
            (tree / f'sub0/level/{z.name}').symlink_to(z)
        A = tree
        got, _v = level_problems(K, [ram_copy(first, 'matrix slot', [flip(first.ram, at, 0x08)])])
    finally:
        A = real
        for q in [tree / f'sub0/level/{z.name}' for z in zlinks] + [tree / 'sub0/level/static_bank.emsc',
                                                                   tree / 'sub0/level/dynamic_objects.emsc']:
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
            C.ELF_PATH, A4.OVERLAY_PATH, C.ISO_PATH]
    missing = [str(p) for p in need if not p.exists()]
    if missing or not A4.ROUTE_A04.exists() or not A4.ARRIVAL.exists():
        print('area04 assets reference: SKIPPED, missing local inputs:', missing or [str(A4.ROUTE_A04)])
        return 0
    t0 = time.time()
    el = L.load_export_level()
    A4.configure(0)
    E1.install()
    pairs = A4.all_captures()
    if any(s != 0 for _c, s in pairs):
        check(False, f'a capture of another sub: {[(c.name, s) for c, s in pairs if s]}')
    run = pairs if FULL else [(c, s) for c, s in pairs if c.name in QUICK]
    caps = [c for c, _s in run]
    print(f'area04 assets reference ({MODE}): {len(caps)} of {len(pairs)} captures (all sub 0)')
    K = SimpleNamespace(el=el, elf=C.read_elf(), first=pairs[0][0])
    K.lmap, K.info = C.build_load_map(K.first)
    K.image = C.LoadedImage(K.lmap)
    proxy = ram_copy(K.first, 'sub1 proxy', [(0x810701, b'\1')])
    K.other = C.LoadedImage(C.build_load_map(proxy)[0])
    lib = T01.build_loaders()
    loaded, cells_rc = check_loaders(lib, A)
    for name, ok in loaded.items():
        check(ok, f'port loader rejects {name}')
    diag = cells_diagnostic(lib, A)
    check(cells_findings_ok(cells_rc, diag), f'em_actor_cells_load: {cells_rc}, bit-29-cleared copy {diag}')
    print(f'  loaders: {sum(loaded.values())}/{len(loaded)} files accepted by the port loaders; finding: '
          f'em_actor_cells_load(sub0/area04_cells.bin) = {cells_rc} (uid 0 bit 29; the cleared copy: {diag})')

    problems, V = run_checks(K, caps)
    for p in problems:
        check(False, p)
    lv = V.get('level', {})
    print(f"  load map + level: {lv.get('objects')} bank objects -> {lv.get('zones')} zone EMDLs, "
          f"{lv.get('dynamic_entries')} dynamic-list entries; {lv.get('kicks')} level kicks and "
          f"{lv.get('dynamic_kicks')} dynamic kicks inside them")
    print(f'  collision, cells, tables, sfx: every comparison over the run\'s captures ({len(problems)} problems)')
    if V['room']:
        print(f"  ctx +0xA0..+0xFF: original 001D8FD0 rebuilt the captured bytes in {len(V['room'])} capture(s) "
              f"from room entries {sorted({r['room_entry'] for r in V['room']})}")
    if FAILS:
        print(f'  canary and controls: skipped, {len(FAILS)} check(s) already failed')
    else:
        print(f'  canary: {canary(K, lib, caps[0])} sections each reported their planted difference')
        if not FAILS:
            print(f'  controls: {controls(K, caps, lib)} changed inputs, each caught (or accepted where marked)')
    print(f'area04 assets reference: {"FAIL" if FAILS else "PASS"} ({len(FAILS)} failures, '
          f'{time.time() - t0:.1f} s)')
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
