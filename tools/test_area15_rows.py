#!/usr/bin/env python3
"""AREA15's and AREA03's beats against their fork recordings, row by row.

A start card is not the route's machine state (tools/mid_route_beats.py), so
a beat's steered walk is not compared. What a card can prove is the scripted
stretch: from the moment the run and the recording enter the same area and
sub (the forced return of a15b_01; AREA03 from its first row) the pad and the
scripts drive the same frames. Each chain runs from its card with the recorded
pad, the recording's rand() word and the tick log (EM_AREA_CHANGE_LOG); the
rows are aligned at the anchor and compared for position, heading and
action; the first difference and the identical count are
reported (comparison only: no anchor writes anything into the run).

  python3 tools/test_area15_rows.py a15b a03

Output under build/k1rows/<chain>/ (ticks deleted after the compare).
Headless; nothing here embeds original bytes.
"""
import json
import os
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mid_route_beats as m  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get('EM_MID_ROUTE_BIN', str(ROOT / 'build/extermination'))).resolve()
END = re.compile(r'block at the pad end|the pack does not hold lsn|FAULT at')
# chain: the area-sub the alignment waits for (a row's first three area bytes)
ANCHORS = {'a15a': (15, 0, 0), 'a15b': (15, 0, 0), 'a03': (3, 0, 1), 'a15c': (15, 0, 0)}


def f32(word):
    return struct.unpack('<f', struct.pack('<I', word))[0]


def run(chain):
    card, beats = m.CHAINS[chain]
    out = ROOT / 'build/k1rows' / chain
    out.mkdir(parents=True, exist_ok=True)
    pad, path, marks, traces = m.prepare(f'rows_{chain}', beats)
    log, ticks = out / 'run.log', out / 'ticks.jsonl'
    env = {k: v for k, v in os.environ.items() if not k.startswith('EM_')}
    env.update(EM_START_AT=card, EM_START_AT_PAD=str(pad), EM_START_AT_PATH=str(path), EM_HEADLESS='1',
               EM_UNCAPPED='1', EM_MUTE='1', EM_START_AT_RAND=f'{m.source_rand(beats[0]):08x}',
               EM_START_AT_FADE=str(m.row0_fade(beats[0])), EM_AREA_CHANGE_LOG=str(ticks))
    with open(log, 'w') as f:
        p = subprocess.Popen([str(BIN)], cwd=ROOT, env=env, stdout=f, stderr=subprocess.STDOUT)
        start = time.time()
        try:
            while p.poll() is None and time.time() - start < 1500:
                time.sleep(0.5)
                if END.search(log.read_text(errors='replace')):
                    break
        finally:
            if p.poll() is None:
                p.terminate()
                p.wait()
    try:
        return compare(chain, traces, ticks)
    finally:
        os.unlink(ticks)


def native_rows(ticks):
    for line in open(ticks):
        if line.startswith('{'):
            try:
                row = json.loads(line)
            except ValueError:
                continue
            if 'player' in row and 'pos_post' in row:
                yield row


def row_problems(r, n):
    why = []
    pos = [f32(w) for w in n['pos_post']]
    if max(abs(a - b) for a, b in zip(pos, r['pos'])) >= 1e-3:
        why.append(f'position {pos} {r["pos"]}')
    if abs(f32(n['yaw_post']) - r['yaw']) >= 1e-3:
        why.append(f'heading {f32(n["yaw_post"])} {r["yaw"]}')
    if n['player'][1] != r['m1F0']:
        why.append(f'action {n["player"][1]} {r["m1F0"]}')
    return why


def compare(chain, traces, ticks):
    native = list(native_rows(ticks))
    anchor = ANCHORS[chain]
    key = bytes(anchor[:3]).hex()
    rows = [r for t in traces for r in t['rows']]
    start = next(i for i, r in enumerate(rows) if r['area4'].startswith(key))
    # the anchor row's place (a load's first frames sit at the spawn entry)
    nstart = None
    for shift in range(0, 2000):
        if start + shift >= len(rows):
            break
        want = rows[start + shift]['pos']
        nstart = next((i for i, n in enumerate(native)
                       if max(abs(f32(w) - x) for w, x in zip(n['pos_post'], want)) < 1e-3), None)
        if nstart is not None:
            start += shift
            break
    print(chain, 'trace rows', len(rows), 'anchor row', start, 'native rows', len(native), 'anchor', nstart)
    if nstart is None:
        return False
    good = bad = run = 0
    first = None
    spans = []
    for i in range(min(len(rows) - start, len(native) - nstart)):
        r, n = rows[start + i], native[nstart + i]
        why = row_problems(r, n)
        if why:
            bad += 1
            first = first or (i, why)
        else:
            good += 1
            run = run + 1 if run >= 0 and bad == 0 else run
            if spans and spans[-1][1] == i - 1:
                spans[-1][1] = i
            else:
                spans.append([i, i])
    print(f'{chain}: {good} rows identical, {bad} differ; {run} identical before the first difference {first}')
    print('  longest identical spans (row offsets from the anchor):',
          sorted(spans, key=lambda x: x[0] - x[1])[:5])
    return not bad and good > 0


def main(argv):
    failed = [c for c in argv or list(ANCHORS) if not run(c)]
    print('rows:', 'PASS' if not failed else 'FAIL ' + ' '.join(failed))
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
