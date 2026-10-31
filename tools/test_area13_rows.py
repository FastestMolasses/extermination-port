#!/usr/bin/env python3
"""AREA13's scripted stretches against their fork recordings, row by row.

A start card is not the route's machine state, and its walks are steered
(tools/mid_route_beats.py), so a beat's walk is not compared. What a card can
prove is a stretch the scripts drive, or a recorded pad with nothing to steer.
Each entry below runs one beat from its card with the recorded pad, the
recording's rand() word and the tick log (EM_AREA_CHANGE_LOG), aligns the
stretch's first row by position and heading and compares position, heading and
action row by row (comparison only: no anchor writes anything into the run).

  python3 tools/test_area13_rows.py            # every stretch
  python3 tools/test_area13_rows.py a13c_03    # the stretches of some beats

Output under build/m1rows/<beat>/ (ticks deleted after the compare).
Headless; nothing here embeds original bytes.
"""
import json
import os
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mid_route_beats as m  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get('EM_MID_ROUTE_BIN', str(ROOT / 'build/extermination'))).resolve()
# beat: [(first row, last row, what)]: the rows TENTH / ELEVENTH / TWELFTH_LEVEL_ROUTE.md
# give for a stretch with the player under a script.
STRETCHES = {
    'a13b_01': [(337, 548, 'door [17] from inside: the transit to entry 9')],
    'a13c_00': [(507, 800, "the recharger's script 0x247420, its prompt and charge")],
    'a13c_02': [(6, 1176, "[44]'s scripts 0x82B090 and 0x82B2D0, the battery page")],
    'a13c_03': [(464, 1283, "the blast scene (script 0x82B3D0, 3B8D = 2)")],
    'a13c_05': [(1140, 1813, "[7]'s script 0x82C510")],
    'a13d_06': [(331, 542, 'door [20] from the north, the transit to entry 5')],
    'a13d_07': [(114, 543, "the hatch [63]'s script 0x82CA50")],
}


def f32(word):
    return struct.unpack('<f', struct.pack('<I', word))[0]


def native_rows(ticks):
    for line in open(ticks, errors='replace'):
        if line.startswith('{'):
            try:
                row = json.loads(line)
            except ValueError:
                continue
            if 'player' in row and 'pos_post' in row:
                yield [f32(w) for w in row['pos_post']], f32(row['yaw_post']), row['player'][1]


def same(n, r):
    pos, yaw, action = n
    return max(abs(a - b) for a, b in zip(pos, r['pos'])) < 1e-3 and abs(yaw - r['yaw']) < 1e-3 and \
        action == r['m1F0']


def compare(rows, native, lo, hi, what):
    want = rows[lo:hi + 1]
    best = (0, -1, -1)                  # identical, native start, offset of the anchor
    # the anchor: a recorded row near the stretch's head a native row equals
    for shift in range(min(120, len(want))):
        for i, n in enumerate(native):
            if same(n, want[shift]):
                good = sum(1 for k in range(min(len(want) - shift, len(native) - i))
                           if same(native[i + k], want[shift + k]))
                if good > best[0]:
                    best = (good, i, shift)
    good, start, shift = best
    run = 0
    if start >= 0:
        while shift + run < len(want) and start + run < len(native) and same(native[start + run], want[shift + run]):
            run += 1
    print(f'  rows {lo}..{hi} ({what}): {good} of {len(want)} identical, {run} consecutive from row '
          f'{lo + shift if start >= 0 else "-"}')
    return good, len(want)


def run(beat):
    card = m.BEATS[beat][0]
    out = ROOT / 'build/m1rows' / beat
    out.mkdir(parents=True, exist_ok=True)
    pad, path, _marks, traces = m.prepare(f'rows_{beat}', (beat,))
    log, ticks = out / 'run.log', out / 'ticks.jsonl'
    env = {k: v for k, v in os.environ.items() if not k.startswith('EM_')}
    env.update(EM_START_AT=card, EM_START_AT_PAD=str(pad), EM_START_AT_PATH=str(path), EM_HEADLESS='1',
               EM_UNCAPPED='1', EM_MUTE='1', EM_START_AT_RAND=f'{m.source_rand(beat):08x}',
               EM_START_AT_FADE=str(m.row0_fade(beat)), EM_AREA_CHANGE_LOG=str(ticks), EM_START_AT_FINE='1')
    with open(log, 'w') as f:
        p = subprocess.Popen([str(BIN)], cwd=ROOT, env=env, stdout=f, stderr=subprocess.STDOUT)
        start = time.time()
        try:
            while p.poll() is None and time.time() - start < 600:
                time.sleep(0.5)
                text = log.read_text(errors='replace')
                if 'block at the pad end' in text or m.FAULT.search(text):
                    break
        finally:
            if p.poll() is None:
                p.terminate()
                p.wait()
    print(beat, '(card', card + ')')
    try:
        native = list(native_rows(ticks))
    finally:
        os.unlink(ticks)
    rows = traces[0]['rows']
    return [compare(rows, native, lo, min(hi, len(rows) - 1), what) for lo, hi, what in STRETCHES[beat]]


def main(argv):
    results = [r for b in (argv or list(STRETCHES)) for r in run(b)]
    print('rows:', sum(g for g, _ in results), 'of', sum(n for _, n in results), 'identical')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
