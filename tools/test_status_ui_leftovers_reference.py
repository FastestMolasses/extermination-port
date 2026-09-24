#!/usr/bin/env python3
"""Execute the original status-UI leftovers and compare em_status_ui_leftovers.c.

docs/STATUS_UI_LEFTOVERS.md. The user's pinned ELF and the captured original
RAM supply every instruction, table and string; none are embedded here.
Routines executed unmodified (census lane L35 plus the BATTERY page draw):

  0020AE40 page frame      0020B0D0 arrows      0020B210 list
  0020BEF0 ring index      0020CD40 / 0020CD60 / 0020CDA0 UI cues
  001C5930 area title      001C5860 band        001C4820 prop / pickup node
  0021B8E0 / 0021B900 / 0021BAC0 / 0021BA70 / 0021BAB0 context record saves
  0022EBE0 cinematic predicate

Every other callee is hooked and recorded (the 2D GS draw layer, the text
blitters, the gauges, the node services, the sound submit); the native
module gets the same values through its workers. Values a worker returns come
from the original where it matters: float_to_int 001281C0 and block_copy
00121870 execute original instructions on both sides, and on the captured
states the string width 001CC170 does too. The test asserts that every jal/j
target of the routines is either hooked or translated.

Arithmetic: COP1 goes through tools/ee_float_model.py (the FallEE core of
test_player_fall_reference.py); the native side uses em_ee_float.h.

Captured states:
  * startup-reference/panel (the original BATTERY confirmation page): the
    native 0020AE40 / 0020B210 / 0020B0D0 calls are replayed through the
    ORIGINAL 00207D00 / 00207E40 / 00209280 / 001281C0 into the captured
    render context; the packet bytes they build must equal, byte for byte,
    both packet buffers the original built for its last two frames.
  * every route beat 00..14 (build/s87/route) plus the opening and playable
    snapshots: the live area-title node and the pickup node the capture
    holds, several frames each, and the render-context saves.

Default run (~10 s): every case class and boundary, branch coverage of every
conditional branch in the translated routines asserted, the missing-worker /
missing-region fail-stop checks. EM_TEST_FULL=1: the exhaustive sweeps and
the long area-title runs (the 300-frame timers played out on the captures).
"""
import ctypes as C
import itertools
import json
import os
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode  # noqa: E402
from test_player_fall_reference import FallEE, nested_bits  # noqa: E402
from test_player_slide_reference import EE, read_elf, s32, sx32, bits, RETURN  # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
REFERENCE = DECOMP / 'build/startup-reference'
ROUTE = DECOMP / 'build/s87/route'
OUT = ROOT / 'build' / os.environ.get('EM_LANE', 'status_ui_leftovers_reference')
M32, M64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF

# ======================================================================
# The routines, their hooked callees and the translated callees
# ======================================================================

SIZES = {0x20AE40: 0x290, 0x20B0D0: 0x134, 0x20B210: 0x9CC, 0x20BEF0: 0x28,
         0x20CD40: 0x14, 0x20CD60: 0x14, 0x20CDA0: 0x14,
         0x1C5930: 0x318, 0x1C5860: 0xD0, 0x1C4820: 0x9C,
         0x21B8E0: 0x14, 0x21B900: 0x14, 0x21BA70: 0xC, 0x21BAB0: 0xC, 0x21BAC0: 0x1C,
         0x22EBE0: 0x50, 0x1B0BA0: 0x5C}
TRANSLATED = set(SIZES)
BLEND, SPRITE, HEALTH, BATTERY = 0x207D00, 0x207E40, 0x208AD0, 0x209280
F2I, COPY, FORMAT, TEXT_FIXED = 0x1281C0, 0x123168, 0x1C5FB0, 0x1CBA50
SOUND, WIDTH, TEXT_PROP = 0x1FB9F0, 0x1CC170, 0x1CC1E0
FREE, BIND, PLACE, PUBLISH, BLOCK_COPY = 0x1AFC10, 0x1B0FD0, 0x1C6380, 0x1B17A0, 0x121870
HOOKED = {BLEND, SPRITE, HEALTH, BATTERY, F2I, COPY, FORMAT, TEXT_FIXED, SOUND, WIDTH,
          TEXT_PROP, FREE, BIND, PLACE, PUBLISH, BLOCK_COPY}

BATTERY_TABLE, BATTERY_ROWS, BATTERY_GLYPH = 0x265C50, 0x265CD0, 0x20042D05A1322000
# Every (frame table, row table, glyph, base flag) pair the decomp's page
# routines pass (their calls, read in ../Extermination/src).
PAGES = [(0x265B80, 0x265BF0, 0x20042605A1321F80, 0x1), (0x265C50, 0x265CD0, 0x20042D05A1322000, 0x2),
         (0x265D20, 0x265D90, 0x20044D05A1321F80, 0x4), (0x265FF0, 0x266060, 0x20042D05A1321F80, 0x8),
         (0x2660E0, 0x266190, 0x20041A05A1321F80, 0x10), (0x2661C0, 0x266270, 0x20042805A1321F80, 0x20),
         (0x2662F0, 0x2663A0, 0x20040E05A1321F00, 0x40), (0x2663F0, 0x2664A0, 0x20041D05A1321F80, 0x80),
         (0x2664F0, 0x2665A0, 0x20042C05A1321F80, 0x100)]
PAGE, CONTEXT, ACTOR, SCRATCH = 0x00960000, 0x00970000, 0x00980000, 0x01F00000
PAGE_SIZE, ACTOR_SIZE, CONTEXT_SIZE = 0x260, 0x2F0, 0x200
METHOD = 0x00BEEF00   # a method word no original function lives at (hooked)


def in_translated(pc):
    return any(start <= pc < start + size for start, size in SIZES.items())


def check_callee_set(elf):
    ee = EE(elf)
    targets = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            word = ee.load(pc)
            if word >> 26 in (2, 3):
                targets.add((word & 0x3FFFFFF) << 2)
    missing = sorted(t for t in targets if t not in HOOKED and t not in TRANSLATED)
    assert not missing, ('callees neither hooked nor translated', [hex(t) for t in missing])
    unused = sorted(t for t in HOOKED if t not in targets)
    assert not unused, ('hooked addresses no routine calls', [hex(t) for t in unused])
    return len(targets)


def branch_sites(elf):
    ee = EE(elf)
    sites = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            word = ee.load(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            if op in (4, 20) and rs == 0 and rt == 0:
                continue
            if ee.branch(word, pc) is not None:
                sites.add(pc)
    return sites


class StringEE(FallEE):
    """FallEE plus the MMI byte ops the SDK string routines use (00123168 and
    its relatives, reached only inside the original 00209280 of the panel
    replay). Each is the architectural definition of the instruction."""

    def mmi(self, word, pc):
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        fn, sub = word & 63, word >> 6 & 31
        full = lambda n: (self.r[n] & M64) | (self.rh[n] << 64)
        lanes = lambda v: [(v >> (8 * i)) & 255 for i in range(16)]

        def put(values):
            value = sum((b & 255) << (8 * i) for i, b in enumerate(values))
            if rd:
                self.r[rd] = value & M64
                self.rh[rd] = value >> 64 & M64
        if fn == 0x08 and sub == 0x09:                                   # psubb
            put([a - b for a, b in zip(lanes(full(rs)), lanes(full(rt)))])
        elif fn == 0x28 and sub == 0x0A:                                 # pceqb
            put([255 if a == b else 0 for a, b in zip(lanes(full(rs)), lanes(full(rt)))])
        elif fn == 0x08 and sub == 0x0A:                                 # pcgtb
            sb = lambda x: x - 256 if x & 128 else x
            put([255 if sb(a) > sb(b) else 0 for a, b in zip(lanes(full(rs)), lanes(full(rt)))])
        else:
            super().mmi(word, pc)


# ======================================================================
# Values both sides get from the original
# ======================================================================

class Values:
    """float_to_int and (on captures) 001CC170 executed from original
    instructions in their own interpreter, cached by argument."""

    def __init__(self, elf, ram=None, spad=None):
        self.ee = StringEE(elf, ram, spad)
        self.f2i_cache, self.width_cache = {}, {}
        self.real_width = ram is not None

    def f2i(self, value_bits):
        if value_bits not in self.f2i_cache:
            v0, _ = nested_bits(self.ee, F2I, (), (value_bits,))
            self.f2i_cache[value_bits] = s32(v0)
        return self.f2i_cache[value_bits]

    def width(self, text):
        if text not in self.width_cache:
            if self.real_width:
                v0, _ = nested_bits(self.ee, WIDTH, (text,))
                self.width_cache[text] = s32(v0)
            else:
                self.width_cache[text] = (text >> 2) % 397   # scripted
        return self.width_cache[text]


def fmt(value, a1, a2):
    """Scripted 001C5FB0 result (the string content only matters as data)."""
    return ('%d/%d/%d' % (value, a1, a2)).encode()[:15]


# ======================================================================
# Original side
# ======================================================================

class Oracle(StringEE):
    def __init__(self, elf, values, ram=None, spad=None, bind_result=0):
        super().__init__(elf, ram, spad)
        self.values = values
        self.calls = []
        self.outcomes = set()
        self.bind_result = bind_result
        self.scratch = SCRATCH
        h = self.hooks
        h[BLEND] = lambda e: self.rec('blend', s32(e.r[4]), s32(e.r[5]))
        h[SPRITE] = lambda e: self.rec('sprite', *(s32(e.r[i]) for i in range(4, 9)), e.r[9] & M32,
                                       e.r[10] & M64)
        h[HEALTH] = lambda e: self.rec('health', e.r[4] & M32, s32(e.r[5]), s32(e.r[6]))
        h[BATTERY] = lambda e: self.rec('battery', e.r[4] & M32, s32(e.r[5]), s32(e.r[6]),
                                        e.r[7] & M64, s32(e.r[8]))
        h[TEXT_FIXED] = lambda e: self.rec('text_fixed', *(s32(e.r[i]) for i in range(4, 9)),
                                           self.cstring(e.r[9]), e.r[10] & M32)
        h[TEXT_PROP] = lambda e: self.rec('text_prop', *(s32(e.r[i]) for i in range(4, 9)),
                                          e.r[9] & M32, e.r[10] & M32)
        h[SOUND] = lambda e: self.rec('sound', *(s32(e.r[i]) for i in range(4, 8)))
        h[FREE] = lambda e: self.rec('free', e.r[4] & M32)
        h[PLACE] = lambda e: self.rec('place', e.r[4] & M32)
        h[PUBLISH] = lambda e: self.rec('publish', e.r[4] & M32)
        h[F2I] = self.f2i
        h[WIDTH] = self.width
        h[FORMAT] = self.format
        h[COPY] = self.copy
        h[BIND] = self.bind
        h[BLOCK_COPY] = self.block_copy
        h[METHOD] = lambda e: self.rec('method', e.r[4] & M32, METHOD)

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and in_translated(pc):
            self.outcomes.add((pc, b[0]))
        return b

    def rec(self, *entry):
        self.calls.append(entry)

    def cstring(self, address):
        out = bytearray()
        while True:
            c = self.load(address + len(out), 1)
            if not c:
                return bytes(out)
            out.append(c)
            assert len(out) < 256

    def f2i(self, e):
        value_bits = e.f[12] & M32
        self.rec('f2i', value_bits)
        e.r[2] = sx32(self.values.f2i(value_bits))

    def width(self, e):
        text = e.r[4] & M32
        self.rec('width', text)
        e.r[2] = sx32(self.values.width(text))

    def format(self, e):
        args = tuple(s32(e.r[i]) for i in range(4, 7))
        self.rec('format', *args)
        self.write(self.scratch, fmt(*args) + b'\0')
        e.r[2] = self.scratch
        self.scratch += 0x20

    def copy(self, e):
        text = self.cstring(e.r[5])
        self.rec('copy', text)
        self.write(e.r[4] & M32, text + b'\0')
        e.r[2] = e.r[4]

    def bind(self, e):
        self.rec('bind', e.r[4] & M32)
        e.r[2] = sx32(self.bind_result)

    def block_copy(self, e):
        # Recorded, then the original 00121870 body runs (hook lifted).
        base = self.load(0x275670)
        self.rec('block_copy', (e.r[4] - base) & M32, (e.r[5] - base) & M32, s32(e.r[6]))
        hook = self.hooks.pop(BLOCK_COPY)
        nested_bits(self, BLOCK_COPY, (e.r[4] & M32, e.r[5] & M32, s32(e.r[6])))
        self.hooks[BLOCK_COPY] = hook

    def invoke(self, entry, regs):
        """Run an original routine with raw 64-bit argument registers."""
        self.calls = []
        for n, value in regs.items():
            self.r[n] = value & M64
        self.r[31] = RETURN
        self.run(entry)
        return self.r[2]


# ======================================================================
# Native side (ctypes)
# ======================================================================

VP, I32, U32, U64 = C.c_void_p, C.c_int32, C.c_uint32, C.c_uint64
FN = {
    'blend': C.CFUNCTYPE(C.c_int, VP, I32, I32),
    'sprite': C.CFUNCTYPE(C.c_int, VP, I32, I32, I32, I32, I32, U32, U64),
    'text_fixed': C.CFUNCTYPE(C.c_int, VP, I32, I32, I32, I32, I32, C.c_char_p, U32),
    'text_proportional': C.CFUNCTYPE(C.c_int, VP, I32, I32, I32, I32, I32, U32, U32),
    'text_width': C.CFUNCTYPE(C.c_int, VP, U32, C.POINTER(I32)),
    'health': C.CFUNCTYPE(C.c_int, VP, U32, I32, I32),
    'battery': C.CFUNCTYPE(C.c_int, VP, U32, I32, I32, U64, I32),
    'float_to_int': C.CFUNCTYPE(C.c_int, VP, U32, C.POINTER(I32)),
    'format': C.CFUNCTYPE(C.c_int, VP, I32, I32, I32, C.POINTER(C.c_void_p)),
    'copy': C.CFUNCTYPE(C.c_int, VP, C.c_void_p, C.c_size_t, C.c_char_p),
    'sound': C.CFUNCTYPE(C.c_int, VP, I32, I32, I32, I32),
    'block_copy': C.CFUNCTYPE(C.c_int, VP, C.c_void_p, U32, U32, I32),
    'free_actor': C.CFUNCTYPE(C.c_int, VP, C.c_void_p),
    'model_bind': C.CFUNCTYPE(C.c_int, VP, C.c_void_p, C.POINTER(I32)),
    'place': C.CFUNCTYPE(C.c_int, VP, C.c_void_p),
    'publish': C.CFUNCTYPE(C.c_int, VP, C.c_void_p),
    'method': C.CFUNCTYPE(C.c_int, VP, C.c_void_p, U32),
}
WORKER_FIELDS = list(FN)


class Workers(C.Structure):
    _fields_ = [('context', VP)] + [(name, FN[name]) for name in WORKER_FIELDS]


class Region(C.Structure):
    _fields_ = [('base', U32), ('size', U32), ('bytes', VP)]


class Memory(C.Structure):
    _fields_ = [('regions', C.POINTER(Region)), ('count', C.c_uint)]


class ListGlobals(C.Structure):
    _fields_ = [('d2821B4', I32), ('d2821B8', I32), ('d282240', I32)]


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('sul.dylib' if sys.platform == 'darwin' else 'sul.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc',
                    'src/game/em_status_ui_leftovers.c', '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    W, M, B = C.POINTER(Workers), C.POINTER(Memory), C.c_void_p
    sig = {
        'em_sul_0020AE40': [W, M, U32, U32, I32],
        'em_sul_0020B0D0': [W, M, U32],
        'em_sul_0020B210': [W, M, B, C.c_size_t, U32, U64, I32, C.POINTER(ListGlobals), C.POINTER(I32)],
        'em_sul_0020BEF0': [B, C.c_size_t, C.POINTER(I32)],
        'em_sul_0020CD40': [W], 'em_sul_0020CD60': [W], 'em_sul_0020CDA0': [W],
        'em_sul_001C5860': [U32],
        'em_sul_001C5930': [W, M, B, C.c_size_t],
        'em_sul_001C4820': [W, B, C.c_size_t],
        'em_sul_0021B8E0': [W, B, C.c_size_t], 'em_sul_0021B900': [W, B, C.c_size_t],
        'em_sul_0021BAC0': [W, B, C.c_size_t, I32], 'em_sul_0021BA70': [W, B, C.c_size_t, U64],
        'em_sul_0021BAB0': [B, C.c_size_t, C.POINTER(U64)],
        'em_sul_0022EBE0': [C.c_uint8, C.c_uint8],
        'em_sul_001B0BA0': [C.POINTER(C.c_int16), B],
    }
    for name, args in sig.items():
        getattr(n, name).argtypes = args
        getattr(n, name).restype = C.c_int32
    return n


class Native:
    """One native call's workers, recording the same tuples as Oracle."""

    def __init__(self, values, actor_address=None, block=None, bind_result=0, missing=None,
                 fault_at=None, replay=None):
        self.values, self.calls = values, []
        self.actor_address, self.block = actor_address, block
        self.bind_result = bind_result
        self.fault_at = fault_at          # 1-based worker call that returns -1
        self.replay = replay              # an Oracle to build packets in (panel replay)
        self.keep = []
        self.actor_pointer = None
        impl = {
            'blend': self.blend, 'sprite': self.sprite, 'text_fixed': self.text_fixed,
            'text_proportional': self.text_prop, 'text_width': self.width, 'health': self.health,
            'battery': self.battery, 'float_to_int': self.f2i, 'format': self.format,
            'copy': self.copy, 'sound': self.sound, 'block_copy': self.block_copy,
            'free_actor': lambda c, a: self.rec('free', self.actor(a)),
            'model_bind': self.bind,
            'place': lambda c, a: self.rec('place', self.actor(a)),
            'publish': lambda c, a: self.rec('publish', self.actor(a)),
            'method': lambda c, a, m: self.rec('method', self.actor(a), m),
        }
        self.cbs = {name: FN[name](impl[name]) for name in WORKER_FIELDS}
        fields = {name: (FN[name]() if name == missing else self.cbs[name]) for name in WORKER_FIELDS}
        self.w = Workers(None, **fields)

    def rec(self, *entry):
        self.calls.append(entry)
        if self.fault_at is not None and len(self.calls) == self.fault_at:
            return -1
        return 0

    def actor(self, pointer):
        assert pointer == self.actor_pointer, (pointer, self.actor_pointer)
        return self.actor_address

    def blend(self, c, slot, mode):
        r = self.rec('blend', slot, mode)
        if self.replay and r >= 0:
            self.replay.invoke(BLEND, {4: slot, 5: mode})
        return r

    def sprite(self, c, slot, x, y, w, h, rgba, tex0):
        r = self.rec('sprite', slot, x, y, w, h, rgba, tex0)
        if self.replay and r >= 0:
            self.replay.invoke(SPRITE, {4: slot, 5: sx32(x), 6: sx32(y), 7: sx32(w), 8: sx32(h),
                                        9: sx32(rgba), 10: tex0})
        return r

    def text_fixed(self, c, slot, x, y, w, h, text, style):
        return self.rec('text_fixed', slot, x, y, w, h, text, style)

    def text_prop(self, c, slot, x, y, w, h, text, style):
        return self.rec('text_prop', slot, x, y, w, h, text, style)

    def width(self, c, text, out):
        out[0] = self.values.width(text)
        return self.rec('width', text)

    def health(self, c, page, x, y):
        return self.rec('health', page, x, y)

    def battery(self, c, page, x, y, tex0, compact):
        r = self.rec('battery', page, x, y, tex0, compact)
        if self.replay and r >= 0:
            self.replay.invoke(BATTERY, {4: page, 5: sx32(x), 6: sx32(y), 7: tex0, 8: sx32(compact)})
        return r

    def f2i(self, c, value_bits, out):
        out[0] = self.values.f2i(value_bits)
        return self.rec('f2i', value_bits)

    def format(self, c, value, a1, a2, out):
        buffer = C.create_string_buffer(fmt(value, a1, a2))
        self.keep.append(buffer)
        out[0] = C.addressof(buffer)
        return self.rec('format', value, a1, a2)

    def copy(self, c, dst, size, src):
        r = self.rec('copy', src)
        if len(src) + 1 > size:
            return -1
        C.memmove(dst, src + b'\0', len(src) + 1)
        return r

    def sound(self, c, *args):
        return self.rec('sound', *args)

    def block_copy(self, c, block, dst, src, count):
        r = self.rec('block_copy', dst, src, count)
        if r >= 0:
            C.memmove(block + dst, block + src, count)
        return r

    def bind(self, c, a, out):
        out[0] = self.bind_result
        return self.rec('bind', self.actor(a))


def memory(regions):
    """(Memory, keep-alive) over [(base, bytes)]."""
    buffers = [C.create_string_buffer(bytes(data), len(data)) for _, data in regions]
    array = (Region * len(regions))(*[Region(base, len(data), C.addressof(buf))
                                      for (base, data), buf in zip(regions, buffers)])
    return Memory(array, len(regions)), (buffers, array)


# ======================================================================
# Case runners: each returns (original result, native result) summaries
# ======================================================================

def ee_regions(o, spans):
    return [(base, o.read(base, size)) for base, size in spans]


DATA_SPAN = (0x265000, 0x3000)          # the page tables, styles and string tables
GAME_SPAN = (0x810400, 0xC00)           # D_008104D8 .. D_00810E7A
TITLE_SPANS = [DATA_SPAN, GAME_SPAN, (0x289B40, 0x100), (0x70003B80, 0x20)]


def run_frame(ctx, case):
    """0020AE40 with one table and flag word."""
    o, native, values = ctx['oracle'], ctx['native'], ctx['values']
    table, flags = case
    o.invoke(0x20AE40, {4: PAGE, 5: table, 6: flags})
    want = list(o.calls)
    mem, keep = memory(ee_regions(o, [DATA_SPAN]))
    nat = Native(values)
    rc = native.em_sul_0020AE40(C.byref(nat.w), C.byref(mem), PAGE, table, flags)
    assert rc == 0 and nat.calls == want, (hex(table), hex(flags), rc, nat.calls, want)
    return len(want)


def run_arrows(ctx, case):
    o, native, values = ctx['oracle'], ctx['native'], ctx['values']
    table, held = case
    o.save(0x810E70, held, 2)
    o.invoke(0x20B0D0, {4: 0, 5: table})
    want = list(o.calls)
    mem, keep = memory(ee_regions(o, [DATA_SPAN, GAME_SPAN]))
    nat = Native(values)
    rc = native.em_sul_0020B0D0(C.byref(nat.w), C.byref(mem), table)
    assert rc == 0 and nat.calls == want, (hex(table), hex(held), nat.calls, want)
    return len(want)


def list_case(rng, flags=None, table=None, glyph=None):
    page = bytearray(rng.randrange(256) for _ in range(PAGE_SIZE))
    count = rng.choice((0, 1, 2, 3, 3, 4, 5, 6, 7, 8, 12))
    page[0x18] = count
    page[0x17] = rng.choice((0, 1, 2, 3, 4, max(0, count - 1), count, rng.randrange(256)))
    page[0x19] = rng.choice((0, 1, max(0, count - 1), count, rng.randrange(256)))
    page[0x12] = rng.choice((0xFF, rng.randrange(256), 0x1B, 0x1D))
    base = rng.choice((0, 0x1B, 0x3A, 0xC, 0xB, -5, rng.randrange(-0x40, 0x40)))
    page[0x1E:0x20] = struct.pack('<h', base)
    for i in range(0x40):
        page[0x50 + i] = rng.choice((0, 1, 2, 3, rng.randrange(8)))
    if flags is None:
        pick = rng.choice(PAGES)
        table, glyph = pick[1], pick[2]
        flags = pick[3] | rng.choice((0, 0x400, 0x200, 0x600, 0x8, 0x28, 0x60))
    return dict(page=bytes(page), table=table, glyph=glyph, flags=flags,
                edge=rng.choice((0, 0x1000, 0x4000, 0x5000, 0x2000)),
                c70=[rng.choice((0, 1, 1, 2)) for _ in range(3)],
                b4=rng.randrange(-2, 3), b8=rng.randrange(-2, 0x40), g40=rng.choice((3, 4, 4, 5)),
                block=bytes(rng.randrange(256) for _ in range(0x60)))


def run_list(ctx, case):
    o, native, values = ctx['oracle'], ctx['native'], ctx['values']
    o.write(PAGE, case['page'])
    o.save(0x810E78, case['edge'], 2)
    o.write(0x810C64, case['block'])
    for i, v in enumerate(case['c70']):
        o.save(0x810C70 + i, v, 1)
    o.save(0x2821B4, case['b4'])
    o.save(0x2821B8, case['b8'])
    o.save(0x282240, case['g40'])
    o.scratch = SCRATCH
    result = s32(o.invoke(0x20B210, {4: PAGE, 5: case['table'], 6: case['glyph'], 7: case['flags']}))
    want = dict(calls=list(o.calls), page=o.read(PAGE, PAGE_SIZE), result=result,
                globals=(s32(o.load(0x2821B4)), s32(o.load(0x2821B8)), s32(o.load(0x282240))))
    mem, keep = memory(ee_regions(o, [DATA_SPAN, GAME_SPAN]))
    page = C.create_string_buffer(case['page'], PAGE_SIZE)
    g = ListGlobals(case['b4'], case['b8'], case['g40'])
    out = I32(0x7777)
    nat = Native(values)
    rc = native.em_sul_0020B210(C.byref(nat.w), C.byref(mem), page, PAGE_SIZE, case['table'],
                                case['glyph'], case['flags'], C.byref(g), C.byref(out))
    got = dict(calls=nat.calls, page=page.raw, result=out.value,
               globals=(g.d2821B4, g.d2821B8, g.d282240))
    assert rc == 0 and got == want, (case['flags'], rc, diff(got, want))
    return len(want['calls'])


def diff(got, want):
    for key in want:
        if got[key] != want[key]:
            if key == 'calls':
                i = next((i for i, (a, b) in enumerate(zip(got[key], want[key])) if a != b),
                         min(len(got[key]), len(want[key])))
                return key, i, got[key][i:i + 3], want[key][i:i + 3]
            if key in ('page', 'actor', 'context'):
                i = next(i for i, (a, b) in enumerate(zip(got[key], want[key])) if a != b)
                return key, hex(i), got[key][i], want[key][i]
            return key, got[key], want[key]
    return None


def run_ring(ctx, case):
    o, native = ctx['oracle'], ctx['native']
    cursor, count, head = case
    o.save(PAGE + 0x17, cursor, 1)
    o.save(PAGE + 0x18, count, 1)
    o.save(PAGE + 0x19, head, 1)
    want = s32(o.invoke(0x20BEF0, {4: PAGE}))
    page = C.create_string_buffer(o.read(PAGE, 0x20), 0x20)
    out = I32()
    assert native.em_sul_0020BEF0(page, 0x20, C.byref(out)) == 0 and out.value == want, case
    return 1


def run_cues(ctx):
    o, native, values = ctx['oracle'], ctx['native'], ctx['values']
    count = 0
    for entry, name in ((0x20CD40, 'em_sul_0020CD40'), (0x20CD60, 'em_sul_0020CD60'),
                        (0x20CDA0, 'em_sul_0020CDA0')):
        o.invoke(entry, {})
        nat = Native(values)
        assert getattr(native, name)(C.byref(nat.w)) == 0 and nat.calls == o.calls, name
        count += 1
    return count


def run_band(ctx, value_bits):
    o, native = ctx['oracle'], ctx['native']
    o.save(0x8104D8, value_bits)
    want = s32(o.invoke(0x1C5860, {}))
    got = native.em_sul_001C5860(value_bits)
    assert got == want, (hex(value_bits), got, want)
    return 1


def run_title(o, native, values, actor_address, frames, setup=None):
    """001C5930 on the node at actor_address for `frames` frames (the
    oracle's memory is the frame's memory; only the node changes)."""
    checks = 0
    for _ in range(frames):
        if setup:
            setup(o)
        before = o.read(actor_address, ACTOR_SIZE)
        o.invoke(0x1C5930, {4: actor_address})
        want = dict(calls=list(o.calls), actor=o.read(actor_address, ACTOR_SIZE))
        mem, keep = memory(ee_regions(o, TITLE_SPANS))
        actor = C.create_string_buffer(before, ACTOR_SIZE)
        nat = Native(values, actor_address=actor_address)
        nat.actor_pointer = C.addressof(actor)
        rc = native.em_sul_001C5930(C.byref(nat.w), C.byref(mem), actor, ACTOR_SIZE)
        got = dict(calls=nat.calls, actor=actor.raw)
        assert rc == 0 and got == want, (hex(actor_address), rc, diff(got, want))
        checks += 1
    return checks


def title_case(rng):
    actor = bytearray(rng.randrange(256) for _ in range(ACTOR_SIZE))
    actor[4] = rng.choice((0, 1, 1, 1, 1, 2, 3, 4, 7))
    actor[5] = rng.choice((0, 0, 1, 1, 2))
    actor[6] = rng.choice((0, 0, 1, 1, 2))
    actor[0x28:0x2A] = struct.pack('<h', rng.choice((1, 2, 300, 0, -1, 240)))
    actor[0x2A:0x2C] = struct.pack('<h', rng.choice((26, 0, 5, 30, 12)))
    actor[0x1F0:0x1F4] = struct.pack('<i', rng.choice((0, 1, 2, 3, 4, 5)))
    actor[0x1F4:0x1F6] = struct.pack('<h', rng.choice((1, 2, 300, 0)))
    infection = rng.choice((0.0, 19.9, 20.0, 20.5, 49.0, 50.0, 51.0, 70.0, 70.5, 89.0, 90.0,
                            99.0, 100.0, 101.0, -3.0, 55.5))
    return dict(actor=bytes(actor), spad=rng.choice((0, 1, 2, 3, 4, 5, 0xFF)),
                b8=rng.choice((0, 0, 0, 1, 2)), area=rng.choice((11, 11, 0, 3)),
                sub=rng.choice((0, 0, 1, 2)), base=rng.choice((26, 0, 7)),
                infection=bits(infection))


def run_title_case(ctx, case):
    o, native, values = ctx['title_oracle'], ctx['native'], ctx['values']
    o.write(ACTOR, case['actor'])
    o.save(0x70003B8D, case['spad'], 1)
    o.save(0x8106B8, case['b8'], 1)
    o.save(0x810700, case['area'], 1)
    o.save(0x810701, case['sub'], 1)
    o.save(0x289B40 + 4 * case['area'], case['base'], 2)
    o.save(0x8104D8, case['infection'])
    return run_title(o, native, values, ACTOR, 1)


def run_node(ctx, case):
    o, native, values = ctx['oracle'], ctx['native'], ctx['values']
    state, bound = case
    actor = bytearray(ACTOR_SIZE)
    actor[4] = state
    actor[0x4C:0x50] = struct.pack('<I', METHOD)
    o.write(ACTOR, bytes(actor))
    o.bind_result = bound
    o.invoke(0x1C4820, {4: ACTOR})
    buf = C.create_string_buffer(bytes(actor), ACTOR_SIZE)
    nat = Native(values, actor_address=ACTOR, bind_result=bound)
    nat.actor_pointer = C.addressof(buf)
    rc = native.em_sul_001C4820(C.byref(nat.w), buf, ACTOR_SIZE)
    assert rc == 0 and nat.calls == o.calls and buf.raw == o.read(ACTOR, ACTOR_SIZE), (case, nat.calls, o.calls)
    return len(o.calls)


def run_context(o, native, values, entry, args, name, extra=()):
    base = o.load(0x275670)
    before = o.read(base, CONTEXT_SIZE)
    regs = {4 + i: a for i, a in enumerate(args)}
    result = o.invoke(entry, regs)
    want = dict(calls=list(o.calls), context=o.read(base, CONTEXT_SIZE))
    buf = C.create_string_buffer(before, CONTEXT_SIZE)
    nat = Native(values)
    if name == 'em_sul_0021BAB0':
        value = U64()
        rc = native.em_sul_0021BAB0(buf, CONTEXT_SIZE, C.byref(value))
        assert rc == 0 and value.value == result & M64
    else:
        rc = getattr(native, name)(C.byref(nat.w), buf, CONTEXT_SIZE, *extra)
    got = dict(calls=nat.calls, context=buf.raw)
    assert rc == 0 and got == want, (name, args, diff(got, want))
    o.write(base, before)
    return 1


def context_cases(o, native, values, rng):
    count = 0
    count += run_context(o, native, values, 0x21B8E0, (), 'em_sul_0021B8E0')
    count += run_context(o, native, values, 0x21B900, (), 'em_sul_0021B900')
    for slot in (0, 1, 2, 3, -1, -4):
        count += run_context(o, native, values, 0x21BAC0, (sx32(slot),), 'em_sul_0021BAC0', (slot,))
    for value in (0, 0x0123456789ABCDEF, M64, rng.getrandbits(64)):
        count += run_context(o, native, values, 0x21BA70, (value,), 'em_sul_0021BA70', (value,))
    count += run_context(o, native, values, 0x21BAB0, (), 'em_sul_0021BAB0')
    return count


def run_area_bases(ctx, elf, rng):
    """001B0BA0 on the ELF's own counts and on random count tables; the
    ELF result must also be the table every capture holds."""
    o, native = ctx['oracle'], ctx['native']
    tables = [None] + [[rng.choice((0, 1, 2, 5, 40, -3, 0x7FFF, -0x8000)) for _ in range(23)]
                       for _ in range(reference_mode.pick(400, 20))]
    elf_table = None
    for counts in tables:
        saved = o.read(0x24A850, 46)
        if counts is not None:
            o.write(0x24A850, struct.pack('<23h', *counts))
        o.write(0x289B40, bytes(0x5C))
        o.invoke(0x1B0BA0, {})
        want = o.read(0x289B40, 0x5C)
        values = (C.c_int16 * 23)(*struct.unpack('<23h', o.read(0x24A850, 46)))
        out = C.create_string_buffer(0x5C)
        assert native.em_sul_001B0BA0(values, out) == 0 and out.raw == want, counts
        if counts is None:
            elf_table = want
        o.write(0x24A850, saved)
    for _, ram_path, _ in captures():
        ram = ram_path.read_bytes()
        assert ram[0x289B40:0x289B40 + 0x5C] == elf_table, ram_path
    return len(tables)


def run_predicate(ctx, case):
    o, native = ctx['oracle'], ctx['native']
    e4, spad = case
    o.save(0x8101E4, e4, 1)
    o.save(0x70003B8D, spad, 1)
    want = s32(o.invoke(0x22EBE0, {}))
    got = native.em_sul_0022EBE0(e4, spad)
    assert got == want, (case, got, want)
    return 1


# ======================================================================
# Fail-stop checks
# ======================================================================

def fail_stop_checks(ctx, rng):
    """A missing reachable worker or region returns -1 before any call or
    write; a worker fault stops the routine at that call."""
    o, native, values = ctx['oracle'], ctx['native'], ctx['values']
    count = 0
    mem, keep = memory(ee_regions(o, [DATA_SPAN, GAME_SPAN]))
    empty = Memory(None, 0)
    need = {'em_sul_0020AE40': ('blend', 'sprite', 'battery'),
            'em_sul_0020B0D0': ('blend', 'sprite'),
            'em_sul_0020B210': ('blend', 'sprite', 'float_to_int', 'sound')}
    for name, fields in need.items():
        for field in fields + (None,):
            nat = Native(values, missing=field)
            page = C.create_string_buffer(bytes(PAGE_SIZE), PAGE_SIZE)
            page[0x18] = b'\x03'
            g, out = ListGlobals(7, 8, 4), I32()
            args = {'em_sul_0020AE40': (C.byref(nat.w), C.byref(mem if field else empty), PAGE, BATTERY_TABLE, 2),
                    'em_sul_0020B0D0': (C.byref(nat.w), C.byref(mem if field else empty), BATTERY_TABLE),
                    'em_sul_0020B210': (C.byref(nat.w), C.byref(mem if field else empty), page, PAGE_SIZE,
                                        BATTERY_ROWS, BATTERY_GLYPH, 0x2, C.byref(g), C.byref(out))}[name]
            assert getattr(native, name)(*args) == -1 and nat.calls == [], (name, field)
            assert page.raw[0x18] == 3 and page.raw[0x1A] == 0 and (g.d2821B4, g.d282240) == (7, 4)
            count += 1
    # 0020B210 with flags & 8 needs format / copy / text_fixed.
    for field in ('format', 'copy', 'text_fixed'):
        nat = Native(values, missing=field)
        page = C.create_string_buffer(bytes(PAGE_SIZE), PAGE_SIZE)
        g, out = ListGlobals(), I32()
        assert native.em_sul_0020B210(C.byref(nat.w), C.byref(mem), page, PAGE_SIZE, BATTERY_ROWS,
                                      BATTERY_GLYPH, 0x8, C.byref(g), C.byref(out)) == -1 and not nat.calls
        count += 1
    # A worker fault stops the list at that call.
    case = list_case(random.Random(5), flags=0x2, table=BATTERY_ROWS, glyph=BATTERY_GLYPH)
    page = C.create_string_buffer(case['page'], PAGE_SIZE)
    g, out = ListGlobals(), I32()
    nat = Native(values, fault_at=1)
    assert native.em_sul_0020B210(C.byref(nat.w), C.byref(mem), page, PAGE_SIZE, BATTERY_ROWS,
                                  BATTERY_GLYPH, 0x2, C.byref(g), C.byref(out)) == -1 and len(nat.calls) == 1
    count += 1
    # The title and the node.
    title_mem, title_keep = memory(ee_regions(ctx['title_oracle'], TITLE_SPANS))
    for field in ('free_actor', 'text_width', 'text_proportional', None):
        nat = Native(values, missing=field, actor_address=ACTOR)
        actor = C.create_string_buffer(bytes([0, 0, 0, 0, 1] + [0] * (ACTOR_SIZE - 5)), ACTOR_SIZE)
        nat.actor_pointer = C.addressof(actor)
        before = actor.raw
        assert native.em_sul_001C5930(C.byref(nat.w), C.byref(title_mem if field else empty), actor,
                                      ACTOR_SIZE) == -1 and nat.calls == [] and actor.raw == before, field
        count += 1
    for field in ('free_actor', 'model_bind', 'place', 'publish', 'method'):
        nat = Native(values, missing=field, actor_address=ACTOR)
        actor = C.create_string_buffer(bytes(ACTOR_SIZE), ACTOR_SIZE)
        nat.actor_pointer = C.addressof(actor)
        assert native.em_sul_001C4820(C.byref(nat.w), actor, ACTOR_SIZE) == -1 and nat.calls == []
        count += 1
    for name in ('em_sul_0020CD40', 'em_sul_0020CD60', 'em_sul_0020CDA0'):
        nat = Native(values, missing='sound')
        assert getattr(native, name)(C.byref(nat.w)) == -1
        count += 1
    # Context saves outside the supplied block, or without block_copy.
    buf = C.create_string_buffer(bytes(CONTEXT_SIZE), CONTEXT_SIZE)
    nat = Native(values)
    assert native.em_sul_0021BAC0(C.byref(nat.w), buf, CONTEXT_SIZE, 7) == -1 and not nat.calls
    nat = Native(values, missing='block_copy')
    assert native.em_sul_0021BA70(C.byref(nat.w), buf, CONTEXT_SIZE, 5) == -1 and buf.raw == bytes(CONTEXT_SIZE)
    count += 2
    return count


# ======================================================================
# The captured BATTERY page: packet bytes against the original's frame
# ======================================================================

SLOT1 = 0x14   # context + 0x10 + 4 * slot 1: the slot's packet cursor


def packet_streams(ram):
    """Start of each slot-1 stream the captured BATTERY page built: the
    00207D00 tag just before the first frame sprite (TEX0 = table word 0)."""
    t0 = struct.unpack_from('<Q', ram, BATTERY_TABLE)[0]
    starts, p = [], 0
    needle = struct.pack('<Q', t0)
    while (p := ram.find(needle, p)) >= 0:
        base = p - 0x40
        if p % 8 == 0 and struct.unpack_from('<Q', ram, base + 0x20)[0] == 0xA400000000008001:
            tag = base - 0x10
            assert ram[tag + 3] == 0x30 and struct.unpack_from('<H', ram, tag)[0] == 8
            starts.append(tag)
        p += 8
    return starts


def panel_replay(elf, native):
    ram = (REFERENCE / 'panel/eeMemory.bin').read_bytes()
    assert ram[0x810146] == 0x21 and ram[0x810135] == 4, 'not the original BATTERY confirmation'
    starts = packet_streams(ram)
    assert len(starts) == 2, [hex(s) for s in starts]
    values = Values(elf)
    results = []
    for start in starts:
        # (a) the original routines, re-executed over the capture with the
        #     real 00207D00 / 00207E40 / 00209280 / 001281C0.
        o = StringEE(elf, ram)
        ctx = o.load(0x275670)
        o.save(ctx + SLOT1, start)
        for entry, regs in ((0x20AE40, {4: 0x810130, 5: BATTERY_TABLE, 6: 2}),
                            (0x20B210, {4: 0x810130, 5: BATTERY_ROWS, 6: BATTERY_GLYPH, 7: 0x402}),
                            (0x20B0D0, {4: 0x810130, 5: BATTERY_TABLE})):
            for n, v in regs.items():
                o.r[n] = v
            o.r[31] = RETURN
            o.run(entry)
        end = o.load(ctx + SLOT1)
        assert o.read(start, end - start) == ram[start:end], 'original re-execution differs from its capture'
        # (b) the native routines, their draw calls replayed through the
        #     original draw layer into a fresh copy of the capture.
        rep = Oracle(elf, values, ram)
        for address in (BLEND, SPRITE, BATTERY, F2I, TEXT_FIXED, COPY, FORMAT):
            rep.hooks.pop(address, None)
        rep.save(ctx + SLOT1, start)
        mem, keep = memory(ee_regions(rep, [DATA_SPAN, GAME_SPAN]))
        nat = Native(values, replay=rep)
        page = C.create_string_buffer(rep.read(0x810130, 0xA0), 0xA0)
        g = ListGlobals(s32(rep.load(0x2821B4)), s32(rep.load(0x2821B8)), s32(rep.load(0x282240)))
        out = I32()
        assert native.em_sul_0020AE40(C.byref(nat.w), C.byref(mem), 0x810130, BATTERY_TABLE, 2) == 0
        assert native.em_sul_0020B210(C.byref(nat.w), C.byref(mem), page, 0xA0, BATTERY_ROWS,
                                      BATTERY_GLYPH, 0x402, C.byref(g), C.byref(out)) == 0
        assert native.em_sul_0020B0D0(C.byref(nat.w), C.byref(mem), BATTERY_TABLE) == 0
        assert rep.load(ctx + SLOT1) == end
        assert rep.read(start, end - start) == ram[start:end], 'native draw stream builds other packets'
        # the list's own writes match the original's
        assert page.raw == o.read(0x810130, 0xA0)
        assert (g.d2821B4, g.d2821B8, g.d282240) == (s32(o.load(0x2821B4)), s32(o.load(0x2821B8)),
                                                     s32(o.load(0x282240)))
        sprites = sum(1 for c in nat.calls if c[0] == 'sprite')
        results.append((end - start, sprites, len(nat.calls)))
    return results


# ======================================================================
# Route beats and snapshots
# ======================================================================

def captures():
    out = [('opening', REFERENCE / 'opening_ee.bin', REFERENCE / 'opening_scratchpad.bin'),
           ('playable', REFERENCE / 'playable_ee.bin', None)]
    for beat in sorted(ROUTE.glob('[01][0-9]_*')):
        out.append((beat.name, beat / 'eeMemory.bin', beat / 'scratchpad.bin'))
    return out


def find_nodes(ram, callback):
    nodes = []
    for i in range(0x100):
        a = 0x7A5640 + i * 0x2F0
        if struct.unpack_from('<I', ram, a + 0x10)[0] == callback and ram[a + 4] < 4:
            nodes.append(a)
    return nodes


def run_capture(job):
    """One capture: its area-title and pickup nodes (several frames) and the
    render-context saves over its context."""
    elf, native = JOB['elf'], JOB['native']
    label, ram_path, spad_path = job
    ram = ram_path.read_bytes()
    spad = spad_path.read_bytes() if spad_path and spad_path.exists() else None
    values = Values(elf, ram, spad)
    o = Oracle(elf, values, ram, spad)
    frames = reference_mode.pick(310, 4)
    title = find_nodes(ram, 0x1C5930)
    pickup = find_nodes(ram, 0x1C4820)
    checks = 0
    for node in title:
        checks += run_title(o, native, values, node, frames)
    for node in pickup:
        method = o.load(node + 0x4C)
        o.hooks[method] = lambda e: e.calls.append(('method', e.r[4] & M32, method))
        buf = C.create_string_buffer(o.read(node, ACTOR_SIZE), ACTOR_SIZE)
        o.invoke(0x1C4820, {4: node})
        nat = Native(values, actor_address=node)
        nat.actor_pointer = C.addressof(buf)
        nat.cbs['method'] = FN['method'](lambda c, a, m: nat.rec('method', nat.actor(a), m))
        nat.w.method = nat.cbs['method']
        assert native.em_sul_001C4820(C.byref(nat.w), buf, ACTOR_SIZE) == 0
        assert nat.calls == o.calls and buf.raw == o.read(node, ACTOR_SIZE), (label, nat.calls, o.calls)
        checks += 1
    checks += context_cases(o, native, values, random.Random(label))
    e4, mode = o.load(0x8101E4, 1), o.load(0x70003B8D, 1)
    predicate = s32(o.invoke(0x22EBE0, {}))
    assert native.em_sul_0022EBE0(e4, mode) == predicate
    checks += 1
    return label, len(title), len(pickup), checks, sorted(o.outcomes)


JOB = {}


def main():
    elf = read_elf()
    callees = check_callee_set(elf)
    native = build_native()
    values = Values(elf)
    oracle = Oracle(elf, values)
    oracle.save(0x275670, CONTEXT)
    for i in range(CONTEXT_SIZE):
        oracle.save(CONTEXT + i, (i * 37 + 11) & 255, 1)
    title_ram = (REFERENCE / 'opening_ee.bin').read_bytes()
    title_values = Values(elf)   # scripted widths for synthetic nodes
    ctx = dict(oracle=oracle, native=native, values=values,
               title_oracle=Oracle(elf, title_values, title_ram))
    ctx['title_oracle'].values = title_values
    rng = random.Random(0x5E1A)
    counts = {}

    # 0020AE40: every page table x flag words (callers' words, their list
    # modifiers and every bit combination in full mode).
    bits_used = (0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x400)
    words = sorted({sum(b for b, on in zip(bits_used, mask) if on)
                    for mask in itertools.product((0, 1), repeat=len(bits_used))})
    frame_cases = [(page[0], word) for page in PAGES for word in words]
    keep = {(p[0], w) for p in PAGES for w in (p[3], p[3] | 0x400, p[3] | 0x600, 0x22, 0x0A, 0x42,
                                               0x48, 0x60, 0x20, 0x21, 0, 0x2A)}
    frame_sel = reference_mode.select(frame_cases, 150, 1, keep=lambda i, c: c in keep)
    counts['0020AE40'] = sum(run_frame(ctx, c) for c in frame_sel)

    arrow_cases = [(page[0], held) for page in PAGES
                   for held in (0, 0x1000, 0x4000, 0x5000, 0x2000, 0x8000, 0xFFFF, 0xAFFF)]
    counts['0020B0D0'] = sum(run_arrows(ctx, c) for c in reference_mode.select(arrow_cases, 30, 2,
                                                                               axes=(lambda c: c[1],)))
    ring_cases = [(c, n, h) for c in (0, 1, 2, 3, 200, 255) for n in (0, 1, 3, 4, 5, 255)
                  for h in (0, 1, 2, 4, 254, 255)]
    counts['0020BEF0'] = sum(run_ring(ctx, c) for c in ring_cases)
    counts['cues'] = run_cues(ctx)

    list_total = reference_mode.pick(12000, 700)
    list_cases = [list_case(random.Random(i)) for i in range(list_total)]
    # The BATTERY page's own words on its own tables, every edge.
    for flags in (0x2, 0x402, 0x602):
        for edge in (0, 0x1000, 0x4000):
            for seed in range(6):
                c = list_case(random.Random(seed * 7 + edge + flags), flags=flags, table=BATTERY_ROWS,
                              glyph=BATTERY_GLYPH)
                c['edge'] = edge
                list_cases.append(c)
    counts['0020B210'] = sum(run_list(ctx, c) for c in list_cases)

    band_values = [bits(v) for v in (0.0, -0.0, 20.0, 19.999998, 20.000002, 50.0, 70.0, 90.0, 100.0,
                                     100.00001, 99.99999, -1.0, 1e30, -1e30, 3.4e38)]
    band_values += [0x7F800000, 0xFF800000, 0x7FC00000, 0x00000001, 0x80000001, 0x7F7FFFFF]
    band_values += [rng.getrandbits(32) for _ in range(reference_mode.pick(20000, 200))]
    band_values += [bits(rng.uniform(-10, 110)) for _ in range(reference_mode.pick(5000, 100))]
    counts['001C5860'] = sum(run_band(ctx, v) for v in band_values)

    title_cases = [title_case(random.Random(i)) for i in range(reference_mode.pick(6000, 500))]
    counts['001C5930 synthetic'] = sum(run_title_case(ctx, c) for c in title_cases)
    node_cases = [(s, b) for s in range(6) for b in (0, 1, -1)]
    counts['001C4820 synthetic'] = sum(run_node(ctx, c) for c in node_cases)
    counts['context saves synthetic'] = context_cases(oracle, native, values, rng)
    pred_cases = [(e, s) for e in range(256) for s in range(256)]
    pred_sel = reference_mode.select(pred_cases, 600, 3, keep=lambda i, c: c[0] in (0, 3, 4) or
                                     c[1] in (0, 1, 2, 3, 4, 5, 255))
    counts['0022EBE0'] = sum(run_predicate(ctx, c) for c in pred_sel)
    counts['001B0BA0'] = run_area_bases(ctx, elf, rng)
    counts['fail-stop'] = fail_stop_checks(ctx, rng)

    # The C fixture: the same fail-stop contract under ASan/UBSan.
    fixture = OUT / 'status_ui_leftovers_test'
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-Isrc', 'tests/status_ui_leftovers_test.c', 'src/game/em_status_ui_leftovers.c',
                    '-o', str(fixture)], cwd=ROOT, check=True)
    fixture_run = subprocess.run([str(fixture)], cwd=ROOT, capture_output=True, text=True)
    assert fixture_run.returncode == 0 and fixture_run.stdout.strip() == 'PASS', \
        fixture_run.stdout + fixture_run.stderr

    # Captured states.
    packet = panel_replay(elf, native)
    JOB.update(elf=elf, native=native)
    beats = reference_mode.parallel_map(run_capture, captures())
    outcomes = set(oracle.outcomes) | set(ctx['title_oracle'].outcomes)
    for *_, cover in beats:
        outcomes.update(cover)
    sites = branch_sites(elf)
    missing = sorted('%06X %s' % (pc, 'taken' if taken else 'not taken')
                     for pc in sites for taken in (True, False) if (pc, taken) not in outcomes)
    assert not missing, ('branch outcomes never exercised', missing)

    reference_mode.banner(reference_mode.part(len(frame_sel), len(frame_cases), '0020AE40 cases'),
                          reference_mode.part(len(list_cases), 12000 + 54, '0020B210 cases'),
                          reference_mode.part(len(title_cases), 6000, '001C5930 synthetic cases'),
                          '%d captures' % len(beats))
    report = {
        'mode': reference_mode.MODE,
        'jal_targets_hooked_or_translated': callees,
        'branch_sites_both_outcomes': len(sites),
        'calls_compared': counts,
        'asan_ubsan_fixture': 'PASS',
        'battery_panel_packets': [{'bytes': b, 'sprites': s, 'calls': n} for b, s, n in packet],
        'captures': [{'capture': label, 'title_nodes': t, 'pickup_nodes': p, 'checks': c}
                     for label, t, p, c, _ in beats],
    }
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('status UI leftovers vs original instructions: PASS', json.dumps(report))


if __name__ == '__main__':
    main()
