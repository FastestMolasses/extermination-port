#!/usr/bin/env python3
"""LOAD GAME on the boot's title (docs/SAVE_GAME.md section 6.4).

`make test-title-load-game`. The game boots headless (EM_STARTUP_TEST=
loadgame, em_frontend.c): the warning / Sony / Deep Space screens, the
attract movie skipped by START, the title menu, Down to LOAD GAME and its
CROSS; on the card screen 00225AC0(0) / 00227300 the policy of
tools/test_card_screen_reference.py (slot 0 on port 0, Yes) on a copy of
the area01 start-point card (make start-saves; docs/SAVE_GAME.md section
7); then 001AC070 state 4's loaded game (em_game_install_loaded_001AC070)
into the slot's area, the placement and control there.

No recording of a load exists. The run checks the original code's own
steps against the card: the screen's end with +0 = 3 and the result 2 in
+0x16, then state 4; the area bytes of the handoff equal the slot's block
+0..+2; the placement equals the slot's load pose D_00810710 / 724 (the
block's, and start.json's); the health equals the slot's D_00810858;
control in that area; the card folder unchanged by the load.

The whole progress block D_00810700..D_00810D3F (the fixture's "block"
lines) is compared byte for byte with the slot's EX_DATA.00 four times:
after the read (001AC070 state 4, before the handoff), after the handoff
(the port's game install), at the placement and at control. The first
three must equal the slot in all 0x640 bytes. At control exactly one
original write is allowed, listed by name (EXPECTED_WRITES): the world
frame counter D_00810750, which 001AE5E0 and 001AE6B0 (em_scene_frame.c)
each bump by one per world frame, so it must equal the slot's value plus
the frames from the placement to control. No other byte may differ.

Negative control: the same run on an empty card (both slots empty folders)
must not hand off a game (the screen's presses have no effect and the run
fails), so the pass cannot come from anything but the card's save.
"""
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get('EM_TITLE_LOAD_BIN', str(ROOT / 'build/extermination')))
OUT = ROOT / 'build/title_load_game'
START_CARD = ROOT / 'build/start_saves/area01'
CARD = OUT / 'card'
EMPTY = OUT / 'empty_card'
SAVE_DIR = 'BASCUS-97112-DS00-00'
TIMEOUT = 300


def card_files(root):
    return sorted(p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file() or p.is_symlink())


def card_hashes(root):
    return {f: hashlib.sha256((root / f).read_bytes()).hexdigest() for f in card_files(root)}


def prepare(root, files):
    """`root` holds exactly `files` from the start-point card (rewritten each
    run); a folder holding anything else, or a symlink, is refused (nothing
    is deleted)."""
    if root.exists():
        extra = [f for f in card_files(root) if f not in files]
        links = [p for p in root.rglob('*') if p.is_symlink()]
        assert not extra and not links, ('the card folder holds other files', root, extra, links)
    for d in ('slot1', 'slot2'):
        (root / d).mkdir(parents=True, exist_ok=True)
    for f in files:
        (root / f).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(START_CARD / f, root / f)
    return card_hashes(root)


def start(card, log_path):
    env = dict(os.environ, EM_UNCAPPED='1', EM_STARTUP_TEST='loadgame', EM_TEST_MEMCARD_ROOT=str(card))
    for k in ('EM_START_AT', 'EM_NEW_GAME', 'EM_SKIP_STARTUP', 'EM_LEVEL_SMOKE_UNTIL'):
        env.pop(k, None)
    log = open(log_path, 'w')
    return subprocess.Popen([str(BIN)], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT), log


def finish(proc, log, log_path):
    """Waits; a scene fault stops the game task but not the process (the
    fail-stop), so the run is ended once one is logged."""
    t0 = time.time()
    while proc.poll() is None:
        time.sleep(0.5)
        if 'em_scene: FAULT' in log_path.read_text(errors='replace') or time.time() - t0 > TIMEOUT:
            time.sleep(0.5)
            proc.kill()
            proc.wait()
            break
    log.close()
    return proc.returncode, log_path.read_text(errors='replace')


BASE = 0x00810700
# The original's writes to the block between the placement and control:
# (address, size, decomp function, reason). Nothing else may change.
EXPECTED_WRITES = [
    (0x00810750, 4, '001AE5E0 / 001AE6B0',
     'the world frame counter: += 1 in each world frame variant'),
]


PREFIX = 'startup test: loadgame'


def blocks(text, prefix=PREFIX):
    got = {}
    for m in re.finditer('^' + re.escape(prefix) + r': block (\w+) ([0-9A-F]+)$', text, re.M):
        assert m.group(1) not in got, ('a block line twice', m.group(1))
        got[m.group(1)] = bytes.fromhex(m.group(2))
    assert sorted(got) == sorted(('read', 'handoff', 'placed', 'control')), ('the block lines', sorted(got))
    for k, b in got.items():
        assert len(b) == 0x640, ('the block line is not 0x640 bytes', k, len(b))
    return got


def differ(a, b):
    return [i for i in range(0x640) if a[i] != b[i]]


def check_block(text, slot, placed_frame, control_frame, prefix=PREFIX):
    got = blocks(text, prefix)
    for k in ('read', 'handoff', 'placed'):
        d = differ(got[k], slot)
        assert not d, (f'the block at "{k}" differs from the slot',
                       [(hex(BASE + i), slot[i], got[k][i]) for i in d[:32]], len(d))
    allowed = set()
    for address, size, _func, _why in EXPECTED_WRITES:
        allowed.update(range(address - BASE, address - BASE + size))
    d = [i for i in differ(got['control'], slot) if i not in allowed]
    assert not d, ('the block at control differs from the slot outside the listed writes',
                   [(hex(BASE + i), slot[i], got['control'][i]) for i in d[:32]], len(d))
    n0 = struct.unpack_from('<i', slot, 0x00810750 - BASE)[0]
    n1 = struct.unpack_from('<i', got['control'], 0x00810750 - BASE)[0]
    assert n1 - n0 == control_frame - placed_frame, \
        ('D_00810750 is not the slot counter plus one per world frame', n0, n1, placed_frame, control_frame)
    return n1 - n0


def f32(block, address):
    return struct.unpack_from('<f', block, address - 0x00810700)[0]


def check_pass(text, block, pose, prefix=PREFIX):
    """The run's lines (`prefix`: the title load test's, or EM_START_AT's under
    EM_START_AT_TEST=1, tools/test_start_at.py) against the slot."""
    lines = [l for l in text.splitlines() if l.startswith(prefix + ':')]
    for l in lines:
        if ': block ' not in l:
            print(l)
    assert 'em_scene: FAULT' not in text, 'a scene fault'
    assert '001AC070 again' not in text, 'the title after a death ran (not the boot title)'
    up = [l for l in lines if 'the card screen up' in l]
    presses = [l for l in lines if ': press ' in l]
    ends = [re.search(r'the load screen ends .*\(title state (\d+), card (\d+) (\d+) (\d+) (\d+)\)', l)
            for l in lines]
    ends = [m for m in ends if m]
    hand = [re.search(r'handoff at engine frame \d+, area ([0-9A-F]{2}) ([0-9A-F]{2}) ([0-9A-F]{2})', l)
            for l in lines]
    hand = [m for m in hand if m]
    placed = [re.search(r'placed at engine frame (\d+) in area \S+ \S+ \S+ at \(([-\d.]+),([-\d.]+),([-\d.]+)\) '
                        r'heading ([-\d.]+), the load pose \(([-\d.]+),([-\d.]+),([-\d.]+)\) heading ([-\d.]+), '
                        r'health ([-\d.]+)', l) for l in lines]
    placed = [m for m in placed if m]
    control = [re.search(r'control in the loaded area ([0-9A-F]{2}) at engine frame (\d+) \(([-\d.]+),([-\d.]+),'
                         r'([-\d.]+)\) heading ([-\d.]+) health ([-\d.]+)', l) for l in lines]
    control = [m for m in control if m]
    assert len(up) == 1 and 1 <= len(presses) <= 40, ('the card screen and its presses', len(up), len(presses))
    assert len(ends) == 1, 'the load screen did not end once'
    state, m0, _m1, _m15, m16 = (int(x) for x in ends[0].groups())
    assert (state, m0, m16) == (4, 3, 2), ('the screen end: state 4, +0 = 3, result 2', state, m0, m16)
    assert len(hand) == 1, 'no loaded game handoff'
    area = tuple(int(x, 16) for x in hand[0].groups())
    assert area == tuple(block[0:3]), ('the handoff area is not the slot block', area, tuple(block[0:3]))
    assert len(placed) == 1, 'no placement'
    placed_frame = int(placed[0].group(1))
    got = [float(x) for x in placed[0].groups()[1:]]
    slot_pose = [f32(block, 0x00810710), f32(block, 0x00810714), f32(block, 0x00810718), f32(block, 0x00810724)]
    want = list(pose['position']) + [pose['rotation'][1]]
    for a, b, c in zip(got[0:4], slot_pose, want):
        assert abs(a - b) < 1e-3 and abs(b - c) < 1e-3, ('the placement is not the load pose', got, slot_pose, want)
    for a, b in zip(got[4:8], slot_pose):
        assert abs(a - b) < 1e-3, ('the block in memory does not hold the slot load pose', got, slot_pose)
    assert abs(got[8] - f32(block, 0x00810858)) < 1e-3, ('health', got[8], f32(block, 0x00810858))
    assert len(control) == 1 and int(control[0].group(1), 16) == block[0], 'no control in the loaded area'
    frames = check_block(text, block, placed_frame, int(control[0].group(2)), prefix)
    assert prefix + ': PASS' in text, 'no PASS line'
    return area, got, frames


def main():
    assert BIN.is_file(), f'{BIN} is missing (make)'
    assert (START_CARD / 'slot1' / SAVE_DIR / 'EX_DATA.00').is_file(), \
        f'the area01 start-point card is missing: {START_CARD} (make start-saves)'
    OUT.mkdir(parents=True, exist_ok=True)
    files = [f for f in card_files(START_CARD) if f.startswith(('slot1/', 'slot2/'))]
    before = prepare(CARD, files)
    empty_before = prepare(EMPTY, [])
    block = (START_CARD / 'slot1' / SAVE_DIR / 'EX_DATA.00').read_bytes()
    assert len(block) == 0x640, 'the slot is not one progress block'
    pose = json.loads((START_CARD / 'start.json').read_text())['load_pose']
    runs = {'load': start(CARD, OUT / 'load.log'), 'empty': start(EMPTY, OUT / 'empty.log')}
    rc, text = finish(*runs['load'], OUT / 'load.log')
    erc, etext = finish(*runs['empty'], OUT / 'empty.log')
    assert rc == 0, ('the load run', rc, OUT / 'load.log')
    area, got, frames = check_pass(text, block, pose)
    assert card_hashes(CARD) == before, 'the load changed the card'
    assert erc != 0 and 'handoff' not in etext and 'loadgame: PASS' not in etext, \
        ('negative control: the empty card handed off a game', erc)
    assert card_hashes(EMPTY) == empty_before, 'the empty card run wrote the card'
    print(f'title load game: PASS (the boot title\'s LOAD GAME of slot 1: area {area[0]:02X} {area[1]:02X} '
          f'{area[2]:02X}, placed at ({got[0]:.3f}, {got[1]:.3f}, {got[2]:.3f}) heading {got[3]:.5f}, '
          f'health {got[8]:.1f}, control; the block equals the slot in all 0x640 bytes after the read, the '
          f'handoff and the placement, and at control but for D_00810750 (+{frames}); the empty card refused)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
