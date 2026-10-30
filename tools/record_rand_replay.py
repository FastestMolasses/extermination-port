#!/usr/bin/env python3
"""Record the comparison-only rand() replay table on the PCSX2 fork.

The first level's level-smoke phases are navigated by the port's own
driver, not replayed from the route's pads, so the rand() stream reaches
the AREA01 arrival at another position than the recording's (every
caller after that draws other values: the AREA00 creatures' choices first;
LEVEL3_BINDING.md iteration 19). The AREA01 / AREA00 phases replay the
recording's pads, so one anchor at the AREA01 area entry puts the
recording's stream in place for everything that follows.

For each requested beat (default: route 15, the AREA11 exit and the AREA01
arrival) this re-drives the beat on the fork from its source state with its
recorded inputs (the decomp's tools/route_capture.py, hidden, under the run
lock), logs every return of 00122BB8 (an inline log probe on its return:
the caller's return address and the result, the new state's low 31 bits)
and steps the state word *(D_0024295C) + 0x58 forward from the row-0
sample. The area entry is the beat's draw from 0x1AE040 state 0's
001FAE70(1) (return address 0x1FAF28) that follows the area change; its
state before the call is recorded as "entry<area>".

Output (one NEW folder; data derived from the user's disc, never committed):
  <out>/table.txt      lines "<key> <state hex>" (entry01: the first entry
                       into AREA01; entry01.2: the revisit's)
  <out>/receipt.json   per beat: rows, draws, the entry's draw index and the
                       replay's row differences
The AREA00 harness passes the table as EM_RAND_REPLAY (comparison only;
em_level_smoke_test.c rand_replay_anchor).

Usage (native arm64 macOS; the decomp's .venv supplies zstandard):
  ../Extermination/.venv/bin/python tools/record_rand_replay.py --out build/rand_replay/<new>
"""
import argparse
import json
import struct
import sys
import time
from pathlib import Path

DECOMP = Path('/Users/abe/Documents/Extermination.nosync/Extermination')
sys.path.insert(0, str(DECOMP / 'tools'))

A, C, M = 1103515245, 12345, 0xFFFFFFFF
RAND_RETURN = 0x122BE0       # 00122BB8's return (the result is in v0 there)
ENTRY_RA = 0x1FAF28          # 001FAE70's draw from 0x1AE040 state 0 (the area entry)
STATE_BLOCK = 0x24295C       # D_0024295C; the state word is at +0x58
# beat -> (route set path, the entered area, the table key). The key counts
# the run's entries into that area: the AREA00 return (a00_10) enters AREA01
# a second time ("entry01.2", the revisit), after an area read the port does
# at host speed (its load veil draws rand() per tick for fewer ticks).
# Door [16] enters AREA02 (entry02, a01r_03) and the duct's sub change runs
# 0x1AE040 state 0 again in AREA02 (entry02.2, a02_00), each after an area
# read at host speed. Door [25] enters AREA04 (entry04, a02_05); door [37]
# enters AREA22 (entry22, a04_05).
BEATS = {'15_level_exit': ('route/15_level_exit', 1, 'entry01'),
         'a00_10_progression_exit': ('route_a00/a00_10_progression_exit', 1, 'entry01.2'),
         'a01r_03_door16': ('route_a01r/a01r_03_door16', 2, 'entry02'),
         'a02_00_duct': ('route_a02/a02_00_duct', 2, 'entry02.2'),
         'a02_05_progression_exit': ('route_a02/a02_05_progression_exit', 4, 'entry04'),
         'a04_05_progression_exit': ('route_a04/a04_05_progression_exit', 0x16, 'entry22')}


def record(beat, path, area, key, out):
    import pcsx2_session
    import route_capture as rc
    t = json.loads((DECOMP / 'build/fork_refs/s87' / path / 'trace.json').read_text())
    pcsx2_session.FORK_MTVU = False
    rc.use_fork('phase', True)
    by_f = {}
    for x in t['inputs']:
        by_f.setdefault(x['f'], []).append(x)
    assert not t.get('teleports'), (beat, 'a beat with teleports cannot be re-driven')
    diffs, states = [], []
    with rc.open_session(Path(t['source_state']), log_dir=out / 'logs' / beat) as s:
        if t['lead_in_frames']:
            s.step(t['lead_in_frames'])
        r = rc.Route(s)
        r.pad_fix = rc.v263_pad_corrections(t['beat'])
        r.begin()
        rc.next_long_frames(r)
        rc.use_a00_sampler(r) if beat.startswith('a00') else rc.use_a01_sampler(r)
        block = s.u32(STATE_BLOCK)
        s._client.call('probe_add', pc=RAND_RETURN, action='log', path='rand_calls.bin', label='rand_ret')

        def sample(f):
            states.append(s.u32(block + 0x58))
            row, want = r.rows[-1], t['rows'][f]
            d = sorted(k for k in set(row) & set(want)
                       if k not in {'counter', 'f', 'fi', 'fld', 'vs', 'vsync'} and row[k] != want[k])
            if d:
                diffs.append((f, d))
        sample(0)
        for f in range(t['frames']):
            for x in by_f.get(f, []):
                r.set_pad(x['buttons'], x['lx'], x['ly'])
            r.step(1)
            sample(f + 1)
        s._client.call('probe_remove', all=True)
        calls = (Path(s._inst.info()['root']) / 'rand_calls.bin').read_bytes()
    records = [struct.unpack_from('<IIIIQIIIIIIIIII', calls, k) for k in range(0, len(calls), 64)]
    state, entry = states[0], None
    for n, rec in enumerate(records):
        after = (state * A + C) & M
        assert after & 0x7FFFFFFF == rec[12], (beat, 'the logged result is not the next state', n)
        if rec[6] == ENTRY_RA and entry is None and n > 0:
            entry = (n, state)
        state = after
    assert entry is not None, (beat, 'no area-entry 001FAE70 draw in the beat')
    return {'beat': beat, 'rows': t['frames'], 'draws': len(records), 'entry_draw': entry[0],
            'key': key, 'area': area, 'state': f'{entry[1]:08x}', 'row_differences': len(diffs),
            'first_differences': diffs[:5]}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True, help='a new folder')
    ap.add_argument('beats', nargs='*', default=list(BEATS))
    args = ap.parse_args()
    out = args.out.absolute()
    if out.exists():
        raise SystemExit(f'{out} exists: record into a new folder')
    out.mkdir(parents=True)
    started, receipt = time.monotonic(), []
    for beat in args.beats:
        path, area, key = BEATS[beat]
        receipt.append(record(beat, path, area, key, out))
        print(json.dumps(receipt[-1]), flush=True)
    (out / 'table.txt').write_text(''.join(f"{r['key']} {r['state']}\n" for r in receipt))
    (out / 'receipt.json').write_text(json.dumps({'beats': receipt, 'seconds': round(time.monotonic() - started, 1)},
                                                 indent=1) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
