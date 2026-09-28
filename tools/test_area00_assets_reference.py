#!/usr/bin/env python3
"""Check the exported AREA00 assets (assets/area00/, docs/AREA00_ASSETS.md)
against the recorded original captures, and load each one through the port's
own loader.

Inputs (all local, all the user's): the assets written by
tools/export_area00_level.py, export_area00_tables.py and export_area00_sfx.py,
the extracted disc files, the pinned boot ELF and AREA00 overlay, and the
AREA00 captures (the arrival ../Extermination/build/s87/route_a01/
a01_07_level_exit/ and build/s87/route_a00/a00_00..a00_09). Missing inputs
print SKIPPED and exit 0.

The comparators are the AREA01 checker's (tools/test_area01_assets_reference.py,
imported unchanged, with export_area01_common pointed at AREA00 and its
module constants GRID / BUILD / OVERLAY_SHA256 set for AREA00): zone_problems,
emsc_header_problems, bank_end, gs_captures, gs_state_problems, emcl_equal,
grid_problems, hulls_inside_table, roster_image / roster_equal / roster_nodes,
emsp_problems with spawn_layout / doors_layout, messages_problems,
world_models_problems, registry_from_ram, load_map_problems, build_loaders.
AREA00's own checks are written here: the two sub-states, the level without
a dynamic list, the directory's uid words (the ORIGINAL 0019C6F0 under the
shaft door's calls), the run-time words of the scripts and overlay data,
the model bindings and the sound container outside the load map.

Checks (every comparison is exact; each capture is checked against the files
of its own sub, the shared files against every capture):
  load map  per sub, in address order, no overlap, the same map from every
            capture's descriptors; every mapped disc byte equals RAM except
            the cell directory's own bytes (checked below); the other sub's
            map differs from RAM from 0x15CE380 on (the loaded sub)
  level     sub<N>/level/static_bank.emsc equals RAM at D_0028A5A0 over the
            bank's extent (bank_end); no dynamic-list file; every textured
            level kick REFs a bank object with the class-0 GS state and no
            kernel-0x00237450 kick exists; the zone EMDL equals a rebuild
            from the bank, byte for byte, and its texels the GS freeze decode
            of every capture of its sub
  emcl      area00.emcl equals the RAM grid rebuild (emcl_from_ram) in every
            capture; D_0028A598 pinned to 0x1501B80, the grid block inside
            the load map and outside the directory allowance
  cells     area00_cells.bin equals the disc bytes and ends where the
            directory ends; every capture's directory: uid words = the
            ORIGINAL 0019C6F0 run for the shaft door's calls, hulls = the
            disc or the ORIGINAL 001A2370 run for the live owner; the moved
            set and the calls equal cells.json
  tables    sub<N>/roster.emro = the rebuild by the original's walks over the
            pinned ELF + overlay and over every capture's RAM, live placement
            nodes keep their copied fields, (live, at rest) per capture
            pinned; both EMSP files (window set from the ELF walk, every
            window against RAM); D_008106C8 = +0x1C of the current spawn
            record in every capture; scripts and overlay data: extent from the
            module header / chain entries / placement table, bytes = the
            pinned module, RAM equal except the run-time words, each inside
            a reached chain record or the owner vector 0x82CCE0 (re-derived),
            the set per capture = tables.json; messages (every byte, counts
            from the ELF, bank sizes from the banks' headers); world models
            (header, span = wm_span over RAM) and every model owner's +0x44 =
            001C6120(table, +0x0D) with its bone count and every bone
            slot set, the bound count per capture pinned
  sfx       sfx/area00_banks.bin = upload section 0's container on the disc;
            the capture bindings are exactly group 1 -> global rows 0..2,
            group 2 -> area row 0, group 4 -> area rows 1, 2, group 3
            refused; sub<N>/sfx/sfx_registry.emsr = the RAM re-derivation of
            every capture of the sub
  ctx       ORIGINAL 001D8FD0 rebuilds ctx +0xA0..+0xFF from the sub's
            room-table entry (flipping the entry changes it)
  loaders   each file through its port loader (compiled privately from src/)
  canary    run_checks again over [a capture, a planted copy of it] per sub
            and a copy of the export tree with file plants; each section
            must report its plant for the copy and stay silent for the
            original (a comparison that reads only one capture fails it);
            every port loader must refuse a broken copy of its file
  controls  a changed input per comparator, file side and RAM side,
            including the accept cases listed in docs/AREA00_ASSETS.md
Default: 3 captures (the arrival, a00_04, a00_08); EM_TEST_FULL=1: all 11.
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
import export_area00_common as A0  # noqa: E402

C = A0.configure(0)
import test_area01_assets_reference as T01  # noqa: E402
import export_area00_level as E1  # noqa: E402
import export_area00_tables as E2  # noqa: E402
import export_area00_sfx as E3  # noqa: E402
from reference_mode import FULL, MODE  # noqa: E402

L, T, S = E1.L, E2.T, E3.S
A = Path(os.environ.get('EM_AREA00_ASSETS') or A0.OUT).resolve()
BUILD = C.ROOT / 'build/area00/assets/test'
CANARY = BUILD / f'canary-{os.getpid()}'
QUICK = ('a01_07_level_exit', 'a00_04_cage_terminal', 'a00_08_switch')
T01.BUILD = BUILD
T01.OVERLAY_SHA256 = A0.OVERLAY_SHA256
# D_0028A598 = 0x1501B80 = chunk04.n<sub>/f06 + 0xA7000 in all 11 captures;
# as for AREA01 the code that fills the directory at load is not identified,
# so the measured address is pinned and the grid block must lie in the map
GRID = T01.GRID = 0x1501B80
FAILS = T01.FAILS
check = T01.check

# (live, at the record's position and rotation) placement nodes per capture,
# by T01.roster_nodes (behaviour and +0x9A); measured, asserted exactly, so
# an empty or unmatched pool cannot pass
ROSTER_LIVE = {'a01_07_level_exit': (66, 65), 'a00_00_descend': (66, 65), 'a00_01_door51_locked': (66, 65),
               'a00_02_south_route': (66, 65), 'a00_03_padlock': (65, 64), 'a00_04_cage_terminal': (65, 61),
               'a00_05_ferry_deck': (65, 61), 'a00_06_cab_roof': (65, 61), 'a00_07_duct_to_ne_room': (65, 61),
               'a00_08_switch': (65, 61), 'a00_09_ne_room_out': (65, 61)}
# model owners bound to a bank model per capture (export_area00_tables'
# owners_bound); asserted exactly, so the binding check cannot pass vacuously
MODEL_OWNERS = {'a01_07_level_exit': 61, 'a00_00_descend': 61, 'a00_01_door51_locked': 61,
                'a00_02_south_route': 61, 'a00_03_padlock': 60, 'a00_04_cage_terminal': 60,
                'a00_05_ferry_deck': 60, 'a00_06_cab_roof': 60, 'a00_07_duct_to_ne_room': 60,
                'a00_08_switch': 60, 'a00_09_ne_room_out': 53}
# the capture bindings (group, slot) -> (container, row); 'area' = area00_banks.bin
BINDING = {(1, 0): ('global', 0), (1, 1): ('global', 1), (1, 2): ('global', 2),
           (2, 0): ('area', 0), (4, 0): ('area', 1), (4, 1): ('area', 2)}
LOADER_SOURCES = T01.LOADER_SOURCES
CANARY_TEST_1 = 0x8153A0      # where the arrival's display lists take TEST_1 (0x5000D, as in AREA01)


def key(cap):
    """The capture name the pinned tables use (a planted copy keeps its source's)."""
    return getattr(cap, 'key', cap.name)


def sub_of(cap):
    return cap.ram[0x810701]


# ---------------------------------------------------------------------------
# Load map


def load_problems(K, sub, caps, cells_len):
    out = list(T01.load_map_problems(K.lmap[sub]))
    image, other = K.image[sub], K.image[1 - sub]
    table = C.u32(caps[0].spad, L.SPAD_CELLS)
    for cap in caps:
        try:
            lmap, _info = C.build_load_map(cap)
        except SystemExit as error:
            out.append(f'{cap.name}: load map: {error}')
            continue
        if lmap != K.lmap[sub]:
            out.append(f'{cap.name}: a different load map')
        if C.u32(cap.spad, L.SPAD_CELLS) != C.u32(K.first[sub].spad, L.SPAD_CELLS):
            out.append(f'{cap.name}: cell directory pointer')
        rows = C.compare_load_map(image, cap, allow=[(table, table + cells_len)])
        if any(r['unexpected_rows'] for r in rows):
            out.append(f'{cap.name}: load map differs from RAM')
        lo, hi = other.span()
        n = hi - E1.SUB_SPLIT
        if other.read(E1.SUB_SPLIT, n) == cap.ram[E1.SUB_SPLIT:E1.SUB_SPLIT + n]:
            out.append(f'{cap.name}: the sub-{1 - sub} load also fits RAM from 0x15CE380')
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
    lvl = A / f'sub{sub}/level'
    bank_img = (lvl / 'static_bank.emsc').read_bytes()
    out += bank_problems(sub, bank_img, caps)
    base, _entry, length = struct.unpack_from('<3I', bank_img, 8)
    bank = bank_img[20:20 + length]
    if (lvl / 'dynamic_objects.emsc').exists():
        out.append(f'sub{sub}: a dynamic-list file (stage 0x000{sub} draws none)')
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


def emcl_problems(K, emcl, caps, cells_len):
    out = []
    for cap in caps:
        sub = sub_of(cap)
        table = C.u32(cap.spad, L.SPAD_CELLS)
        for problem in T01.grid_problems(cap.ram, K.image[sub], (table, table + cells_len)):
            out.append(f'{cap.name}: {problem}')
        where = T01.emcl_equal(emcl, cap.ram)
        if where is not None:
            out.append(f'{cap.name}: area00.emcl differs from the RAM grid rebuild: {where}')
    return out


def cells_problems(K, cells, caps, record):
    """The directory file against the disc, and every capture's RAM
    directory through export_area00_level.verify_directory (0019C6F0 for
    the uid words, 001A2370 for the hulls). `record` = cells.json."""
    out = []
    table = C.u32(caps[0].spad, L.SPAD_CELLS)
    try:
        disc = K.image[0].read(table, len(cells))
    except ValueError as error:
        return [f'cells: {error}']
    if cells != disc:
        k = next(j for j in range(len(cells)) if cells[j] != disc[j])
        out.append(f'area00_cells.bin differs from the disc bytes at +{k:#x}')
    try:
        _count, _hulls, size = L.cell_directory(cells, 0)
        if size != len(cells):
            out.append(f'area00_cells.bin is {len(cells)} bytes, the directory {size}')
        inside = T01.hulls_inside_table(L, cells)
        if inside:
            out.append(f'cells: hulls {inside} start inside the uid table')
        rows, problems, calls = E1.verify_directory(K.elf, cells, caps)
    except (AssertionError, IndexError, struct.error, ValueError) as error:
        return out + [f'cells: the directory does not parse ({error!r})']
    out += problems
    union = sorted({u for x in record['cells']['captures'] for u in x['moved_hulls']})
    if record['cells']['moved_uids'] != union:
        out.append(f'cells.json moved_uids {record["cells"]["moved_uids"]} is not the union of its rows {union}')
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
    A0.configure(sub)
    try:
        disc = T01.roster_image(T01.disc_reader())
        if roster != disc:
            out.append(f'sub{sub}/roster.emro differs from the rebuild from the disc')
        for cap in caps:
            # the RAM rebuild against the disc rebuild (the file equals it above)
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
        A0.configure(0)
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
        # RAM against the module bytes (the independent source), not the file
        words, problems = E2.run_time_words(disc, window[0], cap.ram, records)
        out += [f'{cap.name}: {label}: {p}' for p in problems]
        want = record.get(key(cap), [])
        if [hex(a) for a in words] != want:
            out.append(f'{cap.name}: {label} run-time words {len(words)} != tables.json {len(want)}')
    return out


def model_binding_problems(wm, caps):
    """Every model owner (not a NO_MODEL_BEHAVIOURS node) whose +0x44 names a
    model of the bank: +0x44 = 001C6120(table, +0x0D), the bone count +0x0C
    = the model's, every bone slot set."""
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
    """D_008106C8 = word +0x1C of the capture's current spawn record (area 0,
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
        ov = A0.read_overlay()
        dread = T01.disc_reader()
        slo, shi = E2.scripts_window(dread)
        sdisc = dread(slo, shi - slo)
        records = E2.chain_records(sdisc, slo)
        olo, ohi = E2.overlay_data_window(ov)
        out += window_problems('scripts', read('area00_scripts/scripts.emsc'), (slo, shi - slo), sdisc, caps,
                               rec['scripts']['run_time_words'], records)
        out += window_problems('overlay data', read('overlay_data.emsc'), (olo, ohi - olo),
                               ov[olo - C.OVERLAY_ARENA:], caps, rec['overlay_data']['run_time_words'], records)
        out += T01.messages_problems(read('message_data.emmd'), caps, K.elf)[0]
        wm = read('world_models.emwm')
        out += T01.world_models_problems(wm, caps)[0]
        out += model_binding_problems(wm, caps)
    except (SystemExit, ValueError, KeyError, IndexError, struct.error) as error:
        out.append(f'tables: {error!r}')
    return out


# ---------------------------------------------------------------------------
# Sound


def binding_problem(got, refused):
    """True unless the bindings are exactly BINDING and group 3's header is refused."""
    return got != BINDING or (3, 0) not in refused


def sfx_problems(K, caps_by_sub, banks, registries):
    out = []
    try:
        gname = S.X.GLOBAL_CONTAINER
        gdata = (C.DECOMP / gname).read_bytes()
        _rel, disc, _parsed, info = E3.area_container(K.first[0])
        if banks != disc:
            k = next((j for j in range(min(len(banks), len(disc))) if banks[j] != disc[j]), min(len(banks), len(disc)))
            out.append(f'area00_banks.bin differs from the disc container at +{k:#x} ({len(banks)} / {len(disc)})')
        total = C.u32(banks, 0) if len(banks) >= 4 else -1
        if len(banks) != total or total > info['sections'][0][1]:
            out.append(f'area00_banks.bin: {len(banks)} bytes, container total {total:#x}')
        parsed = S.X.A.parse_container(banks)
        if parsed is None:
            return out + ['area00_banks.bin is not an SShd container']
        containers = {gname: (gdata, S.X.A.parse_container(gdata)), 'area00_banks.bin': (banks, parsed)}
        for sub, caps in sorted(caps_by_sub.items()):
            bound, refused, _rep = S.bindings_from_captures(caps, containers)
            got = {k: ('global' if v[0][0] == gname else 'area', v[0][1]) for k, v in bound.items()}
            if binding_problem(got, refused):
                out.append(f'sub{sub}: bank bindings {got}, refused {sorted(refused)}')
                continue
            for cap in caps:
                # the samples from the disc container (the file is compared with it above)
                want, _counts, _n, _e = T01.registry_from_ram(cap.ram, bound, {gname: gdata, 'area00_banks.bin': disc})
                emsr = registries[sub]
                if emsr != want:
                    k = next((j for j in range(min(len(emsr), len(want))) if emsr[j] != want[j]),
                             min(len(emsr), len(want)))
                    out.append(f'{cap.name}: sub{sub}/sfx/sfx_registry.emsr differs from the RAM re-derivation '
                               f'at +{k:#x}')
    except (SystemExit, ValueError, TypeError, KeyError, IndexError, StopIteration, struct.error) as error:
        out.append(f'sfx: {error!r}')
    return out


# ---------------------------------------------------------------------------


def run_checks(K, caps_by_sub):
    """Every real check over the export tree A; returns (problems, summary)."""
    P, V = [], {}
    cells = (A / 'area00_cells.bin').read_bytes()
    record = json.loads((A / 'cells.json').read_text())
    caps = [c for s in sorted(caps_by_sub) for c in caps_by_sub[s]]
    for sub, sc in sorted(caps_by_sub.items()):
        P += load_problems(K, sub, sc, len(cells))
        p, V[f'level{sub}'] = level_problems(K, sub, sc)
        P += p
    P += emcl_problems(K, (A / 'area00.emcl').read_bytes(), caps, len(cells))
    P += cells_problems(K, cells, caps, record)
    P += tables_problems(K, caps_by_sub)
    P += sfx_problems(K, caps_by_sub, (A / 'sfx/area00_banks.bin').read_bytes(),
                      {s: (A / f'sub{s}/sfx/sfx_registry.emsr').read_bytes() for s in caps_by_sub})
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
    out['spawn'] = ok and all(bool(lib.em_spawn_table_read(table, a, 13 * 0x30)) for a in (0x24AA50, 0x24ACC0))
    doors = big()
    ok = rc('em_spawn_table_load', doors, p('door_destinations.emsp')) == 0
    out['doors'] = ok and bool(lib.em_spawn_table_read(doors, 0x24E140, 4)) and \
        bool(lib.em_spawn_table_read(doors, 0x24DF80, 0x20))
    wm = (tree / 'world_models.emwm').read_bytes()
    out['world models'] = rc('em_world_models_parse', big(), wm, CT.c_size_t(len(wm))) == 0
    for rel in ('area00_scripts/scripts.emsc', 'overlay_data.emsc'):
        out[rel] = rc('em_script_image_load', big(), p(rel)) == 1
    out['emcl'] = rc('em_collision_load', big(), p('area00.emcl')) == 0
    for sub in A0.SUBS:
        out[f'sub{sub}/roster.emro'] = rc('em_actor_roster_load', big(), p(f'sub{sub}/roster.emro')) == 0
        out[f'sub{sub}/level/static_bank.emsc'] = rc('em_script_image_load', big(),
                                                     p(f'sub{sub}/level/static_bank.emsc')) == 1
        for z in sorted(str(x) for x in (tree / f'sub{sub}/level').glob('*.emdl')):
            out[f'sub{sub}/{Path(z).name}'] = rc('em_model_load', big(), z.encode()) == 0
        out[f'sub{sub}/sfx registry'] = rc('em_sfx_registry_load', big(), p(f'sub{sub}/sfx/sfx_registry.emsr')) == 1
    with T01.quiet_fds():
        out['message data'] = rc('em_message_live_install', p('message_data.emmd')) == 1
    cells = rc('em_actor_cells_load', big(), p('area00_cells.bin'))
    return out, cells


# Loader verdicts that are findings, not asset faults (docs/AREA00_ASSETS.md):
# the port's message loader refuses area 0 (em_message_live.c load():
# `area == 0`), while the original indexes D_00264DD0[area + 1] for area 0
# as for any other; em_actor_cells_init refuses the directory because uid
# 0's word carries bit 29 (AREA01_ASSETS.md finding 1, same word form).
FINDING_LOADERS = ('message data',)


def findings_ok(diag, cdiag):
    """Both diagnostic copies load (the message copy returns 1, the cells copy 0)."""
    return diag and cdiag == 0


def loader_findings(lib, tree, mm_bytes=None, cells_bytes=None):
    """The two refusals and their diagnostics: each refused file must load
    once exactly the named field is changed (message area 0 -> 1; bit 29
    cleared in every uid word), so nothing else in the file is refused.
    Returns (message loaded, message diagnostic loaded, cells rc, cells
    diagnostic rc)."""
    BUILD.mkdir(parents=True, exist_ok=True)
    big = lambda: CT.create_string_buffer(16 << 20)
    f = lib.em_message_live_install
    f.restype = CT.c_int
    g = lib.em_actor_cells_load
    g.restype = CT.c_int
    mm = bytearray(mm_bytes if mm_bytes is not None else (tree / 'message_data.emmd').read_bytes())
    struct.pack_into('<I', mm, 8, 1)
    diag_mm = BUILD / f'diag-{os.getpid()}.emmd'
    diag_mm.write_bytes(bytes(mm))
    cells = bytearray(cells_bytes if cells_bytes is not None else (tree / 'area00_cells.bin').read_bytes())
    for uid in range(C.u32(cells, 0)):
        struct.pack_into('<I', cells, 4 + 4 * uid, C.u32(cells, 4 + 4 * uid) & ~0x20000000)
    diag_cells = BUILD / f'diag-{os.getpid()}.bin'
    diag_cells.write_bytes(bytes(cells))
    try:
        with T01.quiet_fds():
            real = f(str(tree / 'message_data.emmd').encode()) == 1
            diag = f(str(diag_mm).encode()) == 1
        crc = g(big(), str(tree / 'area00_cells.bin').encode())
        cdiag = g(big(), str(diag_cells).encode())
    finally:
        diag_mm.unlink()
        diag_cells.unlink()
    return real, diag, crc, cdiag


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


def canary_plants(K, cap, tree, tag):
    """RAM plants for one sub's canary copy of `cap` -> (copy, {section: text})."""
    sub = sub_of(cap)
    ram = cap.ram
    edits, want = [], {}
    bank = (A / f'sub{sub}/level/static_bank.emsc').read_bytes()
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
    if sub == 0:
        edits.append(flip(ram, CANARY_TEST_1, 2))      # TEST_1 of the level kicks' state
        want['GS state'] = 'level GS state'
    ctx = C.u32(ram, L.CTX_PTR)
    edits.append(flip(ram, ctx + 0xA4))
    want['ctx'] = f'ctx room block: {tag}:'
    table = C.u32(cap.spad, L.SPAD_CELLS)
    cells = (A / 'area00_cells.bin').read_bytes()
    _count, hulls, _size = L.cell_directory(cells, 0)
    s0, _e0, _f0 = hulls[0]
    edits.append(flip(ram, table + s0 + 0x20))
    want['cell hull'] = f'{tag}: hull 0 differs'
    grid_vertex = GRID + C.u32(ram, GRID) + 1
    edits.append(flip(ram, grid_vertex))
    want['EMCL'] = f'{tag}: area00.emcl differs from the RAM grid rebuild'
    roster = (A / f'sub{sub}/roster.emro').read_bytes()
    paddr = C.u32(roster, 0x10)
    edits.append(flip(ram, paddr + 0x28 * C.u32(roster, 0x0C) - 1))
    want['roster'] = f'{tag}: roster records differ from RAM'
    _ok, _live, pairs = T01.roster_nodes(T, roster, ram)
    edits.append(flip(ram, pairs[0][1] + 0x03))
    want['placement node fields'] = f'{tag}: a live placement node lost a copied field'
    edits.append(flip(ram, 0x24DF80 + 0x1F))
    want['door windows'] = f'{tag}: door_destinations.emsp window 0x24df80 differs'
    edits.append(flip(ram, 0x24AA50 + 13 * 0x30 - 1))   # the last byte of the area 0 sub 0 rows
    want['spawn windows'] = f'{tag}: spawn_table.emsp window 0x24aa50 differs'
    lo = E2.scripts_window(T01.disc_reader())[0]
    edits.append(flip(ram, lo + 0x10))                 # word +0x10 of the first chain's first record
    want['script words'] = f'{tag}: scripts run-time words'
    edits.append(flip(ram, E2.OWNER_VECTOR + 8, 0x10))  # the vector's third lane
    want['owner vector'] = f'{tag}: overlay data: 0x82cce8: owner vector word'
    area_records = C.u32(K.elf, T01.MSG_TABLES + 4 * (C.AREA + 1) - C.ELF_VADDR + C.ELF_OFFSET)
    edits.append(flip(ram, area_records + 5))
    want['message records'] = f'{tag}: message records differ'
    wm = (A / 'world_models.emwm').read_bytes()
    edits.append(flip(ram, C.u32(wm, 8) + C.u32(wm, 12) - 1))
    want['world models'] = f'{tag}: world model bank differs'
    edits.append(flip(ram, T01.LADDER + 2 * 0x40))
    want['registry'] = f'{tag}: sub{sub}/sfx/sfx_registry.emsr differs'
    # the directory's uid words: sub 0 gets the flag byte that selects the
    # 0019C6F0(2, 0) call without the word it sets; sub 1 loses bit 30
    if sub == 0:
        edits.append((E1.D_0081075D, b'\xff'))
    else:
        edits.append((table + 8 + 3, bytes([ram[table + 8 + 3] & ~0x40 & 0xFF])))
    want['uid words'] = f'{tag}: 0019C6F0 calls' if sub == 0 else f'{tag}: directory bytes differ'
    # a moved hull's owner matrix: node + 0xD0 + 3 of the first moved hull's owner
    owners = L.live_owners(ram)
    uid = min(u for u in (23, 26, 43, 62) if u in owners)
    node = L.POOL_BASE + owners[uid][0][0] * L.POOL_STRIDE
    edits.append(flip(ram, node + 0xD0 + 3, 0x04))
    want['owner matrix'] = f'{tag}: hull {uid} differs and no owner derivation reproduces it'
    # the model bindings: the first bound owner whose +0x0D with bit 0
    # flipped names another model (the copy is second in its list, so a
    # check that reads only the first capture, or is not run, misses it)
    wtab = C.u32(wm, 8)
    offs = [wtab + (C.s32(ram, wtab + 4 + 4 * i) >> 2 << 2) for i in range(C.u32(ram, wtab))]
    slot, owner = next((s, a) for s, a in E2.model_owner_nodes(ram) if C.u32(ram, a + 0x44) in offs
                       and (ram[a + 0x0D] ^ 1) < len(offs) and offs[ram[a + 0x0D] ^ 1] != C.u32(ram, a + 0x44))
    edits.append(flip(ram, owner + 0x0D))
    want['model binding'] = f'{tag} slot {slot}: +0x44 != 001C6120(table, +0x0D)'
    # the current spawn entry's word
    edits.append(flip(ram, T.D_008106C8 + 1))
    want['spawn entry word'] = f'{tag}: D_008106C8 '
    # the GS freeze: the first texture's texel page inverted
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
        (tree / rel).write_bytes(data)
    want, originals = {}, {}
    canary_caps = {}
    for sub in sorted(caps_by_sub):
        cap = caps_by_sub[sub][0]
        fake, w = canary_plants(K, cap, tree, f'canary{sub}')
        want.update({f'sub{sub} {k}': v for k, v in w.items()})
        originals.update({f'sub{sub} {k}': cap.name for k in w})
        canary_caps[sub] = [cap, fake]
    # file plants (not per capture)
    zones = sorted(x.name for x in (real / 'sub1/level').glob('*.emdl'))
    plant('sub0/level/dynamic_objects.emsc', C.emsc(0x177A940, bytes(16)))
    want['dynamic list file'] = 'sub0: a dynamic-list file'
    # an extra zone file (a copy) that sorts before the real one: the count
    # check reports it, and the name check reports the file paired with
    # the zone
    plant('sub0/level/00_a_extra.emdl', (real / f'sub0/level/{zones[0]}').read_bytes())
    want['zone count'] = 'sub0: 2 zone files for 1 source files'
    want['zone name'] = 'sub0/00_a_extra.emdl: zone name'
    rec = json.loads((real / 'cells.json').read_text())
    rec['cells']['captures'][0]['moved_hulls'] = rec['cells']['captures'][0]['moved_hulls'][:-1]
    plant('cells.json', json.dumps(rec).encode())
    want['moved set'] = f'{rec["cells"]["captures"][0]["capture"]}: re-transformed hulls'
    plant('area00_cells.bin', mutate((real / 'area00_cells.bin').read_bytes(), -1))
    want['cells file'] = 'area00_cells.bin differs from the disc bytes'
    plant('sfx/area00_banks.bin', mutate((real / 'sfx/area00_banks.bin').read_bytes(), 0x20000))
    want['sound bank'] = 'area00_banks.bin differs from the disc container'
    plant('sub1/roster.emro', mutate((real / 'sub1/roster.emro').read_bytes(), 0x09))
    want['roster file'] = 'sub1/roster.emro differs from the rebuild from the disc'
    sc = (real / 'area00_scripts/scripts.emsc').read_bytes()
    plant('area00_scripts/scripts.emsc', mutate(sc, len(sc) - 1))
    want['scripts file'] = 'scripts differs from the disc overlay'
    tab = json.loads((real / 'tables.json').read_text())
    first = next(iter(tab['overlay_data']['run_time_words']))
    tab['overlay_data']['run_time_words'][first] = tab['overlay_data']['run_time_words'][first][1:]
    plant('tables.json', json.dumps(tab).encode())
    want['overlay run-time set'] = f'{first}: overlay data run-time words'
    n = len(FAILS)
    try:
        A = tree
        with contextlib.redirect_stdout(io.StringIO()):
            got, _v = run_checks(K, canary_caps)
        # the zone file itself (its own pass: a structure difference stops
        # zone_problems before the texels, which the pass above plants)
        zbuf = (real / f'sub1/level/{zones[0]}').read_bytes()
        plant(f'sub1/level/{zones[0]}', mutate(zbuf, T01.emdl_parts(zbuf)['verts'] + 1))
        want['zone EMDL'] = f'sub1/{zones[0]} differs from a rebuild'
        got += level_problems(K, 1, caps_by_sub[1][:1])[0]
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
    loader_want = [x for x in loader_want if x not in FINDING_LOADERS]
    return len(want) + len(loader_want)


def plant_loader_canary(tree, plant):
    """Replace each loaded file in the tree by one its loader must refuse;
    returns the check_loaders names. The spawn file keeps every window but
    the one holding 0x24AA50; the door file keeps only the pointer array;
    an EMSC keeps its header with a 32-byte window; every other file is cut
    to its first 16 bytes."""
    spawn = T01.emsp_windows((tree / 'spawn_table.emsp').read_bytes())
    plant('spawn_table.emsp', T01.emsp_file([(a, d) for a, d in spawn if not a <= 0x24AA50 < a + len(d)]))
    doors = T01.emsp_windows((tree / 'door_destinations.emsp').read_bytes())
    plant('door_destinations.emsp', T01.emsp_file([(a, d) for a, d in doors if a == T01.D_0024E140]))
    names = ['spawn', 'doors']
    for rel in ('area00_scripts/scripts.emsc', 'overlay_data.emsc', 'sub0/level/static_bank.emsc',
                'sub1/level/static_bank.emsc'):
        blob = (tree / rel).read_bytes()
        base, entry = struct.unpack_from('<2I', blob, 8)
        plant(rel, C.emsc(base, blob[20:52], entry))
        names.append(rel)
    for rel, name in (('sub0/roster.emro', 'sub0/roster.emro'), ('sub1/roster.emro', 'sub1/roster.emro'),
                      ('world_models.emwm', 'world models'), ('area00.emcl', 'emcl'),
                      ('sub0/sfx/sfx_registry.emsr', 'sub0/sfx registry'),
                      ('sub1/sfx/sfx_registry.emsr', 'sub1/sfx registry'), ('message_data.emmd', 'message data')):
        plant(rel, (tree / rel).read_bytes()[:16])
        names.append(name)
    for sub in A0.SUBS:
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
    cap0 = caps_by_sub[0][0]
    cap1 = caps_by_sub.get(1, [None])[0]
    record = json.loads((A / 'cells.json').read_text())
    cells = (A / 'area00_cells.bin').read_bytes()
    count = C.u32(cells, 0)
    table = C.u32(cap0.spad, L.SPAD_CELLS)
    # cells: the file side
    for at in (0, 4, 4 + 4 * count, len(cells) - 1):
        expect(cells_problems(K, mutate(cells, at), [cap0], record), f'cells file +{at:#x}')
    expect(cells_problems(K, cells + b'\0', [cap0], record), 'cells file plus a byte')
    # uid words: D_0081075E = 0xFF selects both calls (the door's C); on the
    # sub-1 capture the (0, 1) call then acts too, and the capture's words
    # and recorded calls no longer fit
    if cap1 is not None:
        fake = ram_copy(cap1, 'both calls', [(E1.D_0081075E, b'\xff')])
        expect(E1.door_calls(fake.ram) != ((2, 0), (0, 1)), 'call selection for D_0081075E', accept=True)
        expect(cells_problems(K, cells, [fake], record), 'D_0081075E = 0xFF on the sub-1 capture')
        # the shaft door gone: the calls have no caller
        doors = [a for _s, a in E1.pool(cap1.ram) if C.u32(cap1.ram, a + 0x10) == E1.SHAFT_DOOR]
        fake = ram_copy(cap1, 'no door', [(a + 0x10, b'\0\0\0\0') for a in doors])
        expect(E1.verify_directory(K.elf, cells, [fake])[1], 'flag calls without a live shaft door')
        fake = ram_copy(cap1, 'setter code', [flip(cap1.ram, E1.FLAG_SETTER + 0x13F)])
        expect(E1.verify_directory(K.elf, cells, [fake])[1], '0019C6F0 code in RAM')
        fake = ram_copy(cap1, 'bit 30', [(table + 8 + 3, bytes([cap1.ram[table + 8 + 3] & 0xBF]))])
        expect(E1.verify_directory(K.elf, cells, [fake])[1], 'uid 1 word without bit 30')
    fake = ram_copy(cap0, 'bit 30 set', [(table + 8 + 3, bytes([cap0.ram[table + 8 + 3] | 0x40]))])
    expect(E1.verify_directory(K.elf, cells, [fake])[1], 'uid 1 bit 30 without a call')
    # moved hulls: each owner kind's hull byte, and a matrix byte
    owners = L.live_owners(cap0.ram)
    _c, hulls, _s = L.cell_directory(cells, 0)
    moved0 = next(r['moved_hulls'] for r in record['cells']['captures'] if r['capture'] == cap0.name)
    kinds = {}
    for uid in moved0:
        kinds.setdefault(C.u32(cap0.ram, L.POOL_BASE + owners[uid][0][0] * L.POOL_STRIDE + 0x10), uid)
    for uid in sorted(kinds.values()):
        s, e, _f = hulls[uid]
        fake = ram_copy(cap0, f'hull {uid}', [flip(cap0.ram, table + e - 1)])
        expect(E1.verify_directory(K.elf, cells, [fake])[1], f'moved hull {uid} last byte')
        node = L.POOL_BASE + owners[uid][0][0] * L.POOL_STRIDE
        fake = ram_copy(cap0, f'matrix {uid}', [flip(cap0.ram, node + 0xD0 + 0x32, 0x10)])
        expect(E1.verify_directory(K.elf, cells, [fake])[1], f'owner matrix of hull {uid}')
    # owner_matrix: an unknown behaviour has no matrix; each known one + 0xD0
    expect(E1.owner_matrix(cap0.ram, L.POOL_BASE + owners[23][0][0] * L.POOL_STRIDE) !=
           L.POOL_BASE + owners[23][0][0] * L.POOL_STRIDE + 0xD0, 'owner matrix 0x825600', accept=True)
    for b in (0x825600, 0x8263C0, 0x219550):
        ram = bytearray(cap0.ram)
        struct.pack_into('<I', ram, 0x1C00010, b)
        expect(E1.owner_matrix(ram, 0x1C00000) != 0x1C000D0, f'owner matrix {b:#x}', accept=True)
    ram = bytearray(cap0.ram)
    struct.pack_into('<I', ram, 0x1C00010, 0x826D40)
    expect(E1.owner_matrix(ram, 0x1C00000) is not None, 'no owner matrix for an AREA01 behaviour', accept=True)
    # the scripts / overlay run-time words
    dread = T01.disc_reader()
    slo, shi = E2.scripts_window(dread)
    sdisc = dread(slo, shi - slo)
    records = E2.chain_records(sdisc, slo)
    gap = next(a for a in range(slo, shi, 4) if not any(r <= a < r + 0x40 for r in records))
    fake = ram_copy(cap0, 'gap word', [flip(cap0.ram, gap)])
    expect(E2.run_time_words(sdisc, slo, fake.ram, records)[1], f'a rewritten word outside the chains ({gap:#x})')
    for lane in range(3):
        fake = ram_copy(cap0, 'vector', [flip(cap0.ram, E2.OWNER_VECTOR + 4 * lane)])
        ov = A0.read_overlay()
        olo, _ohi = E2.overlay_data_window(ov)
        expect(E2.run_time_words(ov[olo - C.OVERLAY_ARENA:], olo, fake.ram, records)[1], f'owner vector lane {lane}')
    owner = next(a for _s, a in T.pool_nodes(cap0.ram) if C.u32(cap0.ram, a + 0x10) == E2.OWNER_VECTOR_BEHAVIOUR)
    ram = bytearray(cap0.ram)
    ram[L.POOL_BASE + 0xFF * L.POOL_STRIDE:L.POOL_BASE + 0x100 * L.POOL_STRIDE] = \
        cap0.ram[owner:owner + L.POOL_STRIDE]
    expect(E2.vector_words(bytes(ram)) is None, 'two vector owners')
    # scripts window extent and bytes
    sc = (A / 'area00_scripts/scripts.emsc').read_bytes()
    for name, bad in (('first byte', mutate(sc, 20)), ('last byte', mutate(sc, len(sc) - 1)),
                      ('entry word', mutate(sc, 12)), ('trailing byte', sc + b'\0'),
                      ('four bytes short', T01.emsc_blob(sc, cut_tail=4))):
        expect(tables_problems(K, {0: [cap0]}, {'area00_scripts/scripts.emsc': bad}), f'scripts {name}')
    od = (A / 'overlay_data.emsc').read_bytes()
    for name, bad in (('first byte', mutate(od, 20)), ('last byte', mutate(od, len(od) - 1)),
                      ('base moved', T01.emsc_blob(od, base_shift=4, cut_head=4))):
        expect(tables_problems(K, {0: [cap0]}, {'overlay_data.emsc': bad}), f'overlay data {name}')
    # every table file's last byte, end to end
    for rel in ('sub0/roster.emro', 'spawn_table.emsp', 'door_destinations.emsp', 'message_data.emmd',
                'world_models.emwm'):
        expect(tables_problems(K, {0: [cap0]}, {rel: mutate((A / rel).read_bytes(), -1)}), f'{rel} last byte')
    expect(tables_problems(K, {0: [cap0]}, {'world_models.emwm': b'EMWM'}), 'truncated world models')
    # the roster count, both ways
    roster = (A / 'sub0/roster.emro').read_bytes()
    _ok, _live, pairs = T01.roster_nodes(T, roster, cap0.ram)
    fake = ram_copy(cap0, 'node moved', [flip(cap0.ram, pairs[1][1] + 0xB1)])
    fake.key = cap0.name
    expect(roster_problems(0, roster, [fake]), 'a placement node moved off its record')
    fake = ram_copy(cap0, 'no pool', [(L.POOL_BASE, bytes(L.POOL_STRIDE * L.POOL_SLOTS))])
    fake.key = cap0.name
    expect(roster_problems(0, roster, [fake]), 'an empty actor pool')
    # model bindings
    wm = (A / 'world_models.emwm').read_bytes()
    node = next(a for _s, a in E2.model_owner_nodes(cap0.ram) if C.u32(cap0.ram, a + 0x44) in
                {C.u32(wm, 8) + (C.s32(cap0.ram, C.u32(wm, 8) + 4 + 4 * i) >> 2 << 2) for i in range(C.u32(wm, 32))})
    expect(model_binding_problems(wm, [ram_copy(cap0, 'model id', [flip(cap0.ram, node + 0x0D)])]), 'model id +0x0D')
    expect(model_binding_problems(wm, [ram_copy(cap0, 'bones', [flip(cap0.ram, node + 0x0C)])]), 'bone count')
    # the load map: the loaded-sub test, a single mapped byte
    if cap1 is not None:
        expect(load_problems(K, 0, [ram_copy(cap1, 'sub1 as sub0', [(0x810701, b'\0')])], len(cells)),
               'a sub-1 load checked as sub 0')
    lmap = K.lmap[0]
    for entry in (lmap[0], lmap[-1]):
        fake = ram_copy(cap0, 'mapped byte', [flip(cap0.ram, entry[0] + entry[3] - 1)])
        expect(load_problems(K, 0, [fake], len(cells)), f'last byte of {entry[4]}')
    # the level: a zone file position byte, a texel byte, a trailing byte
    zfile = sorted((A / 'sub0/level').glob('*.emdl'))[0]
    zbuf = zfile.read_bytes()
    (b, _ids, _bad, _recs), = [z for (s, _k), zz in _ZONES.items() if s == 0 for z in zz.values()]
    gs_caps = T01.gs_captures([cap0])
    prim = K.el.level_template_prim(K.el.BootElf(C.ELF_PATH))
    parts = T01.emdl_parts(zbuf)
    for name, bad in (('position', mutate(zbuf, parts['verts'] + 1)), ('texel', mutate(zbuf, parts['blob'] + 7)),
                      ('last index', mutate(zbuf, parts['blob'] - 128 - 1)), ('GS code', mutate(zbuf, parts['tex'] + 15)),
                      ('trailing byte', zbuf + b'\0')):
        expect(T01.zone_problems(K.el, 'control', bad, b, gs_caps, prim), f'zone {name}')
    expect(T01.gs_state_problems({(1, 2, 3, 4, 5): 1}, T01.level_gs_state(K.el, prim)), 'level GS state')
    # sound: the container file, a registry byte, the binding set
    banks = (A / 'sfx/area00_banks.bin').read_bytes()
    regs = {0: (A / 'sub0/sfx/sfx_registry.emsr').read_bytes()}
    for name, bad in (('byte 0', mutate(banks, 0)), ('last byte', mutate(banks, -1)), ('truncated', banks[:-1]),
                      ('row +0x50', mutate(banks, 0x50))):
        expect(sfx_problems(K, {0: [cap0]}, bad, regs), f'area00_banks.bin {name}')
    reg = regs[0]
    expect(sfx_problems(K, {0: [cap0]}, banks, {0: mutate(reg, len(reg) // 2)}), 'registry middle byte')
    handle_slot = 0x281D50 + 4 * (4 * 0x14 + 1)
    expect(sfx_problems(K, {0: [ram_copy(cap0, 'binding', [(handle_slot, b'\0\0\0\0')])]}, banks, regs),
           'group 4 slot 1 unbound')
    # EMCL: a vertex, a polygon, an index, the rank tail, a trailing byte
    emcl = (A / 'area00.emcl').read_bytes()
    for at in (0x30 + 1, len(emcl) // 2, len(emcl) - 1):
        expect(T01.emcl_equal(mutate(emcl, at), cap0.ram), f'EMCL +{at:#x}')
    expect(T01.emcl_equal(emcl + b'\0', cap0.ram), 'EMCL trailing byte')
    fake = ram_copy(cap0, 'grid pointer', [flip(cap0.ram, E1.D_0028A598 + 1)])
    expect(emcl_problems(K, emcl, [fake], len(cells)), 'D_0028A598')
    # ctx: a captured byte at +0xA0 and +0xFF
    ctx = C.u32(cap0.ram, L.CTX_PTR)
    for off in (0xA0, 0xFF):
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                L.check_ctx_room_block([ram_copy(cap0, 'ctx', [flip(cap0.ram, ctx + off)])], K.elf)
            caught = []
        except SystemExit as error:
            caught = [str(error)]
        expect(caught, f'ctx +{off:#x}')

    # -- the first sweep's survivors (docs/AREA00_ASSETS.md, "Mutation sweep")
    def expect_text(problems, text, what):
        expect([p for p in problems if text in p], what)
    # M03: the allowance is exactly the directory: the bytes just outside it
    for at in (table - 1, table + len(cells)):
        hit = K.image[0].locate(at)
        if hit is not None:
            expect(load_problems(K, 0, [ram_copy(cap0, 'edge', [flip(cap0.ram, at)])], len(cells)),
                   f'the mapped byte {at:#x} next to the directory')
    # M04: the static bank's last byte alone
    bank = (A / 'sub0/level/static_bank.emsc').read_bytes()
    base, _e, length = struct.unpack_from('<3I', bank, 8)
    expect_text(bank_problems(0, bank, [ram_copy(cap0, 'bank end', [flip(cap0.ram, base + length - 1)])]),
                'static bank differs from RAM', 'the static bank\'s last byte')
    for name, bad in (('trailing byte', bank + b'\0'), ('one byte short', T01.emsc_blob(bank, cut_tail=1)),
                      ('base word', mutate(bank, 8)), ('version', mutate(bank, 4))):
        expect(bank_problems(0, bad, [cap0]), f'static bank file {name}')
    # M10: a directory file one byte longer, by the size check itself
    expect_text(cells_problems(K, cells + b'\0', [cap0], record), 'bytes, the directory', 'cells size check')
    # M21: a rewritten word in the last chain's records is explained
    last = max(E2.SCRIPT_ENTRIES)
    fake = ram_copy(cap0, 'last chain', [flip(cap0.ram, last + 0x10)])
    expect(E2.run_time_words(sdisc, slo, fake.ram, records)[1], 'a word of the last chain', accept=True)
    # M23: the recorded run-time set with one word replaced (same count)
    tab = json.loads((A / 'tables.json').read_text())
    words = tab['scripts']['run_time_words'][key(cap0)]
    swapped = dict(tab['scripts']['run_time_words'])
    swapped[key(cap0)] = [hex(int(words[0], 16) + 4)] + words[1:]
    expect(window_problems('scripts', sc, (slo, shi - slo), sdisc, [cap0], swapped, records),
           'a run-time word replaced by its neighbour')
    # M28: the binding verdict
    good = dict(BINDING)
    expect(binding_problem(good, {(3, 0): 3}), 'the pinned bindings', accept=True)
    expect(binding_problem({**good, (4, 1): ('area', 1)}, {(3, 0): 3}), 'a binding to another row')
    expect(binding_problem({k: v for k, v in good.items() if k != (4, 1)}, {(3, 0): 3}), 'a binding missing')
    expect(binding_problem(good, {}), 'group 3 not refused')
    # M29: the container total by its own check
    expect_text(sfx_problems(K, {0: [cap0]}, banks + b'\0', regs), 'container total', 'container length check')
    # M30: an unplanted copy under another name passes the per-capture tables
    clean = [ram_copy(c, f'clean {c.name}') for c in (cap0, cap1) if c is not None]
    expect(cells_problems(K, cells, clean, record), 'clean copies (cells)', accept=True)
    expect(tables_problems(K, {sub_of(c): [c] for c in clean}), 'clean copies (tables)', accept=True)
    # M32: the loader diagnostics verdict
    expect(findings_ok(True, 0) is not True, 'both diagnostics loaded', accept=True)
    for d, c in ((True, -1), (False, 0), (False, -1)):
        expect(findings_ok(d, c), f'a failed diagnostic ({d}, {c}) taken as a pass', accept=True)
    # M33: a capture whose load cursor differs builds a different map
    fake = ram_copy(cap0, 'cursor', [(0x28A740, struct.pack('<I', C.u32(cap0.ram, 0x28A740) + 0x800))])
    expect_text(load_problems(K, 0, [fake], len(cells)), 'a different load map', 'a moved load cursor')
    # M37: a 0019C6F0 call that writes outside the uid words (a directory
    # whose count stops before the uid the record names)
    if cap1 is not None:
        short = struct.pack('<I', 1) + cells[4:]
        try:
            E1.derive_words(K.elf, short, cap1)
            raised = []
        except ValueError as error:
            raised = [str(error)]
        expect(raised, '0019C6F0 writing past the uid words')
    # M39: a capture cells.json does not record
    fake = ram_copy(cap0, 'unrecorded')
    fake.key = 'unrecorded'
    expect_text(cells_problems(K, cells, [fake], record), 'not recorded in cells.json', 'an unrecorded capture')
    # M35: cells.json's moved_uids against its own rows
    bad = json.loads(json.dumps(record))
    bad['cells']['moved_uids'] = bad['cells']['moved_uids'][:-1]
    expect_text(cells_problems(K, cells, [cap0], bad), 'is not the union of its rows', 'cells.json moved_uids')

    # -- the review sweep's survivors (docs/AREA00_ASSETS.md, "Review sweep")
    # R12: a bound owner's first bone slot cleared
    expect_text(model_binding_problems(wm, [ram_copy(cap0, 'bone slot', [(node + 0x110, bytes(4))])]),
                'bone count / slots', 'a cleared bone slot')
    # R41: one owner fewer bound (its model pointer cleared)
    expect_text(model_binding_problems(wm, [ram_copy(cap0, 'unbound', [(node + 0x44, bytes(4))])]),
                'model owners bound, not', 'one bound owner fewer')
    # the spawn entry: an entry byte past the sub's records
    expect_text(spawn_entry_problems([ram_copy(cap0, 'entry', [(0x810702, b'\xff')])]), 'outside the',
                'a spawn entry past the records')
    # R05: a bank record carrying a matrix slot (bit 3 of its w word), in the
    # bank file and in RAM alike, is reported by level_problems' guard
    real = A
    at = None
    for _i, o, units in L.bank_objects(lambda a, k: cap0.ram[a:a + k], base)[1:]:
        r = next((r for r in K.el.walk_records(cap0.ram[o + 0x40:o + 0x40 + L.UNIT * units]) if r), None)
        if r is not None:
            at = o + 0x40 + r[0] + 0x3C
            break
    tree = BUILD / f'slot-{os.getpid()}'
    (tree / 'sub0/level').mkdir(parents=True, exist_ok=True)
    try:
        (tree / 'sub0/level/static_bank.emsc').write_bytes(mutate(bank, 20 + at - base, 0x08))
        (tree / f'sub0/level/{zfile.name}').symlink_to(zfile)
        A = tree
        got, _v = level_problems(K, 0, [ram_copy(cap0, 'matrix slot', [flip(cap0.ram, at, 0x08)])])
    finally:
        A = real
        for q in (tree / f'sub0/level/{zfile.name}', tree / 'sub0/level/static_bank.emsc'):
            q.unlink(missing_ok=True)
        for q in (tree / 'sub0/level', tree / 'sub0', tree):
            q.rmdir()
    for zkey in [z for z in _ZONES if z[0] == 0 and z[1] != bank]:
        del _ZONES[zkey]
    expect_text(got, 'records with a matrix slot', 'a bank record with a matrix slot')
    return n


# ---------------------------------------------------------------------------


def main():
    need = [A / 'sub0/level/level.json', A / 'sub1/level/level.json', A / 'tables.json', A / 'cells.json',
            A / 'sfx/banks.json', C.ELF_PATH, A0.OVERLAY_PATH, C.ISO_PATH]
    missing = [str(p) for p in need if not p.exists()]
    if missing or not A0.ROUTE_A00.exists() or not A0.ARRIVAL.exists():
        print('area00 assets reference: SKIPPED, missing local inputs:', missing or [str(A0.ROUTE_A00)])
        return 0
    t0 = time.time()
    el = L.load_export_level()
    pairs = A0.all_captures()
    run = pairs if FULL else [(c, s) for c, s in pairs if c.name in QUICK]
    caps_by_sub = {}
    for c, s in run:
        caps_by_sub.setdefault(s, []).append(c)
    print(f'area00 assets reference ({MODE}): {len(run)} of {len(pairs)} captures '
          f'(sub 0: {len(caps_by_sub.get(0, []))}, sub 1: {len(caps_by_sub.get(1, []))})')
    K = SimpleNamespace(el=el, elf=C.read_elf(), lmap={}, info={}, image={}, first={})
    for sub in A0.SUBS:
        first = next(c for c, s in pairs if s == sub)
        K.first[sub] = first
        A0.configure(sub)
        K.lmap[sub], K.info[sub] = C.build_load_map(first)
        K.image[sub] = C.LoadedImage(K.lmap[sub])
    A0.configure(0)
    lib = T01.build_loaders()
    loaded, cells_rc = check_loaders(lib, A)
    for name, ok in loaded.items():
        if name not in FINDING_LOADERS:
            check(ok, f'port loader rejects {name}')
    real, diag, crc, cdiag = loader_findings(lib, A)
    check(findings_ok(diag, cdiag), f'loader diagnostics: message area 1 copy {diag}, cells bit-29-cleared copy {cdiag}')
    print(f'  loaders: {sum(v for k, v in loaded.items() if k not in FINDING_LOADERS)}/'
          f'{len(loaded) - len(FINDING_LOADERS)} files accepted by the port loaders; findings: '
          f'em_message_live_install(message_data.emmd) = {int(real)} (area 0 refused; the area-1 copy loads: '
          f'{int(diag)}), em_actor_cells_load(area00_cells.bin) = {crc} (uid 0 bit 29; the cleared copy: {cdiag})')

    problems, V = run_checks(K, caps_by_sub)
    for p in problems:
        check(False, p)
    lv0, lv1 = V.get('level0', {}), V.get('level1', {})
    print(f"  load map + level: sub 0 {lv0.get('objects')} bank objects -> {lv0.get('zones')} zone EMDL, "
          f"{lv0.get('kicks')} level kicks; sub 1 {lv1.get('objects')} objects -> {lv1.get('zones')} zone EMDL, "
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
    print(f'area00 assets reference: {"FAIL" if FAILS else "PASS"} ({len(FAILS)} failures, '
          f'{time.time() - t0:.1f} s)')
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
