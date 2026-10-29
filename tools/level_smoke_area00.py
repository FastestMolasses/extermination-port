"""AREA00 recorded input and strict scene observations, fork by default.

Preparation exports pads only. A native route is certified only by the
independent row comparisons below; these do not certify rendered pixels.
"""
import hashlib
import json
import re
import struct

import level_smoke_area01 as a01
import reference_root as refs

ROUTE_DIR = refs.capture('s87/route_a00')
ARRIVAL_CAPTURE = a01.capture_path('a01_07')
ARRIVAL_ROW = a01.AREA00_ARRIVAL_ROW + 1
MAIN_BEATS = dict(zip((f'a00_{i:02}' for i in range(11)), (
    'a00_00_descend', 'a00_01_door51_locked', 'a00_02_south_route',
    'a00_03_padlock', 'a00_04_cage_terminal', 'a00_05_ferry_deck',
    'a00_06_cab_roof', 'a00_07_duct_to_ne_room', 'a00_08_switch',
    'a00_09_ne_room_out', 'a00_10_progression_exit')))
SIDE_BEATS = {'a00_s0': 'a00_s0_shaft_door_back'}
ROUTE_PHASES = ('a00_s0', *MAIN_BEATS)
SOURCES = dict(zip(MAIN_BEATS, ('a00_arrival', *tuple(MAIN_BEATS)[:-1]))) | {'a00_s0': 'a00_arrival'}
RETURN_TICKS = {'a00_10': 116, 'a00_s0': 176}
OWNERS = dict(shaft_door_r52=0x7B7C00, door_r56=0x7B87C0, door_r51=0x7B7910,
              lock_r54=0x7B81E0, door_r55=0x7B84D0, terminal_r40=0x7B58C0,
              ferry_cab_r47=0x7B6D50, ferry_r48=0x7B7040, r43_825920=0x7B6190,
              beam_r60=0x7B9380)
# The original recorder's corrections reproduce measured v2.6.3 input
# delivery. Fork trace metadata records these same corrections (decomp
# PCSX2_FORK.md, "Results, level groups", and Route._send_corrected_pad).
PAD_CORRECTIONS = {'a00_01': [('late', 0, 1)], 'a00_03': [('late', 0, 2)],
                   'a00_04': [('drop', 70, 1)], 'a00_08': [('late', 0, 3)],
                   'a00_10': [('late', 0, 3)]}


def capture_path(phase):
    return ARRIVAL_CAPTURE if phase == 'a00_arrival' else ROUTE_DIR / (MAIN_BEATS | SIDE_BEATS)[phase] / 'trace.json'


def phase_path(until):
    assert until in ('a00_arrival', *ROUTE_PHASES), ('unknown AREA00 phase', until)
    if until == 'a00_arrival':
        return ('a00_arrival',)
    if until in SIDE_BEATS:
        return ('a00_arrival', until)
    return ('a00_arrival', *tuple(MAIN_BEATS)[:tuple(MAIN_BEATS).index(until) + 1])


def route_capture(phase):
    path = capture_path(phase)
    capture = json.loads(path.read_text())
    rows = capture['rows']
    assert not capture.get('teleports'), (phase, 'reference contains teleports')
    assert len(rows) == capture['frames'] + 1, (phase, 'truncated reference')
    assert all(r['f'] == i and r['counter'] == capture['first_counter'] + i
               for i, r in enumerate(rows)), (phase, 'noncontiguous reference')
    assert rows[-1]['counter'] == capture['last_counter'], (phase, 'reference endpoint')
    if phase == 'a00_arrival':
        row = rows[ARRIVAL_ROW]
        assert row['area4'] == '00000000' and bytes.fromhex(row['slots'])[8:12] == bytes((3, 1, 0, 1)), \
            'AREA00 rebuild boundary moved'
        last = [i for i in capture['inputs'] if i['f'] <= ARRIVAL_ROW][-1]
        assert (last['buttons'], last['lx'], last['ly']) == (0, 127, 127), 'arrival pad is not neutral'
        assert not any(i['f'] > ARRIVAL_ROW for i in capture['inputs']), 'arrival changes its pad'
        assert capture['frames'] - ARRIVAL_ROW == 4016, 'AREA00 arrival script duration moved'
        assert all((r['p5'], r['m1F0'], r['clip']) == (0, 0, 0) for r in rows[-60:]), \
            'AREA00 last 60 rows are not neutral control'
    else:
        assert capture['source'] == capture_path(SOURCES[phase]).parent.name, \
            (phase, 'reference source differs from native path', capture['source'])
        if refs.FORK_MODE:
            applied = [(c['kind'], c['f'], c['frames']) for c in capture.get('v263_pad_corrections', [])]
            assert applied == PAD_CORRECTIONS.get(phase, []), (phase, 'reference pad correction changed', applied)
            assert all(c.get('applied') for c in capture.get('v263_pad_corrections', []))
    return capture


def source_gap(phase, capture):
    if phase == 'a00_arrival':
        return 1
    source = route_capture(SOURCES[phase])
    last = source['inputs'][-1]
    assert (last['buttons'], last['lx'], last['ly']) == (0, 127, 127), (phase, 'source pad is held')
    gap = capture['first_counter'] - source['last_counter']
    assert 1 <= gap <= 120, (phase, 'source counter gap', gap)
    return gap


def command_rows(phase, capture):
    """Replay the recorder's effective pad, preserving same-frame ordering.

    A late first input yields to a newer command in its window, as the
    original capture driver does. A dropout overrides commands for its
    whole window and restores the current policy input at its end.
    """
    if phase == 'a00_arrival':
        return [dict(f=0, buttons=0, lx=127, ly=127)]
    commands = capture['inputs']
    corrections = PAD_CORRECTIONS.get(phase, [])
    assert commands and commands[0]['f'] == 0
    assert all(a['f'] <= b['f'] for a, b in zip(commands, commands[1:])), (phase, 'backward command')
    out, policy, sent, index = [], (0, 127, 127), None, 0
    # Events between policy commands matter at both ends of a correction.
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


def prepare_pads(out, include_sides=False):
    out.mkdir(parents=True, exist_ok=True)
    manifest, count = [], 0
    for phase in ('a00_arrival', *MAIN_BEATS, *(SIDE_BEATS if include_sides else ())):
        capture = route_capture(phase)
        rows = command_rows(phase, capture)
        frames = capture['frames'] - (ARRIVAL_ROW if phase == 'a00_arrival' else 0)
        gap = source_gap(phase, capture)
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


def compare_route_row(smoke, ticks, i, row, phase, owners=True):
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
    observed = tick.get('a00')
    assert observed, (phase, row['f'], 'missing AREA00 canonical observations')
    got.update(hp=round(smoke.f32(observed['hp']), 3), progress=observed['progress'])
    want.update(hp=row['hp'], progress=[row['d2'], row['story758'], row['taken'], row['docs']])
    if owners:
        for key in ('locks', 'wpn'):
            got[key], want[key] = observed[key], row[key]
        got['d7dc'], want['d7dc'] = observed['progress'][0][8:10], row['d7dc']
        for name, address in OWNERS.items():
            value = observed['owners'].get(str(address))
            assert value is not None, (phase, row['f'], name, 'missing canonical owner')
            rawpos = [round(v, 5) for v in struct.unpack('<3f', bytes.fromhex(value[1]))]
            got[name] = [value[0], rawpos, *value[2:]]
            r = row[name]
            want[name] = [r['h'], r['pos'], r['s1F0'], r['t2DC'], int(r['cb'], 16)]
    differences = {k: (got[k], want[k]) for k in got if got[k] != want[k]}
    assert not differences, (phase, f'row {row["f"]}', f'tick {tick["tick"]}', differences)


def route_start(ticks, run, state, phase, capture):
    match = re.search(rf'^level smoke: {phase}: aligned counter=(\d+)', run, re.M)
    assert match, (phase, 'missing alignment')
    counter = int(match.group(1))
    if phase == 'a00_arrival':
        boundary = state.get('area00_arrival')
        assert boundary is not None, 'AREA00 load boundary has not been checked'
        source_counter = ticks[boundary]['counter']
    else:
        source_counter = state.get('area00_ends', {}).get(SOURCES[phase])
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
    rows = capture['rows'][ARRIVAL_ROW:] if phase == 'a00_arrival' else capture['rows']
    start, counter = route_start(ticks, run, state, phase, capture)
    pairs = [(start + k, row) for k, row in enumerate(rows)]
    if phase in RETURN_TICKS:
        pairs = return_pairs(smoke, ticks, run, state, phase, start, rows)
    assert pairs[-1][0] + 1 < len(ticks), (phase, 'missing post-frame tail')
    end = pairs[-1][0]
    assert all(ticks[i]['counter'] == counter + i - start for i in range(start, end + 1)), \
        (phase, 'skipped native main-loop tick')
    for i, row in pairs:
        compare_route_row(smoke, ticks, i, row, phase, owners=phase != 'a00_arrival')
    check_camera_end(ticks[end], phase)
    state['cursor'] = end + 1
    state.setdefault('area00_ends', {})[phase] = ticks[end]['counter']
    print(f'{phase}: PASS {len(pairs)} native rows: player, ground, camera, task, request, message, '
          'fade, health and progress; ending camera bytes equal to saved RAM; ' +
          ('arrival script and final 60 neutral rows (AREA00 owners absent from arrival recording)' if phase == 'a00_arrival'
           else '10 recorded owners, locks, weapon bytes and ferry position'))
    if phase in RETURN_TICKS:
        flag = bytes.fromhex(rows[-1]['story758'])[6]
        assert flag == (0xFF if phase == 'a00_10' else 0), (phase, 'return story flag changed', flag)
        print(f'{phase}: AREA01 return D_0081075E={flag:02X}; bridge motion is outside this recording endpoint')


def return_alignment(smoke, ticks, run, phase, start, rows):
    """The established EXIT rule: only repeated loader holds may shrink.

    Kept separate from the instruction oracle so contract fixtures can
    exercise the alignment without claiming any original execution.
    """
    boundary = re.search(rf'^level smoke: {phase}: boundary counter=(\d+)', run, re.M)
    assert boundary, (phase, 'missing AREA01 return boundary')
    end = next(i for i, t in enumerate(ticks) if t['counter'] == int(boundary.group(1)))
    load = next(r['f'] for r in rows if r['bd8'] == 1)
    arrival = next(r['f'] for r in rows if r['area4'] == '01000001' and
                   bytes.fromhex(r['slots'])[8:12] == bytes((3, 1, 0, 0)))
    assert len(rows) - arrival - 1 == RETURN_TICKS[phase], (phase, 'return endpoint moved')
    assert start + load <= end and end + RETURN_TICKS[phase] + 1 < len(ticks), (phase, 'incomplete return')

    def original_key(row):
        b = bytes.fromhex(row['slots'])
        return tuple(b[8:12]), tuple(b[0x48:0x4C]), row['bd8']

    native = [(i, (tuple(bytes.fromhex(ticks[i]['post'])[:4]),) + smoke.exit_loader(ticks, i))
              for i in range(start + load, end + 1)]
    original = [(i, original_key(rows[i])) for i in range(load, arrival + 1)]
    ns, os = smoke.exit_segments(native), smoke.exit_segments(original)
    assert [s[0] for s in ns] == [s[0] for s in os], (phase, 'loader state order differs', ns, os)
    pairs = [(start + k, rows[k]) for k in range(load)]
    waits = []
    for (key, i, n), (_, k, n0) in zip(ns, os):
        assert n <= n0 and (n0 > 1 or n == 1), (phase, 'loader state duration', key, n, n0)
        pairs.extend((i + q, rows[k + q]) for q in range(n))
        if n != n0:
            waits.append(dict(state=key, original=n0, native=n))
    pairs.extend((end + q, rows[arrival + q]) for q in range(1, RETURN_TICKS[phase] + 1))
    return pairs, waits, end, load, len(ns)


def return_pairs(smoke, ticks, run, state, phase, start, rows):
    pairs, waits, end, load, states = return_alignment(smoke, ticks, run, phase, start, rows)
    import test_area_load_reference as alr
    elf = (smoke.DECOMP / 'config/SCUS_971.12').read_bytes()
    chain, d010 = alr.replay_chain(elf, ticks[start:end + 1])
    veil, draws = alr.replay_veil(elf, ticks[start:end + 1])
    assert d010 == 1 and chain >= end - start - load, (phase, 'original loader chain coverage', chain, d010)
    state.setdefault('area00_host_waits', {})[phase] = waits
    print(f'{phase}: loader {states} states; {chain} original chain steps, {veil} veil steps/{draws} draws; '
          f'host wait changes {json.dumps(waits)}')
    return pairs
