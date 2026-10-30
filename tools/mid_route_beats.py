#!/usr/bin/env python3
"""The middle of the route to the first boss, beat by beat, from start cards.

AREA22 (a22_00..02, SIXTH_LEVEL_ROUTE.md), AREA01's upper floor (a01u_00..02,
a01u_s0 / s1) and AREA06 (a06_00..05, SEVENTH_LEVEL_ROUTE.md), and the way
back (a06b, a01v, a22b: EIGHTH_LEVEL_ROUTE.md). The New Game route smoke
reaches a06_00 (tools/test_level_smoke_area01r.py); the beats past it, and
any beat alone, run from the start-point card built from its fork recording's source state (tools/build_start_saves.py,
EM_START_AT): the boot title's LOAD GAME of that state's progress block,
placed at the recorded player position, then the beat's recorded pad
(EM_START_AT_PAD, the recorder's effective pad: no recorded corrections on
these beats) steered along the recorded path (EM_START_AT_PATH).

A loaded game is not the recording's machine state (the RNG, the timers,
the actors' states and the camera come from the area load, not from the
route), so these runs are not row-by-row comparisons. What each run checks:

  1. no fail-stop: the run reaches the pad script's end without a fault
     line (a worker fault, the loader's, the stream lanes', the GS head's);
  2. the beat's end state: at the script's end the progress block
     D_00810700..D_00810D3F equals the recording's end snapshot, but for
     the bytes a loaded game or the run's clock owns (the frame count
     D_00810750..53, the load pose D_00810710..2F, the loaded flag +0x0F and
     the check byte +0x63F); every other difference is listed by offset;
  3. the end position is within END_RADIUS of the recording's last row
     (the steering keeps the route, not the exact float path).

Usage (native arm64 macOS; build/extermination and make start-saves first):

  python3 tools/mid_route_beats.py --list
  python3 tools/mid_route_beats.py                       # every chain, in route order
  python3 tools/mid_route_beats.py a22 a06               # some chains
  python3 tools/mid_route_beats.py --single a22_02       # one beat from its own card

Output: build/mid_route/<beat>.log (the run), build/mid_route/pads/ (the
pads and paths, disc-derived: never committed).
"""
import json
import math
import os
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import reference_root as refs  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get('EM_MID_ROUTE_BIN', str(ROOT / 'build/extermination')))
OUT = ROOT / 'build/mid_route'
END_RADIUS = 4.0
BLOCK, SIZE = 0x810700, 0x640
# The block bytes a loaded game or the run's clock owns (see the docstring).
OWNED = set(range(0x10, 0x30)) | set(range(0x50, 0x54)) | {0x0F, 0x63F}

A04B_ROUTE = 'levels-recordings-20261018-10/s87/route_a04b'
# beat: (start card, route recording, beat folder). A group's first beat
# starts from the group's arrival card; a01u_s1 starts where a01u_00 does.
BEATS = {
    'a22_00': ('area22', 's87/route_a22', 'a22_00_door7'),
    'a22_01': ('a22_01', 's87/route_a22', 'a22_01_corridor'),
    'a22_02': ('a22_02', 's87/route_a22', 'a22_02_progression_exit'),
    'a01u_00': ('a01u', 's87/route_a01u', 'a01u_00_gap_jump'),
    'a01u_s0': ('a01u_s0', 's87/route_a01u', 'a01u_s0_pickup'),
    'a01u_01': ('a01u_01', 's87/route_a01u', 'a01u_01_catwalk_event'),
    'a01u_02': ('a01u_02', 's87/route_a01u', 'a01u_02_progression_exit'),
    'a01u_s1': ('a01u', 's87/route_a01u', 'a01u_s1_door18_back'),
    'a06_00': ('area06', 's87/route_a06', 'a06_00_beam'),
    'a06_01': ('a06_01', 's87/route_a06', 'a06_01_crate_door2'),
    'a06_02': ('a06_02', 's87/route_a06', 'a06_02_keypad'),
    'a06_03': ('a06_03', 's87/route_a06', 'a06_03_room_out'),
    'a06_04': ('a06_04', 's87/route_a06', 'a06_04_beam_collapse'),
    'a06_05': ('a06_05', 's87/route_a06', 'a06_05_door3_locked'),
    'a06b_00': ('a06b', 's87/route_a06b', 'a06b_00_trigger_jump'),
    'a06b_01': ('a06b_01', 's87/route_a06b', 'a06b_01_door1'),
    'a01v_00': ('a01v', 's87/route_a01v', 'a01v_00_catwalk_east'),
    'a01v_01': ('a01v_01', 's87/route_a01v', 'a01v_01_gap_jump'),
    'a01v_02': ('a01v_02', 's87/route_a01v', 'a01v_02_door18'),
    'a22b_00': ('a22b', 's87/route_a22b', 'a22b_00_to_door7'),
    'a22b_01': ('a22b_01', 's87/route_a22b', 'a22b_01_door6'),
    # AREA04's second visit (the fork continuation; EIGHTH_LEVEL_ROUTE.md):
    # to the first boss. The creature is spawned by the run's event [3]
    # script when the chain plays a04b_03; a04b_04 ends in AREA13.
    'a04b_00': ('a04b_00', A04B_ROUTE, 'a04b_00_door45'),
    'a04b_01': ('a04b_01', A04B_ROUTE, 'a04b_01_npc'),
    'a04b_02': ('a04b_02', A04B_ROUTE, 'a04b_02_door45_out'),
    'a04b_03': ('a04b_03', A04B_ROUTE, 'a04b_03_reader'),
    'a04b_04': ('a04b_04', A04B_ROUTE, 'a04b_04_lift'),
}
GROUPS = {'a22': ('a22_',), 'a01u': ('a01u_',), 'a06': ('a06_',), 'a06b': ('a06b_',), 'a01v': ('a01v_',),
          'a22b': ('a22b_',), 'a04b': ('a04b_',)}
FAULT = re.compile(r'FAULT|faulted|fault:|the game task is stopped|refused|Assertion|'
                   r'(?<!Mkdir, )fail-stop|reached without port code: (?!001D1EF0|00200830)')


def beat_dir(beat):
    _card, route, folder = BEATS[beat]
    return refs.capture(route) / folder


def trace(beat):
    t = json.loads((beat_dir(beat) / 'trace.json').read_text())
    assert not t.get('teleports'), (beat, 'reference contains teleports')
    assert not t.get('v263_pad_corrections'), (beat, 'a recorded pad correction: not modelled here')
    assert len(t['rows']) == t['frames'] + 1, (beat, 'truncated reference')
    return t


def prepare(name, beats):
    """The chain's EMA1 pad (each beat's recorded commands, placed at its
    first counter relative to the chain's first beat), its path (the
    recorded x, z per row; a counter gap between beats holds the last
    position) and the marks (each beat's last row but the last beat's)."""
    traces = [trace(b) for b in beats]
    base = traces[0]['first_counter']
    pads = OUT / 'pads'
    pads.mkdir(parents=True, exist_ok=True)
    rows, sent, path, marks = [], None, [], []
    for b, t in zip(beats, traces):
        off = t['first_counter'] - base
        assert off >= len(path), (b, 'beats overlap', off, len(path))
        while len(path) < off:
            path.append(path[-1])
        for r in t['inputs']:
            value = (r['buttons'], r['lx'], r['ly'])
            if value != sent or (rows and rows[-1][0] == r['f'] + off):
                rows.append((r['f'] + off, *value))
                sent = value
        path += [(r['pos'][0], r['pos'][2]) for r in t['rows']]
        marks.append(off + t['frames'])
    last = marks.pop()
    assert rows and len(rows) <= 4096 and all(0 <= f <= last for f, *_ in rows), name
    pad = pads / f'{name}.pad'
    pad.write_text('\n'.join([f'EMA1 1 {last} 1'] + [f'{f} {b:x} {lx} {ly}' for f, b, lx, ly in rows]) + '\n')
    path_file = pads / f'{name}.path'
    path_file.write_text('\n'.join(f'{i} {x!r} {z!r}' for i, (x, z) in enumerate(path)) + '\n')
    return pad, path_file, marks, traces


def source_rand(beat):
    """The recording's rand() state word (*D_0024295C + 0x58) in the beat's
    source state: the creatures draw the recording's stream from control
    (EM_START_AT_RAND, comparison only)."""
    t = json.loads((beat_dir(beat) / 'trace.json').read_text())
    ram = (Path(t['source_state']).resolve().parent / 'eeMemory.bin').read_bytes()
    block = int.from_bytes(ram[0x24295C:0x242960], 'little')
    return int.from_bytes(ram[block + 0x58:block + 0x5C], 'little')


def end_block(beat):
    ram = (beat_dir(beat) / 'eeMemory.bin').read_bytes()
    return ram[BLOCK:BLOCK + SIZE]


def ranges(offsets):
    out = []
    for o in offsets:
        if out and o == out[-1][1] + 1:
            out[-1][1] = o
        else:
            out.append([o, o])
    return out


def check_end(beat, t, pos, block_hex):
    problems = []
    last = t['rows'][-1]
    d = math.dist(pos, last['pos'])
    if d > END_RADIUS:
        problems.append(f'{beat}: end position {pos} is {d:.2f} from the recording\'s {last["pos"]}')
    got, want = bytes.fromhex(block_hex), end_block(beat)
    diff = [i for i in range(SIZE) if got[i] != want[i] and i not in OWNED]
    if diff:
        problems.append(f'{beat}: end block differs at ' + ', '.join(
            f'D_{BLOCK + a:08X}' + (f'..{BLOCK + b:08X}' if b != a else '') +
            f' (port {got[a:b + 1].hex()} original {want[a:b + 1].hex()})' for a, b in ranges(diff)[:12]))
    return problems


def run(name, card, beats, timeout):
    """One run from the card: the beats' pads in one script; each beat's end
    checked at its mark (the last at the script's end). Returns (results,
    seconds, log): results = [(beat, problems or None when not reached)]."""
    pad, path, marks, traces = prepare(name, beats)
    log_path = OUT / f'{name}.log'
    env = {k: v for k, v in os.environ.items() if not k.startswith('EM_')}
    env.update(EM_START_AT=card, EM_START_AT_PAD=str(pad), EM_START_AT_PATH=str(path), EM_HEADLESS='1',
               EM_UNCAPPED='1', EM_MUTE='1', EM_START_AT_RAND=f'{source_rand(beats[0]):08x}')
    if marks:
        env['EM_START_AT_MARKS'] = ','.join(map(str, marks))
    start = time.time()
    ended = False
    with open(log_path, 'w') as log:
        proc = subprocess.Popen([str(BIN)], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            while proc.poll() is None:
                time.sleep(0.5)
                text = log_path.read_text(errors='replace')
                if 'pad end at' in text and 'block at the pad end' in text:
                    ended = True
                    break
                if FAULT.search(text) or time.time() - start > timeout:
                    break
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(10)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
    text = log_path.read_text(errors='replace')
    seconds = time.time() - start
    faults = [ln for ln in text.splitlines() if FAULT.search(ln) and 'block ' not in ln]
    reports = re.findall(r'pad (?:mark \d+|end) at \(([-\d.]+),([-\d.]+),([-\d.]+)\)[^\n]*\n'
                         r'[^\n]*block at (?:pad mark \d+|the pad end) ([0-9A-F]+)', text)
    results = []
    for k, (b, t) in enumerate(zip(beats, traces)):
        if k < len(reports):
            x, y, z, blk = reports[k]
            results.append((b, check_end(b, t, [float(x), float(y), float(z)], blk)))
        else:
            clock = re.findall(r'pad frame (\d+) at', text)
            why = ('fault: ' + faults[0][:300]) if faults else \
                f'no end report within {timeout:.0f} s (last pad frame {clock[-1] if clock else "-"})'
            results.append((b, [why]))
            break
    if not ended and len(results) == len(beats) and not results[-1][1]:
        results[-1] = (results[-1][0], ['the script did not end'])
    return results, seconds, log_path


# A chain: one run from its card through these beats in recorded order
# (the creatures, the RNG from control and the camera evolve as in the
# recording from the group's arrival; the side beats run alone).
CHAINS = {
    'a22': ('area22', ('a22_00', 'a22_01', 'a22_02')),
    'a01u': ('a01u', ('a01u_00', 'a01u_01', 'a01u_02')),
    'a01u_s0': ('a01u_s0', ('a01u_s0',)),
    'a01u_s1': ('a01u', ('a01u_s1',)),
    'a06': ('area06', ('a06_00', 'a06_01', 'a06_02', 'a06_03', 'a06_04', 'a06_05')),
    'a06b': ('a06b', ('a06b_00', 'a06b_01')),
    'a01v': ('a01v', ('a01v_00', 'a01v_01', 'a01v_02')),
    'a22b': ('a22b', ('a22b_00', 'a22b_01')),
    'a04b': ('a04b_00', ('a04b_00', 'a04b_01', 'a04b_02', 'a04b_03', 'a04b_04')),
}


def main(argv):
    if argv == ['--list']:
        for b, (card, route, folder) in BEATS.items():
            print(f'{b:8} EM_START_AT={card:8} {route}/{folder}')
        for n, (card, beats) in CHAINS.items():
            print(f'chain {n:8} EM_START_AT={card:8} {" ".join(beats)}')
        return 0
    timeout = float(os.environ.get('EM_MID_ROUTE_TIMEOUT', '900'))
    single = '--single' in argv
    argv = [a for a in argv if a != '--single']
    if argv in ([], ['all']):
        names = list(CHAINS) if not single else list(BEATS)
    else:
        names = argv
    if not BIN.exists():
        print(f'{BIN} is missing (make all)', file=sys.stderr)
        return 2
    runs = []
    for n in names:
        if not single and n in CHAINS:
            runs.append((n, *CHAINS[n]))
        elif n in BEATS:
            runs.append((n, BEATS[n][0], (n,)))
        else:
            print(f'mid_route_beats: unknown chain or beat {n!r}', file=sys.stderr)
            return 2
    passed = total = 0
    for name, card, beats in runs:
        results, seconds, log_path = run(name, card, beats, timeout)
        print(f'{name} (EM_START_AT={card}, {seconds:.0f} s, {log_path.relative_to(ROOT)}):')
        for b, problems in results:
            total += 1
            passed += not problems
            print(f'  {b}: {"PASS" if not problems else "FAIL"}')
            for p in problems:
                print(f'    {p}')
        for b in beats[len(results):]:
            total += 1
            print(f'  {b}: not reached')
        sys.stdout.flush()
    print(f'mid_route_beats: {passed} of {total} beats pass')
    return 0 if passed == total else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
