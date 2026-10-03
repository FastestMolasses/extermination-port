#!/usr/bin/env python3
"""The BRANCH side runs of the level smoke, in parallel (audit 1b item 16;
LEVEL_SMOKE.md "The BRANCH side runs").

Each run plays the main line to the side phase's source, then the phase
(em_level_smoke_test.c "branches": the capture lane BRANCH's closed-loop
policies, route_capture.py br_beat_*):

  br_ledge_ammo     (from boxes)          br_00: pickup g0.3 on the 220 ledge
  br_map_item       (from slide)          br_01: the map item g0.6 (0015AFA0)
  br_elevator_up    (from elevator)       br_02: the terminal's ride back up
  br_panel_decline  (from elevator_refusal) br_03: the panel, No, Triangle
  br_crate_stack    (from elevator)       br_04: box r5 broken, r3's fall
  br_west_ledge     (from fence_door)     br_05..br_08: the corridor box, the
                    west-yard ladder up, box r6, pickup g0.5, the ladder down
  br_yard_ammo      (from fence_door)     br_10: pickup g0.1 on the yard floor
  br_cage_key       (from truck_crossing) br_09: ladder A, pickup g0.4
  br_plateau        (from crevice_prompt) br_11..br_13: the raised pipe, the
                    plateau ladder up, pickup g0.2, the ladder down
  br_roger_talk     (from roger)          br_14: Roger's talk 0x828810

tools/test_level_smoke.py checks each run as every level-smoke run, and
tools/level_smoke_branch.py its side phase window by window against the
recordings (decomp build/c10/branch, docs/CAPTURES_C10.md "BRANCH"). The
tick log carries the whole player record and the status block
(EM_LOG_AIM_RECORDS=1) and the BRANCH records ("br").

The runs go side by side, then their checks. EM_BRANCH_SIDES=a,b runs only
those.
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get('EM_LEVEL_SMOKE_BIN', str(ROOT / 'build/extermination')))
OUT = ROOT / 'build/level_smoke_branch'
SIDES = ('br_ledge_ammo', 'br_map_item', 'br_elevator_up', 'br_panel_decline', 'br_crate_stack',
         'br_west_ledge', 'br_yard_ammo', 'br_cage_key', 'br_plateau', 'br_roger_talk')
SIDES = tuple(os.environ['EM_BRANCH_SIDES'].split(',')) if os.environ.get('EM_BRANCH_SIDES') else SIDES
CAPTURES = ROOT.parent / 'Extermination/build/c10/branch'


def main():
    assert CAPTURES.is_dir(), f'BRANCH captures missing: {CAPTURES} (decomp docs/CAPTURES_C10.md "BRANCH")'
    OUT.mkdir(parents=True, exist_ok=True)
    runs = {}
    for side in SIDES:
        env = dict(os.environ, EM_UNCAPPED='1', EM_STARTUP_TEST='newgame-level', EM_LEVEL_SMOKE_UNTIL=side,
                   EM_AREA_CHANGE_LOG=str(OUT / f'{side}.jsonl'), EM_RAND_TRACE=str(OUT / f'{side}.rand'),
                   EM_LOG_AIM_RECORDS='1')
        log = open(OUT / f'{side}.log', 'w')
        runs[side] = (subprocess.Popen([str(BIN)], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT), log)
    failed, checks = [], {}
    for side, (proc, log) in runs.items():
        rc = proc.wait(timeout=3600)
        log.close()
        text = (OUT / f'{side}.log').read_text(errors='replace')
        for line in text.splitlines():
            if line.startswith('level smoke:') and (f'{side}: PASS' in line or 'FAIL' in line):
                print(line)
        if rc != 0:
            failed.append((side, 'run', rc))
            continue
        checks[side] = subprocess.Popen([sys.executable, str(ROOT / 'tools/test_level_smoke.py'),
                                         '--log', str(OUT / f'{side}.jsonl'), '--run-log', str(OUT / f'{side}.log'),
                                         '--rand-trace', str(OUT / f'{side}.rand'), '--require-through', side],
                                        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                        env=dict(os.environ, EM_TEST_JOBS='2'))
    for side, check in checks.items():
        out, _ = check.communicate(timeout=3600)
        for line in out.splitlines():
            if line.startswith(f'level smoke: {side}:') or line.startswith('level smoke: PASS') or \
                    line.startswith(f'{side}:') or check.returncode:
                print(line)
        if check.returncode:
            failed.append((side, 'check', check.returncode))
    assert not failed, ('branch side runs failed', failed)
    print(f'level smoke branch: PASS ({len(SIDES)} side runs: {", ".join(SIDES)})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
