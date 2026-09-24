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
    capture. The close must take as many ticks more than the open, counted
    from the tick whose D_00810E74 holds the edge, as in status_04 (two:
    0020E0C0's exit runs case 0 and case 2 after the hub's edge frame).
    The exit fade (001AEDB0 at the close, 001AEE40(0x20) in state 5)
    must equal route 01_battery's status exit row for row, from its first
    fade substate-2 row to the row where the fade is idle again.
battery
    NOT-LIVE, driven through the legacy pickup (WP-6) so the panel phase has
    item 0x1B; its status pop-up (the take's B0 = 1/B1 = 0x1B request, the
    ITEM page's BATTERY acquisition notice, the TRIANGLE exit) is asserted
    in process only (the take and its timing are the legacy owner's). Later phases compare their own windows only
    (the original's leftover B1 = 0x1B from the ITEM request is not compared).
elevator_refusal, elevator
    Aligned on the scan tick (the first tick with 3B8D != 0 after the press,
    the tick whose D_00810750 the run printed) and route 02/04's first row
    with 3B8D != 0; every row through the release (3B8D back to 0) and 25
    rows after it must equal the capture in the spad bytes 0x70003B8C..93,
    the cinematic camera byte D_008101E4, the letterbox block D_0028A8D0,
    the message block D_002821B0 (mode, phase, token; sampled after step F,
    i.e. the next tick's msg_pre), the power byte D_0081084C, the player X/Z
    from the script's placement on, the player Y while the script owns the
    player (the 150 carried values of the ride included), the heading from
    the script's facing on, and the camera eye/target D_008105D0/E0 from the
    script's first shot until the release (the port's follow camera after a
    release is not the original's, WP-16).
panel
    Two windows. From the scan tick through the status open (the original's
    page enters its module load one row later) as above, plus the request
    bytes D_008106B0/B1 from the 00157F60 row on; the task's +B is 3 from the
    open. From the Yes confirmation (B0 0 -> 1, the discharge start) through
    the release and 25 rows after it as above, plus B0/B1, with the player
    Y compared as retained (the script keeps the ground Y of the approach,
    which is navigation input). Between them the port's page must consume
    the request (B0 1 -> 0) for the prompt; its delay is printed, not
    compared: the port's status modules 0x1F/0x21 are resident, the
    original's ITEM root waits 25 frames on its load of module 0x21 (item
    state 3, route 03 f390..f414, a known WP-5 divergence).
"""
import argparse
import json
from pathlib import Path
import re
import struct
import sys

import test_scene_task_reference as tsr

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route'
STATUS_04 = DECOMP / 'build/s87/frame_trace2/status_04.json'
F_001AE5E0 = 0x1AE5E0
SPAD = 0x70003B8C
WINDOW = (-2, 4)          # ticks before / after an alignment tick (exclusive end)
STATUS_04_PRESSES = (10, 200)  # frame_trace2/exp_status.py: TRIANGLE pressed at f10 and f200


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
    # The press-to-+B delays, measured from the tick whose D_00810E74 holds
    # the START/TRIANGLE edge (& 0x810): 0020CDC0's close runs 0020E0C0's
    # exit (case 0, then case 2) after the hub's edge frame, so the close
    # takes two ticks more than the open. status_04 (exp_status.py pressed
    # TRIANGLE at f10 and f200): +B = 3 at f12, +B = 5 at f204.
    orig_extra = (cc - STATUS_04_PRESSES[1]) - (oo - STATUS_04_PRESSES[0])
    e_open = next(i for i in range(fc + 1, o + 1) if tsr.get(bytes.fromhex(ticks[i]['post']), 0x810E74, 2) & 0x810)
    e_close = next(i for i in range(o + 1, c + 1) if tsr.get(bytes.fromhex(ticks[i]['post']), 0x810E74, 2) & 0x810)
    port_extra = (c - e_close) - (o - e_open)
    assert port_extra == orig_extra, ('close latency over the open', port_extra, orig_extra)
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
          f'{c - o - 1} state-3 ticks without a variant; the close takes {port_extra} ticks more than the '
          f'open after its edge, as in status_04; exit fade identical to route 01_battery '
          f'f{rows[r0]["f"]}..f{rows[r1]["f"]})')


# ------------------------------------------------ interaction phases (WP-4)

def f32(bits_):
    return struct.unpack('<f', struct.pack('<I', bits_))[0]


def port_view(ticks, i):
    t = ticks[i]
    b = bytes.fromhex(t['post'])
    nxt = ticks[i + 1]['msg_pre'] if i + 1 < len(ticks) else None
    return {'spad': b[tsr.OFFSET[SPAD]:tsr.OFFSET[SPAD] + 8].hex(), 'cam': t['cam4'][:2],
            'screen': t['screen8'], 'msg': tuple(nxt) if nxt is not None else None, 'power': t['power'],
            'req': b[tsr.OFFSET[0x8106B0]:tsr.OFFSET[0x8106B0] + 2].hex(),
            'pos': [round(f32(v), 5) for v in t['pos_post']], 'yaw': round(f32(t['yaw_post']), 5),
            'eye': [round(f32(v), 5) for v in t['eye_post']], 'tgt': [round(f32(v), 5) for v in t['tgt_post']],
            'task': (b[3], b[4]), 'variants': tsr.get(b, 0x810750, 4)}


def orig_view(row):
    m = bytes.fromhex(row['msg'])
    return {'spad': row['spad'], 'cam': row['cam_mode'][:2], 'screen': row['screen'][:16],
            'msg': struct.unpack('<3I', m[:12]), 'power': row['power'], 'req': row['req'][:4],
            'pos': row['pos'], 'yaw': row['yaw'], 'eye': row['eye'], 'tgt': row['tgt']}


def selector(spad_hex):
    return spad_hex[2:4]


def scan_alignment(ticks, run, phase, beat, after):
    m = re.search(rf'^level smoke: {phase}: PASS scan_d810750=(\d+)', run, re.M)
    assert m, f'no {phase} PASS line with scan_d810750'
    n = int(m.group(1))
    i = next(i for i in range(after, len(ticks)) if port_view(ticks, i)['variants'] == n)
    assert selector(port_view(ticks, i)['spad']) != '00' and selector(port_view(ticks, i - 1)['spad']) == '00', \
        (phase, 'the printed scan tick is not the first tick with 3B8D != 0', ticks[i]['tick'])
    rows = route_rows(beat)
    f = next(k for k in range(1, len(rows)) if selector(rows[k]['spad']) != '00'
             and selector(rows[k - 1]['spad']) == '00')
    return i, rows, f


def compare_window(ticks, i0, rows, f0, count, what, y_mode='scripted', req=False):
    """Row k of the capture against tick i0 + k (both one per frame)."""
    base = orig_view(rows[f0])
    placed = next((k for k in range(count) if rows[f0 + k]['pos'] != base['pos']), count)
    faced = next((k for k in range(count) if rows[f0 + k]['yaw'] != base['yaw']), count)
    shot = next((k for k in range(count) if rows[f0 + k]['eye'] != base['eye']), count)
    port_y0 = port_view(ticks, i0)['pos'][1]
    # The retained ground Y of the approach (navigation input) offsets the
    # scripted camera's Y by the same amount.
    offset = port_y0 - base['pos'][1] if y_mode == 'retained' else 0.0
    for k in range(count):
        assert i0 + k + 1 < len(ticks), (what, 'the tick log ends inside the window', k)
        p, o = port_view(ticks, i0 + k), orig_view(rows[f0 + k])
        where = f'{what} row f{rows[f0 + k]["f"]} (port tick {ticks[i0 + k]["tick"]})'
        for key in ('spad', 'cam', 'screen', 'power'):
            assert p[key] == o[key], (where, key, p[key], o[key])
        if o['msg'][0] != 4:        # mode-4 lines belong to the status page (WP-5)
            assert p['msg'] == o['msg'], (where, 'message block', p['msg'], o['msg'])
        if req:
            assert p['req'] == o['req'], (where, 'D_008106B0/B1', p['req'], o['req'])
        if k >= placed:
            assert (p['pos'][0], p['pos'][2]) == (o['pos'][0], o['pos'][2]), (where, 'player X/Z', p['pos'],
                                                                              o['pos'])
            if y_mode == 'retained':
                assert p['pos'][1] == port_y0, (where, 'player Y not retained', p['pos'][1], port_y0)
            elif selector(o['spad']) != '00':
                assert p['pos'][1] == o['pos'][1], (where, 'player Y', p['pos'][1], o['pos'][1])
        if k >= faced:
            assert p['yaw'] == o['yaw'], (where, 'heading', p['yaw'], o['yaw'])
        if k >= shot and selector(o['spad']) != '00':
            # The script's camera (the camera byte is not 0): eye and target
            # D_008105D0/E0. After the release the port's follow camera is
            # not the original's (WP-16); not compared.
            for vec in ('eye', 'tgt'):
                assert (p[vec][0], p[vec][2]) == (o[vec][0], o[vec][2]) and \
                    abs(p[vec][1] - o[vec][1] - offset) <= 2e-5, (where, 'camera ' + vec, p[vec], o[vec])
    return placed, faced


def release_row(rows, f0):
    return next(k for k in range(f0 + 1, len(rows)) if selector(rows[k]['spad']) == '00')


AFTER_RELEASE = 25


def check_scripted_terminal(ticks, run, state, phase, beat):
    i0, rows, f0 = scan_alignment(ticks, run, phase, beat, state.get('cursor', 0))
    r = release_row(rows, f0)
    count = r - f0 + AFTER_RELEASE
    placed, faced = compare_window(ticks, i0, rows, f0, count, phase)
    state['cursor'] = i0 + count
    return i0, rows, f0, r, count, placed, faced


def check_battery(ticks, run, state):
    """Route 01: 00184BA0 arms the battery owner 00219550 (3B8D = 3), its
    take program 0x266620 runs on the shared owner (3B8D 1, camera byte 2),
    then 001C47A0 posts B0 = 1 / B1 = 0x1B and the status screen opens.

    Window 1 (the scan to the post): spad, camera byte, letterbox, message
    block and D_008106B0/B1 row for row. The post's own row depends on how
    long op00 sub8 settles the camera target from where it starts: the
    runner reaches the route's stance by pad navigation (not exactly) and the
    port's follow camera holds the target at its own height (WP-16), so the
    port settles in a different number of rows. The check therefore requires
    that in both the post comes two rows after the settle's last target
    change (the settled record, then the animation-end wait, then op09), and
    that the settle ends on the item's X/Z. Window 2 (the post to the page's
    module load, WP-5): the same fields row for row, aligned on the post."""
    i0, rows, f0 = scan_alignment(ticks, run, 'battery', '01_battery', state.get('cursor', 0))
    posted_o = next(k for k in range(len(rows) - f0) if orig_view(rows[f0 + k])['req'] == '011b')
    posted_p = next(k for k in range(len(ticks) - i0 - 1) if port_view(ticks, i0 + k)['req'] == '011b')

    def same(k_port, k_orig, where):
        p, o = port_view(ticks, i0 + k_port), orig_view(rows[f0 + k_orig])
        for key in ('spad', 'cam', 'screen', 'power', 'req'):
            assert p[key] == o[key], (where, rows[f0 + k_orig]['f'], key, p[key], o[key])
        if o['msg'][0] != 4:
            assert p['msg'] == o['msg'], (where, rows[f0 + k_orig]['f'], 'message block', p['msg'], o['msg'])

    for k in range(min(posted_o, posted_p)):
        same(k, k, 'battery take')

    def settle_end(target, posted):
        return max(k for k in range(1, posted) if target(k) != target(k - 1))
    end_o = settle_end(lambda k: orig_view(rows[f0 + k])['tgt'], posted_o)
    end_p = settle_end(lambda k: port_view(ticks, i0 + k)['tgt'], posted_p)
    assert posted_o - end_o == posted_p - end_p == 2, ('op09 after the settle', posted_o, end_o, posted_p, end_p)
    for k, tgt in ((end_o, orig_view(rows[f0 + end_o])['tgt']), (end_p, port_view(ticks, i0 + end_p)['tgt'])):
        assert abs(tgt[0] - 211.6) < 1e-3 and abs(tgt[2] - 227.2) < 1e-3, ('the settle ends off the item', k, tgt)
    load = next(k for k in range(posted_o, len(rows) - f0) if rows[f0 + k]['ui'][8:10] == '03')
    for k in range(load - posted_o):
        same(posted_p + k, posted_o + k, 'battery request')
    state['cursor'] = i0 + posted_p + load - posted_o
    print(f'battery: PASS (scan at port tick {ticks[i0]["tick"]} = route 01 f{rows[f0]["f"]}; the take '
          f'program 0x266620 row for row to the post in spad, camera byte, letterbox, message, power and '
          f'B0/B1; B0 = 1 / B1 = 0x1B {posted_p} rows after the scan (original {posted_o}: the camera '
          f'target settle starts from the port follow camera, WP-16, and the pad-navigated stance), two rows '
          f'after the settle in both; from the post to the page load, route f{rows[f0 + posted_o]["f"]}..'
          f'f{rows[f0 + load - 1]["f"]}, row for row)')


def check_elevator_refusal(ticks, run, state):
    i0, rows, f0, r, count, placed, faced = check_scripted_terminal(ticks, run, state, 'elevator_refusal',
                                                                    '02_elevator_refusal')
    assert all(orig_view(rows[f0 + k])['power'] == 0 for k in range(count)), 'the capture powered up'
    print(f'elevator_refusal: PASS (port ticks {ticks[i0]["tick"]}..{ticks[i0 + count - 1]["tick"]} equal route '
          f'02 f{rows[f0]["f"]}..f{rows[f0 + count - 1]["f"]}: refusal 0x82A990 from the scan to the release at '
          f'f{rows[r]["f"]} and {AFTER_RELEASE} rows after, in spad, camera byte, letterbox, message 0x8000001A, '
          f'power; placement from f{rows[f0 + placed]["f"]}, heading from f{rows[f0 + faced]["f"]}, the script\'s '
          f'camera eye/target until the release)')


def check_panel(ticks, run, state):
    i0, rows, f0 = scan_alignment(ticks, run, 'panel', '03_panel_power', state.get('cursor', 0))
    # Window 1: the scan through the status open, the row before the
    # original's page starts its module load (its ui item state 3).
    posted = next(k for k in range(len(rows) - f0) if orig_view(rows[f0 + k])['req'] == '0182')
    load = next(k for k in range(posted, len(rows) - f0) if rows[f0 + k]['ui'][8:10] == '03')
    placed, faced = compare_window(ticks, i0, rows, f0, posted, 'panel open', y_mode='retained')
    compare_window(ticks, i0 + posted, rows, f0 + posted, load - posted, 'panel request', y_mode='retained',
                   req=True)
    opened = next(k for k in range(posted, load) if port_view(ticks, i0 + k)['task'][0] == 3)
    # Between: the port's page consumes the request for the prompt.
    prompt_port = next(k for k in range(load, 2000) if port_view(ticks, i0 + k)['req'] == '0082')
    prompt_orig = next(k for k in range(load, len(rows) - f0) if orig_view(rows[f0 + k])['req'] == '0082')
    # Window 2: from the Yes confirmation (B0 0 -> 1) through the release.
    yes_port = next(k for k in range(prompt_port, 2000) if port_view(ticks, i0 + k)['req'] == '0182')
    yes_orig = next(k for k in range(prompt_orig, len(rows) - f0) if orig_view(rows[f0 + k])['req'] == '0182')
    r = release_row(rows, f0 + yes_orig)
    count = r - (f0 + yes_orig) + AFTER_RELEASE
    compare_window(ticks, i0 + yes_port, rows, f0 + yes_orig, count, 'panel discharge', y_mode='retained',
                   req=True)
    power_row = next(k for k in range(yes_orig, len(rows) - f0) if orig_view(rows[f0 + k])['power'] == 128)
    state['cursor'] = i0 + yes_port + count
    print(f'panel: PASS (scan to the status open: port ticks {ticks[i0]["tick"]}..{ticks[i0 + load - 1]["tick"]} '
          f'equal route 03 f{rows[f0]["f"]}..f{rows[f0 + load - 1]["f"]}, B0 = 1 / B1 = 0x82 at '
          f'f{rows[f0 + posted]["f"]}, +B = 3 at f{rows[f0 + opened]["f"]}; the prompt consumed the request '
          f'{prompt_port - posted} ticks after it (original {prompt_orig - posted}: its page waits on the module '
          f'load, WP-5); from the Yes confirmation f{rows[f0 + yes_orig]["f"]} through the discharge, the exit, '
          f'script 0x247BE0, power 0x80 at f{rows[f0 + power_row]["f"]} and the release at f{rows[r]["f"]} '
          f'+ {AFTER_RELEASE}: port ticks {ticks[i0 + yes_port]["tick"]}..'
          f'{ticks[i0 + yes_port + count - 1]["tick"]} equal in spad, camera byte, letterbox, message, power, '
          f'B0/B1, placement, heading and the script\'s camera eye/target)')


def check_elevator(ticks, run, state):
    i0, rows, f0, r, count, placed, faced = check_scripted_terminal(ticks, run, state, 'elevator',
                                                                    '04_elevator_ride')
    top = rows[f0]['pos'][1]
    carried = [k for k in range(count) if rows[f0 + k]['pos'][1] < top - 0.1 and selector(rows[f0 + k]['spad']) != '00']
    assert len(carried) >= 150, ('the capture window holds no 150-call carry', len(carried))
    print(f'elevator: PASS (port ticks {ticks[i0]["tick"]}..{ticks[i0 + count - 1]["tick"]} equal route 04 '
          f'f{rows[f0]["f"]}..f{rows[f0 + count - 1]["f"]}: powered 0x82A750 from the scan through the carry '
          f'(player Y f{rows[f0 + carried[0]]["f"]}..f{rows[f0 + carried[-1]]["f"]}, ending '
          f'{rows[f0 + carried[-1]]["pos"][1]}) to the release at f{rows[r]["f"]} and {AFTER_RELEASE} rows '
          f'after, in spad, camera byte, letterbox, message, power, placement, heading and the script\'s camera '
          f'eye/target)')


# Route order (docs/FIRST_LEVEL_ROUTE.md section 3; em_level_smoke_test.c
# k_phases). A later step that makes a phase live adds its capture check here
# in the same commit as its runner.
PHASES = [
    ('first_control', check_first_control),
    ('status', check_status),
    ('battery', check_battery),
    ('elevator_refusal', check_elevator_refusal),
    ('panel', check_panel),
    ('elevator', check_elevator),
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
    checked, not_live, driven = [], [], []
    for name, check in PHASES:
        if re.search(rf'^level smoke: {name}: PASS', run, re.M):
            assert check, f'{name} passed in process but has no capture check here'
            check(ticks, run, state)
            checked.append(name)
        elif re.search(rf'^level smoke: {name}: NOT-LIVE driven', run, re.M):
            driven.append(name)   # driven through a legacy binding; never checked
            not_live.append(name)
        elif re.search(rf'^level smoke: {name}: NOT-LIVE', run, re.M):
            not_live.append(name)
    reached = [p[0] for p in PHASES if p[0] in checked or p[0] in driven]
    assert checked and reached == [p[0] for p in PHASES[:len(reached)]], ('phases checked out of order',
                                                                          checked, driven)
    print(f'level smoke: PASS ({len(checked)} live phase(s) checked against the captures: '
          f'{", ".join(checked)}; NOT-LIVE: {", ".join(not_live) if not_live else "none"})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
