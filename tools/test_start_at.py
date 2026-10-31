#!/usr/bin/env python3
"""EM_START_AT=<name>, the developer switch (em_start_at.h; docs/STARTUP.md
"Developer switches", docs/SAVE_GAME.md section 8).

`make test-start-at`. Needs the start-point cards (make start-saves).

1. EM_START_AT=area01 EM_START_AT_TEST=1: the boot title's LOAD GAME on the
   area01 start-point card itself (read-only), checked as
   tools/test_title_load_game.py checks its copy (check_pass: the screen's
   end with the result 2, the handoff into the slot's area, the placement at
   the slot's load pose, the health, control in AREA01, and the whole
   0x640-byte progress block equal to the slot after the read, the handoff
   and the placement, and at control but for D_00810750's frame count);
   the card server's own line that it serves the card read-only; and the
   handback probe: after control a button held through the test buttons
   (ORed in before the outer input layer, which a still-installed driven
   pad would erase) reaches D_00810E70.
2. EM_START_AT=area01 alone (EM_HEADLESS=1, EM_UNCAPPED=0: paced, as a
   windowed run is): the switch as a developer runs it. It reaches control in AREA01 with the same lines
   (no block lines, no quit: the run goes on as the player's) and the card
   server's read-only line, faster than
   the 1607 paced frames would take (the boot runs unpaced to control);
   the run is ended here after control.
3. The card is unchanged by both runs (every file's SHA-256).
4. Refusals, each before the window opens (exit 1, the reason, no frame
   run): an unknown name; EM_NEW_GAME=1, EM_SKIP_STARTUP=1, EM_STARTUP_TEST
   (also set but empty), EM_TEST_MEMCARD_ROOT, EM_INPUT_PLAY, EM_INPUT_RECORD
   with it; every other EM_* name the source reads (getenv in src/) that is
   not in COMPATIBLE (each of which docs/STARTUP.md must name);
   EM_START_AT_DIR without EM_START_AT_TEST; and, on copies of the card under
   a temporary folder (EM_START_AT_DIR): a missing card, a missing file,
   a slot byte changed (the SHA-256), start.json without the load pose
   (a card from before save2 round 4), a foreign file in slot1, a file in
   slot2, a symlink, and, with start.json's hashes made to agree, a slot
   whose check byte is wrong, whose area byte is not the name's, or whose
   load pose is not start.json's.

EM_TEST_FULL=1 also runs area00 and area01-revisit (step 1's checks) and
reports how far each gets; they are not asserted (the drawbridge run is
still finishing those areas on port main).
"""
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_title_load_game as tl  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get('EM_START_AT_BIN', str(ROOT / 'build/extermination')))
SAVES = ROOT / 'build/start_saves'
OUT = ROOT / 'build/start_at_test'
SAVE_DIR = 'BASCUS-97112-DS00-00'
SLOT = 'slot1/' + SAVE_DIR + '/EX_DATA.00'
PREFIX = 'startup: EM_START_AT'
FULL = os.environ.get('EM_TEST_FULL') == '1'
# The EM_* variables EM_START_AT runs with (em_start_at.c fixture_set's
# comment; docs/STARTUP.md "EM_START_AT=<name>", Compatible): every other
# EM_* name the source reads must be refused with it (step 4).
COMPATIBLE = {
    'EM_START_AT', 'EM_START_AT_TEST', 'EM_START_AT_DIR', 'EM_START_AT_PAD', 'EM_START_AT_PATH', 'EM_START_AT_RAND', 'EM_START_AT_RANDS', 'EM_START_AT_LOADS', 'EM_START_AT_TRACE', 'EM_START_AT_RESTORE', 'EM_START_AT_SYNC', 'EM_START_AT_FADE',
    'EM_START_AT_MARKS',
    'EM_NEW_GAME',  # its own; EM_NEW_GAME=1 refused apart
    'EM_HEADLESS', 'EM_HEADLESS_RASTER', 'EM_UNCAPPED', 'EM_MUTE', 'EM_INPUT_TEST',  # the host; the pad's changes printed
    'EM_PS2_DRIVE_REPLAY', 'EM_IOP_TICK_PHASE',                           # the disc-drive timing
    'EM_GS_BANDS', 'EM_GS_EARLY_OPS', 'EM_GS_EARLY_PARSE', 'EM_GS_LIST', 'EM_GS_SPEC', 'EM_GS_SPEC_CHECK',
    'EM_GS_TEXCACHE', 'EM_GS_THREADS',                                    # the GS head (the same bytes)
    'EM_HUD_FORCE', 'EM_HUD_HOVER', 'EM_AIM_FIRE_TABLES',                 # presentation; a data path
    'EM_AREA_CHANGE_LOG', 'EM_BOX_DUMP', 'EM_CAMERA_TRACE', 'EM_CAM_PRINT', 'EM_COLL_WORLD_DUMP',
    'EM_CONTROL_TRACE', 'EM_DIAG_DIGESTS', 'EM_FB_CAPTURE_TICKS', 'EM_FRAME_TIMING', 'EM_FRAME_TRACE',
    'EM_GS_LIST_COUNTERS', 'EM_GS_SPEC_COUNTERS', 'EM_GS_TC_COUNTERS', 'EM_GS_TEX_COUNTERS',
    'EM_GS_Z_COUNTERS', 'EM_LOG_AIM_RECORDS', 'EM_PRESENT_LOG', 'EM_PRESENT_PX', 'EM_PROBE_TRACE',
    'EM_RAND_TRACE', 'EM_STARTUP_CAPTURE_DIR', 'EM_STATUS_PAGES_TRACE', 'EM_STEP_TRACE',  # observe only
    'EM_GLOW_TRACE',                                                      # observe only
    'EM_STATUS_GS_DUMP', 'EM_LAMP_PAGE_DUMP',                             # a dump file: observe only
    'EM_LEVEL_SMOKE_FB_CAPTURE', 'EM_LEVEL_SMOKE_HUB_CAPTURE', 'EM_LEVEL_SMOKE_MESSAGE_CAPTURE',
    'EM_LEVEL_SMOKE_PAGES_CAPTURE', 'EM_LEVEL_SMOKE_PAGES_DEBUG', 'EM_LEVEL_SMOKE_PAGE_CAPTURE',
    'EM_LEVEL_SMOKE_PHASE_CAPTURE', 'EM_REPLAY_AUDIO', 'EM_REPLAY_CAPTURE', 'EM_REPLAY_CAPTURE_EVERY',
    'EM_REPLAY_MAX_OVERRUN', 'EM_AUDIO_FILE',                             # only under a refused fixture
    'EM_SKIP_SCENE', 'EM_SKIP_DELAY', 'EM_SKIP_RUN',                      # the cutscene skip fixture (START only)
}
# Load-time self-tests (em_collision.c's constructor): they run and exit
# before main, so the switch is never read and no frame runs.
SELFTESTS = {'EM_CARRY_TEST', 'EM_BLOCKER_TEST'}


def env(**kw):
    """The caller's environment without any EM_* variable, and `kw`."""
    e = {k: v for k, v in os.environ.items() if not k.startswith('EM_')}
    e.update(kw)
    return e


def source_names():
    """Every EM_* name the port's source reads with getenv."""
    names = set()
    for p in (ROOT / 'src').rglob('*'):
        if p.suffix in ('.c', '.h', '.m') and p.is_file():
            names |= set(re.findall(r'getenv\("(EM_[A-Z0-9_]+)"\)', p.read_text(errors='replace')))
    return names


def card_hashes(root):
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(root.rglob('*')) if p.is_file()}


def run_report(name, log_path):
    """Step 1: EM_START_AT=<name> EM_START_AT_TEST=1 to its PASS (or a fault,
    which stops the game task but not the process: the run is ended then)."""
    log = open(log_path, 'w')
    proc = subprocess.Popen([str(BIN)], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                            env=env(EM_START_AT=name, EM_START_AT_TEST='1', EM_UNCAPPED='1'))
    return tl.finish(proc, log, log_path)


def run_switch(log_path):
    """Step 2: the switch alone, headless and paced (EM_UNCAPPED=0: a headless
    run is otherwise not paced, docs/PERFORMANCE.md P18, and the boot's own
    unpacing would go unseen); ended 1 s after control. The seconds from the
    first frame (engine frame 0) to control."""
    log = open(log_path, 'w')
    t0 = time.time()
    proc = subprocess.Popen([str(BIN)], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                            env=env(EM_START_AT='area01', EM_HEADLESS='1', EM_UNCAPPED='0'))
    seen = first = None
    while proc.poll() is None and time.time() - t0 < 120:
        time.sleep(0.1)
        text = log_path.read_text(errors='replace')
        if first is None and 'startup: screen 0, engine frame 0\n' in text:
            first = time.time()
        if seen is None and first is not None and PREFIX + ': control in the loaded area' in text:
            seen = time.time() - first
        if seen is not None and time.time() - t0 > seen + 1.0:
            break
        if 'em_scene: FAULT' in text:
            break
    alive = proc.poll() is None
    if alive:
        proc.kill()
    proc.wait()
    log.close()
    return seen, alive, log_path.read_text(errors='replace')


def refused(why, expect, **kw):
    """The binary refuses before any frame: exit 1, `expect` in its output."""
    r = subprocess.run([str(BIN)], cwd=ROOT, env=env(**kw), capture_output=True, text=True, timeout=60)
    out = r.stdout + r.stderr
    assert r.returncode == 1 and 'refused' in out and expect in out and 'startup: screen' not in out, \
        ('not refused', why, r.returncode, out[-600:])
    return out


def why_of(why, expect, **kw):
    refused(why, expect, **kw)
    return why


def copy_card(tmp, name, src):
    """<tmp>/<name>/area01: a copy of the card (tmp is the run's temporary
    folder, removed with it)."""
    root = tmp / name / 'area01'
    for d in ('slot1/' + SAVE_DIR, 'slot2'):
        (root / d).mkdir(parents=True)
    for p in src.rglob('*'):
        if p.is_file():
            shutil.copyfile(p, root / p.relative_to(src))
    return root


def rehash(root):
    """start.json's file hashes made to agree with the card's files."""
    j = json.loads((root / 'start.json').read_text())
    for f in j['files']:
        j['files'][f] = hashlib.sha256((root / 'slot1' / f).read_bytes()).hexdigest()
    (root / 'start.json').write_text(json.dumps(j, indent=1))


def fix_check(block):
    b = bytearray(block)
    b[0x63F] = (-sum(b[:0x63F])) & 0xFF
    return bytes(b)


def refusals(src, tmp):
    done = []
    done.append(why_of('unknown name', 'an unknown start point', EM_START_AT='area03'))
    for k, v, expect in (('EM_NEW_GAME', '1', 'EM_NEW_GAME'), ('EM_SKIP_STARTUP', '1', 'EM_SKIP_STARTUP'),
                         ('EM_STARTUP_TEST', 'loadgame', 'EM_STARTUP_TEST'),
                         ('EM_TEST_MEMCARD_ROOT', str(src), 'EM_TEST_MEMCARD_ROOT'),
                         ('EM_INPUT_PLAY', 'x.rec', 'EM_INPUT_PLAY'), ('EM_INPUT_RECORD', 'x.rec', 'EM_INPUT_RECORD'),
                         ('EM_STARTUP_TEST', '', 'EM_STARTUP_TEST')):   # set but empty: f.test reads it
        done.append(why_of(k, expect, EM_START_AT='area01', **{k: v}))
    # every other EM_* name the source reads: refused, or listed compatible
    # (and named in docs/STARTUP.md's EM_START_AT section)
    doc = (ROOT / 'docs/STARTUP.md').read_text()
    sec = doc[doc.index('### EM_START_AT=<name>'):doc.index('### EM_SKIP_STARTUP=1')]
    names = source_names()
    assert {'EM_LEVEL_SMOKE_UNTIL', 'EM_AIM_FIRE_TEST', 'EM_SCENE', 'EM_CAPTURE_WALK'} <= names, 'the source scan'
    undocumented = sorted(n for n in COMPATIBLE | SELFTESTS if n not in sec)
    assert not undocumented, ('compatible but not in docs/STARTUP.md', undocumented)
    for k in sorted(SELFTESTS):
        r = subprocess.run([str(BIN)], cwd=ROOT, env=env(EM_START_AT='area01', **{k: '1'}), capture_output=True,
                           text=True, timeout=60)
        out = r.stdout + r.stderr
        assert 'PASS' in out and 'startup: ' not in out and 'EM_START_AT' not in out, ('self-test', k, out[-400:])
    fixtures = sorted(names - COMPATIBLE - SELFTESTS - {'EM_NEW_GAME', 'EM_SKIP_STARTUP', 'EM_STARTUP_TEST',
                                            'EM_TEST_MEMCARD_ROOT', 'EM_INPUT_PLAY', 'EM_INPUT_RECORD'})
    for k in fixtures:
        # EM_SCENE with a folder that resolves: refused before its staging
        v = str(ROOT / 'build') if k == 'EM_SCENE' else '1'
        out = refused(k, 'refused: a switch that drives the pad or chooses another route, input or card: ' + k,
                      EM_START_AT='area01', **{k: v})
        assert 'scene: ' not in out, ('EM_SCENE staged before the refusal', out[-400:])
    print(f'start at: {len(fixtures)} more fixtures refused: {" ".join(fixtures)}')
    done += fixtures
    done.append(why_of('EM_START_AT_DIR without the test', 'EM_START_AT_DIR is test input',
                        EM_START_AT='area01', EM_START_AT_DIR=str(OUT)))
    (tmp / 'missing').mkdir()
    done.append(why_of('missing card', 'not built', EM_START_AT='area01', EM_START_AT_TEST='1',
                        EM_START_AT_DIR=str(tmp / 'missing')))

    def case(tag, expect, change):
        root = copy_card(tmp, tag, src)
        change(root)
        done.append(why_of(tag, expect, EM_START_AT='area01', EM_START_AT_TEST='1',
                            EM_START_AT_DIR=str(tmp / tag)))

    def no_file(r):
        (r / 'slot1' / SAVE_DIR / 'EX_DATA.01').replace(r / 'EX_DATA.01.moved')

    def flip(r):
        p = r / SLOT
        b = bytearray(p.read_bytes())
        b[0x100] ^= 1
        p.write_bytes(bytes(b))

    def no_pose(r):
        j = json.loads((r / 'start.json').read_text())
        del j['load_pose']
        (r / 'start.json').write_text(json.dumps(j, indent=1))

    def foreign(r):
        (r / 'slot1' / SAVE_DIR / 'EXTRA').write_bytes(b'x')

    def slot2_file(r):
        (r / 'slot2' / 'x').write_bytes(b'x')

    def link(r):
        # a link with EX_DATA.04's name and EX_DATA.03's bytes (the same as
        # EX_DATA.04's: the hash alone would pass it), moved over the file
        q = r / 'EX_DATA.04.link'
        q.symlink_to(r / 'slot1' / SAVE_DIR / 'EX_DATA.03')
        q.replace(r / 'slot1' / SAVE_DIR / 'EX_DATA.04')

    def check_byte(r):
        p = r / SLOT
        b = bytearray(p.read_bytes())
        b[0x63F] ^= 0x80
        p.write_bytes(bytes(b))
        rehash(r)

    def area_byte(r):
        p = r / SLOT
        b = bytearray(p.read_bytes())
        b[0] = 0x02
        p.write_bytes(fix_check(b))
        rehash(r)

    def pose(r):
        p = r / SLOT
        b = bytearray(p.read_bytes())
        struct.pack_into('<f', b, 0x10, struct.unpack_from('<f', b, 0x10)[0] + 1.0)
        p.write_bytes(fix_check(b))
        rehash(r)

    case('no_file', 'is missing', no_file)
    case('flip', 'SHA-256', flip)
    case('no_pose', 'no load pose', no_pose)
    case('foreign', 'slot1 holds', foreign)
    case('slot2_file', 'slot2 is not an empty folder', slot2_file)
    case('link', 'not a regular file', link)
    case('check_byte', 'check byte', check_byte)
    case('area_byte', 'area byte', area_byte)
    case('pose', 'load pose', pose)
    return done


def main():
    t0 = time.time()
    assert BIN.is_file(), f'{BIN} is missing (make)'
    src = SAVES / 'area01'
    assert (src / SLOT).is_file(), f'the area01 start-point card is missing: {src} (make start-saves)'
    OUT.mkdir(parents=True, exist_ok=True)
    before = card_hashes(src)
    block = (src / SLOT).read_bytes()
    pose = json.loads((src / 'start.json').read_text())['load_pose']

    rc, text = run_report('area01', OUT / 'area01.log')
    assert rc == 0, ('the EM_START_AT=area01 run', rc, OUT / 'area01.log')
    area, got, frames = tl.check_pass(text, block, pose, PREFIX)
    assert 'read-only' in text and str(src.relative_to(ROOT)) in text, 'the run did not use the start-point card'
    ro = f'memory card: {src.relative_to(ROOT)} served read-only'
    assert ro in text, ('the card server did not serve the card read-only', ro)
    hand = [l for l in text.splitlines() if l.startswith(PREFIX + ": the pad is the player's after the handoff")]
    assert len(hand) == 1, 'no handback: the pad is not the player\'s after the loaded game\'s handoff'

    seen, alive, stext = run_switch(OUT / 'switch.log')
    assert seen is not None, ('the switch alone did not reach control', OUT / 'switch.log')
    assert alive, 'the switch alone quit by itself (it must go on as the player\'s run)'
    assert ': block ' not in stext and ': PASS' not in stext and 'em_scene: FAULT' not in stext, \
        'the switch alone reported as the test or faulted'
    assert ro in stext, ('the switch alone: the card server did not serve the card read-only', ro)
    ctl = [l for l in stext.splitlines() if l.startswith(PREFIX + ': control in the loaded area 01 ')]
    assert len(ctl) == 1 and f'({got[0]:.3f},' in ctl[0], ('the switch alone: control', ctl)
    # paced, the frames from engine frame 0 to control (1607) take 26.8 s
    assert seen < 15.0, ('the boot to control was not unpaced', seen)

    assert card_hashes(src) == before, 'a run changed the start-point card'
    with tempfile.TemporaryDirectory(prefix='em_start_at_') as tmp:
        done = refusals(src, Path(tmp))
    assert card_hashes(src) == before, 'a refusal run changed the start-point card'

    extra = []
    if FULL:
        tl.TIMEOUT = 120   # a run that neither faults nor reaches control is ended after 120 s
        for name in ('area00', 'area01-revisit'):
            erc, etext = run_report(name, OUT / f'{name}.log')
            steps = [k for k, s in (('handoff', PREFIX + ": the loaded game's handoff"),
                                    ('placed', PREFIX + ': placed at'),
                                    ('control', PREFIX + ': control in the loaded area'),
                                    ('pass', PREFIX + ': PASS')) if s in etext]
            fault = [l for l in etext.splitlines() if 'em_scene: FAULT' in l or 'RUN_MAX' in l]
            if 'control' not in steps and not any('FAULT' in l for l in fault):
                fault.append(f'no control within {tl.TIMEOUT} s')
            extra.append(f'{name}: {" -> ".join(steps) or "nothing"}' + (f' ({"; ".join(fault)})' if fault else ''))
            print('full:', extra[-1])
    print(f'elapsed {time.time() - t0:.1f} s')
    print(f'start at: PASS (area01: the boot title\'s LOAD GAME of the start-point card, area {area[0]:02X} '
          f'{area[1]:02X} {area[2]:02X}, placed at ({got[0]:.3f}, {got[1]:.3f}, {got[2]:.3f}) heading '
          f'{got[3]:.5f}, control; the block equals the slot in all 0x640 bytes but D_00810750 (+{frames}); '
          f'the switch alone reached control {seen:.1f} s after its first frame and went on; the card unchanged; '
          f'{len(done)} refusals)' + (f'; {"; ".join(extra)}' if extra else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
