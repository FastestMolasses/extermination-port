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
static void pad_apply(uint16_t buttons,float x,float y){(void)x;(void)y;++pads;if(buttons)++held;printf("PAD %u %04x\n",counter,buttons);}
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
    script.write_text('EMAX 1\nhold 3 800 128 128\nhold 2 2800 128 128\nhold 4 800 128 128\n')
    result = subprocess.run([str(binary), str(script)], capture_output=True, text=True)
    actual = [(int(fields[1]), int(fields[2], 16)) for line in result.stdout.splitlines()
              if (fields := line.split())[0] == 'PAD']
    assert result.returncode == 0 and actual[:9] == list(enumerate([0x800]*3+[0x2800]*2+[0x800]*4, 1)), result
    assert 'held=9' in result.stdout
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

    # Captured tails are loaded only when selected, preserve input edges,
    # include the last hold, and never mutate the authored prefix.
    capture_reader = explore.route_capture
    try:
        explore.route_capture = lambda phase: dict(frames=15, inputs=[
            dict(f=0, buttons=0, lx=127, ly=127),
            dict(f=6, buttons=0x4000, lx=127, ly=127),
            dict(f=8, buttons=0, lx=127, ly=0)])
        prefix = dict(script=['mark authored'], recorded_tail=('a01_s5', 6))
        assert explore.case_script(prefix) == ['mark authored', 'mark vent-tail-start',
            'hold 2 4000 127 127', 'hold 7 0 127 0', 'mark vent-recorded-tail-end']
        assert prefix['script'] == ['mark authored']
        assert explore.case_script(dict(script=['mark override'])) == ['mark override']
    finally:
        explore.route_capture = capture_reader

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
    primary = 'AREA11 interaction: 0020E060: D_008106D0 is not the bound panel failed at frame737'
    r = explore.analyze(log+primary+'\nlevel smoke: FAIL phase=a01_s4\n', ticks, case, 1)
    assert r['status'] == 'FAULT' and r['first_fault'] == primary
    primary = 'player closure: reached 00188610 which has no translation on the live path'
    r = explore.analyze(log+primary+'\nem_scene: FAULT later\n', ticks, case, 1)
    assert r['status'] == 'FAULT' and r['first_fault'] == primary
    primary = 'em_area01: owner 001C02E0 node 007A93F0 failed at 70003B64'
    r = explore.analyze(log+primary+'\nlevel smoke: FAIL phase=a01_s5\n', ticks, case, 1)
    assert r['status'] == 'FAULT' and r['first_fault'] == primary
    r = explore.analyze('level smoke: PASS\n', ticks, case, 0)
    assert r['status'] == 'INCOMPLETE' and not r['observations']['ticks']
    r = explore.analyze(log.replace('100', '101'), ticks, case, 0)
    assert r['coverage']['deep-water'] == 'INPUT-COMPLETED-UNVERIFIED'
    r = explore.analyze(log, ticks, case, None, True)
    assert r['status'] == 'TIMEOUT'
    print(f'AREA01 exploration harness: PASS input boundary (ASan/UBSan), {len(invalid)} malformed scripts, '
          'continuous R1/trigger holds, runtime pad-tail loading, bounded navigation failure, phase isolation, and eight reporting contracts; no gameplay parity claim')


if __name__ == '__main__':
    main()
