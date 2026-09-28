#!/usr/bin/env python3
"""The player's face slot (src/game/em_face_slot.c: 001CA700, 001D06D0,
001D06E0, 001CA770 and 001D0C70 -> 001D0720 on the record's +0x90 / +0x94
over the one 001AF710 stack and arena; docs/FACE_ATTACH.md section 6.3)
against the ORIGINAL instructions.

The user's pinned ELF and the captured playable RAM (build/startup-reference/
playable_ee.bin) supply every instruction and input; none are embedded here.

A. The sequence the area scripts and the player stage drive (400 frames): the
   ORIGINAL 001B81D0 (the row of the player's model: 001CA700(player,
   D_0028A490[row], 7), then 001D06D0(player, 1)) at the start and again at
   frames 128 and 280, 001D06E0(player, 1 / 0) at the talk frames, 001CA770
   at frame 250, and 001D0C70 on every attached frame, with a controlled
   00122BB8. The native module runs the same calls (the resource the
   original's row selects, a2 = 7) over a copy of the same RAM: after every
   event the record's +0x90 and +0x94, the slot's 0xD0 bytes (the face
   weights, the resource word +0x60, the control block), D_00275BCC,
   D_00275BD0, the stack word at the cursor and the RNG draw count equal the
   original's.
B. Where the port calls them: the ORIGINAL 00183090 calls 001D0C70 exactly
   when 0x70003B8F == 2, before its body requests; the ORIGINAL 001FD950
   calls 001D06E0(player, 1 / 0) exactly at the speaker-0 start and the
   completion-mask clear while 0x70003B8F == 2 (the port's
   em_player_stage_live w_001D0C70 and the message host's face talk).
C. Fail-stop: 001D0C70, 001D06D0 and 001D06E0 with +0x90 == 0 (where the
   original would address low memory) return -1 and store nothing.

Default run about 2 s; there is no larger sweep (EM_TEST_FULL=1 runs the
same). Runs natively on arm64 macOS (cc, ctypes).
"""
import ctypes as C
import hashlib
import itertools
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from test_face_allocation_reference import Captured, PLAYER  # noqa: E402
from test_interaction_scan_reference import ELF_SHA  # noqa: E402
from test_point_light_reference import signed  # noqa: E402
import test_roger_actor_original_reference as RA  # noqa: E402

OUT = ROOT / 'build/face_slot_reference'
SOURCES = ('src/game/em_face_slot.c', 'src/game/em_roger_actor_original.c', 'src/game/em_opening_face.c')
STACK, SLOTS, SLOT_COUNT, SLOT_BYTES = 0x7D4640, 0x7D5840, 0x480, 0xD0
TABLE = 0x28A490
RANDOM = C.CFUNCTYPE(C.c_uint32, C.c_void_p)


class OriginalFace(Captured):
    """The captured-RAM interpreter with the integer forms 00183090 and
    001FD950 reach beyond the base set (DIV, MFHI / MFLO, NOR)."""
    def plain(self, word):
        op, rs, rt, rd, fn = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31, word & 63
        if op == 0 and fn == 26:
            left = signed(self.r[rs])
            right = signed(self.r[rt])
            assert right
            quotient = abs(left) // abs(right) * (-1 if (left < 0) != (right < 0) else 1)
            self.lo = quotient & 0xFFFFFFFFFFFFFFFF
            self.hi = (left - quotient * right) & 0xFFFFFFFFFFFFFFFF
        elif op == 0 and fn in (16, 18):
            self.r[rd] = self.hi if fn == 16 else self.lo
        elif op == 0 and fn == 39:
            self.r[rd] = ~(self.r[rs] | self.r[rt]) & 0xFFFFFFFFFFFFFFFF
        else:
            super().plain(word)
        self.r[0] = 0


class Slot(C.Structure):
    _fields_ = [('record', C.POINTER(C.c_uint8)), ('record_address', C.c_uint32),
                ('actor', C.POINTER(RA.State)), ('random', RANDOM), ('random_context', C.c_void_p)]


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('face_slot.dylib' if sys.platform == 'darwin' else 'face_slot.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
                    '-fPIC', '-Isrc', *SOURCES, '-lm', '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    P = C.POINTER(Slot)
    n.em_face_slot_001CA700.argtypes = [P, C.c_uint32, C.c_int32, C.POINTER(C.c_int32)]
    n.em_face_slot_001D06D0.argtypes = [P, C.c_uint32]
    n.em_face_slot_001D06E0.argtypes = [P, C.c_uint32]
    n.em_face_slot_001CA770.argtypes = [P]
    n.em_face_slot_001D0C70.argtypes = [P]
    return n


class Native:
    """em_face_slot over a copy of the RAM's record, stack and arena."""

    def __init__(self, lib, ram):
        self.lib = lib
        self.ram = bytearray(ram)
        self.buf = (C.c_uint8 * len(self.ram)).from_buffer(self.ram)
        base = C.addressof(self.buf)
        self.state = RA.State()
        w = self.state.world
        w.d00275BCC = C.cast(base + 0x275BCC, C.POINTER(C.c_int16))
        w.d00275BD0 = C.cast(base + 0x275BD0, C.POINTER(C.c_uint32))
        w.slot_stack = C.cast(base + STACK, C.POINTER(C.c_uint32))
        w.slot_stack_base, w.slot_stack_words = STACK, SLOT_COUNT
        w.slots = C.cast(base + SLOTS, C.POINTER(C.c_uint8))
        w.slots_base, w.slots_size = SLOTS, SLOT_COUNT * SLOT_BYTES
        self.draws = 0
        self.random = RANDOM(self.next)
        self.slot = Slot(C.cast(base + PLAYER, C.POINTER(C.c_uint8)), PLAYER, C.pointer(self.state),
                         self.random, None)

    def next(self, _):
        self.draws += 1
        return (self.draws * 0x94720123) & 0x7FFFFFFF

    def call(self, name, *args):
        return getattr(self.lib, name)(C.byref(self.slot), *args)


def u32(b, a):
    return struct.unpack_from('<I', b, a)[0]


def view(read, draws):
    """The compared storage: +0x90, +0x94, the slot, the stack words."""
    face = read(PLAYER + 0x90, 4)
    slot = struct.unpack('<I', face)[0]
    cursor = struct.unpack('<I', read(0x275BD0, 4))[0]
    return (face, read(PLAYER + 0x94, 2), read(slot, SLOT_BYTES) if slot else b'', read(0x275BCC, 2),
            read(0x275BD0, 4), read(cursor, 4), draws)


def call_order(elf, ram):
    """B: where 00183090 and 001FD950 call the face functions."""
    stages = 0
    for guard, mode, changed in itertools.product((0, 1, 2, 3), (0, 1, 2, 3, 4, 255), (0, 1)):
        o = OriginalFace(elf, ram)
        o.save(0x70003B8F, guard, 1)
        o.save(PLAYER + 0x2F3, mode, 1)
        o.save(PLAYER + 0x1F2, 1, 2)
        o.save(PLAYER + 0x20C, 1 - changed, 2)
        o.save(0x8106D4, 0xA5, 1)
        events = []
        o.calls.update({0x1D0C70: lambda r: events.append('face'),
                        0x1C63E0: lambda r: events.append('foreign body'),
                        0x1C67E0: lambda r: events.append('ordinary body')})
        o.run(0x183090, (PLAYER,))
        expected = ['face'] if guard == 2 else []
        if mode in (1, 3):
            expected += ['foreign body']
        elif not mode and changed:
            expected += ['ordinary body']
        assert events == expected and o.load(0x8106D4, 1) == 0xA5, (guard, mode, changed, events)
        stages += 1
    dialogue = 0
    for guard, timer, speaker, mask in itertools.product((1, 2), (0, 1, 3), (0, 1, 255), (0, 1, 2, 3)):
        o = OriginalFace(elf, ram)
        message = 0x920000
        o.save(0x70003B8F, guard, 1)
        o.save(message + 0x34, 0)
        o.save(message + 0x6C, timer)
        o.save(message + 0x51, speaker, 1)
        o.save(message + 0x64, mask)
        events = []

        def zero(r):
            r.r[2] = 0
        o.calls.update({0x1FE480: zero, 0x1FE530: zero, 0x1CC170: zero, 0x1FE070: lambda r: None,
                        0x1D06E0: lambda r: events.append((r.r[4], r.r[5]))})
        o.run(0x1FD950, (message,))
        expected = []
        if guard == 2:
            if timer and speaker == 0:
                expected = [(PLAYER, 1)]
            elif not timer and mask & 1:
                expected = [(PLAYER, 0)]
        assert events == expected, (guard, timer, speaker, mask, events)
        dialogue += 1
    return stages, dialogue


def main():
    decomp = ROOT.parent / 'Extermination'
    elf = (decomp / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA, 'not the pinned SCUS-97112 ELF'
    ram = (decomp / 'build/startup-reference/playable_ee.bin').read_bytes()
    assert u32(ram, PLAYER + 0x90) == 0, 'the playable capture already holds a player face'
    lib = build()
    stages, dialogue = call_order(elf, ram)

    # A: the sequence.
    o = OriginalFace(elf, ram)
    o.save(0x70003B8F, 1, 1)
    draws = [0]

    def random_word(original):
        draws[0] += 1
        original.r[2] = (draws[0] * 0x94720123) & 0x7FFFFFFF
    o.calls[0x122BB8] = random_word
    n = Native(lib, ram)
    orig_read = lambda a, size: bytes(o.load(a + i, 1) for i in range(size))
    port_read = lambda a, size: bytes(n.ram[a:a + size])

    def attach():
        o.run(0x1B81D0, (PLAYER,))
        # 001B81D0's own row: the captured +0x2FF 0x3B (D_0081078F 0) is row 0x18.
        row = 0x18 if o.load(0x81078F, 1) == 1 or o.load(PLAYER + 0x2FF, 1) == 0x3B else None
        assert row is not None, 'the capture\'s model row'
        result = C.c_int32(-1)
        assert n.call('em_face_slot_001CA700', u32(ram, TABLE + 4 * row), 7, C.byref(result)) == 0
        assert result.value == 1, '001CA700 found no free slot'
        assert n.call('em_face_slot_001D06D0', 1) == 0

    attach()
    attached, events = True, 0
    assert view(orig_read, draws[0]) == view(port_read, n.draws), 'after the first attach'
    for frame in range(400):
        if frame in (5, 80, 150, 300):
            o.run(0x1D06E0, (PLAYER, 1))
            assert n.call('em_face_slot_001D06E0', 1) == 0
        if frame in (50, 125, 200, 350):
            o.run(0x1D06E0, (PLAYER, 0))
            assert n.call('em_face_slot_001D06E0', 0) == 0
        if frame in (128, 280):
            attach()
            attached = True
        if frame == 250:
            o.run(0x1CA770, (PLAYER,))
            assert n.call('em_face_slot_001CA770') == 0
            attached = False
        if attached:
            o.run(0x1D0C70, (PLAYER,))
            assert n.call('em_face_slot_001D0C70') == 0
        a, b = view(orig_read, draws[0]), view(port_read, n.draws)
        assert a == b, ('frame', frame, [i for i in range(len(a)) if a[i] != b[i]])
        events += 1

    # C: fail-stop with +0x90 == 0 (after the release: re-run on a copy).
    fails = 0
    for name, args in (('em_face_slot_001D0C70', ()), ('em_face_slot_001D06D0', (1,)),
                       ('em_face_slot_001D06E0', (0,))):
        m = Native(lib, ram)
        before = bytes(m.ram)
        assert m.call(name, *args) == -1, (name, 'ran with +0x90 == 0')
        assert bytes(m.ram) == before and m.draws == 0, (name, 'stored or drew')
        fails += 1
    print(f'face slot: original-instruction reference PASSED ({events} frames of 001B81D0 / 001D06E0 / '
          f'001CA770 / 001D0C70 equal in +0x90, +0x94, the slot, D_00275BCC / D_00275BD0 and the stack word, '
          f'{draws[0]} RNG draws; the original call sites: {stages} 00183090 and {dialogue} 001FD950 cases; '
          f'{fails} fail-stop checks)')


if __name__ == '__main__':
    main()
