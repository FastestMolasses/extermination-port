#!/usr/bin/env python3
"""A killed creature's dissolve (the glow node 001F6FB0), headless, against
the original's capture.

001F6FB0 (decomp src/func_001F6FB0.c, NEARMISS; port em_area00_fx_glow.c)
follows its parent (node +0x24, the dying creature): its phase grows by
0.016 a frame; sub-state 0 waits for the parent's animation flag, 1 fades
the parent's +0x60..+0x68 and +0x80..+0x88 over 63 frames, 2 zeroes
+0x60..+0x68; past phase 4.0 it sets both +4 bytes to 3. On the next frame
the parent, ahead of it in the pool walk, frees itself (00128C10 /
0012A5D0 at +4 = 3) and the node frees itself (001AFC10). In that last
tick the node no longer reads its parent; the aim / fire composition used
to map the parent's record for every tick and fail-stopped there when the
record was free (FAULT at 001F6FB0, the address the parent's), unless a
new node had taken the record in between (the player still firing).

The run: EM_START_AT=a00_04 (AREA00, the cage room's padlock) with an
EM_START_AT_PAD script: the stick toward the creature nest, R1, Circle
every 16 frames, then nothing, so no impact marker takes the freed
records; EM_GLOW_TRACE=1 prints each glow tick (em_aim_fire_runtime). Two
creatures die (an 00128C10 and the nest's 0012A5D0). Checked: no fault;
for every glow node, sub-state 1 for 63 ticks, 2 for one, state 1 for 250
ticks, state 3 with phase 4.016 on tick 250, and on tick 251 the node
freed with its parent's record already free; the 0012A5D0 creature's
dissolve tick by tick equal to the original's (state, sub-state, phase,
fade and the parent's six fade words, to 1e-4) and freed on the same tick
offset. The reference is the original's run of the same pad from the same
point on the debugging fork
(../Extermination/build/startup-reference/glow_dissolve/a00_04_kill.json,
README.md beside it; nothing original is embedded here). A loaded game is
not the recording's state (rand(), timers), so the frames of the kills
differ; the dissolve is aligned on the node's first tick.

Needs make start-saves. About 10 s.
"""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / 'build/extermination'
CARD = ROOT / 'build/start_saves/a00_04/start.json'
REFERENCE = ROOT.parent / 'Extermination/build/startup-reference/glow_dissolve/a00_04_kill.json'
OUT = ROOT / 'build/glow_dissolve'
FRAMES = 600


def pad_script():
    """EMA1 rows (frame, buttons, lx, ly; canonical EM_PAD bits: R1 0x800,
    Circle 0x2000): stick right (toward the nest from the padlock), R1, a
    Circle every 16 frames, then a neutral pad."""
    rows = [(0, 0, 127, 127), (70, 0, 255, 127), (100, 0, 127, 127), (110, 0x800, 127, 127)]
    f = 130
    while f < 330:
        rows += [(f, 0x2800, 127, 127), (f + 4, 0x800, 127, 127)]
        f += 16
    rows.append((340, 0, 127, 127))
    return '\n'.join([f'EMA1 1 {FRAMES} 1'] + [f'{f} {b:x} {x} {y}' for f, b, x, y in rows]) + '\n'


def run():
    OUT.mkdir(parents=True, exist_ok=True)
    pad = OUT / 'kill.pad'
    pad.write_text(pad_script())
    env = {k: v for k, v in os.environ.items() if not k.startswith('EM_')}
    env.update(EM_START_AT='a00_04', EM_START_AT_PAD=str(pad), EM_GLOW_TRACE='1',
               EM_HEADLESS='1', EM_MUTE='1', EM_UNCAPPED='1')
    p = subprocess.Popen([str(BIN)], cwd=ROOT, env=env, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True)
    lines, t0 = [], time.monotonic()
    try:
        for line in p.stdout:
            lines.append(line.rstrip('\n'))
            # The game runs on after the script (a neutral pad); stop it there.
            if 'EM_START_AT_PAD: the script ended' in line or 'the game task is stopped' in line:
                break
            if time.monotonic() - t0 > 120:
                lines.append('test: timeout')
                break
    finally:
        p.kill()
        p.wait()
    (OUT / 'run.log').write_text('\n'.join(lines) + '\n')
    return lines


TICK = re.compile(r'glow: frame (\d+) node (\w+) st (\d+) sub (\d+) phase (\S+) t (\S+) '
                  r'parent (\w+) live cb (\w+) st (\d+) p60 (\S+) (\S+) (\S+) p80 (\S+) (\S+) (\S+)$')
FREED = re.compile(r'glow: frame (\d+) node (\w+) freed, parent (\w+) (live|free|none)')


def main():
    for need, how in ((BIN, 'make'), (CARD, 'make start-saves'), (REFERENCE, 'the original capture')):
        if not need.exists():
            print(f'FAIL: {need} is missing ({how})')
            return 1
    ref = json.loads(REFERENCE.read_text())
    lines = run()
    failures = []
    faults = [l for l in lines if 'FAULT' in l or 'faulted' in l or l == 'test: timeout']
    if faults:
        failures.append('the run faulted: ' + ' | '.join(faults[:4]))
    if not any('EM_START_AT_PAD: the script ended' in l for l in lines):
        failures.append('the pad script did not end')
    nodes = {}
    for l in lines:
        m = TICK.match(l)
        if m:
            nodes.setdefault(m.group(2), []).append(('tick', int(m.group(1)), m.groups()))
            continue
        m = FREED.match(l)
        if m:
            nodes.setdefault(m.group(2), []).append(('freed', int(m.group(1)), m.groups()))
    compared = 0
    for node, events in nodes.items():
        ticks = [e for e in events if e[0] == 'tick']
        freed = [e for e in events if e[0] == 'freed']
        if not ticks or len(freed) != 1 or events[-1][0] != 'freed':
            failures.append(f'node {node}: {len(ticks)} ticks, {len(freed)} frees (one free, last)')
            continue
        first = ticks[0][1]
        frames = [e[1] - first for e in ticks]
        if frames != list(range(len(ticks))):
            failures.append(f'node {node}: ticks not on consecutive frames')
        subs = [(int(g[2]), int(g[3])) for _, _, g in ticks]
        runs = []
        for s in subs:
            if runs and runs[-1][0] == s:
                runs[-1][1] += 1
            else:
                runs.append([s, 1])
        shape = [tuple(r[0]) for r in runs]
        if shape != [(1, 0), (1, 1), (1, 2), (1, 3), (3, 3)] or runs[1][1] != 63 or runs[2][1] != 1 or \
           runs[4][1] != 1 or sum(r[1] for r in runs[:4]) != 250:
            failures.append(f'node {node}: sub-state runs {runs}')
        last = ticks[-1][2]
        if float(last[4]) != 4.016:
            failures.append(f'node {node}: phase {last[4]} on its state-3 tick')
        f_frame, fg = freed[0][1], freed[0][2]
        if f_frame - first != len(ticks) or fg[3] != 'free' or fg[2] != last[6]:
            failures.append(f'node {node}: freed on tick {f_frame - first}, parent {fg[2]} {fg[3]}')
        if last[7] != '0012A5D0':
            continue
        # The original's dissolve of the same creature kind, tick by tick.
        compared += 1
        want = ref['ticks']
        if len(want) != len(ticks) or ref['freed_offset'] != f_frame - first:
            failures.append(f'node {node}: {len(ticks)} ticks, freed at +{f_frame - first}; '
                            f'original {len(want)}, +{ref["freed_offset"]}')
        for k, (w, (_, _, g)) in enumerate(zip(want, ticks)):
            got = dict(st=int(g[2]), sub=int(g[3]), phase=float(g[4]), t=float(g[5]),
                       p60=[float(x) for x in g[9:12]], p80=[float(x) for x in g[12:15]])
            ok = got['st'] == w['st'] and got['sub'] == w['sub'] and \
                abs(got['phase'] - w['phase']) <= 1e-3 and abs(got['t'] - w['t']) <= 1e-3 and \
                all(abs(a - b) <= 1e-4 for a, b in zip(got['p60'] + got['p80'], w['p60'] + w['p80']))
            if not ok:
                failures.append(f'node {node} tick {k}: {got} != original {w}')
                break
    if len(nodes) < 2:
        failures.append(f'{len(nodes)} glow node(s); the script kills two creatures')
    if not compared:
        failures.append('no 0012A5D0 dissolve to compare with the original')
    for f in failures:
        print('FAIL:', f)
    if failures:
        print(f'see {OUT / "run.log"}')
        return 1
    print(f'PASS: {len(nodes)} dissolves, no fault; each 250 ticks in state 1 (sub-states 0 / 1 63 / 2 1 / 3), '
          f'state 3 on tick 250, freed on tick 251 with its parent; the 0012A5D0 one equal to the original '
          f'tick by tick ({len(ref["ticks"])} ticks)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
