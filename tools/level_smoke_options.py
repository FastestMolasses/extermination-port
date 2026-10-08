#!/usr/bin/env python3
"""The OPTIONS side run's check (docs/OPTIONS.md section 6; LEVEL_SMOKE.md
"The OPTIONS side run"), called by tools/test_level_smoke.py.

The side phase `options` (em_level_smoke_test.c) plays the nine beats of
the capture lane OPTIONS (decomp docs/CAPTURES_C10.md "OPTIONS",
build/c10/options/<beat>/trace.json) in a row from the end of
truck_crossing, with the lane's own closed-loop policies. Per beat, this
compares the run's tick log ("opt", em_scene_bindings.c log_tick_end) with
the recording's rows over the beat's window (the run: from its "beat" line
to the next; the recording: every row):

  * the options state, sampled after every frame on both sides: the
    settings D_00810118 +0..+0xB, the gameplay task's frame state +0xB, the
    options' state +0xC, sub-state +0xD, +0x12, +0x13, the cursor +0x1C
    and the blink timer +0x1E, D_008106C4, the screen offset 0x70003B94 /
    96, the button masks 0x70003B74..83, the pad mode D_00810E6A, the pad
    phase D_00810E50, the committed output mode D_0028215B and the rumble's
    active byte D_00810E56 (both after the frame's steps H and I: the next
    tick's start sample, "opt_pre" / "pad_pre") and, from the load row's
    clear (001AF6F0) on,
    the card record D_00810040's +0, +1, +2, +0x14, +0x15, +0x16, +0x17,
    +0x24, +0x28, +0x2C, +0x48, +0x50, +0x54, +0x58 and the busy byte
    D_00275BD8 (after the frame's loader task in slot 2, which clears it at
    its 0x63 step: the next tick's "loader_pre");
  * the beat's sequence of distinct states (consecutive equal samples
    collapsed) must be the recording's, value for value and in order:
    every press's effect, every blink step, every cursor move, every
    module load's busy edges, every card poll result;
  * every state must last the recording's number of rows, except a screen
    module's load (0x2B in 0022A590's sub-state 2, 0x2A in 00225AC0's
    sub-state 3: host speed, never longer than the recording's) and the
    last state (the run's settle continues past the recording's end).

Known difference, left out on purpose: the card record D_00810040 before
the load row clears it. The original's boot card check (0022A460, the
startup's 001AB9D0 state 3) leaves its last state there (3, 0, 2, ...);
the port's boot card check is the native stand-in (em_frontend), which
leaves it zero. Nothing reads the record before 001AF6F0 clears it.
"""
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CAPTURES = ROOT.parent / 'Extermination/build/c10/options'
BEATS = ('opt_00_browse_close', 'opt_01_vibration', 'opt_02_sound', 'opt_03_screen_position',
         'opt_04_brightness', 'opt_05_button_config', 'opt_06_default', 'opt_07_load_cancel',
         'opt_08_quit_cancel')
MC_FIELDS = ((0, 1), (1, 1), (2, 1), (0x14, 1), (0x15, 1), (0x16, 1), (0x17, 1), (0x24, 4), (0x28, 4),
             (0x2C, 4), (0x48, 4), (0x50, 4), (0x54, 4), (0x58, 4))


def _s16(b, at):
    return struct.unpack_from('<h', b, at)[0]


def _mc(mc):
    out = []
    for at, size in MC_FIELDS:
        out.append(mc[at] if size == 1 else struct.unpack_from('<i', mc, at)[0])
    return tuple(out)


def state(settings, task, c4, x, y, masks, e6a, e50, b15b, rumble, mc, bd8, cleared):
    """task: the record's bytes from +8. `cleared`: the load row has cleared
    the card record (its fields are compared from then on)."""
    def t(off):
        return task[off - 8]
    return (bytes(settings[:0xC]).hex(), t(0xB), t(0xC), t(0xD), t(0x12), t(0x13),
            struct.unpack_from('<H', task, 0x1C - 8)[0], struct.unpack_from('<H', task, 0x1E - 8)[0],
            c4, x, y, masks, e6a, e50, b15b, rumble, _mc(mc) if cleared else None, bd8 if cleared else None)


def port_state(tick, after, cleared):
    """`after`: the next tick, whose start holds this frame's post-frame
    values of what runs after the task (step H's D_0028215B, step I's
    rumble countdown; the recordings sample after the whole frame)."""
    o = tick['opt']
    return state(bytes.fromhex(o[0]), bytes.fromhex(o[1]), o[2], o[3], o[4], o[5], o[6], o[7],
                 after['opt_pre'][0], after['pad_pre'][0], bytes.fromhex(o[9]),
                 bytes.fromhex(after['loader_pre'])[25], cleared)


def capture_state(row, cleared):
    v = row['ov']
    task = bytes.fromhex(row['task0x'])[8:0x20]
    return state(bytes.fromhex(row['opt118']), task, v['c4'], v['x3B94'], v['y3B96'], v['masks'], v['e6a'],
                 v['e50'], v['b15B'], row['rumble']['active'], bytes.fromhex(row['mc040']), v['bd8'], cleared)


def runs(states):
    """[(state, rows)] of the consecutive equal samples."""
    out = []
    for s in states:
        if out and out[-1][0] == s:
            out[-1][1] += 1
        else:
            out.append([s, 1])
    return [tuple(x) for x in out]


def collapse(states):
    out = []
    for s in states:
        if not out or out[-1] != s:
            out.append(s)
    return out


def cleared_from(rows, key):
    """The index of the first sample after the load row's clear: the
    options' state 3 (001AF6F0 cleared the record on the frame it went
    there), or None."""
    for i, r in enumerate(rows):
        if key(r) == 3:
            return i
    return None


PHASES = tuple(b[:6] for b in BEATS)


def check_side(T, ticks, run, state_, phase):
    """One side phase opt_NN: its beat (from the "beat" line to the run's
    end) against the recording."""
    beat = next(b for b in BEATS if b.startswith(phase + '_'))
    m = re.search(rf'^level smoke: {phase}: beat (\S+) at tick (\d+)', run, re.M)
    assert m and m.group(1) == beat, (phase, 'the run played another beat', m and m.group(1))
    assert re.search(rf'^level smoke: {phase}: PASS', run, re.M), (phase, 'no PASS line')
    a = next(i for i, t in enumerate(ticks) if t['tick'] >= int(m.group(2)))
    trace = json.loads((CAPTURES / beat / 'trace.json').read_text())
    rows = trace['rows']
    window = [t for t in ticks[a:] if 'opt' in t]
    assert len(window) > 1, (beat, 'no "opt" samples in the run')
    pc = cleared_from(window, lambda t: bytes.fromhex(t['opt'][1])[0xC - 8])
    rc = cleared_from(rows, lambda r: r['ov']['mstate'] if r['ov']['fstate'] == 2 else -1)
    assert (pc is None) == (rc is None), (beat, 'the load row ran on one side only')
    port = [port_state(t, window[i + 1], pc is not None and i >= pc) for i, t in enumerate(window[:-1])]
    cap = [capture_state(r, rc is not None and i >= rc) for i, r in enumerate(rows)]
    # Both start from the end of route beat 08 (the recording from its
    # snapshot, the run from its truck_crossing phase): before the open the
    # samples must hold one and the same state; the sequences are then
    # compared from the open on, value for value.
    po = next(i for i, x in enumerate(port) if x[1] == 2)
    co = next(i for i, x in enumerate(cap) if x[1] == 2)
    assert set(port[:po]) == set(cap[:co]) and len(set(cap[:co])) == 1, \
        (beat, 'the state before the open differs', set(port[:po]), set(cap[:co]))
    pr, cr = runs(port[po:]), runs(cap[co:])
    port, cap = [x for x, _ in pr], [x for x, _ in cr]
    if port != cap:
        n = next((k for k, (p, c) in enumerate(zip(port, cap)) if p != c), min(len(port), len(cap)))
        lo = max(0, n - 2)
        raise AssertionError((beat, f'the state sequence differs at step {n} of {len(cap)} (port {len(port)})',
                              'port', port[lo:n + 3], 'capture', cap[lo:n + 3]))
    # Every state lasts the recording's rows (the policy's taps reach the
    # game when the capture tool's did: em_level_smoke_test.c opt_frame),
    # except a screen module's load (the busy rows: 0022A590's sub-state 2
    # in state 10, 00225AC0's sub-state 3; host speed) and the last state
    # (the run's settle after the recording's end).
    loads = 0
    for k, ((s, n), (_, m)) in enumerate(zip(pr[:-1], cr[:-1])):
        load = (s[2] == 10 and s[3] == 2) or (s[16] is not None and s[16][0] == 0 and s[16][1] == 3)
        if load:
            loads += 1
            assert n <= m, (beat, 'a screen module load lasted longer than the recording\'s', k, n, m)
            continue
        assert n == m, (beat, f'state {k} lasted {n} frames, the recording\'s {m}', s)
    print(f'level smoke: {phase}: {beat}: the {len(cap)} states from the open equal the recording\'s, '
          f'each for its rows ({loads} screen-module load(s) at host speed; {len(window) - 1} port ticks, '
          f'{len(rows)} recorded rows)')
