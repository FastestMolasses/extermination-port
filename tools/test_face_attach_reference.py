#!/usr/bin/env python3
"""The +0x90 attachment draw (src/game/em_face_attach.c: 001CB3C0, 001D3F50,
001D3E40, vif_append_ref_tag, 001D2910(0), and the 001D88B0 adapter over
em_frh_001D88B0 + em_actor_light) against the ORIGINAL instructions.
docs/FACE_ATTACH.md.

The user's pinned ELF and the captured AREA11 EE RAM + scratchpad images
supply every instruction and every input; none are embedded here.

Oracle. The shared EE of test_anim_runtime_rest_reference.py (the
test_player_slide core with the measured COP1 / VU0 float model, plus the
MMI word interleaves PEXTLW / PEXTUW that 001D8340's 00102798 executes)
runs the ORIGINAL 001CB3C0 over the captured RAM with the draw walk's
current set (D_00275B48 / 44 = owner, D_00275B40 = owner + 0x110, as
001CB590 publishes it), the channel-0 cursor moved to a patterned window.
Nothing is hooked away: pass-through recorders at every callee the native
side reaches through a boundary (001029C0, 001026D0, 001C7900 > 001D88B0 >
001D8130 / 001D8340 / 001D8690, 001CB2C0, 001D1F80, 001D3F50 > 001D3E40 >
vif_append_ref_tag / 001D2910) record the argument registers and the
storage at the entry (the display-list window, the channel cursor, context
+0x50, SPR 0x70003400..0x700034BF, the rig record D_00817BC0, D_00275688
and 001CB3C0's stack matrix), then run the original body.

Native. em_face_attach_001CB3C0 over a copy of the same image (the EE
reads through one region over the RAM copy; the channel, scratch, context
and rig views point into it), with em_anim_rest_001C7900 / _001CB2C0,
em_face_attach_w_001D88B0 -> em_frh_001D88B0 -> em_actor_light, and
w_001D1F80 = em_load_veil_particles_001D1F80. Its trace hook snapshots the
same storage at each callee entry.

Compared: the callee sequence with every argument the native knows (stack
pointers excluded), the fixed storage subset above at every callee entry
(not the whole RAM), then the whole
32 MB RAM and the scratchpad after the call, byte for byte.

Face units. The unit is looked up in the captured display list (the
original's own draw of that frame). Where the snapshot's D_00810610 already
holds the next frame's view, the case is rebuilt with the view the frame was
drawn with (the 16 words at 0x00814040) and must then equal the captured
unit in every byte. A sample runs through
em_object_unit_parse + em_object_unit_run against the ORIGINAL VU1 face
program (test_object_unit_reference's oracle): every triangle equal.

Cases: every owner with draw method 001CAA00 and +0x90 != 0 in the route
beats 00..14, the opening, the Roger encounter and the fence-door capture
(Roger, the player's Dennis face, the opening's second Roger actor);
synthetic variants (lighting modes 1..7 and -1, the REF 2 path, the 0x0F00
rig key, skin slot, other nodes, random matrices, offsets, colours and
weights, an unaligned attachment); fail-stop checks.

EM_TEST_FULL=1 runs every synthetic case and every face unit through VU1.
No original instruction bytes, disassembly or data are written by this
file; the report holds counts and hashes only.
"""
import ctypes as C
import hashlib
import json
import os
import random
import struct
import subprocess
import sys
import time
from pathlib import Path

os.environ.setdefault('EM_TEST_JOBS', '4')

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))

import reference_mode as RM  # noqa: E402
import test_anim_runtime_rest_reference as ARR  # noqa: E402
import test_actor_light_001d89d0_reference as TAL  # noqa: E402
import test_owner_draw_reference as TOD  # noqa: E402
import test_owner_services_reference as OSR  # noqa: E402
import test_player_slide_reference as SR  # noqa: E402
import test_pose_host_workers_reference as PH  # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
OUT = ROOT / 'build/b15/face_attach'
RAM_SIZE = 0x2000000
SPR = 0x70000000
D_CTX, D_B40, D_B44, D_B48 = 0x275670, 0x275B40, 0x275B44, 0x275B48
D_FB0, D_RIG, D_RIGPTR, D_ARENA = 0x250FB0, 0x817BC0, 0x275688, 0x275674
PLAYER = 0x8102B0
DRAW = 0x1CAA00
SCRATCH_RAM = 0x01F00000             # zero in every image (checked)
DL, DL_SIZE = SCRATCH_RAM + 0x1000, 0x400
ALT_SLOT = SCRATCH_RAM + 0x2000      # the unaligned-attachment variant's slot copy
DRAWN_VIEW = 0x814040                # the view the captured frame was drawn with (see the check)
FACES = {0x018C8740: 'Roger', 0x011749C0: 'Dennis', 0x01955140: 'AREA01 NPC'}

U8, U32, I32 = C.c_uint8, C.c_uint32, C.c_int32
PU32 = C.POINTER(U32)

# Callee -> the caller whose call is recorded (the translated boundary).
TOP = 0x1CB3C0
PARENT = {0x1029C0: TOP, 0x1026D0: TOP, 0x1C7900: TOP, 0x1CB2C0: TOP, 0x1D1F80: TOP, 0x1D3F50: TOP,
          0x1D88B0: 0x1C7900, 0x1D8130: 0x1D88B0, 0x1D8340: 0x1D88B0, 0x1D8690: 0x1D88B0,
          0x1D3E40: 0x1D3F50, 0x1D2090: 0x1D3E40, 0x1D2910: 0x1D3E40}
# Argument registers the native knows (a0..a3, t0 = index 4); stack pointers excluded.
ARGS = {0x1029C0: (), 0x1026D0: (), 0x1C7900: (1, 2, 3), 0x1D88B0: (1, 2, 3), 0x1D8130: (0,),
        0x1D8340: (0, 1, 2, 3), 0x1D8690: (0, 1, 2, 3), 0x1CB2C0: (0, 1, 2),
        0x1D1F80: (0, 1, 2), 0x1D3F50: (0,), 0x1D3E40: (0, 1), 0x1D2090: (0, 1), 0x1D2910: (0,)}
CHECKED_CODE = ('func_001CB3C0', 'func_001029C0', 'func_001026D0', 'func_001C7900', 'func_001D88B0',
                'func_001D8130', 'func_001D8340', 'func_001D8690', 'func_001D8C30', 'func_001CB2C0',
                'func_00102948', 'func_001D1F80', 'func_001D3F50', 'func_001D3E40', 'vif_append_ref_tag',
                'func_001D2910', 'func_001D2710', 'func_001D7B30', 'func_00102798')

ELF = LIB = None
IMAGES = {}


def u32(b, a): return struct.unpack_from('<I', b, a)[0]


# ------------------------------------------------------------------ native types

class FaceWorld(C.Structure):
    _fields_ = [('anim', C.POINTER(ARR.Rest)), ('draw', C.POINTER(TOD.DrawWorld)),
                ('light', C.POINTER(TAL.Light)), ('context_address', U32), ('d00250FB0', PU32),
                ('d00275B40', C.POINTER(C.POINTER(OSR.Bone))), ('d00275B40_count', U32)]


VEIL_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, I32, I32, I32)
TRACE_FN = C.CFUNCTYPE(None, C.c_void_p, U32, PU32, PU32)


class FaceWorkers(C.Structure):
    _fields_ = [('ctx', C.c_void_p), ('w_001D1F80', VEIL_FN), ('trace', TRACE_FN)]


class Face(C.Structure):
    _fields_ = [('world', FaceWorld), ('workers', FaceWorkers), ('fault', ARR.Fault), ('frame', C.c_void_p)]


SOURCES = ('src/game/em_face_attach.c', 'src/game/em_anim_runtime_rest.c', 'src/game/em_pose_host_workers.c',
           'src/game/em_player_stage_workers.c', 'src/game/em_player_floor.c', 'src/game/em_player_reaction.c',
           'src/game/em_player_fall.c', 'src/game/em_owner_services_original.c',
           'src/game/em_stream_lanes_original.c', 'src/game/em_sdk_math_original.c',
           'src/game/em_actor_light_001D89D0.c', 'src/game/em_owner_draw_original.c',
           'src/game/em_frame_render_heads.c', 'src/game/em_load_veil_particles.c')


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    # EM_FACE_ATTACH_SOURCE: a mutated copy of em_face_attach.c (defect injection).
    sources = list(SOURCES)
    mutated = os.environ.get('EM_FACE_ATTACH_SOURCE')
    if mutated:
        sources[0] = mutated
    lib = OUT / (('face_attach_mut' if mutated else 'face_attach') + ('.dylib' if sys.platform == 'darwin' else '.so'))
    command = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared', '-fPIC',
               '-Isrc', *sources, '-lm', '-o', str(lib)]
    digest = hashlib.sha256(' '.join(command).encode())
    for path in [ROOT / s for s in sources] + sorted((ROOT / 'src/game').glob('*.h')):
        digest.update(path.read_bytes())
    stamp = Path(str(lib) + '.sha256')
    if not (lib.exists() and stamp.exists() and stamp.read_text() == digest.hexdigest()):
        subprocess.run(command, cwd=ROOT, check=True)
        stamp.write_text(digest.hexdigest())
    n = C.CDLL(str(lib))
    PF_ = C.POINTER(Face)
    n.em_face_attach_001CB3C0.argtypes = [PF_, U32]
    n.em_face_attach_001D3F50.argtypes = [PF_, U32]
    n.em_face_attach_001D3E40.argtypes = [PF_, I32, U32]
    n.em_load_veil_particles_001D1F80.argtypes = [C.POINTER(TOD.Veil), I32, I32, I32]
    for f in ('em_face_attach_001CB3C0', 'em_face_attach_001D3F50', 'em_face_attach_001D3E40',
              'em_load_veil_particles_001D1F80'):
        getattr(n, f).restype = C.c_int
    return n


# ------------------------------------------------------------------ images and cases

def image_list():
    ref, route = DECOMP / 'build/startup-reference', DECOMP / 'build/s87/route'
    out = []
    for beat in TOD.BEATS:
        out.append((beat, route / beat / 'eeMemory.bin', route / beat / 'scratchpad.bin'))
    out.append(('opening', ref / 'opening_ee.bin', ref / 'opening_scratchpad.bin'))
    out.append(('roger_encounter', ref / 'roger-encounter/eeMemory.bin', ref / 'roger-encounter/scratchpad.bin'))
    door = DECOMP / 'build/s87/c7cap/door1/c7_door1_fence_door_side1'
    out.append(('c7_door1', door / 'eeMemory.bin', door / 'scratchpad.bin'))
    return [x for x in out if x[1].exists() and x[2].exists()]


def attached_owners(ram):
    """Every owner (the walk list and the player record) whose draw method
    is 001CAA00 and whose +0x90 is nonzero."""
    out, a, seen = [], u32(ram, 0x275BC0), set()
    while a and a not in seen and a < RAM_SIZE:
        seen.add(a)
        out.append(a)
        a = u32(ram, a + 0x1C)
    out.append(PLAYER)
    return [o for o in out if u32(ram, o + 0x4C) == DRAW and u32(ram, o + 0x90)]


def base_patch(ram):
    """The channel-0 cursor at the patterned window, D_00275B48 / 44 / 40 as
    001CB590 leaves them for the owner (added per case)."""
    ctx = u32(ram, D_CTX)
    return [(DL, bytes((i * 29 + 7) & 0xFF for i in range(DL_SIZE))),
            (ctx + 0x10, struct.pack('<I', DL))]


def owner_patch(owner):
    return [(D_B48, struct.pack('<I', owner)), (D_B44, struct.pack('<I', owner)),
            (D_B40, struct.pack('<I', owner + 0x110))]


def rnd_word(rng):
    k = rng.random()
    if k < 0.08:
        return rng.choice([0, 0x80000000, 1, 0x807FFFFF, 0x00800000, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000,
                           0x3F800000, 0xBF800000])
    return struct.unpack('<I', struct.pack('<f', rng.uniform(-400.0, 400.0)))[0]


def synthetic_cases(rng):
    """Variants on Roger in route beat 14 and the player in the Roger
    encounter capture (both faces draw there), each changing one input
    class."""
    bases = [(b, o) for b, o in (('14_roger_encounter', 0x7A8830), ('roger_encounter', PLAYER))
             if b in IMAGES and o in attached_owners(IMAGES[b][0])]
    out = []
    for base, owner in bases:
        ram = IMAGES[base][0]
        ctx = u32(ram, D_CTX)
        count = ram[owner + 0x0C]
        slot = u32(ram, owner + 0x90)
        for mode in (1, 2, 3, 4, 5, 6, 7, 0xFFFFFFFF):
            out.append(dict(image=base, owner=owner, label=f'mode {mode:#x}',
                            patch=[(ctx + 0x246C, struct.pack('<I', mode))]))
        c0c = u32(ram, ctx + 0x0C)
        out.append(dict(image=base, owner=owner, label='REF 2 (context +0x0C bit 0 clear)',
                        patch=[(ctx + 0x0C, struct.pack('<I', c0c & ~1))]))
        out.append(dict(image=base, owner=owner, label='rig key 0x0F00 (bit 8)',
                        patch=[(ctx + 0x0C, struct.pack('<I', c0c | 0x100))]))
        out.append(dict(image=base, owner=owner, label='skin slot 1', patch=[(ctx + 0x9C, struct.pack('<I', 1))]))
        for bone in (0, count - 1):
            out.append(dict(image=base, owner=owner, label=f'node {bone}',
                            patch=[(owner + 0x94, struct.pack('<h', bone))]))
        out.append(dict(image=base, owner=owner, label='unaligned attachment (+4)',
                        patch=[(ALT_SLOT, ram[slot:slot + 0x70]),
                               (ALT_SLOT + 0x64, ram[slot + 0x60:slot + 0x64]),
                               (owner + 0x90, struct.pack('<I', ALT_SLOT + 4))]))
        out.append(dict(image=base, owner=owner, label='face +0x04 high half set',
                        patch=[(u32(ram, slot + 0x60) + 4,
                                struct.pack('<I', 0x12340000 | (u32(ram, u32(ram, slot + 0x60) + 4) & 0xFFFF)))]))
        for k in range(RM.pick(60, 6)):
            node = u32(ram, owner + 0x110 + 4 * struct.unpack_from('<h', ram, owner + 0x94)[0])
            which = k % 4
            if which == 0:
                patch = [(node + 0x90, struct.pack('<16I', *[rnd_word(rng) for _ in range(16)]))]
                label = 'random node matrix'
            elif which == 1:
                patch = [(D_FB0, struct.pack('<3I', *[rnd_word(rng) for _ in range(3)]))]
                label = 'random D_00250FB0 offset'
            elif which == 2:
                patch = [(owner + 0x80, struct.pack('<4I', *[rnd_word(rng) for _ in range(4)]))]
                label = 'random owner +0x80 colour'
            else:
                patch = [(slot + 0x40, struct.pack('<8I', *[rnd_word(rng) for _ in range(8)]))]
                label = 'random weights'
            out.append(dict(image=base, owner=owner, label=label, patch=patch))
    return out


def apply(case):
    ram, spr = IMAGES[case['image']]
    ram, spr = bytearray(ram), bytearray(spr)
    for at, data in base_patch(ram) + owner_patch(case['owner']) + case.get('patch', []):
        if at >= SPR:
            spr[at - SPR:at - SPR + len(data)] = data
        else:
            ram[at:at + len(data)] = data
    return ram, spr


# ------------------------------------------------------------------ the two sides

def state_bytes(read, ctx):
    return b''.join((read(DL, DL_SIZE), read(ctx + 0x10, 4), read(ctx + 0x50, 16), read(SPR + 0x3400, 0xC0),
                     read(D_RIG, 0x130), read(D_RIGPTR, 4)))


def original(ram, spr, owner):
    ee = ARR.EE(ELF, ram=ram, spad=spr)
    ctx = u32(ram, D_CTX)
    log, stack, frame = [], [], {}

    def make(entry):
        def hook(e):
            parent = stack[-1] if stack else TOP
            if PARENT.get(entry) == parent:
                if parent == TOP and 'at' not in frame:
                    frame['at'] = (e.r[29] & 0xFFFFFFFF) + 0x30
                regs = [e.arg(0), e.arg(1), e.arg(2), e.arg(3), e.r[8] & 0xFFFFFFFF]
                log.append((entry, tuple(regs[i] for i in ARGS[entry]), state_bytes(e.read, ctx),
                            e.read(frame['at'], 64)))
            stack.append(entry)
            del e.hooks[entry]
            ra = e.r[31]
            e.r[31] = SR.RETURN
            e.run(entry)
            e.r[31] = ra
            e.hooks[entry] = hook
            stack.pop()
        return hook

    for entry in PARENT:
        ee.hooks[entry] = make(entry)
    ee.invoke(TOP, (owner,))
    return ee, log


class Native:
    """em_face_attach over a copy of one memory image."""

    def __init__(self, ram, spr, owner):
        self.ram, self.spr = bytearray(ram), bytearray(spr)
        self.rbuf = (U8 * len(self.ram)).from_buffer(self.ram)
        self.base = C.addressof(self.rbuf)
        self.ctx = u32(self.ram, D_CTX)
        self.scratch = ARR.Scratch()
        for name, at in ARR.SCRATCH_AT:
            C.memmove(getattr(self.scratch, name), bytes(self.spr[at:at + 64]), 64)
        self.channels = (ARR.Channel * 4)()
        self.hi = [0] * 4
        for i in range(4):
            word = u32(self.ram, self.ctx + 0x10 + 4 * i)
            self.hi[i] = word & ~(RAM_SIZE - 1) & 0xFFFFFFFF
            self.channels[i].cursor = self.base + (word & (RAM_SIZE - 1))
            self.channels[i].end = self.base + RAM_SIZE
        ptr = lambda at: C.cast(self.base + at, PU32)
        # 001C7900 / 001CB2C0: the anim rest, one region over the RAM copy.
        r = self.rest = ARR.Rest()
        r.world.region[0] = ARR.Region(0, RAM_SIZE, self.base, 0)
        r.world.region_count = 1
        r.world.channel = C.cast(self.channels, C.POINTER(ARR.Channel))
        r.world.channel_count = 4
        r.world.scratch = C.pointer(self.scratch)
        # 001D3E40's context views.
        d = self.draw = TOD.DrawWorld()
        d.ctx_0C, d.ctx_9C, d.d00275674 = ptr(self.ctx + 0x0C), ptr(self.ctx + 0x9C), ptr(D_ARENA)
        d.channel = C.cast(self.channels, C.POINTER(OSR.Channel))
        d.channel_count = 4
        d.ctx_50, d.ctx_50_count = ptr(self.ctx + 0x50), 4
        # 001D88B0's lighting storage.
        l = self.light = TAL.Light()
        w = l.world
        w.d00275688, w.d00817BC0 = ptr(D_RIGPTR), ptr(D_RIG)
        w.ctx_246C = C.cast(self.base + self.ctx + 0x246C, C.POINTER(C.c_int32))
        w.ctx_000C, w.ctx_0220, w.ctx_2380 = ptr(self.ctx + 0x0C), ptr(self.ctx + 0x220), ptr(self.ctx + 0x2380)
        w.d00810700 = C.cast(self.base + 0x810700, C.POINTER(C.c_uint8))
        w.d00251C50, w.d00253170, w.d00810610 = ptr(0x251C50), ptr(0x253170), ptr(0x810610)
        # D_00275B40: the owner's +0x110 node slots as EmOwnerBone.
        count = self.ram[owner + 0x0C]
        # One entry past the count is a NON-NULL guard: an index bound off by
        # one (bone > count) reads it and runs instead of faulting, so the
        # bone_high fail-stop pins the bound itself, not a NULL past the end.
        self.bones = (OSR.Bone * (count + 1))()
        self.bone_ptr = (C.POINTER(OSR.Bone) * (count + 1))()
        for i in range(count):
            node = u32(self.ram, owner + 0x110 + 4 * i)
            if node:
                C.memmove(C.addressof(self.bones[i].world), bytes(self.ram[node + 0x90:node + 0xD0]), 64)
                self.bone_ptr[i] = C.pointer(self.bones[i])
        self.bones[count] = self.bones[0] if count else OSR.Bone()
        self.bone_ptr[count] = C.pointer(self.bones[count])
        # 001D1F80: the veil module over the RAM copy.
        v = self.veil = TOD.Veil()
        v.world.cursor, v.world.cursor_count = ptr(self.ctx + 0x10), 4
        v.world.ctx_9C, v.world.d00275674 = ptr(self.ctx + 0x9C), ptr(D_ARENA)
        v.world.packet, v.world.packet_address, v.world.packet_size = self.base, 0, RAM_SIZE
        f = self.face = Face()
        f.world.anim, f.world.draw, f.world.light = C.pointer(r), C.pointer(d), C.pointer(l)
        f.world.context_address = self.ctx
        f.world.d00250FB0 = ptr(D_FB0)
        f.world.d00275B40 = C.cast(self.bone_ptr, C.POINTER(C.POINTER(OSR.Bone)))
        f.world.d00275B40_count = count
        self.log = []
        self.keep = [VEIL_FN(self.w_1f80), TRACE_FN(self.trace)]
        f.workers.w_001D1F80, f.workers.trace = self.keep
        r.workers.ctx = C.addressof(f)
        r.workers.w_001D88B0 = ARR.LIGHT_FN(C.cast(LIB.em_face_attach_w_001D88B0, C.c_void_p).value)

    def sync_out(self):
        for name, at in ARR.SCRATCH_AT:
            self.spr[at:at + 64] = bytes(getattr(self.scratch, name))
        for i in range(4):
            struct.pack_into('<I', self.ram, self.ctx + 0x10 + 4 * i,
                             (self.channels[i].cursor - self.base) | self.hi[i])

    def read(self, at, n):
        if at >= SPR:
            return bytes(self.spr[at - SPR:at - SPR + n])
        return bytes(self.ram[at:at + n])

    def trace(self, _, callee, args, frame):
        self.sync_out()
        regs = [args[i] for i in range(5)]
        self.log.append((callee, tuple(regs[i] for i in ARGS[callee]), state_bytes(self.read, self.ctx),
                         bytes(C.string_at(frame, 64))))

    def w_1f80(self, _, a0, a1, a2):
        struct.pack_into('<I', self.ram, self.ctx + 0x10, self.channels[0].cursor - self.base)
        rc = LIB.em_load_veil_particles_001D1F80(C.byref(self.veil), a0, a1, a2)
        self.channels[0].cursor = self.base + u32(self.ram, self.ctx + 0x10)
        return rc

    def run(self, owner):
        rc = LIB.em_face_attach_001CB3C0(C.byref(self.face), owner)
        self.sync_out()
        return rc


# ------------------------------------------------------------------ one case

def first_diff(a, b, base=0):
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            return hex(base + i), a[i], b[i]
    return None


def run_case(case):
    ram, spr = apply(case)
    owner = case['owner']
    ee, olog = original(ram, spr, owner)
    nat = Native(ram, spr, owner)
    rc = nat.run(owner)
    label = (case['image'], hex(owner), case.get('label', 'captured'))
    assert rc == 0 and nat.face.fault.code == 0, ('native fault', label, hex(nat.face.fault.address),
                                                  nat.face.fault.code, hex(nat.rest.fault.address),
                                                  nat.rest.fault.code, nat.light.fault.code)
    assert [e[0] for e in olog] == [e[0] for e in nat.log], \
        ('callees', label, [hex(e[0]) for e in olog], [hex(e[0]) for e in nat.log])
    for k, (o, n) in enumerate(zip(olog, nat.log)):
        assert o[1] == n[1], ('arguments', label, hex(o[0]), [hex(x) for x in o[1]], [hex(x) for x in n[1]])
        assert o[2] == n[2], ('storage at the entry of', label, hex(o[0]), first_diff(o[2], n[2]))
        if k and o[3] != n[3]:
            raise AssertionError(('001CB3C0 matrix at the entry of', label, hex(o[0]), first_diff(o[3], n[3])))
    if ee.mem != nat.ram:
        raise AssertionError(('RAM after', label, first_diff(ee.mem, nat.ram)))
    if ee.spad != nat.spr:
        raise AssertionError(('scratchpad after', label, first_diff(ee.spad, nat.spr, SPR)))
    ctx = u32(ram, D_CTX)
    used = u32(nat.ram, ctx + 0x10) - DL
    unit = bytes(nat.ram[DL:DL + used])
    face = u32(ram, u32(ram, owner + 0x90) + 0x60)
    kinds = [olog_entry[0] for olog_entry in olog]
    return dict(image=case['image'], owner=hex(owner), label=case.get('label', 'captured'), bytes=used,
                face=FACES.get(face, hex(face)), calls=len(olog), mode=u32(ram, ctx + 0x246C),
                ref2=not (u32(ram, ctx + 0x0C) & 1), sha=hashlib.sha256(unit).hexdigest()[:16],
                pcs=sorted(ee.pcs), unit=unit, callees=kinds)


def run_case_safe(case):
    try:
        return ('ok', run_case(case))
    except AssertionError as error:
        return ('fail', str(error)[:3000])


# ------------------------------------------------------------------ captured list and VU1

FILL_LANES = {o + k for o in (0xC0, 0xD0, 0xE0, 0xF0) for k in range(4)}   # lighting rows, lane x


def captured_faces(ram, face):
    """Starts of the intact face units for `face` in the captured arena (a
    CALL 0x0023C480 tag 0x160 bytes into the unit, its model REF naming
    face + 0x40)."""
    out, at, needle = [], 0x28F700, struct.pack('<I', 0x23C480)
    while True:
        i = ram.find(needle, at, 0x7635C0)
        if i < 0:
            return out
        at = i + 1
        p = i - 4
        if p % 16 == 0 and p >= 0x160 and u32(ram, p + 0x24) == face + 0x40:
            out.append(p - 0x160)


def in_captured_list(image, owner, unit):
    """'exact' when a face unit of the captured list equals the rebuilt one
    on every byte the builders write (tag bytes +2 and +8..+0xF are never
    written); 'fill' when one equals it on every such byte except lane x of
    the four lighting rows (the camera-fill slot 0 direction, whose input at
    the draw differs from the snapshot's: docs/FACE_ATTACH.md section 5);
    'absent' otherwise."""
    ram = IMAGES[image][0]
    skip = TOD.tag_bytes(unit)
    face = u32(ram, u32(ram, owner + 0x90) + 0x60)
    best = 'absent'
    for start in captured_faces(ram, face):
        cand = ram[start:start + len(unit)]
        diff = {k for k in range(len(unit)) if k not in skip and cand[k] != unit[k]}
        if not diff:
            return 'exact', hex(start)
        if diff <= FILL_LANES:
            best = 'fill', hex(start)
    return best if best != 'absent' else ('absent', None)


def vu1_case(item):
    """The native unit through em_object_unit_parse + em_object_unit_run
    against the ORIGINAL face program (test_object_unit_reference)."""
    import test_object_unit_reference as TOU
    image, unit = item
    img = bytearray(IMAGES[image][0])
    img[DL:DL + len(unit)] = unit
    img = bytes(img)
    want, blocks = TOU.oracle(img, DL, len(unit))
    n, out, _objs, pieces, why = TOU.native(img, unit)
    assert n >= 0, ('em_object_unit refused the face unit', image, n, why)
    got = TOU.ntris(out, n)
    if got != want:
        k = next((i for i, (a, b) in enumerate(zip(got, want)) if a != b), min(len(got), len(want)))
        raise AssertionError(('face triangles differ', image, len(got), len(want), k))
    assert pieces[1] == blocks[TOU.FACE_KERNEL] and pieces[0] == 1, ('face pieces', image)
    return len(want)


# ------------------------------------------------------------------ fail-stop

def check_fail_stop():
    """Each broken input: -1, a latched fault, nothing written (a view or
    room fault), and a second call blocked."""
    image = 'roger_encounter' if 'roger_encounter' in IMAGES else next(iter(IMAGES))
    ram0 = IMAGES[image][0]
    owner = attached_owners(ram0)[0]
    case = dict(image=image, owner=owner)
    checks = 0
    last_fault_address = [0]

    def attempt(mutate, partial=False, code=None):
        nonlocal checks
        ram, spr = apply(case)
        nat = Native(ram, spr, owner)
        mutate(nat)
        before_ram, before_spr = bytes(nat.ram), bytes(nat.spr)
        rc = nat.run(owner)
        assert rc == -1 and nat.face.fault.code != 0, ('fail-stop not taken', mutate.__name__)
        if code is not None:
            assert nat.face.fault.code == code, (mutate.__name__, nat.face.fault.code)
        if not partial:
            assert bytes(nat.ram) == before_ram and bytes(nat.spr) == before_spr, ('wrote before faulting',
                                                                                  mutate.__name__)
        assert LIB.em_face_attach_001CB3C0(C.byref(nat.face), owner) == -1, ('second call ran', mutate.__name__)
        last_fault_address[0] = nat.face.fault.address
        checks += 1

    def no_anim(n): n.face.world.anim = C.POINTER(ARR.Rest)()
    def no_draw(n): n.face.world.draw = C.POINTER(TOD.DrawWorld)()
    def other_channel(n):
        n.other = (OSR.Channel * 4)()
        n.draw.channel = C.cast(n.other, C.POINTER(OSR.Channel))
    def no_light(n): n.face.world.light = C.POINTER(TAL.Light)()
    def no_rig(n): n.light.world.d00817BC0 = PU32()
    def no_view(n): n.light.world.d00810610 = PU32()
    def no_offset(n): n.face.world.d00250FB0 = PU32()
    def no_nodes(n): n.face.world.d00275B40 = C.POINTER(C.POINTER(OSR.Bone))()
    def no_1f80(n): n.face.workers.w_001D1F80 = VEIL_FN()
    def unbound_light(n): n.rest.workers.w_001D88B0 = ARR.LIGHT_FN()
    def no_arena(n): n.draw.d00275674 = PU32()
    def bone_high(n):
        # +0x94 = count: the slot past the end is the non-NULL guard, so only
        # the index bound itself can refuse it.
        assert n.bone_ptr[n.face.world.d00275B40_count], 'guard slot missing'
        struct.pack_into('<h', n.ram, owner + 0x94, n.face.world.d00275B40_count)
    def bone_negative(n): struct.pack_into('<h', n.ram, owner + 0x94, -1)
    def bone_null(n):
        b = struct.unpack_from('<h', n.ram, owner + 0x94)[0]
        n.bone_ptr[b] = C.POINTER(OSR.Bone)()
    def unmapped_owner(n): n.rest.world.region[0].size = owner + 0x90
    def unmapped_slot(n): struct.pack_into('<I', n.ram, owner + 0x90, 0x7FFFFF00)
    def unmapped_face(n):
        slot = u32(n.ram, owner + 0x90)
        struct.pack_into('<I', n.ram, slot + 0x60, 0x7FFFFF00)
    def short_room(n): n.channels[0].end = n.channels[0].cursor + 0x18F
    def short_room_ref2(n):
        struct.pack_into('<I', n.ram, n.ctx + 0x0C, u32(n.ram, n.ctx + 0x0C) & ~1)
        n.channels[0].end = n.channels[0].cursor + 0x19F
    def latched(n): n.face.fault.code, n.face.fault.address = 1, 0x1234

    for m in (no_anim, no_draw, other_channel, no_light, no_rig, no_view, no_offset, no_nodes, no_1f80,
              unbound_light, no_arena, bone_high, bone_negative, bone_null, unmapped_owner, unmapped_slot,
              unmapped_face, short_room, short_room_ref2, latched):
        attempt(m)
    # The node-index bound, pinned by code and address (BAD_INDEX at D_00275B40).
    attempt(bone_high, code=4)
    assert last_fault_address[0] == 0x00275B40, ('bone_high fault address', hex(last_fault_address[0]))

    # A worker that fails after the uploads: -1, WORKER at 001D1F80, the
    # writes before it stay (as the original's would).
    def failing_1f80(n):
        n.keep.append(VEIL_FN(lambda *_: -1))
        n.face.workers.w_001D1F80 = n.keep[-1]
    attempt(failing_1f80, partial=True, code=2)
    # An exact-fit room (no REF 2): runs.
    ram, spr = apply(case)
    nat = Native(ram, spr, owner)
    nat.channels[0].end = nat.channels[0].cursor + 0x190
    assert nat.run(owner) == 0, 'exact room refused'
    return checks + 1


# ------------------------------------------------------------------ main

def main():
    global ELF, LIB
    started = time.time()
    ELF = SR.read_elf()
    PH.ELF = ARR.ELF = ELF
    LIB = build_native()
    ARR.LIB = LIB
    table = PH.function_table()
    for label, path, spr_path in image_list():
        ram = path.read_bytes()
        if ram[0x810700] != 11:
            continue
        assert ram[SCRATCH_RAM:SCRATCH_RAM + 0x3000] == bytes(0x3000), ('scratch RAM not zero', label)
        for name in CHECKED_CODE:
            start, size = table[name]
            assert ram[start:start + size] == ELF[start - 0x100000 + 0x300:start - 0x100000 + 0x300 + size], \
                ('captured code differs from the ELF', label, name)
        assert ram[D_FB0:D_FB0 + 12] == ELF[D_FB0 - 0x100000 + 0x300:D_FB0 - 0x100000 + 0x300 + 12], \
            ('D_00250FB0 differs from the ELF', label)
        IMAGES[label] = (ram, spr_path.read_bytes())
    assert IMAGES, 'no captured AREA11 images'

    captured = [dict(image=label, owner=o) for label in IMAGES for o in attached_owners(IMAGES[label][0])]
    synthetic = synthetic_cases(random.Random(0xFACE))
    chosen = RM.select(synthetic, 30, 0xFA0E, axes=(lambda c: c['label'], lambda c: c['owner']))
    results = RM.parallel_map(run_case_safe, captured + chosen)
    fails = [r[1] for r in results if r[0] == 'fail']
    for f in fails[:8]:
        print('FAIL', f)
    assert not fails, f'{len(fails)} of {len(results)} cases differ'
    rows = [r[1] for r in results]
    cap_rows, syn_rows = rows[:len(captured)], rows[len(captured):]

    # Coverage of the translated functions' original instructions.
    pcs = set()
    for r in rows:
        pcs |= set(r.pop('pcs'))
    missing = {}
    for name in ('func_001CB3C0', 'func_001D3F50', 'func_001D3E40', 'vif_append_ref_tag', 'func_001D88B0'):
        start, size = table[name]
        # a jump counts through its delay slot (the interpreter records the slot)
        miss = [a for a in sorted(PH.reachable(start, size)) if a not in pcs and not (
            (PH.word_at(a) >> 26 in (2, 3) or (PH.word_at(a) >> 26 == 0 and PH.word_at(a) & 63 in (8, 9)))
            and a + 4 in pcs)]
        if miss:
            missing[name] = [hex(a) for a in miss]
    # 001D88B0's own branch tests are all taken both ways by the mode cases;
    # nothing in the translated five may stay unexecuted.
    assert not missing, ('original instructions never executed', missing)

    # The captured display lists: the unit each captured case builds, found
    # in that frame's list where the original drew it.
    listed = {'exact': 0, 'fill': 0, 'absent': 0}
    for r in cap_rows:
        owner = int(r['owner'], 16)
        kind, at = in_captured_list(r['image'], owner, r['unit'])
        r['captured_list'] = [kind, at]
        listed[kind] += 1
        # An owner its behaviour drew this frame (+0x01 set) must have its
        # unit in the list; an undrawn one (the opening's first Roger) need not.
        if IMAGES[r['image']][0][owner + 1]:
            assert kind != 'absent', ('a drawn owner\'s face unit is not in the captured list', r['image'], r['owner'])
    assert listed['exact'], 'no captured face unit equals a rebuilt one exactly'
    # The 'fill' frames: the snapshot's D_00810610 already holds the next
    # frame's view. The 16 words at 0x00814040 hold the view the frame was
    # drawn with (equal to D_00810610 in every 'exact' frame); with them as
    # D_00810610 the original and the native rebuild the captured unit in
    # every byte.
    redo = [dict(image=r['image'], owner=int(r['owner'], 16), label='draw-time view (0x00814040)',
                 patch=[(0x810610, IMAGES[r['image']][0][DRAWN_VIEW:DRAWN_VIEW + 64])])
            for r in cap_rows if r['captured_list'][0] == 'fill']
    for r in cap_rows:
        if r['captured_list'][0] == 'exact':
            ram = IMAGES[r['image']][0]
            assert ram[DRAWN_VIEW:DRAWN_VIEW + 64] == ram[0x810610:0x810650], ('draw-time view', r['image'])
    redone = RM.parallel_map(run_case_safe, redo)
    for case, (status, row) in zip(redo, redone):
        assert status == 'ok', row
        kind, at = in_captured_list(case['image'], case['owner'], row['unit'])
        assert kind == 'exact', ('the draw-time view does not rebuild the captured unit', case['image'], kind)
        row.pop('pcs'); row.pop('unit')
    listed['exact_with_draw_time_view'] = len(redone)

    # VU1: the native units through em_object_unit against the original face program.
    import test_object_unit_reference as TOU
    import test_vu1_object_clip_reference as VC
    TOU.ELF = VC.ELF = TOD.ELF = ELF
    TOU.OUT = OUT / 'object_unit'                   # the shim builds privately
    TOU.LIB = TOU.build_library()
    # (the random-word variants are left out: their special words, e.g. an
    # exponent-255 node lane, are refused by the face program's fail-stop,
    # a renderer contract outside this lane)
    distinct, seen = [], set()
    for r in cap_rows + [r for r in syn_rows if not r['label'].startswith('random')]:
        if r['sha'] not in seen:
            seen.add(r['sha'])
            distinct.append(r)
    # quick: Roger where his face draws (beat 14), the player's Dennis face
    # (the Roger encounter), and a REF 2 unit
    pick = [r for r in distinct if (r['image'], r['owner']) == ('14_roger_encounter', hex(0x7A8830))
            and r['label'] == 'captured'][:1]
    pick += [r for r in distinct if (r['image'], r['owner']) == ('roger_encounter', hex(PLAYER))
             and r['label'] == 'captured'][:1]
    pick += [r for r in distinct if r['ref2']][:1]
    vu_rows = distinct if RM.FULL else pick
    tri = RM.parallel_map(vu1_case, [(r['image'], r['unit']) for r in vu_rows])
    for face in ('Roger', 'Dennis'):
        assert any(t for r, t in zip(vu_rows, tri) if r['face'] == face), ('no drawing unit through VU1', face)
    for r, t in zip(vu_rows, tri):
        r['triangles'] = t
    fails_stop = check_fail_stop()

    for r in rows:
        r.pop('unit')
    by_face = {}
    for r in cap_rows:
        by_face[r['face']] = by_face.get(r['face'], 0) + 1
    line = RM.banner(f'{len(cap_rows)} captured owner draws over {len(IMAGES)} images ({by_face})',
                     RM.part(len(chosen), len(synthetic), 'synthetic cases'),
                     f'{sum(r["calls"] for r in rows)} callee entries compared',
                     f"captured face units: {listed['exact']} rebuilt exactly, {listed['fill']} more exactly "
                     f"with the draw-time view, {listed['absent']} not drawn",
                     RM.part(len(vu_rows), len(distinct), 'distinct units through VU1') +
                     f' ({sum(tri)} triangles)',
                     f'{fails_stop} fail-stop checks')
    report = dict(status='PASS', mode=line, captured=cap_rows, synthetic=[
        {k: r[k] for k in ('label', 'owner', 'bytes', 'mode', 'ref2', 'sha')} for r in syn_rows],
        seconds=round(time.time() - started, 1))
    (OUT / 'report.json').write_text(json.dumps(report, indent=1) + '\n')
    print(f'face attach: original-instruction reference PASSED ({time.time() - started:.1f} s)')


if __name__ == '__main__':
    main()
