#!/usr/bin/env python3
"""export_world_models.py - the AREA11 world model bank (*D_0028A59C).

The AREA11 world owners allocate their models with 001B0EA0:
001C6120(*D_0028A59C, owner +0x0D) = table + (word[1 + id] >> 2 << 2), then
001CA6E0 stores that address at owner +0x44. The draw (001CAA00 -> 001CA990
-> 001C7420 / 001CA940) reads the model's header (+0x04 block qwords, +0x08
bones, +0x0C skeleton offset, +0x20 radius), its skeleton records (0x50 bytes
each) and REFs its block data at +0x40 (docs/OWNER_DRAW.md).

Source (the user's own extracted disc; nothing is embedded here): the
chunk15 files concatenated in index order (the engine loads f05_id97 onward
contiguously; export_props.py's "chunk15 concat view"), with the table 0x123000
bytes in. The table and all 21 models it indexes form one contiguous span.

Spawn records: the AREA11 overlay's placement table (extract/OVERLAY/
AREA11.BIN at original address 0x0082A3C0, 40-byte records, halfword +0x04 =
the model id, word +0x24 = the behaviour) names the id of every placed owner.
Owners spawned at run time (the husk pair) are found in the captures.

--verify-ram (repeatable; defaults to playable_ee.bin and every AREA11 route
capture that exists)
checks, per captured EE RAM image:
  * D_0028A59C == the exported table address (the RAM placement the REF
    targets and 001C6120 handles depend on);
  * the whole exported span equals RAM from D_0028A59C, byte for byte;
  * every owner in the actor list (head D_00275BC0, link +0x1C) whose +0x44 is
    a bank model: +0x44 == 001C6120(D_0028A59C, +0x0D) (001B0EA0's lookup),
    +0x0C == the model's bone count, and every +0x110 slot below it is set;
  * every placed owner's record id equals its captured +0x0D.
The run fails unless the crates (001551B0), drums (00156620), fan (00827630),
truck (00823FF0), elevator (00827B10) and husks (00825940, 00827490) were all
seen bound to bank models.

Output (disc-derived: git-ignored assets/ only):
  assets/scene_snow/world_models.emwm, little-endian, read by
  em_world_models_parse (src/game/em_owner_draw_original.c):
    0x00 'EMWM', u32 version 1, u32 table address, u32 span bytes S,
    u32 table word 0 (model count), 3 x u32 0
    0x20 S original bytes from the table address
  assets/scene_snow/world_models.json: the index (per id: offset, address,
  blocks, bones, bytes, radius bits, the owners seen) and the verification
  summary. Counts, ids and hashes only.

Runs natively on arm64 macOS (pure Python).

Usage (port root):
  python3 tools/export_world_models.py
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
TABLE_OFFSET = 0x123000            # the table inside the chunk15 concatenation
TABLE_ADDRESS = 0x01335F40         # *D_0028A59C in every AREA11 capture (checked)
D_0028A59C, ACTOR_HEAD = 0x28A59C, 0x275BC0
BLOCK_QWORDS = 0x82
BLOCK_HEAD = (0, 0, 0x01000404, 0x6C808000)   # STCYCL 4,4; UNPACK V4-32 128 at 0 (+TOPS)
BLOCK_TAIL = (0x14000000, 0x17000000)          # MSCAL 0 (first block), MSCNT
PLACEMENT, OVERLAY_ARENA, RECORD = 0x0082A3C0, 0x00823500, 40
REQUIRED = {0x001551B0: 'crate', 0x00156620: 'drum', 0x00827630: 'fan', 0x00823FF0: 'truck',
            0x00827B10: 'elevator', 0x00825940: 'husk creature', 0x00827490: 'husk partner'}


def u32(b, a): return struct.unpack_from('<I', b, a)[0]
def s32(b, a): return struct.unpack_from('<i', b, a)[0]


def model_record(data: bytes, off: int, ident: int):
    """(blocks, qwc, bones, skeleton offset, bytes, radius bits) of the model
    at `off`, checking its header and every block's VIF codes."""
    blocks, qwc, bones, skel = struct.unpack_from('<4I', data, off)
    if blocks < 1 or qwc != blocks * BLOCK_QWORDS or bones > 0xFF or skel != 0x40 + 16 * qwc:
        raise SystemExit(f'model {ident:#04x}: header {blocks}/{qwc:#x}/{bones}/{skel:#x} '
                         'is not a block model')
    for b in range(blocks):
        blk = off + 0x40 + 16 * BLOCK_QWORDS * b
        head = struct.unpack_from('<4I', data, blk)
        tail = struct.unpack_from('<4I', data, blk + 16 * (BLOCK_QWORDS - 1))
        if head != BLOCK_HEAD or tail != (BLOCK_TAIL[b > 0], 0, 0, 0):
            raise SystemExit(f'model {ident:#04x} block {b}: VIF codes are not '
                             'STCYCL + UNPACK 128 + MSCAL/MSCNT')
    return blocks, qwc, bones, skel, skel + 0x50 * bones, u32(data, off + 0x20)


def build(extract: Path):
    files = sorted((extract / 'chunk15').glob('*.bin'))
    if not files:
        raise SystemExit(f'{extract}/chunk15: no extracted files')
    cat = b''.join(f.read_bytes() for f in files)
    count = u32(cat, TABLE_OFFSET)
    if not 0 < count < 64:
        raise SystemExit(f'table word 0 = {count}: not the AREA11 model table')
    models = []
    for ident in range(count):
        off = s32(cat, TABLE_OFFSET + 4 + 4 * ident) >> 2 << 2
        if off < 4 + 4 * count:
            raise SystemExit(f'model {ident:#04x}: offset {off:#x} inside the table')
        blocks, qwc, bones, skel, size, radius = model_record(cat, TABLE_OFFSET + off, ident)
        models.append(dict(id=ident, offset=off, blocks=blocks, qwc=qwc, bones=bones,
                           skeleton=skel, bytes=size, radius_bits=radius))
    span = max(m['offset'] + m['bytes'] for m in models)
    ordered = sorted(models, key=lambda m: m['offset'])
    for a, b in zip(ordered, ordered[1:]):
        if a['offset'] + a['bytes'] > b['offset']:
            raise SystemExit(f"models {a['id']:#04x} and {b['id']:#04x} overlap")
    return dict(count=count, models=models, span=cat[TABLE_OFFSET:TABLE_OFFSET + span])


def placements(overlay: bytes):
    """(index, record address, behaviour, model id) of the placement table."""
    out = []
    at = PLACEMENT - OVERLAY_ARENA
    for i in range(256):
        rec = at + RECORD * i
        if rec + RECORD > len(overlay) or overlay[rec] == 0xFF:
            break
        out.append((i, PLACEMENT + RECORD * i, u32(overlay, rec + 36),
                    struct.unpack_from('<H', overlay, rec + 4)[0]))
    return out


def verify_ram(x, ram: bytes, table: int, placed, seen):
    if u32(ram, D_0028A59C) != table:
        raise SystemExit(f'D_0028A59C = {u32(ram, D_0028A59C):#x}, not {table:#x}')
    if ram[table:table + len(x['span'])] != x['span']:
        diff = next(i for i in range(len(x['span'])) if ram[table + i] != x['span'][i])
        raise SystemExit(f'RAM differs from the exported span at +{diff:#x}')
    by_address = {table + m['offset']: m for m in x['models']}
    owners, a, visited = 0, u32(ram, ACTOR_HEAD), set()
    positions = {}
    while a and a not in visited:
        visited.add(a)
        model = u32(ram, a + 0x44)
        m = by_address.get(model)
        if m is not None:
            ident = ram[a + 0x0D]
            lookup = table + (s32(ram, table + 4 + 4 * (ident & 0x7FFF)) >> 2 << 2)
            if lookup != model:
                raise SystemExit(f'owner {a:#x}: +0x44 {model:#x} != 001C6120(table, {ident:#x})')
            if ram[a + 0x0C] != m['bones'] or not all(u32(ram, a + 0x110 + 4 * k) for k in range(m['bones'])):
                raise SystemExit(f'owner {a:#x}: bone count or slots differ from model {ident:#04x}')
            behaviour = u32(ram, a + 0x10)
            seen.setdefault(m['id'], set()).add(behaviour)
            positions[(behaviour, struct.unpack_from('<3I', ram, a + 0xB0))] = ident
            owners += 1
        a = u32(ram, a + 0x1C)
    matched = 0
    for _i, _rec, behaviour, model_id in placed:
        if model_id < x['count'] and behaviour in REQUIRED:
            ids = {v for (b, _p), v in positions.items() if b == behaviour}
            if ids and model_id not in ids:
                raise SystemExit(f'placed {behaviour:#x}: record id {model_id:#x}, captured {ids}')
            matched += bool(ids)
    return owners, matched


def serialize(x, table: int) -> bytes:
    head = struct.pack('<4s7I', b'EMWM', 1, table, len(x['span']), x['count'], 0, 0, 0)
    return head + x['span']


def default_captures():
    route = DECOMP / 'build/s87/route'
    playable = DECOMP / 'build/startup-reference/playable_ee.bin'
    return ([playable] if playable.exists() else []) + sorted(
        p for p in route.glob('*/eeMemory.bin') if not p.parent.name.startswith('15'))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--extract', type=Path, default=DECOMP / 'extract')
    ap.add_argument('--out', type=Path, default=ROOT / 'assets/scene_snow/world_models.emwm')
    ap.add_argument('--table-address', type=lambda v: int(v, 0), default=TABLE_ADDRESS)
    ap.add_argument('--verify-ram', type=Path, action='append', default=None,
                    help='captured EE RAM image (repeatable); default: every AREA11 route capture')
    ap.add_argument('--no-verify', action='store_true')
    args = ap.parse_args(argv)
    x = build(args.extract)
    placed = placements((args.extract / 'OVERLAY/AREA11.BIN').read_bytes())
    captures = [] if args.no_verify else (args.verify_ram if args.verify_ram is not None else default_captures())
    seen, report = {}, []
    for path in captures:
        owners, matched = verify_ram(x, path.read_bytes(), args.table_address, placed, seen)
        report.append(dict(capture=str(path.relative_to(DECOMP) if path.is_relative_to(DECOMP) else path),
                           owners=owners, placed_matched=matched))
    if captures:
        bound = {b for behaviours in seen.values() for b in behaviours}
        missing = [f'{b:#x} {n}' for b, n in REQUIRED.items() if b not in bound]
        if missing:
            raise SystemExit(f'never seen bound to a bank model: {missing}')
    data = serialize(x, args.table_address)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(data)
    # Placement records whose behaviour binds a bank model in the captures
    # (spawners carry a zero id field that is not a model).
    binders = {b for behaviours in seen.values() for b in behaviours} or set(REQUIRED)
    placed_ids = {}
    for _i, rec, behaviour, model_id in placed:
        if behaviour in binders:
            placed_ids.setdefault(model_id, set()).add(behaviour)
    index = dict(
        table_address=hex(args.table_address), count=x['count'], span_bytes=len(x['span']),
        span_sha256=hashlib.sha256(x['span']).hexdigest(),
        models=[dict(id=hex(m['id']), offset=hex(m['offset']), address=hex(args.table_address + m['offset']),
                     blocks=m['blocks'], bones=m['bones'], bytes=m['bytes'], radius_bits=hex(m['radius_bits']),
                     placed_behaviours=sorted(hex(b) for b in placed_ids.get(m['id'], ())),
                     captured_behaviours=sorted(hex(b) for b in seen.get(m['id'], ())))
                for m in x['models']],
        verified=report)
    args.out.with_suffix('.json').write_text(json.dumps(index, indent=1) + '\n')
    used = sorted(seen)
    print(f"wrote {args.out}: table {args.table_address:#x}, {x['count']} models, "
          f"{len(x['span'])} bytes; verified against {len(captures)} captures "
          f"({sum(r['owners'] for r in report)} owner bindings; ids bound: "
          f"{', '.join(hex(i) for i in used) or 'none'})")
    return 0


if __name__ == '__main__':
    sys.exit(main())
