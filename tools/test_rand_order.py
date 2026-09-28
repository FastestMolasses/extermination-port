#!/usr/bin/env python3
"""The port's rand() call order against the original's (docs/RAND_ORDER.md).

Runs a headless New Game to first control + 30 ticks with EM_RAND_TRACE and
compares every call with the decomp's C7 per-call capture of the same
stretch (build/s87/c7cap/rng/newgame, CAPTURES_C7.md section 3), aligned on
the area entry (0x1AE040 state 0's 001FAE70(1), which draws from the
unseeded state 1 in both):
- the area-entry frame and every call after it equal in caller and state up
  to the one known divergence (the husk creature's missing draw, census
  L24), which must be the first difference;
- every opening frame's deterministic callers equal the original's frame for
  frame, and the 30 frames after first control;
- the value-driven callers' totals and the faces' positions are reported;
- the opening's end follows the stream drive's mode (the run's "stream
  drive:" line; EM_PS2_DISC_DRIVE_TIMING, LAUNCHER_OPTIONS.md): with the PS2
  disc-drive timing on, first control up to 16 frames earlier; at host speed
  (the default), exactly the capture's drive wait earlier, with the stream
  request's rows (the run's EM_AREA_CHANGE_LOG) read at host speed.
"""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

import rand_order as R

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--trace', help='an existing EM_RAND_TRACE of a newgame-control run (with --log and '
                                    '--run-log of the same run)')
    ap.add_argument('--log', help='the same run\'s EM_AREA_CHANGE_LOG')
    ap.add_argument('--run-log', help='the same run\'s stderr')
    ap.add_argument('--binary', default=str(ROOT / 'build/extermination'))
    args = ap.parse_args()
    trace, log, run_log = args.trace, args.log, args.run_log
    if not trace:
        out = ROOT / 'build/rand_order'
        out.mkdir(parents=True, exist_ok=True)
        trace, log, run_log = (str(out / n) for n in ('newgame_control.trace', 'ticks.jsonl', 'run.log'))
        env = dict(os.environ, EM_UNCAPPED='1', EM_STARTUP_TEST='newgame-control', EM_RAND_TRACE=trace,
                   EM_AREA_CHANGE_LOG=log)
        run = subprocess.run([args.binary], env=env, cwd=ROOT, capture_output=True, text=True)
        Path(run_log).write_text(run.stdout + run.stderr)
        assert run.returncode == 0 and 'newgame control test: PASS' in run.stdout + run.stderr, \
            ('newgame-control did not pass', run.returncode)
    assert log and run_log, '--trace needs --log and --run-log of the same run (the stream drive\'s mode and rows)'
    mode = R.drive_mode(Path(run_log).read_text())
    with open(log) as f:
        port_req = R.lane0_request_port([json.loads(line) for line in f])
    port = R.port(trace, args.binary)
    orig, marks = R.original('newgame')
    rep, line = R.check_opening(port, orig, marks, (mode, port_req))
    after = R.check_after_control(port, rep['pc'], orig, rep['oc'], 30)
    print('rand order: ' + R.report_driven(rep))
    print(f'rand order: PASS ({line}; {after})')


if __name__ == '__main__':
    sys.exit(main())
