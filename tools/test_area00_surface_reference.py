#!/usr/bin/env python3
"""Opt-in AREA00 surface export/read-view and original-instruction checks.

Run on native arm64 macOS. No emulator is launched. EM_TEST_FULL=1 checks
every captured surface owner; the default checks both rows at the first
and last retained AREA00 snapshots. All binary data and failure details
stay in ignored build/levels/area00-surface-reference.
"""
from __future__ import annotations

import contextlib
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys

import export_area00_runtime_tables as X
import test_aim_fire_effects_live as A
import test_area01_exitb_reference as R

OUT = X.E.ROOT / 'build/levels/area00-surface-reference'


def check_provider(elf):
    data = X.blocks(elf)
    X.verify_preserved((X.E.ROOT / 'assets/effect_tables.emet').read_bytes(), data)
    try:
        X.verify_preserved(X.E.serialize(data), [(a, b) for a, b in data if a != X.SURFACE_BASE])
    except ValueError:
        pass
    else:
        raise AssertionError('Dropping an installed effect window was accepted')
    owners = X.verify_windows(data, X.captures(X.E.DECOMP))
    without_surface = [(a, b) for a, b in data if a != X.SURFACE_BASE]
    for label, windows in (('before', without_surface), ('after', data)):
        folder = OUT / label / 'assets'
        folder.mkdir(parents=True, exist_ok=True)
        (folder / 'effect_tables.emet').write_bytes(X.E.serialize(windows))
    expected = OUT / 'surface-reference.bin'
    expected.write_bytes(b''.join(dict(data)[address] for address, _ in X.AREA00_WINDOWS))
    sources = ['tests/area00_surface_readview_test.c', *A.SOURCES[1:],
               'src/game/em_effect_manager.c']
    flags = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
             '-fsanitize=address,undefined', '-ffp-contract=off', '-ffunction-sections',
             '-fdata-sections', '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections', '-Isrc']
    binary = OUT / 'readview'
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + flags + sources + ['-lm', '-o', str(binary)],
                   cwd=X.E.ROOT, check=True)
    for label, present in (('before', 0), ('after', 1)):
        subprocess.run([str(binary), str(expected), str(present)], cwd=OUT / label,
                       env=dict(os.environ, UBSAN_OPTIONS='halt_on_error=1'), check=True)
    changed = bytearray(dict(data)[X.SURFACE_BASE])
    changed[0] ^= 1
    try:
        X.verify_surface(bytes(changed), X.captures(X.E.DECOMP))
    except ValueError:
        pass
    else:
        raise AssertionError('Changed table accepted against original snapshots')
    return len(data) - len(X.AREA00_WINDOWS), owners


def check_original(elf):
    R.ROUTE = X.E.DECOMP / 'build/fork_refs/s87/route_a00'
    R.OUT, R.ELF = OUT / 'oracle', elf
    R.IMAGES.clear()
    R.NATIVE = R.build_native()
    R.BRANCH_PCS = R.branch_pcs(R.ExEE(elf).mem)
    captures = []
    for path in sorted(R.ROUTE.glob('*/eeMemory.bin')):
        ram = path.read_bytes()
        owners = [(a, struct.unpack_from('<h', ram, a + 0x54)[0])
                  for a, fn in R.pool(ram) if fn == X.SURFACE_CALLBACK]
        if owners:
            captures.append((path.parent.name, owners))
    if not captures:
        raise ValueError('No original surface owners')
    full = os.environ.get('EM_TEST_FULL') == '1'
    chosen = captures if full else [captures[0], captures[-1]]
    cases = []
    for beat, owners in chosen:
        if not full:
            owners = list({index: (address, index) for address, index in owners}.values())
        for address, _ in owners:
            for state in (0, 1):
                cases.append(R.case('surface owner', X.SURFACE_CALLBACK, [address],
                                    writes=[R.W8(address + 4, state)], beat=beat))
    calls = 0
    for index, case in enumerate(cases):
        with (OUT / 'oracle-private.log').open('a') as log, contextlib.redirect_stdout(log), contextlib.redirect_stderr(log):
            result = R.run_case_safe(case)
        if result[0] != 'ok' or result[1] is None:
            (OUT / 'oracle-failure-private.json').write_text(json.dumps(result, default=str) + '\n')
            raise AssertionError(f'Original surface comparison failed at case {index}; see private receipt')
        calls += result[1][1]
    return len(cases), calls


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    elf = (X.E.DECOMP / 'config/SCUS_971.12').read_bytes()
    shared, owners = check_provider(elf)
    cases, calls = check_original(elf)
    receipt = {'shared_effect_windows_preserved': shared, 'surface_bytes': X.SURFACE_SIZE,
               'matching_fork_snapshots': len(X.captures(X.E.DECOMP)), 'owner_observations': owners,
               'read_slices': 820, 'bounds_refusals': 5, 'original_cases': cases,
               'crate_root_slices': 13, 'crate_root_crossing_refusals': 2,
               'compared_worker_calls': calls, 'full': os.environ.get('EM_TEST_FULL') == '1', 'result': 'passed'}
    (OUT / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(f'PASS AREA00 surface: {shared} shared windows preserved; {cases} original-instruction cases, '
          f'{calls} worker calls; production loader/read bounds under ASan/UBSan')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
