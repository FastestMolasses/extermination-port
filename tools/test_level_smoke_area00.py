#!/usr/bin/env python3
"""AREA00 smoke from ordinary New Game, with independent original comparisons.

--prepare exports pad input only. --verify-harness checks the C input driver
and Python checker contracts only; neither option proves native gameplay.
The default is the short harness contract check. --run opts into the long
New Game replay; its default endpoint is the arrival and final neutral frames.
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

import drive_replay
import level_smoke_area00 as a00
import level_smoke_area01 as a01
import reference_root as refs

ROOT = Path(__file__).resolve().parents[1]


def verify_harness(out):
    source = (ROOT / 'src/game/em_level_smoke_test.c').read_text()
    phases = re.findall(r'\{"(a00_[^"]+)".*?a01_route_begin,\s*a01_route_frame,\s*0,\s*(\d+),\s*(\d+),\s*1\}', source, re.S)
    assert [p[0] for p in phases] == ['a00_arrival', *a00.ROUTE_PHASES], ('C/Python phase order', phases)
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
static EmSceneState scene={0};
static unsigned counter,buttons,failed,arrived;
static float lx,ly;
static void pad_apply(unsigned b,float x,float y){buttons=b;lx=x;ly=y;}
static void fail(const char *why){fprintf(stderr,"FAIL %s\n",why);failed=1;}
static unsigned em_frame_counter(void){return counter;}
static void em_stream_live_drive_replay_anchor(const char *phase,unsigned c){(void)phase;(void)c;}
static const EmSceneState *em_scene_state(void){return &scene;}
static unsigned task_byte(unsigned off){return arrived ? (off==8?3:off==9?1:0):0;}
'''.replace('PHASE_ROWS', ',\n'.join('{"%s",%s,%s}' % row for row in phases))
    suffix = r'''
int main(int argc,char **argv){
    if(argc!=4)return 2;
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
        if(atoi(argv[3])>=0 && t.frames+1==atoi(argv[3])){scene=(EmSceneState){1,0,0};arrived=1;}
        int done=a01_route_frame();
        printf("%u %u %a %a\n",counter,buttons,(double)lx,(double)ly);
        if(failed)return 1;
        if(done)return 0;
    }
    return 2;
}
'''
    out.mkdir(parents=True, exist_ok=True)
    cfile, binary = out / 'driver.c', out / 'driver'
    cfile.write_text(prefix + selector + driver + suffix)
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(cfile), '-o', str(binary)], check=True)
    changes = a00.prepare_pads(out / 'pads', True)
    manifest = json.loads((out / 'pads/manifest.json').read_text())
    env = dict(os.environ, EM_LEVEL3_PAD_DIR=str(out / 'pads'))
    submitted = 0
    for entry in manifest:
        phase, capture = entry['phase'], a00.route_capture(entry['phase'])
        path = subprocess.run([str(binary), 'path', phase, '-1'], capture_output=True, text=True, check=True).stdout.splitlines()
        assert path == list(a00.phase_path(phase)), (phase, 'C branch path', path)
        boundary = -1
        if phase in a00.RETURN_TICKS:
            boundary = next(r['f'] for r in capture['rows'] if r['area4'] == '01000001' and
                            bytes.fromhex(r['slots'])[8:12] == bytes((3, 1, 0, 0)))
        for shortening in (0, 7) if boundary >= 0 else (0,):
            result = subprocess.run([str(binary), 'drive', phase, str(boundary - shortening)],
                                    env=env, capture_output=True, text=True, check=True)
            actual = [(int(c), int(b), float.fromhex(x), float.fromhex(y)) for c, b, x, y in
                      (line.split() for line in result.stdout.splitlines())]
            frames, gap = entry['endpoint_row'] - shortening, entry['gap']
            assert len(actual) == frames + gap, (phase, 'driver endpoint', len(actual), frames, gap)
            commands = a00.command_rows(phase, capture)
            chosen, index = (0, 0., 0.), 0
            for c, b, x, y in actual:
                frame = c - gap
                while index < len(commands) and commands[index]['f'] <= frame - 2:
                    cmd = commands[index]
                    chosen = (cmd['buttons'], (cmd['lx'] - 128) / 128., (cmd['ly'] - 128) / 128.)
                    index += 1
                expected = (0, 0., 0.) if frame == frames else chosen
                assert (b, x, y) == expected, (phase, frame, 'C pad command/order', (b, x, y), expected)
            submitted += len(actual)
    path = out / 'pads/a00_00.pad'
    original = path.read_text()
    bad = ('EMA1 1 10 1\n0 0 127 127\n1 0\n',
           'EMA1 1 10 1\n2 0 127 127\n1 0 127 127\n', 'EMA1 1 10 1\n0 0 127 256\n')
    try:
        for content in bad:
            path.write_text(content)
            result = subprocess.run([str(binary), 'drive', 'a00_00', '-1'], env=env, capture_output=True, text=True)
            assert result.returncode == 1 and 'FAIL ' in result.stderr, ('malformed input accepted', content)
    finally:
        path.write_text(original)
    print(f'AREA00 harness: PASS 13 C branch paths, {changes} input commands, {submitted} C frames, '
          '2 shortened return loads and 3 malformed input rejections (ASan/UBSan; no gameplay parity claim)')
    verify_checker()


def verify_checker():
    """Original-shaped positive/negative fixtures, never a native playthrough."""
    import test_level_smoke as smoke

    def bits(value):
        return struct.unpack('<I', struct.pack('<f', value))[0]

    def native_row(row):
        post = bytearray(smoke.tsr.SNAP)
        post[:5] = bytes.fromhex(row['slots'])[8:13]
        for address, value in ((0x8106B0, row['req']), (0x810700, row['area4'][:6]), (smoke.SPAD, row['spad'])):
            start = smoke.tsr.OFFSET[address]
            post[start:start + len(value) // 2] = bytes.fromhex(value)
        block = bytearray(0xD0)
        block[4:8] = bytes.fromhex(row['cam_mode'])
        block[0x10:0x1C] = struct.pack('<3f', *row['cam_eye'])
        block[0x20:0x2C] = struct.pack('<3f', *row['cam_tgt'])
        owners = {str(address): [row[name]['h'], struct.pack('<3f', *row[name]['pos']).hex(),
                  row[name]['s1F0'], row[name]['t2DC'], int(row[name]['cb'], 16)]
                  for name, address in a00.OWNERS.items() if name in row}
        return dict(counter=row['counter'], tick=row['f'], post=post.hex(),
            player=[row['p5'], row['m1F0'], row['m1F1'], row['clip'], bits(row['clock']), int(row['ground'], 16), row['b2F3']],
            pos_post=list(map(bits, row['pos'])), yaw_post=bits(row['yaw']), eye_post=list(map(bits, row['eye'])),
            tgt_post=list(map(bits, row['tgt'])), cam4=row['cam_mode'], screen8=row['screen'][:16], power=row['power'],
            camblk=[block.hex(), struct.pack('<3fI', *row['fwd'], 0).hex(), bytes(20).hex()],
            a00=dict(hp=bits(row['hp']), progress=[row['d2'], row['story758'], row['taken'], row['docs']],
                     owners=owners, locks=row.get('locks'), wpn=row.get('wpn')))

    count = rejected = 0
    for phase in ('a00_arrival', *a00.ROUTE_PHASES):
        capture = a00.route_capture(phase)
        rows = capture['rows'][a00.ARRIVAL_ROW:] if phase == 'a00_arrival' else capture['rows']
        ticks = [native_row(r) for r in rows] + [{}]
        for k, row in enumerate(rows):
            loader = bytearray(27)
            loader[1:5] = bytes.fromhex(row['slots'])[0x48:0x4C]
            loader[25] = row['bd8']
            ticks[k + 1].update(msg_pre=struct.unpack('<3I', bytes.fromhex(row['msg'])[:12]),
                                fade8=row['fade'][:16], loader_pre=loader.hex())
            a00.compare_route_row(smoke, ticks, k, row, phase, owners=phase != 'a00_arrival')
        count += len(rows)
        ram = a00.capture_path(phase).with_name('eeMemory.bin').read_bytes()
        ticks[-2]['camblk'] = [ram[a:a + n].hex() for a, n in (a01.CAMERA_BLOCK, a01.CAMERA_FORWARD, a01.CAMERA_690)]
        a00.check_camera_end(ticks[-2], phase)
        gap = a00.source_gap(phase, capture)
        source_counter = rows[0]['counter'] - gap
        if phase == 'a00_arrival':
            aligned = [dict(counter=source_counter)] + ticks
            state = dict(cursor=1, area00_arrival=0)
            start = 1
        else:
            aligned = [dict(counter=source_counter + k + 1) for k in range(gap - 1)] + ticks
            state = dict(cursor=0, area00_ends={a00.SOURCES[phase]: source_counter})
            start = gap - 1
        run = f'level smoke: {phase}: aligned counter={rows[0]["counter"]}\n'
        assert a00.route_start(aligned, run, state, phase, capture) == (start, rows[0]['counter'])
        for delta in (-1, 1):
            bad_state = copy.deepcopy(state)
            bad_ticks = aligned
            if phase == 'a00_arrival':
                bad_ticks = [dict(counter=source_counter + delta)] + aligned[1:]
            else:
                bad_state['area00_ends'][a00.SOURCES[phase]] += delta
            try:
                a00.route_start(bad_ticks, run, bad_state, phase, capture)
            except AssertionError:
                rejected += 1
            else:
                raise AssertionError((phase, 'checker accepted wrong source counter', delta))
        if phase in a00.RETURN_TICKS:
            arrival = next(r['f'] for r in rows if r['area4'] == '01000001' and
                           bytes.fromhex(r['slots'])[8:12] == bytes((3, 1, 0, 0)))
            run += f'level smoke: {phase}: boundary counter={rows[arrival]["counter"]}\n'
            pairs, waits, _, _, _ = a00.return_alignment(smoke, ticks, run, phase, 0, rows)
            assert pairs == list(enumerate(rows)) and not waits, (phase, 'return alignment')
            broken = copy.deepcopy(ticks)
            loader = bytearray.fromhex(broken[arrival]['loader_pre']); loader[1] ^= 1
            broken[arrival]['loader_pre'] = loader.hex()
            try:
                a00.return_alignment(smoke, broken, run, phase, 0, rows)
            except AssertionError:
                rejected += 1
            else:
                raise AssertionError((phase, 'checker accepted wrong loader state'))
        else:
            with contextlib.redirect_stdout(io.StringIO()):
                a00.check_route(smoke, aligned, run, state, phase)
            assert state['cursor'] == start + len(rows)
            assert state['area00_ends'][phase] == rows[-1]['counter']
        fields = ['camera', 'camera_block', 'progress', 'clock', 'health', 'missing_observation']
        if phase != 'a00_arrival':
            fields += ['owner', 'missing_owner', 'locks', 'wpn', 'ferry']
        for field in fields:
            pair = copy.deepcopy(ticks[:2])
            tick = pair[0]
            if field == 'camera':
                tick['cam4'] = 'ff' + tick['cam4'][2:]
            elif field == 'camera_block':
                block = bytearray.fromhex(tick['camblk'][0]); block[0x27] ^= 0x40; tick['camblk'][0] = block.hex()
            elif field == 'progress':
                tick['a00']['progress'][1] = '01' + tick['a00']['progress'][1][2:]
            elif field == 'clock':
                tick['player'][4] = bits(rows[0]['clock'] + 1)
            elif field == 'health':
                tick['a00']['hp'] = bits(rows[0]['hp'] + 1)
            elif field == 'missing_observation':
                del tick['a00']
            elif field == 'owner':
                next(iter(tick['a00']['owners'].values()))[4] ^= 4
            elif field == 'missing_owner':
                tick['a00']['owners'].clear()
            elif field == 'ferry':
                value = tick['a00']['progress'][0]
                tick['a00']['progress'][0] = value[:8] + 'ff' + value[10:]
            else:
                tick['a00'][field] = 'ff' + tick['a00'][field][2:]
            try:
                a00.compare_route_row(smoke, pair, 0, rows[0], phase, owners=phase != 'a00_arrival')
            except AssertionError:
                rejected += 1
            else:
                raise AssertionError((phase, 'checker accepted corrupt ' + field))
        block = bytearray.fromhex(ticks[-2]['camblk'][0]); block[0x40] ^= 1
        ticks[-2]['camblk'][0] = block.hex()
        try:
            a00.check_camera_end(ticks[-2], phase)
        except AssertionError:
            rejected += 1
        else:
            raise AssertionError((phase, 'checker accepted corrupt ending camera'))
    print(f'AREA00 checker contract: PASS {count} capture-shaped rows, {rejected} rejected corruptions '
          '(no native gameplay or drawing parity claim)')


def prefix_report(out, until):
    import test_level_smoke as smoke
    run = (out / 'run.log').read_text(errors='replace')
    if not (out / 'ticks.jsonl').exists():
        return
    ticks = [t for t in (json.loads(line) for line in (out / 'ticks.jsonl').open()) if 'tick' in t]
    for phase in a00.phase_path(until):
        match = re.search(rf'^level smoke: {phase}: aligned counter=(\d+)', run, re.M)
        if not match:
            print(f'{phase}: NOT PASSED; no native alignment reached', flush=True)
            break
        start = next((i for i, t in enumerate(ticks) if t['counter'] == int(match.group(1))), None)
        if start is None:
            print(f'{phase}: NOT PASSED; alignment has no logged tick', flush=True)
            break
        capture = a00.route_capture(phase)
        rows = capture['rows'][a00.ARRIVAL_ROW:] if phase == 'a00_arrival' else capture['rows']
        count, first = 0, None
        for k, row in enumerate(rows):
            if start + k + 1 >= len(ticks):
                break
            try:
                assert ticks[start + k]['counter'] == ticks[start]['counter'] + k, 'skipped main-loop tick'
                a00.compare_route_row(smoke, ticks, start + k, row, phase, owners=phase != 'a00_arrival')
            except (AssertionError, KeyError) as exc:
                first = str(exc)
                break
            count += 1
        print(f'{phase}: NOT PASSED; {count}/{len(rows)} recorded rows exact before stop'
              + (f'; first difference {first}' if first else ''), flush=True)
        if first or count < len(rows):
            break


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--prepare', action='store_true')
    mode.add_argument('--verify-harness', action='store_true')
    mode.add_argument('--run', action='store_true', help='opt into the long native replay from New Game')
    parser.add_argument('--include-side-pads', action='store_true')
    endpoint = parser.add_mutually_exclusive_group()
    endpoint.add_argument('--until', choices=('a00_arrival', *a00.MAIN_BEATS), default=None)
    endpoint.add_argument('--side', choices=a00.SIDE_BEATS)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/level3/smoke')
    parser.add_argument('--bin', type=Path, default=Path(os.environ.get('EM_LEVEL_SMOKE_BIN', ROOT / 'build/extermination')))
    replay = parser.add_mutually_exclusive_group()
    replay.add_argument('--drive-replay', type=Path, default=drive_replay.DEFAULT_DIR,
                        help='the fork-recorded per-beat drive tables (tools/record_drive_replay.py; default '
                             'build/drive_replay): the run uses the PS2 disc-drive timing switch and the '
                             'comparison-only drive replay (EM_PS2_DRIVE_REPLAY)')
    replay.add_argument('--no-drive-replay', action='store_true',
                        help='no replay: the drive as the environment selects (host speed by default)')
    args = parser.parse_args()
    until, out = args.side or args.until or 'a00_arrival', args.out.resolve()
    if (args.until or args.side) and not (args.run or args.prepare):
        parser.error('--until/--side require --run or --prepare; the default only verifies the harness')
    if args.verify_harness or not (args.run or args.prepare):
        verify_harness(out / 'harness')
        return 0
    a01.prepare_pads(out / 'pads/area01')
    count = a00.prepare_pads(out / 'pads/area00', args.include_side_pads or bool(args.side))
    print(f'AREA00 {refs.label()}: exported {count} pad changes; {" -> ".join(a00.phase_path(until))}; '
          'input preparation is not gameplay verification', flush=True)
    if args.prepare:
        return 0
    binary = args.bin.resolve()
    env = dict(os.environ, EM_UNCAPPED='1', EM_HEADLESS='1', EM_STARTUP_TEST='newgame-level',
               EM_LEVEL_SMOKE_UNTIL=until, EM_LEVEL2_PAD_DIR=str(out / 'pads/area01'),
               EM_LEVEL3_PAD_DIR=str(out / 'pads/area00'), EM_AREA_CHANGE_LOG=str(out / 'ticks.jsonl'),
               EM_RAND_TRACE=str(out / 'rand.trace'), EM_LEVEL_SMOKE_BIN=str(binary))
    replay_info = None
    if not args.no_drive_replay:
        # AREA01's main beats (the route to the AREA00 arrival), then the
        # AREA00 phases on this path, each anchored at its recorded row 0.
        phases = [(p, a01.MAIN_BEATS[p], a01.route_capture(p)['first_counter']) for p in a01.MAIN_BEATS]
        phases += [(p, (a00.MAIN_BEATS | a00.SIDE_BEATS)[p], a00.route_capture(p)['first_counter'])
                   for p in a00.phase_path(until) if p != 'a00_arrival']
        replay_info = drive_replay.write_table(phases, args.drive_replay, out / 'drive_replay.txt')
        env.update(EM_PS2_DISC_DRIVE_TIMING='1', EM_PS2_DRIVE_REPLAY=str(out / 'drive_replay.txt'))
        print(f"AREA00: drive replay of {replay_info['entries']} fork-recorded reads over "
              f"{len(phases)} phases (comparison only)", flush=True)
    started = time.monotonic()
    receipt = dict(binary=str(binary), binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                   reference=refs.label(), until=until, area00_path=a00.phase_path(until), pad_changes=count,
                   pad_manifest=str(out / 'pads/area00/manifest.json'), native_parity=False,
                   drive_replay=replay_info)
    with (out / 'run.log').open('w') as log:
        result = subprocess.run([str(binary)], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=1800)
    receipt.update(run_returncode=result.returncode, seconds=round(time.monotonic() - started, 3))
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    run_text = (out / 'run.log').read_text(errors='replace')
    for line in run_text.splitlines():
        if line.startswith(('level smoke:', 'em_area00:', 'em_scene: FAULT', 'stream drive: replay')):
            print(line, flush=True)
    if replay_info is not None:
        replay_info['run'] = drive_replay.report(run_text)
        (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    if result.returncode:
        prefix_report(out, until)
        return result.returncode
    with (out / 'check.log').open('w') as log:
        result = subprocess.run([sys.executable, str(ROOT / 'tools/test_level_smoke.py'), '--log', str(out / 'ticks.jsonl'),
            '--run-log', str(out / 'run.log'), '--rand-trace', str(out / 'rand.trace'), '--require-through', until],
            cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=1800)
    receipt.update(check_returncode=result.returncode, native_parity=result.returncode == 0,
                   drawing_parity='not certified by AREA00 route rows; original packet/pixel comparisons pending')
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print((out / 'check.log').read_text())
    if result.returncode:
        prefix_report(out, until)
    return result.returncode


if __name__ == '__main__':
    sys.exit(main())
