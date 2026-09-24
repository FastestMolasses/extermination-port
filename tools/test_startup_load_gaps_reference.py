#!/usr/bin/env python3
"""Execute the census lane L34 originals and compare em_startup_load_gaps*.c
and em_task.c (docs/STARTUP_LOAD_GAPS.md).

The user's pinned ELF, the AREA11 overlay extracted from the user's disc and
the captured RAM (build/startup-reference, build/s87/route) supply every
instruction, table and state; none are embedded here. Routines executed
unmodified, each compared with its translation:

  001AB6A0 / 001AB740 / 001AB790   task table     (em_task.c, live)
  001AB4E0  display environments   001AB590  DMA CHCR watchdog
  001AC070  screen-flow task       001AF470  pad button assignment
  001AFCA0  state-0 re-arm, with 001AF5C0, 001AF690, 001AF710 and the C
            runtime memset 00121A28 executed as original code
  001B0F60  node start             001BB0E0  opening-script actor
  001B57E0  pad read, with 001B5F40 and 001B62A0
  001FB100  sound output mode, with 001FC6E0 and block_copy 00121870
  001FB370  bank-load gate, with 001FB3E0 and 001FB910
  008237C0  AREA11 overlay init    00199C50  collision scratchpad tables

Every other callee is hooked, scripted per case and recorded (never
simulated as a claim about the callee); the native side gets the same
script through its workers. The test asserts that the hooked set is exactly
the set of jal targets of these routines, that the original writes no byte
outside the compared storage, and that the unit cases take every
conditional branch of the executed routines both ways (the few outcomes no
input can produce are listed with the reason).

Default run (~10 s): the covering unit cases plus the captured states (the
opening RAM and one route beat). EM_TEST_FULL=1: the exhaustive sweeps and
every route beat.
"""
import ctypes as C
import os
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as rm  # noqa: E402
from test_player_slide_reference import EE, read_elf, sx32, s32, RETURN, DECOMP  # noqa: E402

MASK = 0xFFFFFFFF
OUT = ROOT / 'build' / os.environ.get('EM_LANE', 'startup_load_gaps_reference')
REFERENCE = DECOMP / 'build/startup-reference'
ROUTE = DECOMP / 'build/s87/route'
OVERLAY_FILE = DECOMP / 'extract/OVERLAY/AREA11.BIN'
OVERLAY_BASE = 0x823500

SIZES = {0x1AB4E0: 0xB0, 0x1AB590: 0xBC, 0x1AB6A0: 0xA0, 0x1AB740: 0x44, 0x1AB790: 0x38,
         0x1AC070: 0x334, 0x1AF470: 0x148, 0x1AF5C0: 0xCC, 0x1AF690: 0x5C, 0x1AF710: 0x6C,
         0x1AFCA0: 0x44, 0x1B0F60: 0x6C, 0x1B57E0: 0x74, 0x1B5F40: 0x280, 0x1B62A0: 0x20,
         0x1BB0E0: 0x228, 0x1FB100: 0x108, 0x1FC6E0: 0x8C, 0x1FB370: 0x68, 0x1FB3E0: 0x524,
         0x1FB910: 0xD4, 0x8237C0: 0x20, 0x199C50: 0x158}
# Executed as original code on the original side (pure memory, C runtime).
RUNTIME = {0x121A28, 0x121870}

# Branch outcomes no input can produce (address of the branch, outcome).
UNREACHABLE = {
    # 001AB6A0: the loop always runs its body before the bound test, and the
    # bound test falls out only after the third slot.
}


class GapEE(EE):
    """The shared EE core with the three DMA CHCR registers as memory, a
    record of every non-stack byte written, and branch-outcome recording."""

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.hw = bytearray(0x3000)
        self.written = set()
        self.outcomes = set()

    def _where(self, address):
        address &= MASK
        if 0x10008000 <= address < 0x1000B000:
            return self.hw, address - 0x10008000
        return super()._where(address)

    def save(self, address, value, size=4):
        address &= MASK
        if not 0x7F000000 <= address < 0x7F100000:
            self.written.update(range(address, address + size))
        super().save(address, value, size)

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None:
            self.outcomes.add((pc, b[0]))
        return b

    def full(self, n):
        return self.r[n] & 0xFFFFFFFFFFFFFFFF

    def mmi(self, word, pc):
        """Adds PCPYH (the C runtime memset 00121A28 broadcasts with it)."""
        if word & 63 == 0x29 and (word >> 6 & 31) == 0x1B:
            rt, rd = word >> 16 & 31, word >> 11 & 31
            lo, hi = self.r[rt] & 0xFFFF, self.rh[rt] & 0xFFFF
            if rd:
                self.r[rd] = lo * 0x0001000100010001
                self.rh[rd] = hi * 0x0001000100010001
            return
        super().mmi(word, pc)


def u32(o, a): return o.load(a, 4)
def put32(o, a, v): o.save(a, v & MASK, 4)


# ======================================================================
# Native side
# ======================================================================

I32, U32, U8, I8, I16, U64, VP = C.c_int32, C.c_uint32, C.c_uint8, C.c_int8, C.c_int16, C.c_uint64, C.c_void_p
P = C.POINTER
F = C.CFUNCTYPE


class DisplayWorkers(C.Structure):
    _fields_ = [('ctx', VP), ('w_001002E0', F(I32, VP, P(U8), I32, I32, I32, I32, I32))]


class FlowGlobals(C.Structure):
    _fields_ = [('d275BD4', I32), ('d275BDC', U8), ('d275BE0', U8), ('s3B90', U8)]


FLOW_CALLEES = [('w_001AEDB0', 0x1AEDB0, F(I32, VP, I32)), ('w_001D1EF0', 0x1D1EF0, F(I32, VP)),
                ('w_001AC3B0', 0x1AC3B0, F(I32, VP, P(I32))), ('w_001AC480', 0x1AC480, F(I32, VP, P(I32))),
                ('w_00225A00', 0x225A00, F(I32, VP)), ('w_001ACA20', 0x1ACA20, F(I32, VP, P(I32))),
                ('w_001FBC50', 0x1FBC50, F(I32, VP)), ('w_001D2880', 0x1D2880, F(I32, VP)),
                ('w_001AB790', 0x1AB790, F(I32, VP, U32)), ('w_00225AC0', 0x225AC0, F(I32, VP, I32, P(I32))),
                ('w_001AF150', 0x1AF150, F(I32, VP)), ('w_00200A40', 0x200A40, F(I32, VP, P(I32))),
                ('w_001D2830', 0x1D2830, F(I32, VP, I32, I32))]


class FlowWorkers(C.Structure):
    _fields_ = [('ctx', VP)] + [(n, t) for n, _, t in FLOW_CALLEES]


class BoneSlots(C.Structure):
    _fields_ = [('records', VP), ('slot', U32 * 0x480), ('head', U32), ('count', I16)]


class State0(C.Structure):
    _fields_ = [('player', VP), ('player_self', U32), ('status', VP), ('d81060C', P(U32)),
                ('bones', P(BoneSlots)), ('s31F4', P(U32))]


class State0Workers(C.Structure):
    _fields_ = [('ctx', VP), ('w_001D8BF0', F(I32, VP, P(U8), I32)), ('w_001AF8E0', F(I32, VP)),
                ('w_001D0660', F(I32, VP))]


class NodeStartWorkers(C.Structure):
    _fields_ = [('ctx', VP), ('w_001B0EA0', F(I32, VP, P(U8), P(I32))), ('w_001C63E0', F(I32, VP, P(U8), I32))]


class PadWorkers(C.Structure):
    _fields_ = [('ctx', VP), ('w_00110B80', F(I32, VP, I32, I32, P(I32))),
                ('w_00110E58', F(I32, VP, I32, I32, I32, I32, P(I32))),
                ('w_00110F60', F(I32, VP, I32, I32, I32, I32, P(I32))),
                ('w_001110B0', F(I32, VP, I32, I32, P(U8), P(I32))),
                ('w_001B5940', F(I32, VP, P(U8), P(U8), I32, P(I32)))]


class ScriptActor(C.Structure):
    _fields_ = [('node', VP), ('entry', VP), ('owner', VP)]


SA = P(ScriptActor)
ACTOR_CALLEES = [('w_001BAD40', 0x1BAD40, F(I32, VP, SA, P(I32))), ('w_001C5C90', 0x1C5C90, F(I32, VP, SA)),
                 ('w_001C68C0', 0x1C68C0, F(I32, VP, SA)), ('w_001BA580', 0x1BA580, F(I32, VP, SA, I32)),
                 ('w_001C64F0', 0x1C64F0, F(I32, VP, SA, U32)), ('w_001F9660', 0x1F9660, F(I32, VP, SA, I32)),
                 ('w_001BA540', 0x1BA540, F(I32, VP, SA)), ('w_001AFC10', 0x1AFC10, F(I32, VP, SA)),
                 ('w_method_4C', None, F(I32, VP, SA))]


class ActorWorkers(C.Structure):
    _fields_ = [('ctx', VP)] + [(n, t) for n, _, t in ACTOR_CALLEES]


class SoundFrame(C.Structure):
    _fields_ = [('d821058', U8), ('d28215B', U8), ('d81011C', U8), ('d281FD4', I32), ('d2820F4', I32),
                ('d281B70', U8 * 0x180), ('d281F30', (I32 * 4) * 10)]


class SoundWorkers(C.Structure):
    _fields_ = [('ctx', VP), ('w_001F9CF0', F(I32, VP, I32)), ('w_00119870', F(I32, VP, I32)),
                ('w_0011A608', F(I32, VP, U64, I32, I32)), ('w_001FB9F0', F(I32, VP, I32, I32, I32, I32))]


class BankFile(C.Structure):
    _fields_ = [('address', U32), ('bytes', VP), ('size', U32)]


class BankLoad(C.Structure):
    _fields_ = [('d282150', I8), ('d282151', I8), ('d282159', I8), ('d28215C', I8), ('d282190', I32),
                ('d282194', I32), ('d282198', I32), ('d28219C', U32), ('d2821A0', I32), ('d2821A4', U32),
                ('d281D30', I32 * 8), ('d281D50', I32 * 120), ('d275B18', I32), ('d275B1C', I32),
                ('base', I32 * 5)]


BANK_CALLEES = [('w_001195A8', 0x1195A8, F(I32, VP, I32)), ('w_0010F8F8', 0x10F8F8, F(I32, VP, I32, P(I32))),
                ('w_00119450', 0x119450, F(I32, VP, I32, P(I32))),
                ('w_001194B8', 0x1194B8, F(I32, VP, I32, U32, I32, P(I32))),
                ('w_001199F0', 0x1199F0, F(I32, VP, I32, I32)), ('w_0010F968', 0x10F968, F(I32, VP, I32)),
                ('w_0010BC00', 0x10BC00, F(I32, VP)), ('w_0010BAA0', 0x10BAA0, F(I32, VP, I32)),
                ('w_0010BBE0', 0x10BBE0, F(I32, VP, P(U32), I32, P(I32))),
                ('w_0010BBC0', 0x10BBC0, F(I32, VP, I32, P(I32)))]


class BankWorkers(C.Structure):
    _fields_ = [('ctx', VP)] + [(n, t) for n, _, t in BANK_CALLEES]


class OverlayInit(C.Structure):
    _fields_ = [('d275C1C', U32), ('d275C24', U32), ('d275C28', U32), ('d275C2C', U32)]


class CollFile(C.Structure):
    _fields_ = [('address', U32), ('bytes', VP), ('size', U32), ('d28A5A8', U32), ('d28A5A8_value', I16)]


class Task(C.Structure):
    _fields_ = [('state', U8), ('fn', VP), ('user', U8 * 24)]


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('slg.dylib' if sys.platform == 'darwin' else 'slg.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc',
                    'src/game/em_startup_load_gaps.c', 'src/game/em_startup_load_gaps_sound.c',
                    'src/game/em_task.c', '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    n.em_slg_001AB4E0.argtypes = [P(DisplayWorkers), VP, I32, I32]
    n.em_slg_001AB590.argtypes = [P(U8)]
    n.em_slg_001AB590.restype = None
    n.em_slg_001AC070.argtypes = [P(FlowWorkers), P(FlowGlobals), P(U8)]
    n.em_slg_001AF470.argtypes = [P(C.c_uint16), I32]
    n.em_slg_001AF470.restype = None
    n.em_slg_001AFCA0.argtypes = [P(State0Workers), P(State0)]
    n.em_slg_001B0F60.argtypes = [P(NodeStartWorkers), P(U8), I32, U32, P(I32)]
    n.em_slg_001B57E0.argtypes = [P(PadWorkers), P(U8)]
    n.em_slg_001BB0E0.argtypes = [P(ActorWorkers), SA]
    n.em_slg_001FB100.argtypes = [P(SoundWorkers), P(SoundFrame)]
    n.em_slg_001FB370.argtypes = [P(BankWorkers), P(BankLoad), P(BankFile), P(U32)]
    n.em_slg_008237C0.argtypes = [P(OverlayInit)]
    n.em_slg_008237C0.restype = None
    n.em_slg_00199C50.argtypes = [P(CollFile), P(U32)]
    n.em_task_init.restype = None
    n.em_task_register.argtypes = [C.c_int, VP]
    n.em_task_register.restype = P(Task)
    n.em_task_replace_current.argtypes = [VP]
    n.em_task_replace_current.restype = P(Task)
    n.em_task_dispatch.restype = None
    n.em_task_current.restype = P(Task)
    return n


def workers(struct_type, fns):
    """A worker struct from {field: python callable}; keeps the callbacks alive."""
    w = struct_type()
    keep = []
    for name, ftype in struct_type._fields_:
        if name == 'ctx':
            continue
        if name in fns:
            cb = ftype(fns[name])
            keep.append(cb)
            setattr(w, name, cb)
    w._keep = keep
    return w


def addr_of(buf):
    return C.addressof(buf)


# ======================================================================
# Shared checks
# ======================================================================

def jal_targets(o, start, size):
    out = set()
    for pc in range(start, start + size, 4):
        word = o.load(pc)
        if word >> 26 == 3:
            out.add((word & 0x3FFFFFF) << 2)
    return out


def check_callees(o, routines, hooked):
    """The jal targets of the routines are exactly the hooked, translated or
    runtime set (so nothing runs unhooked on one side only)."""
    targets = set()
    for r in routines:
        targets |= jal_targets(o, r, SIZES[r])
    allowed = set(hooked) | set(routines) | RUNTIME
    missing = sorted(t for t in targets if t not in allowed)
    assert not missing, ('callees neither hooked nor translated', [hex(t) for t in missing])
    unused = sorted(t for t in hooked if t not in targets)
    assert not unused, ('hooked addresses no routine calls', [hex(t) for t in unused])


def check_written(o, allowed, where):
    extra = sorted(a for a in o.written if not any(lo <= a < hi for lo, hi in allowed))
    assert not extra, (where, 'original wrote outside the compared storage', [hex(a) for a in extra[:16]])


COVERAGE = {}


def note_coverage(o, routines):
    for pc, taken in o.outcomes:
        for r in routines:
            if r <= pc < r + SIZES[r]:
                COVERAGE.setdefault(r, set()).add((pc, taken))
    o.outcomes = set()


def conditional_branches(o, start, size):
    out = []
    for pc in range(start, start + size, 4):
        word = o.load(pc)
        op, rt = word >> 26, word >> 16 & 31
        if op in (4, 5, 6, 7, 20, 21, 22, 23) or (op == 1 and rt in (0, 1, 2, 3, 16, 17, 18, 19)) or \
                (op == 17 and (word >> 21 & 31) == 8):
            if op == 4 and (word >> 16 & 0x3FF) == 0:        # beq zero, zero: an unconditional b
                continue
            out.append(pc)
    return out


def assert_coverage(o, routines):
    missing = []
    for r in routines:
        seen = COVERAGE.get(r, set())
        for pc in conditional_branches(o, r, SIZES[r]):
            for taken in (True, False):
                if (pc, taken) not in seen and (pc, taken) not in UNREACHABLE:
                    missing.append((hex(pc), taken))
    assert not missing, ('branch outcomes the unit cases never took', missing)
    return sum(len(conditional_branches(o, r, SIZES[r])) for r in routines)


def call_original(o, entry, args=()):
    o.written = set()
    o.r[29] = 0x7F0F0000
    o.call(entry, args)
    return o.r[2]


# ======================================================================
# 1. Task table: em_task.c against 001AB6A0 / 001AB740 / 001AB790
# ======================================================================

TABLE, CURSOR = 0x28A750, 0x70003B6C
TASK_FNS = [0x01F00000 + 0x100 * i for i in range(5)]


def task_case(elf, native, seed):
    rng = random.Random(seed)
    o = GapEE(elf)
    states = [rng.choice((0, 1, 2, 2, 4, 3, 5, 0xFF)) for _ in range(3)]
    fns = [rng.choice(TASK_FNS) for _ in range(3)]
    users = [bytes(rng.getrandbits(8) for _ in range(24)) for _ in range(3)]
    heads = [bytes(rng.getrandbits(8) for _ in range(3)) for _ in range(3)]
    for i in range(3):
        base = TABLE + 0x20 * i
        o.save(base, states[i], 1)
        o.write(base + 1, heads[i])
        put32(o, base + 4, fns[i])
        o.write(base + 8, users[i])
    put32(o, CURSOR, rng.getrandbits(32))
    # Scripted actions per call: (kind, a, b).
    plan = {}

    def action(index):
        if index not in plan:
            r = random.Random(f'{seed}:{index}')
            kind = r.choice(('none', 'none', 'replace', 'register', 'clear', 'user', 'wake'))
            plan[index] = (kind, r.randrange(3), r.choice(TASK_FNS), r.randrange(8, 0x20), r.getrandbits(8),
                           r.choice((1, 4, 2, 0)))
        return plan[index]

    olog = []

    def make_hook(fn):
        def hook(ee):
            cur = u32(ee, CURSOR)
            index = len(olog)
            olog.append(('call', fn, (cur - TABLE) // 0x20))
            kind, slot, other, k, v, st = action(index)
            if kind == 'replace':
                ee.nested(0x1AB790, (other,))
            elif kind == 'register':
                ee.nested(0x1AB740, (slot, other))
            elif kind == 'clear':
                ee.nested(0x1AB7D0)
            elif kind == 'user':
                ee.save(u32(ee, CURSOR) + k, v, 1)
            elif kind == 'wake':
                ee.save(TABLE + 0x20 * slot, st, 1)
        return hook
    for fn in TASK_FNS:
        o.hooks[fn] = make_hook(fn)

    native.em_task_init()
    recs = [native.em_task_register(i, None) for i in range(3)]
    callbacks, by_ptr = {}, {}
    nlog = []

    def make_cb(fn):
        def cb():
            cur = native.em_task_current()
            at = C.addressof(cur.contents)
            index = len(nlog)
            nlog.append(('call', fn, [C.addressof(r.contents) for r in recs].index(at)))
            kind, slot, other, k, v, st = action(index)
            if kind == 'replace':
                native.em_task_replace_current(callbacks[other])
            elif kind == 'register':
                native.em_task_register(slot, callbacks[other])
            elif kind == 'clear':
                native.em_task_current().contents.state = 0
            elif kind == 'user':
                native.em_task_current().contents.user[k - 8] = v
            elif kind == 'wake':
                recs[slot].contents.state = st
        return cb
    for fn in TASK_FNS:
        cb = C.CFUNCTYPE(None)(make_cb(fn))
        callbacks[fn] = C.cast(cb, VP)
        by_ptr[callbacks[fn].value] = fn
        callbacks[fn]._cb = cb
    for i in range(3):
        rec = recs[i].contents
        rec.state = states[i]
        rec.fn = callbacks[fns[i]]
        for k in range(24):
            rec.user[k] = users[i][k]
    for tick in range(rng.choice((1, 2, 3))):
        if rng.random() < 0.3:                    # a registration outside the dispatch
            slot, fn = rng.randrange(3), rng.choice(TASK_FNS)
            call_original(o, 0x1AB740, (slot, fn))
            native.em_task_register(slot, callbacks[fn])
        call_original(o, 0x1AB6A0)
        native.em_task_dispatch()
        for i in range(3):
            base, rec = TABLE + 0x20 * i, recs[i].contents
            expect = (o.load(base, 1), u32(o, base + 4), o.read(base + 8, 24))
            got = (rec.state, by_ptr.get(rec.fn or 0, rec.fn), bytes(rec.user))
            assert expect == got, ('task table', seed, tick, i, expect, got)
            assert o.read(base + 1, 3) == heads[i], ('bytes +1..+3 changed', seed)
        assert olog == nlog, ('task calls', seed, olog, nlog)
        check_written(o, [(TABLE, TABLE + 0x60), (CURSOR, CURSOR + 4)], ('task', seed))
        # After a dispatch the original cursor rests past the table; the
        # native current record is NULL (em_task.h).
        assert u32(o, CURSOR) == TABLE + 0x60
        assert not native.em_task_current()
    note_coverage(o, (0x1AB6A0, 0x1AB740, 0x1AB790))
    return len(olog)


# ======================================================================
# 2. 001AC070
# ======================================================================

FLOW_REC = 0x28A750
FLOW_G = {'d275BD4': (0x275BD4, 4), 'd275BDC': (0x275BDC, 1), 'd275BE0': (0x275BE0, 1), 's3B90': (0x70003B90, 1)}


def flow_case(elf, native, seed, o=None):
    rng = random.Random(seed)
    o = o or GapEE(elf)
    o.written = set()
    rec = bytearray(rng.getrandbits(8) for _ in range(0x20))
    rec[8] = rng.choice((0, 1, 2, 2, 3, 4, 5, 6, 7, 0xFF))
    rec[0xE] = rng.choice((1, 3, 0))
    rec[0xF] = rng.choice((0, 1, 2, 0xFF))
    g = {'d275BD4': rng.choice((0, 1, 2, 3, -1, 0x7FFFFFFF)), 'd275BDC': rng.choice((0, 1, 2)),
         'd275BE0': rng.getrandbits(8), 's3B90': rng.getrandbits(8)}
    o.write(FLOW_REC, bytes(rec))
    put32(o, 0x70003B6C, FLOW_REC)
    for k, (a, size) in FLOW_G.items():
        o.save(a, g[k] & ((1 << 8 * size) - 1), size)
    rets = {n: rng.choice({'w_001AC480': (0, 1, 1, 3, 3, 2), 'w_001ACA20': (0, 1, 2, 3, 4, -1),
                           'w_00225AC0': (0, 1, 2, 3), 'w_001AC3B0': (0, 1, 5),
                           'w_00200A40': (0, 1)}.get(n, (0,))) for n, _, _ in FLOW_CALLEES}
    # 001AC480 may rewrite +0xE / +0xF before returning (the original re-reads both).
    poke = (rng.choice((0xE, 0xF, 9)), rng.getrandbits(2)) if rng.random() < 0.3 else None

    olog = []
    for name, address, _ in FLOW_CALLEES:
        def hook(ee, name=name):
            args = {'w_001AEDB0': 1, 'w_00225AC0': 1, 'w_001AB790': 1, 'w_001D2830': 2}.get(name, 0)
            olog.append((name,) + tuple(s32(ee.r[4 + i]) for i in range(args)))
            if name == 'w_001AC480' and poke:
                ee.save(FLOW_REC + poke[0], poke[1], 1)
            ee.ret_int(rets[name])
        o.hooks[address] = hook
    call_original(o, 0x1AC070)

    user = (U8 * 24).from_buffer_copy(bytes(rec[8:]))
    ng = FlowGlobals(g['d275BD4'], g['d275BDC'], g['d275BE0'], g['s3B90'])
    nlog = []

    with_out = {'w_001AC3B0', 'w_001AC480', 'w_001ACA20', 'w_00225AC0', 'w_00200A40'}

    def mk(name):
        def fn(_ctx, *args):
            out = args[-1] if name in with_out else None
            args = args[:-1] if name in with_out else args
            nlog.append((name,) + tuple(s32(a) for a in args))
            if name == 'w_001AC480' and poke:
                user[poke[0] - 8] = poke[1]
            if out is not None:
                out[0] = rets[name]
            return 0
        return fn
    w = workers(FlowWorkers, {n: mk(n) for n, _, _ in FLOW_CALLEES})
    assert native.em_slg_001AC070(C.byref(w), C.byref(ng), user) == 0
    assert olog == nlog, ('001AC070 calls', seed, olog, nlog)
    assert o.read(FLOW_REC + 8, 24) == bytes(user), ('001AC070 record', seed)
    assert o.read(FLOW_REC, 8) == bytes(rec[:8])
    got = {'d275BD4': ng.d275BD4 & MASK, 'd275BDC': ng.d275BDC, 'd275BE0': ng.d275BE0, 's3B90': ng.s3B90}
    exp = {k: o.load(a, size) for k, (a, size) in FLOW_G.items()}
    assert got == exp, ('001AC070 globals', seed, got, exp)
    check_written(o, [(FLOW_REC + 8, FLOW_REC + 0x20)] + [(a, a + s) for a, s in FLOW_G.values()], ('flow', seed))
    note_coverage(o, (0x1AC070,))
    return o


# ======================================================================
# 3. 001AF470
# ======================================================================

def config_case(elf, native, configs):
    o = GapEE(elf)
    rng = random.Random(0x1AF470)
    for config in configs:
        o.written = set()
        before = bytes(rng.getrandbits(8) for _ in range(16))
        o.write(0x70003B74, before)
        call_original(o, 0x1AF470, (config,))
        m = (C.c_uint16 * 8).from_buffer_copy(before)
        native.em_slg_001AF470(m, config)
        assert o.read(0x70003B74, 16) == bytes(m), ('001AF470', hex(config))
        check_written(o, [(0x70003B74, 0x70003B84)], ('001AF470', config))
    note_coverage(o, (0x1AF470,))


# ======================================================================
# 4. 001AFCA0 (001AF5C0, 001AF690, 001AF710)
# ======================================================================

PLAYER, STATUS, F60C, SLOTS, RECORDS = 0x8102B0, 0x810130, 0x81060C, 0x7D4640, 0x7D5840
BCC, BD0, S31F4 = 0x275BCC, 0x275BD0, 0x700031F4


def state0_case(elf, native, seed, ram=None, spad=None):
    rng = random.Random(seed)
    o = GapEE(elf, ram, spad)
    if ram is None:
        o.write(PLAYER, bytes(rng.getrandbits(8) for _ in range(0x320)))
        o.write(STATUS, bytes(rng.getrandbits(8) for _ in range(0x180)))
        put32(o, F60C, rng.getrandbits(32))
        o.write(SLOTS, bytes(rng.getrandbits(8) for _ in range(0x480 * 4)))
        o.write(RECORDS, rng.randbytes(0x480 * 0xD0))
        o.save(BCC, rng.getrandbits(16), 2)
        put32(o, BD0, rng.getrandbits(32))
        put32(o, S31F4, rng.getrandbits(32))
    records_before = o.read(RECORDS, 0x480 * 0xD0)
    player = (U8 * 0x320).from_buffer_copy(o.read(PLAYER, 0x320))
    status = (U8 * 0x180).from_buffer_copy(o.read(STATUS, 0x180))
    f60c = U32(u32(o, F60C))
    records = (U8 * (0x480 * 0xD0)).from_buffer_copy(records_before)
    bones = BoneSlots()
    bones.records = addr_of(records)
    for i in range(0x480):
        bones.slot[i] = u32(o, SLOTS + 4 * i)
    bones.head, bones.count = u32(o, BD0), o.load(BCC, 2) - (0x10000 if o.load(BCC, 2) & 0x8000 else 0)
    s31 = U32(u32(o, S31F4))
    olog, nlog = [], []
    player_ret = rng.choice((0, 0, 1))
    fail = rng.choice((None, None, None, 'w_001D8BF0', 'w_001AF8E0', 'w_001D0660')) if ram is None else None

    o.hooks[0x1D8BF0] = lambda ee: (olog.append(('w_001D8BF0', ee.arg(0), s32(ee.r[5]))), ee.ret_int(player_ret))
    o.hooks[0x1AF8E0] = lambda ee: olog.append(('w_001AF8E0',))
    o.hooks[0x1D0660] = lambda ee: olog.append(('w_001D0660',))
    o.written = set()
    call_original(o, 0x1AFCA0)

    s = State0(addr_of(player), PLAYER, addr_of(status), C.pointer(f60c), C.pointer(bones), C.pointer(s31))
    fns = {'w_001D8BF0': lambda _c, p, a: (nlog.append(('w_001D8BF0', PLAYER if C.addressof(p.contents) == addr_of(player) else 0, a)), 0)[1],
           'w_001AF8E0': lambda _c: (nlog.append(('w_001AF8E0',)), 0)[1],
           'w_001D0660': lambda _c: (nlog.append(('w_001D0660',)), 0)[1]}
    w = workers(State0Workers, fns)
    assert native.em_slg_001AFCA0(C.byref(w), C.byref(s)) == 0
    assert olog == nlog, ('001AFCA0 calls', seed, olog, nlog)
    assert o.read(PLAYER, 0x320) == bytes(player), ('player', seed)
    assert o.read(STATUS, 0x180) == bytes(status), ('status', seed)
    assert u32(o, F60C) == f60c.value
    assert o.read(RECORDS, 0x480 * 0xD0) == bytes(records), ('bone records', seed)
    assert [u32(o, SLOTS + 4 * i) for i in range(0x480)] == list(bones.slot), ('bone slot array', seed)
    assert u32(o, BD0) == bones.head and o.load(BCC, 2) == bones.count & 0xFFFF, ('stack head', seed)
    assert u32(o, S31F4) == s31.value == 0
    check_written(o, [(PLAYER, PLAYER + 0x320), (STATUS, STATUS + 0x180), (F60C, F60C + 4),
                      (SLOTS, SLOTS + 0x1200), (RECORDS, RECORDS + 0x480 * 0xD0), (BCC, BCC + 2),
                      (BD0, BD0 + 4), (S31F4, S31F4 + 4)], ('state0', seed))
    note_coverage(o, (0x1AFCA0, 0x1AF5C0, 0x1AF690, 0x1AF710))
    # Fail-stop: an unbound worker faults before the first write.
    if fail:
        before = (bytes(player), bytes(status), f60c.value)
        w2 = workers(State0Workers, {k: v for k, v in fns.items() if k != fail})
        assert native.em_slg_001AFCA0(C.byref(w2), C.byref(s)) == -1
        assert (bytes(player), bytes(status), f60c.value) == before
    return 1


# ======================================================================
# 5. 001B0F60
# ======================================================================

NODE = 0x7A5640


def node_start_case(elf, native, seed, o=None):
    rng = random.Random(seed)
    o = o or GapEE(elf)
    o.written = set()
    node = bytearray(rng.getrandbits(8) for _ in range(0x2F0))
    o.write(NODE, bytes(node))
    word = rng.getrandbits(32)
    put32(o, 0x28A574, word)
    n = rng.choice((0, 1, 5, 0x7FFF, 0x8000, 0x12345, -1))
    busy = rng.choice((0, 0, 1, 7))
    olog = []
    o.hooks[0x1B0EA0] = lambda ee: (olog.append(('b0ea0', ee.arg(0))), ee.ret_int(busy))
    o.hooks[0x1C63E0] = lambda ee: olog.append(('bone', ee.arg(0), s32(ee.r[5])))
    ret = s32(call_original(o, 0x1B0F60, (NODE, n)))
    nn = (U8 * 0x2F0).from_buffer_copy(bytes(node))
    nlog, nret = [], I32()

    def b0ea0(_c, p, out):
        nlog.append(('b0ea0', NODE)); out[0] = busy; return 0
    w = workers(NodeStartWorkers, {'w_001B0EA0': b0ea0,
                                   'w_001C63E0': lambda _c, p, a: (nlog.append(('bone', NODE, a)), 0)[1]})
    assert native.em_slg_001B0F60(C.byref(w), nn, n, word, C.byref(nret)) == 0
    assert (olog, ret) == (nlog, nret.value), ('001B0F60', seed, olog, nlog, ret, nret.value)
    assert o.read(NODE, 0x2F0) == bytes(nn), ('001B0F60 node', seed)
    check_written(o, [(NODE, NODE + 0x2F0)], ('001B0F60', seed))
    note_coverage(o, (0x1B0F60,))
    return o


# ======================================================================
# 6. 001B57E0 / 001B5F40 / 001B62A0
# ======================================================================

PAD = 0x810E40
PAD_CALLEES = {0x110B80: 'w_00110B80', 0x110E58: 'w_00110E58', 0x110F60: 'w_00110F60',
               0x1110B0: 'w_001110B0', 0x1B5940: 'w_001B5940'}


def pad_case(elf, native, seed, o=None, block=None, states=None):
    rng = random.Random(seed)
    o = o or GapEE(elf)
    o.written = set()
    if block is None:
        b = bytearray(rng.getrandbits(8) for _ in range(0x3C))
        b[0x10] = rng.choice((0, 0, 0, 1, 2, 4, 4, 3, 0xFF))
        b[0x11] = rng.choice((0, 1))
        struct.pack_into('<H', b, 0x14, rng.choice((4, 7, 7, 4, 5, 0x104)))
        block = bytes(b)
    o.write(PAD, block)
    script = {}

    def ret_for(name, index):
        key = (name, index)
        if key not in script:
            r = random.Random(f'{seed}:{name}:{index}')
            if states is not None and name == 'w_00110B80':
                script[key] = states
            else:
                script[key] = r.choice({'w_00110B80': (0, 1, 2, 5, 6, 6, 6, 7, 3),
                                        'w_00110E58': (0, 4, 7, 5, 1, -1, 0x7FFFFFFF, 0x10004),
                                        'w_00110F60': (0, 1, 1, 2),
                                        'w_001110B0': (0, 1, 1, 2),
                                        'w_001B5940': (0, 0, 1, 2, -1)}[name])
            script[(name, index, 'poke')] = [(r.randrange(0x30, 0x3C), r.getrandbits(8)) for _ in range(r.randrange(3))]
        return script[key], script[(name, index, 'poke')]
    olog, counts = [], {}
    for address, name in PAD_CALLEES.items():
        def hook(ee, name=name):
            k = counts.get(name, 0); counts[name] = k + 1
            if name == 'w_00110B80': args = (s32(ee.r[4]), s32(ee.r[5]))
            elif name in ('w_00110E58', 'w_00110F60'): args = tuple(s32(ee.r[4 + i]) for i in range(4))
            elif name == 'w_001110B0': args = (s32(ee.r[4]), s32(ee.r[5]), ee.arg(2) - PAD)
            else: args = (ee.arg(0) - PAD, ee.arg(1) - PAD, s32(ee.r[6]))
            olog.append((name,) + args)
            value, pokes = ret_for(name, k)
            if name == 'w_001B5940':
                for at, v in pokes: ee.save(PAD + at, v, 1)
            ee.ret_int(value)
        o.hooks[address] = hook
    call_original(o, 0x1B57E0)

    nb = (U8 * 0x3C).from_buffer_copy(block)
    base = addr_of(nb)
    nlog, ncounts = [], {}

    def mk(name):
        def fn(_c, *args):
            k = ncounts.get(name, 0); ncounts[name] = k + 1
            out = args[-1]
            args = args[:-1]
            if name == 'w_001110B0':
                args = (args[0], args[1], C.addressof(args[2].contents) - base)
            elif name == 'w_001B5940':
                args = (C.addressof(args[0].contents) - base, C.addressof(args[1].contents) - base, args[2])
            nlog.append((name,) + tuple(args))
            value, pokes = ret_for(name, k)
            if name == 'w_001B5940':
                for at, v in pokes: nb[at] = v
            out[0] = value
            return 0
        return fn
    w = workers(PadWorkers, {n: mk(n) for n in PAD_CALLEES.values()})
    assert native.em_slg_001B57E0(C.byref(w), nb) == 0
    assert olog == nlog, ('001B57E0 calls', seed, olog, nlog)
    assert o.read(PAD, 0x3C) == bytes(nb), ('pad block', seed, o.read(PAD, 0x3C).hex(), bytes(nb).hex())
    check_written(o, [(PAD, PAD + 0x3C)], ('pad', seed))
    note_coverage(o, (0x1B57E0, 0x1B5F40, 0x1B62A0))
    return o


# ======================================================================
# 7. 001BB0E0
# ======================================================================

ENTRY, OWNER = 0x6C0000, 0x6D0000
ACTOR_ARGS = {'w_001BAD40': 2, 'w_001BA580': 2, 'w_001F9660': 2}


def actor_case(elf, native, seed, o=None, node_at=None, method=None):
    rng = random.Random(seed)
    o = o or GapEE(elf)
    o.written = set()
    if node_at is None:
        node_at = NODE
        node = bytearray(rng.getrandbits(8) for _ in range(0x2F0))
        node[4] = rng.choice((0, 0, 1, 1, 1, 2, 3, 4, 0xFF))
        struct.pack_into('<H', node, 0x2E, rng.choice((0, 1, 5, 15, 16, 31, 33)))
        struct.pack_into('<II', node, 0x20, ENTRY, OWNER)
        method = 0x01E00000
        struct.pack_into('<I', node, 0x4C, method)
        o.write(NODE, bytes(node))
        entry = bytearray(rng.getrandbits(8) for _ in range(0x2C))
        struct.pack_into('<h', entry, 4, rng.choice((0x270D, 0x270C, 0x270E, 0, 3, 0x1234, -2)))
        struct.pack_into('<h', entry, 0xA, rng.choice((0, 0, 3, 4, 5, 6, 6, 1, 7, -1)))
        o.write(ENTRY, bytes(entry))
        o.write(OWNER, bytes(rng.getrandbits(8) for _ in range(0x30)))
        if rng.random() < 0.5:
            o.save(OWNER + 0x2E, rng.choice((0, 0xFFFF, 1 << (node[0x2E] & 31) & 0xFFFF)), 2)
    entry_at, owner_at = u32(o, node_at + 0x20), u32(o, node_at + 0x24)
    method = method if method is not None else u32(o, node_at + 0x4C)
    node_bytes = o.read(node_at, 0x50)
    done = rng.choice((0, 0, 1))
    olog = []
    for name, address, _ in ACTOR_CALLEES:
        def hook(ee, name=name):
            olog.append((name, ee.arg(0) - node_at) + ((s32(ee.r[5]) if name != 'w_001BAD40' else ee.arg(1),)
                                                       if name in ACTOR_ARGS else ())
                        + ((ee.f[12] & MASK,) if name == 'w_001C64F0' else ()))
            ee.ret_int(done if name == 'w_001BAD40' else 0)
        o.hooks[address if address else method] = hook
    call_original(o, 0x1BB0E0, (node_at,))

    nn = (U8 * 0x50).from_buffer_copy(node_bytes)
    ne = (U8 * 0x2C).from_buffer_copy(o.read(entry_at, 0x2C))
    no = (U8 * 0x30).from_buffer_copy(o.read(owner_at, 0x30))
    view = ScriptActor(addr_of(nn), addr_of(ne), addr_of(no))
    nlog = []

    def mk(name):
        def fn(_c, a, *args):
            assert a.contents.node == view.node
            if name == 'w_001BAD40':
                nlog.append((name, 0, entry_at)); args[0][0] = done
            elif name in ('w_001BA580', 'w_001F9660'):
                nlog.append((name, 0, args[0]))
            elif name == 'w_001C64F0':
                nlog.append((name, 0, args[0]))
            else:
                nlog.append((name, 0))
            return 0
        return fn
    w = workers(ActorWorkers, {n: mk(n) for n, _, _ in ACTOR_CALLEES})
    assert native.em_slg_001BB0E0(C.byref(w), C.byref(view)) == 0
    # 001BA580 takes a byte (unsigned char a1); compare what it can see.
    norm = lambda log: [(e[0], e[1], e[2] & 0xFF) if e[0] == 'w_001BA580' else e for e in log]
    assert norm(olog) == norm(nlog), ('001BB0E0 calls', seed, olog, nlog)
    assert o.read(node_at, 0x50) == bytes(nn), ('001BB0E0 node', seed)
    check_written(o, [(node_at, node_at + 0x50)], ('001BB0E0', seed))
    note_coverage(o, (0x1BB0E0,))
    return o


# ======================================================================
# 8. 001FB100 / 001FC6E0
# ======================================================================

SOUND = {'d821058': (0x821058, 1), 'd28215B': (0x28215B, 1), 'd81011C': (0x81011C, 1),
         'd281FD4': (0x281FD4, 4), 'd2820F4': (0x2820F4, 4)}
SOUND_CALLEES = {0x1F9CF0: 'w_001F9CF0', 0x119870: 'w_00119870', 0x11A608: 'w_0011A608', 0x1FB9F0: 'w_001FB9F0'}


def sound_case(elf, native, seed, o=None, captured=False):
    rng = random.Random(seed)
    o = o or GapEE(elf)
    o.written = set()
    if not captured:
        o.save(0x821058, rng.choice((0, 1, 2)), 1)
        o.save(0x28215B, rng.choice((0, 1, 2)), 1)
        o.save(0x81011C, rng.choice((0, 1, 2, 0x80)), 1)
        put32(o, 0x281FD4, rng.choice((0, 5, 23, 31, 32, 47, 63, 64, 70)))
        put32(o, 0x2820F4, rng.choice((0, 5, 23, 31, 32, 47, 63, 64, 70)))
        o.write(0x281B70, rng.randbytes(0x180))
        for i in range(10):
            put32(o, 0x281F30 + 16 * i, rng.choice((0, 0, 1, 2, 0xFFFFFFFF, 0x80000000)))
            put32(o, 0x281F34 + 16 * i, rng.choice((0, 0xFFFFFFFF, 5, 0x1A, 0x80000000)))
            put32(o, 0x281F38 + 16 * i, rng.getrandbits(32))
            put32(o, 0x281F3C + 16 * i, rng.getrandbits(32))
    s = SoundFrame()
    for k, (a, size) in SOUND.items():
        v = o.load(a, size)
        setattr(s, k, s32(v) if size == 4 else v)
    C.memmove(s.d281B70, o.read(0x281B70, 0x180), 0x180)
    for i in range(10):
        for j in range(4):
            s.d281F30[i][j] = s32(u32(o, 0x281F30 + 16 * i + 4 * j))
    olog = []
    for address, name in SOUND_CALLEES.items():
        def hook(ee, name=name):
            if name == 'w_0011A608':
                olog.append((name, ee.full(4), s32(ee.r[5]), s32(ee.r[6])))
            elif name == 'w_001FB9F0':
                olog.append((name,) + tuple(s32(ee.r[4 + i]) for i in range(4)))
            else:
                olog.append((name, s32(ee.r[4])))
        o.hooks[address] = hook
    call_original(o, 0x1FB100)
    nlog = []
    w = workers(SoundWorkers, {
        'w_001F9CF0': lambda _c, a: (nlog.append(('w_001F9CF0', a)), 0)[1],
        'w_00119870': lambda _c, a: (nlog.append(('w_00119870', a)), 0)[1],
        'w_0011A608': lambda _c, m, a, b: (nlog.append(('w_0011A608', m, a, b)), 0)[1],
        'w_001FB9F0': lambda _c, a, b, c, d: (nlog.append(('w_001FB9F0', a, b, c, d)), 0)[1]})
    assert native.em_slg_001FB100(C.byref(w), C.byref(s)) == 0
    assert olog == nlog, ('001FB100 calls', seed, olog, nlog)
    for k, (a, size) in SOUND.items():
        assert o.load(a, size) == getattr(s, k) & ((1 << 8 * size) - 1), ('001FB100', k, seed)
    assert o.read(0x281B70, 0x180) == bytes(s.d281B70), ('config block', seed)
    got = [s.d281F30[i][j] & MASK for i in range(10) for j in range(4)]
    assert [u32(o, 0x281F30 + 4 * k) for k in range(40)] == got, ('cues', seed)
    check_written(o, [(0x28215B, 0x28215C), (0x281C30, 0x281CF0), (0x281F30, 0x281FD0)], ('sound', seed))
    if not captured:
        note_coverage(o, (0x1FB100, 0x1FC6E0))
    return o


# ======================================================================
# 9. 001FB370 / 001FB3E0 / 001FB910
# ======================================================================

BANK_G = {'d282150': (0x282150, 1), 'd282151': (0x282151, 1), 'd282159': (0x282159, 1),
          'd28215C': (0x28215C, 1), 'd282190': (0x282190, 4), 'd282194': (0x282194, 4),
          'd282198': (0x282198, 4), 'd28219C': (0x28219C, 4), 'd2821A0': (0x2821A0, 4),
          'd2821A4': (0x2821A4, 4), 'd275B18': (0x275B18, 4), 'd275B1C': (0x275B1C, 4)}
FILE_AT = 0x1000000
BANK_TABLE = 0x264890


def bank_case(elf, native, seed, calls):
    rng = random.Random(seed)
    o = GapEE(elf)
    base_words = [u32(o, BANK_TABLE + 4 * i) for i in range(5)]
    address = FILE_AT + rng.choice((0, 4, 0x10, 0x3C))
    entries = rng.choice((0, 1, 2, 3))
    total = rng.choice((0, 1, 2, 3, 4))
    records = max(total, 1)
    size = 0x20 + entries * 16 + (records + 1) * 16 + 0x200
    f = bytearray(rng.randbytes(size))
    struct.pack_into('<I', f, 8, entries)
    struct.pack_into('<I', f, 0xC, total)
    struct.pack_into('<I', f, 0x10, rng.choice((0x100, 0x13F, 0x1000)))
    struct.pack_into('<I', f, 0x18, rng.choice((0x40, 0x100, 0x33)))
    buckets = [rng.choice((0, 1, 1, 2, 4)) for _ in range(records + 1)]
    for i in range(records + 1):
        at = 0x20 + entries * 16 + 16 * i
        struct.pack_into('<III', f, at, rng.choice((0x10, 0x800, 0x123)), rng.choice((0x40, 0x80, 0x1C)), buckets[i])
    o.write(address, bytes(f))
    # A sequence may start mid-load: the inner state, cursor and bucket are
    # then ones a load leaves (the cursor on an entry, the bucket one the
    # file names), plus the out-of-range states the jump table refuses.
    inner = rng.choice((0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, -1, 0x40))
    g = {'d282150': rng.choice((0, 0, 0, 1, 1, 2)), 'd282151': inner,
         'd282159': rng.choice((0, 0, 0, max(total - 1, 0), 0x7F, 0x80, 0xFF)),
         'd28215C': rng.choice((0, 1, 2)), 'd282190': rng.choice((0, 1, 4)) if inner in (2, 3, 4, 5, 6) else
         rng.choice((0x63, 0, 1)), 'd282194': rng.getrandbits(32),
         'd282198': rng.getrandbits(32), 'd28219C': rng.getrandbits(32), 'd2821A0': rng.choice((0, 3)),
         'd2821A4': address + 0x20 + entries * 16, 'd275B18': rng.choice((0, 0, 1, 3)),
         'd275B1C': rng.choice((0, 0, 1))}
    for k, (a, sz) in BANK_G.items():
        o.save(a, g[k] & ((1 << 8 * sz) - 1), sz)
    counts = [rng.choice((0, 1, 2)) for _ in range(8)]
    handles = [rng.getrandbits(32) for _ in range(120)]
    for i in range(8): put32(o, 0x281D30 + 4 * i, counts[i])
    for i in range(120): put32(o, 0x281D50 + 4 * i, handles[i])

    s = BankLoad()
    for k, (a, sz) in BANK_G.items():
        v = o.load(a, sz)
        setattr(s, k, (v - 0x100 if v & 0x80 else v) if sz == 1 else (s32(v) if k not in ('d28219C', 'd2821A4') else v))
    for i in range(8): s.d281D30[i] = counts[i]
    for i in range(120): s.d281D50[i] = s32(handles[i])
    for i in range(5): s.base[i] = s32(base_words[i])
    buf = (U8 * len(f)).from_buffer_copy(bytes(f))
    bf = BankFile(address, addr_of(buf), len(f))

    def ret_for(name, index):
        r = random.Random(f'{seed}:{name}:{index}')
        return {'w_0010F8F8': lambda: r.choice((0x80000, 0x80008, 0x1234567F)),
                'w_00119450': lambda: r.choice((0, 0, 1, 1, 1, -1, 2)),
                'w_001194B8': lambda: r.choice((5, 7, 0x11, -1)),
                'w_0010BBE0': lambda: r.choice((0, 3, 3, 3, 9)),
                'w_0010BBC0': lambda: r.choice((0, 1, -1, -1))}.get(name, lambda: 0)()
    olog, ocount = [], {}
    for name, address_, _ in BANK_CALLEES:
        def hook(ee, name=name):
            k = ocount.get(name, 0); ocount[name] = k + 1
            if name == 'w_0010BBE0':
                d = ee.arg(0)
                olog.append((name, tuple(u32(ee, d + 4 * i) for i in range(4)), s32(ee.r[5])))
            elif name == 'w_001194B8':
                olog.append((name, s32(ee.r[4]), ee.arg(1), s32(ee.r[6])))
            elif name == 'w_001199F0':
                olog.append((name, s32(ee.r[4]), s32(ee.r[5])))
            elif name == 'w_0010BC00':
                olog.append((name,))
            else:
                olog.append((name, s32(ee.r[4])))
            ee.ret_int(ret_for(name, k))
        o.hooks[address_] = hook
    nlog, ncount = [], {}

    def mk(name):
        def fn(_c, *args):
            k = ncount.get(name, 0); ncount[name] = k + 1
            if name == 'w_0010BBE0':
                nlog.append((name, tuple(args[0][i] for i in range(4)), args[1])); args[2][0] = ret_for(name, k)
            elif name in ('w_0010F8F8', 'w_00119450', 'w_0010BBC0'):
                nlog.append((name, args[0])); args[1][0] = ret_for(name, k)
            elif name == 'w_001194B8':
                nlog.append((name, args[0], args[1], args[2])); args[3][0] = ret_for(name, k)
            else:
                nlog.append((name,) + tuple(args))
            return 0
        return fn
    w = workers(BankWorkers, {n: mk(n) for n, _, _ in BANK_CALLEES})
    nret = U32()
    finished = 0
    for step in range(calls):
        o.written = set()
        ret = call_original(o, 0x1FB370, (address,)) & MASK
        assert native.em_slg_001FB370(C.byref(w), C.byref(s), C.byref(bf), C.byref(nret)) == 0, \
            ('native fault', seed, step)
        assert ret == nret.value, ('001FB370 result', seed, step, hex(ret), hex(nret.value))
        assert olog == nlog, ('001FB370 calls', seed, step, olog[-6:], nlog[-6:])
        for k, (a, sz) in BANK_G.items():
            assert o.load(a, sz) == getattr(s, k) & ((1 << 8 * sz) - 1), ('bank', k, seed, step)
        assert [u32(o, 0x281D30 + 4 * i) for i in range(8)] == [c & MASK for c in s.d281D30], ('counts', seed, step)
        assert [u32(o, 0x281D50 + 4 * i) for i in range(120)] == [c & MASK for c in s.d281D50], ('handles', seed, step)
        check_written(o, [(a, a + sz) for a, sz in BANK_G.values()] +
                      [(0x281D30, 0x281D50), (0x281D50, 0x281F30)], ('bank', seed, step))
        finished += ret != 0
    note_coverage(o, (0x1FB370, 0x1FB3E0, 0x1FB910))
    return finished


# ======================================================================
# 10. 008237C0, 11. 00199C50, 12. 001AB4E0, 13. 001AB590
# ======================================================================

def overlay_case(elf, native, overlay):
    o = GapEE(elf)
    o.write(OVERLAY_BASE, overlay)
    for a in (0x275C1C, 0x275C24, 0x275C28, 0x275C2C):
        put32(o, a, 0xA5A5A5A5)
    o.written = set()
    call_original(o, 0x8237C0)
    g = OverlayInit(0xA5A5A5A5, 0xA5A5A5A5, 0xA5A5A5A5, 0xA5A5A5A5)
    native.em_slg_008237C0(C.byref(g))
    assert (u32(o, 0x275C1C), u32(o, 0x275C24), u32(o, 0x275C28), u32(o, 0x275C2C)) == \
        (g.d275C1C, g.d275C24, g.d275C28, g.d275C2C), '008237C0'
    check_written(o, [(0x275C1C, 0x275C20), (0x275C24, 0x275C30)], '008237C0')
    note_coverage(o, (0x8237C0,))


COLL_FILE = 0x1100000


def coll_case(elf, native, seed, o=None, captured=None):
    rng = random.Random(seed)
    o = o or GapEE(elf)
    o.written = set()
    if captured is None:
        at = COLL_FILE + rng.choice((0, 0x10, 0x44))
        f = bytearray(rng.randbytes(0x40))
        struct.pack_into('<i', f, 0x1C, rng.choice((0xC, 0xC, 0, 0xB, 0x10C)))
        struct.pack_into('<h', f, 0x24, rng.choice((0, 7, 0x40, -3, 0x7FFF)))
        o.write(at, bytes(f))
        put32(o, 0x28A598, at)
        half_at = at + rng.choice((0x30, 0x32))
        put32(o, 0x28A5A8, half_at)
        o.write(0x700031F8, rng.randbytes(0x5C))
    at, half_at = u32(o, 0x28A598), u32(o, 0x28A5A8)
    before = o.read(0x700031F8, 0x5C)
    fbytes = o.read(at, 0x40)
    call_original(o, 0x199C50)
    spad = (U32 * 0x17).from_buffer_copy(before)
    fb = (U8 * 0x40).from_buffer_copy(fbytes)
    cf = CollFile(at, addr_of(fb), 0x40, half_at, struct.unpack('<h', o.read(half_at, 2))[0])
    assert native.em_slg_00199C50(C.byref(cf), spad) == 0
    assert o.read(0x700031F8, 0x5C) == bytes(spad), ('00199C50', seed)
    check_written(o, [(0x700031F8, 0x70003254)], ('00199C50', seed))
    if captured is not None:
        assert o.read(0x700031F8, 0x5C) == captured[0x31F8:0x3254], ('00199C50 against the capture', seed)
    else:
        note_coverage(o, (0x199C50,))
    return o


ENV0, ENV1 = 0x810EA0, 0x810EC8


def display_case(elf, native, seed, o=None, captured=False):
    rng = random.Random(seed)
    o = o or GapEE(elf)
    o.written = set()
    if not captured:
        o.write(ENV0, rng.randbytes(0x50))
        o.save(0x70003B94, rng.getrandbits(16), 2)
        o.save(0x70003B96, rng.choice((0, 1, 0x4000, 0x8000, 0xC001, rng.getrandbits(16))), 2)
    dx = s32(o.load(0x70003B94, 2) | (0xFFFF0000 if o.load(0x70003B94, 2) & 0x8000 else 0))
    dy = s32(o.load(0x70003B96, 2) | (0xFFFF0000 if o.load(0x70003B96, 2) & 0x8000 else 0))
    env = (U8 * 0x50).from_buffer_copy(o.read(ENV0, 0x50))
    # 001002E0 (SDK) runs as original code on both sides: natively through a
    # worker that executes it in a second interpreter over the env bytes.
    runner = GapEE(elf, o.mem, o.spad)
    olog, nlog = [], []
    original = o.load(0x1002E0)

    def hook(ee):
        olog.append((ee.arg(0), tuple(s32(ee.r[i]) for i in (5, 6, 7, 8, 9))))
        h = ee.hooks.pop(0x1002E0)            # the same dict the running loop reads
        ee.nested(0x1002E0, tuple(ee.r[i] & MASK for i in (4, 5, 6, 7, 8, 9)))
        ee.hooks[0x1002E0] = h
    assert original
    o.hooks[0x1002E0] = hook
    call_original(o, 0x1AB4E0, (dx & MASK, dy & MASK))
    ebase = addr_of(env)

    def w_disp(_c, p, psm, width, height, x, y):
        which = C.addressof(p.contents) - ebase
        nlog.append((ENV0 + which, (psm, width, height, x, y)))
        runner.write(ENV0 + which, bytes(env[which:which + 0x28]))
        runner.r[29] = 0x7F0F0000
        runner.call(0x1002E0, (ENV0 + which, psm, width, height, x & MASK, y & MASK))
        C.memmove(ebase + which, runner.read(ENV0 + which, 0x28), 0x28)
        return 0
    w = workers(DisplayWorkers, {'w_001002E0': w_disp})
    assert native.em_slg_001AB4E0(C.byref(w), ebase, dx, dy) == 0
    assert olog == nlog, ('001AB4E0 calls', seed, olog, nlog)
    assert o.read(ENV0, 0x50) == bytes(env), ('display envs', seed)
    check_written(o, [(ENV0, ENV0 + 0x50)], ('001AB4E0', seed))
    if not captured:
        note_coverage(o, (0x1AB4E0,))
    return o


def chcr_case(elf, native, values):
    o = GapEE(elf)
    for v in values:
        o.written = set()
        for i in range(3):
            o.save(0x10008000 + 0x1000 * i, v[i], 1)
        call_original(o, 0x1AB590)
        b = (U8 * 3)(*v)
        native.em_slg_001AB590(b)
        assert tuple(o.load(0x10008000 + 0x1000 * i, 1) for i in range(3)) == tuple(b), ('001AB590', v)
        check_written(o, [(0x10008000 + 0x1000 * i, 0x10008001 + 0x1000 * i) for i in range(3)], '001AB590')
    note_coverage(o, (0x1AB590,))


def fail_stop_checks(native):
    """An unbound worker set faults (-1) before the first write."""
    env = (U8 * 0x50)(*range(0x50))
    assert native.em_slg_001AB4E0(C.byref(DisplayWorkers()), env, 1, 2) == -1 and bytes(env) == bytes(range(0x50))
    for state in range(8):
        user = (U8 * 24)(*([state] + [7] * 23))
        g = FlowGlobals(5, 1, 9, 9)
        assert native.em_slg_001AC070(C.byref(FlowWorkers()), C.byref(g), user) == -1
        assert (bytes(user), g.s3B90, g.d275BD4) == (bytes([state] + [7] * 23), 9, 5)
    node = (U8 * 0x50)(*range(0x50))
    r = I32(77)
    assert native.em_slg_001B0F60(C.byref(NodeStartWorkers()), node, 1, 2, C.byref(r)) == -1
    assert bytes(node) == bytes(range(0x50))
    block = (U8 * 0x3C)(*range(0x3C))
    assert native.em_slg_001B57E0(C.byref(PadWorkers()), block) == -1 and bytes(block) == bytes(range(0x3C))
    entry = (U8 * 0x2C)()
    view = ScriptActor(addr_of(node), addr_of(entry), addr_of(entry))
    assert native.em_slg_001BB0E0(C.byref(ActorWorkers()), C.byref(view)) == -1 and bytes(node) == bytes(range(0x50))
    sf = SoundFrame(d821058=0, d28215B=1, d81011C=0)
    assert native.em_slg_001FB100(C.byref(SoundWorkers()), C.byref(sf)) == -1 and sf.d28215B == 1
    bl = BankLoad()
    out = U32(5)
    assert native.em_slg_001FB370(C.byref(BankWorkers()), C.byref(bl), C.byref(BankFile()), C.byref(out)) == -1
    assert bl.d282150 == 0 and out.value == 5


# ======================================================================
# Captured states
# ======================================================================

def captured(name):
    if name == 'opening':
        return ((REFERENCE / 'opening_ee.bin').read_bytes(), (REFERENCE / 'opening_scratchpad.bin').read_bytes())
    d = ROUTE / name
    return (d / 'eeMemory.bin').read_bytes(), (d / 'scratchpad.bin').read_bytes()


def route_beats():
    return sorted(p.name for p in ROUTE.iterdir() if (p / 'eeMemory.bin').exists()) if ROUTE.exists() else []


def task_captured(elf, native, ram, spad):
    """One 001AB6A0 dispatch over the captured table; every slot function is
    hooked (recorded, no effect) on both sides."""
    o = GapEE(elf, ram, spad)
    olog, nlog = [], []
    native.em_task_init()
    recs = [native.em_task_register(i, None) for i in range(3)]
    fns = [u32(o, TABLE + 0x20 * i + 4) for i in range(3)]
    callbacks, by_ptr = {}, {}
    for fn in set(fns):
        o.hooks[fn] = lambda ee, fn=fn: olog.append((fn, (u32(ee, CURSOR) - TABLE) // 0x20))

        def cb(fn=fn):
            at = C.addressof(native.em_task_current().contents)
            nlog.append((fn, [C.addressof(r.contents) for r in recs].index(at)))
        callbacks[fn] = C.CFUNCTYPE(None)(cb)
        by_ptr[C.cast(callbacks[fn], VP).value] = fn
    for i in range(3):
        rec = recs[i].contents
        rec.state = o.load(TABLE + 0x20 * i, 1)
        rec.fn = C.cast(callbacks[fns[i]], VP)
        C.memmove(rec.user, o.read(TABLE + 0x20 * i + 8, 24), 24)
    states = [o.load(TABLE + 0x20 * i, 1) for i in range(3)]
    call_original(o, 0x1AB6A0)
    native.em_task_dispatch()
    for i in range(3):
        rec = recs[i].contents
        assert (o.load(TABLE + 0x20 * i, 1), u32(o, TABLE + 0x20 * i + 4), o.read(TABLE + 0x20 * i + 8, 24)) == \
            (rec.state, by_ptr[rec.fn], bytes(rec.user)), ('captured task table', i)
    assert olog == nlog, ('captured task calls', olog, nlog)
    check_written(o, [(TABLE, TABLE + 0x60), (CURSOR, CURSOR + 4)], 'captured task')
    return 'task states ' + '/'.join(str(v) for v in states) + ' (' + ', '.join(
        '%06X' % fn for fn, _ in olog) + ')'


def captured_state(elf, native, name):
    """The per-frame originals over one captured state: pad (with the libpad
    state as the stable 6), sound, display, collision tables, task table and
    the opening-script actors resident in the pool."""
    ram, spad = captured(name)
    results = []
    o = GapEE(elf, ram, spad)
    pad_case(elf, native, 7, o=o, block=o.read(PAD, 0x3C), states=6)
    results.append(f'pad phase {ram[PAD + 0x10]}')
    o = GapEE(elf, ram, spad)
    sound_case(elf, native, 7, o=o, captured=True)
    o = GapEE(elf, ram, spad)
    display_case(elf, native, 7, o=o, captured=True)
    o = GapEE(elf, ram, spad)
    coll_case(elf, native, 7, o=o, captured=spad)
    results.append(task_captured(elf, native, ram, spad))
    # 001AF470 ran at New Game with D_00810708 (001AF2C0); its block persists.
    m = (C.c_uint16 * 8).from_buffer_copy(bytes(16))
    native.em_slg_001AF470(m, ram[0x810708])
    assert bytes(m) == spad[0x3B74:0x3B84], ('pad assignment against the capture', name)
    # 008237C0 ran at the AREA11 overlay load; D_00275C1C/24/28/2C persist.
    g = OverlayInit()
    native.em_slg_008237C0(C.byref(g))
    assert struct.unpack_from('<I', ram, 0x275C1C)[0] == g.d275C1C and \
        struct.unpack_from('<III', ram, 0x275C24) == (g.d275C24, g.d275C28, g.d275C2C), ('overlay init', name)
    results.append(f'button config {ram[0x810708]}')
    actors = 0
    for i in range(0x100):
        at = NODE + 0x2F0 * i
        if ram[at] and struct.unpack_from('<I', ram, at + 0x10)[0] == 0x1BB0E0:
            method = struct.unpack_from('<I', ram, at + 0x4C)[0]
            o = GapEE(elf, ram, spad)
            actor_case(elf, native, i, o=o, node_at=at, method=method)
            actors += 1
    results.append(f'{actors} script actors')
    return results


# ======================================================================
# Main
# ======================================================================

def main():
    elf = read_elf()
    native = build_native()
    probe = GapEE(elf)
    check_callees(probe, (0x1AC070,), [a for _, a, _ in FLOW_CALLEES])
    check_callees(probe, (0x1AFCA0, 0x1AF5C0, 0x1AF690, 0x1AF710), (0x1D8BF0, 0x1AF8E0, 0x1D0660))
    check_callees(probe, (0x1B0F60,), (0x1B0EA0, 0x1C63E0))
    check_callees(probe, (0x1B57E0, 0x1B5F40, 0x1B62A0), PAD_CALLEES)
    check_callees(probe, (0x1BB0E0,), [a for _, a, _ in ACTOR_CALLEES if a])
    check_callees(probe, (0x1FB100, 0x1FC6E0), SOUND_CALLEES)
    check_callees(probe, (0x1FB370, 0x1FB3E0, 0x1FB910), [a for _, a, _ in BANK_CALLEES])
    check_callees(probe, (0x1AB4E0,), (0x1002E0,))
    check_callees(probe, (0x1AB6A0, 0x1AB740, 0x1AB790, 0x1AF470, 0x1AB590, 0x199C50), ())

    fail_stop_checks(native)
    counts = {}
    seeds = rm.select(range(600), 160, 0x1AB6A0)
    counts['task'] = (len(seeds), sum(task_case(elf, native, s) for s in seeds))
    seeds = rm.select(range(3000), 500, 0x1AC070)
    o = None
    for s in seeds:
        o = flow_case(elf, native, s, o)
    counts['flow'] = len(seeds)
    configs = rm.select([hi << 8 | lo for lo in range(256) for hi in (0, 1, 0xFFFFFF)], 40, 0x1AF470,
                        keep=lambda i, c: c & 0xFF < 4)
    config_case(elf, native, configs)
    counts['config'] = len(configs)
    seeds = rm.select(range(24), 4, 0x1AFCA0)
    counts['state0'] = sum(state0_case(elf, native, s) for s in seeds)
    seeds = rm.select(range(800), 150, 0x1B0F60)
    o = None
    for s in seeds:
        o = node_start_case(elf, native, s, o)
    counts['node_start'] = len(seeds)
    seeds = rm.select(range(4000), 1400, 0x1B57E0)
    o = None
    for s in seeds:
        o = pad_case(elf, native, s, o)
    counts['pad'] = len(seeds)
    seeds = rm.select(range(3000), 500, 0x1BB0E0)
    o = None
    for s in seeds:
        o = actor_case(elf, native, s, o)
    counts['actor'] = len(seeds)
    seeds = rm.select(range(2000), 300, 0x1FB100)
    o = None
    for s in seeds:
        o = sound_case(elf, native, s, o)
    counts['sound'] = len(seeds)
    seeds = rm.select(range(600), 200, 0x1FB3E0)
    counts['bank'] = (len(seeds), sum(bank_case(elf, native, s, rm.pick(90, 60)) for s in seeds))
    overlay_case(elf, native, OVERLAY_FILE.read_bytes())
    seeds = rm.select(range(400), 60, 0x199C50)
    o = None
    for s in seeds:
        o = coll_case(elf, native, s, o)
    counts['coll'] = len(seeds)
    seeds = rm.select(range(200), 16, 0x1AB4E0)
    o = None
    for s in seeds:
        o = display_case(elf, native, s, o)
    counts['display'] = len(seeds)
    values = rm.select([(a, b, c) for a in range(256) for b in (0, 0x30, 0xFF) for c in (0x10, 0xCF)], 60,
                       0x1AB590, keep=lambda i, v: v[0] in (0, 0x10, 0x20, 0x30, 0xFF))
    chcr_case(elf, native, values)
    counts['chcr'] = len(values)
    branches = assert_coverage(probe, sorted(SIZES))
    # The state-0 chain over the captured opening RAM (S1's rebuild ran just before it).
    ram, spad = captured('opening')
    state0_case(elf, native, 1, ram=ram, spad=spad)
    beats = ['opening'] + rm.select(route_beats(), 1, 0x5EED)
    notes = {b: captured_state(elf, native, b) for b in beats}

    rm.banner(f"task {counts['task'][0]} cases ({counts['task'][1]} task calls)",
              f"001AC070 {counts['flow']}", f"001AF470 {counts['config']}",
              f"001AFCA0 {counts['state0']} (+ the opening RAM)", f"001B0F60 {counts['node_start']}",
              f"001B57E0 {counts['pad']}", f"001BB0E0 {counts['actor']}", f"001FB100 {counts['sound']}",
              f"001FB370 {counts['bank'][0]} sequences ({counts['bank'][1]} completions)",
              f"00199C50 {counts['coll']}", f"001AB4E0 {counts['display']}", f"001AB590 {counts['chcr']}",
              rm.part(len(beats) - 1, len(route_beats()), 'route beats') + ' + the opening capture')
    for b, n in notes.items():
        print(f'  captured {b}: ' + ', '.join(n))
    print(f'Startup/load gaps: PASS ({branches} conditional branches, every outcome taken)')


if __name__ == '__main__':
    main()
