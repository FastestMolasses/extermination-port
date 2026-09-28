#!/usr/bin/env python3
"""Export the AREA00 tables: the roster of each sub (placements 0x82BB50 +
deferred groups), the spawn table (area 0 rows), the door destinations, the
overlay scripts, the overlay data section, the message records + area bank
and the world model bank (docs/AREA00_ASSETS.md).

The walks are the AREA11 / AREA01 exporters', imported unchanged
(export_area11_roster.walk_roster / encode_roster,
export_spawn_table.build_spawn_table, export_message_data.export through
export_area01_tables.export_messages, export_area01_tables.export_doors /
export_world_models / walk_chain / match_nodes, with export_area01_common
pointed at AREA00 by export_area00_common.configure). What is AREA00's own:

  * subs 0 and 1 share the placement table and the deferred group (the
    registries name the same addresses), but the roster header carries the
    sub, so each sub gets its own EMRO (sub<N>/roster.emro), checked with
    that sub's captures;
  * the arrival already shows words the script host rewrote (the arrival
    script 0x828D60 ran before the capture), so no capture holds the
    load-time image of the scripts or the overlay data. Both windows are
    the disc module's bytes (pinned by SHA-256), compared with every capture
    except the words that capture shows rewritten; those words are listed
    per capture (tables.json `run_time_words`) and each must lie inside a
    record one of the 14 chains reaches, or be the vector 0x82CCE0 that
    owner 0x8263C0 rewrites every frame (byte-identical C
    func_overlay_AREA00_00826380: [k] = constant + self +0xB0 + 4k); the
    lanes of that vector are re-derived here with EE single adds.

Output (ignored assets/area00/, disc-derived, never committed):
  sub<N>/roster.emro        EMRO v1 (src/game/em_actor_roster.c layout)
  spawn_table.emsp          EMSP v1 (the global export; area 0 rows checked)
  door_destinations.emsp    EMSP windows: D_0024E140[0..0x17) and the area 0 row
  area00_scripts/scripts.emsc  EMSC window 0x8284E0..0x82BB50 (14 chains)
  overlay_data.emsc         EMSC window of the overlay data 0x826F80..0x82D480
  message_data.emmd         EMMD v1 for area 0 (export_message_data layout)
  world_models.emwm (+.json)   EMWM v1 for *D_0028A59C
  tables.json               counts, hashes, verification summary

Usage (port root): python3 tools/export_area00_tables.py
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area00_common as A0  # noqa: E402

C = A0.configure(0)
import export_area01_tables as T  # noqa: E402
import export_area11_roster as R  # noqa: E402
import export_spawn_table as SP  # noqa: E402

PLACEMENTS = 0x82BB50            # D_0024D7C0[0][0] = [0][1] (AREA00_OVERVIEW.md section 4)
SCRIPT_ENTRIES = (0x8284E0, 0x8286E0, 0x828D60, 0x8294E0, 0x8299A0, 0x8299E0, 0x829BE0, 0x82A0E0,
                  0x82A540, 0x82A720, 0x82ABC0, 0x82B080, 0x82B790, 0x82B990)
SUB2_SCRIPT = 0x82D070           # started only by the sub-2 owner 0x826790: not a sub 0/1 chain
OWNER_VECTOR, OWNER_VECTOR_BEHAVIOUR = 0x82CCE0, 0x8263C0
# func_overlay_AREA00_00826380 (byte-identical C): D_0082CCE0[k] = VECTOR_ADD[k] + self[+0xB0 + 4k]
VECTOR_ADD = (-16.099998, 12.0, -40.400024)
DOOR_BEHAVIOURS = (0x1BC350, 0x1BB860, 0x823580, 0x825170)
AREA_BANK = ('extract/chunk04/f00_id41.bin', 0)
# Behaviours whose committed C neither writes nor reads node +0x44 (the model
# pointer): a slot they occupy keeps whatever +0x44 its previous occupant
# left, so the model-binding check skips them. 001EA240 (the rumble / shake
# effect driver, byte-matched C) holds such a word in a00_06 slot 102.
NO_MODEL_BEHAVIOURS = (0x1EA240,)


_POOL_NODES = T.pool_nodes


def model_owner_nodes(ram):
    return [(s, a) for s, a in _POOL_NODES(ram) if C.u32(ram, a + 0x10) not in NO_MODEL_BEHAVIOURS]


def export_world_models(image, caps, placements, groups):
    """export_area01_tables.export_world_models with its pool walk
    restricted to model_owner_nodes (the walk is read by name at call time,
    so it is swapped for the call and restored)."""
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


def scripts_window(read):
    """The lowest chain entry up to the placement table D_0024D7C0[0][sub]
    (the AREA01 rule; the sub-2 chain 0x82D070 lies after it)."""
    table = C.u32(read(C.u32(read(T.D_0024D7C0 + 4 * C.AREA, 4), 0) + 4 * C.SUB, 4), 0)
    return min(SCRIPT_ENTRIES), table


def chain_records(data, base):
    """{record address} reached by the 14 chains (walk_chain, 0x40 bytes each)."""
    out = set()
    for e in SCRIPT_ENTRIES:
        out.update(T.walk_chain(data, base, e))
    return out


def ee_add_f32(a_bits, b_bits):
    import ee_float_model as M
    return M.ee_add(a_bits, b_bits)


def vector_words(ram):
    """{address: expected word} for 0x82CCE0..+0xB from the live 0x8263C0
    owner (its C: constant + self +0xB0 lane, EE single add); {} when no
    such owner is live, None when there are several."""
    owners = [a for _s, a in T.pool_nodes(ram) if C.u32(ram, a + 0x10) == OWNER_VECTOR_BEHAVIOUR]
    if len(owners) != 1:
        return {} if not owners else None
    a = owners[0]
    out = {}
    for k, c in enumerate(VECTOR_ADD):
        cbits = struct.unpack('<I', struct.pack('<f', c))[0]
        out[OWNER_VECTOR + 4 * k] = ee_add_f32(cbits, C.u32(ram, a + 0xB0 + 4 * k))
    return out


def run_time_words(disc, base, ram, records):
    """(words, problems): the words of the window [base, base + len) that
    differ in `ram`, and the problems: a differing word that lies in no
    reached chain record and is not the owner vector, or an owner-vector
    word that does not equal its derivation."""
    words, problems = [], []
    vec = vector_words(ram)
    for k in range(0, len(disc), 4):
        a = base + k
        if ram[a:a + 4] == disc[k:k + 4]:
            continue
        words.append(a)
        if OWNER_VECTOR <= a < OWNER_VECTOR + 12:
            if vec is None or a not in vec or C.u32(ram, a) != vec[a]:
                problems.append(f'{a:#x}: owner vector word not derived from the 0x8263C0 owner')
        elif not any(r <= a < r + 0x40 for r in records):
            problems.append(f'{a:#x}: rewritten word outside every reached chain record')
    return words, problems


def export_roster(read, caps, sub):
    groups, paddr, placements = R.walk_roster(read, C.AREA, sub)
    if paddr != PLACEMENTS:
        raise SystemExit(f'sub {sub}: placement table {paddr:#x}, not {PLACEMENTS:#x}')
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
    for sub in A0.SUBS:
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
            raise SystemExit(f'spawn_table.emsp does not carry the area 0 sub {sub} records')
        subs[sub] = (entries, count, blob)
    for cap, sub in pairs:
        entries, count, blob = subs[sub]
        if cap.ram[entries:entries + len(blob)] != blob or cap.ram[table:table + 12] != read(table, 12):
            raise SystemExit(f'{cap.name}: area 0 spawn records differ from the ELF')
        room = cap.ram[0x810702]
        if room >= count or C.u32(cap.ram, T.D_008106C8) != C.u32(blob, 0x30 * room + 0x1C):
            raise SystemExit(f'{cap.name}: D_008106C8 is not +0x1C of spawn record {room}')
        rows.append(dict(capture=cap.name, sub=sub, entry=room, d8106c8=hex(C.u32(cap.ram, T.D_008106C8)),
                         record_1c=hex(C.u32(blob, 0x30 * room + 0x1C)) if room < count else None))
    global_copy = C.ROOT / 'assets/spawn/spawn_table.emsp'
    return data, dict(table=hex(table), subs={s: dict(entries=hex(e), records=n) for s, (e, n, _b) in subs.items()},
                      captures=rows, equals_assets_spawn=global_copy.exists() and global_copy.read_bytes() == data,
                      sha256=C.sha(data))


def export_scripts(ov, pairs, read):
    lo, hi = scripts_window(read)
    data = ov[lo - C.OVERLAY_ARENA:hi - C.OVERLAY_ARENA]
    chains = {f'{e:08X}': len(T.walk_chain(data, lo, e)) for e in SCRIPT_ENTRIES}
    records = chain_records(data, lo)
    changed = {}
    for cap, _sub in pairs:
        words, problems = run_time_words(data, lo, cap.ram, records)
        if problems:
            raise SystemExit(f'{cap.name}: scripts: {problems[:4]}')
        if words:
            changed[cap.name] = [hex(a) for a in words]
    return C.emsc(lo, data), dict(window=[hex(lo), hex(hi)], chains=chains, records=sum(chains.values()),
                                  run_time_words=changed, sha256=C.sha(data))


def export_overlay_data(ov, pairs, read):
    lo, hi = overlay_data_window(ov)
    data = ov[lo - C.OVERLAY_ARENA:hi - C.OVERLAY_ARENA]
    slo, shi = scripts_window(read)
    records = chain_records(ov[slo - C.OVERLAY_ARENA:shi - C.OVERLAY_ARENA], slo)
    changed = {}
    for cap, _sub in pairs:
        words, problems = run_time_words(data, lo, cap.ram, records)
        if problems:
            raise SystemExit(f'{cap.name}: overlay data: {problems[:4]}')
        if words:
            changed[cap.name] = [hex(a) for a in words]
    return C.emsc(lo, data), dict(window=[hex(lo), hex(hi)], run_time_words=changed, sha256=C.sha(data))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=A0.OUT)
    args = ap.parse_args(argv)
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    elf, ov = C.read_elf(), A0.read_overlay()
    read = C.static_reader(elf, ov)
    pairs = A0.all_captures()
    caps = [c for c, _s in pairs]
    lo, _hi = overlay_data_window(ov)
    for cap in caps:
        if cap.ram[C.OVERLAY_ARENA:lo] != ov[:lo - C.OVERLAY_ARENA]:
            raise SystemExit(f'{cap.name}: overlay text differs from AREA00.BIN')
    report = {}

    report['roster'] = {}
    rosters = {}
    for sub in A0.SUBS:
        A0.configure(sub)
        blob, rosters[sub], report['roster'][sub] = export_roster(read, C.captures(), sub)
        (out / f'sub{sub}').mkdir(exist_ok=True)
        (out / f'sub{sub}/roster.emro').write_bytes(blob)
    A0.configure(0)
    groups, placements = rosters[0]

    spawn, report['spawn'] = export_spawn(elf, read, pairs)
    (out / 'spawn_table.emsp').write_bytes(spawn)

    doors, report['doors'] = T.export_doors(elf, ov, caps, placements)
    row = bytes(read(int(report['doors']['row'], 16), report['doors']['bytes']))
    report['doors']['used'] = [dict(placement=i, behaviour=hex(C.u32(rec, 0x24)), door_id=rec[3] & 0x7F,
                                    area_change=bool(rec[3] & 0x80), record=list(row[4 * (rec[3] & 0x7F):
                                                                                      4 * (rec[3] & 0x7F) + 4]))
                               for i, rec in enumerate(placements) if C.u32(rec, 0x24) in DOOR_BEHAVIOURS]
    (out / 'door_destinations.emsp').write_bytes(doors)

    scripts, report['scripts'] = export_scripts(ov, pairs, read)
    (out / 'area00_scripts').mkdir(exist_ok=True)
    (out / 'area00_scripts/scripts.emsc').write_bytes(scripts)

    odata, report['overlay_data'] = export_overlay_data(ov, pairs, read)
    (out / 'overlay_data.emsc').write_bytes(odata)

    saved = T.AREA_BANK
    T.AREA_BANK = AREA_BANK
    try:
        report['messages'] = T.export_messages(caps, out / 'message_data.emmd')
    finally:
        T.AREA_BANK = saved

    lmap, _info = C.build_load_map(caps[0])
    image = C.LoadedImage(lmap)
    wm, index = export_world_models(image, caps, placements, groups)
    (out / 'world_models.emwm').write_bytes(wm)
    (out / 'world_models.json').write_text(json.dumps(index, indent=1) + '\n')
    report['world_models'] = dict(table=index['table_address'], count=index['count'],
                                  span=index['span_bytes'], verified=index['verified'])
    report['captures'] = [f'{c.name} (sub {s})' for c, s in pairs]
    (out / 'tables.json').write_text(json.dumps(report, indent=1) + '\n')
    r = report
    print(f"roster: {[(s, x['placements'], [g['address'] + 'x' + str(g['records']) for g in x['groups']]) for s, x in r['roster'].items()]}; "
          f"spawn {r['spawn']['subs']}; door row {r['doors']['row']} x{r['doors']['records']}; "
          f"scripts {len(r['scripts']['chains'])} chains / {r['scripts']['records']} records; "
          f"messages {r['messages']['area_records']} area records, bank {r['messages']['area_bank']['bytes']} bytes; "
          f"world models {index['count']} ({index['span_bytes']} bytes at {index['table_address']}); "
          f"{len(caps)} captures")
    return 0


if __name__ == '__main__':
    sys.exit(main())
