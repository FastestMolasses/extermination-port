#!/usr/bin/env python3
"""AREA01 revisit smoke from ordinary New Game, with independent original comparisons.

The revisit (a01r_00 .. a01r_02, tools/level_smoke_area01r.py) starts where
AREA00's a00_10 return ends, so a native run plays New Game, the first
level, AREA01 and AREA00 first (the port cannot start a beat from a recorded
state: no snapshot, placement or captured game state is imported).

--prepare exports pad input only. --verify-harness (the default) checks the C
input driver and the Python checker contracts against the recordings only;
neither proves native gameplay. --run opts into the long New Game replay.
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
import level_smoke_area01r as a01r
import reference_root as refs

ROOT = Path(__file__).resolve().parents[1]


def verify_harness(out):
    """Execute the actual C selector/reader/driver over the recorded inputs.
    Only the driver's game-facing services are mocked."""
    source = (ROOT / 'src/game/em_level_smoke_test.c').read_text()
    phases = re.findall(r'\{"(a01r_[^"]+)".*?a01_route_begin,\s*a01_route_frame,\s*0,\s*(\d+),\s*(\d+),\s*1\}',
                        source, re.S)
    assert [p[0] for p in phases] == list(a01r.ROUTE_PHASES), ('C/Python phase order', phases)
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
static EmSceneState scene={1,0,0};
static unsigned counter,buttons,failed;
static float lx,ly;
static void pad_apply(unsigned b,float x,float y){buttons=b;lx=x;ly=y;}
static void fail(const char *why){fprintf(stderr,"FAIL %s\n",why);failed=1;}
static unsigned em_frame_counter(void){return counter;}
static void em_random_seed(uint32_t s){(void)s;}
static void em_stream_live_drive_replay_anchor(const char *phase,unsigned c){(void)phase;(void)c;}
static const EmSceneState *em_scene_state(void){return &scene;}
static unsigned task_byte(unsigned off){(void)off;return 0;}
'''.replace('PHASE_ROWS', ',\n'.join('{"%s",%s,%s}' % row for row in phases))
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
    changes = a01r.prepare_pads(out / 'pads')
    manifest = json.loads((out / 'pads/manifest.json').read_text())
    env = dict(os.environ, EM_LEVEL4_PAD_DIR=str(out / 'pads'))
    submitted = 0
    for entry in manifest:
        phase, capture = entry['phase'], a01r.route_capture(entry['phase'])
        path = subprocess.run([str(binary), 'path', phase], capture_output=True, text=True, check=True).stdout.splitlines()
        assert path == list(a01r.ROUTE_PHASES[:a01r.ROUTE_PHASES.index(phase) + 1]), (phase, 'C branch path', path)
        result = subprocess.run([str(binary), 'drive', phase], env=env, capture_output=True, text=True, check=True)
        actual = [(int(c), int(b), float.fromhex(x), float.fromhex(y)) for c, b, x, y in
                  (line.split() for line in result.stdout.splitlines())]
        frames, gap = entry['endpoint_row'], entry['gap']
        assert len(actual) == frames + gap, (phase, 'driver endpoint', len(actual), frames, gap)
        commands = a01r.command_rows(phase, capture)
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
    # The C reader selects EM_LEVEL4_PAD_DIR for the revisit and fails without it.
    result = subprocess.run([str(binary), 'drive', 'a01r_00'], env=dict(os.environ, EM_LEVEL2_PAD_DIR=str(out / 'pads')),
                            capture_output=True, text=True)
    assert result.returncode == 1 and 'revisit pad script missing' in result.stderr, ('pad directory', result)
    path = out / 'pads/a01r_00.pad'
    original = path.read_text()
    bad = ('EMA1 1 10 1\n0 0 127 127\n1 0\n',
           'EMA1 1 10 1\n2 0 127 127\n1 0 127 127\n', 'EMA1 1 10 1\n0 0 127 256\n')
    try:
        for content in bad:
            path.write_text(content)
            result = subprocess.run([str(binary), 'drive', 'a01r_00'], env=env, capture_output=True, text=True)
            assert result.returncode == 1 and 'FAIL ' in result.stderr, ('malformed input accepted', content)
    finally:
        path.write_text(original)
    print(f'AREA01 revisit harness: PASS {len(manifest)} C branch paths, {changes} input commands, '
          f'{submitted} C frames, the pad directory and {len(bad)} malformed input rejections '
          '(ASan/UBSan; no gameplay parity claim)')
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
                  for name, address in a01r.OWNERS.items()}
        return dict(counter=row['counter'], tick=row['f'], post=post.hex(),
            player=[row['p5'], row['m1F0'], row['m1F1'], row['clip'], bits(row['clock']), int(row['ground'], 16), row['b2F3']],
            pos_post=list(map(bits, row['pos'])), yaw_post=bits(row['yaw']), eye_post=list(map(bits, row['eye'])),
            tgt_post=list(map(bits, row['tgt'])), cam4=row['cam_mode'], screen8=row['screen'][:16], power=row['power'],
            camblk=[block.hex(), struct.pack('<3fI', *row['fwd'], 0).hex(), bytes(20).hex()],
            a01r=dict(hp=bits(row['hp']), progress=[row['d2'], row['story758'], row['taken'], row['docs']],
                      owners=owners, locks=row['locks'], wpn=row['wpn'], s760=row['story760']))

    count = rejected = 0
    for phase in a01r.ROUTE_PHASES:
        capture = a01r.route_capture(phase)
        a01r.check_owner_nodes(phase, capture)
        rows = capture['rows']
        ticks = [native_row(r) for r in rows] + [{}]
        for k, row in enumerate(rows):
            ticks[k + 1].update(msg_pre=struct.unpack('<3I', bytes.fromhex(row['msg'])[:12]), fade8=row['fade'][:16])
        ram = a01r.capture_path(phase).with_name('eeMemory.bin').read_bytes()
        ticks[-2]['camblk'] = [ram[a:a + n].hex() for a, n in (a01.CAMERA_BLOCK, a01.CAMERA_FORWARD, a01.CAMERA_690)]
        gap = a01r.source_gap(phase, capture)
        source_counter = rows[0]['counter'] - gap
        aligned = [dict(counter=source_counter + k + 1) for k in range(gap - 1)] + ticks
        key = 'area00_ends' if a01r.SOURCES[phase] == 'a00_10' else 'a01r_ends'
        state = dict(cursor=0, **{key: {a01r.SOURCES[phase]: source_counter}})
        run = f'level smoke: {phase}: aligned counter={rows[0]["counter"]}\n'
        with contextlib.redirect_stdout(io.StringIO()):
            a01r.check_route(smoke, aligned, run, state, phase)
        assert state['cursor'] == gap - 1 + len(rows) and state['a01r_ends'][phase] == rows[-1]['counter']
        count += len(rows)
        for delta in (-1, 1):
            bad_state = copy.deepcopy(state)
            bad_state['cursor'] = 0
            bad_state[key][a01r.SOURCES[phase]] = source_counter + delta
            try:
                a01r.route_start(aligned, run, bad_state, phase, capture)
            except AssertionError:
                rejected += 1
            else:
                raise AssertionError((phase, 'checker accepted wrong source counter', delta))
        # The beat's milestones, one corrupt byte each: the event stage
        # D_008107DF (D_008107D8 window byte 7), the bridge counter D_008107E0
        # (byte 8), D_0081075F (story758 byte 7), D_00810760 (story760 byte 0).
        last = len(rows) - 1
        for field in ('df', 'e0', 'story75F', 's760', 'owner', 'missing_owner', 'missing_observation',
                      'locks', 'wpn', 'camera', 'clock', 'health'):
            pair = copy.deepcopy(ticks[last:last + 2])
            tick = pair[0]
            if field in ('df', 'e0'):
                value = bytearray.fromhex(tick['a01r']['progress'][0]); value[7 if field == 'df' else 8] ^= 0x11
                tick['a01r']['progress'][0] = value.hex()
            elif field == 'story75F':
                value = bytearray.fromhex(tick['a01r']['progress'][1]); value[7] ^= 0x11
                tick['a01r']['progress'][1] = value.hex()
            elif field == 's760':
                tick['a01r']['s760'] = '%02x' % (int(tick['a01r']['s760'][:2], 16) ^ 0x11) + tick['a01r']['s760'][2:]
            elif field == 'owner':
                next(iter(tick['a01r']['owners'].values()))[4] ^= 4
            elif field == 'missing_owner':
                tick['a01r']['owners'].clear()
            elif field == 'missing_observation':
                del tick['a01r']
            elif field in ('locks', 'wpn'):
                tick['a01r'][field] = 'ff' + tick['a01r'][field][2:]
            elif field == 'camera':
                tick['cam4'] = 'ff' + tick['cam4'][2:]
            elif field == 'clock':
                tick['player'][4] = bits(rows[last]['clock'] + 1)
            else:
                tick['a01r']['hp'] = bits(rows[last]['hp'] + 1)
            try:
                a01r.compare_route_row(smoke, pair, 0, rows[last], phase)
            except AssertionError:
                rejected += 1
            else:
                raise AssertionError((phase, 'checker accepted corrupt ' + field))
        block = bytearray.fromhex(ticks[-2]['camblk'][0]); block[0x40] ^= 1
        ticks[-2]['camblk'][0] = block.hex()
        try:
            a01r.check_camera_end(ticks[-2], phase)
        except AssertionError:
            rejected += 1
        else:
            raise AssertionError((phase, 'checker accepted corrupt ending camera'))
    print(f'AREA01 revisit checker contract: PASS {count} capture-shaped rows, {rejected} rejected corruptions '
          '(no native gameplay or drawing parity claim)')


def prefix_report(out, until):
    """After a failed or stopped run: how far the AREA00 path and the revisit
    got, row for row. A diagnostic, never a PASS."""
    import test_level_smoke as smoke
    run = (out / 'run.log').read_text(errors='replace')
    if not (out / 'ticks.jsonl').exists():
        return
    ticks = [t for t in (json.loads(line) for line in (out / 'ticks.jsonl').open()) if 'tick' in t]
    for phase in a01r.phase_path(until):
        if phase in a01r.ROUTE_PHASES:
            report = a01r.prefix_report(smoke, ticks, run, phase)
            if report is None:
                print(f'{phase}: NOT PASSED; no native alignment reached', flush=True)
                break
            exact, total, first = report
            print(f'{phase}: NOT PASSED; {exact}/{total} recorded rows exact before stop'
                  + (f'; first difference {first}' if first else ''), flush=True)
            if first or exact < total:
                break
        elif not re.search(rf'^level smoke: {phase}: PASS', run, re.M):
            print(f'{phase}: the run stopped before the revisit (AREA00 path; '
                  'tools/test_level_smoke_area00.py --run reports its rows)', flush=True)
            break


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--prepare', action='store_true')
    mode.add_argument('--verify-harness', action='store_true')
    mode.add_argument('--run', action='store_true', help='opt into the long native replay from New Game')
    parser.add_argument('--until', choices=a01r.ROUTE_PHASES, default=None)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/level4/smoke')
    parser.add_argument('--bin', type=Path, default=Path(os.environ.get('EM_LEVEL_SMOKE_BIN', ROOT / 'build/extermination')))
    parser.add_argument('--rand-replay', type=Path, default=None,
                        help='the fork-recorded rand() anchor table (tools/record_rand_replay.py; default: the '
                             'newest build/rand_replay/*/table.txt when one exists): EM_RAND_REPLAY, comparison only')
    parser.add_argument('--no-rand-replay', action='store_true')
    replay = parser.add_mutually_exclusive_group()
    replay.add_argument('--drive-replay', type=Path, default=drive_replay.DEFAULT_DIR,
                        help='the fork-recorded per-beat drive tables (tools/record_drive_replay.py; default '
                             'build/drive_replay): the run uses the PS2 disc-drive timing switch and the '
                             'comparison-only drive replay (EM_PS2_DRIVE_REPLAY)')
    replay.add_argument('--no-drive-replay', action='store_true',
                        help='no replay: the drive as the environment selects (host speed by default)')
    args = parser.parse_args()
    until, out = args.until or a01r.ROUTE_PHASES[-1], args.out.resolve()
    if args.until and not (args.run or args.prepare):
        parser.error('--until requires --run or --prepare; the default only verifies the harness')
    if args.verify_harness or not (args.run or args.prepare):
        verify_harness(out / 'harness')
        return 0
    a01.prepare_pads(out / 'pads/area01')
    a00.prepare_pads(out / 'pads/area00')
    count = a01r.prepare_pads(out / 'pads/area01r')
    print(f'AREA01 revisit {refs.label()}: exported {count} pad changes; {" -> ".join(a01r.phase_path(until))}; '
          'input preparation is not gameplay verification', flush=True)
    if args.prepare:
        return 0
    binary = args.bin.resolve()
    env = dict(os.environ, EM_UNCAPPED='1', EM_HEADLESS='1', EM_STARTUP_TEST='newgame-level',
               EM_LEVEL_SMOKE_UNTIL=until, EM_LEVEL2_PAD_DIR=str(out / 'pads/area01'),
               EM_LEVEL3_PAD_DIR=str(out / 'pads/area00'), EM_LEVEL4_PAD_DIR=str(out / 'pads/area01r'),
               EM_AREA_CHANGE_LOG=str(out / 'ticks.jsonl'), EM_RAND_TRACE=str(out / 'rand.trace'),
               EM_LEVEL_SMOKE_BIN=str(binary))
    replay_info = None
    if not args.no_drive_replay:
        phases = [(p, a01.MAIN_BEATS[p], a01.route_capture(p)['first_counter']) for p in a01.MAIN_BEATS]
        a00_path = [p for p in a00.phase_path('a00_10') if p != 'a00_arrival']
        phases += [(p, a00.MAIN_BEATS[p], a00.route_capture(p)['first_counter']) for p in a00_path]
        phases += [(p, a01r.MAIN_BEATS[p], a01r.route_capture(p)['first_counter'])
                   for p in a01r.ROUTE_PHASES[:a01r.ROUTE_PHASES.index(until) + 1]]
        traces = {p: a00.capture_path(p) for p in a00_path}
        # The revisit's bank loads (a01r_01's status page: module 0x23 at
        # rows 8068..8090) take the recording's loader polls too.
        traces.update({p: a01r.capture_path(p) for p in a01r.ROUTE_PHASES[:a01r.ROUTE_PHASES.index(until) + 1]})
        # The AREA00 arrival plays inside a01_07's recording; the AREA00
        # load changes the run's counter relation, so its row 0 anchors
        # again (as tools/test_level_smoke_area00.py does).
        arrival = a00.route_capture('a00_arrival')['rows'][a00.ARRIVAL_ROW]['counter']
        replay_info = drive_replay.write_table(phases, args.drive_replay, out / 'drive_replay.txt', traces,
                                               extra_anchors=[('a00_arrival', arrival)])
        env.update(EM_PS2_DISC_DRIVE_TIMING='1', EM_PS2_DRIVE_REPLAY=str(out / 'drive_replay.txt'))
        print(f"AREA01 revisit: drive replay of {replay_info['entries']} fork-recorded reads and "
              f"{replay_info['loader_entries']} loader reads over {len(phases)} phases (comparison only)",
              flush=True)
    # The comparison-only rand() anchor at the AREA01 area entry (the first
    # level is navigated, not replayed: tools/record_rand_replay.py), as
    # tools/test_level_smoke_area00.py passes it.
    rand_table = None
    if not args.no_rand_replay:
        rand_table = args.rand_replay
        if rand_table is None:
            tables = sorted((ROOT / 'build/rand_replay').glob('*/table.txt'), key=lambda p: p.stat().st_mtime)
            rand_table = tables[-1] if tables else None
        if rand_table is not None:
            env.update(EM_RAND_REPLAY=str(rand_table.resolve()))
            print(f'AREA01 revisit: rand() replay anchors from {rand_table} (comparison only)', flush=True)
    started = time.monotonic()
    receipt = dict(binary=str(binary), binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                   reference=refs.label(), until=until, path=a01r.phase_path(until), pad_changes=count,
                   pad_manifest=str(out / 'pads/area01r/manifest.json'), native_parity=False,
                   drive_replay=replay_info, rand_replay=str(rand_table) if rand_table else None)
    with (out / 'run.log').open('w') as log:
        result = subprocess.run([str(binary)], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=3600)
    receipt.update(run_returncode=result.returncode, seconds=round(time.monotonic() - started, 3))
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    run_text = (out / 'run.log').read_text(errors='replace')
    for line in run_text.splitlines():
        if line.startswith(('level smoke:', 'em_area00:', 'em_area01:', 'em_scene: FAULT', 'stream drive: replay',
                            'module loader')):
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
            cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=3600)
    receipt.update(check_returncode=result.returncode, native_parity=result.returncode == 0,
                   drawing_parity='not certified by the revisit route rows')
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print((out / 'check.log').read_text())
    if result.returncode:
        prefix_report(out, until)
    return result.returncode


if __name__ == '__main__':
    sys.exit(main())
