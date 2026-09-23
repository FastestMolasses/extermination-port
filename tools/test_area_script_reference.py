#!/usr/bin/env python3
"""Execute the original AREA11 script interpreter and compare em_area_script.

The user's own ELF interpreter 001BA1A0/001BA1F0 and the ftab_0024D880
command handlers run as original instructions over the captured first-control
RAM (../Extermination/build/startup-reference/playable_ee.bin, which holds the
loaded AREA11 overlay and the ELF script arenas). The native host executes the
same scripts from the same bytes. Every tick compares the result, the ordered
worker calls with their arguments, the script block (+0x1F0..+0x21F), every
modeled global/player/camera byte (3B84/3B8D/3B8F/3B91/3B92 included), the
message block, and the mutated script records. The oracle also fails on any
original write outside the modeled storage.

Worker boundaries: every callee of the handlers is intercepted and recorded.
The SDK math callees (0011E2A8, 001B1240, 001B12B0, 001B1380, 001B1470),
00182F90 and 001B7D60 run as original instructions inside the oracle; on the
native side 0011E2A8 is the native binding em_area_script_sin_0011E2A8, the
other math workers are answered by a scratch execution of the same original
function, 00182F90 by a four-lane VU model (verified here against the
original's writes), and 001B7D60 by the real em_message_op0c. Services
outside the scripts (fade substate, message completion, stream readiness,
camera track cursor, animation-done bit, skip promotion) are one scripted
environment applied identically to both sides between ticks.

A second part replays the native host over the ORIGINAL route captures
(../Extermination/build/s87/route, docs/FIRST_LEVEL_ROUTE.md): from the frame
each captured owner starts its script, the host ticks once per captured frame
and every script-owned field is compared with the next captured row (see
capture_case). A third part sweeps em_area_script_sin_0011E2A8 against the
original 0011E2A8.

No original instruction bytes, script data or disassembly are stored here.
Quick mode shortens record durations (+0x0C) to at most 5 ticks in both
images and samples the sine sweep; EM_TEST_FULL=1 runs every captured
duration and the whole sweep. Every route capture runs in both modes.
"""
import ctypes as C
import hashlib
from pathlib import Path
import struct
import subprocess
import sys

from reference_mode import FULL, banner, parallel_map
from test_point_light_reference import RETURN, bits, number, signed, fp
from test_truck_original_reference import TruckOracle

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent/'Extermination'
REF = DECOMP/'build/startup-reference'
ELF_SHA = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
BUILD = ROOT/'build/area_script_reference'

OVERLAY = (0x823500, 0x82AD00)       # AREA11 overlay arena (scripts 0x8283D0..)
PANEL = (0x246F20, 0x247E20)         # ELF panel script arena
STACK_TOP = 0x01F00000
TRACK = 0x01E80000                   # 001C6120 result (worker-owned)
SCRATCH = 0x01E90000                 # scratch execution area for math workers
SYNTHETIC_OWNER = 0x01E00000
ROGER = 0x7A8830                     # Roger's pool node in the capture
PLAYER = 0x8102B0

# ---------------------------------------------------------------- memory model
REGIONS = {  # name: (base, size, compared size)
    'spad36': (0x70003600, 0x10, 0x10),
    'spad3B': (0x70003B40, 0x60, 0x60),
    'camera': (0x8101E0, 0xD0, 0xD0),
    'player': (0x8102B0, 0x300, 0x300),
    'work': (0x8105D0, 0x30, 0x30),
    'req': (0x8106B0, 0x48, 0x48),
    'prog': (0x810700, 0x200, 0x200),
    'msg': (0x2821B0, 0x400, 0x9C),
    'busy': (0x282150, 0x10, 0x10),
    'fade': (0x28A9A0, 0x10, 0x10),
    'cue': (0x275C70, 0x10, 0x10),
    'stream': (0x821050, 0x10, 0x10),
}


class RamOracle(TruckOracle):
    """TruckOracle arithmetic (EE add/sub guard bit, truncating mul/VU,
    rounding div) over captured RAM, plus lb/nor and write tracking."""
    def __init__(self, elf, ram, spad):
        self.written = set()
        super().__init__(elf)
        self.mem, self.ram, self.spad = {}, ram, spad
        self.r[29] = STACK_TOP
        self.written = set()

    def load(self, address, size=4):
        value = 0
        for i in range(size):
            a = address+i
            b = self.mem.get(a)
            if b is None:
                if a < len(self.ram): b = self.ram[a]
                elif 0x70000000 <= a < 0x70004000: b = self.spad[a-0x70000000]
                else: b = 0
            value |= b << (8*i)
        return value

    def save(self, address, value, size=4):
        for i in range(size):
            self.mem[address+i] = value >> (8*i) & 255
            self.written.add(address+i)

    def plain(self, word):
        op, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        if op == 32:
            address = (self.r[rs]+signed(word & 65535, 16)) & 0xffffffff
            self.r[rt] = signed(self.load(address, 1), 8) & 0xffffffff
        elif op == 0 and word & 63 == 39:
            self.r[rd] = ~(self.r[rs] | self.r[rt]) & 0xffffffff
        elif op == 0 and word & 63 in (10, 11):
            if (self.r[rt] == 0) == (word & 63 == 10): self.r[rd] = self.r[rs]
        elif op == 17 and rs == 16 and word & 63 == 5:
            self.f[word >> 6 & 31] = self.f[rd] & 0x7fffffff
        elif op == 17 and rs == 16 and word & 63 == 36:
            # cvt.w.s: the EE FPU truncates toward zero (saturating).
            x = number(self.f[rd])
            self.f[word >> 6 & 31] = max(-0x80000000, min(0x7fffffff, int(x))) & 0xffffffff
        else:
            super().plain(word)
        self.r[0] = 0

    def nested(self, entry):
        """Run an original callee from inside a hook; registers are live."""
        saved = self.r[31]
        self.depth += 1
        self.run(entry)
        self.depth -= 1
        self.r[31] = saved

    depth = 0
    tails = {}

    def run(self, entry, args=(), floats=(), stop=RETURN):
        """OwnerOracle.run plus `tails`: a hook reached by any control
        transfer (001B99F0 tail-jumps into the record's callback with jr)."""
        self.r[31] = RETURN
        for i, value in enumerate(args): self.r[4+i] = value
        for i, value in enumerate(floats): self.f[12+i] = bits(value)
        pc = entry
        for _ in range(200000):
            if pc == stop: return
            if pc in self.tails:
                self.tails[pc](self); pc = self.r[31]; continue
            word = self.load(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            offset = signed(word & 65535, 16)*4
            indirect_call = op == 0 and word & 63 == 9
            if op in (2, 3) or indirect_call:
                target = self.r[rs] if indirect_call else (pc+4 & 0xf0000000) | (word & 0x3ffffff)*4
                if indirect_call: self.r[word >> 11 & 31] = pc+8
                elif op == 3: self.r[31] = pc+8
                self.plain(self.load(pc+4))
                if target in self.calls:
                    self.calls[target](self)
                    pc = pc+8 if op == 3 or indirect_call else self.r[31]
                else: pc = target
                continue
            branch = None
            if op in (4, 5, 20, 21):
                taken = (self.r[rs] == self.r[rt]) == (op in (4, 20))
                if op in (20, 21) and not taken: pc += 8; continue
                branch = pc+4+offset if taken else pc+8
            elif op in (6, 7, 22, 23):
                taken = signed(self.r[rs]) <= 0 if op in (6, 22) else signed(self.r[rs]) > 0
                if op in (22, 23) and not taken: pc += 8; continue
                branch = pc+4+offset if taken else pc+8
            elif op == 1:
                assert rt in (0, 1, 2, 3), ('REGIMM', rt)
                taken = signed(self.r[rs]) < 0 if rt in (0, 2) else signed(self.r[rs]) >= 0
                if rt in (2, 3) and not taken: pc += 8; continue
                branch = pc+4+offset if taken else pc+8
            elif op == 17 and rs == 8:
                taken = self.condition == bool(rt & 1)
                if rt & 2 and not taken: pc += 8; continue
                branch = pc+4+offset if taken else pc+8
            elif op == 0 and word & 63 == 8: branch = self.r[rs]
            if branch is not None:
                self.plain(self.load(pc+4)); pc = branch
            else:
                try: self.plain(word)
                except AssertionError as error: raise AssertionError(hex(pc), error) from error
                pc += 4
        raise AssertionError('original routine did not return')


class NativeMem:
    """The canonical storage the native world points at, by original address."""
    def __init__(self, ram, spad, owner):
        self.buffers = {}
        for name, (base, size, _) in list(REGIONS.items())+[('owner', (owner, 0x300, 0x300))]:
            data = bytearray(size)
            for i in range(size):
                a = base+i
                if name == 'msg' and i >= 0x9C: continue
                if a < len(ram): data[i] = ram[a]
                elif 0x70000000 <= a < 0x70004000: data[i] = spad[a-0x70000000]
            self.buffers[name] = (base, size, (C.c_ubyte*size).from_buffer_copy(bytes(data)))

    def locate(self, address):
        for base, size, buf in self.buffers.values():
            if base <= address < base+size: return buf, address-base
        raise KeyError(hex(address))

    def pointer(self, address, ctype):
        buf, off = self.locate(address)
        return C.cast(C.addressof(buf)+off, C.POINTER(ctype))

    def load(self, address, size=4):
        buf, off = self.locate(address)
        return sum(buf[off+i] << (8*i) for i in range(size))

    def save(self, address, value, size=4):
        buf, off = self.locate(address)
        for i in range(size): buf[off+i] = value >> (8*i) & 255


# ---------------------------------------------------------------- native types
class Script(C.Structure):
    _fields_ = [('active', C.c_int32), ('phase', C.c_int32), ('pc', C.c_uint32),
                ('skip_phase', C.c_int8), ('skip_request', C.c_uint8)]


class Image(C.Structure):
    _fields_ = [('bytes', C.POINTER(C.c_ubyte)), ('base', C.c_uint32), ('entry', C.c_uint32),
                ('length', C.c_uint32)]


P8, P16, P32, PF = C.POINTER(C.c_uint8), C.POINTER(C.c_int16), C.POINTER(C.c_uint32), C.POINTER(C.c_float)
PS8, PI32 = C.POINTER(C.c_int8), C.POINTER(C.c_int32)
WORLD_FIELDS = [  # name, ctype pointer, original address (None: owner-relative)
    ('spad3B84', P16, 0x70003B84), ('spad3B8D', P8, 0x70003B8D), ('spad3B8F', P8, 0x70003B8F),
    ('spad3B91', P8, 0x70003B91), ('spad3B92', P8, 0x70003B92), ('spad3600', PF, 0x70003600),
    ('d8106EF', P8, 0x8106EF), ('d8106F3', P8, 0x8106F3), ('d8106F4', P8, 0x8106F4),
    ('d8106D4', P8, 0x8106D4),
    ('d810758', P8, 0x810758), ('d8107D8', P8, 0x8107D8), ('d81078F', P8, 0x81078F),
    ('d8101E1', P8, 0x8101E1), ('d8101E2', P8, 0x8101E2), ('d8101E3', P8, 0x8101E3),
    ('d8101E4', P8, 0x8101E4), ('d8101E6', P8, 0x8101E6),
    ('cam_0C', PF, 0x8101EC), ('cam_10', PF, 0x8101F0), ('cam_20', PF, 0x810200),
    ('cam_50', PF, 0x810230), ('cam_54', PF, 0x810234), ('cam_6E', P16, 0x81024E),
    ('cam_70', P32, 0x810250), ('cam_74', PF, 0x810254), ('cam_78', PF, 0x810258),
    ('cam_A0', P16, 0x810280),
    ('d8105D0', PF, 0x8105D0), ('d8105E0', PF, 0x8105E0), ('d8105F0', PF, 0x8105F0),
    ('d2821B0', PI32, 0x2821B0), ('d2821B4', PI32, 0x2821B4), ('d2821BC', PI32, 0x2821BC),
    ('d2821B8', P32, 0x2821B8),
    ('p040', P32, PLAYER+0x40), ('p0A0', PF, PLAYER+0xA0), ('p0B0', PF, PLAYER+0xB0),
    ('p0C0', PF, PLAYER+0xC0), ('p1F2', P16, PLAYER+0x1F2), ('p1F4', PF, PLAYER+0x1F4),
    ('p1F8', PF, PLAYER+0x1F8), ('p200', P32, PLAYER+0x200), ('p20C', P16, PLAYER+0x20C),
    ('p25C', P8, PLAYER+0x25C), ('p2F3', P8, PLAYER+0x2F3), ('p2FF', P8, PLAYER+0x2FF),
    ('self', C.c_uint32, None), ('s040', P32, 0x40), ('s0B0', PF, 0xB0), ('s0C0', PF, 0xC0),
    ('d28A9A0', P16, 0x28A9A0), ('d282157', PS8, 0x282157),
    ('d275C78', P8, 0x275C78), ('d821058', P8, 0x821058),
    ('d24D8F0', P16, None), ('d24D8F0_count', C.c_uint32, None),
]


class World(C.Structure):
    _fields_ = [(name, ctype) for name, ctype, _ in WORLD_FIELDS]


class Host(C.Structure):
    pass


# Worker table: name, original address, argument kinds, result kind.
#   i int, h short, b byte, a original address, f float, v vec4 pointer
WORKERS = [
    ('r_0028A490', None, 'a*', 'u'), ('r_track_head', None, 'a*', 'fo'),
    ('r_player_bone_C0', None, '*', 'v4'),
    ('w_001AEB60', 0x1AEB60, 'h', ''), ('w_001AEBA0', 0x1AEBA0, 'h', ''),
    ('w_001AEDE0', 0x1AEDE0, 'hb', ''), ('w_001AEE10', 0x1AEE10, 'hb', ''),
    ('w_001FD4C0', 0x1FD4C0, 'i', ''),
    ('w_00119828', 0x119828, 'iii', ''), ('w_001D2610', 0x1D2610, 'f', ''),
    ('w_001D25F0', 0x1D25F0, 'f', ''), ('w_001CA770', 0x1CA770, 'a', ''),
    ('w_001FAE70', 0x1FAE70, 'i', ''), ('w_001CA700', 0x1CA700, 'aah', 'io'),
    ('w_001D06D0', 0x1D06D0, 'ab', ''),
    ('w_001DD980', 0x1DD980, 'vv', ''), ('w_0011E2A8', 0x11E2A8, 'f', 'fo'),
    ('w_001C6120', 0x1C6120, 'ai', 'uo'), ('w_0022EC30', 0x22EC30, 'a', ''),
    ('w_00182F90', 0x182F90, 'av', ''), ('w_001B1240', 0x1B1240, 'vff', 'fo'),
    ('w_001B12B0', 0x1B12B0, 'fff', 'fo'),
    ('c_record', None, 'special', ''), ('w_001B7D60', 0x1B7D60, 'special', ''),
    ('w_001C67E0', 0x1C67E0, 'ahff', ''),
    ('w_001B0250', 0x1B0250, '', ''), ('w_0021B9A0', 0x21B9A0, 'iff', ''),
    ('w_001D2830', 0x1D2830, 'ii', ''), ('w_0018CBD0', 0x18CBD0, 'aaf', ''),
    ('w_0018D7B0', 0x18D7B0, 'ai', ''), ('w_001B0460', 0x1B0460, 'i', ''),
    ('w_001FBC50', 0x1FBC50, '', ''), ('w_001FABB0', 0x1FABB0, '', ''),
    ('w_001AED80', 0x1AED80, 'b', ''), ('w_001AEDB0', 0x1AEDB0, 'b', ''),
    ('w_001B1380', 0x1B1380, 'vvf', 'io'), ('w_001B1470', 0x1B1470, 'f', 'fo'),
    ('w_00182BF0', 0x182BF0, 'a', 'io'),
    ('w_001B0C00', 0x1B0C00, 'i', ''), ('w_001B6250', 0x1B6250, 'a', ''),
]
PASSTHROUGH = {0x11E2A8, 0x1B1240, 0x1B12B0, 0x1B1380, 0x1B1470, 0x182F90, 0x1B7D60}
KIND_CTYPE = {'i': C.c_int, 'h': C.c_int16, 'b': C.c_uint8, 'a': C.c_uint32, 'f': C.c_float,
              'v': PF}
OUT_CTYPE = {'io': PI32, 'fo': PF, 'uo': P32}


def worker_type(name, args, result):
    if name == 'r_0028A490': return C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, P32)
    if name == 'r_track_head': return C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, PF)
    if name == 'r_player_bone_C0': return C.CFUNCTYPE(C.c_int, C.c_void_p, PF)
    if name == 'c_record':
        return C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(Host),
                           C.POINTER(C.c_ubyte), PI32)
    if name == 'w_001B7D60':
        return C.CFUNCTYPE(C.c_int, C.c_void_p, P8, C.POINTER(C.c_ubyte), PI32)
    params = [KIND_CTYPE[k] for k in args]
    if result: params.append(OUT_CTYPE[result])
    return C.CFUNCTYPE(C.c_int, C.c_void_p, *params)


WORKER_TYPES = {name: worker_type(name, args, result) for name, _, args, result in WORKERS}


class Workers(C.Structure):
    _fields_ = [('ctx', C.c_void_p)]+[(name, WORKER_TYPES[name]) for name, *_ in WORKERS]


Host._fields_ = [('script', Script), ('st_0E', C.c_int16), ('st_10', C.c_float*4),
                 ('st_20', C.c_float*4), ('image', C.POINTER(Image)),
                 ('world', C.POINTER(World)), ('workers', C.POINTER(Workers)),
                 ('fault_address', C.c_uint32), ('fault_pc', C.c_uint32), ('faulted', C.c_int)]


# ---------------------------------------------------------------- environment
def fbits(value): return bits(value) & 0xffffffff


class Env:
    """Services outside the scripts, driven identically on both sides.
    It sees only the calls its side made; the comparison asserts those are
    identical, so the two environments stay in lockstep."""
    def __init__(self, scenario):
        self.s = scenario
        self.tick = 0
        self.fade_timer = None
        self.fade_next = None
        self.message_age = 0
        self.status_age = 0
        self.stream_age = None
        self.anim_age = 0
        self.frame_calls = 0
        self.callback_calls = {}
        self.busy_age = 0
        self.cue_age = 0

    def before_tick(self, mem):
        s = self.s
        if self.tick in s.get('set', {}):
            for address, value, size in s['set'][self.tick]: mem.save(address, value, size)
        if self.fade_timer is not None:
            self.fade_timer -= 1
            if self.fade_timer <= 0:
                mem.save(0x28A9A0, self.fade_next, 2); self.fade_timer = None
        if mem.load(0x2821B4) == 1:
            self.message_age += 1
            if self.message_age >= s.get('message_ticks', 4):
                mem.save(0x2821B4, 2); self.message_age = 0
        else: self.message_age = 0
        if mem.load(0x282224, 2) == 1:
            self.status_age += 1
            if self.status_age >= 3: mem.save(0x282224, 0x8001, 2)
        if self.stream_age is not None:
            self.stream_age += 1
            if self.stream_age >= 4: mem.save(0x8106F4, 1, 1); self.stream_age = None
        if mem.load(0x8101E4, 1) == 3:
            mem.save(0x810254, fbits(fp(number(mem.load(0x810254))+s.get('cursor_step', 0.5))))
        if not mem.load(PLAYER+0x200) & 0x1000:
            self.anim_age += 1
            if self.anim_age >= s.get('anim_ticks', 6):
                mem.save(PLAYER+0x200, mem.load(PLAYER+0x200) | 0x1000); self.anim_age = 0
        if signed(mem.load(0x282157, 1), 8):
            self.busy_age += 1
            if self.busy_age >= 2: mem.save(0x282157, 0, 1)
        if mem.load(0x821058, 1):
            self.cue_age += 1
            if self.cue_age >= 2: mem.save(0x821058, 0, 1); self.cue_age = 0
        skip = s.get('skip_tick')
        if skip is not None and self.tick >= skip and mem.load(0x70003B91, 1) == 1:
            mem.save(0x70003B91, 2, 1)   # 001AE6B0's 1 -> 2 promotion
        self.tick += 1

    def fade(self, now, later, ticks=3):
        self.fade_next = later; self.fade_timer = ticks
        self.fade_now = now

    def on_call(self, name, args, mem):
        """Side effects of a worker on shared inputs and its result."""
        if name in ('w_001AEDE0', 'w_001B0C00'):
            mem.save(0x28A9A0, 1, 2); self.fade(1, 2)
        elif name == 'w_001AEE10':
            mem.save(0x28A9A0, 3, 2); self.fade(3, 0)
        elif name == 'w_001AEDB0':
            mem.save(0x28A9A0, 2, 2)
        elif name == 'w_001AED80':
            mem.save(0x28A9A0, 0, 2)
        elif name == 'w_001FD4C0':
            self.stream_age = 0
        elif name == 'w_00182BF0':
            self.frame_calls += 1
            return 1 if self.frame_calls <= self.s.get('frame_busy', 1) else 0
        elif name == 'w_001CA700':
            return 1
        elif name == 'w_001C6120':
            return TRACK
        elif name == 'c_record':
            n = self.callback_calls.get(args[0], 0)+1
            self.callback_calls[args[0]] = n
            return 1 if n >= self.s.get('callback_ticks', 3) else 0
        return 0


# ---------------------------------------------------------------- oracle side
def read_args(o, kinds):
    out, ireg, freg = [], 4, 12
    for k in kinds:
        if k == 'f':
            out.append(o.f[freg] & 0xffffffff); freg += 1
        else:
            value = o.r[ireg]; ireg += 1
            if k == 'i': out.append(signed(value))
            elif k == 'h': out.append(signed(value, 16))
            elif k == 'b': out.append(value & 255)
            elif k == 'a': out.append(value & 0xffffffff)
            elif k == 'v': out.append(tuple(o.load(value+4*i) for i in range(4)))
    return tuple(out)


def install_oracle(o, env, calls, callbacks):
    for name, address, kinds, result in WORKERS:
        if address is None or kinds == 'special': continue
        def hook(o, name=name, address=address, kinds=kinds, result=result):
            if o.depth:
                # Inside an original passthrough callee: its own callees run
                # as original instructions and are not script worker calls.
                assert address in PASSTHROUGH, (name, 'reached inside a passthrough')
                o.nested(address)
                return
            args = read_args(o, kinds)
            if address in PASSTHROUGH:
                o.nested(address)
                value = o.f[0] & 0xffffffff if result == 'fo' else signed(o.r[2]) if result else None
                calls.append((name, args) if value is None else (name, args, value))
                return
            value = env.on_call(name, args, o)
            calls.append((name, args))
            if result == 'io': o.r[2] = value & 0xffffffff
            elif result == 'uo': o.r[2] = value & 0xffffffff
        o.calls[address] = hook
    def message(o):
        calls.append(('w_001B7D60', (o.load(o.r[6]+8),)))
        o.nested(0x1B7D60)
    o.calls[0x1B7D60] = message
    o.tails = {}
    for callback in callbacks:
        def record(o, callback=callback):
            calls.append(('c_record', (callback,)))
            o.r[2] = env.on_call('c_record', (callback,), o)
        o.tails[callback] = record


# ---------------------------------------------------------------- native side
def scratch_math(elf, ram, spad, address, args, kinds, result):
    """Answer a math worker by executing the same original function."""
    o = RamOracle(elf, ram, spad)
    ireg, freg = 4, 12
    for k, value in zip(kinds, args):
        if k == 'f': o.f[freg] = value; freg += 1
        elif k == 'v':
            base = SCRATCH+0x100*ireg
            for i, word in enumerate(value): o.save(base+4*i, word)
            o.r[ireg] = base; ireg += 1
        else: o.r[ireg] = value & 0xffffffff; ireg += 1
    o.run(address)
    return o.f[0] & 0xffffffff if result == 'fo' else signed(o.r[2])


def vu(a, b, sign):
    return fbits(fp(number(a)+sign*number(b)))


class Native:
    def __init__(self, lib, elf, ram, spad, owner, image_range, image_bytes, scenario, env,
                 mem=None):
        self.lib, self.env, self.calls = lib, env, []
        self.mem = mem if mem is not None else NativeMem(ram, spad, owner)
        self.elf, self.ram, self.spad = elf, ram, spad
        base, end = image_range
        self.image_buf = (C.c_ubyte*(end-base)).from_buffer_copy(image_bytes)
        self.image = Image(C.cast(self.image_buf, C.POINTER(C.c_ubyte)), base, base, end-base)
        table = struct.unpack_from('<9h', ram, 0x24D8F0)
        self.table = (C.c_int16*9)(*table)
        w = World()
        for name, ctype, address in WORLD_FIELDS:
            if name == 'self': w.self = owner
            elif name == 'd24D8F0': w.d24D8F0 = C.cast(self.table, P16)
            elif name == 'd24D8F0_count': w.d24D8F0_count = 9
            elif name in ('s040', 's0B0', 's0C0'):
                setattr(w, name, self.mem.pointer(owner+address, ctype._type_))
            else:
                setattr(w, name, self.mem.pointer(address, ctype._type_))
        for name in scenario.get('null_world', ()):
            setattr(w, name, None)
        self.world = w
        self.service = C.c_void_p(C.addressof(self.mem.buffers['msg'][2]))
        self.keep = []
        k = Workers()
        for name, address, kinds, result in WORKERS:
            if name in scenario.get('null_workers', ()): continue
            fn = WORKER_TYPES[name](self.make(name, address, kinds, result))
            self.keep.append(fn); setattr(k, name, fn)
        self.workers = k
        self.host = Host()
        lib.em_area_script_init(C.byref(self.host), C.byref(self.image), C.byref(self.world),
                                C.byref(self.workers))
        o = owner+0x1F0
        self.host.st_0E = signed(struct.unpack_from('<H', ram, o+0xE)[0], 16)
        for i in range(4):
            self.host.st_10[i] = struct.unpack_from('<f', ram, o+0x10+4*i)[0]
            self.host.st_20[i] = struct.unpack_from('<f', ram, o+0x20+4*i)[0]

    def make(self, name, address, kinds, result):
        mem, env, calls = self.mem, self.env, self.calls
        def vec(p): return tuple(C.cast(p, P32)[i] for i in range(4))
        if name == 'r_0028A490':
            def fn(_, a, out): out[0] = struct.unpack_from('<I', self.ram, a)[0]; return 0
            return fn
        if name == 'r_track_head':
            def fn(_, track, out):
                assert track == TRACK; out[0] = self.env.s.get('track_head', 6.0); return 0
            return fn
        if name == 'r_player_bone_C0':
            def fn(_, out):
                bone = mem.load(PLAYER+0x114)
                for i in range(4): out[i] = struct.unpack_from('<f', self.ram, bone+0xC0+4*i)[0]
                return 0
            return fn
        if name == 'c_record':
            def fn(_, callback, host, record, out):
                calls.append(('c_record', (callback,)))
                out[0] = env.on_call('c_record', (callback,), mem); return 0
            return fn
        if name == 'w_001B7D60':
            def fn(_, handshake, record, out):
                calls.append(('w_001B7D60', (struct.unpack_from('<I', bytes(record[8:12]))[0],)))
                r = self.lib.em_message_op0c(self.service, handshake, record)
                if r < 0: return -1
                out[0] = r; return 0
            return fn
        def fn(_, *values):
            args, ireg = [], 0
            params = values[:len(kinds)]
            for k, v in zip(kinds, params):
                if k == 'v': args.append(vec(v))
                elif k == 'f': args.append(fbits(v))
                else: args.append(v)
            args = tuple(args)
            if address in PASSTHROUGH:
                if address == 0x182F90:
                    self.model_182F90(args[1]); calls.append((name, args)); return 0
                if address == 0x11E2A8:
                    # The native binding em_area_script_sin_0011E2A8; the
                    # comparison asserts it equals the original's result.
                    out = C.c_float()
                    if self.lib.em_area_script_sin_0011E2A8(values[0], C.byref(out)) < 0: return -1
                    value = fbits(out.value)
                else:
                    value = scratch_math(self.elf, self.ram, self.spad, address, args, kinds, result)
                calls.append((name, args, value))
                if result == 'fo': values[-1][0] = number(value)
                else: values[-1][0] = value
                return 0
            value = env.on_call(name, args, mem)
            calls.append((name, args))
            if result in ('io', 'uo'): values[-1][0] = value
            return 0
        return fn

    def model_182F90(self, target):
        """00182F90 as its decomp C states: delta = target - A0 (VU),
        A0/B0/3B40 += delta (VU), 3B50 = C0."""
        m = self.mem
        a0 = [m.load(PLAYER+0xA0+4*i) for i in range(4)]
        delta = [vu(target[i], a0[i], -1) for i in range(4)]
        for base in (PLAYER+0xA0, PLAYER+0xB0, 0x70003B40):
            for i in range(4): m.save(base+4*i, vu(m.load(base+4*i), delta[i], 1))
        for i in range(4): m.save(0x70003B50+4*i, m.load(PLAYER+0xC0+4*i))


# ---------------------------------------------------------------- lockstep run
def quick_image(data, base, start):
    """Quick mode: clamp +0x0C durations (floats > 5) of the walked records."""
    data = bytearray(data)
    pc, seen = start, set()
    while pc not in seen and base <= pc < base+len(data):
        seen.add(pc)
        o = pc-base
        flags = struct.unpack_from('<I', data, o)[0]
        op = flags & 0xFFF
        if op in (0, 1, 2, 0x10):
            value = struct.unpack_from('<f', data, o+0xC)[0]
            if value > 5.0: struct.pack_into('<f', data, o+0xC, 5.0)
        if flags & 0x80000000: break
        pc = struct.unpack_from('<I', data, o+4)[0] if flags & 0x40000000 else pc+64
    return bytes(data)


def compare_state(o, n, owner, image_range, label):
    diffs = []
    for name, (base, size, compared) in REGIONS.items():
        buf = n.mem.buffers[name][2]
        for i in range(compared):
            if o.load(base+i, 1) != buf[i]: diffs.append((name, hex(base+i), o.load(base+i, 1), buf[i]))
    obuf = n.mem.buffers['owner'][2]
    for i in range(0x300):
        if 0x1F0 <= i < 0x220: continue
        if o.load(owner+i, 1) != obuf[i]: diffs.append(('owner', hex(owner+i), o.load(owner+i, 1), obuf[i]))
    h = n.host
    block = (signed(o.load(owner+0x1F0)), signed(o.load(owner+0x1F4)), o.load(owner+0x1F8),
             signed(o.load(owner+0x1FC, 1), 8), signed(o.load(owner+0x1FE, 2), 16),
             tuple(o.load(owner+0x200+4*i) for i in range(8)))
    native = (h.script.active, h.script.phase, h.script.pc, h.script.skip_phase, h.st_0E,
              tuple(fbits(v) for v in list(h.st_10)+list(h.st_20)))
    if block != native: diffs.append(('script', block, native))
    base, end = image_range
    for i in range(end-base):
        if o.load(base+i, 1) != n.image_buf[i]:
            diffs.append(('record', hex(base+i), o.load(base+i, 1), n.image_buf[i])); break
    assert not diffs, (label, diffs[:12])


def allowed_writes(owner, image_range):
    spans = [(b, b+c) for b, _, c in REGIONS.values()]
    spans += [(owner, owner+0x300), image_range, (STACK_TOP-0x10000, STACK_TOP),
              (TRACK, TRACK+16)]
    return spans


def run_scenario(case):
    label, entry, owner, image_range, scenario = case
    elf, ram, spad, lib = CONTEXT['elf'], CONTEXT['ram'], CONTEXT['spad'], CONTEXT['lib']
    base, end = image_range
    data = CONTEXT['synthetic'] if image_range == SYNTHETIC else ram[base:end]
    if not FULL and not scenario.get('keep_durations'): data = quick_image(data, base, entry)
    o = RamOracle(elf, ram, spad)
    o.write(base, data)
    env_o, env_n = Env(scenario), Env(scenario)
    ocalls = []
    install_oracle(o, env_o, ocalls, scenario.get('callbacks', ()))
    n = Native(lib, elf, ram, spad, owner, image_range, data, scenario, env_n)
    for address, value, size in scenario.get('init', ()):
        o.save(address, value, size); n.mem.save(address, value, size)
    o.save(TRACK, fbits(scenario.get('track_head', 6.0)))
    o.written.clear()
    o.run(0x1BA1A0, (owner+0x1F0, entry))
    assert lib.em_area_script_start(C.byref(n.host), entry) == 0
    spans = allowed_writes(owner, image_range)
    ticks, results, math = 0, [], set()
    limit = scenario.get('limit', 4000)
    while True:
        env_o.before_tick(o); env_n.before_tick(n.mem)
        del ocalls[:]; del n.calls[:]
        o.run(0x1BA1F0, (owner,))
        expected = signed(o.r[2])
        actual = lib.em_area_script_tick(C.byref(n.host))
        if actual == -1:
            assert n.host.faulted == 1 and n.host.fault_address == scenario.get('expect_fault'), (
                label, ticks, hex(n.host.fault_address), hex(n.host.fault_pc), ocalls, n.calls)
            return (label, ticks, 'fault', hex(n.host.fault_address), math)
        assert (expected, ocalls) == (actual, n.calls), (label, ticks, expected, actual,
                                                         ocalls, n.calls, hex(n.host.fault_address))
        compare_state(o, n, owner, image_range, (label, ticks))
        stray = [a for a in o.written if not any(lo <= a < hi for lo, hi in spans)]
        assert not stray, (label, ticks, [hex(a) for a in sorted(stray)[:8]])
        o.written.clear()
        math.update(c for c in n.calls if c[0] in ('w_0011E2A8', 'w_001B1470'))
        results.append(expected)
        ticks += 1
        if expected in (1, 3): break
        assert ticks < limit, (label, 'did not finish')
    assert not scenario.get('expect_fault'), (label, 'expected a fault')
    return (label, ticks, expected, None, math)


# ---------------------------------------------------------------- synthetic
SYNTHETIC = (0x01D00000, 0x01D02000)   # test-generated records, not game data


def rec(op, sub=0, flags=0, jump=0, f0c=0.0, w14=0, w18=0, w1c=0, v20=(0.,)*4, v30=(0.,)*4):
    return struct.pack('<IIIfIIII4f4f', op | flags, jump, sub, f0c, 0, w14, w18, w1c, *v20, *v30)


END, CONT, JUMP = 0x80000000, 0x20000000, 0x40000000
V1, V2 = (310., 290., 180., 1.), (330.5, 285.25, 200., 1.)


def synthetic_arena():
    base = SYNTHETIC[0]
    scripts, data = {}, bytearray()
    def script(name, records):
        scripts[name] = base+len(data)
        for r in records: data.extend(r)
    script('flags+euler', [
        rec(7, 0, w14=1), rec(4, 0, v20=V1), rec(4, 1, v20=V2), rec(4, 2, v20=V1),
        rec(4, 3, v20=V2), rec(4, 7, v20=V1), rec(4, 9, v20=V2), rec(4, 10, v20=V1),
        rec(6, 1, w14=0x40), rec(6, 3, w14=0x41, w18=0x37), rec(6, 4, w14=0x41, w18=0x37),
        rec(6, 5, w14=0x41), rec(6, 6, w14=0x42), rec(7, 6, END, w14=0x43, w18=0x21)])
    script('camera kinds', [
        rec(7, 13), rec(0, 3, v20=V1, v30=V2), rec(0, 4, f0c=3., v20=V2),
        rec(0, 7, f0c=3., v20=V1), rec(0, 9, v20=V2), rec(0, 10, f0c=3., v20=V1),
        rec(0x0D, 6), rec(0x0D, 8), rec(0x0D, 1), rec(0x0D, 4), rec(0x0D, 2), rec(0x0D, 7, f0c=0.),
        rec(0, 5, CONT, f0c=2., v20=V2, v30=V1), rec(7, 4, END)])
    script('owner moves', [
        rec(7, 1, w14=1), rec(1, 0, v30=V1), rec(1, 2, f0c=3., v30=V2), rec(1, 4, f0c=2., v30=V1),
        rec(1, 6, f0c=3., v30=V2), rec(1, 7, f0c=2., v30=V1), rec(1, 5, f0c=3., w14=2, v30=V2),
        rec(0x0A, 2), rec(0x0A, 4, f0c=2., w14=0x21, w1c=0x3C), rec(0x0A, 6),
        rec(0x0A, 7, f0c=3., w14=0x22), rec(0x0A, 1, f0c=0.5, w14=0x23, w1c=0x3C),
        rec(0x0A, 3), rec(7, 4, END)])
    script('fades', [
        rec(7, 2), rec(0x10, 0, w14=8), rec(0x10, 2), rec(0x10, 4), rec(0x10, 6, w14=16),
        rec(0x10, 7, w14=4), rec(0x10, 8), rec(0x10, 9, f0c=3.), rec(0x10, 5),
        rec(0x10, 1, w14=4), rec(0x10, 5), rec(0x10, 3, f0c=2.), rec(7, 4, END)])
    for sub in (9, 10, 11):
        script(f'stream enter {sub}', [rec(7, sub, w18=0x66), rec(2, f0c=2.), rec(7, 5, END, w14=0x44)])
    script('stream enter 12 immediate', [rec(7, 12, w14=1, w18=0x66), rec(7, 4, END)])
    script('enter 7 face', [rec(7, 7), rec(2, f0c=2.), rec(7, 4, END)])
    script('skippable', [rec(0x16), rec(7, 3), rec(2, f0c=30.), rec(2, f0c=30.),
                         rec(0x18), rec(0, 0, v20=V1, v30=V2), rec(7, 4, END)])
    here = base+len(data)
    script('jump', [rec(2, flags=JUMP, jump=here+128, f0c=1.), rec(0x7FF), rec(7, 4, END)])
    script('unported opcode', [rec(7, 0, w14=1), rec(0x14), rec(7, 4, END)])
    script('unadmitted kind', [rec(7, 0, w14=1), rec(0, 8, f0c=3.), rec(7, 4, END)])
    return scripts, bytes(data)


# ---------------------------------------------------------------- scenarios
SPAD_IDLE = [(0x70003B8D, 0, 1), (0x70003B8F, 1, 1), (0x70003B91, 0, 1), (0x70003B92, 0, 1),
             (0x28A9A0, 0, 2), (0x2821B4, 0, 4), (0x2821B0, 0, 4)]
ELEVATOR = 0x828050
PANEL_CALLBACKS = (0x157F60, 0x1575B0, 0x1580C0)


def scenarios():
    out = []
    def add(label, entry, owner=SYNTHETIC_OWNER, arena=OVERLAY, **s):
        s.setdefault('init', SPAD_IDLE)
        out.append((label, entry, owner, arena, s))
    # Truck camera preview (trigger 008251E0).
    add('truck 8292C0', 0x8292C0)
    add('truck 8292C0 waits 3B8F', 0x8292C0,
        init=SPAD_IDLE+[(0x70003B8F, 0, 1)], set={4: [(0x70003B8F, 1, 1)]})
    add('truck 8292C0 while scripted', 0x8292C0,
        init=SPAD_IDLE+[(0x70003B92, 1, 1)], set={3: [(0x70003B92, 0, 1)]})
    # Elevator refusal / powered (owner 00827B10).
    add('elevator refusal 82A990', 0x82A990)
    add('elevator powered 82A750', 0x82A750, callbacks=(ELEVATOR,))
    # Director beats (manager 008253F0).
    # 0x8294C0 waits (op06 sub2) on counter D_008107D8[0x3B] = D_00810813.
    for entry in (0x8294C0, 0x829A40, 0x829CC0):
        add(f'director {entry:X}', entry, frame_busy=2, set={40: [(0x810813, 1, 1)]},
            init=SPAD_IDLE+[(0x810813, 0, 1)])
        for skip in (3, 8, 40):
            add(f'director {entry:X} skip@{skip}', entry, skip_tick=skip)
    # The manager's fourth script; its op09 callbacks 0x825900/0x825920
    # (001DFE10 / 001DFE40, then return 1) are c_record workers here.
    add('director 829E80', 0x829E80, frame_busy=2, callbacks=(0x825900, 0x825920),
        callback_ticks=1)
    for skip in (4, 12):
        add(f'director 829E80 skip@{skip}', 0x829E80, skip_tick=skip,
            callbacks=(0x825900, 0x825920), callback_ticks=1)
    # Roger scripts (owner 008237E0 node).
    add('roger 8283D0', 0x8283D0, owner=ROGER)
    for skip in (6, 14, 30):
        add(f'roger 8283D0 skip@{skip}', 0x8283D0, owner=ROGER, skip_tick=skip)
    add('roger 828810', 0x828810, owner=ROGER, init=SPAD_IDLE+[(0x70003B8D, 3, 1)])
    add('roger 828A10', 0x828A10, owner=ROGER, init=SPAD_IDLE+[(0x70003B8D, 3, 1),
                                                              (0x282157, 1, 1)])
    add('roger 828990', 0x828990, owner=ROGER)
    # Panel scripts (owner 00159210 family; ELF arena).
    for entry in (0x246F20, 0x2477A0, 0x247BE0, 0x247DA0):
        add(f'panel {entry:X}', entry, arena=PANEL, callbacks=PANEL_CALLBACKS)
    # Pickup short programs (00219550 / 0015AFA0 owners): op07 sub13, the
    # 001B6EA0 take callback, op07 sub4.
    for entry in (0x248480, 0x2667E0):
        add(f'pickup {entry:X}', entry, arena=(entry, entry+0xC0), callbacks=(0x1B6EA0,),
            callback_ticks=1)
    # Test-generated records for the admitted sub-commands the level's own
    # scripts do not reach (same oracle, same comparisons).
    scripts, _ = synthetic_arena()
    for name, entry in scripts.items():
        extra = {}
        if name == 'unported opcode': extra = dict(expect_fault=0x24D880+4*0x14)
        if name == 'unadmitted kind': extra = dict(expect_fault=0x1B8FC0)
        if name == 'skippable':
            for skip in (2, 5, 9):
                add(f'synthetic {name} skip@{skip}', entry, arena=SYNTHETIC, skip_tick=skip,
                    keep_durations=1)
        add(f'synthetic {name}', entry, arena=SYNTHETIC, keep_durations=1,
            init=SPAD_IDLE+[(0x810758+0x41, 0x37, 1)], **extra)
    # Fail-stop: a missing worker or world pointer faults on first use.
    add('fault missing 001DD980', 0x8292C0, null_workers=('w_001DD980',),
        expect_fault=0x1DD980)
    add('fault missing 3B92', 0x8292C0, null_world=('spad3B92',), expect_fault=0x70003B92)
    return out


# ---------------------------------------------------------------- route captures
ROUTE = DECOMP/'build/s87/route'     # docs/FIRST_LEVEL_ROUTE.md (pad-only play)
ROUTE_OWNERS = {'trigger_r17': 0x7AA2A0, 'elevator_r19': 0x7AA880, 'roger_r8': 0x7A8830,
                'director_r12': 0x7A93F0, 'panel_r18': 0x7AA590}
CAMERA_WORKERS = ('w_0018CBD0', 'w_0018D7B0', 'w_0022EC30', 'c_record')
# (label, beat, seed, scripts, owner record patches).
#   seed: the route snapshot the beat was played from ('playable' for beat 00,
#     whose source is user slot 04: the first-control capture stands in).
#   scripts: (owner, entry, owner writes after its 001BA1F0 returns nonzero),
#     in the owner walk order (docs/ORIGINAL_FRAME_ORDER.md: r8 before r12).
#     Those writes are the owner's code, not the host's:
#       trigger 008251E0: D_00810792 = 1 (runtime 0x8253B0..0x8253B8);
#       director 008253F0: D_00810813 = 0x10 / 0x20 / 0xFF after the
#         001BA1F0 calls at runtime 0x8255B4 / 0x82569C / 0x82576C;
#       Roger 00823910: D_008107D8 |= 1 after the call at runtime 0x823AB0.
#   patches: 00157860 stores the message operand into the op0C record before
#     it starts the panel script (runtime 0x1577B8.. -> 0x246FB4, 0x157A34 ->
#     0x247834); the word is read from the beat's own end snapshot.
ROUTE_CASES = [
    ('truck preview 8292C0', '07_truck_preview', '06_hill_slide',
     [('trigger_r17', 0x8292C0, [(0x810792, 1, 'set')])], ()),
    ('elevator refusal 82A990', '02_elevator_refusal', '01_battery',
     [('elevator_r19', 0x82A990, [])], ()),
    ('elevator ride 82A750', '04_elevator_ride', '03_panel_power',
     [('elevator_r19', 0x82A750, [])], ()),
    ('panel 246F20 (no battery)', '00_panel_no_battery', 'playable',
     [('panel_r18', 0x246F20, [])], [(0x246FB4, '00_panel_no_battery')]),
    ('panel 2477A0', '03_panel_power', '02_elevator_refusal',
     [('panel_r18', 0x2477A0, [])], [(0x247834, '03_panel_power')]),
    ('panel 247BE0', '03_panel_power', '02_elevator_refusal',
     [('panel_r18', 0x247BE0, [])], ()),
    ('Roger 828990 + director 8294C0', '10_cage_roof_roger', '08_truck_crossing',
     [('roger_r8', 0x828990, []), ('director_r12', 0x8294C0, [(0x810813, 0x10, 'set')])], ()),
    ('director 829A40', '11_crevice_prompt', '10_cage_roof_roger',
     [('director_r12', 0x829A40, [(0x810813, 0x20, 'set')])], ()),
    ('director 829CC0', '13_east_tower', '12_crevice_jump',
     [('director_r12', 0x829CC0, [(0x810813, 0xFF, 'set')])], ()),
    ('Roger encounter 8283D0', '14_roger_encounter', '13_east_tower',
     [('roger_r8', 0x8283D0, [(0x8107D8, 1, 'or')])], ()),
]


def route_block(row, owner):
    s = bytes.fromhex(row[owner]['s1F0'])
    active, phase, pc = struct.unpack_from('<iiI', s)
    return active, phase & 0xFF, pc


def camera_state(m):
    return tuple(m.load(0x8101F0+4*i) for i in range(8))+tuple(m.load(0x8105D0+4*i) for i in range(8))


class RouteMem(NativeMem):
    """One canonical storage shared by every script of a capture."""
    def __init__(self, ram, spad, owners):
        super().__init__(ram, spad, owners[0])
        for i, o in enumerate(owners[1:]):
            self.buffers[f'owner{i+1}'] = (o, 0x300, (C.c_ubyte*0x300).from_buffer_copy(ram[o:o+0x300]))


class RouteEnv:
    """Worker results for the replay. c_record and 00182BF0 answer what the
    capture's script pointer shows (the next row); the rest are the lockstep
    environment's constants."""
    def __init__(self):
        self.s, self.camera_call, self.callback, self.predicate = {}, None, 0, 1

    def on_call(self, name, args, mem):
        if name in CAMERA_WORKERS: self.camera_call = camera_state(mem)
        if name == 'c_record': return self.callback
        if name == 'w_00182BF0': return self.predicate
        if name == 'w_001CA700': return 1
        if name == 'w_001C6120': return TRACK
        return 0


def route_seed(ram, spad, row, patches):
    """The seed snapshot with the recorded fields of the start row."""
    ram, spad = bytearray(ram), bytearray(spad)
    spad[0x3B8C:0x3B94] = bytes.fromhex(row['spad'])
    ram[0x8101E4:0x8101E8] = bytes.fromhex(row['cam_mode'])
    for address, key in ((0x8101F0, 'cam_eye'), (0x810200, 'cam_tgt'), (0x8105D0, 'eye'),
                         (0x8105E0, 'tgt'), (0x810350, 'pos'), (0x810360, 'hip')):
        struct.pack_into('<3f', ram, address, *row[key])
    struct.pack_into('<f', ram, 0x810374, row['yaw'])
    ram[0x810790:0x810794] = bytes.fromhex(row['story790'])
    ram[0x8107D8:0x810818] = bytes.fromhex(row['d2'])
    ram[0x2821B0:0x2821C0] = bytes.fromhex(row['msg'])
    ram[0x28A9A0:0x28A9B0] = bytes.fromhex(row['fade'])
    for address, beat in patches:
        word = struct.unpack_from('<I', (ROUTE/beat/'eeMemory.bin').read_bytes(), address)[0]
        struct.pack_into('<I', ram, address, word)
    return bytes(ram), bytes(spad)


def capture_case(case):
    """Replay the host over one route beat. Per frame f (row f = the main-loop
    top, row f+1 = after that frame):

    Inputs from row f, owned by code outside the scripts: 3B8F (player ready),
    3B91 2 (001AE6B0's skip promotion), the message block (001FCA10), the
    transition substate 0x28A9A0, the player hip +0xB0 (animation), and the
    player position/yaw and camera until the scripts own them (player: from
    the first row with 3B8F = 1, released while an op09 callback carries it;
    camera: from the first host write, released to workers by 0018CBD0 /
    0018D7B0 / 0022EC30 / a callback, and to the game camera when E4 = 0).
    Inputs derived from the capture's script pointer, because their state is
    not in the trace (so those records' durations are not claims): the
    00182BF0 result (op16), the op09 callback result and the phase word it
    owns, the animation-done bit (op0A/3, op15), stream ready 0x8106F4
    (op07/9..12 phase 3) and the playback cursor +0x74 (op0D/0).

    Compared with row f+1: each script's block (active, phase, pc); 3B8D,
    3B91, 3B92; the camera byte E4; while the host owns the camera, the eye
    and target at 0x8101F0/0x810200 and 0x8105D0/0x8105E0 (the trace rounds
    to 5 decimals; for |v| >= 128 that identifies the float exactly); while
    the host owns the player, position and yaw; the message block (except
    001FCA10's own phase 1->2 and teardown to zero); D_00810790..93 and
    D_008107D8..0x810817; and the letterbox block's start (3) / leave (2)
    against the host's 001AEB60 / 001AEBA0 calls."""
    label, beat, seed, scripts, patches = case
    lib, elf = CONTEXT['lib'], CONTEXT['elf']
    import json
    rows = json.loads((ROUTE/beat/'trace.json').read_text())['rows']
    if seed == 'playable':
        ram, spad = CONTEXT['ram'], CONTEXT['spad']
    else:
        ram = (ROUTE/seed/'eeMemory.bin').read_bytes()
        spad = (ROUTE/seed/'scratchpad.bin').read_bytes()
    starts = {}
    for owner, entry, _ in scripts:
        first = next(i for i, r in enumerate(rows) if route_block(r, owner)[0] == 1
                     and entry <= route_block(r, owner)[2] < entry+0x400
                     and route_block(rows[i-1], owner)[0] != 1)
        # Phase 0 at the entry: started without a tick (001BA1A0 only), so
        # the first tick is this frame; otherwise it also ticked the frame before.
        starts[owner, entry] = first if route_block(rows[first], owner)[1:] == (0, entry) else first-1
    frame = min(starts.values())
    ram, spad = route_seed(ram, spad, rows[frame], patches)
    mem = RouteMem(ram, spad, [ROUTE_OWNERS[s[0]] for s in scripts])
    env = RouteEnv()
    hosts = []
    for owner, entry, finish in scripts:
        base, end = OVERLAY if entry >= OVERLAY[0] else PANEL
        n = Native(lib, elf, ram, spad, ROUTE_OWNERS[owner], (base, end), ram[base:end], {}, env,
                   mem=mem)
        hosts.append(dict(owner=owner, entry=entry, finish=finish, n=n, base=base, end=end,
                          started=False, done=False))
    m = mem
    camera, player, carried = 'external', False, False
    checks, diffs = {}, []
    def check(kind, ok, detail):
        checks[kind] = checks.get(kind, 0)+1
        if not ok: diffs.append((frame, kind, detail))
    def vec(address): return [round(number(m.load(address+4*i)), 5) for i in range(3)]
    while not all(h['done'] for h in hosts):
        assert frame+1 < len(rows), (label, 'capture ended before the scripts')
        row, nxt = rows[frame], rows[frame+1]
        sp = bytes.fromhex(row['spad'])
        m.save(0x70003B8F, sp[3], 1)
        if sp[5] == 2 and m.load(0x70003B91, 1) == 1: m.save(0x70003B91, 2, 1)
        for i in range(0, 16, 4): m.save(0x2821B0+i, struct.unpack_from('<I', bytes.fromhex(row['msg']), i)[0])
        m.save(0x28A9A0, struct.unpack_from('<H', bytes.fromhex(row['fade']))[0], 2)
        if sp[3] == 1: player = True
        if not player or carried:
            for i, x in enumerate(row['pos']): m.save(0x810350+4*i, fbits(x))
            m.save(0x810374, fbits(row['yaw']))
        for i, x in enumerate(row['hip']): m.save(0x810360+4*i, fbits(x))
        if camera == 'external':
            for address, key in ((0x8101F0, 'cam_eye'), (0x810200, 'cam_tgt'), (0x8105D0, 'eye'),
                                 (0x8105E0, 'tgt')):
                for i, x in enumerate(row[key]): m.save(address+4*i, fbits(x))
        before_camera = camera_state(m)
        before_player = [m.load(0x810350+4*i) for i in range(3)]+[m.load(0x810374)]
        names = []
        for h in hosts:
            n = h['n']
            if not h['started']:
                if frame < starts[h['owner'], h['entry']]: continue
                assert lib.em_area_script_start(C.byref(n.host), h['entry']) == 0
                h['started'] = True
            if h['done']: continue
            now, after = route_block(row, h['owner']), route_block(nxt, h['owner'])
            pc = n.host.script.pc
            leaving = after[2] != pc or after[0] != 1
            env.callback = 1 if after[2] != now[2] or after[0] != 1 else 0
            env.predicate = 0 if after[2] != now[2] else 1
            if h['base'] <= pc < h['end']:
                record = bytes(n.image_buf[pc-h['base']:pc-h['base']+12])
                op, sub = struct.unpack_from('<I', record)[0] & 0xFFF, struct.unpack_from('<I', record, 8)[0]
                if op == 0x07 and 9 <= sub <= 12 and n.host.script.phase & 0xFF == 3:
                    m.save(0x8106F4, 1 if leaving else 2, 1)
                if op == 0x0D and sub == 0:
                    m.save(0x810254, m.load(0x810258) if leaving else 0)
                if (op == 0x0A and sub == 3) or op == 0x15:
                    m.save(PLAYER+0x200, m.load(PLAYER+0x200) & ~0x1000 | (0x1000 if leaving else 0))
            del n.calls[:]
            env.camera_call = None
            result = lib.em_area_script_tick(C.byref(n.host))
            calls = [c[0] for c in n.calls]
            names += calls
            if 'c_record' in calls and result == 0 and n.host.script.pc == after[2]:
                n.host.script.phase = after[1]
            if env.camera_call is not None:
                if camera_state(m) != env.camera_call: camera = 'host'
                elif camera == 'host': camera = 'worker'
            s = n.host.script
            native = (s.active, s.phase & 0xFF, s.pc)
            check('block', native == after or (native[0] <= 0 and after[0] in (native[0], 0)),
                  (h['owner'], result, native, after, calls))
            assert result != -1, (label, frame, 'fault', hex(n.host.fault_address), hex(n.host.fault_pc))
            if result in (1, 3):
                h['done'] = True
                for address, value, how in h['finish']:
                    m.save(address, (m.load(address, 1) | value) if how == 'or' else value, 1)
        if 'c_record' in names: carried = True
        if m.load(0x8101E4, 1) == 0: camera = 'external'
        elif camera_state(m) != before_camera and env.camera_call is None: camera = 'host'
        nsp = bytes.fromhex(nxt['spad'])
        for address, i in ((0x70003B8D, 1), (0x70003B91, 5), (0x70003B92, 6)):
            check('3B8D/91/92', m.load(address, 1) == nsp[i], (hex(address), m.load(address, 1), nsp[i]))
        check('E4', m.load(0x8101E4, 1) == bytes.fromhex(nxt['cam_mode'])[0],
              (m.load(0x8101E4, 1), nxt['cam_mode']))
        if camera == 'host':
            for address, key in ((0x8101F0, 'cam_eye'), (0x810200, 'cam_tgt'), (0x8105D0, 'eye'),
                                 (0x8105E0, 'tgt')):
                check('camera', vec(address) == nxt[key], (key, vec(address), nxt[key]))
        after_player = [m.load(0x810350+4*i) for i in range(3)]+[m.load(0x810374)]
        if (player and not carried) or after_player != before_player:
            yaw = round(number(m.load(0x810374)), 5)
            check('player', vec(0x810350) == nxt['pos'] and yaw == nxt['yaw'],
                  (vec(0x810350), nxt['pos'], yaw, nxt['yaw']))
        mine = bytes(m.load(0x2821B0+i, 1) for i in range(16))
        theirs, earlier = bytes.fromhex(nxt['msg']), bytes.fromhex(row['msg'])
        service = ((mine[:4]+mine[8:] == theirs[:4]+theirs[8:] and mine[4:8] == earlier[4:8] == b'\1\0\0\0'
                    and theirs[4] == 2) or (mine[4] == 2 and theirs == bytes(16)))
        check('message', mine == theirs or service, (mine.hex(), theirs.hex()))
        check('flags', bytes(m.load(0x810790+i, 1) for i in range(4)).hex() == nxt['story790'],
              nxt['story790'])
        check('progress', bytes(m.load(0x8107D8+i, 1) for i in range(0x40)).hex() == nxt['d2'], nxt['d2'])
        bars = '03' if 'w_001AEB60' in names else '02' if 'w_001AEBA0' in names else None
        if bars or (nxt['screen'][:2] != row['screen'][:2] and nxt['screen'][:2] in ('02', '03')):
            check('letterbox', bars == nxt['screen'][:2], (row['screen'][:2], nxt['screen'][:2], names))
        frame += 1
    assert not diffs, (label, len(diffs), diffs[:6])
    frames = frame-min(starts.values())
    assert checks['block'] >= frames, (label, checks)
    return label, frames, checks


# ---------------------------------------------------------------- sine sweep
def sine_values():
    """0011E2A8 arguments: every ease argument pi*t - pi/2 (t = k/d, the
    handlers' rounding div, truncating mul, guard-bit sub) for durations
    1..400 in full mode (the level scripts use 180, 80 and the director's
    durations; quick mode takes 5, 80, 90, 120, 180), the 0x3FC90FD0 bucket
    and range boundaries, and, in full mode, 20,000 random magnitudes of both
    signs up to 0x4016CBE3."""
    import random
    from test_pose_transition_reference import add as ee_add
    out = set()
    pi, half = 3.14159274101257324, 1.57079637050628662
    for d in (range(1, 401) if FULL else (5, 80, 90, 120, 180)):
        for k in range(d+1):
            out.add(bits(ee_add(fp(pi*number(bits(k/d))), -half)) & 0xffffffff)
    edges = list(range(0x3FC90FC0, 0x3FC90FF0))+list(range(0x3F490FD0, 0x3F490FE0))
    edges += [0, 1, 0x31FFFFFF, 0x32000000, 0x3E999999, 0x3E99999A, 0x3F480000, 0x3F480001,
              0x4016CBE3]
    rng = random.Random(7)
    edges += [rng.randrange(0, 0x4016CBE4) for _ in range(20000 if FULL else 300)]
    for b in edges: out.update((b, b | 0x80000000))
    return sorted(out)


def sine_chunk(values):
    o = RamOracle(CONTEXT['elf'], CONTEXT['ram'], CONTEXT['spad'])
    lib, misses = CONTEXT['lib'], []
    for x in values:
        o.f[12] = x; o.run(0x11E2A8)
        out = C.c_float()
        ok = lib.em_area_script_sin_0011E2A8(number(x), C.byref(out)) == 0
        if not ok or fbits(out.value) != o.f[0] & 0xffffffff:
            misses.append((hex(x), hex(o.f[0] & 0xffffffff), ok and hex(fbits(out.value))))
    return misses


# ---------------------------------------------------------------- build
LAYOUT_PROBE = r'''
#include <stddef.h>
#include <stdio.h>
#include "game/em_area_script.h"
int main(void){printf("%zu %zu %zu %zu %zu\n",sizeof(EmAreaScriptWorld),
 sizeof(EmAreaScriptWorkers),sizeof(EmAreaScript),offsetof(EmAreaScript,image),
 offsetof(EmAreaScriptWorld,d24D8F0_count));return 0;}
'''


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    lib = BUILD/'area_script.dylib'
    sources = ['src/game/em_area_script.c', 'src/game/em_script.c', 'src/game/em_message_service.c',
               'src/game/em_interaction_frame.c', 'src/game/em_interaction_cinematic.c',
               'src/game/em_cinematic_playback.c', 'src/game/em_cinematic_camera.c',
               'src/game/em_camera_rotation.c',
               # binding candidates checked against the original math results
               'src/game/em_item_sdk_math.c', 'src/game/em_interaction_scan.c',
               'src/game/em_item_trail.c', 'src/game/em_fan_original.c']
    subprocess.run(['cc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', *sources, '-o', str(lib)], cwd=ROOT, check=True)
    probe = BUILD/'layout.c'; probe.write_text(LAYOUT_PROBE)
    subprocess.run(['cc', '-std=c11', '-Isrc', str(probe), '-o', str(BUILD/'layout')], cwd=ROOT,
                   check=True)
    sizes = tuple(int(x) for x in subprocess.run([str(BUILD/'layout')], capture_output=True,
                                                 text=True, check=True).stdout.split())
    mine = (C.sizeof(World), C.sizeof(Workers), C.sizeof(Host), Host.image.offset,
            World.d24D8F0_count.offset)
    assert sizes == mine, ('ctypes layout differs from C', sizes, mine)
    native = C.CDLL(str(lib))
    native.em_area_script_init.argtypes = [C.POINTER(Host), C.POINTER(Image), C.POINTER(World),
                                           C.POINTER(Workers)]
    native.em_area_script_start.argtypes = [C.POINTER(Host), C.c_uint32]
    native.em_area_script_tick.argtypes = [C.POINTER(Host)]
    native.em_message_op0c.argtypes = [C.c_void_p, P8, C.POINTER(C.c_ubyte)]
    native.em_area_script_sin_0011E2A8.argtypes = [C.c_float, C.POINTER(C.c_float)]
    native.em_item_sdk_sine.argtypes = [C.c_float]
    native.em_item_sdk_sine.restype = C.c_float
    native.em_fan_original_wrap_001B1470.argtypes = [C.c_float]
    native.em_fan_original_wrap_001B1470.restype = C.c_float
    return native


def check_bindings(lib, math):
    """Binding candidates for the pure SDK workers, checked against the
    original results the scripts produced. 001B1470 must match
    em_fan_original_wrap_001B1470 (asserted). w_0011E2A8 is bound to
    em_area_script_sin_0011E2A8 in the lockstep, so every value here was
    already asserted equal. em_item_sdk_sine models add.s as plain
    truncation; the route captures of the truck preview and director beat 0
    eases reject that model (docs/AREA_SCRIPT.md section 4), so its
    mismatches are reported, not asserted: it is not a w_0011E2A8 binding."""
    wraps = sines = sine_misses = 0
    for name, (arg,), value in sorted(math):
        if name == 'w_001B1470':
            got = fbits(lib.em_fan_original_wrap_001B1470(number(arg)))
            assert got == value, (name, hex(arg), hex(value), hex(got))
            wraps += 1
        else:
            sines += 1
            sine_misses += fbits(lib.em_item_sdk_sine(number(arg))) != value
    return wraps, sines, sine_misses


CONTEXT = {}


def main():
    elf = (DECOMP/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA, 'unexpected boot ELF'
    ram = (REF/'playable_ee.bin').read_bytes()
    spad = bytearray(0x4000)
    CONTEXT.update(elf=elf, ram=ram, spad=bytes(spad), lib=build(),
                   synthetic=synthetic_arena()[1].ljust(SYNTHETIC[1]-SYNTHETIC[0], b'\0'))
    cases = scenarios()
    results = parallel_map(run_scenario, cases, cost=lambda c: 0 if 'fault' in c[0] else 1)
    ticks, math = 0, set()
    for label, count, outcome, fault, seen in results:
        ticks += count; math |= seen
        if FULL: print(f'  {label}: {count} ticks -> {outcome}{" " + fault if fault else ""}')
    wraps, sines, misses = check_bindings(CONTEXT['lib'], math)
    print(f'binding check: 001B1470 = em_fan_original_wrap_001B1470 on {wraps} values; '
          f'em_item_sdk_sine differs from original 0011E2A8 on {misses} of {sines} values '
          f'(not a w_0011E2A8 binding)')
    values = sine_values()
    chunks = [values[i::8] for i in range(8)]
    misses = [m for chunk in parallel_map(sine_chunk, chunks) for m in chunk]
    assert not misses, ('em_area_script_sin_0011E2A8 differs from original 0011E2A8', len(misses),
                        misses[:6])
    print(f'sine sweep: em_area_script_sin_0011E2A8 = original 0011E2A8 on {len(values):,} arguments')
    for case in ROUTE_CASES:
        assert (ROUTE/case[1]/'trace.json').is_file(), ('route capture missing', case[1])
    captured = parallel_map(capture_case, ROUTE_CASES,
                            cost=lambda c: len((ROUTE/c[1]/'trace.json').read_bytes()))
    frames, compared = 0, {}
    for label, count, checks in captured:
        frames += count
        for kind, n in checks.items(): compared[kind] = compared.get(kind, 0)+n
        if FULL: print(f'  route {label}: {count} frames, ' +
                       ', '.join(f'{k} {v}' for k, v in sorted(checks.items())))
    print(f'route captures: {len(captured)} scripted beats, {frames:,} frames, 0 differences; '
          'compared ' + ', '.join(f'{k} {v:,}' for k, v in sorted(compared.items())))
    banner(f'{len(cases)} scripts/variants', f'{ticks:,} lockstep ticks',
           'level durations ' + ('captured' if FULL else 'clamped to 5'),
           f'{len(captured)} route beats')
    print('area script host matches original 001BA1F0 + ftab_0024D880 handlers and the route captures')


if __name__ == '__main__':
    sys.exit(main())
