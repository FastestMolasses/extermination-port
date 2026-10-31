#!/usr/bin/env python3
"""The AREA15 sub 0 arrival on the ORIGINAL (the PCSX2 fork), for its start card.

a19d_20_door50 starts in AREA19's tower: door [50], the load, AREA15 sub 0
entry 0 at f526, then [0]'s long scene 0x826E70 (counter 0x22). A card needs a
source state inside AREA15, so this replays that beat's recorded pad, open
loop, to f526 from a19d_19's end (hidden ForkSession under the run lock,
tools/route_capture.py's Route, no memory written) and snapshots there. The
beat a15a_00_arrival is the rest of a19d_20's recording: its rows from f526,
renumbered, with that recording's end snapshot.

Output (ignored, disc-derived):
  ../Extermination/build/fork_refs/area15_arrival_20261030/s87/route_a15a/

Usage (port root; native arm64 macOS, the fork x86_64 under Rosetta):
  python3 tools/capture_area15_arrival.py
"""
import json
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
sys.path.insert(0, str(DECOMP / 'tools'))
import route_capture as rc  # noqa: E402

GROUP = DECOMP / 'build/fork_refs/levels-recordings-20261018-19/s87/route_a19d'
OUT = DECOMP / 'build/fork_refs/area15_arrival_20261030/s87/route_a15a'
STOP = 526                                 # AREA15 entry 0, the frame before [0]'s D_00810702 store
LEAD_IN = 2


def main():
    trace = json.loads((GROUP / 'a19d_20_door50/trace.json').read_text())
    src = GROUP / 'a19d_19_flights/state.p2s'
    (OUT / 'a15a_src').mkdir(parents=True, exist_ok=True)
    rc.use_fork()
    with rc.open_session(src, log_dir=OUT.parent / 'logs') as s:
        s.step(LEAD_IN)
        r = rc.Route(s)
        r.begin()
        inputs = {i['f']: i for i in trace['inputs']}
        for f in range(STOP):
            if f in inputs:
                i = inputs[f]
                r.set_pad(i['buttons'], i['lx'], i['ly'])
            r.step(1)
        row = r.rows[-1]
        want = trace['rows'][STOP]
        assert (row['counter'], row['pos']) == (want['counter'], want['pos']), (row['counter'], want['counter'])
        print('at', row['counter'], row['pos'], s.read(0x810700, 4).hex())
        s.snapshot(OUT / 'a15a_src')
    beat = OUT / 'a15a_00_arrival'
    beat.mkdir(exist_ok=True)
    rows = trace['rows'][STOP:]
    for k, x in enumerate(rows):
        x['f'] = k
    new = dict(trace, beat='a15a_00_arrival', rows=rows, inputs=[dict(f=0, buttons=0, lx=127, ly=127)], frames=len(rows) - 1,
               first_counter=rows[0]['counter'], source_state=str(OUT / 'a15a_src/state.p2s'),
               what="AREA15 sub 0's arrival: [0]'s script 0x826E70 (a19d_20's rows from f526)")
    (beat / 'trace.json').write_text(json.dumps(new))
    shutil.copy2(GROUP / 'a19d_20_door50/eeMemory.bin', beat / 'eeMemory.bin')
    (OUT / 'manifest.json').write_text(json.dumps({'what': 'tools/capture_area15_arrival.py', 'beats': {
        'a15a_00_arrival': {'source_state': new['source_state'], 'frames': new['frames']}}}, indent=1))


if __name__ == '__main__':
    main()
