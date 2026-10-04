#!/usr/bin/env python3
"""Recorded AREA01 pad replay through the ordinary New Game route.

Use --prepare to export only input scripts. AREA01 runs its bound owners
in every run; an original without an owner faults where it is reached.
No snapshot, player placement or captured game state is imported.
"""
import argparse
import contextlib
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import time

from level_smoke_area01 import (MAIN_BEATS, SIDE_BEATS, ROUTE_PHASES, SOURCES,
                               phase_path, prepare_pads, route_capture)

ROOT = Path(__file__).resolve().parents[1]


def verify_harness(out):
    """Execute the actual C selector/reader/driver over the recorded inputs.

    Only the test driver's game-facing services are mocked. No original
    gameplay state is installed, and this is not a native gameplay proof.
    """
    source = (ROOT/'src/game/em_level_smoke_test.c').read_text()
    phases = re.findall(r'\{"(a01_[^"]+)".*?a01_(?:arrival|route)_begin,\s*'
                        r'a01_(?:arrival|route)_frame,\s*0,\s*(\d+),\s*(\d+),\s*1\}', source, re.S)
    assert [p[0] for p in phases] == ['a01_arrival', *ROUTE_PHASES], ('C/Python phase order', phases)
    rows = ',\n'.join('{"%s",%s,%s}' % row for row in phases)
    selector = source[source.index('static int side_played('):source.index('static void report_not_live(')]
    driver = source[source.index('enum { A01_PAD_MAX'):source.index('/* ------------------------------------------------------------- driver */')]
    prefix = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { const char *name; int side, from_side; } Phase;
static const Phase k_phases[]={PHASE_ROWS};
static struct { int current,until,frames; } t;
typedef struct { int f; uint16_t buttons; uint8_t lx,ly; } AimStick;
typedef struct { unsigned d810700,d810701,d810702; } EmSceneState;
enum { EM_SCENE_TASK_08=8,EM_SCENE_TASK_09,EM_SCENE_TASK_0A,EM_SCENE_TASK_0B };
static EmSceneState scene={1,0,4};
static unsigned counter,buttons,failed,exiting;
static float lx,ly;
static void pad_apply(unsigned b,float x,float y){buttons=b;lx=x;ly=y;}
static void fail(const char *why){fprintf(stderr,"FAIL %s\n",why);failed=1;}
static unsigned em_frame_counter(void){return counter;}
static const EmSceneState *em_scene_state(void){return &scene;}
static unsigned task_byte(unsigned off){return exiting ? (off==8?3:off==9?1:0):0;}
'''.replace('PHASE_ROWS', rows)
    suffix = r'''
int main(int argc,char **argv){
    if(argc!=3)return 2;
    unsigned n=sizeof k_phases/sizeof k_phases[0];
    for(t.until=0;t.until<(int)n && strcmp(k_phases[t.until].name,argv[2]);++t.until){}
    if(t.until==(int)n)return 2;
    if(!strcmp(argv[1],"path")){
        for(int i=0;i<=t.until;++i)if(!k_phases[i].side || side_played(i))puts(k_phases[i].name);
        return 0;
    }
    t.current=t.until;a01_route_begin();
    if(failed)return 1;
    for(counter=1;counter<21000;++counter){
        if(!strcmp(argv[2],"a01_07") && t.frames+1==s_a01_last_frame){scene=(EmSceneState){0};exiting=1;}
        int done=a01_route_frame();
        printf("%u %u %a %a\n",counter,buttons,(double)lx,(double)ly);
        if(failed)return 1;
        if(done)return 0;
    }
    return 2;
}
'''
    out.mkdir(parents=True, exist_ok=True)
    cfile, binary = out/'driver.c', out/'driver'
    cfile.write_text(prefix+selector+driver+suffix)
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    str(cfile),'-o',str(binary)], check=True)
    pads=out/'pads'
    changes=prepare_pads(pads,True)
    manifest=json.loads((pads/'manifest.json').read_text())
    env=dict(os.environ,EM_LEVEL2_PAD_DIR=str(pads))
    submitted=0
    for entry in manifest:
        phase=entry['phase']
        path=subprocess.run([str(binary),'path',phase],capture_output=True,text=True,check=True).stdout.splitlines()
        assert path==list(phase_path(phase)), (phase,'native branch path',path,phase_path(phase))
        assert path[-2]==SOURCES[phase], (phase,'wrong native source',path)
        result=subprocess.run([str(binary),'drive',phase],env=env,capture_output=True,text=True,check=True)
        actual=[(int(c),int(b),float.fromhex(x),float.fromhex(y)) for c,b,x,y in
                (line.split() for line in result.stdout.splitlines())]
        frames,gap=entry['endpoint_row'],entry['gap']
        assert len(actual)==frames+gap, (phase,'driver endpoint',len(actual),frames,gap)
        commands=route_capture(phase)['inputs']
        chosen,index=(0,0.,0.),0
        for c,b,x,y in actual:
            f=c-gap
            while index<len(commands) and commands[index]['f']<=f-2:
                cmd=commands[index]
                chosen=(cmd['buttons'],(cmd['lx']-128)/128.,(cmd['ly']-128)/128.)
                index+=1
            expected=(0,0.,0.) if f==frames else chosen
            assert (b,x,y)==expected, (phase,f,'pad command/order',actual[c-1],expected)
        submitted+=len(actual)
    # A valid prefix cannot disguise a truncated final command at EOF.
    path=pads/'a01_s0.pad'
    original=path.read_text()
    bad=('EMA1 1 10 1\n0 0 127 127\n1 0\n',
         'EMA1 1 10 1\n2 0 127 127\n1 0 127 127\n',
         'EMA1 1 10 1\n0 0 127 256\n')
    try:
        for content in bad:
            path.write_text(content)
            result=subprocess.run([str(binary),'drive','a01_s0'],env=env,capture_output=True,text=True)
            assert result.returncode==1 and 'FAIL ' in result.stderr, ('invalid input accepted',content,result)
    finally:
        path.write_text(original)
    print(f'AREA01 harness: PASS 16 C branch paths, {changes} commands, {submitted} C pad frames, '
          f'{len(bad)} malformed input rejections (ASan/UBSan; no gameplay parity claim)')
    verify_checker()


def verify_checker():
    """Positive capture-shaped fixtures and negative strict-comparison cases."""
    import level_smoke_area01 as a01
    import test_level_smoke as smoke

    def bits(value):
        return struct.unpack('<I',struct.pack('<f',value))[0]

    def camblk(row, ram=None):
        """The tick log's camera bytes: the recording's decoded block fields
        on every row, the saved RAM's bytes on a recording's last row."""
        if ram is not None:
            return [ram[a:a+n].hex() for a,n in (a01.CAMERA_BLOCK,a01.CAMERA_FORWARD,a01.CAMERA_690)]
        block=bytearray(0xD0)
        block[4:8]=bytes.fromhex(row['cam_mode'])
        block[0x10:0x1C]=struct.pack('<3f',*row['cam_eye'])
        block[0x20:0x2C]=struct.pack('<3f',*row['cam_tgt'])
        return [block.hex(),struct.pack('<3fI',*row['fwd'],0).hex(),bytes(20).hex()]

    def native_row(row, ram=None):
        post=bytearray(smoke.tsr.SNAP)
        post[:5]=bytes.fromhex(row['slots'])[8:13]
        for address,value in ((0x8106B0,row['req']),(0x810700,row['area4'][:6]),(smoke.SPAD,row['spad'])):
            start=smoke.tsr.OFFSET[address]
            post[start:start+len(value)//2]=bytes.fromhex(value)
        owners={str(address):[row[name]['h'],struct.pack('<3f',*row[name]['pos']).hex(),
                             row[name]['s1F0'],row[name]['t2DC'],int(row[name]['cb'],16)]
                for name,address in a01.OWNERS.items()}
        return dict(counter=row['counter'],tick=row['f'],post=post.hex(),
                    player=[row['p5'],row['m1F0'],row['m1F1'],row['clip'],bits(row['clock']),
                            int(row['ground'],16),row['b2F3']],
                    pos_post=list(map(bits,row['pos'])),yaw_post=bits(row['yaw']),
                    eye_post=list(map(bits,row['eye'])),tgt_post=list(map(bits,row['tgt'])),
                    cam4=row['cam_mode'],screen8=row['screen'][:16],power=row['power'],camblk=camblk(row,ram),
                    a01=dict(hp=bits(row['hp']),progress=[row['d2'],row['story758'],row['taken'],row['docs']],owners=owners))

    total,rejected=0,0
    for phase in (*MAIN_BEATS,*SIDE_BEATS):
        if phase=='a01_07':
            continue  # Its loader's original-instruction proof belongs to the live run.
        capture=a01.route_capture(phase)
        rows=capture['rows']
        gap=a01.source_gap(phase,capture)
        ticks=[dict(counter=rows[0]['counter']-gap+k+1) for k in range(gap-1)]
        path=a01.capture_path(phase)
        ram=path.with_name('eeMemory.bin').read_bytes()
        ticks.extend(native_row(row,ram if row is rows[-1] else None) for row in rows)
        ticks.append({})
        start=gap-1
        for i,row in enumerate(rows):
            ticks[start+i+1].update(msg_pre=struct.unpack('<3I',bytes.fromhex(row['msg'])[:12]),fade8=row['fade'][:16])
        state=dict(cursor=0,area01_ends={SOURCES[phase]:rows[0]['counter']-gap})
        run=f'level smoke: {phase}: aligned counter={rows[0]["counter"]}\n'
        with contextlib.redirect_stdout(io.StringIO()):
            a01.check_route(smoke,ticks,run,state,phase)
        assert state['cursor']==len(ticks)-1 and state['area01_ends'][phase]==rows[-1]['counter']
        total+=len(rows)
        for field in ('owner','camera','camera_block','progress','clock','health','missing_owner'):
            pair=copy.deepcopy(ticks[start:start+2])
            tick=pair[0]
            if field=='owner':
                owner=next(iter(tick['a01']['owners'].values()))
                owner[4]^=4
            elif field=='camera':tick['cam4']='ff'+tick['cam4'][2:]
            elif field=='camera_block':
                block=bytearray.fromhex(tick['camblk'][0]);block[0x27]^=0x40;tick['camblk'][0]=block.hex()
            elif field=='progress':tick['a01']['progress'][0]='ff'+tick['a01']['progress'][0][2:]
            elif field=='clock':tick['player'][4]=bits(rows[0]['clock']+1)
            elif field=='health':tick['a01']['hp']=bits(rows[0]['hp']+1)
            else:tick['a01']['owners'].clear()
            try:a01.compare_route_row(smoke,pair,0,rows[0],phase)
            except AssertionError:rejected+=1
            else:raise AssertionError((phase,'checker accepted corrupt '+field))
        # The last row's whole camera block against the saved RAM: a byte
        # no per-row field covers (+0x40) must fail the phase.
        end=copy.deepcopy(ticks)
        block=bytearray.fromhex(end[start+len(rows)-1]['camblk'][0]);block[0x40]^=1
        end[start+len(rows)-1]['camblk'][0]=block.hex()
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                a01.check_route(smoke,end,run,dict(cursor=0,area01_ends={SOURCES[phase]:rows[0]['counter']-gap}),phase)
        except AssertionError:rejected+=1
        else:raise AssertionError((phase,'checker accepted a corrupt last camera block'))
        for delta in (1,-1):
            bad=dict(cursor=0,area01_ends={SOURCES[phase]:rows[0]['counter']-gap+delta})
            try:a01.route_start(ticks,run,bad,phase,capture)
            except AssertionError:rejected+=1
            else:raise AssertionError((phase,'checker accepted wrong source counter',delta))
    print(f'AREA01 checker contract: PASS {total} capture-shaped rows and {rejected} rejected '
          'field/source corruptions (no native gameplay parity claim)')


def run_path(out):
    return out / 'run.log'


def prefix_report(out, run_log, phase):
    """After a failed run: how many of the phase's recorded rows the native
    ticks matched exactly (every field compare_route_row checks) before the
    first difference or the end of the log. A diagnostic, never a PASS."""
    import level_smoke_area01 as a01
    import test_level_smoke as smoke
    if phase not in MAIN_BEATS or phase == 'a01_07' or not (out / 'ticks.jsonl').exists():
        return
    run = run_log.read_text(errors='replace')
    match = re.search(rf'^level smoke: {phase}: aligned counter=(\d+)', run, re.M)
    if not match:
        return
    ticks = [t for t in (json.loads(line) for line in (out / 'ticks.jsonl').open()) if 'tick' in t]
    counter = int(match.group(1))
    start = next((i for i, t in enumerate(ticks) if t['counter'] == counter), None)
    if start is None:
        return
    rows = a01.route_capture(phase)['rows']
    exact, first = 0, None
    for k, row in enumerate(rows):
        if start + k + 1 >= len(ticks) or ticks[start + k]['counter'] != counter + k:
            break
        try:
            a01.compare_route_row(smoke, ticks, start + k, row, phase)
        except AssertionError as e:
            first = e.args[0]
            break
        exact += 1
    print(f'{phase}: NOT PASSED; {exact} of {len(rows)} recorded rows exact before the stop'
          + (f'; first difference {first}' if first else ' (the run stopped)'), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--verify-harness', action='store_true',
                        help='test the actual C branch/input driver against all 16 captures, then exit')
    parser.add_argument('--include-side-pads',action='store_true',
                        help='also export all eight side input files/provenance (automatic with --side)')
    endpoint = parser.add_mutually_exclusive_group()
    endpoint.add_argument('--until', choices=('a01_arrival', *MAIN_BEATS),
                          help='main route endpoint (default: a01_arrival)')
    endpoint.add_argument('--side', choices=SIDE_BEATS,
                          help='ordinary New Game through this side\'s source, then its recorded pad route')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/level2/smoke')
    parser.add_argument('--bin', type=Path, default=Path(os.environ.get('EM_LEVEL_SMOKE_BIN', ROOT / 'build/extermination')))
    args = parser.parse_args()
    until = args.side or args.until or 'a01_arrival'
    out = args.out.resolve()
    if args.verify_harness:
        verify_harness(out/'harness')
        return 0
    count = prepare_pads(out / 'pads',args.include_side_pads or bool(args.side))
    side_count=len(SIDE_BEATS) if args.include_side_pads or args.side else 0
    print(f'AREA01: exported {count} recorded pad changes across {len(MAIN_BEATS)} main beats'
          f' and {side_count} side input files; path: {" -> ".join(phase_path(until))}', flush=True)
    if args.prepare:
        return 0
    binary = args.bin.resolve()
    env = dict(os.environ, EM_UNCAPPED='1', EM_STARTUP_TEST='newgame-level',
               EM_LEVEL_SMOKE_UNTIL=until, EM_LEVEL2_PAD_DIR=str(out / 'pads'),
               EM_AREA_CHANGE_LOG=str(out / 'ticks.jsonl'), EM_RAND_TRACE=str(out / 'rand.trace'),
               EM_LEVEL_SMOKE_BIN=str(binary))
    started = time.monotonic()
    receipt = dict(binary=str(binary), binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                   until=until, side=args.side, area01_path=phase_path(until), pad_changes=count,
                   pad_manifest=str(out/'pads/manifest.json'),side_input_files=side_count)
    with (out / 'run.log').open('w') as log:
        result = subprocess.run([str(binary)], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                timeout=1800)
    receipt.update(run_returncode=result.returncode, seconds=round(time.monotonic()-started, 3))
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    for line in (out / 'run.log').read_text(errors='replace').splitlines():
        if line.startswith(('level smoke:', 'em_area01:', 'em_scene: FAULT')):
            print(line, flush=True)
    if result.returncode:
        prefix_report(out, run_path(out), until)
        return result.returncode
    with (out / 'check.log').open('w') as log:
        check = subprocess.run([sys.executable, str(ROOT / 'tools/test_level_smoke.py'),
                                '--log', str(out / 'ticks.jsonl'), '--run-log', str(out / 'run.log'),
                                '--rand-trace', str(out / 'rand.trace'), '--require-through', until],
                               cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=1800)
    receipt['check_returncode'] = check.returncode
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print((out / 'check.log').read_text())
    return check.returncode


if __name__ == '__main__':
    sys.exit(main())
