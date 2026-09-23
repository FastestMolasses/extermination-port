#!/usr/bin/env python3
"""Execute the original pad unpacker 001B5940 and compare em_pad_unpack.

001B5940 (with its callees 001B5C90, 001B5CC0 -> the SDK sqrtf 0011E748,
001B5D70 and 001B5E20) runs from the owner's local ELF in the bounded
interpreter shared with the other reference tests. Only the libpad read
00110B38 is replaced: it copies the test's 8-byte buffer to the caller's
stack slot, which is exactly the data 001B5940 consumes. No original
instruction bytes or data are embedded in or printed by this tool.

Coverage: every left-stick byte on each axis, every integer point next to
the three gait rings, all 16 D-pad nibbles against centred and deflected
sticks, both ports, analog and digital reads, failed status bytes, long
holds through the 32/10-frame repeat countdown, and random multi-frame
sequences from random (including wrapped-timer) initial block states.
"""
import ctypes as C
import hashlib
from pathlib import Path
import random
import subprocess
import tempfile

from test_interaction_scan_reference import ELF_SHA
from test_item_sdk_math_reference import Original
from test_point_light_reference import signed

ROOT = Path(__file__).resolve().parents[1]
BLOCK, PAD = 0x810E70, 0x810E40
READ = 0x110B38
ENTRY = 0x1B5940
MASK64 = 0xFFFFFFFFFFFFFFFF


class PadOracle(Original):
    """Adds the few EE ops the pad path needs (NOR, MULT/MULT1 with rd)."""

    def plain(self, word):
        op, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        fn = word & 63
        if op == 0 and fn == 39:
            self.r[rd] = ~(self.r[rs] | self.r[rt]) & MASK64
        elif (op == 0 or op == 28) and fn == 24:
            product = signed(self.r[rs]) * signed(self.r[rt])
            self.r[rd] = signed(product & 0xFFFFFFFF) & MASK64
        else:
            super().plain(word)
        self.r[0] = 0


class Unpack(C.Structure):
    _fields_ = [('held', C.c_uint16), ('prev_held', C.c_uint16),
                ('pressed', C.c_uint16), ('prev_pressed', C.c_uint16),
                ('repeat', C.c_uint16), ('repeat_timer', C.c_int16),
                ('lx', C.c_uint8), ('ly', C.c_uint8),
                ('rx', C.c_uint8), ('ry', C.c_uint8), ('gait', C.c_uint8)]


class PadState(C.Structure):
    _fields_ = [('buttons', C.c_uint16), ('lx', C.c_float), ('ly', C.c_float),
                ('rx', C.c_float), ('ry', C.c_float)]


HALVES = ('held', 'prev_held', 'pressed', 'prev_pressed', 'repeat', 'repeat_timer')
BYTES = (('lx', 0x24), ('ly', 0x25), ('rx', 0x26), ('ry', 0x27), ('gait', 0x17))


class Pair:
    """One original block in oracle memory and one native EmPadUnpack."""

    def __init__(self, elf, native, initial=None):
        self.oracle = PadOracle(elf)
        self.native = native
        self.state = Unpack()
        self.buffer = bytes(8)
        self.oracle.calls[READ] = self.read
        self.frames = 0
        if initial:
            for name, value in initial.items():
                setattr(self.state, name, value)
            self.put_original(initial)

    def put_original(self, values):
        for i, name in enumerate(HALVES):
            if name in values:
                self.oracle.save(BLOCK + 2 * i, values[name] & 0xFFFF, 2)
        for name, offset in BYTES:
            if name in values:
                self.oracle.save(PAD + offset, values[name], 1)

    def read(self, oracle):
        oracle.write(oracle.r[6], self.buffer + bytes(24))
        oracle.r[2] = 1

    def original(self):
        values = {name: self.oracle.load(BLOCK + 2 * i, 2) for i, name in enumerate(HALVES)}
        values['repeat_timer'] = signed(values['repeat_timer'], 16)
        values.update({name: self.oracle.load(PAD + offset, 1) for name, offset in BYTES})
        return values

    def step(self, raw, port=0, analog=1):
        self.buffer = bytes(raw)
        self.oracle.save(PAD + 4, port)
        self.oracle.save(PAD + 8, 0)
        self.oracle.run(ENTRY, (BLOCK, PAD, analog))
        expected_result = self.oracle.r[2] & 0xFFFFFFFF
        result = self.native.em_pad_unpack(C.byref(self.state),
                                           (C.c_uint8 * 8)(*raw), port, analog)
        actual = {name: getattr(self.state, name) for name, _ in Unpack._fields_}
        expected = self.original()
        assert (result, actual) == (expected_result, expected), dict(
            raw=[hex(b) for b in raw], port=port, analog=analog,
            result=(result, expected_result), actual=actual, expected=expected)
        self.frames += 1
        return actual


def raw_pad(buttons_canonical=0, lx=0x80, ly=0x80, rx=0x80, ry=0x80, status=0):
    return [status, 0x73, ~buttons_canonical & 0xFF, ~(buttons_canonical >> 8) & 0xFF,
            rx, ry, lx, ly]


UP, RIGHT, DOWN, LEFT = 0x10, 0x20, 0x40, 0x80        # canonical EM_PAD_*
START, CROSS, SELECT, L1 = 0x08, 0x4000, 0x01, 0x400


def main():
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    with tempfile.TemporaryDirectory(prefix='em_input_block_') as tmp:
        library = Path(tmp) / 'input.so'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared',
                        '-fPIC', '-I' + str(ROOT / 'src'), str(ROOT / 'src/em_input.c'),
                        '-o', str(library)], check=True)
        native = C.CDLL(str(library))
        native.em_pad_unpack.argtypes = [C.POINTER(Unpack), C.POINTER(C.c_uint8),
                                         C.c_uint, C.c_int]
        native.em_pad_unpack.restype = C.c_int
        native.em_pad_swap.argtypes = [C.c_uint16]
        native.em_pad_swap.restype = C.c_uint16
        native.em_pad_raw.argtypes = [C.POINTER(PadState), C.POINTER(C.c_uint8)]
        native.em_pad_raw.restype = None
        rng = random.Random(0x1B5940)
        frames = 0

        # Native host conversion: canonical EM_PAD bits and float axes feed
        # the libpad buffer so that the original held word is the byte swap.
        pair = Pair(elf, native)
        for bit in range(16):
            buttons = 1 << bit
            raw = (C.c_uint8 * 8)()
            native.em_pad_raw(C.byref(PadState(buttons, 0.0, 0.0, 0.0, 0.0)), raw)
            assert list(raw) == raw_pad(buttons), (bit, list(raw))
            state = pair.step(list(raw))
            if not buttons & 0xF0:
                assert state['held'] == native.em_pad_swap(buttons)
        axis_byte = {-1.0: 0x00, -0.8: 0x1A, -0.5: 0x40, 0.0: 0x80,
                     0.5: 0xC0, 0.8: 0xE6, 1.0: 0xFF}
        for axis, byte in axis_byte.items():
            raw = (C.c_uint8 * 8)()
            native.em_pad_raw(C.byref(PadState(0, axis, -axis, axis, -axis)), raw)
            assert list(raw)[4:] == [byte, axis_byte[-axis], byte, axis_byte[-axis]], (
                axis, list(raw))
        assert native.em_pad_swap(START) == 0x0800 and native.em_pad_swap(UP) == 0x1000
        frames += pair.frames

        # Every stick byte on each axis, released and with the D-pad held.
        for dpad in (0, UP, LEFT | DOWN):
            pair = Pair(elf, native)
            for value in range(256):
                pair.step(raw_pad(dpad, lx=value))
                pair.step(raw_pad(dpad, ly=value))
            frames += pair.frames

        # Integer points on both sides of each gait ring (r^2 = 48^2 ...).
        pair = Pair(elf, native)
        for ring in (48, 88, 122):
            for dx in range(-128, 128):
                for target in (ring * ring, ring * ring + 1):
                    rest = target - dx * dx
                    if rest < 0:
                        continue
                    dy = int(rest ** 0.5)
                    for ddy in (dy, -dy):
                        if -128 <= ddy <= 127 and dx * dx + ddy * ddy in (target, target - 1, target + 1):
                            pair.step(raw_pad(rng.choice((0, UP, CROSS)), lx=0x80 + dx,
                                              ly=0x80 + ddy))
        frames += pair.frames

        # All D-pad nibbles against centred / small / deflected sticks,
        # both ports, analog and digital reads.
        for port in (0, 1):
            for analog in (1, 0):
                pair = Pair(elf, native)
                for nibble in range(16):
                    for lx, ly in ((0x80, 0x80), (0x90, 0x70), (0xFF, 0x80),
                                   (0x00, 0x00), (0x80, 0xE1), (0x0F, 0xE0)):
                        pair.step(raw_pad(nibble << 4 | START, lx, ly, 0x33, 0xC4),
                                  port, analog)
                        pair.step(raw_pad(0, 0x80, 0x80), port, analog)
                frames += pair.frames

        # Long holds: 32-frame first repeat then every 10 frames, for the
        # D-pad and for stick-derived D-pad bits, plus a direction change.
        pair = Pair(elf, native)
        for _ in range(80):
            pair.step(raw_pad(UP | CROSS))
        for _ in range(30):
            pair.step(raw_pad(UP | RIGHT))
        for _ in range(60):
            pair.step(raw_pad(0, ly=0xFF))
        for _ in range(45):
            pair.step(raw_pad(L1, lx=0x00, ly=0x00))
        for _ in range(5):
            pair.step(raw_pad())
        repeats = pair.frames
        frames += pair.frames

        # Failed libpad status leaves every field untouched.
        pair = Pair(elf, native)
        pair.step(raw_pad(UP))
        for status in (1, 0xFF):
            pair.step(raw_pad(CROSS, 0x00, 0xFF, status=status))
        frames += pair.frames

        # Random sequences from random initial states (timer 0 and -1 wrap).
        stick_values = (0x00, 0x01, 0x0F, 0x10, 0x11, 0x50, 0x7F, 0x80, 0x81,
                        0xB0, 0xE0, 0xE1, 0xE2, 0xFC, 0xFD, 0xFE, 0xFF)
        for sequence in range(160):
            initial = {name: rng.randrange(65536) for name in HALVES[:5]}
            initial['repeat_timer'] = rng.choice((0, 1, -1, 2, 10, 32, -32768,
                                                  rng.randrange(-32768, 32768)))
            initial.update({name: rng.randrange(256) for name, _ in BYTES[:4]})
            initial['gait'] = rng.randrange(4)
            pair = Pair(elf, native, initial)
            port = 0 if sequence % 4 else 1
            buttons, lx, ly = rng.randrange(65536), 0x80, 0x80
            for _ in range(40):
                if rng.random() < 0.35:
                    buttons = rng.randrange(65536)
                elif rng.random() < 0.3:
                    buttons ^= 1 << rng.randrange(16)
                if rng.random() < 0.4:
                    lx = rng.choice(stick_values) if rng.random() < 0.6 else rng.randrange(256)
                    ly = rng.choice(stick_values) if rng.random() < 0.6 else rng.randrange(256)
                status = 0 if rng.random() > 0.03 else rng.randrange(1, 256)
                analog = 0 if rng.random() < 0.1 else 1
                pair.step(raw_pad(buttons, lx, ly, rng.randrange(256), rng.randrange(256),
                                  status), port, analog)
            frames += pair.frames
    print(f'input block reference: PASS ({frames} original 001B5940 frames; '
          f'{repeats}-frame repeat hold)')


if __name__ == '__main__':
    main()
