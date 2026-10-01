#!/usr/bin/env python3
"""Check the exported AREA13 assets (assets/area13/, docs/AREA13_ASSETS.md)
against the recorded ninth-level captures, and load each file through the
port's own loader.

Two targets (export_area13_common): 'area13' (AREA13 sub 0: the arrival
../Extermination/build/s87/route_a04b/a04b_04_lift/ and route_a13/a13_00 ..
a13_04) and 'area19' (AREA19 sub 0: route_a13/a13_05, its only capture).
Missing inputs print SKIPPED and exit 0.

The comparators are the AREA01 checker's (tools/test_area01_assets_reference.py,
imported unchanged, with export_area01_common pointed at each target and its
module constants BUILD / OVERLAY_SHA256 / GRID set per target): zone_problems,
emsc_header_problems, emsc_window, gs_captures, gs_state_problems,
level_gs_state, bank_end, grid_problems, emcl_equal, hulls_inside_table,
roster_image / roster_equal / roster_nodes, emsp_problems with spawn_layout /
doors_layout, messages_problems, world_models_problems, registry_from_ram,
load_map_problems, build_loaders, quiet_fds, emsp_windows / emsp_file,
emdl_parts. The derivations are the exporters' (export_area13_level.
verify_directory with the ORIGINAL 001A2370 / 0x219F50 / 0019C6F0 in the EE
interpreter, export_area22_level.check_ctx_block with the ORIGINAL 001D8FD0
and 001D1C50, export_level.export_background for the background).

Checks per target (every comparison is exact):
  load map  in address order, no overlap, the same map from every capture's
            descriptor (the INDEX.IDX sector hash pinned); every row labelled
            by relocation id lies inside [D_0028A490[id], the next address
            the same list relocates) and the rows tile each resident region
            (label_problems); every mapped disc byte equals RAM except the
            cell directory's own bytes; every relocation word = its cursor +
            offset; id 0x45 not relocated and D_0028A5A4 = the pinned
            previous-area value 0x1980000 (also in the capture before the
            load)
  level     static_bank.emsc = RAM over its extent; no dynamic-list file and
            no kernel-0x00237450 kick; every textured level kick REFs a bank
            object with the class-0 GS state; the zone EMDL = a rebuild from
            the bank, its texels the GS freeze decode of every capture;
            background.embg = export_level.export_background's asset for
            every capture (disc texels = that capture's GS freeze), and its
            absence where no capture arms the background
  emcl      <target>.emcl = the RAM grid rebuild in every capture; D_0028A598
            pinned; the grid inside the load map, outside the directory
  cells     <target>_cells.bin = the disc bytes; every byte of every
            capture's directory derived (uid words by the ORIGINAL 0019C6F0,
            hulls by the ORIGINAL 001A2370 / 0x219F50, orphans proven); the
            moved set and flag calls per capture = cells.json; the call-site
            census of 001A2370, 0x219F50 and 0019C6F0 in the ELF and the
            module (every site's function is modelled or is a sub-1 owner
            with no live node and no sub-0 record)
  tables    roster.emro = the rebuild by the original's walks over the
            pinned ELF + overlay and over every capture's RAM, (live, at
            rest) per capture pinned, every live placement node keeps its
            copied fields; both EMSP files; D_008106C8 = +0x1C of record
            D_00810702; the chains = the C's func_001BA1A0 entries =
            tables.json; scripts / overlay-data windows = the module, every
            run-time word in a reached record or a writer window, each
            writer window (the hatch's count-4 set and points, the flame's
            word) holding the value its C gives; message_data.emmd (every
            byte, area table);
            world models, every model owner's +0x44 (the hatch by its own
            rule), the bound count pinned; doors.used = the recomputed list,
            each record = the row in RAM
  sfx       <target>_banks.bin = table entry 0's container on the disc; the
            bindings and refusals pinned; sfx_registry.emsr = the RAM
            re-derivation of every capture
  ctx       ORIGINAL 001D8FD0 + 001D1C50 rebuild ctx +0xA0..+0xFF
  loaders   each file through its port loader (compiled privately from src/)
  canary    run_checks again over [a capture, a planted copy] and a copy of
            the export tree with file plants; each section must report its
            plant for the copy and stay silent for the original; every port
            loader must refuse a broken copy of its file
  controls  a changed input per comparator and the accept cases listed in
            docs/AREA13_ASSETS.md, with a pinned case per named survivor of
            the review (survivor_controls)
Default: AREA13 a04b_04, a13_01, a13_04 and AREA19 a13_05; EM_TEST_FULL=1:
all 7 captures.
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
import subprocess
import sys
import time
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area13_common as A13  # noqa: E402  (first: it keeps AREA01's nested load-map builder)
import export_area13_level as E1  # noqa: E402
import export_area13_tables as E2  # noqa: E402
import export_area13_sfx as E3  # noqa: E402

C = A13.configure('area13')
L = E1.L
T = E2.T
S = E3.S
import test_area01_assets_reference as T01  # noqa: E402
from reference_mode import FULL, MODE  # noqa: E402

# EM_AREA13_ASSETS: check another export tree (mutation sweeps); default assets/area13
A = Path(os.environ.get('EM_AREA13_ASSETS') or A13.OUT).resolve()
BUILD = C.ROOT / 'build/area13/assets/test'
CANARY = BUILD / f'canary-{os.getpid()}'
T01.BUILD = BUILD
FAILS = T01.FAILS
check = T01.check

QUICK = {'area13': ('a04b_04_lift', 'a13_01_door14', 'a13_04_hatch'), 'area19': ('a13_05_shaft',)}
EXCLUDED = {'area13': ['a13_05_shaft'], 'area19': ['a04b_04_lift', 'a13_00_door8', 'a13_01_door14',
                                                   'a13_02_door17', 'a13_03_item27', 'a13_04_hatch']}
# SHA-256 of INDEX.IDX sector D_00810700 + 4: the top descriptor, and the
# nested blocks when there are any (hashes, not disc data)
DESCRIPTOR_SHA256 = {'area13': 'b0bd2a69374a52077d398d836effbd88979986ae2f82f6b2b1ef2747dd257e52',
                     'area19': '3156e71435c0344d30a71807dc5a287c7c92cb44301a767d906e91f912d1a68f'}
GRID = {'area13': 0x17D1E40, 'area19': 0x18FE980}
STALE_5A4 = 0x1980000
LABEL = re.compile(r'(chunk\d\d(?:\.n\d)?)/id([0-9a-f]{2})')
CANARY_TEST_1 = 0x8153A0      # where the level kicks take TEST_1 (0x5000D), as in the earlier areas

# (placement nodes live, of them at their record's position and rotation)
ROSTER_LIVE = {'a04b_04_lift': (66, 66), 'a13_00_door8': (66, 66), 'a13_01_door14': (66, 66),
               'a13_02_door17': (64, 63), 'a13_03_item27': (62, 61), 'a13_04_hatch': (62, 61),
               'a13_05_shaft': (54, 53)}
# model owners bound to a bank model (the opened hatch counted by its own rule)
MODEL_OWNERS = {'a04b_04_lift': 64, 'a13_00_door8': 64, 'a13_01_door14': 64, 'a13_02_door17': 64,
                'a13_03_item27': 64, 'a13_04_hatch': 64, 'a13_05_shaft': 41}
# A placement node whose C rewrites a copied field: AREA13 [47] 0x8293A0
# (func_overlay_AREA13_00829360.c) sets +0x0D = REC[0x2C] (the next 0x28-byte
# record's +4) in state 0 while D_008107F4 bit 0x40 is clear, and back to
# REC[4] in state 1 only with the bit set. {behaviour: rule(ram, node, index, places) -> expected +0x0D}
def _rule_8293a0(ram, node, index, places):
    if ram[0x8107F4] & 0x40 or ram[node + 4] != 1 or index + 1 >= len(places):
        return None                                    # not modelled: the field must be unchanged
    return places[index + 1][4]


FIELD_RULES = {0x8293A0: _rule_8293a0}
BINDING = {
    'area13': {(1, 0): ('global', 0), (1, 1): ('global', 1), (1, 2): ('global', 2), (2, 0): ('area', 0),
               (4, 0): ('area', 1), (4, 1): ('area', 2)},
    'area19': {(1, 0): ('global', 0), (1, 1): ('global', 1), (1, 2): ('global', 2), (2, 0): ('area', 0),
               (4, 0): ('area', 1), (4, 1): ('area', 2), (4, 2): ('area', 3)}}
REFUSED = {'area13': {(3, 0), (4, 2)}, 'area19': {(3, 0)}}
# Every jal / j / address word of 001A2370, 0x219F50 and 0019C6F0 in the ELF
# and the target's module, by the function that holds it (runtime addresses,
# docs/AREA13_OVERLAY.md / AREA19_OVERLAY.md). MODELLED: the owners the
# exporter models. SUB1: sub-1 placement behaviours (never spawned in sub 0;
# the census requires no live node and no sub-0 record of them).
SITES = {
    'area13': {0x1A2370: {0x156620: (0x156EF0,), 0x219550: (0x219668,), 0x219F50: (0x21A104,),
                          0x824BB0: (0x824CE4, 0x825014, 0x82527C, 0x82563C)},
               0x219F50: {0x219870: (0x2198E4,)},
               0x19C6F0: {0x8293A0: (0x82964C, 0x829658, 0x82966C, 0x829678, 0x82998C, 0x829998)}},
    'area19': {0x1A2370: {0x156620: (0x156EF0,), 0x219550: (0x219668,), 0x219F50: (0x21A104,),
                          0x826C10: (0x826D08, 0x826ED4, 0x826F50, 0x8271EC), 0x827430: (0x827490, 0x8274F0),
                          0x827DD0: (0x827F04, 0x828220, 0x828488, 0x828834)},
               0x219F50: {0x219870: (0x2198E4,)},
               0x19C6F0: {0x825C70: (0x825D00, 0x825D14, 0x825E90), 0x826570: (0x82660C, 0x826718),
                          0x826C10: (0x826CFC, 0x827128), 0x8279E0: (0x827A3C,), 0x827B20: (0x827B40,),
                          0x829A70: (0x829AF8, 0x829C50)}}}
# Script callbacks among the owners: {callback: (its chain, the one behaviour
# whose C starts that chain)}. AREA19 0x827B20 (func_overlay_AREA19_00827AE0,
# 0019C6F0(8, 1)) is chain 0x82E090's op09 callback; only [34] 0x8279E0, a
# sub-1 placement, starts that chain.
CALLBACKS = {'area13': {}, 'area19': {0x827B20: (0x82E090, 0x8279E0)}}
SUB1 = {'area13': (), 'area19': (0x826C10, 0x827430, 0x826570, 0x8279E0, 0x829A70)}
FUNC_SIZE = {0x156620: 0x9B0, 0x219550: 0x320, 0x219F50: 0x230, 0x219870: 0x6E0, 0x824BB0: 0x1590,
             0x8293A0: 0x63C, 0x825C70: 0x270, 0x826570: 0x2CC, 0x826C10: 0x818, 0x827430: 0x104,
             0x8279E0: 0x124, 0x827B20: 0x38, 0x827DD0: 0x15A0, 0x829A70: 0x320}
DOOR_CALLEES = ('func_001BC150', 'func_001BC240')


def key(cap):
    """The capture name the pinned tables use (a planted copy keeps its source's)."""
    return getattr(cap, 'key', cap.name)


def use(K):
    """Point every shared module at K's target."""
    E1.install(K.t)
    T01.OVERLAY_SHA256 = K.t.overlay_sha256
    T01._DISC.clear()
    T01.GRID = GRID[K.t.name]


def sub_dir(t, tree=None):
    return (tree or A) / Path(*t.out.relative_to(A13.OUT).parts)


def side_dir(t, tree=None):
    return sub_dir(t, tree).parent


# ---------------------------------------------------------------------------
# Load map


def label_problems(lmap, cap, info):
    """Every row is labelled chunkNN[.nS]/idXX by the original's own reading
    of its block's relocation list: the rows of each block tile its resident
    region from its cursor without a gap (the top block from D_0028A73C, the
    nested one from D_0028A740), and a row labelled idXX lies inside
    [D_0028A490[XX], the next address the same block's list relocates, or
    the region's end), all read from the capture's RAM."""
    out, ram = [], cap.ram
    lists = A13.relocation_lists(cap)
    regions = [(info['chunk'], C.u32(ram, C.D_0028A73C), lists['top'])]
    if lists['nested']:
        regions.append((info['nested'], C.u32(ram, C.D_0028A740), lists['nested']))
    rows = sorted(lmap)
    for chunk, base, entries in regions:
        mine = [r for r in rows if r[4].split('/')[0] == chunk]
        if not mine or mine[0][0] != base:
            out.append(f'{cap.name}: {chunk} rows do not start at its cursor {base:#x}')
            continue
        end = mine[-1][0] + mine[-1][3]
        for x, y in zip(mine, mine[1:]):
            if x[0] + x[3] != y[0]:
                out.append(f'{cap.name}: a gap in {chunk} at {x[0] + x[3]:#x}')
        starts = sorted(C.u32(ram, A13.D_0028A490 + 4 * i) for i, _o in entries)
        ids = {i for i, _o in entries}
        for a, _p, _o, n, label in mine:
            m = LABEL.fullmatch(label)
            if m is None or m.group(1) != chunk or int(m.group(2), 16) not in ids:
                out.append(f'{cap.name}: load map label {label} is not a relocation id of {chunk}')
                continue
            ident = int(m.group(2), 16)
            start = C.u32(ram, A13.D_0028A490 + 4 * ident)
            stop = min([x for x in starts if x > start], default=end)
            if not start <= a < a + n <= stop:
                out.append(f'{cap.name}: load map row {a:#x}+{n:#x} labelled {label} lies outside '
                           f'D_0028A490[{ident:#x}] {start:#x}..{stop:#x}')
    if len(rows) != sum(1 for r in rows if r[4].split('/')[0] in {c for c, _b, _e in regions}):
        out.append(f'{cap.name}: load map rows outside the descriptor\'s blocks')
    return out


def stale_problems(K, caps):
    """Id 0x45 is in no relocation list, D_0028A5A4 holds STALE_5A4 in every
    capture and in the capture before the target's load."""
    out = []
    for cap in caps:
        lists = A13.relocation_lists(cap)
        if any(i == 0x45 for i, _o in lists['top'] + lists['nested']):
            out.append(f'{cap.name}: the descriptor relocates id 0x45')
        if C.u32(cap.ram, E1.D_0028A5A4) != STALE_5A4:
            out.append(f'{cap.name}: D_0028A5A4 = {C.u32(cap.ram, E1.D_0028A5A4):#x}, not {STALE_5A4:#x}')
    prev = E1.previous_word(K.t.name, E1.D_0028A5A4)
    if prev is not None and prev != STALE_5A4:
        out.append(f'{E1.PREVIOUS[K.t.name].name}: D_0028A5A4 = {prev:#x}, not {STALE_5A4:#x}')
    return out


def load_problems(K, caps, cells_len):
    out = list(T01.load_map_problems(K.lmap))
    table = C.u32(K.first.spad, L.SPAD_CELLS)
    for cap in caps:
        try:
            lmap, info = C.build_load_map(cap)
        except SystemExit as error:
            out.append(f'{cap.name}: load map: {error}')
            continue
        if lmap != K.lmap:
            out.append(f'{cap.name}: a different load map')
        if C.u32(cap.spad, L.SPAD_CELLS) != table:
            out.append(f'{cap.name}: cell directory pointer')
        rows = C.compare_load_map(K.image, cap, allow=[(table, table + cells_len)])
        if any(r['unexpected_rows'] for r in rows):
            out.append(f'{cap.name}: load map differs from RAM')
        out += A13.relocation_problems(cap)[0]
        out += label_problems(K.lmap, cap, info)
    out += stale_problems(K, caps)
    return out


# ---------------------------------------------------------------------------
# Level


_ZONES = {}
_BG = {}


def bank_problems(bank_img, caps):
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


def background_problems(K, caps, sub):
    """background.embg against export_level.export_background run on every
    capture (each run compares its disc texels with that capture's GS
    freeze); every capture arms the background (AREA13) or none does
    (AREA19, then no file)."""
    out = []
    path = sub / K.el.BG_ASSET
    armed = [r['armed'] for r in L.check_background(caps)]
    if not any(armed):
        return [f'{K.t.name}: background.embg exists but no capture arms the background'] if path.exists() else []
    if not all(armed):
        return [f'{K.t.name}: the background is armed in {sum(armed)} of {len(caps)} captures']
    if not path.exists():
        return [f'{K.t.name}: no background.embg although the captures arm the background']
    blob = path.read_bytes()
    disc = K.el.BackgroundDisc(None, str(C.ISO_PATH))
    for cap in caps:
        gkey = (id(cap.ram), str(cap.gs))
        if gkey not in _BG:
            scene = BUILD / f'bg-{os.getpid()}' / cap.name
            scene.mkdir(parents=True, exist_ok=True)
            ee = scene / 'eeMemory.bin'
            ee.write_bytes(cap.ram)
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    K.el.export_background(scene, C.ELF_PATH, ee, cap.gs, C.AREA, C.SUB, disc)
                _BG[gkey] = ((scene / K.el.BG_ASSET).read_bytes(), cap.ram)
            except SystemExit as error:
                _BG[gkey] = (f'{error}', cap.ram)
            finally:
                for q in scene.iterdir():
                    q.unlink()
                scene.rmdir()
                with contextlib.suppress(OSError):
                    scene.parent.rmdir()
        got = _BG[gkey][0]
        if isinstance(got, str):
            out.append(f'{cap.name}: background: {got}')
        elif got != blob:
            out.append(f'{cap.name}: background.embg differs from the capture\'s export')
    return out


def zone_pairs(files, zones, image):
    """[(zone file, (source label, zone))] in load-map order."""
    order = [lab for _a, _p, _o, _s, lab in image.map]
    return list(zip(files, sorted(zones.items(), key=lambda z: order.index(z[0]))))


def zone_name_problems(target, pairs):
    """Each zone file is named NN_idXX.emdl after its source row's id."""
    out = []
    for path, (label, _z) in pairs:
        name = Path(path).name
        if name.split('_', 1)[1] != Path(label).name.split('.')[0] + '.emdl':
            out.append(f'{target}/{name}: zone name')
    return out


def level_problems(K, caps, sub):
    out, el, image = [], K.el, K.image
    lvl = sub / 'level'
    bank_img = (lvl / 'static_bank.emsc').read_bytes()
    out += bank_problems(bank_img, caps)
    base, _entry, length = struct.unpack_from('<3I', bank_img, 8)
    bank = bank_img[20:20 + length]
    if (lvl / 'dynamic_objects.emsc').exists():
        out.append(f'{K.t.name}: a dynamic-list file (no capture draws one)')
    read = lambda a, n: bank[a - base:a - base + n]
    try:
        objects = L.bank_objects(read, base)
        kicks, states, _touched = L.check_kicks(caps, objects, [])
    except (SystemExit, struct.error, ValueError) as error:
        return out + [f'level: {error}'], {}
    if any(k['dynamic_kicks'] for k in kicks):
        out.append(f'{K.t.name}: a dynamic-list kick')
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
        out.append(f'{K.t.name}: {len(files)} zone files for {len(zones)} source files')
    pairs = zone_pairs(files, zones, image)
    out += zone_name_problems(K.t.name, pairs)
    for path, (label, (b, _ids, bad, _recs)) in pairs:
        if bad:
            out.append(f'{label}: records with a matrix slot')
        name = Path(path).name
        out += T01.zone_problems(el, f'{K.t.name}/{name}', Path(path).read_bytes(), b, gs_caps, prim)
    out += background_problems(K, caps, sub)
    return out, dict(objects=len(objects), zones=len(files), kicks=sum(k['level_kicks'] for k in kicks))


# ---------------------------------------------------------------------------
# Collision, cells and the call-site census


def emcl_problems(K, emcl, caps, cells_len):
    out = []
    T01.GRID = GRID[K.t.name]
    for cap in caps:
        table = C.u32(cap.spad, L.SPAD_CELLS)
        for problem in T01.grid_problems(cap.ram, K.image, (table, table + cells_len)):
            out.append(f'{cap.name}: {problem}')
        where = T01.emcl_equal(emcl, cap.ram)
        if where is not None:
            out.append(f'{cap.name}: {K.t.name}.emcl differs from the RAM grid rebuild: {where}')
    return out


def cells_problems(K, cells, caps, record):
    out = []
    table = C.u32(K.first.spad, L.SPAD_CELLS)
    try:
        disc = K.image.read(table, len(cells))
    except ValueError as error:
        return [f'cells: {error}']
    if cells != disc:
        k = next(j for j in range(len(cells)) if cells[j] != disc[j])
        out.append(f'{K.t.name}_cells.bin differs from the disc bytes at +{k:#x}')
    try:
        _count, _hulls, size = L.cell_directory(cells, 0)
        if size != len(cells):
            out.append(f'{K.t.name}_cells.bin is {len(cells)} bytes, the directory {size}')
        inside = T01.hulls_inside_table(L, cells)
        if inside:
            out.append(f'cells: hulls {inside} start inside the uid table')
        rows, problems, calls = E1.verify_directory(K.elf, cells, caps)
    except (AssertionError, IndexError, struct.error, ValueError) as error:
        return out + [f'cells: the directory does not parse ({error!r})']
    out += problems
    rec = record['cells']
    union = sorted({u for x in rec['captures'] for u in x['moved_hulls']})
    if rec['moved_uids'] != union:
        out.append(f'cells.json moved_uids {rec["moved_uids"]} is not the union of its rows {union}')
    for r in rows:
        cap = next(c for c in caps if c.name == r['capture'])
        want = next((x['moved_hulls'] for x in rec['captures'] if x['capture'] == key(cap)), None)
        if want is None:
            out.append(f'{r["capture"]}: not recorded in cells.json')
        elif [int(u) for u in r['moved_hulls']] != [int(u) for u in want]:
            out.append(f'{r["capture"]}: re-transformed hulls {r["moved_hulls"]} != cells.json {want}')
    for name, used in calls.items():
        cap = next(c for c in caps if c.name == name)
        want = rec['flag_calls'].get(key(cap))
        if want is None or [list(c) for c in used] != want:
            out.append(f'{name}: flag calls {used} != cells.json {want}')
    return out


def chain_starters(t, chain):
    """The runtime addresses of the target's overlay C functions that pass
    `chain` to func_001BA1A0 (link name + 0x40)."""
    out = set()
    for f in sorted((C.DECOMP / f'src/overlays/{t.label}').glob('*.c')):
        for m in E2.C_CALL.finditer(f.read_text()):
            if int(m.group(2), 16) == chain:
                out.add(int(f.stem[-8:], 16) + 0x40)
    return out


def word_sites(blob, base, word):
    pat, out, k = struct.pack('<I', word), [], blob.find(struct.pack('<I', word))
    while k >= 0:
        if k % 4 == 0:
            out.append(base + k)
        k = blob.find(pat, k + 1)
    return out


def census_problems(K, caps, placements=None, groups=None):
    """Every jal / j / address word of 001A2370, 0x219F50 and 0019C6F0 in the
    ELF and the module is a SITES entry, each inside its function
    (FUNC_SIZE); the modelled owners are the exporter's; a SUB1 owner has
    no live node in any capture and no sub-0 placement or group record."""
    out = []
    text = K.elf[C.ELF_OFFSET:C.ELF_OFFSET + C.ELF_FILESZ]
    pinned = SITES[K.t.name]
    for callee, owners in pinned.items():
        got = []
        for word in ((3 << 26) | (callee >> 2), (2 << 26) | (callee >> 2), callee):
            got += word_sites(text, C.ELF_VADDR, word) + word_sites(K.ov, C.u32(K.ov, 8), word)
        want = sorted(a for sites in owners.values() for a in sites)
        if sorted(got) != want:
            out.append(f'{callee:#x} call sites {[hex(a) for a in sorted(got)]} != the census')
        for owner, sites in owners.items():
            if not all(owner <= a < owner + FUNC_SIZE[owner] for a in sites):
                out.append(f'{callee:#x}: a site of {owner:#x} lies outside it')
    modelled = {E1.PICKUP, E1.DRUM, E1.CREATURES[K.t.name], E1.SCALED_CALLER, E1.SCALED_OWNER,
                E1.FLAG_OWNERS[K.t.name]}
    owners = {o for owners in pinned.values() for o in owners}
    callbacks = CALLBACKS[K.t.name]
    if owners - modelled - set(callbacks) != set(SUB1[K.t.name]):
        out.append(f'unmodelled owners {sorted(hex(o) for o in owners - modelled)} != the sub-1 list')
    for cb, (chain, starter) in callbacks.items():
        if starter not in SUB1[K.t.name] or chain_starters(K.t, chain) != {starter}:
            out.append(f'callback {cb:#x}: chain {chain:#x} is started by '
                       f'{sorted(hex(x) for x in chain_starters(K.t, chain))}, not only the sub-1 {starter:#x}')
        olo, ohi = E2.E02.overlay_data_window(K.ov)
        recs = T.walk_chain(K.ov[olo - C.OVERLAY_ARENA:ohi - C.OVERLAY_ARENA], olo, chain)
        if not any(struct.pack('<I', cb) in K.ov[r - C.OVERLAY_ARENA:r - C.OVERLAY_ARENA + 0x40] for r in recs):
            out.append(f'callback {cb:#x}: no record of chain {chain:#x} names it')
    for o in SUB1[K.t.name]:
        for cap in caps:
            if any(C.u32(cap.ram, a + 0x10) == o for _s, a in T.pool_nodes(cap.ram)):
                out.append(f'{cap.name}: a live node of the sub-1 owner {o:#x}')
        if placements is not None and (any(C.u32(r, 0x24) == o for r in placements) or
                                       any(C.u32(r, 0x28) == o for _a, recs in groups for r in recs)):
            out.append(f'the sub-0 roster names the sub-1 owner {o:#x}')
    return out


# ---------------------------------------------------------------------------
# Tables


def roster_problems(K, roster, caps):
    out = []
    try:
        disc = T01.roster_image(T01.disc_reader())
        if roster != disc:
            out.append(f'{K.t.name}: roster.emro differs from the rebuild from the disc')
        for cap in caps:
            if not T01.roster_equal(disc, cap.ram):
                out.append(f'{cap.name}: roster records differ from RAM')
            _ok, live, _pairs = T01.roster_nodes(T, disc, cap.ram)
            out += [f'{cap.name}: {p}' for p in field_problems(disc, cap.ram)]
            if live != ROSTER_LIVE.get(key(cap)):
                out.append(f'{cap.name}: placement nodes live / at the record position {live} != '
                           f'{ROSTER_LIVE.get(key(cap))}')
    except (ValueError, IndexError, struct.error) as error:
        out.append(f'roster: {error!r}')
    return out


def field_problems(blob, ram):
    """Every live node a placement record spawned keeps the record's +0x03,
    +0x0D and +0x54 (001B6990, T01.roster_nodes' fields), except +0x0D of a
    FIELD_RULES behaviour, which must equal its rule's value."""
    out = []
    groups, count = C.u16(blob, 0x0A), C.u32(blob, 0x0C)
    at = 0x18 + 8 * groups + sum(0x2C * struct.unpack_from('<II', blob, 0x18 + 8 * g)[1] for g in range(groups))
    places = [blob[at + 0x28 * i:at + 0x28 * (i + 1)] for i in range(count)]
    for i, rec in enumerate(places):
        if (C.s16(rec, 0) & 0xFF) == 0x0B:
            continue
        f, _pos, _rot = T.spawn_fields_placement(rec, i)
        for _slot, a in T.pool_nodes(ram):
            if ram[a + 0x10:a + 0x14] != f[0x10] or ram[a + 0x9A] != i:
                continue
            rule = FIELD_RULES.get(C.u32(rec, 0x24))
            want = dict(f)
            if rule is not None and rule(ram, a, i, places) is not None:
                want[0x0D] = bytes([rule(ram, a, i, places)])
            for off in (0x03, 0x0D, 0x54):
                if ram[a + off:a + off + len(want[off])] != want[off]:
                    out.append(f'placement [{i}] node {a:#x} lost its copied field +{off:#x}')
    return out


def window_problems(label, blob, window, disc, caps, records, recorded):
    """An EMSC window of the module's data: header, extent, bytes = the
    module, and every capture's run-time words by E2.run_time_words (a
    reached chain record or a writer window whose value its C gives); the
    words tables.json records must equal them."""
    out = T01.emsc_header_problems(label, blob)
    base, d = T01.emsc_window(blob)
    if (base, len(d)) != window:
        return out + [f'{label}: window {base:#x}+{len(d):#x}, not {window[0]:#x}+{window[1]:#x}']
    if d != disc:
        out.append(f'{label} differs from the disc overlay')
    for cap in caps:
        words, problems = E2.run_time_words(disc, window[0], cap.ram, records)
        out += [f'{cap.name}: {label}: {p}' for p in problems]
        if [hex(a) for a in words] != recorded.get(key(cap), []):
            out.append(f'{cap.name}: {label} run-time words {[hex(a) for a in words]} != tables.json')
    return out


def model_binding_problems(wm, caps):
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
                if table + (C.s32(ram, table + 4 + 4 * (ram[a + 0x0D] & 0x7FFF)) >> 2 << 2) != m:
                    out.append(f'{cap.name} slot {slot}: +0x44 != 001C6120(table, +0x0D)')
                if ram[a + 0x0C] != C.u32(ram, m + 8) or not all(C.u32(ram, a + 0x110 + 4 * k)
                                                                 for k in range(ram[a + 0x0C])):
                    out.append(f'{cap.name} slot {slot}: bone count / slots')
            out += E2.explicit_model_problems(ram, cap.name)
            bound += len(E2.explicit_model_nodes(ram))
            if bound != MODEL_OWNERS.get(key(cap)):
                out.append(f'{cap.name}: {bound} model owners bound, not {MODEL_OWNERS.get(key(cap))}')
        except struct.error as error:
            out.append(f'{cap.name}: model bindings {error!r}')
    return out


def spawn_entry_problems(caps):
    out = []
    dread = T01.disc_reader()
    _table, entries, count = E2.spawn_rows(dread)
    for cap in caps:
        room = cap.ram[0x810702]
        if room >= count:
            out.append(f'{cap.name}: spawn entry {room} outside the {count} records')
            continue
        want = C.u32(dread(entries + 0x30 * room + 0x1C, 4), 0)
        if C.u32(cap.ram, T.D_008106C8) != want:
            out.append(f'{cap.name}: D_008106C8 {C.u32(cap.ram, T.D_008106C8):#x} != +0x1C of spawn record {room}')
    return out


def c_door_behaviours(t):
    """The behaviours whose committed C calls 001BC150 or 001BC240: the
    boot files src/func_*.c (001BC240 itself excluded) and the target's
    overlay files (runtime = link name + 0x40)."""
    out = set()
    pat = re.compile(r'(' + '|'.join(DOOR_CALLEES) + r')\s*\((?!\s*(?:void|char|unsigned)\b)')
    strip = lambda text: re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)
    for f in sorted((C.DECOMP / 'src').glob('func_001B*.c')):
        if f.stem != 'func_001BC240' and pat.search(strip(f.read_text())):
            out.add(int(f.stem[5:], 16))
    for f in sorted((C.DECOMP / f'src/overlays/{t.label}').glob('*.c')):
        if pat.search(strip(f.read_text())):
            out.add(int(f.stem[-8:], 16) + 0x40)
    return out


def doors_used_problems(roster, row, used, caps):
    """tables.json doors.used against the roster's placements whose
    behaviour is a DOOR_BEHAVIOURS entry, each record = the row in RAM."""
    out = []
    groups, count = C.u16(roster, 0x0A), C.u32(roster, 0x0C)
    at = 0x18 + 8 * groups + sum(0x2C * struct.unpack_from('<II', roster, 0x18 + 8 * g)[1] for g in range(groups))
    places = [roster[at + 0x28 * i:at + 0x28 * (i + 1)] for i in range(count)]
    want = []
    for i, rec in enumerate(places):
        if C.u32(rec, 0x24) in E2.DOOR_BEHAVIOURS:
            door = rec[3] & 0x7F
            want.append(dict(placement=i, behaviour=hex(C.u32(rec, 0x24)), door_id=door,
                             area_change=bool(rec[3] & 0x80), record=list(row[4 * door:4 * door + 4])))
    if used != want:
        out.append(f'tables.json doors.used {used} != the recomputed {want}')
    for d in used:
        for cap in caps:
            at = E2.DOOR_ROW[A13.current().name] + 4 * d['door_id']
            if list(cap.ram[at:at + 4]) != d['record']:
                out.append(f'{cap.name}: door {d["door_id"]} record differs from RAM')
    return out


def roster_records(roster):
    """(placement records, [(group address, group records)]) of an EMRO."""
    groups, count = C.u16(roster, 0x0A), C.u32(roster, 0x0C)
    gl, at = [], 0x18 + 8 * groups
    for g in range(groups):
        ga, gn = struct.unpack_from('<II', roster, 0x18 + 8 * g)
        gl.append((ga, [roster[at + 0x2C * k:at + 0x2C * (k + 1)] for k in range(gn)]))
        at += 0x2C * gn
    return [roster[at + 0x28 * i:at + 0x28 * (i + 1)] for i in range(count)], gl


def script_entry_problems(t, rec):
    """SCRIPT_ENTRIES = the chains the committed C starts = tables.json's."""
    out = []
    entries = E2.c_script_entries(t)
    if entries != set(E2.SCRIPT_ENTRIES[t.name]):
        out.append(f'the C starts chains {sorted(hex(e) for e in entries)}, not SCRIPT_ENTRIES')
    if sorted(int(k, 16) for k in rec['scripts']['chains']) != sorted(E2.SCRIPT_ENTRIES[t.name]):
        out.append(f'tables.json chains {sorted(rec["scripts"]["chains"])} != SCRIPT_ENTRIES')
    return out


def door_behaviour_problems(t):
    """DOOR_BEHAVIOURS = the behaviours whose committed C reaches 001BC150."""
    got = c_door_behaviours(t)
    if got != set(E2.DOOR_BEHAVIOURS) - {0x1BC240}:
        return [f'the C door callers {sorted(hex(b) for b in got)} != DOOR_BEHAVIOURS']
    return []


def tables_problems(K, caps, files=None):
    files = files or {}
    t = K.t
    sub, side = sub_dir(t), side_dir(t)
    read = lambda p: files[p] if p in files else p.read_bytes()
    out = []
    try:
        roster = read(sub / 'roster.emro')
        out += roster_problems(K, roster, caps)
        for name, layout in (('spawn_table.emsp', T01.spawn_layout), ('door_destinations.emsp', T01.doors_layout)):
            out += T01.emsp_problems(name, read(side / name), caps, layout(T01.disc_reader()))[0]
        out += spawn_entry_problems(caps)
        rec = json.loads(read(side / 'tables.json'))
        out += script_entry_problems(t, rec)
        out += door_behaviour_problems(t)
        olo, ohi = E2.E02.overlay_data_window(K.ov)
        odisc = K.ov[olo - C.OVERLAY_ARENA:ohi - C.OVERLAY_ARENA]
        records = E2.chain_records(odisc, olo)
        slo, shi = E2.scripts_window(K.ov)
        out += window_problems('scripts', read(side / 'scripts.emsc'), (slo, shi - slo),
                               K.ov[slo - C.OVERLAY_ARENA:shi - C.OVERLAY_ARENA], caps, records,
                               rec['scripts']['run_time_words'])
        out += window_problems('overlay data', read(side / 'overlay_data.emsc'), (olo, ohi - olo), odisc, caps,
                               records, rec['overlay_data']['run_time_words'])
        out += T01.messages_problems(read(sub / 'message_data.emmd'), caps, K.elf)[0]
        wm = read(sub / 'world_models.emwm')
        out += T01.world_models_problems(wm, caps)[0]
        out += model_binding_problems(wm, caps)
        row = bytes(T01.disc_reader()(E2.DOOR_ROW[t.name], 4 * rec['doors']['records']))
        out += doors_used_problems(roster, row, rec['doors']['used'], caps)
        places, gl = roster_records(roster)
        out += census_problems(K, caps, places, gl)
    except (SystemExit, ValueError, KeyError, IndexError, struct.error, json.JSONDecodeError) as error:
        out.append(f'tables: {error!r}')
    return out


# ---------------------------------------------------------------------------
# Sound


def sfx_problems(K, caps, banks, registry):
    out = []
    try:
        gname = S.X.GLOBAL_CONTAINER
        gdata = (C.DECOMP / gname).read_bytes()
        _label, disc, _parsed, _names, first = E3.area_container(K.first)
        if banks != disc:
            k = next((j for j in range(min(len(banks), len(disc))) if banks[j] != disc[j]), min(len(banks), len(disc)))
            out.append(f'{K.t.name}_banks.bin differs from the disc container at +{k:#x} ({len(banks)} / {len(disc)})')
        total = C.u32(banks, 0) if len(banks) >= 4 else -1
        if len(banks) != total or total > first[1]:
            out.append(f'{K.t.name}_banks.bin: {len(banks)} bytes, container total {total:#x}')
        parsed = S.X.A.parse_container(banks)
        if parsed is None:
            return out + [f'{K.t.name}_banks.bin is not an SShd container']
        name = f'{K.t.name}_sub0_banks.bin'
        containers = {gname: (gdata, S.X.A.parse_container(gdata)), name: (banks, parsed)}
        bound, refused, _rep = S.bindings_from_captures(caps, containers)
        got = {k: ('global' if v[0][0] == gname else 'area', v[0][1]) for k, v in bound.items()}
        if got != BINDING[K.t.name] or set(refused) != REFUSED[K.t.name]:
            return out + [f'bank bindings {got}, refused {sorted(refused)}']
        for cap in caps:
            want, _counts, _n, _e = T01.registry_from_ram(cap.ram, bound, {gname: gdata, name: disc})
            if registry != want:
                k = next((j for j in range(min(len(registry), len(want))) if registry[j] != want[j]),
                         min(len(registry), len(want)))
                out.append(f'{cap.name}: sfx_registry.emsr differs from the RAM re-derivation at +{k:#x}')
    except (SystemExit, ValueError, TypeError, KeyError, IndexError, StopIteration, struct.error) as error:
        out.append(f'sfx: {error!r}')
    return out


def ctx_problems(K, caps):
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            return [], E1.E22.check_ctx_block(caps, K.elf)
    except SystemExit as error:
        return [f'ctx block: {error}'], []


def run_checks(K, caps, tree=None):
    """Every real check of K's target over the export tree; (problems, summary)."""
    use(K)
    t = K.t
    sub = sub_dir(t, tree)
    P, V = [], {}
    cells = (sub / f'{t.name}_cells.bin').read_bytes()
    record = json.loads((sub / 'cells.json').read_text())
    P += load_problems(K, caps, len(cells))
    p, V['level'] = level_problems(K, caps, sub)
    P += p
    P += emcl_problems(K, (sub / f'{t.name}.emcl').read_bytes(), caps, len(cells))
    use(K)
    P += cells_problems(K, cells, caps, record)
    P += sfx_problems(K, caps, (sub / f'sfx/{t.name}_banks.bin').read_bytes(),
                      (sub / 'sfx/sfx_registry.emsr').read_bytes())
    P += tables_problems(K, caps)
    p, V['room'] = ctx_problems(K, caps)
    P += p
    return P, V


# ---------------------------------------------------------------------------
# Port loaders


BG_SHIM = r'''
#include "gfx/metal/em_background_gs.h"
int em13_bg_parse(const unsigned char *b, unsigned long n)
{ EmBackgroundGsAsset a; int r = em_background_gs_parse(b, n, &a); if (r) return r;
  return em_background_gs_unsupported(&a) ? 100 : 0; }
'''


def background_lib():
    BUILD.mkdir(parents=True, exist_ok=True)
    src, lib = BUILD / 'bgshim.c', BUILD / 'bgshim.dylib'
    headers = list((C.ROOT / 'src/gfx/metal').glob('*.h')) + [Path(__file__)]
    if not lib.exists() or lib.stat().st_mtime < max(x.stat().st_mtime for x in headers):
        src.write_text(BG_SHIM)
        subprocess.run(['cc', '-std=c11', '-O1', '-Wall', '-Werror', '-shared', '-fPIC',
                        '-I' + str(C.ROOT / 'src'), str(src), '-o', str(lib), '-lm'], check=True)
    out = CT.CDLL(str(lib))
    out.em13_bg_parse.restype = CT.c_int
    out.em13_bg_parse.argtypes = [CT.c_char_p, CT.c_ulong]
    return out


def doors_loader_ok(lib, path, row):
    lib.em_spawn_table_read.restype = CT.c_void_p
    lib.em_spawn_table_read.argtypes = [CT.c_void_p, CT.c_uint32, CT.c_uint32]
    lib.em_spawn_table_load.restype = CT.c_int
    doors = CT.create_string_buffer(16 << 20)
    ok = lib.em_spawn_table_load(doors, str(path).encode()) == 0
    return ok and bool(lib.em_spawn_table_read(doors, 0x24E140, 4 * 0x17)) and \
        bool(lib.em_spawn_table_read(doors, row, 0x10))


def check_loaders(lib, bg, t, tree):
    big = lambda: CT.create_string_buffer(16 << 20)

    def rc(fn, *args):
        f = getattr(lib, fn)
        f.restype = CT.c_int
        return f(*args)
    sub, side = sub_dir(t, tree), side_dir(t, tree)
    out = {}
    lib.em_spawn_table_read.restype = CT.c_void_p
    lib.em_spawn_table_read.argtypes = [CT.c_void_p, CT.c_uint32, CT.c_uint32]
    table = big()
    rows = E2.SPAWN_SUB0[t.name]
    ok = rc('em_spawn_table_load', table, str(side / 'spawn_table.emsp').encode()) == 0
    out['spawn'] = ok and bool(lib.em_spawn_table_read(table, rows[0], rows[1] * 0x30))
    out['doors'] = doors_loader_ok(lib, side / 'door_destinations.emsp', E2.DOOR_ROW[t.name])
    for rel in (side / 'overlay_data.emsc', side / 'scripts.emsc', sub / 'level/static_bank.emsc'):
        out[rel.name] = rc('em_script_image_load', big(), str(rel).encode()) == 1
    wm = (sub / 'world_models.emwm').read_bytes()
    out['world models'] = rc('em_world_models_parse', big(), wm, CT.c_size_t(len(wm))) == 0
    out['emcl'] = rc('em_collision_load', big(), str(sub / f'{t.name}.emcl').encode()) == 0
    out['roster'] = rc('em_actor_roster_load', big(), str(sub / 'roster.emro').encode()) == 0
    for z in sorted(str(x) for x in (sub / 'level').glob('*.emdl')):
        out[Path(z).name] = rc('em_model_load', big(), z.encode()) == 0
    out['sfx registry'] = rc('em_sfx_registry_load', big(), str(sub / 'sfx/sfx_registry.emsr').encode()) == 1
    cells = (sub / f'{t.name}_cells.bin').read_bytes()
    bit29 = any(C.u32(cells, 4 + 4 * u) & 0x20000000 for u in range(C.u32(cells, 0))) if len(cells) >= 4 else False
    crc = rc('em_actor_cells_load', big(), str(sub / f'{t.name}_cells.bin').encode())
    if bit29:
        # uid words with bit 29 (AREA01_ASSETS.md finding 1): the loader's
        # 0x3FFFFFFF mask keeps it and refuses the file; a copy with bit 29
        # cleared in every uid word must load (nothing else is refused)
        diag = bytearray(cells)
        for u in range(C.u32(cells, 0)):
            struct.pack_into('<I', diag, 4 + 4 * u, C.u32(cells, 4 + 4 * u) & ~0x20000000)
        BUILD.mkdir(parents=True, exist_ok=True)
        dpath = BUILD / f'cellsdiag-{os.getpid()}.bin'
        dpath.write_bytes(bytes(diag))
        try:
            out['cells (bit 29 cleared)'] = rc('em_actor_cells_load', big(), str(dpath).encode()) == 0
        finally:
            dpath.unlink()
        out['cells refused (bit 29)'] = crc != 0
    else:
        out['cells'] = crc == 0
    with T01.quiet_fds():
        out['messages'] = rc('em_message_live_install', str(sub / 'message_data.emmd').encode()) == 1
    bgp = sub / 'background.embg'
    if bgp.exists():
        blob = bgp.read_bytes()
        out['background'] = bg.em13_bg_parse(blob, len(blob)) == 0
    return out


# ---------------------------------------------------------------------------
# Canary


def ram_copy(cap, name, edits=(), gs=None):
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


def mutate(data, at, bit=1):
    b = bytearray(data)
    b[at] ^= bit
    return bytes(b)


def node_of(ram, behaviour, uid=None):
    for _s, a in T.pool_nodes(ram):
        if C.u32(ram, a + 0x10) == behaviour and (uid is None or ram[a + 0x0F] == uid):
            return a
    return None


def canary_plants(K, cap, tree, tag):
    """RAM plants for the canary copy of `cap` -> (copy, {section: text})."""
    t, ram = K.t, cap.ram
    sub, side = sub_dir(t), side_dir(t)
    edits, want = [], {}
    bank = (sub / 'level/static_bank.emsc').read_bytes()
    base, _e, length = struct.unpack_from('<3I', bank, 8)
    objects = [base + (C.s32(ram, base + 4 + 4 * i) >> 2 << 2) for i in range(C.u32(ram, base))]
    pad = min(objects) - 1
    edits.append(flip(ram, pad))
    want['load map'] = f'{tag}: load map differs from RAM'
    want['static bank'] = f'{tag}: static bank differs from RAM'
    last = max(objects, key=lambda o: o + 0x820 * C.u32(ram, o))
    edits.append((last, bytes([ram[last] + 1])))
    want['static bank extent'] = f'{tag}: static bank length'
    edits.append(flip(ram, E1.D_0028A5A0 + 2))
    want['D_0028A5A0'] = f'{tag}: D_0028A5A0'
    edits.append(flip(ram, E1.D_0028A5A4 + 1))
    want['stale D_0028A5A4'] = f'{tag}: D_0028A5A4 = '
    rel = A13.relocation_lists(cap)
    ident = rel['top'][-1][0]
    edits.append(flip(ram, A13.D_0028A490 + 4 * ident + 1))
    want['relocation word'] = f'{tag}: D_0028A490[{ident:#x}]'
    edits.append(flip(ram, CANARY_TEST_1, 2))
    want['GS state'] = 'level GS state'
    ctx = C.u32(ram, L.CTX_PTR)
    edits.append(flip(ram, ctx + 0xA4))
    want['ctx'] = f'ctx block: {tag}:'
    table = C.u32(cap.spad, L.SPAD_CELLS)
    cells = (sub / f'{t.name}_cells.bin').read_bytes()
    _count, hulls, _size = L.cell_directory(cells, 0)
    s0, _e0, _f0 = hulls[min(hulls)]
    edits.append(flip(ram, table + s0 + 0x20))
    want['cell hull'] = f'{tag}: hull {min(hulls)} differs'
    grid = GRID[t.name]
    edits.append(flip(ram, grid + C.u32(ram, grid) + 1))
    want['EMCL'] = f'{tag}: {t.name}.emcl differs from the RAM grid rebuild'
    roster = (sub / 'roster.emro').read_bytes()
    paddr = C.u32(roster, 0x10)
    edits.append(flip(ram, paddr + 0x28 * C.u32(roster, 0x0C) - 1))
    want['roster'] = f'{tag}: roster records differ from RAM'
    _ok, _live, pairs = T01.roster_nodes(T, roster, ram)
    edits.append(flip(ram, pairs[0][1] + 0x03))
    want['placement node fields'] = f'{tag}: placement [{pairs[0][0]}] node {pairs[0][1]:#x} lost its copied field +0x3'
    row = E2.DOOR_ROW[t.name]
    edits.append(flip(ram, row + 0x0F))
    want['door windows'] = f'{tag}: door_destinations.emsp window {row:#x} differs'
    rows, n = E2.SPAWN_SUB0[t.name]
    edits.append(flip(ram, rows + n * 0x30 - 1))
    want['spawn windows'] = f'{tag}: spawn_table.emsp window'
    gptr = C.u32(K.elf, T01.MSG_TABLES - C.ELF_VADDR + C.ELF_OFFSET)
    edits.append(flip(ram, gptr + 5))
    want['message records'] = f'{tag}: message records differ'
    a0 = C.u32(ram, 0x28A594)
    edits.append(flip(ram, a0 + T01.message_bank_size(ram, a0) // 2))      # not the last byte: the file plant flips that
    want['area message bank'] = f'{tag}: message banks differ'
    wm = (sub / 'world_models.emwm').read_bytes()
    edits.append(flip(ram, C.u32(wm, 8) + C.u32(wm, 12) - 1))
    want['world models'] = f'{tag}: world model bank differs'
    edits.append(flip(ram, T01.LADDER + 2 * 0x40))
    want['registry'] = f'{tag}: sfx_registry.emsr differs'
    slo = E2.SCRIPT_ENTRIES[t.name][0]
    edits.append(flip(ram, slo + 0x3F))
    want['scripts'] = f'{tag}: scripts run-time words'
    lo, _hi, _b = E2.WRITERS[t.name][0]
    edits.append(flip(ram, lo + 1))
    want['writer window'] = f'{tag}: overlay data: {lo:#x}:'
    creature = node_of(ram, E1.CREATURES[t.name])
    edits.append(flip(ram, C.u32(ram, creature + 0x11C) + 0x90 + 0x33, 0x04))
    want['creature matrix'] = f'{tag}: hull {ram[creature + 0x0F]} differs and no owner derivation'
    scaled = node_of(ram, E1.SCALED_OWNER)
    edits.append(flip(ram, scaled + 0xD0 + 0x33, 0x04))
    want['0x219870 node'] = f'{tag}: hull {ram[scaled + 0x0F]} differs and no owner derivation'
    edits.append(flip(ram, T.D_008106C8 + 1))
    want['spawn entry word'] = f'{tag}: D_008106C8 '
    gs = bytearray(cap.gs.read_bytes())
    zb = next(iter(next(iter(z for k, z in _ZONES.items() if k == bank)).values()))[0]
    at = len(gs) - 0x400000 - 84 + 256 * zb.tex_table[0]['tbp0']
    gs[at:at + 256] = bytes(x ^ 0xFF for x in gs[at:at + 256])
    gpath = tree / f'{tag}_gs.bin'
    gpath.write_bytes(bytes(gs))
    want['zone texels'] = f'texture 0 texels differ from the {tag} GS freeze'
    return ram_copy(cap, tag, edits, gs=gpath), want


def plant_loader_canary(t, tree, plant):
    """Replace each loaded file by one its loader must refuse; returns the
    check_loaders names."""
    sub, side = sub_dir(t, tree), side_dir(t, tree)
    rows = E2.SPAWN_SUB0[t.name]
    spawn = T01.emsp_windows((side / 'spawn_table.emsp').read_bytes())
    end0 = rows[0] + 0x30 * rows[1]
    plant(side / 'spawn_table.emsp', T01.emsp_file([(a, d[:end0 - 1 - a] if a <= rows[0] < a + len(d) else d)
                                                   for a, d in spawn]))
    doors = T01.emsp_windows((side / 'door_destinations.emsp').read_bytes())
    plant(side / 'door_destinations.emsp', T01.emsp_file([(a, d) for a, d in doors if a == T01.D_0024E140]))
    names = ['spawn', 'doors']
    for path in (side / 'overlay_data.emsc', side / 'scripts.emsc', sub / 'level/static_bank.emsc'):
        blob = path.read_bytes()
        b, entry = struct.unpack_from('<2I', blob, 8)
        plant(path, C.emsc(b, blob[20:52], entry))
        names.append(path.name)
    for rel, name in (('roster.emro', 'roster'), ('world_models.emwm', 'world models'),
                      (f'{t.name}.emcl', 'emcl'), ('sfx/sfx_registry.emsr', 'sfx registry'),
                      ('message_data.emmd', 'messages')):
        plant(sub / rel, (sub / rel).read_bytes()[:16])
        names.append(name)
    cells = (sub / f'{t.name}_cells.bin').read_bytes()
    plant(sub / f'{t.name}_cells.bin', cells[:-1])
    names.append('cells (bit 29 cleared)' if any(C.u32(cells, 4 + 4 * u) & 0x20000000
                                                for u in range(C.u32(cells, 0))) else 'cells')
    for z in sorted((sub / 'level').glob('*.emdl')):
        plant(z, z.read_bytes()[:16])
        names.append(z.name)
    if (sub / 'background.embg').exists():
        plant(sub / 'background.embg', (sub / 'background.embg').read_bytes()[:-1])
        names.append('background')
    return names


def canary(K, lib, bg, cap):
    """run_checks over [capture, planted copy] and a tree with file plants;
    each section must report its plant for the copy and stay silent for the
    original. Returns the number of sections."""
    global A
    real, tree = A, CANARY
    t = K.t

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

    def plant(path, data):
        path = Path(path)
        path.unlink(missing_ok=True)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    rsub, rside = sub_dir(t, real), side_dir(t, real)
    csub, cside = sub_dir(t, tree), side_dir(t, tree)
    fake, want = canary_plants(K, cap, tree, 'canary0')
    # the quiet check (the original capture must not report the section):
    # not for the area bank, whose text the message-file plant below also
    # produces for the original
    originals = {k: cap.name for k in want if k != 'area message bank'}
    zones = sorted(x.name for x in (rsub / 'level').glob('*.emdl'))
    plant(csub / 'level/00_a_extra.emdl', (rsub / f'level/{zones[0]}').read_bytes())
    want['zone count'] = f'{t.name}: {len(zones) + 1} zone files for {len(zones)} source files'
    rec = json.loads((rsub / 'cells.json').read_text())
    row = next(r for r in rec['cells']['captures'] if r['capture'] == key(cap))
    row['moved_hulls'] = row['moved_hulls'][:-1]
    rec['cells']['flag_calls'][key(cap)] = []
    plant(csub / 'cells.json', json.dumps(rec).encode())
    want['moved set'] = f'{cap.name}: re-transformed hulls'
    want['flag call record'] = f'{cap.name}: flag calls'
    plant(csub / f'{t.name}_cells.bin', mutate((rsub / f'{t.name}_cells.bin').read_bytes(), -1))
    want['cells file'] = f'{t.name}_cells.bin differs from the disc bytes'
    plant(csub / f'sfx/{t.name}_banks.bin', mutate((rsub / f'sfx/{t.name}_banks.bin').read_bytes(), 0x20000))
    want['sound bank'] = f'{t.name}_banks.bin differs from the disc container'
    plant(csub / 'roster.emro', mutate((rsub / 'roster.emro').read_bytes(), 0x09))
    want['roster file'] = 'roster.emro differs from the rebuild from the disc'
    od = (rside / 'overlay_data.emsc').read_bytes()
    plant(cside / 'overlay_data.emsc', mutate(od, len(od) - 1))
    want['overlay data file'] = 'overlay data differs from the disc overlay'
    mm = (rsub / 'message_data.emmd').read_bytes()
    plant(csub / 'message_data.emmd', mutate(mm, len(mm) - 1))
    want['message file'] = f'{cap.name}: message banks differ'
    tab = json.loads((rside / 'tables.json').read_text())
    tab['overlay_data']['run_time_words'][key(cap)] = ['0x823600']
    tab['scripts']['chains'] = dict(list(tab['scripts']['chains'].items())[1:])
    tab['doors']['used'] = tab['doors']['used'][1:]
    plant(cside / 'tables.json', json.dumps(tab).encode())
    want['overlay run-time record'] = f'{cap.name}: overlay data run-time words'
    want['chain record'] = 'tables.json chains'
    want['doors used'] = 'tables.json doors.used'
    n = len(FAILS)
    try:
        A = tree
        with contextlib.redirect_stdout(io.StringIO()):
            got, _v = run_checks(K, [cap, fake], tree)
            got += tables_problems(K, [cap, fake])
        zbuf = (rsub / f'level/{zones[-1]}').read_bytes()
        plant(csub / f'level/{zones[-1]}', mutate(zbuf, T01.emdl_parts(zbuf)['verts'] + 1))
        want['zone EMDL'] = f'{t.name}/{zones[-1]} differs from a rebuild'
        plant(csub / 'level/dynamic_objects.emsc', C.emsc(STALE_5A4, bytes(16)))
        want['dynamic list file'] = f'{t.name}: a dynamic-list file'
        if (rsub / 'background.embg').exists():
            plant(csub / 'background.embg', mutate((rsub / 'background.embg').read_bytes(), 200))
            want['background file'] = 'background.embg differs from the capture\'s export'
        else:
            plant(csub / 'background.embg', b'EMBG')
            want['background file'] = 'background.embg exists but no capture arms the background'
        (csub / 'level/00_a_extra.emdl').unlink()
        got2 = level_problems(K, [cap], csub)[0]
        loader_want = plant_loader_canary(t, tree, plant)
        with contextlib.redirect_stdout(io.StringIO()), T01.quiet_fds():
            loaded = check_loaders(lib, bg, t, tree)
    finally:
        A = real
        remove_tree()
    del FAILS[n:]
    for section, text in want.items():
        check(any(text in x for x in got + got2), f'canary {t.name}: section {section} did not report "{text}"')
        if section in originals:
            quiet = text.replace('canary0', originals[section])
            if quiet != text:
                check(not any(quiet in x for x in got), f'canary {t.name}: section {section} reported the original')
    for name in loader_want:
        check(not loaded.get(name, True), f'canary {t.name}: loader {name} accepted a broken file')
    return len(want) + len(loader_want)


# ---------------------------------------------------------------------------
# Controls


def controls(K, caps):
    """One changed input per comparator, and the accept cases. Returns the count."""
    use(K)
    t = K.t
    n = 0

    def expect(problems, what, accept=False):
        nonlocal n
        n += 1
        check(bool(problems) != accept, f'control {t.name}: {what} {"rejected" if accept else "missed"}')

    def edited(cap, name, edits):
        return ram_copy(cap, name, edits)
    # the first capture of the run (the arrival / a13_05): it has no orphaned
    # hull, so a single-capture directory check of it is clean (the baseline
    # control below) and every rejection is the changed input's
    first = caps[0]
    by = {c.name: c for c in caps}
    sub = sub_dir(t)
    cells = (sub / f'{t.name}_cells.bin').read_bytes()
    record = json.loads((sub / 'cells.json').read_text())
    _c, hulls, _s = L.cell_directory(cells, 0)
    table = C.u32(first.spad, L.SPAD_CELLS)
    ram = first.ram
    expect(E1.verify_directory(K.elf, cells, [first])[1], f'the directory of {first.name} alone', accept=True)
    # load map: the extracted-file names read as ids (the labels the
    # extractor gives) and a row relabelled by the next id
    named = [(a, p, o, s, f'{lab.split("/")[0]}/{p.stem.split("_")[1]}') for a, p, o, s, lab in K.lmap]
    expect(label_problems(named, first, K.info), 'the map labelled by the extracted file names')
    shifted = list(K.lmap)
    a, p, o, s, lab = shifted[0]
    shifted[0] = (a, p, o, s, lab[:-2] + f'{int(lab[-2:], 16) ^ 1:02x}')
    expect(label_problems(shifted, first, K.info), 'the first row labelled by another id')
    expect(label_problems(K.lmap[1:], first, K.info), 'the first row dropped')
    expect(T01.load_map_problems(K.lmap + [K.lmap[-1]]), 'a duplicated row')
    mid = len(K.lmap) // 2
    expect(label_problems(K.lmap[:mid] + K.lmap[mid + 1:], first, K.info), 'a middle row dropped (a gap)')
    # a row labelled by an id its block's list does not name, lying exactly
    # where D_0028A490[that id] points (AREA13: 0x45, whose stale word lies in
    # the map; AREA19: a nested id on a top row)
    if t.name == 'area13':
        k = next(i for i, r in enumerate(K.lmap) if r[0] <= STALE_5A4 < r[0] + r[3])
        a_, p_, o_, s_, lab_ = K.lmap[k]
        cut = STALE_5A4 - a_
        stop = min(C.u32(ram, A13.D_0028A490 + 4 * i) for i, _o in A13.relocation_lists(first)['top']
                   if C.u32(ram, A13.D_0028A490 + 4 * i) > STALE_5A4)
        rows = K.lmap[:k] + [(a_, p_, o_, cut, lab_), (STALE_5A4, p_, o_ + cut, min(s_ - cut, stop - STALE_5A4),
                                                         lab_.split('/')[0] + '/id45')]
        rows += [(STALE_5A4 + rows[-1][3], p_, o_ + cut + rows[-1][3], s_ - cut - rows[-1][3], lab_)] \
            if s_ - cut > rows[-1][3] else []
        rows += K.lmap[k + 1:]
    else:
        nid = A13.relocation_lists(first)['nested'][0][0]
        a_, p_, o_, s_, lab_ = K.lmap[0]
        rows = [(a_, p_, o_, s_, lab_.split('/')[0] + f'/id{nid:02x}')] + K.lmap[1:]
    expect(label_problems(rows, first, K.info), 'a row labelled by an id its block does not list')
    # the static bank's rows labelled by the id before it (an earlier start,
    # rows beyond its stop)
    bank_id = next(int(lab[-2:], 16) for a, _p, _o, _s, lab in K.lmap if a == C.u32(ram, E1.D_0028A5A0))
    lst = A13.relocation_lists(first)
    order = [i for i, _o in (lst['nested'] if t.name == 'area19' else lst['top'])]
    if order.index(bank_id) > 0:
        before = order[order.index(bank_id) - 1]
    else:
        before = lst['top'][-1][0]
    rows = [r[:4] + (r[4][:-2] + f'{before:02x}',) if r[4].endswith(f'id{bank_id:02x}') else r for r in K.lmap]
    expect(label_problems(rows, first, K.info), 'the static bank rows labelled by an earlier id')
    # the capture before the load with another D_0028A5A4 word
    saved_prev = E1.PREVIOUS[t.name]
    fake_prev = BUILD / f'prev-{os.getpid()}'
    try:
        fake_prev.mkdir(parents=True, exist_ok=True)
        (fake_prev / 'eeMemory.bin').write_bytes(bytes(E1.D_0028A5A4) + struct.pack('<I', STALE_5A4 + 0x10))
        E1.PREVIOUS[t.name] = fake_prev
        expect(stale_problems(K, [first]), 'the previous capture with another D_0028A5A4')
    finally:
        E1.PREVIOUS[t.name] = saved_prev
        (fake_prev / 'eeMemory.bin').unlink(missing_ok=True)
        if fake_prev.exists():
            fake_prev.rmdir()
    K2 = SimpleNamespace(**vars(K))
    K2.lmap = K.lmap[:-1]
    expect([x for x in load_problems(K2, [first], len(cells)) if 'a different load map' in x],
           'the checker\'s map one row short of the capture\'s')
    bank_img = (sub / 'level/static_bank.emsc').read_bytes()
    bbase, _ent, blen = struct.unpack_from('<3I', bank_img, 8)
    longer = C.emsc(bbase, bank_img[20:] + ram[bbase + blen:bbase + blen + 0x10])
    expect(bank_problems(longer, [first]), 'the static bank file 0x10 bytes past the objects\' end')
    expect(stale_problems(K, [edited(first, 'c', [(E1.D_0028A5A4, struct.pack('<I', STALE_5A4 + 4))])]),
           'D_0028A5A4 changed')
    ident, off = A13.relocation_lists(first)['top'][0]
    expect(A13.relocation_problems(edited(first, 'c', [flip(ram, A13.D_0028A490 + 4 * ident)]))[0],
           'a relocation word changed')
    # cells: hull bytes, uid words, owner matrices, the flag owner's state
    moved = sorted(record['cells']['moved_uids'])
    for uid in (moved[0], moved[-1]):
        s_, e_, _f = hulls[uid]
        c = edited(first, 'c', [flip(ram, table + e_ - 1)])
        expect(E1.verify_directory(K.elf, cells, [c])[1], f'moved hull {uid} last byte')
    still = [u for u in hulls if u not in record['cells']['moved_uids']]
    s_, e_, _f = hulls[still[0]]
    expect(E1.verify_directory(K.elf, cells, [edited(first, 'c', [flip(ram, table + s_ + 0x20)])])[1],
           f'an unmoved hull {still[0]}')
    expect(E1.verify_directory(K.elf, cells, [edited(first, 'c', [flip(ram, table + 4 + 3, 0x40)])])[1],
           'uid 0 bit 30')
    flag = node_of(ram, E1.FLAG_OWNERS[t.name])
    for state in (0, 3):
        expect(E1.verify_directory(K.elf, cells, [edited(first, 'c', [(flag + 4, bytes([state]))])])[1],
               f'the flag owner in state {state}')
    if t.name == 'area13':
        expect(E1.verify_directory(K.elf, cells, [edited(first, 'c', [(0x8107F4, bytes([ram[0x8107F4] | 0x40]))])])[1],
               'D_008107F4 bit 0x40 set')
    else:
        expect(E1.verify_directory(K.elf, cells, [edited(first, 'c', [(flag + 4, bytes([2]))])])[1],
               '[9] in state 2 ((0x21, 0) last)')
    scaled = node_of(ram, E1.SCALED_OWNER)
    row2 = struct.unpack_from('<3f', ram, scaled + 0xD0 + 0x20)
    lane = max(range(3), key=lambda k: abs(row2[k]))           # its non-zero lane (the node's rotation)
    for at, bit, what in ((0xB0 + 1, 0x10, 'position x'), (0xD0 + 0x20 + 4 * lane + 2, 0x40,
                                                            f'matrix third row lane {lane}')):
        expect(E1.verify_directory(K.elf, cells, [edited(first, 'c', [flip(ram, scaled + at, bit)])])[1],
               f'the 0x219870 node {what}')
    # a port that re-transforms the 0x219870 hull with node + 0xD0 (the
    # pickup's rule) does not reproduce it
    suid = ram[scaled + 0x0F]
    saved = L.owner_matrix
    try:
        L.owner_matrix = lambda r, nd: nd + 0xD0 if C.u32(r, nd + 0x10) == E1.SCALED_OWNER else saved(r, nd)
        _exp, proofs = L.derive_cell_image(K.elf, cells, first, hulls)
    finally:
        E1.install(t)
    expect([] if proofs.get(suid, ('',))[0] == 'derived' else ['not derived'],
           'the 0x219870 node read as a node + 0xD0 owner')
    creature = node_of(ram, E1.CREATURES[t.name])
    cm = C.u32(ram, creature + 0x11C) + 0x90
    expect(E1.verify_directory(K.elf, cells, [edited(first, 'c', [flip(ram, cm + 0x31, 0x10)])])[1],
           'the creature matrix (*(node + 0x11C) + 0x90)')
    # a port that re-transforms the creature hull with node + 0xD0 does not
    # reproduce it
    cuid = ram[creature + 0x0F]
    try:
        L.owner_matrix = lambda r, nd: nd + 0xD0 if C.u32(r, nd + 0x10) == E1.CREATURES[t.name] else saved(r, nd)
        _exp, proofs = L.derive_cell_image(K.elf, cells, first, hulls)
    finally:
        E1.install(t)
    expect([] if proofs.get(cuid, ('',))[0] == 'derived' else ['not derived'],
           'the creature read as a node + 0xD0 owner')
    # owner_matrix unit cases
    expect([] if E1.owner_matrix(ram, creature) == cm else ['x'], 'owner_matrix of the creature', accept=True)
    expect([] if E1.owner_matrix(ram, scaled) is None else ['x'], 'owner_matrix of 0x219870 (None)', accept=True)
    # orphans (AREA13: uid 77 in a13_03 / a13_04)
    if t.name == 'area13' and 'a13_04_hatch' in by:
        lone = [by['a13_04_hatch']]
        expect(E1.verify_directory(K.elf, cells, lone)[1], 'the orphan 77 without a deriving capture')
        both = [by['a04b_04_lift'], by['a13_04_hatch']]
        expect(E1.verify_directory(K.elf, cells, both)[1], 'the orphan 77 with the arrival', accept=True)
        s_, e_, _f = hulls[77]
        orph = edited(by['a13_04_hatch'], 'c', [flip(by['a13_04_hatch'].ram, table + s_ + 0x24, 0x10)])
        expect(E1.verify_directory(K.elf, cells, [by['a04b_04_lift'], orph])[1], 'the orphan 77 bytes changed')
    # census
    ov2 = bytearray(K.ov)
    site = sorted(SITES[t.name][0x19C6F0][E1.FLAG_OWNERS[t.name]])[0]
    ov2[0x100:0x104] = ov2[site - 0x823500:site - 0x823500 + 4]
    K2 = SimpleNamespace(**vars(K))
    K2.ov = bytes(ov2)
    expect(census_problems(K2, caps[:1]), 'an extra 0019C6F0 call site in the module')
    saved_sites = SITES[t.name]
    try:
        moved_sites = {c: dict(o) for c, o in saved_sites.items()}
        drum = moved_sites[0x1A2370].pop(E1.DRUM)
        moved_sites[0x1A2370][E1.PICKUP] = moved_sites[0x1A2370][E1.PICKUP] + drum
        SITES[t.name] = moved_sites
        expect(census_problems(K, caps[:1]), 'the drum\'s 001A2370 site credited to the pickup')
    finally:
        SITES[t.name] = saved_sites
    if SUB1[t.name]:
        o = SUB1[t.name][0]
        live = next(a for _s, a in T.pool_nodes(ram) if C.u32(ram, a + 0x10) == E1.DRUM)
        expect(census_problems(K, [edited(first, 'c', [(live + 0x10, struct.pack('<I', o))])]),
               'a live node of a sub-1 owner')
    # writer windows
    lo, hi, beh = E2.WRITERS[t.name][0]
    olo, ohi = E2.E02.overlay_data_window(K.ov)
    odisc = K.ov[olo - C.OVERLAY_ARENA:ohi - C.OVERLAY_ARENA]
    records = E2.chain_records(odisc, olo)
    if t.name == 'area13':
        hcap = by.get('a13_04_hatch', first)
        for side_, vals in E2.HATCH_SETS.items():
            c = edited(hcap, 'c', [(lo, struct.pack('<6f', *vals))])
            hatch = [a for a in E2.pool_nodes(c.ram, beh)
                     if (struct.unpack_from('<f', c.ram, a + 0xB8)[0] > 1000.0) == side_]
            opened = struct.unpack_from('<h', c.ram, hatch[0] + 0x28)[0] >= 4 if hatch else False
            expect(E2.run_time_words(odisc, olo, c.ram, records)[1], f'the hatch set z>1000={side_}',
                   accept=opened)
        c = edited(hcap, 'c', [(lo, struct.pack('<6f', 719.8, 160.5, 1252.3, 0.0, 0.031415924, 1.0))])
        expect(E2.run_time_words(odisc, olo, c.ram, records)[1], 'a hatch set with a changed last word')
    else:
        c = edited(first, 'c', [(0x810775, bytes([ram[0x810775] ^ 1]))])
        expect(E2.run_time_words(odisc, olo, c.ram, records)[1], 'D_00810775 bit 0 flipped (y -120)')
        flame = E2.pool_nodes(ram, beh)[0]
        c = edited(first, 'c', [(flame + 4, bytes([2]))])
        expect(E2.run_time_words(odisc, olo, c.ram, records)[1], 'the flame not in state 1')
    gap = next(a for a in range(olo, ohi, 4) if not any(r <= a < r + 0x40 for r in records)
               and not any(lo_ <= a < hi_ for lo_, hi_, _b in E2.WRITERS[t.name]))
    c = edited(first, 'c', [flip(ram, gap)])
    expect(E2.run_time_words(odisc, olo, c.ram, records)[1], 'a gap word of the overlay data')
    # spawn entry, doors, models
    dread = T01.disc_reader()
    _tb, ents, cnt = E2.spawn_rows(dread)
    word = lambda e: C.u32(dread(ents + 0x30 * e + 0x1C, 4), 0)
    other = next(e for e in range(cnt) if word(e) != word(ram[0x810702]))
    c = edited(first, 'c', [(0x810702, bytes([other]))])
    expect(spawn_entry_problems([c]), 'the entry byte changed')
    rec = json.loads((side_dir(t) / 'tables.json').read_text())
    roster = (sub / 'roster.emro').read_bytes()
    row = bytes(T01.disc_reader()(E2.DOOR_ROW[t.name], 4 * rec['doors']['records']))
    used = rec['doors']['used']
    expect(doors_used_problems(roster, row, used, caps), 'doors.used as exported', accept=True)
    bad = copy.deepcopy(used)
    bad[0]['area_change'] = not bad[0]['area_change']
    expect(doors_used_problems(roster, row, bad, caps), 'a door area-change bit flipped')
    expect(doors_used_problems(roster, row, used,
                               [edited(first, 'c', [flip(ram, E2.DOOR_ROW[t.name] + 4 * used[0]['door_id'])])]),
           'the RAM door record changed')
    wm = (sub / 'world_models.emwm').read_bytes()
    wtab = C.u32(wm, 8)
    offs = [wtab + (C.s32(ram, wtab + 4 + 4 * i) >> 2 << 2) for i in range(C.u32(ram, wtab))]
    owner = next(a for _s, a in E2.model_owner_nodes(ram) if C.u32(ram, a + 0x44) in offs
                 and (ram[a + 0x0D] ^ 1) < len(offs) and offs[ram[a + 0x0D] ^ 1] != C.u32(ram, a + 0x44))
    expect(model_binding_problems(wm, [edited(first, 'c', [flip(ram, owner + 0x0D)])]), 'a model owner +0x0D')
    expect(model_binding_problems(wm, [edited(first, 'c', [(owner + 0x44, bytes(4))])]),
           'a model owner unbound (+0x44 = 0)')
    if t.name == 'area13' and 'a13_04_hatch' in by:
        h = next(a for _s, a in E2.explicit_model_nodes(by['a13_04_hatch'].ram))
        c = edited(by['a13_04_hatch'], 'c', [(h + 0x0D, bytes([0xD]))])
        expect(E2.explicit_model_problems(c.ram, 'c'), 'the opened hatch with +0x0D = 0xD', accept=True)
        c = edited(by['a13_04_hatch'], 'c', [flip(by['a13_04_hatch'].ram, h + 0x44, 0x10)])
        expect(E2.explicit_model_problems(c.ram, 'c'), 'the opened hatch +0x44 changed')
    # the flag owner outside its modelled states, with the uid words set to
    # what the other branch would give (the disc word): refused, not accepted
    dcount = C.u32(cells, 0)
    words = [(table + 4 + 4 * u, cells[4 + 4 * u:8 + 4 * u]) for u in range(dcount)]
    c = edited(first, 'c', [(flag + 4, bytes([3]))] + words)
    expect(E1.verify_directory(K.elf, cells, [c])[1], 'the flag owner in state 3 with the disc uid words')
    # the word after a writer window (outside every chain record)
    lo_, hi_, _beh = E2.WRITERS[t.name][0]
    olo_, ohi_ = E2.E02.overlay_data_window(K.ov)
    odisc_ = K.ov[olo_ - C.OVERLAY_ARENA:ohi_ - C.OVERLAY_ARENA]
    recs_ = E2.chain_records(odisc_, olo_)
    after = next(a for a in range(hi_, ohi_, 4) if not any(r <= a < r + 0x40 for r in recs_)
                 and not any(w0 <= a < w1 for w0, w1, _w in E2.WRITERS[t.name]))
    c = edited(first, 'c', [flip(ram, after)])
    expect(E2.run_time_words(odisc_, olo_, c.ram, recs_)[1], f'the word {after:#x} past the writer window')
    # the AREA19 builder: D_0028A740 below the top region's end, and a
    # descriptor list out of order, are refused (not mapped)
    if t.name == 'area19':
        top_end = C.u32(ram, C.D_0028A73C) + K.info['top_length']
        c = edited(first, 'c', [(C.D_0028A740, struct.pack('<I', top_end - 0x10))])
        try:
            A13.build_load_map_both(c)
            got = []
        except SystemExit as error:
            got = [str(error)]
        expect(got, 'D_0028A740 inside the top resident region')
    desc = bytearray(K.sector[:0x100])
    first_, count_ = C.u16(desc, 0x0C), C.u16(desc, 0x0E)
    at_ = 0x20 + 8 * (first_ + count_)
    struct.pack_into('<I', desc, at_, C.u32(desc, at_) | 0x800)     # the first id at offset 0x800, not 0
    try:
        A13._resident_rows(bytes(desc), K.info['chunk'], C.u32(ram, C.D_0028A73C), first)
        got = []
    except SystemExit as error:
        got = [str(error)]
    expect(got, 'a relocation list that does not start at offset 0')
    # the [47] +0x0D rule (AREA13): the record's own +4 is rejected
    if t.name == 'area13':
        node47 = node_of(ram, 0x8293A0)
        disc_roster = T01.roster_image(T01.disc_reader())
        expect(field_problems(disc_roster, edited(first, 'c', [(node47 + 0x0D, bytes([0x14]))]).ram),
               '[47] +0x0D back at its record\'s +4')
        expect(field_problems(disc_roster, edited(first, 'c', [(0x8107F4, bytes([ram[0x8107F4] | 0x40]))]).ram),
               '[47] with D_008107F4 bit 0x40 set (rule not modelled)')
        # the background: another TEX0 in the render ctx is not the asset
        ctx = C.u32(ram, L.CTX_PTR) & 0x1FFFFFF
        expect(background_problems(K, [edited(first, 'c', [flip(ram, ctx + 0x1D0 + 1)])], sub),
               'the ctx background TEX0 changed')
        expect(background_problems(K, [edited(first, 'c', [(ctx + 0x174, struct.pack('<I', 0))])], sub),
               'a capture that does not arm the background')
    else:
        saved_cb = CALLBACKS['area19']
        try:
            CALLBACKS['area19'] = {0x827B20: (0x82E090, 0x825C70)}
            expect(census_problems(K, caps[:1]), 'the callback chain credited to a sub-0 starter')
            CALLBACKS['area19'] = {0x827B20: (0x82D290, 0x8279E0)}
            expect(census_problems(K, caps[:1]), 'the callback credited to a chain that does not name it')
            CALLBACKS['area19'] = {0x827B20: (0x82E090, 0x826570)}
            expect(census_problems(K, caps[:1]), 'the chain credited to another sub-1 behaviour')
            CALLBACKS['area19'] = {0x827B20: (0x82E090, 0x8279E0), 0x826B30: (0x82E090, 0x8279E0)}
            expect(census_problems(K, caps[:1]), 'a callback its chain does not name, the starter right')
        finally:
            CALLBACKS['area19'] = saved_cb
    survivor_controls(K, caps, expect, edited)
    # messages: an area record in RAM
    mm = (sub / 'message_data.emmd').read_bytes()
    aptr = C.u32(K.elf, T01.MSG_TABLES + 4 * (C.AREA + 1) - C.ELF_VADDR + C.ELF_OFFSET)
    expect(T01.messages_problems(mm, [edited(first, 'c', [flip(ram, aptr + 3)])], K.elf)[0], 'an area message record')
    # sfx: the bindings with a slot moved to another row
    saved_b = BINDING[t.name]
    try:
        BINDING[t.name] = {**saved_b, (2, 0): ('area', 1)}
        expect(sfx_problems(K, caps[:1], (sub / f'sfx/{t.name}_banks.bin').read_bytes(),
                            (sub / 'sfx/sfx_registry.emsr').read_bytes()), 'a pinned binding changed')
    finally:
        BINDING[t.name] = saved_b
    saved_r = REFUSED[t.name]
    try:
        REFUSED[t.name] = saved_r | {(4, 3)}
        expect(sfx_problems(K, caps[:1], (sub / f'sfx/{t.name}_banks.bin').read_bytes(),
                            (sub / 'sfx/sfx_registry.emsr').read_bytes()), 'the pinned refused set changed')
    finally:
        REFUSED[t.name] = saved_r
    return n


def survivor_controls(K, caps, expect, edited):
    """Pinned cases for the review's named survivors (docs/AREA13_ASSETS.md,
    "Review survivors") and the hatch point writer. Each filters the report
    for the one check it pins, so a disabled check cannot hide behind
    another. expect counts them."""
    t = K.t
    first = caps[0]
    by = {c.name: c for c in caps}
    ram = first.ram
    sub, side = sub_dir(t), side_dir(t)
    only = lambda problems, text: [x for x in problems if text in x]
    # R01: the last row relabelled to a chunk the descriptor does not name
    a, p, o, s_, lab = K.lmap[-1]
    foreign = K.lmap[:-1] + [(a, p, o, s_, 'chunk99/' + lab.split('/')[1])]
    expect(only(label_problems(foreign, first, K.info), "rows outside the descriptor's blocks"),
           'the last row labelled by a chunk outside the descriptor')
    # R02: a relocation list that names id 0x45
    saved = A13.relocation_lists
    try:
        A13.relocation_lists = lambda cap: {k: (v + [(0x45, 0)] if k == 'top' else v)
                                            for k, v in saved(cap).items()}
        expect(only(stale_problems(K, [first]), 'relocates id 0x45'), 'a top list that relocates id 0x45')
    finally:
        A13.relocation_lists = saved
    # R03: the static bank's last byte changed in RAM (past the bank's middle)
    bank_img = (sub / 'level/static_bank.emsc').read_bytes()
    bbase, _e, blen = struct.unpack_from('<3I', bank_img, 8)
    expect(only(bank_problems(bank_img, [edited(first, 'c', [flip(ram, bbase + blen - 1)])]),
                'static bank differs from RAM'), 'the static bank\'s last byte changed in RAM')
    # R04: the zone file named after another id
    zfiles = sorted(str(x) for x in (sub / 'level').glob('*.emdl'))
    zones = _ZONES.get(bank_img)
    if zones is not None:
        pairs = zone_pairs(zfiles, zones, K.image)
        expect(zone_name_problems(t.name, pairs), 'the zone files as exported', accept=True)
        path0, z0 = pairs[0]
        stem, ident = Path(path0).name.split('_', 1)
        other = f'id{int(ident[2:4], 16) ^ 7:02x}.emdl'
        expect(zone_name_problems(t.name, [(str(Path(path0).with_name(f'{stem}_{other}')), z0)] + pairs[1:]),
               'a zone file named after another id')
    # R05: no background.embg although every capture arms the background
    if (sub / 'background.embg').exists():
        expect(only(background_problems(K, caps[:1], BUILD / f'nobg-{os.getpid()}'), 'no background.embg'),
               'the background file missing')
    # R06 / R07: the cells file longer than its directory; cells.json's moved
    # uids without one uid (caps [] : only the file-level checks run)
    cells = (sub / f'{t.name}_cells.bin').read_bytes()
    record = json.loads((sub / 'cells.json').read_text())
    table = C.u32(K.first.spad, L.SPAD_CELLS)
    longer = K.image.read(table, len(cells) + 0x10)
    expect(only(cells_problems(K, longer, [], record), f'is {len(longer)} bytes, the directory'),
           'the cells file 0x10 disc bytes past its directory')
    rec2 = copy.deepcopy(record)
    rec2['cells']['moved_uids'] = rec2['cells']['moved_uids'][1:]
    expect(only(cells_problems(K, cells, [], rec2), 'is not the union of its rows'),
           'cells.json moved_uids without its first uid')
    # R10: a sub-0 placement naming a sub-1 owner (AREA19)
    roster = (sub / 'roster.emro').read_bytes()
    places, gl = roster_records(roster)
    if SUB1[t.name]:
        k = next(i for i, r in enumerate(places) if (C.s16(r, 0) & 0xFF) != 0x0B)
        bad = list(places)
        bad[k] = bad[k][:0x24] + struct.pack('<I', SUB1[t.name][0]) + bad[k][0x28:]
        expect(only(census_problems(K, caps[:1], bad, gl), 'the sub-0 roster names'),
               'a sub-0 placement naming a sub-1 owner')
    # R11: one live placement node freed (its slot byte 0)
    _ok, _live, pairs_ = T01.roster_nodes(T, roster, ram)
    expect(only(roster_problems(K, roster, [edited(first, 'c', [(pairs_[0][1], b'\0')])]),
                'placement nodes live'), 'a live placement node freed')
    # R13: an EMSC whose header base word is shifted (the data unchanged)
    rec = json.loads((side / 'tables.json').read_text())
    olo, ohi = E2.E02.overlay_data_window(K.ov)
    odisc = K.ov[olo - C.OVERLAY_ARENA:ohi - C.OVERLAY_ARENA]
    records = E2.chain_records(odisc, olo)
    od = bytearray((side / 'overlay_data.emsc').read_bytes())
    struct.pack_into('<I', od, 8, C.u32(od, 8) + 0x10)
    expect(only(window_problems('overlay data', bytes(od), (olo, ohi - olo), odisc, [first], records,
                                rec['overlay_data']['run_time_words']), 'overlay data: window'),
           'overlay_data.emsc with its base word + 0x10')
    # R15: a bound model owner's bone count changed
    wm = (sub / 'world_models.emwm').read_bytes()
    wtab = C.u32(wm, 8)
    offs = {wtab + (C.s32(ram, wtab + 4 + 4 * i) >> 2 << 2) for i in range(C.u32(ram, wtab))}
    owner = next(a for _s, a in E2.model_owner_nodes(ram) if C.u32(ram, a + 0x44) in offs)
    expect(only(model_binding_problems(wm, [edited(first, 'c', [(owner + 0x0C, bytes([ram[owner + 0x0C] ^ 1]))])]),
                'bone count / slots'), 'a bound owner\'s bone count changed')
    expect(only(model_binding_problems(wm, [edited(first, 'c', [(owner + 0x110, bytes(4))])]),
                'bone count / slots'), 'a bound owner\'s first bone slot zeroed')
    # R16: DOOR_BEHAVIOURS without the lift 001BD560 (tables.json would follow it)
    saved_d = E2.DOOR_BEHAVIOURS
    try:
        E2.DOOR_BEHAVIOURS = tuple(b for b in saved_d if b != 0x1BD560)
        expect(door_behaviour_problems(t), 'DOOR_BEHAVIOURS without 001BD560')
    finally:
        E2.DOOR_BEHAVIOURS = saved_d
    expect(door_behaviour_problems(t), 'DOOR_BEHAVIOURS as pinned', accept=True)
    # R17: SCRIPT_ENTRIES and tables.json both without the last chain
    saved_s = E2.SCRIPT_ENTRIES[t.name]
    try:
        drop = max(saved_s)
        E2.SCRIPT_ENTRIES[t.name] = tuple(e for e in saved_s if e != drop)
        rec3 = copy.deepcopy(rec)
        rec3['scripts']['chains'] = {k: v for k, v in rec3['scripts']['chains'].items() if int(k, 16) != drop}
        expect(only(script_entry_problems(t, rec3), 'the C starts chains'),
               f'SCRIPT_ENTRIES and tables.json without the chain {drop:#x}')
    finally:
        E2.SCRIPT_ENTRIES[t.name] = saved_s
    # R29 and the hatch points (AREA13 a13_04: the opened hatch)
    if t.name == 'area13' and 'a13_04_hatch' in by:
        hc = by['a13_04_hatch']
        h = next(a for _s, a in E2.explicit_model_nodes(hc.ram))
        expect(E2.explicit_model_problems(edited(hc, 'c', [(h + 0x0C, bytes([hc.ram[h + 0x0C] ^ 1]))]).ram, 'c'),
               'the opened hatch\'s bone count changed')
        expect(E2.explicit_model_problems(edited(hc, 'c', [(h + 0x110, bytes(4))]).ram, 'c'),
               'the opened hatch\'s first bone slot zeroed')
        plo, phi, beh = next(w for w in E2.WRITERS[t.name] if w[0] == 0x82CAB0)
        pdisc = K.ov[plo - C.OVERLAY_ARENA:phi - C.OVERLAY_ARENA]
        expect(E2.writer_problems(hc.ram, plo, phi, pdisc, beh), 'the hatch points as captured', accept=True)
        other = bytearray(pdisc)
        for at, xyz in E2.HATCH_POINTS[False]:
            other[at - plo:at - plo + 12] = struct.pack('<3f', *xyz)
        expect(E2.writer_problems(edited(hc, 'c', [(plo, bytes(other))]).ram, plo, phi, pdisc, beh),
               'the z <= 1000 points with only the z > 1000 hatch opened')
        expect(E2.writer_problems(edited(hc, 'c', [(h + 4, b'\x01'), (h + 5, b'\x00')]).ram, plo, phi, pdisc, beh),
               'the z > 1000 points with that hatch back in state 1 sub-state 0')
        expect(E2.writer_problems(edited(hc, 'c', [flip(hc.ram, 0x82CB08, 0x01)]).ram, plo, phi, pdisc, beh),
               'the third point\'s z changed')
        expect(E2.writer_problems(edited(hc, 'c', [flip(hc.ram, 0x82CAD0)]).ram, plo, phi, pdisc, beh),
               'a byte between the points changed')


# ---------------------------------------------------------------------------


def target_setup(name, el):
    A13.configure(name)
    t = A13.current()
    every = A13.all_captures(t)
    excluded = sorted(n for n, _a in A13.excluded_captures(t))
    check(excluded == EXCLUDED[name], f'{name}: excluded captures {excluded}')
    caps = every if FULL else [c for c in every if c.name in QUICK[name]]
    K = SimpleNamespace(t=t, el=el, elf=C.read_elf(), ov=A13.read_overlay(t), first=every[0], every=every)
    sector, _note = A13.descriptor(K.first)
    nested = C.u32(sector, 0x18)
    K.sector = sector
    check(C.sha(sector[:0x100 + 0x70 * nested] if nested else sector[:0x100]) == DESCRIPTOR_SHA256[name],
          f'{name}: INDEX.IDX sector {t.area + 4} is not the pinned descriptor')
    use(K)
    K.lmap, K.info = C.build_load_map(K.first)
    K.image = C.LoadedImage(K.lmap)
    return K, caps


def main():
    need = [A / 'sub0/level/level.json', A / 'tables.json', A / 'sub0/cells.json', A / 'sub0/sfx/banks.json',
            A / 'area19/sub0/level/level.json', A / 'area19/tables.json', C.ELF_PATH, C.ISO_PATH,
            A13.AREA13.overlay_path, A13.AREA19.overlay_path, A13.ARRIVAL / 'eeMemory.bin',
            A13.ROUTE_A13 / 'a13_05_shaft/eeMemory.bin']
    missing = [str(p) for p in need if not p.exists()]
    if missing:
        print('area13 assets reference: SKIPPED, missing local inputs:', missing)
        return 0
    t0 = time.time()
    el = L.load_export_level()
    lib = T01.build_loaders()
    bg = background_lib()
    Ks = {}
    for name in ('area13', 'area19'):
        K, caps = target_setup(name, el)
        Ks[name] = (K, caps)
        print(f'area13 assets reference ({MODE}) {K.t.label} sub 0: {len(caps)} of {len(K.every)} captures')
        loaded = check_loaders(lib, bg, K.t, A)
        for lname, ok in loaded.items():
            check(ok, f'{name}: port loader rejects {lname}')
        files = [k for k in loaded if k not in ('cells refused (bit 29)', 'cells (bit 29 cleared)')]
        note = ''
        if 'cells refused (bit 29)' in loaded:
            note = (f'; the cells file refused (bit 29): {loaded["cells refused (bit 29)"]}, '
                    f'its bit-29-cleared copy loads: {loaded["cells (bit 29 cleared)"]}')
        print(f'  loaders: {sum(loaded[k] for k in files)}/{len(files)} files accepted by the port loaders{note}')
        problems, V = run_checks(K, caps)
        for p in problems:
            check(False, f'{name}: {p}')
        lv = V.get('level', {})
        print(f"  load map + level: {lv.get('objects')} bank objects -> {lv.get('zones')} zone EMDL(s), "
              f"{lv.get('kicks')} level kicks; collision, cells, tables, sfx, ctx: {len(problems)} problems")
    if FAILS:
        print(f'  canary and controls: skipped, {len(FAILS)} check(s) already failed')
    else:
        for name, (K, caps) in Ks.items():
            use(K)
            print(f'  canary {name}: {canary(K, lib, bg, caps[0])} sections each reported their planted difference')
            if not FAILS:
                print(f'  controls {name}: {controls(K, caps)} changed inputs, each caught (or accepted where marked)')
    print(f'area13 assets reference: {"FAIL" if FAILS else "PASS"} ({len(FAILS)} failures, '
          f'{time.time() - t0:.1f} s)')
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
