#!/usr/bin/env python3
"""Compare the native packet-chain builders and fog programmer
(src/game/em_packet_chain_original.c) with the ORIGINAL instructions.

The oracle is the bounded EE interpreter of test_effect_manager_reference.py
(the shared one of test_effect_original_reference.py plus 64-bit argument
registers and a chosen stack). It executes the original instructions of the
pinned boot ELF (the user's config/SCUS_971.12):

  001CB5F0, 001CB6B0, 001CB760, 001CB900 with 001CB9B0, 0021B9A0 with its
  jump table, 0021B920, 0021B900 and 00121870 (block_copy); the frame
  chain start 001CB8A0 and splice 001CB800; the fog setters 0021B970 and
  0021BA80 (with 0021BA70); the area fog 001D8FD0 with everything it calls
  (001D7B30 and its 001D2910 flag test, 001B0070, 0021B8E0) unhooked.

The native 001D8FD0 takes 001D7B30 and 001B0070 as workers: the test
answers them by executing the original routine over a copy of the native
RAM at the moment of the call.

COP1 arithmetic comes from tools/ee_float_model.py (docs/EE_FLOAT_MODEL.md).
Memory is a captured route snapshot (../Extermination/build/s87/route/<beat>/
eeMemory.bin; beats 00..14). The oracle's stack lives in the scratchpad, so
every EE RAM byte either side changes is a byte the routines write.

Per case (a sequence of calls on one beat's RAM) the two sides run call by
call. After EVERY call the render context (+0x00..+0xFF: the cursor and the
whole fog state), every byte the original wrote during that call, and the
returned packet address must be equal on both sides; after the sequence the
whole 32 MB of EE RAM must be equal. So a fog call whose result a later call
overwrites (mode 1 re-loads the pair from +0xD8) is still observed.
001CB5F0's caller fills the packet it opens: both sides
write the same bytes through the returned address (native: through the
returned host pointer).

The worker adapters (the binding surface) run the consumer patterns once
per ADAPTER_VARIANTS entry, so all three 0021B9A0 adapters (bits, float with
f12 != f13, heads) and both 001CB760 adapters (3 and 4 arguments) are called
on fog calls of modes 0..5 with scale != bias, compared after every call.

Capture evidence (independent of the oracle):
  * fog: in every beat, the context +0xA0 quadword equals 0021B920 of the
    captured +0xB8/+0xBC pair, and +0xC0..+0xDF equals +0xA0..+0xBF;
  * chains: the frame chains the original left in both DMA buffers are
    walked from 001CB800's start tag. For the buffer whose heads are current
    the chain is replayed through the native builders (call order = block
    address order, the slot of each block from the head table) and every
    word the builders write must equal the captured one; the slot words the
    replay leaves must be the ones 001CB800 spliced (the start tag, every
    head block's +0x14 and the end link).

Nothing original is written by this file; build/packet_chain_reference/
report.json holds only counts.
"""
import ctypes as C
import hashlib
import json
import random
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ee_float_model as FM  # noqa: E402
import test_effect_manager_reference as EM  # noqa: E402
from reference_mode import FULL, banner, in_scope_beat, part, pick, select  # noqa: E402

ROOT, DECOMP, ROUTE, ELF_SHA = EM.ROOT, EM.DECOMP, EM.ROUTE, EM.ELF_SHA
M32, M64 = EM.M32, EM.M64
sx = EM.sx
OUT = ROOT / 'build/packet_chain_reference'

RAM_SIZE = 0x02000000
SPAD_STACK = 0x70003F00           # the oracle's stack (scratchpad, never compared)
D_CTX_PTR, D_STATE_PTR = 0x275670, 0x275674
TABLE = 0x7635C0                  # D_007635C0
ALT_TABLE = 0x01D00000            # a second table in plain RAM (any table address works)
F_BUF = 0x28F700 + 0x1F3EC0       # 001CB800's start tag of buffer 0 (+ n * 0x70000)
BLEND = {0x6A0: 0, 0x720: 1, 0x7A0: 2, 0x820: 3, 0x8A0: 4}   # 001CB9B0 offsets -> mode
BEATS = sorted(p.name for p in ROUTE.iterdir()
               if (p / 'eeMemory.bin').exists() and in_scope_beat(p.name))

IDS = [-0x80000000, -0x1000, -1, 0, 1, 0xFFF, 0x1000, 0x1FFF, 0x123456, 0xFFAFFF, 0xFFB000,
       0xFFB001, 0xFFBFFF, 0xFFC000, 0xFFEFFF, 0xFFF000, 0xFFF001, 0x1000000, 0x7FFFFFFF]
COUNTS = [-2, -1, 0, 1, 2, 5, 6, 9, 0xA, 0xE, 0x10, 0xC1, 0x100]
MODES = [-0x80000000, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 0x80000004 - (1 << 32), 0x7FFFFFFF]
SPECIAL = [0, 0x80000000, 0x00000001, 0x807FFFFF, 0x3F800000, 0xBF800000, 0x43160000,
           0x42C80000, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0xFF800000, 0x7FC00000, 0x00800000,
           0x4B000000, 0x34000000]


def f32(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


# ------------------------------------------------------------- native side ---

class Region(C.Structure):
    _fields_ = [('base', C.c_uint32), ('size', C.c_uint32), ('bytes', C.POINTER(C.c_uint8))]


class Chain(C.Structure):
    _fields_ = [('regions', C.POINTER(Region)), ('region_count', C.c_uint),
                ('d275670', C.c_uint32), ('d275674', C.c_uint32), ('fault', C.c_int32),
                ('fault_function', C.c_uint32), ('fault_address', C.c_uint32)]


RECORD_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_uint32))


class AreaFogWorkers(C.Structure):
    _fields_ = [('ctx', C.c_void_p), ('w_001D7B30', RECORD_FN), ('w_001B0070', RECORD_FN)]


def build_lib():
    OUT.mkdir(parents=True, exist_ok=True)
    lib_path = OUT / 'packet_chain.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', 'src/game/em_packet_chain_original.c',
                    'src/game/em_status_ui_leftovers.c', '-o', str(lib_path)], cwd=ROOT, check=True)
    lib = C.CDLL(str(lib_path))
    P, U8P = C.POINTER, C.POINTER(C.c_uint8)
    lib.em_packet_chain_init.argtypes = [P(Chain), P(Region), C.c_uint, C.c_uint32, C.c_uint32]
    lib.em_packet_chain_clear_fault.argtypes = [P(Chain)]
    lib.em_packet_chain_001CB5F0.argtypes = [P(Chain), C.c_uint32, C.c_int32, C.c_int32,
                                             P(C.c_uint32), P(U8P)]
    lib.em_packet_chain_001CB6B0.argtypes = [P(Chain), C.c_uint32, C.c_int32, C.c_int32, C.c_uint64]
    lib.em_packet_chain_001CB760.argtypes = [P(Chain), C.c_uint32, C.c_int32, C.c_uint64]
    lib.em_packet_chain_001CB900.argtypes = [P(Chain), C.c_uint32, C.c_int32, C.c_int32]
    lib.em_packet_chain_001CB9B0.argtypes = [C.c_uint32, C.c_int32]
    lib.em_packet_chain_001CB9B0.restype = C.c_int32
    lib.em_packet_chain_0021B920.argtypes = [P(Chain), C.c_uint32, C.c_uint32]
    lib.em_packet_chain_0021B9A0.argtypes = [P(Chain), C.c_int32, C.c_uint32, C.c_uint32]
    lib.em_packet_chain_fog.argtypes = [P(Chain), P(C.c_uint32)]
    lib.em_packet_chain_0021B970.argtypes = [P(Chain), C.c_uint32, C.c_uint32]
    lib.em_packet_chain_0021BA80.argtypes = [P(Chain), C.c_int32, C.c_int32, C.c_int32]
    lib.em_packet_chain_001D8FD0.argtypes = [P(Chain), P(AreaFogWorkers)]
    lib.em_packet_chain_001CB8A0.argtypes = [P(Chain), C.c_int32, C.c_uint32, C.c_uint32]
    lib.em_packet_chain_001CB800.argtypes = [P(Chain), C.c_uint32, C.c_int32, C.c_uint32, C.c_uint32]
    lib.em_packet_chain_w_001CB5F0.argtypes = [C.c_void_p, C.c_uint32, C.c_int32, C.c_int32, P(U8P)]
    lib.em_packet_chain_w_001CB6B0.argtypes = [C.c_void_p, C.c_uint32, C.c_int32, C.c_int32, C.c_uint32]
    lib.em_packet_chain_w_001CB760.argtypes = [C.c_void_p, C.c_uint32, C.c_int32, C.c_uint32]
    lib.em_packet_chain_w_001CB760_4.argtypes = [C.c_void_p, C.c_uint32, C.c_int32, C.c_uint32, C.c_uint32]
    lib.em_packet_chain_w_001CB900.argtypes = [C.c_void_p, C.c_uint32, C.c_int32, C.c_int32]
    lib.em_packet_chain_w_0021B9A0.argtypes = [C.c_void_p, C.c_int32, C.c_uint32, C.c_uint32]
    lib.em_packet_chain_w_0021B9A0_float.argtypes = [C.c_void_p, C.c_int32, C.c_float, C.c_float]
    lib.em_packet_chain_w_0021B9A0_heads.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32, C.c_int32]
    return lib


class Native:
    """The native module over a mutable copy of one beat's EE RAM."""

    def __init__(self, lib, ram, regions=None):
        self.lib, self.ram = lib, ram
        self.buf = (C.c_uint8 * len(ram)).from_buffer(ram)
        spans = regions or [(0, len(ram))]
        self.regions = (Region * len(spans))()
        for i, (base, size) in enumerate(spans):
            self.regions[i] = Region(base, size, C.cast(C.byref(self.buf, base), C.POINTER(C.c_uint8)))
        self.chain = Chain()
        u = lambda a: struct.unpack_from('<I', ram, a)[0]
        lib.em_packet_chain_init(C.byref(self.chain), self.regions, len(spans), u(D_CTX_PTR), u(D_STATE_PTR))

    def host_address(self, ptr):
        return C.cast(ptr, C.c_void_p).value - C.addressof(self.buf)

    def area_workers(self):
        """001D8FD0's two workers, answered by the ORIGINAL 001D7B30 and
        001B0070 executed over a copy of this side's RAM when called."""
        def original(entry):
            def call(_ctx, out):
                e = EM.Oracle(ELF_BYTES[0], bytearray(self.ram), bytearray(0x4000))
                e.run(entry, (), stack=SPAD_STACK)
                out[0] = e.r[2] & M32
                return 0
            return RECORD_FN(call)
        self._keep = (original(0x1D7B30), original(0x1B0070))
        self._workers = AreaFogWorkers(None, *self._keep)
        return self._workers


# ------------------------------------------------------------------ calls ---
# A call is (name, args). 'open' carries the fill pattern its caller writes.

def fill_bytes(count, salt):
    n = min(max(count, 0) * 16, 0x40)
    return bytes((salt * 31 + i * 7) & 0xFF for i in range(n))


def oracle_call(e, call):
    name, a = call
    if name == 'open':
        table, id_, count, salt = a
        e.run(0x1CB5F0, (table, id_, count), stack=SPAD_STACK)
        addr = e.r[2] & M32
        data = fill_bytes(count, salt)
        if data:
            e.write(addr, data)
        return addr
    if name == 'ref':
        e.run(0x1CB6B0, (a[0], a[1], a[2], a[3]), stack=SPAD_STACK)
    elif name == 'call':
        e.run(0x1CB760, (a[0], a[1], a[2]), stack=SPAD_STACK)
    elif name == 'blend':
        e.run(0x1CB900, (a[0], a[1], a[2]), stack=SPAD_STACK)
    elif name == 'fog':
        e.run(0x21B9A0, (a[0],), (a[1], a[2]), stack=SPAD_STACK)
    elif name == 'coef':
        e.run(0x21B920, (), (a[0], a[1]), stack=SPAD_STACK)
    elif name == 'fogset':
        e.run(0x21B970, (), (a[0], a[1]), stack=SPAD_STACK)
    elif name == 'fogcol':
        e.run(0x21BA80, (a[0], a[1], a[2]), stack=SPAD_STACK)
    elif name == 'areafog':
        e.run(0x1D8FD0, (), stack=SPAD_STACK)
    elif name == 'start':
        e.run(0x1CB8A0, (a[0], a[1], a[2], a[3]), stack=SPAD_STACK)
    elif name == 'splice':
        e.run(0x1CB800, (a[0], a[1], a[2], a[3]), stack=SPAD_STACK)
    elif name == 'half':           # test setup: a halfword at an absolute address
        e.store(a[0], a[1] & 0xFFFF, 2)
    elif name == 'poke':           # test setup: raw words into the context
        for off, v in a:
            e.store(ctx_of(e.ram) + off, v, 4)
    elif name == 'mem':            # test setup: raw words at absolute addresses
        for addr, v in a:
            e.store(addr, v, 4)
    elif name == 'cursor':         # test setup: the cursor relative to the context
        e.store(ctx_of(e.ram) + 0x18, (ctx_of(e.ram) + a[0]) & M32, 4)
    return None


# Adapter variant v (None = the core functions): which 0021B9A0 adapter and
# which 001CB760 adapter the fog and call blocks go through.
ADAPTER_VARIANTS = (('bits', 'w3'), ('float', 'w4'), ('heads', 'w3'))


def bits_float(bits):
    return struct.unpack('<f', struct.pack('<I', bits & M32))[0]


def native_call(n, call, adapters=None):
    lib, ch = n.lib, C.byref(n.chain)
    name, a = call
    fog_adapter, call_adapter = ADAPTER_VARIANTS[adapters] if adapters is not None else (None, None)
    if name == 'open':
        table, id_, count, salt = a
        ptr = C.POINTER(C.c_uint8)()
        if adapters is not None:
            rc = lib.em_packet_chain_w_001CB5F0(C.addressof(n.chain), table, id_, count, C.byref(ptr))
            addr = n.host_address(ptr) if rc == 0 else None
        else:
            out = C.c_uint32()
            rc = lib.em_packet_chain_001CB5F0(ch, table, id_, count, C.byref(out), C.byref(ptr))
            if rc == 0:
                assert n.host_address(ptr) == out.value, ('host pointer', hex(out.value))
            addr = out.value
        assert rc == 0, ('001CB5F0 fault', n.chain.fault, hex(n.chain.fault_address))
        data = fill_bytes(count, salt)
        for i, b in enumerate(data):
            ptr[i] = b
        return addr
    if name == 'ref':
        rc = (lib.em_packet_chain_w_001CB6B0(C.addressof(n.chain), a[0], a[1], a[2], a[3] & M32)
              if adapters is not None else lib.em_packet_chain_001CB6B0(ch, a[0], a[1], a[2], a[3] & M64))
    elif name == 'call':
        if call_adapter == 'w4':
            rc = lib.em_packet_chain_w_001CB760_4(C.addressof(n.chain), a[0], a[1], a[2] & M32, 0xDEAD)
        elif call_adapter == 'w3':
            rc = lib.em_packet_chain_w_001CB760(C.addressof(n.chain), a[0], a[1], a[2] & M32)
        else:
            rc = lib.em_packet_chain_001CB760(ch, a[0], a[1], a[2] & M64)
    elif name == 'blend':
        rc = (lib.em_packet_chain_w_001CB900(C.addressof(n.chain), *a) if adapters is not None
              else lib.em_packet_chain_001CB900(ch, *a))
    elif name == 'fog':
        mode, f12, f13 = a
        if fog_adapter == 'bits':
            rc = lib.em_packet_chain_w_0021B9A0(C.addressof(n.chain), mode, f12, f13)
        elif fog_adapter == 'float':
            rc = lib.em_packet_chain_w_0021B9A0_float(C.addressof(n.chain), mode,
                                                      bits_float(f12), bits_float(f13))
        elif fog_adapter == 'heads':
            rc = lib.em_packet_chain_w_0021B9A0_heads(C.addressof(n.chain), f12, f13, mode)
        else:
            rc = lib.em_packet_chain_0021B9A0(ch, mode, f12, f13)
    elif name == 'coef':
        rc = lib.em_packet_chain_0021B920(ch, *a)
    elif name == 'fogset':
        rc = lib.em_packet_chain_0021B970(ch, *a)
    elif name == 'fogcol':
        rc = lib.em_packet_chain_0021BA80(ch, *a)
    elif name == 'areafog':
        rc = lib.em_packet_chain_001D8FD0(ch, C.byref(n.area_workers()))
    elif name == 'start':
        rc = lib.em_packet_chain_001CB8A0(ch, a[1], a[2] & M32, a[3] & M32)
    elif name == 'splice':
        rc = lib.em_packet_chain_001CB800(ch, a[0], a[1], a[2] & M32, a[3] & M32)
    elif name == 'half':
        struct.pack_into('<H', n.ram, a[0], a[1] & 0xFFFF)
        rc = 0
    elif name == 'poke':
        for off, v in a:
            struct.pack_into('<I', n.ram, ctx_of(n.ram) + off, v)
        rc = 0
    elif name == 'mem':
        for addr, v in a:
            struct.pack_into('<I', n.ram, addr, v)
        rc = 0
    elif name == 'cursor':
        struct.pack_into('<I', n.ram, ctx_of(n.ram) + 0x18, (ctx_of(n.ram) + a[0]) & M32)
        rc = 0
    assert rc == 0, (name, 'fault', n.chain.fault, hex(n.chain.fault_function), hex(n.chain.fault_address))
    return None


def ctx_of(ram):
    return struct.unpack_from('<I', ram, D_CTX_PTR)[0]


ELF_BYTES = [None]
FREE_WORDS = 0x01E00000           # plain RAM for the chain start / splice result words


def frame_cases(ctx):
    """The fog setters, the area fog and the frame chain start / splice."""
    one, f150 = 0x3F800000, f32(150.0)
    cases = []
    for near, far in ((0xC3510000, 0x43980000), (0, 0x42DC0000), (0x43160000, 0x43160000),
                      (0x7F800000, 0x3F800000), (0x7FC00000, 0x00000001), (0x80000000, 0x4B7F0000)):
        cases.append(('fogset', [('fogset', (near, far))]))
    for rgb in ((48, 48, 48), (0, 0, 0), (-1, 0, 0), (0x7FFFFFFF, -0x80000000, 5), (255, 128, -300),
                (1, 2, 3)):
        cases.append(('fogcol', [('fogcol', rgb)]))
    # the area fog: the capture's own key and flags, flag word bit 0x80, the
    # 0x0F00 override (render flag 8), other area keys and an unknown one
    cases.append(('areafog', [('areafog', ())]))
    cases.append(('areafog-80', [('mem', ((0x8106C8, 0x20081990),)), ('areafog', ())]))
    cases.append(('areafog-flag8', [('poke', ((0xC, 0x143),)), ('areafog', ())]))
    for key in (0x0100, 0x0800, 0x0B00, 0x0F00, 0x1500, 0x2A07):
        cases.append(('areafog-key', [('mem', ((0x810700, (key >> 8) | (key & 0xFF) << 8 | 0x22 << 16),)),
                                      ('areafog', ())]))
    cases.append(('areafog-then-frame', [('areafog', ()), ('fog', (0, 0, 0)), ('fog', (2, one, f150)),
                                         ('fog', (1, 0, 0))]))
    # the chain start and splice, both buffers (and a negative index), with
    # blocks appended in between so the splice walks and clears slots
    for index in (0, 1, -1):
        for a1 in (0, 1, 5):
            for dest in ((ctx, ctx + 4), (FREE_WORDS, FREE_WORDS + 0x40)):
                build = [('open', (TABLE, 0x5000, 2, 21)), ('ref', (TABLE, 0x5000, 9, 0x00233290)),
                         ('call', (TABLE, 0xFFF000, 0x00400000)), ('open', (TABLE, 0x123456, 1, 22)),
                         ('blend', (TABLE, 0, 2)), ('call', (TABLE, 0x5000, 0x00400100))]
                cases.append(('frame-chain', [('half', (0x810E80, index)), ('start', (TABLE, a1) + dest)]
                              + build + [('splice', (TABLE, a1) + dest)]))
                cases.append(('splice-empty', [('half', (0x810E80, index)), ('splice', (TABLE, a1) + dest)]))
    cases.append(('splice-alt', [('half', (0x810E80, 1)), ('open', (ALT_TABLE, 0x7000, 1, 23)),
                                 ('splice', (ALT_TABLE, 0, FREE_WORDS, FREE_WORDS + 4))]))
    return cases


# ------------------------------------------------------------------ cases ---

def boundary_cases():
    """Every id boundary, count, mode and special float, each on its own."""
    cases = []
    for id_ in IDS:
        for table in (TABLE, ALT_TABLE):
            cases.append(('id', [('open', (table, id_, 2, 1)), ('open', (table, id_, 1, 2)),
                                 ('ref', (table, id_, 9, 0x00233290)), ('call', (table, id_, 0x00233290)),
                                 ('blend', (table, id_, 1))]))
    for count in COUNTS:
        cases.append(('count', [('open', (TABLE, 0x5000, count, 3)), ('open', (TABLE, 0x5000, 1, 4))]))
    for payload in (0, 0x0FFFFFFF, 0x10000000, 0xFFFFFFFF, 0x123456789ABCDEF0, -1, 0x7635C0):
        cases.append(('payload', [('ref', (TABLE, 0x8000, 2, payload)), ('ref', (TABLE, 0x8000, -1, payload)),
                                  ('call', (TABLE, 0x8000, payload))]))
    for mode in MODES:
        cases.append(('blend', [('blend', (TABLE, 0x9000, mode))]))
        cases.append(('fog-mode', [('fog', (mode, 0x3F800000, 0x43160000))]))
    for s in SPECIAL:
        for b in (SPECIAL[4], s):
            for mode in (2, 3):
                cases.append(('fog-float', [('fog', (mode, s, b))]))
    # near == far (zero divisor), presets from special values, the latch.
    for near, far in ((0x43160000, 0x43160000), (0, 0x80000000), (0x7F800000, 0x7F800000),
                      (0x7FC00000, 0x3F800000), (0xC3510000, 0x43980000), (0x00000001, 0x00000002)):
        for mode in (-1, 0, 1, 4, 5, 9):
            cases.append(('fog-preset', [('poke', ((0xF8, near), (0xFC, far), (0xD8, far), (0xDC, near),
                                                   (0xB8, near), (0xBC, far))),
                                         ('fog', (mode, 0x3F800000, 0x3F800000))]))
        cases.append(('coef', [('coef', (near, far))]))
    # Slot words holding bits 28..31 (the link keeps the low 28 bits; a
    # word of high bits alone is still "not empty").
    for prev in (0x10000000, 0xF0000000, 0x80001230, 0xFFFFFFFF):
        for call in (('open', (TABLE, 0x5000, 2, 14)), ('ref', (TABLE, 0x5000, 2, 0x00233290)),
                     ('call', (TABLE, 0x5000, 0x00233290))):
            cases.append(('slot-bits', [('mem', ((TABLE + 5 * 4, prev),)), call]))
    return cases


def alias_cases(ctx):
    """The order of memory operations made observable: the block, the slot
    word or the head word placed on the context's +0x18 cursor word, so the
    cursor's re-read after the stores sees what they wrote. One call each
    (the cursor is garbage afterwards)."""
    calls = (('open', (TABLE, 0x5000, 2, 15)), ('ref', (TABLE, 0x5000, 9, 0x00233290)),
             ('call', (TABLE, 0x5000, 0x00233290)), ('blend', (TABLE, 0x5000, 2)))
    cases = []
    for off in (0x00, 0x04, 0x10, 0x14):             # block + off == the cursor word
        for call in calls:
            cases.append(('alias-block', [('mem', ((TABLE + 5 * 4, 0x00400000),)),
                                          ('cursor', (0x18 - 0x100 - off,)), call]))
    for call in calls:
        # slot word == the cursor word (it is never empty: it holds the cursor)
        cases.append(('alias-slot', [(call[0], (ctx + 0x18, 0) + call[1][2:])]))
        # head word == the cursor word: written when the slot is empty
        table = ctx + 0x18 - 0x4000
        for prev in (0, 0x00400000):
            cases.append(('alias-head', [('mem', ((table, prev),)), (call[0], (table, 0) + call[1][2:])]))
    return cases


def consumer_cases():
    """The call patterns of the consumers (docs/PACKET_CHAIN.md section 5)."""
    one, f150, f100 = 0x3F800000, f32(150.0), f32(100.0)
    lane = [('open', (TABLE, 0, 1, 5)), ('open', (TABLE, 0, 0xC1, 6)), ('open', (TABLE, 0, 5, 7)),
            ('open', (TABLE, 0, 0xA, 8)), ('call', (TABLE, 0, 0x00233290)), ('blend', (TABLE, 0, 1))]
    glint = [('fog', (2, one, f150)), ('fog', (3, one, f150)), ('open', (TABLE, 0x4567, 0xE, 9)),
             ('blend', (TABLE, 0x4567, 1)), ('fog', (1, 0, 0))]
    puff = [('fog', (2, one, f100)), ('fog', (3, one, f100)), ('open', (TABLE, 0x2345, 6, 10)),
            ('ref', (TABLE, 0x2345, 2, 0x00251220)), ('blend', (TABLE, 0x2345, 2)), ('fog', (1, 0, 0))]
    head = [('open', (TABLE, 0x3456, 7, 11)), ('ref', (TABLE, 0x3456, 9, 0x00251300)),
            ('open', (TABLE, 0x3456, 1, 12)), ('open', (TABLE, 0x3456, 0x10, 13)),
            ('call', (TABLE, 0x3456, 0x00251400)), ('blend', (TABLE, 0x3456, 3))]
    kinds = [('fog', (2, 0, f32(1e5))), ('fog', (3, 0, f32(1e6))), ('fog', (1, 0, 0))]
    script = [('fog', (0, 0, 0))]
    render = [('call', (TABLE, 0xFFF000, 0x00400000)), ('call', (TABLE, 0xFFC000, 0x00400100))]
    # Every scaled mode with scale != bias, ending on a scaled call (no
    # trailing mode 0/1 re-load), so the last result also reaches the
    # end-of-sequence compare. Finite values (the float adapter runs these).
    scaled = [('fog', (4, f32(1.25), f32(-50.0))), ('fog', (5, f32(0.5), f32(700.0))),
              ('fog', (2, f32(2.0), f32(3.0))), ('fog', (3, f32(0.25), f32(40.0))),
              ('fog', (2, one, f150)), ('fog', (3, 0, f32(1e6)))]
    return [('lane', lane * 2), ('glint', glint), ('puff', puff), ('head', head + head),
            ('kinds', kinds), ('script', script), ('render', render), ('scaled', scaled),
            ('glint-open', glint[:-1]), ('kinds-open', kinds[:-1]),
            ('frame', lane + glint + head + puff + render)]


def random_cases(count, seed):
    rng = random.Random(seed)
    ids = IDS + [rng.randrange(0, 0x1000000) for _ in range(8)]
    out = []
    for _ in range(count):
        seq = []
        pool = [rng.choice(ids) for _ in range(3)]
        for _ in range(rng.randrange(1, 9)):
            k = rng.randrange(6)
            table = TABLE if rng.random() < 0.8 else ALT_TABLE
            id_ = rng.choice(pool)
            if k == 0:
                seq.append(('open', (table, id_, rng.choice(COUNTS[2:] + [rng.randrange(0, 40)]), rng.randrange(256))))
            elif k == 1:
                seq.append(('ref', (table, id_, rng.randrange(-4, 0x20), rng.getrandbits(64))))
            elif k == 2:
                seq.append(('call', (table, id_, rng.getrandbits(64))))
            elif k == 3:
                seq.append(('blend', (table, id_, rng.randrange(-2, 8))))
            elif k == 4:
                fl = lambda: rng.choice(SPECIAL) if rng.random() < 0.3 else f32(rng.uniform(-1e4, 1e4))
                seq.append(('fog', (rng.choice(MODES), fl(), fl())))
            else:
                seq.append(('poke', tuple((off, f32(rng.uniform(-500, 1500))) for off in
                                          (0xB8, 0xBC, 0xD8, 0xDC, 0xF8, 0xFC) if rng.random() < 0.5)))
        out.append(('random', seq))
    return out


# ------------------------------------------------------------------ runner ---

def run_case(lib, elf, ram, spad, pristine, native_ram, case, adapters=None):
    label, seq = case
    e = EM.Oracle(elf, ram, spad)
    n = Native(lib, native_ram)
    ctx = ctx_of(ram)
    for i, c in enumerate(seq):
        mark = len(e.journal)
        got_o = oracle_call(e, c)
        got_n = native_call(n, c, adapters)
        if got_o != got_n:
            raise AssertionError((label, i, c, 'packet address', got_o, got_n))
        spans = [(ctx, 0x100)] + [(o, len(old)) for buf, o, old in e.journal[mark:] if buf is ram]
        for o, size in spans:
            if ram[o:o + size] != native_ram[o:o + size]:
                raise AssertionError((label, 'after call', i, c, 'adapter variant', adapters,
                                      'EE RAM differs at', hex(o), ram[o:o + size].hex(),
                                      native_ram[o:o + size].hex()))
    if ram != native_ram:
        diff = next(i for i in range(0, RAM_SIZE, 4) if ram[i:i + 4] != native_ram[i:i + 4])
        raise AssertionError((label, 'EE RAM differs at', hex(diff), seq))
    changed = sum(len(old) for buf, _, old in e.journal if buf is ram)
    for buf, o, old in e.journal:
        if buf is ram:
            native_ram[o:o + len(old)] = pristine[o:o + len(old)]
    e.restore()
    return changed


def fault_checks(lib, pristine):
    """Fail-stop: unmapped cursor / block / slot / head / packet, NULL
    outputs, the latch and its clear. Nothing may be written."""
    ctx = ctx_of(pristine)
    cursor = struct.unpack_from('<I', pristine, ctx + 0x18)[0]
    block = cursor + 0x100
    checks = 0
    maps = [
        ('cursor', [(0, ctx)], lambda n: n.lib.em_packet_chain_001CB6B0(C.byref(n.chain), TABLE, 0, 1, 0)),
        ('block', [(ctx, 0x100), (TABLE, 0x8000)],
         lambda n: n.lib.em_packet_chain_001CB760(C.byref(n.chain), TABLE, 0, 0)),
        ('slot', [(ctx, 0x100), (block, 0x1000)],
         lambda n: n.lib.em_packet_chain_001CB900(C.byref(n.chain), TABLE, 0, 1)),
        ('head', [(ctx, 0x100), (block, 0x1000), (TABLE, 0x4000)],
         lambda n: n.lib.em_packet_chain_001CB6B0(C.byref(n.chain), TABLE, 0, 1, 0)),
        ('packet', [(ctx, 0x100), (block, 0x40), (TABLE, 0x8000)],
         lambda n: n.lib.em_packet_chain_001CB5F0(C.byref(n.chain), TABLE, 0, 3, C.byref(C.c_uint32()), None)),
        ('context', [(0, ctx + 0x80)],
         lambda n: n.lib.em_packet_chain_0021B9A0(C.byref(n.chain), 1, 0, 0)),
        ('frame-index', [(0, 0x810E80), (0x810E82, RAM_SIZE - 0x810E82)],
         lambda n: n.lib.em_packet_chain_001CB8A0(C.byref(n.chain), 0, ctx, ctx + 4)),
        ('splice-index', [(0, 0x810E80), (0x810E82, RAM_SIZE - 0x810E82)],
         lambda n: n.lib.em_packet_chain_001CB800(C.byref(n.chain), TABLE, 0, ctx, ctx + 4)),
        ('splice-table', [(0, TABLE + 0x2000), (TABLE + 0x2004, RAM_SIZE - TABLE - 0x2004)],
         lambda n: n.lib.em_packet_chain_001CB800(C.byref(n.chain), TABLE, 0, ctx, ctx + 4)),
        ('null-out', [(0, RAM_SIZE)],
         lambda n: n.lib.em_packet_chain_001CB5F0(C.byref(n.chain), TABLE, 0, 3, None, None)),
    ]
    for label, spans, call in maps:
        ram = bytearray(pristine)
        n = Native(lib, ram, spans)
        assert call(n) == -1 and n.chain.fault != 0, ('fault expected', label)
        assert ram == pristine, ('bytes written before a fault', label)
        # latched: a call that would succeed on its own still refuses
        assert n.lib.em_packet_chain_0021B920(C.byref(n.chain), 0, 0x3F800000) == -1, ('latch', label)
        assert ram == pristine, ('latched call wrote', label)
        checks += 1
    ram = bytearray(pristine)
    n = Native(lib, ram)
    assert lib.em_packet_chain_001CB5F0(C.byref(n.chain), TABLE, 0, 3, None, None) == -1
    lib.em_packet_chain_clear_fault(C.byref(n.chain))
    assert lib.em_packet_chain_0021B920(C.byref(n.chain), 0, 0x3F800000) == 0 and n.chain.fault == 0
    checks += 1
    return checks


def fog_capture(lib, elf, beat, ram, spad):
    """The captured fog block against native 0021B920 and the original."""
    ctx = ctx_of(ram)
    near, far = struct.unpack_from('<2I', ram, ctx + 0xB8)
    work = bytearray(ram)
    n = Native(lib, work)
    assert lib.em_packet_chain_0021B920(C.byref(n.chain), near, far) == 0
    assert work[ctx + 0xA0:ctx + 0xB0] == ram[ctx + 0xA0:ctx + 0xB0], (beat, 'fog block vs native 0021B920')
    assert ram[ctx + 0xC0:ctx + 0xE0] == ram[ctx + 0xA0:ctx + 0xC0], (beat, 'captured latch')
    fog = (C.c_uint32 * 4)()
    assert lib.em_packet_chain_fog(C.byref(n.chain), fog) == 0
    assert bytes(struct.pack('<4I', *fog)) == bytes(ram[ctx + 0xA0:ctx + 0xB0])
    e = EM.Oracle(elf, bytearray(ram), bytearray(spad))
    e.run(0x21B920, (), (near, far), stack=SPAD_STACK)
    assert e.ram[ctx + 0xA0:ctx + 0xB0] == ram[ctx + 0xA0:ctx + 0xB0], (beat, 'fog block vs original 0021B920')


def walk_chain(ram, start):
    """The chain 001CB800 spliced from start: a list of (slot, blocks,
    from_heads). Within one slot the links run from newer to older blocks,
    i.e. to lower addresses, so a link to a higher address starts the next
    slot. Two slots whose blocks do not interleave can join one run; a run
    replays to the same bytes as one slot (only its oldest block's head word
    is then known). The slot of a run is the smallest slot above the
    previous run whose captured head word is the run's oldest block, else
    the next slot (heads are never cleared, so stale ones abound)."""
    u = lambda a: struct.unpack_from('<I', ram, a)[0]
    heads = {}
    for i in range(0x1000):
        h = u(TABLE + 0x4000 + 4 * i)
        if h:
            heads.setdefault(h, []).append(i)
    end, cur = start + 0x20, u(start + 4)
    runs, blocks, seen = [], [], set()
    while cur != end:
        if cur in seen or not (0 < cur < RAM_SIZE - 0x20):
            return None
        seen.add(cur)
        w0, w1, w4 = u(cur), u(cur + 4), u(cur + 0x10)
        if w0 == 0x20000000 and w1 == (cur + 0x10) & 0x0FFFFFFF and w4 >> 16 == 0x2000:
            kind = ('open', w4 & 0xFFFF)
        elif w0 >> 28 == 3 and w4 == 0x20000000:
            kind = ('ref', w0 & 0x0FFFFFFF, w1)
        elif w0 == 0x50000000 and w4 == 0x20000000:
            kind = ('call', w1)
        else:
            return None
        if blocks and cur > blocks[-1][0]:
            runs.append(blocks)
            blocks = []
        blocks.append((cur, kind))
        cur = u(cur + 0x14)
    if not blocks:
        return None
    runs.append(blocks)
    groups, last = [], -1
    for i, run in enumerate(runs):
        left = len(runs) - i - 1
        slots = [s for s in heads.get(run[-1][0], ()) if last < s <= 0xFFF - left]
        slot, known = (slots[0], True) if slots else (last + 1, False)
        if slot > 0xFFF - left:
            return None
        groups.append((slot, run, known))
        last = slot
    return groups


def replay_capture(lib, beat, ram):
    """Replay the current frame chain of one beat through the native
    builders; returns (blocks, contiguous) of the buffer that replayed, or
    raises when neither buffer does."""
    u = lambda a: struct.unpack_from('<I', ram, a)[0]
    state = u(D_STATE_PTR)
    results = []
    for buffer in (0, 1):
        start = F_BUF + buffer * 0x70000
        groups = walk_chain(ram, start)
        if groups is None:
            results.append('walk')
            continue
        work = bytearray(ram)
        calls = []
        for slot, blocks, _ in groups:
            for addr, kind in blocks:
                for off in (0, 4, 0x10, 0x14):
                    struct.pack_into('<I', work, addr + off, 0)
                calls.append((addr, slot, kind))
            struct.pack_into('<I', work, TABLE + 0x4000 + 4 * slot, 0)
        calls.sort()
        n = Native(lib, work)
        ctx = ctx_of(ram)
        contiguous, expect, kinds = 0, None, {}
        for addr, slot, kind in calls:
            if expect == addr - 0x100:
                contiguous += 1
            struct.pack_into('<I', work, ctx + 0x18, addr - 0x100)
            id_ = slot << 12
            used = 'blend' if kind[0] == 'ref' and kind[1] == 8 and (kind[2] - state) & M32 in BLEND else kind[0]
            kinds[used] = kinds.get(used, 0) + 1
            if kind[0] == 'open':
                out = C.c_uint32()
                rc = lib.em_packet_chain_001CB5F0(C.byref(n.chain), TABLE, id_, kind[1], C.byref(out), None)
            elif kind[0] == 'ref' and kind[1] == 8 and (kind[2] - state) & M32 in BLEND:
                rc = lib.em_packet_chain_001CB900(C.byref(n.chain), TABLE, id_, BLEND[(kind[2] - state) & M32])
            elif kind[0] == 'ref':
                rc = lib.em_packet_chain_001CB6B0(C.byref(n.chain), TABLE, id_, kind[1], kind[2])
            else:
                rc = lib.em_packet_chain_001CB760(C.byref(n.chain), TABLE, id_, kind[1])
            assert rc == 0, (beat, 'replay fault')
            expect = struct.unpack_from('<I', work, ctx + 0x18)[0]
        ok = True
        firsts = {blocks[-1][0] for _, blocks, _ in groups}
        for addr, _, _ in calls:
            for off in (0, 4, 0x10) + (() if addr in firsts else (0x14,)):
                if work[addr + off:addr + off + 4] != ram[addr + off:addr + off + 4]:
                    ok = False
        heads_known = 0
        for slot, blocks, known in groups:
            if known:
                heads_known += 1
                if u(TABLE + 0x4000 + 4 * slot) != struct.unpack_from('<I', work, TABLE + 0x4000 + 4 * slot)[0]:
                    ok = False
        # 001CB800's splice over the replayed slot words.
        link = start + 4
        for slot, blocks, _ in groups:
            newest = struct.unpack_from('<I', work, TABLE + 4 * slot)[0]
            if newest != blocks[0][0] or u(link) != newest & 0x0FFFFFFF:
                ok = False
            link = blocks[-1][0] + 0x14
        if u(link) != (start + 0x20) & 0x0FFFFFFF:
            ok = False
        if ok:
            return len(calls), contiguous, len(groups), heads_known, kinds
        results.append('mismatch')
    raise AssertionError((beat, 'no buffer replays', results))


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA, 'boot ELF is not the pinned build'
    ELF_BYTES[0] = elf
    lib = build_lib()
    base_cases = boundary_cases() + consumer_cases()
    rand = random_cases(pick(400, 60), 0xC4A1)
    case_beats = select(BEATS, 3, 0x9C, keep=lambda i, b: i == 0)
    stats = {'cases': 0, 'calls': 0, 'bytes': 0, 'faults': 0, 'fog_beats': 0, 'replayed_blocks': 0,
             'contiguous': 0, 'slots': 0, 'heads_known': 0, 'adapters': 0}
    rng = random.Random(7)
    for beat in BEATS:
        d = ROUTE / beat
        ram = bytearray((d / 'eeMemory.bin').read_bytes())
        spad = bytearray((d / 'scratchpad.bin').read_bytes())
        assert len(ram) == RAM_SIZE
        fog_capture(lib, elf, beat, ram, spad)
        stats['fog_beats'] += 1
        blocks, contiguous, slots, known, kinds = replay_capture(lib, beat, ram)
        stats['heads_known'] += known
        for k, v in kinds.items():
            stats['replayed_' + k] = stats.get('replayed_' + k, 0) + v
        stats['replayed_blocks'] += blocks
        stats['contiguous'] += contiguous
        stats['slots'] += slots
        if beat not in case_beats:
            continue
        pristine = bytes(ram)
        native_ram = bytearray(ram)
        cases = (base_cases + alias_cases(ctx_of(ram)) + frame_cases(ctx_of(ram))
                 + (rand if FULL else rng.sample(rand, min(len(rand), 20))))
        for case in cases:
            stats['bytes'] += run_case(lib, elf, ram, spad, pristine, native_ram, case)
            stats['cases'] += 1
            stats['calls'] += len(case[1])
        # the worker adapters take the same path, once per adapter variant
        for variant in range(len(ADAPTER_VARIANTS)):
            for case in consumer_cases():
                run_case(lib, elf, ram, spad, pristine, native_ram, case, adapters=variant)
                stats['adapters'] += 1
                fog_adapter, call_adapter = ADAPTER_VARIANTS[variant]
                for name, _ in case[1]:
                    if name in ('fog', 'call'):
                        key = 'adapter_' + (fog_adapter if name == 'fog' else call_adapter)
                        stats[key] = stats.get(key, 0) + 1
        stats['faults'] += fault_checks(lib, pristine)
        # 001CB9B0 over every mode
        state = struct.unpack_from('<I', ram, D_STATE_PTR)[0]
        for mode in MODES + list(range(-4, 12)):
            e = EM.Oracle(elf, ram, spad)
            e.run(0x1CB9B0, (mode,), stack=SPAD_STACK)
            assert sx(e.r[2] & M64, 64) == lib.em_packet_chain_001CB9B0(state, mode), ('001CB9B0', mode)
        # the float adapter bit-copies (no host conversion of denormal / Inf
        # payloads) and keeps f12 and f13 apart: each special value against
        # 150.0 in both positions, modes 2 and 3, against the bits entry.
        other = f32(150.0)
        for bits in SPECIAL:
            if bits >> 23 & 0xFF == 0xFF and bits & 0x7FFFFF:     # NaN payloads may be quietened by the host ABI
                continue
            for mode in (2, 3):
                for f12, f13 in ((bits, other), (other, bits)):
                    a, b = bytearray(ram), bytearray(ram)
                    na, nb = Native(lib, a), Native(lib, b)
                    assert lib.em_packet_chain_0021B9A0(C.byref(na.chain), mode, f12, f13) == 0
                    assert lib.em_packet_chain_w_0021B9A0_float(C.addressof(nb.chain), mode,
                                                                bits_float(f12), bits_float(f13)) == 0
                    assert a == b, ('float adapter', mode, hex(f12), hex(f13))
                    stats['float_adapter'] = stats.get('float_adapter', 0) + 1
    report = {'mode': 'full' if FULL else 'quick', 'beats': len(BEATS), 'case_beats': len(case_beats), **stats}
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'report.json').write_text(json.dumps(report, indent=1) + '\n')
    banner(part(len(case_beats), len(BEATS), 'beats with call cases'),
           f"{stats['cases']:,} call sequences ({stats['calls']:,} calls, {stats['bytes']:,} bytes written)",
           f"{stats['adapters']} adapter sequences (0021B9A0 calls through bits/float/heads: "
           f"{stats['adapter_bits']}/{stats['adapter_float']}/{stats['adapter_heads']}; 001CB760 "
           f"through 3/4 arguments: {stats['adapter_w3']}/{stats['adapter_w4']})",
           f"{stats['float_adapter']} float-adapter special checks", f"{stats['faults']} fail-stop checks",
           f"fog block and chain replay on all {len(BEATS)} beats "
           f"({stats['replayed_blocks']:,} blocks in {stats['slots']} slots)")
    print('PASS test_packet_chain_reference')


if __name__ == '__main__':
    main()
