#!/usr/bin/env python3
"""ASan/UBSan contract fixture for the existing effects owner's new aim APIs."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/aim-fire/effects-live-test'
SOURCES=['tests/aim_fire_effects_live_test.c','src/game/em_effect_original.c',
         'src/game/em_effect_kinds.c','src/game/em_head_sprite_original.c',
         'src/game/em_packet_chain_original.c','src/game/em_actor_pool.c',
         'src/game/em_status_ui_leftovers.c']
def main():
    OUT.parent.mkdir(parents=True,exist_ok=True)
    flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
           '-ffp-contract=off','-ffunction-sections','-fdata-sections',
           '-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections','-Isrc']
    subprocess.run(shlex.split(os.environ.get('CC','cc'))+flags+SOURCES+['-lm','-o',str(OUT)],cwd=ROOT,check=True)
    env=dict(os.environ);env['UBSAN_OPTIONS']=env.get('UBSAN_OPTIONS','')+':halt_on_error=1'
    subprocess.run([str(OUT)],cwd=ROOT,env=env,check=True)
if __name__=='__main__':main()
