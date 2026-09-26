#!/usr/bin/env python3
"""Build a bounded native render fixture from recovered original hub commands.

This fixture checks original artwork and 2D primitive rasterization. The
status models and live page lifecycle are explicitly outside this image.
It never binds a frozen snapshot into the game or launches without --run.
"""
import argparse
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def array(values,real=False):
    return '{'+','.join((repr(float(x))+'f') if real else str(x)+'u' for x in values)+'}'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run',action='store_true',help='launch only after reserving the shared GPU')
    args=parser.parse_args()
    data=json.loads((ROOT/'assets/scene_snow/panel/status_hub_commands.json').read_text())
    lines=[]
    for c in data['fixture']:
        kind,mode=c['kind'],c['mode']
        if kind=='sprite':
            lines.append(f"sprite({mode},{','.join(map(str,c['xywh']))},{c['rgba']}u,0x{c['tex0']:016x}ULL);")
        elif kind=='rectangle':
            lines.append(f"rectangle({mode},{','.join(map(str,c['xyxy']))},{c['rgba']}u);")
        elif kind=='arc':
            lines.append(f"arc({mode},(const float[24]){array(c['descriptor'],True)});")
        elif kind=='line_strips':
            for strip in c['strips']:
                for a,b in zip(strip,strip[1:]):
                    lines.append(f"line({mode},(const uint32_t[6]){array(a)},(const uint32_t[6]){array(b)});")
        elif kind=='text':
            lines.append(f"text_original({int(c['proportional'])},{','.join(map(str,c['xywh']))},{json.dumps(c['value'])},{c['style'][0]}u);")
        elif kind=='analog_trail':
            lines.append(f"trail({c['xy'][0]}f,{c['xy'][1]}f);")
        else:raise AssertionError(kind)
    output=ROOT/'build/status_hub_visual';output.mkdir(parents=True,exist_ok=True)
    generated=output/'commands.inc';generated.write_text('\n'.join(lines)+'\n')
    binary=output/'fixture'
    command=['clang','-O2','-Wall','-Wextra','-Isrc',
             f'-DEM_STATUS_HUB_COMMANDS="{generated}"',
             'tests/status_hub_visual.c','src/game/em_hud.c','src/game/em_item_geometry.c',
             'src/game/em_item_sdk_math.c','src/game/em_item_trail.c',
             'src/game/em_interaction_scan.c','src/game/em_random.c',
             'src/game/em_status_background.c','src/game/em_status_background_draw.c',
             'src/game/em_sdk_math_original.c',
             'src/game/em_packet_chain_original.c','src/game/em_status_ui_leftovers.c',
             'src/game/em_object_unit.c',
             'src/platform/mac/em_platform_mac.m','src/platform/mac/em_gamepad_mac.m',
             'src/gfx/metal/em_gfx_metal.m','-framework','Cocoa','-framework','Metal',
             '-framework','QuartzCore','-framework','GameController','-Wl,-dead_strip',
             '-o',str(binary)]
    subprocess.run(command,cwd=ROOT,check=True)
    if args.run:subprocess.run([str(binary),str(output/'hub.bmp')],cwd=ROOT,check=True)
    print(json.dumps({'fixture':str(binary),'original_worker_commands':len(data['fixture']),
                      'boundaries':'status models; moving background phase; font and GS/Metal rasterization'}))

if __name__=='__main__':main()
