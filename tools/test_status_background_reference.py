#!/usr/bin/env python3
"""Original 0020A7A0 (the status screens' animated background) against
em_status_background.c.

The oracle executes the ORIGINAL instructions of 0020A7A0 from the user's
own boot ELF with the EE FPU model (tools/ee_float_model.py: add/sub/mul
truncated with the add pre-trim, div.s nearest, cvt.w.s truncated) over
the D_002655A0 state block, and records its worker calls: 00207D00,
00207E40 (slot, x, y, w, h, rgba, tex0 low word) and 00122BB8. The native
step runs over the same state with the same workers. Both the ordered call
list and the resulting 0x60-byte state must match. SDK sin (0011E2A8) runs
as original instructions on the oracle side (the SDK oracle of
tools/test_sdk_math_original_reference.py), and the native side binds
em_sdk_math_original's sinf, as the live port does
(em_status_background_draw.c, over assets/sdk_math_tables.emsm, which must
equal the ELF window). Negative control: with host sinf (IEEE) in its place
the native step leaves the original on at least one case. float_to_int
(001281C0) is the EE cvt.w.s rule; 00128250 truncates a non-negative value
(asserted).

Cases: the .data image (checked against the ELF bytes) and consecutive
frames from it, every pulse boundary (timer 1/0/-1, phase just below and
at 180, the rand reset), both scroll wraps, a layer-2 burst sweep, and the
D_002655A0 blocks of the original RAM captures (status-hub, panel) as
inputs. Capture check: in those images layer 2 is mid-burst; its offsets
are (phase / 0.25 + 1) EE adds of 0.3 (the burst starts on the frame the
timer turns negative, one add before the first phase step), and the
native step, run from a fresh burst for that many frames, must land on the
captured bits (an IEEE add does not: it is checked as a negative control).
No original bytes are embedded in this tool or in the port.
"""
import csv
import ctypes as C
import hashlib
import math
from pathlib import Path
import random
import struct
import subprocess
import sys

from test_camera_rotation_reference import Original as Base
from test_point_light_reference import bits, number, signed
import ee_float_model as ee
from reference_mode import banner, parallel_map, part, pick, select
import test_sdk_math_original_reference as SDK

ROOT = Path(__file__).resolve().parents[1]
STATE = 0x2655A0
TEX0 = 0x20045EE59D421E40
HOST = C.CDLL(None)
HOST.sinf.argtypes = [C.c_float]
HOST.sinf.restype = C.c_float
RETURN = 0x0FFFFFF0


class Original(Base):
    def __init__(self, elf, draws):
        super().__init__(elf)
        self.lo = self.hi = 0
        self.log = []
        self.draws = list(draws)
        self.calls = {
            0x207D00: lambda o: o.log.append(('mode', o.r[4] & 0xFFFFFFFF, o.r[5] & 0xFFFFFFFF)),
            0x207E40: lambda o: o.log.append(('sprite', o.r[4] & 0xFFFFFFFF, signed(o.r[5] & 0xFFFFFFFF),
                                              signed(o.r[6] & 0xFFFFFFFF), signed(o.r[7] & 0xFFFFFFFF),
                                              signed(o.r[8] & 0xFFFFFFFF), o.r[9] & 0xFFFFFFFF,
                                              o.r[10] & 0xFFFFFFFF)),
            0x122BB8: lambda o: o.rand(),
            0x128250: lambda o: o.unsigned(),
            0x1281C0: lambda o: o.r.__setitem__(2, ee.ee_cvt_w_s(o.f[12]) & 0xFFFFFFFF),
            0x11E2A8: lambda o: o.f.__setitem__(0, original_sine(o.f[12])),
        }

    def rand(self):
        value = self.draws.pop(0)
        self.log.append(('rand', value))
        self.r[2] = value

    def unsigned(self):
        value = number(self.f[12])
        assert math.isfinite(value) and value >= 0, value
        self.r[2] = int(value) & 0xFFFFFFFF

    def plain(self, word):
        op, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        if op == 0 and word & 63 == 26:            # div
            a, b = signed(self.r[rs] & 0xFFFFFFFF), signed(self.r[rt] & 0xFFFFFFFF)
            quotient = abs(a) // abs(b) * (1 if (a < 0) == (b < 0) else -1)
            self.lo, self.hi = quotient & 0xFFFFFFFF, (a - quotient * b) & 0xFFFFFFFF
        elif op == 0 and word & 63 == 16:          # mfhi
            self.r[rd] = self.hi
        elif op == 17 and rs == 16 and word & 63 in (0, 1, 2, 3, 7):
            fs, ft, fd, fn = rd, rt, word >> 6 & 31, word & 63
            x, y = self.f[fs], self.f[ft]
            self.f[fd] = {0: lambda: ee.ee_add(x, y), 1: lambda: ee.ee_sub(x, y),
                          2: lambda: ee.ee_mul(x, y), 3: lambda: ee.ee_div(x, y),
                          7: lambda: ee.ee_neg(x)}[fn]()
        elif op == 17 and rs == 16 and word & 63 in (50, 52, 54):
            fs, ft, fn = rd, rt, word & 63
            self.condition = bool({50: ee.ee_c_eq, 52: ee.ee_c_lt, 54: ee.ee_c_le}[fn](self.f[fs], self.f[ft]))
        else:
            super().plain(word)
        self.r[0] = 0

    def run(self, entry, args=(), floats=(), stop=RETURN):
        # The base runner plus bgezl (REGIMM rt 3), which 0020A7A0 uses.
        self.r[31] = RETURN
        for i, value in enumerate(args):
            self.r[4 + i] = value
        pc = entry
        for _ in range(2000000):
            if pc == stop:
                return
            word = self.load(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            offset = signed(word & 65535, 16) * 4
            branch = None
            if op in (2, 3):
                target = (word & 0x3FFFFFF) * 4
                if op == 3:
                    self.r[31] = pc + 8
                self.plain(self.load(pc + 4))
                if target in self.calls:
                    self.calls[target](self)
                    pc += 8
                else:
                    pc = target
                continue
            if op in (4, 5, 20, 21):
                taken = (self.r[rs] & 0xFFFFFFFF) == (self.r[rt] & 0xFFFFFFFF)
                taken = taken == (op in (4, 20))
                if op in (20, 21) and not taken:
                    pc += 8
                    continue
                branch = pc + 4 + offset if taken else pc + 8
            elif op in (6, 7):
                value = signed(self.r[rs] & 0xFFFFFFFF)
                taken = value <= 0 if op == 6 else value > 0
                branch = pc + 4 + offset if taken else pc + 8
            elif op == 1:
                assert rt in (0, 1, 3), rt
                value = signed(self.r[rs] & 0xFFFFFFFF)
                taken = value < 0 if rt == 0 else value >= 0
                if rt == 3 and not taken:
                    pc += 8
                    continue
                branch = pc + 4 + offset if taken else pc + 8
            elif op == 17 and rs == 8:
                taken = self.condition == bool(rt & 1)
                if rt & 2 and not taken:
                    pc += 8
                    continue
                branch = pc + 4 + offset if taken else pc + 8
            elif op == 0 and word & 63 == 8:
                branch = self.r[rs] & 0xFFFFFFFF
            if branch is not None:
                self.plain(self.load(pc + 4))
                pc = branch
            else:
                self.plain(word)
                pc += 4
        raise AssertionError('original 0020A7A0 did not return')


SHIM = r"""
#include "game/em_sdk_math_original.h"
static EmSdkMathTables tables;
static EmSdkMathContext context = {.tables = &tables};
int shim_load(const uint8_t *elf, size_t size)
{
    return em_sdk_math_original_load_tables(elf, size, &tables);
}
/* em_status_background_draw.c's binding: the original sinf; a fault is
 * latched and fails the step. */
float shim_sine(void *unused, float x)
{
    (void)unused;
    return em_sdk_math_original_float_0011E2A8(&context, x);
}
unsigned shim_fault(void)
{
    unsigned fault = context.fault;
    context.fault = 0;
    return fault;
}
"""


def original_sine(word):
    """The original 0011E2A8 on the f12 bits (executed by the SDK oracle)."""
    oracle = SDK.shared_oracle()
    oracle.run(0x11E2A8, floats=(word & 0xFFFFFFFF,))
    return oracle.f[0] & 0xFFFFFFFF


class Layer(C.Structure):
    _fields_ = [('x', C.c_float), ('y', C.c_float), ('phase', C.c_float),
                ('timer', C.c_int32), ('rgba', C.c_float * 4)]


class State(C.Structure):
    _fields_ = [('layer', Layer * 3)]


Sine = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_float)
Random = C.CFUNCTYPE(C.c_int32, C.c_void_p)
Mode = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_int)
Sprite = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int32, C.c_int32, C.c_int32, C.c_int32, C.c_uint32)


class Workers(C.Structure):
    _fields_ = [('context', C.c_void_p), ('sine', Sine), ('random', Random),
                ('mode', Mode), ('sprite', Sprite)]


NATIVE = None
ELF = None
HOST_SINE = False  # the negative control


def native_step(state_bytes, draws):
    log, pending = [], list(draws)

    def rand(_):
        if not pending:
            return -1  # an unscripted draw fails the native step
        value = pending.pop(0)
        log.append(('rand', value))
        return value

    def mode(_, slot, value):
        log.append(('mode', slot, value))
        return 1

    def sprite(_, x, y, w, h, rgba):
        log.append(('sprite', 1, x, y, w, h, rgba, TEX0 & 0xFFFFFFFF))
        return 1

    if HOST_SINE:
        sine = Sine(lambda _, v: HOST.sinf(v))
    else:
        sine = Sine(C.cast(NATIVE.shim_sine, C.c_void_p).value)
    workers = Workers(None, sine, Random(rand), Mode(mode), Sprite(sprite))
    state = State.from_buffer_copy(state_bytes)
    result = NATIVE.em_status_background_step(C.byref(state), C.byref(workers))
    if NATIVE.shim_fault():
        result = -1
    return result, bytes(state), log


def original_step(state_bytes, draws):
    original = Original(ELF, draws)
    original.write(STATE, state_bytes)
    original.run(0x20A7A0, (TEX0,))
    return original.read(STATE, 0x60), original.log


def pack(layers):
    return b''.join(struct.pack('<3fi4f', *layer) for layer in layers)


def check(case):
    name, state_bytes, draws, frames = case
    native_state, original_state = state_bytes, state_bytes
    calls = 0
    for frame in range(frames):
        result, native_state, native_log = native_step(native_state, draws)
        original_state, original_log = original_step(original_state, draws)
        if result != 1 or native_log != original_log or native_state != original_state:
            for index, (a, b) in enumerate(zip(native_log, original_log)):
                if a != b:
                    return f'{name} frame {frame}: call {index}: native {a} original {b}'
            return (f'{name} frame {frame}: result {result}, calls {len(native_log)}/{len(original_log)}, '
                    f'state {native_state.hex()} / {original_state.hex()}')
        calls += len(native_log)
        native_state = original_state
    return calls


def main():
    global NATIVE, ELF
    ELF = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(ELF).hexdigest() == 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
    out = ROOT / 'build/status_background_reference'
    out.mkdir(parents=True, exist_ok=True)
    library = out / ('background.dylib' if sys.platform == 'darwin' else 'background.so')
    shim = out / 'sine_shim.c'
    shim.write_text(SHIM)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-fPIC',
                    '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-Isrc',
                    'src/game/em_status_background.c', 'src/game/em_sdk_math_original.c', str(shim),
                    '-lm', '-o', str(library)], cwd=ROOT, check=True)
    NATIVE = C.CDLL(str(library))
    NATIVE.shim_load.argtypes = [C.c_char_p, C.c_size_t]
    NATIVE.shim_fault.restype = C.c_uint
    assert NATIVE.shim_load(ELF, len(ELF)) == 0, 'SDK table load'
    # The SDK oracle's globals (set before parallel_map forks its workers).
    SDK.ELF = ELF
    SDK.RAM = (ROOT.parent / 'Extermination/build/startup-reference/playable_ee.bin').read_bytes()
    with open(ROOT.parent / 'Extermination/docs/FUNCTIONS.csv') as fh:
        SDK.SIZES = {int(row['vram'], 16): int(row['size_bytes']) for row in csv.DictReader(fh)}
    lo, hi = 0x26C170 - 0x100000 + 0x300, 0x26C658 - 0x100000 + 0x300
    assert SDK.RAM[0x26C170:0x26C658] == ELF[lo:hi], 'captured SDK tables differ from the ELF'
    # The live asset must hold the same window (tools/export_sdk_math_tables.py).
    asset = (ROOT / 'assets/sdk_math_tables.emsm').read_bytes()
    assert asset[:16] == struct.pack('<4s3I', b'EMSM', 1, 0x26C170, 0x4E8) and asset[16:] == ELF[lo:hi], \
        'assets/sdk_math_tables.emsm is not the ELF window (run tools/export_sdk_math_tables.py)'
    NATIVE.em_status_background_step.argtypes = [C.POINTER(State), C.POINTER(Workers)]
    NATIVE.em_status_background_step.restype = C.c_int
    NATIVE.em_status_background_init.argtypes = [C.POINTER(State)]

    # The .data image: the native init must equal the ELF's bytes.
    initial = State()
    NATIVE.em_status_background_init(C.byref(initial))
    data = ELF[STATE - 0x100000 + 0x300:STATE - 0x100000 + 0x300 + 0x60]
    assert bytes(initial) == data, 'em_status_background_init differs from the D_002655A0 .data image'

    rng = random.Random(0x20A7A0)
    draws = [rng.randrange(0x80000000) for _ in range(4)] + [0, 59, 60, 0x7FFFFFFF]
    cases = [('data image', data, draws, pick(24, 4))]
    rgb = (96.0, 96.0, 96.0, 64.0)
    for timer in (2, 1, 0, -1):
        for phase in (0.0, 90.0, 179.5, 179.75, 180.0):
            layers = [(0.25, 0.0, 90.0, 0) + rgb, (0.0, 0.5, phase, timer) + rgb,
                      (37.5, 12.2999992, phase, timer) + rgb]
            cases.append((f'pulse timer {timer} phase {phase}', pack(layers), list(draws), 2))
    for offset in (0.0, 0.25, 0.5, 0.75, 1.0, 127.5, 254.75, 255.0):
        layers = [(offset, 0.0, 90.0, 0) + rgb, (0.0, offset, 90.0, -1) + rgb, (0.0, 0.0, 0.0, 7) + rgb]
        cases.append((f'scroll {offset}', pack(layers), list(draws), 1))
    burst = []
    value = 0.0
    for step in range(pick(720, 720)):
        burst.append(value)
        value = number(ee.ee_add(bits(value), bits(0.3)))
    for index in select(range(len(burst)), 24, 0x1C0, keep=lambda i, _: i in (0, 1, 719)):
        layers = [(0.0, 0.0, 90.0, 0) + rgb, (0.0, 0.0, 0.0, 3) + rgb,
                  (burst[index], burst[index], 0.25 * index, -1) + rgb]
        cases.append((f'burst {index}', pack(layers), list(draws), 1))
    # The original RAM captures (read-only oracle inputs; never copied).
    captures = 0
    for name in ('status-hub/eeMemory.bin', 'panel/eeMemory.bin', 'panel/animation_ee.bin'):
        path = ROOT.parent / 'Extermination/build/startup-reference' / name
        with open(path, 'rb') as image:
            image.seek(STATE)
            block = image.read(0x60)
        cases.append((f'capture {name}', block, list(draws), 2))
        x, y, phase = struct.unpack_from('<3I', block, 0x40)
        frames = int(number(phase) / 0.25) + 1
        # Layer 1 counts down meanwhile (no rand() draw in these frames).
        state = State.from_buffer_copy(pack([(0.0, 0.0, 90.0, 0) + rgb, (0.0, 0.0, 90.0, 100000) + rgb,
                                             (0.0, 0.0, 0.0, 0, 0.0, 0.0, 0.0, 0.0)]))
        for _ in range(frames):
            result, raw, _log = native_step(bytes(state), [])
            assert result == 1
            state = State.from_buffer_copy(raw)
        got = struct.unpack_from('<3I', bytes(state), 0x40)
        assert got == (x, y, phase), (name, [hex(v) for v in got], hex(x), hex(y), hex(phase))
        ieee = 0.0
        for _ in range(frames):
            ieee = number(bits(ieee + number(bits(0.3))))
        assert bits(ieee) != x, (name, 'the IEEE add also reproduces the capture')
        captures += 1
    results = parallel_map(check, cases, cost=lambda c: c[3])
    failures = [r for r in results if isinstance(r, str)]
    for failure in failures:
        print('FAIL', failure)
    if failures:
        return 1
    # Negative control: the IEEE host sinf leaves the original somewhere on
    # the burst/pulse cases (why the live binding is the original sinf).
    global HOST_SINE
    HOST_SINE = True
    control = [c for c in cases if c[0].startswith(('burst', 'pulse', 'data'))]
    if not any(isinstance(check(c), str) for c in control):
        print('FAIL negative control: host sinf matched the original on every case')
        return 1
    HOST_SINE = False
    frames = sum(c[3] for c in cases)
    banner(f'{len(cases)} cases', f'{frames} frames', f'{captures} captured burst states', f'{sum(results):,} ordered calls',
                 part(len([c for c in cases if c[0].startswith('burst')]), 720, 'burst steps'))
    print('status background reference: PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
