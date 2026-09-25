#!/usr/bin/env python3
"""Execute the original BATTERY page 002149F0 and compare em_status_page_record.c.

docs/STATUS_PAGE_RECORD.md. The user's pinned ELF and the captured original
RAM supply every instruction and every record; none are embedded here.

A. Unit oracle. The ORIGINAL 002149F0 runs with every callee hooked and
   recorded (0020A7A0, 0020AE40, 0020B210, 0020B0D0, 0020BBE0, 0020BC50,
   0020CCB0, the cues 0020CD40 / 60 / 80 / A0, 001FCF10, 00207D00, 001FB9F0,
   00185420). The native em_spr_002149F0 gets the same worker results; the
   test compares the ordered call lists, the whole 4 KB game block
   0x810000..0x811000 (the page record D_00810130, the request block
   D_008106B0.., the counts, charge, capacity, buttons), the message words
   D_002821B0 / B4 / B8 and D_00282240, the scratchpad byte 0x70003B8D and
   the owner records, frame by frame (multi-frame sequences run in lockstep,
   each side on its own state). Every byte the original writes must lie in
   those compared regions, both outcomes of every conditional branch of
   002149F0 must be taken, and every jal target of 002149F0 must be hooked.
   Synthetic records over the panel capture: random pages, requests, counts,
   charges, costs, owner types, buttons and worker results, states 0..8 and
   the out-of-range states.
B. Captured states. The panel confirmation (startup-reference/panel), the
   panel ITEM root (panel/root), the status hub (status-hub) and every route
   beat 00..15 (build/s87/route), each for input scripts over several
   frames. There 00185420 is not scripted: the ORIGINAL 00185420 executes
   over the capture (nested) and both sides get its value.
C. Composition (the binding). The native page runs with its draw workers
   bound to the verified translations em_sul_0020AE40 / em_sul_0020B210 /
   em_sul_0020B0D0 (em_status_ui_leftovers), em_cs_0020CCB0
   (em_census_standins), em_rvr_001FCF10 (em_render_verify_rest), the cues
   em_sul_0020CD40 / 60 / A0 and em_spr_0020CD80; the original 002149F0 runs
   with those routines UNHOOKED (original instructions). Both sides record
   the leaves (00207D00, 00207E40, 00207F80, 00208AD0, 00209280, 001281C0
   (original on both sides), 001FCB90, 001FB9F0, the text blitters): the
   leaf streams, the page and the message words must be equal. This is the
   BATTERY page draw 0020AE40 / 0020B0D0 / 0020B210 / 0020CCB0 bound through
   a record-level 002149F0.
D. Fail-stop: every NULL worker / record, a short page, each worker failing,
   a ring index outside the page, a zero owner; 0020CD80 against the original.

Default run (~10 s) samples A and shortens the sequences; EM_TEST_FULL=1 runs
the full sweep and plays the 240-frame timers out.
"""
import ctypes as C
import os
import random
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode  # noqa: E402
from test_player_fall_reference import FallEE, nested_bits  # noqa: E402
from test_player_slide_reference import read_elf, s32, RETURN  # noqa: E402
import test_status_ui_leftovers_reference as sul  # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
REFERENCE = DECOMP / 'build/startup-reference'
ROUTE = DECOMP / 'build/s87/route'
OUT = ROOT / 'build' / os.environ.get('EM_LANE', 'status_page_record_reference')
M32, M64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF

PAGE_FN, PAGE_FN_SIZE, CD80 = 0x2149F0, 0xE80, 0x20CD80
BG, FRAME, LIST, ARROWS, REFILL, SCROLL = 0x20A7A0, 0x20AE40, 0x20B210, 0x20B0D0, 0x20BBE0, 0x20BC50
MARKER, ACCEPT, BACK, REFUSE, CURSOR = 0x20CCB0, 0x20CD40, 0x20CD60, 0x20CD80, 0x20CDA0
MSG, BLEND, SOUND, FIND = 0x1FCF10, 0x207D00, 0x1FB9F0, 0x185420
RECT, MSG_LEAF = 0x207F80, 0x1FCB90
HOOKED = {BG, FRAME, LIST, ARROWS, REFILL, SCROLL, MARKER, ACCEPT, BACK, REFUSE, CURSOR, MSG,
          BLEND, SOUND, FIND}
PAGE, PAGE_SIZE = 0x810130, 0xA0
BLOCK, BLOCK_SIZE = 0x810000, 0x1000          # the game block the page lives in
WORDS = (0x2821B0, 0x2821B4, 0x2821B8, 0x282240)
SPAD = 0x70003B8D
PANEL = 0x7AA590                               # the AREA11 panel record (D_008106D0)
REC_2C, REC_OTHER = 0x1FF0000, 0x1FF0400       # synthetic owners (unused high RAM)
OWNER_SPAN = 0x40


def in_page_fn(pc):
    return PAGE_FN <= pc < PAGE_FN + PAGE_FN_SIZE


def check_callees(elf):
    ee = FallEE(elf)
    targets = set()
    for pc in range(PAGE_FN, PAGE_FN + PAGE_FN_SIZE, 4):
        word = ee.load(pc)
        if word >> 26 in (2, 3):
            targets.add((word & 0x3FFFFFF) << 2)
    assert targets == HOOKED, ('callee set', sorted(map(hex, targets ^ HOOKED)))
    return len(targets)


def branch_sites(elf):
    ee = FallEE(elf)
    sites = set()
    for pc in range(PAGE_FN, PAGE_FN + PAGE_FN_SIZE, 4):
        word = ee.load(pc)
        op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
        if op in (4, 20) and rs == 0 and rt == 0:
            continue
        if ee.branch(word, pc) is not None:
            sites.add(pc)
    return sites


# ======================================================================
# Original side
# ======================================================================

class Undo:
    """Mixin: every store outside the private stack is undo-logged, and the
    ones 002149F0 makes itself (not a nested original) are collected."""
    undo = None

    def init_undo(self):
        self.undo, self.written, self.nesting = [], set(), 0

    def save(self, address, value, size=4):
        address &= M32
        if self.undo is not None and not 0x7F000000 <= address < 0x7F100000:
            self.undo.append((address, self.read(address, size)))
            if not self.nesting:
                self.written.update(range(address, address + size))
        super().save(address, value, size)

    def poke(self, address, value, size):
        self.save(address, value, size)

    def restore(self):
        for address, old in reversed(self.undo):
            super().write(address, old)
        self.undo = []

    def run_page(self):
        self.r[4] = PAGE
        self.r[29] = 0x7F0F0000
        self.r[31] = RETURN
        self.run(PAGE_FN)

    def original_find(self, item):
        # pure original instructions: no hook applies inside 00185420
        self.nesting += 1
        hooks, self.hooks = self.hooks, {}
        try:
            v0, _ = nested_bits(self, FIND, (item,))
        finally:
            self.hooks = hooks
            self.nesting -= 1
        return v0 & M32


class PageOracle(Undo, FallEE):
    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.init_undo()
        self.calls, self.outcomes = [], set()
        self.script = {}            # 'list' / 'scroll' / 'find': queues of results
        self.given = {'list': [], 'scroll': [], 'find': []}
        self.find_original = False
        self.states, self.state_owners = None, ()
        h = self.hooks
        h[BG] = lambda e: self.rec('background', e.r[4] & M64)
        h[FRAME] = lambda e: self.rec('frame', e.r[4] & M32, e.r[5] & M32, s32(e.r[6]))
        h[LIST] = lambda e: self.result('list', ('list', e.r[4] & M32, e.r[5] & M32, e.r[6] & M64,
                                                 s32(e.r[7])))
        h[ARROWS] = lambda e: self.rec('arrows', e.r[4] & M32, e.r[5] & M32)
        h[REFILL] = lambda e: self.rec('refill', e.r[4] & M32, s32(e.r[5]))
        h[SCROLL] = lambda e: self.result('scroll', ('scroll', e.r[4] & M32, e.r[5] & M32,
                                                     e.r[6] & M64, s32(e.r[7])))
        h[MARKER] = lambda e: self.rec('marker', e.r[4] & M32)
        h[ACCEPT] = lambda e: self.rec('accept')
        h[BACK] = lambda e: self.rec('back')
        h[REFUSE] = lambda e: self.rec('refuse')
        h[CURSOR] = lambda e: self.rec('cursor')
        h[MSG] = lambda e: self.rec('message_line')
        h[BLEND] = lambda e: self.rec('blend', s32(e.r[4]), s32(e.r[5]))
        h[SOUND] = lambda e: self.rec('sound', *(s32(e.r[i]) for i in range(4, 8)))
        h[FIND] = self.find

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and in_page_fn(pc):
            self.outcomes.add((pc, b[0]))
        return b

    def rec(self, *entry):
        self.mark()
        self.calls.append(entry)

    def mark(self):
        """The compared state at each worker call (before it), for the
        write-order sweep."""
        if self.states is not None:
            self.states.append((self.read(BLOCK, BLOCK_SIZE), [s32(self.load(a)) for a in WORDS],
                                self.load(SPAD, 1),
                                {a: bytes(self.read(a, OWNER_SPAN)) for a in self.state_owners}))

    def result(self, kind, entry):
        self.mark()
        self.calls.append(entry)
        value = self.script[kind].pop(0) if self.script.get(kind) else 0
        self.given[kind].append(value)
        self.r[2] = value

    def find(self, e):
        item = s32(e.r[4])
        self.rec('find', item)
        value = (self.original_find(item) if self.find_original
                 else (self.script['find'].pop(0) if self.script.get('find') else 0))
        self.given['find'].append(value)
        e.r[2] = value


# ======================================================================
# Native side
# ======================================================================

VP, I32, U32, U64, U8 = C.c_void_p, C.c_int32, C.c_uint32, C.c_uint64, C.c_uint8
SPR_FN = {
    'background': C.CFUNCTYPE(C.c_int, VP, U64),
    'frame': C.CFUNCTYPE(C.c_int, VP, VP, U32, I32),
    'list': C.CFUNCTYPE(C.c_int, VP, VP, U32, U64, I32, C.POINTER(I32)),
    'arrows': C.CFUNCTYPE(C.c_int, VP, VP, U32),
    'list_refill': C.CFUNCTYPE(C.c_int, VP, VP, I32),
    'list_scroll': C.CFUNCTYPE(C.c_int, VP, VP, U32, U64, I32, C.POINTER(I32)),
    'marker': C.CFUNCTYPE(C.c_int, VP, VP),
    'cue_accept': C.CFUNCTYPE(C.c_int, VP),
    'cue_back': C.CFUNCTYPE(C.c_int, VP),
    'cue_refuse': C.CFUNCTYPE(C.c_int, VP),
    'cue_cursor': C.CFUNCTYPE(C.c_int, VP),
    'message_line': C.CFUNCTYPE(C.c_int, VP),
    'blend': C.CFUNCTYPE(C.c_int, VP, I32, I32),
    'sound': C.CFUNCTYPE(C.c_int, VP, I32, I32, I32, I32),
    'find_device': C.CFUNCTYPE(C.c_int, VP, I32, C.POINTER(U32)),
    'owner_read': C.CFUNCTYPE(C.c_int, VP, U32, U32, U32, C.POINTER(I32)),
    'owner_write': C.CFUNCTYPE(C.c_int, VP, U32, U32, U8),
}
SPR_FIELDS = list(SPR_FN)
WORKER_ADDRESS = {'background': BG, 'frame': FRAME, 'list': LIST, 'arrows': ARROWS,
                  'list_refill': REFILL, 'list_scroll': SCROLL, 'marker': MARKER,
                  'cue_accept': ACCEPT, 'cue_back': BACK, 'cue_refuse': REFUSE,
                  'cue_cursor': CURSOR, 'message_line': MSG, 'blend': BLEND, 'sound': SOUND,
                  'find_device': FIND, 'owner_read': PAGE_FN, 'owner_write': PAGE_FN}


class SprWorkers(C.Structure):
    _fields_ = [('context', VP)] + [(n, SPR_FN[n]) for n in SPR_FIELDS]


RECORD_FIELDS = ['page', 'page_size', 'd8106B0', 'd8106B1', 'd8106C5', 'd8106D0', 'd810C7F',
                 'd810CB2', 'd810CB7', 'd810E74', 'd2821B0', 'd2821B4', 'd2821B8', 'd282240',
                 'spad3B8D']
RECORD_OFFSET = {'page': 0x130, 'd8106B0': 0x6B0, 'd8106B1': 0x6B1, 'd8106C5': 0x6C5,
                 'd8106D0': 0x6D0, 'd810C7F': 0xC7F, 'd810CB2': 0xCB2, 'd810CB7': 0xCB7,
                 'd810E74': 0xE74}


class SprRecords(C.Structure):
    _fields_ = [(n, C.c_size_t if n == 'page_size' else VP) for n in RECORD_FIELDS]


class SprFault(C.Structure):
    _fields_ = [('address', U32), ('code', I32)]


class RectWorkers(C.Structure):
    _fields_ = [('context', VP), ('float_to_int', sul.FN['float_to_int']),
                ('rectangle', C.CFUNCTYPE(C.c_int, VP, I32, I32, I32, I32, I32, U32))]


class MsgWorkers(C.Structure):
    _fields_ = [('ctx', VP), ('w_001FCB90', C.CFUNCTYPE(C.c_int, VP, I32, I32, I32, I32))]


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('spr.dylib' if sys.platform == 'darwin' else 'spr.so')
    sources = ['src/game/em_status_page_record.c', 'src/game/em_status_ui_leftovers.c',
               'src/game/em_census_standins.c', 'src/game/em_effect_original.c',
               'src/game/em_owner_services_original.c', 'src/game/em_render_verify_rest.c',
               'src/game/em_sdk_soft_float.c', 'src/game/em_message_draw_original.c']
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc'] + sources +
                   ['-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    n.em_spr_002149F0.argtypes = [C.POINTER(SprWorkers), C.POINTER(SprRecords), C.POINTER(SprFault)]
    n.em_spr_0020CD80.argtypes = [C.POINTER(SprWorkers)]
    SW, M, B = C.POINTER(sul.Workers), C.POINTER(sul.Memory), C.c_void_p
    sig = {'em_sul_0020AE40': [SW, M, U32, U32, I32], 'em_sul_0020B0D0': [SW, M, U32],
           'em_sul_0020B210': [SW, M, B, C.c_size_t, U32, U64, I32, C.POINTER(sul.ListGlobals),
                               C.POINTER(I32)],
           'em_sul_0020CD40': [SW], 'em_sul_0020CD60': [SW], 'em_sul_0020CDA0': [SW],
           'em_cs_0020CCB0': [C.POINTER(RectWorkers), B, C.c_size_t],
           'em_rvr_001FCF10': [C.POINTER(MsgWorkers), C.c_void_p]}
    for name, args in sig.items():
        getattr(n, name).argtypes = args
    for name in list(sig) + ['em_spr_002149F0', 'em_spr_0020CD80']:
        getattr(n, name).restype = C.c_int32
    return n


class NativePage:
    """The native records (a copy of the oracle's at the start of a case)
    and the unit workers: results the original produced are handed back in
    the same order (list / scroll / find)."""

    def __init__(self, lib, oracle, owners, missing=None, fail_at=None, fail_index=None):
        self.lib, self.oracle = lib, oracle
        self.fail_index = fail_index      # the n-th recorded call (0-based) returns -1
        self.block = (C.c_uint8 * BLOCK_SIZE).from_buffer_copy(oracle.read(BLOCK, BLOCK_SIZE))
        self.words = [C.c_int32(s32(oracle.load(a))) for a in WORDS]
        self.spad = C.c_uint8(oracle.load(SPAD, 1))
        self.owner = {a: bytearray(oracle.read(a, OWNER_SPAN)) for a in owners}
        self.calls = []
        self.fail_at = fail_at            # worker name that returns -1
        self.cursor = {'list': 0, 'scroll': 0, 'find': 0}
        self.page_size = PAGE_SIZE
        impl = self.unit_impl()
        self.cbs = {k: SPR_FN[k](impl[k]) for k in SPR_FIELDS}
        self.w = SprWorkers(None, **{k: (SPR_FN[k]() if k == missing else self.cbs[k])
                                     for k in SPR_FIELDS})

    def records(self, page_size=None):
        base = C.addressof(self.block)
        fields = {k: base + off for k, off in RECORD_OFFSET.items()}
        fields.update(page_size=page_size or self.page_size,
                      d2821B0=C.addressof(self.words[0]), d2821B4=C.addressof(self.words[1]),
                      d2821B8=C.addressof(self.words[2]), d282240=C.addressof(self.words[3]),
                      spad3B8D=C.addressof(self.spad))
        return SprRecords(**fields)

    def page_address(self, pointer):
        assert pointer == C.addressof(self.block) + 0x130, 'worker got another page'
        return PAGE

    def rec(self, name, *entry):
        self.calls.append(entry)
        if self.fail_index is not None and len(self.calls) - 1 == self.fail_index:
            return -1
        return -1 if name == self.fail_at else 0

    def given(self, kind):
        values = self.oracle.given[kind]
        i = self.cursor[kind]
        self.cursor[kind] += 1
        return values[i] if i < len(values) else 0

    def unit_impl(self):
        def list_(c, page, table, glyph, flags, out):
            out[0] = s32(self.given('list'))
            return self.rec('list', 'list', self.page_address(page), table, glyph, flags)

        def scroll(c, page, table, glyph, flags, out):
            out[0] = s32(self.given('scroll'))
            return self.rec('list_scroll', 'scroll', self.page_address(page), table, glyph, flags)

        def find(c, item, out):
            out[0] = self.given('find')
            return self.rec('find_device', 'find', item)

        return {
            'background': lambda c, tex: self.rec('background', 'background', tex),
            'frame': lambda c, p, t, f: self.rec('frame', 'frame', self.page_address(p), t, f),
            'list': list_,
            'arrows': lambda c, p, t: self.rec('arrows', 'arrows', self.page_address(p), t),
            'list_refill': lambda c, p, n: self.rec('list_refill', 'refill', self.page_address(p), n),
            'list_scroll': scroll,
            'marker': lambda c, p: self.rec('marker', 'marker', self.page_address(p)),
            'cue_accept': lambda c: self.rec('cue_accept', 'accept'),
            'cue_back': lambda c: self.rec('cue_back', 'back'),
            'cue_refuse': lambda c: self.rec('cue_refuse', 'refuse'),
            'cue_cursor': lambda c: self.rec('cue_cursor', 'cursor'),
            'message_line': lambda c: self.rec('message_line', 'message_line'),
            'blend': lambda c, s, m: self.rec('blend', 'blend', s, m),
            'sound': lambda c, *a: self.rec('sound', 'sound', *a),
            'find_device': find,
            'owner_read': self.owner_read,
            'owner_write': self.owner_write,
        }

    def owner_read(self, c, owner, offset, size, out):
        if self.fail_at == 'owner_read':
            return -1
        data = self.owner[owner]
        if size == 1:
            out[0] = data[offset]
        else:
            out[0] = s32(int.from_bytes(data[offset:offset + 2], 'little', signed=True))
        return 0

    def owner_write(self, c, owner, offset, value):
        if self.fail_at == 'owner_write':
            return -1
        self.owner[owner][offset] = value
        return 0

    def run(self, fault=None):
        fault = fault or SprFault()
        r = self.records()
        return self.lib.em_spr_002149F0(C.byref(self.w), C.byref(r), C.byref(fault)), fault


def snapshot(o, n):
    """(original, native) views of every compared region."""
    want = (o.read(BLOCK, BLOCK_SIZE), [s32(o.load(a)) for a in WORDS], o.load(SPAD, 1),
            {a: bytes(o.read(a, OWNER_SPAN)) for a in n.owner})
    got = (bytes(n.block), [w.value for w in n.words], n.spad.value,
           {a: bytes(v) for a, v in n.owner.items()})
    return want, got


def compared_bytes(owners):
    allowed = set(range(BLOCK, BLOCK + BLOCK_SIZE)) | {SPAD}
    for a in WORDS:
        allowed.update(range(a, a + 4))
    for a in owners:
        allowed.update(range(a, a + OWNER_SPAN))
    return allowed


def describe(want, got):
    out = []
    if want[0] != got[0]:
        out.append(['%06X: %02X/%02X' % (BLOCK + i, a, b)
                    for i, (a, b) in enumerate(zip(want[0], got[0])) if a != b][:12])
    for k, (a, b) in enumerate(zip(want[1:], got[1:])):
        if a != b:
            out.append((k, a, b))
    return out


def lockstep(lib, o, frames, owners, label, find_original=False):
    """Run the frames [(pressed, list results, scroll results, find results)]
    on both sides from the oracle's current memory. Returns the frame count."""
    n = NativePage(lib, o, owners)
    o.find_original = find_original
    allowed = compared_bytes(owners)
    for index, (pressed, lists, scrolls, finds) in enumerate(frames):
        o.poke(0x810E74, pressed, 2)
        n.block[0xE74], n.block[0xE75] = pressed & 255, pressed >> 8
        o.calls, o.written = [], set()
        o.script = {'list': list(lists), 'scroll': list(scrolls), 'find': list(finds)}
        o.given = {'list': [], 'scroll': [], 'find': []}
        n.calls, n.cursor = [], {'list': 0, 'scroll': 0, 'find': 0}
        o.run_page()
        result, fault = n.run()
        where = (label, index, hex(pressed))
        assert result == 0 and fault.code == 0, (where, result, fault.code, hex(fault.address))
        assert n.calls == o.calls, (where, n.calls, o.calls)
        want, got = snapshot(o, n)
        assert want == got, (where, describe(want, got))
        stray = sorted(a for a in o.written if a not in allowed)
        assert not stray, (where, 'original wrote uncompared bytes', [hex(a) for a in stray[:8]])
    return len(frames)


# ======================================================================
# A. synthetic records over the panel capture
# ======================================================================

BUTTONS = (0x20, 0x40, 0x10, 0x800, 0x1000, 0x4000, 0x8000, 0x2000, 0x8, 0x4)


def synthetic_case(rng):
    pick = rng.choice
    state = pick((0, 0, 1, 1, 2, 3, 4, 4, 5, 6, 6, 7, 7, 8, 9, 0xFF))
    page = bytearray(rng.getrandbits(8) for _ in range(PAGE_SIZE))
    page[5] = state
    page[6] = pick((0, 1, 1, 2, 0x78, 0xF0, 0xFF, rng.getrandbits(8)))
    page[0x12] = pick((12, 8, 36, 48, 0, 255, 4, rng.getrandbits(8)))
    page[0x13] = pick((6, 6, 8, 8, 0xA, 0xC, 0xE, 0x10, rng.getrandbits(8)))
    page[0x17] = pick((0, 0, 1, 2, 3, 4))
    page[0x18] = pick((0, 1, 1, 2, 5))
    page[0x1A] = pick((0, 1, 2))
    for i in range(5):
        page[0x50 + i] = pick((0, 1, 2))
    page[0x30:0x34] = pick((PANEL, REC_2C, REC_OTHER)).to_bytes(4, 'little')
    page[0x3C:0x3E] = pick((1, 1, 2, 3, 0, 0xFFFF, 30, 0x8000, 0x14, rng.getrandbits(16))).to_bytes(2, 'little')
    b1 = pick((0x1B, 0x1C, 0x1D, 0x1E, 0x1A, 0x82, 0x84, 0x86, 0x90, 0x98, 0x80, 0x40, 0x42, 0xC2,
               0x00, 0xFF))
    costs = {a: pick((2, 2, 4, 6, 16, 24, 3, 0, -1, -32768, 100, rng.randrange(-40, 40)))
             for a in (PANEL, REC_2C, REC_OTHER)}
    frames = []
    for _ in range(pick((1, 1, 2, 3))):
        pressed = 0
        for bit in BUTTONS:
            if rng.random() < 0.18:
                pressed |= bit
        if rng.random() < 0.05:
            pressed = 0xFFFF
        frames.append((pressed, [pick((0, 0, 0, 1, 2))], [pick((0, 1))],
                       [pick((0, PANEL, REC_2C, REC_OTHER))]))
    return dict(
        page=page, b0=pick((0, 0, 1, 6, 6, 3, 0x80)), b1=b1,
        c5=pick((0, 0xFF, 7)), d0=pick((PANEL, REC_2C, REC_OTHER)),
        counts=[pick((0, 0, 1, 3)) for _ in range(3)],
        charge=pick((0, 2, 4, 8, 10, 11, 12, 13, 24, 36, 48, 0x108, 0xFFFE, 0x8000, 0x7FFF,
                     rng.getrandbits(16))),
        capacity=pick((12, 36, 48, 8, 0, 255, 12)),
        words=[rng.randrange(-3, 40) for _ in WORDS], spad=rng.getrandbits(8),
        costs=costs, types={PANEL: None, REC_2C: 0x2C, REC_OTHER: pick((0x18, 0x2B, 0x2D, 0))},
        frames=frames)


def run_synthetic(lib, o, case, label):
    base_page = o.read(PAGE, PAGE_SIZE)
    o.undo = []
    for i in range(0, PAGE_SIZE, 4):
        o.poke(PAGE + i, int.from_bytes(case['page'][i:i + 4], 'little'), 4)
    apply_case_rest(o, case)
    try:
        return lockstep(lib, o, case['frames'], (PANEL, REC_2C, REC_OTHER), label)
    finally:
        o.restore()
        assert o.read(PAGE, PAGE_SIZE) == base_page


def apply_case_rest(o, case):
    o.poke(0x8106B0, case['b0'], 1)
    o.poke(0x8106B1, case['b1'], 1)
    o.poke(0x8106C5, case['c5'], 1)
    o.poke(0x8106D0, case['d0'], 4)
    for i, count in enumerate(case['counts']):
        o.poke(0x810C7F + i, count, 1)
    o.poke(0x810CB2, case['charge'], 2)
    o.poke(0x810CB7, case['capacity'], 1)
    for a, v in zip(WORDS, case['words']):
        o.poke(a, v, 4)
    o.poke(SPAD, case['spad'], 1)
    for a, cost in case['costs'].items():
        o.poke(a + 0x34, cost, 2)
        if case['types'][a] is not None:
            o.poke(a + 3, case['types'][a], 1)
        o.poke(a + 0xA, 0x11, 1)
        o.poke(a + 0xB, 0x22, 1)


# Directed sequences: every path of the machine, played over many frames.
def directed_sequences(frames_long):
    idle = lambda n: [(0, [0], [0], [0])] * n  # noqa: E731
    press = lambda b, find=0, lst=0: [(b, [lst], [0], [find])]  # noqa: E731
    seqs = []
    # panel request 0x82 (cost 2): confirm, Yes, discharge to the end
    seqs.append(('confirm-yes-discharge', dict(state=0, b0=1, b1=0x82, charge=12, cost=2),
                 idle(1) + press(0x8000) + press(0x40) + idle(frames_long)))
    # the fast finish, the No / cancel paths, the insufficient charge
    seqs.append(('confirm-yes-fast', dict(state=0, b0=1, b1=0x82, charge=12, cost=2),
                 press(0x8000) + press(0x40) + idle(3) + press(0x870)))
    seqs.append(('confirm-no', dict(state=0, b0=1, b1=0x82, charge=12, cost=2),
                 press(0x2000) + press(0x40) + idle(2) + press(0x20)))
    seqs.append(('insufficient', dict(state=0, b0=1, b1=0x88, charge=6, cost=8),
                 idle(1) + press(0x8000) + press(0x40) + idle(frames_long // 2) + press(0x40)))
    # request 6 (the panel's direct request): both outcomes, then the exits
    seqs.append(('request6-ok', dict(state=0, b0=6, b1=0x82, charge=12, cost=2),
                 idle(1) + press(0x8000) + press(0x40)))
    seqs.append(('request6-short', dict(state=0, b0=6, b1=0x82, charge=2, cost=2),
                 idle(frames_long // 2) + press(0x800)))
    seqs.append(('request6-cancel', dict(state=0, b0=6, b1=0x82, charge=12, cost=2),
                 idle(1) + press(0x830)))
    # recharge (0x40 kind) to the capacity, and the full-charge result
    seqs.append(('recharge', dict(state=0, b0=1, b1=0x42, charge=4, capacity=12, cost=2),
                 press(0x8000) + press(0x40) + idle(frames_long)))
    seqs.append(('recharge-full', dict(state=0, b0=1, b1=0x42, charge=12, capacity=12, cost=2),
                 idle(frames_long // 2)))
    # acquisition notices of each kind, their exits, then the list
    for kind in range(3):
        seqs.append(('take-%d' % kind, dict(state=0, b0=1, b1=0x1B + kind, charge=3, counts=[1, 1, 1]),
                     idle(frames_long) + press(0x20)))
    seqs.append(('take-exit', dict(state=0, b0=1, b1=0x1C, counts=[1, 0, 0]), idle(2) + press(0x1000)))
    # the list: no device (0020CD80, state 8), a device (the panel, a 0x2C
    # recharger), the wrap scroll (states 1 -> 2 -> 1)
    seqs.append(('no-device', dict(state=1, counts=[1, 0, 0], rows=1),
                 press(0x40, find=0) + idle(frames_long) + press(0x40, find=0) + press(0x60)))
    seqs.append(('device', dict(state=1, counts=[1, 0, 0], rows=1),
                 press(0x40, find=PANEL) + idle(2) + press(0x20)))
    # every cost line (2 / 4 / 6 / 16 / 24 / other) from the request and the list
    for cost in (2, 3, 4, 6, 16, 24):
        seqs.append(('request-cost-%d' % cost, dict(state=0, b0=1, b1=0x80 + cost, charge=48,
                                                     capacity=48, cost=cost), idle(1)))
        seqs.append(('device-cost-%d' % cost, dict(state=1, counts=[1, 0, 0], rows=1, charge=48,
                                                    capacity=48, cost=cost),
                     press(0x40, find=PANEL) + press(0x8000) + press(0x40) + idle(2)))
    seqs.append(('recharger', dict(state=1, counts=[0, 0, 1], rows=1, charge=4),
                 press(0x40, find=REC_2C) + press(0x8000) + press(0x40) + idle(frames_long)))
    seqs.append(('recharger-full', dict(state=1, counts=[0, 1, 0], rows=1, charge=12, capacity=12),
                 press(0x40, find=REC_2C) + idle(frames_long // 4) + press(0x60)))
    seqs.append(('scroll', dict(state=1, counts=[1, 0, 0], rows=1),
                 [(0, [1], [0], [0]), (0, [0], [0], [0]), (0, [0], [1], [0]), (0, [2], [0], [0])]))
    return seqs


def run_directed(lib, o, name, setup, frames):
    o.undo = []
    for i in range(0, PAGE_SIZE, 4):
        o.poke(PAGE + i, 0, 4)
    o.poke(PAGE + 5, setup['state'], 1)
    o.poke(PAGE + 0x18, setup.get('rows', 0), 1)
    o.poke(PAGE + 0x30, PANEL, 4)
    o.poke(0x8106B0, setup.get('b0', 0), 1)
    o.poke(0x8106B1, setup.get('b1', 0), 1)
    o.poke(0x8106C5, 0, 1)
    o.poke(0x8106D0, PANEL, 4)
    for i, count in enumerate(setup.get('counts', [1, 0, 0])):
        o.poke(0x810C7F + i, count, 1)
    o.poke(0x810CB2, setup.get('charge', 12), 2)
    o.poke(0x810CB7, setup.get('capacity', 12), 1)
    o.poke(PANEL + 0x34, setup.get('cost', 2), 2)
    o.poke(REC_2C + 3, 0x2C, 1)
    o.poke(REC_2C + 0x34, 2, 2)
    o.poke(REC_OTHER + 3, 0x18, 1)
    try:
        return lockstep(lib, o, frames, (PANEL, REC_2C, REC_OTHER), name)
    finally:
        o.restore()


# ======================================================================
# B. captured states
# ======================================================================

def capture_list():
    out = [('panel', REFERENCE / 'panel'), ('panel-root', REFERENCE / 'panel/root'),
           ('status-hub', REFERENCE / 'status-hub')]
    out += [(beat.name, beat) for beat in sorted(ROUTE.glob('[01][0-9]_*'))]
    return out


def load_capture(elf, path):
    ram = (path / 'eeMemory.bin').read_bytes()
    spad = (path / 'scratchpad.bin').read_bytes() if (path / 'scratchpad.bin').exists() else None
    return ram, spad


def capture_scripts(state, frames_long):
    idle = lambda n: [(0, [0], [0], [])] * n  # noqa: E731
    one = lambda b: [(b, [0], [0], [])]  # noqa: E731
    scripts = [('idle', idle(3)), ('cancel', one(0x20)), ('accept', one(0x40) + idle(frames_long)),
               ('up-accept', one(0x8000) + one(0x40) + idle(frames_long)),
               ('down', one(0x2000) + idle(1)), ('finish', one(0x870))]
    if state == 6:
        scripts.append(('discharge-end', idle(31)))
    return scripts


def run_capture_unit(lib, elf, label, path, frames_long):
    ram, spad = load_capture(elf, path)
    o = PageOracle(elf, ram, spad)
    owner = o.load(0x8106D0)
    owners = tuple(sorted({a for a in (owner, o.load(PAGE + 0x30), PANEL) if a}))
    state = o.load(PAGE + 5, 1)
    frames = 0
    for name, script in capture_scripts(state, frames_long):
        o.undo = []
        try:
            frames += lockstep(lib, o, script, owners, (label, name), find_original=True)
        finally:
            o.restore()
    if label == '01_battery':
        # The take's request replayed over the beat's records: B0 = 1,
        # B1 = 0x1B (what 001C47A0 posted), the page at state 0: the notice.
        o.undo = []
        o.poke(0x8106B0, 1, 1)
        o.poke(PAGE + 5, 0, 1)
        try:
            frames += lockstep(lib, o, [(0, [0], [0], [])] * frames_long + [(0x20, [0], [0], [])],
                               owners, (label, 'take-request'), find_original=True)
        finally:
            o.restore()
    return label, state, frames, o.outcomes


# ======================================================================
# C. composition: the page draws bound to their translations
# ======================================================================

class ComposeOracle(Undo, sul.Oracle):
    def __init__(self, elf, values, ram, spad):
        super().__init__(elf, values, ram, spad)
        self.init_undo()
        self.given = {'find': []}
        h = self.hooks
        h[BG] = lambda e: self.rec('background', e.r[4] & M64)
        h[RECT] = lambda e: self.rec('rect', *(s32(e.r[i]) for i in range(4, 9)), e.r[9] & M32)
        h[MSG_LEAF] = lambda e: self.rec('message', *(s32(e.r[i]) for i in range(4, 8)))
        h[REFILL] = lambda e: self.rec('refill', e.r[4] & M32, s32(e.r[5]))
        h[SCROLL] = lambda e: (self.rec('scroll', e.r[4] & M32), e.r.__setitem__(2, 0))
        h[FIND] = self.find

    def find(self, e):
        item = s32(e.r[4])
        self.rec('find', item)
        value = self.original_find(item)
        self.given['find'].append(value)
        e.r[2] = value


class ComposeNative(NativePage):
    """NativePage whose draw workers are the verified translations."""

    def __init__(self, lib, oracle, owners, values):
        self.values = values
        self.leaf = sul.Native(values)
        super().__init__(lib, oracle, owners)
        self.calls = self.leaf.calls = []
        data = oracle.read(*sul.DATA_SPAN)
        self.data = C.create_string_buffer(data, len(data))
        self.regions = (sul.Region * 2)(
            sul.Region(sul.DATA_SPAN[0], len(data), C.addressof(self.data)),
            sul.Region(BLOCK, BLOCK_SIZE, C.addressof(self.block)))
        self.mem = sul.Memory(self.regions, 2)
        self.rvr_fault = C.create_string_buffer(8)   # EmRvrFault {address, code}
        self.rect_cbs = (sul.FN['float_to_int'](self.leaf.f2i),
                         RectWorkers._fields_[2][1](lambda c, *a: self.leaf.rec('rect', *a)))
        self.rect_w = RectWorkers(None, *self.rect_cbs)
        self.msg_cb = MsgWorkers._fields_[1][1](lambda c, *a: self.leaf.rec('message', *a))
        self.msg_w = MsgWorkers(None, self.msg_cb)
        impl = self.compose_impl()
        for k, f in impl.items():
            self.cbs[k] = SPR_FN[k](f)
            setattr(self.w, k, self.cbs[k])

    def set_calls(self):
        self.calls = self.leaf.calls = []

    def compose_impl(self):
        lib, leaf = self.lib, self.leaf
        sw = C.byref(leaf.w)

        def page_ptr(p):
            self.page_address(p)
            return p

        def frame(c, p, table, flags):
            return lib.em_sul_0020AE40(sw, C.byref(self.mem), self.page_address(p), table, flags)

        def list_(c, p, table, glyph, flags, out):
            g = sul.ListGlobals(self.words[1].value, self.words[2].value, self.words[3].value)
            res = C.c_int32(0)
            r = lib.em_sul_0020B210(sw, C.byref(self.mem), page_ptr(p), PAGE_SIZE, table, glyph,
                                    flags, C.byref(g), C.byref(res))
            self.words[1].value, self.words[2].value, self.words[3].value = g.d2821B4, g.d2821B8, g.d282240
            out[0] = res.value
            return r

        def find(c, item, out):
            leaf.rec('find', item)
            out[0] = self.given('find')
            return 0

        return {
            'background': lambda c, tex: leaf.rec('background', tex),
            'frame': frame,
            'list': list_,
            'arrows': lambda c, p, table: lib.em_sul_0020B0D0(sw, C.byref(self.mem), table),
            'list_refill': lambda c, p, n: leaf.rec('refill', self.page_address(p), n),
            'list_scroll': lambda c, p, t, g, f, out: (out.__setitem__(0, 0),
                                                       leaf.rec('scroll', self.page_address(p)))[1],
            'marker': lambda c, p: lib.em_cs_0020CCB0(C.byref(self.rect_w), page_ptr(p), PAGE_SIZE),
            'cue_accept': lambda c: lib.em_sul_0020CD40(sw),
            'cue_back': lambda c: lib.em_sul_0020CD60(sw),
            'cue_refuse': lambda c: lib.em_spr_0020CD80(C.byref(self.w)),
            'cue_cursor': lambda c: lib.em_sul_0020CDA0(sw),
            'message_line': lambda c: lib.em_rvr_001FCF10(C.byref(self.msg_w), self.rvr_fault),
            'blend': lambda c, s, m: leaf.rec('blend', s, m),
            'sound': lambda c, *a: leaf.rec('sound', *a),
            'find_device': find,
        }


def compose(lib, elf, label, path, setup, frames):
    ram, spad = load_capture(elf, path)
    values = sul.Values(elf, ram, spad)
    o = ComposeOracle(elf, values, ram, spad)
    for address, value, size in setup:
        o.poke(address, value, size)
    o.undo = []
    owners = tuple(sorted({a for a in (o.load(0x8106D0), o.load(PAGE + 0x30), PANEL) if a}))
    n = ComposeNative(lib, o, owners, values)
    allowed = compared_bytes(owners)
    leaves = 0
    for index, pressed in enumerate(frames):
        o.poke(0x810E74, pressed, 2)
        n.block[0xE74], n.block[0xE75] = pressed & 255, pressed >> 8
        o.calls, o.written, o.given = [], set(), {'find': []}
        n.set_calls()
        n.cursor = {'list': 0, 'scroll': 0, 'find': 0}
        o.run_page()
        result, fault = n.run()
        where = (label, index, hex(pressed))
        assert result == 0 and fault.code == 0, (where, result, fault.code, hex(fault.address))
        assert n.calls == o.calls, (where, 'leaf streams differ',
                                    next(((i, a, b) for i, (a, b) in enumerate(zip(n.calls, o.calls))
                                          if a != b), (len(n.calls), len(o.calls))))
        want, got = snapshot(o, n)
        assert want == got, (where, describe(want, got))
        stray = sorted(a for a in o.written if a not in allowed)
        assert not stray, (where, [hex(a) for a in stray[:8]])
        leaves += len(o.calls)
    return len(frames), leaves


def composition_runs(lib, elf, frames_long):
    panel, root, hub = REFERENCE / 'panel', REFERENCE / 'panel/root', REFERENCE / 'status-hub'
    beat = lambda name: ROUTE / name  # noqa: E731
    return [
        # the captured confirmation: cursor, Yes, the discharge to the end
        ('panel yes+discharge', panel, [], [0, 0x8000, 0x2000, 0x8000, 0x40] + [0] * frames_long),
        # No -> the list, then the list's accept: the panel (original 00185420)
        ('panel no+list', panel, [], [0x40, 0, 0x40, 0, 0x20]),
        ('status-hub list', hub, [], [0, 0x40, 0x8000, 0x40, 0]),
        ('panel-root list', root, [], [0, 0x20]),
        # route 01's list: 00185420 finds nothing -> 0020CD80, state 8
        ('01 list', beat('01_battery'), [], [0, 0x40] + [0] * (frames_long // 4) + [0x60, 0x20]),
        # route 01's take request replayed: the acquisition notice
        ('01 take notice', beat('01_battery'), [(0x8106B0, 1, 1), (PAGE + 5, 0, 1)],
         [0] * frames_long + [0x4000, 0x20]),
        # route 03's discharge state to its completion
        ('03 discharge end', beat('03_panel_power'), [], [0] * 31),
    ]


def order_sweep(lib, o, case, label):
    """Fail the k-th worker call of the first frame, for every k: the native
    page stops there (-1) with exactly the original's state at that call, so
    every write is ordered correctly against every worker call."""
    checks = 0
    owners = (PANEL, REC_2C, REC_OTHER)
    pressed, lists, scrolls, finds = case['frames'][0]
    o.undo = []
    for i in range(0, PAGE_SIZE, 4):
        o.poke(PAGE + i, int.from_bytes(case['page'][i:i + 4], 'little'), 4)
    apply_case_rest(o, case)
    o.poke(0x810E74, pressed, 2)
    try:
        o.calls, o.states, o.state_owners = [], [], owners
        o.script = {'list': list(lists), 'scroll': list(scrolls), 'find': list(finds)}
        o.given = {'list': [], 'scroll': [], 'find': []}
        mark = len(o.undo)
        o.run_page()
        states, calls = o.states, list(o.calls)
        o.states = None
        for address, old in reversed(o.undo[mark:]):
            FallEE.write(o, address, old)
        del o.undo[mark:]
        for k in range(len(calls)):
            n = NativePage(lib, o, owners, fail_index=k)
            result, fault = n.run()
            assert result == -1 and fault.code == 2, (label, k, result, fault.code)
            assert n.calls == calls[:k + 1], (label, k, n.calls, calls)
            got = (bytes(n.block), [w.value for w in n.words], n.spad.value,
                   {a: bytes(v) for a, v in n.owner.items()})
            assert got == states[k], (label, k, calls[k], describe(states[k], got))
            checks += 1
    finally:
        o.states = None
        o.restore()
    return checks


def single_row_list(elf):
    """002149F0's state 0 lists at most one row (the highest owned kind), so
    the original 0020B210 over the BATTERY tables never raises the wrap event
    that leads to 0020BBE0 / state 2 (0020BC50): executed here for every
    cursor, head, row count 0..1 and button word 0020B210 reads."""
    ram, spad = load_capture(elf, REFERENCE / 'panel')
    values = sul.Values(elf, ram, spad)
    o = ComposeOracle(elf, values, ram, spad)
    o.hooks[RECT] = o.hooks[MSG_LEAF] = lambda e: None
    runs = 0
    for rows in (0, 1):
        for cursor in (0, 1, 3):
            for head in (0, 1):
                for repeat in (0, 0x1000, 0x4000, 0x5000, 0xFFFF):
                    for flags in (2, 0x402, 0x602):
                        o.undo = []
                        o.poke(PAGE + 0x18, rows, 1)
                        o.poke(PAGE + 0x17, cursor if rows else 0, 1)
                        o.poke(PAGE + 0x19, head, 1)
                        o.poke(0x810E78, repeat, 2)
                        value = o.invoke(LIST, {4: PAGE, 5: 0x265CD0, 6: 0x20042D05A1322000, 7: flags})
                        assert s32(value) == 0, (rows, cursor, head, hex(repeat), flags, value)
                        o.restore()
                        runs += 1
    return runs


# ======================================================================
# D. fail-stop
# ======================================================================

def fail_stop(lib, o):
    checks = 0
    o.undo = []
    for i in range(0, PAGE_SIZE, 4):
        o.poke(PAGE + i, 0, 4)
    o.poke(PAGE + 5, 4, 1)
    o.poke(PAGE + 0x30, PANEL, 4)
    o.poke(0x810E74, 0x40, 2)
    try:
        for name in SPR_FIELDS + [None]:
            for record in (RECORD_FIELDS if name is None else [None]):
                if record == 'page_size':
                    continue
                n = NativePage(lib, o, (PANEL,), missing=name)
                before = snapshot(o, n)[1]
                fault = SprFault()
                r = n.records()
                if record:
                    setattr(r, record, None)
                result = lib.em_spr_002149F0(C.byref(n.w), C.byref(r), C.byref(fault))
                assert result == -1 and fault.code == 1 and not n.calls, (name, record)
                assert snapshot(o, n)[1] == before
                checks += 1
        n = NativePage(lib, o, (PANEL,))
        before = snapshot(o, n)[1]
        fault = SprFault()
        r = n.records(page_size=PAGE_SIZE - 1)
        assert lib.em_spr_002149F0(C.byref(n.w), C.byref(r), C.byref(fault)) == -1
        assert fault.code == 3 and not n.calls and snapshot(o, n)[1] == before
        checks += 1
        # each worker failing at its first call: -1, its address named
        paths = {  # (state, pressed, extra pokes) reaching the worker
            'background': (4, 0, []), 'frame': (4, 0, []), 'list': (4, 0, []),
            'arrows': (4, 0, []), 'message_line': (4, 0, []), 'blend': (4, 0, []),
            'marker': (4, 0, []), 'cue_cursor': (4, 0x8000, [(PAGE + 6, 1, 1)]),
            'cue_back': (4, 0x20, []), 'cue_accept': (4, 0x40, [(PAGE + 6, 0, 1), (PAGE + 0x13, 6, 1)]),
            'owner_read': (4, 0x40, [(PAGE + 6, 0, 1), (PAGE + 0x13, 8, 1)]),
            'sound': (6, 0, [(PAGE + 0x3C, 1, 2), (PAGE + 0x12, 12, 1), (0x810CB2, 12, 2)]),
            'owner_write': (6, 0x870, [(PAGE + 0x3C, 5, 2)]),
            'list_scroll': (2, 0, []),
            'list_refill': (1, 0, []), 'find_device': (1, 0x40, [(PAGE + 0x18, 1, 1)]),
            'cue_refuse': (1, 0x40, [(PAGE + 0x18, 1, 1)]),
        }
        for name in SPR_FIELDS:
            state, pressed, pokes = paths[name]
            mark = len(o.undo)
            o.poke(PAGE + 5, state, 1)
            o.poke(0x810E74, pressed, 2)
            for a, v, s in pokes:
                o.poke(a, v, s)
            n = NativePage(lib, o, (PANEL,), fail_at=name)
            if name == 'list_refill':
                o.given['list'] = [1]
            else:
                o.given['list'] = [0]
            o.given['find'] = [0 if name == 'cue_refuse' else PANEL]
            o.given['scroll'] = [0]
            result, fault = n.run()
            assert result == -1 and fault.code == 2 and fault.address == WORKER_ADDRESS[name], (
                name, result, fault.code, hex(fault.address))
            checks += 1
            for a, old in reversed(o.undo[mark:]):
                FallEE.write(o, a, old)
            del o.undo[mark:]
        # a ring index outside the page (state 1, cursor 0x60): after the draws
        o.poke(PAGE + 5, 1, 1)
        o.poke(PAGE + 0x17, 0x60, 1)
        o.poke(PAGE + 0x18, 1, 1)
        o.poke(0x810E74, 0x40, 2)
        o.given = {'list': [0], 'scroll': [], 'find': []}
        n = NativePage(lib, o, (PANEL,))
        result, fault = n.run()
        assert result == -1 and fault.code == 3 and fault.address == PAGE_FN
        assert [c[0] for c in n.calls] == ['background', 'frame', 'list'], n.calls
        checks += 1
        # a zero owner the original would dereference (request 6, D0 = 0)
        o.poke(PAGE + 5, 0, 1)
        o.poke(0x8106B0, 6, 1)
        o.poke(0x8106D0, 0, 4)
        o.poke(0x810E74, 0, 2)
        n = NativePage(lib, o, (PANEL,))
        result, fault = n.run()
        assert result == -1 and fault.code == 3 and not n.calls
        assert n.block[0x130 + 0x13] == 8 and bytes(n.block[0x160:0x164]) == bytes(4)
        checks += 1
    finally:
        o.restore()
    # 0020CD80 against the original
    o.calls = []
    hook = o.hooks.pop(CD80)
    o.r[31] = RETURN
    o.run(CD80)
    o.hooks[CD80] = hook
    n = NativePage(lib, o, ())
    assert lib.em_spr_0020CD80(C.byref(n.w)) == 0 and n.calls == o.calls == [('sound', 2, 0x1000, 0x1000, 0x1000)]
    n = NativePage(lib, o, (), missing='sound')
    assert lib.em_spr_0020CD80(C.byref(n.w)) == -1 and not n.calls
    return checks + 2


# ======================================================================

def main():
    elf = read_elf()
    callees = check_callees(elf)
    sites = branch_sites(elf)
    lib = build_native()
    long_frames = reference_mode.pick(250, 40)
    counts = {}

    ram, spad = load_capture(elf, REFERENCE / 'panel')
    o = PageOracle(elf, ram, spad)
    assert o.load(PANEL + 3, 1) != 0x2C
    total = 30000
    chosen = reference_mode.select(range(total), 1200, 0x2149F0, keep=lambda i, c: i < 40)
    selected = [synthetic_case(random.Random(i)) for i in chosen]
    counts['synthetic frames'] = sum(run_synthetic(lib, o, c, ('synthetic', i))
                                     for i, c in enumerate(selected))
    order = list(selected[:reference_mode.pick(len(selected), 200)])
    for k, (state, pressed, find, lst) in enumerate(
            (st, pr, fd, ls) for st in range(9)
            for pr in (0, 0x20, 0x40, 0x8000, 0x2000, 0x870, 0x5060, 0x60)
            for fd in (0, PANEL, REC_2C) for ls in (0, 1)):
        case = synthetic_case(random.Random(0x0DE0 + k))
        case['page'][5] = state
        case['words'] = [7, 9, 11, 13]
        case['frames'] = [(pressed, [lst], [lst], [find])]
        order.append(case)
    counts['order checks'] = sum(order_sweep(lib, o, c, ('order', i)) for i, c in enumerate(order))
    counts['directed frames'] = sum(run_directed(lib, o, name, setup, frames)
                                    for name, setup, frames in directed_sequences(long_frames))
    outcomes = set(o.outcomes)
    counts['fail-stop checks'] = fail_stop(lib, o)

    capture_frames, states = 0, {}
    for label, path in capture_list():
        name, state, frames, seen = run_capture_unit(lib, elf, label, path, long_frames)
        capture_frames += frames
        states[name] = state
        outcomes |= seen
    counts['capture frames'] = capture_frames

    missing = sorted((hex(pc), taken) for pc in sites for taken in (True, False)
                     if (pc, taken) not in outcomes)
    assert not missing, ('branch outcomes never taken', missing)

    comp_frames = comp_leaves = 0
    for label, path, setup, frames in composition_runs(lib, elf, long_frames):
        f, leaves = compose(lib, elf, label, path, setup, frames)
        comp_frames += f
        comp_leaves += leaves
    counts['composition frames'] = comp_frames
    counts['single-row list runs'] = single_row_list(elf)

    reference_mode.banner(
        reference_mode.part(len(selected), total, 'synthetic cases'),
        f"{counts['synthetic frames']} synthetic frames",
        f"{counts['directed frames']} directed frames",
        f"{capture_frames} capture frames over {len(states)} captures",
        f"{comp_frames} composed frames ({comp_leaves} leaf calls)",
        f"{counts['fail-stop checks']} fail-stop checks",
        f"{counts['order checks']} write-order checks",
        f"{counts['single-row list runs']} single-row 0020B210 runs (no wrap event)")
    print(f'002149F0: {callees} callees hooked, {len(sites)} branch sites, both outcomes of each '
          f'taken; capture page states {sorted(set(states.values()))}')
    print('PASS')


if __name__ == '__main__':
    main()
