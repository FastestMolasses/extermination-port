#!/usr/bin/env python3
"""Exercise the opt-in exploration input boundary and honest result reporting.

This is a harness contract test, not a gameplay or original-code oracle.
"""
import json
from pathlib import Path
import struct
import subprocess

import test_area01_exploration as explore

ROOT = Path(__file__).resolve().parents[1]

DRIVER = r'''
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct {int current;} t;
static const struct {const char *name;} k_phases[]={{"a01_s3"}};
static struct {float pos[3],yaw;} g;
static struct {int d810700;} scene={1};
static unsigned counter,failed,pads,held;
static void fail(const char *why){++failed;fprintf(stderr,"FAIL %s\n",why);}
static unsigned em_frame_counter(void){return counter;}
static const void *scene_ptr(void){return &scene;}
#define em_scene_state() ((const typeof(scene) *)scene_ptr())
static void pad_apply(uint16_t buttons,float x,float y){(void)x;(void)y;++pads;if(buttons)++held;}
static float nav_stick_toward(float x,float z,float m){
    (void)m;float d=hypotf(x-g.pos[0],z-g.pos[2]);g.pos[0]=x;g.pos[2]=z;return d;
}
#include "game/em_area01_exploration_test.h"
int main(int argc,char **argv){
    if(argc!=2)return 2;
    setenv("EM_AREA01_EXPLORE_SCRIPT",argv[1],1);
    setenv("EM_AREA01_EXPLORE_PHASE","a01_s0",1);
    if(a01_explore_begin()!=0 || a01_explore.active || failed)return 3;
    setenv("EM_AREA01_EXPLORE_PHASE","a01_s3",1);
    if(!a01_explore_begin())return 4;
    for(counter=1;!failed && counter<100;++counter)
        if(a01_explore_frame()){printf("DONE pads=%u held=%u\n",pads,held);return 0;}
    return failed ? 1 : 5;
}
'''


def main():
    out = ROOT / 'build/level2-crashes/exploration-harness'
    out.mkdir(parents=True, exist_ok=True)
    source, binary, script = out/'driver.c', out/'driver', out/'input'
    source.write_text(DRIVER)
    subprocess.run(['clang', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I'+str(ROOT/'src'),
                    str(source), '-o', str(binary)], check=True)
    valid = 'EMAX 1\nmove 1 0 .5 1 5\nhold 3 4000 128 128\nface 0 5\nmark finished\n'
    script.write_text(valid)
    result = subprocess.run([str(binary), str(script)], capture_output=True, text=True)
    assert result.returncode == 0 and 'held=3' in result.stdout, result
    assert 'MARK finished' in result.stderr and 'COMPLETE' in result.stderr
    invalid = ['EMAX 2\nhold 1 0 128 128\n', 'EMAX 1\n',
               'EMAX 1\nmove nan 0 1 1 10\n', 'EMAX 1\nmove 1 0 1 0 10\n',
               'EMAX 1\nhold 3 10000 128 128\n', 'EMAX 1\nhold 0 0 128 128\n',
               'EMAX 1\nhold 3 0 256 128\n', 'EMAX 1\nmark extra words\n',
               'EMAX 1\nface 0 5 trailing\n', 'EMAX 1\nunknown\n']
    for text in invalid:
        script.write_text(text)
        result = subprocess.run([str(binary), str(script)], capture_output=True, text=True)
        assert result.returncode == 1 and 'malformed' in result.stderr, (text, result)
    script.write_text('EMAX 1\nface 2 1\n')
    result = subprocess.run([str(binary), str(script)], capture_output=True, text=True)
    assert result.returncode == 1 and 'BLOCKED' in result.stderr, result

    ticks = out/'ticks.jsonl'
    bits = lambda x: struct.unpack('<I', struct.pack('<f', x))[0]
    post = bytearray(99);post[96:99] = bytes((1, 0, 4))
    row = dict(tick=1, counter=100, player=[0, 0, 0, 0], water=[0x5B, 2, 0, 0],
               pos_post=list(map(bits, (0., -40., -1000.))), post=post.hex(),
               a01=dict(hp=bits(95.), progress=['00']))
    ticks.write_text(json.dumps(row)+'\n')
    case = dict(script=[], claims=['shallow-water', 'deep-water', 'fire'])
    log = 'AREA01 explore: BEGIN phase=a01_s3 counter=100 steps=1\nAREA01 explore: COMPLETE\n'
    r = explore.analyze(log, ticks, case, 0)
    assert r['status'] == 'TARGET-NOT-OBSERVED' and r['input_completed'] and r['coverage']['deep-water'] == 'OBSERVED'
    assert r['coverage']['shallow-water'] == r['coverage']['fire'] == 'INPUT-COMPLETED-UNVERIFIED'
    r = explore.analyze(log+'em_scene: FAULT first\nlater faulted\n', ticks, case, 1)
    assert r['status'] == 'FAULT' and r['first_fault'] == 'em_scene: FAULT first'
    r = explore.analyze('level smoke: PASS\n', ticks, case, 0)
    assert r['status'] == 'INCOMPLETE' and not r['observations']['ticks']
    r = explore.analyze(log.replace('100', '101'), ticks, case, 0)
    assert r['coverage']['deep-water'] == 'INPUT-COMPLETED-UNVERIFIED'
    r = explore.analyze(log, ticks, case, None, True)
    assert r['status'] == 'TIMEOUT'
    print(f'AREA01 exploration harness: PASS input boundary (ASan/UBSan), {len(invalid)} malformed scripts, '
          'bounded navigation failure, phase isolation, and five reporting contracts; no gameplay parity claim')


if __name__ == '__main__':
    main()
