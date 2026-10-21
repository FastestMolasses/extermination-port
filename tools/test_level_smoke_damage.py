#!/usr/bin/env python3
"""The DAMAGE side runs of the level smoke, in parallel (docs/DAMAGE.md
section 8; LEVEL_SMOKE.md "The DAMAGE side runs").

Each run plays the main line to the side phase's source, then the phase
(em_level_smoke_test.c, the capture lane DAMAGE's closed-loop policies):
dmg_flame (from crevice_prompt: the flame's contacts to 35, to 10 and to 0,
the death, the game over with no input, the title after the death, Up and
Cross, the New Game to first control; the recordings dmg_00..dmg_04),
dmg_load (from crevice_prompt: dmg_flame's way to the title after the death,
then Cross on LOAD GAME, the memory-card screen to its slot choice, Triangle
back to the title menu; the recordings dmg_00..dmg_03 and dmg_05),
dmg_load_game (from crevice_prompt: dmg_load's way to the load screen, then
LOAD GAME of slot 1 on a copy of the area01 start-point card, the loaded
game into its area to control; no recording of a load, docs/SAVE_GAME.md
section 6.3), dmg_crevice_fall (from crevice_prompt: the walking jump short of the north
block, the landing hit; dmg_06), dmg_pit_fall (from truck_preview: the
truck's fall, the walk off its roof onto the pit floor with 001755B0 at the
fall start, the game over; dmg_07) and dmg_fan (from roger: fan r2's hit and
the reaction 0021E9C0; dmg_08, a replay of its pad aligned on the fan's
cycle). tools/test_level_smoke.py checks each run as every level-smoke
run, and tools/level_smoke_damage.py its side phase window by window
against the recordings (decomp build/c10/damage, docs/CAPTURES_C10.md
"DAMAGE"). dmg_pit_fall and dmg_fan log the whole player record
(EM_LOG_AIM_RECORDS=1): the fall start's and the fan hit's record words.

The runs go side by side, then their checks. dmg_flame plays the intro
movie of its second New Game in full (the recording does: no input there),
like every run under EM_UNCAPPED decoded as fast as the host allows.
EM_DAMAGE_SIDES=a,b runs only those.
"""
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get('EM_LEVEL_SMOKE_BIN', str(ROOT / 'build/extermination')))
OUT = ROOT / 'build/level_smoke_damage'
SIDES = ('dmg_flame', 'dmg_load', 'dmg_load_game', 'dmg_crevice_fall', 'dmg_pit_fall', 'dmg_fan')
RECORDS = ('dmg_pit_fall', 'dmg_fan')   # EM_LOG_AIM_RECORDS=1: the tick log's "aimrec"
SIDES = tuple(os.environ['EM_DAMAGE_SIDES'].split(',')) if os.environ.get('EM_DAMAGE_SIDES') else SIDES
import reference_root as refs  # noqa: E402  (EM_REFERENCE: fork by default)
CAPTURES = refs.capture('c10/damage')


# dmg_load_game's card: the area01 start-point card (make start-saves,
# docs/SAVE_GAME.md section 7) copied file by file into its own host card
# folder (EM_TEST_MEMCARD_ROOT), never the game's data/memcard.
START_CARD = ROOT / 'build/start_saves/area01'
LOAD_CARD = OUT / 'dmg_load_game_card'


def card_files(root):
    return sorted(p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file() or p.is_symlink())


def card_hashes(root):
    return {f: hashlib.sha256((root / f).read_bytes()).hexdigest() for f in card_files(root)}


def prepare_load_card():
    """The copy, rewritten from the start-point card each run; a copy
    holding any other file or a symlink is refused (nothing is deleted)."""
    assert (START_CARD / 'slot1').is_dir() and (START_CARD / 'slot2').is_dir(), \
        f'dmg_load_game: the start-point card is missing: {START_CARD} (make start-saves)'
    want = [f for f in card_files(START_CARD) if f.startswith(('slot1/', 'slot2/'))]
    if LOAD_CARD.exists():
        extra = [f for f in card_files(LOAD_CARD) if f not in want]
        links = [p for p in LOAD_CARD.rglob('*') if p.is_symlink()]
        assert not extra and not links, ('dmg_load_game: the card copy holds other files', extra, links)
    for d in ('slot1', 'slot2'):
        (LOAD_CARD / d).mkdir(parents=True, exist_ok=True)
    for f in want:
        (LOAD_CARD / f).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(START_CARD / f, LOAD_CARD / f)
    return card_hashes(LOAD_CARD)


def main():
    assert CAPTURES.is_dir(), f'DAMAGE captures missing: {CAPTURES} (decomp docs/CAPTURES_C10.md "DAMAGE")'
    OUT.mkdir(parents=True, exist_ok=True)
    card_before = prepare_load_card() if 'dmg_load_game' in SIDES else None
    runs = {}
    for side in SIDES:
        env = dict(os.environ, EM_UNCAPPED='1', EM_STARTUP_TEST='newgame-level', EM_LEVEL_SMOKE_UNTIL=side,
                   EM_AREA_CHANGE_LOG=str(OUT / f'{side}.jsonl'), EM_RAND_TRACE=str(OUT / f'{side}.rand'))
        if side in RECORDS:
            env['EM_LOG_AIM_RECORDS'] = '1'
        if side == 'dmg_load_game':
            env['EM_TEST_MEMCARD_ROOT'] = str(LOAD_CARD)
        log = open(OUT / f'{side}.log', 'w')
        runs[side] = (subprocess.Popen([str(BIN)], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT), log)
    failed, checks = [], {}
    for side, (proc, log) in runs.items():
        rc = proc.wait(timeout=2400)
        log.close()
        text = (OUT / f'{side}.log').read_text(errors='replace')
        for line in text.splitlines():
            if line.startswith('level smoke:') and (f'{side}: PASS' in line or 'FAIL' in line):
                print(line)
        if rc != 0:
            failed.append((side, 'run', rc))
            continue
        checks[side] = subprocess.Popen([sys.executable, str(ROOT / 'tools/test_level_smoke.py'),
                                         '--log', str(OUT / f'{side}.jsonl'), '--run-log', str(OUT / f'{side}.log'),
                                         '--rand-trace', str(OUT / f'{side}.rand'), '--require-through', side],
                                        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                        env=dict(os.environ, EM_TEST_JOBS='2'))
    for side, check in checks.items():
        out, _ = check.communicate(timeout=2400)
        for line in out.splitlines():
            if line.startswith(f'level smoke: {side}:') or line.startswith('level smoke: PASS') or check.returncode:
                print(line)
        if check.returncode:
            failed.append((side, 'check', check.returncode))
    if card_before is not None and 'dmg_load_game' in checks:
        # A load writes nothing on the card.
        if card_hashes(LOAD_CARD) != card_before:
            failed.append(('dmg_load_game', 'the load changed the card', 1))
    assert not failed, ('damage side runs failed', failed)
    print(f'level smoke damage: PASS ({len(SIDES)} side runs: {", ".join(SIDES)})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
