#!/usr/bin/env python3
"""Execute the remaining animation-runtime originals and compare em_anim_runtime_rest.c.

docs/ANIM_RUNTIME_REST.md (lane L33-anim-runtime-rest). The user's pinned
ELF and the captured AREA11 EE RAM + scratchpad images (the playable image
and the PCSX2 route beats, docs/FIRST_LEVEL_ROUTE.md) supply every
instruction and every input; none are embedded here.

The interpreter is the shared EE of test_player_slide_reference.py with the
COP1 / VU0 macro of test_coll_move_reference.FloatEE (every float operation
through tools/ee_float_model.py, docs/EE_FLOAT_MODEL.md), as subclassed by
test_pose_host_workers_reference.py to record the executed addresses.

Executed, unmodified (never hooked): 001C9E40 (with the SDK 0011E748 sqrtf
and its kernel 0011CB90), 001C9D50 (with quat_nlerp 001CA0A0 and
quat_to_mat3 001CA1C0), 001C7900 (with 001D88B0 and everything below it),
001CB2C0 and 001CAAC0 (with 00102948 and 001CB760), 001CACB0 and
anim_bone_array_setup 001CB5B0. The route case also runs 001CB3C0, the
attachment draw that calls 001C7900 and 001CB2C0 on Roger's record.

Hooks on the original side: 001CABA0 (the indicator draw, a GS packet
builder the census counts as a renderer boundary) and, in the 001CB3C0
case only, 001D1F80 and 001D3F50 (display-list call and mesh kernel,
renderer boundaries); each only records its arguments. 001D88B0 and
001CB760 are wrapped by pass-through recorders: the original body runs and
its argument words are recorded.

The native side runs over a copy of the same memory image. Its workers are:
  * w_0011E748 = em_anim_rest_sqrt_0011E748 over the production
    em_sdk_math_original.c (tables from the user's ELF, D_0026C5D0 read from
    the native RAM image);
  * w_001D88B0 and w_001CB760 = the ORIGINAL routines executed by a second
    interpreter directly over the native RAM and scratchpad (the channel
    cursors and SPR matrices synchronised in and out), so the native side
    gets exactly what the original callee does with the native's arguments;
  * w_001CABA0 records its arguments.
After every case the native RAM (channel cursors written back) must equal
the original RAM byte for byte, the native scratchpad must equal the
original scratchpad, every recorded worker call must match, and every
return value must match.

EM_TEST_FULL=1 runs every route beat and the exhaustive random sweeps.
"""
import ctypes as C
import hashlib
import math
import random
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode  # noqa: E402
import test_player_slide_reference as SR  # noqa: E402
import test_pose_host_workers_reference as PH  # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
REFERENCE = DECOMP / 'build/startup-reference'
ROUTE = DECOMP / 'build/s87/route'
LANE = ROOT / 'build/b7-anim-runtime-rest'

PLAYER, ROGER = 0x8102B0, 0x7A8830
D_CTX, D_B40, D_B48, D_MODE = 0x275670, 0x275B40, 0x275B48, 0x26C5D0
BUF_NEW, BUF_OLD = 0x288D40, 0x287F40          # anim_matrix_player's two pose buffers
SCRATCH_RAM = 0x01F00000                        # zero in every image (checked)
M_AT, Q_AT, OWNER_AT, ATT_AT, POS_AT = (SCRATCH_RAM + 0x100, SCRATCH_RAM + 0x200, SCRATCH_RAM + 0x400,
                                        SCRATCH_RAM + 0x800, SCRATCH_RAM + 0xC00)
SIDE = 0x7F000100                               # interpreter stack region (outside RAM/SPR)
RAM_SIZE = 0x2000000
SYNTH = '06_hill_slide'                         # the synthetic cases' image (has a scratchpad)

Q9E40, BLEND, UPLOAD, ATTACH, DEPTH, INDICATOR, SETUP = (0x1C9E40, 0x1C9D50, 0x1C7900, 0x1CB2C0,
                                                         0x1CAAC0, 0x1CACB0, 0x1CB5B0)
DRAW3C0, LIGHT, INSERT, IND_DRAW, DL_CALL, MESH = 0x1CB3C0, 0x1D88B0, 0x1CB760, 0x1CABA0, 0x1D1F80, 0x1D3F50

EXECUTED = ('func_001C9E40', 'func_001C9D50', 'func_001C7900', 'func_001CB2C0', 'func_001CAAC0',
            'func_001CACB0', 'anim_bone_array_setup')
# Statically reachable but never executable: 001C9E40's fall-through after
# the three branch tests (the branch index is 0, 1 or 2 by construction, so
# the jump to the epilogue and its delay slot cannot run).
UNREACHABLE = {0x1C9F34, 0x1C9F38}

ELF = None
LIB = None
IMAGES = {}
SDK_TABLES = None

U8, U32 = C.c_uint8, C.c_uint32
PU32, PF = C.POINTER(U32), C.POINTER(C.c_float)


def F(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def fnum(word):
    return struct.unpack('<f', struct.pack('<I', word & 0xFFFFFFFF))[0]


def u32(buf, at):
    return struct.unpack_from('<I', buf, at)[0]


# ------------------------------------------------------------------ interpreter

MASK = 0xFFFFFFFF


class EE(PH.EE):
    """The pose-host EE (records executed addresses) plus the MMI forms
    001D88B0's callees execute beyond the shared set (PEXTLW: rd = rs.w1
    rt.w1 rs.w0 rt.w0, high to low; PEXTUW the same over the upper words)."""

    def mmi(self, word, pc):
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        fn, sub = word & 63, word >> 6 & 31
        if fn == 0x08 and sub == 0x12:
            a, b = self.r[rs] & 0xFFFFFFFFFFFFFFFF, self.r[rt] & 0xFFFFFFFFFFFFFFFF
        elif fn == 0x28 and sub == 0x12:
            a, b = self.rh[rs] & 0xFFFFFFFFFFFFFFFF, self.rh[rt] & 0xFFFFFFFFFFFFFFFF
        else:
            return super().mmi(word, pc)
        if rd:
            self.r[rd] = (b & MASK) | (a & MASK) << 32
            self.rh[rd] = (b >> 32) | (a >> 32) << 32


# ------------------------------------------------------------------ native side

class Region(C.Structure):
    _fields_ = [('address', U32), ('size', U32), ('bytes', C.c_void_p), ('writable', C.c_int)]


class Channel(C.Structure):
    _fields_ = [('cursor', C.c_void_p), ('end', C.c_void_p)]


class Scratch(C.Structure):
    _fields_ = [('s3400', U32 * 16), ('s3440', U32 * 16), ('s3480', U32 * 16), ('s3AC0', U32 * 16)]


SCRATCH_AT = (('s3400', 0x3400), ('s3440', 0x3440), ('s3480', 0x3480), ('s3AC0', 0x3AC0))


class World(C.Structure):
    _fields_ = [('region', Region * 8), ('region_count', C.c_uint), ('channel', C.POINTER(Channel)),
                ('channel_count', U32), ('scratch', C.POINTER(Scratch)), ('spad34C0', PU32),
                ('spad34D0', PU32), ('spad34E0', PU32), ('spad3760', PU32), ('d275B40', PU32),
                ('d275B48', PU32)]


SQRT_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, U32, PU32)
LIGHT_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, PU32, PF, PF, U32)
INSERT_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, U32, C.c_int32, U32)
IND_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, U32, U32)


class Workers(C.Structure):
    _fields_ = [('sqrt_ctx', C.c_void_p), ('w_0011E748', SQRT_FN), ('ctx', C.c_void_p),
                ('w_001D88B0', LIGHT_FN), ('w_001CB760', INSERT_FN), ('w_001CABA0', IND_FN)]


class Fault(C.Structure):
    _fields_ = [('address', U32), ('code', C.c_int32)]


class Rest(C.Structure):
    _fields_ = [('world', World), ('workers', Workers), ('fault', Fault)]


class SdkContext(C.Structure):
    """EmSdkMathContext: tables, world {d26C5D0}, workers {context + four}, fault."""
    _fields_ = [('tables', C.c_void_p), ('d26C5D0', C.c_void_p), ('wctx', C.c_void_p),
                ('w0', C.c_void_p), ('w1', C.c_void_p), ('w2', C.c_void_p), ('w3', C.c_void_p),
                ('fault', U32)]


SOURCES = ('src/game/em_anim_runtime_rest.c', 'src/game/em_pose_host_workers.c',
           'src/game/em_player_stage_workers.c', 'src/game/em_player_floor.c',
           'src/game/em_player_reaction.c', 'src/game/em_player_fall.c',
           'src/game/em_owner_services_original.c',
           'src/game/em_stream_lanes_original.c', 'src/game/em_sdk_math_original.c')


def build_native():
    LANE.mkdir(parents=True, exist_ok=True)
    lib = LANE / ('anim_runtime_rest' + ('.dylib' if sys.platform == 'darwin' else '.so'))
    command = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared', '-fPIC',
               '-Isrc', *SOURCES, '-lm', '-o', str(lib)]
    digest = hashlib.sha256(' '.join(command).encode())
    for path in [ROOT / s for s in SOURCES] + sorted((ROOT / 'src').rglob('*.h')):
        digest.update(path.read_bytes())
    stamp = Path(str(lib) + '.sha256')
    if not (lib.exists() and stamp.exists() and stamp.read_text() == digest.hexdigest()):
        subprocess.run(command, cwd=ROOT, check=True)
        stamp.write_text(digest.hexdigest())
    n = C.CDLL(str(lib))
    R = C.POINTER(Rest)
    for name, args in {'001C9E40': [R, PU32, PU32], '001C9D50': [R, PU32, PU32, PU32, U32],
                       '001C7900': [R, PU32, U32, C.c_int32, C.c_int32, C.POINTER(C.c_void_p)],
                       '001CB2C0': [R, U32, C.c_int32, C.c_int32],
                       '001CAAC0': [R, U32, U32, C.POINTER(C.c_int32)],
                       '001CACB0': [R, U32], '001CB5B0': [R]}.items():
        getattr(n, 'em_anim_rest_' + name).argtypes = args
        getattr(n, 'em_anim_rest_' + name).restype = C.c_int
    n.em_anim_rest_sqrt_0011E748.argtypes = [C.c_void_p, U32, PU32]
    n.em_anim_rest_sqrt_0011E748.restype = C.c_int
    n.em_sdk_math_original_load_tables.argtypes = [C.c_char_p, C.c_size_t, C.c_void_p]
    n.em_sdk_math_original_load_tables.restype = C.c_int
    return n


def addr(fn):
    return C.cast(fn, C.c_void_p).value


def side_ee(ram, spad):
    """An interpreter that executes directly over the given RAM / scratchpad
    bytearrays (no copy)."""
    ee = EE(ELF, ram=b'', spad=b'')
    ee.mem, ee.spad = ram, spad
    return ee


class Native:
    """em_anim_runtime_rest over a copy of one memory image."""

    def __init__(self, ram, spad):
        self.ram, self.spad = bytearray(ram), bytearray(spad)
        self.rbuf = (U8 * len(self.ram)).from_buffer(self.ram)
        self.sbuf = (U8 * len(self.spad)).from_buffer(self.spad)
        self.base, self.sbase = C.addressof(self.rbuf), C.addressof(self.sbuf)
        self.scratch = Scratch()
        self.channels = (Channel * 4)()
        self.ctx = u32(self.ram, D_CTX) & (RAM_SIZE - 1)
        self.hi = [0] * 4
        self.sync_in()
        r = self.r = Rest()
        w = r.world
        w.region[0] = Region(0, RAM_SIZE, self.base, 0)
        w.region_count = 1
        w.channel, w.channel_count = C.cast(self.channels, C.POINTER(Channel)), 4
        w.scratch = C.pointer(self.scratch)
        for name, at in (('spad34C0', 0x34C0), ('spad34D0', 0x34D0), ('spad34E0', 0x34E0), ('spad3760', 0x3760)):
            setattr(w, name, C.cast(self.sbase + at, PU32))
        w.d275B40, w.d275B48 = C.cast(self.base + D_B40, PU32), C.cast(self.base + D_B48, PU32)
        self.sdk = SdkContext(C.addressof(SDK_TABLES), self.base + D_MODE)
        r.workers.sqrt_ctx = C.addressof(self.sdk)
        r.workers.w_0011E748 = SQRT_FN(addr(LIB.em_anim_rest_sqrt_0011E748))
        self.keep = [LIGHT_FN(self.light), INSERT_FN(self.insert), IND_FN(self.indicator)]
        r.workers.w_001D88B0, r.workers.w_001CB760, r.workers.w_001CABA0 = self.keep
        self.log = []
        self.mbuf = (U32 * 16)()

    # The native state held outside the images, synchronised both ways.
    def sync_out(self):
        for name, at in SCRATCH_AT:
            self.spad[at:at + 64] = bytes(getattr(self.scratch, name))
        for i in range(4):
            ptr = self.channels[i].cursor
            struct.pack_into('<I', self.ram, self.ctx + 0x10 + 4 * i, (ptr - self.base) | self.hi[i])

    def sync_in(self):
        for name, at in SCRATCH_AT:
            C.memmove(getattr(self.scratch, name), bytes(self.spad[at:at + 64]), 64)
        for i in range(4):
            word = u32(self.ram, self.ctx + 0x10 + 4 * i)
            self.hi[i] = word & ~(RAM_SIZE - 1) & 0xFFFFFFFF
            self.channels[i].cursor = self.base + (word & (RAM_SIZE - 1))
            if not self.channels[i].end:
                self.channels[i].end = self.base + RAM_SIZE

    def export(self):
        self.sync_out()
        return self.ram, self.spad

    def to_ee(self, ptr):
        return (ptr - self.base) | self.hi[0] if ptr else 0

    # Workers.
    def light(self, _, position, a, b, token):
        p = C.cast(position, C.c_void_p).value
        at = p - C.addressof(self.mbuf)
        assert at == 0x30, ('001D88B0 position is not m + 0x30', at)
        assert C.cast(a, C.c_void_p).value == C.addressof(self.scratch) + Scratch.s3400.offset
        assert C.cast(b, C.c_void_p).value == C.addressof(self.scratch) + Scratch.s3440.offset
        self.log.append(('001D88B0', bytes(self.mbuf)[0x30:0x40], 0x70003400, 0x70003440, token))
        self.sync_out()
        ee = side_ee(self.ram, self.spad)
        ee.write(SIDE, bytes(self.mbuf))
        ee.invoke(LIGHT, (SIDE + 0x30, 0x70003400, 0x70003440, token))
        self.sync_in()
        return 0

    def insert(self, _, table, key, payload):
        self.log.append(('001CB760', table, key & 0xFFFFFFFF, payload))
        self.sync_out()
        side_ee(self.ram, self.spad).invoke(INSERT, (table, key, payload))
        self.sync_in()
        return 0

    def indicator(self, _, owner, model):
        self.log.append(('001CABA0', owner, model))
        return 0


# ------------------------------------------------------------------ original side

def passthrough(ee, entry, log, record):
    """Run the original body of `entry` in place, recording its arguments."""
    def hook(e):
        log.append(record(e))
        del e.hooks[entry]
        ra = e.r[31]
        e.r[31] = SR.RETURN
        e.run(entry)
        e.r[31] = ra
        e.hooks[entry] = hook
    ee.hooks[entry] = hook


def recording(ee, entry, log, record):
    def hook(e):
        log.append(record(e))
    ee.hooks[entry] = hook


def original_hooks(ee, log):
    passthrough(ee, LIGHT, log, lambda e: ('001D88B0', e.read(e.arg(0), 16), e.arg(1), e.arg(2), e.arg(3)))
    passthrough(ee, INSERT, log, lambda e: ('001CB760', e.arg(0), e.arg(1), e.arg(2)))
    recording(ee, IND_DRAW, log, lambda e: ('001CABA0', e.arg(0), e.arg(1)))


# ------------------------------------------------------------------ one case

class Mismatch(AssertionError):
    pass


def first_diff(a, b, base=0):
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            return hex(base + i), a[i], b[i]
    return None


def words(buf, at, n=16):
    return list(struct.unpack_from(f'<{n}I', buf, at))


def run_op(ee, nat, op, log):
    """One operation on both sides: (original result, native status, native result)."""
    kind, r = op[0], C.byref(nat.r)
    ptr = lambda at: C.cast(nat.base + at, PU32)
    if kind == '9E40':
        m = op[1]
        ee.write(M_AT, struct.pack('<16I', *m))
        nat.ram[M_AT:M_AT + 64] = struct.pack('<16I', *m)
        ee.invoke(Q9E40, (Q_AT, M_AT))
        return 0, LIB.em_anim_rest_001C9E40(r, ptr(Q_AT), ptr(M_AT)), 0
    if kind == '9D50':
        out, a, b, blend = op[1:]
        ee.invoke(BLEND, (out, a, b), (blend,))
        return 0, LIB.em_anim_rest_001C9D50(r, ptr(out), ptr(a), ptr(b), blend), 0
    if kind == '7900':
        m, token, vuaddr, chan = op[1:]
        ee.write(SIDE, struct.pack('<16I', *m))
        v0, _ = ee.invoke(UPLOAD, (SIDE, token, vuaddr, chan))
        C.memmove(nat.mbuf, struct.pack('<16I', *m), 64)
        first = C.c_void_p()
        status = LIB.em_anim_rest_001C7900(r, nat.mbuf, token, vuaddr, chan, C.byref(first))
        return v0, status, nat.to_ee(first.value)
    if kind == 'B2C0':
        owner, vuaddr, chan = op[1:]
        ee.invoke(ATTACH, (owner, vuaddr, chan))
        return 0, LIB.em_anim_rest_001CB2C0(r, owner, vuaddr, chan), 0
    if kind == 'AAC0':
        position, payload = op[1:]
        v0, _ = ee.invoke(DEPTH, (position, payload))
        key = C.c_int32(-7)
        status = LIB.em_anim_rest_001CAAC0(r, position, payload, C.byref(key))
        return v0, status, key.value & 0xFFFFFFFF
    if kind == 'ACB0':
        ee.invoke(INDICATOR, (op[1],))
        return 0, LIB.em_anim_rest_001CACB0(r, op[1]), 0
    if kind == 'B5B0':
        ee.invoke(SETUP, (op[1],))                      # the callers' count argument (not read)
        return 0, LIB.em_anim_rest_001CB5B0(r), 0
    if kind == '3C0':
        # Roger's attachment draw 001CB3C0 as the route runs it: 001C7900 and
        # 001CB2C0 execute inside it; their arguments (with 001C7900's stack
        # matrix) are captured, then the native runs the same two calls.
        owner = op[1]
        seen = []
        passthrough(ee, UPLOAD, seen, lambda e: ('7900', e.read(e.arg(0), 64), e.arg(1), e.arg(2), e.arg(3)))
        passthrough(ee, ATTACH, seen, lambda e: ('B2C0', e.arg(0), e.arg(1), e.arg(2)))
        boundary = []                                   # renderer boundaries, not this lane's
        recording(ee, DL_CALL, boundary, lambda e: ('001D1F80', e.arg(0), e.arg(1), e.arg(2)))
        recording(ee, MESH, boundary, lambda e: ('001D3F50', e.arg(0)))
        ee.invoke(DRAW3C0, (owner,))
        for name in (UPLOAD, ATTACH, DL_CALL, MESH):
            del ee.hooks[name]
        assert [s[0] for s in seen] == ['7900', 'B2C0'], seen
        _, m, token, vuaddr, chan = seen[0]
        C.memmove(nat.mbuf, m, 64)
        first = C.c_void_p()
        s1 = LIB.em_anim_rest_001C7900(r, nat.mbuf, token, vuaddr, chan, C.byref(first))
        _, owner2, vuaddr2, chan2 = seen[1]
        s2 = LIB.em_anim_rest_001CB2C0(r, owner2, vuaddr2, chan2)
        return 0, s1 | s2, 0
    raise AssertionError(('op', kind))


def run_case(case):
    ram, spad = IMAGES[case['image']]
    ram, spad = bytearray(ram), bytearray(spad)
    for at, data in case.get('patch', ()):
        if at >= 0x70000000: spad[at - 0x70000000:at - 0x70000000 + len(data)] = data
        else: ram[at:at + len(data)] = data
    ee = EE(ELF, ram=ram, spad=spad)
    log = []
    original_hooks(ee, log)
    nat = Native(ram, spad)
    del ram, spad
    for index, op in enumerate(case['ops']):
        original, status, native = run_op(ee, nat, op, log)
        where = (case['label'], index, op[0])
        if status != 0:
            raise Mismatch(('native fault', where, status, hex(nat.r.fault.address), nat.r.fault.code))
        if original != native:
            raise Mismatch(('result', where, hex(original), hex(native)))
    if log != nat.log:
        raise Mismatch(('worker calls', case['label'], log[:6], nat.log[:6]))
    ram_n, spad_n = nat.export()
    if ee.mem != ram_n:
        raise Mismatch(('RAM', case['label'], first_diff(ee.mem, ram_n)))
    if ee.spad != spad_n:
        raise Mismatch(('scratchpad', case['label'], first_diff(ee.spad, spad_n, 0x70000000)))
    return ee.pcs, len(log)


def run_case_safe(case):
    try:
        return ('ok',) + run_case(case)
    except Mismatch as error:
        return ('fail', str(error)[:2000])


# ------------------------------------------------------------------ inputs

def rotation(rng, scale=1.0, translate=True):
    """A random row-major rotation matrix (rows scaled), as raw words."""
    ax, ay, az = (rng.uniform(-math.pi, math.pi) for _ in range(3))
    cx, sx, cy, sy, cz, sz = math.cos(ax), math.sin(ax), math.cos(ay), math.sin(ay), math.cos(az), math.sin(az)
    rows = [[cy * cz, cy * sz, -sy], [sx * sy * cz - cx * sz, sx * sy * sz + cx * cz, sx * cy],
            [cx * sy * cz + sx * sz, cx * sy * sz - sx * cz, cx * cy]]
    m = []
    for row in rows:
        m += [F(v * scale) for v in row] + [0]
    t = [rng.uniform(-500, 500) for _ in range(3)] if translate else [0.0] * 3
    return m + [F(v) for v in t] + [F(1.0)]


def diagonal_cases():
    """001C9E40's branch boundaries: each diagonal word largest, ties, and a
    trace of exactly zero."""
    out = []
    for d in ((1, 1, 1), (-1, -1, 1), (-1, 1, -1), (1, -1, -1), (0.5, 0.5, -1), (-1, 0.5, 0.5),
              (0.5, -1, 0.5), (0, 0, 0), (-1, -1, -1), (2, -1, -1), (-0.25, -0.25, -0.25)):
        m = [0] * 16
        m[0], m[5], m[10], m[15] = F(d[0]), F(d[1]), F(d[2]), F(1.0)
        m[1], m[2], m[4], m[6], m[8], m[9] = (F(v) for v in (0.125, -0.25, 0.375, 0.5, -0.0625, 0.75))
        out.append(m)
    return out


def indicator_owners(ram):
    return [a for a in range(0x700000, 0x820000, 0x10) if u32(ram, a + 0x4C) == INDICATOR]


def image_list():
    images = [('playable_ee', REFERENCE / 'playable_ee.bin', None)]
    quick = ('06_hill_slide', '14_roger_encounter')
    if ROUTE.exists():
        for beat in sorted(p for p in ROUTE.iterdir() if p.name[:2].isdigit()):
            if (beat / 'eeMemory.bin').exists() and (reference_mode.FULL or beat.name in quick):
                images.append((beat.name, beat / 'eeMemory.bin', beat / 'scratchpad.bin'))
    if not reference_mode.FULL:
        assert len(images) == 1 + len(quick), ('route captures missing', images)
    missing = [str(p) for _, p, _ in images if not p.exists()]
    assert not missing, ('captured RAM missing', missing)
    return images


def captured_cases(label, ram, rng):
    """Every lane routine over one captured image, with the inputs the route
    gives it."""
    n = ram[PLAYER + 0xC]
    nodes = [u32(ram, PLAYER + 0x110 + 4 * i) for i in range(n)]
    blend = u32(ram, PLAYER + 0x208)
    picked = nodes if reference_mode.FULL else [nodes[i] for i in sorted(rng.sample(range(n), 4))]
    cases = []
    # 001C9D50 as anim_matrix_player (0017B660) calls it: node i +0x90 from
    # the two pose buffers at the player's +0x208 blend.
    ops = [('9D50', node + 0x90, BUF_NEW + 0x40 * i, BUF_OLD + 0x40 * i, blend)
           for i, node in enumerate(nodes) if node in picked]
    for b in (F(0.0), F(1.0), F(0.25), F(1.5), F(-0.5)):
        i = rng.randrange(n)
        ops.append(('9D50', nodes[i] + 0x90, BUF_NEW + 0x40 * i, BUF_OLD + 0x40 * i, b))
    cases.append({'image': label, 'label': (label, 'blend'), 'ops': ops})
    # 001C9E40 over every captured node world matrix.
    cases.append({'image': label, 'label': (label, 'quat'),
                  'ops': [('9E40', words(ram, node + 0x90)) for node in picked]})
    # 001CB3C0 on Roger (the route's 001C7900 / 001CB2C0 caller), with
    # D_00275B40 set to Roger's node array as for Roger's own draw.
    if u32(ram, ROGER + 0x10) == 0x8237E0 and u32(ram, ROGER + 0x4C) == 0x1CAA00 and u32(ram, ROGER + 0x90):
        cases.append({'image': label, 'label': (label, 'roger 001CB3C0'), 'ops': [('3C0', ROGER)],
                      'patch': [(D_B40, struct.pack('<I', ROGER + 0x110))]})
    # 001C7900 with the player's node matrices (token = player + 0x80).
    ops = [('7900', words(ram, node + 0x90), PLAYER + 0x80, 0x3F5, 0) for node in picked[:3]]
    cases.append({'image': label, 'label': (label, 'upload'), 'ops': ops})
    # 001CB2C0 on Roger; 001CAAC0 at the player and node positions; the
    # indicator owners through 001CACB0; 001CB5B0.
    ops = [('B2C0', ROGER, 0x3F3, 0)] if u32(ram, ROGER + 0x90) else []
    ops += [('AAC0', PLAYER + 0xB0, PLAYER)] + [('AAC0', node + 0xC0, node) for node in picked]
    ops += [('ACB0', owner) for owner in indicator_owners(ram)]
    ops += [('B5B0', 0)]
    cases.append({'image': label, 'label': (label, 'leaves'), 'ops': ops})
    return cases


def random_cases(rng, count):
    """Synthetic inputs on the playable image: every branch and boundary."""
    cases = []
    ram, spad = IMAGES[SYNTH]
    vp = struct.unpack_from('<16f', spad, 0x3AC0)

    def depth(p):
        z = p[0] * vp[2] + p[1] * vp[6] + p[2] * vp[10] + vp[14]
        w = p[0] * vp[3] + p[1] * vp[7] + p[2] * vp[11] + vp[15]
        return 16 * z / w if w else float('inf')

    def search(want):
        for _ in range(200000):
            p = tuple(rng.uniform(-60000, 60000) for _ in range(3))
            if want(depth(p)):
                return p
        raise AssertionError('no point with the wanted depth on the captured view-projection')
    for index in range(count):
        kind = index % 5
        patch, ops = [], []
        if kind == 0:
            mats = [rotation(rng, rng.choice((1.0, 0.5, 2.0, 1.2))) for _ in range(6)]
            if index % 10 == 0:
                mats += diagonal_cases()
            if index % 15 == 0:                         # raw bit patterns
                mats += [[rng.getrandbits(32) for _ in range(16)] for _ in range(4)]
            ops = [('9E40', m) for m in mats]
        elif kind == 1:
            a, b = rotation(rng), rotation(rng)
            if index % 3 == 0:                          # a negative quaternion dot
                b = [a[k] if k in (0, 5, 10) or k >= 12 else (a[k] ^ 0x80000000) for k in range(16)]
            patch = [(M_AT, struct.pack('<16I', *a)), (M_AT + 0x40, struct.pack('<16I', *b))]
            for blend in (F(rng.uniform(-0.25, 1.25)), F(0.5), rng.getrandbits(32)):
                ops.append(('9D50', Q_AT, M_AT, M_AT + 0x40, blend))
            ops.append(('9D50', M_AT, M_AT, M_AT + 0x40, F(0.75)))   # out aliasing a
        elif kind == 2:
            ops = [('7900', rotation(rng, rng.uniform(0.5, 2.0)), PLAYER + 0x80, rng.randrange(0x400),
                    rng.randrange(4))]
        elif kind == 3:
            att = ATT_AT + 0x80 * rng.randrange(4) + rng.choice((0, 0, 4, 8, 12))   # unaligned too
            owner = bytearray(0x100)
            struct.pack_into('<I', owner, 0x90, att)
            patch = [(OWNER_AT, bytes(owner)), ((att + 0x40) & ~15, rng.randbytes(0x20))]
            ops = [('B2C0', OWNER_AT, rng.randrange(0x400), rng.randrange(4))]
        else:
            points = [(rng.uniform(-3000, 3000), rng.uniform(-500, 1500), rng.uniform(-3000, 3000)),
                      tuple(fnum(u32(ram, PLAYER + 0xB0 + 4 * k)) + rng.uniform(-40, 40) for k in range(3)),
                      tuple(rng.uniform(-1e6, 1e6) for _ in range(3))]
            # The key's clamps: a negative 12.4 depth (the last bucket) and
            # one below 0x1000 (the first bucket), found on the captured
            # view-projection with host floats (the oracle decides).
            points += [search(lambda d: -1e6 < d < -64), search(lambda d: 0 <= d < 0x1000 - 64)]
            for k, p in enumerate(points):
                at = POS_AT + 0x20 * k
                patch.append((at, struct.pack('<4f', *p, rng.choice((1.0, 0.0, -3.0)))))
                ops.append(('AAC0', at + rng.choice((0, 4, 12)), rng.getrandbits(32)))
            ops.append(('B5B0', 0))
            patch.append((D_B48, struct.pack('<I', rng.choice((0xFFFFFF00, rng.getrandbits(32))))))
        cases.append({'image': SYNTH, 'label': ('random', kind, index), 'ops': ops, 'patch': patch})
    return cases


# ------------------------------------------------------------------ checks without the oracle

def check_sqrt_domain(rng):
    """001C9E40's square-root argument is 1 + (a sum that the branch choice
    keeps non-negative), so the SDK sqrtf error path (a negative argument)
    is never taken. Random raw words of every class must never make the
    native call fault."""
    ram, spad = IMAGES[SYNTH]
    nat = Native(ram, spad)
    q, m = (U32 * 4)(), (U32 * 16)()
    count = reference_mode.pick(200000, 4000)
    specials = [0, 0x80000000, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0xFF800000, 0x7FC00000, 0x00000001,
                F(1.0), F(-1.0)]
    for i in range(count):
        for k in range(16):
            m[k] = rng.choice(specials) if i % 3 == 0 else rng.getrandbits(32)
        assert LIB.em_anim_rest_001C9E40(C.byref(nat.r), q, m) == 0, ('sqrt domain', list(m), nat.sdk.fault)
    return count


def check_fail_stop():
    """Every entry faults (-1) with nothing written when a view or worker it
    reaches is missing, an index or address is outside the owned storage, or
    a worker fails; a latched fault blocks later calls."""
    ram, spad = IMAGES[SYNTH]
    checks = 0
    null = lambda field, kind: (lambda n: setattr(n.r.world, field, kind()))

    def attempt(mutate, call, partial=None):
        nat = Native(ram, spad)
        mutate(nat)
        before_ram, before_spad = (bytes(x) for x in nat.export())
        status = call(nat, C.byref(nat.r))
        assert status == -1, ('fail-stop status', status)
        assert nat.r.fault.code != 0, 'no fault latched'
        after_ram, after_spad = nat.export()
        if partial is None:
            assert after_ram == before_ram and after_spad == before_spad, 'fail-stop wrote'
        else:
            partial(before_ram, before_spad, after_ram, after_spad)
        assert call(nat, C.byref(nat.r)) == -1, 'latched fault did not block'
        return 1

    m_ptr = lambda n: C.cast(n.base + PLAYER + 0x110, PU32)
    up = lambda n, r: LIB.em_anim_rest_001C7900(r, n.mbuf, PLAYER + 0x80, 0x3F5, 0, None)
    blend = lambda n, r: LIB.em_anim_rest_001C9D50(r, C.cast(n.base + Q_AT, PU32), C.cast(n.base + M_AT, PU32),
                                                   C.cast(n.base + M_AT, PU32), F(0.5))

    def short(n):
        n.channels[0].end = n.channels[0].cursor + 0xFF

    def failing_sqrt_second(n):
        calls = []

        def sqrt(ctx, x, out):
            calls.append(x)
            if len(calls) > 1: return -1
            out[0] = F(1.0)
            return 0
        n.hold = SQRT_FN(sqrt)
        n.r.workers.w_0011E748 = n.hold

    def only_34C0(before_ram, before_spad, after_ram, after_spad):
        assert after_ram == before_ram
        assert after_spad[:0x34C0] == before_spad[:0x34C0] and after_spad[0x34D0:] == before_spad[0x34D0:]

    def failing_light(n):
        n.hold = LIGHT_FN(lambda *a: -1)
        n.r.workers.w_001D88B0 = n.hold

    for index, (mutate, call, partial) in enumerate((
            (null('scratch', C.POINTER(Scratch)), up, None),
            (lambda n: setattr(n.r.workers, 'w_001D88B0', LIGHT_FN()), up, None),
            (null('channel', C.POINTER(Channel)), up, None),
            (lambda n: setattr(n.r.world, 'channel_count', 0), up, None),
            (short, up, None),
            (failing_light, up, None),
            (lambda n: None, lambda n, r: LIB.em_anim_rest_001C7900(r, n.mbuf, 0, 0x3F5, 4, None), None),
            (lambda n: None, lambda n, r: LIB.em_anim_rest_001C7900(r, n.mbuf, 0, 0x3F5, -1, None), None),
            (lambda n: setattr(n.r.world, 'region_count', 0),
             lambda n, r: LIB.em_anim_rest_001CB2C0(r, ROGER, 0x3F3, 0), None),
            (lambda n: None, lambda n, r: LIB.em_anim_rest_001CB2C0(r, 0x3000000, 0x3F3, 0), None),
            (lambda n: struct.pack_into('<I', n.ram, OWNER_AT + 0x90, 0x2FFFFF0),
             lambda n, r: LIB.em_anim_rest_001CB2C0(r, OWNER_AT, 0x3F3, 0), None),
            (lambda n: setattr(n.channels[0], 'end', n.channels[0].cursor + 0x3F),
             lambda n, r: LIB.em_anim_rest_001CB2C0(r, ROGER, 0x3F3, 0), None),
            (lambda n: setattr(n.r.workers, 'w_001CB760', INSERT_FN()),
             lambda n, r: LIB.em_anim_rest_001CAAC0(r, PLAYER + 0xB0, 0, None), None),
            (null('scratch', C.POINTER(Scratch)),
             lambda n, r: LIB.em_anim_rest_001CAAC0(r, PLAYER + 0xB0, 0, None), None),
            (lambda n: None, lambda n, r: LIB.em_anim_rest_001CAAC0(r, 0x2000000, 0, None), None),
            (lambda n: setattr(n.r.workers, 'w_001CABA0', IND_FN()),
             lambda n, r: LIB.em_anim_rest_001CACB0(r, ROGER), None),
            (lambda n: None, lambda n, r: LIB.em_anim_rest_001CACB0(r, 0x1FFFFC0), None),
            (null('d275B40', PU32), lambda n, r: LIB.em_anim_rest_001CB5B0(r), None),
            (null('d275B48', PU32), lambda n, r: LIB.em_anim_rest_001CB5B0(r), None),
            (lambda n: setattr(n.r.workers, 'w_0011E748', SQRT_FN()),
             lambda n, r: LIB.em_anim_rest_001C9E40(r, C.cast(n.base + Q_AT, PU32), m_ptr(n)), None),
            (lambda n: setattr(n.r.workers, 'w_0011E748', SQRT_FN()), blend, None),
            (null('spad34E0', PU32), blend, None),
            (null('spad3760', PU32), blend, None),
            (failing_sqrt_second, blend, only_34C0),
    )):
        try:
            checks += attempt(mutate, call, partial)
        except AssertionError as error:
            raise AssertionError(('fail-stop check', index, error)) from None
    return checks


# ------------------------------------------------------------------ coverage

def check_coverage(pcs):
    table = PH.function_table()
    missing = {}
    for name in EXECUTED:
        start, size = table[name]
        want = PH.reachable(start, size) - UNREACHABLE
        got = {pc for pc in want if pc in pcs or (PH.word_at(pc) >> 26 in (2, 3) or
                                                  (PH.word_at(pc) >> 26 == 0 and PH.word_at(pc) & 63 in (8, 9)))
               and pc + 4 in pcs}
        lost = sorted(want - got)
        if lost:
            missing[name] = [hex(pc) for pc in lost[:12]]
    return missing


# ------------------------------------------------------------------ main

def main():
    global ELF, LIB, SDK_TABLES
    started = time.time()
    ELF = SR.read_elf()
    PH.ELF = ELF
    LIB = build_native()
    SDK_TABLES = (C.c_uint64 * 256)()
    assert LIB.em_sdk_math_original_load_tables(ELF, len(ELF), SDK_TABLES) == 0
    table = PH.function_table()
    for label, path, spad_path in image_list():
        ram = path.read_bytes()
        spad = spad_path.read_bytes() if spad_path else bytes(0x4000)
        assert ram[SCRATCH_RAM:SCRATCH_RAM + 0x2000] == bytes(0x2000), ('scratch RAM not zero', label)
        for name in EXECUTED + ('func_001CB3C0', 'func_001D88B0', 'func_001CB760', 'func_0011E748',
                                'quat_nlerp', 'quat_to_mat3', 'func_00102948'):
            start, size = table[name]
            assert ram[start:start + size] == ELF[start - 0x100000 + 0x300:start - 0x100000 + 0x300 + size], \
                ('captured code differs from the ELF', label, name)
        IMAGES[label] = (ram, spad)
    rng = random.Random(0x7A11)
    cases = []
    for label in IMAGES:
        cases += captured_cases(label, IMAGES[label][0], rng)
    route_cases = len(cases)
    all_random = random_cases(random.Random(0x7A12), reference_mode.pick(600, 60))
    picked = reference_mode.select(all_random, 25, 0x7A13, axes=(lambda c: c['label'][1],))
    cases += picked
    results = reference_mode.parallel_map(run_case_safe, cases, cost=lambda c: len(c['ops']))
    failures = [r[1] for r in results if r[0] == 'fail']
    for f in failures[:10]:
        print('FAIL', f)
    assert not failures, f'{len(failures)} of {len(cases)} cases differ'
    pcs, calls = set(), 0
    for r in results:
        pcs |= r[1]
        calls += r[2]
    missing = check_coverage(pcs)
    assert not missing, ('original instructions never executed', missing)
    domain = check_sqrt_domain(random.Random(0x7A14))
    fails = check_fail_stop()
    reference_mode.banner(f'{route_cases} captured-state cases over {len(IMAGES)} images',
                          reference_mode.part(len(picked), len(all_random), 'synthetic cases'),
                          f'{sum(len(c["ops"]) for c in cases):,} original calls, {calls} worker calls compared',
                          f'{domain:,} sqrt-domain matrices', f'{fails} fail-stop checks')
    print(f'anim runtime rest: original-instruction reference PASSED ({time.time() - started:.1f} s)')


if __name__ == '__main__':
    main()
