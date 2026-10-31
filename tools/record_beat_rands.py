#!/usr/bin/env python3
"""Record a beat's rand() state word per row on the PCSX2 fork (comparison only).

A run from a start card keeps the recording's bugs only for as long as the
same rand() words reach them: a load consumes another count of draws, and the
port's draws of the frame need not fall in the original's order across owners
(a bug's choice differs from the first frame that does). This re-drives each
beat from its source state with its recorded pad, hidden, under the run lock,
and reads the state word *(D_0024295C) + 0x58 after every row. The rows are
checked against the recording's trace by position (the beat replays exactly).

Output, disc-derived and ignored: build/beat_rands/<beat>.txt, one line per row
"<script frame> <state hex>": the state after row r, for script frame r + 1
(EM_START_AT_RANDS, as tools/capture_boss_fight.py's anchors).

Usage (native arm64 macOS; the decomp's .venv supplies zstandard):
  ../Extermination/.venv/bin/python tools/record_beat_rands.py a08_01 a08_02
"""
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
sys.path.insert(0, str(DECOMP / 'tools'))
sys.path.insert(0, str(ROOT / 'tools'))
import mid_route_beats as m  # noqa: E402

STATE_BLOCK = 0x24295C        # D_0024295C; the state word is at +0x58
OUT = ROOT / 'build/beat_rands'
BUGS = False                  # --bugs: the bugs' header and position per row, a diagnosis of a run from a card


def bug_nodes(path):
    """The pool's 0012A5D0 nodes in a snapshot."""
    ram = path.read_bytes()
    return [a for a in range(0x7A5640, 0x7C8000, 0x2F0) if ram[a] and struct.unpack_from('<I', ram, a + 0x10)[0] == 0x12A5D0]


def record(beat):
    import route_capture as rc
    import pcsx2_session
    pcsx2_session.FORK_MTVU = False
    rc.use_fork('phase', True)
    t = json.loads((m.beat_dir(beat) / 'trace.json').read_text())
    inputs = {}
    for i in t['inputs']:
        inputs.setdefault(i['f'], []).append(i)
    states = []
    bugs = {}
    nodes = sorted(set(bug_nodes(m.beat_dir(beat) / 'eeMemory.bin') + bug_nodes(Path(t['source_state']).resolve().parent /
                                                                                'eeMemory.bin')))
    with rc.open_session(Path(t['source_state']).resolve(), log_dir=OUT / 'logs' / beat) as s:
        block = s.u32(STATE_BLOCK)
        far = 0
        for f in range(t['frames'] + 1):
            states.append(s.u32(block + 0x58))
            if BUGS:
                bugs[f] = {n: (s.read(n, 16).hex(), s.read(n + 0xB0, 12).hex()) for n in nodes}
            if f % 50 == 0:
                x, z = struct.unpack('<ff', s.read(0x810350, 4) + s.read(0x810358, 4)) if hasattr(s, 'read') else (None, None)
                if x is not None:
                    far = max(far, abs(x - t['rows'][f]['pos'][0]) + abs(z - t['rows'][f]['pos'][2]))
            if f == t['frames']:
                break
            for i in inputs.get(f, ()):
                s.pad(i['buttons'], lx=i['lx'], ly=i['ly'])
            s.step(1)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / f'{beat}.txt').write_text(''.join(f'{r + 1} {w:08x}\n' for r, w in enumerate(states)))
    if BUGS:
        (OUT / f'{beat}.bugs.json').write_text(json.dumps(bugs))
    print(f'{beat}: {len(states)} rows, largest position difference at the 50-row samples {far:.3f}', flush=True)


def main(argv):
    global BUGS
    BUGS = '--bugs' in argv
    argv = [a for a in argv if a != '--bugs']
    for beat in argv:
        record(beat)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
