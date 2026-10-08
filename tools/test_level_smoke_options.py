#!/usr/bin/env python3
"""The OPTIONS side runs of the level smoke, in parallel (docs/OPTIONS.md
section 6; LEVEL_SMOKE.md "The OPTIONS side runs").

Each run plays the main line to truck_crossing (the end of route beat 08,
the recordings' source snapshot), then one side phase opt_NN
(em_level_smoke_test.c, the capture lane OPTIONS's closed-loop policies):
opt_00 browse and close, opt_01 vibration, opt_02 sound, opt_03 screen
position, opt_04 brightness, opt_05 button config, opt_06 default,
opt_07 the load row's card screen to its slot choice, opt_08 the quit
prompt. tools/test_level_smoke.py checks each run as every level-smoke run,
and tools/level_smoke_options.py its beat against the recording (decomp
build/c10/options/<beat>/trace.json, docs/CAPTURES_C10.md "OPTIONS").

EM_OPTIONS_SIDES=opt_00,opt_03 runs only those; EM_TEST_JOBS=n bounds the
runs side by side (default 4).
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import level_smoke_options  # noqa: E402

BIN = Path(os.environ.get('EM_LEVEL_SMOKE_BIN', str(ROOT / 'build/extermination')))
OUT = ROOT / 'build/level_smoke_options'
SIDES = level_smoke_options.PHASES
SIDES = tuple(os.environ['EM_OPTIONS_SIDES'].split(',')) if os.environ.get('EM_OPTIONS_SIDES') else SIDES
JOBS = max(1, int(os.environ.get('EM_TEST_JOBS', '4')))


def run_side(side):
    env = dict(os.environ, EM_UNCAPPED='1', EM_STARTUP_TEST='newgame-level', EM_LEVEL_SMOKE_UNTIL=side,
               EM_AREA_CHANGE_LOG=str(OUT / f'{side}.jsonl'), EM_RAND_TRACE=str(OUT / f'{side}.rand'))
    log = open(OUT / f'{side}.log', 'w')
    return subprocess.Popen([str(BIN)], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT), log


def main():
    assert level_smoke_options.CAPTURES.is_dir(), \
        f'OPTIONS captures missing: {level_smoke_options.CAPTURES} (decomp docs/CAPTURES_C10.md "OPTIONS")'
    OUT.mkdir(parents=True, exist_ok=True)
    failed, pending, running, checks = [], list(SIDES), {}, {}
    while pending or running:
        while pending and len(running) < JOBS:
            side = pending.pop(0)
            running[side] = run_side(side)
        side = next(iter(running))
        proc, log = running.pop(side)
        rc = proc.wait(timeout=2400)
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
        out, _ = check.communicate(timeout=2400)
        for line in out.splitlines():
            if line.startswith(f'level smoke: {side}:') or line.startswith('level smoke: PASS') or check.returncode:
                print(line)
        if check.returncode:
            failed.append((side, 'check', check.returncode))
    assert not failed, ('options side runs failed', failed)
    print(f'level smoke options: PASS ({len(SIDES)} side runs: {", ".join(SIDES)})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
