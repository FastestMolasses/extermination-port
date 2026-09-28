#!/usr/bin/env python3
"""Run the live render context (src/game/em_render_context_live.c, the one
canonical render context of docs/RENDER_CONTEXT.md section 8) against the
ORIGINAL instructions over captured AREA11 RAM.

The module under test is the exact object the game links: the lane
translations (em_frame_render_heads, em_render_context, em_packet_chain_original,
em_load_veil_particles, em_render_verify_rest, em_actor_light_001D89D0,
em_player_equipment, em_status_ui_leftovers and the SDK leaves) composed
over the module's own storage, with the external views and the host workers
the binder supplies. Per route beat, both sides start from the beat's
snapshot (eeMemory.bin + scratchpad.bin; the native side receives the bytes
of every range it owns through em_rcl_poke and the external ranges as
views), then run the same sequence of original entries:

  001C1DC0 (the area render init: flags, 001C1F50's TEX0 / colour, the area
  fog 001D8FD0 through 001D7B30),
  per frame: 001D1AE0(D_00810E80) (main loop step B), 001D1C50, a burst of
  effect-style fog programmer calls 0021B9A0 (2, 3, then 1), 001D1EA0(1),
  main-loop step V 001D2300 (with D_00810E88 = D_00810E80, the port's field
  model), the flip of D_00810E80, step W 001D2580(field), and a camera move
  (D_00810610 on both sides, so the frame head's one-frame view lag is
  exercised),
  the zoom writers 001D25F0(480), 001D2610(0), 001D2610(1),
  001DD950(&D_008105E0, 2 + 1.02 d, d) (001DD980's store),
  a status frame (D_008106C4 = 1: 001D1C50, 001D2830(3, 1), 001D1EA0(0),
  then step V on its flag-3 / D_008106C4 path and step W),
  an opening-style frame (D_008101E4 = 3: 001DDE10's projected path),
  a tear-down frame (001D1EF0, then step V),
  and step V under render flag 4 set (no 001E0DF0) and flag 0x20 clear.

Step V's 001D21E0 ends in the hardware kick: on the original side the DMA
channel pointer (dmac_channel_base returns D1_CHCR 0x10009000) and the VIF1
register byte 0x10003C20 are a scratch window, and dma_wait_and_submit,
0011B9E0 and the syscall stub 0010BAA0 are recorded; 00101F08's list
argument must equal the native kick's (em_rcl_kick).

After every entry the bytes of every range the module owns (the 5 MB packet
arena and chain table, the render context with its GS blocks and skin
records, the .data blocks and the scratchpad blocks) must equal the
original's RAM, and the ordered log of the recorded callees must match.

Recorded callees (both sides record the call, neither executes it):
  001D7C30  the point-light tick: the live binding runs em_point_light
            (test_point_light_reference) on its own storage;
  001C1EA0  001C1DC0's weather spawn (the scene's pool).
001C1E70's 001D52E0 (the static-object grid header) runs on both sides: the
original over the snapshot's bank, the module's em_swc_001D52E0 over the
exported bank (assets/scene_snow/static_world.emsw, docs/STATIC_WORLD.md).
The bind performs 001AF690's zeroing of D_008101D0..DF; the original side
gets the same zeroing at that point.
sqrtf 0011E748 and tanf 0011E398 run as the ORIGINAL routines in a separate
interpreter on both sides (test_frame_render_heads_reference.Leaf).

Default run: 3 beats (00, 14 and a fixed-seed pick), 3 frames each (about
10 s). EM_TEST_FULL=1: all 15 beats, 6 frames each.
"""
import ctypes as C
import math
import os
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode  # noqa: E402
import test_frame_render_heads_reference as FRH  # noqa: E402
from test_player_slide_reference import RETURN, read_elf, sx32  # noqa: E402

MASK = 0xFFFFFFFF
DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route'
OUT = ROOT / 'build' / 'render_context_live_reference'
EXPORT = ROOT / 'assets/render_context.emrc'

SOURCES = ['em_render_context_live', 'em_gs_blocks_original', 'em_frame_kick', 'em_frame_render_heads', 'em_render_context', 'em_packet_chain_original',
           'em_status_ui_leftovers', 'em_load_veil_particles', 'em_actor_light_001D89D0',
           'em_owner_services_original', 'em_effect_original', 'em_player_equipment',
           'em_player_stage_workers', 'em_render_verify_rest', 'em_sdk_math_original', 'em_sdk_soft_float',
           'em_census_standins', 'em_message_draw_original', 'em_static_world', 'em_static_world_compose',
           'em_owner_draw_original', 'em_camera_commit_original', 'em_stream_lanes_original']
STATIC_WORLD = ROOT / 'assets/scene_snow/static_world.emsw'

OWNED = ((0x28F700, 0x76B5C0 - 0x28F700), (0x811CC0, 0x817240 - 0x811CC0), (0x250F30, 0x2250),
         (0x275670, 0x30), (0x70003A40, 0x100), (0x70003B60, 4), (0x70003B70, 4), (0x241010, 8), (0x26E510, 16),
         (0x26E850, 16), (0x253560, 0x90), (0x817240, 0x80), (0x8101D0, 0x10))
EXTERNAL = ((0x810610, 0x40), (0x8105E0, 0x10), (0x8106B0, 0x48), (0x810700, 3), (0x8101E4, 1),
            (0x70003B8D, 1), (0x8102B0, 0x320), (0x28A5A0, 4))
RECORDED = (0x1D7C30, 0x1C1EA0)
# 001D21E0's hardware callees (the renderer boundary): recorded on the
# original side only; 00101F08(channel, list) is compared with em_rcl_kick.
HARDWARE = {0x101BB8: 'dmac_channel_base', 0x100A60: 'dma_wait_and_submit', 0x11B9E0: '0011B9E0',
            0x10BAA0: '0010BAA0', 0x101F08: '00101F08'}
D1_CHCR, VIF1_BYTE = 0x10009000, 0x10003C20
LEAVES = (0x11E748, 0x11E398)

U32, VP = C.c_uint32, C.c_void_p
F_VOID = C.CFUNCTYPE(C.c_int, VP)
F_LEAF = C.CFUNCTYPE(C.c_int, VP, U32, C.POINTER(U32))
F_BLOCK = C.CFUNCTYPE(C.c_int, VP, U32)


class External(C.Structure):
    _fields_ = [('address', U32), ('size', U32), ('bytes', VP)]


class Workers(C.Structure):
    _fields_ = [('ctx', VP), ('w_001D7C30', F_VOID), ('w_0011E748', F_LEAF), ('w_0011E398', F_LEAF),
                ('w_001C1EA0', F_BLOCK)]


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('rcl.dylib' if sys.platform == 'darwin' else 'rcl.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
                    '-fPIC', '-Isrc'] + ['src/game/%s.c' % s for s in SOURCES] + ['-o', str(lib)],
                   cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    n.em_rcl_init.argtypes = [C.c_char_p, VP]
    n.em_rcl_static_world_load.argtypes = [C.c_char_p]
    n.em_rcl_bind.argtypes = [C.POINTER(External), C.c_uint, C.POINTER(Workers)]
    n.em_rcl_poke.argtypes = [U32, C.c_char_p, U32]
    n.em_rcl_bytes.argtypes = [U32, U32]
    n.em_rcl_bytes.restype = C.POINTER(C.c_uint8)
    n.em_rcl_fault.restype = U32
    n.em_rcl_frame_views.argtypes = [VP, VP]
    n.em_rcl_kick.argtypes = [C.POINTER(U32), C.POINTER(U32)]
    for name, args in (('em_rcl_001D1AE0', [C.c_int32]), ('em_rcl_001D1C50', []), ('em_rcl_001D1EA0', [C.c_int32]),
                       ('em_rcl_001D2300', []), ('em_rcl_001D2580', [C.c_int32]), ('em_rcl_001D1EF0', []),
                       ('em_rcl_001D2830', [C.c_int32, C.c_int32]),
                       ('em_rcl_001C1DC0', []), ('em_rcl_001D25F0', [U32]), ('em_rcl_001D2610', [U32]),
                       ('em_rcl_0021B9A0', [C.c_int32, U32, U32]), ('em_rcl_001DD950', [U32, U32, U32])):
        getattr(n, name).argtypes = args
        getattr(n, name).restype = C.c_int
    return n


def F(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


class KickEE(FRH.FrhEE):
    """FrhEE with 001D21E0's two hardware bytes as a scratch window (the DMA
    channel control and the VIF1 register byte it edits before the kick)."""

    def __init__(self, *args):
        self.hw = {D1_CHCR: bytearray(16), VIF1_BYTE: bytearray(16)}
        super().__init__(*args)

    def _where(self, address):
        a = address & MASK
        for base, buf in self.hw.items():
            if base <= a < base + len(buf):
                return buf, a - base
        return super()._where(address)


class Pair:
    """The original interpreter and the native module over one beat."""

    def __init__(self, beat, first):
        ram = (ROUTE / beat / 'eeMemory.bin').read_bytes()
        spad = (ROUTE / beat / 'scratchpad.bin').read_bytes()
        self.beat = beat
        self.ee = KickEE(ELF, ram, spad)
        self.olog, self.nlog = [], []
        self.kicks = []
        for address, name in HARDWARE.items():
            self.ee.hooks[address] = self._hardware(address)
        for address in RECORDED:
            self.ee.hooks[address] = self._recorder(address)
        for address in LEAVES:
            self.ee.hooks[address] = self._leaf(address)
        # Native: one module per process; each beat re-seeds every owned range.
        E80[:] = ram[0x810E80:0x810E82]
        E88[:] = ram[0x810E88:0x810E8A]
        if first:
            assert NATIVE.em_rcl_init(str(EXPORT).encode(), C.addressof(E80)) == 0, 'em_rcl_init'
            assert NATIVE.em_rcl_static_world_load(str(STATIC_WORLD).encode()) == 0, 'em_rcl_static_world_load'
        for address, size in OWNED:
            data = self.ee.read(address, size)
            assert NATIVE.em_rcl_poke(address, data, size) == 0, ('poke', hex(address))
        self.ext = []
        views = (External * len(EXTERNAL))()
        for i, (address, size) in enumerate(EXTERNAL):
            buf = (C.c_uint8 * size).from_buffer_copy(self.ee.read(address, size))
            self.ext.append((address, size, buf))
            views[i] = External(address, size, C.addressof(buf))
        self.workers = Workers(None, F_VOID(self._n_record(0x1D7C30)), F_LEAF(self._n_leaf(0x11E748)),
                               F_LEAF(self._n_leaf(0x11E398)), F_BLOCK(self._n_block(0x1C1EA0)))
        assert NATIVE.em_rcl_bind(views, len(EXTERNAL), C.byref(self.workers)) == 0, 'em_rcl_bind'
        self.ee.write(0x8101D0, bytes(16))   # 001AF690 (the bind performs it natively)
        self._views = views
        # The frame loop's views: D_00810E88 and D_008106C4 (inside the
        # request block view, the same bytes as the binder's).
        req = next(buf for base, size, buf in self.ext if base == 0x8106B0)
        assert NATIVE.em_rcl_frame_views(C.addressof(E88), C.addressof(req) + 0x14) == 0, 'frame views'

    # ---- recorded callees -------------------------------------------------
    def _recorder(self, address):
        def hook(ee):
            self.olog.append((hex(address), ee.r[4] & MASK if address == 0x1C1EA0 else None))
            ee.r[2] = 0
        return hook

    def _leaf(self, address):
        def hook(ee):
            ee.f[0] = LEAF(address, ee.f[12] & MASK)
        return hook

    def _hardware(self, address):
        def hook(ee):
            if address == 0x101BB8:
                assert ee.r[4] & MASK == 1, ('dmac_channel_base', ee.r[4])
                ee.r[2] = D1_CHCR
                return
            if address == 0x101F08:
                assert ee.r[4] & MASK == D1_CHCR, ('00101F08 channel', hex(ee.r[4] & MASK))
                self.kicks.append(ee.r[5] & MASK)
            ee.r[2] = 0
        return hook
    def _n_record(self, address):
        def cb(_ctx):
            self.nlog.append((hex(address), None))
            return 0
        return cb

    def _n_block(self, address):
        def cb(_ctx, a0):
            self.nlog.append((hex(address), a0 & MASK))
            return 0
        return cb

    def _n_leaf(self, address):
        def cb(_ctx, x, out):
            out[0] = LEAF(address, x & MASK)
            return 0
        return cb

    # ---- steps ------------------------------------------------------------
    def poke_external(self, address, data):
        """A write to a byte another module owns: both sides."""
        self.ee.write(address, data)
        for base, size, buf in self.ext:
            if base <= address < base + size:
                buf[address - base:address - base + len(data)] = data
                return
        if address == 0x810E80:
            E80[:] = data
            return
        if address == 0x810E88:
            E88[:] = data
            return
        raise AssertionError(('not an external', hex(address)))

    def original(self, entry, ints=(), floats=()):
        ee = self.ee
        ee.r[29] = 0x7F0F0000
        for i, v in enumerate(ints):
            ee.r[4 + i] = sx32(v)
        for i, v in enumerate(floats):
            ee.f[12 + i] = v & MASK
        ee.r[31] = RETURN
        ee.run(entry)

    def step(self, label, entry, native, ints=(), floats=()):
        kicks = len(self.kicks)
        self.original(entry, ints, floats)
        rc = native()
        where = (self.beat, label)
        assert rc == 0, (where, 'native fault', hex(NATIVE.em_rcl_fault()))
        assert self.olog == self.nlog, (where, 'recorded calls differ', self.olog, self.nlog)
        if len(self.kicks) != kicks:
            chain, count = U32(), U32()
            assert NATIVE.em_rcl_kick(C.byref(chain), C.byref(count)) == 0
            assert len(self.kicks) == kicks + 1 and chain.value == self.kicks[-1], (
                where, 'kick', [hex(k) for k in self.kicks[kicks:]], hex(chain.value))
        for address, size in OWNED:
            want = self.ee.read(address, size)
            got = C.string_at(NATIVE.em_rcl_bytes(address, size), size)
            if want != got:
                diff = [hex(address + i) for i in range(size) if want[i] != got[i]]
                raise AssertionError((where, 'bytes differ', diff[:12], len(diff)))


def camera_move(pair, frame):
    """Rotate D_00810610 about its y axis by a frame-dependent angle and
    shift its translation row, on both sides (the camera stage of the frame
    that follows the head)."""
    words = list(struct.unpack('<16I', pair.ee.read(0x810610, 0x40)))
    m = [struct.unpack('<f', struct.pack('<I', w))[0] for w in words]
    a = 0.01 * (frame + 1)
    c, s = math.cos(a), math.sin(a)
    out = list(m)
    for r in range(4):
        x, z = m[4 * r + 0], m[4 * r + 2]
        out[4 * r + 0], out[4 * r + 2] = c * x + s * z, -s * x + c * z
    out[12] += 0.5 * (frame + 1)
    pair.poke_external(0x810610, struct.pack('<16f', *out))


def step_vw(pair, label):
    """Main-loop steps V and W as the port runs them: D_00810E88 = the
    buffer index (the field model), 001D2300, the flip of D_00810E80, then
    001D2580(field). Returns the entries run."""
    n = NATIVE
    e80 = struct.unpack('<h', bytes(E80))[0]
    field = e80 & 1
    pair.poke_external(0x810E88, struct.pack('<h', field))
    pair.step('%s 001D2300' % label, 0x1D2300, lambda: n.em_rcl_001D2300())
    pair.poke_external(0x810E80, struct.pack('<h', 1 - e80 if e80 in (0, 1) else 0))
    pair.step('%s 001D2580(%d)' % (label, field), 0x1D2580, lambda: n.em_rcl_001D2580(field), ints=(field,))
    return 2


def run_beat(beat_and_first):
    beat, first = beat_and_first
    p = Pair(beat, first)
    n = NATIVE
    frames = 6 if reference_mode.FULL else 3
    steps = 0
    p.step('001C1DC0', 0x1C1DC0, lambda: n.em_rcl_001C1DC0())
    steps += 1
    for frame in range(frames):
        e80 = struct.unpack('<h', bytes(E80))[0]
        p.step('f%d 001D1AE0' % frame, 0x1D1AE0, lambda: n.em_rcl_001D1AE0(e80), ints=(e80,))
        p.step('f%d 001D1C50' % frame, 0x1D1C50, lambda: n.em_rcl_001D1C50())
        for mode, scale, bias in ((2, F(1.0), F(150.0)), (3, F(1.0), F(150.0)), (1, 0, 0)):
            p.step('f%d 0021B9A0(%d)' % (frame, mode), 0x21B9A0,
                   lambda: n.em_rcl_0021B9A0(mode, scale, bias), ints=(mode,), floats=(scale, bias))
        p.step('f%d 001D1EA0(1)' % frame, 0x1D1EA0, lambda: n.em_rcl_001D1EA0(1), ints=(1,))
        steps += 6 + step_vw(p, 'f%d' % frame)
        camera_move(p, frame)
    for label, entry, value, call in (
            ('001D25F0(480)', 0x1D25F0, F(480.0), lambda v: n.em_rcl_001D25F0(v)),
            ('001D2610(0)', 0x1D2610, F(0.0), lambda v: n.em_rcl_001D2610(v)),
            ('001D2610(1)', 0x1D2610, F(1.0), lambda v: n.em_rcl_001D2610(v))):
        p.step(label, entry, lambda: call(value), floats=(value,))
        steps += 1
    d = 37.25
    f12, f13 = F(2.0 + 1.02 * d), F(d)
    p.step('001DD950', 0x1DD950, lambda: n.em_rcl_001DD950(0x8105E0, f12, f13), ints=(0x8105E0,),
           floats=(f12, f13))
    # A status frame (D_008106C4 != 0), then an opening-style frame
    # (D_008101E4 == 3: 001DDE10's 001DD950 path).
    p.poke_external(0x8106C4, b'\x01')
    e80 = struct.unpack('<h', bytes(E80))[0]
    p.step('status 001D1AE0', 0x1D1AE0, lambda: n.em_rcl_001D1AE0(e80), ints=(e80,))
    p.step('status 001D1C50', 0x1D1C50, lambda: n.em_rcl_001D1C50())
    p.step('status 001D2830(3, 1)', 0x1D2830, lambda: n.em_rcl_001D2830(3, 1), ints=(3, 1))
    p.step('status 001D1EA0(0)', 0x1D1EA0, lambda: n.em_rcl_001D1EA0(0), ints=(0,))
    steps += 4 + step_vw(p, 'status')
    p.poke_external(0x8106C4, b'\x00')
    p.poke_external(0x8101E4, b'\x03')
    e80 = struct.unpack('<h', bytes(E80))[0]
    p.step('opening 001D1AE0', 0x1D1AE0, lambda: n.em_rcl_001D1AE0(e80), ints=(e80,))
    p.step('opening 001D1C50', 0x1D1C50, lambda: n.em_rcl_001D1C50())
    p.step('opening 001D1EA0(1)', 0x1D1EA0, lambda: n.em_rcl_001D1EA0(1), ints=(1,))
    steps += 3 + step_vw(p, 'opening')
    # The tear-down frame (0x1AE040 state 5 / state 0): 001D1EF0, then V
    # takes the black clear (flag 3) and clears the flag.
    p.poke_external(0x8101E4, b'\x00')
    e80 = struct.unpack('<h', bytes(E80))[0]
    p.step('teardown 001D1AE0', 0x1D1AE0, lambda: n.em_rcl_001D1AE0(e80), ints=(e80,))
    p.step('teardown 001D1EF0', 0x1D1EF0, lambda: n.em_rcl_001D1EF0())
    steps += 2 + step_vw(p, 'teardown')
    # Step V's other gate branches: render flag 4 set (no 001E0DF0), then
    # flag 0x20 clear, each restored afterwards.
    for flag, on, restore in ((4, 1, 0), (0x20, 0, 1)):
        e80 = struct.unpack('<h', bytes(E80))[0]
        p.step('gate %#x 001D1AE0' % flag, 0x1D1AE0, lambda: n.em_rcl_001D1AE0(e80), ints=(e80,))
        p.step('gate %#x 001D2830' % flag, 0x1D2830, lambda: n.em_rcl_001D2830(flag, on), ints=(flag, on))
        steps += 2 + step_vw(p, 'gate %#x' % flag)
        p.step('gate %#x restore' % flag, 0x1D2830, lambda: n.em_rcl_001D2830(flag, restore),
               ints=(flag, restore))
        steps += 1
    ctx = 0x811CC0
    return (beat, steps, len(p.olog), len(p.kicks), p.ee.load(ctx + 0xC), p.ee.load(ctx + 0x174),
            p.ee.read(ctx + 0xA0, 16).hex())


def main():
    global ELF, NATIVE, LEAF, E80, E88
    started = time.time()
    assert EXPORT.exists(), 'run tools/export_render_context.py first'
    ELF = read_elf()
    NATIVE = build_native()
    LEAF = FRH.Leaf(ELF)
    E80 = (C.c_uint8 * 2)()
    E88 = (C.c_uint8 * 2)()
    beats = sorted(p.name for p in ROUTE.iterdir()
                   if (p / 'eeMemory.bin').exists() and not p.name.startswith('15'))
    picked = reference_mode.select(beats, 3, 0x1C50, keep=lambda i, b: b in ('00_panel_no_battery',
                                                                              '14_roger_encounter'))
    # The module is one static instance: the beats run in this process, in
    # order (each re-seeds every range the module owns).
    for i, beat in enumerate(picked):
        beat, steps, recorded, kicks, flags, flags_hi, fog = run_beat((beat, i == 0))
        print('%s: PASS %d entries, all owned bytes (packet arena, chain table, context, GS blocks, skin '
              'records, .data, scratchpad) equal the original after each; %d recorded calls identical; '
              '%d step-V kicks of the same list; flags %#x / %#x, fog %s'
              % (beat, steps, recorded, kicks, flags, flags_hi, fog))
    reference_mode.banner(reference_mode.part(len(picked), len(beats), 'route beats'))
    print('render context live vs original instructions: PASS (%.1fs)' % (time.time() - started))


if __name__ == '__main__':
    main()
