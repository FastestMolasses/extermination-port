#!/usr/bin/env python3
"""Check the exported AREA06 assets (assets/area06/, docs/AREA06_ASSETS.md)
against the recorded original captures, and load each one through the port's
own loader.

Inputs (all local, all the user's): the assets written by
tools/export_area06_level.py, export_area06_tables.py and export_area06_sfx.py,
the extracted disc files, the pinned boot ELF and AREA06 overlay, the disc
image, and the AREA06 captures (the arrival ../Extermination/build/s87/
route_a01u/a01u_02_progression_exit/ and build/s87/route_a06/a06_00..a06_05,
a06_s1; every one is sub 0; a06_s0 ends in AREA01 and is excluded by its
area byte). Missing inputs print SKIPPED and exit 0.

The comparators are the AREA01 checker's (tools/test_area01_assets_reference.py,
imported unchanged, with export_area01_common pointed at AREA06 and its
module constants GRID / BUILD / OVERLAY_SHA256 set for AREA06):
level_bank_problems (the static bank and the dynamic list), zone_problems,
emsc_header_problems, gs_captures, gs_state_problems, emcl_equal,
grid_problems, hulls_inside_table, roster_image / roster_equal /
roster_nodes, emsp_problems with spawn_layout / doors_layout,
messages_problems, world_models_problems, registry_from_ram,
load_map_problems, build_loaders. The directory derivation is
export_area06_level.verify_directory (the ORIGINAL 0019C6F0, 001A2370 and
0x219F50 in the EE interpreter). The ctx block is export_area22_level's
check_ctx_block (the ORIGINAL 001D8FD0 and 001D1C50). AREA06's own checks
are written here: the id labels and the relocation words of the load map,
the sub-1 nested block against RAM, the beam's +0x0D rule for the placement
nodes, the run-time words of the script and overlay-data windows, the
001A2370 / 0x219F50 / 0019C6F0 call-site census, the model bindings and the
sound bindings.

Checks (every comparison is exact):
  load map  in address order, no overlap, the same map from every
            capture's descriptors, every nested row labelled chunk10.n0/idXX
            and inside [D_0028A490[XX], the next relocated address), the top
            row at D_0028A490[0x41]; every relocated word of both lists =
            its cursor + offset; every mapped disc byte equals RAM except
            the cell directory's own bytes (checked below); the sub-1
            nested files do not fit RAM
  level     sub0/level/static_bank.emsc and dynamic_objects.emsc equal RAM
            at D_0028A5A0 / D_0028A5A4 over their extents; every textured
            level kick REFs a bank object with the class-0 GS state, every
            kernel-0x00237450 kick REFs a dynamic-list entry; the zone EMDL
            equals a rebuild from the bank, byte for byte, and its texels the
            GS freeze decode of every capture
  emcl      sub0/area06.emcl equals the RAM grid rebuild (emcl_from_ram) in
            every capture; D_0028A598 pinned, the grid block inside the load
            map and outside the directory allowance
  cells     sub0/area06_cells.bin equals the disc bytes and ends where the
            directory ends; every byte of every capture's directory: the uid
            words by the ORIGINAL 0019C6F0 for [9]'s last call, each hull the
            disc or the ORIGINAL 001A2370 / 0x219F50 run for its live owner,
            or an orphan proven from another capture; the moved set and the
            flag calls per capture equal cells.json; the call sites of
            001A2370, 0x219F50 and 0019C6F0 in the ELF and the module equal
            the census (so the owner model names every caller)
  tables    sub0/roster.emro = the rebuild by the original's walks over the
            pinned ELF + overlay and over every capture's RAM, (live, at
            rest) per capture pinned, every live placement node keeps its
            copied fields (+0x0D of the beam by its C's rule); both EMSP
            files; tables.json doors.used = the roster's 001BC350 / 001BB860
            placements (door id, area-change bit) with each record equal to
            the RAM door row; D_008106C8 = +0x1C of the current spawn record; scripts
            and overlay data: extent, bytes = the pinned module, RAM equal
            except the run-time words, each in a reached chain record, the
            set per capture = tables.json; the chain set = the C's
            func_001BA1A0 data symbols = tables.json's chains;
            sub0/message_data.emmd (every byte); world models and every model
            owner's +0x44 = 001C6120(table, +0x0D) with its bone count and
            bone slots, the bound count pinned
  sfx       sub0/sfx/area06_banks.bin = upload section 0's container on the
            disc; the capture bindings pinned; sub0/sfx/sfx_registry.emsr =
            the RAM re-derivation of every capture
  ctx       ORIGINAL 001D8FD0 + 001D1C50 rebuild ctx +0xA0..+0xFF from room
            entry 0x0600 (flipping the entry changes it; 001D1C50 changes
            none of it: spawn word bit 0x80 is clear in every capture)
  loaders   each file through its port loader (compiled privately from src/)
  canary    run_checks again over [a capture, a planted copy of it] and a
            copy of the export tree with file plants; each section must
            report its plant for the copy and stay silent for the original;
            every port loader must refuse a broken copy of its file
  controls  a changed input per comparator, file side and RAM side, and the
            accept cases listed in docs/AREA06_ASSETS.md
Default: 3 captures (the arrival, a06_04, a06_s1); EM_TEST_FULL=1: all 8.
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
import export_area06_common as A6  # noqa: E402  (first: it keeps AREA01's nested load-map builder)
import export_area06_level as E1  # noqa: E402
import export_area06_tables as E2  # noqa: E402
import export_area06_sfx as E3  # noqa: E402

C = A6.configure(0)
E1.install()
import test_area01_assets_reference as T01  # noqa: E402
from reference_mode import FULL, MODE  # noqa: E402

L, T, S = E1.L, E2.T, E3.S
A = Path(os.environ.get('EM_AREA06_ASSETS') or A6.OUT).resolve()
BUILD = C.ROOT / 'build/area06/assets/test'
CANARY = BUILD / f'canary-{os.getpid()}'
QUICK = ('a01u_02_progression_exit', 'a06_04_beam_collapse', 'a06_s1_bar')
T01.BUILD = BUILD
T01.OVERLAY_SHA256 = A6.OVERLAY_SHA256
# D_0028A598 = D_0028A740 + the id-0x42 relocation offset 0, measured in
# every capture (the relocation check ties it to the descriptor)
GRID = 0x133A1C0
T01.GRID = GRID
FAILS = T01.FAILS
check = T01.check

# (live, at the record's position and rotation) placement nodes per capture
# (T01.roster_nodes); measured, asserted exactly
ROSTER_LIVE = {'a01u_02_progression_exit': (55, 54), 'a06_00_beam': (55, 53), 'a06_01_crate_door2': (55, 53),
               'a06_02_keypad': (55, 53), 'a06_03_room_out': (55, 53), 'a06_04_beam_collapse': (55, 52),
               'a06_05_door3_locked': (55, 52), 'a06_s1_bar': (55, 53)}
# model owners bound to a bank model per capture; asserted exactly
MODEL_OWNERS = {'a01u_02_progression_exit': 43, 'a06_00_beam': 43, 'a06_01_crate_door2': 42, 'a06_02_keypad': 42,
                'a06_03_room_out': 42, 'a06_04_beam_collapse': 42, 'a06_05_door3_locked': 42, 'a06_s1_bar': 41}
# the capture bindings, (group, slot) -> (container, row); 'area' =
# area06_banks.bin; and the refused slots
BINDING = {(1, 0): ('global', 0), (1, 1): ('global', 1), (1, 2): ('global', 2),
           (2, 0): ('area', 0), (4, 0): ('area', 1), (4, 1): ('area', 2), (4, 2): ('area', 3)}
REFUSED = {(3, 0)}
SPAWN_ROWS = E2.SPAWN_SUB0
DOOR_ROW = E2.DOOR_ROW
CANARY_TEST_1 = 0x8153A0      # where the level kicks take TEST_1 (0x5000D), as in the earlier areas
# SHA-256 of INDEX.IDX sector 10 as the user's disc image gives it (a hash,
# not disc data): the relocation lists, the sections and the resident
# offsets every check reads are as fixed as the pinned ELF
DESCRIPTOR_SHA256 = '2e1032752fff8ddaad5f2fa210c141f2feb4e48e530478e979f3b00808e94135'
LABEL = re.compile(r'chunk10\.n0/id([0-9a-f]{2})')
TOP_LABEL = 'chunk10/f00_id41.bin'
# the beam [11] (0x824560): its C writes +0x0D = 0xC in state 0 (story
# flag 16 set, then state 2) and REC[0x2C] when the collapse ends (then
# state 2); states 1 and 4 keep the record's +0x0D
BEAM = E1.BEAM
DRUMS = 4
# every call of 001A2370 in the pinned boot ELF and the AREA06 overlay (jal;
# no j, no address-as-data word): the drum 0x156620 (0x156EF0), the pickup
# 0x219550 (0x219668), 0x219F50 (0x21A104) and the beam 0x824560 x3
RETRANSFORM_SITES = (0x156EF0, 0x219668, 0x21A104, 0x8248C4, 0x824CD8, 0x825B74)
# every call of 0x219F50 (only 0x219870, state 0) and of 0019C6F0 (only [9])
SCALED_SITES = (0x2198E4,)
FLAG_SITES = (0x823730, 0x823ADC)


def key(cap):
    """The capture name the pinned tables use (a planted copy keeps its source's)."""
    return getattr(cap, 'key', cap.name)


def sub_path(rel):
    return A / 'sub0' / rel


# ---------------------------------------------------------------------------
# Load map


def label_problems(lmap, cap):
    """Every nested load-map row is labelled chunk10.n0/idXX by the
    original's own reading of the relocation list: the nested rows tile
    D_0028A740 .. their end without a gap, and a row labelled idXX lies
    inside [D_0028A490[XX], the next address D_0028A490 holds for any id of
    the nested descriptor's list (hash-pinned), or the end), read from the
    capture's RAM; the top row starts at D_0028A490[0x41]
    = D_0028A73C. (The extracted files' names read the list as block
    offsets and are shifted by the resident offset; a map labelled by them
    fails here.)"""
    out, ram = [], cap.ram
    rows = sorted(lmap)
    top = [r for r in rows if not r[4].startswith('chunk10.n')]
    nested = [r for r in rows if r[4].startswith('chunk10.n')]
    base, nbase = C.u32(ram, C.D_0028A73C), C.u32(ram, C.D_0028A740)
    if [(r[0], r[4]) for r in top] != [(base, TOP_LABEL)] or C.u32(ram, A6.D_0028A490 + 4 * 0x41) != base:
        out.append(f'{cap.name}: the top row is not {TOP_LABEL} at D_0028A490[0x41] = D_0028A73C')
    if not nested or nested[0][0] != nbase:
        out.append(f'{cap.name}: the nested rows do not start at D_0028A740')
        return out
    for x, y in zip(nested, nested[1:]):
        if x[0] + x[3] != y[0]:
            out.append(f'{cap.name}: a gap in the nested load map at {x[0] + x[3]:#x}')
    end = nested[-1][0] + nested[-1][3]
    for _a, _p, _o, _n, label in nested:
        if LABEL.fullmatch(label) is None:
            out.append(f'{cap.name}: load map label {label} is not a relocation id')
    # every id the nested descriptor relocates (the descriptor is hash-pinned)
    idents = [i for i, _o in A6.relocation_lists(*A6.descriptors(cap))['nested']]
    starts = sorted({C.u32(ram, A6.D_0028A490 + 4 * i) for i in idents})
    for a, _p, _o, n, label in nested:
        m = LABEL.fullmatch(label)
        if m is None:
            continue
        ident = int(m.group(1), 16)
        start = C.u32(ram, A6.D_0028A490 + 4 * ident)
        stop = min([x for x in starts if x > start], default=end)
        if not start <= a < a + n <= stop:
            out.append(f'{cap.name}: load map row {a:#x}+{n:#x} labelled {label} lies outside '
                       f'D_0028A490[{ident:#x}] {start:#x}..{stop:#x}')
    return out


def descriptor_problems(disc=None):
    """The disc descriptor (INDEX.IDX sector 10, or `disc`) is the pinned one."""
    if disc is None:
        disc = C.iso_descriptor(A6.AREA + 4)
    if disc is None:
        return ['no disc image for the descriptor']
    if C.sha(bytes(disc)) != DESCRIPTOR_SHA256:
        return [f'INDEX.IDX sector 10 is not the pinned descriptor ({C.sha(bytes(disc))[:16]})']
    return []


def load_problems(K, caps, cells_len):
    out = list(T01.load_map_problems(K.lmap))
    image = K.image
    table = C.u32(K.first.spad, L.SPAD_CELLS)
    for cap in caps:
        try:
            A6.configure(0)
            lmap, _info = C.build_load_map(cap)
        except SystemExit as error:
            out.append(f'{cap.name}: load map: {error}')
            continue
        if lmap != K.lmap:
            out.append(f'{cap.name}: a different load map')
        out += label_problems(K.lmap, cap)
        out += A6.relocation_problems(cap)[0]
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
    kicks and the zone EMDL. Returns (problems, summary)."""
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
        out.append(f'sub0: {len(files)} zone files for {len(zones)} relocation ids')
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
            out.append(f'{cap.name}: sub0/area06.emcl differs from the RAM grid rebuild: {where}')
    return out


def cells_problems(K, cells, caps, record):
    """The directory file against the disc, and every capture's RAM
    directory through export_area06_level.verify_directory. `record` =
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
        out.append(f'sub0/area06_cells.bin differs from the disc bytes at +{k:#x}')
    try:
        _count, _hulls, size = L.cell_directory(cells, 0)
        if size != len(cells):
            out.append(f'sub0/area06_cells.bin is {len(cells)} bytes, the directory {size}')
        inside = T01.hulls_inside_table(L, cells)
        if inside:
            out.append(f'cells: hulls {inside} start inside the uid table')
        rows, problems, calls = E1.verify_directory(K.elf, cells, caps)
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
        wcalls = record['cells']['flag_calls'].get(key(cap))
        if wcalls is not None and [list(c) for c in calls.get(cap.name, ())] != wcalls:
            out.append(f'{r["capture"]}: 0019C6F0 calls {calls.get(cap.name)} != cells.json {wcalls}')
    return out


def word_sites(blob, base, word):
    """The 4-aligned addresses in `blob` (loaded at `base`) holding `word`."""
    pat, out, k = struct.pack('<I', word), [], blob.find(struct.pack('<I', word))
    while k >= 0:
        if k % 4 == 0:
            out.append(base + k)
        k = blob.find(pat, k + 1)
    return out


def call_sites(elf, ov, target):
    """Every jal / j to `target` and every word holding its address, in the
    pinned ELF's load segment and the module."""
    text = elf[C.ELF_OFFSET:C.ELF_OFFSET + C.ELF_FILESZ]
    load = C.u32(ov, 8)
    sites = []
    for word in ((3 << 26) | (target >> 2), (2 << 26) | (target >> 2), target):
        sites += word_sites(text, C.ELF_VADDR, word) + word_sites(ov, load, word)
    return tuple(sorted(sites))


def retransform_census_problems(elf, ov, caps):
    """The call sites of 001A2370 equal RETRANSFORM_SITES (so the owner
    model names every caller: owner_matrix's node + 0xD0 owners and
    0x219F50), those of 0x219F50 equal SCALED_SITES (only 0x219870) and
    those of 0019C6F0 FLAG_SITES (only [9]); every live 0x219870 node has
    a hull uid or none."""
    out = []
    for target, want, name in ((L.RETRANSFORM, RETRANSFORM_SITES, '001A2370'),
                               (E1.SCALED_CALLER, SCALED_SITES, '0x219F50'),
                               (E1.E02.FLAG_SETTER, FLAG_SITES, '0019C6F0')):
        got = call_sites(elf, ov, target)
        if got != want:
            out.append(f'{name} call sites {[hex(a) for a in got]} != the census')
    return out


C_CALL = r'func_001BA1A0\(\s*\w+\s*,\s*\(?[^,()]*\)?\s*&?D_overlay_AREA06_([0-9A-Fa-f]{8})'


def c_script_entries():
    """Every chain the AREA06 overlay's committed C starts: the data symbol
    of each func_001BA1A0 call in src/overlays/AREA06/ (the data names are
    runtime addresses)."""
    out = set()
    for f in sorted((C.DECOMP / 'src/overlays/AREA06').glob('*.c')):
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


def kind_allowed(ram, node, kind, fallen):
    """The +0x0D values the beam's C (BEAM) can leave in `node`, given its
    record's +4 (`kind`) and REC[0x2C] (`fallen`: 0x2C bytes into its
    0x28-byte record, i.e. +4 of the record after it): in states 0, 1 and 4
    the record's (state 0 spawns with it, and its else branch into 1 / 4
    does not write +0x0D), in state 2 the constant 0xC (state 0 with story
    flag 16 set) or REC[0x2C] (the collapse's end). State 3 and other values:
    the C writes nothing there (state 3 frees the node), so any of the
    three."""
    state = ram[node + 4]
    if state in (0, 1, 4):
        return {kind}
    if state == 2:
        return {0xC, fallen}
    return {kind, 0xC, fallen}


def placements_at(blob):
    """The offset of the placement records in a roster blob."""
    groups = C.u16(blob, 0x0A)
    return 0x18 + 8 * groups + sum(0x2C * struct.unpack_from('<II', blob, 0x18 + 8 * g)[1] for g in range(groups))


def roster_node_problems(blob, ram):
    """(problems, (live, placed)): T01.roster_nodes' pairs, each live
    placement node keeping +0x03 and +0x54, and +0x0D as the record gives
    it, or, for the beam, a value kind_allowed permits."""
    _ok, live, pairs = T01.roster_nodes(T, blob, ram)
    at = placements_at(blob)
    out = []
    for i, a in pairs:
        rec = blob[at + 0x28 * i:at + 0x28 * (i + 1)]
        f, _pos, _rot = T.spawn_fields_placement(rec, i)
        for off in (0x03, 0x54):
            if ram[a + off:a + off + len(f[off])] != f[off]:
                out.append(f'placement[{i}] node {a:#x} +{off:#x}')
        if C.u32(ram, a + 0x10) == BEAM:
            if ram[a + 0x0D] not in kind_allowed(ram, a, rec[4], blob[at + 0x28 * i + 0x2C]):
                out.append(f'placement[{i}] node {a:#x} +0xd {ram[a + 0x0D]} outside the beam\'s kinds')
        elif ram[a + 0x0D] != rec[4]:
            out.append(f'placement[{i}] node {a:#x} +0xd')
    return out, live


DOOR_CODE = (0x1BC350, 0x1BB860)     # the placement doors' behaviours, 001BC350 / 001BB860


def doors_used_problems(roster, used, caps):
    """tables.json doors.used (sub 0), recomputed here rather than by the
    exporter's doors_used: the roster file's placements whose behaviour
    (+0x24) is 001BC350 or 001BB860, with the door id (+0x03 & 0x7F) and the
    area-change bit (+0x03 & 0x80) of each; every listed record must equal
    the 4 bytes of the door row DOOR_ROW in each capture's RAM."""
    count, at = C.u32(roster, 0x0C), placements_at(roster)
    want = []
    for i in range(count):
        rec = roster[at + 0x28 * i:at + 0x28 * (i + 1)]
        if C.u32(rec, 0x24) in DOOR_CODE:
            want.append((i, C.u32(rec, 0x24), rec[3] & 0x7F, bool(rec[3] & 0x80)))
    try:
        got = [(d['placement'], int(d['behaviour'], 16), d['door_id'], d['area_change']) for d in used]
        records = [(d['door_id'], bytes(d['record'])) for d in used]
    except (KeyError, TypeError, ValueError) as error:
        return [f'tables.json doors.used does not parse ({error!r})']
    out = []
    if got != want:
        out.append(f'tables.json doors.used {got} differs from the roster placements {want}')
    for door, record in records:
        if not 0 <= door < 6 or len(record) != 4:
            out.append(f'tables.json doors.used door {door}: outside the 6-record row or not 4 bytes')
            continue
        for cap in caps:
            if cap.ram[DOOR_ROW + 4 * door:DOOR_ROW + 4 * door + 4] != record:
                out.append(f'{cap.name}: tables.json doors.used record of door {door} differs from RAM')
    return out


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
    """D_008106C8 = word +0x1C of the capture's current spawn record (area 6,
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
        out += doors_used_problems(read('sub0/roster.emro'), rec['doors']['used']['0'], caps)
        out += script_entry_problems(E2.SCRIPT_ENTRIES, rec['scripts']['chains'], c_script_entries())
        ov = A6.read_overlay()
        olo, ohi = E2.E02.overlay_data_window(ov)
        odisc = ov[olo - C.OVERLAY_ARENA:ohi - C.OVERLAY_ARENA]
        records = E2.chain_records(odisc, olo)
        slo, shi = E2.scripts_window(ov)
        sdisc = ov[slo - C.OVERLAY_ARENA:shi - C.OVERLAY_ARENA]
        out += window_problems('scripts', read('area06_scripts/scripts.emsc'), (slo, shi - slo), sdisc, caps,
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
        A6.configure(0)
        _label, disc, _parsed, _names, first = E3.E02.area_container(K.first)
        if banks != disc:
            k = next((j for j in range(min(len(banks), len(disc))) if banks[j] != disc[j]), min(len(banks), len(disc)))
            out.append(f'sub0/sfx/area06_banks.bin differs from the disc container at +{k:#x} '
                       f'({len(banks)} / {len(disc)})')
        total = C.u32(banks, 0) if len(banks) >= 4 else -1
        if len(banks) != total or total > first[1]:
            out.append(f'sub0/sfx/area06_banks.bin: {len(banks)} bytes, container total {total:#x}')
        parsed = S.X.A.parse_container(banks)
        if parsed is None:
            return out + ['sub0/sfx/area06_banks.bin is not an SShd container']
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


def ctx_problems(K, caps):
    """export_area22_level.check_ctx_block: the ORIGINAL 001D8FD0 and then
    the ORIGINAL 001D1C50 rebuild every captured byte; with spawn word bit
    0x80 clear the room entry must matter and 001D1C50 must change none of
    it."""
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            rows = E1.E22.check_ctx_block(caps, K.elf)
    except SystemExit as error:
        return [f'ctx block: {error}'], []
    E1.install()
    return [], rows


def run_checks(K, caps):
    """Every real check over the export tree A; returns (problems, summary)."""
    P, V = [], {}
    cells = sub_path('area06_cells.bin').read_bytes()
    record = json.loads(sub_path('cells.json').read_text())
    P += load_problems(K, caps, len(cells))
    p, V['level'] = level_problems(K, caps)
    P += p
    P += emcl_problems(K, sub_path('area06.emcl').read_bytes(), caps, len(cells))
    P += cells_problems(K, cells, caps, record)
    P += sfx_problems(K, caps, sub_path('sfx/area06_banks.bin').read_bytes(),
                      sub_path('sfx/sfx_registry.emsr').read_bytes())
    P += tables_problems(K, caps)
    P += retransform_census_problems(K.elf, A6.read_overlay(), caps)
    p, V['room'] = ctx_problems(K, caps)
    P += p
    return P, V


# ---------------------------------------------------------------------------
# Port loaders


def doors_loader_ok(lib, path):
    """em_spawn_table_load accepts the door file and serves both windows
    the door code reads: the pointer array 0x24E140 (0x17 words) and the
    row DOOR_ROW (0x18 bytes)."""
    lib.em_spawn_table_read.restype = CT.c_void_p
    lib.em_spawn_table_read.argtypes = [CT.c_void_p, CT.c_uint32, CT.c_uint32]
    lib.em_spawn_table_load.restype = CT.c_int
    doors = CT.create_string_buffer(16 << 20)
    ok = lib.em_spawn_table_load(doors, str(path).encode()) == 0
    return ok and bool(lib.em_spawn_table_read(doors, 0x24E140, 4 * 0x17)) and \
        bool(lib.em_spawn_table_read(doors, DOOR_ROW, 0x18))


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
    for rel in ('area06_scripts/scripts.emsc', 'overlay_data.emsc', 'sub0/level/static_bank.emsc',
                'sub0/level/dynamic_objects.emsc'):
        out[rel] = rc('em_script_image_load', big(), p(rel)) == 1
    wm = (tree / 'sub0/world_models.emwm').read_bytes()
    out['sub0/world models'] = rc('em_world_models_parse', big(), wm, CT.c_size_t(len(wm))) == 0
    out['sub0/emcl'] = rc('em_collision_load', big(), p('sub0/area06.emcl')) == 0
    out['sub0/roster.emro'] = rc('em_actor_roster_load', big(), p('sub0/roster.emro')) == 0
    for z in sorted(str(x) for x in (tree / 'sub0/level').glob('*.emdl')):
        out[f'sub0/{Path(z).name}'] = rc('em_model_load', big(), z.encode()) == 0
    out['sub0/sfx registry'] = rc('em_sfx_registry_load', big(), p('sub0/sfx/sfx_registry.emsr')) == 1
    with T01.quiet_fds():
        out['sub0/message data'] = rc('em_message_live_install', p('sub0/message_data.emmd')) == 1
    out['sub0/cells'] = rc('em_actor_cells_load', big(), p('sub0/area06_cells.bin')) == 0
    return out


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
    edits.append(flip(ram, A6.D_0028A490 + 4 * 0x72 + 1))    # a relocation word no pointer copy reads
    want['relocation word'] = f'{tag}: D_0028A490[0x72]'
    want['id label'] = f'{tag}: load map row'
    edits.append(flip(ram, CANARY_TEST_1, 2))      # TEST_1 of the level kicks' state
    want['GS state'] = 'level GS state'
    ctx = C.u32(ram, L.CTX_PTR)
    edits.append(flip(ram, ctx + 0xA4))
    want['ctx'] = f'ctx block: {tag}:'
    table = C.u32(cap.spad, L.SPAD_CELLS)
    cells = sub_path('area06_cells.bin').read_bytes()
    _count, hulls, _size = L.cell_directory(cells, 0)
    s0, _e0, _f0 = hulls[min(hulls)]
    edits.append(flip(ram, table + s0 + 0x20))
    want['cell hull'] = f'{tag}: hull {min(hulls)} differs'
    edits.append((table + 8 + 3, bytes([ram[table + 8 + 3] ^ 0x40])))    # uid 1's word, bit 30
    want['uid word'] = f'{tag}: directory bytes differ at'
    flagger = node_of(ram, E1.FLAG_OWNER)
    edits.append(flip(ram, flagger + 5))           # [9]'s sub-state: the other 0019C6F0 call
    want['flag call'] = f'{tag}: 0019C6F0 calls {((E1.FLAG_KEY, ram[flagger + 5] ^ 1),)}'
    grid = GRID
    edits.append(flip(ram, grid + C.u32(ram, grid) + 1))
    want['EMCL'] = f'{tag}: sub0/area06.emcl differs from the RAM grid rebuild'
    roster = sub_path('roster.emro').read_bytes()
    paddr = C.u32(roster, 0x10)
    edits.append(flip(ram, paddr + 0x28 * C.u32(roster, 0x0C) - 1))
    want['roster'] = f'{tag}: roster records differ from RAM'
    _ok, _live, pairs = T01.roster_nodes(T, roster, ram)
    edits.append(flip(ram, pairs[0][1] + 0x03))
    want['placement node fields'] = f'{tag}: a live placement node lost a copied field'
    edits.append(flip(ram, DOOR_ROW + 0x17))
    want['door windows'] = f'{tag}: door_destinations.emsp window {DOOR_ROW:#x} differs'
    rows, n = SPAWN_ROWS
    edits.append(flip(ram, rows + n * 0x30 - 1))   # the last byte of the sub-0 spawn rows
    want['spawn windows'] = f'{tag}: spawn_table.emsp window'
    edits.append(flip(ram, min(E2.SCRIPT_ENTRIES) + 0x10))   # word +0x10 of the first chain's first record
    want['script words'] = f'{tag}: scripts run-time words'
    ov = A6.read_overlay()
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
    # the beam's matrix (it re-transforms hull 28 in this capture), and the
    # 0x219870 node's (0x219F50 re-derives hull 26 from it)
    beam = node_of(ram, BEAM)
    edits.append(flip(ram, E1.owner_matrix(ram, beam) + 0x33, 0x04))
    want['owner matrix'] = f'{tag}: hull {ram[beam + 0x0F]} differs and no owner derivation reproduces it'
    scaled = node_of(ram, E1.SCALED_OWNER)
    edits.append(flip(ram, scaled + 0xD0 + 0x33, 0x04))
    want['scaled matrix'] = f'{tag}: hull {ram[scaled + 0x0F]} differs and no owner derivation reproduces it'
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
    want['zone count'] = f'sub0: {len(zones) + 1} zone files for {len(zones)} relocation ids'
    want['zone name'] = 'sub0/00_a_extra.emdl: zone name'
    rec = json.loads((real / 'sub0/cells.json').read_text())
    row = next(r for r in rec['cells']['captures'] if r['capture'] == key(cap))
    row['moved_hulls'] = row['moved_hulls'][:-1]
    rec['cells']['flag_calls'][key(cap)] = [[E1.FLAG_KEY, 7]]
    plant('sub0/cells.json', json.dumps(rec).encode())
    want['moved set'] = f'{cap.name}: re-transformed hulls'
    want['flag calls record'] = f'{cap.name}: 0019C6F0 calls'
    plant('sub0/area06_cells.bin', mutate((real / 'sub0/area06_cells.bin').read_bytes(), -1))
    want['cells file'] = 'sub0/area06_cells.bin differs from the disc bytes'
    plant('sub0/sfx/area06_banks.bin', mutate((real / 'sub0/sfx/area06_banks.bin').read_bytes(), 0x20000))
    want['sound bank'] = 'sub0/sfx/area06_banks.bin differs from the disc container'
    plant('sub0/roster.emro', mutate((real / 'sub0/roster.emro').read_bytes(), 0x09))
    want['roster file'] = 'sub0/roster.emro differs from the rebuild from the disc'
    sc = (real / 'area06_scripts/scripts.emsc').read_bytes()
    plant('area06_scripts/scripts.emsc', mutate(sc, len(sc) - 1))
    want['scripts file'] = 'scripts differs from the disc overlay'
    tab = json.loads((real / 'tables.json').read_text())
    words = tab['overlay_data']['run_time_words'].get(key(cap), [])
    tab['overlay_data']['run_time_words'][key(cap)] = words[1:] if words else ['0x826000']
    tab['doors']['used']['0'][0]['record'][1] ^= 1
    plant('tables.json', json.dumps(tab).encode())
    want['overlay run-time set'] = f'{cap.name}: overlay data run-time words'
    want['doors used'] = 'tables.json doors.used record of door'
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
            loaded = check_loaders(lib, tree)
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
    for rel in ('area06_scripts/scripts.emsc', 'overlay_data.emsc', 'sub0/level/static_bank.emsc',
                'sub0/level/dynamic_objects.emsc'):
        blob = (tree / rel).read_bytes()
        base, entry = struct.unpack_from('<2I', blob, 8)
        plant(rel, C.emsc(base, blob[20:52], entry))
        names.append(rel)
    for rel, name in (('sub0/message_data.emmd', 'sub0/message data'), ('sub0/roster.emro', 'sub0/roster.emro'),
                      ('sub0/world_models.emwm', 'sub0/world models'), ('sub0/area06.emcl', 'sub0/emcl'),
                      ('sub0/sfx/sfx_registry.emsr', 'sub0/sfx registry'), ('sub0/area06_cells.bin', 'sub0/cells')):
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
    first = caps[0]                                   # the arrival: beam and 0x219870 live, [9] +5 = 0
    record = json.loads((A / 'sub0/cells.json').read_text())
    cells = (A / 'sub0/area06_cells.bin').read_bytes()
    count = C.u32(cells, 0)
    table = C.u32(first.spad, L.SPAD_CELLS)
    _c, hulls, _s = L.cell_directory(cells, 0)
    verify = lambda cs, directory=cells: E1.verify_directory(K.elf, directory, cs)[1]
    # cells: the file side
    for at in (0, 4, 4 + 4 * count, len(cells) - 1):
        expect(cells_problems(K, mutate(cells, at), [first], record), f'cells file +{at:#x}')
    expect_text(cells_problems(K, cells + b'\0', [first], record), 'bytes, the directory', 'cells size check')
    # every moved hull's last byte in the arrival, and each live owner's matrix
    for uid in record['cells']['moved_uids']:
        s, e, _f = hulls[int(uid)]
        fake = ram_copy(first, f'hull {uid}', [flip(first.ram, table + e - 1)])
        expect(verify([fake]), f'moved hull {uid} last byte')
    tried = 0
    for behaviour in E1.NODE_D0_OWNERS:
        node = node_of(first.ram, behaviour)
        if node is None or first.ram[node + 0x0F] not in hulls:
            continue
        s, e, _f = hulls[first.ram[node + 0x0F]]
        if first.ram[table + s:table + e] == cells[s:e]:
            continue                   # this owner's hull is not moved here (the drums): nothing derives it
        tried += 1
        fake = ram_copy(first, f'matrix {behaviour:#x}', [flip(first.ram, node + 0xD0 + 0x32, 0x10)])
        expect(verify([fake]), f'owner matrix of {behaviour:#x} (node + 0xD0)')
    check(tried == 2, f'control: {tried} node + 0xD0 owners with a moved hull, not the beam and the pickup')
    # the 0x219870 node: 0x219F50 run whole over the capture derives hull 26
    scaled = node_of(first.ram, E1.SCALED_OWNER)
    check(scaled is not None and first.ram[scaled + 0x0F] == 26, 'control: the 0x219870 node owns hull 26')
    for off, what in ((0xD0 + 0x32, 'its matrix translation'), (0xD0 + 0x21, 'its matrix third row'),
                      (0xB2, 'its position')):
        fake = ram_copy(first, f'scaled {what}', [flip(first.ram, scaled + off, 0x10)])
        expect_text(verify([fake]), 'hull 26 differs', f'the 0x219870 node with {what} changed')
    ee = bytearray(first.ram)
    struct.pack_into('<I', ee, scaled + 0x10, E1.PICKUP)
    expect_text(verify([ram_copy(first, 'scaled as pickup', [(scaled + 0x10, struct.pack('<I', E1.PICKUP))])]),
                'hull 26 differs', 'the 0x219870 node read as a node + 0xD0 owner (the unscaled matrix)')
    # derive_scaled_hull refuses a run that writes outside the hull it was
    # given: the same run with hull 26's range cut before its last written byte
    s26, e26, f26 = hulls[26]
    full = E1.derive_scaled_hull(K.elf, cells, first, scaled, hulls)
    last = max(k for k in range(e26 - s26) if full[k] != cells[s26 + k])
    check(E1.derive_scaled_hull(K.elf, cells, first, scaled, {**hulls, 26: (s26, s26 + last, f26)}) == b'',
          'control: a 0x219F50 run that writes outside the given hull range is not refused')
    fake = ram_copy(first, '219F50 code', [flip(first.ram, E1.SCALED_CALLER + 0x40)])
    expect_text(verify([fake]), 'code in RAM differs', '0x219F50 code in RAM')
    # orphans. Hull 26 in a06_s1 (the 0x219870 node freed): with the arrival
    # accepted (equal to its derivation), alone rejected
    s1 = by_name.get('a06_s1_bar')
    if s1 is not None:
        expect_text(verify([s1]), 'no capture derives it', 'a06_s1 alone (hull 26 orphaned)')
        expect(verify([first, s1]), 'a06_s1 with the arrival', accept=True)
        s26, e26, _f = hulls[26]
        fake = ram_copy(s1, 'orphan 26 changed', [flip(s1.ram, table + e26 - 1)])
        expect(verify([first, fake]), 'the orphaned hull 26 changed')
    # hull 28 in a06_04 (the beam took uid 27): with the arrival accepted by
    # the written-words rule, alone rejected; a word 001A2370 does not
    # write (the hull header) changed rejected
    fall = by_name.get('a06_04_beam_collapse')
    if fall is not None:
        check(fall.ram[node_of(fall.ram, BEAM) + 0x0F] == 27, 'control: the beam carries uid 27 after the collapse')
        expect_text(verify([fall]), 'no capture derives it', 'a06_04 alone (hull 28 orphaned)')
        expect(verify([first, fall]), 'a06_04 with the arrival (written words)', accept=True)
        s28, e28, _f = hulls[28]
        fake = ram_copy(fall, 'orphan 28 header', [flip(fall.ram, table + s28 + 0x18)])
        expect(verify([first, fake]), 'the orphaned hull 28\'s prim count changed')
        other = ram_copy(fall, 'second orphan', [flip(fall.ram, table + e28 - 4)])
        expect_text(verify([first, fall, other]), 'orphan bytes differ', 'two orphaning captures that disagree')
        # hull 27 lacks the 0x800 bit: the ORIGINAL leaves it at disc for the fallen beam
        check(C.u16(cells, hulls[27][0] + 0x18 + 4) & 0x800 == 0, 'control: hull 27 has no 0x800 bit')
    # the uid words: [9]'s call (0019C6F0)
    flagger = node_of(first.ram, E1.FLAG_OWNER)
    expect(verify([first]), 'the arrival ([9] +5 = 0)', accept=True)
    fake = ram_copy(first, '[9] +5 = 1', [(flagger + 5, b'\1')])
    expect_text(verify([fake]), 'directory bytes differ', '[9] in sub-state 1 with the uid-0 word at rest')
    w0 = table + 4 + 3
    fake = ram_copy(first, 'called', [(flagger + 5, b'\1'), (w0, bytes([first.ram[w0] & ~0x40 & 0xFF]))])
    expect(verify([fake]), '[9] in sub-state 1 with bit 30 of uid 0 cleared', accept=True)
    fake = ram_copy(first, 'bit 30 only', [(w0, bytes([first.ram[w0] & ~0x40 & 0xFF]))])
    expect_text(verify([fake]), 'directory bytes differ', 'bit 30 of uid 0 cleared with [9] in sub-state 0')
    for state, sub_state, what in ((2, 0, 'state 2'), (1, 2, 'sub-state 2')):
        fake = ram_copy(first, what, [(flagger + 4, bytes([state])), (flagger + 5, bytes([sub_state]))])
        expect_text(verify([fake]), 'is not one live node', f'[9] in {what}')
    fake = ram_copy(first, 'no [9]', [(flagger, b'\0')])
    expect_text(verify([fake]), 'is not one live node', 'no live [9]')
    # "(0xD, 0) made last" and "no call yet" must give the same image: a
    # directory whose uid-0 word lacks bit 30 makes them disagree
    cleared = bytearray(cells)
    cleared[7] &= ~0x40 & 0xFF
    expect_text(verify([first], bytes(cleared)), 'disagree',
                'a disc uid-0 word without bit 30 with [9] in sub-state 0')
    fake = ram_copy(first, '19C6F0 code', [flip(first.ram, E1.E02.FLAG_SETTER + 0x10)])
    expect_text(verify([fake]), '0019C6F0 code in RAM', '0019C6F0 code in RAM')
    # the key names a class-0x0B record: its first placement record keyed 0xD
    check(E1.flag_calls(first.ram) == ((0xD, 0),), 'control: the arrival\'s flag call is (0xD, 0)')
    # the drums: their hulls carry the 0x800 bit, all at rest (state 1). A
    # knocked drum (matrix changed) moves exactly its own hull by the
    # ORIGINAL 001A2370, and verify_directory accepts that only because
    # owner_matrix names the drum; the same hull with the matrix at rest is
    # rejected
    from test_coll_move_reference import FloatEE
    drums = [a for _s, a in E2.T.pool_nodes(first.ram)
             if C.u32(first.ram, a + 0x10) == E1.DRUM and first.ram[a + 0x0F] in hulls]
    check(len(drums) == DRUMS, f'control: {len(drums)} live drums owning a hull, not {DRUMS}')
    check(all(C.u16(cells, hulls[first.ram[d + 0x0F]][0] + 0x18 + 4) & 0x800 for d in drums),
          'control: every drum hull carries the 0x800 bit')
    drum = drums[0]
    ds, de, _f = hulls[first.ram[drum + 0x0F]]
    knock = [flip(first.ram, drum + 0xD0 + 0x32, 0x10)]
    kram = bytearray(first.ram)
    kram[knock[0][0]] = knock[0][1][0]
    runner = FloatEE(K.elf, bytes(kram), first.spad)
    runner.write(table, cells)
    runner.call(L.RETRANSFORM, (drum, drum + 0xD0))
    got = runner.read(table, len(cells))
    check(got[ds:de] != cells[ds:de] and got[:ds] == cells[:ds] and got[de:] == cells[de:],
          'control: the knocked drum moves exactly its own hull')
    fake = ram_copy(first, 'knocked drum', knock + [(table + ds, bytes(got[ds:de]))])
    expect(verify([fake]), 'a knocked drum\'s hull (001A2370, drum + 0xD0)', accept=True)
    fake = ram_copy(first, 'drum hull, matrix at rest', [(table + ds, bytes(got[ds:de]))])
    expect(verify([fake]), 'a drum hull moved without its matrix')
    # the uid words: bit 30 of uid 1, and the 001A2370 code in RAM
    fake = ram_copy(first, 'uid word', [(table + 8 + 3, bytes([first.ram[table + 8 + 3] ^ 0x40]))])
    expect_text(verify([fake]), 'directory bytes differ', 'uid 1 word bit 30')
    fake = ram_copy(first, 'retransform code', [flip(first.ram, L.RETRANSFORM + 0x767)])
    expect(verify([fake]), '001A2370 code in RAM')
    # owner_matrix unit controls
    ram = bytearray(first.ram)
    node = 0x1C00000
    for b, want in ((BEAM, node + 0xD0), (0x219550, node + 0xD0), (0x156620, node + 0xD0), (0x219870, None),
                    (0x219F50, None), (0x823580, None), (0x824340, None), (0x823B50, None), (0x825510, None)):
        ram[node:node + 0x2F0] = bytes(0x2F0)
        struct.pack_into('<I', ram, node + 0x10, b)
        expect(E1.owner_matrix(bytes(ram), node) != want, f'owner matrix {b:#x}', accept=True)
    # the call-site census: accepted, an extra site of each target
    ov = A6.read_overlay()
    expect(retransform_census_problems(K.elf, ov, [first]), 'the call-site census', accept=True)
    for target, name in ((L.RETRANSFORM, '001A2370'), (E1.SCALED_CALLER, '0x219F50'),
                         (E1.E02.FLAG_SETTER, '0019C6F0')):
        extra = bytearray(ov)
        struct.pack_into('<I', extra, len(ov) - 4, (3 << 26) | (target >> 2))
        expect_text(retransform_census_problems(K.elf, bytes(extra), [first]), f'{name} call sites',
                    f'an extra {name} call')
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
    expect(E2.run_time_words(odisc, olo, fake.ram, records)[1], 'a placement-table word')
    fake = ram_copy(first, 'group word', [flip(first.ram, E2.GROUPS[0][0][0] + 0x10)])
    expect(E2.run_time_words(odisc, olo, fake.ram, records)[1], 'a deferred-group word')
    lastw = olo + len(odisc) - 4
    check(not any(r <= lastw < r + 0x40 for r in records), 'control: the last overlay-data word is in a chain')
    fake = ram_copy(first, 'last window word', [flip(first.ram, lastw + 3, 0x80)])
    expect_text(E2.run_time_words(odisc, olo, fake.ram, records)[1], f'{lastw:#x}: rewritten word outside',
                'the last overlay-data word')
    rec_t = json.loads((A / 'tables.json').read_text())
    expect_text(window_problems('overlay data', (A / 'overlay_data.emsc').read_bytes(), (olo, len(odisc)), odisc,
                                [fake], rec_t['overlay_data']['run_time_words'], records),
                'run-time words', 'the last overlay-data word against tables.json')
    if fall is not None:
        expect(window_problems('overlay data', (A / 'overlay_data.emsc').read_bytes(), (olo, len(odisc)), odisc,
                               [fall], rec_t['overlay_data']['run_time_words'], records),
               'a06_04\'s rewritten words of chain 0x827180', accept=True)
        drop = tuple(e for e in E2.SCRIPT_ENTRIES if e != 0x827180)
        recs2 = set()
        for e in drop:
            recs2.update(T.walk_chain(odisc, olo, e))
        expect(E2.run_time_words(odisc, olo, fall.ram, recs2)[1], 'a06_04\'s words without chain 0x827180')
    # the chain set: pinned to the C and to tables.json
    centries = c_script_entries()
    expect(script_entry_problems(E2.SCRIPT_ENTRIES, rec_t['scripts']['chains'], centries), 'the chain set',
           accept=True)
    expect(len(centries) != 4, 'four chains in the C', accept=True)
    drop = tuple(e for e in E2.SCRIPT_ENTRIES if e != 0x8276C0)
    expect_text(script_entry_problems(drop, rec_t['scripts']['chains'], centries), 'differ from the C',
                'the checker\'s chains without 0x8276C0')
    expect_text(script_entry_problems(E2.SCRIPT_ENTRIES + (0x826F00,), rec_t['scripts']['chains'], centries),
                'differ from the C', 'the checker\'s chains with an extra entry')
    fewer = {k: v for k, v in rec_t['scripts']['chains'].items() if int(k, 16) != 0x8276C0}
    expect_text(script_entry_problems(E2.SCRIPT_ENTRIES, fewer, centries), 'tables.json scripts chains',
                'tables.json without chain 0x8276C0')
    # tables.json doors.used against the roster placements and the RAM door row
    roster = (A / 'sub0/roster.emro').read_bytes()
    used = rec_t['doors']['used']['0']
    expect(doors_used_problems(roster, used, [first]), 'doors.used as exported', accept=True)
    expect_text(doors_used_problems(roster, used[1:], [first]), 'differs from the roster placements',
                'doors.used with a door dropped')
    wrong = copy.deepcopy(used)
    wrong[0]['area_change'] = not wrong[0]['area_change']
    expect_text(doors_used_problems(roster, wrong, [first]), 'differs from the roster placements',
                'doors.used with an area-change bit flipped')
    wrong = copy.deepcopy(used)
    wrong[-1]['record'][0] ^= 1
    expect_text(doors_used_problems(roster, wrong, [first]), 'record of door', 'doors.used with a wrong record')
    fake = ram_copy(first, 'door row', [flip(first.ram, DOOR_ROW + 4 * used[0]['door_id'])])
    expect_text(doors_used_problems(roster, used, [fake]), 'record of door', 'the RAM door row against doors.used')
    # the windows
    sc = (A / 'area06_scripts/scripts.emsc').read_bytes()
    for name, bad in (('first byte', mutate(sc, 20)), ('last byte', mutate(sc, len(sc) - 1)),
                      ('entry word', mutate(sc, 12)), ('trailing byte', sc + b'\0'),
                      ('four bytes short', T01.emsc_blob(sc, cut_tail=4))):
        expect(tables_problems(K, [first], {'area06_scripts/scripts.emsc': bad}), f'scripts {name}')
    od = (A / 'overlay_data.emsc').read_bytes()
    for name, bad in (('first byte', mutate(od, 20)), ('last byte', mutate(od, len(od) - 1)),
                      ('base moved', T01.emsc_blob(od, base_shift=4, cut_head=4))):
        expect(tables_problems(K, [first], {'overlay_data.emsc': bad}), f'overlay data {name}')
    if fall is not None:
        tab = json.loads((A / 'tables.json').read_text())
        words = tab['scripts']['run_time_words'][key(fall)]
        swapped = dict(tab['scripts']['run_time_words'])
        swapped[key(fall)] = [hex(int(words[0], 16) + 4)] + words[1:]
        slo, shi = E2.scripts_window(ov)
        expect(window_problems('scripts', sc, (slo, shi - slo), ov[slo - C.OVERLAY_ARENA:shi - C.OVERLAY_ARENA],
                               [fall], swapped, records), 'a run-time word replaced by its neighbour')
    # every table file's last byte
    for rel in ('sub0/roster.emro', 'spawn_table.emsp', 'door_destinations.emsp', 'sub0/message_data.emmd',
                'sub0/world_models.emwm'):
        expect(tables_problems(K, [first], {rel: mutate((A / rel).read_bytes(), -1)}), f'{rel} last byte')
    expect(tables_problems(K, [first], {'sub0/world_models.emwm': b'EMWM'}), 'truncated world models')
    mm = (A / 'sub0/message_data.emmd').read_bytes()
    for cut in ('arec', 'abank', 'grec', 'gbank', 'rows'):
        expect(tables_problems(K, [first], {'sub0/message_data.emmd': T01.mm_blob(mm, cut)}),
               f'message file one {cut} unit short')
    # the roster and the beam's +0x0D rule
    roster = (A / 'sub0/roster.emro').read_bytes()
    _ok, _live, pairs = T01.roster_nodes(T, roster, first.ram)
    fake = ram_copy(first, 'node moved', [flip(first.ram, pairs[1][1] + 0xB1)])
    expect(roster_problems(roster, [fake]), 'a placement node moved off its record')
    fake = ram_copy(first, 'no pool', [(L.POOL_BASE, bytes(L.POOL_STRIDE * L.POOL_SLOTS))])
    expect(roster_problems(roster, [fake]), 'an empty actor pool')
    beam = node_of(first.ram, BEAM, index=11)
    at = placements_at(roster)
    brec = roster[at + 0x28 * 11:at + 0x28 * 13]
    check(beam is not None and (brec[4], brec[0x2C]) == (11, 12),
          f'control: the beam record\'s +4 / REC[0x2C] {brec[4]} / {brec[0x2C]}, not 11 / 12')
    later = [c for c in caps if node_of(c.ram, BEAM) and c.ram[node_of(c.ram, BEAM) + 4] == 2]
    expect(roster_problems(roster, [first] + later), 'the beam in state 4 / 1 and (if run) state 2', accept=True)
    for val, state, what in ((12, 1, 'state 1 with the fallen kind'), (12, 4, 'state 4 with the fallen kind'),
                             (11, 2, 'state 2 with the record kind'), (7, 3, 'state 3 with another kind'),
                             (12, 0, 'state 0 with the fallen kind')):
        fake = ram_copy(first, what, [(beam + 0x0D, bytes([val])), (beam + 4, bytes([state]))])
        expect_text(roster_problems(roster, [fake]), 'lost a copied field', f'beam {what}')
    for val, state, what in ((12, 2, 'state 2 with the fallen kind'), (11, 3, 'state 3 with the record kind'),
                             (12, 3, 'state 3 with the fallen kind')):
        fake = ram_copy(first, what, [(beam + 0x0D, bytes([val])), (beam + 4, bytes([state]))])
        expect([p for p in roster_problems(roster, [fake]) if 'lost a copied field' in p], f'beam {what}',
               accept=True)
    fake = ram_copy(first, '+0x54', [flip(first.ram, beam + 0x54)])
    expect_text(roster_problems(roster, [fake]), 'lost a copied field', 'a placement node +0x54')
    other = next(a for i, a in pairs if C.u32(first.ram, a + 0x10) != BEAM)
    fake = ram_copy(first, '+0x0d', [flip(first.ram, other + 0x0D)])
    expect_text(roster_problems(roster, [fake]), 'lost a copied field', 'a placement node +0x0D')
    # model bindings
    wm = (A / 'sub0/world_models.emwm').read_bytes()
    wtab = C.u32(wm, 8)
    offs = {wtab + (C.s32(first.ram, wtab + 4 + 4 * i) >> 2 << 2) for i in range(C.u32(wm, 32))}
    node = next(a for _s, a in E2.E02.model_owner_nodes(first.ram) if C.u32(first.ram, a + 0x44) in offs)
    expect(model_binding_problems(wm, [ram_copy(first, 'model id', [flip(first.ram, node + 0x0D)])]), 'model id +0x0D')
    expect(model_binding_problems(wm, [ram_copy(first, 'bones', [flip(first.ram, node + 0x0C)])]), 'bone count')
    multi = next((a for _s, a in E2.E02.model_owner_nodes(first.ram) if first.ram[a + 0x0C] >= 2 and
                  C.u32(first.ram, a + 0x44) in offs), None)
    if multi is not None:
        last_slot = multi + 0x110 + 4 * (first.ram[multi + 0x0C] - 1)
        expect_text(model_binding_problems(wm, [ram_copy(first, 'bone slot', [(last_slot, bytes(4))])]),
                    'bone count / slots', 'the last bone slot of a multi-bone owner cleared')
    expect_text(model_binding_problems(wm, [ram_copy(first, 'unbound', [(node + 0x44, bytes(4))])]),
                'model owners bound, not', 'one bound owner fewer')
    # the spawn entry
    expect_text(spawn_entry_problems([ram_copy(first, 'entry', [(0x810702, bytes([SPAWN_ROWS[1]]))])]),
                'outside the', 'the spawn entry one past the sub-0 records')
    expect(spawn_entry_problems([ram_copy(first, 'entry 1', [(0x810702, b'\x01')])]),
           'another spawn entry (1) than D_008106C8 names')
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
    # the id labels and the relocation words
    expect(label_problems(K.lmap, first), 'the id-labelled map', accept=True)
    block_map, _i = A6._BLOCK_BUILD_LOAD_MAP(first)
    by_names = [(a, p, o, s, re.sub(r'f\d\d_id([0-9a-f]{2})\.bin$', r'id\1', l) if l.startswith('chunk10.n') else l)
                for a, p, o, s, l in block_map]
    expect_text(label_problems(by_names, first), 'lies outside',
                'the map labelled by the ids in the extracted files\' names')
    expect_text(label_problems(block_map, first), 'is not a relocation id',
                'the map labelled by the extracted files\' names')
    relabel = [(a, p, o, s, 'chunk10.n0/id44' if l.endswith('/id42') else l) for a, p, o, s, l in K.lmap]
    expect_text(label_problems(relabel, first), 'lies outside', 'the grid row labelled id 0x44')
    relabel = [(a, p, o, s, 'chunk10.n0/id44' if l.endswith('/id45') else l) for a, p, o, s, l in K.lmap]
    expect_text(label_problems(relabel, first), 'lies outside',
                'the dynamic-list row labelled id 0x44 (past the end of id 0x44)')
    relabel = [(a, p, o, s, 'chunk10.n0/id71' if l.endswith('/id7a') else l) for a, p, o, s, l in K.lmap]
    expect_text(label_problems(relabel, first), 'lies outside', 'the last row labelled by an earlier id')
    relabel = [(a, p, o, s, 'chunk10.n0/f02_id44.bin' if l.endswith('/id45') else l) for a, p, o, s, l in K.lmap]
    expect_text(label_problems(relabel, first), 'is not a relocation id', 'a row labelled by a file name')
    nested = [r for r in K.lmap if r[4].startswith('chunk10.n')]
    dropped = [r for r in K.lmap if r is not nested[len(nested) // 2]]
    expect_text(label_problems(dropped, first), 'a gap in the nested load map', 'a nested row dropped')
    expect_text(label_problems([r for r in K.lmap if r[4] != TOP_LABEL], first), 'the top row',
                'the top row dropped')
    for ident in (0x42, 0x41, 0x7A):
        fake = ram_copy(first, f'reloc {ident:#x}', [flip(first.ram, A6.D_0028A490 + 4 * ident + 1)])
        expect_text(A6.relocation_problems(fake)[0], f'D_0028A490[{ident:#x}]', f'relocation word {ident:#x}')
    expect(descriptor_problems(), 'the pinned descriptor', accept=True)
    desc = bytes(C.iso_descriptor(A6.AREA + 4))
    expect_text(descriptor_problems(mutate(desc, 0x100 + 0x30 + 4)), 'not the pinned descriptor',
                'a descriptor with a changed nested relocation word')
    # the level: zone file bytes, the GS state, the bank and list files
    zfile = sorted((A / 'sub0/level').glob('*.emdl'))[0]
    zbuf = zfile.read_bytes()
    b = next(z for zz in _ZONES.values() for lab, z in zz.items() if lab.endswith('/id44'))[0]
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
    banks = sub_path('sfx/area06_banks.bin').read_bytes()
    reg = sub_path('sfx/sfx_registry.emsr').read_bytes()
    for name, bad in (('byte 0', mutate(banks, 0)), ('last byte', mutate(banks, -1)), ('truncated', banks[:-1]),
                      ('row +0x50', mutate(banks, 0x50))):
        expect(sfx_problems(K, [first], bad, reg), f'area06_banks.bin {name}')
    expect_text(sfx_problems(K, [first], banks + b'\0', reg), 'container total', 'container length')
    expect(sfx_problems(K, [first], banks, mutate(reg, len(reg) // 2)), 'registry middle byte')
    expect(sfx_problems(K, [first], banks, mutate(reg, len(reg) - 1)), 'registry last byte')
    handle_slot = 0x281D50 + 4 * (4 * 0x14 + 2)
    expect(sfx_problems(K, [ram_copy(first, 'binding', [(handle_slot, b'\0\0\0\0')])], banks, reg),
           'group 4 slot 2 unbound')
    second = caps[1]
    expect_text(sfx_problems(K, [first, ram_copy(second, 'binding 2', [(handle_slot, b'\0\0\0\0')])], banks, reg),
                'a different bank binding', 'group 4 slot 2 unbound in the second capture only')
    why = E3.E04.refusal_reasons(caps, REFUSED)
    expect(not ('without SShd magic' in why[(3, 0)]), 'the refusal reason (group 3: no magic)', accept=True)
    good = dict(BINDING)
    expect(binding_problem(good, set(REFUSED)), 'the pinned bindings', accept=True)
    expect(binding_problem({**good, (2, 0): ('area', 1)}, set(REFUSED)), 'another row')
    expect(binding_problem({k: v for k, v in good.items() if k != (4, 2)}, set(REFUSED)), 'a binding missing')
    expect(binding_problem(good, set()), 'nothing refused')
    expect(binding_problem(good, set(REFUSED) | {(5, 0)}), 'an extra refused slot')
    # EMCL
    emcl = (A / 'sub0/area06.emcl').read_bytes()
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
    # ctx: a byte of each record; 001D8FD0 alone must rebuild the arrival
    ctx = C.u32(first.ram, L.CTX_PTR)
    for off in (0xA0, 0xC4, 0xFF):
        expect(ctx_problems(K, [ram_copy(first, 'ctx', [flip(first.ram, ctx + off)])])[0], f'ctx +{off:#x}')
    fake = ram_copy(first, 'bit 0x80', [(E1.E22.D_008106C8, bytes([first.ram[E1.E22.D_008106C8] | 0x80]))])
    expect(ctx_problems(K, [fake])[0], 'the spawn word bit 0x80 set (001D1C50 rewrites the block)')
    for cap in caps:
        check(E1.E22.ctx_rebuild(cap, K.elf, setup=False) == E1.E22.ctx_rebuild(cap, K.elf),
              f'control: {cap.name}: 001D1C50 changes the ctx block with bit 0x80 clear')
    # cells.json against its own rows, an unrecorded capture, clean copies
    fake = ram_copy(first, 'unrecorded')
    fake.key = 'unrecorded'
    expect_text(cells_problems(K, cells, [fake], record), 'not recorded in cells.json', 'an unrecorded capture')
    bad = json.loads(json.dumps(record))
    bad['cells']['moved_uids'] = bad['cells']['moved_uids'][:-1]
    expect_text(cells_problems(K, cells, [first], bad), 'is not the union of its rows', 'cells.json moved_uids')
    bad = json.loads(json.dumps(record))
    bad['cells']['flag_calls'][key(first)] = [[E1.FLAG_KEY, 1]]
    expect_text(cells_problems(K, cells, [first], bad), '0019C6F0 calls', 'cells.json flag calls')
    clean = [ram_copy(c, f'clean {c.name}') for c in caps]
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
            C.ELF_PATH, A6.OVERLAY_PATH, C.ISO_PATH]
    missing = [str(p) for p in need if not p.exists()]
    if missing or not A6.ROUTE_A06.exists() or not A6.ARRIVAL.exists():
        print('area06 assets reference: SKIPPED, missing local inputs:', missing or [str(A6.ROUTE_A06)])
        return 0
    t0 = time.process_time()
    w0 = time.time()
    el = L.load_export_level()
    A6.configure(0)
    E1.install()
    pairs = A6.all_captures()
    if any(s != 0 for _c, s in pairs):
        check(False, f'a capture of another sub: {[(c.name, s) for c, s in pairs if s]}')
    excluded = A6.excluded_captures()
    check([n for n, _a in excluded] == ['a06_s0_door1_back'], f'excluded captures {excluded}')
    run = pairs if FULL else [(c, s) for c, s in pairs if c.name in QUICK]
    caps = [c for c, _s in run]
    print(f'area06 assets reference ({MODE}): {len(caps)} of {len(pairs)} captures (all sub 0)')
    for p in descriptor_problems():
        check(False, p)
    K = SimpleNamespace(el=el, elf=C.read_elf(), first=pairs[0][0])
    K.lmap, K.info = C.build_load_map(K.first)
    K.image = C.LoadedImage(K.lmap)
    proxy = ram_copy(K.first, 'sub1 proxy', [(0x810701, b'\1')])
    K.other = C.LoadedImage(C.build_load_map(proxy)[0])
    lib = T01.build_loaders()
    loaded = check_loaders(lib, A)
    for name, ok in loaded.items():
        check(ok, f'port loader rejects {name}')
    print(f'  loaders: {sum(loaded.values())}/{len(loaded)} files accepted by the port loaders')

    problems, V = run_checks(K, caps)
    for p in problems:
        check(False, p)
    lv = V.get('level', {})
    print(f"  load map + level: {lv.get('objects')} bank objects -> {lv.get('zones')} zone EMDL(s), "
          f"{lv.get('dynamic_entries')} dynamic-list entries; {lv.get('kicks')} level kicks and "
          f"{lv.get('dynamic_kicks')} dynamic kicks inside them")
    print(f'  collision, cells, tables, sfx: every comparison over the run\'s captures ({len(problems)} problems)')
    if V['room']:
        print(f"  ctx +0xA0..+0xFF: original 001D8FD0 + 001D1C50 rebuilt the captured bytes in {len(V['room'])} "
              f"capture(s) from room entries {sorted({r['room_entry'] for r in V['room']})}")
    if FAILS:
        print(f'  canary and controls: skipped, {len(FAILS)} check(s) already failed')
    else:
        print(f'  canary: {canary(K, lib, caps[0])} sections each reported their planted difference')
        if not FAILS:
            print(f'  controls: {controls(K, caps, lib)} changed inputs, each caught (or accepted where marked)')
    print(f'area06 assets reference: {"FAIL" if FAILS else "PASS"} ({len(FAILS)} failures, '
          f'{time.process_time() - t0:.1f} s CPU, {time.time() - w0:.1f} s wall)')
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
