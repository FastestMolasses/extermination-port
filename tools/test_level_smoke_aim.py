#!/usr/bin/env python3
"""The aim/fire side runs of the level smoke, in parallel.

Each run plays the main line through truck_crossing, then one side phase
(em_level_smoke_test.c): aim_r1_hold / aim_r2_hold walk to the start of the
AIM captures aim_00_r1_hold / aim_01_r2_hold (decomp docs/CAPTURES_C10.md
"AIM"), hold R1 (R2) for the capture's 79 ticks and run to the capture's last
row (check_aim_hold; LEVEL_SMOKE.md "aim_r1_hold, aim_r2_hold"); the AIM
replays (LEVEL_SMOKE.md "The AIM replays") align on a capture's row f13 and
feed its pad script: aim_fire (aim_03_single_fire: fire and the shots' records),
aim_melee (aim_09_melee: the knife, its reach probe and trail node),
aim_light (aim_08_light_holster: the gun lamp on and off, holstered and
redrawn), aim_world (aim_04_world_hit: the walk and the stick aim, the
rounds into the ground, the pillar, past the fence and a miss) and
aim_cable (aim_10_cable_shots, then aim_11_cable_melee: the rounds at the
cable, the knife at its foot and the cable reaction), and with
EM_TEST_FULL=1 also aim_both (aim_02), aim_reload (aim_06) and
aim_reload_empty (aim_07) (check_aim_replay). The stick replays read their
pad scripts from the files pad_script writes (EM_AIM_PAD_SCRIPT).
tools/test_level_smoke.py checks each run as every level-smoke run (the
main line's phases and the whole-run checks) and its side phase row for row
against its capture; the struck points by direction only (bearing and
elevation, LEVEL_SMOKE.md "The AIM replays").

The original aim / fire path is the only one in AREA11 (AIM_FIRE.md); the
runs set no switch. Duration: each run plays about 5,550 ticks before its
capture's start, because a run can only reach route 08's end by playing the
main line (the port has no state restore). The runs go side by side, then
their checks side by side (EM_TEST_JOBS=2 each): measured 2026-10-02 on the
10-core development machine, 8 min 57 s for the default seven with the
machine's load average near 50 (other sessions' jobs). EM_AIM_SIDES=a,b
runs only those.
"""
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get('EM_LEVEL_SMOKE_BIN', str(ROOT / 'build/extermination')))
OUT = ROOT / 'build/level_smoke_aim'
SIDES = ('aim_r1_hold', 'aim_r2_hold', 'aim_fire', 'aim_melee', 'aim_light', 'aim_world', 'aim_cable')
if os.environ.get('EM_TEST_FULL') == '1':
    SIDES += ('aim_both', 'aim_reload', 'aim_reload_empty')
SIDES = tuple(os.environ['EM_AIM_SIDES'].split(',')) if os.environ.get('EM_AIM_SIDES') else SIDES


# The stick replays' pad scripts: the captures' inputs (trace.json
# 'inputs'), aim_11's after aim_10's at its frame 1448 + f (the seven idle
# frames between the two recordings: their frame counters).
STICK_SCRIPTS = {'aim_world': ('aim_04_world_hit',), 'aim_cable': ('aim_10_cable_shots', 'aim_11_cable_melee')}
AIMFIRE = ROOT.parent / 'Extermination/build/aimfire/capture'


def pad_script(side):
    """Write the side's pad script ("frame buttons lx ly" per line) for
    em_level_smoke_test.c aim_script_begin; returns its path."""
    lines, offset, last_counter = [], 0, None
    for beat in STICK_SCRIPTS[side]:
        path = AIMFIRE / beat / 'trace.json'
        assert path.exists(), f'AIM capture missing: {path} (decomp docs/CAPTURES_C10.md "AIM")'
        trace = json.loads(path.read_text())
        if last_counter is not None:
            offset += trace['first_counter'] - last_counter
        lines += [f"{i['f'] + offset} {i['buttons']:x} {i['lx']} {i['ly']}" for i in trace['inputs']]
        offset += trace['rows'][-1]['f']
        last_counter = trace['last_counter']
    out = OUT / f'{side}.pad'
    out.write_text('\n'.join(lines) + '\n')
    return out


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    runs = {}
    for side in SIDES:
        env = dict(os.environ, EM_UNCAPPED='1', EM_STARTUP_TEST='newgame-level', EM_LEVEL_SMOKE_UNTIL=side,
                   EM_AREA_CHANGE_LOG=str(OUT / f'{side}.jsonl'), EM_RAND_TRACE=str(OUT / f'{side}.rand'))
        if side in STICK_SCRIPTS:
            env['EM_AIM_PAD_SCRIPT'] = str(pad_script(side))
        log = open(OUT / f'{side}.log', 'w')
        runs[side] = (subprocess.Popen([str(BIN)], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT), log)
    failed, checks = [], {}
    for side, (proc, log) in runs.items():
        rc = proc.wait(timeout=1800)
        log.close()
        text = (OUT / f'{side}.log').read_text(errors='replace')
        for line in text.splitlines():
            if line.startswith('level smoke:') and (side in line or 'FAIL' in line or 'PASS' in line):
                print(line)
        if rc != 0:
            failed.append((side, 'run', rc))
            continue
        # the checks side by side as well (EM_TEST_JOBS=2 inside each:
        # they are the parallel work here)
        checks[side] = subprocess.Popen([sys.executable, str(ROOT / 'tools/test_level_smoke.py'),
                                         '--log', str(OUT / f'{side}.jsonl'), '--run-log', str(OUT / f'{side}.log'),
                                         '--rand-trace', str(OUT / f'{side}.rand'), '--require-through', side],
                                        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                        env=dict(os.environ, EM_TEST_JOBS='2'))
    for side, check in checks.items():
        out, _ = check.communicate(timeout=1800)
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
