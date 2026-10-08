#!/usr/bin/env python3
"""The DAMAGE side runs of the level smoke, in parallel (docs/DAMAGE.md
section 8; LEVEL_SMOKE.md "The DAMAGE side runs").

Each run plays the main line to the side phase's source, then the phase
(em_level_smoke_test.c, the capture lane DAMAGE's closed-loop policies):
dmg_flame (from crevice_prompt: the flame's contacts to 35, to 10 and to 0,
the death, the game over with no input, the title after the death, Up and
Cross, the New Game to first control; the recordings dmg_00..dmg_04),
dmg_load (from crevice_prompt: dmg_flame's way to the title after the death,
then Cross on LOAD GAME, the memory-card screen to its slot choice, Triangle
back to the title menu; the recordings dmg_00..dmg_03 and dmg_05),
dmg_crevice_fall (from crevice_prompt: the walking jump short of the north
block, the landing hit; dmg_06) and dmg_pit_fall (from truck_preview: the
truck's fall, the walk off its roof onto the pit floor, the game over;
dmg_07). tools/test_level_smoke.py checks each run as every level-smoke
run, and tools/level_smoke_damage.py its side phase window by window
against the recordings (decomp build/c10/damage, docs/CAPTURES_C10.md
"DAMAGE").

The runs go side by side, then their checks. dmg_flame plays the intro
movie of its second New Game in full (the recording does: no input there),
like every run under EM_UNCAPPED decoded as fast as the host allows.
EM_DAMAGE_SIDES=a,b runs only those.
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get('EM_LEVEL_SMOKE_BIN', str(ROOT / 'build/extermination')))
OUT = ROOT / 'build/level_smoke_damage'
SIDES = ('dmg_flame', 'dmg_load', 'dmg_crevice_fall', 'dmg_pit_fall')
SIDES = tuple(os.environ['EM_DAMAGE_SIDES'].split(',')) if os.environ.get('EM_DAMAGE_SIDES') else SIDES
CAPTURES = ROOT.parent / 'Extermination/build/c10/damage'


def main():
    assert CAPTURES.is_dir(), f'DAMAGE captures missing: {CAPTURES} (decomp docs/CAPTURES_C10.md "DAMAGE")'
    OUT.mkdir(parents=True, exist_ok=True)
    runs = {}
    for side in SIDES:
        env = dict(os.environ, EM_UNCAPPED='1', EM_STARTUP_TEST='newgame-level', EM_LEVEL_SMOKE_UNTIL=side,
                   EM_AREA_CHANGE_LOG=str(OUT / f'{side}.jsonl'), EM_RAND_TRACE=str(OUT / f'{side}.rand'))
        log = open(OUT / f'{side}.log', 'w')
        runs[side] = (subprocess.Popen([str(BIN)], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT), log)
    failed, checks = [], {}
    for side, (proc, log) in runs.items():
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
    assert not failed, ('damage side runs failed', failed)
    print(f'level smoke damage: PASS ({len(SIDES)} side runs: {", ".join(SIDES)})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
