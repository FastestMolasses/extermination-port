#!/usr/bin/env python3
"""ASan/UBSan contract fixture for canonical equipment fields and boundaries."""
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/aim-fire/equipment-live-test'

def main():
    OUT.parent.mkdir(parents=True, exist_ok=True)
    flags = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
             '-fsanitize=address,undefined', '-ffunction-sections', '-fdata-sections',
             '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections', '-Isrc']
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + flags +
                   ['tests/aim_fire_equipment_live_test.c', 'src/game/em_actor_pool.c',
                    'src/game/em_player_equipment.c',
                    '-lm', '-o', str(OUT)], cwd=ROOT, check=True)
    env = dict(os.environ)
    env['UBSAN_OPTIONS'] = env.get('UBSAN_OPTIONS', '') + ':halt_on_error=1'
    subprocess.run([str(OUT)], cwd=ROOT, env=env, check=True)

if __name__ == '__main__':
    main()
