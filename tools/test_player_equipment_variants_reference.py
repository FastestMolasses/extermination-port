#!/usr/bin/env python3
"""Execute the original equipment variant routines and compare
em_player_equipment_variants.c.

docs/PLAYER_EQUIPMENT.md section 4.7. The user's pinned ELF supplies every
instruction and table; none are embedded here. Routines executed
unmodified (from their original instructions):

  00188C70  flavour 1 variant 0x15
  00189090  flavour 2 variant 1      00189330  flavour 2 variant 2
  001899C0  flavour 2 variant 3, with 00189630 and 00189730 (its unit's)
  00189A20  flavour 2 variant 0xC
  copy_qw4 (00102958)  (the plain copy, not hooked)

Hooked on both sides and recorded: 001026A0 and 001028B8 (evaluated by
running the ORIGINAL SDK routine on the recorded inputs, so both sides store
the original's values), 001C9610, 001F4010 (the 16 words it is handed),
001FBD50 and 00122BB8 (a scripted value, identical on both sides). The test
asserts that the hooked set is exactly the set of jal targets of these
routines that are not executed here.

Every case runs one routine over a synthetic image (the player record
D_008102B0, its bone 4 / bone 19 records, the node and its three work bones
named by D_00275B40, D_008106CC, D_00810CA8 and 0x70003B68) and compares
the node bytes (+0x01, +0x04..+0x06, +0x28, +0x2A, +0x38, +0x70..+0x7F,
+0xD0..+0x10F), the work bones' +0x70..+0x7B and +0x90..+0xCF, the
scratchpad 0x700036A0..0x700036DF and 0x700038A0..0x700038AF and the worker
calls; every byte the original writes must lie in a compared range. Every
conditional branch outcome of the six routines must be reached.

Default run: a fixed-seed sample (a few seconds). EM_TEST_FULL=1: more.
No original instruction bytes, disassembly or data are written by this
file.
"""
import ctypes as C
import random
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import reference_mode as RM  # noqa: E402
import test_player_equipment_reference as T  # noqa: E402

ROOT = T.ROOT
OUT = ROOT / 'build' / 'player_equipment_variants_reference'
M32 = T.M32
P, U8, I16, U32, I32, VP, U32P = T.P, T.U8, T.I16, T.U32, T.I32, T.VP, T.U32P
Bone, BoneP, Node, NP, Fault, fn = T.Bone, T.BoneP, T.Node, T.NP, T.Fault, T.fn

V8C70, V9090, V9330, V9630, V9730, V99C0, V9A20 = (0x188C70, 0x189090, 0x189330, 0x189630, 0x189730,
                                                   0x1899C0, 0x189A20)
SIZES = {V8C70: 0x180, V9090: 0x2A0, V9330: 0x300, V9630: 0x100, V9730: 0x290, V99C0: 0x60,
         V9A20: 0x310}
ENTRIES = {V8C70: '00188C70', V9090: '00189090', V9330: '00189330', V99C0: '001899C0',
           V9A20: '00189A20'}
COPIES = {0x102958}
HOOKED = {0x1026A0: '001026A0', 0x1028B8: '001028B8', 0x1C9610: '001C9610', 0x1F4010: '001F4010',
          0x1FBD50: '001FBD50', 0x122BB8: '00122BB8'}

PLAYER = 0x008102B0
NODE = 0x00680000
BONE_BASE = 0x006C0000          # work bones 0..2 at +0x100 i
PBONE4, PBONE19 = 0x006C1000, 0x006C1100
G_B40, G_CC, G_CA8, S_3B68 = 0x275B40, 0x8106CC, 0x810CA8, 0x70003B68
TABLE_BASE, TABLE_WORDS = 0x24A220, (0x24A4B0 - 0x24A220) // 4
BONES = 3


def check_callee_set(elf):
    targets = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            o = pc - 0x100000 + 0x300
            word = int.from_bytes(elf[o:o + 4], 'little')
            if word >> 26 == 3:
                targets.add((word & 0x3FFFFFF) << 2)
    missing = sorted(t for t in targets if t not in HOOKED and t not in SIZES and t not in COPIES)
    assert not missing, ('callees neither hooked nor executed', [hex(t) for t in missing])
    unused = sorted(t for t in HOOKED if t not in targets)
    assert not unused, ('hooks no routine calls', [hex(t) for t in unused])


# ---- native side ------------------------------------------------------------

class Xb(C.Structure):
    _fields_ = [('b06', U8), ('h2A', I16), ('w38', U32), ('v70', U32 * 4)]


VW_TYPES = [('w_001026A0', fn(U32P, U32P, U32P)), ('w_001028B8', fn(U32P, U32P, U32P)),
            ('w_001C9610', fn(P(BoneP), U32, I32, U32P)), ('w_001F4010', fn(I32, U32P)),
            ('w_001FBD50', fn(I32, I32, U32)), ('w_00122BB8', fn(P(I32)))]


class VWorkers(C.Structure):
    _fields_ = [('ctx', VP)] + VW_TYPES


class VWorld(C.Structure):
    _fields_ = [('player', P(U8)), ('player_bone4', BoneP), ('player_bone19', BoneP),
                ('bones', P(BoneP)), ('bones_count', U32), ('d008106CC', P(U8)), ('d00810CA8', P(U8)),
                ('d00248B98', P(I16)), ('d00248C78', P(I16)), ('table', U32P), ('table_words', U32),
                ('spad3600', U32P), ('spad38A0', U32P), ('spad3B68', P(I32))]


class Variants(C.Structure):
    _fields_ = [('workers', P(VWorkers)), ('world', P(VWorld)), ('fault', P(Fault))]


LAYOUT_PROBE = r'''
#include <stddef.h>
#include <stdio.h>
#include "game/em_player_equipment_variants.h"
#define O(T, f) printf(#T " " #f " %zu\n", offsetof(T, f))
int main(void) {
  O(EmPlayerEquipmentVariantBytes, h2A); O(EmPlayerEquipmentVariantBytes, w38);
  O(EmPlayerEquipmentVariantBytes, v70);
  O(EmPlayerEquipmentVariantWorkers, w_001C9610); O(EmPlayerEquipmentVariantWorkers, w_00122BB8);
  O(EmPlayerEquipmentVariantWorld, bones_count); O(EmPlayerEquipmentVariantWorld, table_words);
  O(EmPlayerEquipmentVariantWorld, spad3B68);
  O(EmPlayerEquipmentNode, bone); O(EmPlayerEquipmentNode, w214);
  printf("sizes %zu %zu %zu %zu\n", sizeof(EmPlayerEquipmentVariantBytes),
         sizeof(EmPlayerEquipmentVariantWorkers), sizeof(EmPlayerEquipmentVariantWorld),
         sizeof(EmPlayerEquipmentNode));
  return 0;
}
'''
LAYOUT = {'EmPlayerEquipmentVariantBytes': Xb, 'EmPlayerEquipmentVariantWorkers': VWorkers,
          'EmPlayerEquipmentVariantWorld': VWorld, 'EmPlayerEquipmentNode': Node}


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('variants.dylib' if sys.platform == 'darwin' else 'variants.so')
    flags = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-ffp-contract=off', '-Isrc']
    subprocess.run(['cc', *flags, '-shared', '-fPIC', 'src/game/em_player_equipment_variants.c',
                    '-o', str(lib)], cwd=ROOT, check=True)
    probe_c, probe = OUT / 'layout.c', OUT / 'layout'
    probe_c.write_text(LAYOUT_PROBE)
    subprocess.run(['cc', *flags, str(probe_c), '-o', str(probe)], cwd=ROOT, check=True)
    for line in subprocess.run([str(probe)], check=True, capture_output=True, text=True).stdout.split('\n'):
        parts = line.split()
        if len(parts) == 3 and parts[0] in LAYOUT:
            got = getattr(LAYOUT[parts[0]], parts[1]).offset
            assert got == int(parts[2]), ('ctypes layout', parts, got)
        elif parts and parts[0] == 'sizes':
            want = [C.sizeof(Xb), C.sizeof(VWorkers), C.sizeof(VWorld), C.sizeof(Node)]
            assert [int(x) for x in parts[1:]] == want, ('ctypes sizes', parts, want)
    native = C.CDLL(str(lib))
    V = P(Variants)
    native.em_player_equipment_00188C70.argtypes = [V, NP]
    for name in ('00189090', '00189330', '001899C0', '00189A20'):
        getattr(native, 'em_player_equipment_' + name).argtypes = [V, NP, P(Xb)]
    return native


# ---- one case -------------------------------------------------------------------

def f(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


THRESHOLDS = [1.0, 2.0, 7.0, 8.0, 11.0, 12.0, 19.0, 20.0, 21.0, 27.0, 39.0, 50.0, 52.0]


def clock(rng):
    r = rng.random()
    if r < 0.3:
        return f(rng.choice(THRESHOLDS))
    if r < 0.7:
        t = rng.choice(THRESHOLDS)
        return f(t + rng.choice((-0.5, 0.5, -0.01, 0.01)))
    return f(rng.uniform(0.0, 80.0))


def rfloat(rng, lo=-50.0, hi=50.0):
    return f(rng.uniform(lo, hi))


class Case:
    def __init__(self, entry, seed, elf, ram0, sdk):
        self.entry, self.seed, self.elf, self.sdk = entry, seed, elf, sdk
        rng = random.Random(seed)
        self.ram = ram0
        self.spad = bytearray(0x4000)
        self.undo = []
        b98 = [T.sx(int.from_bytes(ram0[0x248B98 + 2 * k:0x248B9A + 2 * k], 'little'), 16) for k in range(8)]
        c78 = [T.sx(int.from_bytes(ram0[0x248C78 + 2 * k:0x248C7A + 2 * k], 'little'), 16) for k in range(8)]
        self.b98, self.c78 = b98, c78
        k = {V8C70: 4, V9330: 1, V99C0: 2}.get(entry, 1)
        r = rng.random()
        clip = b98[k] if r < 0.4 else c78[k] if r < 0.75 else rng.randrange(-0x8000, 0x8000)
        self.setup = setup = []

        def put(a, v, n=4):
            setup.append((a, v & ((1 << (8 * n)) - 1), n))
        # the player record
        put(PLAYER + 0x04, rng.choice((1, 1, 1, 1, 1, 1, 1, 0, 2)), 1)
        put(PLAYER + 0x3C, clock(rng))
        put(PLAYER + 0x1F0, rng.choice((0x33, 0x33, 0x33, 0x32)), 1)
        put(PLAYER + 0x200, rng.choice((0, 0x1000, 0x1001, 0xEFFF, rng.getrandbits(32))))
        put(PLAYER + 0x20C, clip, 2)
        put(PLAYER + 0x274, rng.choice((0, 1, 1)), 1)
        want = {V8C70: 4, V9090: 3, V9330: 1, V99C0: 2, V9A20: 5}[entry]
        put(PLAYER + 0x275, want if rng.random() < 0.6 else rng.choice((0, 1, 2, 3, 4, 5, 7)), 1)
        put(PLAYER + 0xB0, rfloat(rng))
        put(PLAYER + 0xB4, rfloat(rng))
        put(PLAYER + 0xB8, rfloat(rng))
        put(PLAYER + 0x120, PBONE4)                                    # D_008103D0
        put(PLAYER + 0x15C, PBONE19)
        for base in (PBONE4, PBONE19):
            for i in range(16):
                put(base + 0x90 + 4 * i, rfloat(rng))
        # the node and its work bones
        put(NODE + 0x01, rng.randrange(2), 1)
        put(NODE + 0x04, 1, 1)
        states = {V8C70: 2, V9090: 5, V9330: 7, V99C0: 8, V9A20: 7}[entry]
        put(NODE + 0x05, rng.randrange(states), 1)
        put(NODE + 0x06, rng.choice((0, 1, 2, 3)), 1)
        put(NODE + 0x0C, rng.choice((1, 2, 3, 3, 0)), 1)
        h28 = rng.choice((0, 0, 1, 2, 5, 6, 7, 8, -1, rng.randrange(0, 20)))
        put(NODE + 0x28, h28, 2)
        put(NODE + 0x2A, rng.choice((0, 1)), 2)
        put(NODE + 0x38, rng.choice((0x3D567750, 0xBDA14BDD, rfloat(rng, -1, 1))))
        for i in range(4):
            put(NODE + 0x70 + 4 * i, rfloat(rng, -2, 2) if rng.random() < 0.8 else 0)
        for i in range(16):
            put(NODE + 0xD0 + 4 * i, rfloat(rng))
        for i in range(BONES):
            put(NODE + 0x110 + 4 * i, BONE_BASE + 0x100 * i)
            for j in range(3):
                put(BONE_BASE + 0x100 * i + 0x70 + 4 * j, rfloat(rng, -1, 1))
            for j in range(16):
                put(BONE_BASE + 0x100 * i + 0x90 + 4 * j, rfloat(rng))
        put(G_B40, NODE + 0x110)
        put(G_CC, rng.choice((0, 0, 0, 1)), 1)
        put(G_CA8, rng.choice((0, 1, 0x100)), 2)
        put(S_3B68, rng.choice((0, 1, 2, 3)))
        for i in range(4):
            put(0x700038A0 + 4 * i, rfloat(rng))
        for i in range(16):
            put(0x700036A0 + 4 * i, rfloat(rng))
        self.rand_values = [rng.getrandbits(31) for _ in range(4)]

    def mem(self, a, n=4):
        buf, o = (self.spad, a - 0x70000000) if a >= 0x70000000 else (self.ram, a)
        return int.from_bytes(buf[o:o + n], 'little')

    def apply(self, ee):
        for a, v, n in self.setup:
            ee.poke(a, v, n)

    # ---- the original side ------------------------------------------------
    def run_original(self, outcomes):
        ee = T.EE(self.elf, self.ram, self.spad)
        ee.undo = self.undo
        self.apply(ee)
        ee.writes = []
        ee.ranges = tuple((s, s + n) for s, n in SIZES.items())
        ee.outcomes = outcomes
        log, rand = [], list(self.rand_values)

        def sdk(entry):
            a_n, b_n, out_n = T.SDK_SHAPE[entry]

            def hook(e):
                a, b = e.words(e.arg(1), a_n), e.words(e.arg(2), b_n)
                log.append((T.SDK[entry], a, b))
                e.put_words(e.arg(0), self.sdk.run(entry, a, b, out_n))
            return hook
        ee.stubs[0x1026A0] = sdk(0x1026A0)
        ee.stubs[0x1028B8] = sdk(0x1028B8)

        def h_9610(e):
            log.append(('001C9610', e.arg(0) == NODE + 0x110, e.g32(5), e.words(e.arg(2), 16)))

        def h_4010(e):
            log.append(('001F4010', e.g32(4), e.words(e.arg(1), 16)))

        def h_BD50(e):
            log.append(('001FBD50', e.arg(0) == PLAYER, e.g32(5), e.g32(6), e.f[12]))

        def h_2BB8(e):
            v = rand.pop(0)
            log.append(('00122BB8',))
            e.set32(2, v)
        ee.stubs.update({0x1C9610: h_9610, 0x1F4010: h_4010, 0x1FBD50: h_BD50, 0x122BB8: h_2BB8})
        ee.call(self.entry, (NODE, PLAYER))
        return log, ee.writes

    # ---- the native side ----------------------------------------------------
    def run_native(self, native, ram, spad):
        rd = lambda a, n=4: int.from_bytes((spad if a >= 0x70000000 else ram)[  # noqa: E731
            (a - 0x70000000 if a >= 0x70000000 else a):(a - 0x70000000 if a >= 0x70000000 else a) + n], 'little')
        log, rand = [], list(self.rand_values)
        keep = []

        def words(ptr, n):
            return tuple(ptr[i] for i in range(n))

        def bone_from(addr):
            # EmOwnerBone's fields are not at their original offsets: copy
            # the two the routines touch, +0x70..+0x7B and +0x90..+0xCF.
            b = Bone()
            C.memmove(C.addressof(b) + Bone.rot.offset, bytes(ram[addr + 0x70:addr + 0x7C]), 12)
            for i in range(16):
                b.world[i] = rd(addr + 0x90 + 4 * i)
            return b
        bones = [bone_from(BONE_BASE + 0x100 * i) for i in range(BONES)]
        pb4, pb19 = bone_from(PBONE4), bone_from(PBONE19)
        arr = (BoneP * 56)()
        for i in range(BONES):
            arr[i] = C.pointer(bones[i])
        player = (U8 * 0x320).from_buffer_copy(bytes(ram[PLAYER:PLAYER + 0x320]))
        node = Node()
        node.drawn, node.state, node.b05 = rd(NODE + 1, 1), rd(NODE + 4, 1), rd(NODE + 5, 1)
        node.bone_count, node.h28 = rd(NODE + 0xC, 1), rd(NODE + 0x28, 2)
        for i in range(16):
            node.mD0[i] = rd(NODE + 0xD0 + 4 * i)
        for i in range(BONES):
            node.bone[i] = C.pointer(bones[i])
        x = Xb()
        x.b06, x.h2A, x.w38 = rd(NODE + 6, 1), T.sx(rd(NODE + 0x2A, 2), 16), rd(NODE + 0x38)
        for i in range(4):
            x.v70[i] = rd(NODE + 0x70 + 4 * i)
        cc = (U8 * 1)(rd(G_CC, 1))
        ca8 = (U8 * 2)(*bytes(ram[G_CA8:G_CA8 + 2]))
        b98 = (I16 * 8)(*self.b98)
        c78 = (I16 * 8)(*self.c78)
        table = (U32 * TABLE_WORDS)(*[rd(TABLE_BASE + 4 * i) for i in range(TABLE_WORDS)])
        s3600 = (U32 * 0x48)(*[rd(0x70003600 + 4 * i) for i in range(0x48)])
        s38A0 = (U32 * 0x10)(*[rd(0x700038A0 + 4 * i) for i in range(0x10)])
        s3B68 = (I32 * 1)(T.sx(rd(S_3B68), 32))

        def w_A0(_, m, v, out):
            a, b = words(m, 16), words(v, 4)
            log.append(('001026A0', a, b))
            for i, w in enumerate(self.sdk.run(0x1026A0, a, b, 4)):
                out[i] = w
            return 0

        def w_B8(_, a_, b_, out):
            a, b = words(a_, 4), words(b_, 4)
            log.append(('001028B8', a, b))
            for i, w in enumerate(self.sdk.run(0x1028B8, a, b, 4)):
                out[i] = w
            return 0

        def w_9610(_, bones_, count_, n, world):
            log.append(('001C9610', C.addressof(bones_.contents) == C.addressof(arr), n, words(world, 16)))
            return 0

        def w_4010(_, i, at):
            log.append(('001F4010', i, words(at, 16)))
            return 0

        def w_BD50(_, i, a2, f12):
            log.append(('001FBD50', True, i, a2, f12))
            return 0

        def w_2BB8(_, out):
            log.append(('00122BB8',))
            out[0] = rand.pop(0)
            return 0
        types = dict(VW_TYPES)
        fns = {name: types[name](h) for name, h in (('w_001026A0', w_A0), ('w_001028B8', w_B8),
                                                     ('w_001C9610', w_9610), ('w_001F4010', w_4010),
                                                     ('w_001FBD50', w_BD50), ('w_00122BB8', w_2BB8))}
        keep.append(fns)
        workers = VWorkers(None, *[fns[name] for name, _ in VW_TYPES])
        world = VWorld(C.cast(player, P(U8)), C.pointer(pb4), C.pointer(pb19), C.cast(arr, P(BoneP)), 56,
                       C.cast(cc, P(U8)), C.cast(ca8, P(U8)), C.cast(b98, P(I16)), C.cast(c78, P(I16)),
                       C.cast(table, U32P), TABLE_WORDS, C.cast(s3600, U32P), C.cast(s38A0, U32P),
                       C.cast(s3B68, P(I32)))
        fault = Fault()
        v = Variants(C.pointer(workers), C.pointer(world), C.pointer(fault))
        name = 'em_player_equipment_' + ENTRIES[self.entry]
        if self.entry == V8C70:
            rc = getattr(native, name)(C.byref(v), C.byref(node))
        else:
            rc = getattr(native, name)(C.byref(v), C.byref(node), C.byref(x))
        state = {}
        state['node'] = (node.drawn, node.state, node.b05, x.b06, node.h28, x.h2A & 0xFFFF, x.w38,
                         tuple(x.v70), tuple(node.mD0))
        state['bones'] = tuple(bytes(C.string_at(C.addressof(b) + Bone.rot.offset, 12)) +
                               b''.join(w.to_bytes(4, 'little') for w in b.world) for b in bones)
        state['s36A0'] = tuple(s3600[0x28 + i] for i in range(16))
        state['s38A0'] = tuple(s38A0[i] for i in range(4))
        return rc, (fault.address, fault.code), log, state

    def original_state(self):
        m = self.mem
        x70 = tuple(m(NODE + 0x70 + 4 * i) for i in range(4))
        return {
            'node': (m(NODE + 1, 1), m(NODE + 4, 1), m(NODE + 5, 1), m(NODE + 6, 1), m(NODE + 0x28, 2),
                     m(NODE + 0x2A, 2), m(NODE + 0x38), x70, tuple(m(NODE + 0xD0 + 4 * i) for i in range(16))),
            'bones': tuple(bytes(self.ram[BONE_BASE + 0x100 * i + 0x70:BONE_BASE + 0x100 * i + 0x7C]) +
                           bytes(self.ram[BONE_BASE + 0x100 * i + 0x90:BONE_BASE + 0x100 * i + 0xD0])
                           for i in range(BONES)),
            's36A0': tuple(m(0x700036A0 + 4 * i) for i in range(16)),
            's38A0': tuple(m(0x700038A0 + 4 * i) for i in range(4)),
        }


def compared(a):
    if NODE <= a < NODE + 0x2F0:
        o = a - NODE
        return o in (1, 4, 5, 6, 0x28, 0x29, 0x2A, 0x2B) or 0x38 <= o < 0x3C or 0x70 <= o < 0x80 or \
            0xD0 <= o < 0x110
    for i in range(BONES):
        o = a - (BONE_BASE + 0x100 * i)
        if 0x70 <= o < 0x7C or 0x90 <= o < 0xD0:
            return True
    return 0x700036A0 <= a < 0x700036E0 or 0x700038A0 <= a < 0x700038B0


def run_case(native, elf, ram, sdk, entry, seed, outcomes):
    case = Case(entry, seed, elf, ram, sdk)
    # The native side reads the image as it was before the call: the
    # setup applied to a copy.
    ram_b, spad_b = bytearray(ram), bytearray(0x4000)
    for a, v, k in case.setup:
        buf, o = (spad_b, a - 0x70000000) if a >= 0x70000000 else (ram_b, a)
        buf[o:o + k] = v.to_bytes(k, 'little')
    wild = None
    try:
        try:
            log_o, writes = case.run_original(outcomes)
        except T.Unsupported as e:
            # The original read outside the EE memory (a table index past
            # the window, e.g. +0x28 left negative by 00189630 when 00189730
            # indexes D_0024A410 with it): the native side must fault there.
            wild = e
        if wild is None:
            state_o = case.original_state()
            stray = sorted({a for a, n in writes for a in range(a, a + n)
                            if not compared(a) and not T.STACK_LOW <= a < T.STACK})
            assert not stray, ('original writes outside the compared ranges', ENTRIES[entry], seed,
                               [hex(a) for a in stray[:8]])
    finally:
        T.restore(case.undo)
    rc, fault, log_n, state_n = case.run_native(native, ram_b, spad_b)
    if wild is not None:
        assert rc != 0 and fault[1] == 4, ('the original read wild memory; native did not fault',
                                           ENTRIES[entry], seed, wild, rc, fault)
        return 'wild'
    if rc != 0:
        # a table read outside the window: the original read further data
        assert fault[1] == 4, ('native fault', ENTRIES[entry], seed, hex(fault[0]), fault[1])
        return 'fault'
    assert log_n == log_o, ('worker calls', ENTRIES[entry], seed, first(log_o, log_n))
    for key in state_o:
        assert state_n[key] == state_o[key], ('state', key, ENTRIES[entry], seed, state_o[key], state_n[key])
    return 'ok'


def first(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return i, x, y
    return len(a), a[len(b):], b[len(a):]


def main():
    elf = T.read_elf()
    check_callee_set(elf)
    ram = T.elf_ram(elf)
    sdk = T.SdkEval(elf, T.elf_ram(elf))
    native = build_native()
    outcomes = set()
    count = RM.pick(6000, 1200)
    results = {}
    for n in range(count):
        entry = list(ENTRIES)[n % len(ENTRIES)]
        seed = 0x5EED0000 + n
        r = run_case(native, elf, ram, sdk, entry, seed, outcomes)
        results[r] = results.get(r, 0) + 1
    # Boundary: 00189630 leaves +0x28 at -2 and 00189730's state 3 then
    # indexes D_0024A410 at -1; the vector address it forms is outside the
    # EE memory, where the native side faults.
    r = run_case(native, elf, ram, sdk, V99C0, 0x5EED0E0E, outcomes)
    assert r == 'wild', ('the wild-index boundary case', r)
    results[r] = results.get(r, 0) + 1
    branches = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            o = pc - 0x100000 + 0x300
            w = int.from_bytes(elf[o:o + 4], 'little')
            op, rt = w >> 26, w >> 16 & 31
            if op in (4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17) or (op == 1 and rt <= 3) or \
                    (op == 0x11 and (w >> 21 & 31) == 8):
                if op == 4 and (w >> 21 & 31) == rt:
                    continue                     # an unconditional b
                branches.add(pc)
    missing = sorted((hex(pc), t) for pc in branches for t in (True, False) if (pc, t) not in outcomes)
    print(f'test_player_equipment_variants_reference: mode {RM.MODE}: {count} cases {results}, '
          f'{len(branches)} conditional branches, {len(missing)} outcomes unreached')
    assert not missing, ('branch outcomes unreached', missing)
    print('PASS test_player_equipment_variants_reference')


if __name__ == '__main__':
    main()
