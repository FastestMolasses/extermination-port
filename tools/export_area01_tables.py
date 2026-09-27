#!/usr/bin/env python3
"""Export the AREA01 sub-0 tables: roster (placements 0x82BD50 + deferred
groups), spawn table (area 1 rows 0x24B1A0), door destinations, the overlay
scripts, the overlay data section, the message records + area bank and the
world model bank (docs/AREA01_ASSETS.md).

Every table is walked the way the original reads it (the byte-matched C under
../Extermination/src: 001B6910/001B6660/001B6990 for the roster, 001B07C0 /
001B0250 for the spawn records, 001BC150 for the door row, 001B0EA0 ->
001C6120 for the model bank) by the existing AREA11 exporters, imported
unchanged: export_area11_roster.walk_roster/encode_roster,
export_spawn_table.build_spawn_table, export_message_data.export (area 1 and
the area bank *D_0028A594 names), export_world_models.model_record/serialize.

Verification (fails the export): every exported byte equals every AREA01
capture (the overlay data words the game rewrites at run time are listed, not
exported differently); the live pool nodes a placement or group record spawned
carry exactly the fields 001B6990 / 001B6660 copy from it (position/rotation
differences are listed as moved owners); every owner bound to a bank model
has +0x44 = 001C6120(*D_0028A59C, +0x0D) and the model's bone count; the
message records and both banks equal RAM; the script chains walk inside the
exported window.

Output (ignored assets/area01/, disc-derived, never committed):
  roster.emro              EMRO v1 (src/game/em_actor_roster.c layout)
  spawn_table.emsp         EMSP v1 (the global export; area 1 rows checked)
  door_destinations.emsp   EMSP windows: D_0024E140[0..0x17) and its area 1 row
  area01_scripts/scripts.emsc  EMSC window 0x829860..0x82BD50 (14 chains)
  overlay_data.emsc        EMSC window of the overlay data 0x828A00..0x82CD00
  message_data.emmd        EMMD v1 for area 1 (export_message_data layout)
  world_models.emwm (+.json)  EMWM v1 for *D_0028A59C
  tables.json              counts, hashes, verification summary

Usage (port root): python3 tools/export_area01_tables.py
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area01_common as C  # noqa: E402
import export_area11_roster as R  # noqa: E402
import export_spawn_table as SP  # noqa: E402
import export_message_data as M  # noqa: E402
import export_world_models as W  # noqa: E402

D_0024D650, D_0024E140, D_0024D7C0, D_0024D820 = 0x24D650, 0x24E140, 0x24D7C0, 0x24D820
D_00264DD0, D_0028A594, D_0028A4E8, D_0028A59C = 0x264DD0, 0x28A594, 0x28A4E8, 0x28A59C
D_008106C8 = 0x8106C8
OVERLAY_DATA = (0x828A00, 0x82CD00)
SCRIPTS = (0x829860, 0x82BD50)
SCRIPT_ENTRIES = (0x829860, 0x8298E0, 0x829E60, 0x829FA0, 0x82A660, 0x82A7B0, 0x82AA90, 0x82AC10,
                  0x82AD10, 0x82AD90, 0x82B0D0, 0x82B4D0, 0x82B590, 0x82BAD0)
POOL_BASE, POOL_STRIDE, POOL_SLOTS, ACTOR_HEAD = 0x7A5640, 0x2F0, 0x100, 0x275BC0
AREA_BANK = ('extract/chunk05/f00_id41.bin', 0)


# ---------------------------------------------------------------------------
# Roster


def pool_nodes(ram):
    out = []
    for slot in range(POOL_SLOTS):
        a = POOL_BASE + slot * POOL_STRIDE
        if ram[a]:
            out.append((slot, a))
    return out


def spawn_fields_placement(rec, index):
    """The node fields 001B6990 writes from a 0x28-byte placement record."""
    f = {0x03: rec[2:3], 0x2E: struct.pack('<H', rec[3]), 0x0D: rec[4:5], 0x9A: bytes([index & 0xFF]),
         0x54: rec[8:10], 0x56: rec[10:12], 0x10: rec[0x24:0x28]}
    if (C.s16(rec, 0) & -0xE1) == 2:
        f[0x9E] = rec[6:7]
    else:
        f[0x0E] = rec[6:8]
    return f, rec[0x0C:0x18], rec[0x18:0x24]


def spawn_fields_group(rec):
    """The node fields 001B6660 writes from a 0x2C-byte group record."""
    f = {0x9A: rec[2:3], 0x03: rec[6:7], 0x2E: struct.pack('<H', rec[7]), 0x0D: rec[8:9],
         0x54: rec[0x0C:0x0E], 0x56: rec[0x0E:0x10], 0x10: rec[0x28:0x2C]}
    if (C.s16(rec, 4) & ~0xE0) == 2:
        f[0x9E] = rec[0x0A:0x0B]
    else:
        f[0x0E] = rec[0x0A:0x0C]
    return f, rec[0x10:0x1C], rec[0x1C:0x28]


def match_nodes(ram, sources):
    """sources = [(label, fields, pos, rot)]. A node matches a source when
    every copied field is equal; returns per source the matching slots and
    whether position and rotation still equal the record."""
    nodes = pool_nodes(ram)
    rows = []
    for label, fields, pos, rot in sources:
        hits = []
        for slot, a in nodes:
            if all(ram[a + off:a + off + len(v)] == v for off, v in fields.items()):
                hits.append(dict(slot=slot, position_equal=ram[a + 0xB0:a + 0xBC] == pos,
                                 rotation_equal=ram[a + 0xC0:a + 0xCC] == rot))
        partial = []
        if not hits:
            # same behaviour and +0x9A: the owner rewrote some copied fields
            for slot, a in nodes:
                if ram[a + 0x10:a + 0x14] == fields[0x10] and ram[a + 0x9A:a + 0x9B] == fields[0x9A]:
                    partial.append(dict(slot=slot, changed=sorted(
                        hex(off) for off, v in fields.items() if ram[a + off:a + off + len(v)] != v)))
        rows.append(dict(source=label, nodes=hits, partial=partial))
    return rows


def export_roster(elf, ov, caps):
    read = C.static_reader(elf, ov)
    groups, paddr, placements = R.walk_roster(read, C.AREA, C.SUB)
    data = R.encode_roster(C.AREA, C.SUB, groups, paddr, placements)
    if paddr != 0x82BD50:
        raise SystemExit(f'placement table {paddr:#x}, not 0x82BD50')
    sources = []
    for i, rec in enumerate(placements):
        if (C.s16(rec, 0) & 0xFF) == 0x0B:
            continue                                  # 001B6990 skips class 0x0B
        f, pos, rot = spawn_fields_placement(rec, i)
        sources.append((f'placement[{i}]', f, pos, rot))
    for address, records in groups:
        for i, rec in enumerate(records):
            f, pos, rot = spawn_fields_group(rec)
            sources.append((f'group {address:#x}[{i}]', f, pos, rot))
    per_capture, ambiguous = [], set()
    for cap in caps:
        for address, records in groups:
            if cap.ram[address:address + 0x2C * len(records)] != b''.join(records):
                raise SystemExit(f'{cap.name}: group {address:#x} differs from the overlay')
        if cap.ram[paddr:paddr + 0x28 * len(placements)] != b''.join(placements):
            raise SystemExit(f'{cap.name}: placement table differs from the overlay')
        rows = match_nodes(cap.ram, sources)
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
    return data, dict(placement_table=hex(paddr), placements=len(placements),
                      groups=[dict(address=hex(a), records=len(r)) for a, r in groups],
                      captures=per_capture, ambiguous_sources=sorted(ambiguous), sha256=C.sha(data))


# ---------------------------------------------------------------------------
# Spawn table and doors


def export_spawn(elf, caps):
    data = SP.build_spawn_table(elf)
    read = C.static_reader(elf, C.read_overlay())
    table = C.u32(read(D_0024D650 + 4 * C.AREA, 4), 0)
    entries = C.u32(read(table + 4 * C.SUB, 4), 0)
    if entries != 0x24B1A0:
        raise SystemExit(f'area 1 sub 0 spawn entries at {entries:#x}, not 0x24B1A0')
    nxt = C.u32(read(table + 4 * (C.SUB + 1), 4), 0)
    count = (nxt - entries) // 0x30 if nxt > entries else 10
    blob = read(entries, 0x30 * count)
    # the window must hold exactly these bytes
    ranges = [struct.unpack_from('<II', data, 16 + 8 * k) for k in range(C.u32(data, 8))]
    body = 16 + 8 * len(ranges)
    found = False
    for address, size in ranges:
        if address <= entries and entries + len(blob) <= address + size:
            at = body + entries - address
            found = data[at:at + len(blob)] == blob
        body += size
    if not found:
        raise SystemExit('spawn_table.emsp does not carry the area 1 sub 0 records')
    rows = []
    for cap in caps:
        if cap.ram[entries:entries + len(blob)] != blob or cap.ram[table:table + 8] != read(table, 8):
            raise SystemExit(f'{cap.name}: area 1 spawn records differ from the ELF')
        room = cap.ram[0x810702]
        rows.append(dict(capture=cap.name, entry=room, d8106c8=hex(C.u32(cap.ram, D_008106C8)),
                         record_1c=hex(C.u32(blob, 0x30 * room + 0x1C)) if room < count else None))
    global_copy = C.ROOT / 'assets/spawn/spawn_table.emsp'
    same = global_copy.exists() and global_copy.read_bytes() == data
    return data, dict(table=hex(table), entries=hex(entries), records=count, captures=rows,
                      equals_assets_spawn=same, sha256=C.sha(data))


def export_doors(elf, ov, caps, placements):
    read = C.static_reader(elf, ov)
    rows = [C.u32(read(D_0024E140 + 4 * k, 4), 0) for k in range(0x17)]
    row = rows[C.AREA]
    # the row ends where the next address the table names (or the table) begins
    end = min(p for p in set(rows) | {D_0024E140} if p > row)
    data = read(row, end - row)
    for cap in caps:
        if cap.ram[row:end] != data:
            raise SystemExit(f'{cap.name}: door row differs from the ELF')
    used = []
    for i, rec in enumerate(placements):
        if C.u32(rec, 0x24) in (0x1BC350, 0x1BB860, 0x823580):
            door = rec[3] & 0x7F
            if 4 * door + 4 > len(data):
                raise SystemExit(f'placement[{i}] door id {door} outside the exported row')
            used.append(dict(placement=i, door_id=door, area_change=bool(rec[3] & 0x80),
                             record=list(data[4 * door:4 * door + 4])))
    # EMSP v1 address windows (em_spawn_table_parse's container): the
    # pointer array D_0024E140[0..0x17) and the area 1 row 001BC150 indexes.
    ptrs = read(D_0024E140, 4 * 0x17)
    for cap in caps:
        if cap.ram[D_0024E140:D_0024E140 + len(ptrs)] != ptrs:
            raise SystemExit(f'{cap.name}: D_0024E140 differs from the ELF')
    ranges = sorted([(row, data), (D_0024E140, ptrs)])
    blob = struct.pack('<4sIII', b'EMSP', 1, len(ranges), 0)
    blob += b''.join(struct.pack('<II', a, len(d)) for a, d in ranges) + b''.join(d for _a, d in ranges)
    return blob, dict(row=hex(row), bytes=len(data), records=len(data) // 4, used=used,
                      windows=[[hex(a), len(d)] for a, d in ranges], sha256=C.sha(data))


# ---------------------------------------------------------------------------
# Scripts and overlay data


def walk_chain(data, base, entry):
    pc, walked = entry, []
    for _ in range(256):
        if not base <= pc < base + len(data) or (pc - base) % 4:
            raise SystemExit(f'script {entry:#x}: record {pc:#x} leaves the window')
        walked.append(pc)
        flags = C.u32(data, pc - base)
        if flags & 0x80000000:
            return walked
        pc = C.u32(data, pc - base + 4) if flags & 0x40000000 else pc + 64
    raise SystemExit(f'script {entry:#x}: no stop record')


def export_scripts(ov, caps):
    lo, hi = SCRIPTS
    data = ov[lo - C.OVERLAY_ARENA:hi - C.OVERLAY_ARENA]
    chains = {}
    for e in SCRIPT_ENTRIES:
        chains[f'{e:08X}'] = len(walk_chain(data, lo, e))
    # The window is the load-time image: the arrival capture must hold it
    # unchanged. Later beats show words the script host rewrote while a chain
    # ran; they are listed with their record and offset, not exported.
    changed = {}
    for cap in caps:
        diff = [lo + k for k in range(0, len(data), 4) if cap.ram[lo + k:lo + k + 4] != data[k:k + 4]]
        if diff and cap.name == C.ARRIVAL.name:
            raise SystemExit(f'{cap.name}: the arrival already differs from the disc at {diff}')
        if diff:
            changed[cap.name] = [f'{a:#x} (record {a - (a - lo) % 64:#x} +{(a - lo) % 64:#x})' for a in diff]
    return C.emsc(lo, data), dict(window=[hex(lo), hex(hi)], chains=chains,
                                  records=sum(chains.values()), run_time_words=changed, sha256=C.sha(data))


def export_overlay_data(ov, caps):
    lo, hi = OVERLAY_DATA
    data = ov[lo - C.OVERLAY_ARENA:hi - C.OVERLAY_ARENA]
    changed = {}
    for cap in caps:
        diff = [lo + k for k in range(0, len(data), 4) if cap.ram[lo + k:lo + k + 4] != data[k:k + 4]]
        if diff:
            changed[cap.name] = [hex(a) for a in diff]
    return C.emsc(lo, data), dict(window=[hex(lo), hex(hi)], run_time_words=changed, sha256=C.sha(data))


# ---------------------------------------------------------------------------
# Messages


def export_messages(caps, out):
    elf = M.Elf(C.ELF_PATH)
    saved = M.AREA, dict(M.BANKS)
    M.AREA = C.AREA
    M.BANKS = {'global': M.BANKS['global'], 'area': AREA_BANK}
    try:
        report = M.export(C.DECOMP, out)
    finally:
        M.AREA, M.BANKS = saved[0], saved[1]
    blob = out.read_bytes()
    area, gcount, acount, srows, _cursor, gbank, abank = struct.unpack_from('<7I', blob, 8)
    if area != C.AREA:
        raise SystemExit('message export area mismatch')
    at = 140
    grecs = blob[at:at + 8 * gcount]; at += 8 * gcount
    arecs = blob[at:at + 8 * acount]; at += 8 * acount
    at += 16 * srows
    gb = blob[at:at + gbank]; at += gbank
    ab = blob[at:at + abank]
    gptr, aptr = elf.u32(D_00264DD0), elf.u32(D_00264DD0 + 4 * (C.AREA + 1))
    rows = []
    for cap in caps:
        if cap.ram[gptr:gptr + len(grecs)] != grecs or cap.ram[aptr:aptr + len(arecs)] != arecs:
            raise SystemExit(f'{cap.name}: message record tables differ from RAM')
        if cap.ram[C.u32(cap.ram, D_0028A4E8):C.u32(cap.ram, D_0028A4E8) + gbank] != gb:
            raise SystemExit(f'{cap.name}: global bank differs from RAM at *D_0028A4E8')
        if cap.ram[C.u32(cap.ram, D_0028A594):C.u32(cap.ram, D_0028A594) + abank] != ab:
            raise SystemExit(f'{cap.name}: area bank differs from RAM at *D_0028A594')
        rows.append(cap.name)
    return dict(area_table=hex(aptr), area_records=acount, global_records=gcount,
                area_bank=dict(source=AREA_BANK[0], bytes=abank), global_bank_bytes=gbank,
                stream_rows=srows, captures_equal=rows, sha256=report['sha256'])


# ---------------------------------------------------------------------------
# World model bank


def export_world_models(image, caps, placements, groups):
    table = C.u32(caps[0].ram, D_0028A59C)
    for cap in caps:
        if C.u32(cap.ram, D_0028A59C) != table:
            raise SystemExit(f'{cap.name}: D_0028A59C differs')
    hit = image.locate(table)
    if hit is None:
        raise SystemExit('world model table outside the load map')
    avail = image.read(table, image.span()[1] - table)
    count = C.u32(avail, 0)
    if not 0 < count < 256:
        raise SystemExit(f'model table word 0 = {count}')
    models = []
    for ident in range(count):
        off = C.s32(avail, 4 + 4 * ident) >> 2 << 2
        if off < 4 + 4 * count:
            raise SystemExit(f'model {ident:#x}: offset inside the table')
        blocks, qwc, bones, skel, size, radius = W.model_record(avail, off, ident)
        models.append(dict(id=ident, offset=off, blocks=blocks, qwc=qwc, bones=bones, skeleton=skel,
                           bytes=size, radius_bits=radius))
    span = max(m['offset'] + m['bytes'] for m in models)
    x = dict(count=count, models=models, span=avail[:span])
    seen, rows = {}, []
    by_address = {table + m['offset']: m for m in models}
    record_param = {}
    for i, rec in enumerate(placements):
        record_param.setdefault(C.u32(rec, 0x24), set()).add(rec[4])
    for _a, recs in groups:
        for rec in recs:
            record_param.setdefault(C.u32(rec, 0x28), set()).add(rec[8])
    for cap in caps:
        if cap.ram[table:table + span] != x['span']:
            raise SystemExit(f'{cap.name}: model bank differs from RAM')
        bound = 0
        for slot, a in pool_nodes(cap.ram):
            model = C.u32(cap.ram, a + 0x44)
            m = by_address.get(model)
            if m is None:
                continue
            ident = cap.ram[a + 0x0D]
            lookup = table + (C.s32(cap.ram, table + 4 + 4 * (ident & 0x7FFF)) >> 2 << 2)
            if lookup != model:
                raise SystemExit(f'{cap.name} slot {slot}: +0x44 != 001C6120(table, +0x0D)')
            if cap.ram[a + 0x0C] != m['bones'] or not all(C.u32(cap.ram, a + 0x110 + 4 * k)
                                                          for k in range(m['bones'])):
                raise SystemExit(f'{cap.name} slot {slot}: bone count / slots differ from model {ident:#x}')
            behaviour = C.u32(cap.ram, a + 0x10)
            seen.setdefault(m['id'], set()).add(behaviour)
            bound += 1
        rows.append(dict(capture=cap.name, owners_bound=bound))
    index = dict(table_address=hex(table), count=count, span_bytes=span, span_sha256=C.sha(x['span']),
                 models=[dict(id=hex(m['id']), offset=hex(m['offset']), address=hex(table + m['offset']),
                              blocks=m['blocks'], bones=m['bones'], bytes=m['bytes'],
                              radius_bits=hex(m['radius_bits']),
                              captured_behaviours=sorted(hex(b) for b in seen.get(m['id'], ())),
                              record_params_of_those_behaviours=sorted(
                                  hex(p) for b in seen.get(m['id'], ()) for p in record_param.get(b, ())))
                         for m in models],
                 verified=rows, source=image.locate(table)[4])
    return W.serialize(x, table), index


# ---------------------------------------------------------------------------


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=C.OUT)
    args = ap.parse_args(argv)
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    elf, ov = C.read_elf(), C.read_overlay()
    caps = C.captures()
    for cap in caps:
        if cap.ram[C.OVERLAY_ARENA:0x828A00] != ov[:0x828A00 - C.OVERLAY_ARENA]:
            raise SystemExit(f'{cap.name}: overlay text differs from AREA01.BIN')
    lmap, _info = C.build_load_map(caps[0])
    image = C.LoadedImage(lmap)
    report = {}

    roster, report['roster'] = export_roster(elf, ov, caps)
    (out / 'roster.emro').write_bytes(roster)
    read = C.static_reader(elf, ov)
    groups, _paddr, placements = R.walk_roster(read, C.AREA, C.SUB)

    spawn, report['spawn'] = export_spawn(elf, caps)
    (out / 'spawn_table.emsp').write_bytes(spawn)

    doors, report['doors'] = export_doors(elf, ov, caps, placements)
    (out / 'door_destinations.emsp').write_bytes(doors)

    scripts, report['scripts'] = export_scripts(ov, caps)
    (out / 'area01_scripts').mkdir(exist_ok=True)
    (out / 'area01_scripts/scripts.emsc').write_bytes(scripts)

    odata, report['overlay_data'] = export_overlay_data(ov, caps)
    (out / 'overlay_data.emsc').write_bytes(odata)

    report['messages'] = export_messages(caps, out / 'message_data.emmd')

    wm, index = export_world_models(image, caps, placements, groups)
    (out / 'world_models.emwm').write_bytes(wm)
    (out / 'world_models.json').write_text(json.dumps(index, indent=1) + '\n')
    report['world_models'] = dict(table=index['table_address'], count=index['count'],
                                  span=index['span_bytes'], verified=index['verified'])
    report['captures'] = [c.name for c in caps]
    (out / 'tables.json').write_text(json.dumps(report, indent=1) + '\n')
    r = report
    print(f"roster: {r['roster']['placements']} placements at {r['roster']['placement_table']}, "
          f"groups {[g['address'] + 'x' + str(g['records']) for g in r['roster']['groups']]}; "
          f"spawn rows {r['spawn']['entries']} x{r['spawn']['records']}; door row {r['doors']['row']} "
          f"x{r['doors']['records']}; scripts {len(r['scripts']['chains'])} chains / "
          f"{r['scripts']['records']} records; messages {r['messages']['area_records']} area records, "
          f"bank {r['messages']['area_bank']['bytes']} bytes; world models {index['count']} "
          f"({index['span_bytes']} bytes at {index['table_address']}); {len(caps)} captures equal")
    return 0


if __name__ == '__main__':
    sys.exit(main())
