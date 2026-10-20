#!/usr/bin/env python3
"""Opt-in checks of the AREA00 arrival's added read views (2026-10-19).

Run on native arm64 macOS. No emulator is launched. The AREA00 arrival's
frames 0 and 1 reach three original reads the port did not serve:

  * 0022DCD0 (decomp NEARMISS C) keys AREA00 ((D_00810700 << 8) +
    D_00810701 = 0 or 1) to the 64-byte record list D_00269400 and scales
    the 0x90-byte record D_0026A900 every frame (001D0400); it keeps its
    tables in the boot BSS D_00822CF0..D_008234C0;
  * 0015A2C0 (NEARMISS C) reads the 20-byte config row D_00248120 + 20 *
    (short)+0x54; AREA00's placements use rows 1, 3, 4 and 5.

This checks, with the user's own pinned ELF and the retained fork
snapshots: every exported byte equals the ELF and every AREA00 snapshot
(the record list ends at its terminator word); changed bytes, a moved
terminator and an owner naming an unexported row are refused; no decomp
function other than 0022DCD0 names an address of the BSS block the port
serves for it (em_area01_live.c); and the production loader serves exactly
these windows (every slice, with bounds refusals) under ASan/UBSan.
All binary data stays in ignored build/levels/area00-arrival-bindings.
"""
from __future__ import annotations

import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

import export_area00_runtime_tables as X
import test_aim_fire_effects_live as A

OUT = X.E.ROOT / 'build/levels/area00-arrival-bindings'
BSS = (0x00822CF0, 0x008234C0)


def refused(check, *args):
    try:
        check(*args)
    except ValueError:
        return 1
    raise AssertionError('A corrupted table was accepted')


def check_tables(elf):
    data = X.blocks(elf)
    paths = X.captures(X.E.DECOMP)
    X.verify_windows(data, paths)
    wanted = dict(data)
    windows = (*X.LIGHT_WINDOWS, *X.FIELD_WINDOWS)
    rejected = 0
    for address, _ in windows:
        changed = dict(wanted)
        block = bytearray(changed[address])
        block[len(block) // 2] ^= 0x10
        changed[address] = bytes(block)
        rejected += refused(X.verify_windows, list(changed.items()), paths)
    # A terminator one record early, and none where the list is exported.
    table = bytearray(wanted[X.LIGHT_TABLE])
    early = bytearray(table)
    early[5 * 0x40:5 * 0x40 + 4] = b'\xff\xff\xff\xff'
    rejected += refused(X.verify_light_table, {X.LIGHT_TABLE: bytes(early)})
    late = bytearray(table)
    late[-4:] = b'\x00\x00\x00\x00'
    rejected += refused(X.verify_light_table, {X.LIGHT_TABLE: bytes(late)})
    # The retained owners name rows 0, 1, 3, 4, 5 only; a row-2 owner is refused.
    rows = set()
    for path in paths:
        rows |= X.field_rows(path.read_bytes())
    assert rows <= {0, 1, 3, 4, 5} and {3, 4} <= rows, rows
    ram = bytearray(paths[1].read_bytes())
    for slot in range(256):
        at = 0x7A5640 + slot * 0x2F0
        if ram[at] and int.from_bytes(ram[at + 0x10:at + 0x14], 'little') == 0x0015A2C0:
            ram[at + 0x54:at + 0x56] = (2).to_bytes(2, 'little')
            break
    else:
        raise AssertionError('No 0015A2C0 owner in the second retained snapshot')
    probe = OUT / 'row2-ram.bin'
    probe.write_bytes(bytes(ram))
    rejected += refused(X.verify_field_rows, [probe])
    probe.unlink()
    return data, len(paths), rejected


def check_bss_users():
    """Only func_0022DCD0.c names an address in [D_00822CF0, D_008234C0)."""
    users = {}
    pattern = re.compile(r'D_(00[0-9A-F]{6})\b')
    for path in sorted((X.E.DECOMP / 'src').glob('*.c')):
        for word in set(pattern.findall(path.read_text(errors='replace'))):
            if BSS[0] <= int(word, 16) < BSS[1]:
                users.setdefault(path.name, set()).add(word)
    assert list(users) == ['func_0022DCD0.c'], sorted(users)
    return len(users['func_0022DCD0.c'])


def check_readview(data):
    wanted = dict(data)
    windows = (*X.LIGHT_WINDOWS, *X.FIELD_WINDOWS)
    expected = OUT / 'expected.bin'
    expected.write_bytes(b''.join(wanted[address] for address, _ in windows))
    before = [(a, b) for a, b in data if a not in dict(windows)]
    for label, blocks in (('before', before), ('after', data)):
        folder = OUT / label / 'assets'
        folder.mkdir(parents=True, exist_ok=True)
        (folder / 'effect_tables.emet').write_bytes(X.E.serialize(blocks))
    sources = ['tests/area00_arrival_readview_test.c', *A.SOURCES[1:], 'src/game/em_effect_manager.c']
    flags = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
             '-ffp-contract=off', '-ffunction-sections', '-fdata-sections',
             '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections', '-Isrc']
    binary = OUT / 'readview'
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + flags + sources + ['-lm', '-o', str(binary)],
                   cwd=X.E.ROOT, check=True)
    for label, present in (('before', 0), ('after', 1)):
        subprocess.run([str(binary), str(expected), str(present)], cwd=OUT / label,
                       env=dict(os.environ, UBSAN_OPTIONS='halt_on_error=1'), check=True)
    # The installed export holds the same bytes.
    X.verify_preserved((X.E.ROOT / 'assets/effect_tables.emet').read_bytes(), data)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    elf = (X.E.DECOMP / 'config/SCUS_971.12').read_bytes()
    data, snapshots, rejected = check_tables(elf)
    names = check_bss_users()
    check_readview(data)
    print(f'PASS AREA00 arrival read views: 3 windows equal the ELF and {snapshots} retained snapshots; '
          f'{rejected} corruptions refused; the BSS block is 0022DCD0\'s alone ({names} names)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
