#!/usr/bin/env python3
"""The aim camera's side runs of the level smoke, in parallel.

Each run plays the main line through truck_crossing, then the side phase
aim_r1_hold or aim_r2_hold (em_level_smoke_test.c): it walks to the start of
the AIM captures aim_00_r1_hold / aim_01_r2_hold (decomp
docs/CAPTURES_C10.md "AIM"), holds R1 (R2) for the capture's 79 ticks and
runs to the capture's last row. tools/test_level_smoke.py then checks the run
as every level-smoke run (the main line's phases and the whole-run checks)
and the aim phase row for row against its capture (check_aim_hold;
LEVEL_SMOKE.md "aim_r1_hold, aim_r2_hold").

The runs set the aim/fire gate (EM_AIM_FIRE_ORIGINAL=1 with a fixture name,
AIM_FIRE.md section 1): the original stances and the aim camera run only
behind it. About 3 min (two runs side by side, then their checks).
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get('EM_LEVEL_SMOKE_BIN', str(ROOT / 'build/extermination')))
OUT = ROOT / 'build/level_smoke_aim'
SIDES = ('aim_r1_hold', 'aim_r2_hold')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    runs = {}
    for side in SIDES:
        env = dict(os.environ, EM_UNCAPPED='1', EM_STARTUP_TEST='newgame-level', EM_LEVEL_SMOKE_UNTIL=side,
                   EM_AIM_FIRE_ORIGINAL='1', EM_AIM_FIRE_TEST='smoke',
                   EM_AREA_CHANGE_LOG=str(OUT / f'{side}.jsonl'), EM_RAND_TRACE=str(OUT / f'{side}.rand'))
        log = open(OUT / f'{side}.log', 'w')
        runs[side] = (subprocess.Popen([str(BIN)], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT), log)
    failed = []
    for side, (proc, log) in runs.items():
        rc = proc.wait(timeout=900)
        log.close()
        text = (OUT / f'{side}.log').read_text(errors='replace')
        for line in text.splitlines():
            if line.startswith('level smoke:') and (side in line or 'FAIL' in line or 'PASS' in line):
                print(line)
        if rc != 0:
            failed.append((side, 'run', rc))
            continue
        check = subprocess.run([sys.executable, str(ROOT / 'tools/test_level_smoke.py'),
                                '--log', str(OUT / f'{side}.jsonl'), '--run-log', str(OUT / f'{side}.log'),
                                '--rand-trace', str(OUT / f'{side}.rand'), '--require-through', side],
                               cwd=ROOT, capture_output=True, text=True)
        out = check.stdout + check.stderr
        for line in out.splitlines():
            if line.startswith(f'{side}:') or line.startswith('level smoke: PASS') or check.returncode:
                print(line)
        if check.returncode:
            failed.append((side, 'check', check.returncode))
    assert not failed, ('aim side runs failed', failed)
    print(f'level smoke aim: PASS ({len(SIDES)} side runs: {", ".join(SIDES)})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
