#!/usr/bin/env python3
"""Compare the native effect manager (em_effect_manager.c) with the original code.

The oracle is the bounded EE/VU0 interpreter of test_effect_original_reference.py,
extended here with the integer multiply, divide, HI/LO moves and bitwise NOR
it lacked, and a caller-chosen stack. It
executes the ORIGINAL instructions of the pinned boot ELF (the user's
config/SCUS_971.12):

  001F0360 (the barrel), 001F6210 with 001F5CA0, 001F6BB0 with 001F6AC0,
  001F6EB0, 001F40C0, 001F0720, 001F0A60, 001F4D40, 001F1180 (whole: its
  countdown, facing test 001028D0/00102760/00102738 and draw block), and the
  leaves 001CD370, copy_qw4, 00102948, 001029C0, 00102A60 (+001029E8),
  001026A0, 001026D0, 001028B8, 00102C58, 00102918 and float_to_int 001281C0.

COP1 and VU0 arithmetic come from tools/ee_float_model.py (docs/EE_FLOAT_MODEL.md);
VCLIPw is outside that model and runs on finite operands only. Memory is a
captured route snapshot (../Extermination/build/s87/route/<beat>/eeMemory.bin +
scratchpad.bin).

Workers. 0021B9A0 (fog programmer) and 0011E2A8 (sinf) are bound to the
ORIGINAL instructions on both sides: the oracle runs them in place, the native
worker runs them in a shadow copy of the same RAM. Everything else is
recorded with scripted results: 001CB5F0/001CB760/001CB900 (packet chain),
00122BB8 (rand), 001F5C20, 001D8C20, 001C6120, 001D3D90, 001CAAC0, 001F6760,
001F66F0, 001F6850, 001F6640, 001F6E40, 001F6E80, 001F3620, 001F3E30, 001CD520.

After every call: every byte the original changed must be a byte the native
module models (or stack / oracle scratch); every modelled byte, every packet
byte, every worker call (with argument bits) and the fog programmer's state
must be equal.

Capture evidence (independent of the model): the two DMA buffers in each
route snapshot hold the original's own 001F0720 packets of the last two
frames, and beats 03/05/13 hold the map pickup's 001F0A60 glint packet. The
native packets are compared with them.

No original instruction bytes, disassembly or data are written by this file;
build/effect_manager_reference/report.json holds only counts.
"""
import ctypes as C
import hashlib
import json
import random
import struct
import subprocess
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ee_float_model as FM  # noqa: E402
import test_effect_original_reference as EO  # noqa: E402
from reference_mode import FULL, banner, part, pick, select  # noqa: E402

ROOT, DECOMP, ROUTE, ELF_SHA = EO.ROOT, EO.DECOMP, EO.ROUTE, EO.ELF_SHA
M32, M64, M128 = EO.M32, EO.M64, EO.M128
RET = EO.RET
Unsupported = EO.Unsupported
sx, fbits = EO.sx, EO.fbits
OUT = ROOT / 'build/effect_manager_reference'

ONE = 0x3F800000
STACK, NESTED_STACK = 0x01F80000, 0x01F40000
SCRATCH = 0x01E00000                 # oracle-only vectors
PKT, PKT_END = 0x01E40000, 0x01E80000  # oracle packet storage
LIST, LIST_BYTES = 0x01E80000, 0x10000  # synthetic display list for 001F6210
RING = 0x28F700 + 0x4DBEC0
CHAIN = 0x7635C0
ENTITIES = 0x7709C0
BEATS = sorted(p.name for p in ROUTE.iterdir() if (p / 'eeMemory.bin').exists())
MAP_OWNER = 0x7A67E0


# ------------------------------------------------------------------ oracle ---

class Oracle(EO.EE):
    """The shared interpreter plus integer multiply/divide, HI/LO moves, NOR
    and run(stack=)."""

    def __init__(self, elf, ram, spad, journal=None):
        super().__init__(elf, ram, spad)
        if journal is not None:
            self.journal = journal
        self.hi = self.lo = 0

    def execute(self, w, pc):
        op, fn = w >> 26, w & 63
        rs, rt, rd = w >> 21 & 31, w >> 16 & 31, w >> 11 & 31
        if (op == 0 or (op == 0x1C and (w >> 6 & 31) == 0)) and fn == 0x18:   # mult / mult1
            p = sx(self.r[rs], 32) * sx(self.r[rt], 32)
            if op == 0:
                self.lo, self.hi = sx(p & M32), sx(p >> 32 & M32)
            self.set32(rd, p & M32)
            return
        if op == 0 and fn == 0x1A:        # div
            a, b = sx(self.r[rs], 32), sx(self.r[rt], 32)
            if b == 0:
                raise Unsupported(('div by zero', hex(pc)))
            q = abs(a) // abs(b)
            q = q if (a < 0) == (b < 0) else -q
            self.lo, self.hi = sx(q & M32), sx((a - q * b) & M32)
            return
        if op == 0 and fn == 0x27:        # nor
            self.set64(rd, ~(self.r[rs] | self.r[rt]) & M64)
            return
        if op == 0 and fn in (0x10, 0x12):  # mfhi / mflo
            self.set64(rd, (self.hi if fn == 0x10 else self.lo) & M64)
            return
        super().execute(w, pc)

    def run(self, entry, args=(), floats=(), stack=STACK):
        self.r = [0] * 32
        self.r[28], self.r[29], self.r[31] = 0x27D370, stack, RET
        for i, v in enumerate(args):
            self.set64(4 + i, v & M64 if v >= 0 else v & M64)
        for i, v in enumerate(floats):
            self.f[12 + i] = v
        pc = entry
        while pc != RET:
            self.steps += 1
            if self.steps > 20_000_000:
                raise AssertionError('runaway')
            w = self.fetch(pc)
            op, rs, rt = w >> 26, w >> 21 & 31, w >> 16 & 31
            target, likely = None, False
            if op in (2, 3):
                target = (pc & 0xF0000000) | (w & 0x3FFFFFF) << 2
                if op == 3:
                    self.set64(31, pc + 8)
            elif op == 0 and (w & 63) in (8, 9):
                target = self.u32(rs)
                if w & 63 == 9:
                    self.set64(w >> 11 & 31, pc + 8)
            elif op in (4, 5, 0x14, 0x15):
                taken = ((self.r[rs] & M64) == (self.r[rt] & M64)) == (op in (4, 0x14))
                likely = op >= 0x14
                target = pc + 4 + (sx(w & 0xFFFF, 16) << 2) if taken else None
            elif op in (6, 7, 0x16, 0x17):
                v = sx(self.r[rs], 64)
                taken = v <= 0 if op in (6, 0x16) else v > 0
                likely = op >= 0x16
                target = pc + 4 + (sx(w & 0xFFFF, 16) << 2) if taken else None
            elif op == 1:
                v = sx(self.r[rs], 64)
                if rt > 3:
                    raise Unsupported(('regimm', hex(pc), rt))
                taken = v < 0 if rt in (0, 2) else v >= 0
                likely = rt in (2, 3)
                target = pc + 4 + (sx(w & 0xFFFF, 16) << 2) if taken else None
            elif op == 0x11 and rs == 8:
                taken = self.cond == (rt & 1)
                likely = bool(rt & 2)
                target = pc + 4 + (sx(w & 0xFFFF, 16) << 2) if taken else None
            else:
                self.execute(w, pc)
                pc += 4
                continue
            if target is None:
                if not likely:
                    self.execute(self.fetch(pc + 4), pc + 4)
                pc += 8
                continue
            self.execute(self.fetch(pc + 4), pc + 4)
            if target in self.stubs:
                self.stubs[target](self)
                links = op == 3 or (op == 0 and w & 63 == 9)
                pc = pc + 8 if links else self.r[31] & M32
                continue
            pc = target


def nested(e, entry, args=(), floats=()):
    """Run an original function on e's memory (same journal) on a second
    stack; returns (v0, f0)."""
    sub = Oracle(e.elf, e.ram, e.spad, journal=e.journal)
    sub.run(entry, args, floats, stack=NESTED_STACK)
    return sub.r[2] & M64, sub.f[0]


# ------------------------------------------------------------- native side ---

class Tables(C.Structure):
    _fields_ = [('pal', C.c_uint8 * 0x100), ('aura', C.c_uint8 * 0x320), ('kind', C.c_uint8 * 0xA20),
                ('list', C.c_uint8 * 0x4B0), ('colour', C.c_uint8 * 0x50)]


class Globals(C.Structure):
    _fields_ = [('d810700', C.c_uint8), ('d810701', C.c_uint8), ('d810702', C.c_uint8),
                ('d81075D', C.c_uint8), ('d810778', C.c_uint8), ('d81077B', C.c_uint8),
                ('d81079E', C.c_uint8), ('d25D524', C.c_int32), ('d25D6E4', C.c_int32),
                ('d275C44', C.c_int32), ('d28A59C', C.c_uint32), ('d275670', C.c_uint32),
                ('list_cursor', C.c_uint32), ('spad3400', C.c_uint32 * 32), ('spad3600', C.c_uint32 * 4),
                ('spad3A20', C.c_uint32)]


class View(C.Structure):
    _fields_ = [('clip0', C.c_uint32 * 16), ('clip2', C.c_uint32 * 16), ('fog', C.c_uint32 * 4),
                ('camera', C.c_uint32 * 16), ('screen', C.c_uint32 * 16)]


class Entity(C.Structure):
    _fields_ = [('live', C.c_int16), ('kind', C.c_int16)]


U32P = C.POINTER(C.c_uint32)
F = C.CFUNCTYPE
W_V = F(C.c_int, C.c_void_p)
W_PU = F(C.c_int, C.c_void_p, U32P)
W_I = F(C.c_int, C.c_void_p, C.c_int32)
W_6120 = F(C.c_int, C.c_void_p, C.c_uint32, C.c_int32, U32P)
W_RAND = F(C.c_int, C.c_void_p, C.POINTER(C.c_int32))
W_U = F(C.c_int, C.c_void_p, C.c_uint32)
W_AAC0 = F(C.c_int, C.c_void_p, U32P, C.c_uint32, C.c_uint32)
W_AT = F(C.c_void_p, C.c_void_p, C.c_uint32, C.c_uint32)
W_WORD = F(C.c_int, C.c_void_p, C.c_uint32, U32P)
W_UU = F(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32)
W_3620 = F(C.c_int, C.c_void_p, C.c_uint32, C.c_int32, C.POINTER(Entity))
W_3E30 = F(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32, C.c_int32, C.c_int32, C.c_int32)
W_5F0 = F(C.c_int, C.c_void_p, C.c_uint32, C.c_int32, C.c_int32, C.POINTER(C.c_void_p))
W_760 = F(C.c_int, C.c_void_p, C.c_uint32, C.c_int32, C.c_uint32)
W_900 = F(C.c_int, C.c_void_p, C.c_uint32, C.c_int32, C.c_int32)
W_9A0 = F(C.c_int, C.c_void_p, C.c_int32, C.c_uint32, C.c_uint32)
W_SIN = F(C.c_int, C.c_void_p, C.c_uint32, U32P)
W_D520 = F(C.c_int, C.c_void_p, C.c_int32, C.c_int32, C.c_uint32, C.c_uint64, C.c_uint32, C.c_uint32,
           C.c_uint32, C.c_uint32)
WORKER_TYPES = [('w_001F5C20', W_V), ('w_001F5CA0', W_PU), ('w_001D8C20', W_I), ('w_001C6120', W_6120),
                ('w_00122BB8', W_RAND), ('w_001D3D90', W_U), ('w_001CAAC0', W_AAC0), ('w_list_at', W_AT),
                ('w_001F6760', W_PU), ('w_001F66F0', W_U), ('w_001F6850', W_V), ('w_001F6640', W_U),
                ('w_word', W_WORD), ('w_001F6E40', W_UU), ('w_001F6E80', W_UU), ('w_001F3620', W_3620),
                ('w_001F3E30', W_3E30), ('w_001CB5F0', W_5F0), ('w_001CB760', W_760),
                ('w_001CB900', W_900), ('w_0021B9A0', W_9A0), ('w_0011E2A8', W_SIN),
                ('w_001CD520', W_D520)]


class Workers(C.Structure):
    _fields_ = [('ctx', C.c_void_p)] + WORKER_TYPES


class Fault(C.Structure):
    _fields_ = [('address', C.c_uint32), ('code', C.c_int32)]


class Manager(C.Structure):
    _fields_ = [('tables', C.POINTER(Tables)), ('globals', C.POINTER(Globals)),
                ('decals', C.POINTER(EO.Decals)), ('view', C.POINTER(View)),
                ('entities', C.POINTER(Entity)), ('workers', C.POINTER(Workers)), ('fault', Fault)]


class Aura(C.Structure):
    _fields_ = [('angle', C.c_uint32), ('timer', C.c_uint32), ('variant', C.c_int16),
                ('index', C.c_int16), ('state', C.c_int32)]


AURA_RAND = F(C.c_int32, C.c_void_p)
AURA_DRAW = F(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32)


class AuraWorkers(C.Structure):
    _fields_ = [('ctx', C.c_void_p), ('w_00122BB8', AURA_RAND), ('w_draw', AURA_DRAW)]


def build_lib():
    OUT.mkdir(parents=True, exist_ok=True)
    lib_path = OUT / 'effect_manager.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', 'src/game/em_effect_manager.c',
                    'src/game/em_effect_original.c', 'src/game/em_owner_services_original.c',
                    'src/game/em_pickup_items_original.c', '-o', str(lib_path)], cwd=ROOT, check=True)
    lib = C.CDLL(str(lib_path))
    P = C.POINTER
    lib.em_effect_manager_load_tables.argtypes = [C.c_char_p, C.c_size_t, P(Tables)]
    for name in ('001F0360', '001F6210', '001F6BB0', '001F6EB0', '001F40C0'):
        getattr(lib, 'em_effect_manager_' + name).argtypes = [P(Manager)]
    lib.em_effect_manager_001F0720.argtypes = [P(Manager), C.c_int32]
    lib.em_effect_manager_001F0A60.argtypes = [P(Manager), C.c_int32, C.c_int32, U32P, C.c_uint32,
                                               C.c_uint32, C.c_uint32, C.c_uint32, C.c_uint32]
    lib.em_effect_manager_001F4D40.argtypes = [P(Manager), C.c_uint32, U32P, C.c_uint32, C.c_uint32]
    lib.em_effect_manager_aura_draw.argtypes = [P(Manager), U32P, C.c_uint32, C.c_uint32, C.c_uint32]
    lib.em_pickup_aura_001F1180.argtypes = [P(Aura), P(C.c_float), P(C.c_float), C.c_uint8,
                                            P(AuraWorkers)]
    return lib


# ------------------------------------------------------------------- world ---

class Beat:
    """A route snapshot: the oracle's RAM and a shadow copy for the native
    side's original-bound workers. Both are restored after every case."""

    def __init__(self, elf, name):
        self.name = name
        d = ROUTE / name
        self.ram = bytearray((d / 'eeMemory.bin').read_bytes())
        self.spad = bytearray((d / 'scratchpad.bin').read_bytes())
        self.sram = bytearray(self.ram)
        self.sspad = bytearray(self.spad)
        self.elf = elf


def u32_at(buf, a): return struct.unpack_from('<I', buf, a)[0]


class World:
    """One case: the original image and its native twin with worker scripts."""

    def __init__(self, lib, tables, beat, script=None):
        self.lib, self.beat = lib, beat
        self.script = script or {}
        self.ee = Oracle(beat.elf, beat.ram, beat.spad)
        self.sh = Oracle(beat.elf, beat.sram, beat.sspad)
        self.o_log, self.n_log = [], []
        self.o_pk, self.n_pk = [], []
        self.pkt_next = PKT
        self.counts = {}
        self.keep = []
        self.tables = tables
        self.glob, self.view = Globals(), View()
        self.decals = EO.Decals()
        self.entities = (Entity * 0x80)()
        self.list_buf = (C.c_uint8 * LIST_BYTES)()
        self.pull(self.ee, self.glob, self.view, self.decals, self.entities)
        C.memmove(self.list_buf, bytes(beat.ram[LIST:LIST + LIST_BYTES]), LIST_BYTES)
        self.workers = Workers(None, *[t(getattr(self, 'n_' + name[2:])) for name, t in WORKER_TYPES])
        self.m = Manager(C.pointer(self.tables), C.pointer(self.glob), C.pointer(self.decals),
                         C.pointer(self.view), self.entities, C.pointer(self.workers), Fault())
        e = self.ee
        for a, fn in ((0x1CB5F0, self.o_5F0), (0x1CB760, self.o_760), (0x1CB900, self.o_900),
                      (0x21B9A0, self.o_9A0), (0x11E2A8, self.o_sin), (0x122BB8, self.o_rand),
                      (0x1F5C20, self.o_5C20), (0x1D8C20, self.o_8C20), (0x1C6120, self.o_6120),
                      (0x1D3D90, self.o_3D90), (0x1CAAC0, self.o_AAC0), (0x1F6760, self.o_6760),
                      (0x1F66F0, self.o_66F0), (0x1F6850, self.o_6850), (0x1F6640, self.o_6640),
                      (0x1F6E40, self.o_6E40), (0x1F6E80, self.o_6E80), (0x1F3620, self.o_3620),
                      (0x1F3E30, self.o_3E30), (0x1CD520, self.o_D520)):
            e.stubs[a] = fn
        ctx = u32_at(beat.ram, 0x275670)
        self.ctx = ctx
        self.allowed = []
        for lo, hi in ((RING, RING + 7 * 0xC00), (0x70003400, 0x70003480), (0x70003600, 0x70003610),
                       (0x70003A20, 0x70003A24), (0x275C44, 0x275C48), (ctx + 0x80, ctx + 0x100),
                       (ctx + 0x1C, ctx + 0x20), (PKT, PKT_END), (LIST, LIST + LIST_BYTES),
                       (NESTED_STACK - 0x4000, NESTED_STACK), (STACK - 0x4000, STACK),
                       (SCRATCH, SCRATCH + 0x1000)):
            self.allowed.append((lo, hi))
        for i in range(0x80):
            a = ENTITIES + 0x90 * i + 0x80
            self.allowed.append((a, a + 4))

    def close(self):
        self.ee.restore()
        self.sh.restore()

    # ---- RAM -> native
    @staticmethod
    def pull(e, g, v, d, ents):
        ld = e.load
        g.d810700, g.d810701, g.d810702 = ld(0x810700, 1), ld(0x810701, 1), ld(0x810702, 1)
        g.d81075D, g.d810778, g.d81077B, g.d81079E = (ld(0x81075D, 1), ld(0x810778, 1),
                                                      ld(0x81077B, 1), ld(0x81079E, 1))
        g.d25D524, g.d25D6E4, g.d275C44 = sx(ld(0x25D524)), sx(ld(0x25D6E4)), sx(ld(0x275C44))
        g.d28A59C = ld(0x28A59C)
        ctx = ld(0x275670)
        g.d275670 = ctx
        g.list_cursor = ld(ctx + 0x1C)
        for i in range(32):
            g.spad3400[i] = ld(0x70003400 + 4 * i)
        for i in range(4):
            g.spad3600[i] = ld(0x70003600 + 4 * i)
        g.spad3A20 = ld(0x70003A20)
        for i in range(16):
            v.clip0[i] = ld(ctx + 0x2240 + 4 * i)
            v.clip2[i] = ld(ctx + 0x22C0 + 4 * i)
            v.camera[i] = ld(0x70003AC0 + 4 * i)
            v.screen[i] = ld(0x70003A40 + 4 * i)
        for i in range(4):
            v.fog[i] = ld(ctx + 0xA0 + 4 * i)
        for n in range(7):
            d.index[n] = sx(ld(0x81F950 + 4 * n))
        blob = bytes(e.ram[RING:RING + 7 * 0xC00])
        C.memmove(C.addressof(d.slot), blob, len(blob))
        for i in range(0x80):
            ents[i].live = sx(ld(ENTITIES + 0x90 * i + 0x80, 2), 16)
            ents[i].kind = sx(ld(ENTITIES + 0x90 * i + 0x82, 2), 16)

    def put(self, a, data):
        """Write the same bytes into the oracle RAM, the shadow RAM and (after
        the call) re-pull the native state."""
        self.ee.write(a, data)
        self.sh.write(a, data)

    def repull(self):
        self.pull(self.ee, self.glob, self.view, self.decals, self.entities)
        C.memmove(self.list_buf, bytes(self.ee.ram[LIST:LIST + LIST_BYTES]), LIST_BYTES)

    # ---- scripts
    def next(self, key, side, default=0):
        """The next scripted value of `key` for one side ('o' or 'n')."""
        i = self.counts.get((key, side), 0)
        self.counts[(key, side)] = i + 1
        seq = self.script.get(key, ())
        return seq[i] if i < len(seq) else default

    # ---- original workers
    def o_5F0(self, e):
        count = sx(e.r[6], 32)
        a = self.pkt_next
        self.pkt_next += (count * 16 + 0xF) & ~0xF
        assert self.pkt_next <= PKT_END
        e.write(a, b'\xA5' * (count * 16))
        self.o_log.append(('5F0', e.u32(4), sx(e.r[5], 32), count))
        self.o_pk.append((a, count))
        e.set32(2, a)

    def o_760(self, e): self.o_log.append(('760', e.u32(4), sx(e.r[5], 32), e.u32(6)))
    def o_900(self, e): self.o_log.append(('900', e.u32(4), sx(e.r[5], 32), sx(e.r[6], 32)))

    def o_9A0(self, e):
        self.o_log.append(('9A0', sx(e.r[4], 32), e.f[12], e.f[13]))
        nested(e, 0x21B9A0, (e.r[4] & M64,), (e.f[12], e.f[13]))

    def o_sin(self, e):
        self.o_log.append(('sin', e.f[12]))
        e.f[0] = nested(e, 0x11E2A8, (), (e.f[12],))[1]

    def o_rand(self, e):
        v = self.next('rand', 'o')
        self.o_log.append(('rand', v))
        e.set32(2, v)

    def o_5C20(self, e): self.o_log.append(('5C20',))
    def o_8C20(self, e): self.o_log.append(('8C20', sx(e.r[4], 32)))

    def o_6120(self, e):
        h = self.next('6120', 'o', 0x00ABC000)
        self.o_log.append(('6120', e.u32(4), sx(e.r[5], 32)))
        e.set32(2, h)

    def o_3D90(self, e):
        self.o_log.append(('3D90', e.u32(4)))
        step = self.next('3D90', 'o', 0)
        if step:
            ctx = e.load(0x275670)
            e.store(ctx + 0x1C, e.load(ctx + 0x1C) + step)

    def o_AAC0(self, e):
        a0 = e.u32(4)
        self.o_log.append(('AAC0', tuple(e.load(a0 + 4 * i) for i in range(4)), e.u32(5), e.u32(6)))

    def o_6760(self, e):
        p = self.next('6760', 'o', 0x25D270)
        self.o_log.append(('6760',))
        e.set32(2, p)

    def o_66F0(self, e): self.o_log.append(('66F0', e.u32(4)))
    def o_6850(self, e): self.o_log.append(('6850',))
    def o_6640(self, e): self.o_log.append(('6640', e.u32(4)))
    def o_6E40(self, e): self.o_log.append(('6E40', e.u32(4), e.u32(5)))
    def o_6E80(self, e): self.o_log.append(('6E80', e.u32(4), e.u32(5)))

    def o_3620(self, e):
        a0, kind = e.u32(4), sx(e.r[5], 32)
        self.o_log.append(('3620', a0, kind))
        live, new_kind = self.next('3620', 'o', (0, None))
        e.store(a0 + 0x80, live, 2)
        if new_kind is not None:
            e.store(a0 + 0x82, new_kind, 2)

    def o_3E30(self, e):
        self.o_log.append(('3E30', e.u32(4), e.u32(5), sx(e.r[6], 32), sx(e.r[7], 32), sx(e.r[8], 32)))

    def o_D520(self, e):
        self.o_log.append(('D520', sx(e.r[4], 32), sx(e.r[5], 32), e.u32(6), e.r[7] & M64, e.r[8] & M64,
                           e.f[12], e.f[13], e.f[14]))

    # ---- native workers
    def n_001F5C20(self, _):
        self.n_log.append(('5C20',))
        return 0

    def n_001F5CA0(self, _, out):
        out[0] = nested(self.sh, 0x1F5CA0)[0] & M32   # the original selector on the shadow RAM
        return 0

    def n_001D8C20(self, _, mode):
        self.n_log.append(('8C20', mode))
        return 0

    def n_001C6120(self, _, bank, ident, out):
        out[0] = self.next('6120', 'n', 0x00ABC000)
        self.n_log.append(('6120', bank, ident))
        return 0

    def n_00122BB8(self, _, out):
        v = self.next('rand', 'n')
        self.n_log.append(('rand', v))
        out[0] = sx(v)
        return 0

    def n_001D3D90(self, _, handle):
        self.n_log.append(('3D90', handle))
        step = self.next('3D90', 'n', 0)
        self.glob.list_cursor = (self.glob.list_cursor + step) & M32
        return 0

    def n_001CAAC0(self, _, pos, cursor, context):
        self.n_log.append(('AAC0', tuple(pos[i] for i in range(4)), cursor, context))
        return 0

    def n_list_at(self, _, address, size):
        if not (LIST <= address and address + size <= LIST + LIST_BYTES):
            return None
        return C.addressof(self.list_buf) + (address - LIST)

    def n_001F6760(self, _, out):
        out[0] = self.next('6760', 'n', 0x25D270)
        self.n_log.append(('6760',))
        return 0

    def n_001F66F0(self, _, rec):
        self.n_log.append(('66F0', rec))
        return 0

    def n_001F6850(self, _):
        self.n_log.append(('6850',))
        return 0

    def n_001F6640(self, _, rec):
        self.n_log.append(('6640', rec))
        return 0

    def n_word(self, _, address, out):
        out[0] = self.sh.load(address)
        return 0

    def n_001F6E40(self, _, a0, a1):
        self.n_log.append(('6E40', a0, a1))
        return 0

    def n_001F6E80(self, _, a0, a1):
        self.n_log.append(('6E80', a0, a1))
        return 0

    def n_001F3620(self, _, address, kind, ent):
        self.n_log.append(('3620', address, kind))
        live, new_kind = self.next('3620', 'n', (0, None))
        ent.contents.live = sx(live, 16)
        if new_kind is not None:
            ent.contents.kind = sx(new_kind, 16)
        return 0

    def n_001F3E30(self, _, a0, a1, a2, a3, t0):
        self.n_log.append(('3E30', a0, a1, a2, a3, t0))
        return 0

    def n_001CB5F0(self, _, chain, ident, count, out):
        buf = (C.c_uint8 * (count * 16))(*([0xA5] * (count * 16)))
        self.keep.append(buf)
        self.n_pk.append(buf)
        self.n_log.append(('5F0', chain, ident, count))
        out[0] = C.addressof(buf)
        return 0

    def n_001CB760(self, _, chain, ident, address):
        self.n_log.append(('760', chain, ident, address))
        return 0

    def n_001CB900(self, _, chain, ident, mode):
        self.n_log.append(('900', chain, ident, mode))
        return 0

    def n_0021B9A0(self, _, mode, f12, f13):
        self.n_log.append(('9A0', mode, f12, f13))
        nested(self.sh, 0x21B9A0, (mode & M64,), (f12, f13))
        for i in range(4):
            self.view.fog[i] = self.sh.load(self.ctx + 0xA0 + 4 * i)
        return 0

    def n_0011E2A8(self, _, x, out):
        self.n_log.append(('sin', x))
        out[0] = nested(self.sh, 0x11E2A8, (), (x,))[1]
        return 0

    def n_001CD520(self, _, a0, a1, pos, tex0, rgb, f12, f13, f14):
        self.n_log.append(('D520', a0, a1, pos, tex0, sx(rgb) & M64, f12, f13, f14))
        return 0

    # ---- one call on both sides, then the comparison
    def run(self, label, entry, args, floats, native):
        e = self.ee
        mark = len(e.journal)
        self.o_log.clear()
        self.n_log.clear()
        self.o_pk.clear()
        self.n_pk.clear()
        self.keep.clear()
        self.pkt_next = PKT
        e.run(entry, args, floats)
        first = {}
        for buf, o, old in e.journal[mark:]:
            b0 = 0x70000000 if buf is e.spad else 0
            for i, b in enumerate(old):
                first.setdefault(b0 + o + i, b)
        res = native()
        assert res == 0, (label, 'native result', res, hex(self.m.fault.address), self.m.fault.code)
        self.compare(label, first)

    def compare(self, label, first):
        e = self.ee
        changed = [a for a, b in first.items() if e.load(a, 1) != b]
        stray = sorted(a for a in changed if not any(lo <= a < hi for lo, hi in self.allowed))
        assert not stray, (label, 'original wrote unmodelled bytes', [hex(a) for a in stray[:12]])
        assert self.m.fault.code == 0, (label, 'native fault', hex(self.m.fault.address), self.m.fault.code)
        # calls and packets
        assert self.o_log == self.n_log, dict(case=label, original=self.o_log, native=self.n_log)
        assert len(self.o_pk) == len(self.n_pk)
        for k, ((a, count), buf) in enumerate(zip(self.o_pk, self.n_pk)):
            want = bytes(e.ram[a:a + 16 * count])
            got = bytes(buf)
            assert want == got, (label, 'packet', k, count, first_diff(want, got))
        # modelled state
        g2, v2, d2, n2 = Globals(), View(), EO.Decals(), (Entity * 0x80)()
        self.pull(e, g2, v2, d2, n2)
        for name, _ in Globals._fields_:
            a, b = getattr(g2, name), getattr(self.glob, name)
            if not isinstance(a, int):
                a, b = list(a), list(b)
            assert a == b, (label, 'global', name, a, b)
        assert list(v2.fog) == list(self.view.fog), (label, 'fog', list(v2.fog), list(self.view.fog))
        assert bytes(d2) == bytes(self.decals), (label, 'ring')
        assert [(x.live, x.kind) for x in n2] == [(x.live, x.kind) for x in self.entities], (label, 'entities')
        assert bytes(self.list_buf) == bytes(e.ram[LIST:LIST + LIST_BYTES]), (label, 'display list')
        c = self.ctx
        assert bytes(e.ram[c + 0x80:c + 0x100]) == bytes(self.beat.sram[c + 0x80:c + 0x100]), \
            (label, 'fog programmer state')


def first_diff(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return hex(i), a[i & ~15:(i & ~15) + 16].hex(), b[i & ~15:(i & ~15) + 16].hex()
    return None


# ------------------------------------------------------------------ checks ---

def bump(counts, key, n=1):
    counts[key] = counts.get(key, 0) + n


def check_tables(elf, beat, tables):
    for lo, n in ((0x259CD0, 0x100), (0x259DD0, 0x320), (0x25A350, 0xA20), (0x25CA40, 0x4B0), (0x26EB20, 0x50)):
        o = lo - 0x100000 + 0x300
        assert bytes(beat.ram[lo:lo + n]) == elf[o:o + n], (beat.name, 'static window differs', hex(lo))
    o = 0x259CD0 - 0x100000 + 0x300
    assert bytes(tables.pal) == elf[o:o + 0x100]


def call_barrel(lib, w, label):
    w.run(label, 0x1F0360, (), (), lambda: lib.em_effect_manager_001F0360(C.byref(w.m)))


def barrel_frames(lib, tables, beat, frames, counts, seed):
    """001F0360 on the captured frame, then `frames` more frames in lockstep
    with a few synthetic live slots in every drawn lane (so the age pass
    reaches its float path)."""
    rng = random.Random(seed)
    w = World(lib, tables, beat)
    try:
        call_barrel(lib, w, (beat.name, 'barrel', 0))
        bump(counts, 'barrel frames')
        if frames:
            for n in (0, 1, 3, 4, 5, 6):
                total = 60 * (1 if n in (0, 5, 6) else 2)
                for i in rng.sample(range(32), 4):
                    life = rng.choice((1, 2, total - 1, total, total + 1, total + 2, rng.randrange(1, 200)))
                    a = RING + n * 0xC00 + i * 0x60
                    w.put(a + 0x58, struct.pack('<i', life))
                    w.put(a + 0x4C, struct.pack('<I', fbits(rng.uniform(0, 80))))
            w.repull()
        for f in range(frames):
            call_barrel(lib, w, (beat.name, 'barrel', f + 1))
            bump(counts, 'barrel frames')
    finally:
        w.close()


def lane_capture(lib, tables, beat, counts):
    """Model-independent: the original's own 001F0720 packets of the last two
    frames sit in the snapshot's two DMA buffers (six lanes, 0xDD0 apart:
    packet 1 at -0x30, the lane packet, packet 3 at +0xC30, packet 4 at
    +0xCA0). The latest buffer (the one holding the context +0x18 cursor)
    must hold all 24 native packets byte for byte; the older one packets 1..3
    (its packet 4 holds the previous frame's view, counted when equal)."""
    w = World(lib, tables, beat)
    try:
        call_barrel(lib, w, (beat.name, 'capture'))
        packets = [bytes(b) for b in w.n_pk]
    finally:
        w.close()
    assert len(packets) == 24
    ram = beat.ram
    tag = struct.pack('<II', 0x01000101, 0x6CC00020)
    hits, i = set(), 0
    while True:
        i = ram.find(tag, i + 1)
        if i < 0:
            break
        hits.add(i - 8)
    chains = [h for h in sorted(hits) if all(h + k * 0xDD0 in hits for k in range(6)) and h - 0xDD0 not in hits]
    ctx = u32_at(ram, 0x275670)
    cursor = u32_at(ram, ctx + 0x18)

    def parts(h):
        out = []
        for k in range(6):
            d2 = h + k * 0xDD0
            got = [bytes(ram[d2 - 0x30:d2 - 0x20]), bytes(ram[d2:d2 + 0xC10]),
                   bytes(ram[d2 + 0xC30:d2 + 0xC80]), bytes(ram[d2 + 0xCA0:d2 + 0xD40])]
            out.append([g == x for g, x in zip(got, packets[4 * k:4 * k + 4])])
        return out
    latest = [h for h in chains if abs(cursor - h) < 0x8000 and all(all(p) for p in parts(h))]
    assert latest, (beat.name, 'no chain in the latest DMA buffer equals the native barrel packets',
                    [(hex(h), parts(h)) for h in chains])
    older = [h for h in chains if abs(cursor - h) >= 0x8000 and all(p[0] and p[1] and p[2] for p in parts(h))]
    assert older, (beat.name, 'no chain in the older DMA buffer equals native packets 1..3')
    bump(counts, 'capture lane packets (latest frame)', 24)
    bump(counts, 'capture lane packets 1..3 (previous frame)', 18)
    if any(all(p[3] for p in parts(h)) for h in older):
        bump(counts, 'capture lane packet 4 (previous frame, static view)', 6)


def lane_synthetic(lib, tables, beat, counts, rng, cases):
    """001F0720(n) for every preset and out-of-range n, over lanes whose
    countdowns sit on every boundary of the age pass."""
    w = World(lib, tables, beat)
    try:
        for k in range(cases):
            n = [0, 1, 2, 3, 4, 5, 6, 7, -1, 0x7FFFFFFF, -0x80000000][k % 11]
            for lane in range(7):
                total = 60 * (1 if lane in (0, 5, 6) else 2)
                for i in range(32):
                    life = rng.choice((-5, -1, 0, 1, 2, total - 1, total, total + 1, 0x7FFFFFFF, -0x80000000,
                                       rng.randrange(-300, 300)))
                    a = RING + lane * 0xC00 + i * 0x60
                    w.put(a + 0x58, struct.pack('<i', life))
                    w.put(a + 0x4C, struct.pack('<I', rng.getrandbits(32)))
            w.repull()
            w.run((beat.name, 'lane', n), 0x1F0720, (n & M64,), (),
                  lambda: lib.em_effect_manager_001F0720(C.byref(w.m), n))
            bump(counts, '001F0720 synthetic calls')
    finally:
        w.close()


def rand_float(rng, lo, hi): return fbits(rng.uniform(lo, hi))


def glint_direct(lib, tables, beat, counts, rng, cases):
    """001F0A60 with visible and clipped positions around the map owner,
    every colour mode, depth offsets on both sides of w, both chains."""
    w = World(lib, tables, beat)
    try:
        owner = [struct.unpack('<f', struct.pack('<I', u32_at(beat.ram, MAP_OWNER + 0x100 + 4 * i)))[0]
                 for i in range(3)]
        eye = [struct.unpack('<f', struct.pack('<I', u32_at(beat.ram, 0x8105D0 + 4 * i)))[0] for i in range(3)]
        for k in range(cases):
            kind = k % 4
            if kind == 3:       # behind / far off: mostly clipped
                p = [eye[i] + (eye[i] - owner[i]) * rng.uniform(0.5, 3) for i in range(3)]
            else:
                p = [owner[i] + rng.uniform(-20, 20) for i in range(3)]
            pos = [fbits(x) for x in p] + [rng.getrandbits(32)]
            mode = [0, 1, 2, 3, 4, 5, -1, 0x100][k % 8]
            ca, cb = rng.getrandbits(32), rng.getrandbits(32)
            f12 = rand_float(rng, -3.2, 3.2)
            f13 = rng.choice((rand_float(rng, 0, 30), 0, 0x80000000))
            f14 = rng.choice((0, rand_float(rng, -50, 50), rand_float(rng, 0, 400), fbits(1.0),
                              fbits(-5000.0), fbits(5000.0)))
            a0 = rng.choice((0, 0, 1))
            w.put(SCRATCH, struct.pack('<4I', *pos))
            arr = (C.c_uint32 * 4)(*pos)
            w.run((beat.name, 'glint', k), 0x1F0A60, (a0, mode & M64, SCRATCH, ca, cb), (f12, f13, f14),
                  lambda: lib.em_effect_manager_001F0A60(C.byref(w.m), a0, mode, arr, ca, cb, f12, f13, f14))
            bump(counts, '001F0A60 direct calls')
            if w.o_pk:
                bump(counts, '001F0A60 direct calls that drew')
    finally:
        w.close()


def aura_run(lib, tables, beat, counts, ticks, label, flip=False, force=None, rands=()):
    """001F1180 (whole) on the map owner, original vs em_pickup_aura_001F1180
    with em_effect_manager_aura_draw as its w_draw, for `ticks` ticks."""
    w = World(lib, tables, beat, {'rand': list(rands)})
    try:
        e = w.ee
        if force is not None:
            w.put(MAP_OWNER + 0x2D0, struct.pack('<IIhhi', *force))
        if flip:
            p = [u32_at(beat.ram, MAP_OWNER + 0x100 + 4 * i) for i in range(3)]
            q = [u32_at(beat.ram, 0x8105D0 + 4 * i) for i in range(3)]
            f = [2 * struct.unpack('<f', struct.pack('<I', a))[0] - struct.unpack('<f', struct.pack('<I', b))[0]
                 for a, b in zip(p, q)]
            w.put(0x8105D0, struct.pack('<3f', *f))
        w.repull()
        w.allowed.append((MAP_OWNER + 0x2D0, MAP_OWNER + 0x2E0))
        world = [e.load(MAP_OWNER + 0xD0 + 4 * i) for i in range(16)]
        eye = [e.load(0x8105D0 + 4 * i) for i in range(4)]
        fw = (C.c_float * 16)()
        fe = (C.c_float * 4)()
        C.memmove(fw, struct.pack('<16I', *world), 64)
        C.memmove(fe, struct.pack('<4I', *eye), 16)
        wd = (C.c_uint32 * 16)(*world)
        area = e.load(0x810700, 1)
        aura = Aura(*struct.unpack('<IIhhi', bytes(beat.ram[MAP_OWNER + 0x2D0:MAP_OWNER + 0x2E0])))

        def arand(_):
            v = w.next('rand', 'n')
            w.n_log.append(('rand', v))
            return sx(v)

        def adraw(_, record, angle, timer):
            return lib.em_effect_manager_aura_draw(C.byref(w.m), wd, record, angle, timer)
        aw = AuraWorkers(None, AURA_RAND(arand), AURA_DRAW(adraw))
        drew = 0
        for t in range(ticks):
            w.run((beat.name, label, t), 0x1F1180, (MAP_OWNER,), (),
                  lambda: lib.em_pickup_aura_001F1180(C.byref(aura), fw, fe, area, C.byref(aw)))
            got = struct.pack('<IIhhi', aura.angle, aura.timer, aura.variant, aura.index, aura.state)
            assert got == bytes(e.ram[MAP_OWNER + 0x2D0:MAP_OWNER + 0x2E0]), (beat.name, label, t, 'aura block')
            drew += bool(w.o_pk)
        bump(counts, '001F1180 ticks', ticks)
        bump(counts, '001F1180 ticks that drew (001F0A60 packet)', drew)
    finally:
        w.close()


def aura_capture(lib, tables, beat, counts):
    """Model-independent: in beats 03, 05 and 13 the snapshot's two DMA
    buffers hold the map pickup's glint packets of the last two frames. The
    native draw block, fed the aura state that ramps to the captured one
    (the one angle in [-180, 180] that ramps to the captured angle; the one
    timer t with t + 0.05 = the captured timer under the EE add), the
    snapshot's view and the original 0021B9A0 on the snapshot's context, must
    reproduce the latest frame's packet byte for byte."""
    ram = beat.ram
    tag = struct.pack('<IQQ', 0x5000000D, 0x6035400000008002, 0x414141)
    found, i = [], 0
    while True:
        i = ram.find(tag, i + 1)
        if i < 0:
            break
        found.append(i - 0xC)
    angle, timer, variant, index, state = struct.unpack('<IIhhi', bytes(ram[MAP_OWNER + 0x2D0:MAP_OWNER + 0x2E0]))
    if not found:
        return None
    assert state == 1
    ctx = u32_at(ram, 0x275670)
    cursor = u32_at(ram, ctx + 0x18)
    latest = [a for a in found if abs(cursor - a) < 0x8000]
    assert len(latest) == 1, (beat.name, [hex(a) for a in found])
    want = bytes(ram[latest[0]:latest[0] + 0xE0])
    six = fbits(6.0)
    # The ramp keeps the angle in [-180, 180]: the pre-image is angle - 6, or
    # angle + 354 when the step wrapped; only one of them lies in the range.
    angles = [a for a in (FM.ee_sub(angle, six), FM.ee_add(angle, fbits(354.0)))
              if (lambda s: s if FM.ee_c_le(s, fbits(180.0)) else FM.ee_sub(s, fbits(360.0)))(FM.ee_add(a, six)) == angle
              and FM.ee_c_le(fbits(-180.0), a) and FM.ee_c_le(a, fbits(180.0))]
    step = fbits(0.05)
    base = FM.ee_sub(timer, step)
    timers = [t for t in range(base - 4, base + 5) if FM.ee_add(t, step) == timer]
    base_rec = {2: 0x25A040, 1: 0x259F90}.get(u32_at(ram, 0x810700) & 0xFF, 0x259EE0)
    record = base_rec + index * 0x2C if variant == 1 else 0x259DD0 + variant * 0x2C
    world = (C.c_uint32 * 16)(*[u32_at(ram, MAP_OWNER + 0xD0 + 4 * i) for i in range(16)])
    tried = matched = 0
    for a in angles:
        for t in timers:
            w = World(lib, tables, beat)
            try:
                assert lib.em_effect_manager_aura_draw(C.byref(w.m), world, record, a, t) == 0
                tried += 1
                matched += bool(w.n_pk) and bytes(w.n_pk[0]) == want
            finally:
                w.close()
    assert tried == 1 and matched == 1, (beat.name, 'the pre-image does not reproduce the captured glint packet',
                                         tried, matched)
    bump(counts, 'capture glint packets reproduced')
    return tried, matched


def sprites_6210(lib, tables, beat, counts, rng, synthetic):
    """001F6210: the captured AREA11 frame (001F5CA0 gives 0), then every
    area key that has a list (the whole loop: model lookup, transform,
    colour key search and jitter, the three display-list blocks)."""
    w = World(lib, tables, beat)
    try:
        w.run((beat.name, '6210'), 0x1F6210, (), (), lambda: lib.em_effect_manager_001F6210(C.byref(w.m)))
        bump(counts, '001F6210 calls')
    finally:
        w.close()
    keys = [0x301, 0x302, 0x400, 0x401, 0x700, 0x800, 0x803, 0xD00, 0xF00, 0x1500]
    for key in keys[:synthetic]:
        script = {'rand': [rng.choice((0, 1, 0x7FFFFFFF, rng.getrandbits(31))) for _ in range(4)],
                  '6120': [rng.getrandbits(32) for _ in range(4)],
                  '3D90': [rng.choice((0, 0x30, 0x100)) for _ in range(4)]}
        w = World(lib, tables, beat, script)
        try:
            w.put(0x810700, bytes([key >> 8, key & 0xFF]))
            w.put(w.ctx + 0x1C, struct.pack('<I', LIST))
            w.repull()
            w.run((beat.name, '6210', hex(key)), 0x1F6210, (), (),
                  lambda: lib.em_effect_manager_001F6210(C.byref(w.m)))
            assert any(x[0] == 'AAC0' for x in w.o_log), (hex(key), 'list not reached')
            bump(counts, '001F6210 calls')
            bump(counts, '001F6210 list records', sum(x[0] == 'AAC0' for x in w.o_log))
        finally:
            w.close()
    # One synthetic colour window: with the ELF rows (w = 0.1) and D_0026EB60.w
    # = 2^23 the jitter's w lane is unobservable (0.1 + 2^23 truncates to
    # 2^23), so row 0 w = 5.0 and D_0026EB60.w = 0 make it visible.
    patched = Tables()
    C.memmove(C.byref(patched), C.byref(tables), C.sizeof(Tables))
    patched.colour[0xC:0x10] = list(struct.pack('<f', 5.0))
    patched.colour[0x4C:0x50] = [0, 0, 0, 0]
    w = World(lib, patched, beat, {'rand': [0x12345678, 0x7FFFFFFF, 3], '3D90': [0x40]})
    try:
        w.put(0x26EB2C, struct.pack('<f', 5.0))
        w.put(0x26EB6C, struct.pack('<I', 0))
        w.put(0x810700, bytes([0x0D, 0x00]))
        w.put(w.ctx + 0x1C, struct.pack('<I', LIST))
        w.repull()
        w.run((beat.name, '6210', 'colour w'), 0x1F6210, (), (), lambda: lib.em_effect_manager_001F6210(C.byref(w.m)))
        bump(counts, '001F6210 calls')
    finally:
        w.close()


def selector_cases(rng):
    """Area/room keys that reach a path (0x1301, 0, 0x700, 0x1200) with every
    sub-mode and guard combination, plus the keys that return at once."""
    import itertools
    cases = []
    for sub, r0, r1, b78, b7b, b9e in itertools.product((0, 1, 2, 3, 4, 5, 6, 8, 9), (-1, 0), (-1, 0),
                                                         (0xFF, 0), (0xFF, 0), (0xFF, 0)):
        cases.append((0x13, 1, sub, 0, b78, b7b, b9e, r0, r1, 5, 5, 0x25D270, -1))
    for sub, b5d, p, rp in itertools.product((0, 1, 2), (0xFF, 0), (0x25D270, 0x25D2C0, SCRATCH + 0x100), (-1, 0)):
        cases.append((0, 0, sub, b5d, 0, 0, 0, rng.choice((-1, 0)), rng.choice((-1, 0)), 5, 5, p, rp))
    for (area, room), sub, t in itertools.product(((7, 0), (0x12, 0)), (0, 1, 2, 5), (-1, 0, 5)):
        cases.append((area, room, sub, 0, 0, 0, 0, 0, 0, t, t, 0x25D270, -1))
    for area, room in ((0x11, 0), (0xE, 0), (2, 0), (1, 0), (0, 1), (0, 2), (0xB, 0), (0xB, 1), (7, 1),
                       (0x13, 0), (0x12, 1)):
        cases.append((area, room, rng.choice((1, 2)), 0xFF, 0xFF, 0, 0xFF, -1, -1, -1, -1, 0x25D270, 0))
    return cases


def selectors(lib, tables, beat, counts, cases):
    """001F6BB0 (with the original 001F6AC0 reads) and 001F6EB0 over area /
    room / sub-mode keys and every guard byte and ready word."""
    w = World(lib, tables, beat)
    try:
        for c in cases:
            area, room, sub, b5d, b78, b7b, b9e, r0, r1, t7, t12, p, rp = c
            w.put(0x810700, bytes([area, room, sub]))
            for a, v in ((0x81075D, b5d), (0x810778, b78), (0x81077B, b7b), (0x81079E, b9e)):
                w.put(a, bytes([v]))
            w.put(0x25D270 + 0x24, struct.pack('<i', r0))
            w.put(0x25D2C0 + 0x24, struct.pack('<i', r1))
            w.put(SCRATCH + 0x100 + 0x24, struct.pack('<i', rp))
            w.put(0x25D524, struct.pack('<i', t7))
            w.put(0x25D6E4, struct.pack('<i', t12))
            w.script = {'6760': [p]}
            w.counts = {}
            w.repull()
            w.run(('6BB0', c), 0x1F6BB0, (), (), lambda: lib.em_effect_manager_001F6BB0(C.byref(w.m)))
            bump(counts, '001F6BB0 cases')
            bump(counts, '001F6BB0 worker calls', len(w.o_log))
            w.run(('6EB0', c), 0x1F6EB0, (), (), lambda: lib.em_effect_manager_001F6EB0(C.byref(w.m)))
            bump(counts, '001F6EB0 cases')
            bump(counts, '001F6EB0 worker calls', len(w.o_log))
    finally:
        w.close()


def entities_40C0(lib, tables, beat, counts, rng, cases):
    """001F40C0 with live entities of every kind; 001F3620 keeps, kills or
    re-kinds them."""
    w = World(lib, tables, beat)
    try:
        for k in range(cases):
            live = []
            for i in range(0x80):
                on = rng.random() < 0.3
                kind = rng.randrange(27)
                w.put(ENTITIES + 0x90 * i + 0x80, struct.pack('<hh', 0 if on else rng.choice((1, -1, 2)), kind))
                if on:
                    live.append(i)
            outcomes = [rng.choice(((0, None), (0, None), (1, None), (0, rng.randrange(27)), (-3, None)))
                        for _ in live]
            w.script = {'3620': outcomes}
            w.counts = {}
            w.repull()
            w.run(('40C0', k), 0x1F40C0, (), (), lambda: lib.em_effect_manager_001F40C0(C.byref(w.m)))
            bump(counts, '001F40C0 sweeps')
            bump(counts, '001F40C0 live entities', len(live))
    finally:
        w.close()


def pulse_4D40(lib, tables, beat, counts, rng, cases):
    """001F4D40 over colour words (byte-sized, and full 32-bit), rand values
    on the shift boundaries and arbitrary positions / sizes."""
    w = World(lib, tables, beat)
    try:
        for k in range(cases):
            if k % 3 == 0:
                col = [rng.getrandbits(32) for _ in range(4)]
            else:
                col = [rng.choice((0, 0x10, 0x20, 0x40, 0x66, 0x7F, 0x80, 0xFF)) for _ in range(4)]
            r = rng.choice((0, 1, 0x7FFFFF, 0x800000, 0x7FFFFFFF, 0x40000000, rng.getrandbits(31)))
            w.script = {'rand': [r]}
            w.counts = {}
            w.put(SCRATCH + 0x200, struct.pack('<4I', *col))
            pos = rng.choice((0x7A68B0, SCRATCH + 0x300, rng.getrandbits(25)))
            f12, f13 = rand_float(rng, 0, 40), rand_float(rng, 0, 10)
            arr = (C.c_uint32 * 4)(*col)
            w.run(('4D40', k), 0x1F4D40, (pos, SCRATCH + 0x200), (f12, f13),
                  lambda: lib.em_effect_manager_001F4D40(C.byref(w.m), pos, arr, f12, f13))
            bump(counts, '001F4D40 calls')
    finally:
        w.close()


def fail_stop(lib, tables, beat, counts):
    """A reached NULL worker faults before any write; a latched fault makes
    later calls return -1."""
    w = World(lib, tables, beat)
    try:
        for name, _ in WORKER_TYPES:
            saved = getattr(w.workers, name)
            setattr(w.workers, name, type(saved)())
            before = (bytes(w.glob), bytes(w.decals))
            w.m.fault = Fault()
            r = lib.em_effect_manager_001F0360(C.byref(w.m))
            reached = name in ('w_001F5C20', 'w_001F5CA0', 'w_001CB5F0', 'w_001CB760', 'w_001CB900')
            if reached:
                assert r == -1 and w.m.fault.code == 1, (name, r, w.m.fault.code)
                assert (bytes(w.glob), bytes(w.decals)) == before, (name, 'wrote before the fault')
                assert not w.n_log, (name, 'called a worker before the fault', w.n_log)
                assert lib.em_effect_manager_001F0720(C.byref(w.m), 0) == -1
                bump(counts, 'fail-stop checks')
            setattr(w.workers, name, saved)
            w.close()
            w = World(lib, tables, beat)
    finally:
        w.close()


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    lib = build_lib()
    tables = Tables()
    assert lib.em_effect_manager_load_tables(elf, len(elf), C.byref(tables)) == 0
    counts = {}
    rng = random.Random(0x1F0360)
    run_beats = select(BEATS, 3, 0x1F0360, keep=lambda i, b: b in ('03_panel_power', '05_boxes', '13_east_tower'))
    for name in run_beats:
        beat = Beat(elf, name)
        check_tables(elf, beat, tables)
        seed = zlib.crc32(name.encode())
        r = random.Random(seed)
        barrel_frames(lib, tables, beat, pick(40, 3), counts, seed)
        lane_capture(lib, tables, beat, counts)
        aura_capture(lib, tables, beat, counts)
        aura_run(lib, tables, beat, counts, pick(200, 12), 'aura')
        aura_run(lib, tables, beat, counts, pick(60, 8), 'aura-flip', flip=True)
        aura_run(lib, tables, beat, counts, pick(60, 6), 'aura-forced', flip=True,
                 force=(fbits(174.0), 0, 1, 2, 1), rands=[r.getrandbits(31) for _ in range(8)])
        glint_direct(lib, tables, beat, counts, r, pick(200, 24))
        lane_synthetic(lib, tables, beat, counts, r, pick(44, 11))
        if name == run_beats[0] or FULL:
            sprites_6210(lib, tables, beat, counts, r, pick(10, 10))
            selectors(lib, tables, beat, counts, select(selector_cases(r), 70, 7,
                                                             axes=(lambda c: (c[0], c[1]), lambda c: (c[0], c[2]))))
            entities_40C0(lib, tables, beat, counts, r, pick(20, 3))
            pulse_4D40(lib, tables, beat, counts, r, pick(300, 40))
            fail_stop(lib, tables, beat, counts)
    banner(part(len(run_beats), len(BEATS), 'route beats'))
    for k in sorted(counts):
        print(f'  {k}: {counts[k]:,}')
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'report.json').write_text(json.dumps({'mode': 'full' if FULL else 'quick', 'beats': run_beats,
                                                 'counts': counts}, indent=1))
    print('effect manager reference: 001F0360, 001F6210, 001F6BB0, 001F6EB0, 001F40C0, 001F0720, '
          '001F0A60, 001F4D40 and the 001F1180 draw block match the original instructions PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
