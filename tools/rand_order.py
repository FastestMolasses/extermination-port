#!/usr/bin/env python3
"""rand() call order: the port's EM_RAND_TRACE against the original's C7
per-call capture (docs/RAND_ORDER.md; the decomp's docs/CAPTURES_C7.md
section 3).

The original trace (build/s87/c7cap/rng/<stretch>/rand.jsonl in the decomp)
holds every call of 00122BB8 with its frame, the caller's return address and
the state word before the call. The port's trace (src/game/em_random.c,
EM_RAND_TRACE) holds the main-loop counter, the state word and six native
return addresses per call; they are resolved here with the binary's symbol
table (nm) to the translated functions and from those to the original
callers. Both sides are reduced to (original function, state) per call.

Classes:
- DETERMINISTIC callers draw on every frame of a fixed schedule (the sway
  001D7C30 twice, the indicator children's 001F54E0 once each, the barrel's
  eleven 001F4D40 glow markers) or once on a fixed event (001FAE70, the
  items' 001F1110, the effect owner 008235F0 and the security gun 00825940
  at their first tick). Their per-frame skeleton (the sequence of these calls) does
  not depend on the values drawn.
- The others draw when a timer they took from an earlier draw runs out (the
  faces 001D0720, the head sprites 001E2560, the weather 001E55F0, the aura
  001F1180) or on the walk (00179B90, 001EA240); their frames follow the
  values once the streams differ.

No original code or data is reproduced here: the tables name original
functions and the return addresses the capture recorded.
"""
from __future__ import annotations

import collections
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
CAPTURE = DECOMP / 'build/s87/c7cap/rng'

# The original callers' return addresses (CAPTURES_C7.md section 3) and the
# function each lies in.
ORIGINAL_RA = {
    0x1F4D74: 0x1F4D40, 0x1F54FC: 0x1F54E0, 0x1D7D44: 0x1D7C30, 0x1D7DD4: 0x1D7C30,
    0x1D0A10: 0x1D0720, 0x1D0AAC: 0x1D0720, 0x1D0B80: 0x1D0720, 0x1D077C: 0x1D0720,
    0x1D08A4: 0x1D0720, 0x1D08D4: 0x1D0720, 0x179BA0: 0x179B90,
    0x1E26C4: 0x1E2560, 0x1E2724: 0x1E2560, 0x1E25BC: 0x1E2560,
    0x1E5660: 0x1E55F0, 0x1E5690: 0x1E55F0, 0x1E57F8: 0x1E55F0, 0x1E564C: 0x1E55F0,
    0x1E56D4: 0x1E55F0, 0x1E5710: 0x1E55F0, 0x1E5758: 0x1E55F0,
    0x1EA424: 0x1EA240, 0x1EA430: 0x1EA240, 0x1F11F8: 0x1F1180, 0x1F14F0: 0x1F1180,
    0x1FAF28: 0x1FAE70, 0x1F112C: 0x1F1110,
    0x8236B4: 0x8235F0,   # AREA11 overlay: the effect owner's first tick
    0x8259F0: 0x825940,   # AREA11 overlay: the security gun's lifecycle 0
}

# The port's translated functions (the first non-wrapper frame of a call)
# and the original function each translates.
PORT_FN = {
    'em_point_light_tick': 0x1D7C30,
    'em_effect_kinds_001F54E0': 0x1F54E0,
    'em_effect_manager_001F4D40': 0x1F4D40,
    'em_effect_manager_aura_draw': 0x1F1180,     # 001F1180's draw (0x1F14F0)
    'em_pickup_aura_001F1110': 0x1F1110,
    'em_pickup_aura_001F1180': 0x1F1180,
    'em_area11_effect_tick': 0x8235F0,
    'em_weather_tick': 0x1E55F0,
    'em_head_sprite_original_tick': 0x1E2560,
    'hs_rand_wait': 0x1E2560,
    'em_opening_face_tick': 0x1D0720,
    'footstep_rand5': 0x179B90,
    'x_step_random5': 0x179B90,
    'driver_seed': 0x1EA240,
    'em_effect_original_001EA240': 0x1EA240,
    'music_select': 0x1FAE70,
    'em_stream_lanes_001FAE70': 0x1FAE70,
    'em_status_background_step': 0x20A7A0,       # the status pages' background pulse
    'gun_setup': 0x825940,                       # the security gun's lifecycle 0 (em_security_gun)
    'em_gun_tick': 0x825940,                     # the same when gun_setup is inlined
    'em_aim_fire_target_001854E0': 0x1854E0,     # the laser dot (AIM_FIRE.md)
    'em_aim_fire_target_00185760': 0x185760,     # the beam
    'em_area00_hud_001E2BA0': 0x1E2BA0,          # the beam's shaded line
    'em_effect_original_001EF9D0': 0x1EF9D0,     # an effect's spawn (the impacts 001861C0 / 0018ABA0 spawn)
    'em_area00_fx_001F5040': 0x1F5040,           # the muzzle node's jitter (chain step AIMLIVE)
    'em_area00_fx_001F2F90': 0x1F2F90,           # a debris piece's seed (the shell casing, the impact debris)
    'em_area00_fx_001F3620': 0x1F3620,           # a piece's tumble and bounce sound
    'f3620_wobble': 0x1F3620,                    # its tumble (a static step of the same routine)
    'em_aim_fire_marker_0018ABA0': 0x18ABA0,     # the impact marker's ricochet
    'marker_ricochet': 0x18ABA0,                 # the same (its 001FBD50 step, a static of the routine)
    'em_area00_world_0018A180': 0x18A180,        # the knife's strike reaction (the cable hit; its LCG draw)
    'em_gun_rest_0021AAC0': 0x21AAC0,            # the cable's hit effect node (its rand() draws)
    'em_gun_rest_0021A500': 0x21A500,            # the cable's strand node
    'strip_offset': 0x21A500,                    # its strip offsets (a static step of the routine)
    'em_aim_fire_lamp_00187780': 0x187780,       # the gun lamp's flare size (AIM_FIRE.md section 10)
    'em_area01_ui_0022BBC0': 0x22BBC0,           # the flame contact's burn node: its ring LCG seed (DAMAGE.md)
    'em_effect_001F77B0': 0x1F77B0,              # the death decal's particle draws (DAMAGE.md)
    'em_player_reaction_0021D800': 0x21D800,     # the flinch's clip pick (DAMAGE.md)
    'em_crate_original_tick': 0x1551B0,          # a box's break: the husk's quarter turn, a raised box's kick (BRANCH br_04 / br_06)
    'em_player_footstep_tick': 0x187350,         # the wading ripple's 00122BB8 draw (PLAYER_FLOOR.md P14/P15; AREA01 water, a01_02 f39)
    # AREA01 owners (the arrival's world frames; no per-call AREA01 capture,
    # LEVEL2_BINDING.md "RNG evidence and limits")
    'em_area01_exitb_001E8E80': 0x1E8E80,        # a lattice set-up's dome heights
    'f_1E8E80': 0x1E8E80,
    'em_area01_exitb_001E9580': 0x1E9580,        # the other lattice set-up
    'f_1E9580': 0x1E9580,
    'em_status_scene_glow_001F4BF0': 0x1F4BF0,   # the flame services' glow
    'em_area01_render_001F4A10': 0x1F4A10,
    'em_area01_sys_001E7D20': 0x1E7D20,          # the ripple surface
    'f_1E7D20': 0x1E7D20,
    'em_area01_sys_001E3D90': 0x1E3D90,
    'em_area01_sys_0015A2C0': 0x15A2C0,          # the floor fields
    'em_area01_ovl_00826D40': 0x826D40,          # AREA01 overlay 00826D40
    'a01_ovl_826d40': 0x826D40,
}
# Frames that only forward a draw (the worker adapters over em_random_next).
WRAPPERS = {'em_random_next', 'w_rand', 'indicator_rand', 'face_random', 'random_range', 'countdown',
            'w_00122BB8', 'random_word', 'gun_rand',
            'aim_fire_binding_call', 'em_aim_fire_live_call',   # the aim/fire binding's 00122BB8 and dispatch
            'target_call0', 'em_aim_fire_binding_frame',        # em_aim_fire_target's callee adapter
            'beam_worker',                                      # em_aim_fire_render_live's 001E2BA0 callee adapter
            'fx_call', 'fx_call0', 'fx_rand_unit', 'forward',   # em_area00_fx's callee adapter and the world bridge
            'external_call', 'em_aim_fire_world_live_call',
            'hit_call',                                         # em_aim_fire_world_live's em_area00_world callee adapter
            'ui_call', 'ui_call0', 'dd_rand',
            'em_player_misc_random',
            # The AREA01 composition's 00122BB8 path: em_area01_live's worker
            # (em_random_next), the runtime's call and the pickup binding's
            # aura adapter (em_area01_pickup_live) forward 001F1110's draw.
            'worker', 'em_area01_runtime_call', 'aura_random',
            # ... and its other adapters' 00122BB8 forwards: the indicator's
            # (invoke, random_value), the flame services' random_worker, the
            # runtime's call adapters (sys_call, exitb_call), the overlay's
            # ovl_00122BB8, and dome, the per-cell height rule both lattice
            # set-ups 001E8E80 / 001E9580 share (the caller is the frame above).
            'invoke', 'random_value', 'random_worker', 'sys_call', 'exitb_call', 'ovl_00122BB8', 'dome',
            'h_random'}                                         # em_area11_boxes' 00122BB8 worker                            # the player workers' 00122BB8                   # em_area01_ui's callee adapter, 001F77B0's rand worker

DETERMINISTIC = {0x1D7C30, 0x1F54E0, 0x1F4D40, 0x1FAE70, 0x1F1110, 0x8235F0, 0x825940}
NAMES = {0x1D7C30: 'sway 001D7C30', 0x1F54E0: 'indicator 001F54E0', 0x1F4D40: 'glow marker 001F4D40',
         0x1FAE70: 'music 001FAE70', 0x1F1110: 'item 001F1110', 0x8235F0: 'effect owner 008235F0',
         0x825940: 'security gun 00825940', 0x1D0720: 'face 001D0720', 0x1E2560: 'head sprite 001E2560',
         0x1E55F0: 'weather 001E55F0', 0x1F1180: 'aura 001F1180', 0x179B90: 'step 00179B90',
         0x1EA240: 'footstep 001EA240', 0x20A7A0: 'status background 0020A7A0', 0x1854E0: 'laser dot 001854E0',
         0x185760: 'beam 00185760', 0x1E2BA0: 'beam line 001E2BA0', 0x1EF9D0: 'effect spawn 001EF9D0',
         0x1F5040: 'muzzle node 001F5040', 0x187780: 'gun lamp 00187780', 0x1F2F90: 'debris seed 001F2F90', 0x1F3620: 'debris piece 001F3620',
         0x18ABA0: 'impact marker 0018ABA0', 0x18A180: 'knife strike 0018A180',
         0x21AAC0: 'cable hit node 0021AAC0', 0x21A500: 'cable strand node 0021A500',
         0x22BBC0: 'burn node 0022BBC0', 0x1F77B0: 'death decal 001F77B0', 0x21D800: 'flinch 0021D800',
         0x1551B0: 'box 001551B0', 0x187350: 'wading ripple 00187350'}


def name(fn):
    return NAMES.get(fn, f'{fn:08X}')


# ------------------------------------------------------------ the original

def original(stretch):
    """frame -> [(function, state)] and the markers, from the C7 capture.
    Route stretches are keyed by the recorded route frame (rec_f)."""
    frames, marks = collections.OrderedDict(), []
    for line in (CAPTURE / stretch / 'rand.jsonl').open():
        d = json.loads(line)
        f = d.get('rec_f', d['f'])
        if 'mark' in d:
            marks.append((f, d['mark'], d.get('a0')))
            continue
        ra = int(d['ra'], 16)
        assert ra in ORIGINAL_RA, ('an original caller the table does not name', stretch, hex(ra))
        frames.setdefault(f, []).append((ORIGINAL_RA[ra], int(d['s'], 16)))
    return frames, marks


# ------------------------------------------------------------ the port

def symbols(binary):
    """Sorted (address, name) of the binary's functions, image-relative."""
    out = subprocess.run(['nm', '-n', str(binary)], capture_output=True, text=True, check=True).stdout
    table, base = [], None
    for line in out.splitlines():
        parts = line.split()
        if len(parts) != 3 or parts[1] not in 'tT':
            continue
        address, symbol = int(parts[0], 16), parts[2]
        if symbol in ('__mh_execute_header', '__executable_start'):
            base = address
        table.append((address, symbol[1:] if symbol.startswith('_') else symbol))
    assert base is not None, ('no image base symbol in', binary)
    rows = sorted((a - base, s) for a, s in table)
    for s in PORT_FN:
        assert sum(1 for _, n in rows if n == s) <= 1, ('a mapped port function name is not unique', s)
    return rows


def port(trace, binary):
    """counter -> [(function, state, native function)] from EM_RAND_TRACE."""
    import bisect
    rows = symbols(binary)
    starts = [a for a, _ in rows]
    cache = {}

    def fn_of(offset):
        if offset not in cache:
            i = bisect.bisect_right(starts, offset - 1) - 1
            cache[offset] = rows[i][1] if i >= 0 else '?'
        return cache[offset]

    frames = collections.OrderedDict()
    for line in Path(trace).open():
        parts = line.split()
        counter, state = int(parts[0], 16), int(parts[1], 16)
        chain = [fn_of(int(p, 16)) for p in parts[2:]]
        mapped = next((PORT_FN[c] for c in chain if c not in WRAPPERS and c in PORT_FN), None)
        first = next((c for c in chain if c not in WRAPPERS), None)
        assert mapped is not None and PORT_FN.get(first) == mapped, \
            ('a port rand() caller the table does not name (add it to PORT_FN with its original)', chain)
        frames.setdefault(counter, []).append((mapped, state, first))
    return frames


# ------------------------------------------------------------ comparisons

def skeleton(calls):
    return [c[0] for c in calls if c[0] in DETERMINISTIC]


def area_entry_original(frames, marks):
    return next(f for f, m, _ in marks if m.startswith('001AFCF0'))


def area_entry_port(frames):
    """The port's area-entry frame: 0x1AE040 state 0's 001FAE70(1) draws the
    first call after the New Game commit, from the unseeded state 1."""
    for counter, calls in frames.items():
        if calls[0][0] == 0x1FAE70 and calls[0][1] == 1:
            return counter
    raise AssertionError('no area-entry 001FAE70 draw from state 1 in the port trace')


def first_control(frames, after):
    """The first frame after `after` whose calls include 001FAE70: the
    opening controller's 001FAE70(0) as it returns control."""
    return next(f for f in frames if f > after and any(c[0] == 0x1FAE70 for c in frames[f]))


def call_for_call(port_frames, p0, orig_frames, o0):
    """Walks both streams from their area-entry frames. Returns the number of
    calls equal in function, relative frame and state, and the first
    difference (None when one stream ends)."""
    def flat(frames, f0):
        return [(f - f0, fn, s) for f, calls in frames.items() if f >= f0 for fn, s, *_ in calls]
    p, o = flat(port_frames, p0), flat(orig_frames, o0)
    n = 0
    while n < min(len(p), len(o)) and p[n] == o[n]:
        n += 1
    diff = (p[n] if n < len(p) else None, o[n] if n < len(o) else None) if n < min(len(p), len(o)) else None
    return n, diff


def skeleton_frames(port_frames, p0, orig_frames, o0, count):
    """Relative frames 1..count-1 whose deterministic skeleton differs."""
    bad = []
    for k in range(1, count):
        ps = skeleton(port_frames.get(p0 + k, []))
        os_ = skeleton(orig_frames.get(o0 + k, []))
        if ps != os_:
            bad.append((k, ps, os_))
    return bad


def totals(frames, lo, hi):
    return collections.Counter(c[0] for f, calls in frames.items() if lo <= f < hi for c in calls)


def stage_positions(frames, lo, hi, fn):
    """Where fn's calls sit in their frames: before the frame's first
    glow marker (the pool walk) or after its last (the player stage of a
    cutscene frame, 001AE6B0, or later)."""
    out = collections.Counter()
    for f, calls in frames.items():
        if not lo <= f < hi:
            continue
        seq = [c[0] for c in calls]
        marks = [i for i, c in enumerate(seq) if c == 0x1F4D40]
        for i, c in enumerate(seq):
            if c == fn:
                out['after the barrel' if marks and i > marks[-1] else 'in the pool walk'] += 1
    return out


def spawn_frame(frames, f0, f1):
    """The first frame in [f0, f1) with a face draw (001D0720) in the pool
    walk after the indicator children (the frame's last 001F54E0) and before
    the glow markers (its first 001F4D40): the opening script's class-9
    001BB0E0 record's first tick (op14 001BAC00 spawned it at the tail of the
    walk; 001BAD40's 001BA8E0 attaches its face and 001BB0E0 falls through
    into 001BA580). Its frame follows the stream request's wait (op07 sub
    12's phase 3), as the opening's end does."""
    for f in range(f0, f1):
        seq = [c[0] for c in frames.get(f, [])]
        ind = [i for i, c in enumerate(seq) if c == 0x1F54E0]
        glow = [i for i, c in enumerate(seq) if c == 0x1F4D40]
        if not ind or not glow:
            continue
        if any(c == 0x1D0720 and ind[-1] < i < glow[0] for i, c in enumerate(seq)):
            return f
    raise AssertionError('no frame with the opening body\'s first face tick')


def structure_after(port_frames, p, orig_frames, o, count):
    """From port frame p against original frame o, frame for frame: the
    number of frames whose caller sequences are equal before the first
    difference, and that difference (k, port callers, original callers) or
    None."""
    for k in range(count):
        ps = [c[0] for c in port_frames.get(p + k, [])]
        os_ = [c[0] for c in orig_frames.get(o + k, [])]
        if ps != os_:
            return k, (k, ps, os_)
    return count, None


def opening(port_frames, orig_frames, orig_marks):
    """The New Game opening, aligned on the area entry. Returns a report."""
    o0, p0 = area_entry_original(orig_frames, orig_marks), area_entry_port(port_frames)
    oc, pc = first_control(orig_frames, o0), first_control(port_frames, p0)
    n, diff = call_for_call(port_frames, p0, orig_frames, o0)
    window = min(oc - o0, pc - p0)
    bad = skeleton_frames(port_frames, p0, orig_frames, o0, window)
    return dict(o0=o0, p0=p0, oc=oc, pc=pc, equal_calls=n, first_difference=diff, window=window,
                skeleton_bad=bad,
                orig_totals=totals(orig_frames, o0, oc), port_totals=totals(port_frames, p0, pc),
                orig_face=stage_positions(orig_frames, o0, oc, 0x1D0720),
                port_face=stage_positions(port_frames, p0, pc, 0x1D0720))


def after_control(port_frames, pc, orig_frames, oc, count):
    """The frames after first control: skeleton frame for frame."""
    return skeleton_frames(port_frames, pc, orig_frames, oc, count + 1)


def fmt_totals(t):
    return ', '.join(f'{name(k)} {v}' for k, v in sorted(t.items(), key=lambda kv: -kv[1]))


# ------------------------------------------------------------ the checks

# The first divergence the opening has (docs/RAND_ORDER.md section 3): the
# stream request's wait. op07 sub 12 holds the script in its phase 3 until
# the request's read keys on (D_008106F4 == 1); the original's drive made it
# wait longer than the port's (host speed, the default: the fields the
# capture's request waited; the PS2 disc-drive timing switch: the drive
# model's). So the port's op14 (001BAC00) spawns the opening's actors that
# many frames earlier, and in their first frame the port's class-9 record
# draws its face's first values (001D0720) where the original, still
# waiting, draws its glow markers (001F4D40) from the same state. Every
# earlier call is equal in caller and state: the security gun's AE+1 draw
# (census L24), Roger's owner's face at AE+2 and the player's face in the
# player stage after the barrel from AE+5 (00183090 -> 001D0C70 -> 001D0720,
# the chain's OPENING step). From the spawn on the port's frames are the
# original's that many frames later, caller for caller, until a value-driven
# caller's timer (drawn from a state the wait moved) first differs.
FIRST_DIFFERENCE_CALLERS = (0x1D0720, 0x1F4D40)
# The opening's end with the PS2 disc-drive timing switch on (the drive
# model): the original waited on the area music's read (a 16-field seek after
# the New Game's last module-loader read, IOP_STREAM.md "Drive model"); the model
# serves a first read as a full seek (6 fields). So the port's first control
# may come up to 16 frames earlier; never later. With the switch off (host
# speed, the default) check_opening requires the exact difference instead.
OPENING_END_SLACK = 16


# ------------------------------------------------------------ the drive mode

# The stream drive's mode, from the "stream drive: <mode>:" line the level
# smoke and newgame-control print (em_stream_live_drive_report; the switch is
# em_settings' EM_PS2_DISC_DRIVE_TIMING, LAUNCHER_OPTIONS.md "PS2 disc-drive
# timing"): 'ps2' (the drive model) or 'host' (host speed, the default).
def drive_mode(run_log):
    m = re.search(r'^stream drive: (PS2 disc-drive timing on|host speed): ', run_log, re.M)
    assert m, 'the run log has no "stream drive:" line (the drive mode)'
    return 'ps2' if m.group(1).startswith('PS2') else 'host'


# A host-speed read in the main-loop-top rows (IOP_STREAM.md section 4): the
# read sequencer 001FA0D0 takes one step per field; its ready query 00113280
# (phase 1) finds the drive idle, so phase 1 lasts one row; it issues the
# read (phase 2) and its next step's 00112D18 finds it done, so phase 2 lasts
# one row. Everything else around the read is the code's.
HOST_READY_ROWS = 1
HOST_READ_ROWS = 1

STREAM_OPENING = DECOMP / 'build/s87/c7cap/stream/opening/frames.jsonl'   # CAPTURES_C7.md section 1


def lane0_request(rows):
    """The opening's stream request on lane 0 (001FD4C0 -> 001FA790(0, 0x3F)
    with the D_008106F4 hold), over main-loop-top rows (active0, phase, lane,
    load0) that start before it: the lane's start (the last 0 -> 1 of
    D_00282154 before the first key-on), the first row the sequencer is in
    phase 1 on lane 0, the read's issue (phase 2), done (+0x03 = 2) and the
    key-on (D_00282154 = 2). `wait` is the fields the drive made the
    sequencer wait beyond a host-speed read: the ready query's extra rows
    plus the read's extra rows."""
    keyon = next(k for k, r in enumerate(rows) if r[0] == 2)
    start = max(k for k in range(1, keyon) if rows[k][0] == 1 and rows[k - 1][0] == 0)
    p1 = next(k for k in range(start, keyon) if rows[k][1] == 1 and rows[k][2] == 0)
    issue = next(k for k in range(p1, keyon) if rows[k][1] == 2 and rows[k][2] == 0)
    done = next(k for k in range(issue, keyon) if rows[k][3] == 2)
    return dict(start=start, p1=p1, issue=issue, done=done, keyon=keyon,
                wait=(issue - p1 - HOST_READY_ROWS) + (done - issue - HOST_READ_ROWS))


def lane0_request_capture():
    """lane0_request over the C7 stream capture of the New Game opening."""
    rows = []
    for line in STREAM_OPENING.open():
        r = json.loads(line)
        lb = bytes.fromhex(r['lb'])
        rows.append((lb[0], lb[3], lb[4], int(r['L0']['b0_3'][6:8], 16)))
    return lane0_request(rows)


def lane0_request_port(ticks):
    """lane0_request over the port's tick log (EM_AREA_CHANGE_LOG): each
    tick's "stream" row is the state before its frame, the main-loop top."""
    rows = [(t['stream'][3], t['stream'][1], t['stream'][2], t['stream'][6]) for t in ticks if 'stream' in t]
    return lane0_request(rows)


def check_opening(port_frames, orig_frames, orig_marks, drive):
    """Asserts the opening's rand() order against the C7 newgame capture and
    returns the report and a summary line. `drive` is (mode, the port's
    lane0_request) with mode 'ps2' or 'host' (drive_mode).

    The opening's end: with the PS2 disc-drive timing switch on, first
    control may come up to OPENING_END_SLACK frames earlier than the
    original's, never later. With the switch off (host speed) the stream
    request must run as the capture's with every drive wait gone: the
    port's ready query and read take the host-speed rows, the hold from the
    read's end to the key-on equals the capture's, and first control comes
    exactly the capture's drive wait earlier (the C7 stream capture's
    opening, whose run matches newgame_samples' fade-in frame, CAPTURES_C7.md
    section 1)."""
    mode, port_req = drive
    rep = opening(port_frames, orig_frames, orig_marks)
    o0, p0 = rep['o0'], rep['p0']
    po = [(fn, s) for fn, s, _ in port_frames[p0]]
    assert po == orig_frames[o0], ('rand order: the area-entry frame', po, orig_frames[o0])
    diff = rep['first_difference']
    late = (rep['oc'] - o0) - (rep['pc'] - p0)
    o_spawn = spawn_frame(orig_frames, o0, rep['oc']) - o0
    p_spawn = spawn_frame(port_frames, p0, rep['pc']) - p0
    # The opening's actors spawn when the stream request's wait ends: as
    # much earlier as the opening's end.
    assert p_spawn + late == o_spawn, \
        ('rand order: the opening\'s actors spawn away from the stream request\'s end', p_spawn, o_spawn, late)
    # The first difference is that frame's: the port's body draws its face
    # where the original draws its glow markers, from the same state.
    assert diff is not None and diff[0][0] == diff[1][0] == p_spawn and \
        (diff[0][1], diff[1][1]) == FIRST_DIFFERENCE_CALLERS and diff[0][2] == diff[1][2], \
        ('rand order: the first difference is not the opening actors\' spawn at the stream request\'s end',
         rep['equal_calls'], diff and (diff[0][0], name(diff[0][1]), hex(diff[0][2])),
         diff and (diff[1][0], name(diff[1][1]), hex(diff[1][2])), p_spawn)
    # From the spawn: the original's frames `late` later, caller for caller,
    # until a value-driven caller first differs.
    equal, sdiff = structure_after(port_frames, p0 + p_spawn, orig_frames, o0 + o_spawn,
                                   rep['pc'] - p0 - p_spawn)
    assert equal >= 1, ('rand order: the spawn frame\'s callers differ from the original\'s', sdiff)
    if sdiff:
        k, ps, os_ = sdiff
        i = next(i for i in range(max(len(ps), len(os_))) if i >= len(ps) or i >= len(os_) or ps[i] != os_[i])
        involved = {c for c in (ps[i] if i < len(ps) else None, os_[i] if i < len(os_) else None) if c is not None}
        assert involved - DETERMINISTIC, \
            ('rand order: after the spawn a deterministic caller differs first', 'AE+%d' % (p_spawn + k),
             [name(c) for c in ps], [name(c) for c in os_])
    rep['spawn'], rep['late_spawn'], rep['after_spawn'] = p_spawn, late, equal
    # Every opening frame's deterministic callers, the security gun's AE+1
    # draw included, equal the original's.
    assert not rep['skeleton_bad'], \
        ('rand order: an opening frame\'s deterministic callers differ',
         [('AE+%d' % k, [name(c) for c in ps], [name(c) for c in os_]) for k, ps, os_ in rep['skeleton_bad'][:3]])
    late = (rep['oc'] - o0) - (rep['pc'] - p0)
    orig_req = lane0_request_capture()
    rep['late'], rep['orig_request'], rep['port_request'] = late, orig_req, port_req
    if mode == 'ps2':
        assert 0 <= late <= OPENING_END_SLACK, ('rand order: the opening\'s end', rep['pc'] - p0, rep['oc'] - o0)
        end = (f'first control {late} frame(s) earlier than the original\'s AE+{rep["oc"] - o0} (the area music\'s '
               f'read: disc timing; the drive model waits {port_req["wait"]} field(s) in the stream request, the '
               f'capture {orig_req["wait"]})')
    else:
        assert mode == 'host', ('rand order: unknown drive mode', mode)
        assert (port_req['issue'] - port_req['p1'], port_req['done'] - port_req['issue']) == \
            (HOST_READY_ROWS, HOST_READ_ROWS) and port_req['wait'] == 0, \
            ('rand order: the opening\'s stream request did not read at host speed', port_req)
        assert port_req['keyon'] - port_req['done'] == orig_req['keyon'] - orig_req['done'], \
            ('rand order: the opening\'s hold from the read to the key-on differs from the capture', port_req, orig_req)
        assert late == orig_req['wait'], \
            ('rand order: the opening\'s end is not the capture\'s drive wait earlier', late, orig_req)
        end = (f'first control {late} frame(s) earlier than the original\'s AE+{rep["oc"] - o0}, exactly the '
               f'fields the capture\'s stream request waited on the drive ({orig_req["issue"] - orig_req["p1"]} '
               f'rows for the ready query and {orig_req["done"] - orig_req["issue"]} for the read, against host '
               f'speed\'s {HOST_READY_ROWS} and {HOST_READ_ROWS}; the hold to the key-on '
               f'{orig_req["keyon"] - orig_req["done"]} rows in both)')
    return rep, (f'the area entry (port counter {p0} = original frame n{o0}) and {rep["equal_calls"]} calls equal '
                 f'in caller and state (the security gun\'s AE+1 draw, Roger\'s owner\'s face at AE+2, the player\'s '
                 f'face in the player stage after the barrel from AE+5) up to the opening\'s actors\' spawn at '
                 f'AE+{rep["spawn"]}, the stream request\'s end, {late} frame(s) before the original\'s '
                 f'AE+{rep["spawn"] + late}; from there {rep["after_spawn"]} frame(s) equal the original\'s '
                 f'{late} later caller for caller (the body\'s face and head sprite in the pool walk, the player\'s '
                 f'face after the barrel) until a value-driven caller\'s timer differs; the deterministic callers '
                 f'(sway, indicators, glow markers, music, item, effect owner, security gun) equal frame for frame '
                 f'over AE+1..AE+{rep["window"] - 1}; {end}')


def check_after_control(port_frames, pc, orig_frames, oc, count):
    bad = after_control(port_frames, pc, orig_frames, oc, count)
    assert not bad, ('rand order: a frame after first control', [(k, [name(c) for c in ps], [name(c) for c in os_])
                                                                 for k, ps, os_ in bad[:3]])
    return f'the {count} frames after first control equal the original\'s in their deterministic callers'


def report_driven(rep):
    """The value-driven callers over the opening (reported, not asserted)."""
    driven = lambda t: {k: v for k, v in t.items() if k not in DETERMINISTIC}
    face = lambda c: ', '.join(f'{v} {k}' for k, v in sorted(c.items()))
    return (f'value-driven callers over the opening: original {fmt_totals(driven(rep["orig_totals"]))}; '
            f'port {fmt_totals(driven(rep["port_totals"]))}; the faces 001D0720 run {face(rep["orig_face"])} '
            f'in the original and {face(rep["port_face"])} in the port (in the pool walk Roger\'s owner and the '
            f'opening\'s 001BB0E0 body; after the barrel the player stage 00183090 -> 001D0C70)')
