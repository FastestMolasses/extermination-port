#!/usr/bin/env python3
"""Record the first boss fight on the ORIGINAL (the PCSX2 fork).

Two wins are recorded, both pad input only. The intended one (group `g`,
the user's hint, 2026-10-30): the SHOTGUN UNIT (LOWER U.R.S. entry 1, the
take D_00810C65 with D_00810CA8 = 6 shells) lies at (416.1, 15, 102.2) in the
room behind door [45], beside the NPC [2] the route visits; the take opens
the V.U.R.S. page on it, Cross twice, Left, Cross fits it (D_00810CA6 = 1).
In the R1 stance each Square fires a shell (the +275 = 3 machine 00171B00,
the gun event 00186A60): 42 at under 20 units, 35 under 50, else 25, one
hit a tick (the pellets all store the creature's +0x36).

  a04b_g0_shotgun    from a04b_01_npc's end: the take, the fit
  a04b_g1_door45     door [45] out (a04b_02's beat function)
  a04b_g2_reader     the reader (a04b_03's beat function)
  a04b_g3_fight      R1 and Square at the creature to its death, control
  a04b_g4_drop       the drop
  a04b_g5_lift       the lift to AREA13

The pistol win (group `s`, no attachment) from a04b_03_reader's end:

The creature (0012E3A0, +0x0D = 3, group 0x826790, node 0x7C2660 on the
recorded branch) that event [3] 0x8246B0 spawns at AREA04's lift [51] when
the reader [50] takes item 0x23. Three side beats after a04b_03_reader's end,
driven with pad input only through the decomp's tools/route_capture.py (its
Route, samplers and walk helpers; hidden ForkSession under the run lock
build/.pcsx2.lock). No game memory is written, nothing is teleported.

  a04b_s5_fight  north along the east balcony to z 395, turn, hold R1 and tap
                 Circle at the creature until its +0x34 is 0; the pad neutral
                 through its death, [3]'s second stage 0x824930 and script
                 0x8286D0, to control with counter 0x12 = 0xFF.
  a04b_s6_drop   to the drop the script spawned at (544.9, 55, 255.7), Use,
                 the item page, Triangle, control.
  a04b_s7_lift   to button [54], Use, the keyed ride, AREA13 entry 0, control.

Each beat is written like route_capture's (trace.json with the beat's sampler
rows, inputs and snapshot; state.p2s / eeMemory.bin) under
../Extermination/build/fork_refs/boss_fight_20261030/s87/route_a04b/<beat>/,
with one neutral lead-in frame after the source state, as a04b_04 has, plus
boss.json: per row the creature's +0x20..0x70, its +0x1F0..0x270, the four
spawned bugs' first 16 bytes, D_00810C60..BF, the player's +0x220 / +0x228.
Disc-derived output, never committed. Nothing else is touched.

`replay` runs the whole chain open-loop in one session from a04b_03's source
state (a04b_02_door45_out's end): a04b_03's recorded inputs, then the three
beats' inputs, with a fork capture store of every frame, a per-tick table of
the creature, the player and the story block, memwatches on counter 0x12,
flag 0x12 and the creature's +0x34, and the rows checked against the beats'.

Usage (port root, the decomp's venv; macOS arm64, the fork under Rosetta):
    ../Extermination/.venv/bin/python tools/capture_boss_fight.py fight
    ../Extermination/.venv/bin/python tools/capture_boss_fight.py drop
    ../Extermination/.venv/bin/python tools/capture_boss_fight.py lift
    ../Extermination/.venv/bin/python tools/capture_boss_fight.py replay
    ../Extermination/.venv/bin/python tools/capture_boss_fight.py rand
"""
import json
import math
import shutil
import struct
import sys
from pathlib import Path

DECOMP = Path(__file__).resolve().parents[2] / 'Extermination'
sys.path.insert(0, str(DECOMP / 'tools'))
import route_capture as rc  # noqa: E402

LEAD_IN = 1
ROUTE = DECOMP / 'build/fork_refs/levels-recordings-20261018-10/s87/route_a04b'
OUT = DECOMP / 'build/fork_refs/boss_fight_20261030/s87/route_a04b'
CREATURE = 0x7C2660            # the pistol branch's; use_nodes() finds a branch's own
BUGS = {'bug_a': 0x7C2080, 'bug_b': 0x7C2370, 'bug_c': 0x7C3800, 'bug_d': 0x7C3AF0}
DROP = (544.9, 255.7)
SHOTGUN = (416.1, 102.2)
SHOTGUN_NODE = 0x7A6200        # the placement s0 g[4] (00219550)
R3 = 0x7AA590
# The fire cadence: a press of `FIRE_ON` frames every `FIRE_PERIOD` frames
# (the stance takes a shot at most about every 13 frames). Two runs with
# other cadences from this state died; this one wins with health to spare.
FIRE_PERIOD, FIRE_ON = 8, 2
OWNERS, SPANS = {}, []


def set_nodes():
    global OWNERS, SPANS
    OWNERS = dict(rc.A04B_OWNERS, creature=CREATURE, **BUGS)
    SPANS = list(rc.A04B_SPANS)
    for name, base in [('creature', CREATURE)] + list(BUGS.items()):
        SPANS += [(name + ':h', base, 0x10), (name + ':p', base + 0xB0, 0x10),
                  (name + ':s', base + 0x1F0, 0x10), (name + ':t', base + 0x2DC, 0x14),
                  (name + ':c', base + 0x10, 0x4), (name + ':r', base + 0xC0, 0x10)]


set_nodes()


def use_nodes(s):
    """The branch's creature ([3]'s +0x240) and the four spawned bugs (the
    0012A5D0 nodes above the hall's placements)."""
    global CREATURE, BUGS
    CREATURE = s.u32(R3 + 0x240)
    nodes = [a for a in range(0x7A5640, 0x7A5640 + 0x2F0 * 256, 0x2F0)
             if a >= 0x7BA000 and s.u32(a + 0x10) == 0x12A5D0 and s.read(a, 4)[0]]
    if not CREATURE or len(nodes) != 4:
        raise RuntimeError(f'creature {CREATURE:#x} bugs {[hex(n) for n in nodes]}')
    BUGS = dict(zip(('bug_a', 'bug_b', 'bug_c', 'bug_d'), nodes))
    set_nodes()


def use_sampler(r):
    sampler = rc.A01USampler(r.s, spans=SPANS)
    r.sampler = sampler
    r.now = lambda: rc.decode_a04b(sampler.raw(), owners=OWNERS)
    r.rows[0] = dict(r.now(), f=0)


def creature(s):
    head = s.read(CREATURE, 0x10)
    return dict(state=head[4], sub=head[5], step=head[6], flags=head[0xD],
                hp=struct.unpack('<h', s.read(CREATURE + 0x34, 4)[:2])[0])


def story(s):
    return dict(counter=s.read(0x8107E8, 4)[2], flag=s.read(0x810768, 4)[2],
                hp=struct.unpack('<f', s.read(0x8104D0, 4))[0],
                inf=struct.unpack('<f', s.read(0x8104D8, 4))[0],
                mag=s.read(0x810C60, 4)[2], rounds=struct.unpack('<H', s.read(0x810CB4, 4)[:2])[0])


def extras(s):
    return dict(cr=s.read(CREATURE + 0x20, 0x50).hex(), ent=s.read(CREATURE + 0x1F0, 0x80).hex(),
                bugs=[s.read(a, 0x10).hex() for a in BUGS.values()], items=s.read(0x810C60, 0x60).hex(),
                hp=struct.unpack('<f', s.read(0x8104D0, 4))[0], inf=struct.unpack('<f', s.read(0x8104D8, 4))[0])


def beat_fight(r):
    use_sampler(r)
    s = r.s
    how = rc.a04_go(r, [(545, 395)], limit=400)
    if how != 'ok':
        raise RuntimeError('the north end not reached: ' + how)
    rc.a06_face(r, math.pi)
    hp0 = story(s)['hp']
    tick = 0
    while True:
        c, st = creature(s), story(s)
        if c['hp'] == 0 and c['state'] == 2:
            break
        if st['hp'] <= 0:
            raise RuntimeError(f'the player died: {c} {st}')
        if st['rounds'] == 0:
            raise RuntimeError(f'out of rounds: {c} {st}')
        if r.rows[-1]['m1F0'] in rc.A04_GRABS:
            rc.a04_shake(r)
            continue
        tick += 1
        buttons = rc.PAD['R1']
        if (tick - 1) % FIRE_PERIOD < FIRE_ON:
            buttons |= rc.PAD['CIRCLE']
        r.set_pad(buttons)
        r.step(1)
    r.set_pad(0)
    dead = r.frame_index
    for _ in range(1500):
        r.step(1)
        if story(s)['counter'] == 0xFF and rc.in_control(r.rows[-1]):
            break
    else:
        raise TimeoutError('no control after the creature: ' + rc.summary(r.rows[-1]))
    st = story(s)
    return dict(what='the balcony north, R1 and Circle at the creature to its death, [3] stage 2, control',
                hp_before=hp0, hp_end=st['hp'], inf_end=st['inf'], rounds_end=st['rounds'], death_frame=dead)


def beat_drop(r):
    use_sampler(r)
    s = r.s
    before = s.read(0x810C60, 0x60)
    node = s.read(CREATURE, 0x10)
    if node[2] != 0xC7 or struct.unpack('<I', s.read(CREATURE + 0x10, 4))[0] != 0x15AFA0:
        raise RuntimeError('the drop is not at the creature\'s node: ' + node.hex())
    for k in range(600):
        row = r.rows[-1]
        if row['m1F0'] in rc.A04_GRABS:
            rc.a04_shake(r)
            continue
        if s.read(CREATURE, 8)[5] != 0:          # the drop's +5: the take started
            break
        near = math.hypot(row['pos'][0] - DROP[0], row['pos'][2] - DROP[1]) <= 7
        lx, ly = rc.stick_values(r, *DROP)
        r.set_pad(rc.PAD['CROSS'] if near and k % 6 < 2 else 0, lx, ly)
        r.step(1)
    else:
        raise TimeoutError('the drop was not taken: ' + rc.summary(r.rows[-1]))
    r.set_pad(0)
    rc.a04b_control(r, 3000, 40)
    rc.a06_settle(r, 10)
    after = s.read(0x810C60, 0x60)
    gained = {hex(0x810C60 + i): (before[i], after[i]) for i in range(0x60) if before[i] != after[i]}
    if not gained:
        raise RuntimeError('no item gained')
    return dict(what='the drop (0015AFA0 at the creature\'s node): Use, the item page, Triangle', gained=gained,
                hp_end=story(s)['hp'])


def beat_lift(r):
    use_sampler(r)
    s = r.s
    how = rc.a04_go(r, [(552, 272), (561, 262), (566, 250), (566, 244)], limit=300)
    if how != 'ok':
        raise RuntimeError('button [54] not reached: ' + how)
    rc.a06_use(r, 566.0, 243.0, -math.pi / 2, lambda row: row['button_r54']['h'][8:10] == '01')
    r.until(lambda row: row['area4'][:2] == '0d', 3000)
    rc.a04b_control(r, 3000, 60)
    rc.a06_settle(r, 10)
    return dict(what='button [54] with flag 0x12 = 0xFF: the keyed ride, AREA13 entry 0', hp_end=story(s)['hp'])


def beat_shotgun(r):
    rc.use_a04b_sampler(r)
    s = r.s
    before = s.read(0x810C60, 0x60)
    for k in range(700):
        row = r.rows[-1]
        if row['m1F0'] in rc.A04_GRABS:
            rc.a04_shake(r)
            continue
        if s.read(SHOTGUN_NODE, 8)[5] != 0:      # the take started
            break
        near = math.hypot(row['pos'][0] - SHOTGUN[0], row['pos'][2] - SHOTGUN[1]) <= 8
        lx, ly = rc.stick_values(r, *SHOTGUN)
        r.set_pad(rc.PAD['CROSS'] if near and k % 6 < 2 else 0, lx, ly)
        r.step(1)
    else:
        raise TimeoutError('the unit was not taken: ' + rc.summary(r.rows[-1]))
    r.set_pad(0)
    r.until(lambda row: s.read(0x810130, 8)[4:6] == bytes((4, 3)), 900)     # the V.U.R.S. page's notice
    r.idle(30)
    r.press('CROSS', 2, after=30)               # the notice to the list
    r.press('CROSS', 2, after=30)               # the entry: the prompt, on No
    r.press('LEFT', 2, after=20)
    r.press('CROSS', 2, after=60)               # Yes: D_00810CA6 = 1
    r.press('TRIANGLE', 2)
    r.until(rc.in_control, 900)
    rc.a06_settle(r, 10)
    after = s.read(0x810C60, 0x60)
    if s.read(0x810CA4, 4)[2] != 1:
        raise RuntimeError('the unit is not fitted: ' + s.read(0x810CA4, 8).hex())
    gained = {hex(0x810C60 + i): (before[i], after[i]) for i in range(0x60) if before[i] != after[i]}
    return dict(what='the SHOTGUN UNIT at (416.1, 15, 102.2): take, the V.U.R.S. page, fitted', gained=gained,
                hp_end=story(s)['hp'])


def beat_fight_shotgun(r):
    s = r.s
    use_nodes(s)
    use_sampler(r)
    rc.a06_face(r, math.pi)
    hp0 = story(s)['hp']
    tick = 0
    while True:
        c, st = creature(s), story(s)
        if c['hp'] == 0 and c['state'] == 2:
            break
        if st['hp'] <= 0:
            raise RuntimeError(f'the player died: {c} {st}')
        if r.rows[-1]['m1F0'] in rc.A04_GRABS:
            rc.a04_shake(r)
            continue
        tick += 1
        buttons = rc.PAD['R1']
        if tick >= 30 and tick % 50 < 2:
            buttons |= rc.PAD['SQUARE']
        r.set_pad(buttons)
        r.step(1)
    r.set_pad(0)
    dead = r.frame_index
    for _ in range(1500):
        r.step(1)
        if story(s)['counter'] == 0xFF and rc.in_control(r.rows[-1]):
            break
    else:
        raise TimeoutError('no control after the creature: ' + rc.summary(r.rows[-1]))
    st = story(s)
    return dict(what='R1 and Square (the shotgun) at the creature to its death, [3] stage 2, control',
                hp_before=hp0, hp_end=st['hp'], inf_end=st['inf'], shells_end=s.read(0x810CA8, 4)[0],
                death_frame=dead, creature=hex(CREATURE), bugs={k: hex(v) for k, v in BUGS.items()})


BEATS = {
    'g0': ('a04b_g0_shotgun', ROUTE / 'a04b_01_npc/state.p2s', 'a04b_01_npc', beat_shotgun),
    'g1': ('a04b_g1_door45', OUT / 'a04b_g0_shotgun/state.p2s', 'a04b_g0_shotgun', rc.a04b_beat_door45_out),
    'g2': ('a04b_g2_reader', OUT / 'a04b_g1_door45/state.p2s', 'a04b_g1_door45', rc.a04b_beat_reader),
    'g3': ('a04b_g3_fight', OUT / 'a04b_g2_reader/state.p2s', 'a04b_g2_reader', beat_fight_shotgun),
    'g4': ('a04b_g4_drop', OUT / 'a04b_g3_fight/state.p2s', 'a04b_g3_fight', None),
    'g5': ('a04b_g5_lift', OUT / 'a04b_g4_drop/state.p2s', 'a04b_g4_drop', None),
    'fight': ('a04b_s5_fight', ROUTE / 'a04b_03_reader/state.p2s', 'a04b_03_reader', beat_fight),
    'drop': ('a04b_s6_drop', OUT / 'a04b_s5_fight/state.p2s', 'a04b_s5_fight', beat_drop),
    'lift': ('a04b_s7_lift', OUT / 'a04b_s6_drop/state.p2s', 'a04b_s6_drop', beat_lift),
}


def run(which):
    global CREATURE
    name, src, source, fn = BEATS[which]
    rc.use_fork()
    if which in ('g1', 'g2', 'g4', 'g5'):
        # the wait for the idle Use-polling state (EIGHTH_LEVEL_ROUTE.md
        # "Fork continuation work": neutral timing only)
        from record_level_routes import install_idle_wait
        install_idle_wait(rc, [])
    if which in ('g4', 'g5'):
        fn = beat_drop if which == 'g4' else beat_lift
        meta = json.loads((OUT / 'a04b_g3_fight/trace.json').read_text())
        CREATURE = int(meta['creature'], 16)
        BUGS.update({k: int(v, 16) for k, v in meta['bugs'].items()})
        set_nodes()
    rc.beat_dir = lambda n: OUT / n
    out = OUT / name
    if out.exists():
        raise SystemExit(f'{out} exists; remove it first')
    with rc.open_session(src, log_dir=OUT.parent / 'logs' / name) as s:
        s.step(LEAD_IN)
        r = rc.Route(s)
        boss = []
        step = r.step

        def sampled_step(n=1):
            row = None
            for _ in range(n):
                row = step(1)
                boss.append(dict(extras(s), f=row['f'], counter=row['counter']))
            return row
        r.begin()
        boss.append(dict(extras(s), f=0, counter=r.rows[0]['counter']))
        r.step = sampled_step
        try:
            meta = fn(r)
        except Exception as exc:
            fail = OUT.parent / '_failed' / name
            fail.mkdir(parents=True, exist_ok=True)
            (fail / 'trace.json').write_text(json.dumps({'error': repr(exc), 'inputs': r.inputs, 'rows': r.rows,
                                                         'boss': boss}, separators=(',', ':')))
            raise
        meta.update(source=source, tail_idle_frames=0, emulator='fork', generation=rc.GENERATION,
                    source_state=str(src), lead_in_frames=LEAD_IN, phase_correction_frames=0, phase_lock=False,
                    recorder='port tools/capture_boss_fight.py')
        saved = r.save(name, meta)
        (saved / 'boss.json').write_text(json.dumps(boss, separators=(',', ':')) + '\n')
        print(name, 'frames', r.frame_index, {k: v for k, v in meta.items() if k in (
            'hp_end', 'inf_end', 'rounds_end', 'shells_end', 'death_frame', 'gained', 'creature')})


def replay():
    """One session from a04b_03's source state through the three beats,
    open loop, every frame in a capture store (see the docstring)."""
    rc.use_fork()
    a04b_03 = json.loads((ROUTE / 'a04b_03_reader/trace.json').read_text())
    beats = [json.loads((OUT / n / 'trace.json').read_text())
             for n in ('a04b_s5_fight', 'a04b_s6_drop', 'a04b_s7_lift')]
    src = ROUTE / 'a04b_02_door45_out/state.p2s'
    store_out = OUT.parent / 'store'
    if store_out.exists():
        raise SystemExit(f'{store_out} exists; remove it first')
    report = {'beats': []}
    with rc.open_session(src, log_dir=OUT.parent / 'logs' / 'replay') as s:
        client = s._client
        root = Path(s._inst.root)
        spans = [dict(name='creature', src='ee', addr=CREATURE, len=0x300),
                 dict(name='player', src='ee', addr=0x8102B0, len=0x2F0),
                 dict(name='story', src='ee', addr=0x810700, len=0x640),
                 dict(name='r3', src='ee', addr=0x7AA590, len=0x300),
                 dict(name='spad', src='ee', addr=0x70003B60, len=0x40)]
        client.call('table_add', name='boss', path='boss_table.bin', trigger='tick', spans=spans)
        for name, start, n in (('counter12', 0x8107EA, 1), ('flag12', 0x81076A, 1), ('hp34', CREATURE + 0x34, 2)):
            client.call('memwatch_add', start=start, len=n, access='w', onchange=True, description=name)
        client.call('capture_start', dir='store', regions='default', keyframe_every=120, state_every=120,
                    max_bytes=6 * 2**30)
        s.step(LEAD_IN)

        def play(trace, frame_base):
            r = rc.Route(s)
            r.begin()
            use_sampler(r)
            inputs = {i['f']: i for i in trace['inputs']}
            bad = []
            for f in range(trace['frames'] + 1):
                row = r.rows[-1]
                want = trace['rows'][f]
                if (row['counter'], row['pos'], row['m1F0'], row['yaw']) != (
                        want['counter'], want['pos'], want['m1F0'], want['yaw']):
                    bad.append(f)
                    if len(bad) > 5:
                        break
                if f == trace['frames']:
                    break
                if f in inputs:
                    i = inputs[f]
                    r.set_pad(i['buttons'], i['lx'], i['ly'])
                r.step(1)
            r.set_pad(0)
            report['beats'].append(dict(beat=trace['beat'], frames=trace['frames'], bad_rows=bad))
            print(trace['beat'], 'rows differ at', bad[:6] if bad else 'none')
            return bad
        ok = not play(a04b_03, 0)
        for t in beats:
            if not ok:
                break
            s.step(LEAD_IN)
            ok = not play(t, 0)
        client.call('capture_stop')
        client.call('table_remove', name='boss')
        log = client.call('memwatch_log', max=5000, symbols=False)
        report['memwatch'] = log.get('records', [])
        shutil.copytree(root / 'store', store_out)
        store_out.joinpath('boss_table.bin').write_bytes((root / 'boss_table.bin').read_bytes())
        schema = root / 'boss_table.bin.json'
        if schema.exists():
            shutil.copy(schema, store_out / 'boss_table.bin.json')
    (OUT.parent / 'replay.json').write_text(json.dumps(report, indent=1) + '\n')


def replay_shotgun():
    """The whole shotgun chain, open loop, in one session from a04b_01's end:
    a store of every frame, the per-tick table, memwatches on counter 0x12,
    flag 0x12 and the creature's +0x34, the rand() log and the state word per
    row (s87/g_*)."""
    global CREATURE
    rc.use_fork()
    names = ('a04b_g0_shotgun', 'a04b_g1_door45', 'a04b_g2_reader', 'a04b_g3_fight', 'a04b_g4_drop', 'a04b_g5_lift')
    beats = [json.loads((OUT / n / 'trace.json').read_text()) for n in names]
    CREATURE = int(beats[3]['creature'], 16)
    BUGS.update({k: int(v, 16) for k, v in beats[3]['bugs'].items()})
    set_nodes()
    store_out = OUT.parent / 'g_store'
    if store_out.exists():
        raise SystemExit(f'{store_out} exists; remove it first')
    report, rows = {'beats': []}, {}
    with rc.open_session(ROUTE / 'a04b_01_npc/state.p2s', log_dir=OUT.parent / 'logs' / 'g_replay') as s:
        client = s._client
        root = Path(s._inst.root)
        block = s.u32(0x24295C)
        client.call('table_add', name='boss', path='boss_table.bin', trigger='tick', spans=[
            dict(name='creature', src='ee', addr=CREATURE, len=0x300),
            dict(name='player', src='ee', addr=0x8102B0, len=0x2F0),
            dict(name='story', src='ee', addr=0x810700, len=0x640),
            dict(name='r3', src='ee', addr=R3, len=0x300),
            dict(name='spad', src='ee', addr=0x70003B60, len=0x40)])
        for name, start, n in (('counter12', 0x8107EA, 1), ('flag12', 0x81076A, 1), ('hp34', CREATURE + 0x34, 2)):
            client.call('memwatch_add', start=start, len=n, access='w', onchange=True, description=name)
        client.call('probe_add', pc=0x122BE0, action='log', path='rand_calls.bin', label='rand_ret')
        client.call('capture_start', dir='store', regions='default', keyframe_every=120, state_every=120,
                    max_bytes=6 * 2**30)
        s.step(LEAD_IN)
        for k, t in enumerate(beats):
            r = rc.Route(s)
            r.begin()
            r.sampler = rc.A01USampler(s, spans=SPANS)
            r.now = lambda sampler=r.sampler: rc.decode_a04b(sampler.raw(), owners=OWNERS)
            inputs = {i['f']: i for i in t['inputs']}
            bad = []
            for f in range(t['frames'] + 1):
                now = r.now()
                want = t['rows'][f]
                rows[want['counter']] = s.u32(block + 0x58)
                if (now['counter'], now['pos'], now['m1F0'], now['yaw']) != (
                        want['counter'], want['pos'], want['m1F0'], want['yaw']):
                    bad.append(f)
                    if len(bad) > 5:
                        break
                if f == t['frames']:
                    break
                if f in inputs:
                    i = inputs[f]
                    s.pad(i['buttons'], lx=i['lx'], ly=i['ly'])
                s.step(1)
            s.pad(0)
            report['beats'].append(dict(beat=t['beat'], frames=t['frames'], bad_rows=bad))
            print(t['beat'], 'rows differ at', bad[:6] if bad else 'none', flush=True)
            if bad:
                break
            if k + 1 < len(beats):
                s.step(LEAD_IN)
        client.call('capture_stop')
        client.call('table_remove', name='boss')
        client.call('probe_remove', all=True)
        report['memwatch'] = client.call('memwatch_log', max=5000, symbols=False).get('records', [])
        shutil.copytree(root / 'store', store_out)
        for name in ('boss_table.bin', 'boss_table.bin.json', 'rand_calls.bin'):
            shutil.copy(root / name, OUT.parent / ('g_' + name))
    (OUT.parent / 'g_replay.json').write_text(json.dumps(report, indent=1) + '\n')
    (OUT.parent / 'g_rand_rows.json').write_text(json.dumps(rows) + '\n')


def write_manifest():
    """The set's manifest.json (tools/build_start_saves.py reads it)."""
    beats = {}
    for d in sorted(OUT.iterdir()):
        if (d / 'trace.json').is_file():
            t = json.loads((d / 'trace.json').read_text())
            beats[d.name] = dict(folder=str(d.relative_to(DECOMP)), source=t['source'], source_state=t['source_state'],
                                 frames=t['frames'], first_counter=t['first_counter'], last_counter=t['last_counter'])
    (OUT / 'manifest.json').write_text(json.dumps(
        dict(what='the first boss fight, pad input only (tools/capture_boss_fight.py)',
             set=str(OUT.relative_to(DECOMP)), beats=beats), indent=1) + '\n')


def anchors(chain):
    """The comparison anchors of tools/test_boss_fight.py, from the replay's
    store, under build/boss_fight/<chain>/ (disc-derived; ignored): the RAM at
    the reader beat's first row (anchor0.bin) and at the row reader [50]'s lock
    byte is set (anchor.bin), restore.txt with each one's script frame and
    clock word, rands.txt (the rand() word at the lock), sync.txt (the script's
    clock at the lock). A loaded game puts every bug back at its placement, one
    beside the reader, so the first restore keeps them where the recording has
    them; the lock is the first row the native run can hold exactly."""
    sys.path.insert(0, str(DECOMP.parent / 'pcsx2-fork/extermination/python'))
    from pcsx2dbg import pxfs
    if chain == 'shotgun':
        first_beat, reader_path, store_name, rows_name = (OUT / 'a04b_g0_shotgun', OUT / 'a04b_g2_reader',
                                                          'g_store', 'g_rand_rows.json')
    else:
        first_beat, reader_path, store_name, rows_name = (ROUTE / 'a04b_03_reader', ROUTE / 'a04b_03_reader',
                                                          'store', 'rand_rows.json')
    first = json.loads((first_beat / 'trace.json').read_text())['first_counter']
    reader = json.loads((reader_path / 'trace.json').read_text())
    lock = next(r for r in reader['rows'] if r['l845'] == 'a8')
    store = pxfs.Store(OUT.parent / store_name)
    frame = lock['counter'] - first
    if store.read_addr('ee', 0x810845, frame, 1)[0] != 0xA8 or store.read_addr('ee', 0x810845, frame - 1, 1)[0] == 0xA8:
        raise SystemExit('the store frames are not the rows')
    out = Path(__file__).resolve().parents[1] / 'build/boss_fight' / chain
    out.mkdir(parents=True, exist_ok=True)
    (out / 'anchor.bin').write_bytes(store.image('ee_ram', frame))
    rows = json.loads((OUT.parent / rows_name).read_text())
    # the script's clock is the row + 1 once the row has run
    f = lock['f'] + 1
    (out / 'rands.txt').write_text(f'{f} {rows[str(lock["counter"])]:08x}\n')
    (out / 'sync.txt').write_text(f'810845:a8:{f}\n')
    clock = struct.unpack('<I', store.read('ee_scratch', frame, 0x3B68, 4))[0]
    start = reader['rows'][0]['counter'] - first
    (out / 'anchor0.bin').write_bytes(store.image('ee_ram', start))
    clock0 = struct.unpack('<I', store.read('ee_scratch', start, 0x3B68, 4))[0]
    base = f'build/boss_fight/{chain}'          # the port root is the run's directory
    (out / 'restore.txt').write_text(f'{base}/anchor0.bin:1:{clock0:x},{base}/anchor.bin:{f}:{clock:x}\n')
    print(chain, 'anchors at rows', reader['rows'][0]['f'], lock['f'])


def rand_log():
    """Every 00122BB8 return of the chain, open loop as replay() (an inline
    log probe, so no observer effect): s87/rand_calls.bin, and the state word
    *(D_0024295C) + 0x58 per row: s87/rand_rows.json."""
    rc.use_fork()
    a04b_03 = json.loads((ROUTE / 'a04b_03_reader/trace.json').read_text())
    beats = [json.loads((OUT / n / 'trace.json').read_text())
             for n in ('a04b_s5_fight', 'a04b_s6_drop', 'a04b_s7_lift')]
    rows = {}
    with rc.open_session(ROUTE / 'a04b_02_door45_out/state.p2s', log_dir=OUT.parent / 'logs' / 'rand') as s:
        block = s.u32(0x24295C)
        s._client.call('probe_add', pc=0x122BE0, action='log', path='rand_calls.bin', label='rand_ret')
        s.step(LEAD_IN)
        for t in [a04b_03] + beats:
            inputs = {i['f']: i for i in t['inputs']}
            pad = (0, 0x7F, 0x7F)
            for f in range(t['frames'] + 1):
                rows[t['rows'][f]['counter']] = s.u32(block + 0x58)
                if f == t['frames']:
                    break
                if f in inputs:
                    i = inputs[f]
                    pad = (i['buttons'], i['lx'], i['ly'])
                    s.pad(pad[0], lx=pad[1], ly=pad[2])
                s.step(1)
            s.pad(0)
            s.step(LEAD_IN)
        s._client.call('probe_remove', all=True)
        calls = Path(s._inst.info()['root']) / 'rand_calls.bin'
        shutil.copy(calls, OUT.parent / 'rand_calls.bin')
    (OUT.parent / 'rand_rows.json').write_text(json.dumps(rows) + '\n')


if __name__ == '__main__':
    what = sys.argv[1:] or ['fight']
    for w in what:
        if w == 'manifest':
            write_manifest()
        elif w == 'anchors':
            anchors('shotgun')
            anchors('pistol')
        elif w == 'replay':
            replay()
        elif w == 'rand':
            rand_log()
        elif w == 'greplay':
            replay_shotgun()
        else:
            run(w)
