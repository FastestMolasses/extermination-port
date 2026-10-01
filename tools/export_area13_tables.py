#!/usr/bin/env python3
"""Export the tables of each area the a13 captures load (AREA13 sub 0, and
AREA19 sub 0 at the a13_05 arrival): the roster (placements + deferred
groups), the spawn table (the target's sub-0 rows), the door destinations,
the overlay scripts, the overlay data section, the message records + area
bank and the world model bank (docs/AREA13_ASSETS.md).

The walks are the AREA11 / AREA01 / AREA02 exporters', imported unchanged
(export_area11_roster.walk_roster / encode_roster,
export_spawn_table.build_spawn_table, export_message_data.export through
export_area01_tables.export_messages, export_area01_tables.export_doors /
walk_chain / match_nodes / spawn-field helpers, and from
export_area02_tables: export_world_models with its model-owner filter,
overlay_data_window, area_bank and bank_source), with export_area01_common
pointed at the target by export_area13_common.configure. What is this
lane's own:

  * the script chains are the ones the target's committed overlay C starts
    (every func_001BA1A0 argument in src/overlays/AREA13 resp. AREA19:
    SCRIPT_ENTRIES, re-read from the C by the checker); the window is the
    lowest entry up to the end of the last record any chain reaches;
  * every overlay-data word that differs from the disc module in a capture
    lies in a record a chain reaches, or in a window a named byte-identical
    writer stores to (WRITERS): AREA13's hatch 0x826850 writes 0x82CA00 ..
    0x82CA14 at its count 4 (one of two constant sets, by its z) and three
    points 0x82CAB0 / 0x82CAC0 / 0x82CB00 when it starts script 0x82CA50
    (inside reached records, values checked all the same), AREA19's
    flame [11] 0x824690 writes 0x82B234 = 5 + y (y = -120 with D_00810775
    bit 0, else -110) every frame in its state 1. writer_problems checks
    those values exactly;
  * the doors: every placement whose behaviour reaches 001BC150 (the door
    commit that reads D_0024E140[area] + 4 * (door id & 0x7F)): 001BC350
    and AREA13's / AREA19's 0x823580 through 001BC240, 001BB860, 001BD9F0
    and 001BD560 directly (their C). export_area01_tables.export_doors' own
    `used` list is AREA01's and is recomputed here;
  * the spawn rows of the target's sub 0: D_0024D650[area] names the sub
    pointers; the sub-0 records run to the next address any table, sub
    array or record list starts at (E22-style bound), 0x30 bytes each; in
    every capture D_008106C8 = +0x1C of record D_00810702.

Output (ignored assets/area13/, disc-derived, never committed), under
sub0/ (AREA13) and area19/sub0/ + area19/ (AREA19):
  roster.emro                 EMRO v1 (src/game/em_actor_roster.c layout)
  message_data.emmd           EMMD v1 for the area
  world_models.emwm (+.json)  EMWM v1 for *D_0028A59C
and next to them (assets/area13/ resp. assets/area13/area19/):
  spawn_table.emsp            EMSP v1 (the global export; the target rows checked)
  door_destinations.emsp      EMSP windows: D_0024E140[0..0x17) and the area row
  scripts.emsc                EMSC window of the chains
  overlay_data.emsc           EMSC window of the overlay data section
  tables.json                 counts, hashes, verification summary

Usage (port root): python3 tools/export_area13_tables.py [--target area13|area19]
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area13_common as A13  # noqa: E402  (first: it keeps AREA01's nested load-map builder)
import export_area02_tables as E02  # noqa: E402  (sets AREA02 state on import)

C = A13.configure('area13')            # this lane's state from here on
T = E02.T                              # export_area01_tables
R = E02.R                              # export_area11_roster
SP = E02.SP                            # export_spawn_table

PLACEMENTS = {'area13': 0x82D570, 'area19': 0x82E3D0}          # D_0024D7C0[area][0]
GROUPS = {'area13': ((0x829D00, 29), (0x82A230, 2)), 'area19': ((0x829E00, 43), (0x82A590, 1))}
# every func_001BA1A0 script argument of the committed overlay C (c_script_entries)
SCRIPT_ENTRIES = {
    'area13': (0x82A360, 0x82A620, 0x82A770, 0x82AB70, 0x82ADF0, 0x82B090, 0x82B2D0, 0x82B3D0, 0x82B810,
               0x82BC90, 0x82BD10, 0x82BDD0, 0x82BE90, 0x82C110, 0x82C510, 0x82CA50, 0x82CE10, 0x82CFD0),
    'area19': (0x82AD50, 0x82B6E0, 0x82BA00, 0x82BD90, 0x82C410, 0x82C4D0, 0x82C690, 0x82C6E0, 0x82CA20,
               0x82CC70, 0x82CFF0, 0x82D290, 0x82D590, 0x82D990, 0x82DA10, 0x82DD10, 0x82E090, 0x82F690)}
# placements whose behaviour reaches 001BC150 (module docstring)
DOOR_BEHAVIOURS = (0x1BC350, 0x1BB860, 0x1BD9F0, 0x1BD560, 0x823580)
SPAWN_SUB0 = {'area13': (0x24C910, 11), 'area19': (0x24CF10, 14)}
DOOR_ROW = {'area13': 0x24E0A0, 'area19': 0x24E0F0}            # D_0024E140[area]
# run-time writers of overlay-data words, each modelled in writer_problems:
# (lo, hi, behaviour); the hatch's count-4 set lies outside the chain records,
# its point window inside them (its values are checked all the same)
WRITERS = {'area13': ((0x82CA00, 0x82CA18, 0x826850), (0x82CAB0, 0x82CB0C, 0x826850)),
           'area19': ((0x82B234, 0x82B238, 0x824690),)}
HATCH_SETS = {   # func_overlay_AREA13_00826810.c, count 4: [0..5] by z > 1000
    True: (719.8, 160.5, 1252.3, 0.0, 0.031415924, 0.0),
    False: (1071.5, 160.5, 845.2, 0.0, 1.5865042, 0.0)}
# func_overlay_AREA13_00826810.c, state 1 sub-state 0 (+0x0B bit 2), before it
# starts script 0x82CA50 and steps +5 to 1: three points by z > 1000, stored
# together; the bytes between them are not written
HATCH_POINTS = {
    True: ((0x82CAB0, (730.2, 192.9, 1276.4)), (0x82CAC0, (725.7, 174.6, 1262.6)),
           (0x82CB00, (719.8, 160.5, 1252.3))),
    False: ((0x82CAB0, (1098.5, 188.9, 854.0)), (0x82CAC0, (1081.5, 173.2, 850.9)),
            (0x82CB00, (1071.5, 160.5, 845.2)))}
C_CALL = re.compile(r'func_001BA1A0\s*\(\s*\w+\s*,\s*D_overlay_AREA(\d\d)_([0-9A-F]{8})\s*\)')


def target():
    return A13.current()


def c_script_entries(t=None):
    """{entry address} of every func_001BA1A0(…, D_overlay_AREAxx_…) call in
    the target's committed overlay C (data addresses in the C are runtime
    addresses, docs/AREA13_OVERLAY.md)."""
    t = t or target()
    out = set()
    for f in sorted((C.DECOMP / f'src/overlays/{t.label}').glob('*.c')):
        for m in C_CALL.finditer(f.read_text()):
            out.add(int(m.group(2), 16))
    return out


def chain_records(data, base, entries=None):
    """{record address} reached by the chains (walk_chain, 0x40 bytes each)."""
    out = set()
    for e in entries or SCRIPT_ENTRIES[target().name]:
        out.update(T.walk_chain(data, base, e))
    return out


def scripts_window(ov):
    lo, hi = E02.overlay_data_window(ov)
    records = chain_records(ov[lo - C.OVERLAY_ARENA:hi - C.OVERLAY_ARENA], lo)
    return min(SCRIPT_ENTRIES[target().name]), max(records) + 0x40


def pool_nodes(ram, behaviour):
    return [a for _s, a in E02._POOL_NODES(ram) if C.u32(ram, a + 0x10) == behaviour]


def writer_problems(ram, lo, hi, disc, behaviour):
    """The bytes [lo, hi) of `ram` against the writer's own C (`disc` = the
    module's bytes there): AREA13's hatch writes one of HATCH_SETS once a
    hatch node of that side (z > 1000 or not) has counted +0x28 to 4 or
    more; AREA19's flame writes 5 + y while live in state 1 (y -120 with
    D_00810775 bit 0, else -110), and its node must be live in state 1.
    The hatch's point window 0x82CAB0 .. 0x82CB0C holds the module bytes or
    one HATCH_POINTS set (the bytes between the points the module's), with
    a hatch of that side past state 1 sub-state 0 (state 1 with +5 >= 1, or
    state 2)."""
    got = ram[lo:hi]
    if behaviour == 0x826850 and lo == 0x82CAB0:
        if got == disc:
            return []
        for side, points in HATCH_POINTS.items():
            want = bytearray(disc)
            for at, xyz in points:
                want[at - lo:at - lo + 12] = struct.pack('<3f', *xyz)
            if got == bytes(want):
                ok = [a for a in pool_nodes(ram, behaviour)
                      if (struct.unpack_from('<f', ram, a + 0xB8)[0] > 1000.0) == side
                      and ((ram[a + 4] == 1 and ram[a + 5] >= 1) or ram[a + 4] == 2)]
                return [] if ok else [f'{lo:#x}: the hatch points for z > 1000 = {side} without a hatch of that '
                                      'side past state 1 sub-state 0']
        return [f'{lo:#x}: {got.hex()} is neither the module bytes nor a hatch point set']
    if behaviour == 0x826850:
        if got == disc:
            return []
        for side, values in HATCH_SETS.items():
            if got == struct.pack('<6f', *values):
                ok = [a for a in pool_nodes(ram, behaviour)
                      if (struct.unpack_from('<f', ram, a + 0xB8)[0] > 1000.0) == side
                      and struct.unpack_from('<h', ram, a + 0x28)[0] >= 4]
                return [] if ok else [f'{lo:#x}: the hatch set for z > 1000 = {side} without a hatch of that side '
                                      'past count 4']
        return [f'{lo:#x}: {got.hex()} is neither the module bytes nor a hatch constant set']
    if behaviour == 0x824690:
        nodes = pool_nodes(ram, behaviour)
        if len(nodes) != 1 or ram[nodes[0] + 4] != 1:
            return [] if got == disc and not nodes else [f'{lo:#x}: the flame 0x824690 is not one live node in '
                                                         'state 1']
        want = struct.pack('<f', 5.0 + (-120.0 if ram[0x810775] & 1 else -110.0))
        return [] if got == want else [f'{lo:#x}: {got.hex()} is not 5 + y ({want.hex()})']
    return [f'{lo:#x}: no writer model for behaviour {behaviour:#x}']


def run_time_words(disc, base, ram, records, writers=None):
    """(words, problems): the words of the window [base, base + len) that
    differ in `ram`; a problem for each that lies in no reached chain record
    and in no WRITERS window, and for each writer window whose bytes its
    writer model rejects."""
    writers = WRITERS[target().name] if writers is None else writers
    words, problems = [], []
    for k in range(0, len(disc), 4):
        a = base + k
        if ram[a:a + 4] == disc[k:k + 4]:
            continue
        words.append(a)
        if not any(r <= a < r + 0x40 for r in records) and not any(lo <= a < hi for lo, hi, _b in writers):
            problems.append(f'{a:#x}: rewritten word outside every reached chain record and writer window')
    for lo, hi, behaviour in writers:
        if base <= lo and hi <= base + len(disc):
            problems += writer_problems(ram, lo, hi, disc[lo - base:hi - base], behaviour)
    return words, problems


# Model owners whose +0x44 is not 001C6120(table, +0x0D): the AREA13 hatch
# 0x826850 in state 2 (func_overlay_AREA13_00826810.c: when its script ends it
# sets the model func_001C6120(D_0028A59C, 0xD) through 001CA6E0 and leaves
# +0x0D as it was). {behaviour: (state, model id)}; checked by
# explicit_model_problems instead of the generic rule.
EXPLICIT_MODELS = {0x826850: (2, 0xD)}


def explicit_model_nodes(ram):
    return [(s, a) for s, a in E02._POOL_NODES(ram)
            if C.u32(ram, a + 0x10) in EXPLICIT_MODELS and ram[a + 4] == EXPLICIT_MODELS[C.u32(ram, a + 0x10)][0]]


def model_owner_nodes(ram):
    """E02.model_owner_nodes without the EXPLICIT_MODELS nodes."""
    skip = {a for _s, a in explicit_model_nodes(ram)}
    return [(s, a) for s, a in E02.model_owner_nodes(ram) if a not in skip]


def explicit_model_problems(ram, name):
    """Each EXPLICIT_MODELS node: +0x44 = 001C6120(table, model id), its bone
    count +0x0C the model's (+8) and every bone slot set."""
    out = []
    table = C.u32(ram, 0x28A59C)
    for slot, a in explicit_model_nodes(ram):
        ident = EXPLICIT_MODELS[C.u32(ram, a + 0x10)][1]
        model = table + (C.s32(ram, table + 4 + 4 * ident) >> 2 << 2)
        if C.u32(ram, a + 0x44) != model:
            out.append(f'{name} slot {slot}: +0x44 != 001C6120(table, {ident:#x})')
        elif ram[a + 0x0C] != C.u32(ram, model + 8) or not all(C.u32(ram, a + 0x110 + 4 * k)
                                                               for k in range(ram[a + 0x0C])):
            out.append(f'{name} slot {slot}: bone count / slots')
    return out


class RunImage:
    """A LoadedImage seen up to the end of the contiguous run of mapped rows
    that holds `address` (export_area01_tables.export_world_models reads the
    bank to image.span()'s end; AREA19's map has a gap between the top and
    the nested resident regions, the cursor gap)."""

    def __init__(self, image, address):
        self.image, self.map = image, image.map
        rows = sorted(image.map)
        k = next(i for i, e in enumerate(rows) if e[0] <= address < e[0] + e[3])
        lo = hi = k
        while lo > 0 and rows[lo - 1][0] + rows[lo - 1][3] == rows[lo][0]:
            lo -= 1
        while hi + 1 < len(rows) and rows[hi][0] + rows[hi][3] == rows[hi + 1][0]:
            hi += 1
        self.lo, self.hi = rows[lo][0], rows[hi][0] + rows[hi][3]

    def locate(self, address):
        return self.image.locate(address) if self.lo <= address < self.hi else None

    def read(self, address, size):
        if not (self.lo <= address and address + size <= self.hi):
            raise ValueError(f'{address:#x}+{size:#x} outside the contiguous run {self.lo:#x}..{self.hi:#x}')
        return self.image.read(address, size)

    def span(self):
        return self.lo, self.hi


def export_world_models(image, caps, placements, groups):
    """export_area01_tables.export_world_models with its pool walk restricted
    to model_owner_nodes, over the contiguous run that holds the bank
    (RunImage), and the EXPLICIT_MODELS nodes checked on their own."""
    saved = T.pool_nodes
    T.pool_nodes = model_owner_nodes
    try:
        wm, index = T.export_world_models(RunImage(image, C.u32(caps[0].ram, 0x28A59C)), caps, placements,
                                          groups)
    finally:
        T.pool_nodes = saved
    problems = [p for cap in caps for p in explicit_model_problems(cap.ram, cap.name)]
    if problems:
        raise SystemExit('; '.join(problems))
    index['explicit_models'] = {cap.name: [hex(a) for _s, a in explicit_model_nodes(cap.ram)] for cap in caps}
    return wm, index


def export_roster(read, caps):
    t = target()
    groups, paddr, placements = R.walk_roster(read, C.AREA, t.sub)
    if paddr != PLACEMENTS[t.name]:
        raise SystemExit(f'{t.name}: placement table {paddr:#x}, not {PLACEMENTS[t.name]:#x}')
    if tuple((a, len(r)) for a, r in groups) != GROUPS[t.name]:
        raise SystemExit(f'{t.name}: groups {[(hex(a), len(r)) for a, r in groups]}')
    data = R.encode_roster(C.AREA, t.sub, groups, paddr, placements)
    sources = []
    for i, rec in enumerate(placements):
        if (C.s16(rec, 0) & 0xFF) == 0x0B:
            continue                                  # 001B6990 skips class 0x0B
        f, pos, rot = T.spawn_fields_placement(rec, i)
        sources.append((f'placement[{i}]', f, pos, rot))
    for address, records in groups:
        for i, rec in enumerate(records):
            f, pos, rot = T.spawn_fields_group(rec)
            sources.append((f'group {address:#x}[{i}]', f, pos, rot))
    per_capture, ambiguous = [], set()
    for cap in caps:
        for address, records in groups:
            if cap.ram[address:address + 0x2C * len(records)] != b''.join(records):
                raise SystemExit(f'{cap.name}: group {address:#x} differs from the overlay')
        if cap.ram[paddr:paddr + 0x28 * len(placements)] != b''.join(placements):
            raise SystemExit(f'{cap.name}: placement table differs from the overlay')
        rows = T.match_nodes(cap.ram, sources)
        live = [r for r in rows if r['nodes']]
        for r in live:
            if len(r['nodes']) > 1:
                ambiguous.add(r['source'])
        per_capture.append(dict(
            capture=cap.name, sources_live=len(live),
            placements_live=sum(1 for r in live if r['source'].startswith('placement')),
            moved=[r['source'] for r in live if not all(n['position_equal'] and n['rotation_equal']
                                                       for n in r['nodes'])],
            fields_rewritten={r['source']: r['partial'] for r in rows if r['partial']}))
    return data, (groups, placements), dict(
        sub=t.sub, placement_table=hex(paddr), placements=len(placements),
        groups=[dict(address=hex(a), records=len(r)) for a, r in groups],
        captures=per_capture, ambiguous_sources=sorted(ambiguous), sha256=C.sha(data))


def spawn_rows(read):
    """(table, entries, count) of the target's sub-0 spawn records:
    D_0024D650[area] -> sub pointer array; the records run to the next
    address any area's table, sub array or record list starts at (or
    D_0024D650), 0x30 bytes each."""
    u32 = lambda a: C.u32(read(a, 4), 0)
    table = u32(T.D_0024D650 + 4 * C.AREA)
    entries = u32(table + 4 * target().sub)
    starts = {T.D_0024D650}
    for area in range(0x17):
        tb = u32(T.D_0024D650 + 4 * area)
        if not tb:
            continue
        starts.add(tb)
        k = 0
        while k < 4 and u32(tb + 4 * k):
            starts.add(u32(tb + 4 * k))
            k += 1
    end = min(s for s in starts if s > entries)
    return table, entries, (end - entries) // 0x30


def export_spawn(elf, read, caps):
    """The global spawn export; its windows must carry the target's sub-0
    rows, and in every capture D_008106C8 = +0x1C of record D_00810702."""
    t = target()
    data = SP.build_spawn_table(elf)
    table, entries, count = spawn_rows(read)
    if (entries, count) != SPAWN_SUB0[t.name]:
        raise SystemExit(f'{t.name} sub-0 spawn rows {entries:#x} x{count}, not {SPAWN_SUB0[t.name]}')
    blob = read(entries, 0x30 * count)
    windows = [struct.unpack_from('<II', data, 16 + 8 * k) for k in range(C.u32(data, 8))]
    body, found = 16 + 8 * len(windows), False
    for address, size in windows:
        if address <= entries and entries + len(blob) <= address + size:
            at = body + entries - address
            found = data[at:at + len(blob)] == blob
        body += size
    if not found:
        raise SystemExit(f'spawn_table.emsp does not carry the {t.name} sub-0 records')
    rows = []
    for cap in caps:
        if cap.ram[entries:entries + len(blob)] != blob or cap.ram[table:table + 8] != read(table, 8):
            raise SystemExit(f'{cap.name}: spawn records differ from the ELF')
        room = cap.ram[0x810702]
        if room >= count or C.u32(cap.ram, T.D_008106C8) != C.u32(blob, 0x30 * room + 0x1C):
            raise SystemExit(f'{cap.name}: D_008106C8 is not +0x1C of spawn record {room}')
        rows.append(dict(capture=cap.name, entry=room, d8106c8=hex(C.u32(cap.ram, T.D_008106C8))))
    global_copy = C.ROOT / 'assets/spawn/spawn_table.emsp'
    return data, dict(table=hex(table), entries=hex(entries), records=count,
                      record_words_1c=[hex(C.u32(blob, 0x30 * k + 0x1C)) for k in range(count)],
                      captures=rows, equals_assets_spawn=global_copy.exists() and global_copy.read_bytes() == data,
                      sha256=C.sha(data))


def window_export(ov, caps, lo, hi, records):
    data = ov[lo - C.OVERLAY_ARENA:hi - C.OVERLAY_ARENA]
    changed = {}
    for cap in caps:
        words, problems = run_time_words(data, lo, cap.ram, records)
        if problems:
            raise SystemExit(f'{cap.name}: window {lo:#x}: {problems[:4]}')
        if words:
            changed[cap.name] = [hex(a) for a in words]
    return data, changed


def doors_used(placements, row):
    """The placements whose behaviour reaches 001BC150 and their records."""
    out = []
    for i, rec in enumerate(placements):
        if C.u32(rec, 0x24) not in DOOR_BEHAVIOURS:
            continue
        door = rec[3] & 0x7F
        if 4 * door + 4 > len(row):
            raise SystemExit(f'placement[{i}]: door id {door} outside the row')
        out.append(dict(placement=i, behaviour=hex(C.u32(rec, 0x24)), door_id=door,
                        area_change=bool(rec[3] & 0x80), record=list(row[4 * door:4 * door + 4])))
    return out


def export_target(name, out_root):
    A13.configure(name)
    t = target()
    out = out_root / Path(*t.out.relative_to(A13.OUT).parts)      # .../sub0
    side = out.parent                                              # area13/ or area13/area19/
    out.mkdir(parents=True, exist_ok=True)
    elf, ov = C.read_elf(), A13.read_overlay(t)
    read = C.static_reader(elf, ov)
    caps = C.captures()
    olo, ohi = E02.overlay_data_window(ov)
    for cap in caps:
        if cap.ram[C.OVERLAY_ARENA:olo] != ov[:olo - C.OVERLAY_ARENA]:
            raise SystemExit(f'{cap.name}: overlay text differs from {t.overlay_path.name}')
    entries = c_script_entries(t)
    if entries != set(SCRIPT_ENTRIES[t.name]):
        raise SystemExit(f'{t.name}: the C starts chains {sorted(hex(e) for e in entries)}, not SCRIPT_ENTRIES')
    report = dict(target=name)
    blob, (groups, placements), report['roster'] = export_roster(read, caps)
    (out / 'roster.emro').write_bytes(blob)
    lmap, _info = C.build_load_map(caps[0])
    image = C.LoadedImage(lmap)
    bank, _label = E02.area_bank(image, caps[0])
    for cap in caps[1:]:
        if E02.area_bank(image, cap)[0] != bank:
            raise SystemExit(f'{cap.name}: a different area message bank')
    rel = A13.relocation_problems(caps[0])[1]
    if rel.get(0x41) != bank:
        raise SystemExit(f'*D_0028A594 = {bank:#x} is not the relocated id 0x41')
    source, labels = E02.bank_source(image, bank, C.SCRATCH)
    saved = T.AREA_BANK
    T.AREA_BANK = (str(source), 0)
    try:
        report['messages'] = T.export_messages(caps, out / 'message_data.emmd')
    finally:
        T.AREA_BANK = saved
    report['messages']['area_bank'].update(source='the resident image from the bank address', address=hex(bank),
                                           resident_files=labels)
    wm, index = export_world_models(image, caps, placements, groups)
    if rel.get(0x43) != int(index['table_address'], 16):
        raise SystemExit(f'*D_0028A59C = {index["table_address"]} is not the relocated id 0x43')
    (out / 'world_models.emwm').write_bytes(wm)
    (out / 'world_models.json').write_text(json.dumps(index, indent=1) + '\n')
    report['world_models'] = dict(table=index['table_address'], count=index['count'], span=index['span_bytes'],
                                  source=index['source'], verified=index['verified'])

    spawn, report['spawn'] = export_spawn(elf, read, caps)
    (side / 'spawn_table.emsp').write_bytes(spawn)

    doors, report['doors'] = T.export_doors(elf, ov, caps, placements)
    if int(report['doors']['row'], 16) != DOOR_ROW[name]:
        raise SystemExit(f'door row {report["doors"]["row"]}, not {DOOR_ROW[name]:#x}')
    row = bytes(read(DOOR_ROW[name], report['doors']['bytes']))
    report['doors']['used'] = doors_used(placements, row)
    (side / 'door_destinations.emsp').write_bytes(doors)

    records = chain_records(ov[olo - C.OVERLAY_ARENA:ohi - C.OVERLAY_ARENA], olo)
    slo, shi = scripts_window(ov)
    sdata, schanged = window_export(ov, caps, slo, shi, records)
    chains = {f'{e:08X}': len(T.walk_chain(sdata, slo, e)) for e in SCRIPT_ENTRIES[name]}
    report['scripts'] = dict(window=[hex(slo), hex(shi)], chains=chains, records=sum(chains.values()),
                             distinct_records=len(records), run_time_words=schanged, sha256=C.sha(sdata))
    (side / 'scripts.emsc').write_bytes(C.emsc(slo, sdata))

    odata, ochanged = window_export(ov, caps, olo, ohi, records)
    report['overlay_data'] = dict(window=[hex(olo), hex(ohi)], run_time_words=ochanged,
                                  writers=[[hex(lo), hex(hi), hex(b)] for lo, hi, b in WRITERS[name]],
                                  sha256=C.sha(odata))
    (side / 'overlay_data.emsc').write_bytes(C.emsc(olo, odata))

    report['captures'] = [c.name for c in caps]
    report['excluded_captures'] = [dict(capture=n, area_bytes=list(a)) for n, a in A13.excluded_captures(t)]
    (side / 'tables.json').write_text(json.dumps(report, indent=1) + '\n')
    r = report
    print(f"{t.label}: roster {r['roster']['placements']} placements, "
          f"{[g['address'] + 'x' + str(g['records']) for g in r['roster']['groups']]}; spawn {r['spawn']['entries']} "
          f"x{r['spawn']['records']}; door row {r['doors']['row']} x{r['doors']['records']}; doors used "
          f"{[(d['placement'], d['door_id'], d['record']) for d in r['doors']['used']]}; scripts {len(chains)} chains "
          f"/ {r['scripts']['records']} records in {r['scripts']['window']}; messages "
          f"{r['messages'].get('area_records')} area / {r['messages'].get('global_records')} global records; world "
          f"models {r['world_models']['count']} ({r['world_models']['span']} bytes at {r['world_models']['table']}); "
          f"run-time words {ochanged}; {len(caps)} captures")
    return report


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=A13.OUT)
    ap.add_argument('--target', choices=sorted(A13.TARGETS), action='append')
    args = ap.parse_args(argv)
    out = args.out.resolve()
    for name in args.target or ('area13', 'area19'):
        export_target(name, out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
