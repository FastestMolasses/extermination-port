#!/usr/bin/env python3
"""Export the AREA22 tables: the roster (placements + deferred groups), the
spawn table (area 0x16 rows), the door destinations, the overlay data
section, the message records + global bank and the world model bank
(docs/AREA22_ASSETS.md).

The walks are the AREA11 / AREA01 / AREA00 / AREA02 / AREA04 exporters',
imported unchanged (export_area11_roster.walk_roster / encode_roster,
export_spawn_table.build_spawn_table, export_area01_tables.export_doors /
match_nodes / spawn-field helpers, export_message_data's ELF reader, bank
walk and constants, and from export_area02_tables: export_world_models with
its model-owner filter and overlay_data_window), with export_area01_common
pointed at AREA22 by export_area22_common.configure. What is AREA22's own:

  * one sub-state: placement table 0x823A20 (21 records) and the deferred
    group 0x8236B0 (19), the one group 001B6910's list names. The group
    0x823600 (3 records, area_overview's "nest group" at D_0024D820[0x16]
    [nest base]) is not on that list, so it is not in the EMRO; its bytes
    are in overlay_data.emsc (SIXTH_LEVEL_ROUTE.md section 2.1);
  * no script chains: the committed AREA22 overlay C has no func_001BA1A0
    call, so no scripts window is exported (the checker re-reads the C);
    every overlay-data word must equal the disc module in every capture;
  * the spawn rows: D_0024D650[0x16] names one room pointer; its entries
    run from 0x24D4E0 to the next room array or table the ELF names
    (6 records of 0x30 bytes);
  * no area message table: D_00264DD0[0x17] is 0 in the ELF and in every
    capture, and the descriptor has no id-0x41 file, so D_0028A594 keeps
    the previous area's bank address (0x152E740, inside the AREA22 grid
    block). message_data.emmd is written with the area record count and the
    area bank size 0 (export_message_data.export refuses a zero table
    pointer, so this lane assembles the same EMMD v1 layout from the same
    ELF reads and bank walk); the port's em_message_live_install refuses a
    file with no area records (finding in docs/AREA22_ASSETS.md);
  * the doors: the placement doors are 001BC350 ([6]) and 001BB860 ([7],
    [8], [10]); the row D_0024E140[0x16] = 0x24E130 holds 4 records.

Output (ignored assets/area22/, disc-derived, never committed):
  sub0/roster.emro            EMRO v1 (src/game/em_actor_roster.c layout)
  sub0/message_data.emmd      EMMD v1 for area 0x16 (no area records, no area bank)
  sub0/world_models.emwm (+.json)  EMWM v1 for *D_0028A59C
  spawn_table.emsp            EMSP v1 (the global export; the area 0x16 rows checked)
  door_destinations.emsp      EMSP windows: D_0024E140[0..0x17) and the area 0x16 row
  overlay_data.emsc           EMSC window of the overlay data 0x823600..0x823E00
  tables.json                 counts, hashes, verification summary

Usage (port root): python3 tools/export_area22_tables.py
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area22_common as A22  # noqa: E402
import export_area02_tables as E02  # noqa: E402  (sets AREA02 state on import)

C = A22.configure()                    # AREA22 from here on
T = E02.T                              # export_area01_tables
R = E02.R                              # export_area11_roster
SP = E02.SP                            # export_spawn_table
M = T.M                                # export_message_data

PLACEMENTS = 0x823A20                  # D_0024D7C0[0x16][0]
GROUPS = ((0x8236B0, 19),)             # the 001B6910 list D_0024D820[0x16][0]
# the group at D_0024D820[0x16][nest base] (area_overview's 'nest group',
# nest base = s16 D_0024A850[0x16] or 1): not walked by 001B6910, so not in
# the EMRO; its bytes are in overlay_data.emsc
NEST_GROUP = (0x823600, 3)
SCRIPT_ENTRIES = ()                    # the AREA22 overlay C starts no chain
DOOR_BEHAVIOURS = (0x1BC350, 0x1BB860)
SPAWN_ROWS = (0x24D4E0, 6)             # D_0024D650[0x16] -> room 0 entries
DOOR_ROW = 0x24E130                    # D_0024E140[0x16], 4 records to 0x24E140
# the spawn entry of each capture's last room load (D_008106C8 is set from
# its word +0x1C then). The ladder climb of a22_s1 writes the entry byte
# D_00810702 = 3 at its top-out without a room load (SIXTH_LEVEL_ROUTE.md
# section 2.2, the writer not traced), so in a22_s1 and a22_s2 the entry
# byte (3) and the load entry (1, door [7] in a22_00) differ
LOAD_ENTRY = {'a04_05_progression_exit': 0, 'a22_00_door7': 1, 'a22_01_corridor': 1, 'a22_s0_pickup': 1,
              'a22_s1_ladder_reader': 1, 'a22_s2_door10_locked': 1}
MSG_TABLE_INDEX = C.AREA + 1           # D_00264DD0[area + 1]
C_CALL = r'func_001BA1A0\s*\('


def c_script_calls():
    """[(file, count)] of func_001BA1A0 calls in the committed AREA22 overlay C."""
    out = []
    for f in sorted((C.DECOMP / 'src/overlays/AREA22').glob('*.c')):
        n = len(re.findall(C_CALL, f.read_text()))
        if n:
            out.append((f.name, n))
    return out


def run_time_words(disc, base, ram):
    """[address] of the words of the window that differ in `ram` (AREA22 has
    no chain record, so every one is a problem for the caller)."""
    return [base + k for k in range(0, len(disc), 4) if ram[base + k:base + k + 4] != disc[k:k + 4]]


def export_roster(read, caps):
    groups, paddr, placements = R.walk_roster(read, C.AREA, 0)
    if paddr != PLACEMENTS:
        raise SystemExit(f'placement table {paddr:#x}, not {PLACEMENTS:#x}')
    if tuple((a, len(r)) for a, r in groups) != GROUPS:
        raise SystemExit(f'groups {[(hex(a), len(r)) for a, r in groups]}')
    data = R.encode_roster(C.AREA, 0, groups, paddr, placements)
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
        placement_table=hex(paddr), placements=len(placements),
        groups=[dict(address=hex(a), records=len(r)) for a, r in groups],
        captures=per_capture, ambiguous_sources=sorted(ambiguous), sha256=C.sha(data))


def nest_group(read):
    """The group at D_0024D820[0x16][nest base] (nest base = s16
    D_0024A850[0x16], 0 read as 1), walked like a deferred group (0x2C-byte
    records to s16 -1): area_overview's reading, recorded, not in the EMRO."""
    u32 = lambda a: C.u32(read(a, 4), 0)
    base = C.s16(read(0x24A850 + 2 * C.AREA, 2), 0) or 1
    g = u32(u32(T.D_0024D820 + 4 * C.AREA) + 4 * base)
    n = 0
    while C.s16(read(g + 0x2C * n, 2), 0) != -1:
        n += 1
    if (g, n) != NEST_GROUP:
        raise SystemExit(f'nest group {g:#x} x{n}, not {NEST_GROUP}')
    return dict(address=hex(g), records=n, nest_base=base, in_emro=False,
                note='not on 001B6910\'s list; its bytes are in overlay_data.emsc')


def spawn_rows(read):
    """(entries, count): D_0024D650[0x16] -> room pointer array (one
    non-zero word), entries up to the next address any area's table or room
    array starts at (or D_0024D650), 0x30 bytes a record."""
    u32 = lambda a: C.u32(read(a, 4), 0)
    table = u32(T.D_0024D650 + 4 * C.AREA)
    entries = u32(table)
    if u32(table + 4):
        raise SystemExit('area 0x16 names a second room pointer')
    starts = {T.D_0024D650}
    for area in range(0x17):
        t = u32(T.D_0024D650 + 4 * area)
        if not t:
            continue
        starts.add(t)
        k = 0
        while k < 4 and u32(t + 4 * k):
            starts.add(u32(t + 4 * k))
            k += 1
    end = min(s for s in starts if s > entries)
    return table, entries, (end - entries) // 0x30


def export_spawn(elf, read, caps):
    """The global spawn export; its windows must carry the area 0x16 rows,
    and in every capture D_008106C8 = +0x1C of the record of its last room
    load (LOAD_ENTRY), with the entry byte D_00810702 inside the rows."""
    data = SP.build_spawn_table(elf)
    table, entries, count = spawn_rows(read)
    if (entries, count) != SPAWN_ROWS:
        raise SystemExit(f'area 0x16 spawn rows {entries:#x} x{count}, not {SPAWN_ROWS}')
    blob = read(entries, 0x30 * count)
    windows = [struct.unpack_from('<II', data, 16 + 8 * k) for k in range(C.u32(data, 8))]
    body, found = 16 + 8 * len(windows), False
    for address, size in windows:
        if address <= entries and entries + len(blob) <= address + size:
            at = body + entries - address
            found = data[at:at + len(blob)] == blob
        body += size
    if not found:
        raise SystemExit('spawn_table.emsp does not carry the area 0x16 records')
    rows = []
    for cap in caps:
        if cap.ram[entries:entries + len(blob)] != blob or cap.ram[table:table + 8] != read(table, 8):
            raise SystemExit(f'{cap.name}: area 0x16 spawn records differ from the ELF')
        room, load = cap.ram[0x810702], LOAD_ENTRY.get(cap.name)
        if load is None or room >= count or C.u32(cap.ram, T.D_008106C8) != C.u32(blob, 0x30 * load + 0x1C):
            raise SystemExit(f'{cap.name}: D_008106C8 is not +0x1C of the load entry {load}')
        rows.append(dict(capture=cap.name, entry_byte=room, load_entry=load,
                         d8106c8=hex(C.u32(cap.ram, T.D_008106C8)), record_1c=hex(C.u32(blob, 0x30 * load + 0x1C))))
    global_copy = C.ROOT / 'assets/spawn/spawn_table.emsp'
    return data, dict(table=hex(table), entries=hex(entries), records=count, captures=rows,
                      record_1c_words=[hex(C.u32(blob, 0x30 * k + 0x1C)) for k in range(count)],
                      equals_assets_spawn=global_copy.exists() and global_copy.read_bytes() == data,
                      sha256=C.sha(data))


def message_blob(elf_path):
    """(EMMD v1 bytes, report) for area 0x16: export_message_data's layout,
    ELF reads and bank walk, with the area record count and the area bank
    size 0 (D_00264DD0[0x17] is 0)."""
    elf = M.Elf(elf_path)
    pointers = [elf.u32(M.TABLES + 4 * i) for i in range(M.TABLE_WORDS)]
    if pointers[MSG_TABLE_INDEX] != 0:
        raise SystemExit(f'D_00264DD0[{MSG_TABLE_INDEX:#x}] = {pointers[MSG_TABLE_INDEX]:#x}: an area table exists')
    starts = {p for p in pointers if p}
    starts |= {elf.u32(M.STREAM_NAMES + 4 * i) for i in range(2)}
    starts |= {elf.u32(M.STREAM_LISTS + 4 * i) for i in range(0x17)}
    gptr = pointers[0]
    gcount = (min(s for s in starts if s > gptr) - gptr) // 8
    grecs = elf.read(gptr, 8 * gcount)
    rows = []
    for i in range(512):
        row = elf.read(M.STREAMS + 16 * i, 16)
        if struct.unpack_from('<i', row)[0] == -1:
            break
        rows.append(row)
    else:
        raise SystemExit('D_0026EC60 has no -1 row')
    if elf.u32(M.LINE_CFG + 0x14) != M.STYLE or elf.u32(M.TEMPLATE + 0x14) != 0:
        raise SystemExit('unexpected style pointers in D_00264CD0 / D_00264BF0')
    path, base = M.BANKS['global']
    gbank = M.bank_extent((C.DECOMP / path).read_bytes(), base)
    header = struct.pack('<4s8I', b'EMMD', 1, C.AREA, gcount, 0, len(rows), M.CURSOR, len(gbank), 0)
    body = (elf.read(M.COLORS, 64) + elf.read(M.LINE_CFG, 20) + elf.read(M.TEMPLATE, 20) + grecs +
            b''.join(rows) + gbank)
    blob = header + body
    return blob, dict(global_table=hex(gptr), global_records=gcount, area_table_index=hex(MSG_TABLE_INDEX),
                      area_table=0, area_records=0, stream_rows=len(rows), global_bank=dict(source=path,
                      bytes=len(gbank)), area_bank_bytes=0, sha256=hashlib.sha256(blob).hexdigest())


def export_messages(caps, out):
    blob, rep = message_blob(C.ELF_PATH)
    gcount, srows, gbank = rep['global_records'], rep['stream_rows'], rep['global_bank']['bytes']
    at = 140
    grecs = blob[at:at + 8 * gcount]
    streams = blob[at + 8 * gcount:at + 8 * gcount + 16 * srows]
    gb = blob[at + 8 * gcount + 16 * srows:]
    gptr = int(rep['global_table'], 16)
    stale = set()
    for cap in caps:
        if C.u32(cap.ram, T.D_00264DD0 + 4 * MSG_TABLE_INDEX) != 0:
            raise SystemExit(f'{cap.name}: D_00264DD0[{MSG_TABLE_INDEX:#x}] is not 0')
        if cap.ram[gptr:gptr + len(grecs)] != grecs:
            raise SystemExit(f'{cap.name}: global message records differ from RAM')
        if cap.ram[M.STREAMS:M.STREAMS + len(streams)] != streams:
            raise SystemExit(f'{cap.name}: message stream rows differ from RAM')
        g0 = C.u32(cap.ram, T.D_0028A4E8)
        if cap.ram[g0:g0 + gbank] != gb:
            raise SystemExit(f'{cap.name}: global bank differs from RAM at *D_0028A4E8')
        stale.add(C.u32(cap.ram, T.D_0028A594))
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(blob)
    rep['d_0028a594'] = sorted(hex(s) for s in stale)
    rep['captures_equal'] = [c.name for c in caps]
    return rep


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=A22.OUT)
    args = ap.parse_args(argv)
    out = args.out.resolve()
    (out / 'sub0').mkdir(parents=True, exist_ok=True)
    A22.configure()
    elf, ov = C.read_elf(), A22.read_overlay()
    read = C.static_reader(elf, ov)
    caps = A22.all_captures()
    olo, ohi = E02.overlay_data_window(ov)
    for cap in caps:
        if cap.ram[C.OVERLAY_ARENA:olo] != ov[:olo - C.OVERLAY_ARENA]:
            raise SystemExit(f'{cap.name}: overlay text differs from AREA22.BIN')
    calls = c_script_calls()
    if calls:
        raise SystemExit(f'the AREA22 overlay C starts script chains: {calls}')
    report = {}
    blob, (groups, placements), report['roster'] = export_roster(read, caps)
    (out / 'sub0/roster.emro').write_bytes(blob)
    report['roster']['nest_group'] = nest_group(read)
    lmap, _info = C.build_load_map(caps[0])
    image = C.LoadedImage(lmap)
    report['messages'] = export_messages(caps, out / 'sub0/message_data.emmd')
    wm, index = E02.export_world_models(image, caps, placements, groups)
    (out / 'sub0/world_models.emwm').write_bytes(wm)
    (out / 'sub0/world_models.json').write_text(json.dumps(index, indent=1) + '\n')
    report['world_models'] = dict(table=index['table_address'], count=index['count'], span=index['span_bytes'],
                                  source=index['source'], verified=index['verified'])

    spawn, report['spawn'] = export_spawn(elf, read, caps)
    (out / 'spawn_table.emsp').write_bytes(spawn)

    doors, report['doors'] = T.export_doors(elf, ov, caps, placements)
    if int(report['doors']['row'], 16) != DOOR_ROW:
        raise SystemExit(f'door row {report["doors"]["row"]}, not {DOOR_ROW:#x}')
    row = bytes(read(DOOR_ROW, report['doors']['bytes']))
    report['doors']['used'] = [
        dict(placement=i, behaviour=hex(C.u32(rec, 0x24)), door_id=rec[3] & 0x7F, area_change=bool(rec[3] & 0x80),
             record=list(row[4 * (rec[3] & 0x7F):4 * (rec[3] & 0x7F) + 4]))
        for i, rec in enumerate(placements) if C.u32(rec, 0x24) in DOOR_BEHAVIOURS]
    for d in report['doors']['used']:
        if 4 * d['door_id'] + 4 > len(row):
            raise SystemExit(f'placement[{d["placement"]}]: door id {d["door_id"]} outside the row')
    (out / 'door_destinations.emsp').write_bytes(doors)

    odata = ov[olo - C.OVERLAY_ARENA:ohi - C.OVERLAY_ARENA]
    for cap in caps:
        words = run_time_words(odata, olo, cap.ram)
        if words:
            raise SystemExit(f'{cap.name}: overlay data words differ from the module: {[hex(a) for a in words[:4]]}')
    report['scripts'] = dict(chains={}, note='the committed AREA22 overlay C calls func_001BA1A0 nowhere; no '
                                             'scripts window')
    report['overlay_data'] = dict(window=[hex(olo), hex(ohi)], run_time_words={}, sha256=C.sha(odata))
    (out / 'overlay_data.emsc').write_bytes(C.emsc(olo, odata))

    report['captures'] = [c.name for c in caps]
    report['excluded_captures'] = [dict(capture=n, area_bytes=list(a)) for n, a in A22.excluded_captures()]
    (out / 'tables.json').write_text(json.dumps(report, indent=1) + '\n')
    r = report
    print(f"roster: {r['roster']['placements']} placements, "
          f"{[g['address'] + 'x' + str(g['records']) for g in r['roster']['groups']]}; "
          f"spawn {r['spawn']['entries']} x{r['spawn']['records']}; door row {r['doors']['row']} x{r['doors']['records']}; "
          f"no scripts; messages {r['messages']['global_records']} global records, {r['messages']['stream_rows']} "
          f"stream rows, global bank {r['messages']['global_bank']['bytes']} bytes, no area table "
          f"(D_0028A594 {r['messages']['d_0028a594']}); world models {r['world_models']['count']} "
          f"({r['world_models']['span']} bytes at {r['world_models']['table']}); overlay data {r['overlay_data']['window']} "
          f"equal in {len(caps)} captures")
    return 0


if __name__ == '__main__':
    sys.exit(main())
