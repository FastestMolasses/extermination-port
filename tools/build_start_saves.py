#!/usr/bin/env python3
"""Build the start-point saves EM_START_AT will load (docs/SAVE_GAME.md
section 7).

A start-point save is a memory card holding one saved game made by the
ORIGINAL save writer from the state the fork recordings start an area from:
the progress block D_00810700 as it is at that area's arrival, written with
the original's own code into the original's card layout (docs/SAVE_GAME.md
section 2). Before the save, the save terminal's own step runs over the
arrival frame (00159B90's two quadword copies 00102948, original code in the
reference interpreter): the player record's +0xA0 / +0xC0 into the load pose
D_00810710 / D_00810720, which a loaded game places the player from (a tool
choice like the arrival frame: the original saves only at a terminal).
Nothing is synthesized by hand: the writer 00228A90 runs in the
reference interpreter over the recording's RAM, the card answered by the
port's host card em_memcard on a folder, and the port's translation
em_save_00228A90 runs the same save on a second folder, which must be
byte-identical (test_save_reference's harness, as test_memcard_host runs it).

Each card is then read back with the original's load code over another
recording's RAM (c10 opt_07, the options' load row): the load enumeration
002283B0 must list exactly the saved slot with its record, and LOAD GAME's
read 00228B60 must put the slot's 0x640 bytes into D_00810700; the
translations em_save_002283B0 / em_save_00228B60 run the same reads and
must equal the original at every callee entry. The block read back is the
arrival's block with the load pose, +0xF = 1 and the check byte +0x63F (the
two bytes the writer sets), and nothing else changed. A card failing any check is not
written.

The start points (the state each route recording starts from, named by the
route's manifest: its first beat's source state):

  area01           s87/route_a01's a01_00 source, AREA11's level exit:
                   the AREA01 arrival (block area 1)
  area00           s87/route_a00's a00_00 source, AREA01's level exit:
                   the AREA00 arrival (block area 0)
  area01-revisit   s87/route_a01r's a01r_00 source, AREA00's progression
                   exit: the AREA01 revisit arrival (block area 1)
  area02           s87/route_a02's a02_00 source, the revisit's door [16]
                   (a01r_03): the AREA02 arrival (block area 2)
  a02_01 .. a02_05 the source state of route_a02's beat a02_01 .. a02_05
                   (the previous beat's end, AREA02 sub 0): a loaded game
                   placed where that beat starts (discovery runs)
  a04_00 .. a04_05 the source state of route_a04's beat (AREA04's main line)
  a04b_00 .. a04b_04
                   the source state of the fork continuation's route_a04b
                   beat (AREA04's second visit; a04b_00: a22b_01's end)
  area19, a19_01, a19_02, a19b_01, a19b_02, a19c_00 .. a19c_07,
  a19d_00 .. a19d_20
                   AREA19 sub 0 and sub 1 (route_a19, route_a19b, route_a19c,
                   route_a19d): the source state of that beat
  boss             the source state of a04b_g2 in the fight's own recording
                   (tools/capture_boss_fight.py: the SHOTGUN UNIT fitted, six
                   shells, in the hall outside door [45])
  a01_03, a01_05, a01_s7, a01_07, a00_01, a00_04, a01r_01
                   the source state of that route beat: a loaded game
                   placed where the beat starts (the cutscene skip runs,
                   tools/test_cutscene_skip.py)
  area22, a22_01, a22_02, a01u, a01u_01, a01u_02, a01u_s0, area06,
  a06_01 .. a06_05, a06b, a06b_01, a01v, a01v_01, a01v_02, a22b, a22b_01
                   the middle of the route to the first boss (route_a22,
                   route_a01u, route_a06, route_a06b, route_a01v,
                   route_a22b): the source state of that beat, a loaded
                   game placed where it starts (discovery runs; the area
                   byte 0x16 is AREA22)

Output, disc-derived (the icon, the block): build/start_saves/<name>/, a
host card root (slot1/BASCUS-97112-DS00-00/ with the eight files the writer
makes, the save in EX_DATA.00 on port 0; slot2/ empty) and start.json (the
source state, its SHA-256, the block's area bytes, the load pose, each file's
SHA-256).
The folder is git-ignored (build/). An existing card is overwritten file by
file only when it differs; a folder holding anything else is refused.

Usage (native arm64 macOS, about 15 s for all three):

  python3 tools/build_start_saves.py             # every start point
  python3 tools/build_start_saves.py area01      # one
  python3 tools/build_start_saves.py --list

Needs the user's pinned ELF (tools/test_player_slide_reference.read_elf) and
the fork recordings in the reference tools/reference_root.py selects.
"""
import hashlib
import json
import os
import struct
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
os.environ.setdefault('EM_LANE', 'start_saves')
import reference_root as refs  # noqa: E402
import test_save_reference as S  # noqa: E402
import test_load_reference as L  # noqa: E402
import test_memcard_host as MH  # noqa: E402
import test_area01_render_reference as R  # noqa: E402
from test_player_slide_reference import EE, RETURN, STACK_TOP, read_elf  # noqa: E402

MASK = 0xFFFFFFFF
MC, BLOCK, SIZE = S.MC, S.BLOCK, 0x640
DIR = MH.DIR
SLOT, PORT = 0, 0                  # the save: EX_DATA.00 on port 0 (data/memcard/slot1)
LOAD_BEAT = 'o7'                   # the RAM the load runs over (c10 opt_07, the options' load row)
OUT = ROOT / 'build' / 'start_saves'
FILES = [DIR + '/' + f for f in (DIR, 'EX.ico', 'icon.sys')] + MH.SLOTS
WRITTEN = (0xF, 0x63F)             # the bytes the writer sets in the chosen slot (docs/SAVE_GAME.md 2.1)
# The save terminal's step before a save (00159B90 state 1 / sub-state 0, when
# its Use 00157CE0 answers nonzero): two quadword copies 00102948, the player
# record's position +0xA0 (D_00810350) into the load pose D_00810710 and its
# rotation +0xC0 (D_00810370) into D_00810720 (docs/SAVE_GAME.md section 7).
QCOPY = 0x102948
POSE_COPIES = ((0x810710, 0x810350), (0x810720, 0x810370))
POSE = range(0x10, 0x30)           # D_00810710..72F: the block's bytes the two copies write

# The fork recordings of AREA19's route groups (a19c_00 was recorded apart).
A19 = 'levels-recordings-20261018-10/s87/route_a19'
A19B = 'levels-recordings-20261018-13/s87/route_a19b'
A19C0 = 'levels-recordings-20261018-14/s87/route_a19c'
A19C = 'levels-recordings-20261018-17/s87/route_a19c'
A19D = 'levels-recordings-20261018-19/s87/route_a19d'

START_POINTS = {
    'area01': {'route': 's87/route_a01', 'first': 'a01_00_', 'area': 1},
    'area00': {'route': 's87/route_a00', 'first': 'a00_00_', 'area': 0},
    'area01-revisit': {'route': 's87/route_a01r', 'first': 'a01r_00_', 'area': 1},
    'area02': {'route': 's87/route_a02', 'first': 'a02_00_', 'area': 2},
    # AREA02's later beats (discovery ahead of the row-exact frontier): the
    # source state of each beat, a loaded game placed at its start.
    'a02_01': {'route': 's87/route_a02', 'first': 'a02_01_', 'area': 2},
    'a02_02': {'route': 's87/route_a02', 'first': 'a02_02_', 'area': 2},
    'a02_03': {'route': 's87/route_a02', 'first': 'a02_03_', 'area': 2},
    'a02_04': {'route': 's87/route_a02', 'first': 'a02_04_', 'area': 2},
    'a02_05': {'route': 's87/route_a02', 'first': 'a02_05_', 'area': 2},
    # The beats whose skippable scenes tools/test_cutscene_skip.py skips
    # from a loaded game (docs/AREA_SCRIPT.md "The skip path"): the source
    # state of each, a loaded game placed where that beat starts.
    'a01_03': {'route': 's87/route_a01', 'first': 'a01_03_', 'area': 1},
    'a01_05': {'route': 's87/route_a01', 'first': 'a01_05_', 'area': 1},
    'a01_s7': {'route': 's87/route_a01', 'first': 'a01_s7_', 'area': 1},
    'a01_07': {'route': 's87/route_a01', 'first': 'a01_07_', 'area': 1},
    'a00_01': {'route': 's87/route_a00', 'first': 'a00_01_', 'area': 0},
    'a00_04': {'route': 's87/route_a00', 'first': 'a00_04_', 'area': 0},
    'a01r_01': {'route': 's87/route_a01r', 'first': 'a01r_01_', 'area': 1},
    # The middle of the route to the first boss (lane C1; docs/STARTUP.md
    # "EM_START_AT=<name>"): AREA22 (SIXTH_LEVEL_ROUTE.md), AREA01's upper
    # floor and AREA06 (SEVENTH_LEVEL_ROUTE.md), and the way back over the
    # beam, the upper floor and AREA22 (EIGHTH_LEVEL_ROUTE.md). Each is the
    # source state of that beat; a group's first beat is its arrival (the
    # side beats a01u_s1 / a06_s0 start from the a01u / area06 states).
    'area22': {'route': 's87/route_a22', 'first': 'a22_00_', 'area': 0x16},
    'a22_01': {'route': 's87/route_a22', 'first': 'a22_01_', 'area': 0x16},
    'a22_02': {'route': 's87/route_a22', 'first': 'a22_02_', 'area': 0x16},
    'a01u': {'route': 's87/route_a01u', 'first': 'a01u_00_', 'area': 1},
    'a01u_01': {'route': 's87/route_a01u', 'first': 'a01u_01_', 'area': 1},
    'a01u_02': {'route': 's87/route_a01u', 'first': 'a01u_02_', 'area': 1},
    'a01u_s0': {'route': 's87/route_a01u', 'first': 'a01u_s0_', 'area': 1},
    'area06': {'route': 's87/route_a06', 'first': 'a06_00_', 'area': 6},
    'a06_01': {'route': 's87/route_a06', 'first': 'a06_01_', 'area': 6},
    'a06_02': {'route': 's87/route_a06', 'first': 'a06_02_', 'area': 6},
    'a06_03': {'route': 's87/route_a06', 'first': 'a06_03_', 'area': 6},
    'a06_04': {'route': 's87/route_a06', 'first': 'a06_04_', 'area': 6},
    'a06_05': {'route': 's87/route_a06', 'first': 'a06_05_', 'area': 6},
    'a06b': {'route': 's87/route_a06b', 'first': 'a06b_00_', 'area': 6},
    'a06b_01': {'route': 's87/route_a06b', 'first': 'a06b_01_', 'area': 6},
    'a01v': {'route': 's87/route_a01v', 'first': 'a01v_00_', 'area': 1},
    'a01v_01': {'route': 's87/route_a01v', 'first': 'a01v_01_', 'area': 1},
    'a01v_02': {'route': 's87/route_a01v', 'first': 'a01v_02_', 'area': 1},
    'a22b': {'route': 's87/route_a22b', 'first': 'a22b_00_', 'area': 0x16},
    'a22b_01': {'route': 's87/route_a22b', 'first': 'a22b_01_', 'area': 0x16},
    # AREA04's main line (FIFTH_LEVEL_ROUTE.md): the source state of each
    # beat a04_00 .. a04_05, a loaded game placed where that beat starts.
    'a04_00': {'route': 's87/route_a04', 'first': 'a04_00_', 'area': 4},
    'a04_01': {'route': 's87/route_a04', 'first': 'a04_01_', 'area': 4},
    'a04_02': {'route': 's87/route_a04', 'first': 'a04_02_', 'area': 4},
    'a04_03': {'route': 's87/route_a04', 'first': 'a04_03_', 'area': 4},
    'a04_04': {'route': 's87/route_a04', 'first': 'a04_04_', 'area': 4},
    'a04_05': {'route': 's87/route_a04', 'first': 'a04_05_', 'area': 4},
    # AREA04's second visit (EIGHTH_LEVEL_ROUTE.md, the fork continuation
    # levels-recordings-20261018-10): a04b_00 from the end of a22b_01 (door
    # [6] into AREA04 entry 1), each later one from the previous beat's end.
    'a04b_00': {'route': 'levels-recordings-20261018-10/s87/route_a04b', 'first': 'a04b_00_', 'area': 4},
    'a04b_01': {'route': 'levels-recordings-20261018-10/s87/route_a04b', 'first': 'a04b_01_', 'area': 4},
    'a04b_02': {'route': 'levels-recordings-20261018-10/s87/route_a04b', 'first': 'a04b_02_', 'area': 4},
    'a04b_03': {'route': 'levels-recordings-20261018-10/s87/route_a04b', 'first': 'a04b_03_', 'area': 4},
    'a04b_04': {'route': 'levels-recordings-20261018-10/s87/route_a04b', 'first': 'a04b_04_', 'area': 4},
    # The first boss (tools/capture_boss_fight.py): the shotgun chain's
    # reader beat, door [45] behind the player and the SHOTGUN UNIT fitted.
    'boss': {'route': 'boss_fight_20261030/s87/route_a04b', 'first': 'a04b_g2_', 'area': 4},
    # AREA19 (lane J1): the entry-9 rooms (TENTH_LEVEL_ROUTE.md: area19 is
    # a13_05's end, the foot of the ladder under AREA13's hatch [62]), entry
    # 10's side (TWELFTH_LEVEL_ROUTE.md, from a19b_00's end: the hatch [63]'s
    # arrival is lane H1's) and on through sub 1 (THIRTEENTH and
    # FOURTEENTH_LEVEL_ROUTE.md). Each is the source state of that beat.
    'area19': {'route': A19, 'first': 'a19_00_', 'area': 0x13},
    'a19_01': {'route': A19, 'first': 'a19_01_', 'area': 0x13},
    'a19_02': {'route': A19, 'first': 'a19_02_', 'area': 0x13},
    'a19b_01': {'route': A19B, 'first': 'a19b_01_', 'area': 0x13},
    'a19b_02': {'route': A19B, 'first': 'a19b_02_', 'area': 0x13},
    'a19c_00': {'route': A19C0, 'first': 'a19c_00_', 'area': 0x13},
    **{f'a19c_0{k}': {'route': A19C, 'first': f'a19c_0{k}_', 'area': 0x13} for k in range(1, 8)},
    **{f'a19d_{k:02}': {'route': A19D, 'first': f'a19d_{k:02}_', 'area': 0x13} for k in range(0, 21)},
}


class Refused(Exception):
    """A start point that cannot be built (the message says why)."""


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def image_key(name):
    return 'start:' + name


def arrival(name, points=None):
    """The recording state the start point is built from: the source state of
    its route's first beat (the manifest's), checked against that snapshot's
    own hash and against the first beat's first row (the area bytes)."""
    points = START_POINTS if points is None else points
    if name not in points:
        raise Refused(f'unknown start point {name!r} (known: {", ".join(sorted(points))})')
    p = points[name]
    route = refs.capture(p['route'])
    manifest = route / 'manifest.json'
    if not manifest.is_file():
        raise Refused(f'{name}: the recording {manifest} is missing')
    beats = json.loads(manifest.read_text())['beats']
    first = [b for b in beats if b.startswith(p['first'])]
    if len(first) != 1:
        raise Refused(f'{name}: {manifest} has {len(first)} beats named {p["first"]}* (want one)')
    beat = beats[first[0]]
    state = Path(beat['source_state']).resolve()
    folder = state.parent
    fork = refs.FORK.resolve()
    if fork not in folder.parents:
        raise Refused(f'{name}: the source state {state} is not a fork recording under {fork}')
    ee, spad, snap = folder / 'eeMemory.bin', folder / 'scratchpad.bin', folder / 'snapshot.json'
    for f in (ee, spad, snap):
        if not f.is_file():
            raise Refused(f'{name}: {f} is missing')
    ram = ee.read_bytes()
    want = json.loads(snap.read_text()).get('ee_sha256')
    if sha256(ram) != want:
        raise Refused(f'{name}: {ee} does not match its snapshot.json ee_sha256')
    block = ram[BLOCK:BLOCK + SIZE]
    if block[0] != p['area']:
        raise Refused(f'{name}: the arrival block\'s area byte is {block[0]}, not {p["area"]}')
    trace = route / first[0] / 'trace.json'
    if not trace.is_file():
        raise Refused(f'{name}: {trace} is missing')
    row0 = json.loads(trace.read_text())['rows'][0]
    if row0.get('area4') != block[:4].hex():
        raise Refused(f'{name}: the first row of {first[0]} has the area bytes {row0.get("area4")}, '
                      f'the state {block[:4].hex()}')
    return {'name': name, 'folder': folder, 'beat': first[0], 'ram': ram, 'spad': spad.read_bytes(),
            'ee_sha256': want, 'block': bytes(block)}


def at_terminal(a):
    """The arrival at a save terminal: 00159B90's two copies before its save,
    run as the original 00102948 in the reference interpreter over the
    arrival RAM. Returns the arrival with that RAM and block, and refuses
    when the copies wrote anything but the load pose or a pose other than
    the player record's +0xA0 / +0xC0 quadwords, or when the pose is not a
    placed player (the w lanes 1.0)."""
    ee = EE(read_elf(), a['ram'], a['spad'])
    for dst, src in POSE_COPIES:
        ee.r[4], ee.r[5], ee.r[29], ee.r[31] = dst, src, STACK_TOP, RETURN
        ee.run(QCOPY)
    ram = bytes(ee.mem)
    block = ram[BLOCK:BLOCK + SIZE]
    lo, hi = BLOCK + POSE.start, BLOCK + POSE.stop
    if len(ram) != len(a['ram']) or ram[:lo] != a['ram'][:lo] or ram[hi:] != a['ram'][hi:]:
        raise Refused(f'{a["name"]}: the terminal\'s copies wrote outside the load pose')
    if any(block[0x10 + 0x10 * k:0x20 + 0x10 * k] != a['ram'][src:src + 0x10]
           for k, (_dst, src) in enumerate(POSE_COPIES)):
        raise Refused(f'{a["name"]}: the load pose is not the player record\'s +0xA0 / +0xC0')
    pos, rot = struct.unpack_from('<4f', block, 0x10), struct.unpack_from('<4f', block, 0x20)
    if pos[3] != 1.0 or rot[3] != 1.0:
        raise Refused(f'{a["name"]}: the player record at the arrival is not placed '
                      f'(+0xAC {pos[3]}, +0xCC {rot[3]})')
    return dict(a, ram=ram, block=bytes(block), arrival_block=a['block'], pose=(pos[:3], rot[:3]))


def register(arrivals):
    """The arrival images in the save harness, beside its own recordings."""
    for a in arrivals:
        key = image_key(a['name'])
        S.IMAGES[key] = a['folder']
        R.IMAGES[key] = (a['ram'], a['spad'])
    S.BEATS = list(S.IMAGES)


def save(tmp, a):
    """00228A90 on both sides of the harness, each on its own host card:
    returns the oracle's card folder (test_memcard_host.save_both checks the
    two cards are byte-identical and the layout docs/SAVE_GAME.md section 2
    records)."""
    _end_ram, _end_spad, start, root = MH.save_both(tmp, 'save-' + a['name'], image_key(a['name']), SLOT, PORT,
                                                    False)
    assert start[BLOCK:BLOCK + SIZE] == a['block'], (a['name'], 'the save started from another block')
    files = MH.card_files(root, PORT)
    if sorted(files) != sorted(FILES):
        raise Refused(f'{a["name"]}: the writer made {sorted(files)}')
    slot = files[MH.SLOTS[SLOT]]
    changed = [i for i in range(SIZE) if slot[i] != a['block'][i]]
    if not set(changed) <= set(WRITTEN) or slot[0xF] != 1 or sum(slot) & 0xFF:
        raise Refused(f'{a["name"]}: the saved slot is not the arrival block with +0xF and its check byte '
                      f'(changed {[hex(i) for i in changed]})')
    return root, files, len(S.MODELS['oracle'].order)


def host_cards(root, n):
    MH.HostCard.roots, MH.HostCard.dirs = [root] * n, ()


def load(root, files, name):
    """The original's load code over the card, on both sides of the load
    harness: the enumeration lists only the saved slot; LOAD GAME's read puts
    the slot into D_00810700."""
    blk = files[MH.SLOTS[SLOT]]
    S.CARD = {}
    # the load enumeration 002283B0 from its first call
    host_cards(root, 3)                # the oracle's pre-run, then both sides
    b = L.rec(PORT) + L.helpers() + [(MC + 0x6C + n, 0xEE, 1) for n in range(5)]
    b += [(MC + 0x74 + i, 0xEE, 1) for i in range(60)]
    _ram, spad = S.make(LOAD_BEAT, {'b': b})
    frames, value = L.prerun(bytes(spad), 0x2283B0, 400)
    if value != 1:
        raise Refused(f'{name}: the original load enumeration answered {value} on the card')

    def listed(ee):
        got = [ee.load(MC + 0x6C + n, 1) for n in range(5)]
        assert got == [1 if n == SLOT else 0 for n in range(5)], (name, 'slots present', got)
        assert ee.load(MC + 0x68) == 1, (name, 'count')
        rec = bytes(ee.mem[MC + 0x74 + 12 * SLOT:MC + 0x80 + 12 * SLOT])
        assert rec[:7] + rec[8:] == blk[0:6] + blk[0xA:0xB] + blk[0x50:0x54], (name, 'slot record', rec.hex())
        return True
    entries = S.run_both(('start-load', name), bytes(spad), [('call', 0x2283B0, (MC,))] * frames,
                         check=listed)[2]
    assert S.MODELS['oracle'].order == S.MODELS['native'].order, (name, 'load command order differs')
    # LOAD GAME's read 00228B60 of the slot into D_00810700
    host_cards(root, 3)
    ram, spad = S.make(LOAD_BEAT, {'b': L.rec(PORT, SLOT) + L.helpers()})
    assert bytes(ram[BLOCK:BLOCK + SIZE]) != blk, (name, 'the load RAM already holds the block')
    frames, value = L.prerun(bytes(spad), 0x228B60, 40)
    if value != 1:
        raise Refused(f'{name}: LOAD GAME\'s read answered {value}')

    def restored(ee):
        assert bytes(ee.mem[BLOCK:BLOCK + SIZE]) == blk, (name, 'D_00810700 is not the saved slot')
        assert S.cstr(ee.mem, L.PATH) == MH.SLOTS[SLOT], (name, 'the path')
        return True
    entries += S.run_both(('start-read', name), bytes(spad), [('call', 0x228B60, (MC,))] * frames,
                          check=restored)[2]
    assert S.MODELS['oracle'].order == S.MODELS['native'].order, (name, 'read command order differs')
    return entries


def setup(names, points=None):
    """Every requested arrival (refusing before anything runs), then the
    save harness over them. Returns the arrivals."""
    arrivals = [at_terminal(arrival(n, points)) for n in names]
    MH.LIB = MH.build_lib()
    register(arrivals)
    S.setup()
    S.Card = MH.HostCard               # both sides of the harness answer from em_memcard
    return arrivals


def build(names, out=OUT, points=None):
    """Build, check and write each start point under `out`. Returns
    {name: (status, folder, checks)}, status 'written' or 'up to date',
    checks the save's card commands and the load's callee entries compared
    on both sides."""
    out = Path(out)
    if out.is_symlink() or (out.exists() and not out.is_dir()):
        raise Refused(f'{out} is not a folder')
    arrivals = setup(names, points)
    made = {}
    with tempfile.TemporaryDirectory(prefix='em_start_saves_') as tmp:
        for a in arrivals:
            made[a['name']] = save(tmp, a)
        L.setup()                      # the load lane on the same harness
        S.Card = MH.HostCard
        checks = {}
        for a in arrivals:
            root, files, commands = made[a['name']]
            checks[a['name']] = (commands, load(root, files, a['name']))
        result = {}
        for a in arrivals:
            _root, files, _n = made[a['name']]
            result[a['name']] = write(out / a['name'], a, files) + (checks[a['name']],)
    MH.LIB.em_memcard_reset()
    return result


def manifest(a, files):
    return {
        'name': a['name'],
        'what': 'a start-point save: the original writer 00228A90 over the arrival state '
                '(docs/SAVE_GAME.md section 7)',
        'source': str(a['folder'].relative_to(refs.BUILD.resolve())),
        'route_beat': a['beat'],
        'ee_sha256': a['ee_sha256'],
        'block_head': a['block'][:16].hex(),
        'load_pose': {'position': list(a['pose'][0]), 'rotation': list(a['pose'][1]),
                      'by': "00159B90's two copies 00102948 at the arrival frame (the save terminal's step)"},
        'card': {'port': PORT, 'slot': SLOT, 'folder': 'slot%d' % (PORT + 1)},
        'files': {k: sha256(v) for k, v in sorted(files.items())},
    }


def write(dest, a, files):
    """The card under dest (a host card root) and its start.json; file by
    file, only where different; anything else in dest refuses."""
    dest = Path(dest)
    want = {'slot%d/%s' % (PORT + 1, k): v for k, v in files.items()}
    want['start.json'] = (json.dumps(manifest(a, files), indent=1) + '\n').encode()
    dirs = {'slot1', 'slot2', 'slot%d/%s' % (PORT + 1, DIR)}
    if dest.is_symlink() or (dest.exists() and not dest.is_dir()):
        raise Refused(f'{dest} is not a folder')
    if dest.exists():
        for p in sorted(dest.rglob('*')):
            rel = str(p.relative_to(dest))
            if p.is_symlink() or not (rel in want and p.is_file() or rel in dirs and p.is_dir()):
                raise Refused(f'{dest} holds {rel}, which this tool does not write: move it away first')
    status = 'up to date'
    for d in sorted(dirs):
        (dest / d).mkdir(parents=True, exist_ok=True)
    for rel, data in sorted(want.items()):
        p = dest / rel
        if not (p.is_file() and p.read_bytes() == data):
            p.write_bytes(data)
            status = 'written'
    return status, dest


def main(argv):
    if argv == ['--list']:
        for n, p in START_POINTS.items():
            print('%-15s %s (%s*), block area %d' % (n, p['route'], p['first'], p['area']))
        return 0
    names = argv or list(START_POINTS)
    if len(set(names)) != len(names):
        print('build_start_saves: a start point is named twice', file=sys.stderr)
        return 2
    try:
        result = build(names)
    except AssertionError as ex:       # a check of the harness (the two sides, the card read back)
        print(f'build_start_saves: refused: a check failed: {ex}', file=sys.stderr)
        return 2
    except Refused as ex:
        print(f'build_start_saves: refused: {ex}', file=sys.stderr)
        return 2
    for n, (status, folder, (commands, entries)) in result.items():
        print(f'{n}: {status}: {folder.relative_to(ROOT)} (the save: {commands} card commands, both sides equal; '
              f'read back: {entries} callee entries equal to the original\'s)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
