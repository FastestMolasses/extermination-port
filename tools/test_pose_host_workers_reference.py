#!/usr/bin/env python3
"""Execute the original clip / skeleton routines and compare em_pose_host_workers.c.

docs/POSE_HOST_WORKERS.md. The user's pinned ELF and the captured AREA11 EE
RAM + scratchpad images (the playable image and the PCSX2 route beats,
docs/FIRST_LEVEL_ROUTE.md) supply every instruction, the player's clip bank
and its node records; none are embedded here.

The interpreter is the shared EE of test_player_slide_reference.py with the
COP1 / VU0 macro of test_coll_move_reference.FloatEE (every float operation
through tools/ee_float_model.py, docs/EE_FLOAT_MODEL.md). This file only
subclasses it to record the executed instruction addresses.

Executed, unmodified (never hooked): 001749A0, 001749F0, 001C6120, 001C61D0,
001C8480, 001C63E0, 001C67E0, 001C64F0 (anim_advance_time, with the stage
lane's native translation bound to this module's clip workers), 001C8710,
001C87C0, 001C8D50, 001C8F10, 001C90D0, 001C92C0, 001C84D0, 001C85D0,
001C86A0, 001CA0A0, 001CA1C0, 001281C0 + 001278C0, 00128250, 001C94B0,
001029C0, 00102918, 00102C58, 00102A60 / 00102B08 / 00102BB0 (+ 001029E8),
001C6DA0, 001C9940, 001C68C0, 001C6960, 00178910 with 0011DF78 and
001B1470, and 0017C540.

The native side runs over a copy of the same memory image: one region maps
the whole 32 MB RAM, the record is the player record inside it, and the
globals (D_00275BF8.., D_008111F0.., D_008106F3, the scratchpad words) are
EmPoseGlobals fields. After every case the native RAM with the globals
written back must equal the original's RAM byte for byte, and the original's
scratchpad must equal the initial one plus the native scratchpad fields.
Every return value and flags word is compared.

The SDK matrix routines (001029C0, 00102C58, build_trs_matrix and the
rotations 00102B08 / 00102BB0 / 00102A60) and 00128250 are not translated
by this module: it binds em_owner_services_original.c and
em_stream_lanes_original.c, linked into the test library as in the game.
Those bindings are compared with the original directly (the rotations, the
build_trs_matrix / euler adapters with aliased arguments, 00128250) and
through every skeleton case. `advance` (anim_clip_init's zero-blend step) is
em_pose_host_player_advance over em_player_stage_anim_advance with this
module's stage adapters (clip_resolve, skeleton_frame, 001C8710, 001C87C0,
anim_sample_bones). 00178910's 0019BC40 (column table) and 0011E620 (atan2)
are hooked on both sides with the same scripted table and libm atan2f.

The bound-actor view adapters (em_pose_view_*: the slide / climb request,
arbiter and clip_frames, the hang / 0E-18 / major2 node readers through
D_00275B40, the 00162A40 and 00161790 skeleton-then-node-1 steps) are compared
with the same original calls, D_00275B40 holding the player's +110 as
anim_bone_array_setup leaves it for the player's callback.

EM_TEST_FULL=1 runs every route beat and the exhaustive random sweeps.
"""
import ctypes as C
import hashlib
import os
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
from test_coll_move_reference import FloatEE  # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
REFERENCE = DECOMP / 'build/startup-reference'
ROUTE = DECOMP / 'build/s87/route'
LANE = ROOT / 'build/b6-pose-host-workers'
FUNCTIONS = DECOMP / 'docs/FUNCTIONS.csv'

PLAYER = 0x8102B0
SCRATCH_RAM = 0x01F00000      # zero RAM in every image (checked): leaf operands, hit record
HIT_RECORD = 0x01FE0000
D_BF8, D_BF4, D_BF0, D_BEC = 0x275BF8, 0x275BF4, 0x275BF0, 0x275BEC
D_SCRATCH, D_6F3 = 0x8111F0, 0x8106F3
D_AUX = 0x282250

REQUEST, ARBITER, CLIP_INIT, BONE_INIT = 0x1749A0, 0x1749F0, 0x1C67E0, 0x1C63E0
FRAMES, RESOLVE, HEADER, ADVANCE = 0x1C61D0, 0x1C8480, 0x1C6120, 0x1C64F0
S8710, S87C0, SAMPLE = 0x1C8710, 0x1C87C0, 0x1C8D50
EVAL, E68C0, E6960, E9940, TRS = 0x1C6DA0, 0x1C68C0, 0x1C6960, 0x1C9940, 0x1C94B0
NLERP, QMAT, TOUINT, DEC_R, DEC_T = 0x1CA0A0, 0x1CA1C0, 0x128250, 0x1C84D0, 0x1C85D0
LEDGE, HANDOFF, COLUMN, ATAN2 = 0x178910, 0x17C540, 0x19BC40, 0x11E620
ROTATIONS = (0x102B08, 0x102BB0, 0x102A60)

# The originals whose instructions this oracle executes (coverage).
EXECUTED = ('func_001749A0', 'anim_clip_arbiter', 'func_001C6120', 'func_001C61D0',
            'anim_clip_resolve', 'bone_init_default_2', 'anim_clip_init', 'func_001C8710',
            'func_001C87C0', 'anim_sample_bones', 'anim_sample_rotation', 'func_001C90D0',
            'func_001C92C0', 'func_001C84D0', 'anim_decode_translation', 'func_001C86A0',
            'quat_nlerp', 'quat_to_mat3', 'func_00128250', 'build_trs_matrix', 'func_001029C0',
            'func_00102918', 'func_00102C58', 'anim_eval_skeleton', 'func_001C9940', 'func_001C68C0',
            'func_001C6960', 'func_00178910', 'func_0017C540')

ELF = None
IMAGES = {}       # label -> (ram, spad)
BANK = {}         # clip -> (frames, next, start, events)
LIB = None
LIBC = C.CDLL(None)
LIBC.atan2f.argtypes = [C.c_float, C.c_float]
LIBC.atan2f.restype = C.c_float


def F(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def fnum(word):
    return struct.unpack('<f', struct.pack('<I', word & 0xFFFFFFFF))[0]


def u32(buf, at):
    return struct.unpack_from('<I', buf, at)[0]


def s16(value):
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


# ------------------------------------------------------------------ the oracle

class EE(FloatEE):
    """The shared EE with the measured float model; records every executed
    instruction address (a jump counts through its delay slot)."""

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.pcs = set()

    def branch(self, word, pc):
        self.pcs.add(pc)
        return super().branch(word, pc)

    def execute(self, word, pc):
        self.pcs.add(pc)
        return super().execute(word, pc)


_LEAF = None


def leaf_ee():
    """One ELF-only interpreter per process for the leaf cases."""
    global _LEAF
    if _LEAF is None:
        _LEAF = EE(ELF)
    return _LEAF


def original_rotate(matrix, angle, axis):
    """The original 00102B08 / 00102BB0 / 00102A60(out, in, angle) over 16 words."""
    ee = leaf_ee()
    src, dst = SCRATCH_RAM + 0x800, SCRATCH_RAM + 0x840
    ee.write(src, struct.pack('<16I', *matrix))
    ee.invoke(ROTATIONS[axis], (dst, src), (angle,))
    return list(struct.unpack('<16I', ee.read(dst, 64)))


# ------------------------------------------------------------------ native side

U32, U8, U16 = C.c_uint32, C.c_uint8, C.c_uint16


class Region(C.Structure):
    _fields_ = [('address', U32), ('size', U32), ('bytes', C.c_void_p), ('writable', C.c_int)]


class FloorTable(C.Structure):
    _fields_ = [('count', C.c_int), ('flags', U16 * 20), ('height', C.c_float * 20), ('aux', C.c_float * 20)]


PU32 = C.POINTER(U32)


class Globals(C.Structure):
    _fields_ = [('d275BF8', U32), ('d275BF4', U32), ('d275BF0', U32), ('d275BEC', U32),
                ('d8111F0', U8 * 0x6C), ('d8106F3', C.POINTER(U8)), ('spad3400', PU32), ('spad3440', PU32),
                ('spad3600', PU32), ('spad3760', PU32), ('spad3A3C', PU32), ('spad38B0', PU32),
                ('spad3A20', PU32), ('column', C.POINTER(FloorTable))]


class LedgeHit(C.Structure):
    _fields_ = [('x', U32), ('z', U32), ('nx', U32), ('nz', U32)]


COLUMN_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float), C.POINTER(FloorTable))
HIT_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(LedgeHit))
ATAN2_FN = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_float, C.c_float)
ADVANCE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(U8), C.c_float)


class Callees(C.Structure):
    _fields_ = [('context', C.c_void_p), ('column', COLUMN_FN), ('ledge_hit', HIT_FN),
                ('atan2', ATAN2_FN), ('advance_context', C.c_void_p), ('advance', ADVANCE_FN)]


class Host(C.Structure):
    _fields_ = [('region', Region * 12), ('region_count', C.c_uint), ('globals', C.POINTER(Globals)),
                ('callees', Callees), ('events', C.c_int16 * 512)]


class StageScene(C.Structure):
    _fields_ = [(name, U8) for name in ('spad3B8D', 'spad3B8F', 'area', 'busy')] + \
               [('d8106F1', C.POINTER(U8)), ('d810CB6', C.POINTER(U8))]


class StageGlobals(C.Structure):
    _fields_ = [('d8106C8', C.c_int32), ('d810701', U8), ('d810770', U8), ('d81083C', U8),
                ('d810C7E', U8), ('spad3A20', U32), ('d810707', C.POINTER(U8))]


class ClipRates(C.Structure):
    _fields_ = [('count', U32), ('rate', C.c_float * 459)]


STAGE_CALLEES = ('w001D0C70', 'bone_init', 'clip_init', 'clip_resolve', 'skeleton_frame', 'w001C8710',
                 'w001C87C0', 'sample_bones', 'sound', 'cue', 'w001EFE00', 'w001F00A0', 'w001F0060',
                 'atan2', 'link20', 'clip_lookup', 'request', 'w0015C9D0', 'link1C', 'sound_stop')


class StageCallees(C.Structure):
    _fields_ = [('context', C.c_void_p)] + [(name, C.c_void_p) for name in STAGE_CALLEES]


class StageHost(C.Structure):
    _fields_ = [('stage', C.POINTER(StageScene)), ('globals', C.POINTER(StageGlobals)),
                ('rates', C.POINTER(ClipRates)), ('callees', StageCallees)]


class LiveActor(C.Structure):
    _fields_ = [('bytes', U8 * 0x320), ('link_owner', C.c_void_p), ('link_prev', C.c_void_p),
                ('link_flags', U8), ('link_type', U8)]


SOURCES = ('src/game/em_pose_host_workers.c', 'src/game/em_player_stage_workers.c',
           'src/game/em_player_floor.c', 'src/game/em_player_reaction.c', 'src/game/em_player_fall.c',
           'src/game/em_owner_services_original.c', 'src/game/em_stream_lanes_original.c')


def build_native():
    LANE.mkdir(parents=True, exist_ok=True)
    lib = LANE / ('pose_host_workers' + ('.dylib' if sys.platform == 'darwin' else '.so'))
    command = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared', '-fPIC',
               '-Isrc', *SOURCES, '-lm', '-o', str(lib)]
    digest = hashlib.sha256(' '.join(command).encode())
    for path in [ROOT / s for s in SOURCES] + \
            sorted((ROOT / 'src').rglob('*.h')):
        digest.update(path.read_bytes())
    stamp = Path(str(lib) + '.sha256')
    if not (lib.exists() and stamp.exists() and stamp.read_text() == digest.hexdigest()):
        subprocess.run(command, cwd=ROOT, check=True)
        stamp.write_text(digest.hexdigest())
    n = C.CDLL(str(lib))
    VP, P8, PU = C.c_void_p, C.POINTER(U8), C.POINTER(U32)
    rec = [VP, P8, U32]
    for name, args in {
        '001749A0': rec + [C.c_int, C.c_int, C.c_float, C.POINTER(C.c_int)],
        '001749F0': rec + [C.c_int, C.c_float, C.c_float, C.POINTER(C.c_int)],
        '001C67E0': rec + [C.c_int, C.c_float, C.c_float],
        '001C63E0': rec + [C.c_int],
        '001C8710': rec + [C.c_int, C.c_float], '001C87C0': rec + [C.c_int, C.c_float],
        '001C8D50': rec + [C.c_int, C.c_float, C.c_float],
        '001C8480': [VP, U32, C.c_int], '001C61D0': [VP, U32, C.c_int, C.POINTER(C.c_int32)],
        '001C6120': [VP, U32, C.c_int, PU],
        '001C6DA0': rec, '001C68C0': rec, '001C6960': rec,
        '001C9940': rec + [C.c_int, P8],
        '00178910': rec + [C.c_int, C.POINTER(C.c_int)],
    }.items():
        getattr(n, 'em_pose_host_' + name).argtypes = args
        getattr(n, 'em_pose_host_' + name).restype = C.c_int
    n.em_pose_host_build_trs_matrix.argtypes = [VP, PU, PU, PU, PU]
    n.em_stream_lanes_00128250.argtypes = [U32]
    n.em_stream_lanes_00128250.restype = U32
    for name in ('rotate_x_00102B08', 'rotate_y_00102BB0', 'rotate_z_00102A60'):
        getattr(n, 'em_owner_services_' + name).argtypes = [PU, PU, U32]
        getattr(n, 'em_owner_services_' + name).restype = C.c_int
    n.em_pose_host_001C84D0.argtypes = [P8, PU, PU]
    n.em_pose_host_001C85D0.argtypes = [P8, PU, PU]
    n.em_pose_host_001CA0A0.argtypes = [PU, PU, PU, U32]
    n.em_pose_host_001CA1C0.argtypes = [PU, PU, PU, PU]
    PA = C.POINTER(LiveActor)
    for name, args in {
        'request': [VP, PA, C.c_int, C.c_int, C.c_float], 'arbiter': [VP, PA, C.c_int, C.c_float, C.c_float],
        'clip_frames': [VP, U32, C.c_int, C.POINTER(C.c_int32)],
        'clip_frames_actor': [VP, PA, C.c_int, C.POINTER(C.c_int)],
        'eval_skeleton': [VP, PA], 'skeleton': [VP, PA], 'handoff': [VP, PA],
        'ledge_top': [VP, PA, C.c_int, C.POINTER(C.c_int)],
        'node_word': [VP, U32, C.c_uint, PU],
        'stage_bone_init': [VP, PA, C.c_int], 'stage_clip_init': [VP, PA, C.c_int, C.c_float, C.c_float],
        'stage_8710': [VP, PA, C.c_int, C.c_float], 'stage_87C0': [VP, PA, C.c_int, C.c_float],
        'stage_sample_bones': [VP, PA, C.c_int, C.c_float, C.c_float],
        'stage_request': [VP, PA, C.c_int, C.c_int, C.c_float],
    }.items():
        getattr(n, 'em_pose_host_' + name).argtypes = args
    n.em_player_stage_anim_advance.argtypes = [VP, VP, C.c_float, PU]
    for name, args in {
        'request': [VP, C.c_int, C.c_int, C.c_float], 'arbiter': [VP, C.c_int, C.c_float, C.c_float],
        'clip_frames': [VP, C.c_int, C.POINTER(C.c_int)],
        'node_bits': [VP, C.c_int, C.c_uint, PU], 'node_float': [VP, C.c_int, C.c_uint, C.POINTER(C.c_float)],
        'root_node': [VP, C.c_uint, PU], 'eval_node1': [VP, PA, C.POINTER(C.c_float)],
        'eval_hip': [VP, C.POINTER(C.c_float), C.POINTER(C.c_float)],
        'eval_node1_xyz': [VP, PA, C.POINTER(C.c_float)], 'hip_xz': [VP, PU, PU],
        'node1_words': [VP, PU], 'root_clock': [VP, PU], 'bone': [VP, C.c_uint, PU],
    }.items():
        getattr(n, 'em_pose_view_' + name).argtypes = args
        getattr(n, 'em_pose_view_' + name).restype = C.c_int
    n.em_pose_host_request_bits.argtypes = [VP, PA, C.c_int, C.c_int, U32]
    n.em_pose_host_euler.argtypes = [VP, PU, PU, PU]
    return n


def addr(fn):
    return C.cast(fn, C.c_void_p).value


class Native:
    """The native host over a copy of one memory image."""

    def __init__(self, ram, spad, ledge=None):
        self.ram = bytearray(ram)
        self.spad = bytes(spad)
        self.buf = (U8 * len(self.ram)).from_buffer(self.ram)
        self.base = C.addressof(self.buf)
        g = self.g = Globals()
        g.d275BF8, g.d275BF4, g.d275BF0, g.d275BEC = (u32(ram, a) for a in (D_BF8, D_BF4, D_BF0, D_BEC))
        C.memmove(g.d8111F0, bytes(ram[D_SCRATCH:D_SCRATCH + 0x6C]), 0x6C)
        # Shared storage by pointer: D_008106F3 is the native RAM's own byte
        # (as the game would share one copy with the area script); the
        # scratchpad words and the column table are host arrays.
        g.d8106F3 = C.cast(self.base + D_6F3, C.POINTER(U8))
        self.sp = {}
        for name, at, count in self.SPAD:
            arr = self.sp[name] = (U32 * count)(*(u32(spad, at + 4 * k) for k in range(count)))
            setattr(g, name, C.cast(arr, PU32))
        self.table = FloorTable()
        g.column = C.pointer(self.table)
        # 0x70003A20 is the stage lane's EmPlayerStageGlobals.spad3A20 (one
        # copy, as the binding notes require).
        self.sglobals = StageGlobals()
        self.sglobals.spad3A20 = u32(spad, 0x3A20)
        at = C.addressof(self.sglobals) + StageGlobals.spad3A20.offset
        self.sp['spad3A20'] = (U32 * 1).from_address(at)
        g.spad3A20 = C.cast(at, PU32)
        h = self.host = Host()
        h.region[0] = Region(0, len(self.ram), self.base, 1)
        h.region_count = 1
        h.globals = C.pointer(g)
        self.keep = []
        self.log = []
        if ledge is not None:
            self.ledge = ledge

            def column(_, point, table):
                self.log.append(('column', tuple(F(point[k]) for k in range(3))))
                rows = ledge['table']
                table[0].count = len(rows)
                for k, (flags, height, aux) in enumerate(rows):
                    table[0].flags[k] = flags
                    table[0].height[k] = fnum(height)
                    table[0].aux[k] = fnum(aux)
                return 0

            def hit(_, out):
                out[0].x, out[0].z, out[0].nx, out[0].nz = ledge['hit']
                return 0
            h.callees.column = self.hold(COLUMN_FN(column))
            h.callees.ledge_hit = self.hold(HIT_FN(hit))
            fixed = ledge.get('atan2')
            h.callees.atan2 = self.hold(ATAN2_FN(
                lambda _, y, x: LIBC.atan2f(y, x) if fixed is None else fnum(fixed)))
        # anim_clip_init's zero-blend step: the stage lane's anim_advance_time
        # bound to this module's clip workers.
        self.scene, self.rates = StageScene(), ClipRates()
        # D_008106F1 / D_00810CB6 / D_00810707: one byte each, where the
        # original keeps it (the stage lane's canonical-pointer binding).
        self.scene.d8106F1 = C.cast(self.base + 0x8106F1, C.POINTER(U8))
        self.scene.d810CB6 = C.cast(self.base + 0x810CB6, C.POINTER(U8))
        self.sglobals.d810707 = C.cast(self.base + 0x810707, C.POINTER(U8))
        st = self.stage = StageHost(C.pointer(self.scene), C.pointer(self.sglobals), C.pointer(self.rates))
        st.callees.context = C.addressof(h)
        for field, fn in (('clip_resolve', 'stage_clip_resolve'), ('skeleton_frame', 'stage_skeleton_frame'),
                          ('w001C8710', 'stage_8710'), ('w001C87C0', 'stage_87C0'),
                          ('sample_bones', 'stage_sample_bones')):
            setattr(st.callees, field, addr(getattr(LIB, 'em_pose_host_' + fn)))
        h.callees.advance_context = C.addressof(st)
        h.callees.advance = ADVANCE_FN(addr(LIB.em_pose_host_player_advance))

    SPAD = (('spad3400', 0x3400, 16), ('spad3440', 0x3440, 16), ('spad3600', 0x3600, 4),
            ('spad3760', 0x3760, 11), ('spad38B0', 0x38B0, 4), ('spad3A3C', 0x3A3C, 1),
            ('spad3A20', 0x3A20, 1))

    def hold(self, fn):
        self.keep.append(fn)
        return fn

    def rec(self, at=PLAYER):
        return C.cast(self.base + at, C.POINTER(U8))

    def export(self):
        """The native memory image with the globals written back where the
        original keeps them."""
        ram, g = bytearray(self.ram), self.g
        for at, value in ((D_BF8, g.d275BF8), (D_BF4, g.d275BF4), (D_BF0, g.d275BF0), (D_BEC, g.d275BEC)):
            struct.pack_into('<I', ram, at, value)
        ram[D_SCRATCH:D_SCRATCH + 0x6C] = bytes(g.d8111F0)
        spad = bytearray(self.spad)
        for name, at, count in self.SPAD:
            arr = self.sp[name]
            for k in range(count): struct.pack_into('<I', spad, at + 4 * k, arr[k])
        if hasattr(self, 'ledge'):
            t = self.table
            struct.pack_into('<I', spad, 0x31E0, t.count & 0xFFFFFFFF)
            for k in range(max(0, t.count)):
                struct.pack_into('<H', spad, 0x3170 + 2 * k, t.flags[k])
                struct.pack_into('<f', spad, 0x30F0 + 4 * k, t.height[k])
                struct.pack_into('<f', ram, D_AUX + 4 * k, t.aux[k])
        return ram, spad


# ------------------------------------------------------------------ operations

def run_op(ee, nat, op):
    """One operation on both sides: (original result, native result)."""
    kind, args = op[0], op[1:]
    rec, size, bones, bsize = nat.rec(), 0x320, nat.rec(PLAYER + 0x110), 0x320 - 0x110
    n = ee.mem[PLAYER + 0xC]
    h = C.byref(nat.host)
    out = C.c_int(-7)
    if kind == 'request':
        clip, flags, blend = args
        v0, _ = ee.invoke(REQUEST, (PLAYER, clip, flags), (blend,))
        status = LIB.em_pose_host_001749A0(h, rec, size, clip, flags, fnum(blend), C.byref(out))
        return v0, (status, out.value)
    if kind == 'arbiter':
        clip, blend, frame = args
        v0, _ = ee.invoke(ARBITER, (PLAYER, clip), (blend, frame))
        status = LIB.em_pose_host_001749F0(h, rec, size, clip, fnum(blend), fnum(frame), C.byref(out))
        return v0, (status, out.value)
    if kind == 'clip_init':
        clip, blend, frame = args
        ee.invoke(CLIP_INIT, (PLAYER, clip), (blend, frame))
        return 0, (LIB.em_pose_host_001C67E0(h, rec, size, clip, fnum(blend), fnum(frame)), 0)
    if kind == 'bone_init':
        ee.invoke(BONE_INIT, (PLAYER, args[0]))
        return 0, (LIB.em_pose_host_001C63E0(h, rec, size, args[0]), 0)
    if kind == 'frames':
        bank = u32(ee.mem, PLAYER + 0x40)
        v0, _ = ee.invoke(FRAMES, (bank, args[0]))
        frames = C.c_int32(-7)
        status = LIB.em_pose_host_001C61D0(h, bank, args[0], C.byref(frames))
        return v0, (status, frames.value)
    if kind == 'resolve':
        bank = u32(ee.mem, PLAYER + 0x40)
        ee.invoke(RESOLVE, (bank, args[0]))
        return 0, (LIB.em_pose_host_001C8480(h, bank, args[0]), 0)
    if kind in ('8710', '87C0'):
        ee.invoke(S8710 if kind == '8710' else S87C0, (PLAYER + 0x110, n), (args[0],))
        fn = LIB.em_pose_host_001C8710 if kind == '8710' else LIB.em_pose_host_001C87C0
        return 0, (fn(h, bones, bsize, n, fnum(args[0])), 0)
    if kind == 'sample':
        ee.invoke(SAMPLE, (PLAYER + 0x110, n), args)
        return 0, (LIB.em_pose_host_001C8D50(h, bones, bsize, n, fnum(args[0]), fnum(args[1])), 0)
    if kind in ('eval', '68C0', '6960'):
        entry = {'eval': EVAL, '68C0': E68C0, '6960': E6960}[kind]
        ee.invoke(entry, (PLAYER,))
        fn = {'eval': LIB.em_pose_host_001C6DA0, '68C0': LIB.em_pose_host_001C68C0,
              '6960': LIB.em_pose_host_001C6960}[kind]
        return 0, (fn(h, rec, size), 0)
    if kind == 'advance':
        v0, _ = ee.invoke(ADVANCE, (PLAYER,), (args[0],))
        flags = U32(0xDEADBEEF)
        status = LIB.em_player_stage_anim_advance(C.byref(nat.stage), nat.base + PLAYER, fnum(args[0]),
                                                  C.byref(flags))
        return v0 & 0xFFFFFFFF, (status, flags.value)
    if kind == 'handoff':
        ee.invoke(HANDOFF, (PLAYER,))
        return 0, (LIB.em_pose_host_handoff(h, C.cast(nat.base + PLAYER, C.POINTER(LiveActor))), 0)
    if kind == 'ledge':
        v0, _ = ee.invoke(LEDGE, (PLAYER, args[0]))
        status = LIB.em_pose_host_00178910(h, rec, size, args[0], C.byref(out))
        return v0, (status, out.value)
    raise AssertionError(('op', kind))


class Mismatch(AssertionError):
    pass


def first_diff(a, b, base=0):
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            return hex(base + i), a[i], b[i]
    return None


def ledge_hooks(ee, script):
    def column(e):
        point = tuple(e.load(0x700038B0 + 4 * k) for k in range(3))
        script.setdefault('points', []).append(point)
        rows = script['table']
        e.save(0x700031E0, len(rows))
        for k, (flags, height, aux) in enumerate(rows):
            e.save(0x70003170 + 2 * k, flags, 2)
            e.save(0x700030F0 + 4 * k, height)
            e.save(D_AUX + 4 * k, aux)

    def atan2(e):
        fixed = script.get('atan2')
        e.f[0] = F(LIBC.atan2f(fnum(e.f[12]), fnum(e.f[13]))) if fixed is None else fixed
    ee.hooks[COLUMN] = column
    ee.hooks[ATAN2] = atan2


def run_case(case):
    """Run one case on both sides; returns the executed original addresses."""
    ram, spad = IMAGES[case['image']]
    ram, spad = bytearray(ram), bytearray(spad)
    for at, data in case.get('patch', ()):
        if at >= 0x70000000: spad[at - 0x70000000:at - 0x70000000 + len(data)] = data
        else: ram[at:at + len(data)] = data
    ledge = case.get('ledge')
    ee = EE(ELF, ram=ram, spad=spad)
    if ledge is not None: ledge_hooks(ee, ledge)
    nat = Native(ram, spad, ledge=ledge)
    del ram
    for index, op in enumerate(case['ops']):
        original, (status, native) = run_op(ee, nat, op)
        where = (case['label'], index, op)
        if status != 0:
            raise Mismatch(('native fault', where, status))
        if op[0] == 'advance':                  # the halfword result 0015BA50 stores to +200
            original, native = original & 0xFFFF, native & 0xFFFF
        if op[0] in ('request', 'arbiter', 'frames', 'ledge', 'advance') and original != native:
            raise Mismatch(('result', where, hex(original), hex(native & 0xFFFFFFFF)))
    if ledge is not None:
        column_calls = [e[1] for e in nat.log if e[0] == 'column']
        if column_calls != ledge.get('points', []):
            raise Mismatch(('column query point', case['label'], column_calls, ledge.get('points')))
    ram_n, spad_n = nat.export()
    if ee.mem != ram_n:
        raise Mismatch(('RAM', case['label'], first_diff(ee.mem, ram_n)))
    if ee.spad != spad_n:
        raise Mismatch(('scratchpad', case['label'], first_diff(ee.spad, spad_n, 0x70000000)))
    return ee.pcs


def run_case_safe(case):
    try:
        return ('ok', run_case(case))
    except Mismatch as error:
        return ('fail', str(error)[:2000])


# ------------------------------------------------------------------ cases

def bank_headers(ram):
    """clip -> (frames, next, start, events) from the player's bank, read
    through the same directory the original uses (001C6120's rule)."""
    bank = u32(ram, PLAYER + 0x40)
    count = u32(ram, bank)
    out = {}
    for clip in range(count):
        header = bank + ((struct.unpack_from('<i', ram, bank + 4 + 4 * clip)[0] >> 2) << 2)
        frames, nxt, start = struct.unpack_from('<Hhh', ram, header + 2)
        out[clip] = (frames, nxt, start, u32(ram, header + 0x14))
    return out


def image_list():
    images = [('playable_ee', REFERENCE / 'playable_ee.bin', None)]
    quick = ('05_boxes', '06_hill_slide', '08_truck_crossing')
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
    """The operations over one captured image, as captured and perturbed."""
    cases = []
    current = s16(struct.unpack_from('<H', ram, PLAYER + 0x20C)[0])
    rate = u32(ram, PLAYER + 0x34)

    def case(ops, patch=(), tag=''):
        cases.append(dict(image=label, ops=ops, patch=list(patch),
                          label=(label, tag or ops[0][0], len(cases))))
    case([('eval',)], tag='eval')
    case([('68C0',)])
    case([('6960',)])
    node0 = u32(ram, PLAYER + 0x110)
    case([('eval',)], patch=[(node0 + 0x64, struct.pack('<h', 3))], tag='eval_node0_parent')
    for dt in (rate, F(1.0), F(2.5)):
        case([('87C0', dt)])
    case([('87C0', F(1.0))], patch=[(D_6F3, b'\x01')], tag='87C0_latch')
    case([('resolve', 0x15D), ('8710', F(7.0))])
    case([('8710', F(0.0))])
    case([('resolve', 1), ('sample', F(5.0), F(8.0))])
    case([('sample', F(0.0), F(1.0))])
    case([('request', current, 0, F(8.0))])
    case([('request', 1, 0, F(8.0)), ('eval',)])
    case([('request', 0x15D, 1, F(8.0))])
    case([('request', 4, 0, F(0.0))], tag='request_zero_blend')
    case([('arbiter', current, F(12.0), F(0.0))])
    case([('arbiter', 5, F(6.0), F(4.0))])
    case([('arbiter', 2, F(0.0), F(27.0))], tag='arbiter_zero_blend')
    case([('bone_init', 0), ('68C0',)])
    case([('frames', c) for c in (0, 1, 0x15D, 458, current)])
    case([('handoff',)])
    # A request, then the ordinary per-callback advance (0015BA50's step is
    # +34 = rate * +204; 1.0 here) and the skeleton at the end.
    ticks = reference_mode.pick(40, 12)
    hold = [c for c, (fr, nx, _, _) in sorted(BANK.items()) if 2 <= fr <= 10 and nx == -2]
    loop = [c for c, (fr, nx, _, _) in sorted(BANK.items()) if fr <= 10 and nx == -1]
    follow = [c for c, (fr, nx, _, _) in sorted(BANK.items()) if fr <= 20 and 0 <= nx < len(BANK)]
    assert hold and loop and follow, 'the bank lacks a short hold / loop / follow-on clip'
    for clip, blend, count in ((hold[0], F(4.0), ticks), (loop[0], F(0.0), ticks),
                               (follow[0], F(0.0), max(ticks, BANK[follow[0]][0] + 4)), (1, F(8.0), ticks)):
        case([('request', clip, 1, blend)] + [('advance', F(1.0))] * count + [('eval',)], tag='sequence')
    return cases


def random_node_patch(rng, ram, crossing):
    """Perturb the node channel clocks, rates and values of the captured
    player's nodes; `crossing` makes some channels cross a key this step."""
    patch = []
    for i in range(ram[PLAYER + 0xC]):
        node = u32(ram, PLAYER + 0x110 + 4 * i)
        for off in (0x58, 0x5C, 0x60):
            if rng.random() < 0.5:
                value = rng.choice([0.0, 0.25, 0.5, 1.0, 1.5, 3.0, -0.5]) if crossing else rng.uniform(2.0, 20.0)
                patch.append((node + off, struct.pack('<f', value)))
        if rng.random() < 0.3:
            patch.append((node + 0x50, struct.pack('<f', rng.choice([0.0, 0.5, 1.0, 1.25, -0.25, rng.random()]))))
        if rng.random() < 0.3:
            patch.append((node + 0x54, struct.pack('<f', rng.uniform(-1.0, 1.0))))
        for off in (0x00, 0x0C, 0x18, 0x24, 0x30, 0x40, 0x70, 0x7C):
            if rng.random() < 0.15:
                lanes = 4 if off in (0x30, 0x40) else 3
                patch.append((node + off, struct.pack('<%df' % lanes, *(rng.uniform(-2, 2) for _ in range(lanes)))))
        if rng.random() < 0.15:
            parent = rng.choice([-1] + list(range(ram[PLAYER + 0xC])))
            patch.append((node + 0x64, struct.pack('<h', parent)))
        if rng.random() < 0.1:
            patch.append((node + 0x88, struct.pack('<3h', *(rng.choice([0x1000, 0x800, 0x1800, -0x1000, 3])
                                                            for _ in range(3)))))
    return patch


def random_cases(rng, count):
    """Randomized operations over the playable image's real bank and nodes."""
    label = 'playable_ee'
    ram = IMAGES[label][0]
    clips = sorted(BANK)
    out = []
    for index in range(count):
        kind = index % 7
        patch = random_node_patch(rng, ram, crossing=rng.random() < 0.7)
        clip = rng.choice(clips)
        frames = max(1, BANK[clip][0])
        frame = F(float(rng.randrange(frames)))
        if kind == 0:
            ops = [('resolve', clip), ('87C0', F(rng.choice([0.5, 1.0, 1.2, 1.4, 0.8, 2.0, 3.3, 0.0])))]
            if rng.random() < 0.3: patch.append((D_6F3, b'\x01'))
        elif kind == 1:
            ops = [('resolve', clip), ('8710', frame)]
        elif kind == 2:
            ops = [('resolve', clip), ('sample', frame, F(rng.choice([1.0, 4.0, 8.0, 12.0, 0.5])))]
        elif kind == 3:
            ops = [('request', clip, rng.choice([0, 1]), F(rng.choice([0.0, 1.0, 4.0, 8.0, 12.0])))]
        elif kind == 4:
            ops = [('arbiter', clip, F(rng.choice([0.0, 6.0, 8.0])), frame)]
        elif kind == 5:
            rec = [(PLAYER + 0xB0, struct.pack('<3f', *(rng.uniform(-300, 300) for _ in range(3)))),
                   (PLAYER + 0xC0, struct.pack('<3f', *(rng.uniform(-7, 7) for _ in range(3)))),
                   (PLAYER + 0x60, struct.pack('<3f', *(rng.choice([1.0, 0.5, 2.0, -1.0]) for _ in range(3))))]
            patch += rec
            ops = [(rng.choice(['eval', '68C0', '6960']),)]
        else:
            ops = [('bone_init', clip), ('87C0', F(1.0)), ('68C0',)]
        out.append(dict(image=label, ops=ops, patch=patch, label=('random', kind, index)))
    return out


SYN_BANK = 0x01F80000         # a synthetic clip bank (zero RAM in every image, checked)
SYN_NODES = 4


def pack_rotation(words):
    """The inverse of 001C84D0 for four 20-bit channels (top bits of floats)."""
    k = [w >> 12 & 0xFFFFF for w in words]
    return struct.pack('<5H', k[0] & 0xFFFF, (k[0] >> 16) | (k[1] & 0xFFF) << 4,
                       (k[1] >> 12) | (k[2] & 0xFF) << 8, (k[2] >> 8) | (k[3] & 0xF) << 12, k[3] >> 4)


def pack_translation(words, flag=0):
    """The inverse of 001C85D0 for three 26-bit channels; `flag` sets the
    0x8000 bit of the fifth halfword (the scale key's clip-end mark)."""
    c = [w >> 6 & 0x3FFFFFF for w in words]
    s4 = (c[2] >> 12) & 0x7FFF | (0x8000 if flag else 0)
    return struct.pack('<5H', c[0] & 0xFFFF, (c[0] >> 16) | (c[1] & 0x3F) << 10, (c[1] >> 6) & 0xFFFF,
                       (c[1] >> 22) | (c[2] & 0xFFF) << 4, s4)


def synthetic_bank(rng, clips=3):
    """A small clip bank in the original layout: the count, the directory
    (word offsets, 001C6120), per clip a header (+2 frames, +4 next, +6
    start, +8/+C/+10 section offsets, +14 event table, +20 parents) and three
    sections of per-node key streams (12-byte records: a packed key and its
    time at +A). Streams carry several keys; clip 1 has an event table."""
    data = bytearray(4 + 4 * clips)
    struct.pack_into('<I', data, 0, clips)
    headers = []
    for clip in range(clips):
        while len(data) % 16: data.append(0)
        header = len(data)
        struct.pack_into('<I', data, 4 + 4 * clip, header)
        frames = rng.choice([12, 20, 30])
        nxt = [-2, -1, 0][clip % 3]
        body = bytearray(0x20 + 4 * SYN_NODES)
        struct.pack_into('<Hhh', body, 2, frames, nxt, rng.choice([0, 2]))
        for i in range(SYN_NODES):
            struct.pack_into('<h', body, 0x20 + 4 * i, -1 if i == 0 else rng.randrange(i))
        events = b''
        if clip == 1:
            events = struct.pack('<hH', 2, 0) + struct.pack('<hHhH', 3, 0x10, 5, 0x100)
        sections = []
        for channel in range(3):
            directory = bytearray(4 * SYN_NODES)
            streams = bytearray()
            for i in range(SYN_NODES):
                struct.pack_into('<I', directory, 4 * i, len(directory) + len(streams))
                cuts = sorted(rng.sample(range(1, frames), rng.choice([0, 1, 2, 4])))
                times = [0] + cuts + [frames, frames + 50, frames + 90]
                for k, t in enumerate(times):
                    if rng.random() < 0.05:
                        key = bytes(rng.getrandbits(8) for _ in range(10))
                    elif channel == 0:
                        key = pack_rotation([F(rng.uniform(-1, 1)) for _ in range(4)])
                    else:
                        key = pack_translation([F(rng.uniform(-40, 40) if channel == 1 else rng.uniform(0.5, 1.5))
                                                for _ in range(3)], flag=channel == 2 and k >= len(times) - 3)
                    streams += key + struct.pack('<H', t)
            sections.append(bytes(directory + streams))
        offset = len(body) + len(events)
        struct.pack_into('<I', body, 0x14, len(body) if events else 0)
        for channel, section in enumerate(sections):
            struct.pack_into('<I', body, 0x8 + 4 * channel, offset)
            offset += len(section)
        data += body + events + b''.join(sections)
        headers.append(header)
    return bytes(data)


def synthetic_cases(rng, count):
    """Operations over a synthetic bank with multi-key streams (the captured
    player bank's scale streams have one interval each)."""
    label = 'playable_ee'
    out = []
    for index in range(count):
        bank = synthetic_bank(rng)
        clip = rng.randrange(3)
        patch = [(SYN_BANK, bank), (PLAYER + 0x40, struct.pack('<I', SYN_BANK)),
                 (PLAYER + 0xC, bytes([SYN_NODES])), (PLAYER + 0x20C, struct.pack('<h', 7))]
        frame = F(float(rng.randrange(12)))
        kind = index % 4
        if kind == 0:
            ops = [('bone_init', clip), ('8710', frame), ('eval',)]
        elif kind == 1:
            ops = [('bone_init', rng.randrange(3)), ('resolve', clip), ('sample', frame, F(rng.choice([1.0, 3.0, 8.0])))]
            ops += [('87C0', F(rng.choice([0.5, 1.0, 1.4, 2.5, 4.0]))) for _ in range(rng.choice([3, 8]))]
            ops += [('68C0',)]
            if rng.random() < 0.3: patch.append((D_6F3, b'\x01'))
        elif kind == 2:
            ops = [('bone_init', 0), ('request', clip, rng.choice([0, 1]), F(rng.choice([0.0, 2.0, 6.0])))]
            ops += [('advance', F(rng.choice([1.0, 1.2, 0.8, 1.4, 2.5])))] * rng.choice([6, 16, 30]) + [('eval',)]
        else:
            ops = [('bone_init', 1), ('arbiter', clip, F(rng.choice([0.0, 4.0])), frame)]
            ops += [('advance', F(1.0))] * 8 + [('6960',)]
        out.append(dict(image=label, ops=ops, patch=patch, label=('synthetic', kind, index)))
    return out


# Fixed atan2 results (both sides) whose heading 3pi/2 + atan2 001B1470
# must reduce over many 2pi steps: exponents 0x8C..0x8F in quick mode, and up
# to 0x94 (about 480,000 steps in the interpreter) in full mode.
HEADINGS = (9000.0, -9000.0, 40000.0, -50000.0)
HEADINGS_FULL = (3.0e6, -3.0e6)


def ledge_cases(rng, count, headings=()):
    out = []
    for index, value in enumerate(headings):
        hit = (F(12.0), F(-30.0), F(0.6), F(-0.8))
        patch = [(PLAYER + 0xB4, struct.pack('<f', 0.0)), (PLAYER + 0x316, b'\x00'),
                 (0x700031B0, struct.pack('<I', hit[0])), (0x700031B8, struct.pack('<I', hit[1])),
                 (0x700031D0, struct.pack('<I', HIT_RECORD)),
                 (HIT_RECORD + 0x24, struct.pack('<I', hit[2])), (HIT_RECORD + 0x2C, struct.pack('<I', hit[3]))]
        script = dict(table=[(1, F(20.5), F(0.5))], hit=hit, atan2=F(value))
        out.append(dict(image='playable_ee', ops=[('ledge', 1)], patch=patch, ledge=script,
                        label=('ledge_heading', index)))
    for index in range(count):
        b4 = rng.choice([0.0, 10.0, -20.5, rng.uniform(-100, 100)])
        y = fnum(F(20.5 + b4))
        rows = []
        for _ in range(rng.choice([0, 1, 2, 3, 6])):
            flags = rng.choice([0, 1, 3, 2, 0x101])
            aux = F(rng.choice([0.0, 0.5, 0.6283185, 0.62831855, 0.7, -1.0]))
            height = F(y + rng.choice([-2.0, -1.0, -0.999, 0.0, 0.5, 0.9999, 1.0, 1.5]))
            rows.append((flags, height, aux))
        hit = tuple(F(v) for v in (rng.uniform(-400, 400), rng.uniform(-400, 400),
                                   rng.uniform(-1, 1), rng.uniform(-1, 1)))
        patch = [(PLAYER + 0xB4, struct.pack('<f', b4)), (PLAYER + 0x316, bytes([rng.choice([0, 1])])),
                 (0x700031B0, struct.pack('<I', hit[0])), (0x700031B8, struct.pack('<I', hit[1])),
                 (0x700031D0, struct.pack('<I', HIT_RECORD)),
                 (HIT_RECORD + 0x24, struct.pack('<I', hit[2])), (HIT_RECORD + 0x2C, struct.pack('<I', hit[3]))]
        script = dict(table=rows, hit=hit)
        out.append(dict(image='playable_ee', ops=[('ledge', rng.choice([0, 1]))], patch=patch, ledge=script,
                        label=('ledge', index)))
    return out


# ------------------------------------------------------------------ leaves

def check_leaves(rng):
    """00128250 (the em_stream_lanes binding), the key decoders, quat_nlerp,
    quat_to_mat3, the build_trs_matrix and 00102C58 adapters and the
    em_owner_services rotations they bind."""
    ee = leaf_ee()
    n = 0
    specials = [0, 0x80000000, 1, 0x007FFFFF, 0x00800000, 0x3F800000, 0x3F7FFFFF, 0x40000000, 0x4EFFFFFF,
                0x4F000000, 0x4F7FFFFF, 0x4F800000, 0x5F000000, 0x7F7FFFFF, 0x7F800000, 0xFF800000,
                0x7FC00000, 0x7F800001, 0xBF800000, 0x42F00000, 0x42EFFFFF, 0x3EFFFFFF, 0x4B000001]
    for v in specials + [rng.getrandbits(32) for _ in range(reference_mode.pick(20000, 300))]:
        if (v >> 23 & 0xFF) == 0xFF and v & 0x7FFFFF and not v & 0x400000:
            continue                                   # no signalling NaN through the FPU registers
        v0, _ = ee.invoke(TOUINT, (), (v,))
        got = LIB.em_stream_lanes_00128250(v)
        assert v0 & 0xFFFFFFFF == got, ('00128250', hex(v), hex(v0), hex(got))
        n += 1
    src, dst = SCRATCH_RAM, SCRATCH_RAM + 0x40
    for _ in range(reference_mode.pick(5000, 150)):
        data = bytes(rng.getrandbits(8) for _ in range(10))
        spad0 = [rng.getrandbits(32) for _ in range(4)]
        for which, entry, lanes, fn in ((0, DEC_R, 4, LIB.em_pose_host_001C84D0),
                                        (1, DEC_T, 3, LIB.em_pose_host_001C85D0)):
            ee.write(src, data)
            ee.write(dst, bytes(16))
            ee.spad[0x3600:0x3610] = struct.pack('<4I', *spad0)
            ee.invoke(entry, (src, dst))
            out, sp = (U32 * 4)(), (U32 * 4)(*spad0)
            fn((U8 * 10).from_buffer_copy(data), out, sp)
            assert list(struct.unpack('<%dI' % lanes, ee.read(dst, 4 * lanes))) == list(out)[:lanes], \
                ('decode', which, data.hex())
            assert list(struct.unpack('<4I', ee.spad[0x3600:0x3610])) == list(sp), ('decode spad', which)
            n += 1
    specials = [0, 0x80000000, 0x3F800000, 0xBF800000, 0x3F000000, 0x3F800001, 0x7F7FFFFF, 0xFF7FFFFF,
                0x00000001, 0x3E800000, 0x40000000]
    for _ in range(reference_mode.pick(20000, 300)):
        a = [rng.choice(specials) if rng.random() < 0.1 else F(rng.uniform(-1.2, 1.2)) for _ in range(4)]
        b = [rng.choice(specials) if rng.random() < 0.1 else F(rng.uniform(-1.2, 1.2)) for _ in range(4)]
        t = rng.choice(specials + [F(rng.uniform(-0.5, 1.5)) for _ in range(6)])
        ee.write(src, struct.pack('<4I', *a))
        ee.write(src + 0x10, struct.pack('<4I', *b))
        ee.invoke(NLERP, (dst, src, src + 0x10), (t,))
        out = (U32 * 4)()
        LIB.em_pose_host_001CA0A0(out, (U32 * 4)(*a), (U32 * 4)(*b), t)
        assert list(struct.unpack('<4I', ee.read(dst, 16))) == list(out), ('nlerp', a, b, hex(t))
        q = [F(rng.uniform(-1.5, 1.5)) for _ in range(4)]
        tr = [rng.getrandbits(32) & 0xBFFFFFFF for _ in range(3)]
        ee.write(src, struct.pack('<4I', *q))
        ee.write(src + 0x10, struct.pack('<3I', *tr))
        ee.invoke(QMAT, (dst, src, src + 0x10))
        m, sp = (U32 * 16)(), (U32 * 11)(*struct.unpack('<11I', ee.spad[0x3760:0x378C]))
        LIB.em_pose_host_001CA1C0(m, (U32 * 4)(*q), (U32 * 3)(*tr), sp)
        assert list(struct.unpack('<16I', ee.read(dst, 64))) == list(m), ('quat_to_mat3', q)
        assert list(struct.unpack('<11I', ee.spad[0x3760:0x378C])) == list(sp), 'quat_to_mat3 scratch'
        n += 2
    # The build_trs_matrix and 00102C58 adapters (em_owner_services).
    nat = Native(IMAGES['playable_ee'][0], IMAGES['playable_ee'][1])
    for index in range(reference_mode.pick(3000, 40)):
        pos = [F(rng.uniform(-500, 500)) for _ in range(3)]
        rot = [F(rng.choice([0.0, -0.0, 3.14159274, -3.14159274, 1.5707964, rng.uniform(-7, 7)])) for _ in range(3)]
        sc = [F(rng.choice([1.0, 0.5, 2.0, -1.0, rng.uniform(-3, 3)])) for _ in range(3)]
        ee.write(src, struct.pack('<4I', *pos, 0))
        ee.write(src + 0x10, struct.pack('<4I', *rot, 0))
        ee.write(src + 0x20, struct.pack('<4I', *sc, 0))
        ee.invoke(TRS, (dst, src, src + 0x10, src + 0x20))
        expect = list(struct.unpack('<16I', ee.read(dst, 64)))
        out = (U32 * 16)()
        status = LIB.em_pose_host_build_trs_matrix(C.byref(nat.host), out, (U32 * 3)(*pos), (U32 * 3)(*rot),
                                                   (U32 * 3)(*sc))
        assert status == 0 and list(out) == expect, ('build_trs_matrix', pos, rot, sc)
        m = [rng.getrandbits(32) & 0xBFFFFFFF for _ in range(16)]
        ee.write(src + 0x40, struct.pack('<16I', *m))
        ee.invoke(0x102C58, (dst, src + 0x40, src + 0x10))
        out = (U32 * 16)()
        status = LIB.em_pose_host_euler(None, out, (U32 * 16)(*m), (U32 * 3)(*rot))
        assert status == 0 and list(out) == list(struct.unpack('<16I', ee.read(dst, 64))), ('00102C58', rot)
        n += 2
    # Aliased arguments: every pointer into one 24-word buffer, on both
    # sides, the whole buffer compared (each argument is read where the
    # original reads it, so an argument inside the output sees the stores
    # made before that read). The matrix rows, the scale and the position
    # are quadword reads in the original (the low four address bits are not
    # used), so those pointers stay 16-byte aligned, as every caller's are;
    # the angles are word reads and may sit anywhere.
    buf_at = SCRATCH_RAM + 0x200
    for index in range(reference_mode.pick(400, 24)):
        words = [F(rng.uniform(-3, 3)) for _ in range(24)]
        kind = index % 4
        if kind == 0:                   # euler in place (in == out), angles after it
            args, call = (0, 0, 16), 'euler'
        elif kind == 1:                 # euler with in four words above out (partial overlap)
            args, call = (0, 4, 20), 'euler'
        elif kind == 2:                 # build_trs_matrix: position = row 3, scale = row 0
            args, call = (0, 12, 17, 0), 'trs'
        else:                           # build_trs_matrix: rotation straddling out's end
            args, call = (4, 20, 18, 0), 'trs'
        ee.write(buf_at, struct.pack('<24I', *words))
        ptrs = tuple(buf_at + 4 * k for k in args)
        if call == 'euler':
            ee.invoke(0x102C58, ptrs)
        else:
            ee.invoke(TRS, ptrs)
        buf = (U32 * 24)(*words)
        at = [C.cast(C.byref(buf, 4 * k), PU32) for k in args]
        fn = LIB.em_pose_host_euler if call == 'euler' else LIB.em_pose_host_build_trs_matrix
        assert fn(None, *at) == 0, ('aliased', call, args)
        assert list(buf) == list(struct.unpack('<24I', ee.read(buf_at, 96))), ('aliased', call, args)
        n += 1
    # The em_owner_services rotations the adapters bind, against the original.
    for _ in range(reference_mode.pick(6000, 300)):
        m = [F(rng.uniform(-3, 3)) if rng.random() < 0.9 else rng.choice([0, 0x80000000, 0x3F800000])
             for _ in range(16)]
        angle = F(rng.choice([0.0, -0.0, 3.14159274, -3.14159274, 1.5707964, -1.5707964, 6.2831855,
                              rng.uniform(-7, 7), rng.uniform(-0.01, 0.01)]))
        axis = rng.randrange(3)
        out = (U32 * 16)()
        fn = (LIB.em_owner_services_rotate_x_00102B08, LIB.em_owner_services_rotate_y_00102BB0,
              LIB.em_owner_services_rotate_z_00102A60)[axis]
        assert fn(out, (U32 * 16)(*m), angle) == 0
        assert list(out) == original_rotate(m, angle, axis), ('owner services rotation', axis, hex(angle))
        n += 1
    # 0017C540 over random records (through the adapter).
    for _ in range(reference_mode.pick(2000, 60)):
        rec = bytearray(rng.getrandbits(8) for _ in range(0x320))
        rec[0x25C] = rng.choice([0, 0, 1, 7])
        ee.write(SCRATCH_RAM + 0x1000, bytes(rec))
        ee.invoke(HANDOFF, (SCRATCH_RAM + 0x1000,))
        actor = LiveActor()
        C.memmove(actor.bytes, bytes(rec), 0x320)
        assert LIB.em_pose_host_handoff(None, C.byref(actor)) == 0
        assert bytes(actor.bytes) == ee.read(SCRATCH_RAM + 0x1000, 0x320), '0017C540'
        n += 1
    return n, ee.pcs


# ------------------------------------------------------------------ adapters, fail-stop

def check_adapters():
    """The EmPlayerLiveActor adapters and the EmPlayerStageCallees entries
    over a record copied out of the playable image, against the original."""
    ram, spad = IMAGES['playable_ee']
    checks = 0
    for name, entry, ints, fl, call in (
            ('request', REQUEST, (0x15D, 1), (F(8.0),), lambda n, h, a: LIB.em_pose_host_request(h, a, 0x15D, 1, 8.0)),
            ('arbiter', ARBITER, (5,), (F(6.0), F(4.0)), lambda n, h, a: LIB.em_pose_host_arbiter(h, a, 5, 6.0, 4.0)),
            ('eval', EVAL, (), (), lambda n, h, a: LIB.em_pose_host_eval_skeleton(h, a)),
            ('skeleton', E68C0, (), (), lambda n, h, a: LIB.em_pose_host_skeleton(h, a)),
            ('bone_init', BONE_INIT, (1,), (), lambda n, h, a: LIB.em_pose_host_stage_bone_init(h, a, 1)),
            ('clip_init', CLIP_INIT, (2,), (F(4.0), F(3.0)),
             lambda n, h, a: LIB.em_pose_host_stage_clip_init(h, a, 2, 4.0, 3.0)),
            ('stage_request', REQUEST, (1, 0), (F(0.0),), lambda n, h, a: LIB.em_pose_host_stage_request(h, a, 1, 0, 0.0)),
    ):
        ee = EE(ELF, ram=ram, spad=spad)
        nat = Native(ram, spad)
        actor = LiveActor()
        C.memmove(actor.bytes, bytes(ram[PLAYER:PLAYER + 0x320]), 0x320)
        ee.invoke(entry, (PLAYER,) + ints, fl)
        assert call(nat, C.byref(nat.host), C.byref(actor)) == 0, name
        ram_n, spad_n = nat.export()
        ram_n[PLAYER:PLAYER + 0x320] = bytes(actor.bytes)
        assert ee.mem == ram_n, ('adapter RAM', name, first_diff(ee.mem, ram_n))
        assert ee.spad == spad_n, ('adapter scratchpad', name)
        checks += 1
    nat = Native(ram, spad)
    actor = LiveActor()
    C.memmove(actor.bytes, bytes(ram[PLAYER:PLAYER + 0x320]), 0x320)
    ee = EE(ELF, ram=ram, spad=spad)
    for clip in (0, 0x15D):
        v0, _ = ee.invoke(FRAMES, (u32(ram, PLAYER + 0x40), clip))
        out = C.c_int(-1)
        assert LIB.em_pose_host_clip_frames_actor(C.byref(nat.host), C.byref(actor), clip, C.byref(out)) == 0
        assert out.value == v0, ('clip_frames_actor', clip)
        checks += 1
    word = U32()
    node0 = u32(ram, PLAYER + 0x110)
    assert LIB.em_pose_host_node_word(C.byref(nat.host), node0, 0x64, C.byref(word)) == 0
    assert word.value == u32(ram, node0 + 0x64)
    return checks + 1


class View(C.Structure):
    _fields_ = [('host', C.c_void_p), ('actor', C.c_void_p), ('d275B40', C.POINTER(U32))]


D_B40 = 0x275B40


def check_views():
    """The bound-actor view adapters (slide / climb request, arbiter,
    clip_frames; the hang / 0E-18 / major2 node readers; the recovery and
    climb skeleton steps) against the original. D_00275B40 holds the
    player's +110 in both images, as anim_bone_array_setup leaves it for the
    player's callback (00161790 and 00162A40 read node 1 through it)."""
    ram, spad = IMAGES['playable_ee']
    ram = bytearray(ram)
    struct.pack_into('<I', ram, D_B40, PLAYER + 0x110)
    checks = 0

    def fresh():
        ee = EE(ELF, ram=ram, spad=spad)
        nat = Native(ram, spad)
        actor = LiveActor()
        C.memmove(actor.bytes, bytes(ram[PLAYER:PLAYER + 0x320]), 0x320)
        word = U32(PLAYER + 0x110)
        view = View(C.addressof(nat.host), C.addressof(actor), C.pointer(word))
        return ee, nat, actor, view, word

    def same(ee, nat, actor, name):
        ram_n, spad_n = nat.export()
        ram_n[PLAYER:PLAYER + 0x320] = bytes(actor.bytes)
        assert ee.mem == ram_n, ('view RAM', name, first_diff(ee.mem, ram_n))
        assert ee.spad == spad_n, ('view scratchpad', name)

    for name, entry, ints, fl, call in (
            ('request', REQUEST, (0x15D, 1), (F(8.0),), lambda v: LIB.em_pose_view_request(v, 0x15D, 1, 8.0)),
            ('request_same', REQUEST, (s16(u32(ram, PLAYER + 0x20C)), 0), (F(8.0),),
             lambda v: LIB.em_pose_view_request(v, s16(u32(ram, PLAYER + 0x20C)), 0, 8.0)),
            ('arbiter', ARBITER, (5,), (F(0.0), F(4.0)), lambda v: LIB.em_pose_view_arbiter(v, 5, 0.0, 4.0)),
            ('request_bits', REQUEST, (1, 1), (F(12.0),),
             lambda v: LIB.em_pose_host_request_bits(v._obj.host, C.cast(v._obj.actor, C.POINTER(LiveActor)), 1, 1,
                                                     F(12.0))),
    ):
        ee, nat, actor, view, _ = fresh()
        ee.invoke(entry, (PLAYER,) + ints, fl)
        assert call(C.byref(view)) == 0, name
        same(ee, nat, actor, name)
        checks += 1
    ee, nat, actor, view, _ = fresh()
    for clip in (0, 1, 0x15D):
        v0, _ = ee.invoke(FRAMES, (u32(ram, PLAYER + 0x40), clip))
        out = C.c_int(-1)
        assert LIB.em_pose_view_clip_frames(C.byref(view), clip, C.byref(out)) == 0
        assert out.value == v0, ('view clip_frames', clip)
        checks += 1
    same(ee, nat, actor, 'clip_frames')
    for node, offset in ((0, 4), (0, 8), (1, 8), (1, 0xC0), (1, 0xC4), (1, 0xC8), (1, 0xCC), (3, 0x90)):
        address = u32(ram, u32(ram, D_B40) + 4 * node) + offset
        bits, value = U32(), C.c_float()
        assert LIB.em_pose_view_node_bits(C.byref(view), node, offset, C.byref(bits)) == 0
        assert LIB.em_pose_view_node_float(C.byref(view), node, offset, C.byref(value)) == 0
        assert bits.value == u32(ram, address) and F(value.value) == u32(ram, address), ('node', node, offset)
        if node == 0:
            assert LIB.em_pose_view_root_node(C.byref(view), offset, C.byref(bits)) == 0
            assert bits.value == u32(ram, address)
        checks += 1
    node1 = u32(ram, u32(ram, D_B40) + 4)
    x, z, words = U32(), U32(), (U32 * 16)()
    assert LIB.em_pose_view_hip_xz(C.byref(view), C.byref(x), C.byref(z)) == 0
    assert (x.value, z.value) == (u32(ram, node1 + 0xC0), u32(ram, node1 + 0xC8)), 'hip_xz'
    assert LIB.em_pose_view_node1_words(C.byref(view), words) == 0
    assert list(words)[:4] == [u32(ram, node1 + 0xC0 + 4 * k) for k in range(4)], 'node1_words'
    assert LIB.em_pose_view_root_clock(C.byref(view), C.byref(x)) == 0
    assert x.value == u32(ram, u32(ram, u32(ram, D_B40)) + 8), 'root_clock'
    for slot in (0, 4, 20):
        node = u32(ram, u32(ram, D_B40) + 4 * slot)
        assert LIB.em_pose_view_bone(C.byref(view), slot, words) == 0
        assert list(words) == [u32(ram, node + 0x90 + 4 * k) for k in range(16)], ('bone', slot)
    checks += 6
    # 00102C58 through the void-context adapter (EmPlayerLadderWorkers.euler).
    ee = leaf_ee()
    nat = Native(ram, spad)
    m = [F(v) for v in (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 3, 4, 5, 1)]
    angles = [F(0.3), F(-1.2), F(2.5)]
    ee.write(SCRATCH_RAM + 0x40, struct.pack('<16I', *m))
    ee.write(SCRATCH_RAM + 0x100, struct.pack('<3I', *angles))
    ee.invoke(0x102C58, (SCRATCH_RAM, SCRATCH_RAM + 0x40, SCRATCH_RAM + 0x100))
    out = (U32 * 16)()
    assert LIB.em_pose_host_euler(C.byref(nat.host), out, (U32 * 16)(*m), (U32 * 3)(*angles)) == 0
    assert list(out) == list(struct.unpack('<16I', ee.read(SCRATCH_RAM, 64))), 'euler'
    # The angles inside out (words 4..6): each is read after the previous
    # rotation stored out, as the original reloads it.
    ee.write(SCRATCH_RAM, bytes(64))
    ee.write(SCRATCH_RAM + 0x10, struct.pack('<3I', *angles))
    ee.invoke(0x102C58, (SCRATCH_RAM, SCRATCH_RAM + 0x40, SCRATCH_RAM + 0x10))
    out = (U32 * 16)(*([0] * 4 + angles + [0] * 9))
    assert LIB.em_pose_host_euler(C.byref(nat.host), out, (U32 * 16)(*m),
                                  C.cast(C.byref(out, 16), C.POINTER(U32))) == 0
    assert list(out) == list(struct.unpack('<16I', ee.read(SCRATCH_RAM, 64))), 'euler aliased'
    # build_trs_matrix with the rotation inside out: read after each rotation.
    pos, sc = [F(3.0), F(-2.0), F(7.5)], [F(1.5), F(0.5), F(-1.0)]
    ee.write(SCRATCH_RAM, bytes(64))
    ee.write(SCRATCH_RAM + 0x10, struct.pack('<3I', *angles))
    ee.write(SCRATCH_RAM + 0x100, struct.pack('<4I', *pos, 0))
    ee.write(SCRATCH_RAM + 0x110, struct.pack('<4I', *sc, 0))
    ee.invoke(TRS, (SCRATCH_RAM, SCRATCH_RAM + 0x100, SCRATCH_RAM + 0x10, SCRATCH_RAM + 0x110))
    out = (U32 * 16)(*([0] * 4 + angles + [0] * 9))
    assert LIB.em_pose_host_build_trs_matrix(C.byref(nat.host), out, (U32 * 3)(*pos),
                                             C.cast(C.byref(out, 16), C.POINTER(U32)), (U32 * 3)(*sc)) == 0
    assert list(out) == list(struct.unpack('<16I', ee.read(SCRATCH_RAM, 64))), 'build_trs_matrix aliased'
    checks += 3
    # 00162A40 state 0xA / 00161790 pull-up: anim_eval_skeleton(p), then node 1.
    ee, nat, actor, view, _ = fresh()
    ee.invoke(EVAL, (PLAYER,))
    node1 = u32(ee.mem, u32(ee.mem, D_B40) + 4)
    out = (C.c_float * 4)()
    assert LIB.em_pose_view_eval_node1(C.byref(view), C.byref(actor), out) == 0
    assert [F(out[k]) for k in range(4)] == [u32(ee.mem, node1 + 0xC0 + 4 * k) for k in range(4)], 'eval_node1'
    same(ee, nat, actor, 'eval_node1')
    ee, nat, actor, view, _ = fresh()
    ee.invoke(EVAL, (PLAYER,))
    y, w8 = C.c_float(), C.c_float()
    assert LIB.em_pose_view_eval_hip(C.byref(view), C.byref(y), C.byref(w8)) == 0
    assert (F(y.value), F(w8.value)) == (u32(ee.mem, node1 + 0xC4), u32(ee.mem, node1 + 0x8)), 'eval_hip'
    same(ee, nat, actor, 'eval_hip')
    ee, nat, actor, view, _ = fresh()
    ee.invoke(EVAL, (PLAYER,))
    out = (C.c_float * 4)(-1.0, -1.0, -1.0, 12345.0)
    assert LIB.em_pose_view_eval_node1_xyz(C.byref(view), C.byref(actor), out) == 0
    assert [F(out[k]) for k in range(3)] == [u32(ee.mem, node1 + 0xC0 + 4 * k) for k in range(3)], 'eval_node1_xyz'
    assert out[3] == 12345.0, 'eval_node1_xyz wrote a fourth float'
    same(ee, nat, actor, 'eval_node1_xyz')
    checks += 3
    # Fail-stop: no D_00275B40, or one pointing at unmapped memory, writes nothing.
    for bad in (None, 0x02100000):
        _, nat, actor, view, word = fresh()
        if bad is None: view.d275B40 = C.POINTER(U32)()
        else: word.value = bad
        before, rec = nat.export(), bytes(actor.bytes)
        out = (C.c_float * 4)()
        assert LIB.em_pose_view_eval_node1(C.byref(view), C.byref(actor), out) == -1
        assert LIB.em_pose_view_eval_hip(C.byref(view), C.byref(C.c_float()), C.byref(C.c_float())) == -1
        assert LIB.em_pose_view_eval_node1_xyz(C.byref(view), C.byref(actor), out) == -1
        assert LIB.em_pose_view_bone(C.byref(view), 4, (U32 * 16)()) == -1
        assert LIB.em_pose_view_hip_xz(C.byref(view), C.byref(U32()), C.byref(U32())) == -1
        assert nat.export() == before and bytes(actor.bytes) == rec, 'view fail-stop wrote'
        checks += 1
    return checks


def check_fail_stop():
    """Every entry faults (-1) with nothing written when a worker it can
    reach is missing or a record it touches is not mapped."""
    ram, spad = IMAGES['playable_ee']
    checks = 0

    def attempt(mutate, call):
        nat = Native(ram, spad)
        mutate(nat)
        before = nat.export()
        status = call(nat, C.byref(nat.host))
        assert status == -1, ('fail-stop status', status)
        assert nat.export() == before, 'fail-stop wrote'
        return 1

    def unbound(field, kind=PU32):
        return lambda n: setattr(n.g, field, kind())
    no_advance = lambda n: setattr(n.host.callees, 'advance', ADVANCE_FN())
    unmapped = lambda n: setattr(n.host, 'region_count', 0)
    out = C.c_int()
    rec = lambda n: n.rec()
    for mutate, call in (
            (unbound('spad3440'), lambda n, h: LIB.em_pose_host_001C6DA0(h, rec(n), 0x320)),
            (unbound('spad3400'), lambda n, h: LIB.em_pose_host_001C68C0(h, rec(n), 0x320)),
            (unbound('spad3760'), lambda n, h: LIB.em_pose_host_001C6960(h, rec(n), 0x320)),
            (unbound('d8106F3', C.POINTER(U8)),
             lambda n, h: LIB.em_pose_host_001C87C0(h, n.rec(PLAYER + 0x110), 0x210, 21, 1.0)),
            (unbound('column', C.POINTER(FloorTable)),
             lambda n, h: LIB.em_pose_host_00178910(h, rec(n), 0x320, 1, C.byref(out))),
            (unbound('spad3A20'), lambda n, h: LIB.em_pose_host_001C8D50(h, n.rec(PLAYER + 0x110), 0x210, 21,
                                                                       1.0, 8.0)),
            (no_advance, lambda n, h: LIB.em_pose_host_001749A0(h, rec(n), 0x320, 3, 1, 0.0, C.byref(out))),
            (no_advance, lambda n, h: LIB.em_pose_host_001749F0(h, rec(n), 0x320, 3, 8.0, 0.0, C.byref(out))),
            (no_advance, lambda n, h: LIB.em_pose_host_001C67E0(h, rec(n), 0x320, 3, 8.0, 0.0)),
            (unmapped, lambda n, h: LIB.em_pose_host_001749A0(h, rec(n), 0x320, 3, 1, 8.0, C.byref(out))),
            (unmapped, lambda n, h: LIB.em_pose_host_001C63E0(h, rec(n), 0x320, 0)),
            (unmapped, lambda n, h: LIB.em_pose_host_001C87C0(h, n.rec(PLAYER + 0x110), 0x210, 21, 1.0)),
            (unmapped, lambda n, h: LIB.em_pose_host_001C8710(h, n.rec(PLAYER + 0x110), 0x210, 21, 1.0)),
            (unmapped, lambda n, h: LIB.em_pose_host_001C8D50(h, n.rec(PLAYER + 0x110), 0x210, 21, 1.0, 8.0)),
            (unmapped, lambda n, h: LIB.em_pose_host_001C8480(h, u32(ram, PLAYER + 0x40), 1)),
            (lambda n: None, lambda n, h: LIB.em_pose_host_00178910(h, rec(n), 0x320, 1, C.byref(out))),
            (lambda n: None, lambda n, h: LIB.em_pose_host_001749A0(h, rec(n), 0x100, 3, 1, 8.0, C.byref(out))),
    ):
        checks += attempt(mutate, call)
    return checks + check_heading_refusal()


def check_heading_refusal():
    """00178910's one native refusal, grounded in the float model: 001B1470's
    2pi step (EE sub / add, tools/ee_float_model.py) leaves every angle of
    biased exponent 0x9A..0xFE unchanged, so the original never returns on
    one, and moves every smaller angle (those reduce; HEADINGS run them
    against the original). Exponent 0xFF clamps to 0x7F7FFFFF and then
    stalls. The native reduces a heading of exponent 0x99 and refuses 0x9A
    and a non-finite atan2 result with the record unwritten."""
    import ee_float_model as M
    two_pi = 0x40C90FDB
    for e in range(0x80, 0xFF):
        for mant in (0, 1, 0x400000, 0x7FFFFF):
            x = e << 23 | mant
            moves = M.ee_sub(x, two_pi) != x and M.ee_add(x | 0x80000000, two_pi) != x | 0x80000000
            assert moves == (e < 0x9A), ('001B1470 step', hex(x))
    assert M.ee_sub(0x7F800000, two_pi) == 0x7F7FFFFF, '001B1470 step on exponent 0xFF'
    ram, spad = IMAGES['playable_ee']
    y = M.ee_add(F(20.5), u32(ram, PLAYER + 0xB4))
    checks = 0
    for value, refused in ((1.0e8, False), (-1.0e8, False), (2.0e8, True), (-2.0e8, True),
                           (float('inf'), True)):
        heading = M.ee_add(0x4096CBE4, F(value))
        assert ((heading >> 23 & 0xFF) >= 0x9A) == refused, (value, hex(heading))
        script = dict(table=[(1, y, F(0.5))], hit=(F(12.0), F(-30.0), F(0.6), F(-0.8)), atan2=F(value))
        nat = Native(ram, spad, ledge=script)
        before = bytes(nat.ram[PLAYER:PLAYER + 0x320])
        out = C.c_int(-7)
        status = LIB.em_pose_host_00178910(C.byref(nat.host), nat.rec(), 0x320, 1, C.byref(out))
        if refused:
            assert status == -1 and bytes(nat.ram[PLAYER:PLAYER + 0x320]) == before, ('heading refusal', value)
        else:
            reduced = fnum(u32(nat.ram, PLAYER + 0x218))
            pi = fnum(0x40490FDB)
            assert status == 0 and out.value == 1 and -pi < reduced <= pi, ('heading', value)
        checks += 1
    return checks


# ------------------------------------------------------------------ coverage

def function_table():
    sizes = {}
    for line in FUNCTIONS.read_text().splitlines()[1:]:
        vram, name, size = line.split(',')[:3]
        sizes[name] = (int(vram, 16), int(size))
    return sizes


def word_at(address):
    return u32(ELF, address - 0x100000 + 0x300)


def reachable(start, size):
    """Instruction addresses reachable from start inside [start, start+size),
    minus the negative-word block of the unsigned-to-float idiom after a
    halfword load (a branch on a zero-extended halfword that cannot be taken)."""
    end, seen, todo, dead = start + size, set(), [start], set()
    while todo:
        pc = todo.pop()
        while start <= pc < end and pc not in seen:
            seen.add(pc)
            w = word_at(pc)
            op, rs, rt = w >> 26, w >> 21 & 31, w >> 16 & 31
            target = pc + 4 + s16(w) * 4
            if op == 0 and w & 63 == 8 or op == 2:
                seen.add(pc + 4)
                break
            if op == 4 and rs == 0 and rt == 0:
                seen.add(pc + 4)
                pc = target
                continue
            if op == 1 and rt == 0:                           # bltz
                follow, at_target = word_at(pc + 12), word_at(target)
                idiom = ((at_target >> 26 == 0 and at_target & 63 == 2 and (at_target >> 16 & 31) == rs
                          and (at_target >> 6 & 31) == 1) or
                         (at_target >> 26 == 12 and (at_target >> 21 & 31) == rs and at_target & 0xFFFF == 1))
                if idiom and follow >> 26 == 4 and (follow >> 16 & 31) == 0 and (follow >> 21 & 31) == 0:
                    join = pc + 12 + 4 + s16(follow) * 4
                    dead.update(range(pc + 20, join, 4))
                    dead.add(target)
            if op in (1, 4, 5, 6, 7, 20, 21, 22, 23) or (op == 17 and rs == 8):
                seen.add(pc + 4)
                todo.append(target)
                pc += 8
                continue
            pc += 4
    return seen - dead


def check_coverage(pcs):
    table = function_table()
    missing = {}
    for name in EXECUTED:
        start, size = table[name]
        want = reachable(start, size)
        got = {pc for pc in want if pc in pcs or (word_at(pc) >> 26 in (2, 3) or
                                                  (word_at(pc) >> 26 == 0 and word_at(pc) & 63 in (8, 9)))
               and pc + 4 in pcs}
        lost = sorted(want - got)
        if lost:
            missing[name] = [hex(pc) for pc in lost[:12]] + (['...'] if len(lost) > 12 else [])
    return missing


# ------------------------------------------------------------------ capture check

def capture_check(label, ram, spad):
    """Native only, against the capture itself: the player tail 0015BCF0
    evaluated the skeleton after the frame's animation step, from +B0 (the
    value it copies to +A0 afterwards). Re-evaluating the captured channels
    with that position must give the captured node world matrices, when the
    tail's selection is one of this module's evaluators."""
    rec = bytes(ram[PLAYER:PLAYER + 0x320])
    mode, hold, clip = rec[0x2F3], rec[0x303], s16(struct.unpack_from('<H', rec, 0x20C)[0])
    row = struct.unpack_from('<h', ram, 0x248C90 + clip * 0xC)[0]
    if mode == 0 and hold:
        return None
    kind = ('eval' if row else '68C0') if mode == 0 else ('68C0' if mode in (3, 4) else '6960')
    nat = Native(ram, spad)
    nat.ram[PLAYER + 0xB0:PLAYER + 0xBC] = rec[0xA0:0xAC]
    fn = {'eval': LIB.em_pose_host_001C6DA0, '68C0': LIB.em_pose_host_001C68C0,
          '6960': LIB.em_pose_host_001C6960}[kind]
    assert fn(C.byref(nat.host), nat.rec(), 0x320) == 0
    worst = []
    for i in range(rec[0xC]):
        node = u32(rec, 0x110 + 4 * i)
        a, b = ram[node + 0x90:node + 0xD0], nat.ram[node + 0x90:node + 0xD0]
        if a != b:
            worst.append(i)
    return kind, worst


# ------------------------------------------------------------------ main

def main():
    global ELF, LIB, BANK
    started = time.time()
    ELF = SR.read_elf()
    LIB = build_native()
    for label, path, spad_path in image_list():
        ram = path.read_bytes()
        spad = spad_path.read_bytes() if spad_path else bytes(0x4000)
        assert ram[SCRATCH_RAM:SCRATCH_RAM + 0x2000] == bytes(0x2000), ('scratch RAM not zero', label)
        assert ram[HIT_RECORD:HIT_RECORD + 0x40] == bytes(0x40), ('hit record RAM not zero', label)
        assert ram[SYN_BANK:SYN_BANK + 0x10000] == bytes(0x10000), ('synthetic bank RAM not zero', label)
        IMAGES[label] = (ram, spad)
    table = function_table()
    for label, (ram, _) in IMAGES.items():
        for name in EXECUTED + ('func_001029E8', 'func_00102A60', 'func_00102B08', 'func_00102BB0',
                                'anim_advance_time', 'float_to_int', 'func_001278C0', 'func_0011DF78',
                                'func_001B1470'):
            start, size = table[name]
            assert ram[start:start + size] == ELF[start - 0x100000 + 0x300:start - 0x100000 + 0x300 + size], \
                ('captured code differs from the ELF', label, name)
    BANK = bank_headers(IMAGES['playable_ee'][0])
    rng = random.Random(0x5E1)

    leaves, pcs = check_leaves(rng)
    cases = []
    for label in IMAGES:
        cases += captured_cases(label, IMAGES[label][0], rng)
    all_random = random_cases(random.Random(0x5E2), reference_mode.pick(700, 70))
    cases += reference_mode.select(all_random, 28, 0x5E3, axes=(lambda c: c['label'][1],))
    all_synthetic = synthetic_cases(random.Random(0x5E5), reference_mode.pick(400, 16))
    cases += all_synthetic
    all_ledge = ledge_cases(random.Random(0x5E4), reference_mode.pick(1500, 60),
                            HEADINGS + (HEADINGS_FULL if reference_mode.FULL else ()))
    cases += all_ledge
    results = reference_mode.parallel_map(run_case_safe, cases, cost=lambda c: len(c['ops']))
    failures = [r[1] for r in results if r[0] == 'fail']
    for f in failures[:10]:
        print('FAIL', f)
    assert not failures, f'{len(failures)} of {len(cases)} cases differ'
    for r in results:
        pcs |= r[1]
    adapters = check_adapters()
    fails = check_fail_stop()
    views = check_views()
    missing = check_coverage(pcs)
    assert not missing, ('original instructions never executed', missing)

    captures = []
    for label, (ram, spad) in IMAGES.items():
        result = capture_check(label, ram, spad)
        if result is not None:
            captures.append((label, result))
    exact = [label for label, (_, worst) in captures if not worst]
    for label, (kind, worst) in captures:
        if worst:
            print(f'  capture {label}: {kind} differs from the captured world matrices at nodes {worst}')
    assert captures, 'capture re-evaluation checked no image (every one skipped)'
    assert len(exact) == len(captures), ('capture re-evaluation differs', len(captures) - len(exact))
    reference_mode.banner(f'{leaves:,} leaf comparisons',
                          f'{len(cases):,} cases over {len(IMAGES)} captured images '
                          f'({sum(len(c["ops"]) for c in cases):,} original calls)',
                          f'{adapters} adapter, {views} view and {fails} fail-stop checks',
                          f'capture re-evaluation exact on {len(exact)} of {len(captures)} images')
    print(f'pose host workers: original-instruction reference PASSED ({time.time() - started:.1f} s)')


if __name__ == '__main__':
    main()
