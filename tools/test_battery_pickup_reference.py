#!/usr/bin/env python3
"""Original149F0 pickup initialization, notice and no-device browsing.

The original page body executes from the user's ELF. Shared list drawing
is an explicit worker boundary; the existing packet test covers the same
flags402 BATTERY geometry. No original bytes are embedded here.
"""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import subprocess
import sys

from test_item_sdk_math_reference import Original, ELF_SHA

ROOT = Path(__file__).resolve().parents[1]
UI = 0x960000


def fixture(elf, counts, acquired):
    original = Original(elf)
    for kind, count in enumerate(counts):
        original.save(0x810c7f + kind, count, 1)
    original.save(0x8106b0, 1, 1)
    original.save(0x8106b1, 0x1b + acquired, 1)
    original.save(0x810cb2, 7, 2)
    original.save(0x810cb7, 11, 1)
    calls = []
    original.calls[0x20a7a0] = lambda _: calls.append(('background',))
    original.calls[0x20ae40] = lambda o: calls.append(('frame', o.r[6]))
    original.calls[0x20b0d0] = lambda _: calls.append(('close',))
    def list_worker(o):
        calls.append(('list', o.r[7]))
        o.r[2] = 0
    original.calls[0x20b210] = list_worker
    original.calls[0x20cd60] = lambda _: calls.append(('sound', 1))
    original.calls[0x20cd80] = lambda _: calls.append(('sound', 2))
    original.calls[0x185420] = lambda o: o.r.__setitem__(2, 0)
    original.run(0x2149f0, (UI,))
    assert not calls
    selected = max(i for i, count in enumerate(counts) if count)
    capacity = (12, 36, 48)[acquired]
    assert original.load(UI + 0x18, 1) == 1
    assert original.load(UI + 0x50, 1) == selected
    assert original.load(0x810cb2, 2) == original.load(0x810cb7, 1) == capacity
    assert original.load(0x8106b0, 1) == 0
    assert original.load(UI + 5, 1) == 3 and original.load(UI + 6, 1) == 240
    assert original.load(0x282240) == (4 if selected == acquired else 3)
    return original, calls, selected, capacity


def main():
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    output = ROOT / 'build/battery_pickup_reference'
    output.mkdir(parents=True, exist_ok=True)
    library = output / ('pickup.dylib' if sys.platform == 'darwin' else 'pickup.so')
    # Existing non-GPU graphics stubs supply the link boundary. Its main is
    # never called from this library; no rendered command log is produced.
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
        '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-Isrc',
        'tests/battery_ui_test.c', 'src/game/em_battery_ui.c', 'src/game/em_panel.c',
        '-lm', '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.em_battery_ui_load.argtypes = [C.c_char_p]
    native.em_battery_ui_load.restype = C.c_void_p
    native.em_battery_ui_begin_pickup.argtypes = [C.c_void_p, C.c_int, C.c_int, C.c_int]
    native.em_battery_ui_original_step.argtypes = [C.c_void_p]
    native.em_battery_ui_tick.argtypes = [C.c_void_p, C.c_uint, C.POINTER(C.c_int), C.c_int]
    native.em_battery_ui_free.argtypes = [C.c_void_p]
    checks = cases = 0
    for counts in itertools.product((0, 1), repeat=3):
        for acquired in range(3):
            if not counts[acquired]:
                continue
            for press_frame, button in ((None, 0), (0, 0x40), (0, 0x20), (29, 0x1000),
                                        (239, 0x4000), (0, 0x8000), (0, 0x2000)):
                original, calls, selected, capacity = fixture(elf, counts, acquired)
                ui = native.em_battery_ui_load(str(ROOT / 'assets/scene_snow/panel/battery.emba').encode())
                assert ui and native.em_battery_ui_begin_pickup(ui, capacity, selected, acquired)
                charge = C.c_int(capacity)
                for frame in range(240):
                    pressed = button if frame == press_frame else 0
                    calls.clear()
                    original.save(0x810e74, pressed, 2)
                    original.run(0x2149f0, (UI,))
                    events = native.em_battery_ui_tick(ui, pressed, C.byref(charge), 0)
                    assert native.em_battery_ui_original_step(ui) == original.load(UI + 5, 1)
                    assert events == (4 if ('sound', 1) in calls else 0)
                    assert calls[:4] == [('background',), ('frame', 2), ('list', 0x402), ('close',)]
                    assert charge.value == capacity
                    checks += 1
                    if original.load(UI + 5, 1) == 1:
                        break
                # A real empty185420 lookup raises the original error banner,
                # retaining status ownership. It cannot synthesize a panel.
                calls.clear()
                original.save(0x810e74, 0x40, 2)
                original.run(0x2149f0, (UI,))
                assert native.em_battery_ui_tick(ui, 0x40, C.byref(charge), 0) == 64
                assert native.em_battery_ui_original_step(ui) == original.load(UI + 5, 1) == 8
                assert ('sound', 2) in calls
                native.em_battery_ui_free(ui)
                cases += 1
    result = {'initialization_and_input_cases': cases, 'original_notice_callbacks': checks,
              'original_capacity_and_group_selection': 'PASS', 'no_device_browse': 'PASS',
              'boundaries': 'shared list/draw workers, native frame host and final pixels'}
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
