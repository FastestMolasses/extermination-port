#!/usr/bin/env python3
"""Shared inputs of the AREA19S1 asset exporters (docs/AREA19S1_ASSETS.md):
AREA19 sub 1 over the thirteenth level's a19c captures.

AREA19 (area 0x13) has two subs (nested blocks chunk23.n0 / chunk23.n1 of
INDEX.IDX sector 23). Every earlier capture loads sub 0 (AREA19_ASSETS.md,
AREA19X_ASSETS.md). The thirteenth level's ladder at z 859.5 led into sub 1
(THIRTEENTH_LEVEL_ROUTE.md: request 13 01 07 01 at a19c_06 f1460); its door
[52] gave sub 1 entry 1. The two a19c captures that end in sub 1 are this
lane's (../Extermination/build/s87/route_a19c/, ignored):

  a19c_06_ladder959   area bytes 13 01 07   the foot of entry 7 (y 380)
  a19c_07_door52      area bytes 13 01 01   entry 1, after [34]'s script

a19c_00 .. a19c_05 end in AREA19 sub 0 (13 00 0A / 13 00 02) and are listed
as excluded (SUB0_CAPTURES; export_area13_common.all_captures refuses an
AREA19 capture of another sub than its target's, so they are not handed to
it). a19c_05 is the last capture before the sub-1 load (D_0028A5A4's
previous capture).

The AREA13 lane's exporters (export_area13_common / _level / _tables /
_sfx) model AREA19 through their target object export_area13_common.AREA19,
whose `sub` every load-map, roster, spawn and sound step reads. They are
imported and run UNCHANGED, through export_area19_common (imported first:
the AREA19 lane's re-pointing) and export_area19x_common (the AREA19X
lane's [43] rule, which acts on sub-0 captures only and so on none here).
`install()` re-points them at sub 1:
  * export_area13_common.AREA19: sub -> 1, out -> TREE/sub1; OUT -> TREE,
    SCRATCH -> build/area19s1/assets/scratch, capture_paths -> the two
    sub-1 folders;
  * export_area13_level.PREVIOUS['area19'] -> a19c_05_door27;
    export_area13_level.flag_calls / owner_matrix / verify_directory ->
    flag_calls / owner_matrix / verify_directory below (sub 1's 0019C6F0
    callers and 001A2370 owners, read from their overlay C, and the freed
    drums' hulls), which fall back to the AREA13 lane's functions outside
    sub 1;
  * export_area13_tables: the sub-1 roster pins (PLACEMENTS, GROUPS), the
    sub-1 spawn rows (SPAWN_SUB0, read by spawn_rows through the target's
    sub), the fire [39]'s overlay-data window (WRITERS, writer_problems
    below) and export_area02_tables.NO_MODEL_BEHAVIOURS + 0x1E3D90.
uninstall() restores the AREA19X lane's sub-0 state.
The full export is written to the scratch tree TREE; export_area19s1_split.py
then places it under assets/area19s1/ (OUT; a folder inside assets/area19/
would fail the AREA19X lane's split checks).

Nothing here is original data: every byte is read from the user's own files
(the pinned ELF and overlay, the extracted chunk23 files, the disc image)
and checked against the captures.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area19_common as A19  # noqa: E402  (first: re-points the AREA13 lane's AREA19 target)
import export_area19x_common as X19  # noqa: E402  ([43]'s rule; sub-0 captures only)
import export_area13_level as LV  # noqa: E402
import export_area13_tables as TB  # noqa: E402

A13, C = A19.A13, A19.C
E02L = LV.E02                                      # export_area02_level (derive_words reads flag_calls)
ROOT, DECOMP = A13.ROOT, A13.DECOMP
A19_TREE = A19.OUT                                 # assets/area19 (the sub-0 export), read only
OUT = ROOT / 'assets/area19s1'                     # not assets/area19/sub1 (export_area19s1_split.py)
SCRATCH = ROOT / 'build/area19s1/assets'
TREE = SCRATCH / 'tree'                            # the full export over the two sub-1 captures
ROUTE_A19C = DECOMP / 'build/s87/route_a19c'
SUB = 1
CAPTURE_NAMES = ('a19c_06_ladder959', 'a19c_07_door52')
SUB0_CAPTURES = ('a19c_00_bar840', 'a19c_01_bar841', 'a19c_02_use7', 'a19c_03_slide', 'a19c_04_walkway',
                 'a19c_05_door27')
PREVIOUS = ROUTE_A19C / 'a19c_05_door27'           # the last capture before the sub-1 load
TARGET = A13.AREA19

# Sub 1's roster (D_0024D7C0[0x13][1], D_0024D820[0x13][1]) and spawn rows
# (D_0024D650[0x13][1]): read by the original table walks
# (export_area11_roster.walk_roster, export_area13_tables.spawn_rows) from
# the pinned ELF and overlay; pinned here as the AREA13 lane pins sub 0's.
PLACEMENTS = 0x82ED60                              # 57 records
GROUPS = ((0x82A5F0, 21),)
SPAWN = (0x24D1B0, 9)
# Overlay-data words written at run time in sub 1, outside every reached
# chain record: the fire [39] 0x823D10 (func_overlay_AREA19_00823CD0.c,
# NEARMISS: the original's stores are the C's, its header says; the values
# below are checked against both captures) writes its smoke-column packets
# 0x82B000 / 0x82B090 every frame of states 1 / 2 (except at entries 2, 4,
# 5) while its size (+0x214, 1.0 from state 0, shrinking only in state 2) is
# non-zero and D_008107F9 bit 7 is clear: {address: factor} of the size (the
# last loop pass, j = 3, leaves 12.0 at +0x10 / +0x18), and 0x82B0A4 = 0.
FIRE, FIRE_LO, FIRE_HI = 0x823D10, 0x82B000, 0x82B0F0
FIRE_WORDS = {0x82B004: 15.0, 0x82B014: 10.0, 0x82B040: 8.0, 0x82B044: 8.0, 0x82B050: 8.0, 0x82B054: 8.0,
              0x82B094: 40.0, 0x82B0D0: 20.0, 0x82B0D4: 20.0, 0x82B0E0: 20.0, 0x82B0E4: 30.0}
FIRE_CONSTANTS = {0x82B010: 12.0, 0x82B018: 12.0}
WRITERS = ((FIRE_LO, FIRE_HI, FIRE),)
_BASE_WRITER_PROBLEMS = TB.writer_problems


def fire_words(disc, size):
    """The window FIRE_LO .. FIRE_HI after the fire wrote it with `size`."""
    out = bytearray(disc)
    for at, factor in FIRE_WORDS.items():
        struct.pack_into('<f', out, at - FIRE_LO, factor * size)    # size 1.0: exact in any float model
    for at, value in FIRE_CONSTANTS.items():
        struct.pack_into('<f', out, at - FIRE_LO, value)
    struct.pack_into('<I', out, 0x82B0A4 - FIRE_LO, 0)
    return bytes(out)


def writer_problems(ram, lo, hi, disc, behaviour):
    """export_area13_tables.writer_problems plus the fire [39]: the window
    holds the module bytes, or fire_words(1.0) with one live fire node in
    state 1 whose size is 1.0, D_008107F9 bit 7 clear and the entry byte not
    2, 4 or 5."""
    if behaviour != FIRE or (lo, hi) != (FIRE_LO, FIRE_HI):
        return _BASE_WRITER_PROBLEMS(ram, lo, hi, disc, behaviour)
    got = ram[lo:hi]
    if got == disc:
        return []
    if got != fire_words(disc, 1.0):
        return [f'{lo:#x}: {got.hex()} is neither the module bytes nor the fire\'s size-1.0 words']
    nodes = TB.pool_nodes(ram, FIRE)
    if len(nodes) != 1 or ram[nodes[0] + 4] != 1 or struct.unpack_from('<f', ram, nodes[0] + 0x214)[0] != 1.0 \
            or ram[0x8107F9] & 0x80 or ram[0x810702] in (2, 4, 5):
        return [f'{lo:#x}: the fire\'s words without one live fire node in state 1 of size 1.0 that draws smoke']
    return []


# Behaviours whose code neither writes nor reads node +0x44, skipped by the
# model-binding check (export_area02_tables.NO_MODEL_BEHAVIOURS, the AREA00
# rule): the fire / flash driver 0x1E3D90 (src/func_001E3D90.c, NEARMISS;
# its .s has no access to the node's +0x44 either) that group 0x82A9C0
# spawns (chain 0x82E090's 0x827B20 callback). In a19c_07 its two nodes
# occupy slots 42 / 43, whose +0x44 still holds their previous occupants'
# binding (the 0x827B60 objects' model 0x2B, as in a19c_06).
NO_MODEL = (0x1E3D90,)
_BASE_NO_MODEL = TB.E02.NO_MODEL_BEHAVIOURS

# 001A2370 callers of sub 1 that pass self + 0xD0 (their overlay C):
#   [36] 0x826C10 (func_overlay_AREA19_00826BD0.c, NEARMISS): state 0 and
#        every height step;
#   [35] 0x827430 (func_overlay_AREA19_008273F0.c, NEARMISS): state 0 and
#        whenever the linked object's height changes.
# Both NEARMISS bodies diverge only in register / scheduling choices (their
# headers); the argument (the node + 0xD0) is the same in their .s.
SELF_MATRIX_OWNERS = (0x826C10, 0x827430)
# 0019C6F0 callers of sub 1 (their overlay C), keys 7, 0x15, 8 and 5.
NOTE_34 = 0x8279E0
_BASE_FLAG_CALLS = LV.flag_calls
_BASE_OWNER_MATRIX = LV.owner_matrix


def capture_paths():
    """The two sub-1 capture folders in route order."""
    return [ROUTE_A19C / n for n in CAPTURE_NAMES if (ROUTE_A19C / n / 'eeMemory.bin').exists()]


def in_sub1(ram):
    return ram[0x810700] == TARGET.area and ram[0x810701] == SUB


def owner_matrix(ram, node):
    """export_area13_level.owner_matrix, plus sub 1's self-matrix owners
    [36] and [35] (node + 0xD0)."""
    if in_sub1(ram) and C.u32(ram, node + 0x10) in SELF_MATRIX_OWNERS:
        return node + 0xD0
    return _BASE_OWNER_MATRIX(ram, node)


def _nodes(ram, behaviour):
    return [a for _s, a in E02L.pool(ram) if C.u32(ram, a + 0x10) == behaviour]


# {owner: (key, {state: the call its last 0019C6F0 call was, or None for no
# call this load})}; a state not listed does not decide the last call.
#   [38] 0x826570 (func_overlay_AREA19_00826530.c, byte-identical): state 0
#        calls (7, 1) on its way to state 1 unless flag 0x20 or 0x23 is set;
#        state 1 calls (7, 0) only on its way to state 2. Live in state 0: no
#        call yet; in state 1: (7, 1). State 2 follows either no call or
#        (7, 0): not decided.
#   [36] 0x826C10 (func_overlay_AREA19_00826BD0.c, NEARMISS): state 0 calls
#        (0x15, 1) on its way to state 4 (D_00810779 != 0xFF); state 1 calls
#        (0x15, 0) only on its way to state 2. State 0: none; 4 and 1:
#        (0x15, 1); 2 not decided.
#   [46] 0x829A70 (func_overlay_AREA19_00829A30.c, NEARMISS): (5, 0) on
#        every way into state 2 (state 0 with D_00810838 set, state 1's Use
#        with bit 0) and no other call. States 0 and 1: none; 2: (5, 0).
FLAG_STATES = {
    0x826570: (7, {0: None, 1: (7, 1)}),
    0x826C10: (0x15, {0: None, 4: (0x15, 1), 1: (0x15, 1)}),
    0x829A70: (5, {0: None, 1: None, 2: (5, 0)}),
}
# [34] 0x8279E0 (func_overlay_AREA19_008279A0.c, byte-identical) and its
# chain 0x82E090's op09 callback 0x827B20 (func_overlay_AREA19_00827AE0.c,
# byte-identical) are the only key-8 callers, and both call (8, 1). [34]
# reaches state 3 (then 001AFC10 frees it) only after one of them ran: from
# state 0 when flag 0x46 is set (it calls (8, 1) there), or from state 1
# when chain 0x82E090 ends; that chain has no jump record and its 0x827B20
# record lies before its stop record, which sets flag 0x46 (D_0081079E) =
# 0xFF. So: live in state 0, or in state 1 with +5 0 (the chain not
# started): no call; live in state 3, or not live with D_0081079E = 0xFF:
# (8, 1); anything else (the chain running, or gone without the flag): not
# decided.
NOTE_KEY, FLAG_46 = 8, 0x81079E


def flag_calls(ram):
    """The 0019C6F0 calls of sub 1 whose last effect the capture decides,
    one per key, from FLAG_STATES and the [34] rule above. A node in a
    state that does not decide its key's last call, or two live nodes of
    one owner: ValueError. Outside sub 1: the AREA13 lane's flag_calls."""
    if not in_sub1(ram):
        return _BASE_FLAG_CALLS(ram)
    calls = []
    for owner, (key, states) in FLAG_STATES.items():
        nodes = _nodes(ram, owner)
        if len(nodes) > 1:
            raise ValueError(f'{owner:#x}: {len(nodes)} live nodes')
        for a in nodes:
            if ram[a + 4] not in states:
                raise ValueError(f'{owner:#x} node {a:#x} in state {ram[a + 4]}: its last 0019C6F0 call (key '
                                 f'{key:#x}) is not decided by the capture')
            if states[ram[a + 4]]:
                calls.append(states[ram[a + 4]])
    notes = _nodes(ram, NOTE_34)
    if len(notes) > 1:
        raise ValueError(f'{NOTE_34:#x}: {len(notes)} live nodes')
    if notes:
        state, sub = ram[notes[0] + 4], ram[notes[0] + 5]
        if state == 3:
            calls.append((NOTE_KEY, 1))
        elif not (state == 0 or (state == 1 and sub == 0)):
            raise ValueError(f'[34] {NOTE_34:#x} in state {state} (+5 {sub}): the key-8 call is not decided')
    elif ram[FLAG_46] == 0xFF:
        calls.append((NOTE_KEY, 1))
    else:
        raise ValueError(f'[34] {NOTE_34:#x} not live and D_0081079E = {ram[FLAG_46]:#x}: the key-8 call is not '
                         'decided')
    return tuple(calls)


# Hulls whose owner was freed in the same capture they moved in. The 0x827B60
# placements (func_overlay_AREA19_00827B20.c, byte-identical: [6]..[8], [20],
# [26], [28]..[33]) wait for D_0081081E == 1 (set by chain 0x82E090's op09
# callback 0x827B10, func_overlay_AREA19_00827AD0.c), count +0x9A frames down
# and in state 2 set their behaviour to 0x156620 (the drum, a 001A2370 caller
# with node + 0xD0). In a19c_07 eight of them have moved their hulls and been
# freed, their slots reused by other nodes: no live owner, no earlier
# capture with the hull moved, no matrix left to run 001A2370 with. Such a
# hull is accepted (not derived) when: the capture is in sub 1, its uid is a
# 0x827B60 placement's, no live node carries the uid, D_0081081E != 0, and
# every word that differs from the disc is a word the ORIGINAL 001A2370
# writes for that hull (export_area02_level.written_words). Its proof reads
# FREED_PROOF; the checker pins the set (docs/AREA19S1_ASSETS.md, known gaps).
DRUM_TO_BE = 0x827B60
COUNTER_46 = 0x81081E
FREED_PROOF = 'freed 0x827B60 drum: not derived (only words 001A2370 writes differ)'
_ORPHAN = 'differs, no live node owns it, and it equals no derivation of it in another capture'
_BASE_VERIFY = LV.verify_directory
_PLACEMENTS = {}


def placements(elf):
    """Sub 1's placement records (walk_roster over the pinned ELF and
    overlay), memoised."""
    if 'p' not in _PLACEMENTS:
        read = C.static_reader(elf, C.read_overlay())
        _PLACEMENTS['p'] = TB.R.walk_roster(read, TARGET.area, SUB)[2]
    return _PLACEMENTS['p']


def drum_uids(elf):
    return {rec[7] for rec in placements(elf) if C.u32(rec, 0x24) == DRUM_TO_BE}


def freed_drum_problems(elf, disc, cap, uid, hulls):
    """[] when hull `uid` of `cap` meets the freed-drum rule above."""
    if not in_sub1(cap.ram):
        return [f'{cap.name}: not an AREA19 sub-1 capture']
    if uid not in drum_uids(elf):
        return [f'{cap.name}: hull {uid} is not a 0x827B60 placement\'s']
    if any(C.u16(cap.ram, a + 0x0E) >> 8 == uid for _s, a in E02L.pool(cap.ram)):
        return [f'{cap.name}: hull {uid} has a live node']
    if not cap.ram[COUNTER_46]:
        return [f'{cap.name}: D_0081081E = 0: no 0x827B60 node has counted down']
    s, e, _f = hulls[uid]
    table = C.u32(cap.spad, LV.L.SPAD_CELLS)
    data = cap.ram[table + s:table + e]
    moved = {k for k in range(0, e - s, 4) if data[k:k + 4] != disc[s + k:s + k + 4]}
    try:
        written = E02L.written_words(elf, disc, cap, uid, hulls)
    except ValueError as error:
        return [str(error)]
    if not moved or not moved <= written:
        return [f'{cap.name}: hull {uid} words {sorted(hex(k) for k in moved - written)[:6]} are not words '
                '001A2370 writes']
    return []


def verify_directory(elf, disc, caps):
    """export_area13_level.verify_directory, then each of its unresolved
    orphan hulls re-tested by freed_drum_problems: accepted ones leave the
    problem list and carry FREED_PROOF in their row."""
    rows, problems, calls = _BASE_VERIFY(elf, disc, caps)
    _count, hulls, _size = LV.L.cell_directory(disc, 0)
    by = {c.name: c for c in caps}
    words = {}
    keep = []
    for p in problems:
        name, _sep, rest = p.partition(': hull ')
        uid = rest.split(' ', 1)[0]
        if name in by and uid.isdigit() and rest == f'{uid} {_ORPHAN}':
            cap = by[name]
            if cap.name not in words:
                try:
                    words[cap.name] = E02L.derive_words(elf, disc, cap)[0]
                except ValueError as error:
                    keep.append(str(error))
                    continue
            got = freed_drum_problems(elf, words[cap.name], cap, int(uid), hulls)
            if not got:
                row = next(r for r in rows if r['capture'] == name)
                row['proofs'][int(uid)] = FREED_PROOF
                continue
            keep += got
        keep.append(p)
    return rows, keep, calls


def install():
    """Re-point the AREA13 lane's AREA19 target at sub 1, the two a19c
    captures and the scratch tree (idempotent). Returns
    export_area01_common configured for AREA19 sub 1."""
    X19.install()                                  # the AREA19 / AREA19X lanes' re-pointing, then ours
    TARGET.sub = SUB
    TARGET.out = TREE / 'sub1'
    A13.OUT = TREE
    A13.SCRATCH = SCRATCH / 'scratch'
    A13.capture_paths = capture_paths
    LV.PREVIOUS['area19'] = PREVIOUS
    LV.flag_calls = flag_calls
    LV.owner_matrix = owner_matrix
    LV.verify_directory = verify_directory
    TB.PLACEMENTS['area19'] = PLACEMENTS
    TB.GROUPS['area19'] = GROUPS
    TB.SPAWN_SUB0['area19'] = SPAWN
    TB.WRITERS['area19'] = WRITERS
    TB.writer_problems = writer_problems
    TB.E02.NO_MODEL_BEHAVIOURS = _BASE_NO_MODEL + NO_MODEL
    C_ = A13.configure(TARGET)
    LV.install('area19')
    return C_


def uninstall():
    """Back to the AREA19X lane's sub-0 state (for checks over its exports)."""
    TARGET.sub = 0
    LV.flag_calls = _BASE_FLAG_CALLS
    LV.owner_matrix = _BASE_OWNER_MATRIX
    LV.verify_directory = _BASE_VERIFY
    TB.PLACEMENTS['area19'] = _SUB0_PINS['placements']
    TB.GROUPS['area19'] = _SUB0_PINS['groups']
    TB.SPAWN_SUB0['area19'] = _SUB0_PINS['spawn']
    TB.WRITERS['area19'] = _SUB0_PINS['writers']
    TB.writer_problems = _BASE_WRITER_PROBLEMS
    TB.E02.NO_MODEL_BEHAVIOURS = _BASE_NO_MODEL
    X19.install()
    LV.install('area19')


_SUB0_PINS = dict(placements=TB.PLACEMENTS['area19'], groups=TB.GROUPS['area19'],
                  spawn=TB.SPAWN_SUB0['area19'], writers=TB.WRITERS['area19'])

install()
