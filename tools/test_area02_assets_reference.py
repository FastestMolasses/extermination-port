#!/usr/bin/env python3
"""Check the exported AREA02 assets (assets/area02/, docs/AREA02_ASSETS.md)
against the recorded original captures, and load each one through the port's
own loader.

Inputs (all local, all the user's): the assets written by
tools/export_area02_level.py, export_area02_tables.py and export_area02_sfx.py,
the extracted disc files, the pinned boot ELF and AREA02 overlay, and the
AREA02 captures (the arrival ../Extermination/build/s87/route_a01r/
a01r_03_door16/ and build/s87/route_a02/a02_00..a02_04, a02_s0). Missing
inputs print SKIPPED and exit 0.

The comparators are the AREA01 checker's (tools/test_area01_assets_reference.py,
imported unchanged, with export_area01_common pointed at AREA02 and its
module constants GRID / BUILD / OVERLAY_SHA256 set for AREA02 and the sub):
zone_problems, emsc_header_problems, bank_end, gs_captures,
gs_state_problems, emcl_equal, grid_problems, hulls_inside_table,
roster_image / roster_equal / roster_nodes, emsp_problems with spawn_layout /
doors_layout, messages_problems, world_models_problems, registry_from_ram,
load_map_problems, build_loaders. AREA02's own checks are written here:
every level file per sub, the level without a dynamic list, the directory's
uid words (the ORIGINAL 0019C6F0 under the calls flag_calls derives) and the
orphaned car hull, the trigger table's done words, the sub-1 message-bank
finding and the per-sub sound containers.

Checks (every comparison is exact; each capture is checked against the files
of its own sub, the shared files against every capture):
  load map  per sub, in address order, no overlap, the same map from every
            capture's descriptors; every mapped disc byte equals RAM except
            the cell directory's own bytes (checked below); the other sub's
            map does not fit RAM
  level     sub<N>/level/static_bank.emsc equals RAM at D_0028A5A0 over the
            bank's extent (bank_end); no dynamic-list file; every textured
            level kick REFs a bank object with the class-0 GS state and no
            kernel-0x00237450 kick exists; each zone EMDL equals a rebuild
            from the bank, byte for byte, and its texels the GS freeze decode
            of every capture of its sub
  emcl      sub<N>/area02.emcl equals the RAM grid rebuild (emcl_from_ram) in
            every capture of the sub; D_0028A598 pinned per sub, the grid
            block inside the load map and outside the directory allowance
  cells     sub<N>/area02_cells.bin equals the disc bytes and ends where the
            directory ends; every capture's directory: uid words = the
            ORIGINAL 0019C6F0 run for the derived calls, hulls = the disc,
            the ORIGINAL 001A2370 run for the live owner, or (the freed car)
            orphan_problems; the moved set and the calls equal cells.json
  tables    sub<N>/roster.emro = the rebuild by the original's walks over the
            pinned ELF + overlay and over every capture's RAM, (live, at
            rest) per capture pinned; both EMSP files (window set from the
            ELF walk, every window against RAM); D_008106C8 = +0x1C of the
            current spawn record; scripts and overlay data: extent, bytes =
            the pinned module, RAM equal except the run-time words, each in a
            reached chain record or a trigger done word equal to its
            derivation from the car's x, the set per capture = tables.json;
            sub0/message_data.emmd (every byte); sub 1 has no message file
            and its *D_0028A594 is the top cursor and fails the bank walk
            (finding); world models per sub and every model owner's +0x44 =
            001C6120(table, +0x0D) with its bone count and bone slots, the
            bound count per capture pinned
  sfx       sub<N>/sfx/area02_banks.bin = upload section 0's container on
            the disc; the capture bindings pinned per sub (sub 1: group 4
            refused); sub<N>/sfx/sfx_registry.emsr = the RAM re-derivation
            of every capture of the sub
  ctx       ORIGINAL 001D8FD0 rebuilds ctx +0xA0..+0xFF from the sub's
            room-table entry (flipping the entry changes it)
  loaders   each file through its port loader (compiled privately from src/)
  canary    run_checks again over [a capture, a planted copy of it] per sub
            and a copy of the export tree with file plants; each section
            must report its plant for the copy and stay silent for the
            original; every port loader must refuse a broken copy of its file
  controls  a changed input per comparator, file side and RAM side, and the
            accept cases listed in docs/AREA02_ASSETS.md
Default: 3 captures (the arrival, a02_01, a02_02); EM_TEST_FULL=1: all 7.
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
import export_area02_common as A2  # noqa: E402

C = A2.configure(0)
import test_area01_assets_reference as T01  # noqa: E402
import export_area02_level as E1  # noqa: E402
import export_area02_tables as E2  # noqa: E402
import export_area02_sfx as E3  # noqa: E402
from reference_mode import FULL, MODE  # noqa: E402

L, T, S = E1.L, E2.T, E3.S
A = Path(os.environ.get('EM_AREA02_ASSETS') or A2.OUT).resolve()
BUILD = C.ROOT / 'build/area02/assets/test'
CANARY = BUILD / f'canary-{os.getpid()}'
QUICK = ('a01r_03_door16', 'a02_01_switch', 'a02_02_ladder_escape')
T01.BUILD = BUILD
T01.OVERLAY_SHA256 = A2.OVERLAY_SHA256
# D_0028A598 per sub, measured in every capture (n<N>/f02_id44 + 0x10C800 /
# + 0x79000); as for AREA01 / AREA00 the code that fills it at load is not
# identified, so the measured address is pinned and the grid block must
# lie in the map
GRIDS = {0: 0x13DCA00, 1: 0x13DB640}
FAILS = T01.FAILS
check = T01.check

# (live, at the record's position and rotation) placement nodes per capture
# (T01.roster_nodes); measured, asserted exactly
ROSTER_LIVE = {'a01r_03_door16': (14, 14), 'a02_s0_mts_bed': (14, 14), 'a02_00_duct': (54, 53),
               'a02_01_switch': (54, 51), 'a02_02_ladder_escape': (49, 48), 'a02_03_over_wreck': (49, 48),
               'a02_04_panel': (49, 48)}
# model owners bound to a bank model per capture; asserted exactly
MODEL_OWNERS = {'a01r_03_door16': 14, 'a02_s0_mts_bed': 14, 'a02_00_duct': 41, 'a02_01_switch': 41,
                'a02_02_ladder_escape': 31, 'a02_03_over_wreck': 31, 'a02_04_panel': 31}
# the capture bindings per sub, (group, slot) -> (container, row); 'area' =
# the sub's area02_banks.bin; and the refused slots
BINDING = {0: {(1, 0): ('global', 0), (1, 1): ('global', 1), (1, 2): ('global', 2),
               (2, 0): ('area', 0), (4, 0): ('area', 1), (4, 1): ('area', 2)},
           1: {(1, 0): ('global', 0), (1, 1): ('global', 1), (1, 2): ('global', 2), (2, 0): ('area', 0)}}
REFUSED = {0: {(3, 0)}, 1: {(3, 0), (4, 0), (4, 1)}}
SPAWN_ROWS = {0: (0x24B560, 7), 1: (0x24B6B0, 7)}
DOOR_ROW = 0x24DFC0
CANARY_TEST_1 = 0x8153A0      # where the level kicks take TEST_1 (0x5000D), as in AREA01 / AREA00


def key(cap):
    """The capture name the pinned tables use (a planted copy keeps its source's)."""
    return getattr(cap, 'key', cap.name)


def sub_of(cap):
    return cap.ram[0x810701]


def sub_path(sub, rel):
    return A / f'sub{sub}' / rel


# ---------------------------------------------------------------------------
# Load map


def load_problems(K, sub, caps, cells_len):
    out = list(T01.load_map_problems(K.lmap[sub]))
    image, other = K.image[sub], K.image[1 - sub]
    table = C.u32(K.first[sub].spad, L.SPAD_CELLS)
    for cap in caps:
        try:
            A2.configure(sub)
            lmap, _info = C.build_load_map(cap)
        except SystemExit as error:
            out.append(f'{cap.name}: load map: {error}')
            continue
        finally:
            A2.configure(0)
        if lmap != K.lmap[sub]:
            out.append(f'{cap.name}: a different load map')
        if C.u32(cap.spad, L.SPAD_CELLS) != table:
            out.append(f'{cap.name}: cell directory pointer')
        rows = C.compare_load_map(image, cap, allow=[(table, table + cells_len)])
        if any(r['unexpected_rows'] for r in rows):
            out.append(f'{cap.name}: load map differs from RAM')
        # the other sub's nested files against this RAM
        nested = [m for m in other.map if '.n' in m[4]]
        if all(other.read(a, n) == cap.ram[a:a + n] for a, _p, _o, n, _l in nested):
            out.append(f'{cap.name}: the sub-{1 - sub} load also fits RAM')
    return out


# ---------------------------------------------------------------------------
# Level


_ZONES = {}


def bank_problems(sub, bank_img, caps):
    """The static bank's EMSC header, and against every capture: D_0028A5A0,
    every byte over its length, and its length = bank_end over RAM."""
    out = T01.emsc_header_problems(f'sub{sub} static bank', bank_img)
    base, _entry, length = struct.unpack_from('<3I', bank_img, 8)
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


def level_problems(K, sub, caps):
    """The static bank, the absence of a dynamic list, the kicks and the zone
    EMDLs of one sub. Returns (problems, summary)."""
    out, el, image = [], K.el, K.image[sub]
    lvl = sub_path(sub, 'level')
    bank_img = (lvl / 'static_bank.emsc').read_bytes()
    out += bank_problems(sub, bank_img, caps)
    base, _entry, length = struct.unpack_from('<3I', bank_img, 8)
    bank = bank_img[20:20 + length]
    if (lvl / 'dynamic_objects.emsc').exists():
        out.append(f'sub{sub}: a dynamic-list file (no capture draws one)')
    for cap in caps:
        if image.locate(C.u32(cap.ram, E1.D_0028A5A4)) is not None:
            out.append(f'{cap.name}: D_0028A5A4 lies in the load map')
    read = lambda a, n: bank[a - base:a - base + n]
    try:
        objects = L.bank_objects(read, base)
        kicks, states, _touched = L.check_kicks(caps, objects, [])
    except (SystemExit, struct.error, ValueError) as error:
        return out + [f'sub{sub} level: {error}'], {}
    prim = el.level_template_prim(el.BootElf(C.ELF_PATH))
    out += T01.gs_state_problems(states, T01.level_gs_state(el, prim))

    class BankImage:
        def read(self, a, n):
            return read(a, n)

    zkey = (sub, bank_img)
    if zkey not in _ZONES:
        _ZONES[zkey] = L.build_zones(el, BankImage(), objects, lambda a: image.locate(a)[4])
    zones = _ZONES[zkey]
    try:
        gs_caps = T01.gs_captures(caps)
    except SystemExit as error:
        return out + [str(error)], {}
    files = sorted(str(x) for x in lvl.glob('*.emdl'))
    if len(files) != len(zones):
        out.append(f'sub{sub}: {len(files)} zone files for {len(zones)} source files')
    order = [l for _a, _p, _o, _s, l in image.map]
    for path, (label, (b, _ids, bad, _recs)) in zip(files, sorted(zones.items(), key=lambda z: order.index(z[0]))):
        if bad:
            out.append(f'{label}: records with a matrix slot')
        name = Path(path).name
        if name.split('_', 1)[1] != Path(label).name.split('.')[0] + '.emdl':
            out.append(f'sub{sub}/{name}: zone name')
        out += T01.zone_problems(el, f'sub{sub}/{name}', Path(path).read_bytes(), b, gs_caps, prim)
    return out, dict(objects=len(objects), zones=len(files), kicks=sum(k['level_kicks'] for k in kicks))


# ---------------------------------------------------------------------------
# Collision and cells


def emcl_problems(K, sub, emcl, caps, cells_len):
    out = []
    T01.GRID = GRIDS[sub]
    for cap in caps:
        table = C.u32(cap.spad, L.SPAD_CELLS)
        for problem in T01.grid_problems(cap.ram, K.image[sub], (table, table + cells_len)):
            out.append(f'{cap.name}: {problem}')
        where = T01.emcl_equal(emcl, cap.ram)
        if where is not None:
            out.append(f'{cap.name}: sub{sub}/area02.emcl differs from the RAM grid rebuild: {where}')
    return out


def cells_problems(K, sub, cells, caps, record):
    """The directory file against the disc, and every capture's RAM
    directory through export_area02_level.verify_directory (0019C6F0 for
    the uid words, 001A2370 for the hulls, orphan_problems for the freed
    car's hull). `record` = sub<N>/cells.json."""
    out = []
    table = C.u32(K.first[sub].spad, L.SPAD_CELLS)
    try:
        disc = K.image[sub].read(table, len(cells))
    except ValueError as error:
        return [f'sub{sub} cells: {error}']
    if cells != disc:
        k = next(j for j in range(len(cells)) if cells[j] != disc[j])
        out.append(f'sub{sub}/area02_cells.bin differs from the disc bytes at +{k:#x}')
    try:
        _count, _hulls, size = L.cell_directory(cells, 0)
        if size != len(cells):
            out.append(f'sub{sub}/area02_cells.bin is {len(cells)} bytes, the directory {size}')
        inside = T01.hulls_inside_table(L, cells)
        if inside:
            out.append(f'sub{sub} cells: hulls {inside} start inside the uid table')
        rows, problems, calls = E1.verify_directory(K.elf, cells, caps)
    except (AssertionError, IndexError, struct.error, ValueError) as error:
        return out + [f'sub{sub} cells: the directory does not parse ({error!r})']
    out += problems
    union = sorted({u for x in record['cells']['captures'] for u in x['moved_hulls']})
    if record['cells']['moved_uids'] != union:
        out.append(f'sub{sub}/cells.json moved_uids {record["cells"]["moved_uids"]} is not the union of its rows {union}')
    for r in rows:
        want = next((x['moved_hulls'] for x in record['cells']['captures'] if x['capture'] == key(
            next(c for c in caps if c.name == r['capture']))), None)
        if want is None:
            out.append(f'{r["capture"]}: not recorded in cells.json')
        elif [int(u) for u in r['moved_hulls']] != [int(u) for u in want]:
            out.append(f'{r["capture"]}: re-transformed hulls {r["moved_hulls"]} != cells.json {want}')
    for cap in caps:
        got = [list(c) for c in calls.get(cap.name, ())]
        if cap.name in calls and got != record['cells']['flag_calls'].get(key(cap)):
            out.append(f'{cap.name}: 0019C6F0 calls {got} != cells.json')
    return out


# ---------------------------------------------------------------------------
# Tables


def roster_problems(sub, roster, caps):
    out = []
    A2.configure(sub)
    try:
        disc = T01.roster_image(T01.disc_reader())
        if roster != disc:
            out.append(f'sub{sub}/roster.emro differs from the rebuild from the disc')
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
        out.append(f'sub{sub} roster: {error!r}')
    finally:
        A2.configure(0)
    return out


def window_problems(label, blob, window, disc, caps, record, records):
    """An EMSC data window: header, extent = `window`, bytes = the module,
    and against every capture's RAM except its run-time words, which must
    each be explained (E2.run_time_words) and equal tables.json's list."""
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
            for slot, a in E2.model_owner_nodes(ram):
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
    """D_008106C8 = word +0x1C of the capture's current spawn record (area 2,
    sub D_00810701, entry D_00810702), the records read from the pinned ELF."""
    out = []
    dread = T01.disc_reader()
    table = C.u32(dread(T.D_0024D650 + 4 * C.AREA, 4), 0)
    for cap in caps:
        sub, room = sub_of(cap), cap.ram[0x810702]
        entries = C.u32(dread(table + 4 * sub, 4), 0)
        count = (C.u32(dread(table + 4 * (sub + 1), 4), 0) - entries) // 0x30
        if room >= count:
            out.append(f'{cap.name}: spawn entry {room} outside the {count} sub-{sub} records')
            continue
        want = C.u32(dread(entries + 0x30 * room + 0x1C, 4), 0)
        got = C.u32(cap.ram, T.D_008106C8)
        if got != want:
            out.append(f'{cap.name}: D_008106C8 {got:#x} != +0x1C of spawn record {room} ({want:#x})')
    return out


def message_bank_finding(caps):
    """Sub 1 (finding): *D_0028A594 is the top cursor D_0028A73C (the value
    AREA01 left) and export_message_data's bank walk refuses the resident
    bytes there. [] when that holds in every sub-1 capture."""
    import export_message_data as M
    out = []
    for cap in caps:
        bank = C.u32(cap.ram, T.D_0028A594)
        if bank != C.u32(cap.ram, 0x28A73C):
            out.append(f'{cap.name}: *D_0028A594 {bank:#x} is not the top cursor')
            continue
        try:
            M.bank_extent(cap.ram[bank:0x2000000], 0)
            out.append(f'{cap.name}: the sub-1 area bank at {bank:#x} parses (the finding no longer holds)')
        except (ValueError, struct.error):
            pass
    return out


def tables_problems(K, caps_by_sub, files=None):
    files = files or {}
    read = lambda rel: files[rel] if rel in files else (A / rel).read_bytes()
    caps = [c for s in sorted(caps_by_sub) for c in caps_by_sub[s]]
    out = []
    try:
        for sub, sc in sorted(caps_by_sub.items()):
            out += roster_problems(sub, read(f'sub{sub}/roster.emro'), sc)
        for name, layout in (('spawn_table.emsp', T01.spawn_layout), ('door_destinations.emsp', T01.doors_layout)):
            out += T01.emsp_problems(name, read(name), caps, layout(T01.disc_reader()))[0]
        out += spawn_entry_problems(caps)
        rec = json.loads(read('tables.json'))
        ov = A2.read_overlay()
        olo, ohi = E2.overlay_data_window(ov)
        odisc = ov[olo - C.OVERLAY_ARENA:ohi - C.OVERLAY_ARENA]
        records = E2.chain_records(odisc, olo)
        slo, shi = E2.scripts_window(ov)
        sdisc = ov[slo - C.OVERLAY_ARENA:shi - C.OVERLAY_ARENA]
        out += window_problems('scripts', read('area02_scripts/scripts.emsc'), (slo, shi - slo), sdisc, caps,
                               rec['scripts']['run_time_words'], records)
        out += window_problems('overlay data', read('overlay_data.emsc'), (olo, ohi - olo), odisc, caps,
                               rec['overlay_data']['run_time_words'], records)
        for sub, sc in sorted(caps_by_sub.items()):
            if sub == 0:
                out += T01.messages_problems(read('sub0/message_data.emmd'), sc, K.elf)[0]
            else:
                if 'sub1/message_data.emmd' in files or (A / 'sub1/message_data.emmd').exists():
                    out.append('sub1: a message file (the sub has no area bank: finding)')
                out += message_bank_finding(sc)
            wm = read(f'sub{sub}/world_models.emwm')
            out += T01.world_models_problems(wm, sc)[0]
            out += model_binding_problems(wm, sc)
    except (SystemExit, ValueError, KeyError, IndexError, struct.error) as error:
        out.append(f'tables: {error!r}')
    return out


# ---------------------------------------------------------------------------
# Sound


def binding_problem(sub, got, refused):
    """True unless the bindings are exactly BINDING[sub] and the refused set REFUSED[sub]."""
    return got != BINDING[sub] or set(refused) != REFUSED[sub]


def sfx_problems(K, sub, caps, banks, registry):
    out = []
    try:
        gname = S.X.GLOBAL_CONTAINER
        gdata = (C.DECOMP / gname).read_bytes()
        A2.configure(sub)
        try:
            _label, disc, _parsed, _names, first = E3.area_container(K.first[sub])
        finally:
            A2.configure(0)
        if banks != disc:
            k = next((j for j in range(min(len(banks), len(disc))) if banks[j] != disc[j]), min(len(banks), len(disc)))
            out.append(f'sub{sub}/sfx/area02_banks.bin differs from the disc container at +{k:#x} '
                       f'({len(banks)} / {len(disc)})')
        total = C.u32(banks, 0) if len(banks) >= 4 else -1
        if len(banks) != total or total > first[1]:
            out.append(f'sub{sub}/sfx/area02_banks.bin: {len(banks)} bytes, container total {total:#x}')
        parsed = S.X.A.parse_container(banks)
        if parsed is None:
            return out + [f'sub{sub}/sfx/area02_banks.bin is not an SShd container']
        name = f'area02_sub{sub}_banks.bin'
        containers = {gname: (gdata, S.X.A.parse_container(gdata)), name: (banks, parsed)}
        bound, refused, _rep = S.bindings_from_captures(caps, containers)
        got = {k: ('global' if v[0][0] == gname else 'area', v[0][1]) for k, v in bound.items()}
        if binding_problem(sub, got, refused):
            return out + [f'sub{sub}: bank bindings {got}, refused {sorted(refused)}']
        for cap in caps:
            # the samples from the disc container (the file is compared with it above)
            want, _counts, _n, _e = T01.registry_from_ram(cap.ram, bound, {gname: gdata, name: disc})
            if registry != want:
                k = next((j for j in range(min(len(registry), len(want))) if registry[j] != want[j]),
                         min(len(registry), len(want)))
                out.append(f'{cap.name}: sub{sub}/sfx/sfx_registry.emsr differs from the RAM re-derivation at +{k:#x}')
    except (SystemExit, ValueError, TypeError, KeyError, IndexError, StopIteration, struct.error) as error:
        out.append(f'sub{sub} sfx: {error!r}')
    return out


# ---------------------------------------------------------------------------


def run_checks(K, caps_by_sub):
    """Every real check over the export tree A; returns (problems, summary)."""
    P, V = [], {}
    for sub, sc in sorted(caps_by_sub.items()):
        cells = sub_path(sub, 'area02_cells.bin').read_bytes()
        record = json.loads(sub_path(sub, 'cells.json').read_text())
        P += load_problems(K, sub, sc, len(cells))
        p, V[f'level{sub}'] = level_problems(K, sub, sc)
        P += p
        P += emcl_problems(K, sub, sub_path(sub, 'area02.emcl').read_bytes(), sc, len(cells))
        P += cells_problems(K, sub, cells, sc, record)
        P += sfx_problems(K, sub, sc, sub_path(sub, 'sfx/area02_banks.bin').read_bytes(),
                          sub_path(sub, 'sfx/sfx_registry.emsr').read_bytes())
    P += tables_problems(K, caps_by_sub)
    V['room'] = []
    for sub, sc in sorted(caps_by_sub.items()):
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                V['room'] += L.check_ctx_room_block(sc, K.elf)
        except SystemExit as error:
            P.append(f'ctx room block: {error}')
    return P, V


# ---------------------------------------------------------------------------
# Port loaders


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
    out['spawn'] = ok and all(bool(lib.em_spawn_table_read(table, a, n * 0x30)) for a, n in SPAWN_ROWS.values())
    doors = big()
    ok = rc('em_spawn_table_load', doors, p('door_destinations.emsp')) == 0
    out['doors'] = ok and bool(lib.em_spawn_table_read(doors, 0x24E140, 4)) and \
        bool(lib.em_spawn_table_read(doors, DOOR_ROW, 0x10))
    for rel in ('area02_scripts/scripts.emsc', 'overlay_data.emsc'):
        out[rel] = rc('em_script_image_load', big(), p(rel)) == 1
    for sub in A2.SUBS:
        wm = (tree / f'sub{sub}/world_models.emwm').read_bytes()
        out[f'sub{sub}/world models'] = rc('em_world_models_parse', big(), wm, CT.c_size_t(len(wm))) == 0
        out[f'sub{sub}/emcl'] = rc('em_collision_load', big(), p(f'sub{sub}/area02.emcl')) == 0
        out[f'sub{sub}/roster.emro'] = rc('em_actor_roster_load', big(), p(f'sub{sub}/roster.emro')) == 0
        out[f'sub{sub}/level/static_bank.emsc'] = rc('em_script_image_load', big(),
                                                     p(f'sub{sub}/level/static_bank.emsc')) == 1
        for z in sorted(str(x) for x in (tree / f'sub{sub}/level').glob('*.emdl')):
            out[f'sub{sub}/{Path(z).name}'] = rc('em_model_load', big(), z.encode()) == 0
        out[f'sub{sub}/sfx registry'] = rc('em_sfx_registry_load', big(), p(f'sub{sub}/sfx/sfx_registry.emsr')) == 1
    with T01.quiet_fds():
        out['sub0/message data'] = rc('em_message_live_install', p('sub0/message_data.emmd')) == 1
    cells = {sub: rc('em_actor_cells_load', big(), p(f'sub{sub}/area02_cells.bin')) for sub in A2.SUBS}
    return out, cells


def cells_findings_ok(rc, diag):
    """The loader verdicts docs/AREA02_ASSETS.md records: the sub-0 directory
    is refused (uid 0's word carries bit 29, AREA01_ASSETS.md finding 1)
    and loads once bit 29 is cleared in every uid word; sub 1's loads."""
    return rc.get(0) == -1 and diag == 0 and rc.get(1) == 0


def cells_diagnostic(lib, tree, cells_bytes=None):
    """em_actor_cells_load over a copy of sub0/area02_cells.bin with bit 29
    cleared in every uid word (nothing else changed)."""
    BUILD.mkdir(parents=True, exist_ok=True)
    g = lib.em_actor_cells_load
    g.restype = CT.c_int
    cells = bytearray(cells_bytes if cells_bytes is not None else (tree / 'sub0/area02_cells.bin').read_bytes())
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
    for _s, a in E1.pool(ram):
        if C.u32(ram, a + 0x10) == behaviour and (index is None or ram[a + 0x9A] == index) and \
                (uid is None or ram[a + 0x0F] == uid):
            return a
    return None


def canary_plants(K, cap, tree, tag):
    """RAM plants for one sub's canary copy of `cap` -> (copy, {section: text})."""
    sub = sub_of(cap)
    ram = cap.ram
    edits, want = [], {}
    bank = sub_path(sub, 'level/static_bank.emsc').read_bytes()
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
    edits.append(flip(ram, CANARY_TEST_1, 2))      # TEST_1 of the level kicks' state
    want['GS state'] = 'level GS state'
    ctx = C.u32(ram, L.CTX_PTR)
    edits.append(flip(ram, ctx + 0xA4))
    want['ctx'] = f'ctx room block: {tag}:'
    table = C.u32(cap.spad, L.SPAD_CELLS)
    cells = sub_path(sub, 'area02_cells.bin').read_bytes()
    _count, hulls, _size = L.cell_directory(cells, 0)
    s0, _e0, _f0 = hulls[min(hulls)]
    edits.append(flip(ram, table + s0 + 0x20))
    want['cell hull'] = f'{tag}: hull {min(hulls)} differs'
    grid = GRIDS[sub]
    edits.append(flip(ram, grid + C.u32(ram, grid) + 1))
    want['EMCL'] = f'{tag}: sub{sub}/area02.emcl differs from the RAM grid rebuild'
    roster = sub_path(sub, 'roster.emro').read_bytes()
    paddr = C.u32(roster, 0x10)
    edits.append(flip(ram, paddr + 0x28 * C.u32(roster, 0x0C) - 1))
    want['roster'] = f'{tag}: roster records differ from RAM'
    _ok, _live, pairs = T01.roster_nodes(T, roster, ram)
    edits.append(flip(ram, pairs[0][1] + 0x03))
    want['placement node fields'] = f'{tag}: a live placement node lost a copied field'
    edits.append(flip(ram, DOOR_ROW + 0x0F))
    want['door windows'] = f'{tag}: door_destinations.emsp window {DOOR_ROW:#x} differs'
    rows, n = SPAWN_ROWS[sub]
    edits.append(flip(ram, rows + n * 0x30 - 1))   # the last byte of the sub's spawn rows
    want['spawn windows'] = f'{tag}: spawn_table.emsp window'
    edits.append(flip(ram, min(E2.SCRIPT_ENTRIES) + 0x10))   # word +0x10 of the first chain's first record
    want['script words'] = f'{tag}: scripts run-time words'
    # a trigger done word the car's x does not explain (record 2's, x 222.8);
    # without a car (sub 1) any rewritten trigger word is reported
    edits.append((E2.TRIGGERS + 2 * E2.TRIGGER_SIZE + 4, struct.pack('<I', 1)))
    want['trigger word'] = (f'{tag}: overlay data: {E2.TRIGGERS + 2 * E2.TRIGGER_SIZE + 4:#x}' if sub == 0 else
                            f'{tag}: overlay data: trigger table rewritten')
    area_records = C.u32(K.elf, T01.MSG_TABLES + 4 * (C.AREA + 1) - C.ELF_VADDR + C.ELF_OFFSET)
    if sub == 0:
        edits.append(flip(ram, area_records + 5))
        want['message records'] = f'{tag}: message records differ'
    else:
        edits.append((T.D_0028A594, struct.pack('<I', C.u32(ram, 0x28A73C) + 4)))
        want['message bank finding'] = f'{tag}: *D_0028A594'
    wm = sub_path(sub, 'world_models.emwm').read_bytes()
    edits.append(flip(ram, C.u32(wm, 8) + C.u32(wm, 12) - 1))
    want['world models'] = f'{tag}: world model bank differs'
    edits.append(flip(ram, T01.LADDER + 2 * 0x40))
    want['registry'] = f'{tag}: sub{sub}/sfx/sfx_registry.emsr differs'
    # uid 1's word: bit 30 toggled (sub 0's copy is a02_01: no call made yet;
    # sub 1 makes none)
    edits.append((table + 8 + 3, bytes([ram[table + 8 + 3] ^ 0x40])))
    want['uid word'] = f'{tag}: directory bytes differ at'
    # the matrix of the first hull this capture shows re-transformed by a
    # live owner (cells.json), one byte flipped
    rec = json.loads(sub_path(sub, 'cells.json').read_text())
    moved = next((r['moved_hulls'] for r in rec['cells']['captures'] if r['capture'] == key(cap)), [])
    for uid in moved:
        owner = next((a for _s, a in E1.pool(ram) if ram[a + 0x0F] == int(uid)
                      and E1.owner_matrix(ram, a) is not None), None)
        if owner is not None:
            edits.append(flip(ram, E1.owner_matrix(ram, owner) + 0x33, 0x04))
            want['owner matrix'] = f'{tag}: hull {uid} differs and no owner derivation reproduces it'
            break
    wtab = C.u32(wm, 8)
    offs = [wtab + (C.s32(ram, wtab + 4 + 4 * i) >> 2 << 2) for i in range(C.u32(ram, wtab))]
    slot, owner = next((s, a) for s, a in E2.model_owner_nodes(ram) if C.u32(ram, a + 0x44) in offs
                       and (ram[a + 0x0D] ^ 1) < len(offs) and offs[ram[a + 0x0D] ^ 1] != C.u32(ram, a + 0x44))
    edits.append(flip(ram, owner + 0x0D))
    want['model binding'] = f'{tag} slot {slot}: +0x44 != 001C6120(table, +0x0D)'
    edits.append(flip(ram, T.D_008106C8 + 1))
    want['spawn entry word'] = f'{tag}: D_008106C8 '
    gs = bytearray(cap.gs.read_bytes())
    zb = next(v for (s, _b), z in _ZONES.items() if s == sub for v in z.values())[0]
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


def canary(K, lib, caps_by_sub):
    """run_checks over {sub: [capture, planted copy]} and a tree with file
    plants; each section must report its plant for the copy and stay silent
    for the original. Returns the number of sections."""
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
    want, originals = {}, {}
    canary_caps = {}
    for sub in sorted(caps_by_sub):
        cap = caps_by_sub[sub][0]
        fake, w = canary_plants(K, cap, tree, f'canary{sub}')
        want.update({f'sub{sub} {k}': v for k, v in w.items()})
        originals.update({f'sub{sub} {k}': cap.name for k in w})
        canary_caps[sub] = [cap, fake]
    zones0 = sorted(x.name for x in (real / 'sub0/level').glob('*.emdl'))
    zones1 = sorted(x.name for x in (real / 'sub1/level').glob('*.emdl'))
    plant('sub0/level/dynamic_objects.emsc', C.emsc(0x177A940, bytes(16)))
    want['dynamic list file'] = 'sub0: a dynamic-list file'
    plant('sub0/level/00_a_extra.emdl', (real / f'sub0/level/{zones0[0]}').read_bytes())
    want['zone count'] = f'sub0: {len(zones0) + 1} zone files for {len(zones0)} source files'
    want['zone name'] = 'sub0/00_a_extra.emdl: zone name'
    cap0 = caps_by_sub[0][0]
    rec = json.loads((real / 'sub0/cells.json').read_text())
    row = next(r for r in rec['cells']['captures'] if r['capture'] == key(cap0))
    row['moved_hulls'] = row['moved_hulls'][:-1]
    plant('sub0/cells.json', json.dumps(rec).encode())
    want['moved set'] = f'{cap0.name}: re-transformed hulls'
    plant('sub0/area02_cells.bin', mutate((real / 'sub0/area02_cells.bin').read_bytes(), -1))
    want['cells file'] = 'sub0/area02_cells.bin differs from the disc bytes'
    plant('sub1/sfx/area02_banks.bin', mutate((real / 'sub1/sfx/area02_banks.bin').read_bytes(), 0x20000))
    want['sound bank'] = 'sub1/sfx/area02_banks.bin differs from the disc container'
    plant('sub1/roster.emro', mutate((real / 'sub1/roster.emro').read_bytes(), 0x09))
    want['roster file'] = 'sub1/roster.emro differs from the rebuild from the disc'
    sc = (real / 'area02_scripts/scripts.emsc').read_bytes()
    plant('area02_scripts/scripts.emsc', mutate(sc, len(sc) - 1))
    want['scripts file'] = 'scripts differs from the disc overlay'
    tab = json.loads((real / 'tables.json').read_text())
    words = tab['overlay_data']['run_time_words'].get(key(cap0), [])
    tab['overlay_data']['run_time_words'][key(cap0)] = words[1:] if words else ['0x825680']
    plant('tables.json', json.dumps(tab).encode())
    want['overlay run-time set'] = f'{cap0.name}: overlay data run-time words'
    plant('sub1/message_data.emmd', (real / 'sub0/message_data.emmd').read_bytes())
    want['sub1 message file'] = 'sub1: a message file'
    n = len(FAILS)
    try:
        A = tree
        with contextlib.redirect_stdout(io.StringIO()):
            got, _v = run_checks(K, canary_caps)
        # the zone file itself (its own pass: a structure difference stops
        # zone_problems before the texels, which the pass above plants)
        zbuf = (real / f'sub1/level/{zones1[0]}').read_bytes()
        plant(f'sub1/level/{zones1[0]}', mutate(zbuf, T01.emdl_parts(zbuf)['verts'] + 1))
        want['zone EMDL'] = f'sub1/{zones1[0]} differs from a rebuild'
        got += level_problems(K, 1, caps_by_sub[1][:1])[0]
        (tree / 'sub1/message_data.emmd').unlink()
        loader_want = plant_loader_canary(tree, plant)
        with contextlib.redirect_stdout(io.StringIO()), T01.quiet_fds():
            loaded, _cells = check_loaders(lib, tree)
    finally:
        A = real
        remove_tree()
    del FAILS[n:]
    for section, text in want.items():
        check(any(text in x for x in got), f'canary: section {section} did not report "{text}"')
        if section in originals:
            quiet = text.replace(section.split()[0].replace('sub', 'canary'), originals[section])
            if quiet != text:
                check(not any(quiet in x for x in got), f'canary: section {section} reported the original too')
    for name in loader_want:
        check(not loaded.get(name, True), f'canary: loader {name} accepted a broken file')
    return len(want) + len(loader_want)


def plant_loader_canary(tree, plant):
    """Replace each loaded file in the tree by one its loader must refuse;
    returns the check_loaders names. The spawn file loses the last byte of
    the sub-1 rows (its window cut short); the door file keeps only the pointer
    array; an EMSC keeps its header with a 32-byte window; every other file
    is cut to its first 16 bytes."""
    spawn = T01.emsp_windows((tree / 'spawn_table.emsp').read_bytes())
    end1 = SPAWN_ROWS[1][0] + 0x30 * SPAWN_ROWS[1][1]
    plant('spawn_table.emsp', T01.emsp_file([(a, d[:end1 - 1 - a] if a <= SPAWN_ROWS[1][0] < a + len(d) else d)
                                             for a, d in spawn]))
    doors = T01.emsp_windows((tree / 'door_destinations.emsp').read_bytes())
    plant('door_destinations.emsp', T01.emsp_file([(a, d) for a, d in doors if a == T01.D_0024E140]))
    names = ['spawn', 'doors']
    for rel in ('area02_scripts/scripts.emsc', 'overlay_data.emsc', 'sub0/level/static_bank.emsc',
                'sub1/level/static_bank.emsc'):
        blob = (tree / rel).read_bytes()
        base, entry = struct.unpack_from('<2I', blob, 8)
        plant(rel, C.emsc(base, blob[20:52], entry))
        names.append(rel)
    cut = [('sub0/message_data.emmd', 'sub0/message data')]
    for sub in A2.SUBS:
        cut += [(f'sub{sub}/roster.emro', f'sub{sub}/roster.emro'),
                (f'sub{sub}/world_models.emwm', f'sub{sub}/world models'),
                (f'sub{sub}/area02.emcl', f'sub{sub}/emcl'),
                (f'sub{sub}/sfx/sfx_registry.emsr', f'sub{sub}/sfx registry')]
    for rel, name in cut:
        plant(rel, (tree / rel).read_bytes()[:16])
        names.append(name)
    for sub in A2.SUBS:
        for z in sorted(tree.joinpath(f'sub{sub}/level').glob('*.emdl')):
            plant(f'sub{sub}/level/{z.name}', z.read_bytes()[:16])
            names.append(f'sub{sub}/{z.name}')
    return names


# ---------------------------------------------------------------------------
# Controls


def controls(K, caps_by_sub):
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
    by_name = {c.name: c for s in caps_by_sub.values() for c in s}
    cap1 = caps_by_sub[1][0]
    live = by_name.get('a02_01_switch') or caps_by_sub[0][0]     # the car live, no call made
    after = by_name.get('a02_02_ladder_escape') or caps_by_sub[0][-1]   # calls made, the car freed
    record = json.loads((A / 'sub0/cells.json').read_text())
    cells = (A / 'sub0/area02_cells.bin').read_bytes()
    count = C.u32(cells, 0)
    table = C.u32(live.spad, L.SPAD_CELLS)
    _c, hulls, _s = L.cell_directory(cells, 0)
    # cells: the file side
    for at in (0, 4, 4 + 4 * count, len(cells) - 1):
        expect(cells_problems(K, 0, mutate(cells, at), [live], record), f'cells file +{at:#x}')
    expect_text(cells_problems(K, 0, cells + b'\0', [live], record), 'bytes, the directory', 'cells size check')
    # uid words: the derived calls
    expect(E1.flag_calls(after.ram) != ((1, 1), (0x1D, 1), (0x1E, 1)), 'the calls of a02_02', accept=True)
    expect(E1.flag_calls(live.ram) != (), 'no call in a02_01', accept=True)
    kind8 = node_of(live.ram, E1.DISPATCH, index=33)
    fake = ram_copy(live, 'kind 8 done', [(kind8 + 4, b'\x03')])
    expect(E1.flag_calls(fake.ram) != ((1, 1),), 'a kind-8 node out of state 1 made (1, 1)', accept=True)
    expect(E1.verify_directory(K.elf, cells, [fake])[1], 'the (1, 1) call without its cleared word')
    gate = node_of(live.ram, E1.GATE, index=38)
    fake = ram_copy(live, 'gate done', [(gate, b'\x00')])
    expect(E1.flag_calls(fake.ram) != ((0x1D, 1), (0x1E, 1)), 'a freed gate [38] made its calls', accept=True)
    switch = node_of(after.ram, E1.DISPATCH, index=31)
    fake = ram_copy(after, 'switch state 3', [(switch + 4, b'\x03')])
    expect(E1.verify_directory(K.elf, cells, [live, fake])[1], 'load-time calls (the switch not in state 1)')
    fake = ram_copy(after, 'setter code', [flip(after.ram, E1.FLAG_SETTER + 0x13F)])
    expect(E1.verify_directory(K.elf, cells, [live, fake])[1], '0019C6F0 code in RAM')
    fake = ram_copy(after, 'bit 30 kept', [(table + 8 + 3, bytes([after.ram[table + 8 + 3] | 0x40]))])
    expect(E1.verify_directory(K.elf, cells, [live, fake])[1], 'uid 1 word with bit 30 after the call')
    fake = ram_copy(live, 'bit 30 cleared', [(table + 8 + 3, bytes([live.ram[table + 8 + 3] & 0xBF]))])
    expect(E1.verify_directory(K.elf, cells, [fake])[1], 'uid 1 bit 30 cleared without a call')
    short = struct.pack('<I', 1) + cells[4:]
    try:
        E1.derive_words(K.elf, short, after)
        raised = []
    except ValueError as error:
        raised = [str(error)]
    expect(raised, '0019C6F0 on a directory whose count stops before the uids')
    # moved hulls: each owner's hull byte and matrix
    for uid in (46, 48):
        s, e, _f = hulls[uid]
        fake = ram_copy(live, f'hull {uid}', [flip(live.ram, table + e - 1)])
        expect(E1.verify_directory(K.elf, cells, [fake])[1], f'moved hull {uid} last byte')
    pick = node_of(live.ram, E1.PICKUP, uid=48)
    fake = ram_copy(live, 'matrix 48', [flip(live.ram, pick + 0xD0 + 0x32, 0x10)])
    expect(E1.verify_directory(K.elf, cells, [fake])[1], 'owner matrix of hull 48 (node + 0xD0)')
    car = node_of(live.ram, E1.DISPATCH, index=32)
    fake = ram_copy(live, 'matrix 46', [flip(live.ram, C.u32(live.ram, car + 0x110) + 0x90 + 0x32, 0x10)])
    expect(E1.verify_directory(K.elf, cells, [fake])[1], 'owner matrix of hull 46 (bone 0 + 0x90)')
    # the orphaned car hull
    s46, e46, _f = hulls[46]
    written = E1.written_words(K.elf, cells, after, 46, hulls)
    unwritten = next(k for k in range(0, e46 - s46, 4) if k not in written)
    fake = ram_copy(after, 'orphan unwritten', [flip(after.ram, table + s46 + unwritten)])
    expect_text(E1.verify_directory(K.elf, cells, [live, fake])[1], 'does not rewrite',
                f'orphan hull word +{unwritten:#x} that 001A2370 does not write')
    w0 = min(written)
    fake = ram_copy(after, 'orphan written', [flip(after.ram, table + s46 + w0)])
    expect_text(E1.verify_directory(K.elf, cells, [live, after, fake])[1], 'differ between the captures',
                'orphan bytes differing between the orphaning captures')
    expect_text(E1.verify_directory(K.elf, cells, [after])[1], 'no capture derives it',
                'an orphan with no derivation in the run')
    expect(E1.verify_directory(K.elf, cells, [live, after])[1], 'the orphan with a02_01', accept=True)
    # owner_matrix unit controls
    ram = bytearray(live.ram)
    node = 0x1C00000
    for b, extra, want in ((0x825100, {}, node + 0xD0), (0x219550, {}, node + 0xD0),
                           (0x823930, {2: 4, 0x0D: 7}, 0x1234560 + 0x90), (0x823930, {2: 4, 0x0D: 8}, None),
                           (0x823930, {2: 9, 0x0D: 7}, None), (0x825600, {}, None)):
        ram[node:node + 0x2F0] = bytes(0x2F0)
        struct.pack_into('<I', ram, node + 0x10, b)
        struct.pack_into('<I', ram, node + 0x110, 0x1234560)
        for off, v in extra.items():
            ram[node + off] = v
        expect(E1.owner_matrix(bytes(ram), node) != want, f'owner matrix {b:#x} {extra}', accept=True)
    # the run-time words
    ov = A2.read_overlay()
    olo, ohi = E2.overlay_data_window(ov)
    odisc = ov[olo - C.OVERLAY_ARENA:ohi - C.OVERLAY_ARENA]
    records = E2.chain_records(odisc, olo)
    gap = next(a for a in range(olo, ohi, 4) if not any(r <= a < r + 0x40 for r in records)
               and not E2.TRIGGERS <= a < E2.TRIGGERS + E2.TRIGGER_SIZE * E2.TRIGGER_COUNT)
    fake = ram_copy(live, 'gap word', [flip(live.ram, gap)])
    expect(E2.run_time_words(odisc, olo, fake.ram, records)[1], f'a rewritten word outside the chains ({gap:#x})')
    t8 = E2.TRIGGERS + 8 * E2.TRIGGER_SIZE + 4                    # x 260: not passed
    fake = ram_copy(after, 'trigger 8', [(t8, struct.pack('<I', 1))])
    expect(E2.run_time_words(odisc, olo, fake.ram, records)[1], 'a done word the car did not pass')
    t0 = E2.TRIGGERS + 4                                          # x -24.6: passed in a02_02
    fake = ram_copy(after, 'trigger 0', [(t0, struct.pack('<I', 0))])
    expect(E2.run_time_words(odisc, olo, fake.ram, records)[1], 'a passed trigger not done')
    fake = ram_copy(after, 'trigger id', [flip(after.ram, E2.TRIGGERS)])
    expect(E2.run_time_words(odisc, olo, fake.ram, records)[1], 'a trigger id word rewritten')
    freed = [a for s in range(0x100) for a in (L.POOL_BASE + L.POOL_STRIDE * s,)
             if not after.ram[a] and C.u32(after.ram, a + 0x10) == E1.DISPATCH]
    fake = ram_copy(after, 'no car', [(a + 0x10, bytes(4)) for a in freed])
    expect(E2.run_time_words(odisc, olo, fake.ram, records)[1], 'trigger words with the car unknown')
    expect(E2.run_time_words(odisc, olo, after.ram, records)[1], 'a02_02 trigger words', accept=True)
    fake = ram_copy(after, 'last chain', [flip(after.ram, max(E2.SCRIPT_ENTRIES) + 0x10)])
    expect(E2.run_time_words(odisc, olo, fake.ram, records)[1], 'a word of the last chain', accept=True)
    # the windows
    sc = (A / 'area02_scripts/scripts.emsc').read_bytes()
    for name, bad in (('first byte', mutate(sc, 20)), ('last byte', mutate(sc, len(sc) - 1)),
                      ('entry word', mutate(sc, 12)), ('trailing byte', sc + b'\0'),
                      ('four bytes short', T01.emsc_blob(sc, cut_tail=4))):
        expect(tables_problems(K, {0: [live]}, {'area02_scripts/scripts.emsc': bad}), f'scripts {name}')
    od = (A / 'overlay_data.emsc').read_bytes()
    for name, bad in (('first byte', mutate(od, 20)), ('last byte', mutate(od, len(od) - 1)),
                      ('base moved', T01.emsc_blob(od, base_shift=4, cut_head=4))):
        expect(tables_problems(K, {0: [live]}, {'overlay_data.emsc': bad}), f'overlay data {name}')
    tab = json.loads((A / 'tables.json').read_text())
    words = tab['scripts']['run_time_words'][key(live)]
    swapped = dict(tab['scripts']['run_time_words'])
    swapped[key(live)] = [hex(int(words[0], 16) + 4)] + words[1:]
    slo, shi = E2.scripts_window(ov)
    expect(window_problems('scripts', sc, (slo, shi - slo), ov[slo - C.OVERLAY_ARENA:shi - C.OVERLAY_ARENA],
                           [live], swapped, records), 'a run-time word replaced by its neighbour')
    # every table file's last byte
    for rel in ('sub0/roster.emro', 'spawn_table.emsp', 'door_destinations.emsp', 'sub0/message_data.emmd',
                'sub0/world_models.emwm'):
        expect(tables_problems(K, {0: [live]}, {rel: mutate((A / rel).read_bytes(), -1)}), f'{rel} last byte')
    expect(tables_problems(K, {0: [live]}, {'sub0/world_models.emwm': b'EMWM'}), 'truncated world models')
    expect(tables_problems(K, {1: [cap1]}, {'sub1/world_models.emwm': mutate((A / 'sub1/world_models.emwm')
                                                                              .read_bytes(), -1)}),
           'sub1/world_models.emwm last byte')
    # the roster
    roster = (A / 'sub0/roster.emro').read_bytes()
    _ok, _live, pairs = T01.roster_nodes(T, roster, live.ram)
    fake = ram_copy(live, 'node moved', [flip(live.ram, pairs[1][1] + 0xB1)])
    expect(roster_problems(0, roster, [fake]), 'a placement node moved off its record')
    fake = ram_copy(live, 'no pool', [(L.POOL_BASE, bytes(L.POOL_STRIDE * L.POOL_SLOTS))])
    expect(roster_problems(0, roster, [fake]), 'an empty actor pool')
    # model bindings
    wm = (A / 'sub0/world_models.emwm').read_bytes()
    wtab = C.u32(wm, 8)
    node = next(a for _s, a in E2.model_owner_nodes(live.ram) if C.u32(live.ram, a + 0x44) in
                {wtab + (C.s32(live.ram, wtab + 4 + 4 * i) >> 2 << 2) for i in range(C.u32(wm, 32))})
    expect(model_binding_problems(wm, [ram_copy(live, 'model id', [flip(live.ram, node + 0x0D)])]), 'model id +0x0D')
    expect(model_binding_problems(wm, [ram_copy(live, 'bones', [flip(live.ram, node + 0x0C)])]), 'bone count')
    multi = next(a for _s, a in E2.model_owner_nodes(live.ram) if live.ram[a + 0x0C] >= 2 and
                 C.u32(live.ram, a + 0x44) in {wtab + (C.s32(live.ram, wtab + 4 + 4 * i) >> 2 << 2)
                                               for i in range(C.u32(wm, 32))})
    last_slot = multi + 0x110 + 4 * (live.ram[multi + 0x0C] - 1)
    expect_text(model_binding_problems(wm, [ram_copy(live, 'bone slot', [(last_slot, bytes(4))])]),
                'bone count / slots', 'the last bone slot of a multi-bone owner cleared')
    expect_text(model_binding_problems(wm, [ram_copy(live, 'unbound', [(node + 0x44, bytes(4))])]),
                'model owners bound, not', 'one bound owner fewer')
    # messages: the sub-1 finding, and a sub-0 bank address in sub 1
    expect(message_bank_finding([cap1]), 'the sub-1 bank finding', accept=True)
    bank0 = C.u32(live.ram, T.D_0028A594)
    fake = ram_copy(cap1, 'bank moved', [(T.D_0028A594, struct.pack('<I', bank0)),
                                         (0x28A73C, struct.pack('<I', bank0)),
                                         (bank0, live.ram[bank0:bank0 + 0x100])])
    expect_text(message_bank_finding([fake]), 'parses', 'a parseable bank in sub 1')
    # the loader-finding verdict
    expect(not cells_findings_ok({0: -1, 1: 0}, 0), 'the recorded loader verdicts', accept=True)
    for rc, d in (({0: 0, 1: 0}, 0), ({0: -1, 1: -1}, 0), ({0: -1, 1: 0}, -1)):
        expect(not cells_findings_ok(rc, d), f'loader verdicts {rc} / {d} taken as the finding')
    # the spawn entry
    expect_text(spawn_entry_problems([ram_copy(live, 'entry', [(0x810702, b'\xff')])]), 'outside the',
                'a spawn entry past the records')
    # the load map
    expect(load_problems(K, 0, [ram_copy(cap1, 'sub1 as sub0', [(0x810701, b'\0')])], len(cells)),
           'a sub-1 load checked as sub 0')
    lmap = K.lmap[0]
    for entry in (lmap[0], lmap[-1]):
        fake = ram_copy(live, 'mapped byte', [flip(live.ram, entry[0] + entry[3] - 1)])
        expect(load_problems(K, 0, [fake], len(cells)), f'last byte of {entry[4]}')
    for at in (table - 1, table + len(cells)):
        if K.image[0].locate(at) is not None:
            expect(load_problems(K, 0, [ram_copy(live, 'edge', [flip(live.ram, at)])], len(cells)),
                   f'the mapped byte {at:#x} next to the directory')
    fake = ram_copy(live, 'cursor', [(0x28A740, struct.pack('<I', C.u32(live.ram, 0x28A740) + 0x800))])
    expect_text(load_problems(K, 0, [fake], len(cells)), 'a different load map', 'a moved load cursor')
    # the other sub's whole nested image planted into RAM is reported
    planted = [(a, K.image[1].read(a, n)) for a, _p, _o, n, l in K.image[1].map if '.n' in l]
    expect_text(load_problems(K, 0, [ram_copy(live, 'sub1 image', planted)], len(cells)), 'also fits RAM',
                'the other sub\'s nested image in RAM')
    # one other-sub nested file alone in RAM is not "the other load fits"
    one = [(a, K.image[1].read(a, n)) for a, _p, _o, n, l in K.image[1].map if '.n' in l][-1:]
    expect([p for p in load_problems(K, 0, [ram_copy(live, 'one sub1 file', one)], len(cells))
            if 'also fits RAM' in p], 'one sub-1 file in RAM', accept=True)
    # the per-capture scratchpad directory pointer
    fake = ram_copy(live, 'spad pointer')
    sp = bytearray(live.spad)
    struct.pack_into('<I', sp, L.SPAD_CELLS, C.u32(sp, L.SPAD_CELLS) + 0x10)
    fake.spad = bytes(sp)
    expect_text(load_problems(K, 0, [fake], len(cells)), 'cell directory pointer', 'the scratchpad directory pointer')
    # a 0019C6F0 call that acts on no record (its key renamed in the placement
    # table) while the capture's word still holds bit 30: only the return
    # check reports it
    rec2 = C.u32(after.ram, C.u32(after.ram, 0x24D7C0 + 4 * A2.AREA)) + 2 * 0x28
    fake = ram_copy(after, 'key renamed', [(rec2 + 4, struct.pack('<h', 0x7F)),
                                           (table + 4 + 4 * 2 + 3, bytes([after.ram[table + 4 + 4 * 2 + 3] | 0x40]))])
    expect_text(E1.verify_directory(K.elf, cells, [live, fake])[1], 'no record acted',
                'a 0019C6F0 call that finds no record')
    # the trigger rule at equality: a car standing exactly on an undone
    # trigger's x has not passed it (the C tests +0xB0 > x)
    x2 = odisc[E2.TRIGGERS + 2 * E2.TRIGGER_SIZE + 0x10 - olo:E2.TRIGGERS + 2 * E2.TRIGGER_SIZE + 0x14 - olo]
    fake = ram_copy(after, 'car on trigger 2', [(a + 0xB0, x2) for a in freed])
    expect(E2.run_time_words(odisc, olo, fake.ram, records)[1], 'the car exactly at an undone trigger', accept=True)
    # block_bytes refuses a file list with a gap
    tmp = BUILD / f'gap-{os.getpid()}'
    tmp.mkdir(parents=True, exist_ok=True)
    try:
        (tmp / 'a.bin').write_bytes(bytes(16))
        (tmp / 'b.bin').write_bytes(bytes(16))
        try:
            E3.block_bytes([(tmp / 'a.bin', 0, 16), (tmp / 'b.bin', 32, 16)], 0, 48)
            raised = []
        except SystemExit as error:
            raised = [str(error)]
        expect(raised, 'a container file list with a gap')
    finally:
        for q in (tmp / 'a.bin', tmp / 'b.bin'):
            q.unlink(missing_ok=True)
        tmp.rmdir()
    # the level: zone file bytes, the GS state, the bank file
    zfile = sorted((A / 'sub0/level').glob('*.emdl'))[0]
    zbuf = zfile.read_bytes()
    b = next(z for (s, _k), zz in _ZONES.items() if s == 0 for lab, z in zz.items() if lab.endswith('f02_id44.bin'))[0]
    gs_caps = T01.gs_captures([live])
    prim = K.el.level_template_prim(K.el.BootElf(C.ELF_PATH))
    parts = T01.emdl_parts(zbuf)
    for name, bad in (('position', mutate(zbuf, parts['verts'] + 1)), ('texel', mutate(zbuf, parts['blob'] + 7)),
                      ('last index', mutate(zbuf, parts['blob'] - 128 - 1)), ('GS code', mutate(zbuf, parts['tex'] + 15)),
                      ('trailing byte', zbuf + b'\0')):
        expect(T01.zone_problems(K.el, 'control', bad, b, gs_caps, prim), f'zone {name}')
    expect(T01.zone_problems(K.el, 'control', zbuf, b, gs_caps, prim), 'the zone file itself', accept=True)
    expect(T01.gs_state_problems({(1, 2, 3, 4, 5): 1}, T01.level_gs_state(K.el, prim)), 'level GS state')
    bank = (A / 'sub0/level/static_bank.emsc').read_bytes()
    base, _e, length = struct.unpack_from('<3I', bank, 8)
    expect_text(bank_problems(0, bank, [ram_copy(live, 'bank end', [flip(live.ram, base + length - 1)])]),
                'static bank differs from RAM', 'the static bank\'s last byte')
    for name, bad in (('trailing byte', bank + b'\0'), ('one byte short', T01.emsc_blob(bank, cut_tail=1)),
                      ('base word', mutate(bank, 8)), ('version', mutate(bank, 4))):
        expect(bank_problems(0, bad, [live]), f'static bank file {name}')
    fake = ram_copy(live, 'dyn pointer', [(E1.D_0028A5A4, struct.pack('<I', base))])
    expect_text(level_problems(K, 0, [fake])[0], 'D_0028A5A4 lies in the load map', 'D_0028A5A4 inside the map')
    # sound
    for sub, cap in ((0, live), (1, cap1)):
        banks = sub_path(sub, 'sfx/area02_banks.bin').read_bytes()
        reg = sub_path(sub, 'sfx/sfx_registry.emsr').read_bytes()
        for name, bad in (('byte 0', mutate(banks, 0)), ('last byte', mutate(banks, -1)), ('truncated', banks[:-1]),
                          ('row +0x50', mutate(banks, 0x50))):
            expect(sfx_problems(K, sub, [cap], bad, reg), f'sub{sub} area02_banks.bin {name}')
        expect_text(sfx_problems(K, sub, [cap], banks + b'\0', reg), 'container total', f'sub{sub} container length')
        expect(sfx_problems(K, sub, [cap], banks, mutate(reg, len(reg) // 2)), f'sub{sub} registry middle byte')
    banks = sub_path(0, 'sfx/area02_banks.bin').read_bytes()
    reg = sub_path(0, 'sfx/sfx_registry.emsr').read_bytes()
    handle_slot = 0x281D50 + 4 * (4 * 0x14 + 1)
    expect(sfx_problems(K, 0, [ram_copy(live, 'binding', [(handle_slot, b'\0\0\0\0')])], banks, reg),
           'group 4 slot 1 unbound')
    for sub in A2.SUBS:
        good = dict(BINDING[sub])
        expect(binding_problem(sub, good, set(REFUSED[sub])), f'sub{sub} pinned bindings', accept=True)
        expect(binding_problem(sub, {**good, (2, 0): ('area', 1)}, set(REFUSED[sub])), f'sub{sub} another row')
        expect(binding_problem(sub, {k: v for k, v in good.items() if k != (1, 2)}, set(REFUSED[sub])),
               f'sub{sub} a binding missing')
        expect(binding_problem(sub, good, set()), f'sub{sub} nothing refused')
        expect(binding_problem(sub, good, set(REFUSED[sub]) | {(5, 0)}), f'sub{sub} an extra refused slot')
        expect(binding_problem(sub, good, set(sorted(REFUSED[sub])[:-1])), f'sub{sub} a refused slot missing')
    # EMCL
    emcl = (A / 'sub0/area02.emcl').read_bytes()
    T01.GRID = GRIDS[0]
    for at in (0x30 + 1, len(emcl) // 2, len(emcl) - 1):
        expect(T01.emcl_equal(mutate(emcl, at), live.ram), f'EMCL +{at:#x}')
    expect(T01.emcl_equal(emcl + b'\0', live.ram), 'EMCL trailing byte')
    fake = ram_copy(live, 'grid pointer', [flip(live.ram, E1.D_0028A598 + 1)])
    expect(emcl_problems(K, 0, emcl, [fake], len(cells)), 'D_0028A598')
    expect(emcl_problems(K, 1, (A / 'sub1/area02.emcl').read_bytes(), [live], len(cells)), 'the sub-1 EMCL on sub 0')
    # ctx
    ctx = C.u32(live.ram, L.CTX_PTR)
    for off in (0xA0, 0xFF):
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                L.check_ctx_room_block([ram_copy(live, 'ctx', [flip(live.ram, ctx + off)])], K.elf)
            caught = []
        except SystemExit as error:
            caught = [str(error)]
        expect(caught, f'ctx +{off:#x}')
    # cells.json against its own rows, an unrecorded capture, clean copies
    fake = ram_copy(live, 'unrecorded')
    fake.key = 'unrecorded'
    expect_text(cells_problems(K, 0, cells, [fake], record), 'not recorded in cells.json', 'an unrecorded capture')
    bad = json.loads(json.dumps(record))
    bad['cells']['moved_uids'] = bad['cells']['moved_uids'][:-1]
    expect_text(cells_problems(K, 0, cells, [live], bad), 'is not the union of its rows', 'cells.json moved_uids')
    bad = json.loads(json.dumps(record))
    if key(after) in bad['cells']['flag_calls']:
        bad['cells']['flag_calls'][key(after)] = bad['cells']['flag_calls'][key(after)][:-1]
        expect_text(cells_problems(K, 0, cells, [live, after], bad), '0019C6F0 calls', 'cells.json flag calls')
    clean = [ram_copy(c, f'clean {c.name}') for c in (live, after)]
    expect(cells_problems(K, 0, cells, clean, record), 'clean copies (cells)', accept=True)
    expect(tables_problems(K, {0: clean, 1: [ram_copy(cap1, 'clean sub1')]}), 'clean copies (tables)', accept=True)
    # a bank record carrying a matrix slot (bit 3 of its w word), in the bank
    # file and in RAM alike, is reported by level_problems' guard
    real = A
    at = None
    for _i, o, units in L.bank_objects(lambda a, k: live.ram[a:a + k], base)[1:]:
        r = next((r for r in K.el.walk_records(live.ram[o + 0x40:o + 0x40 + L.UNIT * units]) if r), None)
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
        got, _v = level_problems(K, 0, [ram_copy(live, 'matrix slot', [flip(live.ram, at, 0x08)])])
    finally:
        A = real
        for q in [tree / f'sub0/level/{z.name}' for z in zlinks] + [tree / 'sub0/level/static_bank.emsc']:
            q.unlink(missing_ok=True)
        for q in (tree / 'sub0/level', tree / 'sub0', tree):
            q.rmdir()
    for zkey in [z for z in _ZONES if z[0] == 0 and z[1] != bank]:
        del _ZONES[zkey]
    expect_text(got, 'records with a matrix slot', 'a bank record with a matrix slot')
    return n


# ---------------------------------------------------------------------------


def main():
    need = [A / 'sub0/level/level.json', A / 'sub1/level/level.json', A / 'tables.json', A / 'sub0/cells.json',
            A / 'sub1/cells.json', A / 'sub0/sfx/banks.json', A / 'sub1/sfx/banks.json', C.ELF_PATH,
            A2.OVERLAY_PATH, C.ISO_PATH]
    missing = [str(p) for p in need if not p.exists()]
    if missing or not A2.ROUTE_A02.exists() or not A2.ARRIVAL.exists():
        print('area02 assets reference: SKIPPED, missing local inputs:', missing or [str(A2.ROUTE_A02)])
        return 0
    t0 = time.time()
    el = L.load_export_level()
    pairs = A2.all_captures()
    run = pairs if FULL else [(c, s) for c, s in pairs if c.name in QUICK]
    caps_by_sub = {}
    for c, s in run:
        caps_by_sub.setdefault(s, []).append(c)
    print(f'area02 assets reference ({MODE}): {len(run)} of {len(pairs)} captures '
          f'(sub 0: {len(caps_by_sub.get(0, []))}, sub 1: {len(caps_by_sub.get(1, []))})')
    K = SimpleNamespace(el=el, elf=C.read_elf(), lmap={}, info={}, image={}, first={})
    for sub in A2.SUBS:
        first = next(c for c, s in pairs if s == sub)
        K.first[sub] = first
        A2.configure(sub)
        K.lmap[sub], K.info[sub] = C.build_load_map(first)
        K.image[sub] = C.LoadedImage(K.lmap[sub])
    A2.configure(0)
    lib = T01.build_loaders()
    loaded, cells_rc = check_loaders(lib, A)
    for name, ok in loaded.items():
        check(ok, f'port loader rejects {name}')
    diag = cells_diagnostic(lib, A)
    check(cells_findings_ok(cells_rc, diag), f'em_actor_cells_load: {cells_rc}, bit-29-cleared sub-0 copy {diag}')
    print(f'  loaders: {sum(loaded.values())}/{len(loaded)} files accepted by the port loaders; finding: '
          f'em_actor_cells_load(sub0/area02_cells.bin) = {cells_rc[0]} (uid 0 bit 29; the cleared copy: {diag}), '
          f'sub1 = {cells_rc[1]}')

    problems, V = run_checks(K, caps_by_sub)
    for p in problems:
        check(False, p)
    lv0, lv1 = V.get('level0', {}), V.get('level1', {})
    print(f"  load map + level: sub 0 {lv0.get('objects')} bank objects -> {lv0.get('zones')} zone EMDLs, "
          f"{lv0.get('kicks')} level kicks; sub 1 {lv1.get('objects')} objects -> {lv1.get('zones')} zone EMDLs, "
          f"{lv1.get('kicks')} kicks; no dynamic list")
    print('  collision, cells, tables, sfx: every comparison over the run\'s captures '
          f'({len(problems)} problems)')
    if V['room']:
        print(f"  ctx +0xA0..+0xFF: original 001D8FD0 rebuilt the captured bytes in {len(V['room'])} capture(s) "
              f"from room entries {sorted({r['room_entry'] for r in V['room']})}")
    if FAILS:
        print(f'  canary and controls: skipped, {len(FAILS)} check(s) already failed')
    else:
        print(f'  canary: {canary(K, lib, {s: c[:1] for s, c in caps_by_sub.items()})} sections each reported '
              'their planted difference')
        if not FAILS:
            print(f'  controls: {controls(K, caps_by_sub)} changed inputs, each caught (or accepted where marked)')
    print(f'area02 assets reference: {"FAIL" if FAILS else "PASS"} ({len(FAILS)} failures, '
          f'{time.time() - t0:.1f} s)')
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
