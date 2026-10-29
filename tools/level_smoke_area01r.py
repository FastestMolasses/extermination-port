"""AREA01 revisit recorded input and strict scene observations, fork by default.

The revisit's main line (FOURTH_LEVEL_ROUTE.md section 4; docs/AREA01_REVISIT.md):
a01r_00 (back to the train room), a01r_01 (the event: D_008107DF 1 .. 0xFF,
D_0081075F = 0xFF, item 0x20) and a01r_02 (the bridge: D_008107E0 -> 0xFF,
D_00810760 = 0xFF). a01r_00 starts where AREA00's a00_10 return ends.
The side beats (FOURTH_LEVEL_ROUTE.md section 4) branch from a main beat's
end: a01r_s0 (the event's pickup) from a01r_01, a01r_s1 (door [14],
locked) from a01r_02; a run --until a side plays the main line to its
source, then only the side.
Preparation exports pads only. A native route is certified only by the row
comparisons below; they do not certify rendered pixels.
"""
import hashlib
import json
import re
import struct

import level_smoke_area00 as a00
import level_smoke_area01 as a01
import reference_root as refs

ROUTE_DIR = refs.capture('s87/route_a01r')
# AREA02's beats follow the revisit on the same native path (their pads in
# the same EM_LEVEL4_PAD_DIR); their recordings are route_a02's.
A02_ROUTE_DIR = refs.capture('s87/route_a02')
MAIN_BEATS = {'a01r_00': 'a01r_00_to_train_room', 'a01r_01': 'a01r_01_event', 'a01r_02': 'a01r_02_bridge',
              'a01r_03': 'a01r_03_door16', 'a02_00': 'a02_00_duct'}
SIDE_BEATS = {'a01r_s0': 'a01r_s0_pickup', 'a01r_s1': 'a01r_s1_door14_locked'}
# em_level_smoke_test.c's phase order: each side right after its source.
ROUTE_PHASES = ('a01r_00', 'a01r_01', 'a01r_s0', 'a01r_02', 'a01r_s1', 'a01r_03', 'a02_00')
SOURCES = {'a01r_00': 'a00_10', 'a01r_01': 'a01r_00', 'a01r_02': 'a01r_01', 'a01r_03': 'a01r_02',
           'a01r_s0': 'a01r_01', 'a01r_s1': 'a01r_02', 'a02_00': 'a01r_03'}
# FOURTH_LEVEL_ROUTE.md section 1.1 (measured in the a00_10 end snapshot;
# route_capture checks every one against each beat's saved RAM).
OWNERS = dict(shaft_door_r12=0x7ADD60, r13_158D30=0x7AE050, door_r14=0x7AE340, door_r15=0x7AE630,
              door_r16=0x7AE920, bridge_r41=0x7B3290, bridge_r42=0x7B3580, r45_8267C0=0x7B3E50,
              g5_825950=0x7A64F0, g7_825950=0x7A6AD0, g6_826CF0=0x7A67E0, g0_823CD0=0x7A5640,
              g1_823CD0=0x7A5930, g2_823CD0=0x7A5C20, g3_823CD0=0x7A5F10, g4_823CD0=0x7A6200)
# FOURTH_LEVEL_ROUTE.md section 1.1, A02_OWNERS: the sub-1 table s1_* and
# the sub-0 table s0_* (the loads reuse the pool addresses; every AREA02 row
# samples both). The tick log's "a02" observation (em_scene_bindings.c
# log_area02) holds the same nodes.
A02_OWNERS = dict(s1_pick_g0=0x7A5640, s1_pick_g1=0x7A5930, s1_pick_g2=0x7A5C20, s1_bed_r0=0x7A5F10,
                  s1_r1_1C4AF0=0x7A6200, s1_r2_159970=0x7A64F0, s1_save_r3=0x7A67E0, s1_panel_r4=0x7A6AD0,
                  s1_door_r5=0x7A6DC0, s1_door_r6=0x7A70B0, s0_lock_r21=0x7AD780, s0_door_r22=0x7ADA70,
                  s0_panel_r24=0x7AE050, s0_door_r25=0x7AE340, s0_lamp_r26=0x7AE630, s0_r27_825100=0x7AE920,
                  s0_turntable_r28=0x7AEC10, s0_r31_823980=0x7AF4E0, s0_k7_r32=0x7AF7D0, s0_k8_r33=0x7AFAC0,
                  s0_k11_r34=0x7AFDB0, s0_k10_r35=0x7B00A0, s0_gate_r36=0x7B0390, s0_gate_r37=0x7B0680,
                  s0_gate_r38=0x7B0970)


def owners_of(phase):
    """The recorded owner table and the tick log's observation key."""
    return (A02_OWNERS, 'a02') if phase.startswith('a02_') else (OWNERS, 'a01r')


# The fork recorder's input corrections (trace metadata v263_pad_corrections).
PAD_CORRECTIONS = {'a01r_01': [('late', 0, 5)]}


def capture_path(phase):
    if phase == 'a00_10':
        return a00.capture_path(phase)
    return (A02_ROUTE_DIR if phase.startswith('a02_') else ROUTE_DIR) / (MAIN_BEATS | SIDE_BEATS)[phase] / 'trace.json'


def revisit_path(until):
    """The revisit phases a run --until `until` plays: the main line up to
    it, without the side beats it passes (a side: then only that side)."""
    assert until in ROUTE_PHASES, ('unknown AREA01 revisit phase', until)
    return tuple(p for p in ROUTE_PHASES[:ROUTE_PHASES.index(until) + 1] if p not in SIDE_BEATS or p == until)


def phase_path(until):
    """The native path: New Game through AREA00's a00_10, then the revisit."""
    return (*a00.phase_path('a00_10'), *revisit_path(until))


def source_capture(phase):
    source = SOURCES[phase]
    return a00.route_capture(source) if source == 'a00_10' else route_capture(source)


def route_capture(phase):
    path = capture_path(phase)
    capture = json.loads(path.read_text())
    rows = capture['rows']
    assert not capture.get('teleports'), (phase, 'reference contains teleports')
    assert len(rows) == capture['frames'] + 1, (phase, 'truncated reference')
    assert all(r['f'] == i and r['counter'] == capture['first_counter'] + i
               for i, r in enumerate(rows)), (phase, 'noncontiguous reference')
    assert rows[-1]['counter'] == capture['last_counter'], (phase, 'reference endpoint')
    assert capture['source'] == capture_path(SOURCES[phase]).parent.name, \
        (phase, 'reference source differs from native path', capture['source'])
    if refs.FORK_MODE:
        applied = [(c['kind'], c['f'], c['frames']) for c in capture.get('v263_pad_corrections', [])]
        assert applied == PAD_CORRECTIONS.get(phase, []), (phase, 'reference pad correction changed', applied)
        assert all(c.get('applied') for c in capture.get('v263_pad_corrections', []))
    return capture


def check_owner_nodes(phase, capture=None):
    """Each OWNERS node holds the recorded owner in the beat's saved RAM:
    callback +0x10 and position +0xB0 equal the last row's."""
    capture = capture or route_capture(phase)
    ram = capture_path(phase).with_name('eeMemory.bin').read_bytes()
    row = capture['rows'][-1]
    for name, node in owners_of(phase)[0].items():
        callback = struct.unpack_from('<I', ram, node + 0x10)[0]
        pos = [round(v, 5) for v in struct.unpack_from('<3f', ram, node + 0xB0)]
        want = row[name]
        assert callback == int(want['cb'], 16) and ram[node:node + 16].hex() == want['h'], \
            (phase, name, 'recorded owner is not at its node', hex(node))
        assert pos == [round(v, 5) for v in want['pos']], (phase, name, 'position', pos, want['pos'])


def source_gap(phase, capture):
    source = source_capture(phase)
    last = source['inputs'][-1]
    assert (last['buttons'], last['lx'], last['ly']) == (0, 127, 127), (phase, 'source pad is held')
    gap = capture['first_counter'] - source['last_counter']
    assert 1 <= gap <= 120, (phase, 'source counter gap', gap)
    return gap


def command_rows(phase, capture):
    """The recorder's effective pad (level_smoke_area00.command_rows with this
    level's corrections): a late first input yields to a newer command in
    its window, as the original capture driver does. A beat whose first
    input comes later (a01r_s1: f10) holds the pad the session started
    with until then (route_capture: the pad as the session left it,
    cleared), which is the C driver's pad before its first command."""
    commands = capture['inputs']
    corrections = PAD_CORRECTIONS.get(phase, [])
    assert commands and (commands[0]['f'] == 0 or not corrections)
    assert all(a['f'] <= b['f'] for a, b in zip(commands, commands[1:])), (phase, 'backward command')
    out, policy, sent, index = [], (0, 127, 127), None, 0
    frames = sorted({r['f'] for r in commands} | {f for _, f, n in corrections} |
                    {f + n for _, f, n in corrections})
    for frame in frames:
        changed = []
        while index < len(commands) and commands[index]['f'] == frame:
            r = commands[index]
            policy = (r['buttons'], r['lx'], r['ly'])
            changed.append(policy)
            index += 1
        for value in changed or [policy]:
            effective = value
            for kind, begin, duration in corrections:
                if begin <= frame < begin + duration:
                    if kind == 'drop' or (kind == 'late' and frame == begin):
                        effective = (0, 127, 127)
            if changed or effective != sent:
                out.append(dict(f=frame, buttons=effective[0], lx=effective[1], ly=effective[2]))
                sent = effective
    return out


def prepare_pads(out):
    out.mkdir(parents=True, exist_ok=True)
    manifest, count = [], 0
    for phase in ROUTE_PHASES:
        capture = route_capture(phase)
        rows = command_rows(phase, capture)
        frames, gap = capture['frames'], source_gap(phase, capture)
        assert 0 < len(rows) <= 2048 and 1 <= frames <= 20000
        assert all(0 <= r['f'] <= frames and 0 <= r['buttons'] <= 65535 and
                   0 <= r['lx'] <= 255 and 0 <= r['ly'] <= 255 for r in rows)
        lines = [f'EMA1 1 {frames} {gap}'] + [f"{r['f']} {r['buttons']:x} {r['lx']} {r['ly']}" for r in rows]
        (out / f'{phase}.pad').write_text('\n'.join(lines) + '\n')
        manifest.append(dict(phase=phase, reference=refs.label(),
            capture=str(capture_path(phase)), capture_sha256=hashlib.sha256(capture_path(phase).read_bytes()).hexdigest(),
            endpoint_row=frames, gap=gap, pad_changes=len(rows), native_path=phase_path(phase),
            command_delay_rows=2, corrections=PAD_CORRECTIONS.get(phase, []),
            certification='input preparation only; independent native capture comparison required'))
        count += len(rows)
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return count


def check_camera_end(tick, phase):
    path = capture_path(phase)
    capture = route_capture(phase)
    snapshot = json.loads(path.with_name('snapshot.json').read_text())
    assert snapshot['main_loop_counter'] == capture['last_counter'], (phase, 'camera snapshot counter differs')
    ram = path.with_name('eeMemory.bin').read_bytes()
    assert hashlib.sha256(ram).hexdigest() == snapshot['ee_sha256'], (phase, 'snapshot RAM digest differs')
    blocks = tick.get('camblk')
    # the camera block, the forward, D_00810690.. and the eye / target / up
    # quads D_008105D0.. (a01.CAMERA_BLOCKS: the tick log's camblk)
    assert blocks and len(blocks) == len(a01.CAMERA_BLOCKS), (phase, 'missing complete camera observation')
    for (address, size), value in zip(a01.CAMERA_BLOCKS, blocks):
        assert bytes.fromhex(value) == ram[address:address + size], (phase, 'ending camera differs', hex(address))


def compare_route_row(smoke, ticks, i, row, phase):
    """Every field a00's rows compare (player, ground, camera, task, request,
    message, fade, health, progress, locks, weapon bytes), the revisit's
    sixteen owners and D_00810760..63 (story760). D_008107DF (df) and
    D_008107E0 (e0) are bytes 7 and 8 of the compared D_008107D8 window, and
    D_0081075F byte 7 of story758."""
    tick, following = ticks[i:i + 2]
    p, o = smoke.port_view(ticks, i), smoke.orig_view(row)
    keys = ('spad', 'screen', 'msg', 'power', 'pos', 'yaw', 'eye', 'tgt')
    got, want = {k: p[k] for k in keys}, {k: o[k] for k in keys}
    post, player = bytes.fromhex(tick['post']), tick['player']
    got.update(cam=tick['cam4'], req=post[smoke.tsr.OFFSET[0x8106B0]:smoke.tsr.OFFSET[0x8106B0] + 10].hex(),
               area=post[smoke.tsr.OFFSET[0x810700]:smoke.tsr.OFFSET[0x810700] + 3].hex(),
               slot0=tuple(post[:5]), player=tuple(player[k] for k in (0, 1, 2, 3, 6)),
               ground=hex(player[5]), clock=round(smoke.f32(player[4]), 3), fade=following['fade8'],
               **a01.camera_vectors(tick, (phase, row['f'])))
    want.update(cam=row['cam_mode'], req=row['req'], area=row['area4'][:6],
                slot0=tuple(bytes.fromhex(row['slots'])[8:13]),
                player=(row['p5'], row['m1F0'], row['m1F1'], row['clip'], row['b2F3']),
                ground=row['ground'], clock=row['clock'], fade=row['fade'][:16],
                cam_eye=row['cam_eye'], cam_tgt=row['cam_tgt'], fwd=row['fwd'])
    owners, key = owners_of(phase)
    observed = tick.get(key)
    assert observed, (phase, row['f'], 'missing AREA01 revisit observations')
    got.update(hp=round(smoke.f32(observed['hp']), 3), progress=observed['progress'], s760=observed['s760'],
               locks=observed['locks'], wpn=observed['wpn'], d7dc=observed['progress'][0][8:10])
    want.update(hp=row['hp'], progress=[row['d2'], row['story758'], row['taken'], row['docs']],
                s760=row['story760'], locks=row['locks'], wpn=row['wpn'], d7dc=row['d7dc'])
    for name, address in owners.items():
        value = observed['owners'].get(str(address))
        assert value is not None, (phase, row['f'], name, 'missing canonical owner')
        rawpos = [round(v, 5) for v in struct.unpack('<3f', bytes.fromhex(value[1]))]
        got[name] = [value[0], rawpos, *value[2:]]
        r = row[name]
        want[name] = [r['h'], [round(v, 5) for v in r['pos']], r['s1F0'], r['t2DC'], int(r['cb'], 16)]
    differences = {k: (got[k], want[k]) for k in got if got[k] != want[k]}
    assert not differences, (phase, f'row {row["f"]}', f'tick {tick["tick"]}', differences)


def route_start(ticks, run, state, phase, capture):
    match = re.search(rf'^level smoke: {phase}: aligned counter=(\d+)', run, re.M)
    assert match, (phase, 'missing alignment')
    counter = int(match.group(1))
    source = SOURCES[phase]
    ends = state.get('area00_ends' if source == 'a00_10' else 'a01r_ends', {})
    source_counter = ends.get(source)
    assert source_counter is not None, (phase, 'source phase not checked')
    gap = source_gap(phase, capture)
    assert counter == source_counter + gap, (phase, 'source counter gap differs', counter, source_counter, gap)
    start = next(i for i, tick in enumerate(ticks) if tick['counter'] == counter)
    assert start == state['cursor'] + gap - 1, (phase, 'source gap skipped a tick')
    assert all(ticks[i]['counter'] == source_counter + i - state['cursor'] + 1
               for i in range(state['cursor'], start)), (phase, 'noncontiguous source gap')
    return start, counter


def check_route(smoke, ticks, run, state, phase):
    capture = route_capture(phase)
    rows = capture['rows']
    start, counter = route_start(ticks, run, state, phase, capture)
    if phase in a00.RETURN_TICKS:
        # door [16]'s AREA02 load: the gameplay rows, the loader's states (a
        # host wait may be shorter) and the fixed rows after AREA02 state 0
        pairs = a00.return_pairs(smoke, ticks, run, state, phase, start, rows)
    else:
        pairs = [(start + k, row) for k, row in enumerate(rows)]
    end = pairs[-1][0]
    assert end + 1 < len(ticks), (phase, 'missing post-frame tail')
    if phase not in a00.RETURN_TICKS:
        assert all(ticks[i]['counter'] == counter + i - start for i in range(start, end + 1)), \
            (phase, 'skipped native main-loop tick')
    for i, row in pairs:
        compare_route_row(smoke, ticks, i, row, phase)
    check_camera_end(ticks[end], phase)
    state['cursor'] = end + 1
    state.setdefault('a01r_ends', {})[phase] = ticks[end]['counter']
    last = rows[-1]
    print(f'{phase}: PASS {len(rows)} native rows: player, ground, camera, task, request, message, fade, '
          f'health, progress (D_008107DF {last["df"]}, D_008107E0 {last["e0"]}, D_00810760 '
          f'{last["story760"][:2]}), locks, weapon bytes and 16 revisit owners; ending camera bytes '
          'equal to saved RAM')


def prefix_report(smoke, ticks, run, phase):
    """After a failed run: the rows of `phase` matched exactly before the first
    difference or the end of the log. A diagnostic, never a PASS."""
    match = re.search(rf'^level smoke: {phase}: aligned counter=(\d+)', run, re.M)
    if not match:
        return None
    counter = int(match.group(1))
    start = next((i for i, t in enumerate(ticks) if t['counter'] == counter), None)
    if start is None:
        return None
    rows = route_capture(phase)['rows']
    exact, first = 0, None
    for k, row in enumerate(rows):
        if start + k + 1 >= len(ticks) or ticks[start + k]['counter'] != counter + k:
            break
        try:
            compare_route_row(smoke, ticks, start + k, row, phase)
        except AssertionError as e:
            first = e.args[0]
            break
        exact += 1
    return exact, len(rows), first
