#!/usr/bin/env python3
"""Export the AREA06 tables: the roster of sub 0 (placements + deferred
group), the spawn table (area 6 rows), the door destinations, the overlay
scripts, the overlay data section, the message records + area bank and the
world model bank (docs/AREA06_ASSETS.md).

The walks are the AREA11 / AREA01 / AREA00 / AREA02 / AREA04 exporters',
imported unchanged (export_area11_roster.walk_roster / encode_roster,
export_spawn_table.build_spawn_table, export_message_data.export through
export_area01_tables.export_messages, export_area01_tables.export_doors /
walk_chain / match_nodes / spawn-field helpers, and from
export_area02_tables: export_world_models with its model-owner filter,
overlay_data_window, area_bank and bank_source), with export_area01_common
pointed at AREA06 by export_area06_common.configure. What is AREA06's own:

  * only sub 0 is captured: placement table 0x827AC0 (57 records) and one
    deferred group, 0x826000 (33) (D_0024D7C0[6][0] and 001B6910's list).
    The per-sub files (roster, message file, model bank) are written under
    sub0/;
  * the script chains are the four the overlay's committed C starts (every
    func_001BA1A0 argument in src/overlays/AREA06/, SCRIPT_ENTRIES): 0x826D40
    and 0x827040 (the keypad 0x824340), 0x827180 (the beam 0x824560) and
    0x8276C0 (the sub-1 owner 0x825E20). The window is the lowest entry up
    to the end of the last record any chain reaches;
  * every overlay-data word that differs from the disc module in a capture
    must lie in a record one of the chains reaches (run_time_words). The
    beam's chain 0x827180 has rewritten words in a06_04 and a06_05 (after
    it ran);
  * the area message bank *D_0028A594 = the top block's id 0x41 (relocated
    by the top list); it is exported from the resident image;
  * the doors: the placement doors are 001BC350 and 001BB860 (the AREA01
    door behaviour 0x823580 that export_area01_tables.export_doors also
    lists is AREA06's [9], not a door; `used` is recomputed here).

Output (ignored assets/area06/, disc-derived, never committed):
  sub0/roster.emro            EMRO v1 (src/game/em_actor_roster.c layout)
  sub0/message_data.emmd      EMMD v1 for area 6
  sub0/world_models.emwm (+.json)  EMWM v1 for *D_0028A59C
  spawn_table.emsp            EMSP v1 (the global export; the area 6 sub-0 rows checked)
  door_destinations.emsp      EMSP windows: D_0024E140[0..0x17) and the area 6 row
  area06_scripts/scripts.emsc EMSC window 0x826D40..0x827AC0 (4 chains)
  overlay_data.emsc           EMSC window of the overlay data 0x826000..0x828F00
  tables.json                 counts, hashes, verification summary

Usage (port root): python3 tools/export_area06_tables.py
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area06_common as A6  # noqa: E402  (first: it keeps AREA01's nested load-map builder)
import export_area02_tables as E02  # noqa: E402  (sets AREA02 state on import)

C = A6.configure(0)                    # AREA06 from here on
T = E02.T                              # export_area01_tables
R = E02.R                              # export_area11_roster
SP = E02.SP                            # export_spawn_table

PLACEMENTS = {0: 0x827AC0}             # D_0024D7C0[6][sub] (SEVENTH_LEVEL_ROUTE.md section 3.1)
GROUPS = {0: ((0x826000, 33),)}
# every func_001BA1A0 script argument of the committed AREA06 overlay C
SCRIPT_ENTRIES = (0x826D40, 0x827040, 0x827180, 0x8276C0)
DOOR_BEHAVIOURS = (0x1BC350, 0x1BB860)
SPAWN_SUB0 = (0x24C0D0, 4)             # area 6 sub-0 spawn records (to the sub-1 pointer 0x24C190)
DOOR_ROW = 0x24E028                    # D_0024E140[6], 6 records to 0x24E040


def chain_records(data, base):
    """{record address} reached by the chains (walk_chain, 0x40 bytes each)."""
    out = set()
    for e in SCRIPT_ENTRIES:
        out.update(T.walk_chain(data, base, e))
    return out


def scripts_window(ov):
    """The lowest chain entry up to the end of the last record reached."""
    lo, hi = E02.overlay_data_window(ov)
    records = chain_records(ov[lo - C.OVERLAY_ARENA:hi - C.OVERLAY_ARENA], lo)
    return min(SCRIPT_ENTRIES), max(records) + 0x40


def run_time_words(disc, base, ram, records):
    """(words, problems): the words of the window [base, base + len) that
    differ in `ram`, and a problem for each that lies in no reached chain
    record."""
    words, problems = [], []
    for k in range(0, len(disc), 4):
        a = base + k
        if ram[a:a + 4] == disc[k:k + 4]:
            continue
        words.append(a)
        if not any(r <= a < r + 0x40 for r in records):
            problems.append(f'{a:#x}: rewritten word outside every reached chain record')
    return words, problems


def export_roster(read, caps, sub):
    groups, paddr, placements = R.walk_roster(read, C.AREA, sub)
    if paddr != PLACEMENTS[sub]:
        raise SystemExit(f'sub {sub}: placement table {paddr:#x}, not {PLACEMENTS[sub]:#x}')
    if tuple((a, len(r)) for a, r in groups) != GROUPS[sub]:
        raise SystemExit(f'sub {sub}: groups {[(hex(a), len(r)) for a, r in groups]}')
    data = R.encode_roster(C.AREA, sub, groups, paddr, placements)
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
        sub=sub, placement_table=hex(paddr), placements=len(placements),
        groups=[dict(address=hex(a), records=len(r)) for a, r in groups],
        captures=per_capture, ambiguous_sources=sorted(ambiguous), sha256=C.sha(data))


def export_spawn(elf, read, caps):
    """The global spawn export; its window must carry the area 6 sub-0 rows
    (the table D_0024D650[6]: sub 0 at 0x24C0D0, sub 1 at 0x24C190, so 4
    sub-0 records), and in every capture D_008106C8 = +0x1C of the current
    record (sub 0, entry D_00810702)."""
    data = SP.build_spawn_table(elf)
    table = C.u32(read(T.D_0024D650 + 4 * C.AREA, 4), 0)
    entries = C.u32(read(table, 4), 0)
    nxt = C.u32(read(table + 4, 4), 0)
    count = (nxt - entries) // 0x30
    if (entries, count) != SPAWN_SUB0:
        raise SystemExit(f'area 6 sub-0 spawn rows {entries:#x} x{count}, not {SPAWN_SUB0}')
    blob = read(entries, 0x30 * count)
    windows = [struct.unpack_from('<II', data, 16 + 8 * k) for k in range(C.u32(data, 8))]
    body, found = 16 + 8 * len(windows), False
    for address, size in windows:
        if address <= entries and entries + len(blob) <= address + size:
            at = body + entries - address
            found = data[at:at + len(blob)] == blob
        body += size
    if not found:
        raise SystemExit('spawn_table.emsp does not carry the area 6 sub 0 records')
    rows = []
    for cap in caps:
        if cap.ram[entries:entries + len(blob)] != blob or cap.ram[table:table + 8] != read(table, 8):
            raise SystemExit(f'{cap.name}: area 6 spawn records differ from the ELF')
        room = cap.ram[0x810702]
        if room >= count or C.u32(cap.ram, T.D_008106C8) != C.u32(blob, 0x30 * room + 0x1C):
            raise SystemExit(f'{cap.name}: D_008106C8 is not +0x1C of spawn record {room}')
        rows.append(dict(capture=cap.name, entry=room, d8106c8=hex(C.u32(cap.ram, T.D_008106C8)),
                         record_1c=hex(C.u32(blob, 0x30 * room + 0x1C))))
    global_copy = C.ROOT / 'assets/spawn/spawn_table.emsp'
    return data, dict(table=hex(table), sub0=dict(entries=hex(entries), records=count), sub1_entries=hex(nxt),
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
    """The placement doors (DOOR_BEHAVIOURS) and their records in the row."""
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


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=A6.OUT)
    args = ap.parse_args(argv)
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    A6.configure(0)
    elf, ov = C.read_elf(), A6.read_overlay()
    read = C.static_reader(elf, ov)
    pairs = A6.all_captures()
    caps = [c for c, _s in pairs]
    olo, ohi = E02.overlay_data_window(ov)
    for cap in caps:
        if cap.ram[C.OVERLAY_ARENA:olo] != ov[:olo - C.OVERLAY_ARENA]:
            raise SystemExit(f'{cap.name}: overlay text differs from AREA06.BIN')
    report = dict(roster={}, messages={}, world_models={})
    placements_by_sub = {}
    for sub in A6.SUBS:
        A6.configure(sub)
        sc = C.captures()
        blob, (groups, placements), report['roster'][sub] = export_roster(read, sc, sub)
        placements_by_sub[sub] = placements
        (out / f'sub{sub}').mkdir(exist_ok=True)
        (out / f'sub{sub}/roster.emro').write_bytes(blob)
        lmap, _info = C.build_load_map(sc[0])
        image = C.LoadedImage(lmap)
        bank, _label = E02.area_bank(image, sc[0])
        for cap in sc[1:]:
            if E02.area_bank(image, cap)[0] != bank:
                raise SystemExit(f'{cap.name}: a different area message bank')
        rel = A6.relocation_problems(sc[0])[1]
        if rel.get(0x41) != bank:
            raise SystemExit(f'*D_0028A594 = {bank:#x} is not the relocated id 0x41')
        source, labels = E02.bank_source(image, bank, C.SCRATCH)
        saved = T.AREA_BANK
        T.AREA_BANK = (str(source), 0)
        try:
            report['messages'][sub] = T.export_messages(sc, out / f'sub{sub}/message_data.emmd')
        finally:
            T.AREA_BANK = saved
        report['messages'][sub]['area_bank'].update(source='the resident image from the bank address',
                                                    address=hex(bank), resident_files=labels)
        wm, index = E02.export_world_models(image, sc, placements, groups)
        if rel.get(0x43) != int(index['table_address'], 16):
            raise SystemExit(f'*D_0028A59C = {index["table_address"]} is not the relocated id 0x43')
        (out / f'sub{sub}/world_models.emwm').write_bytes(wm)
        (out / f'sub{sub}/world_models.json').write_text(json.dumps(index, indent=1) + '\n')
        report['world_models'][sub] = dict(table=index['table_address'], count=index['count'],
                                           span=index['span_bytes'], source=index['source'],
                                           verified=index['verified'])
    A6.configure(0)

    spawn, report['spawn'] = export_spawn(elf, read, caps)
    (out / 'spawn_table.emsp').write_bytes(spawn)

    doors, report['doors'] = T.export_doors(elf, ov, caps, placements_by_sub[0])
    if int(report['doors']['row'], 16) != DOOR_ROW:
        raise SystemExit(f'door row {report["doors"]["row"]}, not {DOOR_ROW:#x}')
    row = bytes(read(DOOR_ROW, report['doors']['bytes']))
    report['doors']['used'] = {sub: doors_used(placements_by_sub[sub], row) for sub in A6.SUBS}
    (out / 'door_destinations.emsp').write_bytes(doors)

    records = chain_records(ov[olo - C.OVERLAY_ARENA:ohi - C.OVERLAY_ARENA], olo)
    slo, shi = scripts_window(ov)
    sdata, schanged = window_export(ov, caps, slo, shi, records)
    chains = {f'{e:08X}': len(T.walk_chain(sdata, slo, e)) for e in SCRIPT_ENTRIES}
    report['scripts'] = dict(window=[hex(slo), hex(shi)], chains=chains, records=sum(chains.values()),
                             distinct_records=len(records), run_time_words=schanged, sha256=C.sha(sdata))
    (out / 'area06_scripts').mkdir(exist_ok=True)
    (out / 'area06_scripts/scripts.emsc').write_bytes(C.emsc(slo, sdata))

    odata, ochanged = window_export(ov, caps, olo, ohi, records)
    report['overlay_data'] = dict(window=[hex(olo), hex(ohi)], run_time_words=ochanged, sha256=C.sha(odata))
    (out / 'overlay_data.emsc').write_bytes(C.emsc(olo, odata))

    report['captures'] = [f'{c.name} (sub {s})' for c, s in pairs]
    (out / 'tables.json').write_text(json.dumps(report, indent=1) + '\n')
    r = report
    print(f"roster: {[(s, x['placements'], [g['address'] + 'x' + str(g['records']) for g in x['groups']]) for s, x in r['roster'].items()]}; "
          f"spawn {r['spawn']['sub0']}; door row {r['doors']['row']} x{r['doors']['records']}; "
          f"doors used {[(d['placement'], d['door_id'], d['record']) for d in r['doors']['used'][0]]}; "
          f"scripts {len(chains)} chains / {r['scripts']['records']} records in {r['scripts']['window']}; "
          f"messages {[(s, m.get('area_records'), m.get('global_records'), m['area_bank']) for s, m in r['messages'].items()]}; "
          f"world models {[(s, w['count'], w['span'], w['table']) for s, w in r['world_models'].items()]}; "
          f"run-time words {sorted({len(v) for v in ochanged.values()})}; {len(caps)} captures")
    return 0


if __name__ == '__main__':
    sys.exit(main())
