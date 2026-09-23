#!/usr/bin/env python3
"""The live first-level smoke (SCENE_COORDINATOR_DESIGN.md step S13;
docs/LEVEL_SMOKE.md) against the original captures.

Input: the stderr of an `EM_STARTUP_TEST=newgame-level` run (--run-log) and
its scene tick log (--log, EM_AREA_CHANGE_LOG: one line per slot-0 task tick
with the canonical state before and after the tick and the worker calls of
the tick, see em_scene_bindings.c).

Every phase the run reports as PASS must have a capture check below; a
phase reported NOT-LIVE is listed and not checked. The phases follow the
route of docs/FIRST_LEVEL_ROUTE.md section 3, whose captures live in
../Extermination/build/s87/route/<beat>/trace.json (read-only; nothing from
them is copied or printed beyond compared values).

first_control
    The first-control tick (the one whose post-tick D_00810750 is the value
    the run printed) against
    - status_04.json frame 0 (frame_trace2, the original's idle gameplay
      frame from save state 04; samples are taken after the frame): task
      +8/+9/+B/+C, the spad bytes 0x70003B8C..93, D_008106B0/C5/CE, and one
      001AE5E0 in the tick;
    - route 01_battery row f0 (save state 04): the spad bytes, the request
      bytes D_008106B0..B9, the area bytes D_00810700/701 and the eight bytes
      of the 0x28A9A0 fade block.
status
    The START-opened status screen against status_04.json (opened and closed
    with TRIANGLE from state 04 in the original; START and TRIANGLE take the
    same 001AE7E0 arm, D_00810E74 & 0x810): aligned at the tick where +B becomes 3
    (001AE7E0 r == 2) and at the tick where it becomes 5 (the hub's close),
    two ticks before to three after each must equal the capture in task
    +8/+9/+B/+C, the spad bytes, D_008106B0/C5/CE and whether 001AE5E0 ran;
    every tick between is state 3 sub-step 1 without a variant, as in the
    capture. The exit fade (001AEDB0 at the close, 001AEE40(0x20) in state 5)
    must equal route 01_battery's status exit row for row, from its first
    fade substate-2 row to the row where the fade is idle again.
"""
import argparse
import json
from pathlib import Path
import re
import sys

import test_scene_task_reference as tsr

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route'
STATUS_04 = DECOMP / 'build/s87/frame_trace2/status_04.json'
F_001AE5E0 = 0x1AE5E0
SPAD = 0x70003B8C
WINDOW = (-2, 4)          # ticks before / after an alignment tick (exclusive end)


def snap(tick, key='post'):
    b = bytes.fromhex(tick[key])
    return {
        'task': (b[0], b[1], b[3], b[4]),                     # +8, +9, +B, +C
        'spad': b[tsr.OFFSET[SPAD]:tsr.OFFSET[SPAD] + 8].hex(),
        'b0c5ce': (tsr.get(b, 0x8106B0), tsr.get(b, 0x8106C5), tsr.get(b, 0x8106CE)),
        'req': b[tsr.OFFSET[0x8106B0]:tsr.OFFSET[0x8106B0] + 10].hex(),
        'area': b[tsr.OFFSET[0x810700]:tsr.OFFSET[0x810700] + 2].hex(),
        'variants': tsr.get(b, 0x810750, 4),
        'variant': any(e[1] == F_001AE5E0 for e in tick['trace']),
    }


def original_status_frames():
    assert STATUS_04.exists(), f'original capture missing: {STATUS_04} (ORIGINAL_FRAME_ORDER.md Q7)'
    d = json.loads(STATUS_04.read_text())
    ran = {e['frame'] for e in d['hits'] if e.get('variant') == '001AE5E0'}
    return [{'task': (f['p8'], f['p9'], f['B'], f['C']), 'spad': f['spad'],
             'b0c5ce': tuple(f['B0_C5_CE']), 'variant': f['fr'] in ran} for f in d['frames']]


def route_rows(beat):
    path = ROUTE / beat / 'trace.json'
    assert path.exists(), f'route capture missing: {path} (docs/FIRST_LEVEL_ROUTE.md)'
    return json.loads(path.read_text())['rows']


def fade_substate(hex8):
    b = bytes.fromhex(hex8)
    return b[0] | b[1] << 8


KEYS = ('task', 'spad', 'b0c5ce', 'variant')


def same(port, orig, what):
    for k in KEYS:
        assert port[k] == orig[k], (what, k, port[k], orig[k])


# ------------------------------------------------------------------ phases

def check_first_control(ticks, run, state):
    m = re.search(r'level smoke: first_control: PASS .*d810750=(\d+)', run)
    assert m, 'no first_control PASS line with d810750'
    n = int(m.group(1))
    idx = next(i for i, t in enumerate(ticks) if snap(t)['variants'] == n and snap(t)['variant'])
    port = snap(ticks[idx])
    orig = original_status_frames()[0]
    same(port, orig, 'first control vs status_04 frame 0')
    row = route_rows('01_battery')[0]
    assert port['spad'] == row['spad'], ('spad vs route 01 f0', port['spad'], row['spad'])
    assert port['req'] == row['req'], ('D_008106B0..B9 vs route 01 f0', port['req'], row['req'])
    assert port['area'] == row['area'], ('D_00810700/701 vs route 01 f0', port['area'], row['area'])
    # The fade block after the first-control frame: the original ticks it after
    # the task, so it is the next tick's start sample (the run always logs one
    # more tick after its last phase, em_level_smoke_test.c finish()).
    assert idx + 1 < len(ticks), ('no tick after first control: the post-frame fade block was not logged',
                                  ticks[idx]['tick'])
    fade = ticks[idx + 1]['fade8']
    assert fade == row['fade'][:16], ('fade block vs route 01 f0', fade, row['fade'][:16])
    state['first_control'] = idx
    print(f'first_control: PASS (tick {ticks[idx]["tick"]}: task {port["task"]}, spad {port["spad"]}, '
          f'B0/C5/CE {port["b0c5ce"]}, one 001AE5E0, as status_04 frame 0; spad, D_008106B0..B9, area '
          f'{port["area"]} and fade {fade} as route 01_battery f0)')


def check_window(ticks, orig, at, oat, what):
    lo, hi = WINDOW
    assert at + lo >= 0 and at + hi <= len(ticks), (what, 'window outside the log')
    for k in range(lo, hi):
        same(snap(ticks[at + k]), orig[oat + k], f'{what} {k:+d}')


def check_status(ticks, run, state):
    orig = original_status_frames()
    fc = state['first_control']
    o = next(i for i in range(fc + 1, len(ticks)) if snap(ticks[i])['task'][2] == 3)
    oo = next(i for i, f in enumerate(orig) if f['task'][2] == 3)
    c = next(i for i in range(o + 1, len(ticks)) if snap(ticks[i])['task'][2] == 5)
    cc = next(i for i in range(oo + 1, len(orig)) if orig[i]['task'][2] == 5)
    check_window(ticks, orig, o, oo, 'status open')
    check_window(ticks, orig, c, cc, 'status close')
    for i in range(o + 1, c):
        p = snap(ticks[i])
        assert (p['task'][2:], p['variant'], p['spad']) == ((3, 1), False, orig[oo + 1]['spad']), \
            ('status tick', ticks[i]['tick'], p)
    for i in range(oo + 1, cc):
        assert (orig[i]['task'][2:], orig[i]['variant']) == ((3, 1), False), ('capture status frame', i)
    rows = route_rows('01_battery')
    r0 = next(i for i, r in enumerate(rows) if fade_substate(r['fade'][:16]) == 2)
    r1 = next(i for i in range(r0 + 1, len(rows)) if fade_substate(rows[i]['fade'][:16]) == 0)
    e = next(i for i in range(o, len(ticks)) if fade_substate(ticks[i]['fade8']) == 2)
    assert e > c, ('exit fade before the close', e, c)
    for k in range(r1 - r0 + 1):
        got, want = ticks[e + k]['fade8'], rows[r0 + k]['fade'][:16]
        assert got == want, ('status exit fade row', k, got, want)
    print(f'status: PASS (open at tick {ticks[o]["tick"]}, close at {ticks[c]["tick"]}: the ticks around '
          f'each equal status_04 frames {oo + WINDOW[0]}..{oo + WINDOW[1] - 1} and '
          f'{cc + WINDOW[0]}..{cc + WINDOW[1] - 1} in task bytes, spad, B0/C5/CE and variant; '
          f'{c - o - 1} state-3 ticks without a variant; exit fade identical to route 01_battery '
          f'f{rows[r0]["f"]}..f{rows[r1]["f"]})')


# Route order (docs/FIRST_LEVEL_ROUTE.md section 3; em_level_smoke_test.c
# k_phases). A later step that makes a phase live adds its capture check here
# in the same commit as its runner.
PHASES = [
    ('first_control', check_first_control),
    ('status', check_status),
    ('battery', None),
    ('elevator_refusal', None),
    ('panel', None),
    ('elevator', None),
    ('boxes', None),
    ('slide', None),
    ('truck_preview', None),
    ('truck_crossing', None),
    ('cage_roof', None),
    ('crevice_prompt', None),
    ('crevice_jump', None),
    ('east_tower', None),
    ('roger', None),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--log', type=Path, required=True, help='EM_AREA_CHANGE_LOG of a newgame-level run')
    parser.add_argument('--run-log', type=Path, required=True, help='stderr of the same run')
    args = parser.parse_args()
    run = args.run_log.read_text()
    assert 'level smoke: FAIL' not in run, 'the run reported a failure'
    assert re.search(r'^level smoke: PASS ', run, re.M), 'the run has no final PASS line'
    ticks = [json.loads(line) for line in args.log.open()]
    state = {}
    checked, not_live = [], []
    for name, check in PHASES:
        if re.search(rf'^level smoke: {name}: PASS', run, re.M):
            assert check, f'{name} passed in process but has no capture check here'
            check(ticks, run, state)
            checked.append(name)
        elif re.search(rf'^level smoke: {name}: NOT-LIVE', run, re.M):
            not_live.append(name)
    assert checked and checked == [p[0] for p in PHASES[:len(checked)]], ('phases checked out of order',
                                                                          checked)
    print(f'level smoke: PASS ({len(checked)} live phase(s) checked against the captures: '
          f'{", ".join(checked)}; NOT-LIVE: {", ".join(not_live) if not_live else "none"})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
