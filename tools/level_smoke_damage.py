"""level_smoke_damage.py - the DAMAGE side runs against the recordings.

The level smoke's side phases dmg_flame, dmg_crevice_fall and dmg_pit_fall
(src/game/em_level_smoke_test.c; docs/DAMAGE.md section 8) play the decomp
capture lane DAMAGE's closed-loop policies (route_capture.py dmg_beat_*) on
the port. The two runs start from different places and their policies react
to their own states, so a recording and a run are compared window by window,
each window aligned on its event and then compared tick by tick against the
recording's rows (decomp docs/CAPTURES_C10.md "DAMAGE"; build/c10/damage/
<beat>/trace.json):

  the hits        every hit of the run against a recorded hit with the same
                  flinch clip and low-health latch: from the hit's tick to
                  the hand-back (+4 = 1) +4..+7, +1F0, +1F1, the clip, its
                  clock, +0x00, +0x0F, +0x20E and +0x235 equal; the
                  knock-back's per-tick step within KNOCK_STEP; the rumble
                  (the pad block's +0x16 / +0x18 / +0x19 / +0x28 after the
                  frame) equal on the hit's next ticks; the health path
                  equal; the burn node 0022BBC0 spawned on the contact tick
                  and freed after BURN_TICKS ticks, as recorded
  the heartbeat   at <= 35 / <= 10 every activation that no hit started has
                  the recorded motor byte, duration and period, the first
                  one the recorded offset from the latch
  the death       from the death tick to the screen module's load request:
                  every field above, the task slot's +8 / +9 / +A, the fade
                  and D_008106B9 equal; the decal node 001F77B0 spawned at
                  the recorded offset; the big-motor rumble at the recorded
                  offset
  the game over   aligned on the end of screen module 0x27's load (task +A
                  2 -> 3; the port's loader reads at host speed, LAUNCHER_
                  OPTIONS.md "PS2 disc-drive timing", so its load is shorter
                  and reported): the task slot, the hold +0x18 and the fade
                  equal to the hold's end and the fade-out
  the title       the hold's end to 001ADF00's install of 001AC070 (the run
                  log's "startup: 001AC070 again"): equal counters; the menu
                  with the cursor on its second entry (in process); its
                  first input frame no later than the recording's (screen
                  module 1 resident at host speed)
  the New Game    Cross to the game task's return: equal counters; the game
                  task to first control: the same tick count as the run's
                  own boot New Game (the same chain, both at host speed);
                  first control's place, heading and health equal to the
                  recording's
  the falls       the crevice landing hit (dmg_06) aligned on the hit: every
                  field and the height path equal to the hand-back; the pit
                  (dmg_07) aligned on the death: every field and the height
                  path (from the death) equal to the load request, then the
                  game over as above.
"""
from __future__ import annotations

import json
import math
import re
import struct
from pathlib import Path

DECOMP = Path(__file__).resolve().parents[2] / 'Extermination'
CAPTURES = DECOMP / 'build/c10/damage'
KNOCK_STEP = 0.01          # per-tick knock-back step, |port - recording|
HEIGHT_PATH = 0.001        # the fall's height relative to its event
FIELDS = ('ev', 't', 'p4', 'p5', 'p6', 'p7', 'm', 'm1', 'clip', 'clk', 'i20E', 'b235')
REQ_B9 = 0x09              # D_008106B9 in the scene state's request block (D_008106B0..)
# The tick log's "post" snapshot (em_scene_bindings.c log_snapshot): the
# task record +8..+0x1F, the request block (0x48), D_00810700..702,
# D_00810730[0x20], D_00810750, 0x70003B68, 3B84, 3B8A, 3B8C, then 3B8D.
POST_AREA, POST_3B8D = 24 + 0x48, 24 + 0x48 + 3 + 0x20 + 4 + 4 + 2 + 2 + 1
GAME_TASK, TITLE_TASK = 0x1ACEC0, 0x1AC070


def f32(word):
    return struct.unpack('<f', struct.pack('<I', word))[0]


def capture(beat):
    return json.loads((CAPTURES / beat / 'trace.json').read_text())


def crow(r):
    v, rb = r['vit'], r['rumble']
    b = bytes.fromhex(r['slots'][:0x40])
    fl = r.get('flame')
    return dict(flame=(int(fl['a'][:2], 16), fl['cool210']) if fl else None,
                counter=r['counter'], hp=v['health'], ev=v['ev'], t=v['type'], p4=v['p4'], p5=v['p5'],
                p6=v['p6'], p7=v['p7'], m=r['m1F0'], m1=r['m1F1'], clip=r['clip'], clk=r['clock'],
                i20E=v['inv20E'], b235=v['b235'], pad=(rb['active'], rb['big'], rb['small'], rb['dur']),
                pos=r['pos'], fn=struct.unpack_from('<I', b, 4)[0], slot=(b[8], b[9], b[0xA]),
                hold=struct.unpack_from('<h', b, 0x18)[0], fade=int(r['fade'][:2], 16), b6B9=r['gv']['b6B9'],
                nodes=cnodes(r))


def cnodes(r):
    out = {}
    for v in (r.get('pool_delta') or {}).values():
        b = bytes.fromhex(v)
        cb = struct.unpack_from('<I', b, 0x10)[0]
        if b[0] and cb in (0x22BBC0, 0x1F77B0):
            out[cb] = out.get(cb, 0) + 1
    return out


def flame_row(t):
    """The flame 008235F0's +0x00 and its +0x210 contact cooldown (the tick
    log's overlay11 record), or None."""
    for r in t.get('overlay11', []):
        if r[1] == 0x8235F0:
            return (r[2], r[9])
    return None


def prows(ticks, start):
    """The run's game-task ticks from `start`, as capture-like rows. The pad
    block and the fade after the frame are the next tick's pre-samples."""
    out = []
    for i in range(start, len(ticks) - 1):
        t, nxt = ticks[i], ticks[i + 1]
        if 'dmg' not in t:
            continue
        d, pl, post = t['dmg'], t['player'], bytes.fromhex(t['post'])
        nodes = {}
        for n in d[17]:
            nodes[n[1]] = nodes.get(n[1], 0) + 1
        out.append(dict(tick=t['tick'], counter=t['counter'], hp=f32(d[0]), ev=d[4], t=d[5], p4=pl[7], p5=pl[0],
                        p6=d[9], p7=d[10], m=pl[1], m1=pl[2], clip=pl[3], clk=f32(pl[4]), i20E=d[6], b235=d[7],
                        pad=tuple(nxt['pad_pre']) if 'pad_pre' in nxt else None,
                        pos=[f32(x) for x in t['pos_post']], slot=(post[0], post[1], post[2]),
                        hold=struct.unpack_from('<h', post, 0x10)[0], fade=nxt['fade'], b6B9=post[24 + REQ_B9],
                        nodes=nodes, gap=nxt['counter'] - t['counter'], flame=flame_row(t)))
    return out


def phase_start(run, phase, ticks):
    """The first tick of the side phase: its first step ("done idle at tick
    N") ends 35 neutral ticks after the start."""
    m = re.search(rf'^level smoke: {phase}: done idle at tick (\d+)', run, re.M)
    assert m, (phase, 'no step lines in the run log')
    first = int(m.group(1)) - 35
    return next(i for i, t in enumerate(ticks) if t['tick'] >= first)


def hits(rows):
    return [i for i in range(1, len(rows)) if rows[i]['hp'] < rows[i - 1]['hp']]


def hand_back(rows, i):
    """Ticks from the hit to the first with +4 = 1 again (or the death)."""
    for k in range(1, 400):
        if i + k >= len(rows) or rows[i + k]['p4'] == 1 or rows[i + k]['hp'] <= 0:
            return k
    raise AssertionError('damage: a hit reaction never handed back')


def window_diff(c, a, p, b, n):
    return [k for k in range(1, n) if any(c[a + k][x] != p[b + k][x] for x in FIELDS)]


def step_lengths(rows, i, n):
    return [math.hypot(rows[i + k]['pos'][0] - rows[i + k - 1]['pos'][0],
                       rows[i + k]['pos'][2] - rows[i + k - 1]['pos'][2]) for k in range(1, n)]


def check_hits(c, p, name):
    ch, ph = hits(c), hits(p)
    assert [c[i]['hp'] for i in ch] == [p[i]['hp'] for i in ph], \
        (name, 'the health path differs', [c[i]['hp'] for i in ch], [p[i]['hp'] for i in ph])
    worst = 0.0
    for n, b in enumerate(ph):
        if p[b]['hp'] <= 0:
            continue                                     # the death: check_death
        end = hand_back(p, b)
        same = [a for a in ch if c[a]['hp'] > 0 and c[a + 1]['clip'] == p[b + 1]['clip'] and
                c[a + 1]['b235'] == p[b + 1]['b235']]
        assert same, (name, 'hit', n, 'no recorded hit with its flinch clip and latch', p[b + 1]['clip'])
        a = min(same, key=lambda a: len(window_diff(c, a, p, b, end + 1)))
        diff = window_diff(c, a, p, b, end + 1)
        assert hand_back(c, a) == end and not diff, \
            (name, 'hit', n, 'the reaction differs from the recording', 'end', hand_back(c, a), end, 'ticks', diff[:5],
             [(x, c[a + diff[0]][x], p[b + diff[0]][x]) for x in FIELDS if diff and c[a + diff[0]][x] != p[b + diff[0]][x]])
        steps = [abs(x - y) for x, y in zip(step_lengths(c, a, end), step_lengths(p, b, end))]
        worst = max([worst] + steps)
        assert max(steps) <= KNOCK_STEP, (name, 'hit', n, 'the knock-back step differs', max(steps))
        for k in range(0, 6):
            assert c[a + k]['pad'] == p[b + k]['pad'], (name, 'hit', n, 'the rumble differs at tick', k,
                                                         c[a + k]['pad'], p[b + k]['pad'])
        # The burn node 0022BBC0: spawned by the contact (the tick before
        # the hit) and freed after the recorded life.
        def life(rows, i):
            if 0x22BBC0 not in rows[i - 1]['nodes']:
                return None
            return next((k for k in range(0, 200) if 0x22BBC0 not in rows[i - 1 + k]['nodes']), None)
        assert life(c, a) == life(p, b), (name, 'hit', n, 'the burn node lives', life(c, a), life(p, b))
        # The flame's +0x00 and contact cooldown +0x210 from the contact on.
        if c[a - 1]['flame'] is not None:
            for k in range(-1, 62):
                assert c[a + k]['flame'] == p[b + k]['flame'], (name, 'hit', n, 'the flame differs at tick', k,
                                                                 c[a + k]['flame'], p[b + k]['flame'])
    return len(ph), worst


def heartbeat(rows, hit_ticks):
    """Rumble activations (rising +0x16) no hit started: (tick, motor,
    duration); a hit's rumble starts on the hit's next tick."""
    out = []
    for i in range(1, len(rows) - 1):
        a, b = rows[i - 1]['pad'], rows[i]['pad']
        if b and a and b[0] and not a[0] and (i - 1) not in hit_ticks and rows[i]['hp'] > 0:
            out.append((i, b[2], b[3]))
    return out


def check_heartbeat(c, p, name):
    ch, ph = set(hits(c)), set(hits(p))
    hc, hp_ = heartbeat(c, ch), heartbeat(p, ph)
    latch_c = next(i for i in range(1, len(c)) if c[i]['b235'] & 1 and not c[i - 1]['b235'] & 1)
    latch_p = next(i for i in range(1, len(p)) if p[i]['b235'] & 1 and not p[i - 1]['b235'] & 1)
    assert hc[0][0] - latch_c == hp_[0][0] - latch_p, \
        (name, 'the first heartbeat after the latch', hc[0][0] - latch_c, hp_[0][0] - latch_p)
    def pattern(beats, rows):
        return {(motor, dur, b2[0] - b1[0]) for (b1, b2) in zip(beats, beats[1:])
                for motor, dur in [(b1[1], b1[2])] if b2[1] == b1[1] and not any(
                    b1[0] < h < b2[0] for h in hits(rows))}
    pc, pp = pattern(hc, c), pattern(hp_, p)
    assert pp and pp <= pc, (name, 'heartbeat activations the recording does not have', sorted(pp - pc))
    return len(hp_)


def check_death(c, p, name):
    dc = next(i for i in range(1, len(c)) if c[i]['hp'] <= 0 < c[i - 1]['hp'])
    dp = next(i for i in range(1, len(p)) if p[i]['hp'] <= 0 < p[i - 1]['hp'])
    keys = FIELDS + ('slot', 'fade', 'b6B9')
    # To 001AD4E0 step 1's load request (+9 = 2, +A = 2).
    req = next(k for k in range(1, 3000) if c[dc + k]['slot'][1:] == (2, 2))
    for k in range(1, req + 1):
        a, b = c[dc + k], p[dp + k]
        bad = [x for x in keys if a[x] != b[x]]
        assert not bad, (name, 'the death differs at tick', k, [(x, a[x], b[x]) for x in bad])
    def first(rows, i, cb):
        return next((k for k in range(0, req + 1) if cb in rows[i + k]['nodes']), None)
    assert first(c, dc, 0x1F77B0) == first(p, dp, 0x1F77B0), \
        (name, 'the decal node 001F77B0 spawns at', first(c, dc, 0x1F77B0), first(p, dp, 0x1F77B0))
    big = lambda rows, i: next((k for k in range(0, req + 1) if rows[i + k]['pad'] and rows[i + k]['pad'][2] == 0xEE),
                               None)
    assert big(c, dc) == big(p, dp), (name, 'the death rumble at', big(c, dc), big(p, dp))
    return dc, dp, req


def check_game_over(c, p, dc, dp, req, name):
    """Aligned on the load's end (+A 2 -> 3): the hold, the fade, the task
    slot, to the end of the recording's rows."""
    lc = next(k for k in range(req, len(c) - dc) if c[dc + k]['slot'][2] == 3)
    lp = next(k for k in range(req, len(p) - dp) if p[dp + k]['slot'][2] == 3)
    assert lp <= lc, (name, 'the port loaded screen module 0x27 slower than the disc', lp, lc)
    n = 0
    for k in range(0, len(c) - (dc + lc)):
        if dp + lp + k >= len(p):
            break
        a, b = c[dc + lc + k], p[dp + lp + k]
        bad = [x for x in ('slot', 'hold', 'fade') if a[x] != b[x]]
        assert not bad, (name, 'the game over differs at tick', k, 'after the load', [(x, a[x], b[x]) for x in bad])
        n += 1
    return lc - req, lp - req, n


def control_ticks(ticks, start):
    """The tick of first control after `start`: the area 0x0B, the task at
    +8 = 3 with the frame machine in state 1 (+0xB), 0x70003B8D clear after
    the opening set it, the player's +4 = 1."""
    opening = False
    for t in ticks[start:]:
        post = bytes.fromhex(t['post'])
        if post[POST_AREA] != 0x0B:
            continue
        if post[POST_3B8D]:
            opening = True
        elif opening and post[0] == 3 and post[3] == 1 and t.get('player') and t['player'][7] == 1:
            return t['tick']
    raise AssertionError('damage: no first control in the tick log')


def flame_to_title(ticks, run, state, phase):
    """dmg_00..dmg_03 of a run that plays the flame's death (dmg_flame,
    dmg_load): the hits, the heartbeat, the death, the game over and the
    title after the death. A summary of what was checked."""
    beats = ('dmg_00_flame_hit', 'dmg_01_flame_low_health', 'dmg_02_flame_death')
    c = [crow(r) for b in beats for r in capture(b)['rows']]
    start = phase_start(run, phase, ticks)
    state['damage_from'] = start
    p = prows(ticks, start)
    nh, worst = check_hits(c, p, phase)
    nb = check_heartbeat(c, p, phase)
    dc, dp, req = check_death(c, p, phase)
    load_c, load_p, n = check_game_over(c, p, dc, dp, req, phase)
    # dmg_03 (its own rows, from the dmg_02 end snapshot): the hold's end
    # (+A 3 -> 4) to the install of 001AC070.
    c3 = [crow(r) for r in capture('dmg_03_gameover_timeout')['rows']]
    hold_end_c = next(r['counter'] for r in c3 if r['slot'] == (3, 2, 4))
    install_c = next(r['counter'] for r in c3 if r['fn'] == TITLE_TASK)
    prompt_c = next(r['counter'] for r in c3 if r['fn'] == TITLE_TASK and r['slot'][:2] == (2, 2) and r['fade'] == 0)
    hold_end_p = next(r['counter'] for r in p if r['slot'] == (3, 2, 4))
    install_p = int(re.search(r'^startup: 001AC070 again \(D_00275BDC = 1\), engine frame (\d+)', run, re.M).group(1))
    assert install_p - hold_end_p == install_c - hold_end_c, \
        (f'{phase}: the hold end to 001AC070', install_p - hold_end_p, install_c - hold_end_c)
    prompt_p = int(re.search(rf'^level smoke: {phase}: the title menu takes input at counter (\d+), cursor 1$',
                             run, re.M).group(1))
    assert 0 < prompt_p - install_p <= prompt_c - install_c, \
        (f'{phase}: 001AC070 to the menu', prompt_p - install_p, prompt_c - install_c)
    return (f'{nh} hits as recorded (knock-back within {worst:.4f}), {nb} heartbeats, the death and its decal to '
            f'the load request ({req} ticks), screen module 0x27 loaded in {load_p} ticks (the disc {load_c}), {n} '
            f'game-over ticks after it, 001AC070 {install_p - hold_end_p} after the hold, the menu after '
            f'{prompt_p - install_p} (the disc {prompt_c - install_c})')


def check_dmg_flame(ticks, run, state):
    summary = flame_to_title(ticks, run, state, 'dmg_flame')
    # dmg_04: the confirm to the game task. The recording's rows hold the
    # frame 001AC480 entered its sub 3 (the confirm) and the frame 001AC070
    # state 4's 001AB790 replaced the task; its pad reaches the game three
    # frames after the tool set it. The run's pad reaches the frontend on
    # the tick after the press, and its tick log resumes on the frame after
    # the replacement.
    c4 = capture('dmg_04_new_game')
    rows4 = [crow(r) for r in c4['rows']]
    confirm_c = next(r['counter'] for r in rows4 if r['fn'] == TITLE_TASK and r['slot'][1] == 3)
    back_c = next(r['counter'] for r in rows4 if r['fn'] == GAME_TASK)
    confirm_p = int(re.search(r'^level smoke: dmg_flame: press CROSS at counter (\d+)$', run, re.M).group(1)) + 1
    back_i = next(i for i, t in enumerate(ticks) if t['counter'] > confirm_p)
    back_p = ticks[back_i]['counter'] - 1
    assert back_p - confirm_p == back_c - confirm_c, ('dmg_flame: the confirm to the game task', back_p - confirm_p,
                                                      back_c - confirm_c)
    # The whole-run checks (the render context, the indicator children, the
    # rand() order, ...) describe one game from the boot's New Game: they
    # stop where the New Game after the death begins (test_level_smoke.py).
    state['second_game'] = back_i
    m = re.search(r'^level smoke: dmg_flame: first control again at tick (\d+) counter (\d+) '
                  r'\(([-\d.]+),([-\d.]+),([-\d.]+)\) yaw=([-\d.]+) health=([\d.]+)$', run, re.M)
    assert m, 'dmg_flame: no first control after the New Game'
    first4 = next(r for r in rows4[1:] if r['counter'] == rows4[0]['counter'] + c4['marks']['first_control'])
    got = [float(m.group(k)) for k in (3, 4, 5, 6, 7)]
    want = first4['pos'] + [round(c4['rows'][c4['marks']['first_control']]['yaw'], 5), first4['hp']]
    assert all(abs(x - y) <= 1e-4 for x, y in zip(got, want)), ('dmg_flame: first control', got, want)
    # The boot's New Game in the same run, against the New Game after the
    # death: their first ticks to first control by the same tick-log test.
    boot = control_ticks(ticks, 0) - ticks[0]['tick']
    again = control_ticks(ticks, back_i) - ticks[back_i]['tick']
    if boot is not None:
        assert again == boot, ('dmg_flame: the New Game after a death took', again, 'ticks; the boot New Game',
                               boot)
    print(f'level smoke: dmg_flame: {summary}, the game task {back_p - confirm_p} after the confirm, first control '
          f'{again} ticks later' + (f' (the boot New Game: {boot})' if boot is not None else ''))


TITLE_ROW = re.compile(r'^startup: title row counter (\d+) state (\d+) sub (\d+) fade (-?\d+) busy (\d+) '
                       r'card (\d+) (\d+) (\d+) (\d+) (\d+)$', re.M)


def title_state(s8, s9, fade, busy):
    """001AC070's state, 001AC480's sub-state in state 2 (else 0), the fade
    D_0028A9A0 and, in state 5, the busy byte D_00275BD8. In state 2 the
    busy byte is left out: the title's screen module 1 is the frontend's
    native stand-in (resident at host speed, no loader task; the recording's
    busy rows 1 there), checked by its timing only."""
    return (s8, s9 if s8 == 2 else 0, fade, busy if s8 == 5 else None)


def check_dmg_load(ticks, run, state):
    """dmg_load: dmg_00..dmg_03 as dmg_flame, then dmg_05 (the title's load
    screen, docs/OPTIONS.md section 6). The title rows (the frontend's
    "startup: title row" lines, each the state after the frame before its
    counter) against the recording's from the Cross's fade-out on: the
    sequence of distinct states equal, each lasting the recording's rows
    except the load of screen module 0x2A (state 5, busy; host speed), the
    title's screen module 1 (state 2 sub 1; host speed) and the last state
    (the idle before the Triangle included: the policy's taps reach the
    title flow when the capture tool's did); the
    press to the fade-out as recorded for both presses; the card record:
    load mode (+0x14 = 2), the slot choice (+0x15 = 1) reached and the
    result 1 (the exit)."""
    summary = flame_to_title(ticks, run, state, 'dmg_load')
    rec = capture('dmg_05_load_screen')['rows']
    cap = []
    for r in rec:
        b = bytes.fromhex(r['slots'][:0x40])
        cap.append((r['counter'], title_state(b[8], b[9], int(r['fade'][:2], 16), r['bd8']),
                    int(r['pressed'], 16)))
    port = [(int(m.group(1)) - 1, title_state(*(int(m.group(k)) for k in (2, 3, 4, 5))),
             tuple(int(m.group(k)) for k in range(6, 11))) for m in TITLE_ROW.finditer(run)]
    cross = int(re.search(r'^level smoke: dmg_load: press CROSS \(load\) at counter (\d+)$', run, re.M).group(1))
    tri = int(re.search(r'^level smoke: dmg_load: press TRIANGLE at counter (\d+)$', run, re.M).group(1))
    prompt = int(re.search(r'^level smoke: dmg_load: the title menu takes input again at counter (\d+)', run,
                           re.M).group(1))
    # The presses: the recording's from the row its pad reached the game
    # (pressed set) to the fade-out (D_0028A9A0 3); the run's pad reaches
    # the game on the tick after the press line.
    def fade_out(rows, after):
        return next(c for c, s, *_ in rows if c >= after and s[2] == 3)
    arrivals = [c for c, _, pr in cap if pr]
    cross_c = arrivals[0]
    tri_c = next(c for c in arrivals if c > fade_out(cap, cross_c) + 40)
    lat = [(fade_out(port, cross + 1) - (cross + 1), fade_out(cap, cross_c) - cross_c),
           (fade_out(port, tri + 1) - (tri + 1), fade_out(cap, tri_c) - tri_c)]
    assert all(a == b for a, b in lat), ('dmg_load: a press to its fade-out (Cross, Triangle)', lat)
    a_c, a_p = fade_out(cap, cross_c), fade_out(port, cross + 1)
    cs = [s for c, s, *_ in cap if c >= a_c]
    ps = [s for c, s, *_ in port if a_p <= c <= prompt]
    def runs(states):
        out = []
        for x in states:
            if out and out[-1][0] == x:
                out[-1][1] += 1
            else:
                out.append([x, 1])
        return out
    cr, pr = runs(cs), runs(ps)
    # The recording runs on 30 frames past the prompt; the run's rows stop
    # at the prompt's first row.
    end = next(i for i, (x, _) in enumerate(cr) if i and x == (2, 2, 0, None))
    cr = cr[:end + 1]
    assert [x for x, _ in pr] == [x for x, _ in cr], ('dmg_load: the title states differ', [x for x, _ in pr],
                                                       [x for x, _ in cr])
    idle = next(i for i, (x, _) in enumerate(cr) if x == (5, 0, 0, 0))
    host = 0
    for i, ((x, n), (_, m)) in enumerate(zip(pr[:-1], cr[:-1])):
        if (x[0] == 5 and x[3] == 1) or (x[0] == 2 and x[1] == 1):
            assert n <= m, ('dmg_load: a screen module load lasted longer than the recording\'s', x, n, m)
            host += 1
        else:
            assert n == m, ('dmg_load: a title state lasted', n, 'rows, the recording\'s', m, x)
    # The card record over the screen: load mode, the slot choice, the exit.
    cards = [card for c, s, card in port if a_p <= c <= prompt and s[0] == 5]
    assert any(cd[2] == 2 for cd in cards), 'dmg_load: 00225AC0 never stored the load mode (+0x14 = 2)'
    assert any(cd[0] == 1 and cd[3] == 1 for cd in cards), 'dmg_load: the slot choice (+0x15 = 1) never ran'
    assert cards[-1][0] == 3 and cards[-1][4] == 1, ('dmg_load: the screen did not end with the exit', cards[-1])
    print(f'level smoke: dmg_load: {summary}; the load screen: {len(cr)} title states from the Cross\'s fade-out to '
          f'the menu equal the recording\'s, each for its rows ({host} screen-module load(s) at host speed; '
          f'the idle before the Triangle {pr[idle][1]} rows), both presses to '
          f'their fade-outs as recorded ({lat[0][1]}, {lat[1][1]} frames)')


def check_dmg_crevice_fall(ticks, run, state):
    c = [crow(r) for r in capture('dmg_06_crevice_fall')['rows']]
    p = prows(ticks, phase_start(run, 'dmg_crevice_fall', ticks))
    ci, pi = hits(c), hits(p)
    assert [c[i]['hp'] for i in ci] == [95.0] and [p[i]['hp'] for i in pi] == [95.0], \
        ('dmg_crevice_fall: one landing hit to 95', [c[i]['hp'] for i in ci], [p[i]['hp'] for i in pi])
    a, b = ci[0], pi[0]
    n = len(c) - a
    assert b + n <= len(p), ('dmg_crevice_fall: the run ends before the recording', len(p) - b, n)
    for k in range(0, n):
        ra, rb = c[a + k], p[b + k]
        bad = [x for x in FIELDS if ra[x] != rb[x]]
        dy = abs((ra['pos'][1] - c[a]['pos'][1]) - (rb['pos'][1] - p[b]['pos'][1]))
        assert not bad and dy <= HEIGHT_PATH and ra['pad'] == rb['pad'], \
            ('dmg_crevice_fall: differs at tick', k, [(x, ra[x], rb[x]) for x in bad], dy, ra['pad'], rb['pad'])
    print(f'level smoke: dmg_crevice_fall: the landing hit and {n} ticks after it as recorded')


def check_dmg_pit_fall(ticks, run, state):
    c = [crow(r) for r in capture('dmg_07_pit_fall')['rows']]
    p = prows(ticks, phase_start(run, 'dmg_pit_fall', ticks))
    dc = next(i for i in range(1, len(c)) if c[i]['hp'] <= 0 < c[i - 1]['hp'])
    dp = next(i for i in range(1, len(p)) if p[i]['hp'] <= 0 < p[i - 1]['hp'])
    for k in range(0, 200):
        a, b = c[dc + k], p[dp + k]
        if a['slot'][1:] == (2, 2):
            break
        dy = abs((a['pos'][1] - c[dc]['pos'][1]) - (b['pos'][1] - p[dp]['pos'][1]))
        assert dy <= HEIGHT_PATH, ('dmg_pit_fall: the body falls differently at tick', k, dy)
    _, _, req = check_death(c, p, 'dmg_pit_fall')
    load_c, load_p, n = check_game_over(c, p, dc, dp, req, 'dmg_pit_fall')
    print(f'level smoke: dmg_pit_fall: the 0x5D floor death and the fall to the load request ({req} ticks), '
          f'screen module 0x27 loaded in {load_p} ticks (the disc {load_c}), {n} game-over ticks after it')
