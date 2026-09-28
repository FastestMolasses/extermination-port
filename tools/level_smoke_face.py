#!/usr/bin/env python3
"""The level smoke's attachment check (001CB3C0 live: em_owner_draw_live
with em_face_attach; docs/FACE_ATTACH.md section 7, docs/LEVEL_SMOKE.md
"Face attachments").

tools/test_level_smoke.py calls check_face(ticks, state) after the phase
checks. The tick log's "owner_units" carries, per 001CAA00 call of the last
drawn frame, the face unit's bytes and digest (items 10 and 11), and
"face_units", per attached call (the ones given the attachment's regions:
Roger, and the player while a script holds its face), [record, frame, face
bytes, sample]; on sampled calls (per record the first, then every 200th,
at most 80) the sample holds the call's inputs and every byte it appended.

It checks:
  A. over the whole run: every attached call appended exactly one face unit
     (0x190 bytes, or 0x1A0 with the fog-off REF 2), whose digest the log
     holds, and nothing else faulted (a fault stops the run). Roger's
     record draws one in every frame his +0x4C ran; the player's only while
     its +0x90 holds a slot (the encounter's 001B81D0 .. 001CA770).
  B. every sampled call (quick: the first and last of each record and two
     between; EM_TEST_FULL=1: all): the ORIGINAL 001CAA00 (001CA990's body
     unit, then 001CB3C0 -> 001C7900, 001CB2C0, 001D1F80, 001D3F50 ->
     001D3E40) executes over route 14's RAM with the port's inputs patched
     in: the record's bytes, its node records' +0x90..+0xCF world matrices,
     the attachment slot (the face weights +0x40..+0x5F and the face
     resource word +0x60), the tick's point-light pool (the tick log's
     `lights`, rebuilt byte for byte), D_00810610, the cull planes context
     +0x2410, the scratchpad view-projection 0x70003AC0, context +0x0C /
     +0x9C, the rig record D_00817BC0 and D_00275688, D_00810700 / 701.
     Every byte the original writes (the bytes two runs agree on over a
     display-list window filled with a pattern and with its complement)
     equals the port's appended byte, the byte count is equal, and the face
     unit's length is equal.
"""
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as RM  # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route'
BASE = '14_roger_encounter'
FACE_BYTES = (0x190, 0x1A0)
ROGER = 0x7A8830


def u32(b, a):
    return struct.unpack_from('<I', b, a)[0]


def patched(sample, pool):
    """Route 14's RAM and scratchpad with the sample's inputs patched in."""
    ram = bytearray((ROUTE / BASE / 'eeMemory.bin').read_bytes())
    spr = bytearray((ROUTE / BASE / 'scratchpad.bin').read_bytes())
    record = sample['record']
    rec = bytes.fromhex(sample['record_bytes'])
    ram[record:record + len(rec)] = rec
    nodes = bytes.fromhex(sample['nodes'])
    count = rec[0x0C]
    assert len(nodes) == 64 * count, ('face sample: node matrices', hex(record), len(nodes), count)
    for k in range(count):
        node = u32(rec, 0x110 + 4 * k)
        ram[node + 0x90:node + 0xD0] = nodes[64 * k:64 * k + 64]
    if sample['slot_address']:
        slot = sample['slot_address']
        ram[slot:slot + 0xD0] = bytes.fromhex(sample['slot'])
    ctx = u32(ram, 0x275670)
    ram[ctx + 0x210:ctx + 0x2220] = pool
    ram[0x810610:0x810650] = bytes.fromhex(sample['view_810610'])
    ram[ctx + 0x2410:ctx + 0x2450] = bytes.fromhex(sample['planes_2410'])
    struct.pack_into('<I', ram, ctx + 0x0C, sample['ctx_0C'])
    struct.pack_into('<I', ram, ctx + 0x9C, sample['ctx_9C'])
    rig = bytes.fromhex(sample['rig'])
    ram[0x817BC0:0x817BC0 + len(rig)] = rig
    struct.pack_into('<I', ram, 0x275688, sample['rig_word'])
    ram[0x810700], ram[0x810701] = sample['area']
    spr[0x3AC0:0x3B00] = bytes.fromhex(sample['vp_3AC0'])
    return bytes(ram), bytes(spr)


def original_unit(ram, spr, owner, invert):
    """The ORIGINAL 001CAA00(owner) over a display-list window filled with a
    byte pattern (its complement when `invert`, so every byte the call does
    not write differs between the two runs): the appended bytes."""
    import test_owner_draw_reference as tod
    o = tod.EE(tod.ELF, ram, spr)
    ctx = u32(ram, 0x275670)
    o.put32(ctx + 0x10, tod.CAP_DL)
    o.put(tod.CAP_DL, bytes(((i * 29 + 7) & 0xFF) ^ (0xFF if invert else 0) for i in range(tod.CAP_DL_SIZE)))
    for a, v in ((0x275B48, owner), (0x275B44, owner), (0x275B40, owner + 0x110)):
        o.put32(a, v)
    o.run(tod.DRAW, (owner,))
    used = o.load(ctx + 0x10) - tod.CAP_DL
    return o.read(tod.CAP_DL, used)


def face_part(unit):
    """The byte offset where 001CB3C0's face unit starts in a 001CAA00
    run's bytes (its colour CNT: the one followed by one node CNT and
    001CB2C0's weights CNT, VIF UNPACK to 0x3F3), or None."""
    q, starts = 0, []
    while q < len(unit):
        w0 = u32(unit, q)
        tag_id, qwc = (w0 >> 28) & 7, w0 & 0xFFFF
        if tag_id == 1 and qwc == 5 and u32(unit, q + 28) == 0x6C0403F5:
            starts.append(q)
        if tag_id == 1 and qwc == 3 and u32(unit, q + 28) == 0x6C0203F3:
            return starts[-1] if starts else None
        q += 16 + (16 * qwc if tag_id == 1 else 0)
    return None


def sample_check(item):
    """B for one sample: (tick index, record, sample, pool)."""
    tick, record, sample, pool = item
    sample = dict(sample, record=record)
    ram, spr = patched(sample, pool)
    a = original_unit(ram, spr, record, False)
    b = original_unit(ram, spr, record, True)
    port = bytes.fromhex(sample['unit'])
    where = ('face sample', tick, hex(record))
    assert len(a) == len(b) == len(port), (where, 'appended bytes', len(port), len(a), len(b))
    written = [k for k in range(len(a)) if a[k] == b[k]]
    bad = [k for k in written if port[k] != a[k]]
    assert not bad, (where, 'the port\'s byte differs from the original\'s at', hex(bad[0]),
                     port[bad[0] & ~15:(bad[0] & ~15) + 16].hex(), a[bad[0] & ~15:(bad[0] & ~15) + 16].hex())
    start = face_part(a)
    assert start is not None, (where, 'the original appended no face unit')
    assert len(a) - start == sample['face_bytes'], (where, 'face unit bytes', len(a) - start, sample['face_bytes'])
    return len(a), len(a) - start, len(written)


def check_face(ticks, state):
    import test_level_smoke as tls
    import test_owner_draw_reference as tod
    if not tod.ELF:
        tod.ELF = (DECOMP / 'config/SCUS_971.12').read_bytes()
    # A: every attached call's face unit.
    calls, faces, per_record, samples, seen = 0, 0, {}, [], set()
    for i, t in enumerate(ticks):
        rows = t.get('face_units') or []
        units = {u[0]: u for u in t.get('owner_units') or []}
        for record, frame, face_bytes, sample in rows:
            if (record, frame) in seen:
                continue          # a tick without a drawn frame logs the last drawn frame's calls again
            seen.add((record, frame))
            calls += 1
            assert face_bytes in FACE_BYTES, ('face units: an attached call without its face unit', t['tick'],
                                             hex(record), face_bytes)
            u = units.get(record)
            assert u and len(u) >= 12 and u[10] == face_bytes and u[11], \
                ('face units: the owner log lacks the face unit', t['tick'], hex(record), u)
            faces += 1
            per_record[record] = per_record.get(record, 0) + 1
            if sample:
                pool, _ = tls.port_light_pool(t)
                assert pool is not None, ('face units: no point-light pool at a sampled tick', t['tick'])
                sample = dict(sample, face_bytes=face_bytes)
                samples.append((t['tick'], record, sample, pool))
    if not calls:
        return
    assert ROGER in per_record, ('face units: Roger drew no face unit in the run', sorted(map(hex, per_record)))
    # B: the sampled calls through the ORIGINAL 001CAA00.
    chosen = samples if RM.FULL else _quick(samples)
    results = RM.parallel_map(sample_check, chosen)
    by = {}
    for (_, record, _, _), (used, face, written) in zip(chosen, results):
        by.setdefault(record, []).append((used, face))
    print(f'face units: PASS ({faces} face units over {calls} attached 001CAA00 calls: '
          + ', '.join(f'{hex(r)} {n}' for r, n in sorted(per_record.items()))
          + f'; {len(chosen)} of {len(samples)} sampled calls re-executed by the ORIGINAL 001CAA00 + 001CB3C0 '
          f'over route 14\'s RAM with the port\'s inputs, every written byte equal: '
          + ', '.join(f'{hex(r)} {len(v)} (units {sorted({u for u, _ in v})}, faces {sorted({f for _, f in v})})'
                      for r, v in sorted(by.items())) + ')')


def _quick(samples):
    """The first and last sample of each record and two between."""
    out = []
    for record in sorted({s[1] for s in samples}):
        mine = [s for s in samples if s[1] == record]
        keep = {0, len(mine) - 1, len(mine) // 3, 2 * len(mine) // 3}
        out += [mine[k] for k in sorted(keep) if 0 <= k < len(mine)]
    return out
