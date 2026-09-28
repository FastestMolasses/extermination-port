#!/usr/bin/env python3
"""Export the AREA02 tables: the roster of each sub (placements + deferred
groups), the spawn table (area 2 rows), the door destinations, the overlay
scripts, the overlay data section, the message records + area bank of each
sub and the world model bank of each sub (docs/AREA02_ASSETS.md).

The walks are the AREA11 / AREA01 / AREA00 exporters', imported unchanged
(export_area11_roster.walk_roster / encode_roster,
export_spawn_table.build_spawn_table, export_message_data.export through
export_area01_tables.export_messages, export_area01_tables.export_doors /
export_world_models / walk_chain / match_nodes, with export_area01_common
pointed at AREA02 by export_area02_common.configure). What is AREA02's own:

  * the subs have their own placement tables (0x827830 sub 0, 0x828170
    sub 1) and deferred groups, their own model banks and their own area
    message banks (*D_0028A594 lies in a different file per sub), so the
    roster, the message file and the model bank are per sub
    (sub<N>/roster.emro, sub<N>/message_data.emmd, sub<N>/world_models.emwm);
  * the script chains are the twelve the overlay's committed C starts
    (every func_001BA1A0 argument in src/overlays/AREA02/, SCRIPT_ENTRIES).
    The placement tables lie between them, so the AREA01 rule (the lowest
    entry up to the placement table) does not apply: the window is the
    lowest entry up to the end of the last record any chain reaches;
  * the arrival (a01r_03) and a02_00 hold the load-time image of the
    overlay data: no word differs from the disc module. Later captures show
    rewritten words; each must lie in a record a chain reaches, or be the
    done word (+4) of a record of the trigger table 0x827350 that the car's
    0x824910 (func_overlay_AREA02_008248D0, byte-identical C) sets to 1 once
    the car's +0xB0 exceeds the record's x (trigger_words).

Output (ignored assets/area02/, disc-derived, never committed):
  sub<N>/roster.emro          EMRO v1 (src/game/em_actor_roster.c layout)
  sub<N>/message_data.emmd    EMMD v1 for area 2 with the sub's area bank
  sub<N>/world_models.emwm (+.json)  EMWM v1 for the sub's *D_0028A59C
  spawn_table.emsp            EMSP v1 (the global export; area 2 rows checked)
  door_destinations.emsp      EMSP windows: D_0024E140[0..0x17) and the area 2 row
  area02_scripts/scripts.emsc EMSC window 0x826780..0x829190 (12 chains)
  overlay_data.emsc           EMSC window of the overlay data 0x825680..0x829280
  tables.json                 counts, hashes, verification summary

Usage (port root): python3 tools/export_area02_tables.py
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area02_common as A2  # noqa: E402

C = A2.configure(0)
import export_area01_tables as T  # noqa: E402
import export_area11_roster as R  # noqa: E402
import export_spawn_table as SP  # noqa: E402

PLACEMENTS = {0: 0x827830, 1: 0x828170}      # D_0024D7C0[2][sub] (FOURTH_LEVEL_ROUTE.md section 2.2)
# every func_001BA1A0 script argument of the committed AREA02 overlay C
SCRIPT_ENTRIES = (0x826780, 0x826980, 0x8269C0, 0x826A00, 0x826A40, 0x826A80, 0x826AC0, 0x826B40,
                  0x826F40, 0x827670, 0x828C50, 0x828E10)
# func_overlay_AREA02_008248D0 (byte-identical C): 18 records of 0x20 bytes
# (id, done, pad, pad, position); run by the car (0x8242F0 kind 7)
TRIGGERS, TRIGGER_COUNT, TRIGGER_SIZE = 0x827350, 18, 0x20
DISPATCH = 0x823930
DOOR_BEHAVIOURS = (0x1BC350, 0x1BB860)
# Behaviours whose committed C neither writes nor reads node +0x44 (the model
# pointer): a slot they occupy keeps whatever +0x44 its previous occupant
# left, so the model-binding check skips them (AREA00_ASSETS.md finding 7)
NO_MODEL_BEHAVIOURS = (0x1EA240,)


_POOL_NODES = T.pool_nodes


def model_owner_nodes(ram):
    return [(s, a) for s, a in _POOL_NODES(ram) if C.u32(ram, a + 0x10) not in NO_MODEL_BEHAVIOURS]


def export_world_models(image, caps, placements, groups):
    """export_area01_tables.export_world_models with its pool walk
    restricted to model_owner_nodes (swapped for the call and restored)."""
    saved = T.pool_nodes
    T.pool_nodes = model_owner_nodes
    try:
        return T.export_world_models(image, caps, placements, groups)
    finally:
        T.pool_nodes = saved


def overlay_data_window(ov):
    """The module's data section: its last data-size bytes (MWo3 header word
    4), loaded whole at the header's load address (word 2)."""
    load, size = C.u32(ov, 8), C.u32(ov, 0x10)
    return load + len(ov) - size, load + len(ov)


def chain_records(data, base):
    """{record address} reached by the chains (walk_chain, 0x40 bytes each)."""
    out = set()
    for e in SCRIPT_ENTRIES:
        out.update(T.walk_chain(data, base, e))
    return out


def scripts_window(ov):
    """The lowest chain entry up to the end of the last record reached."""
    lo, hi = overlay_data_window(ov)
    records = chain_records(ov[lo - C.OVERLAY_ARENA:hi - C.OVERLAY_ARENA], lo)
    return min(SCRIPT_ENTRIES), max(records) + 0x40


def car_x(ram):
    """The car's +0xB0 (0x823930 dispatch 4, +0x0D 7): the live node's, or,
    when it has freed itself, the one pool slot that still carries its
    behaviour word with +0 clear (a freed slot keeps +0xB0; measured in
    a02_02..a02_04). None when neither exists or several do."""
    live = [a for s in range(0x100) for a in (0x7A5640 + 0x2F0 * s,)
            if ram[a] and C.u32(ram, a + 0x10) == DISPATCH and ram[a + 2] & 0x1F == 4 and ram[a + 0x0D] == 7]
    if len(live) == 1:
        return struct.unpack_from('<f', ram, live[0] + 0xB0)[0], 'live'
    if live:
        return None, 'several cars'
    freed = [a for s in range(0x100) for a in (0x7A5640 + 0x2F0 * s,)
             if not ram[a] and C.u32(ram, a + 0x10) == DISPATCH]
    if len(freed) == 1:
        return struct.unpack_from('<f', ram, freed[0] + 0xB0)[0], 'freed'
    return None, 'no car'


def trigger_words(disc, base, ram):
    """{address: expected word} of every trigger done word (+4), by the car's
    code: a record is done (1) once the car's +0xB0 exceeds its x (+0x10);
    the car runs east only, so its current x decides. The disc must hold 0
    in every done word. None when the car's x is not known."""
    x, _how = car_x(ram)
    if x is None:
        return None
    out = {}
    for k in range(TRIGGER_COUNT):
        at = TRIGGERS + TRIGGER_SIZE * k
        if C.u32(disc, at - base + 4) != 0:
            return None
        tx = struct.unpack_from('<f', disc, at - base + 0x10)[0]
        out[at + 4] = 1 if x > tx else 0
    return out


def run_time_words(disc, base, ram, records):
    """(words, problems): the words of the window [base, base + len) that
    differ in `ram`, and the problems: a differing word that lies in no
    reached chain record and is not a trigger done word, or a trigger done
    word (rewritten or not) that does not equal its derivation."""
    words, problems = [], []
    lo, hi = TRIGGERS, TRIGGERS + TRIGGER_SIZE * TRIGGER_COUNT
    covers = base <= lo and hi <= base + len(disc)
    trig = trigger_words(disc, base, ram) if covers else None
    if covers:
        if trig is None:
            if ram[lo:hi] != disc[lo - base:hi - base]:
                problems.append('trigger table rewritten while the car\'s x is unknown')
        else:
            for a, v in trig.items():
                if C.u32(ram, a) != v:
                    problems.append(f'{a:#x}: trigger done word {C.u32(ram, a)} != {v} by the car\'s x')
    for k in range(0, len(disc), 4):
        a = base + k
        if ram[a:a + 4] == disc[k:k + 4]:
            continue
        words.append(a)
        if lo <= a < hi:
            if trig is None or a not in trig:
                problems.append(f'{a:#x}: trigger table word other than a done word rewritten')
        elif not any(r <= a < r + 0x40 for r in records):
            problems.append(f'{a:#x}: rewritten word outside every reached chain record')
    return words, problems


def export_roster(read, caps, sub):
    groups, paddr, placements = R.walk_roster(read, C.AREA, sub)
    if paddr != PLACEMENTS[sub]:
        raise SystemExit(f'sub {sub}: placement table {paddr:#x}, not {PLACEMENTS[sub]:#x}')
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


def export_spawn(elf, read, pairs):
    data = SP.build_spawn_table(elf)
    table = C.u32(read(T.D_0024D650 + 4 * C.AREA, 4), 0)
    windows = [struct.unpack_from('<II', data, 16 + 8 * k) for k in range(C.u32(data, 8))]
    rows, subs = [], {}
    for sub in A2.SUBS:
        entries = C.u32(read(table + 4 * sub, 4), 0)
        nxt = C.u32(read(table + 4 * (sub + 1), 4), 0)
        count = (nxt - entries) // 0x30
        blob = read(entries, 0x30 * count)
        body, found = 16 + 8 * len(windows), False
        for address, size in windows:
            if address <= entries and entries + len(blob) <= address + size:
                at = body + entries - address
                found = data[at:at + len(blob)] == blob
            body += size
        if not found:
            raise SystemExit(f'spawn_table.emsp does not carry the area 2 sub {sub} records')
        subs[sub] = (entries, count, blob)
    for cap, sub in pairs:
        entries, count, blob = subs[sub]
        if cap.ram[entries:entries + len(blob)] != blob or cap.ram[table:table + 12] != read(table, 12):
            raise SystemExit(f'{cap.name}: area 2 spawn records differ from the ELF')
        room = cap.ram[0x810702]
        if room >= count or C.u32(cap.ram, T.D_008106C8) != C.u32(blob, 0x30 * room + 0x1C):
            raise SystemExit(f'{cap.name}: D_008106C8 is not +0x1C of spawn record {room}')
        rows.append(dict(capture=cap.name, sub=sub, entry=room, d8106c8=hex(C.u32(cap.ram, T.D_008106C8)),
                         record_1c=hex(C.u32(blob, 0x30 * room + 0x1C))))
    global_copy = C.ROOT / 'assets/spawn/spawn_table.emsp'
    return data, dict(table=hex(table), subs={s: dict(entries=hex(e), records=n) for s, (e, n, _b) in subs.items()},
                      captures=rows, equals_assets_spawn=global_copy.exists() and global_copy.read_bytes() == data,
                      sha256=C.sha(data))


def window_export(ov, pairs, lo, hi, records):
    data = ov[lo - C.OVERLAY_ARENA:hi - C.OVERLAY_ARENA]
    changed = {}
    for cap, _sub in pairs:
        words, problems = run_time_words(data, lo, cap.ram, records)
        if problems:
            raise SystemExit(f'{cap.name}: window {lo:#x}: {problems[:4]}')
        if words:
            changed[cap.name] = [hex(a) for a in words]
    return data, changed


def area_bank(image, cap):
    """(bank address, [labels of the mapped files it starts in and runs
    on into]) of *D_0028A594 in the capture's load map."""
    bank = C.u32(cap.ram, T.D_0028A594)
    hit = image.locate(bank)
    if hit is None:
        raise SystemExit(f'{cap.name}: area message bank {bank:#x} outside the load map')
    return bank, hit[4]


def bank_source(image, bank, scratch):
    """The resident bytes from the bank address to the end of the run of
    contiguous mapped files that holds it, written to the scratch tree for
    export_message_data (whose bank_extent then bounds the bank by its own
    header). The sub-1 bank starts in chunk06/f00_id96 and runs on into
    f01_id71, contiguous in RAM. Returns (path, [labels])."""
    entries = sorted(image.map)
    k = next(i for i, e in enumerate(entries) if e[0] <= bank < e[0] + e[3])
    end, labels = entries[k][0] + entries[k][3], [entries[k][4]]
    for e in entries[k + 1:]:
        if e[0] != end:
            break
        end, labels = e[0] + e[3], labels + [e[4]]
    scratch.mkdir(parents=True, exist_ok=True)
    path = scratch / 'area_message_bank.bin'
    path.write_bytes(image.read(bank, end - bank))
    return path, labels


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=A2.OUT)
    args = ap.parse_args(argv)
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    elf, ov = C.read_elf(), A2.read_overlay()
    read = C.static_reader(elf, ov)
    pairs = A2.all_captures()
    caps = [c for c, _s in pairs]
    olo, ohi = overlay_data_window(ov)
    for cap in caps:
        if cap.ram[C.OVERLAY_ARENA:olo] != ov[:olo - C.OVERLAY_ARENA]:
            raise SystemExit(f'{cap.name}: overlay text differs from AREA02.BIN')
    report = dict(roster={}, messages={}, world_models={})
    placements_by_sub = {}
    for sub in A2.SUBS:
        A2.configure(sub)
        sc = C.captures()
        blob, (groups, placements), report['roster'][sub] = export_roster(read, sc, sub)
        placements_by_sub[sub] = placements
        (out / f'sub{sub}').mkdir(exist_ok=True)
        (out / f'sub{sub}/roster.emro').write_bytes(blob)
        lmap, _info = C.build_load_map(sc[0])
        image = C.LoadedImage(lmap)
        bank, _label = area_bank(image, sc[0])
        for cap in sc[1:]:
            if area_bank(image, cap)[0] != bank:
                raise SystemExit(f'{cap.name}: a different area message bank')
        source, labels = bank_source(image, bank, C.SCRATCH)
        saved = T.AREA_BANK
        T.AREA_BANK = (str(source), 0)
        mm = out / f'sub{sub}/message_data.emmd'
        try:
            report['messages'][sub] = T.export_messages(sc, mm)
            report['messages'][sub]['area_bank'].update(source='the resident image from the bank address',
                                                        address=hex(bank), resident_files=labels)
        except ValueError as error:
            # finding: *D_0028A594 names no bank the accessors can walk
            # (sub 1: it is the top cursor D_0028A73C, the AREA01 value,
            # now chunk06/f00_id96); no message file is written for the sub
            if mm.exists():
                mm.unlink()
            report['messages'][sub] = dict(area_bank=dict(address=hex(bank), resident_files=labels,
                                                          top_cursor=hex(C.u32(sc[0].ram, 0x28A73C)),
                                                          refused=f'export_message_data.bank_extent: {error}'))
        finally:
            T.AREA_BANK = saved
        wm, index = export_world_models(image, sc, placements, groups)
        (out / f'sub{sub}/world_models.emwm').write_bytes(wm)
        (out / f'sub{sub}/world_models.json').write_text(json.dumps(index, indent=1) + '\n')
        report['world_models'][sub] = dict(table=index['table_address'], count=index['count'],
                                           span=index['span_bytes'], source=index['source'],
                                           verified=index['verified'])
    A2.configure(0)

    spawn, report['spawn'] = export_spawn(elf, read, pairs)
    (out / 'spawn_table.emsp').write_bytes(spawn)

    doors, report['doors'] = T.export_doors(elf, ov, caps, placements_by_sub[0])
    row = bytes(read(int(report['doors']['row'], 16), report['doors']['bytes']))
    report['doors']['used'] = {
        sub: [dict(placement=i, behaviour=hex(C.u32(rec, 0x24)), door_id=rec[3] & 0x7F,
                   area_change=bool(rec[3] & 0x80), record=list(row[4 * (rec[3] & 0x7F):4 * (rec[3] & 0x7F) + 4]))
              for i, rec in enumerate(placements_by_sub[sub]) if C.u32(rec, 0x24) in DOOR_BEHAVIOURS]
        for sub in A2.SUBS}
    for sub, used in report['doors']['used'].items():
        for d in used:
            if 4 * d['door_id'] + 4 > len(row):
                raise SystemExit(f'sub {sub} placement[{d["placement"]}]: door id {d["door_id"]} outside the row')
    (out / 'door_destinations.emsp').write_bytes(doors)

    records = chain_records(ov[olo - C.OVERLAY_ARENA:ohi - C.OVERLAY_ARENA], olo)
    slo, shi = scripts_window(ov)
    sdata, schanged = window_export(ov, pairs, slo, shi, records)
    chains = {f'{e:08X}': len(T.walk_chain(sdata, slo, e)) for e in SCRIPT_ENTRIES}
    report['scripts'] = dict(window=[hex(slo), hex(shi)], chains=chains, records=sum(chains.values()),
                             run_time_words=schanged, sha256=C.sha(sdata))
    (out / 'area02_scripts').mkdir(exist_ok=True)
    (out / 'area02_scripts/scripts.emsc').write_bytes(C.emsc(slo, sdata))

    odata, ochanged = window_export(ov, pairs, olo, ohi, records)
    report['overlay_data'] = dict(window=[hex(olo), hex(ohi)], run_time_words=ochanged, sha256=C.sha(odata),
                                  car_x={c.name: car_x(c.ram)[1] for c in caps})
    (out / 'overlay_data.emsc').write_bytes(C.emsc(olo, odata))

    report['captures'] = [f'{c.name} (sub {s})' for c, s in pairs]
    (out / 'tables.json').write_text(json.dumps(report, indent=1) + '\n')
    r = report
    print(f"roster: {[(s, x['placements'], [g['address'] + 'x' + str(g['records']) for g in x['groups']]) for s, x in r['roster'].items()]}; "
          f"spawn {r['spawn']['subs']}; door row {r['doors']['row']} x{r['doors']['records']}; "
          f"scripts {len(chains)} chains / {r['scripts']['records']} records in {r['scripts']['window']}; "
          f"messages {[(s, m.get('area_records'), m['area_bank']) for s, m in r['messages'].items()]}; "
          f"world models {[(s, w['count'], w['span'], w['table']) for s, w in r['world_models'].items()]}; "
          f"{len(caps)} captures")
    return 0


if __name__ == '__main__':
    sys.exit(main())
