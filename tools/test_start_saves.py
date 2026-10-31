#!/usr/bin/env python3
"""The start-point saves (tools/build_start_saves.py, docs/SAVE_GAME.md
section 7): built by the ORIGINAL save writer from each area's arrival
state, read back by the original's load code.

1. All three start points built into a temporary folder: the builder runs
   the original 00228A90 and its translation over the arrival RAM on two
   host cards (byte-identical, the layout of docs/SAVE_GAME.md section 2),
   then the original load enumeration 002283B0 and LOAD GAME's read
   00228B60 and their translations over the opt_07 RAM (equal at every
   callee entry; only the saved slot listed; D_00810700 = the slot).
2. Each card checked again here from the recording files alone: the saved
   slot is the arrival's D_00810700 (read from its eeMemory.bin) with the
   load pose D_00810710..72F = the player record's +0xA0 / +0xC0 quadwords
   (the save terminal's 00159B90 step), +0xF = 1 and the check byte that
   makes all 0x640 bytes sum to zero, the other
   four slots one block with +0xF = 0 and the area bytes zero, start.json's
   hashes the files'; the three slots differ from each other.
3. Negative controls: a slot byte flipped, then EX.ico removed: the
   original's enumeration refuses the card (the builder writes nothing);
   an unknown name, an arrival whose area byte is not the expected one and
   a missing recording are refused before anything runs, and so is an
   arrival whose player record is not placed (+0xAC not 1.0); a card folder
   holding a foreign file or a symlink is refused; a second write of the
   same card changes nothing.
4. If build/start_saves/<name> exists (python3 tools/build_start_saves.py),
   it must equal the fresh build: a stale card fails.

About 20 s, no EM_TEST_FULL variant (every start point every run).
"""
import json
import os
import struct
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# Start points whose first beats leave from one recorded state (route tables:
# a01u_00's end, a06_03's end).
SAME_SOURCE = (('a01u_01', 'a01u_s0'), ('a06_04', 'a06b'), ('a13b_02', 'a13c_00'), ('a03c_02', 'a03d_00'))
sys.path.insert(0, str(ROOT / 'tools'))
import build_start_saves as B  # noqa: E402
import test_memcard_host as MH  # noqa: E402

BLOCK, SIZE = B.BLOCK, B.SIZE


def tree(folder):
    folder = Path(folder)
    return {str(p.relative_to(folder)): p.read_bytes() for p in sorted(folder.rglob('*')) if p.is_file()}


def refused(fn, text):
    try:
        fn()
    except B.Refused as ex:
        assert text in str(ex), ('refused for another reason', str(ex), text)
        return
    raise AssertionError(('not refused', text))


def check_card(name, folder):
    a = B.arrival(name)
    raw = (a['folder'] / 'eeMemory.bin').read_bytes()
    live = bytearray(raw[BLOCK:BLOCK + SIZE])
    assert live[0] == B.START_POINTS[name]['area'], name
    # the save terminal's step (00159B90): the player record's +0xA0 / +0xC0
    # quadwords (D_00810350 / D_00810370) into the load pose D_00810710 / 720
    live[0x10:0x20] = raw[0x810350:0x810360]
    live[0x20:0x30] = raw[0x810370:0x810380]
    assert struct.unpack_from('<f', live, 0x1C)[0] == 1.0 and struct.unpack_from('<f', live, 0x2C)[0] == 1.0, \
        (name, 'the arrival\'s player record is not placed')
    live[0xF] = 1
    live[0x63F] = (0x100 - sum(live[:0x63F])) & 0xFF
    files = tree(folder)
    base = 'slot1/' + B.DIR + '/'
    assert sorted(files) == sorted(['start.json'] + ['slot1/' + f for f in B.FILES]), (name, sorted(files))
    slot = files[base + 'EX_DATA.00']
    assert slot == bytes(live) and sum(slot) & 0xFF == 0, (name, 'EX_DATA.00 is not the arrival block saved')
    others = {files[base + 'EX_DATA.%02d' % n] for n in range(1, 5)}
    assert len(others) == 1, (name, 'the four other slots differ')
    blank = others.pop()
    assert len(blank) == SIZE and blank[0xF] == 0 and blank[:3] == bytes(3), (name, 'the New Game slots')
    assert (Path(folder) / 'slot2').is_dir() and not any((Path(folder) / 'slot2').iterdir()), (name, 'slot2')
    meta = json.loads(files['start.json'])
    assert meta['name'] == name and meta['ee_sha256'] == a['ee_sha256'], name
    assert meta['block_head'] == a['block'][:16].hex(), name
    want_pose = [list(struct.unpack_from('<3f', live, 0x10)), list(struct.unpack_from('<3f', live, 0x20))]
    assert [meta['load_pose']['position'], meta['load_pose']['rotation']] == want_pose, (name, 'start.json pose')
    for rel, digest in meta['files'].items():
        assert B.sha256(files['slot1/' + rel]) == digest, (name, rel)
    return slot


def negative_controls(tmp, made_root):
    """The original's enumeration over damaged copies of a built card."""
    a = B.arrival('area01')
    good = tree(made_root)
    for damage in ('flip', 'drop'):
        root = Path(tmp) / ('damaged-' + damage)
        for rel, data in good.items():
            if rel.startswith('slot1/'):
                (root / rel).parent.mkdir(parents=True, exist_ok=True)
                (root / rel).write_bytes(data)
        (root / 'slot2').mkdir(parents=True, exist_ok=True)
        files = MH.card_files(root, 0)
        if damage == 'flip':
            p = root / 'slot1' / B.DIR / 'EX_DATA.00'
            d = bytearray(p.read_bytes())
            d[0x100] ^= 1
            p.write_bytes(bytes(d))
        else:
            (root / 'slot1' / B.DIR / 'EX.ico').unlink()
        refused(lambda: B.load(root, files, a['name']), 'the original load enumeration answered 3')


def main():
    t0 = time.time()
    names = list(B.START_POINTS)
    # refusals before anything runs
    refused(lambda: B.arrival('area03'), 'unknown start point')
    wrong = {'area01': dict(B.START_POINTS['area01'], area=0)}
    refused(lambda: B.arrival('area01', wrong), 'area byte is 1, not 0')
    missing = {'area01': dict(B.START_POINTS['area01'], route='s87/route_none')}
    refused(lambda: B.arrival('area01', missing), 'is missing')
    swapped = {'area00': dict(B.START_POINTS['area00'], first='a01_00_')}
    refused(lambda: B.arrival('area00', swapped), 'beats named a01_00_*')
    unplaced = B.arrival('area01')
    ram = bytearray(unplaced['ram'])
    ram[0x81035C:0x810360] = bytes(4)           # the player record's +0xAC (w) not 1.0
    refused(lambda: B.at_terminal(dict(unplaced, ram=bytes(ram))), 'is not placed')
    with tempfile.TemporaryDirectory(prefix='em_test_start_saves_') as tmp:
        out = Path(tmp) / 'out'
        result = B.build(names, out)
        assert sorted(result) == sorted(names), result
        slots = {}
        for n, (status, folder, (commands, entries)) in result.items():
            assert status == 'written' and folder == out / n, (n, status)
            assert commands == 26 and entries > 0, (n, commands, entries)
            slots[n] = check_card(n, folder)
        # beats that leave from one recorded state save one block, and only those
        for a, b in SAME_SOURCE:
            assert slots[a] == slots[b], (a, b, 'share a source state')
        assert len(set(slots.values())) == len(names) - len(SAME_SOURCE), 'two start points saved the same block'
        # the write is file by file and refuses what it does not own
        a = B.at_terminal(B.arrival('area01'))
        files = MH.card_files(out / 'area01', 0)
        assert B.write(out / 'area01', a, files)[0] == 'up to date'
        (out / 'area01' / 'notes.txt').write_bytes(b'x')
        refused(lambda: B.write(out / 'area01', a, files), 'holds notes.txt')
        (out / 'area01' / 'notes.txt').unlink()
        (out / 'link').symlink_to(out / 'area01')
        refused(lambda: B.write(out / 'link', a, files), 'is not a folder')
        stale = out / 'area01' / 'slot1' / B.DIR / 'EX_DATA.01'
        kept = stale.read_bytes()
        stale.write_bytes(b'\0' * len(kept))
        assert B.write(out / 'area01', a, files)[0] == 'written' and stale.read_bytes() == kept
        negative_controls(tmp, out / 'area01')
        # the developer's built cards, if any, are the fresh build
        built = [n for n in names if (B.OUT / n).exists()]
        for n in built:
            assert tree(B.OUT / n) == tree(out / n), (n, 'build/start_saves is stale: '
                                                         'run python3 tools/build_start_saves.py')
    MH.LIB.em_memcard_reset()
    print(f'{len(names)} start points built by the original writer and read back by the original load code '
          f'(both sides equal); {len(built)} of build/start_saves current')
    print('elapsed %.1f s' % (time.time() - t0))
    print('start saves: PASS')


if __name__ == '__main__':
    main()
