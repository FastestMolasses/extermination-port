"""Opt-in AREA01 arrival idle, compared with the original route beat 15.

The rebuild is row 741; rows 742..801 are 60 neutral world frames. This
checks only fields present in that capture and the scene tick log. It does
not certify all AREA01 owner records or the later walking/interaction route.
"""
import json
import re
from pathlib import Path


CAPTURE = Path(__file__).resolve().parents[2] / 'Extermination/build/s87/route/15_level_exit/trace.json'
ARRIVAL_ROW = 741
WORLD_TICKS = 60


def check_arrival(smoke, ticks, run, state):
    assert CAPTURE.exists(), f'AREA01 arrival capture missing: {CAPTURE}'
    capture = json.loads(CAPTURE.read_text())
    rows = capture['rows']
    assert len(rows) > ARRIVAL_ROW + WORLD_TICKS, 'AREA01 arrival capture is truncated'
    assert not capture.get('teleports'), 'AREA01 arrival reference contains teleports'
    last_input = [v for v in capture['inputs'] if v['f'] <= ARRIVAL_ROW][-1]
    assert (last_input['buttons'], last_input['lx'], last_input['ly']) == (0, 127, 127), \
        ('AREA01 arrival reference is not neutral', last_input)
    assert not any(ARRIVAL_ROW < v['f'] <= ARRIVAL_ROW + WORLD_TICKS for v in capture['inputs']), \
        'AREA01 arrival reference changes its pad during the idle'
    match = re.search(r'^level smoke: a01_arrival: aligned counter=(\d+)', run, re.M)
    assert match, 'AREA01 arrival has no rebuild alignment'
    arrival = state.get('area01_arrival')
    assert arrival is not None and ticks[arrival]['counter'] == int(match.group(1)), \
        ('AREA01 arrival alignment differs from the checked exit rebuild', arrival, match.group(1))
    assert arrival + WORLD_TICKS + 1 < len(ticks), \
        'AREA01 arrival needs all 60 idle ticks and the next tick for post-frame fields'
    for offset in range(WORLD_TICKS + 1):
        i, row = arrival + offset, rows[ARRIVAL_ROW + offset]
        tick, following = ticks[i], ticks[i + 1]
        assert tick['counter'] == ticks[arrival]['counter'] + offset, \
            ('AREA01 arrival skipped a main-loop tick', offset, tick['counter'])
        b = bytes.fromhex(tick['post'])
        pl = tick['player']
        p = smoke.port_view(ticks, i)
        o = smoke.orig_view(row)
        fields = ('spad', 'screen', 'msg', 'power', 'pos', 'yaw', 'eye', 'tgt')
        got, want = {k: p[k] for k in fields}, {k: o[k] for k in fields}
        got.update(
            cam=tick['cam4'],
            req=b[smoke.tsr.OFFSET[0x8106B0]:smoke.tsr.OFFSET[0x8106B0] + 10].hex(),
            area=b[smoke.tsr.OFFSET[0x810700]:smoke.tsr.OFFSET[0x810700] + 3].hex(),
            slot0=tuple(b[:5]),
            player=(pl[0], pl[1], pl[2], pl[3], pl[6]), ground=hex(pl[5]),
            clock=round(smoke.f32(pl[4]), 3), fade=following['fade8'],
            story=(tick['story'][0], tick['story'][1], tick['story792']))
        want.update(
            cam=row['cam_mode'], req=row['req'], area=row['area4'][:6],
            slot0=tuple(bytes.fromhex(row['slots'])[8:13]),
            player=(row['p5'], row['m1F0'], row['m1F1'], row['clip'], row['b2F3']),
            ground=row['ground'], clock=row['clock'], fade=row['fade'][:16],
            story=(int(row['d2'][:2], 16), int(row['flags758'][:2], 16), row['story792']))
        if not offset:
            # Existing exit check documents the one rebuild-only difference:
            # native pose attach initializes +0x3C one frame before 0015C420.
            # Every subsequent world's clock is compared without an offset.
            got.pop('clock'), want.pop('clock')
        differences = {k: (got[k], want[k]) for k in got if got[k] != want[k]}
        assert not differences, (f'AREA01 arrival route 15 f{row["f"]}, port tick {tick["tick"]}', differences)
    state['cursor'] = arrival + WORLD_TICKS + 1
    print('a01_arrival: PASS (route 15 f741..f801: rebuild plus 60 neutral world ticks; '
          'player, ground, camera, selector, request, task, story bytes, message, bars and fade; '
          'rebuild clock alone excepted as in exit; AREA01 route owners not yet certified)')
