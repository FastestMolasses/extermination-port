"""The AREA01 arrival (the whole route's last phase) and the opt-in recorded
AREA01 route, using original code timing.

The rebuild is row 741; rows 742..801 are 60 neutral world frames. This
checks only fields present in that capture and the scene tick log. The main
route checks all 11 recorded owner addresses through AREA00 arrival state 0;
level 3's rebuild and gameplay are outside this endpoint.
"""
import hashlib
import json
import re
import struct
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
    state.setdefault('area01_ends', {})['a01_arrival'] = ticks[state['cursor'] - 1]['counter']
    print('a01_arrival: PASS (route 15 f741..f801: rebuild plus 60 neutral world ticks; '
          'player, ground, camera, selector, request, task, story bytes, message, bars and fade; '
          'rebuild clock alone excepted as in exit; AREA01 route owners not yet certified)')


ROUTE_DIR = CAPTURE.parents[2] / 'route_a01'
MAIN_BEATS = {'a01_00': 'a01_00_train_room', 'a01_01': 'a01_01_tunnel',
              'a01_02': 'a01_02_shaft_landing', 'a01_03': 'a01_03_shaft_locked',
              'a01_04': 'a01_04_return_north', 'a01_05': 'a01_05_npc_bridge_talk',
              'a01_06': 'a01_06_return_south', 'a01_07': 'a01_07_level_exit'}
SIDE_BEATS = {'a01_s0': 'a01_s0_npc_first_talk', 'a01_s1': 'a01_s1_sentry_doc',
              'a01_s2': 'a01_s2_control_room_items', 'a01_s3': 'a01_s3_fire_contact',
              'a01_s4': 'a01_s4_east_room', 'a01_s5': 'a01_s5_duct',
              'a01_s6': 'a01_s6_bridge_blocked', 'a01_s7': 'a01_s7_npc_third_talk'}
# Match the C phase order: a side run branches at its source's completed
# endpoint. The two children of s0 replay that side first, never main 00.
ROUTE_PHASES = ('a01_s0', 'a01_s2', 'a01_s5', 'a01_s3', 'a01_s4', 'a01_s6',
                'a01_00', 'a01_01', 'a01_s1', 'a01_02', 'a01_03', 'a01_04',
                'a01_05', 'a01_s7', 'a01_06', 'a01_07')
FROM_SIDE = {'a01_s2': 'a01_s0', 'a01_s5': 'a01_s0'}
SOURCES = dict(zip(MAIN_BEATS, ('a01_arrival', *tuple(MAIN_BEATS)[:-1]))) | {
    'a01_s0': 'a01_arrival', 'a01_s1': 'a01_01', 'a01_s2': 'a01_s0',
    'a01_s3': 'a01_arrival', 'a01_s4': 'a01_arrival', 'a01_s5': 'a01_s0',
    'a01_s6': 'a01_arrival', 'a01_s7': 'a01_05'}
AREA00_ARRIVAL_ROW = 529


def phase_path(until):
    """AREA01 phases reached from ordinary New Game, including a side's parent."""
    if until == 'a01_arrival':
        return ('a01_arrival',)
    assert until in ROUTE_PHASES, ('unknown AREA01 phase', until)
    before = ROUTE_PHASES[:ROUTE_PHASES.index(until)]
    return ('a01_arrival',) + tuple(p for p in before if p not in SIDE_BEATS or p == FROM_SIDE.get(until)) + (until,)


def capture_path(phase):
    return CAPTURE if phase == 'a01_arrival' else ROUTE_DIR / (MAIN_BEATS | SIDE_BEATS)[phase] / 'trace.json'


def route_capture(phase):
    path = capture_path(phase)
    capture = json.loads(path.read_text())
    assert capture['source'] == capture_path(SOURCES[phase]).parent.name, \
        (phase, 'reference source differs from native branch', capture['source'], SOURCES[phase])
    assert not capture.get('teleports'), (phase, 'reference contains teleports')
    rows = capture['rows']
    assert len(rows) == capture['frames'] + 1
    assert all(row['f'] == i for i, row in enumerate(rows)), (phase, 'noncontiguous reference')
    assert all(row['counter'] == capture['first_counter'] + i for i, row in enumerate(rows)), \
        (phase, 'noncontiguous reference counter')
    assert rows[-1]['counter'] == capture['last_counter']
    if phase == 'a01_07':
        row = rows[AREA00_ARRIVAL_ROW]
        assert row['area4'] == '00000000' and bytes.fromhex(row['slots'])[8:12] == bytes((3,1,0,0)), \
            ('AREA00 arrival boundary moved', row['f'])
        assert bytes.fromhex(rows[AREA00_ARRIVAL_ROW+1]['slots'])[8:12] == bytes((3,1,0,1)), \
            'AREA00 next row must be its first rebuild'
    return capture


def source_gap(phase, capture):
    source = capture_path(SOURCES[phase])
    previous = json.loads(source.read_text())
    # The test driver releases the pad at a completed source. Every captured
    # source is also neutral here; an unrecorded held input must not be lost.
    last = previous['inputs'][-1]
    assert (last['buttons'], last['lx'], last['ly']) == (0, 127, 127), (phase, 'source pad is held', last)
    gap = capture['first_counter'] - previous['last_counter']
    assert 1 <= gap <= 120, (phase, gap)
    return gap


def prepare_pads(out, include_sides=False):
    """Export test input only, to an ignored build directory."""
    out.mkdir(parents=True, exist_ok=True)
    count, manifest = 0, []
    beats = MAIN_BEATS | (SIDE_BEATS if include_sides else {})
    for phase, beat in beats.items():
        capture = route_capture(phase)
        source = capture['source']
        path = capture_path(SOURCES[phase])
        gap = source_gap(phase, capture)
        frames = AREA00_ARRIVAL_ROW if phase == 'a01_07' else capture['frames']
        lines = [f"EMA1 1 {frames} {gap}"]
        last = -1
        for row in capture['inputs']:
            # The recorder can issue several commands before another frame
            # advances (e.g. a01_00 f34). Preserve their order; the driver's
            # existing last-command-at-or-before-frame rule selects the last.
            assert last <= row['f'] <= frames and row['f'] >= 0, (phase, 'input order/range', row)
            assert 0 <= row['buttons'] <= 65535 and 0 <= row['lx'] <= 255 and 0 <= row['ly'] <= 255
            last = row['f']
            lines.append(f"{row['f']} {row['buttons']:x} {row['lx']} {row['ly']}")
        assert 0 < len(capture['inputs']) <= 2048 and 1 <= frames <= 20000
        (out / f'{phase}.pad').write_text('\n'.join(lines) + '\n')
        count += len(capture['inputs'])
        manifest.append(dict(phase=phase, capture=beat, source=source, gap=gap,
            capture_sha256=hashlib.sha256((ROUTE_DIR/beat/'trace.json').read_bytes()).hexdigest(),
            source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
            first_counter=capture['first_counter'], last_counter=capture['last_counter'],
            capture_frames=capture['frames'], endpoint_row=frames, pad_changes=len(capture['inputs']),
            native_phase=True, native_source=SOURCES[phase], native_path=phase_path(phase), command_delay_rows=2,
            same_frame_commands='preserved in recorded order; last command applies',
            endpoint='AREA00 arrival state 0 before rebuild' if phase=='a01_07' else 'capture end'))
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return count


def route_start(ticks, run, state, phase, capture):
    match = re.search(rf'^level smoke: {phase}: aligned counter=(\d+)', run, re.M)
    assert match, (phase, 'no row-0 alignment')
    counter = int(match.group(1))
    source = SOURCES[phase]
    source_counter = state.get('area01_ends', {}).get(source)
    assert source_counter is not None, (phase, 'source phase was not checked', source)
    gap = source_gap(phase, capture)
    assert counter == source_counter + gap, (phase, 'wrong source/counter gap', source, source_counter, counter, gap)
    start = next(i for i, tick in enumerate(ticks) if tick['counter'] == counter)
    assert start == state['cursor'] + gap - 1, (phase, 'source gap skipped a native tick')
    assert all(ticks[i]['counter'] == source_counter + i - state['cursor'] + 1
               for i in range(state['cursor'], start)), (phase, 'noncontiguous source gap')
    return start, counter


def check_route(smoke, ticks, run, state, phase):
    if phase == 'a01_07':
        return check_exit_boundary(smoke, ticks, run, state)
    capture = route_capture(phase)
    rows = capture['rows']
    start, counter = route_start(ticks, run, state, phase, capture)
    assert start + len(rows) < len(ticks), (phase, 'missing post-frame tail')
    for offset, row in enumerate(rows):
        i = start + offset
        assert ticks[i]['counter'] == counter + offset, (phase, offset, 'skipped main-loop tick')
        compare_route_row(smoke,ticks,i,row,phase)
    state['cursor'] = start + len(rows)
    state['area01_ends'][phase] = ticks[state['cursor'] - 1]['counter']
    print(f'{phase}: PASS ({len(rows)} consecutive captured rows: player, collision, camera, '
          'requests, task state, message, letterbox, fade, 11 owners, health and progress)')


def check_rng_checkpoints(state, frames):
    """Audit the native LCG chain; report original snapshot equality separately.

    R.port has already resolved *all* native calls, including AREA01, and
    fails on an unmapped caller. Original AREA01 captures contain no per-call
    stream. Their endpoint RAM supplies one state word, not an order oracle.
    The documented first-level value-dependent divergence can carry into
    these endpoints, so a difference is reported and never called parity.
    """
    ends = state.get('area01_ends', {})
    if not ends:
        return
    checkpoints = sorted((counter, phase) for phase, counter in ends.items())
    native, calls, index, reports = 1, 0, 0, []
    ordered = list(frames.items())
    assert [c for c, _ in ordered] == sorted(c for c, _ in ordered), 'RNG trace counters moved backward'
    for counter, phase in checkpoints:
        while index < len(ordered) and ordered[index][0] <= counter:
            c, draws = ordered[index]
            for _, before, *_ in draws:
                assert before == native, ('AREA01 native RNG chain broken', phase, c, hex(before), hex(native))
                native = (native * 0x41C64E6D + 0x3039) & 0xFFFFFFFF
                calls += 1
            index += 1
        path = capture_path(phase)
        capture = json.loads(path.read_text())
        snapshot = json.loads(path.with_name('snapshot.json').read_text())
        assert snapshot['main_loop_counter'] == capture['last_counter'], (phase, 'RNG snapshot counter differs')
        with path.with_name('eeMemory.bin').open('rb') as memory:
            memory.seek(0x24295C)
            pointer, = struct.unpack('<I', memory.read(4))
            assert pointer == 0x242670, (phase, 'original SDK RNG pointer moved', hex(pointer))
            memory.seek(pointer + 0x58)
            original, = struct.unpack('<I', memory.read(4))
        reports.append(dict(phase=phase, native_counter=counter, original_counter=capture['last_counter'],
                            native_state=native, original_state=original, equal=native == original))
    state['area01_rng_checkpoints'] = reports
    print(f'AREA01 RNG: PASS native LCG continuity through {calls} mapped calls to the last checked '
          'endpoint; original per-call AREA01 capture unavailable')
    for row in reports:
        print(f'AREA01 RNG checkpoint {row["phase"]}: native {row["native_state"]:08X}, '
              f'original {row["original_state"]:08X}, {"equal" if row["equal"] else "DIFFERENT"} '
              '(diagnostic only; no capture seed installed)')
    if 'area00_arrival' in state:
        print('AREA01 RNG checkpoint a01_07: unavailable at row 529; its saved RAM is after level-3 gameplay')


def compare_route_row(smoke,ticks,i,row,phase):
    """The same strict fields for every gameplay and aligned loader tick."""
    tick, after = ticks[i], ticks[i + 1]
    p, o = smoke.port_view(ticks, i), smoke.orig_view(row)
    keys = ('spad', 'screen', 'msg', 'power', 'pos', 'yaw', 'eye', 'tgt')
    got, want = {k: p[k] for k in keys}, {k: o[k] for k in keys}
    b, player = bytes.fromhex(tick['post']), tick['player']
    got.update(cam=tick['cam4'], req=b[smoke.tsr.OFFSET[0x8106B0]:smoke.tsr.OFFSET[0x8106B0]+10].hex(),
               area=b[smoke.tsr.OFFSET[0x810700]:smoke.tsr.OFFSET[0x810700]+3].hex(),
               slot0=tuple(b[:5]), player=tuple(player[k] for k in (0,1,2,3,6)),
               ground=hex(player[5]), clock=round(smoke.f32(player[4]),3), fade=after['fade8'])
    want.update(cam=row['cam_mode'], req=row['req'], area=row['area4'][:6],
                slot0=tuple(bytes.fromhex(row['slots'])[8:13]),
                player=(row['p5'],row['m1F0'],row['m1F1'],row['clip'],row['b2F3']),
                ground=row['ground'],clock=row['clock'],fade=row['fade'][:16])
    observed = tick.get('a01')
    assert observed, (phase, row['f'], 'missing AREA01 owner observations')
    got['hp'],want['hp'] = round(smoke.f32(observed['hp']), 5),row['hp']
    got['progress'],want['progress'] = observed['progress'],[row['d2'],row['story758'],row['taken'],row['docs']]
    for name,address in OWNERS.items():
        values = observed['owners'].get(str(address))
        assert values is not None, (phase, row['f'], name, 'unavailable canonical owner')
        rawpos = [round(v, 5) for v in struct.unpack('<3f', bytes.fromhex(values[1]))]
        got[name] = [values[0], rawpos, values[2], values[3], values[4]]
        owner = row[name]
        want[name] = [owner['h'], owner['pos'], owner['s1F0'], owner['t2DC'], int(owner['cb'], 16)]
    different = {k: (got[k], want[k]) for k in got if got[k] != want[k]}
    assert not different, (phase, f'row {row["f"]}', f'tick {tick["tick"]}', different)


def check_exit_boundary(smoke,ticks,run,state):
    """Same asynchronous load alignment as the established first-level exit.

    Every gameplay tick remains consecutive and exact. Only the hold counts
    of identical loader states may shrink; every emitted native loader tick
    and the final pre-rebuild arrival are checked, then chain/veil instructions
    replay every recorded call. No level-3 rebuild tick is requested.
    """
    capture=route_capture('a01_07')
    rows=capture['rows']
    start,_=route_start(ticks,run,state,'a01_07',capture)
    boundary=re.search(r'^level smoke: a01_07: boundary counter=(\d+) \(capture row 529;',run,re.M)
    assert boundary,'AREA01 exit lacks arrival alignment'
    end=next(i for i,t in enumerate(ticks) if t['counter']==int(boundary.group(1)))
    assert start>=state['cursor'] and end==len(ticks)-1,('AREA01 exit did not stop at arrival',start,end,len(ticks))
    assert all(ticks[i]['counter']==ticks[start]['counter']+i-start for i in range(start,end+1)), \
        'AREA01 exit skipped a native main-loop tick'
    tails=[t for t in state.get('tails',[]) if t['counter']==ticks[end]['counter']]
    assert len(tails)==1 and 'loader_pre' in tails[0],'AREA00 arrival lacks one actual post-frame loader tail'
    tail=tails[0]
    observed=ticks+[dict(msg_pre=tail['msg'],fade8=tail['fade8'],loader_pre=tail['loader_pre'])]
    load=next(r['f'] for r in rows if r['bd8']==1)
    assert load==234 and start+load<=end,'AREA01 exit load boundary moved'
    for k in range(load):compare_route_row(smoke,observed,start+k,rows[k],'a01_07')
    def original_key(row):
        b=bytes.fromhex(row['slots']);return tuple(b[8:12]),tuple(b[0x48:0x4C]),row['bd8']
    port_states=[(i,(tuple(bytes.fromhex(ticks[i]['post'])[:4]),)+smoke.exit_loader(observed,i))
                 for i in range(start+load,end+1)]
    orig_states=[(k,original_key(rows[k])) for k in range(load,AREA00_ARRIVAL_ROW+1)]
    ps,os_=smoke.exit_segments(port_states),smoke.exit_segments(orig_states)
    assert [x[0] for x in ps]==[x[0] for x in os_],('AREA01 exit loader state order',ps,os_)
    waits=[]
    for (key,i,n),(_,k,n0) in zip(ps,os_):
        assert n<=n0 and (n0>1 or n==1),('AREA01 exit loader state duration',key,n,n0)
        if n!=n0:waits.append(dict(state=key,original=n0,native=n,host_wait_rows_skipped=n0-n))
        for q in range(n):compare_route_row(smoke,observed,i+q,rows[k+q],'a01_07')
    assert port_states[-1][1]==original_key(rows[AREA00_ARRIVAL_ROW])
    import test_area_load_reference as alr
    elf=(smoke.DECOMP/'config/SCUS_971.12').read_bytes()
    chain,d010=alr.replay_chain(elf,ticks[start:end+1])
    veil,draws=alr.replay_veil(elf,ticks[start:end+1])
    assert d010==1 and chain>=end-(start+load),('AREA01 exit original chain coverage',chain,d010)
    state['cursor']=end+1
    state['area00_arrival']=end
    state['area01_host_waits']=waits
    print(f'a01_07: PASS ({load} consecutive gameplay/exit rows; {len(ps)} loader states, '
          f'{end-start-load+1} native load/arrival ticks; {chain} original chain ticks, '
          f'{veil} veil steps/{draws} draws; all route fields and 11 owners; '
          f'host wait counts only: {json.dumps(waits)}; capture row529 AREA00 arrival state0, '
          'before the out-of-scope level3 rebuild)')


OWNERS = dict(npc_r36=0x7B0390, r37_826CF0=0x7B0680, shaft_door_r12=0x7ABD10,
              r13_158D30=0x7AC000, door_r15=0x7AC5E0, doc_g0_1=0x7A5930,
              n7A70B0_826D40=0x7A70B0, n7A7690_826D40=0x7A7690,
              n7A7C70_826D40=0x7A7C70, r41_8261A0=0x7B1240, r42_8261A0=0x7B1530)
