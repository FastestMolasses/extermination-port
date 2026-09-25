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
    Y compared as retained while the script owns the player (the script
    keeps the ground Y of the approach, which is navigation input) and as
    the original's after the release (the floor service re-grounds it). Between them the port's page must consume
    the request (B0 1 -> 0) for the prompt; its delay is printed, not
    compared: the port's status modules 0x1F/0x21 are resident, the
    original's ITEM root waits 25 frames on its load of module 0x21 (item
    state 3, route 03 f390..f414, a known WP-5 divergence).
boxes
    Both ledge climbs of route 05 row for row (check_boxes).
slide
    Route 06 from the slide entry (+5 = 0x1C, f72) through the landing, the
    skid-out, the hand-back and 12 idle rows: +5, +1F0, +1F1, clip, clock
    and ground row for row; the heading's values and order with node
    crossings within one row; the per-row motion within 0.0025 off the
    crossing rows; the landed Y within 0.02 (check_slide).
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
            if selector(o['spad']) == '00':
                # After the release the floor service 00175900 re-grounds
                # the player on its first ordinary callback (the elevator
                # actor, the panel's floor): the original's Y exactly.
                assert p['pos'][1] == o['pos'][1], (where, 'player Y after the release', p['pos'][1],
                                                    o['pos'][1])
            elif y_mode == 'retained':
                assert p['pos'][1] == port_y0, (where, 'player Y not retained', p['pos'][1], port_y0)
            else:
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


# ----------------------------------------------------- boxes (census L25)

# Route 05's two climbs: the first row with +5 = 2 (f175 onto crate r4
# 0x7A7C70, f393 onto crate r3 0x7A7980), through the landing (+5 back to 0)
# and the idle return after it (clip 0 counting down from 12), 91 rows each.
BOX_CLIMBS = ((175, 0x7A7C70, 203.776), (393, 0x7A7980, 217.78619))
BOX_ROWS = 91
# The stance the runner walks to is within its navigation tolerance of the
# route's (em_level_smoke_test.c boxes_frame), and the climb's end placement
# follows the wall distance: X/Z are compared as displacement from the entry
# row within this bound; everything else row for row.
BOX_XZ_TOLERANCE = 0.01


def check_boxes(ticks, run, state):
    rows = route_rows('05_boxes')
    live = [i for i in range(1, len(ticks)) if 'player' in ticks[i]]
    # The boxes' climbs are the ledge climbs before the slide (route 06);
    # the later phases' climbs (crevice_climbs, east_tower_climb) have their
    # own checks.
    slide = next((i for i in live if ticks[i]['player'][0] == 0x1C), len(ticks))
    entries = [i for i in live if i < slide and ticks[i]['player'][0] == 2 and ticks[i - 1]['player'][0] != 2]
    assert len(entries) == 2, ('the run entered the ledge climb other than twice before the slide', len(entries))
    notes = []
    for (e, (r0, crate, top)) in zip(entries, BOX_CLIMBS):
        assert rows[r0]['p5'] == 2 and rows[r0 - 1]['p5'] != 2, ('route 05 climb entry moved', r0)
        pe = [f32(v) for v in ticks[e]['pos_post']]
        oe = rows[r0]['pos']
        hang = None
        worst = 0.0
        for k in range(BOX_ROWS):
            t, r = ticks[e + k], rows[r0 + k]
            p = t['player']
            pos = [f32(v) for v in t['pos_post']]
            got = {'p5': p[0], 'm1F0': p[1], 'm1F1': p[2], 'clip': p[3], 'ground': hex(p[5]),
                   'yaw': round(f32(t['yaw_post']), 5)}
            want = {'p5': r['p5'], 'm1F0': r['m1F0'], 'm1F1': r['m1F1'], 'clip': r['clip'],
                    'ground': r['ground'], 'yaw': r['yaw']}
            # The entry row's clock is the idle clip's before the climb's
            # request (its phase is the press timing, navigation input).
            if k:
                got['clock'], want['clock'] = round(f32(p[4]), 3), r['clock']
            assert got == want, ('boxes climb row', r['f'], k, got, want)
            # Y: from the entry the lift is relative to the stance's floor;
            # once the original holds its hang height (and on the crate top)
            # it is absolute, equal row for row.
            if hang is None and k and r['clip'] == 0x78 and r['pos'][1] == rows[r0 + k - 1]['pos'][1] \
                    and r['pos'][1] != oe[1]:
                hang = k
            if hang is not None:
                assert round(pos[1], 5) == r['pos'][1], ('boxes climb Y', r['f'], k, pos[1], r['pos'][1])
            else:
                assert abs((pos[1] - pe[1]) - (r['pos'][1] - oe[1])) < 1e-4, \
                    ('boxes climb lift', r['f'], k, pos[1] - pe[1], r['pos'][1] - oe[1])
            for a in (0, 2):
                worst = max(worst, abs((pos[a] - pe[a]) - (r['pos'][a] - oe[a])))
        assert hang is not None, ('no hang rows in the climb window', r0)
        assert worst <= BOX_XZ_TOLERANCE, ('boxes climb X/Z displacement', r0, worst)
        land = next(k for k in range(BOX_ROWS) if rows[r0 + k]['p5'] == 0)
        assert rows[r0 + land]['ground'] == hex(crate) and rows[r0 + land]['pos'][1] == round(top, 5)
        notes.append(f'f{rows[r0]["f"]}..f{rows[r0 + BOX_ROWS - 1]["f"]} (hang from f{rows[r0 + hang]["f"]}, '
                     f'on {crate:#x} at y {top} from f{rows[r0 + land]["f"]}, X/Z within {worst:.4f})')
    print(f'boxes: PASS (both ledge climbs equal route 05 row for row in +5, +1F0, +1F1, clip, clock, ground, '
          f'heading and Y (the lift relative to the stance): {"; ".join(notes)})')


# ------------------------------------------------ slide (census L03)

# Route 06: the first row with +5 = 0x1C (f72, entered by 001796C0 from the
# hill's authored class-0x1000 grid nodes) through the slide (0016C6A0 with
# clips 0x5E / 0x61), the landing (+1F0 0x30 -> 0, f138), the skid-out
# (clips 0x60 / 0x65), the hand-back to state 0 (f181) and 12 idle rows.
SLIDE_ROWS = 122
# The entry point is where the port's walk (navigation input, and the
# legacy follow camera the stick is steered against, WP-16) first meets a
# class-0x1000 node: about 0.6 from the original's in X/Z. The slide then
# crosses the authored nodes' boundaries (the downhill heading +218 and the
# slope +9C change) at rows that follow that entry point, so:
# - the heading takes the original's values in the original's order, each
#   change within SLIDE_CROSSING_ROWS of the original's row;
# - the per-row motion (the step from the previous row) equals the
#   original's within SLIDE_STEP_TOLERANCE except on a crossing row (either
#   run's heading changes there or on a neighbour) and on the landing row;
#   a crossing one row early changes the speed gain of that row
#   (0.01 sin(slope)), which is the constant residual after it;
# - the landing row and every later step are exact in timing; the landed Y
#   is the floor under the port's own X/Z, compared absolutely within
#   SLIDE_LAND_Y_TOLERANCE.
SLIDE_CROSSING_ROWS = 1
SLIDE_STEP_TOLERANCE = 0.0025
SLIDE_LAND_Y_TOLERANCE = 0.02


def check_slide(ticks, run, state):
    rows = route_rows('06_hill_slide')
    live = [i for i in range(1, len(ticks)) if 'player' in ticks[i]]
    entries = [i for i in live if ticks[i]['player'][0] == 0x1C and ticks[i - 1]['player'][0] != 0x1C]
    assert len(entries) == 1, ('the run entered the slope slide other than once', len(entries))
    e = entries[0]
    r0 = next(k for k in range(1, len(rows)) if rows[k]['p5'] == 0x1C and rows[k - 1]['p5'] != 0x1C)
    assert rows[r0]['f'] == 72, ('route 06 slide entry moved', rows[r0]['f'])
    assert e + SLIDE_ROWS < len(ticks), ('the tick log ends inside the slide window', len(ticks) - e)
    pos = [[f32(v) for v in ticks[e + k]['pos_post']] for k in range(SLIDE_ROWS)]
    yaw = [round(f32(ticks[e + k]['yaw_post']), 5) for k in range(SLIDE_ROWS)]
    orig = [rows[r0 + k] for k in range(SLIDE_ROWS)]
    # State, action, +1F1, clip, ground: row for row; the clock from the row
    # after the entry (the entry row's clock is the walk clip's, whose phase
    # at the entry is navigation input).
    for k in range(SLIDE_ROWS):
        p, r = ticks[e + k]['player'], orig[k]
        got = {'p5': p[0], 'm1F0': p[1], 'm1F1': p[2], 'clip': p[3], 'ground': hex(p[5])}
        want = {'p5': r['p5'], 'm1F0': r['m1F0'], 'm1F1': r['m1F1'], 'clip': r['clip'], 'ground': r['ground']}
        if k:
            got['clock'], want['clock'] = round(f32(p[4]), 3), r['clock']
        assert got == want, ('slide row', r['f'], k, got, want)
    land = next(k for k in range(SLIDE_ROWS) if orig[k]['m1F0'] == 0)
    back = next(k for k in range(SLIDE_ROWS) if orig[k]['p5'] == 0)
    assert orig[land]['f'] == 138 and orig[back]['f'] == 181, ('route 06 landing / hand-back moved', land, back)
    # Heading: from row 2 (0016C6A0 sub-state 1 turns +C4 onto +218 at
    # 0.10471976 per tick; rows 0/1 carry the walk's heading).
    def changes(seq):
        return [(k, seq[k]) for k in range(3, len(seq)) if seq[k] != seq[k - 1]]
    ours, theirs = changes(yaw[:land + 1]), changes([r['yaw'] for r in orig[:land + 1]])
    assert yaw[2] == orig[2]['yaw'], ('slide heading after the turn', yaw[2], orig[2]['yaw'])
    assert [v for _, v in ours] == [v for _, v in theirs], ('slide heading sequence', ours, theirs)
    shift = max(abs(a - b) for (a, _), (b, _) in zip(ours, theirs)) if ours else 0
    assert shift <= SLIDE_CROSSING_ROWS, ('slide node crossing moved', ours, theirs)
    assert yaw[land:] == [r['yaw'] for r in orig[land:]], ('heading after the landing', yaw[land:])
    crossing = {k + d for k, _ in ours + theirs for d in (-1, 0, 1)}
    worst = 0.0
    for k in range(1, SLIDE_ROWS):
        if k in crossing or k == land:
            continue
        for a in range(3):
            d = abs((pos[k][a] - pos[k - 1][a]) - (orig[k]['pos'][a] - orig[k - 1]['pos'][a]))
            assert d <= SLIDE_STEP_TOLERANCE, ('slide step', orig[k]['f'], k, 'xyz'[a], d)
            worst = max(worst, d)
    land_dy = abs(pos[land][1] - orig[land]['pos'][1])
    assert land_dy <= SLIDE_LAND_Y_TOLERANCE, ('landed Y', pos[land][1], orig[land]['pos'][1])
    after = max(abs((pos[k][a] - pos[k - 1][a]) - (orig[k]['pos'][a] - orig[k - 1]['pos'][a]))
                for k in range(land + 1, SLIDE_ROWS) for a in range(3))
    entry_xz = max(abs(pos[0][a] - orig[0]['pos'][a]) for a in (0, 2))
    state['cursor'] = e + SLIDE_ROWS
    print(f'slide: PASS (route 06 f{orig[0]["f"]}..f{orig[-1]["f"]} row for row in +5, +1F0, +1F1, clip, clock and '
          f'ground: the entry at f72, clips 0x5E/0x61, the landing at f{orig[land]["f"]}, the skid-out 0x60/0x65, '
          f'the hand-back at f{orig[back]["f"]} and the idle return; the heading takes the original values '
          f'{[v for _, v in theirs]} with crossings within {shift} row(s); per-row motion within {worst:.5f} off the '
          f'crossing rows (entry X/Z {entry_xz:.3f} from the original\'s), after the landing within {after:.5f}; '
          f'landed Y {pos[land][1]:.5f} against {orig[land]["pos"][1]})')


# ------------------------------------------ truck preview (census L23, L19)

def first_frame_row(rows):
    return next(k for k in range(1, len(rows)) if selector(rows[k]['spad']) != '00'
                and selector(rows[k - 1]['spad']) == '00')


def check_truck_preview(ticks, run, state):
    """Route 07: the trigger 008251E0 starts 0x8292C0 on the frame the
    player's +0xA0 is in its band; the next frame's op07/2 opens the
    scripted frame (3B8D = 2, camera byte 1, the bars). Aligned on the first
    tick with 3B8D != 0 after the slide and route 07's first such row (f164),
    every row through the release (07/4, f527) and 25 rows after it is
    compared as for the terminal scripts (compare_window: spad, camera byte,
    letterbox, message, power, the placement 01/1 from f167, the heading
    04/8 from f168, the camera shots from f169, the re-grounded Y after the
    release), plus D_00810792 row for row (1 from the release row: the
    trigger's store after 001BA1F0 returned 1) and, from the row after the
    first, the player record's +5, +1F0, +1F1, clip and ground: 0015B130's
    admission (+5 = 0, +1F0 = 0x41, f165) and 00182DF0's release tail
    (+1F0 = 0, f527). The trigger freeing itself (route f528) is asserted in
    process."""
    rows = route_rows('07_truck_preview')
    f0 = first_frame_row(rows)
    assert rows[f0]['f'] == 164, ('route 07 script frame moved', rows[f0]['f'])
    start = max(state.get('cursor', 0), 1)
    i0 = next(i for i in range(start, len(ticks)) if selector(port_view(ticks, i)['spad']) != '00'
              and selector(port_view(ticks, i - 1)['spad']) == '00')
    r = release_row(rows, f0)
    count = r - f0 + AFTER_RELEASE
    placed, faced = compare_window(ticks, i0, rows, f0, count, 'truck_preview')
    for k in range(count):
        assert ticks[i0 + k]['story792'] == rows[f0 + k]['story792'], \
            ('truck_preview D_00810792', rows[f0 + k]['f'], ticks[i0 + k]['story792'], rows[f0 + k]['story792'])
        # The player record from the admission row on (the row before is
        # the approach's walk, navigation input): 0015B130's admission +5 = 0
        # / +1F0 = 0x41 (f165), 00182DF0's release tail (f527) and the idle
        # after it.
        if k:
            p, o = ticks[i0 + k]['player'], rows[f0 + k]
            got = (p[0], p[1], p[2], p[3], hex(p[5]))
            want = (o['p5'], o['m1F0'], o['m1F1'], o['clip'], o['ground'])
            assert got == want, ('truck_preview player +5/+1F0/+1F1/clip/ground', o['f'], got, want)
    state['cursor'] = i0 + count
    print(f'truck_preview: PASS (port ticks {ticks[i0]["tick"]}..{ticks[i0 + count - 1]["tick"]} equal route 07 '
          f'f{rows[f0]["f"]}..f{rows[f0 + count - 1]["f"]}: 0x8292C0 from its frame to the release at '
          f'f{rows[r]["f"]} and {AFTER_RELEASE} rows after, in spad, camera byte, letterbox, message, power, '
          f'D_00810792 and the player record (+5, +1F0, +1F1, clip, ground: the admission and the release); placement from f{rows[f0 + placed]["f"]}, heading from f{rows[f0 + faced]["f"]}, the '
          f'script\'s camera eye/target until the release, the re-grounded Y after it)')



# ------------------------------------------------ truck crossing (census L23)

TRUCK_RECORD = 0x7A9FB0         # route_capture.py truck_r16 (placement record 16)
TRUCK_AFTER_REST = 10           # rows compared after the fall's last beat


def truck_view_port(tick):
    t = tick['truck']
    assert t is not None, ('no live truck node in the tick', tick['tick'])
    record, head, pos, t2dc = t
    return {'record': record, 'h': head, 'pos': [round(f32(v), 5) for v in pos], 't2DC': t2dc,
            'story': tick['story792']}


def truck_view_orig(row):
    t = row['truck_r16']
    return {'record': TRUCK_RECORD, 'h': t['h'], 'pos': t['pos'], 't2DC': t['t2DC'], 'story': row['story792']}


def check_truck_crossing(ticks, run, state):
    """Route 08: standing on the truck (the player's +0x214 = the truck
    record, whose +0x0D is 9, and +0x0A != 0) arms 00823FF0 (state 4:
    001B1E20(0, 0), +0x2EC = 1, f43); +0x2EC counts through the shake to
    0x2F (state 1 from f89), the 119 fall beats move +0xB0 and the matrix,
    and the last beat stores D_00810792 = 0xFF with state 2 (f209). The
    arm row is aligned (the first row with +0x2EC = 1; the approach is
    navigation input) and from it every row through the rest and
    TRUCK_AFTER_REST rows after is compared: the truck record's
    +0x00..+0x0F, +0xB0 (to the capture's 5 decimals), +0x2DC..+0x2EF and
    D_00810792, and the player's ground on the arm row (the truck record).
    The player's own walk off the truck is navigation (the legacy
    locomotion, WP-15/L12); its end on the low ground is asserted in
    process."""
    rows = route_rows('08_truck_crossing')
    shake = lambda t2dc: int.from_bytes(bytes.fromhex(t2dc)[16:20], 'little')
    r0 = next(k for k in range(len(rows)) if shake(rows[k]['truck_r16']['t2DC']) == 1)
    assert rows[r0]['f'] == 43, ('route 08 arm moved', rows[r0]['f'])
    rest = next(k for k in range(r0, len(rows)) if rows[k]['story792'] == 0xFF)
    start = max(state.get('cursor', 0), 1)
    i0 = next(i for i in range(start, len(ticks)) if ticks[i].get('truck') is not None
              and shake(ticks[i]['truck'][3]) == 1)
    assert ticks[i0]['player'][5] == TRUCK_RECORD and rows[r0]['ground'] == hex(TRUCK_RECORD), \
        ('the arm row does not stand on the truck', hex(ticks[i0]['player'][5]), rows[r0]['ground'])
    count = rest - r0 + 1 + TRUCK_AFTER_REST
    assert i0 + count < len(ticks), ('the tick log ends inside the truck window', len(ticks) - i0)
    fell = None
    for k in range(count):
        p, o = truck_view_port(ticks[i0 + k]), truck_view_orig(rows[r0 + k])
        assert p == o, ('truck record', rows[r0 + k]['f'], k, p, o)
        if fell is None and o['h'][8:10] == '01':
            fell = k
    state['cursor'] = i0 + count
    print(f'truck_crossing: PASS (port ticks {ticks[i0]["tick"]}..{ticks[i0 + count - 1]["tick"]} equal route 08 '
          f'f{rows[r0]["f"]}..f{rows[r0 + count - 1]["f"]} in the truck record (+0x00..+0x0F, +0xB0, +0x2DC..+0x2EF) '
          f'and D_00810792: armed from the truck top at f{rows[r0]["f"]}, falling from f{rows[r0 + fell]["f"]}, '
          f'0xFF and state 2 at f{rows[rest]["f"]} (y {rows[rest]["truck_r16"]["pos"][1]}), {TRUCK_AFTER_REST} '
          f'rows at rest)')


# ------------------------------------ ladders, climbs, jump (census L09..L11)

def state_entries(ticks, value, start=1, end=None):
    """The ticks where the player record's +5 becomes `value`."""
    end = len(ticks) if end is None else end
    return [i for i in range(max(start, 1), end) if 'player' in ticks[i] and 'player' in ticks[i - 1]
            and ticks[i]['player'][0] == value and ticks[i - 1]['player'][0] != value]


def route_entries(rows, value):
    return [k for k in range(1, len(rows)) if rows[k]['p5'] == value and rows[k - 1]['p5'] != value]


def record_row(tick, row, k):
    """+5, +1F0, +1F1, clip, ground and heading of a port tick and a route
    row; the clock from the row after the entry (the entry row's clock is
    the previous clip's, whose phase at the press is navigation input)."""
    p = tick['player']
    got = {'p5': p[0], 'm1F0': p[1], 'm1F1': p[2], 'clip': p[3], 'ground': hex(p[5]),
           'yaw': round(f32(tick['yaw_post']), 5)}
    want = {'p5': row['p5'], 'm1F0': row['m1F0'], 'm1F1': row['m1F1'], 'clip': row['clip'],
            'ground': row['ground'], 'yaw': row['yaw']}
    if k:
        got['clock'], want['clock'] = round(f32(p[4]), 3), row['clock']
    return got, want


# The walks of beats 10, 11 and 12 step off an edge once each (route 10
# f88, 11 f498, 12 f42): the fall 5 / 0xB (00162DB0), the landing 8 / 0xF
# (clip 0x6E) and its recovery 8 / 1 into the walk state 1. The edge and
# the speed are navigation input, the rest is the fall's.
FALL_AFTER_HANDBACK = 2


def check_fall(ticks, e, rows, r0, what):
    """From the port's fall entry e and the route's r0: +5, +1F0, +1F1,
    clip and clock (once the fall's clip has replaced the walk's) row for
    row and the Y as
    the drop from the entry row, through the landing; from the landing
    (aligned on the first row with +5 = 8 in each run) through the
    hand-back to state 1 and FALL_AFTER_HANDBACK rows: +5, +1F0, +1F1, clip
    and clock row for row. The landing must come on the same row as the
    original's when both falls start at the same height (to 0.001); a
    different start height (the walk's own edge point, navigation input)
    may move it by one row. Returns a note."""
    land_p = next(k for k in range(1, 200) if ticks[e + k]['player'][0] == 8)
    land_o = next(k for k in range(1, 200) if rows[r0 + k]['p5'] == 8)
    ye_p, ye_o = f32(ticks[e]['pos_post'][1]), rows[r0]['pos'][1]
    same_height = abs(ye_p - ye_o) < 1e-3
    assert land_p == land_o or (not same_height and abs(land_p - land_o) == 1), \
        (what, 'fall landing row', land_p, land_o, ye_p, ye_o)
    walk_clip = rows[r0]['clip']
    for k in range(1, min(land_p, land_o)):
        # The walk clip's clock (its phase at the edge) until the fall's
        # request replaces the clip.
        got, want = record_row(ticks[e + k], rows[r0 + k], k if rows[r0 + k]['clip'] != walk_clip else 0)
        got.pop('yaw'), want.pop('yaw')
        assert got == want, (what, 'fall row', rows[r0 + k]['f'], k, got, want)
        drop = (f32(ticks[e + k]['pos_post'][1]) - ye_p) - (rows[r0 + k]['pos'][1] - ye_o)
        assert abs(drop) < 1e-4, (what, 'fall drop', rows[r0 + k]['f'], k, drop)
    back = next(k for k in range(land_o, 200) if rows[r0 + k]['p5'] == 1)
    for j in range(back - land_o + 1 + FALL_AFTER_HANDBACK):
        # The landing row still shows the fall clip's clock, which counts
        # from the entry: compared only when both land on the same row.
        got, want = record_row(ticks[e + land_p + j], rows[r0 + land_o + j], 1 if j or land_p == land_o else 0)
        got.pop('yaw'), want.pop('yaw')
        assert got == want, (what, 'landing row', rows[r0 + land_o + j]['f'], j, got, want)
    return (f'the fall f{rows[r0]["f"]} (landing f{rows[r0 + land_o]["f"]}'
            f'{"" if land_p == land_o else f", the port one row {"earlier" if land_p < land_o else "later"} from its own edge height"}'
            f', hand-back f{rows[r0 + back]["f"]})')


def fall_between(ticks, start, end, rows, what):
    falls = state_entries(ticks, 5, start, end)
    r_falls = route_entries(rows, 5)
    assert len(falls) == 1 and len(r_falls) == 1, (what, 'the walk did not step off one edge', falls, r_falls)
    return check_fall(ticks, falls[0], rows, r_falls[0], what)


# Route 10's two ladder climbs on the x 360 column (FIRST_LEVEL_ROUTE.md
# beat 10): from the first row with +5 = 0xB (0015D4C0 case 0x32, f268 and
# f780) through the hand-back (ladder A: the dismount's 0017C440 re-entry
# into state 1, f581; ladder B: f1090, the last row before director beat 0
# takes the player, f1091).
LADDER_WINDOWS = ((268, 581), (780, 1090))


def check_cage_ladders(ticks, run, state):
    """Both climbs row for row: +5, +1F0, +1F1, clip, clock, ground and the
    heading exactly; X/Z exactly (00177030 mode 4 places +B0/+B8 on the
    column node's centre, 00199DB0, so the stance, navigation input, does
    not carry into them); Y as the lift from the entry row while the
    player is on the ladder (+1F0 0x15 / 0x17 / 0x18: the entry keeps the
    stance's ground Y, which is the floor under the port's own approach) and
    exactly from the hand-back on (the dismount sets the cage floor and the
    roof). The press itself and the stick's up push are navigation; the
    runner pushes the stick at the capture's pad latency
    (em_level_smoke_test.c ladder_climb)."""
    rows = route_rows('10_cage_roof_roger')
    assert [rows[k]['f'] for k in route_entries(rows, 0xB)] == [w[0] for w in LADDER_WINDOWS], \
        ('route 10 ladder entries moved', route_entries(rows, 0xB))
    start = max(state.get('cursor', 0), 1)
    entries = state_entries(ticks, 0xB, start)
    assert len(entries) == 2, ('the run entered the ladder other than twice', len(entries))
    notes = [fall_between(ticks, start, entries[0], rows, 'cage_ladders')]
    for e, (f0, f1) in zip(entries, LADDER_WINDOWS):
        r0 = next(k for k in range(len(rows)) if rows[k]['f'] == f0)
        count = f1 - f0 + 1
        assert e + count < len(ticks), ('the tick log ends inside a ladder window', f0)
        pe, oe = f32(ticks[e]['pos_post'][1]), rows[r0]['pos'][1]
        lift_worst = 0.0
        for k in range(count):
            t, r = ticks[e + k], rows[r0 + k]
            got, want = record_row(t, r, k)
            assert got == want, ('ladder row', r['f'], k, got, want)
            pos = [f32(v) for v in t['pos_post']]
            for a in (0, 2):
                assert round(pos[a], 5) == r['pos'][a], ('ladder X/Z', r['f'], k, pos, r['pos'])
            if r['m1F0'] in (0x15, 0x17, 0x18):
                lift = abs((pos[1] - pe) - (r['pos'][1] - oe))
                lift_worst = max(lift_worst, lift)
                assert lift < 1e-4, ('ladder lift', r['f'], k, pos[1] - pe, r['pos'][1] - oe)
            else:
                assert round(pos[1], 5) == r['pos'][1], ('ladder Y after the hand-back', r['f'], k, pos[1],
                                                         r['pos'][1])
        climb = [k for k in range(count) if rows[r0 + k]['m1F0'] == 0x17]
        top = next(k for k in range(count) if rows[r0 + k]['m1F0'] == 0x18)
        notes.append(f'f{f0}..f{f1} (entry 0xB, climb 0x17 f{rows[r0 + climb[0]]["f"]}, dismount 0x18 '
                     f'f{rows[r0 + top]["f"]}, on y {rows[r0 + count - 1]["pos"][1]}; lift within {lift_worst:.1e})')
        state['cursor'] = e + count
    print(f'cage_ladders: PASS (the walk\'s step-off and both climbs equal route 10: {notes[0]}; the climbs row '
          f'for row in +5, +1F0, +1F1, clip, clock, ground, heading and X/Z, Y as the lift on the ladder and '
          f'exactly after it: {"; ".join(notes[1:])})')


def check_climbs(ticks, state, beat, phase, windows):
    """Ledge climbs (0015DF10, state 2) of a route beat, aligned on the
    first row with +5 = 2: +5, +1F0, +1F1, clip, clock, ground, heading and
    Y row for row (the stances stand on the same floor); X/Z as the
    displacement from the entry row within the distance between the two
    stances (navigation input: 0015DF10 carries the stance along the wall
    and places it at the wall's distance, so the stance offset bounds the
    residual) plus 0.001."""
    rows = route_rows(beat)
    r_entries = route_entries(rows, 2)
    assert [rows[k]['f'] for k in r_entries] == [w[0] for w in windows], (beat, 'climb entries moved', r_entries)
    start = max(state.get('cursor', 0), 1)
    entries = state_entries(ticks, 2, start)[:len(windows)]
    assert len(entries) == len(windows), (phase, 'the run entered the ledge climb fewer times', len(entries))
    notes = []
    for e, r0, (f0, f1) in zip(entries, r_entries, windows):
        count = f1 - f0 + 1
        assert e + count < len(ticks), (phase, 'the tick log ends inside a climb window', f0)
        pe = [f32(v) for v in ticks[e]['pos_post']]
        oe = rows[r0]['pos']
        stance = ((pe[0] - oe[0]) ** 2 + (pe[2] - oe[2]) ** 2) ** 0.5
        worst = 0.0
        for k in range(count):
            t, r = ticks[e + k], rows[r0 + k]
            got, want = record_row(t, r, k)
            assert got == want, (phase, 'climb row', r['f'], k, got, want)
            pos = [f32(v) for v in t['pos_post']]
            assert round(pos[1], 5) == r['pos'][1], (phase, 'climb Y', r['f'], k, pos[1], r['pos'][1])
            d = (((pos[0] - pe[0]) - (r['pos'][0] - oe[0])) ** 2 + ((pos[2] - pe[2]) - (r['pos'][2] - oe[2])) ** 2) ** 0.5
            worst = max(worst, d)
        assert worst <= stance + 1e-3, (phase, 'climb X/Z displacement', f0, worst, stance)
        land = next(k for k in range(count) if rows[r0 + k]['p5'] != 2)
        notes.append(f'f{f0}..f{f1} (onto y {rows[r0 + land]["pos"][1]} at f{rows[r0 + land]["f"]}; stance '
                     f'{stance:.3f} from the original\'s, X/Z residual {worst:.4f})')
        state['cursor'] = e + count
    return notes


# Route 11: the tank climb f172 through the idle return (f276; at f277 the
# original's walk to the pipes starts) and the pipe-end climb f642 through
# its landing (f706, the row director beat 1 claims: 3B8D = 3).
CREVICE_CLIMBS = ((172, 276), (642, 706))
# Route 13: the east tower climb f438 through its landing (f531, director
# beat 2's claim row).
TOWER_CLIMBS = ((438, 531),)


def check_crevice_climbs(ticks, run, state):
    start = max(state.get('cursor', 0), 1)
    climbs = state_entries(ticks, 2, start)
    assert len(climbs) >= 2, ('crevice_climbs: fewer than two ledge climbs', len(climbs))
    fall = fall_between(ticks, climbs[0], climbs[1], route_rows('11_crevice_prompt'), 'crevice_climbs')
    notes = check_climbs(ticks, state, '11_crevice_prompt', 'crevice_climbs', CREVICE_CLIMBS)
    print(f'crevice_climbs: PASS (the tank and pipe-end ledge climbs equal route 11 row for row in +5, +1F0, '
          f'+1F1, clip, clock, ground, heading and Y, X/Z within the stance offset: {"; ".join(notes)}; the pipes '
          f'walk\'s step-off: {fall})')


def check_east_tower_climb(ticks, run, state):
    notes = check_climbs(ticks, state, '13_east_tower', 'east_tower_climb', TOWER_CLIMBS)
    print(f'east_tower_climb: PASS (the high ledge climb equals route 13 row for row in +5, +1F0, +1F1, clip, '
          f'clock, ground, heading and Y, X/Z within the stance offset: {"; ".join(notes)})')


# Route 12: the running jump from its entry (+5 = 6, f230) through the
# landing (8 / 0xF, f277), the recovery 8 / 1 and the hand-back into the
# walk state 1 (f304).
JUMP_WINDOW = (230, 304)
JUMP_STEP_TOLERANCE = 1e-4
JUMP_MIN_FREE_ROWS = 30


def check_crevice_jump(ticks, run, state):
    """Row for row: +5, +1F0, +1F1, clip, clock, ground and Y (the arc; both
    take off from the plateau's floor 269.647). The heading is 0015EC50's
    take-off heading, the stick's direction at the press (navigation):
    constant through the window in both runs, compared with the port's own
    entry row. Horizontally 001634A0 carries the player along that heading;
    the per-row step length must equal the original's within
    JUMP_STEP_TOLERANCE on every row where both runs step along their
    headings (free flight; the rows where the arc slides along the north
    block's edge depend on the lateral offset the heading makes), at least
    JUMP_MIN_FREE_ROWS of them."""
    import math
    rows = route_rows('12_crevice_jump')
    r_entries = route_entries(rows, 6)
    assert [rows[k]['f'] for k in r_entries] == [JUMP_WINDOW[0]], ('route 12 jump entry moved', r_entries)
    r0 = r_entries[0]
    start = max(state.get('cursor', 0), 1)
    # The phase ends where the east tower's ledge climb (+5 = 2) begins;
    # beat 14's tower jump (the roger phase) is a later running jump.
    climbs = state_entries(ticks, 2, start)
    entries = state_entries(ticks, 6, start, climbs[0] if climbs else None)
    assert len(entries) == 1, ('the run entered the running jump other than once', len(entries))
    e = entries[0]
    fall = fall_between(ticks, start, e, rows, 'crevice_jump')
    count = JUMP_WINDOW[1] - JUMP_WINDOW[0] + 1
    assert e + count < len(ticks), ('the tick log ends inside the jump window', len(ticks) - e)
    heading_p = round(f32(ticks[e]['yaw_post']), 5)
    heading_o = rows[r0]['yaw']
    free, landing = 0, None
    for k in range(count):
        t, r = ticks[e + k], rows[r0 + k]
        got, want = record_row(t, r, k)
        assert got['yaw'] == heading_p and want['yaw'] == heading_o, ('jump heading changed', r['f'], k)
        got['yaw'] = want['yaw'] = None
        assert got == want, ('jump row', r['f'], k, got, want)
        pos = [f32(v) for v in t['pos_post']]
        assert round(pos[1], 5) == r['pos'][1], ('jump Y', r['f'], k, pos[1], r['pos'][1])
        if landing is None and r['p5'] == 8:
            landing = k
        if k:
            prev = [f32(v) for v in ticks[e + k - 1]['pos_post']]
            po, pr = r['pos'], rows[r0 + k - 1]['pos']
            dp = (pos[0] - prev[0], pos[2] - prev[2])
            do = (po[0] - pr[0], po[2] - pr[2])
            sp, so = math.hypot(*dp), math.hypot(*do)
            if sp > 0 and so > 0 and abs(math.atan2(*dp) - heading_p) < 1e-3 and \
                    abs(math.atan2(*do) - heading_o) < 1e-3:
                assert abs(sp - so) <= JUMP_STEP_TOLERANCE, ('jump step length', r['f'], k, sp, so)
                free += 1
    assert free >= JUMP_MIN_FREE_ROWS, ('too few free-flight rows compared', free)
    state['cursor'] = e + count
    print(f'crevice_jump: PASS (route 12 f{JUMP_WINDOW[0]}..f{JUMP_WINDOW[1]} row for row in +5, +1F0, +1F1, clip, '
          f'clock, ground and Y: the jump 6 / 0x0C (clips 0x69 / 0x6B), the landing 8 / 0xF at '
          f'f{rows[r0 + landing]["f"]} (clip 0x6E) and the hand-back; the step length equals the original\'s '
          f'within {JUMP_STEP_TOLERANCE} on {free} free-flight rows; take-off heading {heading_p} against '
          f'{heading_o} (navigation); the approach\'s step-off off the pipe: {fall})')


# Route order (docs/FIRST_LEVEL_ROUTE.md section 3; em_level_smoke_test.c
# k_phases). A later step that makes a phase live adds its capture check here
# in the same commit as its runner.
# ------------------------------------------------ Roger's encounter (census L22)

ROGER_SCRIPT = 'd0838200'       # the script block's pc 0x008283D0 (little-endian)
ROGER_CLIP_PC = 'd0858200'      # pc 0x008285D0: after the op0B sub 4 clip init


def roger_view_port(tick):
    r = tick['roger']
    assert r is not None, ('no live Roger node in the tick', tick['tick'])
    _record, head, pos, block = r
    return {'h': head, 'pos': [round(f32(v), 5) for v in pos], 'block': block}


def roger_view_orig(row):
    r = row['roger_r8']
    return {'h': r['h'], 'pos': r['pos'], 'block': r['s1F0']}


def equipment_view_port(tick):
    e = tick.get('equipment')
    assert e is not None, ('no live equipment node in the tick', tick['tick'])
    _record, head, pos = e
    return {'h': head, 'pos': [round(f32(v), 5) for v in pos]}


def equipment_view_orig(row):
    r = row['attach_r9']
    return {'h': r['h'], 'pos': r['pos']}


def check_roger(ticks, run, state):
    """Route 14: in mid-air the player crosses into Roger's quad 0x82AB80;
    Roger 008237E0's ordinary branch starts 0x8283D0 (f283: +5 = 1, the
    block active with pc 0x8283D0). Its op16 (00182BF0) holds until the
    player lands (f288 in the original: the landing row follows the port's
    own jump, navigation), then 001B82D0 sub 12 opens the scripted frame.
    Two windows:
    - the start: the port's first tick with the block at pc 0x8283D0 against
      route f283 in Roger's +0x00..+0x0F, +0xB0 and block, and every tick
      until the port's frame opens with the block still at the op16;
    - from the first tick with 3B8D != 0 (route f288) every row to the end
      of the capture (the release at f1758 and 60 rows): the spad bytes, the
      camera byte, the letterbox, the fade block 0x28A9A0 (the next tick's
      start sample), the message block, Roger's +0x00..+0x0F, +0xB0 and
      block, the equipment node's +0x00..+0x0F and, from Roger's clip init
      (op0B sub 4, f358; before it his idle clip's phase is the time since
      the area load), its +0xB0 (001C5C90's copy of Roger's node 1),
      D_008107D8 and D_00810813, the player record's +5,
      +1F0, +1F1, clip, clock and +0x2F3, the camera eye / target while the camera byte is 3
      (the bank 0x96 timeline, 0022EEF0) or the scripted frame holds a shot,
      and the player position and heading from the 01/9 placement (f1756)
      on."""
    rows = route_rows('14_roger_encounter')
    s0 = next(k for k in range(len(rows)) if rows[k]['roger_r8']['s1F0'][16:24] == ROGER_SCRIPT
              and rows[k]['roger_r8']['s1F0'][:8] == '01000000')
    assert rows[s0]['f'] == 283, ('route 14 script start moved', rows[s0]['f'])
    f0 = next(k for k in range(s0, len(rows)) if selector(rows[k]['spad']) != '00')
    assert rows[f0]['f'] == 288, ('route 14 frame moved', rows[f0]['f'])
    start = max(state.get('cursor', 0), 1)
    t0 = next(i for i in range(start, len(ticks)) if ticks[i].get('roger') is not None
              and ticks[i]['roger'][3][16:24] == ROGER_SCRIPT and ticks[i]['roger'][3][:8] == '01000000')
    i0 = next(i for i in range(t0, len(ticks)) if selector(port_view(ticks, i)['spad']) != '00')
    got, want = roger_view_port(ticks[t0]), roger_view_orig(rows[s0])
    assert got == want, ('roger script start', rows[s0]['f'], got, want)
    for i in range(t0, i0):
        got = roger_view_port(ticks[i])
        assert got['block'] == want['block'] and got['h'] == want['h'], \
            ('roger op16 wait', ticks[i]['tick'], got, want)
    count = len(rows) - f0
    assert i0 + count < len(ticks), ('the tick log ends inside the encounter window', len(ticks) - i0, count)
    release = release_row(rows, f0)
    # The equipment's +0xB0 is Roger's node 1 (001C5C90). Until the script's
    # op0B sub 4 (001B8020 -> 001C67E0, pc 0x8285D0 at f358) re-initializes
    # Roger's clip, his idle clip's phase is the time since the area load
    # (the capture's save state vs the port's own walk): +0xB0 from there.
    clip0 = next(k for k in range(count) if rows[f0 + k]['roger_r8']['s1F0'][16:24] == ROGER_CLIP_PC)
    assert rows[f0 + clip0]['f'] == 358, ('route 14 Roger clip init moved', rows[f0 + clip0]['f'])
    for k in range(count):
        t, row = ticks[i0 + k], rows[f0 + k]
        p, o = port_view(ticks, i0 + k), orig_view(row)
        where = f'roger row f{row["f"]} (port tick {t["tick"]})'
        for key in ('spad', 'cam', 'screen', 'power', 'msg'):
            assert p[key] == o[key], (where, key, p[key], o[key])
        assert ticks[i0 + k + 1]['fade8'] == row['fade'][:16], (where, 'fade block', ticks[i0 + k + 1]['fade8'],
                                                                 row['fade'][:16])
        got, want = roger_view_port(t), roger_view_orig(row)
        assert got == want, (where, 'Roger +0x00..+0x0F, +0xB0, block', got, want)
        d2 = bytes.fromhex(row['d2'])
        assert (t['story'][0], t['story'][3]) == (d2[0], d2[0x3B]), (where, 'D_008107D8 / D_00810813',
                                                                     t['story'], (d2[0], d2[0x3B]))
        pl = t['player']
        assert (pl[0], pl[1], pl[2], pl[3], pl[6]) == (row['p5'], row['m1F0'], row['m1F1'], row['clip'], row['b2F3']), \
            (where, 'player +5/+1F0/+1F1/clip/+0x2F3', pl, row)
        assert round(f32(pl[4]), 3) == row['clock'], (where, 'player clock +0x3C', f32(pl[4]), row['clock'])
        got, want = equipment_view_port(t), equipment_view_orig(row)
        assert got['h'] == want['h'], (where, 'the equipment node (001C5C90) +0x00..+0x0F', got, want)
        if k >= clip0:
            assert got['pos'] == want['pos'], (where, 'the equipment node (001C5C90) +0xB0', got, want)
        if selector(o['spad']) != '00' and (o['cam'] == '03' or row['f'] >= rows[release - 2]['f']):
            assert (p['eye'], p['tgt']) == (o['eye'], o['tgt']), (where, 'camera eye/target', p['eye'], o['eye'],
                                                                  p['tgt'], o['tgt'])
        if row['f'] >= rows[release - 2]['f']:
            assert (p['pos'], p['yaw']) == (o['pos'], o['yaw']), (where, 'player placement', p['pos'], o['pos'],
                                                                  p['yaw'], o['yaw'])
    cam3 = next(k for k in range(count) if rows[f0 + k]['cam_mode'][:2] == '03')
    state['cursor'] = i0 + count
    print(f'roger: PASS (the script start 0x8283D0 at port tick {ticks[t0]["tick"]} = route f{rows[s0]["f"]} in Roger\'s '
          f'record and block, held at op16 until the landing ({i0 - t0} ticks in the port, {f0 - s0} rows in the '
          f'original: the jump is navigation); port ticks {ticks[i0]["tick"]}..{ticks[i0 + count - 1]["tick"]} equal route 14 '
          f'f{rows[f0]["f"]}..f{rows[f0 + count - 1]["f"]}: the frame, the fade, the bars, the message, the bank 0x96 '
          f'timeline from f{rows[f0 + cam3]["f"]} (camera eye/target), Roger\'s record and block, the equipment node (+0xB0 from f{rows[f0 + clip0]["f"]}), the player record '
          f'(+0x2F3 = 2 at f{rows[f0 + next(k for k in range(count) if rows[f0 + k]["b2F3"] == 2)]["f"]}), the release at '
          f'f{rows[release]["f"]} with D_008107D8 = 1 and the placement ({rows[release]["pos"]}, {rows[release]["yaw"]}), '
          f'and {count - (release - f0)} rows after it)')


PHASES = [
    ('first_control', check_first_control),
    ('status', check_status),
    ('battery', check_battery),
    ('elevator_refusal', check_elevator_refusal),
    ('panel', check_panel),
    ('elevator', check_elevator),
    ('boxes', check_boxes),
    ('slide', check_slide),
    ('truck_preview', check_truck_preview),
    ('truck_crossing', check_truck_crossing),
    ('cage_ladders', check_cage_ladders),
    ('cage_roof', None),
    ('crevice_climbs', check_crevice_climbs),
    ('crevice_prompt', None),
    ('crevice_jump', check_crevice_jump),
    ('east_tower_climb', check_east_tower_climb),
    ('east_tower', None),
    ('roger', check_roger),
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
