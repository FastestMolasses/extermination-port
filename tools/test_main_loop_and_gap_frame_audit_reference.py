#!/usr/bin/env python3
"""REPORT (not a make target): the live em_frame.c against the steps of the
original main loop 0x1AAE40 (docs/MAIN_LOOP_AND_GAP.md section 4).

It links the live src/game/em_frame.c with the recording stubs of
tests/main_loop_and_gap_frame_audit_test.c into a shared library (headless:
no window, no GPU, no audio), runs one em_frame_step in three scenarios
(ordinary frame; the movie arms in the task dispatch and ends at once; the
movie plays two more presentation steps), maps each recorded call onto the
original step it stands for, and prints what em_frame.c misses, what it
calls that the original loop does not, and what it repeats.

Nothing about em_frame.c's current deviations is pinned: they are
non-original behaviour, listed for the coordinator to remove, not
expectations. The only property that fails this report (exit 1) is a
fidelity property: the steps em_frame.c does have must appear in the
original order. Exit 2 means the report could not run against the current
em_frame.c (the stubs no longer link, or em_frame.c calls something the
map below does not know); update the stubs / map then. The step list is
the order em_mlg_001AAE40_frame (verified against the original by
tools/test_main_loop_and_gap_reference.py) runs.
"""
import ctypes as C
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LANE = os.environ.get('EM_LANE', 'b7-main-loop-and-gap')
OUT = ROOT / 'build' / LANE

# em_frame.c call -> the original step it stands for (em_frame.c's own
# comments and em_frame.h); a native-only call has no step of 0x1AAE40.
AUDIT_MAP = {'begin': 'B', 'unpack': 'C', 'screen_fade_tick': 'D', 'task_dispatch': 'E',
             'message_tick': 'F', 'transition_fade_tick': 'G', 'step_h': 'H', 'movie_pump': 'M',
             'parity^1': 'W:flip', 'counter+1': 'W:counter'}
# 'field' is the vblank's work (D_00810E90 and the IOP's field), not a step
# of the loop.
NATIVE_ONLY = ('poll', 'gamepad', 'input_pad', 'pad_raw', 'field',
               'message_render', 'end', 'overlay')
SCENARIOS = {0: 'ordinary frame', 1: 'movie arms and ends at once',
             2: 'movie plays two more presentation steps'}


def original_steps(movie):
    """The steps of one original frame, in order (em_mlg_001AAE40_frame)."""
    s = ['A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L']
    if movie:
        s += ['M', 'N', 'O']
    return s + ['P', 'T0', 'R', 'S1', 'S2', 'T', 'U', 'V', 'W:flip', 'W:call', 'W:counter']


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('frame_audit.dylib' if sys.platform == 'darwin' else 'frame_audit.so')
    r = subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                        '-shared', '-fPIC', '-Isrc', 'src/game/em_frame.c',
                        'tests/main_loop_and_gap_frame_audit_test.c', '-o', str(lib)],
                       cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        print('frame audit: the recording stubs no longer build against the live em_frame.c '
              '(update tests/main_loop_and_gap_frame_audit_test.c):', flush=True)
        print(r.stderr[-2000:], flush=True)
        sys.exit(2)
    audit = C.CDLL(str(lib))
    audit.mlg_audit_run.restype = C.c_char_p
    audit.mlg_audit_run.argtypes = [C.c_int]
    return audit


def main():
    audit = build()
    order_ok, unknown = True, set()
    for scenario, what in SCENARIOS.items():
        calls = [c for c in audit.mlg_audit_run(scenario).decode().split() if c != 'step']
        steps, native_only = [], []
        g = 0
        for c in calls:
            if c in AUDIT_MAP:
                s = AUDIT_MAP[c]
                if s == 'G':
                    s = 'G' if g == 0 else 'O'
                    g += 1
                steps.append(s)
            elif c in NATIVE_ONLY:
                if c not in native_only:
                    native_only.append(c)
            else:
                unknown.add(c)
        want = original_steps(scenario != 0)
        repeated = sorted({s for s in steps if steps.count(s) > 1})
        order = [s for s in want if s in steps]
        dedup = []
        for s in steps:
            if (not dedup or dedup[-1] != s) and s not in dedup:
                dedup.append(s)
        in_order = dedup == order
        order_ok &= in_order
        print(f'scenario {scenario} ({what}):', flush=True)
        print(f'  order of the steps it has: {"original" if in_order else "NOT ORIGINAL"} '
              f'({" ".join(dedup)})', flush=True)
        print(f'  missing: {" ".join(s for s in want if s not in steps) or "none"}', flush=True)
        print(f'  native-only calls: {" ".join(native_only) or "none"}', flush=True)
        if repeated:
            print(f'  repeated within one engine frame: {" ".join(repeated)}', flush=True)
    if unknown:
        print(f'frame audit: em_frame.c calls {" ".join(sorted(unknown))}, which the map does not '
              'know (update AUDIT_MAP / NATIVE_ONLY)', flush=True)
        sys.exit(2)
    if not order_ok:
        print('frame audit: em_frame.c runs original steps out of the original order', flush=True)
        sys.exit(1)
    print('frame audit: the steps em_frame.c has are in the original order '
          '(the missing, native-only and repeated items above are deviations to remove)', flush=True)


if __name__ == '__main__':
    main()
