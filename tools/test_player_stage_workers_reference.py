#!/usr/bin/env python3
"""Execute the original player stage workers and compare em_player_stage_workers.c.

docs/PLAYER_STAGE_WORKERS.md. The user's pinned ELF (and, for the captured
cases, the captured AREA11 EE RAM images) supplies every instruction and
table; none are embedded here. The shared bounded interpreter
(player_callback_oracle.PlayerCallbackOracle -> test_point_light_reference.Oracle)
runs the instructions; this file wraps it so that every COP1 instruction goes
through tools/ee_float_model.py (docs/EE_FLOAT_MODEL.md) instead of the
shared interpreter's truncating arithmetic, and runs its own fetch loop to
record every executed original instruction address.

Executed, unmodified (never hooked):
  0021C440 reaction with 0021BB00, 0021BC40, 0021C200, 0021C270, 0021C350,
           0021C3F0, 0021D4E0, 0021D640, 0021D6C0, 0017C370, 001B1470,
           copy_qw4 (00102958)
  0015D100 drain with 001B0070 and 0021BB00;  0015D000 heartbeat
  00183090 commit;  00182B30 / 00182D70 / 00174A50 (the 0015B130 prelude)
  001C64F0 anim_advance_time with float_to_int (001281C0 / 001278C0), and
           over captured RAM also anim_clip_resolve (001C8480 / 001C6120)
  0015B530 (+4 = 4 handler);  0011A070 (argument decode, see below)

Hooked boundaries, scripted per case and recorded on both sides (order and
arguments): 001FBD50 sound, 001B61C0 rumble, 001EFE00, 001F00A0 (returns a
scripted effect record whose bytes are compared afterwards), 001F0060,
0011E620 atan2 (the same host model on both sides), 0017B490, 001749A0,
0015C9D0, 001D0C70, bone_init_default_2, anim_clip_init, anim_clip_resolve
(synthetic cases), 001C8710, 001C87C0, anim_sample_bones, and 0015B530's
seven routines. Every case compares all 0x320 record bytes, the globals
(0x70003B8F, 0x70003A20, D_008106F1, D_00810707, ...), the return value and
the callee sequence. Every reachable instruction of every executed original
function must be executed by the default run (asserted), and every native
routine must fault (-1) with nothing written when any worker it can reach
is missing.

EM_TEST_FULL=1 runs the exhaustive random sweep.
"""
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from player_callback_oracle import PlayerCallbackOracle, ELF_SHA256  # noqa: E402
import ee_float_model as M  # noqa: E402
import reference_mode  # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
REFERENCE = DECOMP / 'build/startup-reference'
ROUTE = DECOMP / 'build/s87/route'
LANE = ROOT / 'build/b5-player-stage-workers'
RETURN, STACK = 0xBADF00D, 0x7F0000
ELF = None                # the pinned boot ELF (main sets it)
RATES = None              # EmPlayerClipRates loaded from the exporter's output (main)

ACTOR = 0x680000          # synthetic record
LINK20 = 0x6A0000         # synthetic *(p+20) object
LINK1C = 0x6B0000         # synthetic *(p+1C) object
RECORD = 0x6C0000         # synthetic 001F00A0 effect record
HEADERS = 0x6D0000        # synthetic clip headers, 0x100 apart by clip id
SKELETON = 0x6E0000       # synthetic *(p+110)
PLAYER = 0x8102B0         # the player record in the captured RAM

SPAD_3B8D, SPAD_3B8F, SPAD_3A20 = 0x70003B8D, 0x70003B8F, 0x70003A20
D_AREA, D_6F1, D_6C8, D_701, D_770, D_83C, D_707, D_C7E = (
    0x810700, 0x8106F1, 0x8106C8, 0x810701, 0x810770, 0x81083C, 0x810707, 0x810C7E)
D_HEADER = 0x275BF8
D_CB6, D_6B3 = 0x810CB6, 0x8106B3

REACTION, DRAIN, HEARTBEAT, COMMIT = 0x21C440, 0x15D100, 0x15D000, 0x183090
CHECK, NOTIFY, ROW, ADVANCE, MAJOR4, STOP = 0x182B30, 0x182D70, 0x174A50, 0x1C64F0, 0x15B530, 0x11A070
SOUND, CUE, EFE00, F00A0, F0060, ATAN2 = 0x1FBD50, 0x1B61C0, 0x1EFE00, 0x1F00A0, 0x1F0060, 0x11E620
LOOKUP, REQUEST, C9D0, D0C70, BONE_INIT, CLIP_INIT = 0x17B490, 0x1749A0, 0x15C9D0, 0x1D0C70, 0x1C63E0, 0x1C67E0
RESOLVE, S8710, S87C0, SAMPLE = 0x1C8480, 0x1C8710, 0x1C87C0, 0x1C8D50
MEMSET, SPU_CMD = 0x121A28, 0x1157F0
MAJOR4_ROUTINES = (0x182DF0, 0x1837A0, 0x1837B0, 0x162DB0, 0x163B40, 0x1838B0, 0x183910)

# The original functions this file executes (their instructions are counted
# for the coverage assertion; sizes from the decomp's FUNCTIONS.csv).
EXECUTED = {
    0x21C440: 'func_0021C440', 0x21BB00: 'func_0021BB00', 0x21BC40: 'func_0021BC40',
    0x21C200: 'func_0021C200', 0x21C270: 'func_0021C270', 0x21C350: 'func_0021C350',
    0x21C3F0: 'func_0021C3F0', 0x21D4E0: 'func_0021D4E0', 0x21D640: 'func_0021D640',
    0x21D6C0: 'func_0021D6C0', 0x17C370: 'func_0017C370', 0x1B1470: 'func_001B1470',
    0x15D100: 'func_0015D100', 0x1B0070: 'func_001B0070', 0x15D000: 'func_0015D000',
    0x183090: 'func_00183090', 0x182B30: 'func_00182B30', 0x182D70: 'func_00182D70',
    0x174A50: 'func_00174A50', 0x1C64F0: 'anim_advance_time', 0x1281C0: 'float_to_int',
    0x1278C0: 'func_001278C0', 0x15B530: 'func_0015B530',
}

LIBC = C.CDLL(None)
LIBC.atan2f.argtypes = [C.c_float, C.c_float]
LIBC.atan2f.restype = C.c_float


def fbits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def fnum(word):
    return struct.unpack('<f', struct.pack('<I', word & 0xFFFFFFFF))[0]


def s16(value):
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def s32(value):
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value >> 31 else value


# ------------------------------------------------------------------ the oracle

class EE(PlayerCallbackOracle):
    """The shared interpreter with COP1 through ee_float_model and its own
    fetch loop (executed-address recording; branch-likely forms)."""

    def __init__(self, elf, ram=None):
        super().__init__(elf, b'')
        self.calls = {}           # only this file's hooks
        self.ram = ram
        self.log = []
        self.pcs = set()
        self.eacc = 0
        self.lo = self.hi = 0

    def load(self, address, size=4):
        if self.ram is not None and address not in self.mem and not 0x100000 <= address < 0x275B00 \
                and address + size <= len(self.ram):
            return int.from_bytes(self.ram[address:address + size], 'little')
        return super().load(address, size)

    def plain(self, word):
        r, f = self.r, self.f
        op = word >> 26
        if op == 17:
            rs, fn = word >> 21 & 31, word & 63
            ft, fs, fd = word >> 16 & 31, word >> 11 & 31, word >> 6 & 31
            if rs == 16:
                a, b = f[fs], f[ft]
                if fn == 0: f[fd] = M.ee_add(a, b)
                elif fn == 1: f[fd] = M.ee_sub(a, b)
                elif fn == 2: f[fd] = M.ee_mul(a, b)
                elif fn == 3: f[fd] = M.ee_div(a, b)
                elif fn == 6: f[fd] = M.ee_mov(a)
                elif fn == 7: f[fd] = M.ee_neg(a)
                elif fn == 36: f[fd] = M.ee_cvt_w_s(a)
                elif fn == 24: self.eacc = M.ee_adda(a, b)
                elif fn == 25: self.eacc = M.ee_suba(a, b)
                elif fn == 26: self.eacc = M.ee_mula(a, b)
                elif fn == 28: f[fd] = M.ee_madd(self.eacc, a, b)
                elif fn == 29: f[fd] = M.ee_msub(self.eacc, a, b)
                elif fn == 50: self.condition = bool(M.ee_c_eq(a, b))
                elif fn == 52: self.condition = bool(M.ee_c_lt(a, b))
                elif fn == 54: self.condition = bool(M.ee_c_le(a, b))
                else: raise AssertionError(('COP1.S not modelled', fn))
                return
            if rs == 20 and fn == 32:
                f[fd] = M.ee_cvt_s_w(f[fs] & 0xFFFFFFFF)
                return
            if rs in (0, 4):
                return super().plain(word)
            raise AssertionError(('COP1', rs, fn))
        if op == 0:
            fn, rs, rt, rd = word & 63, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
            if fn in (10, 11):                      # movz / movn
                if (r[rt] == 0) == (fn == 10): r[rd] = r[rs]
                r[0] = 0
                return
            if fn in (24, 25):                      # mult / multu (EE: rd = LO too)
                x, y = (s32(r[rs]), s32(r[rt])) if fn == 24 else (r[rs] & 0xFFFFFFFF, r[rt] & 0xFFFFFFFF)
                product = x * y
                self.lo, self.hi = product & 0xFFFFFFFF, (product >> 32) & 0xFFFFFFFF
                if rd: r[rd] = self.lo
                r[0] = 0
                return
            if fn in (16, 18):                      # mfhi / mflo
                r[rd] = self.hi if fn == 16 else self.lo
                r[0] = 0
                return
            if fn == 56:                            # dsll
                r[rd] = ((r[rt] & 0xFFFFFFFFFFFFFFFF) << (word >> 6 & 31)) & 0xFFFFFFFFFFFFFFFF
                r[0] = 0
                return
            if fn in (4, 6, 7):                     # sllv / srlv / srav
                shift, value = r[rs] & 31, r[rt] & 0xFFFFFFFF
                if fn == 4: r[rd] = (value << shift) & 0xFFFFFFFF
                elif fn == 6: r[rd] = value >> shift
                else: r[rd] = (s32(value) >> shift) & 0xFFFFFFFF
                r[0] = 0
                return
        if op in (55, 63):                          # ld / sd
            address = (self.r[word >> 21 & 31] + s16(word)) & 0xFFFFFFFF
            rt = word >> 16 & 31
            if op == 55: self.r[rt] = self.load(address, 8)
            else: self.save(address, self.r[rt] & 0xFFFFFFFFFFFFFFFF, 8)
            self.r[0] = 0
            return
        super().plain(word)

    def run(self, entry, args=(), floats=(), stop=RETURN):
        self.r[31] = RETURN
        self.r[29] = STACK
        for i, value in enumerate(args): self.r[4 + i] = value & 0xFFFFFFFF
        for i, value in enumerate(floats): self.f[12 + i] = value
        pc = entry
        for _ in range(200000):
            if pc == stop: return self.r[2] & 0xFFFFFFFF
            self.pcs.add(pc)
            word = self.load(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            offset = s16(word) * 4
            branch = None
            if op in (2, 3):
                target = (pc & 0xF0000000) | (word & 0x3FFFFFF) * 4
                if op == 3: self.r[31] = pc + 8
                self.pcs.add(pc + 4)
                self.plain(self.load(pc + 4))
                if target in self.calls:
                    self.calls[target](self)
                    pc += 8
                else:
                    pc = target
                continue
            likely = False
            if op in (4, 5, 20, 21):
                taken = ((self.r[rs] & 0xFFFFFFFF) == (self.r[rt] & 0xFFFFFFFF)) == (op in (4, 20))
                likely = op in (20, 21)
            elif op in (6, 7, 22, 23):
                value = s32(self.r[rs])
                taken = value <= 0 if op in (6, 22) else value > 0
                likely = op in (22, 23)
            elif op == 1:
                value = s32(self.r[rs])
                taken = value < 0 if rt in (0, 2) else value >= 0
                likely = rt in (2, 3)
                assert rt in (0, 1, 2, 3), ('REGIMM', rt)
            elif op == 17 and rs == 8:
                taken = self.condition == bool(rt & 1)
                likely = bool(rt & 2)
            elif op == 0 and word & 63 == 8:
                branch, taken = self.r[rs] & 0xFFFFFFFF, True
            else:
                try: self.plain(word)
                except AssertionError as error: raise AssertionError(hex(pc), error) from error
                pc += 4
                continue
            if branch is None: branch = pc + 4 + offset if taken else pc + 8
            if likely and not taken:
                pc += 8
                continue
            self.pcs.add(pc + 4)
            self.plain(self.load(pc + 4))
            pc = branch
        raise AssertionError(('original routine did not return', hex(entry)))


def reachable(elf, start, size):
    """Instruction addresses reachable from `start` inside [start, start+size)."""
    end, seen, todo = start + size, set(), [start]
    word = lambda a: int.from_bytes(elf[a - 0x100000 + 0x300:a - 0x100000 + 0x304], 'little')
    while todo:
        pc = todo.pop()
        while start <= pc < end and pc not in seen:
            seen.add(pc)
            w = word(pc)
            op, rs, rt = w >> 26, w >> 21 & 31, w >> 16 & 31
            target = pc + 4 + s16(w) * 4
            if op == 0 and w & 63 == 8:              # jr: the delay slot, then out
                seen.add(pc + 4)
                break
            if op == 2:                              # j (a tail jump out)
                seen.add(pc + 4)
                break
            conditional = op in (1, 4, 5, 6, 7, 20, 21, 22, 23) or (op == 17 and rs == 8)
            if op == 4 and rs == 0 and rt == 0:      # b
                seen.add(pc + 4)
                pc = target
                continue
            if conditional:
                seen.add(pc + 4)
                todo.append(target)
                pc += 8
                continue
            pc += 4
    return seen


# ------------------------------------------------------------------ native side

class LiveActor(C.Structure):
    _fields_ = [('bytes', C.c_uint8 * 0x320), ('link_owner', C.c_void_p), ('link_prev', C.c_void_p),
                ('link_flags', C.c_uint8), ('link_type', C.c_uint8)]


class StageScene(C.Structure):
    _fields_ = [('spad3B8D', C.c_uint8), ('spad3B8F', C.c_uint8), ('area', C.c_uint8),
                ('busy', C.c_uint8), ('d8106F1', C.POINTER(C.c_uint8)), ('d810CB6', C.POINTER(C.c_uint8))]


class Globals(C.Structure):
    _fields_ = [('d8106C8', C.c_int32), ('d810701', C.c_uint8), ('d810770', C.c_uint8),
                ('d81083C', C.c_uint8), ('d810C7E', C.c_uint8), ('spad3A20', C.c_uint32),
                ('d810707', C.POINTER(C.c_uint8))]


# D_008106F1, D_00810CB6 and D_00810707 are pointers at one canonical byte
# each (em_player_floor.h / em_player_stage_workers.h): the native side keeps
# one cell per byte and the structs point at it.
CANONICAL = ('d8106F1', 'd810CB6', 'd810707')


def canonical_struct(cls, values, cells, drop=()):
    out = cls()
    for key, value in values.items():
        if key in CANONICAL:
            cells[key] = C.c_uint8(value)
            if key not in drop: setattr(out, key, C.pointer(cells[key]))
        else:
            setattr(out, key, value)
    return out


def canonical_values(obj, keys, cells):
    return {k: cells[k].value if k in cells else getattr(obj, k) for k in keys}


def canonical_host(rates, callees):
    """A host whose stage scene and globals point at zeroed canonical bytes."""
    cells = {}
    scene = canonical_struct(StageScene, dict(d8106F1=0, d810CB6=0), cells)
    glob = canonical_struct(Globals, dict(d810707=0), cells)
    host = Host(C.pointer(scene), C.pointer(glob), rates, callees)
    host.keep_alive = (cells, scene, glob)
    return host


class ClipHeader(C.Structure):
    _fields_ = [('frames', C.c_uint16), ('next', C.c_int16), ('start', C.c_int16),
                ('events', C.c_uint32), ('event_count', C.c_int16),
                ('event_pairs', C.POINTER(C.c_int16))]


class ClipRates(C.Structure):
    _fields_ = [('count', C.c_uint32), ('rate', C.c_float * 459)]


PA = C.POINTER(LiveActor)
FN = {
    'w001D0C70': C.CFUNCTYPE(C.c_int, C.c_void_p),
    'bone_init': C.CFUNCTYPE(C.c_int, C.c_void_p, PA, C.c_int),
    'clip_init': C.CFUNCTYPE(C.c_int, C.c_void_p, PA, C.c_int, C.c_float, C.c_float),
    'clip_resolve': C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_int, C.POINTER(ClipHeader)),
    'skeleton_frame': C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_int16)),
    'w001C8710': C.CFUNCTYPE(C.c_int, C.c_void_p, PA, C.c_int, C.c_float),
    'w001C87C0': C.CFUNCTYPE(C.c_int, C.c_void_p, PA, C.c_int, C.c_float),
    'sample_bones': C.CFUNCTYPE(C.c_int, C.c_void_p, PA, C.c_int, C.c_float, C.c_float),
    'sound': C.CFUNCTYPE(C.c_int, C.c_void_p, PA, C.c_int, C.c_int, C.c_float),
    'cue': C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_int, C.c_int, C.c_int),
    'w001EFE00': C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, PA),
    'w001F00A0': C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, PA, C.c_int,
                             C.POINTER(C.POINTER(C.c_uint8))),
    'w001F0060': C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_int),
    'atan2': C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_float, C.c_float),
    'link20': C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32), C.POINTER(C.c_uint32)),
    'clip_lookup': C.CFUNCTYPE(C.c_int, C.c_void_p, PA, C.c_int, C.c_int, C.c_int, C.POINTER(C.c_int16)),
    'request': C.CFUNCTYPE(C.c_int, C.c_void_p, PA, C.c_int, C.c_int, C.c_float),
    'w0015C9D0': C.CFUNCTYPE(C.c_int, C.c_void_p, PA),
    'link1C': C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint8),
    'sound_stop': C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_int),
}
CALLEE_ORDER = ('w001D0C70', 'bone_init', 'clip_init', 'clip_resolve', 'skeleton_frame', 'w001C8710',
                'w001C87C0', 'sample_bones', 'sound', 'cue', 'w001EFE00', 'w001F00A0', 'w001F0060',
                'atan2', 'link20', 'clip_lookup', 'request', 'w0015C9D0', 'link1C', 'sound_stop')


class Callees(C.Structure):
    _fields_ = [('context', C.c_void_p)] + [(name, FN[name]) for name in CALLEE_ORDER]


class Host(C.Structure):
    _fields_ = [('stage', C.POINTER(StageScene)), ('globals', C.POINTER(Globals)),
                ('rates', C.POINTER(ClipRates)), ('callees', Callees)]


STATE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, PA)


class Major4(C.Structure):
    _fields_ = [('stage', C.POINTER(StageScene)), ('routine', STATE_FN * 7), ('routine_context', C.c_void_p * 7)]


def build_native(source=ROOT / 'src/game/em_player_stage_workers.c', name='stage_workers'):
    """`source`/`name` let a scratch harness build a variant into its own
    library; the default is the module under test."""
    LANE.mkdir(parents=True, exist_ok=True)
    lib = LANE / (name + ('.dylib' if sys.platform == 'darwin' else '.so'))
    command = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
               '-fPIC', '-Isrc', str(source), 'src/game/em_player_floor.c', '-lm', '-o', str(lib)]
    digest = hashlib.sha256(' '.join(command).encode())
    for path in [Path(source), ROOT / 'src/game/em_player_floor.c'] + sorted((ROOT / 'src').rglob('*.h')):
        digest.update(path.read_bytes())
    stamp = Path(str(lib) + '.sha256')
    if not (lib.exists() and stamp.exists() and stamp.read_text() == digest.hexdigest()):
        subprocess.run(command, cwd=ROOT, check=True)
        stamp.write_text(digest.hexdigest())
    native = C.CDLL(str(lib))
    for name in ('commit', 'reaction', 'scripted_check'):
        getattr(native, 'em_player_stage_' + name).argtypes = [C.c_void_p, PA, C.POINTER(C.c_int)]
    for name in ('drain', 'heartbeat', 'scripted_notify'):
        getattr(native, 'em_player_stage_' + name).argtypes = [C.c_void_p, PA]
    native.em_player_stage_row_request.argtypes = [C.c_void_p, PA, C.c_float]
    native.em_player_stage_anim_advance.argtypes = [C.c_void_p, PA, C.c_float, C.POINTER(C.c_uint32)]
    native.em_player_stage_stop_sound.argtypes = [C.c_void_p, C.c_int]
    native.em_player_stage_clip_rate.argtypes = [C.c_void_p, C.c_int, C.POINTER(C.c_float)]
    native.em_player_stage_0015B530.argtypes = [C.c_void_p, PA]
    native.em_player_clip_rates_load.argtypes = [C.POINTER(ClipRates), C.c_char_p]
    native.em_player_clip_rates_parse.argtypes = [C.POINTER(ClipRates), C.c_char_p, C.c_size_t]
    native.em_player_float_to_int.argtypes = [C.c_uint32]
    native.em_player_float_to_int.restype = C.c_int32
    native.em_player_001B1470.argtypes = [C.c_uint32]
    native.em_player_001B1470.restype = C.c_uint32
    native.em_player_stage_workers_bind.argtypes = [C.c_void_p, C.c_void_p]
    native.em_player_stage_begin.argtypes = [PA, C.POINTER(StageScene), C.c_void_p]
    native.em_player_stage_dispatch.argtypes = [PA, C.c_void_p]
    native.em_player_stage_end.argtypes = [PA, C.POINTER(StageScene)]
    native.em_player_stage_0015B130.argtypes = [C.c_void_p, PA]
    native.em_player_stage_0015B770.argtypes = [C.c_void_p, PA]
    return native


# ------------------------------------------------------------------ one case

class Case:
    """One scripted scenario: the record, the globals and the callee script,
    run through the original and through the native routine."""

    def __init__(self, raw, scene=None, globals_=None, script=None, ram=None, base=ACTOR):
        self.raw = bytearray(raw)
        self.scene = dict(spad3B8D=0, spad3B8F=0, area=0xB, d8106F1=0, d810CB6=0, busy=0)
        self.scene.update(scene or {})
        self.globals = dict(d8106C8=0, d810701=0, d810770=0, d81083C=0, d810707=0, d810C7E=0, spad3A20=0)
        self.globals.update(globals_ or {})
        self.script = dict(record=RECORD, clip=7, link20=(0x3F800000, 0x40000000), record_bytes=bytes(0x110),
                           link1C_byte=0x55, headers={}, skeleton=0)
        self.script.update(script or {})
        self.ram = ram
        self.base = base
        self.oracle_elf = ELF

    # -- the original
    def oracle(self, elf):
        o = EE(elf, self.ram)
        o.write(self.base, bytes(self.raw))
        s, g = self.scene, self.globals
        o.save(SPAD_3B8D, s['spad3B8D'], 1); o.save(SPAD_3B8F, s['spad3B8F'], 1)
        o.save(D_AREA, s['area'], 1); o.save(D_6F1, s['d8106F1'], 1)
        o.save(D_CB6, s['d810CB6'], 1); o.save(D_6B3, s['busy'], 1)
        o.save(D_6C8, g['d8106C8'] & 0xFFFFFFFF); o.save(D_701, g['d810701'], 1)
        o.save(D_770, g['d810770'], 1); o.save(D_83C, g['d81083C'], 1)
        o.save(D_707, g['d810707'], 1); o.save(D_C7E, g['d810C7E'], 1); o.save(SPAD_3A20, g['spad3A20'])
        sc = self.script
        o.write(RECORD, sc['record_bytes'])
        if self.ram is None:
            if struct.unpack_from('<I', self.raw, 0x20)[0] == LINK20:
                o.save(LINK20 + 0xC0, sc['link20'][0]); o.save(LINK20 + 0xC8, sc['link20'][1])
            o.save(LINK1C + 4, sc['link1C_byte'], 1)
            o.save(SKELETON + 0x8E, sc['skeleton'] & 0xFFFF, 2)
            for clip, header in sc['headers'].items():
                o.write(HEADERS + clip * 0x100, header)
        base = self.base
        log = o.log

        def actor_arg(reg):
            assert o.r[reg] & 0xFFFFFFFF == base, ('actor argument', hex(o.r[reg]))

        def ret(value=0): o.r[2] = value & 0xFFFFFFFF
        c = o.calls
        c[SOUND] = lambda o: (actor_arg(4), log.append(('sound', s32(o.r[5]), s32(o.r[6]), o.f[12])), ret())
        c[CUE] = lambda o: (log.append(('cue', s32(o.r[4]), s32(o.r[5]), s32(o.r[6]), s32(o.r[7]))), ret())
        c[EFE00] = lambda o: (actor_arg(5), log.append(('efe00', o.r[4] & 0xFFFFFFFF)), ret())

        def f00a0(o):
            assert o.r[5] & 0xFFFFFFFF == base + 0xB0 and o.r[6] & 0xFFFFFFFF == base + 0xC0
            log.append(('f00a0', o.r[4] & 0xFFFFFFFF, s32(o.r[7])))
            ret(sc['record'])
        c[F00A0] = f00a0
        c[F0060] = lambda o: (log.append(('f0060', o.r[4] & 0xFFFFFFFF, s32(o.r[5]))), ret())
        c[ATAN2] = lambda o: (log.append(('atan2', o.f[12], o.f[13])),
                              o.f.__setitem__(0, fbits(LIBC.atan2f(fnum(o.f[12]), fnum(o.f[13])))))

        def lookup(o):
            actor_arg(4)
            log.append(('lookup', s32(o.r[5]), s32(o.r[6]), s32(o.r[7])))
            ret(sc['clip'])
        c[LOOKUP] = lookup
        c[REQUEST] = lambda o: (actor_arg(4), log.append(('request', s32(o.r[5]), s32(o.r[6]), o.f[12])), ret())
        c[C9D0] = lambda o: (actor_arg(4), log.append(('c9d0',)), ret())
        c[D0C70] = lambda o: (log.append(('d0c70',)), ret())
        c[BONE_INIT] = lambda o: (actor_arg(4), log.append(('bone_init', s32(o.r[5]))), ret())
        c[CLIP_INIT] = lambda o: (actor_arg(4), log.append(('clip_init', s32(o.r[5]), o.f[12], o.f[13])), ret())
        if self.ram is None:
            def resolve(o):
                log.append(('resolve', o.r[4] & 0xFFFFFFFF, s16(o.r[5])))
                o.save(D_HEADER, HEADERS + (o.r[5] & 0x7FFF) * 0x100)
            c[RESOLVE] = resolve
        c[S8710] = lambda o: (self.node_arg(o), log.append(('8710', o.r[5] & 0xFFFFFFFF, o.f[12])), ret())
        c[S87C0] = lambda o: (self.node_arg(o), log.append(('87c0', o.r[5] & 0xFFFFFFFF, o.f[12])), ret())
        c[SAMPLE] = lambda o: (self.node_arg(o), log.append(('sample', o.r[5] & 0xFFFFFFFF, o.f[12], o.f[13])), ret())
        for address in MAJOR4_ROUTINES:
            c[address] = (lambda a: lambda o: (actor_arg(4), log.append(('routine', a)), ret()))(address)
        for address, tag in getattr(self, 'hooks', ()):
            c[address] = (lambda t: lambda o: (actor_arg(4), log.append(t), ret()))(tag)
        return o

    def node_arg(self, o):
        assert o.r[4] & 0xFFFFFFFF == self.base + 0x110

    def oracle_state(self, o):
        record = o.read(self.base, 0x320)
        scene = dict(spad3B8D=o.load(SPAD_3B8D, 1), spad3B8F=o.load(SPAD_3B8F, 1), area=o.load(D_AREA, 1),
                     d8106F1=o.load(D_6F1, 1), d810CB6=o.load(D_CB6, 1), busy=o.load(D_6B3, 1))
        glob = dict(d8106C8=s32(o.load(D_6C8)), d810701=o.load(D_701, 1), d810770=o.load(D_770, 1),
                    d81083C=o.load(D_83C, 1), d810707=o.load(D_707, 1), d810C7E=o.load(D_C7E, 1),
                    spad3A20=o.load(SPAD_3A20))
        word = struct.unpack_from('<I', self.raw, 0x1C)[0]
        extra = (o.read(RECORD, 0x110), o.load(word + 4, 1) if word else None)
        return record, scene, glob, extra

    # -- the native routine
    def native(self, lib, call, missing=None):
        log = []
        keep = []
        sc = self.script
        actor = LiveActor()
        C.memmove(actor.bytes, bytes(self.raw), 0x320)
        cells = {}
        scene = canonical_struct(StageScene, self.scene, cells, drop=(missing,))
        glob = canonical_struct(Globals, self.globals, cells, drop=(missing,))
        record = (C.c_uint8 * 0x110).from_buffer_copy(sc['record_bytes'])
        ram = self.ram
        reader = EE(self.oracle_elf, ram) if ram is not None else None
        word1C = struct.unpack_from('<I', self.raw, 0x1C)[0]
        link1C = {'byte': (reader.load(word1C + 4, 1) if reader else sc['link1C_byte']) if word1C else None}
        headers = {}

        def ok(fn): keep.append(fn); return fn
        impl = {
            'w001D0C70': lambda _: (log.append(('d0c70',)), 0)[1],
            'bone_init': lambda _, a, clip: (log.append(('bone_init', clip)), 0)[1],
            'clip_init': lambda _, a, clip, blend, frame: (log.append(('clip_init', clip, fbits(blend), fbits(frame))), 0)[1],
            'sound': lambda _, a, i, a2, radius: (log.append(('sound', i, a2, fbits(radius))), 0)[1],
            'cue': lambda _, a0, a1, a2, a3: (log.append(('cue', a0, a1, a2, a3)), 0)[1],
            'w001EFE00': lambda _, i, a: (log.append(('efe00', i)), 0)[1],
            'w001F0060': lambda _, i, a1: (log.append(('f0060', i, a1)), 0)[1],
            'request': lambda _, a, clip, flags, blend: (log.append(('request', clip, flags, fbits(blend))), 0)[1],
            'w0015C9D0': lambda _, a: (log.append(('c9d0',)), 0)[1],
            'sound_stop': lambda _, track, hard: (log.append(('stop', track, hard)), 0)[1],
        }

        def f00a0(_, i, a, a3, out):
            log.append(('f00a0', i, a3))
            out[0] = C.cast(record, C.POINTER(C.c_uint8)) if sc['record'] else C.POINTER(C.c_uint8)()
            return 0

        def atan2(_, y, x):
            log.append(('atan2', fbits(y), fbits(x)))
            return LIBC.atan2f(y, x)

        def link20(_, word, c0, c8):
            if ram is None:
                assert word == LINK20, hex(word)
                c0[0], c8[0] = sc['link20']
            else:
                c0[0], c8[0] = reader.load(word + 0xC0), reader.load(word + 0xC8)
            return 0

        def lookup(_, a, a1, a2, a3, out):
            log.append(('lookup', a1, a2, a3))
            out[0] = s16(sc['clip'])
            return 0

        def link1c(_, word, value):
            assert word == word1C and word, hex(word)
            link1C['byte'] = value
            return 0

        def resolve(_, bank, clip, out):
            if ram is None:
                log.append(('resolve', bank, clip))
                data = sc['headers'][clip & 0x7FFF]
                address = None
            else:
                address = self.resolve_captured(bank, clip)
                data = reader.read(address, 0x20)
            frames, nxt, start = struct.unpack_from('<Hhh', data, 2)
            events = struct.unpack_from('<I', data, 0x14)[0]
            out[0].frames, out[0].next, out[0].start, out[0].events = frames, nxt, start, events
            out[0].event_count = 0
            out[0].event_pairs = C.POINTER(C.c_int16)()
            if events:
                if ram is None:
                    source = data
                else:
                    count = s16(reader.load(address + events, 2))
                    source = reader.read(address, events + 4 + 4 * max(0, count))
                count = struct.unpack_from('<h', source, events)[0]
                out[0].event_count = count
                pairs = (C.c_int16 * max(1, 2 * count))()
                for k in range(2 * max(0, count)):
                    pairs[k] = struct.unpack_from('<h', source, events + 4 + 2 * k)[0]
                headers[len(headers)] = pairs
                out[0].event_pairs = C.cast(pairs, C.POINTER(C.c_int16))
            return 0

        def skeleton(_, word, out):
            if ram is None:
                assert word == SKELETON, hex(word)
                out[0] = s16(sc['skeleton'])
            else:
                out[0] = s16(reader.load(word + 0x8E, 2))
            return 0
        impl.update({'w001F00A0': f00a0, 'atan2': atan2, 'link20': link20, 'clip_lookup': lookup,
                     'link1C': link1c, 'clip_resolve': resolve, 'skeleton_frame': skeleton,
                     'w001C8710': lambda _, a, n, f: (log.append(('8710', n, fbits(f))), 0)[1],
                     'w001C87C0': lambda _, a, n, f: (log.append(('87c0', n, fbits(f))), 0)[1],
                     'sample_bones': lambda _, a, n, x, y: (log.append(('sample', n, fbits(x), fbits(y))), 0)[1]})
        callees = Callees()
        for name in CALLEE_ORDER:
            if name != missing: setattr(callees, name, ok(FN[name](impl[name])))
        host = Host(None if missing == '__stage__' else C.pointer(scene),
                    None if missing == '__globals__' else C.pointer(glob), C.pointer(RATES), callees)
        result = call(lib, host, actor, log)
        scene_out = canonical_values(scene, self.scene, cells)
        glob_out = canonical_values(glob, self.globals, cells)
        extra = (bytes(record), link1C['byte'])
        return result, bytes(actor.bytes), scene_out, glob_out, extra, log

    def resolve_captured(self, bank, clip):
        """anim_clip_resolve's header address: the original 001C6120 run over
        the same RAM (not re-implemented here)."""
        side = EE(self.oracle_elf, self.ram)
        return side.run(0x1C6120, (bank, clip & 0xFFFFFFFF))




# ------------------------------------------------------------------ routines

def call_result(name):
    def call(lib, host, actor, log):
        out = C.c_int(-7)
        status = getattr(lib, 'em_player_stage_' + name)(C.byref(host), C.byref(actor), C.byref(out))
        return (status, out.value)
    return call


def call_plain(name):
    def call(lib, host, actor, log):
        return (getattr(lib, 'em_player_stage_' + name)(C.byref(host), C.byref(actor)),)
    return call


def call_row(blend):
    def call(lib, host, actor, log):
        return (lib.em_player_stage_row_request(C.byref(host), C.byref(actor), C.c_float(fnum(blend))),)
    return call


def call_advance(step):
    def call(lib, host, actor, log):
        out = C.c_uint32(0xDEAD)
        status = lib.em_player_stage_anim_advance(C.byref(host), C.byref(actor), C.c_float(fnum(step)),
                                                  C.byref(out))
        return (status, out.value)
    return call


# ---- the workers bound into the stage (em_player_stage_workers_bind) -----

from test_player_floor_reference import TABLE1, TABLE2, PHASE13, PHASE14, MAJORS  # noqa: E402


class StageWorkers(C.Structure):
    """EmPlayerStageWorkers (em_player_floor.h)."""
    _fields_ = [('context', C.c_void_p)] + [(name, C.c_void_p) for name in (
                    'clip_rate', 'advance', 'commit', 'reaction', 'drain', 'heartbeat', 'scripted_check',
                    'scripted_notify', 'row_request', 'stop_sound')] + [
                ('major', STATE_FN * 7), ('major_context', C.c_void_p * 7),
                ('state', STATE_FN * 0x26), ('state_context', C.c_void_p * 0x26),
                ('state2', STATE_FN * 0x1A), ('state2_context', C.c_void_p * 0x1A),
                ('phase13', STATE_FN * 5), ('phase13_context', C.c_void_p * 5),
                ('phase14', STATE_FN * 4), ('phase14_context', C.c_void_p * 4)]


class Stage(C.Structure):
    _fields_ = [('scene', C.POINTER(StageScene)), ('workers', C.POINTER(StageWorkers))]


def compose_hooks(which):
    if which == '0015BA50':
        return [(address, ('major', n)) for n, address in MAJORS.items() if n != 4]
    if which == '0015B130':
        return [(address, ('state', address)) for address in TABLE1]
    return [(address, ('state', address)) for address in set(TABLE2.values()) | set(PHASE13) | set(PHASE14)]


def call_stage(which):
    """em_player_stage_* from em_player_floor.c with this module's workers
    bound by em_player_stage_workers_bind; callbacks log like the hooks."""
    def call(lib, host, actor, log):
        keep = []

        def routine(tag):
            fn = STATE_FN(lambda _, a: (log.append(tag), 0)[1]); keep.append(fn); return fn
        w = StageWorkers()
        lib.em_player_stage_workers_bind(C.byref(w), C.byref(host))
        for n in (0, 1, 2, 5, 6): w.major[n] = routine(('major', n))
        m4 = Major4(host.stage)
        for i, address in enumerate(MAJOR4_ROUTINES): m4.routine[i] = routine(('routine', address))
        w.major[4] = C.cast(lib.em_player_stage_0015B530, STATE_FN)
        w.major_context[4] = C.addressof(m4)
        for i, address in enumerate(TABLE1[:0x25]): w.state[i] = routine(('state', address))
        for i, address in TABLE2.items(): w.state2[i] = routine(('state', address))
        for i, address in enumerate(PHASE13): w.phase13[i] = routine(('state', address))
        for i, address in enumerate(PHASE14): w.phase14[i] = routine(('state', address))
        if which == '0015BA50':
            status = lib.em_player_stage_begin(C.byref(actor), host.stage, C.byref(w))
            if status == 0: status = lib.em_player_stage_dispatch(C.byref(actor), C.byref(w))
            if status == 0: status = lib.em_player_stage_end(C.byref(actor), host.stage)
            return (status,)
        stage = Stage(host.stage, C.pointer(w))
        fn = lib.em_player_stage_0015B130 if which == '0015B130' else lib.em_player_stage_0015B770
        return (fn(C.byref(stage), C.byref(actor)),)
    return call


def compose_case(which):
    def generate(rng, label):
        case = reaction_case(rng, label) if rng.randrange(2) else drain_case(rng, label)
        raw = case.raw
        raw[0x214:0x218] = bytes(4); raw[0x308:0x30C] = bytes(4)   # +214/+308: port pointers beside the image
        putf(raw, 0xC4, rng.choice([0.0, 3.0, -3.1, 7.0]))
        if case.globals['d8106C8'] == 0: case.globals['d8106C8'] = rng.choice([0, 0x64])
        putf(raw, 0x220, rng.choice([50.0, 30.0, 8.0, 1.5, 0.0, rng.uniform(0, 100)]))
        put(raw, 0x210, rng.choice([0, 0x3C, 0x78]), 2)
        put(raw, 0x20E, rng.choice([0, 0, 1, 2]), 2)
        clips = list(range(12))
        case.script['headers'] = {c: clip_header(rng, clips) for c in clips + [c | 0x4000 for c in clips]}
        put(raw, 0x2C, rng.choice(clips), 2)
        putf(raw, 0x3C, rng.choice([0.5, 1.0, 3.0, 10.0]))
        case.scene.update(spad3B8D=rng.choice([0, 0, 3]), spad3B8F=rng.choice([0, 1, 2]),
                          area=rng.choice([0xB, 0xB, 0x15]), d810CB6=rng.choice([0, 0, 1]), busy=rng.choice([0, 1]))
        if which == '0015BA50':
            raw[4] = rng.choice([0, 1, 2, 3, 4, 4, 4, 5, 6, 7])
            raw[5] = rng.choice([0, 0x17, 1, 5, 8, 0xC, 2])
            put(raw, 0x20C, rng.randrange(459), 2)
            put(raw, 0x1F2, rng.choice([struct.unpack_from('<h', raw, 0x20C)[0], rng.randrange(459)]), 2)
            raw[0x2F3] = rng.choice([0, 0, 1, 3, 5])
            putf(raw, 0x204, rng.choice([1.0, 0.75, 2.0, rng.uniform(0.1, 3.0)]))
            putf(raw, 0x1F4, rng.choice([1.0, 0.5, 2.0]))
            putf(raw, 0x1F8, rng.choice([0.0, 8.0]))
        elif which == '0015B130':
            raw[4] = 1
            raw[5] = rng.choice(list(range(0x28)) + [0x19, 5, 0x1C])
            raw[0x1F0] = rng.choice([raw[0x1F0], 0x2A, 0x17, 0x3C, 0x3E, 0x41])
        else:
            raw[4] = 2
            raw[5] = rng.choice(list(range(0x1D)) + [0xD, 0xE, 0x19]); raw[0xD] = rng.randrange(6)
        case.hooks = compose_hooks(which)
        case.label = (which, label)
        return case
    return generate


ROUTINES = {
    # name: (entry, native call builder, how the original's return value is compared, reachable workers)
    'reaction': (REACTION, lambda case: call_result('reaction'), 'result',
                 ('sound', 'cue', 'w001EFE00', 'w001F00A0', 'w001F0060', 'atan2', 'link20', 'clip_lookup',
                  'request')),
    'commit': (COMMIT, lambda case: call_result('commit'), 'result', ('w001D0C70', 'bone_init', 'clip_init')),
    'scripted_check': (CHECK, lambda case: call_result('scripted_check'), 'result', ()),
    'drain': (DRAIN, lambda case: call_plain('drain'), None, ('w001F0060', 'w0015C9D0')),
    'heartbeat': (HEARTBEAT, lambda case: call_plain('heartbeat'), None, ('cue',)),
    'scripted_notify': (NOTIFY, lambda case: call_plain('scripted_notify'), None, ('link1C',)),
    'row_request': (ROW, lambda case: call_row(case.blend), None, ('clip_lookup', 'request')),
    'advance': (ADVANCE, lambda case: call_advance(case.step), 'flags',
                ('clip_resolve', 'skeleton_frame', 'w001C8710', 'w001C87C0', 'sample_bones')),
    # The stage with the workers bound (composition; no fail-stop sweep).
    'stage_0015BA50': (0x15BA50, lambda case: call_stage('0015BA50'), None, None),
    'stage_0015B130': (0x15B130, lambda case: call_stage('0015B130'), None, None),
    'stage_0015B770': (0x15B770, lambda case: call_stage('0015B770'), None, None),
}


def run_case(elf, lib, name, case, coverage=None):
    entry, builder, compare_ret, _ = ROUTINES[name]
    o = case.oracle(elf)
    floats = ()
    if name == 'row_request': floats = (case.blend,)
    if name == 'advance': floats = (case.step,)
    value = o.run(entry, (case.base,), floats)
    if coverage is not None: coverage |= o.pcs
    expected = case.oracle_state(o)
    result, record, scene, glob, extra, log = case.native(lib, builder(case))
    where = (name, case.label)
    assert result[0] == 0, (where, 'native fault', result)
    if compare_ret == 'result':
        assert result[1] == value, (where, 'return', result[1], value)
    elif compare_ret == 'flags':
        assert result[1] == value, (where, 'flags', hex(result[1]), hex(value))
    for k in range(0x320):
        assert record[k] == expected[0][k], (where, 'record', hex(k), record[k], expected[0][k])
    assert scene == expected[1], (where, 'scene', scene, expected[1])
    assert glob == expected[2], (where, 'globals', glob, expected[2])
    assert extra == expected[3], (where, 'extra memory')
    assert log == o.log, (where, 'callees', log, o.log)
    return o.log


# ------------------------------------------------------------------ scenarios

F = fbits
HEALTH = (0.0, -1.0, 0.5, 1.0, 1.5, 2.0, 2.5, 10.0, 10.01, 35.0, 35.01, 36.0, 60.0, 61.0, 100.0)


def put(raw, offset, value, size=1):
    raw[offset:offset + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')


def putf(raw, offset, value):
    raw[offset:offset + 4] = struct.pack('<I', value if isinstance(value, int) else F(value))


def random_raw(rng):
    raw = bytearray(rng.randrange(256) for _ in range(0x320))
    put(raw, 0x20, LINK20, 4); put(raw, 0x1C, rng.choice([0, LINK1C]), 4)
    put(raw, 0x40, 0x1A0000, 4); put(raw, 0x110, SKELETON, 4)
    return raw


def reaction_case(rng, label):
    raw = random_raw(rng)
    putf(raw, 0x220, rng.choice(HEALTH + (0x00000001, 0x80000000, rng.uniform(-50, 150))))
    putf(raw, 0x224, rng.choice([0.0, 0.0, 1.0, 3.0, 50.0, 200.0, 0x00000010, rng.uniform(0, 100)]))
    putf(raw, 0x22C, rng.choice([0.0, 0.0, 5.0, 50.0, 120.0, 0x80000001, rng.uniform(0, 100)]))
    putf(raw, 0x228, rng.choice([0.0, 50.0, 99.99, 100.0, 150.0]))
    putf(raw, 0xC4, rng.choice([0.0, 3.0, -3.1, 3.1415927, 7.0, -9.5]))
    raw[0x234] = rng.choice([0, 1, 1, 2]); raw[0] = rng.choice([0, 1, 1, 2, 3])
    raw[0xF] = rng.choice(list(range(0, 0xD)) + [0x63, 0x81, 0x87, rng.randrange(256)])
    raw[0xD] = rng.choice([0, 1, 2, 3, 4])
    raw[0x1F0] = rng.choice([0x3B, 0x3C, 0x27, 0x34, 0x35, 0x33, 0x2C, 0x10, 0x17, 0x1A, 0x1D, 0x2A,
                             0x21, 0x22, 0x2F, 0x30, 0x39, 0x2D, 0xB, 0xC, 0xD, 6, 7, 8, 9, 0xA, 0xF,
                             0x14, 0x11, 0x13, 0x15, 0x18, 0x1B, 0x1E, 0x20, 0x23, 0x26, 0x28, 0x2E,
                             0x3E, 0, 1, 2, rng.randrange(256)])
    raw[0x1F1] = rng.choice([0, 1, 2, 3])
    raw[4] = rng.choice([1, 1, 2, 4, 5]); raw[5] = rng.choice([0, 1, 0x1D, 0x1E, 0x21, 0x22, 0x1F, 0x20, 8,
                                                               rng.randrange(0x30)])
    raw[6] = rng.choice([0, 1, 2, 4, 5, 6])
    raw[0x23A] = rng.choice([0x5B, 6, 1]); raw[0x23B] = rng.choice([0xA, 6, 0]); raw[0x31E] = rng.choice([0, 1])
    case = Case(raw, scene=dict(d8106F1=rng.choice([0, 1]), area=rng.choice([0xB, 8])),
                globals_=dict(d81083C=rng.choice([0, 0, 0, 1]), d810701=rng.choice([2, 0]),
                              d810770=rng.choice([0xFF, 0])),
                script=dict(record=rng.choice([RECORD, 0]), clip=rng.choice([5, -3, 0x1234]),
                            link20=(F(rng.uniform(-1, 1)), F(rng.uniform(-1, 1))),
                            record_bytes=bytes(rng.randrange(256) for _ in range(0x110))))
    case.label = label
    return case


# Deterministic 0021C440 scenarios over a neutral record (health 50, no
# pending damage, hit code 0, mode 0x3E, +4/+5 = 1/3): one per path class,
# run in every mode ahead of the random sweep.
REACTION_FIELDS = {'health': (0x220, 'f'), 'dmg': (0x224, 'f'), 'inf': (0x22C, 'f'), 'meter': (0x228, 'f'),
                   'c234': (0x234, 'b'), 'e0': (0, 'b'), 'F': (0xF, 'b'), 'D': (0xD, 'b'),
                   'mode': (0x1F0, 'b'), 'var': (0x1F1, 'b'), 's4': (4, 'b'), 's5': (5, 'b'), 's6': (6, 'b'),
                   's23A': (0x23A, 'b'), 's23B': (0x23B, 'b'), 's31E': (0x31E, 'b')}
REACTION_BASE = dict(health=50.0, dmg=0.0, inf=0.0, meter=0.0, c234=0, e0=1, F=0, D=0, mode=0x3E, var=0,
                     s4=1, s5=3, s6=0, s23A=1, s23B=0, s31E=0)
REACTION_SCENARIOS = [
    dict(health=0.0, dmg=3.0, inf=5.0), dict(health=-1.0),
    dict(c234=1, inf=5.0), dict(c234=1, inf=5.0, F=3), dict(c234=1, inf=5.0, dmg=2.0),
    dict(mode=8, F=2, e0=2, dmg=1.0), dict(mode=8, F=7), dict(mode=8, e0=1), dict(F=0x85),
    *[dict(F=k) for k in (1, 3, 4, 5, 8, 9, 0xA, 0xB)],
    dict(F=2, mode=0x2C, D=2), dict(F=2, mode=0x2C, D=1), dict(F=2, mode=0x10), dict(F=2, mode=0x39),
    dict(F=2, mode=0x27, dmg=2.0), dict(F=2, mode=0x27), dict(F=2, dmg=5.0),
    dict(F=6, dmg=60.0, c234=1), dict(F=6, dmg=60.0), dict(F=6, dmg=1.0),
    dict(F=7, mode=0x17), dict(F=7),
    dict(F=4, mode=6, var=3, inf=2.0), dict(F=4, mode=7, var=0), dict(F=9, mode=6, var=1, dmg=1.0),
    dict(mode=0x3B), dict(mode=0x3C, D=0, dmg=4.0), dict(mode=0x3C, D=0), dict(mode=0x3C, D=1),
    dict(mode=0x3C, D=2), dict(mode=0x3C, D=3),
    dict(mode=0x27, dmg=4.0), dict(mode=0x27), dict(mode=0x27, F=0x12), dict(mode=0x34, inf=5.0),
    dict(mode=0x33, s4=1, s5=0x1F, dmg=4.0),
    dict(g83C=1),
    dict(c234=1, s23A=6, s31E=0, s4=1, s5=0), dict(c234=1, s23A=6, s31E=1, s5=1),
    dict(c234=1, s23A=0x5B, s5=0x21), dict(c234=1, s23A=0x5B, s5=0x1D, var=1, mode=0x33),
    dict(c234=1, s23A=0x5B, s5=0x1E, var=1), dict(c234=1, s23A=0x5B, s5=0x1D, var=0),
    dict(c234=1, s23A=0x5B, s5=0x1E, var=0), dict(c234=1, s23A=0x5B, s5=0x1F),
    dict(c234=1, s23A=6, s5=1, health=2.0, record=0), dict(c234=1, e0=2, s23A=6, s5=1),
    dict(s23B=0xA, s4=1, s5=0), dict(s23B=0xA, s5=1, health=5.0), dict(s23B=0xA, s5=1, health=5.0, c234=1),
    dict(s23B=0xA, s5=0x1D, var=1, mode=0x33), dict(s23B=0xA, s5=0x1E, var=1),
    dict(s23B=0xA, s5=0x1D, var=0), dict(s23B=0xA, s5=0x1E, var=0), dict(s23B=0xA, s5=0x22),
    dict(s23B=0xA, s4=2, s5=0), dict(s23B=0xA, e0=2),
    dict(s23B=0xA, area=8, g701=2, g770=0xFF), dict(s23B=0xA, area=8, g701=2, g770=0),
    dict(s23B=0xA, area=8, g701=0),
    dict(dmg=4.0, mode=0x10), dict(dmg=4.0, F=0xC), dict(dmg=4.0), dict(inf=4.0), dict(dmg=4.0, inf=4.0),
    dict(dmg=60.0, F=0x63, f6F1=1), dict(dmg=60.0, F=0x63, f6F1=0), dict(dmg=60.0, c234=1, f6F1=1),
    dict(dmg=60.0),
    dict(dmg=4.0, s4=1, s5=0x1D, meter=100.0, f6F1=1, mode=0x33), dict(dmg=4.0, s4=1, s5=0x1E, meter=50.0),
    dict(dmg=4.0, s4=1, s5=0x1D, meter=100.0), dict(dmg=4.0, meter=100.0, f6F1=1),
    dict(dmg=4.0, meter=100.0), dict(dmg=4.0, s4=2, s5=0x1D),
    dict(inf=60.0, meter=50.0), dict(inf=60.0, meter=50.0, health=40.0), dict(inf=60.0, meter=50.0, c234=2),
    dict(health=30.0), dict(health=30.0, F=9, dmg=1.0),
]
# EE-arithmetic cases: 0021C350's sub.s and 0021C270's add.s on operands
# whose exact result needs rounding (truncation and the pre-trim decide).
_ARITH = random.Random(0x21C350)
REACTION_SCENARIOS += [dict(F=9, health=_ARITH.uniform(35.5, 500.0), dmg=_ARITH.uniform(0.001, 30.0))
                       for _ in range(24)]
REACTION_SCENARIOS += [dict(F=4, meter=_ARITH.uniform(0.0, 99.0), inf=_ARITH.uniform(0.0001, 0.9))
                       for _ in range(24)]


def reaction_scenario(rng, index):
    spec = dict(REACTION_BASE)
    spec.update(REACTION_SCENARIOS[index])
    raw = random_raw(rng)
    for name, (offset, kind) in REACTION_FIELDS.items():
        if kind == 'f': putf(raw, offset, spec[name])
        else: raw[offset] = spec[name]
    putf(raw, 0xC4, rng.choice([0.0, 3.0, -3.1, 3.1415927, 7.0, -9.5]))   # 001B1470 input via 0017C370
    case = Case(raw, scene=dict(d8106F1=spec.get('f6F1', 0), area=spec.get('area', 0xB)),
                globals_=dict(d81083C=spec.get('g83C', 0), d810701=spec.get('g701', 0),
                              d810770=spec.get('g770', 0)),
                script=dict(record=spec.get('record', RECORD), clip=-3,
                            link20=(F(rng.uniform(-1, 1)), F(rng.uniform(-1, 1))),
                            record_bytes=bytes(rng.randrange(256) for _ in range(0x110))))
    case.label = ('scenario', index)
    return case


def commit_case(rng, label):
    raw = random_raw(rng)
    raw[0x2F3] = rng.choice([0, 0, 1, 2, 3, 4, 5])
    put(raw, 0x1F2, rng.choice([3, -2, 0x150]), 2)
    put(raw, 0x20C, rng.choice([3, 4, -2, 0x150]), 2)
    putf(raw, 0x1F8, rng.choice([0.0, 8.0, 12.5]))
    case = Case(raw, scene=dict(spad3B8F=rng.choice([0, 1, 2])))
    case.label = label
    return case


def drain_case(rng, label):
    raw = random_raw(rng)
    raw[0x234] = rng.choice([0, 0, 1])
    raw[0x1F0] = rng.choice([0, 1, 2, 8, 0x14, 0xF, rng.randrange(256)])
    put(raw, 0x300, rng.choice([0, 0x166, 0x167, 0x167, -1, 0x7FFF]), 2)
    put(raw, 0x2FC, rng.choice([0, 0xEE, 0xEF, 0xEF, 0xF0]), 2)
    putf(raw, 0x220, rng.choice([0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 35.5, 36.0, 36.5, 37.0, 100.0]))
    raw[0x235] = rng.choice([0, 1, 0xFE, 0xFF])
    case = Case(raw, globals_=dict(d8106C8=rng.choice([0, 4, 0x64, 0x24, 0x44, 0x60, rng.randrange(1 << 31)]),
                                   d810C7E=rng.choice([0, 0, 1])))
    case.label = label
    return case


HEARTBEAT_HEALTH = (0.0, 0x80000000, 0x00000003, 5.0, 10.0, 10.01, 35.0, 35.01, 100.0, -4.0)
HEARTBEAT_COUNTER = (0, 1, 0x3B, 0x3C, 0x3D, 0x77, 0x78, 0x79, -1)


def heartbeat_case(rng, label):
    raw = random_raw(rng)
    if isinstance(label, int) and label < len(HEARTBEAT_HEALTH) * len(HEARTBEAT_COUNTER):
        health = HEARTBEAT_HEALTH[label // len(HEARTBEAT_COUNTER)]
        counter = HEARTBEAT_COUNTER[label % len(HEARTBEAT_COUNTER)]
    else:
        health, counter = rng.choice(HEARTBEAT_HEALTH), rng.choice(HEARTBEAT_COUNTER)
    putf(raw, 0x220, health)
    put(raw, 0x210, counter, 2)
    case = Case(raw)
    case.label = label
    return case


def check_case(rng, label):
    raw = random_raw(rng)
    raw[4], raw[5] = rng.choice([(5, 1), (5, 0), (1, 1), (1, 0), (4, 1)])
    putf(raw, 0x220, rng.choice([0.0, -1.0, 5.0, 50.0]))
    raw[0x25F] = rng.choice([0, 0, 0, 2])
    raw[0x1F0] = rng.choice([0x3C, 0x3D, 8, 0, 1, 0x41, rng.randrange(256)])
    case = Case(raw, scene=dict(d8106F1=rng.choice([0, 0, 1])))
    case.label = label
    return case


def notify_case(rng, label):
    raw = random_raw(rng)
    case = Case(raw, scene=dict(spad3B8F=rng.choice([0, 1, 2])), script=dict(link1C_byte=rng.randrange(256)))
    case.label = label
    return case


def row_case(rng, label):
    raw = random_raw(rng)
    case = Case(raw, script=dict(clip=rng.choice([0, 0x5F, -1, 0x1C8, 0x12345])))
    case.blend = F(rng.choice([8.0, 12.0, 0.0, rng.uniform(0, 20)]))
    case.label = label
    return case


def clip_header(rng, clips):
    """A synthetic clip-bank header: frames +2, next +4, start +6, event table +14."""
    data = bytearray(0x100)
    struct.pack_into('<H', data, 2, rng.choice([1, 2, 5, 30, 200, 0, 0x8001, 0xFFFF]))
    struct.pack_into('<h', data, 4, rng.choice([-2, -1, rng.choice(clips), rng.choice(clips) | 0x4000]))
    struct.pack_into('<h', data, 6, rng.choice([0, 5, -3, 40]))
    if rng.randrange(3):
        struct.pack_into('<I', data, 0x14, 0x40)
        count = rng.choice([0, 1, 3, 6, -1])
        struct.pack_into('<h', data, 0x40, count)
        for k in range(max(0, count)):
            struct.pack_into('<hH', data, 0x44 + 4 * k, rng.choice([0, 1, 2, 3, 5, 10, 29, -1]),
                             rng.choice([0x1, 0x10, 0x800, 0x8000, 0x7FFF, 0x100]))
    return bytes(data)


def advance_case(rng, label):
    raw = random_raw(rng)
    clips = list(range(12))
    headers = {clip: clip_header(rng, clips) for clip in clips + [c | 0x4000 for c in clips]}
    clip = rng.choice(clips)
    put(raw, 0x2C, clip | rng.choice([0, 0x8000]), 2)
    putf(raw, 0x3C, rng.choice([0.5, 1.0, 1.5, 2.0, 3.0, 5.25, 10.0, 0.0, -1.0, 29.0, 0x7F800000,
                                0xFF800000, 0x7FC00000, 0x00000005, 3.5e9, -3.5e9, rng.uniform(0, 40)]))
    raw[0xC] = rng.choice([21, 22, 1])
    case = Case(raw, script=dict(headers=headers, skeleton=rng.choice([0, 3, 10, -4])))
    case.step = F(rng.choice([0.0, 0.5, 1.0, 1.25, 1.5, 2.0, 2.5, 3.3, -1.0, 0x00000001]))
    case.label = label
    return case


GENERATORS = {'reaction': reaction_case, 'commit': commit_case, 'drain': drain_case,
              'heartbeat': heartbeat_case, 'scripted_check': check_case, 'scripted_notify': notify_case,
              'row_request': row_case, 'advance': advance_case,
              'stage_0015BA50': compose_case('0015BA50'), 'stage_0015B130': compose_case('0015B130'),
              'stage_0015B770': compose_case('0015B770')}
COUNTS = {'reaction': (12000, 700), 'commit': (1500, 60), 'drain': (3000, 150), 'heartbeat': (1500, 100),
          'scripted_check': (1500, 60), 'scripted_notify': (600, 20), 'row_request': (600, 20),
          'advance': (4000, 260), 'stage_0015BA50': (3000, 120), 'stage_0015B130': (3000, 160),
          'stage_0015B770': (1000, 40)}


# ------------------------------------------------------------------ captured RAM

def captured_images():
    """(label, ram) for the playable AREA11 image and the route beats."""
    images = [('playable_ee', REFERENCE / 'playable_ee.bin')]
    quick = ('05_boxes', '06_hill_slide', '08_truck_crossing')
    if ROUTE.exists():
        for beat in sorted(ROUTE.iterdir()):
            if (beat / 'eeMemory.bin').exists() and (reference_mode.FULL or beat.name in quick):
                images.append((beat.name, beat / 'eeMemory.bin'))
    if not reference_mode.FULL:
        assert len(images) == 1 + len(quick), ('route captures missing', images)
    missing = [str(path) for _, path in images if not path.exists()]
    assert not missing, ('captured RAM missing', missing)
    return [(label, path.read_bytes()) for label, path in images]


def captured_cases(ram, label, rng):
    """The captured player record, as captured and with the fields each
    worker branches on perturbed; the record's own words (+20, +40, +110)
    point into the captured RAM."""
    base = bytearray(ram[PLAYER:PLAYER + 0x320])
    out = []

    def case(name, raw, **kw):
        c = Case(raw, ram=ram, base=PLAYER, scene=kw.get('scene'), globals_=kw.get('globals_'),
                 script=kw.get('script'))
        c.label = (label, name, len(out))
        c.step = kw.get('step', 0)
        c.blend = kw.get('blend', F(8.0))
        out.append((name, c))
    area = ram[D_AREA]
    scene = dict(area=area, d8106F1=ram[D_6F1], spad3B8F=0)
    glob = dict(d8106C8=s32(struct.unpack_from('<I', ram, D_6C8)[0]), d810701=ram[D_701], d810770=ram[D_770],
                d81083C=ram[D_83C], d810707=ram[D_707], d810C7E=ram[D_C7E])
    for name in ('reaction', 'drain', 'heartbeat', 'scripted_check', 'commit'):
        case(name, bytearray(base), scene=scene, globals_=glob)
    rate = struct.unpack_from('<I', base, 0x34)[0]
    for step in (rate, F(1.0), F(2.5)):
        case('advance', bytearray(base), scene=scene, globals_=glob, step=step)
    for code in range(1, 0xC):          # every hit code over the captured record
        raw = bytearray(base); raw[0xF] = code
        case('reaction', raw, scene=scene, globals_=glob)
    for damage in (3.0, 150.0):
        raw = bytearray(base); putf(raw, 0x224, damage); raw[0xF] = 0
        case('reaction', raw, scene=scene, globals_=glob)
        raw = bytearray(base); putf(raw, 0x22C, damage); raw[0xF] = 0
        case('reaction', raw, scene=scene, globals_=glob)
    raw = bytearray(base); raw[0] = 1; raw[0x234] = 1; raw[0x23A] = 0x5B; raw[0xF] = 0
    case('reaction', raw, scene=scene, globals_=glob)
    raw = bytearray(base); raw[0] = 1; raw[0x23B] = 0xA; raw[0xF] = 0
    case('reaction', raw, scene=scene, globals_=glob)
    for counter in (0x167, 0xEF):
        raw = bytearray(base); put(raw, 0x300, counter, 2); put(raw, 0x2FC, counter, 2)
        case('drain', raw, scene=scene, globals_=dict(glob, d8106C8=glob['d8106C8'] | 0x64))
    for clock in (1.0, 0.5):
        raw = bytearray(base); putf(raw, 0x3C, clock)
        case('advance', raw, scene=scene, globals_=glob, step=F(1.0))
    return out


# ------------------------------------------------------------------ extra checks

def check_float_helpers(elf, lib, rng, coverage):
    """001B1470 and 001281C0 alone, over boundary and random bit patterns."""
    specials = [0, 0x80000000, 1, 0x80000001, 0x7F800000, 0xFF800000, 0x7FC00000, 0x7F800001, 0xFFC00000,
                0x7F900000, 0xFF900001,
                F(3.1415927), F(-3.1415927), 0x40490FDC, 0xC0490FDC, F(6.2831855), F(-6.2831855), F(100.0),
                F(-1000.0), F(0.5), F(-0.5), F(1.0), F(2147483520.0), F(2147483648.0), F(-2147483648.0),
                F(-2147483904.0), F(3.5e9), 0x3F7FFFFF, 0x4EFFFFFF, 0x4F000000, 0xCF000000, 0xCF000001]
    values = specials + [rng.randrange(1 << 32) for _ in range(reference_mode.pick(20000, 600))] + \
        [F(rng.uniform(-40, 40)) for _ in range(reference_mode.pick(4000, 200))]
    for value in values:
        o = EE(elf)
        got = o.run(0x1281C0, (), (value,))
        coverage |= o.pcs
        assert lib.em_player_float_to_int(value) & 0xFFFFFFFF == got, ('float_to_int', hex(value))
        magnitude = value & 0x7FFFFFFF
        if magnitude < 0x7F800000 and (magnitude >> 23) < 0x90:     # the wrap loops terminate
            o = EE(elf)
            o.run(0x1B1470, (), (value,))
            coverage |= o.pcs
            assert lib.em_player_001B1470(value) == o.f[0], ('001B1470', hex(value), hex(o.f[0]))
    return len(values)


def check_predicates(elf, lib, rng, coverage):
    """0021BB00, 0021BC40 and 0021D640 alone over every +1F0 value (with
    the +4/+5/+6/+D bytes they test), against the exported natives."""
    lib.em_player_0021BB00.argtypes = lib.em_player_0021BC40.argtypes = lib.em_player_0021D640.argtypes = [PA]
    cases = 0
    for mode in range(256):
        for s4, s5, s6, d in ((1, 8, 2, 2), (1, 8, 4, 0), (1, 8, 5, 1), (1, 8, 0, 2), (1, 7, 2, 0),
                              (2, 8, 2, 1), (1, 0x1F, 0, 0), (1, 0x20, 0, 2), (2, 0x1F, 0, 0)):
            raw = bytearray(0x320); raw[0x1F0] = mode; raw[4], raw[5], raw[6], raw[0xD] = s4, s5, s6, d
            actor = LiveActor(); C.memmove(actor.bytes, bytes(raw), 0x320)
            for entry, fn in ((0x21BB00, lib.em_player_0021BB00), (0x21BC40, lib.em_player_0021BC40),
                              (0x21D640, lib.em_player_0021D640)):
                o = EE(elf); o.write(ACTOR, bytes(raw))
                got = o.run(entry, (ACTOR,))
                coverage |= o.pcs
                assert fn(C.byref(actor)) == got, (hex(entry), hex(mode), s4, s5, s6, d)
                cases += 1
    return cases


# Reachable by control flow, never by value (listed with the reason; the
# coverage assertion excludes exactly these):
DEAD_BY_VALUE = {
    # anim_advance_time: bltz on a halfword loaded with lhu (001C6610,
    # 001C66F8) -- the unsigned-to-float fix-up never runs.
    **{pc: 'lhu value is never negative' for pc in range(0x1C6628, 0x1C6640, 4)},
    **{pc: 'lhu value is never negative' for pc in range(0x1C6718, 0x1C6734, 4)},
    # 0021C440 hit_a: its dead exit tests +234 == 1, which hit_a's own
    # entry test (0021CA9C) already requires; the +234 != 1 arm never runs.
    **{pc: 'hit_a requires +234 == 1' for pc in range(0x21CC1C, 0x21CC34, 4)},
    # 0021C440 hit code 2: mode 0x2C with +D != 2 already left through
    # 0021BB00 (0021C518), which returns 1 for exactly that pair.
    0x21C604: '0021BB00 took mode 0x2C with +D != 2',
}


def check_major4(elf, lib, coverage):
    cases = 0
    for flag in (0, 1, 3):
        for state in (0, 1, 5, 8, 0xC, 0x17, 2, 0x19, 0xFF):
            raw = bytearray(0x320); raw[5] = state
            o = EE(elf)
            o.write(ACTOR, bytes(raw)); o.save(SPAD_3B8D, flag, 1)
            for address in MAJOR4_ROUTINES:
                o.calls[address] = (lambda a: lambda o: (o.log.append(a), o.r.__setitem__(2, 0)))(address)
            o.run(MAJOR4, (ACTOR,))
            coverage |= o.pcs
            log, keep = [], []
            scene = StageScene(spad3B8D=flag)
            m = Major4(C.pointer(scene))
            for i, address in enumerate(MAJOR4_ROUTINES):
                fn = STATE_FN((lambda a: lambda _, actor: (log.append(a), 0)[1])(address)); keep.append(fn)
                m.routine[i] = fn
            actor = LiveActor(); C.memmove(actor.bytes, bytes(raw), 0x320)
            assert lib.em_player_stage_0015B530(C.byref(m), C.byref(actor)) == 0
            assert log == o.log, ('0015B530', flag, state, log, o.log)
            assert bytes(actor.bytes) == bytes(raw)
            for i in range(7):
                saved = m.routine[i]; m.routine[i] = STATE_FN()
                assert lib.em_player_stage_0015B530(C.byref(m), C.byref(actor)) == -1
                m.routine[i] = saved
            cases += 1
    return cases


def check_stop_sound(elf, lib):
    """0011A070's argument decode: the original frees track (arg & 0x7FFF)
    (00121A28 over D_0027E0C0 + track * 0x78) and, with arg & 0x8000, sends
    command 3 to that track's kind-2 voices (001157F0). The native forwarder
    must hand the same (track, hard) to em_sfx_driver_stop."""
    cases = 0
    for track in (0, 1, 5, 47):
        for hard in (0, 1):
            for high in (0, 0x10000):
                arg = track | hard << 15 | high
                o = EE(elf)
                o.save(0x27E0C0 + track * 0x78 + 0x32, 1, 2); o.save(0x27E0C0 + track * 0x78 + 0x2E, 0, 2)
                o.save(0x27CCC0 + 3 * 0x6A + 0x1A, 2, 2); o.save(0x27CCC0 + 3 * 0x6A + 6, track, 2)
                o.calls[MEMSET] = lambda o: (o.log.append(('memset', (o.r[4] - 0x27E0C0) // 0x78, o.r[5], o.r[6])),
                                             o.r.__setitem__(2, 0))
                o.calls[SPU_CMD] = lambda o: (o.log.append(('cmd', o.r[4], o.r[5])), o.r.__setitem__(2, 0))
                o.run(STOP, (arg,))
                freed = [entry[1] for entry in o.log if entry[0] == 'memset']
                keyed = [entry for entry in o.log if entry[0] == 'cmd']
                assert freed == [track], (arg, o.log)
                assert (keyed == [('cmd', 3, 3)]) == bool(hard), (arg, o.log)
                log = []
                stop = FN['sound_stop'](lambda _, t, h: (log.append((t, h)), 0)[1])
                callees = Callees(); callees.sound_stop = stop
                host = canonical_host(None, callees)
                assert lib.em_player_stage_stop_sound(C.byref(host), arg) == 0
                assert log == [(freed[0], int(bool(keyed)))], (arg, log)
                cases += 1
    host = canonical_host(None, Callees())
    assert lib.em_player_stage_stop_sound(C.byref(host), 3) == -1
    # Without the canonical D_008106F1 / D_00810707 byte the host is not ready.
    for field in ('d8106F1', 'd810707'):
        host = canonical_host(None, Callees()); host.callees.sound_stop = stop
        target = host.stage.contents if field == 'd8106F1' else host.globals.contents
        setattr(target, field, C.POINTER(C.c_uint8)())
        log.clear()
        assert lib.em_player_stage_stop_sound(C.byref(host), 3) == -1 and not log, field
    return cases


def check_clip_rates(elf, lib, images):
    """The exporter's output, loaded by em_player_clip_rates_load, equals
    every row of D_00248C98 in the pinned ELF, and the worker faults outside
    the table."""
    out = LANE / 'clip_rates.emcr'
    subprocess.run([sys.executable, str(ROOT / 'tools/export_player_tables.py'), '--output', str(out),
                    '--report', str(LANE / 'clip_rates.json')],
                   cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    global RATES
    rates = RATES = ClipRates()
    assert lib.em_player_clip_rates_load(C.byref(rates), str(out).encode()) == 0
    host = canonical_host(C.pointer(rates), Callees())
    for clip in range(459):
        expected = int.from_bytes(elf[0x248C98 - 0x100000 + 0x300 + 12 * clip:][:4], 'little')
        value = C.c_float()
        assert lib.em_player_stage_clip_rate(C.byref(host), clip, C.byref(value)) == 0
        assert F(value.value) == expected, ('clip rate', clip)
    for clip in (-1, 459, 0x7FFF):
        assert lib.em_player_stage_clip_rate(C.byref(host), clip, C.byref(C.c_float())) == -1
    data = out.read_bytes()
    for bad in (data[:-4], data + b'\0\0\0\0', b'EMCX' + data[4:], data[:8] + struct.pack('<I', 458) + data[12:-4]):
        assert lib.em_player_clip_rates_parse(C.byref(ClipRates()), bad, len(bad)) == -1
    # Captured cross-check (reported, not asserted: +20C may change after
    # 0015BA50 in the same frame): +34 against rate[+20C] * 1.0.
    agree = total = 0
    for _, ram in images:
        clip = struct.unpack_from('<h', ram, PLAYER + 0x20C)[0]
        if 0 <= clip < 459:
            total += 1
            agree += struct.unpack_from('<I', ram, PLAYER + 0x34)[0] == \
                int.from_bytes(elf[0x248C98 - 0x100000 + 0x300 + 12 * clip:][:4], 'little')
    out.unlink()
    (LANE / 'clip_rates.json').unlink()
    return agree, total


def check_fail_stop(lib, rng):
    """Every native routine refuses to run (-1, nothing written) when any
    worker it can reach is missing."""
    checks = 0
    for name, (_, builder, _, workers) in ROUTINES.items():
        if workers is None: continue
        case = GENERATORS[name](rng, ('fail-stop', name))
        for missing in workers + ('__stage__', '__globals__', 'd8106F1', 'd810707'):
            result, record, scene, glob, extra, log = case.native(lib, builder(case), missing=missing)
            assert result[0] == -1, (name, missing, result)
            assert record == bytes(case.raw) and not log, (name, missing)
            assert scene == case.scene and glob == case.globals, (name, missing)
            checks += 1
    return checks


# ------------------------------------------------------------------ main

def main():
    start = time.time()
    global ELF
    elf = ELF = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256, 'wrong original executable'
    lib = build_native()
    rng = random.Random(0x21C440)
    coverage = set()
    counts = {}
    images = captured_images()
    agree, rows_total = check_clip_rates(elf, lib, images)
    for index in range(len(REACTION_SCENARIOS)):
        run_case(elf, lib, 'reaction', reaction_scenario(rng, index), coverage)
    predicates = check_predicates(elf, lib, rng, coverage)
    for name, generator in GENERATORS.items():
        full, quick = COUNTS[name]
        total = reference_mode.pick(full, quick)
        for i in range(total):
            run_case(elf, lib, name, generator(rng, i), coverage)
        counts[name] = total
    captured = 0
    for label, ram in images:
        for name, case in captured_cases(ram, label, rng):
            run_case(elf, lib, name, case, coverage)
            captured += 1
    helpers = check_float_helpers(elf, lib, rng, coverage)
    major4 = check_major4(elf, lib, coverage)
    stops = check_stop_sound(elf, lib)
    fail_stop = check_fail_stop(lib, rng)

    # Every reachable instruction of every executed original function ran.
    sizes = {}
    for line in (DECOMP / 'docs/FUNCTIONS.csv').read_text().splitlines()[1:]:
        fields = line.split(',')
        sizes[int(fields[0], 16)] = int(fields[2])
    missed = {}
    for address, name in EXECUTED.items():
        need = reachable(elf, address, sizes[address]) - set(DEAD_BY_VALUE)
        gap = sorted(need - coverage)
        if gap: missed[name] = [hex(pc) for pc in gap]
    assert not missed, ('reachable original instructions never executed', missed)
    instructions = sum(len(reachable(elf, a, sizes[a]) - set(DEAD_BY_VALUE)) for a in EXECUTED)

    parts = [reference_mode.part(v, COUNTS[k][0], k + ' cases') for k, v in counts.items()]
    reference_mode.banner(*parts)
    print(f'player stage workers: {sum(counts.values())} synthetic + {captured} captured-RAM cases '
          f'({len(images)} images), {helpers} float_to_int/001B1470 values, {major4} 0015B530 '
          f'cases, {stops} 0011A070 decodes, 459 clip-rate rows (captured +34 agrees on {agree}/{rows_total}), '
          f'{fail_stop} fail-stop checks, {len(REACTION_SCENARIOS)} 0021C440 scenarios, {predicates} '
          f'predicate cases; {instructions} reachable original instructions in {len(EXECUTED)} functions all '
          f'executed ({len(DEAD_BY_VALUE)} value-dead excluded); {time.time() - start:.1f} s')


if __name__ == '__main__':
    main()
