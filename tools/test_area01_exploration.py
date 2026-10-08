#!/usr/bin/env python3
"""Opt-in, headless AREA01 exploration from the ordinary New Game route.

Each case starts a fresh native process, scripts only pad input, and retains
the first fault and observed coverage. A completed input script is not a
claim of gameplay parity or complete coverage. No emulator is launched.
Examples: --list; --case water; --case status --case aim-fire; --all.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time

from level_smoke_area01 import (MAIN_BEATS, SIDE_BEATS, phase_path, prepare_pads, route_capture)

ROOT = Path(__file__).resolve().parents[1]
PHASE = 'a01_s3'  # An existing side slot whose source is AREA01 arrival.
FAULT = re.compile(r'\bfault(?:ed)?\b|unbound worker|not translated|no translation|does not hold|failed at (?:frame|[0-9a-f]{8})|level smoke: FAIL|AREA01 explore: BLOCKED', re.I)


def move(x, z, tolerance=2, magnitude=1, limit=900):
    return f'move {x} {z} {tolerance} {magnitude} {limit}'


def near(x, z, tolerance=1, magnitude=.4, limit=150):
    """As move, but geometry in the way ends the step instead of the run."""
    return f'near {x} {z} {tolerance} {magnitude} {limit}'


def hold(frames, buttons=0, lx=128, ly=128):
    return f'hold {frames} {buttons:x} {lx} {ly}'


def face(yaw):
    return f'face {yaw} 240'


def mark(name):
    return f'mark {name}'


# Author-authored navigation inputs. Unlike a capture replay these probes
# adapt stick direction to the current native camera. Coordinates are
# navigation targets, never values written into the player.
EAST_BYPASS = [move(x, z) for x, z in (
    (33, -571), (26, -594), (20, -617), (30, -650), (33, -672),
    (33, -704), (1, -699), (1, -730), (18, -733))] + [
    near(17, -753, 2, 1, 300), near(2, -790, 2, 1, 300), near(-13, -821, 2, 1, 300)]
TUNNEL = [move(x, z) for x, z in (
    (-26, -848), (-14, -856), (-14, -885), (14, -905), (15, -970), (5, -986))]
WATER = EAST_BYPASS + [mark('tunnel-mouth')] + TUNNEL + [
    mark('water-north-edge'), move(5, -1004), hold(90), mark('water-interior'),
    move(-20, -1004), hold(90), mark('water-west'), move(24, -1004), hold(90),
    mark('water-east'), move(5, -1032), hold(90), mark('water-exit')]
# Past crate 10 and between the fires to the attribute-0x32 ladder face
# (z -719): Use six units from it, the stick up through the climb, then
# release on the y 25 ledge (its pickup lies three units ahead).
WEST_LADDER = [move(x, z) for x, z in (
    (33, -571), (10, -597), (-18, -612), (-25, -635), (-28, -660), (-42, -690),
    (-58, -702))] + [move(-70, -704, .5, .5), near(-70, -713, .5, .4, 200), hold(30),
    face(3.14159265), hold(20), mark('ladder-foot'), hold(2, 0x4000), hold(30),
    hold(175, 0, 128, 0), hold(40), mark('ladder-top')]
LEDGE = WEST_LADDER + [face(3.14159265), hold(10), hold(2, 0x4000), hold(200),
                       mark('ledge-use'), hold(2, 0x1000), hold(90), mark('ledge-pickup')]
WATER_WEST = LEDGE + [near(x, z, 2, 1, 400) for x, z in (
    (-52, -762), (-42, -800), (-40, -860), (-40, -920))] + [
    mark('west-walkway'), near(-10, -920, 2, 1, 300), hold(60), mark('dropped-to-tunnel'),
    near(9.6, -977.7, 2, 1, 600), near(5, -1004, 2, 1, 300), hold(90),
    mark('water-from-west-walkway'), near(5, -1032, 2, 1, 300), hold(60)]
# From the tunnel mouth (the end of the recorded a01_00): the recorded
# tunnel line, the water's edges, the lower tunnel, the shaft-landing
# stairs (AREA01's one surface-0x35 polygon), the locked shaft door's Use
# (its door program's message request), and back down.
SHAFT = [move(x, z) for x, z in (
    (-24.8, -835.6), (-20.5, -856.9), (-10.1, -868), (-11.1, -890.5), (9.5, -902.7),
    (15.5, -922.5), (13.3, -946.3), (15.1, -970.2), (9.6, -977.7))] + [
    mark('water-edge'), move(5, -1000), hold(30), near(-22, -1000, 3, 1, 300), hold(30),
    near(22, -1000, 3, 1, 300), hold(30), near(-22, -1014, 3, 1, 300), near(22, -1014, 3, 1, 300),
    hold(20), hold(2, 0x2000), hold(40), hold(2, 0x8000), hold(60), hold(40, 0x800),
    hold(2, 0x2800), hold(30, 0x800), hold(30), mark('water-done')] + [
    move(x, z) for x, z in ((5, -1030), (5, -1100), (4.1, -1163.8), (-15.6, -1177.3),
                            (-25.5, -1194.2), (-29, -1213))] + [
    mark('stairs'), move(-40.4, -1226.4), move(-39.2, -1250.1), move(-40.5, -1271.5, 1, .5),
    mark('landing'), hold(20), face(2.35619449), hold(20), hold(2, 0x4000), hold(400),
    hold(2, 0x4000), hold(600), mark('shaft-door'), move(-39.2, -1250.1), move(-40.4, -1226.4),
    move(-29, -1213), near(-5, -1213, 3, 1, 300), hold(30), mark('lower-tunnel')]
# Aim and fire at the crate stack: the shots meet the floor fields'
# boxes (001A8DA0 / 001A8CE0).
STACK_SHOTS = [move(33, -571), move(26, -594), move(20, -617), move(25, -660, 1, .5), hold(20),
               face(-2.96834), hold(20), hold(50, 0x800)] + \
    [step for _ in range(10) for step in (hold(2, 0x2800), hold(25, 0x800))] + [
    hold(30), face(-2.37271), hold(20), hold(50, 0x800)] + \
    [step for _ in range(10) for step in (hold(2, 0x2800), hold(25, 0x800))] + [
    hold(60), mark('stack-shots')]
# Use at the crate stack's north face, hang, pull up (as a01_00 does),
# Use toward the stack's pickup, then step off its south side.
STACK_CLIMB = [move(33, -571), move(26, -594), move(20, -617), move(30, -650), move(33, -672),
               move(25, -700), near(15.82, -706.4, .5, .4, 300), hold(10), face(3.14159265),
               hold(20), mark('stack-use'), hold(2, 0x4000), hold(54), hold(110, 0, 128, 0),
               hold(30), mark('stack-top'), hold(30), face(3.14159265), hold(20), hold(2, 0x4000),
               hold(150), hold(2, 0x1000), hold(90), mark('stack-pickup'), near(17, -745, 2, 1, 300),
               hold(60), mark('stack-off')]
# Hang on the crate stack's north face, shimmy left then right along it
# (the hang's side probes 001784E0 / 0017E6E0 / 0017F130 and its sound
# 00182AF0), pull up, then walk back off the north edge into a hang.
SHIMMY = [move(33, -571), move(26, -594), move(20, -617), move(30, -650), move(33, -672),
          move(25, -700), near(15.82, -706.4, .5, .4, 300), hold(10), face(3.14159265), hold(20),
          mark('stack-use'), hold(2, 0x4000), hold(60), mark('hanging'), hold(60, 0, 0, 128),
          hold(20), hold(60, 0, 255, 128), hold(20), mark('shimmied'), hold(110, 0, 128, 0), hold(30),
          mark('stack-top'), hold(30), face(0), hold(20), near(15.8, -700, 1, .5, 200), hold(90),
          mark('edge-hang'), hold(30)]
# The knife at crates 10 and 11 from their open sides.
CRATES = [move(33, -571), move(10, -597), move(-18, -612), move(-28, -641.5, 1, .5),
          near(-36, -641.5), hold(20), face(-1.5707963), hold(20), mark('crate10')] + \
    [step for _ in range(4) for step in (hold(2, 0x2000), hold(45), hold(2, 0x8000), hold(70))] + [
    mark('crate10-done')] + [move(x, z) for x, z in (
    (-18, -612), (10, -597), (33, -571), (26, -594), (20, -617), (30, -650), (33, -672),
    (33, -704), (1, -699), (1, -730), (18, -733))] + [move(19, -759, 1, .5), near(22, -764),
    hold(20), face(3.14159265), hold(20), mark('crate11')] + \
    [step for _ in range(4) for step in (hold(2, 0x2000), hold(45), hold(2, 0x8000), hold(70))] + [
    mark('crate11-done'), hold(120)]

CASES = {
    # Deep water (depth 2) is not claimed: AREA01's one surface-0x5B polygon
    # (x -30..32, z -1021.5..-980, y -27.923) gave depth 1 across it in every
    # run (the floor under it lies about four units down).
    'water': dict(script=WATER, claims=['shallow-water', 'slopes'],
                  description='ground bypass around crates, tunnel water entry, crossing and exit'),
    'water-west': dict(script=WATER_WEST, claims=['ladders', 'west-ledge', 'shallow-water', 'pickup'],
                       description='west ladder and walkway, then approach water from the west ledge'),
    'ladder': dict(script=WEST_LADDER, claims=['ladders', 'west-ledge'],
                   description='walk west of the train, approach the ladder and press Use/up'),
    'ledge': dict(script=LEDGE, claims=['ladders', 'west-ledge', 'pickup'],
                  description='climb the west ladder and take the ledge pickup'),
    'shaft': dict(phase='a01_01', script=SHAFT,
                  claims=['shallow-water', 'melee', 'aim', 'fire', 'stairs', 'locked-shaft-door'],
                  description='tunnel, water edges, lower tunnel, landing stairs, locked shaft door Use'),
    'stack-shots': dict(script=STACK_SHOTS, claims=['aim', 'fire'],
                        description='aim and fire twenty shots at the crate stack'),
    'stack-climb': dict(script=STACK_CLIMB, claims=['ledge-hang', 'pull-up'],
                        description='hang and pull up onto the crate stack, Use on top, step off'),
    'shimmy': dict(script=SHIMMY, claims=['ledge-hang', 'pull-up'],
                   description='hang on the crate stack, shimmy both ways, pull up, back into a hang'),
    'crates': dict(script=CRATES, claims=['melee'],
                   description='knife crates 10 and 11 from their open sides'),
    'vent': dict(phase='a01_s5', script=[move(90, -540, 1.2), move(120, -535, 1.2),
                move(130, -530, 1.2), hold(30), move(133, -529.5, .35, .4), hold(40),
                face(1.5707963), hold(30), mark('vent-use'), hold(2, 0x4000),
                hold(330), mark('vent-entry-end')], recorded_tail=('a01_s5', 376),
                claims=['vent-crawl', 'duct-pickup'],
                description='closed-loop duct entry, then retained crawl/pickup/exit inputs'),
    'status': dict(script=[hold(30), hold(2, 0x8), hold(100), mark('status-open-input'),
                           hold(2, 0x8), hold(100), mark('status-close-input')],
                   claims=['status'], description='open and close the status hub in AREA01'),
    'aim-fire': dict(script=[hold(30), face(3.14159265), hold(30),
                             hold(80, 0x800), hold(2, 0x2800), hold(60, 0x800),
                             hold(2, 0x2800), hold(90), mark('aim-fire-input-end')],
                     claims=['aim', 'fire'], description='R1 aim and two Circle trigger presses'),
    'melee': dict(script=[hold(30), hold(2, 0x2000), hold(90),
                          hold(2, 0x8000), hold(120), mark('melee-input-end')],
                  claims=['melee'], description='unarmed light and heavy knife inputs'),
    'control-door': dict(script=[move(54.5, -563.4, .35, .4), hold(40),
                                 face(1.59978), hold(30), mark('control-door-use'),
                                 hold(2, 0x4000), hold(240), mark('control-door-end')],
                         claims=['control-room'],
                         description='closed-loop approach to the captured control-door Use stance'),
    'east-door': dict(script=[move(46.5, -578), move(65.4, -599.7), move(96.2, -606.8),
                              move(122.5, -609.88, .35, .4), hold(40),
                              face(1.54245), hold(30), mark('east-door-use'),
                              hold(2, 0x4000), hold(240), mark('east-door-end')],
                      claims=['east-room'],
                      description='closed-loop approach to the captured east-door Use stance'),
}

RECORDED_CLAIMS = {
    'a01_00': ['crates', 'ledge-hang', 'pull-up', 'fall-land'],
    'a01_01': ['tunnel', 'slopes'],
    'a01_02': ['shallow-water', 'slopes', 'shaft-landing'],
    'a01_03': ['locked-shaft-door'], 'a01_04': ['control-room'],
    'a01_05': ['npc-second-talk'], 'a01_06': ['control-room-return'],
    'a01_07': ['area-exit'], 'a01_s0': ['control-room', 'npc-first-talk'],
    'a01_s1': ['database-pickup'], 'a01_s2': ['control-room-pickups'],
    'a01_s3': ['fire-damage'], 'a01_s4': ['east-room', 'save-terminal-decline'],
    'a01_s5': ['vent-crawl', 'duct-pickup'],
    'a01_s6': ['raised-bridge-boundary'], 'a01_s7': ['npc-third-talk'],
}
for _phase, _beat in (MAIN_BEATS | SIDE_BEATS).items():
    CASES[_phase] = dict(phase=_phase, claims=RECORDED_CLAIMS[_phase], description=_beat)

UNREACHED = {
    'north-room': 'SECOND_LEVEL_ROUTE.md section 9.3: raised bridges on the first visit; boundary probe only',
    'upper-floor': 'SECOND_LEVEL_ROUTE.md section 9.4: no ground route established; ladder reaches y25, not y60',
    'enemy-damage': 'No retained first-visit enemy-attack route; fire damage is tested separately',
    'all-shot-surfaces-and-objects': 'Aim/fire probe covers its actual path only; exhaustive impact coverage is not established',
}
OBSERVABLE_CLAIMS = {'shallow-water', 'deep-water', 'status', 'fire-damage',
                     'ledge-hang', 'pull-up', 'fall-land', 'vent-crawl',
                     'melee', 'aim', 'fire', 'ladders', 'control-room', 'east-room', 'pickup'}


def f32(bits):
    return struct.unpack('<f', struct.pack('<I', bits))[0]


def read_observations(path, start):
    observations = dict(ticks=0, states=[], actions=[], clips=[], depths=[], surfaces=[],
                        entries=[], min_health=None, max_y=None, last=None,
                        status_open=False, progress_changed=False, magazine_start=None,
                        magazine_min=None, shot_nodes=False)
    states, actions, clips, depths, surfaces, entries = (set() for _ in range(6))
    initial_progress = None
    if start is None or not path.exists():
        return observations
    opener = gzip.open if path.suffix == '.gz' else open
    with opener(path, 'rt') as stream:
        for line in stream:
            row = json.loads(line)
            if 'tick' not in row or row.get('counter', 0) < start:
                continue
            observations['ticks'] += 1
            player = row.get('player', [])
            if len(player) > 3:
                states.add(player[0]); actions.add(player[1]); clips.add(player[3])
            water = row.get('water')
            if water:
                surfaces.add(water[0]); depths.add(water[1])
            pos = [f32(v) for v in row.get('pos_post', [])]
            if pos:
                observations['max_y'] = pos[1] if observations['max_y'] is None else max(pos[1], observations['max_y'])
            hp = row.get('a01', {}).get('hp')
            if hp is not None:
                hp = f32(hp)
                observations['min_health'] = hp if observations['min_health'] is None else min(hp, observations['min_health'])
            # log_snapshot follows test_scene_task_reference.LAYOUT: task
            # +8.. (24 bytes), request (72), then area/sub/entry (3).
            # 001AE040's +B values 3/5 are the status-screen states.
            post = bytes.fromhex(row.get('post', ''))
            if len(post) >= 99:
                observations['status_open'] |= post[3] in (3, 5)
                if post[96] == 1: entries.add(post[98])
            fire = row.get('fire')
            if fire:
                if observations['magazine_start'] is None: observations['magazine_start'] = fire[1]
                observations['magazine_min'] = fire[1] if observations['magazine_min'] is None else min(fire[1], observations['magazine_min'])
                observations['shot_nodes'] |= any(n[2] in (0x0018ABA0, 0x001F5040) for n in fire[6])
            progress = row.get('a01', {}).get('progress')
            if progress is not None:
                if initial_progress is None: initial_progress = progress
                observations['progress_changed'] |= progress != initial_progress
            observations['last'] = dict(counter=row.get('counter'), pos=pos, player=player,
                                        water=water, hp=hp, task=list(post[:5]),
                                        area=list(post[96:99]) if len(post) >= 99 else [])
    observations.update(states=sorted(states), actions=sorted(actions), clips=sorted(clips), depths=sorted(depths),
                        surfaces=sorted(surfaces), entries=sorted(entries))
    return observations


def coverage(claims, observed, complete):
    """Observation gates deliberately do not turn issued input into evidence."""
    actions = observed['actions']
    states = observed['states']
    gates = {
        'shallow-water': 1 in observed['depths'], 'deep-water': 2 in observed['depths'],
        'status': observed['status_open'], 'fire-damage': observed['min_health'] is not None and observed['min_health'] < 100,
        'ledge-hang': 0x10 in actions, 'pull-up': 0x11 in actions,
        'fall-land': 0x0B in actions and 0x0F in actions,
        'vent-crawl': 0x2D in actions, 'melee': 0x21 in states or 0x22 in states,
        'aim': 0x1D in states or 0x1E in states,
        'ladders': any(a in actions for a in (0x15, 0x16, 0x17)),
        'fire': observed['shot_nodes'] and observed['magazine_min'] < observed['magazine_start'],
        'control-room': 1 in observed['entries'], 'east-room': 8 in observed['entries'],
        'pickup': observed['progress_changed'],
    }
    return {claim: ('OBSERVED' if gates.get(claim, False) else
                    'INPUT-COMPLETED-UNVERIFIED' if complete else 'BLOCKED-OR-NOT-OBSERVED')
            for claim in claims}


def analyze(run, ticks, case, returncode, timed_out=False):
    phase = case.get('phase', PHASE)
    pattern = (r'AREA01 explore: BEGIN phase=\S+ counter=(\d+)' if 'script' in case else
               rf'^level smoke: {phase}: aligned counter=(\d+)')
    match = re.search(pattern, run, re.M)
    start = int(match.group(1)) if match else None
    arrival = re.search(r'^level smoke: a01_arrival: aligned counter=(\d+)', run, re.M)
    observation_start = start if start is not None else int(arrival.group(1)) if arrival else None
    first = next((line for line in run.splitlines() if FAULT.search(line)), None)
    complete = (returncode == 0 and not timed_out and first is None and start is not None and
                (('AREA01 explore: COMPLETE' in run) if 'script' in case else
                 bool(re.search(rf'^level smoke: {phase}: PASS', run, re.M))))
    observed = read_observations(ticks, observation_start)
    status = 'INPUT-COMPLETED' if complete else 'TIMEOUT' if timed_out else 'FAULT' if first else 'INCOMPLETE'
    if first and ('exploration input did not reach' in first or 'AREA01 explore: BLOCKED' in first):
        status = 'ROUTE-BLOCKED'
    covered = coverage(case['claims'], observed, complete)
    if complete and any(covered[claim] != 'OBSERVED' for claim in set(case['claims']) & OBSERVABLE_CLAIMS):
        status = 'TARGET-NOT-OBSERVED'
    stopped = re.search(r'^level smoke: FAIL phase=(\S+)', run, re.M)
    return dict(status=status, input_completed=complete, requested_phase=phase, requested_phase_entered=start is not None,
                stopped_phase=stopped.group(1) if stopped else None,
                start_counter=start, observation_start=observation_start,
                observations_scope='requested phase' if start is not None else 'AREA01 prerequisite prefix',
                first_fault=first, observations=observed,
                coverage=covered,
                markers=re.findall(r'^AREA01 explore: MARK (.+)$', run, re.M),
                original_parity='NOT-CHECKED: run the existing AREA01 smoke checker for recorded input')


def recorded_prefix(run, path, phase):
    """Reuse the untouched strict checker; a matching prefix is not a PASS."""
    import level_smoke_area01 as a01
    import test_level_smoke as smoke
    match = re.search(rf'^level smoke: {phase}: aligned counter=(\d+)', run, re.M)
    if not match or not path.exists() or phase == 'a01_07':
        return dict(status='NOT-CHECKED', reason='phase not entered, log missing, or area-exit special case')
    counter = int(match.group(1))
    rows = a01.route_capture(phase)['rows']
    previous, exact, first_difference = None, 0, None
    opener = gzip.open if path.suffix == '.gz' else open
    with opener(path, 'rt') as stream:
        for line in stream:
            tick = json.loads(line)
            if 'tick' not in tick or tick.get('counter', 0) < counter:
                continue
            if previous is not None:
                try:
                    assert previous['counter'] == counter + exact, 'noncontiguous phase ticks'
                    a01.compare_route_row(smoke, [previous, tick], 0, rows[exact], phase)
                except AssertionError as error:
                    first_difference = str(error)
                    break
                exact += 1
                if exact == len(rows):
                    break
            previous = tick
    return dict(status='PREFIX-DIAGNOSTIC', exact_rows=exact, captured_rows=len(rows),
                first_difference=first_difference,
                limit='Existing row fields only; this does not check the whole camera tail, RNG, or prior phases.')


def case_script(case):
    """Resolve pad-only capture tails at runtime; no captured state is embedded."""
    script = list(case['script'])
    if 'recorded_tail' not in case:
        return script
    phase, start = case['recorded_tail']
    capture = route_capture(phase)
    commands = [row for row in capture['inputs'] if row['f'] >= start]
    assert commands and commands[0]['f'] == start, (phase, 'missing tail start', start)
    script.append(mark('vent-tail-start'))
    for i, command in enumerate(commands):
        end = commands[i+1]['f'] if i+1 < len(commands) else capture['frames']
        assert end >= command['f'], (phase, 'unordered tail inputs')
        if end > command['f']:
            script.append(hold(end-command['f'], command['buttons'], command['lx'], command['ly']))
    script.append(mark('vent-recorded-tail-end'))
    return script


def run_case(name, case, out, binary, timeout, keep_ticks=False):
    out.mkdir(parents=True, exist_ok=True)
    pads = out / 'pads'
    prepare_pads(pads, True)
    phase = case.get('phase', PHASE)
    env = dict(os.environ, EM_HEADLESS='1', EM_UNCAPPED='1', EM_STARTUP_TEST='newgame-level',
               EM_LEVEL_SMOKE_UNTIL=phase, EM_LEVEL2_PAD_DIR=str(pads),
               EM_AREA_CHANGE_LOG=str(out / 'ticks.jsonl'), EM_RAND_TRACE=str(out / 'rand.trace'))
    env.pop('EM_AREA01_EXPLORE_SCRIPT', None)
    env.pop('EM_AREA01_EXPLORE_PHASE', None)
    env.pop('EM_TEST_FULL', None)
    if 'script' in case:
        script = out / 'exploration.input'
        script.write_text('EMAX 1\n' + '\n'.join(case_script(case)) + '\n')
        env.update(EM_AREA01_EXPLORE_SCRIPT=str(script), EM_AREA01_EXPLORE_PHASE=phase)
    print(f'{name}: running {" -> ".join(phase_path(phase))}', flush=True)
    binary_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
    started, timed_out, returncode = time.monotonic(), False, None
    with (out / 'run.log').open('w') as log:
        try:
            result = subprocess.run([str(binary)], cwd=ROOT, env=env, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=timeout)
            returncode = result.returncode
        except subprocess.TimeoutExpired:
            timed_out = True  # subprocess.run kills and waits for the child.
    report = analyze((out / 'run.log').read_text(errors='replace'), out / 'ticks.jsonl', case, returncode, timed_out)
    if 'script' not in case:
        report['recorded_prefix'] = recorded_prefix((out / 'run.log').read_text(errors='replace'),
                                                    out / 'ticks.jsonl', phase)
    ticks = out / 'ticks.jsonl'
    if ticks.exists() and not keep_ticks:
        compressed = ticks.with_suffix('.jsonl.gz')
        with ticks.open('rb') as source, gzip.open(compressed, 'wb', compresslevel=1) as destination:
            shutil.copyfileobj(source, destination)
        ticks.unlink()
        ticks = compressed
    report.update(case=name, description=case['description'], returncode=returncode,
                  seconds=round(time.monotonic() - started, 3), binary=str(binary),
                  binary_sha256=binary_hash,
                  log=str(out / 'run.log'), path=list(phase_path(phase)),
                  ticks=str(ticks),
                  pad_manifest=str(pads / 'manifest.json'))
    if 'script' in case:
        report.update(input_script=str(script), input_sha256=hashlib.sha256(script.read_bytes()).hexdigest())
    if 'recorded_tail' in case:
        report['recorded_input_tail'] = dict(phase=case['recorded_tail'][0], first_frame=case['recorded_tail'][1],
                                            scope='Pad inputs only, following authored navigation; no capture parity claim')
    (out / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{name}: {report["status"]}; first fault: {report["first_fault"] or "none logged"}', flush=True)
    print(f'  coverage: {json.dumps(report["coverage"], sort_keys=True)}', flush=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--list', action='store_true')
    parser.add_argument('--all', action='store_true', help='all probes and all retained route beats (many fresh boots)')
    parser.add_argument('--case', action='append', choices=CASES, default=[])
    parser.add_argument('--script', type=Path, help='replace a single selected synthetic case with an EMAX input script')
    parser.add_argument('--bin', type=Path, default=ROOT / 'build/extermination')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/level2-crashes/exploration')
    parser.add_argument('--timeout', type=int, default=1800)
    parser.add_argument('--keep-ticks', action='store_true', help='retain large uncompressed tick logs; otherwise gzip them')
    args = parser.parse_args()
    if args.list:
        for name, case in CASES.items():
            print(f'{name:12s} {case["description"]} [{", ".join(case["claims"])}]')
        for name, reason in UNREACHED.items():
            print(f'{name:12s} NOT-COVERED: {reason}')
        return 0
    names = list(CASES) if args.all else list(dict.fromkeys(args.case))
    if not names: parser.error('choose --case, --all, or --list; exploration is opt-in')
    if args.script and (len(names) != 1 or 'script' not in CASES[names[0]]):
        parser.error('--script requires exactly one synthetic --case')
    binary, out = args.bin.resolve(), args.out.resolve()
    if not binary.is_file(): parser.error(f'native binary missing: {binary}; build it first')
    results = []
    for name in names:
        case = dict(CASES[name])
        if args.script:
            lines = args.script.read_text().splitlines()
            if not lines or lines[0] != 'EMAX 1': parser.error('--script needs EMAX 1 header')
            case['script'] = lines[1:]
            case.pop('recorded_tail', None)
        results.append(run_case(name, case, out / name, binary, args.timeout, args.keep_ticks))
    summary = dict(results=results, not_covered=UNREACHED,
                   unrun=[name for name in CASES if name not in names],
                   conclusion='Inputs and observations only; no exhaustive safety or original-parity claim.')
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(f'Receipt: {out / "summary.json"}')
    return 1 if any(r['status'] != 'INPUT-COMPLETED' for r in results) else 0


if __name__ == '__main__':
    sys.exit(main())
