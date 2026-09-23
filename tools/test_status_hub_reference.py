#!/usr/bin/env python3
"""Original CDC0 normal hub and D930 table0, at explicit draw boundaries."""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import struct
import subprocess
import sys

from test_item_sdk_math_reference import Original, ELF_SHA
from test_status_page_reference import State, FIELDS, ITEM_FIELDS
from test_item_trail_reference import Stick
from test_point_light_reference import bits, number, signed
from reference_mode import MODE, banner, part, select

ROOT = Path(__file__).resolve().parents[1]
UI, DRAW = 0x810130, 0x970000


def expected(elf, initial, infection, buttons, stick):
    original = Original(elf)
    for name, address, size in FIELDS:
        original.save(address, getattr(initial, name), size)
    for name, address, size in ITEM_FIELDS:
        original.save(address, getattr(initial.item, name), size)
    original.save(0x81085c, bits(infection))
    original.save(0x810e74, buttons, 2)
    calls = []
    for address, event in ((0x1afeb0, 0), (0x1afe60, 1), (0x20e020, 2),
                            (0x20e250, 4), (0x1b0000, 6), (0x209df0, 7)):
        original.calls[address] = lambda _, e=event: calls.append((e, 0))
    def install(o):
        calls.append((3, 0x20e6f0))
        o.r[2] = DRAW
    original.calls[0x1aff10] = install
    def background(o):
        assert o.r[4] == 0x20045ee59d421e40
        calls.append((5, 0))
    original.calls[0x20a7a0] = background
    def sample(o):
        assert o.r[4] == 0x700038a0
        o.write(o.r[4], bytes(stick))
    original.calls[0x1b62c0] = sample
    def sound(o):
        assert o.r[5:8] == [0x1000] * 3
        calls.append((8, o.r[4]))
    original.calls[0x1fb9f0] = sound
    original.run(0x20cdc0)
    if initial.step == 0:
        assert original.load(DRAW + 0x10) == 0x20e6f0
    output = State()
    for name, address, size in FIELDS:
        setattr(output, name, original.load(address, size))
    for name, address, size in ITEM_FIELDS:
        setattr(output.item, name, original.load(address, size))
    return output, calls, signed(original.r[2])


def main():
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    output = ROOT / 'build/status_hub_reference'
    output.mkdir(parents=True, exist_ok=True)
    library = output / ('hub.dylib' if sys.platform == 'darwin' else 'hub.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
        '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-Isrc',
        'src/game/em_status_hub.c', '-lm', '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    Worker = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(State), C.c_int, C.c_uint)
    native.em_status_hub_tick.argtypes = [C.POINTER(State), C.c_float, C.c_uint,
                                         C.POINTER(Stick), Worker, C.c_void_p]
    angles = [0.0, 3.14159274, -3.14159274]
    for boundary in (-2.670354, -2.3561945, -0.7853982, 0.7853982, 2.3561945):
        encoded = bits(boundary)
        angles.extend(number(encoded + d) for d in (-1, 0, 1))
    magnitudes = [0, number(bits(.8) - 1), number(bits(.8)), 1]
    checks = 0
    cases = itertools.product((0, 1), range(5), (0, 0x20, 0x10, 0x800, 0x40, 0x60),
                               (0, 19.99, 20, 49, 69, 89, 99.9, 100))
    full = [(step, old_hover, buttons, infection, magnitude, angle)
            for step, old_hover, buttons, infection in cases
            for magnitude, angle in (itertools.product(magnitudes, angles) if step else [(0, 0)])]
    # Quick: every step/hover/button combination, every infection threshold,
    # every stick magnitude x sector-boundary angle, every hover x angle and
    # button x magnitude pairing, plus a fixed-seed sample of the product.
    selected = select(full, 5000, 0x20CDC0, axes=(
        lambda c: c[:3], lambda c: c[3], lambda c: (c[0], c[4], c[5]),
        lambda c: (c[0], c[1], c[5]), lambda c: (c[0], c[2], c[4])))
    for step, old_hover, buttons, infection, magnitude, angle in selected:
        state = State()
        state.active, state.phase, state.step = 1, 1, step
        state.item.hover = old_hover
        state.item.message_mode, state.item.message_phase = 2, 2
        state.item.message_group, state.item.message_line = 3, 41
        stick = Stick(0, 0, magnitude, angle)
        try:
            wanted, calls, result = expected(elf, state, infection, buttons, stick)
        except AssertionError as error:
            raise AssertionError((step, old_hover, buttons, infection, magnitude, angle), error) from error
        actual = []
        worker = Worker(lambda _, __, event, arg: (actual.append((event, arg)), 1)[1])
        got = native.em_status_hub_tick(C.byref(state), infection, buttons,
                                         C.byref(stick), worker, None)
        assert bytes(state) == bytes(wanted), (step, old_hover, infection, magnitude, angle,
                                              list(bytes(state)), list(bytes(wanted)))
        assert got == result and actual == calls, (got, result, actual, calls)
        checks += 1
    banner(part(checks, len(full), 'original hub state/call cases'))
    report = {'mode': MODE, 'original_hub_state_and_call_cases': checks, 'normal_hub_and_hover': 'PASS',
              'boundaries': 'draw-record actors, background and209DF0 renderer; normalphase1only'}
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
