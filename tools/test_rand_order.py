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
- the value-driven callers' totals and the faces' positions are reported.
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

import rand_order as R

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--trace', help='an existing EM_RAND_TRACE of a newgame-control run')
    ap.add_argument('--binary', default=str(ROOT / 'build/extermination'))
    args = ap.parse_args()
    trace = args.trace
    if not trace:
        out = ROOT / 'build/rand_order'
        out.mkdir(parents=True, exist_ok=True)
        trace = str(out / 'newgame_control.trace')
        env = dict(os.environ, EM_UNCAPPED='1', EM_STARTUP_TEST='newgame-control', EM_RAND_TRACE=trace)
        run = subprocess.run([args.binary], env=env, cwd=ROOT, capture_output=True, text=True)
        (out / 'run.log').write_text(run.stdout + run.stderr)
        assert run.returncode == 0 and 'newgame control test: PASS' in run.stdout + run.stderr, \
            ('newgame-control did not pass', run.returncode)
    port = R.port(trace, args.binary)
    orig, marks = R.original('newgame')
    rep, line = R.check_opening(port, orig, marks)
    after = R.check_after_control(port, rep['pc'], orig, rep['oc'], 30)
    print('rand order: ' + R.report_driven(rep))
    print(f'rand order: PASS ({line}; {after})')


if __name__ == '__main__':
    sys.exit(main())
