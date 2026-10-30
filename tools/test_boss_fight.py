#!/usr/bin/env python3
"""The first boss fight against its fork recording (docs/EIGHTH_LEVEL_ROUTE.md
section 9; decomp build/fork_refs/boss_fight_20261030, tools/capture_boss_fight.py).

`make test-boss-fight` (about two minutes a chain; a milestone test, not part
of the quick runs). The native game plays the recorded pad from a start card
through the reader, the fight, the drop and the lift up to AREA13's load, with
the comparison anchors of EM_START_AT_SYNC / _RANDS / _RESTORE (docs/STARTUP.md;
written by tools/capture_boss_fight.py anchors), and every row from the reader's
lock byte on is compared with the recording: the player's position, heading,
action, health and infection, the creature's first 16 bytes and position, and
the spawned bugs'.

  shotgun  card boss, a04b_g2 .. a04b_g5: the SHOTGUN UNIT, the intended win
  pistol   card a04b_03, a04b_03, a04b_s5 .. a04b_s7

What a loaded game cannot hold, and the anchors supply (comparison only): a
load puts the bugs at their placements, restarts the items' glow timers and
the NPC's face timers and the periodic clock 0x70003B68, and the steered walk
to the reader is not row-exact, so the bugs' bites there are not the
recording's. From the lock on nothing is written.

Usage (port root): python3 tools/test_boss_fight.py [shotgun] [pistol]
EM_BOSS_BIN=<binary> runs another build. Output under build/boss_fight/<chain>/.
Headless; nothing here embeds original bytes.
"""
import json
import os
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mid_route_beats as m  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get('EM_BOSS_BIN', str(ROOT / 'build/extermination'))).resolve()
CHAINS = {
    'shotgun': ('boss', ('a04b_g2', 'a04b_g3', 'a04b_g4', 'a04b_g5')),
    'pistol': ('a04b_03', ('a04b_03', 'a04b_s5', 'a04b_s6', 'a04b_s7')),
}
END = re.compile(r'block at the pad end|the pack does not hold lsn|FAULT at|faulted at')


def f32(word):
    return struct.unpack('<f', struct.pack('<I', word))[0]


def run(chain):
    card, beats = CHAINS[chain]
    out = ROOT / 'build/boss_fight' / chain
    for name in ('anchor.bin', 'anchor0.bin', 'rands.txt', 'sync.txt', 'restore.txt'):
        if not (out / name).exists():
            raise SystemExit(f'{out / name} missing: python3 tools/capture_boss_fight.py anchors')
    pad, path, _, traces = m.prepare(f'boss_{chain}', beats)
    # The steering stops at the reader: the fight's rows are the pad's own.
    rows = path.read_text().splitlines()[:traces[0]['frames'] + 1]
    path.write_text('\n'.join(rows) + '\n')
    log, ticks = out / 'run.log', out / 'ticks.jsonl'
    env = {k: v for k, v in os.environ.items() if not k.startswith('EM_')}
    env.update(EM_START_AT=card, EM_START_AT_PAD=str(pad), EM_START_AT_PATH=str(path), EM_HEADLESS='1',
               EM_UNCAPPED='1', EM_MUTE='1', EM_START_AT_RAND=f'{m.source_rand(beats[0]):08x}',
               EM_AREA_CHANGE_LOG=str(ticks), EM_LOG_AIM_RECORDS='1',
               EM_START_AT_RANDS=str(out / 'rands.txt'),
               EM_START_AT_SYNC=(out / 'sync.txt').read_text().strip(),
               EM_START_AT_RESTORE=(out / 'restore.txt').read_text().strip())
    with open(log, 'w') as f:
        p = subprocess.Popen([str(BIN)], cwd=ROOT, env=env, stdout=f, stderr=subprocess.STDOUT)
        start = time.time()
        try:
            while p.poll() is None and time.time() - start < 600:
                time.sleep(0.5)
                if END.search(log.read_text(errors='replace')):
                    break
        finally:
            if p.poll() is None:
                p.terminate()
                p.wait()
    return compare(chain, traces, log, ticks)


def compare(chain, traces, log, ticks):
    text = log.read_text(errors='replace')
    native = []
    for line in open(ticks):
        if line.startswith('{'):
            try:
                row = json.loads(line)
            except ValueError:
                continue          # the line the run's end cut
            if 'a04' in row:
                native.append(row)
    lock_native = next(i for i, n in enumerate(native) if bytes.fromhex(n['a04']['locks'])[5] == 0xA8)
    lock = next(r for r in traces[0]['rows'] if r['l845'] == 'a8')
    by_counter = {n['counter']: n for n in native}
    offset = native[lock_native]['counter'] - lock['counter']
    good = bad = 0
    first_bad = None
    ended = None
    for t in traces:
        for r in t['rows']:
            if r['counter'] < lock['counter']:
                continue
            if int(r['area'][:2], 16) != 4:
                ended = ended or (t['beat'], r['f'])
                break
            n = by_counter.get(r['counter'] + offset)
            if n is None:
                ended = ended or (t['beat'], r['f'])
                break
            why = row_problems(r, n)
            if why:
                bad += 1
                first_bad = first_bad or (t['beat'], r['f'], why)
            else:
                good += 1
        if ended:
            break
    # The load after the lift is AREA13's: out of the fight's scope, its
    # module is not in the pack and the run stops at the request.
    area13 = 'the pack does not hold lsn' in text
    fault = re.search(r'em_scene: FAULT at.*', text)
    print(f'{chain}: {good} rows identical, {bad} differ, the compare ends at {ended or "the last row"}'
          + (' (AREA13\'s load)' if area13 else ''))
    if first_bad:
        print('  first difference:', first_bad)
    if fault:
        print('  ', fault.group(0))
    return not bad and good > 0 and not fault


def row_problems(r, n):
    why = []
    pos = [f32(w) for w in n['pos_post']]
    if max(abs(a - b) for a, b in zip(pos, r['pos'])) >= 1e-3:
        why.append(f'position {pos} {r["pos"]}')
    if abs(f32(n['yaw_post']) - r['yaw']) >= 1e-3:
        why.append(f'heading {f32(n["yaw_post"])} {r["yaw"]}')
    if n['player'][1] != r['m1F0']:
        why.append(f'action {n["player"][1]} {r["m1F0"]}')
    record = bytes.fromhex(n['aimrec'][0]) if n.get('aimrec') else None
    if record:
        health, infection = struct.unpack_from('<f', record, 0x220)[0], struct.unpack_from('<f', record, 0x228)[0]
        if health != r['hp'] or infection != r['inf']:
            why.append(f'vitals {health} {infection} {r["hp"]} {r["inf"]}')
    if 'creature' in r and 'a04c' in n:
        got = list(n['a04c']['owners'].values())[0]
        want = r['creature']
        if got[0] != want['h'] or got[1] != struct.pack('<3f', *want['pos']).hex():
            why.append(f'creature {got[0]} {want["h"]}')
    bugs = [(struct.unpack('<3f', bytes.fromhex(b[2])[:12]), b[1]) for b in n.get('a04b', [])]
    for name in ('bug_a', 'bug_b', 'bug_c', 'bug_d'):
        want = r.get(name)
        if not want or want['cb'] != '0x12a5d0' or int(want['h'][:2], 16) == 0:
            continue
        hit = [b for b in bugs if max(abs(x - y) for x, y in zip(b[0], want['pos'])) < 2e-3]
        if not hit or hit[0][1][:14] != want['h'][:14]:
            why.append(f'{name} {want["h"]} {want["pos"]}')
    return why


def main(argv):
    names = argv or list(CHAINS)
    if not BIN.exists():
        raise SystemExit(f'{BIN} missing: make')
    failed = [n for n in names if not run(n)]
    print('boss fight:', 'PASS' if not failed else 'FAIL ' + ' '.join(failed))
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
