#!/usr/bin/env python3
"""Skipping a first-level cutscene, compared with the original's captures.

START (or SELECT) during a skippable scene promotes 0x70003B91 from 1 to 2
(001AE6B0); the script interpreter (001BA1F0) scans to the scene's skip
landing op18 (001B6BF0), which fades out, then stores -1 in the player's
clip index +20C and hands the player back; the 0015BA50 prologues that run
while +20C is -1 read row -1 of D_00248C98 (the ELF's row before the table,
rate 0.0), and 00182DF0 restores +20C at the release. The port faulted
there before the clip-rate export carried row -1 (docs/PLAYER_STAGE_WORKERS.md
section 2.2).

The native run is the real game, headless: EM_STARTUP_TEST=newgame-skip
(New Game, then START in the opening) or, for the route scenes, the level
smoke with EM_SKIP_SCENE=N (em_opening_control_test.c drives only the
START press; the smoke drives everything else). The fixture prints one
"skip sample:" row per frame from two frames before 3B91 becomes 2 until
eight frames after control returns.

The reference is the original's captures of the same skips
(../Extermination/build/startup-reference/cutscene_skip/, recorded in
PCSX2 from the user's own disc; nothing original is embedded here). Rows
are aligned on the first frame with 3B91 == 2. Every aligned row must
agree exactly on 3B91, 3B8D, the player's +4, +5, +1F0, +20C, +2F3, +34,
+204 and D_008101E4, and on when control returns. The position and the
facing +C4: where the original's landing places the player (its landing
row differs from the row before: the opening, Roger's encounter), the port
must land on the same bits and keep them to the end of the window; where
it does not (the director beats), the scene started from wherever the
route left the player, which the level smoke does not reproduce to the
bit, so each row's change from the row before the promotion must agree
(within 1e-4). The transition substate D_0028A9A0 must agree when the fade starts
(3 on the row after the promotion) and ends (1 on the control row); the
row on which it steps 3 -> 2 is reported only: it comes one row later in
the port in every run (an open item, AREA_SCRIPT.md "The skip path"; a
sampling offset does not explain it), while the landing that waits on it
runs on the same frame in both.

The later levels (lane K1, 2026-10-29): the skips of AREA01 (the NPC's
three conversations 0x829E60 / 0x829FA0 / 0x82A660 and the shaft door's
0x8298E0), AREA00 (the arrival 0x828D60, door [51]'s 0x8294E0, the cage
terminal's scene) and the AREA01 revisit's event, compared with the
original's skips recorded on the debugging fork (the same folder,
CAPTURE_README.md "Later levels"; each pressed START 10 frames after the
first accepting frame of its route beat). Most run from a loaded game
(EM_START_AT=<start point>, the beat's recorded pad as EM_START_AT_PAD with
its path as EM_START_AT_PATH; the skip fixture runs in a loaded game too
and quits 300 frames after control). A loaded game is not the recording's
state (its timers, rand(), the camera the stick is read against), so the
position and the facing are not compared; every other field is, row by row,
plus the event flags D_00810758..760 and the counters D_008107D8..7E7 at
control and 300 frames later, which must be the original's bytes. Door
[51]'s pad starts 90 frames late: after a load the player takes no stick
input for the post-control lock, and the beat's first stick turn must land.
AREA01's 0x8298E0 and 0x829FA0 run on the AREA01 route harness from New
Game instead (their beats' first presses miss from a loaded game), compared
as the AREA11 route scenes plus the story bytes at control.

Default run (about 12 s): the opening, START on the first accepting frame,
and in parallel four loaded skips: the NPC's first and third conversations,
the AREA00 arrival and door [51]. EM_TEST_FULL=1 adds the opening 45 frames
later, the four skippable AREA11 route scenes (director beats 0..2 and
Roger's encounter, each a level-smoke run through roger), the cage terminal
and the revisit's event from loaded games, and AREA01's 0x8298E0 and
0x829FA0 on the route harness (several minutes; run in parallel).
"""
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, str(Path(__file__).resolve().parent))
import reference_mode  # noqa: E402
import level_smoke_area00 as a00  # noqa: E402
import level_smoke_area01 as a01  # noqa: E402
import level_smoke_area01r as a01r  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
CAPTURES = DECOMP / 'build/startup-reference/cutscene_skip'
BIN = ROOT / 'build/extermination'
LANE = ROOT / 'build/cutscene_skip'

# (label, capture, environment). Scene N counts the rises of 3B91 to 1 on
# the route (1 = the opening); EM_SKIP_DELAY is the press's distance from
# the first frame that accepts a skip, as in the capture.
OPENING = ('opening', 'opening/skip_at_armed',
           {'EM_STARTUP_TEST': 'newgame-skip', 'EM_SKIP_DELAY': '0'})
FULL_RUNS = [
    ('opening+45', 'opening/skip_at_f95', {'EM_STARTUP_TEST': 'newgame-skip', 'EM_SKIP_DELAY': '45'}),
] + [(f'route scene {n} ({name})', f'{name}/skip_armed_plus10',
      {'EM_STARTUP_TEST': 'newgame-level', 'EM_LEVEL_SMOKE_UNTIL': 'roger', 'EM_SKIP_SCENE': str(n),
       'EM_SKIP_DELAY': '10'})
     for n, name in ((2, '10_cage_roof_roger'), (3, '11_crevice_prompt'), (4, '13_east_tower'),
                     (5, '14_roger_encounter'))]

# Loaded-game skips: (label, capture, start point, pad set, beat, pad delay,
# steer: EM_START_AT_PATH from the recording; a01_s7 starts at the NPC and
# a00_04 at its terminal, and their recorded pads reach the scene alone,
# where the path's steering changed the player's clip before the Use).
LOADED = [
    ('AREA01 NPC first talk (a01_s0)', 'a01_s0_npc_first_talk/skip_armed_plus10', 'area01', 'area01', 'a01_s0', 0, True),
    ('AREA01 NPC third talk (a01_s7)', 'a01_s7_npc_third_talk/skip_armed_plus10', 'a01_s7', 'area01', 'a01_s7', 0, False),
    ('AREA00 arrival (a01_07)', 'a00_arrival/skip_armed_plus10', 'a01_07', 'area01', 'a01_07', 0, True),
    ('AREA00 door [51] (a00_01)', 'a00_01_door51_locked/skip_armed_plus10', 'a00_01', 'area00', 'a00_01', 90, True),
]
FULL_LOADED = [
    ('AREA00 cage terminal (a00_04)', 'a00_04_cage_terminal/skip_armed_plus10', 'a00_04', 'area00', 'a00_04', 0, False),
    ('AREA01 revisit event (a01r_01)', 'a01r_01_event/skip_armed_plus10', 'a01r_01', 'area01r', 'a01r_01', 0, True),
]
# AREA01 route scenes on the AREA01 harness (New Game, the recorded pads):
# the 6th and 7th rises of 3B91 to 1 from New Game.
FULL_AREA01 = [(f'AREA01 route scene {n} ({beat})', f'{name}/skip_armed_plus10', n, beat)
               for n, beat, name in ((6, 'a01_03', 'a01_03_shaft_locked'), (7, 'a01_05', 'a01_05_npc_bridge_talk'))]
SKIP_RUN = 300                       # frames after control a loaded run goes on (EM_SKIP_RUN)

SAMPLE = re.compile(r'^skip sample: scene=(\d+) rel=(-?\d+) frame=(\d+) (.*)$')


def le_word(hexstr):
    """A capture word written as its little-endian bytes -> its value."""
    return struct.unpack('<I', bytes.fromhex(hexstr))[0]


def capture_rows(name):
    trace = json.loads((CAPTURES / name / 'trace.json').read_text())
    first = trace['first_3B91_2_f']
    rows = {}
    for row in trace['rows']:
        record = bytes.fromhex(row['player_hex'])
        rows[row['f'] - first] = {
            'b3B91': row['b3B91'], 'b3B8D': row['b3B8D'], 'fade': row['fade16'],
            'p4': row['p4'], 'p5': row['p5'], 'm1F0': row['m1F0'], 'clip': row['clip'],
            'b2F3': row['b2F3'], 'w34': le_word(row['w34']), 'w204': le_word(row['w204']),
            'pos': struct.unpack_from('<3I', record, 0xA0), 'yaw': struct.unpack_from('<I', record, 0xC4)[0],
            'cam': int(row['cam_mode'][:2], 16)}
    return rows


def native_rows(log):
    rows = {}
    for line in log.splitlines():
        m = SAMPLE.match(line)
        if not m: continue
        fields = dict(item.split('=', 1) for item in m.group(4).split())
        rows[int(m.group(2))] = {
            'b3B91': int(fields['b3B91']), 'b3B8D': int(fields['b3B8D']), 'fade': int(fields['fade']),
            'p4': int(fields['p4']), 'p5': int(fields['p5']), 'm1F0': int(fields['m1F0']),
            'clip': int(fields['clip']), 'b2F3': int(fields['b2F3']),
            'w34': int(fields['w34'], 16), 'w204': int(fields['w204'], 16),
            'pos': tuple(int(v, 16) for v in fields['pos'].split(',')), 'yaw': int(fields['yaw'], 16),
            'cam': int(fields['cam'])}
    return rows


STATE = re.compile(r'^skip state: scene=\d+ at=(control|end) frame=\d+ flags=([0-9a-f]+) story=([0-9a-f]+) '
                   r'area=([0-9a-f]+)$', re.M)


def native_state(log):
    return {m.group(1): (m.group(2), m.group(3)) for m in STATE.finditer(log)}


def capture_state(name):
    """The flags D_00810758..760 and the counters D_008107D8..7E7 at control and
    SKIP_RUN rows later in the original's capture."""
    trace = json.loads((CAPTURES / name / 'trace.json').read_text())
    by_f = {row['f']: row for row in trace['rows']}
    out = {}
    for at, f in (('control', trace['control_f']), ('end', trace['control_f'] + SKIP_RUN)):
        if f in by_f:
            out[at] = (by_f[f]['flags'][16:34], by_f[f]['story'])
    return out


# The recorded pads (the level smoke harnesses' export of the fork
# recordings) and the recordings the paths come from.
PAD_SETS = {'area01': lambda out: a01.prepare_pads(out, True), 'area00': a00.prepare_pads,
            'area01r': a01r.prepare_pads}
CAPTURE_OF = {'area01': a01.capture_path, 'area00': a00.capture_path, 'area01r': a01r.capture_path}


def prepare_inputs(runs, routes):
    """Each loaded run's pad and path ("f x z" per recorded row), delayed by
    its pad delay (a neutral pad and the first position before it)."""
    sets = {r[3] for r in runs} | ({'area01'} if routes else set())
    for name in sets:
        PAD_SETS[name](LANE / 'pads' / name)
    inputs = {}
    for label, _capture, _point, pads, beat, delay, _steer in runs:
        pad = (LANE / 'pads' / pads / f'{beat}.pad').read_text().splitlines()
        rows = json.loads(CAPTURE_OF[pads](beat).read_text())['rows']
        head = pad[0].split()
        head[2] = str(int(head[2]) + delay)
        lines = [' '.join(head)] + (['0 0 127 127'] if delay else [])
        lines += [' '.join([str(int(r.split()[0]) + delay)] + r.split()[1:]) for r in pad[1:]]
        path = [f"{f} {rows[0]['pos'][0]!r} {rows[0]['pos'][2]!r}" for f in range(delay)]
        path += [f"{r['f'] + delay} {r['pos'][0]!r} {r['pos'][2]!r}" for r in rows]
        stem = LANE / 'pads' / f'{beat}_skip'
        stem.with_suffix('.pad').write_text('\n'.join(lines) + '\n')
        stem.with_suffix('.path').write_text('\n'.join(path) + '\n')
        inputs[label] = (stem.with_suffix('.pad'), stem.with_suffix('.path'))
    return inputs


def run(label, env_extra):
    LANE.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, EM_UNCAPPED='1', **env_extra)
    env.pop('EM_TEST_FULL', None)
    log_path = LANE / (re.sub(r'[^a-z0-9]+', '_', label.lower()).strip('_') + '.log')
    start = time.time()
    with open(log_path, 'w') as out:
        code = subprocess.run([str(BIN)], cwd=ROOT, env=env, stdout=out, stderr=subprocess.STDOUT,
                              timeout=900).returncode
    return code, log_path.read_text(errors='replace'), log_path, time.time() - start


def floats(bits):
    """Float values of one word or a tuple of words."""
    words = bits if isinstance(bits, tuple) else (bits,)
    return [struct.unpack('<f', struct.pack('<I', w))[0] for w in words]


def control_rel(rows):
    return min((r for r, row in rows.items() if r >= 0 and row['b3B91'] == 0 and row['b3B8D'] == 0 and row['p4'] == 1),
               default=None)


def fade_step(rows, want):
    hits = [r for r in sorted(rows) if r >= 0 and rows[r]['fade'] == want]
    return hits[0] if hits else None


def compare(label, capture, env_extra):
    code, log, log_path, seconds = run(label, env_extra)
    problems = []
    if code != 0: problems.append(f'exit code {code}')
    if 'cutscene skip test: PASS' not in log: problems.append('no "cutscene skip test: PASS" line')
    faults = [line for line in log.splitlines() if 'worker fault' in line or 'FAIL' in line]
    problems += [f'log: {line}' for line in faults[:3]]
    loaded = 'EM_START_AT' in env_extra
    if env_extra.get('EM_STARTUP_TEST') == 'newgame-level' and 'level smoke: PASS' not in log:
        problems.append('the level smoke did not pass after the skip')
    native = native_rows(log)
    original = capture_rows(capture)
    if not native:
        problems.append('no skip samples')
        return label, problems, seconds, log_path, None
    oc, nc = control_rel(original), control_rel(native)
    if nc is None:
        problems.append(f'control never returned (the original: promotion + {oc})')
        return label, problems[:12], seconds, log_path, None
    if oc != nc: problems.append(f'control returns at promotion + {nc}, the original at + {oc}')
    landing = min((r for r in original if r >= 0 and original[r]['clip'] == -1), default=None)
    if landing is None: problems.append('the original capture has no +20C = -1 row')
    placed = landing is not None and any(original[landing][k] != original[landing - 1][k] for k in ('pos', 'yaw'))
    if -1 not in native or -1 not in original:
        problems.append('no row before the promotion to measure the placement from')
        return label, problems, seconds, log_path, None
    compared = 0
    fields = ('b3B91', 'b3B8D', 'p4', 'p5', 'm1F0', 'clip', 'b2F3', 'w34', 'w204', 'cam')
    for rel in sorted(native):
        if rel not in original or rel > oc + 8: continue
        mine, theirs = native[rel], original[rel]
        for field in fields:
            if mine[field] != theirs[field]:
                problems.append(f'rel {rel} {field}: port {mine[field]!r} original {theirs[field]!r}')
        if loaded:
            pass        # a loaded game's position and facing are not the recording's
        elif placed and rel >= landing:
            for field in ('pos', 'yaw'):
                if mine[field] != theirs[field]:
                    problems.append(f'rel {rel} {field} (placed by the landing): port {floats(mine[field])} '
                                    f'original {floats(theirs[field])}')
        else:
            for field in ('pos', 'yaw'):
                dn = [a - b for a, b in zip(floats(mine[field]), floats(native[-1][field]))]
                do = [a - b for a, b in zip(floats(theirs[field]), floats(original[-1][field]))]
                if any(abs(a - b) > 1e-4 for a, b in zip(dn, do)):
                    problems.append(f'rel {rel} {field} change since rel -1: port {dn} original {do}')
        compared += 1
    for want, rel in ((3, 1), (1, oc)):
        if native.get(rel, {}).get('fade') != want or original.get(rel, {}).get('fade') != want:
            problems.append(f'fade at rel {rel}: port {native.get(rel, {}).get("fade")} '
                            f'original {original.get(rel, {}).get("fade")} (want {want})')
    if 'flags' in json.loads((CAPTURES / capture / 'trace.json').read_text())['rows'][0]:
        want, have = capture_state(capture), native_state(log)
        for at in ('control', 'end') if loaded else ('control',):
            if have.get(at) != want.get(at):
                problems.append(f'story bytes at {at}: port {have.get(at)} original {want.get(at)}')
    minus1 = [r for r in sorted(native) if native[r]['clip'] == -1]
    note = (f'{compared} rows, control +{nc}, +20C -1 at rel {minus1}, '
            f'{"position not compared (loaded game)" if loaded else "placed by the landing" if placed else "not moved by the landing"}, '
            f'fade 3->2 at rel {fade_step(native, 2)} (original {fade_step(original, 2)})')
    return label, problems[:12], seconds, log_path, note


def main():
    start = time.time()
    if not BIN.exists():
        sys.exit(f'{BIN} is missing (make all)')
    if not (CAPTURES / OPENING[1] / 'trace.json').exists():
        sys.exit(f'{CAPTURES} is missing the original skip captures')
    loaded = LOADED + (FULL_LOADED if reference_mode.FULL else [])
    routes = FULL_AREA01 if reference_mode.FULL else []
    for label, capture, *_ in loaded + routes:
        if not (CAPTURES / capture / 'trace.json').exists():
            sys.exit(f'{CAPTURES / capture} is missing (CAPTURE_README.md "Later levels")')
    inputs = prepare_inputs(loaded, routes)
    runs = [OPENING] + (FULL_RUNS if reference_mode.FULL else [])
    runs += [(label, capture, {'EM_START_AT': point, 'EM_START_AT_PAD': str(inputs[label][0]),
                               'EM_SKIP_SCENE': '1', 'EM_SKIP_DELAY': '10', 'EM_SKIP_RUN': str(SKIP_RUN),
                               'EM_HEADLESS': '1', **({'EM_START_AT_PATH': str(inputs[label][1])} if steer else {})})
             for label, capture, point, _pads, _beat, _delay, steer in loaded]
    runs += [(label, capture, {'EM_STARTUP_TEST': 'newgame-level', 'EM_LEVEL_SMOKE_UNTIL': beat,
                               'EM_LEVEL2_PAD_DIR': str(LANE / 'pads/area01'), 'EM_SKIP_SCENE': str(n),
                               'EM_SKIP_DELAY': '10'})
             for label, capture, n, beat in routes]
    with ThreadPoolExecutor(max_workers=len(runs)) as pool:
        results = list(pool.map(lambda r: compare(*r), runs))
    failed = 0
    for label, problems, seconds, log_path, note in results:
        status = 'FAIL' if problems else 'ok'
        failed += bool(problems)
        print(f'  {label}: {status} ({seconds:.1f} s) {note or ""}')
        for problem in problems:
            print(f'    {problem}')
        if problems: print(f'    log: {log_path}')
    reference_mode.banner(reference_mode.part(len(runs), 1 + len(FULL_RUNS) + len(LOADED) + len(FULL_LOADED) +
                                              len(FULL_AREA01), 'skip runs'))
    verdict = 'FAIL' if failed else 'PASS'
    print(f'cutscene skip: {verdict} {len(runs) - failed}/{len(runs)} runs agree with the original '
          f'captures ({time.time() - start:.1f} s)')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
