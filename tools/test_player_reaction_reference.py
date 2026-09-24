#!/usr/bin/env python3
"""Execute the original +4 = 2 reaction states and compare em_player_reaction.c.

docs/PLAYER_REACTION.md. The user's pinned ELF supplies every instruction;
none are embedded here. The shared bounded interpreter (EE, from
tools/test_player_slide_reference.py) runs the original routines, with its
COP1 replaced here by tools/ee_float_model.py (docs/EE_FLOAT_MODEL.md: the
shared interpreter's add/sub have no pre-trim and its div.s truncates; the
shared file is not edited). A VU0 macro op in these routines fails the test.

Executed unmodified (the native translates them in em_player_reaction.c):
  0021D800 0021E240 0021E490 00223C70 0021F330 0021F850 002202C0 0021DBB0
  0021E9C0 0021EAD0 0021EF30 (the state routines) and the helpers they call:
  0017C540 0021D530 0021D250 0021D2E0 00179880 00182870 0021D490 0021C120
  0021C190 0021D1A0 0021D600 001754E0 001B1470 0011DF78.
0021D250, 0021D2E0 and 00179880 are translated once, in em_player_fall.c;
the native reaction routines reach them through em_player_reaction.c's
bridge, so this oracle checks that bridge and the owner together.
Hooked boundaries (the native's workers), scripted per case and recorded,
never simulated as a claim about the callee: 001749A0, 001749F0, 001C61D0,
001FBD50, 001B61C0, 00122BB8, 001EFD90, 001EFE00, 00175900, 00178B90,
001764E0, 00174AC0, 001C6DA0 (+ node 1), 001AEDE0, 0015C1F0, 001FAFD0,
0011E620, and 0021C270 / 0021C350 (translated by lane player-stage-workers).
A hook that takes the actor writes the case's scripted bytes into it on both
sides, so every read after a call is tested; the 0021C270 hook also writes a
scripted D_008106F1.

Compared per case: all 0x320 actor bytes, the scene bytes the routines
write (D_008106F1, D_008106F0, D_008106BC, D_00275B08), the scratchpad words
0x700038A0..AC and 0x70003A20 (0021D2E0 builds its effect point at
0x700038A0), the return code, and
every worker call in order with its arguments; and, at entry to EVERY hooked
call (before the hook runs), the same 0x320 actor bytes and four scene bytes
as the callee is handed them, so a store moved across a call (for example
+38/+21C written after 00178B90 instead of before) fails whether or not a
hook mutates that offset. The original must stay inside
the executed routines (any other callee faults), and every run must execute
every statically reachable instruction of every routine and take every
conditional branch both ways.

Default: 8,000 state cases + 400 helper cases (every routine, every
sub-state, the boundaries). EM_TEST_FULL=1: 40,000 + 6,000.
"""
import ctypes as C
import hashlib
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from test_player_slide_reference import EE, read_elf, sx32, STACK_TOP  # noqa: E402
import ee_float_model as M  # noqa: E402
import reference_mode  # noqa: E402

ACTOR = 0x680000
NODE_TABLE, NODES = 0x6D0000, 0x6D1000        # synthetic D_00275B40 pointer table / records
D_00275B40, D_00275B08 = 0x275B40, 0x275B08
D_8106F1, D_8106F0, D_81083C, D_8106BC = 0x8106F1, 0x8106F0, 0x81083C, 0x8106BC
D_810E70, D_810E74 = 0x810E70, 0x810E74
SPAD_3B8D, SPAD_3B7E, SPAD_3B7C, SPAD_3B76 = 0x70003B8D, 0x70003B7E, 0x70003B7C, 0x70003B76
SPAD_38A0, SPAD_3A20 = 0x700038A0, 0x70003A20

# The translated routines (entry, size in bytes).
STATES = {
    '0021D800': (0x21D800, 0x3B0), '0021E240': (0x21E240, 0x24C), '0021E490': (0x21E490, 0x1B4),
    '00223C70': (0x223C70, 0x614), '0021F330': (0x21F330, 0x520), '0021F850': (0x21F850, 0x2EC),
    '002202C0': (0x2202C0, 0x5FC), '0021DBB0': (0x21DBB0, 0x688), '0021E9C0': (0x21E9C0, 0x10C),
    '0021EAD0': (0x21EAD0, 0x460), '0021EF30': (0x21EF30, 0x3F8),
}
HELPERS = {
    '0017C540': (0x17C540, 0x40), '0021D530': (0x21D530, 0xC4), '0021D250': (0x21D250, 0x8C),
    '0021D2E0': (0x21D2E0, 0x1A4), '00179880': (0x179880, 0x50), '00182870': (0x182870, 0x1F4),
    '0021D490': (0x21D490, 0x50), '0021C120': (0x21C120, 0x70), '0021C190': (0x21C190, 0x6C),
    '0021D1A0': (0x21D1A0, 0xAC), '0021D600': (0x21D600, 0x38), '001754E0': (0x1754E0, 0xD0),
    '001B1470': (0x1B1470, 0x9C), '0011DF78': (0x11DF78, 0x1C),
}
RANGES = sorted((entry, entry + size) for entry, size in list(STATES.values()) + list(HELPERS.values()))

REQUEST, ARBITER, FRAMES, SOUND, RUMBLE = 0x1749A0, 0x1749F0, 0x1C61D0, 0x1FBD50, 0x1B61C0
RANDOM, EFFECT, ATTACH = 0x122BB8, 0x1EFD90, 0x1EFE00
FLOOR, TRANSLATE, PROBES, HEADING, SKELETON = 0x175900, 0x178B90, 0x1764E0, 0x174AC0, 0x1C6DA0
FADE, MODEL_REFRESH, STREAM_CHECK, ATAN2 = 0x1AEDE0, 0x15C1F0, 0x1FAFD0, 0x11E620
W0021C270, W0021C350 = 0x21C270, 0x21C350

LIBC = C.CDLL(None)
LIBC.atan2f.argtypes = [C.c_float, C.c_float]
LIBC.atan2f.restype = C.c_float
_F, _I = struct.Struct('<f'), struct.Struct('<I')


def fbits(value):
    return _I.unpack(_F.pack(value))[0]


def ffloat(word):
    return _F.unpack(_I.pack(word & 0xFFFFFFFF))[0]


def s(value):
    value &= 0xFFFFFFFF
    return value - 0x100000000 if value & 0x80000000 else value


def host_atan2(ybits, xbits):
    """The host model both sides bind for SDK 0011E620."""
    return fbits(LIBC.atan2f(ffloat(ybits), ffloat(xbits)))


# ---------------------------------------------------------------- the EE ---

class ReactionEE(EE):
    """The shared EE with COP1 through ee_float_model, execution confined to
    the translated routines, and instruction / branch coverage recorded."""

    def __init__(self, elf):
        super().__init__(elf)
        self.acc_bits = 0
        self.executed = set()
        self.branches = set()      # (pc, taken)

    def reset(self):
        self.r = [0] * 32
        self.rh = [0] * 32
        self.f = [0] * 32
        self.hi = self.lo = 0
        self.acc_bits = 0
        self.cond = False
        self.r[28] = 0x27D370
        self.r[29] = STACK_TOP

    def macro(self, word):
        raise AssertionError(('unexpected VU0 macro op in a reaction routine', hex(word)))

    def execute(self, word, pc):
        for low, high in RANGES:
            if low <= pc < high:
                break
        else:
            raise AssertionError(('the original left the translated routines at', hex(pc)))
        self.executed.add(pc)
        super().execute(word, pc)

    def branch(self, word, pc):
        result = super().branch(word, pc)
        if result is not None:
            self.executed.add(pc)
            self.branches.add((pc, result[0]))
        return result

    def cop1(self, word, pc):
        rs, rt = word >> 21 & 31, word >> 16 & 31
        fs, fd, fn = word >> 11 & 31, word >> 6 & 31, word & 63
        f = self.f
        if rs in (0, 2, 4, 6):                       # mfc1 / cfc1 / mtc1 / ctc1
            return super().cop1(word, pc)
        if rs == 20:
            if fn == 32:                             # cvt.s.w
                f[fd] = M.ee_cvt_s_w(f[fs] & 0xFFFFFFFF); return
            raise AssertionError(('cvt', fn, hex(pc)))
        if rs != 16:
            raise AssertionError(('COP1', rs, hex(pc)))
        x, y = f[fs] & 0xFFFFFFFF, f[rt] & 0xFFFFFFFF
        if fn == 0: f[fd] = M.ee_add(x, y)
        elif fn == 1: f[fd] = M.ee_sub(x, y)
        elif fn == 2: f[fd] = M.ee_mul(x, y)
        elif fn == 3: f[fd] = M.ee_div(x, y)
        elif fn == 6: f[fd] = M.ee_mov(x)
        elif fn == 7: f[fd] = M.ee_neg(x)
        elif fn == 24: self.acc_bits = M.ee_adda(x, y)
        elif fn == 25: self.acc_bits = M.ee_suba(x, y)
        elif fn == 26: self.acc_bits = M.ee_mula(x, y)
        elif fn == 28: f[fd] = M.ee_madd(self.acc_bits, x, y)
        elif fn == 29: f[fd] = M.ee_msub(self.acc_bits, x, y)
        elif fn == 36: f[fd] = M.ee_cvt_w_s(x)
        elif fn == 48: self.cond = False
        elif fn == 50: self.cond = bool(M.ee_c_eq(x, y))
        elif fn == 52: self.cond = bool(M.ee_c_lt(x, y))
        elif fn == 54: self.cond = bool(M.ee_c_le(x, y))
        else: raise AssertionError(('FPU op outside the model', fn, hex(pc)))


# ------------------------------------------------------ static coverage ---

def static_reachable(ee, entry, size):
    """Every instruction of [entry, entry + size) that some path from the
    entry can execute (branch targets, fall-throughs, delay slots, jump-table
    targets), and the conditional branches among them."""
    end = entry + size
    reach, branches, work = set(), set(), [entry]
    while work:
        pc = work.pop()
        if pc in reach or not entry <= pc < end:
            continue
        reach.add(pc)
        word = ee.load(pc)
        op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
        imm = word & 0xFFFF
        target = pc + 4 + ((imm - 0x10000 if imm & 0x8000 else imm) << 2)
        if op == 0 and word & 63 == 8:                                   # jr
            reach.add(pc + 4)
            if rs != 31:
                work.extend(jump_table(ee, pc, entry))
            continue
        if op == 2:                                                      # j
            reach.add(pc + 4); work.append((pc & 0xF0000000) | ((word & 0x3FFFFFF) << 2)); continue
        if op in (4, 5, 6, 7, 20, 21, 22, 23) or op == 1 or (op == 17 and rs == 8):
            unconditional = op == 4 and rs == 0 and rt == 0
            reach.add(pc + 4)
            work.append(target)
            if not unconditional:
                branches.add(pc)
                work.append(pc + 8)
            continue
        work.append(pc + 4)
    return reach, branches


def jump_table(ee, jr_pc, entry):
    """The targets of a `jr` through a jump table: sltiu at, r, N ... lui /
    addiu table ... lw ... jr, all within the preceding instructions."""
    count = table_hi = table = None
    for pc in range(jr_pc - 4, max(entry, jr_pc - 64) - 4, -4):
        word = ee.load(pc)
        op = word >> 26
        if op == 11 and count is None:                                   # sltiu
            count = word & 0xFFFF
        elif op == 15 and table_hi is None:                              # lui
            table_hi = (word & 0xFFFF) << 16
        elif op == 9 and table is None and (word >> 21 & 31) != 29:      # addiu (lo)
            lo = word & 0xFFFF
            table = lo - 0x10000 if lo & 0x8000 else lo
    assert count and table_hi is not None and table is not None, ('jump table', hex(jr_pc))
    base = table_hi + table
    return [ee.load(base + 4 * i) for i in range(count)]


# Statically reachable code no input can execute, with the proof:
# instruction -> reason; conditional branch -> the direction that cannot occur.
INFEASIBLE = {}
INFEASIBLE_BRANCHES = {}


def coverage_check(executed, branches):
    """Every statically reachable instruction of every routine executed, and
    every conditional branch taken both ways, except the INFEASIBLE lists."""
    missing, one_way = {}, {}

    # j / jal / jr run in the interpreter's loop, not through execute():
    # one counts as executed when its delay slot did.
    def jump(pc):
        word = EE_.load(pc)
        return word >> 26 in (2, 3) or (word >> 26 == 0 and word & 63 in (8, 9))
    executed = executed | {pc - 4 for pc in executed if jump(pc - 4)}
    for name, (entry, size) in list(STATES.items()) + list(HELPERS.items()):
        reach, conditional = static_reachable(EE_, entry, size)
        gap = sorted(reach - executed - set(INFEASIBLE))
        if gap: missing[name] = [hex(pc) for pc in gap[:8]]
        for pc in conditional:
            seen = {taken for (at, taken) in branches if at == pc} | INFEASIBLE_BRANCHES.get(pc, set())
            if seen != {True, False}:
                one_way.setdefault(name, []).append((hex(pc), sorted(seen)))
    return missing, one_way


# ------------------------------------------------------- native binding ---

class LiveActor(C.Structure):
    _fields_ = [('bytes', C.c_uint8 * 0x320), ('link_owner', C.c_void_p), ('link_prev', C.c_void_p),
                ('link_flags', C.c_uint8), ('link_type', C.c_uint8)]


class Scene(C.Structure):
    # Float fields as raw words (same size and alignment as float).
    _fields_ = [('root4', C.c_uint32), ('root8', C.c_uint32), ('node2', C.c_uint32 * 4),
                ('node3', C.c_uint32 * 4), ('node7', C.c_uint32 * 4), ('pad_held', C.c_uint16),
                ('pad_pressed', C.c_uint16), ('spad3B76', C.c_uint16), ('spad3B7C', C.c_uint16),
                ('spad3B7E', C.c_uint16), ('scripted', C.c_uint8), ('d81083C', C.c_uint8),
                ('d8106F1', C.POINTER(C.c_uint8)), ('d8106F0', C.c_uint8), ('d8106BC', C.c_uint8),
                ('d275B08', C.c_int32)]


A = C.POINTER(LiveActor)
REQUEST_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.c_int, C.c_int, C.c_float)
ARBITER_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.c_int, C.c_float, C.c_float)
FRAMES_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.c_int, C.POINTER(C.c_int))
SOUND_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.c_uint)
RUMBLE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_int, C.c_int, C.c_int)
RANDOM_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_uint32))
EFFECT_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32), C.POINTER(C.c_uint32))
ATTACH_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.c_uint32, C.POINTER(C.c_uint32))
FLOOR_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.c_int, C.POINTER(C.c_int))
ARG_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.c_int)
ACTOR_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A)
SKELETON_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, A, C.POINTER(C.c_uint32))
FADE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_int)
PLAIN_FN = C.CFUNCTYPE(C.c_int, C.c_void_p)
ATAN2_FN = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_float, C.c_float)
REFRESH_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Scene))


class Scratch(C.Structure):
    # EmPlayerLandScratch (em_player_fall.h).
    _fields_ = [('s38A0', C.c_uint32 * 4), ('s3A20', C.c_uint32)]


class Workers(C.Structure):
    _fields_ = [('context', C.c_void_p), ('request', REQUEST_FN), ('arbiter', ARBITER_FN),
                ('clip_frames', FRAMES_FN), ('sound', SOUND_FN), ('rumble', RUMBLE_FN),
                ('random', RANDOM_FN), ('effect', EFFECT_FN), ('attach', ATTACH_FN),
                ('floor', FLOOR_FN), ('translate', ARG_FN), ('probes', ACTOR_FN),
                ('heading', ARG_FN), ('skeleton', SKELETON_FN), ('fade', FADE_FN),
                ('model_refresh', ACTOR_FN), ('stream_check', PLAIN_FN), ('atan2', ATAN2_FN),
                ('w0021C270', ACTOR_FN), ('w0021C350', ACTOR_FN), ('scratch', C.POINTER(Scratch))]


class Reaction(C.Structure):
    _fields_ = [('workers', Workers), ('scene', C.POINTER(Scene)), ('refresh', REFRESH_FN),
                ('refresh_context', C.c_void_p)]


def build_native():
    out = ROOT / 'build/player_reaction_reference'
    out.mkdir(parents=True, exist_ok=True)
    lib = out / ('reaction.dylib' if sys.platform == 'darwin' else 'reaction.so')
    command = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
               '-fPIC', '-Isrc', 'src/game/em_player_reaction.c', 'src/game/em_player_fall.c',
               '-o', str(lib)]
    digest = hashlib.sha256(' '.join(command).encode())
    for path in ('src/game/em_player_reaction.c', 'src/game/em_player_reaction.h',
                 'src/game/em_player_fall.c', 'src/game/em_player_fall.h',
                 'src/game/em_ee_float.h', 'src/game/em_player_floor.h'):
        digest.update((ROOT / path).read_bytes())
    stamp = Path(str(lib) + '.sha256')
    if not (lib.exists() and stamp.exists() and stamp.read_text() == digest.hexdigest()):
        subprocess.run(command, cwd=ROOT, check=True)
        stamp.write_text(digest.hexdigest())
    native = C.CDLL(str(lib))
    S, W = C.POINTER(Scene), C.POINTER(Workers)
    for name in STATES:
        getattr(native, 'em_player_reaction_' + name).argtypes = [A, S, W]
        getattr(native, 'em_player_reaction_live_' + name).argtypes = [C.c_void_p, A]
    for name, types in (('0017C540', [A]), ('0021D530', [A, S])):
        getattr(native, 'em_player_reaction_' + name).argtypes = types
        getattr(native, 'em_player_reaction_' + name).restype = None
    native.em_player_fall_drop.argtypes = [A]          # 00179880's one translation
    native.em_player_fall_drop.restype = None
    native.em_player_reaction_0021D250.argtypes = [A, C.c_int, W]
    native.em_player_reaction_0021D2E0.argtypes = [A, C.c_int16, C.c_int, W]
    native.em_player_reaction_00182870.argtypes = [A, C.c_int, W]
    native.em_player_reaction_001754E0.argtypes = [A, S, C.POINTER(Scratch), C.c_int,
                                                    C.POINTER(C.c_int)]
    native.em_player_reaction_w0021D2E0.argtypes = [C.c_void_p, A, C.c_int, C.c_int]
    native.em_player_reaction_w0021C190.argtypes = [C.c_void_p, A, C.POINTER(C.c_int)]
    native.em_player_reaction_wrap.argtypes = [C.c_float]
    native.em_player_reaction_wrap.restype = C.c_float
    return native


# ----------------------------------------------------------- one case ---

# Actor offsets the hooks may overwrite (the bytes callees really change or
# that the routines read after a call), and the values they pick from.
MUTABLE = (0x6, 0x7, 0xD, 0xF, 0x5, 0x23A, 0x236, 0x234, 0x1F1, 0x302, 0x25C, 0x319, 0x31F,
           0x200, 0x201, 0x3C, 0x3D, 0x3E, 0x3F, 0xB4, 0xB5, 0xB6, 0xB7, 0x220, 0x221, 0x222, 0x223,
           0x224, 0x227, 0x228, 0x22B, 0x2EC, 0x2EF, 0x250, 0x253, 0x28, 0x29)


def word_bytes(offset, word):
    return {offset + i: word >> (8 * i) & 0xFF for i in range(4)}


class Script:
    """One case's scripted callee results and hook mutations, consumed in
    call order by both sides."""

    def __init__(self, rng):
        self.floor = [rng.choice([0, 0, 1, 1, 2]) for _ in range(8)]
        self.random = [rng.getrandbits(32) for _ in range(4)]
        self.frames = [rng.choice([0, 45, 120, 37, -3, (1 << 24) + 3, 0x7FFFFFFF]) for _ in range(2)]
        self.handles = [rng.choice([0, 0x00DEAD00, rng.getrandbits(32)]) for _ in range(4)]
        self.node1 = [[fbits(rng.uniform(-300, 300)) for _ in range(3)] for _ in range(2)]
        self.flags = [rng.choice([0, 1, 1]) for _ in range(2)]   # D_008106F1 after 0021C270
        self.mutations = []
        for _ in range(24):
            if rng.randrange(3) == 0:
                self.mutations.append({})
                continue
            change = {}
            for offset in rng.sample(MUTABLE, rng.randrange(1, 5)):
                change[offset] = rng.randrange(256)
            if rng.randrange(4) == 0:   # an angle 001754E0 wraps: finite (001B1470 loops on Inf/NaN)
                change.update(word_bytes(0x24C, fbits(rng.uniform(-10, 10))))
            if rng.randrange(3) == 0:   # the health 0021C350 leaves: often <= 0
                change.update(word_bytes(0x220, fbits(rng.choice([-1.0, 0.0, 5.0, 50.0]))))
            if rng.randrange(3) == 0:   # the meter 0021C270 leaves: around 100
                change.update(word_bytes(0x228, fbits(rng.choice([100.0, 99.99999, 150.0, 20.0]))))
            if rng.randrange(2):        # a flag word with / without 0x1000 and 0x8000
                word = rng.choice([0, 0x1000, 0x8000, 0x9000, rng.getrandbits(32)])
                change.update(word_bytes(0x200, word))
            self.mutations.append(change)

    def copy(self):
        other = Script.__new__(Script)
        for key, value in self.__dict__.items():
            setattr(other, key, [list(v) if isinstance(v, list) else v for v in value])
        return other

    @staticmethod
    def pop(queue, default):
        return queue.pop(0) if queue else default


class Original:
    """The hooks on the original side; every call is recorded, with the actor
    and scene bytes as they stand at the call (before the hook runs)."""

    def __init__(self, ee, script):
        self.ee, self.s, self.log, self.snaps = ee, script, [], []
        hooks = {
            REQUEST: lambda o: self.actor_call(('request', s(o.r[5]), s(o.r[6]), o.f[12] & 0xFFFFFFFF)),
            ARBITER: lambda o: self.actor_call(('arbiter', s(o.r[5]), o.f[12] & 0xFFFFFFFF,
                                                o.f[13] & 0xFFFFFFFF)),
            FRAMES: self.frames, SOUND: self.sound,
            RUMBLE: lambda o: self.plain(('rumble', s(o.r[4]), s(o.r[5]), s(o.r[6]), s(o.r[7]))),
            RANDOM: self.random, EFFECT: self.effect, ATTACH: self.attach,
            FLOOR: self.floor,
            TRANSLATE: lambda o: self.actor_call(('translate', s(o.r[5]))),
            PROBES: lambda o: self.actor_call(('probes',)),
            HEADING: lambda o: self.actor_call(('heading', s(o.r[5]))),
            SKELETON: self.skeleton,
            FADE: lambda o: self.plain(('fade', s(o.r[4]), s(o.r[5]))),
            MODEL_REFRESH: lambda o: self.actor_call(('model_refresh',)),
            STREAM_CHECK: lambda o: self.plain(('stream_check',)),
            ATAN2: self.atan2,
            W0021C270: self.w0021C270,
            W0021C350: lambda o: self.actor_call(('w0021C350',)),
        }
        ee.hooks = {pc: self.snapped(pc, hook) for pc, hook in hooks.items()}

    def snapped(self, pc, hook):
        def run(o):
            self.snaps.append((pc, o.read(ACTOR, 0x320), o.load(D_8106F1, 1), o.load(D_8106F0, 1),
                               o.load(D_8106BC, 1), o.load(D_00275B08, 4)))
            hook(o)
        return run

    def mutate(self):
        for offset, value in Script.pop(self.s.mutations, {}).items():
            self.ee.save(ACTOR + offset, value, 1)

    def plain(self, entry):
        self.log.append(entry)
        self.ee.r[2] = 0

    def actor_call(self, entry):
        assert self.ee.r[4] & 0xFFFFFFFF == ACTOR, (entry, 'a0 is not the actor', hex(self.ee.r[4]))
        self.log.append(entry)
        self.mutate()
        self.ee.r[2] = 0

    def w0021C270(self, o):
        self.actor_call(('w0021C270',))
        o.save(D_8106F1, Script.pop(self.s.flags, 0), 1)

    def frames(self, o):
        self.log.append(('frames', o.r[4] & 0xFFFFFFFF, s(o.r[5])))
        o.r[2] = sx32(Script.pop(self.s.frames, 0))

    def sound(self, o):
        assert o.r[4] & 0xFFFFFFFF == ACTOR, ('sound a0', hex(o.r[4]))
        assert o.r[6] & 0xFFFFFFFF == 0 and o.f[12] & 0xFFFFFFFF == 0x43960000, ('sound args',)
        self.log.append(('sound', o.r[5] & 0xFFFFFFFF))
        o.r[2] = 0

    def random(self, o):
        self.log.append(('random',))
        o.r[2] = sx32(Script.pop(self.s.random, 0))

    def effect(self, o):
        position = tuple(o.load(o.r[5] + 4 * i) for i in range(4))
        rotation = tuple(o.load(o.r[6] + 4 * i) for i in range(4))
        self.log.append(('effect', o.r[4] & 0xFFFFFFFF, position, rotation))
        o.r[2] = 0

    def attach(self, o):
        assert o.r[5] & 0xFFFFFFFF == ACTOR, ('attach a1', hex(o.r[5]))
        self.log.append(('attach', o.r[4] & 0xFFFFFFFF))
        o.r[2] = sx32(Script.pop(self.s.handles, 0))

    def floor(self, o):
        self.actor_call(('floor', s(o.r[5])))
        o.r[2] = sx32(Script.pop(self.s.floor, 0))

    def skeleton(self, o):
        self.actor_call(('skeleton',))
        node = Script.pop(self.s.node1, [0, 0, 0])
        record = o.load(NODE_TABLE + 4)
        for i in range(3): o.save(record + 0xC0 + 4 * i, node[i])

    def atan2(self, o):
        y, x = o.f[12] & 0xFFFFFFFF, o.f[13] & 0xFFFFFFFF
        self.log.append(('atan2', y, x))
        o.f[0] = host_atan2(y, x)


class Native:
    """The same workers on the native side."""

    def __init__(self, script, flag):
        self.s, self.log, self.keep, self.flag = script, [], [], flag
        self.snaps, self.bound = [], None
        w = self.workers = Workers()
        self.scratch = Scratch()
        w.scratch = C.pointer(self.scratch)

        def keep(kind, fn, pc):
            def run(*args):
                self.snap(pc)
                return fn(*args)
            callback = kind(run)
            self.keep.append(callback)
            return callback

        def actor_call(entry, actor):
            self.log.append(entry)
            for offset, value in Script.pop(self.s.mutations, {}).items():
                actor.contents.bytes[offset] = value
            return 0

        w.request = keep(REQUEST_FN, lambda _, a, clip, force, blend:
                         actor_call(('request', clip, force, fbits(blend)), a), REQUEST)
        w.arbiter = keep(ARBITER_FN, lambda _, a, clip, blend, frame:
                         actor_call(('arbiter', clip, fbits(blend), fbits(frame)), a), ARBITER)

        def frames(_, a, clip, out):
            word = struct.unpack_from('<I', bytes(a.contents.bytes), 0x40)[0]
            self.log.append(('frames', word, clip))
            out[0] = Script.pop(self.s.frames, 0)
            return 0
        w.clip_frames = keep(FRAMES_FN, frames, FRAMES)
        w.sound = keep(SOUND_FN, lambda _, a, sid: (self.log.append(('sound', sid)), 0)[1], SOUND)
        w.rumble = keep(RUMBLE_FN, lambda _, x, y, z, u: (self.log.append(('rumble', x, y, z, u)), 0)[1],
                        RUMBLE)

        def rand(_, out):
            self.log.append(('random',)); out[0] = Script.pop(self.s.random, 0); return 0
        w.random = keep(RANDOM_FN, rand, RANDOM)
        w.effect = keep(EFFECT_FN, lambda _, eid, p, r: (self.log.append(
            ('effect', eid, tuple(p[i] for i in range(4)), tuple(r[i] for i in range(4)))), 0)[1], EFFECT)

        def attach(_, a, aid, out):
            self.log.append(('attach', aid)); out[0] = Script.pop(self.s.handles, 0) & 0xFFFFFFFF
            return 0
        w.attach = keep(ATTACH_FN, attach, ATTACH)

        def floor(_, a, search, out):
            actor_call(('floor', search), a); out[0] = Script.pop(self.s.floor, 0); return 0
        w.floor = keep(FLOOR_FN, floor, FLOOR)
        w.translate = keep(ARG_FN, lambda _, a, arg: actor_call(('translate', arg), a), TRANSLATE)
        w.probes = keep(ACTOR_FN, lambda _, a: actor_call(('probes',), a), PROBES)
        w.heading = keep(ARG_FN, lambda _, a, arg: actor_call(('heading', arg), a), HEADING)

        def skeleton(_, a, node1):
            actor_call(('skeleton',), a)
            node = Script.pop(self.s.node1, [0, 0, 0])
            for i in range(3): node1[i] = node[i]
            return 0
        w.skeleton = keep(SKELETON_FN, skeleton, SKELETON)
        w.fade = keep(FADE_FN, lambda _, x, y: (self.log.append(('fade', x, y)), 0)[1], FADE)
        w.model_refresh = keep(ACTOR_FN, lambda _, a: actor_call(('model_refresh',), a), MODEL_REFRESH)
        w.stream_check = keep(PLAIN_FN, lambda _: (self.log.append(('stream_check',)), 0)[1], STREAM_CHECK)

        def atan2(_, y, x):
            y, x = fbits(y), fbits(x)          # finite operands: exact through double
            self.log.append(('atan2', y, x)); return ffloat(host_atan2(y, x))
        w.atan2 = keep(ATAN2_FN, atan2, ATAN2)

        def w0021C270(_, a):
            actor_call(('w0021C270',), a)
            self.flag.value = Script.pop(self.s.flags, 0)
            return 0
        w.w0021C270 = keep(ACTOR_FN, w0021C270, W0021C270)
        w.w0021C350 = keep(ACTOR_FN, lambda _, a: actor_call(('w0021C350',), a), W0021C350)

    def bind(self, actor, scene):
        """The case's actor and scene, which every worker snapshots on entry."""
        self.bound = (actor, scene)

    def snap(self, pc):
        if self.bound is None:
            return
        actor, scene = self.bound
        self.snaps.append((pc, bytes(actor.bytes), self.flag.value, scene.d8106F0, scene.d8106BC,
                           scene.d275B08 & 0xFFFFFFFF))


# Sub-state values each routine dispatches on (+6), plus ones it ignores.
SUBSTATES = {
    '0021D800': [0, 1, 2], '0021E240': [0, 1, 2, 3], '0021E490': [0, 1, 2, 3, 4],
    '00223C70': [0, 1, 2, 10, 11, 12, 20, 21, 22, 23, 30, 31, 32, 33, 3, 40],
    '0021F330': [0, 1, 2, 3, 10, 11, 12, 13, 4, 14], '0021F850': list(range(10)),
    '002202C0': list(range(15)), '0021DBB0': list(range(15)), '0021E9C0': [0, 1, 2],
    '0021EAD0': list(range(10)), '0021EF30': list(range(10)),
}

THRESHOLDS = (80.0, 50.0, 16.0, 22.0, 35.0, 60.0, 24.0, 18.0, 10.0, 30.0)
SPECIAL = (0x80000000, 0x00000001, 0x80000001, 0x7F800000, 0xFF800000, 0x7FC00000, 0x7F7FFFFF)


def near(rng, value):
    word = fbits(value)
    return rng.choice([word, word + 1, word - 1 if value else 0x80000000])


def random_actor(rng, routine):
    raw = bytearray(rng.getrandbits(8) for _ in range(0x320))
    put = lambda at, word: struct.pack_into('<I', raw, at, word & 0xFFFFFFFF)
    fl = lambda at, value: struct.pack_into('<f', raw, at, value)
    raw[6] = rng.choice(SUBSTATES[routine])
    raw[7] = rng.choice([0, 0, 1, 2, 3, 4])
    put(0x200, rng.choice([0, 0x1000, 0x8000, 0x9000, rng.getrandbits(32)]))
    put(0x3C, near(rng, rng.choice(THRESHOLDS)) if rng.randrange(3) else fbits(rng.uniform(-10, 200)))
    put(0x220, rng.choice([0, 0x80000000, 1, near(rng, 35.0), near(rng, 60.0), fbits(100.0),
                           fbits(rng.uniform(-50, 150)), near(rng, 0.0), rng.choice(SPECIAL)]))
    for at in (0x224, 0x22C):
        put(at, rng.choice([0, 0, 0x80000000, 1, fbits(1.0), fbits(rng.uniform(0.1, 80)),
                            rng.choice(SPECIAL)]))
    put(0x228, rng.choice([near(rng, 100.0), fbits(50.0), fbits(150.0), fbits(rng.uniform(0, 200)),
                           rng.choice(SPECIAL)]))
    put(0x2EC, rng.choice([near(rng, -4.0), near(rng, -3.96), 0, fbits(rng.uniform(-10, 3)),
                           rng.choice(SPECIAL)]))
    fl(0xB4, rng.uniform(-250, 250)) if rng.randrange(4) else put(0xB4, rng.choice(SPECIAL))
    for at in (0xC4, 0x24C, 0x26C):
        fl(at, rng.choice([rng.uniform(-10, 10), 3.1415927, -3.1415927, 1.5707964, 0.0]))
    for at in (0x70, 0x78):
        fl(at, rng.choice([rng.uniform(-2, 2), 0.0, -0.0, 1.0]))
    fl(0x250, rng.uniform(-100, 300))
    raw[0x1F0] = rng.choice([0xE, 0x17, 0x3E, rng.getrandbits(8)])
    raw[0x1F1] = rng.choice([0, 1, 2, 3, 4, 5])
    raw[0x236] = rng.choice([0, 1])
    raw[0x234] = rng.choice([0, 1, 2])
    raw[0x237] = rng.choice([0, 0, 1])
    raw[0x319] = rng.choice([0, 1])
    raw[5] = rng.choice([0x17, 0x18, 0, 2, rng.getrandbits(8)])
    raw[0x23A] = rng.choice([0x5D, 0x5D, 0, 1, 2, 3, 4, 5, 6, 7, 8, 0x5A, 0x5B, 0x5C, 0xD, 0xE,
                             rng.getrandbits(8)])
    raw[0x23C] = rng.choice([0, 1, 2])
    raw[0xF] = rng.choice([0, 2, 0x63, 0xB, 3, rng.getrandbits(8)])
    raw[0xD] = rng.choice([0, 1])
    raw[0x302] = rng.choice([0, 1])
    raw[0x25C] = rng.choice([0, 1])
    raw[0x25F] = rng.choice([0, 0, 2])
    struct.pack_into('<H', raw, 0x300, rng.choice([0, 0, 0x8000, 0x7FFF]))
    raw[0x31F] = rng.choice([0, 0, 1, 0x3C])
    raw[0x23F] = rng.choice([0, 1])
    raw[0x2FE] = rng.choice([0, 1, 0x80, 0xFF])
    struct.pack_into('<h', raw, 0x28, rng.choice([0, 0, 1, 5, 6, -1, 0x7FFF]))
    return raw


def random_scene(rng):
    """The scene, and the shared D_008106F1 byte its pointer names."""
    scene, flag = Scene(), C.c_uint8(rng.choice([0, 1]))
    scene.d8106F1 = C.pointer(flag)
    scene.root4, scene.root8 = fbits(rng.uniform(-20, 20)), fbits(rng.uniform(-20, 20))
    for name in ('node2', 'node3', 'node7'):
        vector = getattr(scene, name)
        for i in range(4): vector[i] = fbits(rng.uniform(-300, 300))
    scene.pad_held = rng.choice([0, 0x10, 0x20, 0x30, rng.getrandbits(16)])
    scene.pad_pressed = rng.choice([0, 0x40, rng.getrandbits(16)])
    scene.spad3B7E, scene.spad3B7C, scene.spad3B76 = 0x10, 0x20, 0x40
    if rng.randrange(4) == 0:
        scene.spad3B7E, scene.spad3B7C, scene.spad3B76 = (rng.getrandbits(16) for _ in range(3))
    scene.scripted = rng.choice([0, 0, 0, 3])
    scene.d81083C = rng.choice([0, 1, 1])
    scene.d8106F0, scene.d8106BC = rng.getrandbits(8), rng.getrandbits(8)
    scene.d275B08 = rng.getrandbits(31)
    return scene, flag


def case_spad(tag):
    """The scratchpad words 0x700038A0..AC and 0x70003A20 a case starts
    from (their own generator, so the case's other draws are unchanged)."""
    rng = random.Random('spad/%s' % (tag,))
    return [rng.getrandbits(32) for _ in range(5)]


def load_spad(ee, native, words):
    for i in range(4):
        ee.save(SPAD_38A0 + 4 * i, words[i])
        native.scratch.s38A0[i] = words[i]
    ee.save(SPAD_3A20, words[4])
    native.scratch.s3A20 = words[4]


def load_case(ee, raw, scene, flag):
    ee.reset()
    ee.write(ACTOR, bytes(raw))
    ee.save(D_00275B40, NODE_TABLE)
    for i in range(8): ee.save(NODE_TABLE + 4 * i, NODES + 0x100 * i)
    ee.save(NODES + 4, scene.root4); ee.save(NODES + 8, scene.root8)
    for n, name in ((2, 'node2'), (3, 'node3'), (7, 'node7')):
        vector = getattr(scene, name)
        for i in range(4): ee.save(NODES + 0x100 * n + 0xC0 + 4 * i, vector[i])
    ee.save(D_810E70, scene.pad_held, 2); ee.save(D_810E74, scene.pad_pressed, 2)
    ee.save(SPAD_3B76, scene.spad3B76, 2); ee.save(SPAD_3B7C, scene.spad3B7C, 2)
    ee.save(SPAD_3B7E, scene.spad3B7E, 2); ee.save(SPAD_3B8D, scene.scripted, 1)
    ee.save(D_81083C, scene.d81083C, 1); ee.save(D_8106F1, flag.value, 1)
    ee.save(D_8106F0, scene.d8106F0, 1); ee.save(D_8106BC, scene.d8106BC, 1)
    ee.save(D_00275B08, scene.d275B08)


SNAP_FIELDS = ('D_008106F1', 'D_008106F0', 'D_008106BC', 'D_00275B08')
SNAPS_COMPARED = [0]


def compare(ee, actor, scene, flag, where, original, native):
    assert native.log == original.log, (where, 'calls', original.log, native.log)
    # The state each callee is handed: all 0x320 actor bytes and the shared
    # scene bytes at entry to every call, so a store moved across a call fails.
    assert native.bound is not None, (where, 'native side not bound to the case')
    assert len(native.snaps) == len(original.snaps), (where, 'call snapshots', len(original.snaps),
                                                       len(native.snaps))
    SNAPS_COMPARED[0] += len(original.snaps)
    for index, (want, have) in enumerate(zip(original.snaps, native.snaps)):
        assert want[0] == have[0], (where, 'call', index, hex(want[0]), hex(have[0]))
        if want[1] != have[1]:
            diff = [k for k in range(0x320) if want[1][k] != have[1][k]]
            raise AssertionError((where, 'actor differs at call', index, hex(want[0]),
                                  [hex(k) for k in diff[:24]]))
        for name, w, h in zip(SNAP_FIELDS, want[2:], have[2:]):
            assert w == h, (where, name, 'differs at call', index, hex(want[0]), w, h)
    image = bytes(actor.bytes)
    want = ee.read(ACTOR, 0x320)
    if image != want:
        diff = [hex(k) for k in range(0x320) if image[k] != want[k]]
        raise AssertionError((where, 'actor bytes differ at', diff[:24]))
    for name, have, address, size in (('D_008106F1', flag.value, D_8106F1, 1),
                                      ('D_008106F0', scene.d8106F0, D_8106F0, 1),
                                      ('D_008106BC', scene.d8106BC, D_8106BC, 1),
                                      ('D_00275B08', scene.d275B08 & 0xFFFFFFFF, D_00275B08, 4)):
        assert have == ee.load(address, size), (where, name, have, ee.load(address, size))
    spad = [ee.load(SPAD_38A0 + 4 * i) for i in range(4)] + [ee.load(SPAD_3A20)]
    mine = list(native.scratch.s38A0) + [native.scratch.s3A20]
    assert mine == spad, (where, 'scratchpad 0x700038A0..AC / 0x70003A20', [hex(v) for v in spad],
                          [hex(v) for v in mine])


# Deterministic scenarios, run first for each routine (the rest are random):
# the paths a random draw reaches too rarely for the default run.
# raw: offset -> byte, or ('f', float) / ('w', word) / ('h', short); scene:
# field -> value, 'flag' for D_008106F1; script: queue -> list.
def _sc(raw=None, scene=None, **script):
    return {'raw': raw or {}, 'scene': scene or {}, 'script': script}


_GRABBED = {6: 1, 0x2FE: 0, 0x23F: 0, 0x28: ('h', 0), 0x200: ('w', 0)}
_HELD = {'d81083C': 1, 'scripted': 0, 'pad_pressed': 0}
# 0021F330 sub-state 1 calls heading, then w0021C350 or w0021C270: the second
# mutation is what that worker leaves.
_NONE = [{}] * 8
SCENARIOS = {
    '0021F330': [
        _sc({**_GRABBED, 0x224: ('f', 10.0), 0x234: 1}, _HELD,
            mutations=[{}, word_bytes(0x220, fbits(-1.0))] + _NONE),
        _sc({**_GRABBED, 0x224: ('f', 10.0), 0x234: 0}, _HELD,
            mutations=[{}, word_bytes(0x220, fbits(0.0))] + _NONE),
        _sc({**_GRABBED, 0x224: ('f', 10.0), 0x220: ('f', 50.0)}, _HELD, mutations=_NONE),
        _sc({**_GRABBED, 0x224: ('w', 0), 0x22C: ('w', 0), 7: 0}, _HELD, mutations=_NONE),
        _sc({**_GRABBED, 0x224: ('w', 0), 0x22C: ('w', 0), 7: 0, 0x28: ('h', 3), 0x2FE: 5}, _HELD,
            mutations=_NONE),
        _sc({**_GRABBED, 0x224: ('w', 0), 0x22C: ('f', 150.0)}, _HELD,
            mutations=[{}, word_bytes(0x228, fbits(100.0))] + _NONE, flags=[1]),
        _sc({**_GRABBED, 0x224: ('w', 0), 0x22C: ('f', 150.0)}, _HELD,
            mutations=[{}, word_bytes(0x228, fbits(100.0))] + _NONE, flags=[0]),
        _sc({**_GRABBED, 0x224: ('w', 0), 0x22C: ('f', 150.0)}, _HELD,
            mutations=[{}, word_bytes(0x228, fbits(50.0))] + _NONE, flags=[1]),
    ],
    '00223C70': [
        _sc({6: 0, 0x1F1: 0, 0x220: ('f', -1.0), 0xF: 0x63}),
        _sc({6: 0, 0x1F1: 0, 0x220: ('f', -1.0), 0xF: 0, 0x234: 0}),
        _sc({6: 0, 0x1F1: 0, 0x220: ('f', 50.0)}),
        _sc({6: 0, 0x1F1: 1, 0x228: ('f', 100.0)}, {'flag': 1}),
    ],
}


def apply_scenario(forced, raw, scene, flag, script):
    for offset, value in forced['raw'].items():
        if isinstance(value, tuple):
            kind, number = value
            if kind == 'f': struct.pack_into('<f', raw, offset, number)
            elif kind == 'w': struct.pack_into('<I', raw, offset, number)
            else: struct.pack_into('<h', raw, offset, number)
        else:
            raw[offset] = value
    for name, value in forced['scene'].items():
        if name == 'flag': flag.value = value
        else: setattr(scene, name, value)
    for name, value in forced['script'].items():
        setattr(script, name, list(value))


ELF = NATIVE = EE_ = None


def run_state(case):
    routine, seed, index = case
    rng = random.Random(seed)
    raw = random_actor(rng, routine)
    scene, flag = random_scene(rng)
    script = Script(rng)
    if index < len(SCENARIOS.get(routine, ())):
        apply_scenario(SCENARIOS[routine][index], raw, scene, flag, script)
    load_case(EE_, raw, scene, flag)
    original = Original(EE_, script.copy())
    native = Native(script.copy(), flag)
    load_spad(EE_, native, case_spad((routine, seed)))
    EE_.call(STATES[routine][0], (ACTOR,))
    actor = LiveActor(); C.memmove(actor.bytes, bytes(raw), 0x320)
    native.bind(actor, scene)
    result = getattr(NATIVE, 'em_player_reaction_' + routine)(C.byref(actor), C.byref(scene),
                                                               C.byref(native.workers))
    assert result == 0, (routine, seed, result)
    compare(EE_, actor, scene, flag, (routine, seed), original, native)
    return raw[6]


# ------------------------------------------------------- helper cases ---

def run_helper(case):
    """The exported helpers with arguments the state routines never pass
    (00182870 a1 = 0, 0021D250 a1 = 1, 0021D2E0 a2 = 0/1) and on their own."""
    name, seed = case
    rng = random.Random(seed)
    raw = random_actor(rng, '0021D800')
    scene, flag = random_scene(rng)
    raw[0x23A] = rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 0xD, 0xE, 0x5A, 0x5B, 0x5C, 0x5D, 0x44])
    script = Script(rng)
    load_case(EE_, raw, scene, flag)
    original = Original(EE_, script.copy())
    native = Native(script.copy(), flag)
    load_spad(EE_, native, case_spad((name, seed)))
    actor = LiveActor(); C.memmove(actor.bytes, bytes(raw), 0x320)
    native.bind(actor, scene)
    ref, w, sc = C.byref(actor), C.byref(native.workers), C.byref(scene)
    arg = rng.choice([0, 1])
    entry = HELPERS[name][0]
    if name == '00182870':
        EE_.call(entry, (ACTOR, arg)); assert NATIVE.em_player_reaction_00182870(ref, arg, w) == 0
    elif name == '0021D250':
        EE_.call(entry, (ACTOR, arg)); assert NATIVE.em_player_reaction_0021D250(ref, arg, w) == 0
    elif name == '0021D2E0':
        EE_.call(entry, (ACTOR, 0x78, arg))
        assert NATIVE.em_player_reaction_0021D2E0(ref, 0x78, arg, w) == 0
    elif name == '0017C540':
        EE_.call(entry, (ACTOR,)); NATIVE.em_player_reaction_0017C540(ref)
    elif name == '0021D530':
        EE_.call(entry, (ACTOR,)); NATIVE.em_player_reaction_0021D530(ref, sc)
    elif name == '00179880':
        EE_.call(entry, (ACTOR, ACTOR + 0x2EC)); NATIVE.em_player_fall_drop(ref)
    elif name == '001754E0':
        count = rng.choice([0, 1, 6, 7])
        out = C.c_int(-7)
        EE_.call(entry, (ACTOR, count))
        assert NATIVE.em_player_reaction_001754E0(ref, sc, C.byref(native.scratch), count,
                                                  C.byref(out)) == 0
        assert out.value == s(EE_.r[2]), (name, seed, out.value, EE_.r[2])
    compare(EE_, actor, scene, flag, (name, seed), original, native)


def run_voice(surface, arg, depth):
    """00182870 on one floor attribute, argument and +23C."""
    raw = bytearray(0x320)
    raw[0x23A], raw[0x23C] = surface, depth
    scene, flag = Scene(), C.c_uint8(0)
    scene.d8106F1 = C.pointer(flag)
    script = Script(random.Random(surface * 4 + arg * 2 + depth))
    load_case(EE_, raw, scene, flag)
    original = Original(EE_, script.copy())
    native = Native(script.copy(), flag)
    load_spad(EE_, native, case_spad(('voice', surface, arg, depth)))
    actor = LiveActor(); C.memmove(actor.bytes, bytes(raw), 0x320)
    native.bind(actor, scene)
    EE_.call(HELPERS['00182870'][0], (ACTOR, arg))
    assert NATIVE.em_player_reaction_00182870(C.byref(actor), arg, C.byref(native.workers)) == 0
    compare(EE_, actor, scene, flag, ('00182870', surface, arg, depth), original, native)


def run_wrap(seed):
    rng = random.Random(seed)
    for _ in range(200):
        value = rng.choice([rng.uniform(-40, 40), 3.1415927, -3.1415927, 6.2831855, -6.2831855,
                            ffloat(0x40490FDC), ffloat(0xC0490FDC), 0.0, -0.0, 1e-39])
        EE_.reset()
        EE_.f[12] = fbits(value)
        EE_.call(HELPERS['001B1470'][0])
        have = fbits(NATIVE.em_player_reaction_wrap(value))
        assert have == EE_.f[0] & 0xFFFFFFFF, ('001B1470', value, hex(have), hex(EE_.f[0]))


# --------------------------------------------------------- live binding ---

def live_section():
    """The live and helper adapters: refresh runs first; a missing worker,
    scene, D_008106F1 pointer or refresh faults before any write."""
    native = Native(Script(random.Random(5)), C.c_uint8(0))
    flag = C.c_uint8(0)
    scene = Scene()
    scene.d8106F1 = C.pointer(flag)
    refreshed = []

    def refresh(_, sc):
        refreshed.append(1); sc.contents.root8 = fbits(2.5); return 0
    keep = REFRESH_FN(refresh)
    binding = Reaction(native.workers, C.pointer(scene), keep, None)
    actor = LiveActor()
    actor.bytes[6] = 1          # 0021E9C0 sub-state 1 without 0x1000: +38 = root8 - +21C
    assert NATIVE.em_player_reaction_live_0021E9C0(C.addressof(binding), C.byref(actor)) == 0
    assert refreshed == [1] and struct.unpack_from('<f', bytes(actor.bytes), 0x38)[0] == 2.5
    for field in ('sound', 'atan2', 'stream_check', 'w0021C270', 'w0021C350', 'scratch'):
        broken = Reaction(native.workers, C.pointer(scene), keep, None)
        setattr(broken.workers, field, type(getattr(broken.workers, field))())
        before = bytes(actor.bytes)
        assert NATIVE.em_player_reaction_live_0021E9C0(C.addressof(broken), C.byref(actor)) == -1
        assert NATIVE.em_player_reaction_w0021D2E0(C.addressof(broken), C.byref(actor), 0x78, 1) == -1
        assert bytes(actor.bytes) == before, field
    empty = Reaction(native.workers, None, keep, None)
    assert NATIVE.em_player_reaction_live_0021D800(C.addressof(empty), C.byref(actor)) == -1
    unshared = Scene()          # no D_008106F1 pointer
    loose = Reaction(native.workers, C.pointer(unshared), keep, None)
    assert NATIVE.em_player_reaction_live_0021D800(C.addressof(loose), C.byref(actor)) == -1
    # The helper adapters major2 binds run the translation through the binding.
    actor = LiveActor()
    actor.bytes[0x31F] = 0
    result = C.c_int(-1)
    assert NATIVE.em_player_reaction_w0021C190(C.addressof(binding), C.byref(actor), C.byref(result)) == 0
    assert result.value == 1 and actor.bytes[0x31F] == 0xFF
    # A worker fault inside the routine stops it with -1.
    failing = Native(Script(random.Random(6)), C.c_uint8(0))
    failing.workers.sound = SOUND_FN(lambda *_: -1)
    failing.keep.append(failing.workers.sound)
    actor = LiveActor()
    assert NATIVE.em_player_reaction_0021E9C0(C.byref(actor), C.byref(scene),
                                              C.byref(failing.workers)) == -1


# ----------------------------------------------------------------- main ---

def main():
    global ELF, NATIVE, EE_
    ELF = read_elf()
    NATIVE = build_native()
    EE_ = ReactionEE(ELF)
    total = reference_mode.pick(40000, 8000)
    names = list(STATES)
    cases = [(names[i % len(names)], 0x5EAC7 + i, i // len(names)) for i in range(total)]
    seen = {}
    for case in cases:
        seen.setdefault(case[0], set()).add(run_state(case))
    helper_total = reference_mode.pick(6000, 400)
    helper_names = ('00182870', '0021D250', '0021D2E0', '0017C540', '0021D530', '00179880',
                    '001754E0')
    for i in range(helper_total):
        run_helper((helper_names[i % len(helper_names)], 0xA11CE + i))
    for surface in (0, 1, 2, 3, 4, 5, 6, 7, 8, 0xD, 0xE, 0x5A, 0x5B, 0x5C, 0x44):
        for arg in (0, 1):
            for depth in (0, 1):
                run_voice(surface, arg, depth)
    for seed in range(reference_mode.pick(20, 2)):
        run_wrap(seed)
    live_section()
    for name, subs in SUBSTATES.items():
        assert set(subs) <= seen[name], (name, 'sub-states not run', sorted(set(subs) - seen[name]))
    assert SNAPS_COMPARED[0] > 0, 'no per-call snapshots compared'
    missing, one_way = coverage_check(EE_.executed, EE_.branches)
    assert not missing, ('original instructions never executed', missing)
    assert not one_way, ('branches taken one way only', one_way)
    instructions = sum(len(static_reachable(EE_, e, n)[0]) for e, n in
                       list(STATES.values()) + list(HELPERS.values()))
    reference_mode.banner(
        reference_mode.part(total, 40000, 'state cases'),
        reference_mode.part(helper_total, 6000, 'helper cases'),
        f'{len(STATES)} states + {len(HELPERS)} helpers, all {instructions} reachable original '
        'instructions executed and every conditional branch both ways, all 0x320 actor bytes, '
        'scene bytes and worker calls identical at exit and at entry to each of '
        f'{SNAPS_COMPARED[0]:,} worker calls')
    print('player reaction reference: PASS')


if __name__ == '__main__':
    main()
