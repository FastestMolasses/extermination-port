#!/usr/bin/env python3
"""Shared inputs of the AREA13X asset exporters (docs/AREA13X_ASSETS.md):
AREA13 sub 0 over the later AREA13 captures.

The AREA13 lane (export_area13_common / _level / _tables / _sfx,
docs/AREA13_ASSETS.md) exported AREA13 sub 0 from the a13 group (the
arrival a04b_04 and a13_00 .. a13_04). AREA13 was loaded a second time when
the tenth level came back up AREA19's ladder (a13b_00, from AREA19); the
a13b group and the eleventh level's a13c group (the south field) run in
that second load. Every capture of both groups that ends in AREA13 is this
lane's (13, ../Extermination/build/s87/, ignored):

  route_a13b/a13b_00_ladder_up .. a13b_04_lift_call, a13b_s0_roof_ladder
  route_a13c/a13c_00_recharger .. a13c_06_south

(a13b_05 ends in AREA04 and is excluded.) Measured, not assumed: all 13
hold area bytes 0D 00 xx, the AREA13 module (MWo3 id 10) resident, sub 0,
and D_0028A73C = 0x133C1C0, 0x1B80 above the a13 group's 0x133A640; the a13c
group loads no other sub and no other block (AREA13's descriptor is flat:
one resident region, no nested block), so it shares the a13b load map.

The AREA13 lane's modules are imported and run UNCHANGED; `install()`
re-points their AREA13 target and adds what the later captures need:
  * export_area13_common: AREA13.out -> SCRATCH/tree/sub0, OUT ->
    SCRATCH/tree, SCRATCH -> build/area13x/assets/scratch, capture_paths ->
    the a13b and a13c folders (all_captures keeps the 13 above);
  * export_area13_level.PREVIOUS['area13'] -> route_a19/a19_02_duct_back
    (the last capture before the second load; D_0028A5A4 = 0x1980000 there
    as well);
  * export_area13_level.flag_calls -> `flag_calls` below ([47]'s 0019C6F0
    pairs with D_008107F4 bit 0x40 set, from its byte-identical C);
  * export_area13_level.verify_directory -> `verify_directory` below (the
    AREA13 lane's rule plus the knocked-drum orphan proof);
  * export_area13_tables.explicit_model_nodes / explicit_model_problems
    -> the functions below: [45]'s model after its sequence (it sets model
    0x12 without changing +0x0D or the bone count).
The full export is written to the scratch tree; export_area13x_split.py
then keeps what differs from the a13 group's export (assets/area13/) under
assets/area13/reload/ (OUT here).

Nothing here is original data: every byte is read from the user's own files
(the pinned ELF and overlay, the extracted chunk17 files, the disc image)
and checked against the captures.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area13_common as A13  # noqa: E402  (first: it keeps AREA01's nested load-map builder)
import export_area13_level as LV  # noqa: E402
import export_area13_tables as TB  # noqa: E402

C = A13.C
L, E02, E06 = LV.L, LV.E02, LV.E06
ROOT, DECOMP = A13.ROOT, A13.DECOMP
A13_TREE = A13.OUT                                 # the a13 group's export (assets/area13), read only
OUT = ROOT / 'assets/area13/reload'                # what differs from it (export_area13x_split.py)
SCRATCH = ROOT / 'build/area13x/assets'
TREE = SCRATCH / 'tree'                            # the full export over the later captures
ROUTE_A13B = DECOMP / 'build/s87/route_a13b'
ROUTE_A13C = DECOMP / 'build/s87/route_a13c'
PREVIOUS = DECOMP / 'build/s87/route_a19/a19_02_duct_back'
# the AREA13 captures of the second load, in route order (each checked by
# all_captures: area byte 0x0D, sub 0, overlay id 10 resident)
CAPTURE_NAMES = ('a13b_00_ladder_up', 'a13b_01_door17', 'a13b_02_button15', 'a13b_03_door8',
                 'a13b_04_lift_call', 'a13b_s0_roof_ladder', 'a13c_00_recharger', 'a13c_01_to_machine',
                 'a13c_02_battery', 'a13c_03_blast', 'a13c_04_cure', 'a13c_05_boom', 'a13c_06_south')
EXCLUDED = ('a13b_05_lift_ride',)
CURSOR = 0x133C1C0                                 # D_0028A73C of the second load (measured)
FIRST_CURSOR = 0x133A640                           # the a13 group's (AREA13_ASSETS.md)
FLAG_OWNER = 0x8293A0                              # [47] (func_overlay_AREA13_00829360.c)
PLACEMENTS = 0x82D570                              # D_0024D7C0[0x0D][0]
DRUM = LV.DRUM                                     # 0x156620
_LANE_FLAG_CALLS = LV.flag_calls                   # the AREA13 lane's rule (bit 0x40 clear)
_LANE_VERIFY_DIRECTORY = LV.verify_directory
_LANE_EXPLICIT_NODES = TB.explicit_model_nodes
_LANE_EXPLICIT_PROBLEMS = TB.explicit_model_problems
# [45] 0x827150 (func_overlay_AREA13_00827110.c, byte-identical): spawned
# with +0x0D = REC[4] = 0x11 (D_00810774 != 0xFF at its state 0), its state
# 1 sub-state +5 = 3 reloads the record's second pose and sets the model
# func_001CA6E0(self, 001C6120(D_0028A59C, 0x12)) without changing +0x0D,
# and steps +5 to 4, where it stays (only that case stores 4). So a node in
# state 1 with +0x0D = 0x11 and +5 = 4 carries model 0x12. 001CA6E0
# (func_001CA5E0: +0x44 and the +0x4C handler) leaves the bone count +0x0C,
# and 001C62C0 (bone_init_default_1.c) walks the old count: +0x0C stays the
# +8 of the spawn binding's model 001C6120(table, +0x0D).
SEQUENCE_OWNER, SEQUENCE_MODEL = 0x827150, 0x12


def capture_paths():
    """The a13b and a13c capture folders in route order."""
    out = []
    for route, prefix in ((ROUTE_A13B, 'a13b_*'), (ROUTE_A13C, 'a13c_*')):
        out += sorted(p for p in route.glob(prefix) if (p / 'eeMemory.bin').exists())
    order = {n: i for i, n in enumerate(CAPTURE_NAMES + EXCLUDED)}
    return sorted(out, key=lambda p: (order.get(p.name, len(order)), p.name))


def flag_calls(ram):
    """The 0019C6F0 calls that decide the uid words, AREA13 [47] 0x8293A0
    with D_008107F4 bit 0x40 SET (func_overlay_AREA13_00829360.c,
    byte-identical). State 0 with the bit clear sets +0x0D = REC[0x2C] and
    calls (0x1F, 1), (0x20, 0); with it set it keeps +0x0D (001B6990 copied
    REC[4]) and calls (0x1F, 0), (0x20, 1); either way +0x28 = 0x6E0. State
    1 counts +0x28 down while the bit is set and, once it is 0 with +0x0D =
    0x13, restores REC[4] and calls (0x1F, 0), (0x20, 1). With REC[0x2C] =
    0x13 and REC[4] != 0x13 (both read from the record in RAM):
      * +0x0D = REC[0x2C] and +0x28 != 0: state 0 ran with the bit clear
        and the restore has not run: (0x1F, 1), (0x20, 0) last;
      * +0x0D = REC[4]: the restore ran, or state 0 ran with the bit set:
        (0x1F, 0), (0x20, 1) last.
    Anything else (another state, count, a record breaking the premises,
    +0x0D = 0x13 with +0x28 = 0): ValueError. With the bit clear: the AREA13
    lane's own rule, unchanged."""
    t = A13.current()
    if t is not A13.AREA13 or ram[0x810700] != t.area or ram[0x810701] != t.sub or not ram[0x8107F4] & 0x40:
        return _LANE_FLAG_CALLS(ram)
    nodes = [a for _s, a in E02.pool(ram) if C.u32(ram, a + 0x10) == FLAG_OWNER]
    if len(nodes) != 1 or ram[nodes[0] + 4] != 1:
        raise ValueError(f'[47] (0x8293A0) is not one live node in state 1 (bit 0x40 set): '
                         f'{[(hex(a), ram[a + 4]) for a in nodes]}')
    a = nodes[0]
    rec = PLACEMENTS + 0x28 * ram[a + 0x9A]
    model, first, second, count = ram[a + 0x0D], ram[rec + 4], ram[rec + 0x2C], C.s16(ram, a + 0x28)
    if second != 0x13 or first == 0x13:
        raise ValueError(f'[47] record {rec:#x}: +4 {first:#x}, +0x2C {second:#x} (the rule needs 0x13 only at +0x2C)')
    if model == second and count != 0:
        return ((0x1F, 1), (0x20, 0))
    if model == first:
        return ((0x1F, 0), (0x20, 1))
    raise ValueError(f'[47] +0x0D {model:#x} with +0x28 {count}: not a state its C reaches with the bit set')


def explicit_model_nodes(ram):
    """export_area13_tables.explicit_model_nodes (the opened hatch) plus
    [45] in state 1 with +0x0D = 0x11 and +5 = 4 (SEQUENCE_OWNER above): the
    model owners whose +0x44 is not 001C6120(table, +0x0D)."""
    out = [(s, a) for s, a in _LANE_EXPLICIT_NODES(ram) if C.u32(ram, a + 0x10) != SEQUENCE_OWNER]
    out += [(s, a) for s, a in TB.E02._POOL_NODES(ram)
            if C.u32(ram, a + 0x10) == SEQUENCE_OWNER and ram[a + 4] == 1 and ram[a + 0x0D] == 0x11
            and ram[a + 5] == 4]
    return out


def explicit_model_problems(ram, name):
    """export_area13_tables.explicit_model_problems for the lane's nodes,
    and for [45] after its sequence: +0x44 = 001C6120(table, 0x12), +0x0C
    = the +8 of 001C6120(table, +0x0D) and every one of those bone slots
    set."""
    saved, TB.explicit_model_nodes = TB.explicit_model_nodes, _LANE_EXPLICIT_NODES
    try:
        out = _LANE_EXPLICIT_PROBLEMS(ram, name)           # the hatch, by the lane's own rule
    finally:
        TB.explicit_model_nodes = saved
    table = C.u32(ram, 0x28A59C)
    model = lambda ident: table + (C.s32(ram, table + 4 + 4 * ident) >> 2 << 2)
    for slot, a in explicit_model_nodes(ram):
        if C.u32(ram, a + 0x10) != SEQUENCE_OWNER:
            continue
        if C.u32(ram, a + 0x44) != model(SEQUENCE_MODEL):
            out.append(f'{name} slot {slot}: +0x44 != 001C6120(table, {SEQUENCE_MODEL:#x})')
        elif ram[a + 0x0C] != C.u32(ram, model(ram[a + 0x0D]) + 8) or not all(
                C.u32(ram, a + 0x110 + 4 * k) for k in range(ram[a + 0x0C])):
            out.append(f'{name} slot {slot}: bone count / slots (the spawn binding\'s)')
    return out


def node_image(cap, uid, behaviour):
    """[node address] of the live nodes of `behaviour` carrying `uid` in `cap`."""
    out = []
    for slot in range(L.POOL_SLOTS):
        a = L.POOL_BASE + slot * L.POOL_STRIDE
        if cap.ram[a] and cap.ram[a + 0x0F] == uid and C.u32(cap.ram, a + 0x10) == behaviour:
            out.append(a)
    return out


def knocked_drum_hull(elf, disc, cap, uid, hulls, donor, node):
    """The hull bytes the ORIGINAL 001A2370 writes for the drum `node` (its
    pool record copied from `donor`, the last capture where it was live,
    at rest) in `cap`'s RAM over the directory `disc`, with only its matrix
    +0xD0's translation row changed: set to (centre x, min y, centre z) of
    `cap`'s own hull. b'' when the call touched another hull.

    What this shows (measured on the ORIGINAL): 001A2370 moves a drum hull's
    box centre by the matrix (centre = M * (0, 6.004, 0) for these drums)
    and keeps the half extents and the other words; a tilted matrix with
    another translation gives the same bytes. So the translation read from
    the hull is a representative, not the drum's final position, and the
    proof covers the other 15 of the hull's 18 words (the box, its flags
    and layout are the drum record's, only moved)."""
    from test_coll_move_reference import FloatEE
    table = C.u32(cap.spad, L.SPAD_CELLS)
    s, e, _f = hulls[uid]
    hull = cap.ram[table + s:table + e]
    cx, cz = struct.unpack_from('<f', hull, 0x20)[0], struct.unpack_from('<f', hull, 0x28)[0]
    miny = struct.unpack_from('<f', hull, 4)[0]
    ram = bytearray(cap.ram)
    ram[node:node + L.POOL_STRIDE] = donor.ram[node:node + L.POOL_STRIDE]
    struct.pack_into('<3f', ram, node + 0x100, cx, miny, cz)
    ee = FloatEE(elf, bytes(ram), cap.spad)
    ee.write(table, disc)
    ee.call(L.RETRANSFORM, (node, node + 0xD0))
    got = ee.read(table, len(disc))
    return got[s:e] if got[:s] + got[e:] == disc[:s] + disc[e:] else b''


def knocked_drum_proof(elf, disc, cap, uid, hulls, earlier):
    """(node, donor name) when `uid` is the hull of a drum 0x156620 that was
    live at rest (state 1) in the most recent earlier capture of the run
    with a live node of that uid, and knocked_drum_hull reproduces every
    byte of the capture's hull; else None."""
    table = C.u32(cap.spad, L.SPAD_CELLS)
    s, e, _f = hulls[uid]
    want = cap.ram[table + s:table + e]
    for donor in reversed(earlier):
        if any(donor.ram[a] and donor.ram[a + 0x0F] == uid for _s, a in E02.pool(donor.ram)) and \
                not node_image(donor, uid, DRUM):
            return None                                  # the uid's last owner is not a drum
        nodes = node_image(donor, uid, DRUM)
        if not nodes:
            continue
        if len(nodes) != 1 or donor.ram[nodes[0] + 4] != 1:
            return None
        if knocked_drum_hull(elf, disc, cap, uid, hulls, donor, nodes[0]) != want:
            return None
        return nodes[0], donor.name
    return None


def verify_directory(elf, disc, caps):
    """export_area13_level.verify_directory's rules (the uid words through
    the ORIGINAL 0019C6F0 with flag_calls, the hulls through the ORIGINAL
    001A2370 / 0x219F50, an orphan equal to a derivation of it in another
    capture of the run), plus one orphan proof the later captures need: a
    knocked drum. a13c_03 .. a13c_06 hold hulls 7, 9 and 13 moved and no
    live node of them; in a13c_02 each was a live drum 0x156620 at rest
    (state 1). Its C (src/func_00156620.c, NEARMISS) re-transforms the hull
    with 001A2370(node, node + 0xD0) every tick of its knocked state 2 and
    frees the node in state 3 (001AFC10), so the hull keeps the last
    transform. The drum's final matrix is not recorded anywhere (the freed
    slots are reused), so the proof reads the translation from the hull
    itself and shows the rest: knocked_drum_proof. Returns (rows, problems,
    calls)."""
    LV.install()
    count, hulls, size = L.cell_directory(disc, 0)
    rows, problems, calls, derived, pending = [], [], {}, {}, []
    for k, cap in enumerate(caps):
        table = C.u32(cap.spad, L.SPAD_CELLS)
        if size != len(disc) or C.s16(cap.spad, L.SPAD_CELL_COUNT) != count:
            problems.append(f'{cap.name}: directory count/size')
            continue
        if not L.retransform_code_equal(elf, cap.ram) or not all(
                cap.ram[a:a + n] == elf[a - C.ELF_VADDR + C.ELF_OFFSET:a - C.ELF_VADDR + C.ELF_OFFSET + n]
                for a, n in E06.SCALED_CODE):
            problems.append(f'{cap.name}: 001A2370 / 0x219F50 code in RAM differs from the ELF')
            continue
        try:
            image, used = E02.derive_words(elf, disc, cap)
        except ValueError as error:
            problems.append(str(error))
            continue
        calls[cap.name] = used
        img = cap.ram[table:table + size]
        expected, proofs = L.derive_cell_image(elf, image, cap, hulls)
        expected = bytearray(expected)
        for uid, (kind, node, behaviour) in sorted(proofs.items()):
            s, e, _f = hulls[uid]
            if kind == 'underived':
                for n in [a for _s, a in E02.pool(cap.ram) if cap.ram[a + 0x0F] == uid
                          and C.u32(cap.ram, a + 0x10) == LV.SCALED_OWNER]:
                    hull = E06.derive_scaled_hull(elf, image, cap, n, hulls)
                    if hull and hull == img[s:e]:
                        expected[s:e] = hull
                        kind, node, behaviour = proofs[uid] = ('scaled', n, LV.SCALED_OWNER)
                        break
            if kind in ('derived', 'scaled'):
                derived.setdefault(uid, set()).add(img[s:e])
            elif kind == 'orphan':
                pending.append((k, cap, uid, img[s:e], image))
                expected[s:e] = img[s:e]                               # proven below
            else:
                problems.append(f'{cap.name}: hull {uid} differs and no owner derivation reproduces it')
        bad = [j for j in range(size) if expected[j] != img[j]]
        if bad:
            problems.append(f'{cap.name}: directory bytes differ at {[hex(j) for j in bad[:8]]}')
        rows.append(dict(capture=cap.name, moved_hulls=sorted(proofs),
                         proofs={u: (f'001A2370(node {n:#x}, behaviour {b:#x})' if kd == 'derived' else
                                     f'0x219F50(node {n:#x})' if kd == 'scaled' else kd)
                                 for u, (kd, n, b) in sorted(proofs.items())}))
    for k, cap, uid, data, image in pending:
        row = next(r for r in rows if r['capture'] == cap.name)
        if data in derived.get(uid, set()):
            row['proofs'][uid] = 'orphan: equal to a derivation of it in another capture'
            continue
        drum = knocked_drum_proof(elf, image, cap, uid, hulls, caps[:k])
        if drum is not None:
            row['proofs'][uid] = (f'knocked drum: 001A2370(node {drum[0]:#x} as in {drum[1]}) with the '
                                  'translation read from the hull')
            continue
        problems.append(f'{cap.name}: hull {uid} differs, no live node owns it, and it equals no derivation of '
                        'it in another capture and no knocked drum')
    return rows, problems, calls


def install():
    """Re-point the AREA13 lane's AREA13 target at the later captures and
    the scratch tree (idempotent). Returns export_area01_common configured
    for AREA13."""
    A13.AREA13.out = TREE / 'sub0'
    A13.OUT = TREE
    A13.SCRATCH = SCRATCH / 'scratch'
    A13.capture_paths = capture_paths
    LV.PREVIOUS['area13'] = PREVIOUS
    LV.flag_calls = flag_calls
    LV.verify_directory = verify_directory
    TB.explicit_model_nodes = explicit_model_nodes
    TB.explicit_model_problems = explicit_model_problems
    C_ = A13.configure(A13.AREA13)
    LV.install('area13')
    return C_


install()
