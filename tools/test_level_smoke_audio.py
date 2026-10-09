#!/usr/bin/env python3
"""The AUDIO side runs of the level smoke, in parallel (chain step AUDIO;
FIRST_LEVEL_AUDIT.md section 1b item 1; LEVEL_SMOKE.md "The sound state").

Each run plays the main line to the side phase's source, then the decomp's
designed audio beat (em_level_smoke_test.c "aud_*": the capture tool's own
walk_path and idle frames, build/s87/audio/tools/audio_capture.py):

  aud_walk_outdoor  (from slide)          walk_outdoor: the low ground, 120
                                          idle ticks
  aud_walk_room     (from fence_door)     walk_room: behind the fence door,
                                          120 idle ticks
  aud_flame         (from crevice_prompt) flame: toward the flame, 300 ticks
                                          standing, away east, 120 idle ticks

tools/test_level_smoke.py checks each run as every level-smoke run, and
tools/level_smoke_audio.py its beat against the audio capture (decomp
build/s87/audio, docs/CAPTURES_AUDIO.md): the looped services and the
sustained voices row for row.

The runs go side by side, then their checks. EM_AUDIO_SIDES=a,b runs only
those.
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get('EM_LEVEL_SMOKE_BIN', str(ROOT / 'build/extermination')))
OUT = ROOT / 'build/level_smoke_audio'
SIDES = ('aud_walk_outdoor', 'aud_walk_room', 'aud_flame')
SIDES = tuple(os.environ['EM_AUDIO_SIDES'].split(',')) if os.environ.get('EM_AUDIO_SIDES') else SIDES
CAPTURES = ROOT.parent / 'Extermination/build/s87/audio'


def main():
    assert CAPTURES.is_dir(), f'audio captures missing: {CAPTURES} (decomp docs/CAPTURES_AUDIO.md)'
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
            if line.startswith(f'{side}:') or line.startswith('audio:') or \
                    line.startswith('level smoke: PASS') or check.returncode:
                print(line)
        if check.returncode:
            failed.append((side, 'check', check.returncode))
    assert not failed, ('audio side runs failed', failed)
    print(f'level smoke audio: PASS ({len(SIDES)} side runs: {", ".join(SIDES)})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
