#!/usr/bin/env python3
"""Compare em_module_loader.c with the ORIGINAL instructions (docs/MODULE_LOADER.md).

The user's pinned boot ELF supplies every instruction; the user's own RAM
captures (../Extermination/build/s87/route, build/s87/c7cap/h7,
build/s87/loadwait, build/startup-reference) and disc image supply the
state; the oracle's drive reads the disc image itself, not the pack
under test. Nothing original is embedded here; build/ holds counts only.

A. Leaves, executed unmodified: 00200780 (disc read), 00200730 (poll),
   00200830 (DMA send), 00200890 (player packet), 00200970 (texture
   restore). Their SDK callees (00113280, 00112440, 00112D18, 00113680,
   00101BB8, 00102468, 00101F08, 001CCB10) are hooked, scripted per case
   and recorded; the native side gets the same script through
   EmModuleLoaderSdk. Every call and argument (the read-mode bytes the
   original builds on its stack included), every result and the set of
   bytes the original writes are compared. The hooked set must equal the
   functions' jal targets.
B. Whole loads over the captured route-03 RAM: the original 001FF080 +
   001AB740 + 001FF0D0 + 001FF830 + 001FF3F0 + 001FEF70 + 001AB7D0 +
   00200780 + 00200730 + 00200830 + 00101BB8, with only the libcdvd RPC and
   the DMA hardware leaves answered by a drive model, against the native
   em_task slot 2 + em_module_loader + em_status_scene loader. Compared:
   every callee entry (00200780/00200730/00200830 and each SDK leaf) with
   its arguments and the whole modelled memory at that moment, the modelled
   memory after every frame, every byte the original instructions write
   (inside the modelled set) and every byte the drive delivers, and the
   bytes each DMA send hands the consumer.
C. The captures: the host-speed load takes 10 dispatches and its rows are
   exactly the captured rows without the 14 busy-poll rows; the measured
   drive reproduces the captured slot-2 record and D_00275BD8 of every one
   of the 24 frames of route 03 (h7) and route 01 (load-wait probe); the
   module-0x21 chunk the loader DMAs is exactly the GS upload the port's
   BATTERY atlas was decoded from; and the facts the smoke's alignment rule
   rests on (the wait rows change nothing but the frame counters; two
   rand() calls per wait frame from 001D7C30).

D. Pinned cases (named mutants): D_00282157 non-zero on some frames; a
   relocated pack (other disc positions, reads starting inside wider
   ranges, D_0028A748 unlike D_0028A744) under the measured drive; the
   disc-only pack (no capture); module 0x2B with spad 0x70003B90 = 0 / 1 / 2;
   00200890 after module 3's whole load (DMA chains inside a delivered
   region); 00200970 with 001CCB10 changing spad 0x70003B90; the orphan
   fault. The captured code bytes of both routes are asserted equal to the
   pinned ELF before any whole load.

Default run ~2 s; EM_TEST_FULL=1 adds the leaf sweep and whole loads of
every eligible module header on the user's disc.
"""
import ctypes as C
import hashlib
import itertools
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
from reference_mode import FULL, banner, select  # noqa: E402
from test_player_slide_reference import EE, RETURN, ELF_SHA256  # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
LANE = os.environ.get('EM_LANE', 'b15/module_loader')
OUT = ROOT / 'build' / LANE
ROUTE = DECOMP / 'build/s87/route'
H7 = DECOMP / 'build/s87/c7cap/h7/fields/frames.jsonl'
PROBE01 = DECOMP / 'build/s87/loadwait/01_battery/probe_boundary.json'
RNG01 = DECOMP / 'build/s87/c7cap/rng/r01/rand.jsonl'
PANEL_GS = DECOMP / 'build/startup-reference/panel/gs.bin'
ISO = DECOMP / 'Extermination-rebuilt.iso'
MASK = 0xFFFFFFFF

SLOT, SPAD_SLOT, SPAD_3B90 = 0x28A790, 0x70003B6C, 0x70003B90
HEADER, BD8, GATE = 0x289BC0, 0x275BD8, 0x282157
WORDS = (0x275C70, 0x275C74)
# D_0028A490..D_0028A74F: the resource-slot table, whose slots are also the
# cursors D_0028A5A0 and D_0028A734..D_0028A748 (one storage), up to the
# task table D_0028A750.
RELOC = 0x28A490
RELOC_WORDS = 0xB0
CURSORS = (0x28A5A0, 0x28A738, 0x28A73C, 0x28A744, 0x28A748)
SNAP = 1 + 24 + 2 + 2 * 4 + RELOC_WORDS * 4 + 0x800

READ, POLL, DMA, PACKET, RESTORE = 0x200780, 0x200730, 0x200830, 0x200890, 0x200970
READY, CDREAD, SYNC, ERROR = 0x113280, 0x112440, 0x112D18, 0x113680
CHANNEL, WAIT, SEND, UPLOAD = 0x101BB8, 0x102468, 0x101F08, 0x1CCB10
SIZES = {READ: 0xA8, POLL: 0x44, DMA: 0x60, PACKET: 0xD8, RESTORE: 0x64}
LEAF_CALLEES = {READ: {READY, CDREAD}, POLL: {SYNC, ERROR}, DMA: {CHANNEL, WAIT, SEND},
                PACKET: {DMA}, RESTORE: {DMA, UPLOAD, PACKET}}


def s32(v):
    v &= MASK
    return v - (1 << 32) if v & 0x80000000 else v


# ------------------------------------------------------------ the oracle ---

class LoaderEE(EE):
    """The shared EE core (paddub, lq/sq included) logging every store:
    (address, size, inside_hook). Stack stores are dropped."""

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.stores = []
        self.in_hook = 0

    def save(self, address, value, size=4):
        a = address & MASK
        if not 0x7F000000 <= a < 0x7F100000:
            self.stores.append((a, size, self.in_hook))
        super().save(address, value, size)

    def write(self, address, data):
        a = address & MASK
        if not 0x7F000000 <= a < 0x7F100000:
            self.stores.append((a, len(data), self.in_hook))
        super().write(address, data)

    def mmi(self, word, pc):
        """Adds PCPYH (the C runtime memset 00121A28 broadcasts with it;
        002009E0's bss clear)."""
        if word & 63 == 0x29 and (word >> 6 & 31) == 0x1B:
            rt, rd = word >> 16 & 31, word >> 11 & 31
            lo, hi = self.r[rt] & 0xFFFF, self.rh[rt] & 0xFFFF
            if rd:
                self.r[rd] = lo * 0x0001000100010001
                self.rh[rd] = hi * 0x0001000100010001
            return
        super().mmi(word, pc)

    def hook(self, address, fn):
        def wrapped(ee):
            ee.in_hook += 1
            try:
                fn(ee)
            finally:
                ee.in_hook -= 1
        self.hooks[address] = wrapped

    def watch(self, address, fn):
        """fn(ee) at the entry of an original routine that still runs."""
        def entry(ee):
            fn(ee)
            ra = ee.r[31]
            ee.r[31] = RETURN
            del ee.hooks[address]
            try:
                ee.run(address)
            finally:
                ee.hooks[address] = entry
            ee.r[31] = ra
        self.hooks[address] = entry

    def call(self, entry, args=()):
        for i, v in enumerate(args):
            self.r[4 + i] = s32(v) & 0xFFFFFFFFFFFFFFFF
        self.r[31] = RETURN
        self.run(entry)
        return self.r[2] & MASK

    def snapshot(self):
        out = bytearray([self.load(SLOT, 1)])
        out += self.read(SLOT + 8, 24)
        out += bytes([self.load(BD8, 1), self.load(GATE, 1)])
        for a in WORDS:
            out += struct.pack('<I', self.load(a))
        out += self.read(RELOC, RELOC_WORDS * 4)
        out += self.read(HEADER, 0x800)
        return bytes(out)


def modelled():
    """The original addresses the snapshot covers (the loader may write)."""
    s = {SLOT} | set(range(SLOT + 8, SLOT + 0x20)) | {BD8}
    for a in WORDS:
        s |= set(range(a, a + 4))
    return s | set(range(RELOC, RELOC + RELOC_WORDS * 4)) | set(range(HEADER, HEADER + 0x800))


MODELLED = modelled()


def jal_targets(ee, start, size):
    out = set()
    for pc in range(start, start + size, 4):
        w = ee.load(pc)
        if w >> 26 == 3:
            out.add((w & 0x3FFFFFF) << 2)
    return out


# ------------------------------------------------------------ the native ---

I32P, U32P, U8P = C.POINTER(C.c_int32), C.POINTER(C.c_uint32), C.POINTER(C.c_uint8)
SDK_TYPES = [
    ('ready_00113280', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int32, I32P)),
    ('read_00112440', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32, U8P, I32P)),
    ('sync_00112D18', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int32, I32P)),
    ('error_00113680', C.CFUNCTYPE(C.c_int, C.c_void_p, I32P)),
    ('channel_00101BB8', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int32, U32P)),
    ('wait_00102468', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_int32, C.c_int32)),
    ('send_00101F08', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32)),
    ('upload_001CCB10', C.CFUNCTYPE(C.c_int, C.c_void_p)),
]


class Sdk(C.Structure):
    _fields_ = [('ctx', C.c_void_p)] + SDK_TYPES


class Fault(C.Structure):
    _fields_ = [('address', C.c_uint32), ('code', C.c_int32)]


GATE_READER = C.CFUNCTYPE(C.c_uint8, C.c_void_p)


class Views(C.Structure):
    _fields_ = [('d275BD8', U8P), ('r_00282157', GATE_READER), ('r_00282157_ctx', C.c_void_p),
                ('d810CA4', U8P), ('d810CA6', U8P), ('spad3B90', U8P),
                ('d810700', U8P), ('d810701', U8P), ('d810703', U8P), ('d810704', U8P),
                ('d810707', U8P), ('d810C60', U8P)]


def _slot_word(address):
    slot = (address - RELOC) >> 2
    return property(lambda self: self.d28A490[slot],
                    lambda self, value: self.d28A490.__setitem__(slot, value))


class Loader(C.Structure):  # EmStatusSceneLoader
    _fields_ = [(n, C.c_uint8) for n in ('d282157', 'd275BD8', 'spad3B90', 'd810CA4', 'd810CA6')] + [
        (n, C.c_uint32) for n in ('d275C70', 'd275C74')] + [
        ('d28A490', C.c_uint32 * RELOC_WORDS), ('header', C.c_uint8 * 0x800)]
    d28A5A0 = _slot_word(0x28A5A0)
    d28A738 = _slot_word(0x28A738)
    d28A73C = _slot_word(0x28A73C)
    d28A744 = _slot_word(0x28A744)
    d28A748 = _slot_word(0x28A748)


TRACE = C.CFUNCTYPE(None, C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32, C.c_uint32, C.c_uint32)
BANK = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, U8P, C.c_uint32, U32P)
AREA_BYTES = (0x810700, 0x810701, 0x810703, 0x810704, 0x810707, 0x810C60)
CHAIN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, U8P, C.c_uint32)


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    lib_path = OUT / 'module_loader.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', 'src/game/em_module_loader.c',
                    'src/game/em_status_scene_original.c', 'src/game/em_task.c',
                    '-o', str(lib_path)], cwd=ROOT, check=True)
    lib = C.CDLL(str(lib_path))
    F = C.POINTER(Fault)
    lib.em_module_loader_read_00200780.argtypes = [C.POINTER(Sdk), C.c_uint32 * 2, C.c_uint32,
                                                   C.c_int32, C.c_int32, I32P, F]
    lib.em_module_loader_poll_00200730.argtypes = [C.POINTER(Sdk), I32P, F]
    lib.em_module_loader_dma_00200830.argtypes = [C.POINTER(Sdk), C.c_uint32, F]
    lib.em_module_loader_packet_00200890.argtypes = [C.POINTER(Sdk), C.c_uint8, C.c_uint8,
                                                     C.c_uint32 * 5, U32P, F]
    lib.em_module_loader_restore_00200970.argtypes = [C.POINTER(Sdk), C.c_int32, C.c_uint32, U8P,
                                                      C.c_uint8, C.c_uint8, C.c_uint32 * 5, F]
    lib.em_module_loader_packet_00200890.restype = C.c_int
    lib.em_module_loader_sdk.argtypes = [C.c_void_p]
    lib.em_module_loader_sdk.restype = C.POINTER(Sdk)
    lib.em_module_loader_orphaned.argtypes = [F]
    lib.em_module_loader_open.argtypes = [C.c_char_p]
    lib.em_module_loader_open.restype = C.c_void_p
    lib.em_module_loader_close.argtypes = [C.c_void_p]
    lib.em_module_loader_set_views.argtypes = [C.c_void_p, C.POINTER(Views)]
    lib.em_module_loader_set_chain_hook.argtypes = [C.c_void_p, CHAIN, C.c_void_p]
    lib.em_module_loader_set_area_chain_hook.argtypes = [C.c_void_p, CHAIN, C.c_void_p]
    lib.em_module_loader_set_bank_hook.argtypes = [C.c_void_p, BANK, C.c_void_p]
    lib.em_module_loader_set_trace.argtypes = [C.c_void_p, TRACE, C.c_void_p]
    lib.em_module_loader_set_drive.argtypes = [C.c_void_p, C.c_int]
    lib.em_module_loader_bind_live.argtypes = [C.c_void_p]
    lib.em_module_loader_request_001FF080.argtypes = [C.c_void_p, C.c_uint8, C.c_uint8]
    lib.em_module_loader_field.argtypes = [C.c_void_p]
    lib.em_module_loader_failed.argtypes = [C.c_void_p, F]
    lib.em_module_loader_counts.argtypes = [C.c_void_p, U32P, U32P, U32P]
    lib.em_module_loader_snapshot.argtypes = [C.c_void_p, C.c_void_p, C.c_uint8 * SNAP]
    lib.em_module_loader_snapshot.restype = None
    lib.em_module_loader_state.argtypes = [C.c_void_p]
    lib.em_module_loader_state.restype = C.POINTER(Loader)
    lib.em_module_loader_memory.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32]
    lib.em_module_loader_memory.restype = C.c_void_p
    lib.em_task_init.restype = None
    lib.em_task_dispatch.restype = None
    return lib


def sdk(**fns):
    s = Sdk()
    keep = []
    for name, typ in SDK_TYPES:
        fn = fns.get(name)
        if fn is not None:
            cb = typ(fn)
            keep.append(cb)
            setattr(s, name, cb)
    s._keep = keep
    return s


# ======================================================= A: the leaves ====

LEAF_CALL_CAP = 64  # no case scripts more calls; past it the native side fails, never spins


class LeafRig:
    """One leaf case: scripted SDK results, recorded on both sides. An
    exhausted script or more than LEAF_CALL_CAP native calls makes the
    native leaf return a port failure, so a wrong loop fails the case
    instead of hanging the run. `upload_spad` (optional): the byte 001CCB10
    leaves in spad 0x70003B90 (both sides), to pin that 00200970 reads it
    after the call."""

    def __init__(self, elf, script, upload_spad=None):
        self.script = script  # dict: name -> list of results
        self.o = LoaderEE(elf)
        self.expected, self.actual = [], []
        self.spad3b90 = C.c_uint8(0)
        q = {k: list(v) for k, v in script.items()}
        o = self.o

        def rec(name, n, ret=None, mode=False):
            def fn(ee):
                args = [ee.r[4 + i] & MASK for i in range(n)]
                if mode:
                    args.append(tuple(ee.read(args[3], 3)))
                    args = args[:3] + [args[4]]
                self.expected.append((name, *args))
                if ret:
                    ee.r[2] = s32(q[ret].pop(0)) & 0xFFFFFFFFFFFFFFFF
            return fn
        o.hook(READY, rec('ready', 1, 'ready'))
        o.hook(CDREAD, rec('read', 4, 'read', mode=True))
        o.hook(SYNC, rec('sync', 1, 'sync'))
        o.hook(ERROR, rec('error', 0, 'error'))
        o.hook(CHANNEL, rec('channel', 1, 'channel'))
        o.hook(WAIT, rec('wait', 3))
        o.hook(SEND, rec('send', 2))
        record_upload = rec('upload', 0)

        def upload(ee):
            record_upload(ee)
            if upload_spad is not None:
                ee.save(SPAD_3B90, upload_spad, 1)
        o.hook(UPLOAD, upload)
        n = {k: list(v) for k, v in script.items()}
        A = self.actual

        def ret(name, out, conv=s32):
            if len(A) > LEAF_CALL_CAP or not n.get(name):
                return -1  # port failure: the case fails at once
            out[0] = conv(n[name].pop(0))
            return 0

        def native_upload(_):
            A.append(('upload',))
            if upload_spad is not None:
                self.spad3b90.value = upload_spad
            return 0
        self.native = sdk(
            ready_00113280=lambda _, m, out: A.append(('ready', m & MASK)) or ret('ready', out),
            read_00112440=lambda _, l, s, b, md, out: A.append(('read', l, s, b, tuple(md[:3]))) or
            ret('read', out),
            sync_00112D18=lambda _, m, out: A.append(('sync', m & MASK)) or ret('sync', out),
            error_00113680=lambda _, out: A.append(('error',)) or ret('error', out),
            channel_00101BB8=lambda _, ch, out: A.append(('channel', ch & MASK)) or
            ret('channel', out, lambda v: v & MASK),
            wait_00102468=lambda _, b, m, t: A.append(('wait', b, m & MASK, t & MASK)) or 0,
            send_00101F08=lambda _, b, c: A.append(('send', b, c)) or 0,
            upload_001CCB10=native_upload)

    def finish(self, label):
        assert self.actual == self.expected, (label, self.actual, self.expected)
        wrote = {a for a, n, hooked in self.o.stores if not hooked for a in range(a, a + n)}
        assert not wrote, (label, 'the original wrote outside its stack', sorted(hex(a) for a in wrote)[:8])


def check_callees(elf):
    ee = LoaderEE(elf)
    for fn, size in SIZES.items():
        assert jal_targets(ee, fn, size) == LEAF_CALLEES[fn], (hex(fn), sorted(map(hex, jal_targets(ee, fn, size))))
    return len(SIZES)


def check_read(elf, lib):
    sizes = [0, 1, 0x7FF, 0x800, 0x801, 0x50800, 0x7FFFF800, 0x7FFFFFFF, -1, -0x800, -0x80000000]
    offsets = [0, 0x7FF, 0x800, 0x10800, 0xE4BC000, 0x7FFFFFFF, -1, -0x800, -0x80000000]
    descs = [(0x9D7D0, 0x1C000), (0x801D8, 0xEAFC000), (0xFFFFFFFF, 0), (0, 0xFFFFFFFF), (5, 0x801),
             (0x100, 0x80000000), (0x100, 0xFFFFF000)]
    scripts = [[1], [0, 1], [0, 0, 0, 7], [-1]]
    cases = list(itertools.product(sizes, offsets, descs, scripts, (0x289BC0, 0x19A3F40)))
    # Always: the whole-file form (size < 0) over descriptor sizes whose
    # rounding crosses bit 31 (logical against arithmetic shift).
    cases = select(cases, 120, 0x200780, axes=(lambda c: c[0], lambda c: c[1], lambda c: c[2],
                                              lambda c: len(c[3]), lambda c: c[4]),
                   keep=lambda i, c: c[0] == -1 and c[2][1] >= 0x7FFFF801 and c[1] == 0 and len(c[3]) == 1)
    for size, offset, desc, accept, buf in cases:
        replies = [2, 6, 0, -1][:len(accept)] + [2] * len(accept)
        rig = LeafRig(elf, dict(ready=replies, read=accept))
        o = rig.o
        file = 0x28A488
        o.save(file, desc[0]); o.save(file + 4, desc[1])
        o.stores.clear()
        got = o.call(READ, (file, buf, offset, size))
        fault, out = Fault(), C.c_int32()
        r = lib.em_module_loader_read_00200780(C.byref(rig.native), (C.c_uint32 * 2)(*desc), buf,
                                               offset, size, C.byref(out), C.byref(fault))
        assert r == 0 and fault.code == 0, (size, offset, fault.code)
        assert (out.value & MASK) == got, ('bytes', size, offset, desc, hex(out.value & MASK), hex(got))
        rig.finish(('read', size, offset, desc, accept))
    return len(cases)


def check_poll(elf, lib):
    cases = list(itertools.product((0, 1, 2, -1, 0x7FFFFFFF), (0, 1, -1, 6, 0x80000000)))
    for busy, error in cases:
        rig = LeafRig(elf, dict(sync=[busy], error=[error]))
        got = rig.o.call(POLL)
        fault, out = Fault(), C.c_int32()
        assert lib.em_module_loader_poll_00200730(C.byref(rig.native), C.byref(out), C.byref(fault)) == 0
        assert (out.value & MASK) == got, ('poll', busy, error, out.value, got)
        rig.finish(('poll', busy, error))
    return len(cases)


def check_dma(elf, lib):
    cases = list(itertools.product((0x10009000, 0, 0xFFFFFFF0), (0x19A3F40, 0, 0x80000000, 0xFFFFFFFF)))
    for base, chain in cases:
        rig = LeafRig(elf, dict(channel=[base]))
        rig.o.call(DMA, (chain,))
        fault = Fault()
        assert lib.em_module_loader_dma_00200830(C.byref(rig.native), chain, C.byref(fault)) == 0
        rig.finish(('dma', base, chain))
    return len(cases)


TABLE = (0x1A00000, 0x1A00400, 0x1A00800, 0x1A00C00, 0x1A01000)


def packet_globals(o, d707, c60, d564=0x1B00000, spad=0):
    o.save(0x810707, d707, 1); o.save(0x810C60, c60, 1)
    for i, v in enumerate(TABLE):
        o.save(0x28A4B0 + 4 * i, v)
    o.save(0x28A564, d564); o.save(SPAD_3B90, spad, 1)
    o.stores.clear()


def check_packet(elf, lib):
    n = 0
    for d707, c60 in itertools.product((0, 1, 2, 3, 0x80, 0xFF), (0, 1, 2, 3, 0x80, 0xFF)):
        rig = LeafRig(elf, dict(channel=[0x10009000]))
        packet_globals(rig.o, d707, c60)
        rig.o.call(PACKET)
        fault, chain = Fault(), C.c_uint32()
        assert lib.em_module_loader_packet_00200890(C.byref(rig.native), d707, c60, (C.c_uint32 * 5)(*TABLE),
                                                    C.byref(chain), C.byref(fault)) == 0
        assert ('send', 0x10009000, chain.value) in rig.expected
        rig.finish(('packet', d707, c60))
        n += 1
    for a0, spad, d707, c60 in itertools.product((0, 1, -1, 2), (0, 1, 2, 3), (0, 1, 2), (0, 1, 2)):
        rig = LeafRig(elf, dict(channel=[0x10009000] * 2))
        packet_globals(rig.o, d707, c60, spad=spad)
        rig.o.call(RESTORE, (a0,))
        fault = Fault()
        rig.spad3b90.value = spad
        assert lib.em_module_loader_restore_00200970(C.byref(rig.native), a0, 0x1B00000, C.byref(rig.spad3b90),
                                                     d707, c60, (C.c_uint32 * 5)(*TABLE), C.byref(fault)) == 0
        rig.finish(('restore', a0, spad, d707, c60))
        n += 1
    # Pinned: 00200970 reads spad 0x70003B90 after 001CCB10 returns. The
    # upload changes the byte; the packet follows the new value.
    for before, after in ((0, 2), (2, 0), (1, 2), (2, 3)):
        rig = LeafRig(elf, dict(channel=[0x10009000] * 2), upload_spad=after)
        packet_globals(rig.o, 1, 0, spad=before)
        rig.o.call(RESTORE, (0,))
        fault = Fault()
        rig.spad3b90.value = before
        assert lib.em_module_loader_restore_00200970(C.byref(rig.native), 0, 0x1B00000, C.byref(rig.spad3b90),
                                                     1, 0, (C.c_uint32 * 5)(*TABLE), C.byref(fault)) == 0
        sends = [e for e in rig.expected if e[0] == 'send']
        assert len(sends) == (2 if after == 2 else 1), ('post-upload spad', before, after, sends)
        rig.finish(('restore after upload', before, after))
        n += 1
    return n


# ================================================== B: whole loads ========

_DISC = []


def disc():
    """The user's disc image (read in place), opened once."""
    if not _DISC:
        import export_module_loader as X
        _DISC.append(X.Disc(ISO))
    return _DISC[0]


class Drive:
    """The oracle side's drive: every read lands at once with the disc's own
    sectors (read from the ISO, not from the pack under test; `shift` maps a
    relocated pack's lsn back to the image). The measured option keeps a
    read busy for its measured fields, named by (file, sector in file,
    sectors) against the descriptors in the oracle's RAM."""
    MEASURED = ((0, 0x21, 1, 6), (1, 0xE4BC000 >> 11, 161, 8))

    def __init__(self, measured, index_lsn, data_lsn, shift=0, area_lsn=0):
        self.measured, self.shift = measured, shift
        self.base = (index_lsn, data_lsn, area_lsn)
        self.table = self.MEASURED + (NEW_GAME_READS() if measured and area_lsn else ())
        self.field, self.busy_until = 0, 0
        self.delivered = []

    def read(self, ee, lsn, sectors, buf):
        if sectors:
            ee.write(buf, disc().sectors(lsn - self.shift, sectors))
            self.delivered.append((buf, sectors * 0x800))
        busy = 0
        if self.measured:
            for f, sector, n, b in self.table:
                if self.base[f] + sector == lsn and n == sectors:
                    busy = b
        self.busy_until = self.field + busy + (1 if busy else 0)


NEWGAME = DECOMP / 'build/startup-reference/newgame_samples.jsonl'
_NEW_GAME = []


def new_game_transitions():
    """{state: the frame label of the first New Game sample showing it} for
    the slot-2 record (state, +8, +9, +A, +B, +0xE) of the PCSX2 New Game
    capture (samples read about twice a frame while the VM ran)."""
    out = {}
    for line in NEWGAME.open():
        d = json.loads(line)
        t = d['tasks'][2]
        u = bytes.fromhex(t['user'])
        out.setdefault((t['state'], u[0], u[1], u[2], u[3], u[6]), d['frame'])
    return out


def NEW_GAME_READS():
    """The New Game loads' reads with the busy fields the capture shows,
    derived here from the samples (issue frame to done frame, less the
    done poll): (file, sector in file, sectors, busy), file 2 = the area
    file D_0028A3C0[0x0B]. The native drive's table (em_module_loader.c
    MEASURED) must equal it."""
    if not _NEW_GAME:
        f = new_game_transitions()
        pairs = (  # (issue state, done state) -> the read
            (((2, 0, 1, 0, 0, 3), (2, 0, 2, 0, 0, 3)), (0, 3, 1)),
            (((2, 0, 4, 0, 0, 3), (2, 0, 5, 0, 0, 3)), (1, 0x214800 >> 11, 1175)),
            (((2, 1, 1, 0, 0, 0), (2, 1, 2, 0, 0, 0)), (2, 0, 15)),
            (((2, 1, 3, 0, 0, 0), (2, 1, 4, 0, 0, 0)), (0, 0x0F, 1)),
            (((2, 1, 4, 1, 4, 0), (2, 1, 4, 1, 5, 0)), (1, 0x76E7800 >> 11, 149)),
            (((2, 1, 4, 2, 2, 0), (2, 1, 4, 2, 3, 0)), (1, 0x7732000 >> 11, 433)),
            (((2, 1, 6, 0, 0, 0), (2, 1, 7, 0, 0, 0)), (1, 0x780A800 >> 11, 3292)))
        for (issue, done), (file, sector, n) in pairs:
            _NEW_GAME.append((file, sector, n, f[done] - f[issue] - 1))
    return tuple(_NEW_GAME)


PACK_HEADER = 0x120   # EMML version 2 (tools/export_module_loader.py)


def load_pack(path):
    data = Path(path).read_bytes()
    assert data[:4] == b'EMML' and struct.unpack_from('<I', data, 4)[0] == 2
    n = struct.unpack_from('<I', data, 8)[0]
    out = {}
    for i in range(n):
        lsn, sectors, off, _ = struct.unpack_from('<4I', data, PACK_HEADER + 0x10 * i)
        out[(lsn, sectors)] = data[off:off + sectors * 0x800]
    return out


def pack_globals(path):
    """{address: word} of the descriptors and cursor seeds a pack carries."""
    data = Path(path).read_bytes()[:0x40]
    words = struct.unpack_from('<11I', data, 0x10)
    addrs = (0x28A480, 0x28A484, 0x28A488, 0x28A48C) + WORDS + CURSORS
    return dict(zip(addrs, words))


def relocated_pack(src, dst, shift, before, after, seeds):
    """A pack holding the same reads at other disc positions: every lsn
    (descriptors included) moved by `shift`, every range widened by
    `before` / `after` sectors of the image's neighbouring sectors (so each
    read starts inside a range), and cursor seeds overridden by `seeds`."""
    data = Path(src).read_bytes()
    g = pack_globals(src)
    g[0x28A480] += shift
    g[0x28A488] += shift
    g.update(seeds)
    order = sorted(load_pack(src))
    blob = bytearray(struct.pack('<4sIII', b'EMML', 2, len(order), 0))
    blob += struct.pack('<11I', *(g[a] for a in (0x28A480, 0x28A484, 0x28A488, 0x28A48C) + WORDS + CURSORS)) \
        + bytes(4)
    # The boot tables (0x40..0x120): the area files' lsns move with the disc.
    tables = bytearray(data[0x40:PACK_HEADER])
    for i in range(0x17):
        lsn = struct.unpack_from('<I', tables, 0x20 + 8 * i)[0]
        struct.pack_into('<I', tables, 0x20 + 8 * i, lsn + shift if lsn else 0)
    blob += tables
    offset = PACK_HEADER + 0x10 * len(order)
    table, payload = bytearray(), bytearray()
    for lsn, sectors in order:
        n = sectors + before + after
        table += struct.pack('<4I', lsn - before + shift, n, offset + len(payload), 0)
        payload += disc().sectors(lsn - before, n)
    Path(dst).write_bytes(bytes(blob + table + payload))
    assert data[:4] == b'EMML'
    return dst


class WholeLoad:
    """001FF080(0, module) then a dispatch per frame, both sides."""

    def __init__(self, elf, lib, ram, spad, pack_path, measured=False, scribble=True, seed_from_pack=False,
                 spad3b90=None, shift=0):
        """seed_from_pack: write the pack's descriptors and cursor seeds into
        the oracle's RAM (a pack not taken from this capture). spad3b90: a
        value for spad 0x70003B90 on both sides. shift: see Drive."""
        self.lib = lib
        self.o = LoaderEE(elf, ram, spad)
        o = self.o
        if seed_from_pack:
            for a, v in pack_globals(pack_path).items():
                o.save(a, v)
        if spad3b90 is not None:
            o.save(SPAD_3B90, spad3b90, 1)
        self.drive = Drive(measured, o.load(0x28A480), o.load(0x28A488), shift,
                           o.load(0x28A3C0 + 8 * 0x0B))
        self.expected, self.actual = [], []
        self.sent_o, self.sent_n = [], []
        # The pre-load state of route 03 f390: slot 2 idle with the previous
        # load's bytes, D_00275BD8 raised by the ITEM root (CAPTURES_C7 6).
        o.save(BD8, 1, 1)
        # D_00282157 is 0 at every frame of both captured waits (h7, probe).
        o.save(GATE, 0, 1)
        if scribble:  # the load must fill these itself
            o.write(HEADER, bytes([0xA5]) * 0x800)
        o.stores.clear()
        # Native: the same globals.
        lib.em_task_init()
        self.ml = lib.em_module_loader_open(str(pack_path).encode())
        assert self.ml, 'pack did not open'
        lib.em_module_loader_bind_live(self.ml)
        lib.em_module_loader_set_drive(self.ml, 1 if measured else 0)
        self.bd8, self.gate = C.c_uint8(1), C.c_uint8(o.load(GATE, 1))
        self.ca4, self.ca6 = C.c_uint8(o.load(0x810CA4, 1)), C.c_uint8(o.load(0x810CA6, 1))
        self.s3b90 = C.c_uint8(o.load(SPAD_3B90, 1))
        self.gate_cb = GATE_READER(lambda _: self.gate.value)
        # 001FFCD0's area bytes and 00200890's two, from the capture.
        self.area = {a: C.c_uint8(o.load(a, 1)) for a in AREA_BYTES}
        self.views = Views(C.pointer(self.bd8), self.gate_cb, None, C.pointer(self.ca4),
                           C.pointer(self.ca6), C.pointer(self.s3b90),
                           *(C.pointer(self.area[a]) for a in AREA_BYTES))
        self.bank_script, self.bank_calls = [], 0
        self.bank_cb = BANK(self.n_bank)
        lib.em_module_loader_set_bank_hook(self.ml, self.bank_cb, None)
        self.extra_modelled = set()
        lib.em_module_loader_set_views(self.ml, C.byref(self.views))
        st = lib.em_module_loader_state(self.ml).contents
        for name, a in zip(('d275C70', 'd275C74', 'd28A5A0', 'd28A738', 'd28A73C', 'd28A744', 'd28A748'),
                           WORDS + CURSORS):
            assert getattr(st, name) == o.load(a), ('pack cursor differs from the capture', name)
        for i in range(RELOC_WORDS):
            st.d28A490[i] = o.load(RELOC + 4 * i)
        C.memmove(st.header, o.read(HEADER, 0x800), 0x800)
        self.trace_cb = TRACE(self.n_trace)
        self.chain_cb = CHAIN(self.n_chain)
        lib.em_module_loader_set_trace(self.ml, self.trace_cb, None)
        lib.em_module_loader_set_chain_hook(self.ml, self.chain_cb, None)
        lib.em_module_loader_set_area_chain_hook(self.ml, self.chain_cb, None)
        self._hooks()

    # -- native side
    def n_snapshot(self):
        buf = (C.c_uint8 * SNAP)()
        self.lib.em_module_loader_snapshot(self.ml, None, buf)
        return bytes(buf)

    def n_trace(self, _, callee, a0, a1, a2, a3):
        self.actual.append((callee, a0, a1, a2, a3, self.n_snapshot()))

    def n_chain(self, _, chain, data, size):
        self.sent_n.append((chain, size, hashlib.sha256(C.string_at(data, size)).hexdigest()))
        return 0

    def n_bank(self, _, address, data, size, result):
        """001FB370 (native): the scripted results (the sound-bank test
        proves the chain itself)."""
        result[0] = self.bank_script[self.bank_calls] if self.bank_calls < len(self.bank_script) else 0
        self.bank_calls += 1
        return 0

    # -- oracle side
    def _hooks(self):
        o, d = self.o, self.drive

        def entry(callee, n, fix=lambda a: a):
            def fn(ee):
                args = fix([ee.r[4 + i] & MASK for i in range(n)] + [0] * (4 - n))
                self.expected.append((callee, *args[:4], ee.snapshot()))
            return fn
        o.watch(READ, entry(READ, 4))
        o.watch(POLL, entry(POLL, 0))
        def dma(ee):
            # 00200890's own 00200830 is compared through its leaves: the
            # native traces the loader's 00200830 worker calls only.
            if not PACKET <= (ee.r[31] & MASK) < PACKET + SIZES[PACKET]:
                entry(DMA, 1)(ee)
        o.watch(DMA, dma)
        o.watch(CHANNEL, entry(CHANNEL, 1))

        def ready(ee):
            entry(READY, 1)(ee); ee.r[2] = 2

        def cdread(ee):
            lsn, sectors, buf, mode = (ee.r[4 + i] & MASK for i in range(4))
            m = ee.read(mode, 3)
            self.expected.append((CDREAD, lsn, sectors, buf, m[0] | m[1] << 8 | m[2] << 16, ee.snapshot()))
            assert not (d.measured and d.field < d.busy_until), 'read while busy'
            d.read(ee, lsn, sectors, buf)
            ee.r[2] = 1

        def sync(ee):
            entry(SYNC, 1)(ee); ee.r[2] = int(d.field < d.busy_until)

        def error(ee):
            entry(ERROR, 0)(ee); ee.r[2] = 0

        def wait(ee):
            entry(WAIT, 3)(ee); ee.r[2] = 0

        def send(ee):
            entry(SEND, 2)(ee)
            chain = ee.r[5] & MASK
            for buf, size in reversed(d.delivered):
                if buf <= chain < buf + size:
                    n = buf + size - chain
                    self.sent_o.append((chain, n, hashlib.sha256(ee.read(chain, n)).hexdigest()))
                    return
            raise AssertionError(('DMA of bytes the drive did not deliver', hex(chain)))

        def unreached(name):
            def fn(ee):
                raise AssertionError((name, 'reached: not on the modelled path'))
            return fn
        self.o_bank_calls = 0

        def bank(ee):
            """001FB370 (original side): the same script."""
            entry(0x1FB370, 1)(ee)
            k = self.o_bank_calls
            self.o_bank_calls += 1
            ee.r[2] = self.bank_script[k] if k < len(self.bank_script) else 0

        def flush(ee):
            ee.r[2] = 0

        for a, f in ((READY, ready), (CDREAD, cdread), (SYNC, sync), (ERROR, error), (WAIT, wait),
                     (SEND, send), (0x1FB370, bank), (0x10BAA0, flush),
                     (0x200360, unreached('00200360'))):
            o.hook(a, f)
        o.watch(0x2009E0, entry(0x2009E0, 2))

    # -- one frame
    def request(self, module):
        o = self.o
        o.r[29] = 0x7F0F0000
        before = len(o.stores)
        o.call(0x1FF080, (0, module))
        assert self.lib.em_module_loader_request_001FF080(self.ml, 0, module) == 0
        for a, n, _ in o.stores[before:]:
            assert set(range(a, a + n)) <= MODELLED | set(range(SLOT + 4, SLOT + 8)), ('request store', hex(a))
        assert o.load(SLOT + 4) == 0x1FF0D0
        assert self.n_snapshot() == o.snapshot(), ('request', first_diff(self.n_snapshot(), o.snapshot()))

    def frame(self, label, expect_fault=None, gate=0):
        o, lib = self.o, self.lib
        self.expected.clear(); self.actual.clear()
        # D_00282157 for this frame, on both sides (the stream lanes' byte).
        o.save(GATE, gate, 1)
        self.gate.value = gate
        before = len(o.stores)
        # 001AB6A0 for slot 2: promote 1/4 to 2, park the record, call.
        if o.load(SLOT, 1) in (1, 4):
            o.save(SLOT, 2, 1)
        if o.load(SLOT, 1) == 2:
            o.save(SPAD_SLOT, SLOT)
            o.r[29] = 0x7F0F0000
            o.stores = o.stores[:before]  # the dispatcher's own stores are the harness's
            o.call(0x1FF0D0)
        lib.em_task_dispatch()
        fault = Fault()
        if lib.em_module_loader_failed(self.ml, C.byref(fault)):
            # Fail-stop where the translation's modelled table ends: the
            # original writes the named global at that address instead.
            assert expect_fault == (fault.address, fault.code), (label, hex(fault.address), fault.code)
            assert any(a == fault.address and n == 4 and not h for a, n, h in o.stores[before:]), \
                (label, 'the original did not write the faulting address')
            return 'fault'
        assert [e[:5] for e in self.actual] == [e[:5] for e in self.expected], \
            (label, [tuple(map(hex, e[:5])) for e in self.actual], [tuple(map(hex, e[:5])) for e in self.expected])
        for n, e in zip(self.actual, self.expected):
            assert n[5] == e[5], (label, 'memory at the entry of', hex(e[0]), first_diff(n[5], e[5]))
        assert self.n_snapshot() == o.snapshot(), (label, 'after the frame', first_diff(self.n_snapshot(), o.snapshot()))
        self.check_stores(label, o.stores[before:])
        self.drive.field += 1
        lib.em_module_loader_field(self.ml)

    def check_stores(self, label, stores):
        for a, n, hooked in stores:
            span = set(range(a, a + n))
            if hooked:
                assert any(b <= a and a + n <= b + s for b, s in self.drive.delivered), (label, 'drive store', hex(a))
            else:
                assert span <= MODELLED or span <= self.extra_modelled or \
                    all(x in MODELLED or x in self.extra_modelled for x in span), \
                    (label, 'original store outside the model', hex(a), n)

    def slot_state(self):
        return self.o.load(SLOT, 1)

    def request_area(self):
        """001FF080(1, 0): the area load of D_00810700 / D_00810701."""
        o = self.o
        o.r[29] = 0x7F0F0000
        o.call(0x1FF080, (1, 0))
        assert self.lib.em_module_loader_request_001FF080(self.ml, 1, 0) == 0
        assert self.n_snapshot() == o.snapshot(), ('area request', first_diff(self.n_snapshot(), o.snapshot()))

    def area_bytes_equal(self, label):
        for a, v in self.area.items():
            assert v.value == self.o.load(a, 1), (label, 'area byte', hex(a), v.value, self.o.load(a, 1))

    def run(self, module, limit=200, expect_fault=None, gates=None, area=False, stop=None):
        """Returns the per-frame snapshots from the request frame on (None
        when the expected fail-stop fault ended the load). gates: {frame:
        D_00282157 value} (default 0, as in both captured waits). area:
        001FF080(1, 0) instead of a module."""
        if area:
            self.request_area()
            # 001FFCD0's own stores: the area latches (D_00810701 cleared,
            # D_00810703 / D_00810704), and 002009E0's bss clear after the
            # overlay file (its word +0x14 bytes at the file's end).
            self.extra_modelled = set(AREA_BYTES[1:4])
        else:
            self.request(module)
        rows = []
        for f in range(limit):
            if area and f == 1:
                # The overlay file landed at frame 0's read (the drive
                # writes at issue): its header names the bss 002009E0 clears.
                self.bss = self.overlay_bss()
                self.extra_modelled |= set(range(*self.bss))
            if self.frame((module, f), expect_fault, (gates or {}).get(f, 0)) == 'fault':
                return None
            rows.append(self.o.snapshot())
            if area:
                self.area_bytes_equal((module, f))
            if self.slot_state() == 0 or (stop and stop(self.o)):
                break
        else:
            raise AssertionError((module, 'load did not finish'))
        assert self.sent_n == self.sent_o, (module, self.sent_n, self.sent_o)
        delivered = self.drive.delivered
        for k, (buf, size) in enumerate(delivered):
            # A later read over the same bytes replaced them (both sides).
            if any(b < buf + size and buf < b + n for b, n in delivered[k + 1:]):
                continue
            got = self.lib.em_module_loader_memory(self.ml, buf, size)
            assert got and C.string_at(got, size) == self.o.read(buf, size), (module, 'delivered bytes', hex(buf))
        return rows

    bss = None

    def overlay_bss(self):
        """[start, end) of the overlay's bss 002009E0 clears (D_00275304[0]
        + the file size, the overlay header's word +0x14 long)."""
        o = self.o
        area = o.load(0x810700, 1)
        p, size = o.load(0x275304), o.load(0x28A3C4 + 8 * area)
        return p + size, p + size + o.load(p + 0x14)

    def close(self):
        self.lib.em_module_loader_bind_live(None)
        self.lib.em_module_loader_close(self.ml)


def first_diff(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return i, x, y
    return len(a), len(b)


def slot_fields(snap):
    """(+0, +8..+0x1F, BD8) of a snapshot."""
    return snap[:25] + snap[25:26]


def capture_rows_h7():
    rows = [json.loads(line) for line in H7.read_text().splitlines()]
    out = {}
    for r in rows:
        s = bytes.fromhex(r['slot2'])
        out[r['f']] = s[:1] + s[8:32] + bytes([r['bd8']])
    return out


def capture_rows_01():
    rows = json.loads(PROBE01.read_text())['frame_rows']
    out = {}
    for r in rows:
        s = r['slot2']
        out[r['f']] = (s['state'], s['s8'], s['s9'], s['sA'], s['sB'], s['module'], s['kind'],
                       s['count14'], s['index16'], r['bd8'])
    return out


def fields01(snap):
    u = snap[1:25]
    return (snap[0], u[0], u[1], u[2], u[3], u[6], u[7], u[12] | u[13] << 8, u[14] | u[15] << 8, snap[25])


CODE_RANGE = (0x100000, 0x240000)  # every routine the whole loads execute lies inside


def assert_code_is_elf(elf, ram, label):
    """The whole loads run the code bytes of the captured RAM: they must be
    the pinned ELF's own bytes."""
    lo, hi = CODE_RANGE
    phoff = struct.unpack_from('<I', elf, 28)[0]
    size, count = struct.unpack_from('<HH', elf, 42)
    for i in range(count):
        kind, off, va, _, filesz, *_ = struct.unpack_from('<8I', elf, phoff + i * size)
        if kind == 1 and va <= lo and hi <= va + filesz:
            assert ram[lo:hi] == elf[off + lo - va:off + hi - va], (label, 'captured code differs from the ELF')
            return hi - lo
    raise AssertionError('no ELF segment covers the code range')


def check_whole_loads(elf, lib, pack):
    ram = (ROUTE / '03_panel_power/eeMemory.bin').read_bytes()
    spad = (ROUTE / '03_panel_power/scratchpad.bin').read_bytes()
    assert_code_is_elf(elf, ram, 'route 03')
    h7 = capture_rows_h7()
    # Host speed: the loader's steps, no drive time.
    w = WholeLoad(elf, lib, ram, spad, pack)
    host = w.run(0x21)
    dispatches, reads, unmeasured = C.c_uint32(), C.c_uint32(), C.c_uint32()
    lib.em_module_loader_counts(w.ml, C.byref(dispatches), C.byref(reads), C.byref(unmeasured))
    w.close()
    assert (dispatches.value, reads.value, unmeasured.value) == (10, 3, 0), (dispatches.value, reads.value)
    assert len(host) == 10, len(host)
    captured = [h7[f] for f in range(391, 415)]
    distinct = [r for i, r in enumerate(captured) if i == 0 or r != captured[i - 1]]
    busy_rows = len(captured) - len(distinct)
    assert busy_rows == 14 and [slot_fields(s) for s in host] == distinct, \
        ('host rows are not the captured rows without the busy polls', busy_rows)
    assert h7[390][:1] == b'\x00' and h7[390][25] == 1, 'f390: slot idle and BD8 raised before the request'
    # Measured drive: the captured 24 frames, row for row (route 03).
    w = WholeLoad(elf, lib, ram, spad, pack, measured=True)
    measured = w.run(0x21)
    lib.em_module_loader_counts(w.ml, C.byref(dispatches), C.byref(reads), C.byref(unmeasured))
    w.close()
    assert (dispatches.value, reads.value) == (24, 3), (dispatches.value, reads.value)
    unmeasured = unmeasured.value
    assert [slot_fields(s) for s in measured] == captured, 'measured rows differ from route 03 (h7)'
    assert unmeasured == 0, unmeasured
    # Route 01 (the battery pop-up), its own capture and pre-load RAM.
    ram01 = (ROUTE / '01_battery/eeMemory.bin').read_bytes()
    spad01 = (ROUTE / '01_battery/scratchpad.bin').read_bytes()
    assert_code_is_elf(elf, ram01, 'route 01')
    p01 = capture_rows_01()
    w = WholeLoad(elf, lib, ram01, spad01, pack, measured=True)
    r01 = w.run(0x21)
    w.close()
    cap01 = [p01[f] for f in range(194, 218)]
    assert [fields01(s) for s in r01] == cap01, 'measured rows differ from route 01'
    w = WholeLoad(elf, lib, ram01, spad01, pack)
    host01 = w.run(0x21)
    w.close()
    kept = [r for i, r in enumerate(cap01) if i == 0 or r != cap01[i - 1]]
    dropped = [194 + i for i, r in enumerate(cap01) if i and r == cap01[i - 1]]
    assert [fields01(s) for s in host01] == kept and \
        dropped == list(range(195, 201)) + list(range(203, 211)), ('route 01 host rows', dropped)
    return len(host), len(measured), len(r01), busy_rows


def check_pinned_loads(elf, lib, pack, disc_pack):
    """Pinned whole-load cases over route 03 (each kills a named mutant):
    a non-zero D_00282157 on some frames; a relocated pack (other disc
    positions, every read starting inside a wider range, D_0028A748 unlike
    D_0028A744) under the measured drive; the disc-only pack."""
    ram = (ROUTE / '03_panel_power/eeMemory.bin').read_bytes()
    spad = (ROUTE / '03_panel_power/scratchpad.bin').read_bytes()
    h7 = capture_rows_h7()
    captured = [h7[f] for f in range(391, 415)]
    distinct = [r for i, r in enumerate(captured) if i == 0 or r != captured[i - 1]]
    # 1. The gate: 001FF0D0 does nothing while D_00282157 != 0.
    gates = {0: 1, 3: 2, 4: 0x80}
    w = WholeLoad(elf, lib, ram, spad, pack)
    rows = w.run(0x21, gates=gates)
    w.close()
    assert len(rows) == 10 + len(gates), len(rows)
    assert [slot_fields(r) for f, r in enumerate(rows) if f not in gates] == distinct, 'gated load rows'
    for f in gates:
        prev = rows[f - 1] if f else None
        assert prev is None or slot_fields(rows[f]) == slot_fields(prev), ('gated frame advanced', f)
    # 2. Relocated pack, measured drive: rows still the captured 24.
    seeds = {0x28A748: 0x1C00000}
    assert pack_globals(pack)[0x28A744] != seeds[0x28A748]
    rel = relocated_pack(pack, OUT / 'modules_relocated.emml', shift=0x40, before=3, after=2, seeds=seeds)
    w = WholeLoad(elf, lib, ram, spad, rel, measured=True, seed_from_pack=True, shift=0x40)
    rows = w.run(0x21)
    dispatches, reads, unmeasured = C.c_uint32(), C.c_uint32(), C.c_uint32()
    lib.em_module_loader_counts(w.ml, C.byref(dispatches), C.byref(reads), C.byref(unmeasured))
    dest = [b for b, _ in w.drive.delivered]
    w.close()
    assert [slot_fields(r) for r in rows] == captured and unmeasured.value == 0, 'relocated measured load'
    assert dest == [HEADER, 0x1C00000], [hex(b) for b in dest]
    # 3. The disc-only pack (no capture): the same host-speed load.
    w = WholeLoad(elf, lib, ram, spad, disc_pack, seed_from_pack=True)
    rows = w.run(0x21)
    w.close()
    assert [slot_fields(r) for r in rows] == distinct, 'disc-only pack load'
    return 3


def check_packet_after_load(elf, lib, full_pack):
    """00200890 on both sides after module 3's whole load: the packet words
    are the relocation slots 8..12 the load just wrote, pointing inside the
    payload the drive delivered, so each DMA starts inside a region."""
    ram = (ROUTE / '03_panel_power/eeMemory.bin').read_bytes()
    spad = (ROUTE / '03_panel_power/scratchpad.bin').read_bytes()
    w = WholeLoad(elf, lib, ram, spad, full_pack)
    try:
        assert w.run(3, limit=400) is not None
        o, st = w.o, lib.em_module_loader_state(w.ml).contents
        words = [st.d28A490[8 + k] for k in range(5)]
        assert words == [o.load(0x28A4B0 + 4 * k) for k in range(5)]
        sdk_ptr = lib.em_module_loader_sdk(w.ml)
        inside = 0
        for d707, c60 in itertools.product((0, 1, 2), (0, 1, 2)):
            w.expected.clear(); w.actual.clear(); w.sent_o.clear(); w.sent_n.clear()
            o.save(0x810707, d707, 1); o.save(0x810C60, c60, 1)
            o.r[29] = 0x7F0F0000
            o.call(PACKET)
            fault, chain = Fault(), C.c_uint32()
            assert lib.em_module_loader_packet_00200890(sdk_ptr, d707, c60, (C.c_uint32 * 5)(*words),
                                                        C.byref(chain), C.byref(fault)) == 0, fault.code
            leaves = (CHANNEL, WAIT, SEND)
            exp = [e for e in w.expected if e[0] in leaves]
            act = [e for e in w.actual if e[0] in leaves]
            assert [e[:5] for e in act] == [e[:5] for e in exp], ('00200890', d707, c60)
            assert all(a[5] == e[5] for a, e in zip(act, exp)), ('00200890 memory', d707, c60)
            assert w.sent_n == w.sent_o and len(w.sent_n) == 1, ('00200890 bytes', d707, c60)
            buf = max(b for b, n in w.drive.delivered if b <= chain.value < b + n)
            inside += chain.value != buf
        # D_0028A4B0 (slot 8) is the payload's first byte; the other four
        # packets start inside it (8 of the 9 selections).
        assert inside == 8 and words[0] == w.drive.delivered[-1][0], (inside, hex(words[0]))
    finally:
        w.close()
    return 9


def check_new_game_rows(rows):
    """The recorded drive's rows against the capture: every distinct
    loader state first appears at the capture's frame (relative to module
    3's first dispatch), except that the states after the sound-bank step
    come one frame earlier: the PS2 made 9 001FB370 calls where the port's
    host-speed SIF DMA makes 8 (IOP_STREAM.md "The sound-bank transfer")."""
    cap = new_game_transitions()
    first = {}
    for k, snap in enumerate(rows):
        u = snap[1:25]
        first.setdefault((snap[0], u[0], u[1], u[2], u[3], u[6]), k)
    # Each load from its own first dispatch (the slot-0 task's frames between
    # the two loads are not this test's).
    starts = {0: (2, 0, 1, 0, 0, 3), 1: (2, 1, 1, 0, 0, 0)}
    after_bank = False
    checked = 0
    for state, k in sorted(first.items(), key=lambda e: e[1]):
        if state not in cap or state[0] != 2:
            continue
        if state[:5] == (2, 1, 4, 2, 2):
            after_bank = True
        ref = starts[0 if state[5] == 3 else 1]
        want = cap[state] - cap[ref] - (1 if after_bank else 0)
        got = k - first[ref]
        assert got == want, ('New Game row', state, 'port frame', got, 'capture frame', cap[state] - cap[ref])
        checked += 1
    assert checked >= 15, checked


def check_new_game_loads(elf, lib, pack, measured=False):
    """The New Game's two loads through the loader, whole, on both sides
    over the route-03 RAM (which holds AREA11 and the pre-load cursors):
    001AD1A0's module 3 (kind 0 from D_0028A738), then 001ADF50's area load
    001FF080(1, 0) of AREA11: the ORIGINAL 001FF0D0 + 001FFCD0 + 001FF590 +
    00200780 / 00200730 / 00200830 / 00200890 + 002009E0 (its FlushCache
    hooked, its memset 00121A28 original) against em_module_loader with its
    area workers. 001FB370 is scripted alike on both sides (7 pending calls,
    then the bank's end, as test_sound_bank_reference shows the real chain
    does); the drive is the model of the pack under test. Compared as for
    the module loads: every callee entry with the modelled memory, the
    snapshot after every frame, every original store (the area latches and
    the overlay's bss clear included), every delivered byte and every DMA
    send (the area's texture upload and the player's packet), and the area
    bytes after every frame. Returns (module-3 dispatches, area dispatches)."""
    ram = (ROUTE / '03_panel_power/eeMemory.bin').read_bytes()
    spad = (ROUTE / '03_panel_power/scratchpad.bin').read_bytes()
    w = WholeLoad(elf, lib, ram, spad, pack, measured=measured)
    try:
        m3 = w.run(3, limit=800)
        assert m3 is not None
        st = lib.em_module_loader_state(w.ml).contents
        assert st.d28A490[0xAB] == 0x13351C0 == w.o.load(0x28A73C), hex(st.d28A490[0xAB])
        w.bank_script = [0] * 7 + [0x1335F40]
        sent_before = len(w.sent_o)
        area = w.run(0, limit=800, area=True)
        assert area is not None
        assert w.bank_calls == w.o_bank_calls == 8, (w.bank_calls, w.o_bank_calls)
        sends = w.sent_o[sent_before:]
        assert len(sends) == 2 and sends[0][0] == 0x1335F40 and sends[1][0] == w.o.load(0x28A4B0), \
            [(hex(c), n) for c, n, _ in sends]
        if measured:
            check_new_game_rows(m3 + area)
        lo, hi = w.bss
        got = lib.em_module_loader_memory(w.ml, lo, hi - lo)
        assert got and C.string_at(got, hi - lo) == w.o.read(lo, hi - lo) == bytes(hi - lo), 'bss clear'
        for a, v in ((0x28A73C, 0x1335F40), (0x28A740, 0x19A3F40), (0x28A744, 0x19A3F40),
                     (0x28A748, 0x19A3F40), (0x28A5A0, 0x1516F40)):
            assert w.o.load(a) == v, (hex(a), hex(w.o.load(a)))
    finally:
        w.close()
    # The 001FEF70 chaining (the d810CA4 / d810CA6 views): with D_00810CA6 = 1
    # the area load's last dispatch turns the record into module 0x32's load
    # (+8..+0xC cleared, +0xE = 0x32) on both sides; compared up to there.
    w = WholeLoad(elf, lib, ram, spad, pack, measured=measured)
    try:
        assert w.run(3, limit=800) is not None
        w.o.save(0x810CA6, 1, 1)
        w.ca6.value = 1
        w.bank_script = [0] * 7 + [0x1335F40]
        chained = w.run(0, limit=800, area=True,
                        stop=lambda o: o.load(SLOT + 8, 1) == 0 and o.load(SLOT + 0xE, 1) == 0x32)
        assert chained is not None and w.o.load(SLOT + 0xE, 1) == 0x32 and w.slot_state() == 2
    finally:
        w.close()
    return len(m3), len(area)


# ============================================ B': other modules (full) ====

def module_header(disc, m):
    return disc.sectors(disc.index[0] + m, 1)


def out_of_table(h):
    """(address, code) of the native fail-stop for the first relocation slot
    past the slot table (slot 0xB0 on: the task table D_0028A750), or None."""
    a, b, c = struct.unpack_from('<H', h, 0xE)[0], struct.unpack_from('<I', h, 0x10)[0], \
        struct.unpack_from('<I', h, 0x1C)[0]
    for k in range(c):
        idx = struct.unpack_from('<I', h, 0x20 + 8 * (a + b) + 4 * k)[0] >> 24
        if idx >= RELOC_WORDS:
            return (RELOC + 4 * idx, 4)
    return None


def eligible_modules():
    """Module ids 0..0x37 whose INDEX.IDX header names itself (word 0), other
    than 0x21 (checked above) and the kind-3 ids 0x32..0x35 (they reach the
    untranslated 001FB370), whose data fits the captured buffers."""
    import export_module_loader as X
    disc = X.Disc(ISO)
    out = []
    for m in range(0x38):
        if m in (0x21, 0x32, 0x33, 0x34, 0x35):
            continue
        h = module_header(disc, m)
        total = struct.unpack_from('<I', h, 8)[0]
        if struct.unpack_from('<I', h, 0)[0] == m and total <= 0x600000:
            out.append(m)
    return out


def build_full_pack(modules):
    path = OUT / 'modules_full.emml'
    subprocess.run([sys.executable, str(ROOT / 'tools/export_module_loader.py'), '--out', str(path),
                    '--capture', str(ROUTE / '03_panel_power'),
                    '--modules', ','.join(hex(m) for m in modules)], check=True, capture_output=True)
    return path


def check_other_modules(elf, lib):
    """Whole loads of real module headers at host speed. Since the slot
    table is modelled whole (0xB0 words, its cursors among them), every
    real header loads through: the 14 that stopped at the old 68-word model
    (slots such as 0x44 = D_0028A5A0, 0x86, 0x87, up to 0x9A) now write the
    same words as the original. A slot past the table would still fail-stop
    at the address the original writes."""
    import export_module_loader as X
    disc = X.Disc(ISO)
    ram = (ROUTE / '03_panel_power/eeMemory.bin').read_bytes()
    spad = (ROUTE / '03_panel_power/scratchpad.bin').read_bytes()
    modules = eligible_modules() if FULL else [0x1F, 0x03, 0x04]
    pack = build_full_pack(sorted(set(modules) | {0x2B}))
    done, stopped = [], []
    for m in modules:
        expect = out_of_table(module_header(disc, m))
        w = WholeLoad(elf, lib, ram, spad, pack)
        try:
            rows = w.run(m, limit=400, expect_fault=expect)
        finally:
            w.close()
        if rows is None:
            stopped.append((m, expect[0]))
        else:
            assert expect is None, (hex(m), 'expected a fail-stop')
            done.append(m)
    # Pinned: 0x2B's destination follows spad 0x70003B90 (0: the fixed
    # buffer 0x1800000, else D_0028A748), read through the spad3B90 view.
    for value, dest in ((0, 0x1800000), (1, 0x19A3F40), (2, 0x19A3F40)):
        w = WholeLoad(elf, lib, ram, spad, pack, spad3b90=value)
        try:
            assert w.run(0x2B, limit=400) is not None
            got = [b for b, _ in w.drive.delivered]
        finally:
            w.close()
        assert got == [HEADER, dest], (value, [hex(b) for b in got])
    assert not stopped, ('a real module header names a slot past D_0028A490[0xAF]', stopped)
    return done, stopped


# ================================================== C: capture facts ======

def check_upload_is_atlas(pack):
    """The module-0x21 chunk (what 001FF3F0 DMAs) is one GS transfer that
    equals the captured BATTERY page's GS memory, and every TEX0 of the
    port's BATTERY atlas (the ELF page tables D_00265C50 / D_00265CD0 and
    export_panel's two extra words) has TBP0 and CBP inside it."""
    sys.path.insert(0, str(DECOMP / 'tools'))
    import export_level as el
    from gs_vram import read_localmem
    chunk = load_pack(OUT / 'modules.emml')[(0x801D8 + (0xE4BC000 >> 11), 161)][:0x50800]
    lm, log = bytearray(4 * 1024 * 1024), []
    el._bg_gs_upload(el._bg_section_chain(chunk), lm, log)
    assert len(log) == 1, log
    dbp, dbw, dx, dy, w, h = log[0]
    est = el._load('_est_bg_up', 'extract_subtextures.py')
    _, cap = read_localmem(PANEL_GS)
    words = 0
    for y in range(dy, dy + h):
        for x in range(dx, dx + w):
            a = (dbp * 256 + est.psmct32_word(x, y, dbw) * 4) & 0x3FFFFF
            assert lm[a:a + 4] == cap[a:a + 4], ('upload differs from the panel capture', x, y)
            words += 1
    ee = LoaderEE(read_elf_bytes())
    toks = [ee.load(0x265C50 + 8 * i, 8) for i in range(16)] + [ee.load(0x265CD0 + 8 * i, 8) for i in range(9)]
    toks += [0x20042D05A1322000, 0x20043C859D422150]
    blocks = range(dbp, dbp + w * h // 64)
    for t in toks:
        assert (t & 0x3FFF) in blocks and ((t >> 37) & 0x3FFF) in blocks, hex(t)
    return words, len(toks)


def check_wait_facts():
    """The rows the host-speed load drops change nothing but the frame
    counters (route 03 trace), and each costs two rand() calls from
    001D7C30 (route 01 rand capture, the pop-up's wait f194..f217)."""
    rows = json.loads((ROUTE / '03_panel_power/trace.json').read_text())['rows']
    frozen = {'counter', 'f'}
    for i in range(392, 415):
        changed = {k for k in rows[i] if rows[i][k] != rows[i - 1][k]} - frozen
        assert not changed, (i, changed)
    rng = [json.loads(line) for line in RNG01.read_text().splitlines()]
    per = {}
    for r in rng:
        if 194 <= r['f'] <= 217:
            per.setdefault(r['f'], []).append(r['ra'])
    assert sorted(per) == list(range(194, 218)), sorted(per)
    for f, ras in per.items():
        assert sorted(ras) == ['1d7d44', '1d7dd4'], (f, ras)
    return 23, 2


def read_elf_bytes():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256, 'wrong original executable'
    return elf


def check_faults(lib, pack):
    """Fail-stop: a read the pack does not hold, a chain the drive did not
    deliver, a missing consumer, and the untranslated streamers."""
    n = 0
    lib.em_task_init()
    ml = lib.em_module_loader_open(str(pack).encode())
    lib.em_module_loader_bind_live(ml)
    assert lib.em_module_loader_request_001FF080(ml, 0, 0x1F) == 0
    lib.em_task_dispatch()  # header of 0x1F: not in the default pack
    fault = Fault()
    assert lib.em_module_loader_failed(ml, C.byref(fault)) and fault.address == 0x112440 and fault.code == 4
    lib.em_module_loader_close(ml); n += 1
    for state, callee in ((1, 0x1FFCD0), (2, 0x200360)):
        lib.em_task_init()
        ml = lib.em_module_loader_open(str(pack).encode())
        lib.em_module_loader_bind_live(ml)
        assert lib.em_module_loader_request_001FF080(ml, state, 0) == 0
        lib.em_task_dispatch()
        assert lib.em_module_loader_failed(ml, C.byref(fault)) and fault.address == callee and fault.code == 1
        lib.em_module_loader_close(ml); n += 1
    # No consumer: the 0x21 chunk's DMA faults at 00101F08.
    lib.em_task_init()
    ml = lib.em_module_loader_open(str(pack).encode())
    lib.em_module_loader_bind_live(ml)
    assert lib.em_module_loader_request_001FF080(ml, 0, 0x21) == 0
    for _ in range(10):
        lib.em_task_dispatch()
    assert lib.em_module_loader_failed(ml, C.byref(fault)) and fault.address == 0x101F08 and fault.code == 1
    lib.em_module_loader_close(ml); n += 1
    # Unbound mid-load: the running slot-2 task latches the orphan fault.
    lib.em_task_init()
    ml = lib.em_module_loader_open(str(pack).encode())
    lib.em_module_loader_bind_live(ml)
    assert lib.em_module_loader_request_001FF080(ml, 0, 0x21) == 0
    assert not lib.em_module_loader_orphaned(C.byref(fault))
    lib.em_module_loader_bind_live(None)
    lib.em_task_dispatch()
    assert lib.em_module_loader_orphaned(C.byref(fault)) and (fault.address, fault.code) == (0x1FF0D0, 1)
    lib.em_module_loader_bind_live(ml)  # a new binding clears it
    assert not lib.em_module_loader_orphaned(None)
    lib.em_module_loader_bind_live(None)
    lib.em_module_loader_close(ml); n += 1
    # Unbound: the request refuses.
    assert lib.em_module_loader_request_001FF080(None, 0, 0x21) == -1
    assert lib.em_module_loader_open(b'/nonexistent/modules.emml') is None
    return n + 2


# ============================================ D: sanitizer run (native) ====

ASAN_DRIVER = r"""
#include <stdio.h>
#include <string.h>
#include "game/em_module_loader.h"
static int sent;
static unsigned sum;
static int hook(void *ctx, uint32_t chain, const uint8_t *bytes, uint32_t size)
{
    (void)ctx; (void)chain;
    for (uint32_t i = 0; i < size; ++i) /* every byte handed over is readable */
        sum += bytes[i];
    ++sent;
    return 0;
}
static int load(const char *pack, int drive, uint8_t module, uint32_t frames_max)
{
    em_task_init();
    EmModuleLoader *ml = em_module_loader_open(pack);
    if (!ml) return -1;
    uint8_t bd8 = 1;
    EmModuleLoaderViews v = {.d275BD8 = &bd8};
    em_module_loader_set_views(ml, &v);
    em_module_loader_set_chain_hook(ml, hook, NULL);
    em_module_loader_set_drive(ml, drive);
    em_module_loader_bind_live(ml);
    if (em_module_loader_request_001FF080(ml, 0, module) != 0) return -2;
    uint32_t f = 0;
    for (; f < frames_max && bd8; ++f) {
        em_module_loader_field(ml);
        em_task_dispatch();
    }
    int failed = em_module_loader_failed(ml, NULL);
    uint8_t snap[EM_MODULE_LOADER_SNAPSHOT_SIZE];
    em_module_loader_snapshot(ml, NULL, snap);
    em_module_loader_close(ml);
    return failed ? -3 : (int)f;
}
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    int host = load(argv[1], EM_MODULE_LOADER_DRIVE_HOST, 0x21, 100);
    int measured = load(argv[1], EM_MODULE_LOADER_DRIVE_MEASURED, 0x21, 100);
    int missing = load(argv[1], EM_MODULE_LOADER_DRIVE_HOST, 0x1F, 100);
    int full = load(argv[2], EM_MODULE_LOADER_DRIVE_HOST, 0x03, 100);
    printf("%d %d %d %d %d\n", host, measured, missing, full, sent);
    return 0;
}
"""


def check_sanitized(pack, full_pack):
    """The native module under ASan/UBSan: a host and a measured 0x21 load,
    a missing header (fail-stop) and module 3 (relocations, a 2.3 MiB
    payload) from the other-modules pack."""
    src = OUT / 'asan_driver.c'
    exe = OUT / 'asan_driver'
    src.write_text(ASAN_DRIVER)
    subprocess.run(['cc', '-std=c11', '-g', '-O1', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-Isrc', str(src), 'src/game/em_module_loader.c',
                    'src/game/em_status_scene_original.c', 'src/game/em_task.c', '-o', str(exe)],
                   cwd=ROOT, check=True)
    out = subprocess.run([str(exe), str(pack), str(full_pack)], check=True, capture_output=True,
                         text=True).stdout.split()
    host, measured, missing, full, sent = map(int, out)
    assert (host, measured, missing) == (10, 24, -3) and full > 0 and sent >= 2, out
    return host, measured, full


# ================================================================ main ====

def main():
    started = time.time()
    elf = read_elf_bytes()
    lib = build()
    pack = OUT / 'modules.emml'
    subprocess.run([sys.executable, str(ROOT / 'tools/export_module_loader.py'), '--out', str(pack),
                    '--capture', str(ROUTE / '03_panel_power')], check=True, capture_output=True)
    # The disc-only export (what an end user runs): the same pack but for
    # the D_00275C74 seed, which every bank load writes before reading.
    disc_pack = OUT / 'modules_disc.emml'
    subprocess.run([sys.executable, str(ROOT / 'tools/export_module_loader.py'), '--out', str(disc_pack)],
                   check=True, capture_output=True)
    a, b = pack.read_bytes(), disc_pack.read_bytes()
    assert len(a) == len(b) and [i for i in range(len(a)) if a[i] != b[i]] == [0x24, 0x25, 0x26], \
        'disc-only pack differs beyond the D_00275C74 seed'
    assert struct.unpack_from('<I', b, 0x24)[0] == 0x10E99C0
    counts = {}
    counts['callee_sets'] = check_callees(elf)
    counts['read_00200780'] = check_read(elf, lib)
    counts['poll_00200730'] = check_poll(elf, lib)
    counts['dma_00200830'] = check_dma(elf, lib)
    counts['packet_00200890_restore_00200970'] = check_packet(elf, lib)
    host, measured, r01, busy = check_whole_loads(elf, lib, pack)
    counts['pinned_whole_loads'] = check_pinned_loads(elf, lib, pack, disc_pack)
    done, stopped = check_other_modules(elf, lib)
    counts['packet_after_module3'] = check_packet_after_load(elf, lib, OUT / 'modules_full.emml')
    ng_host = check_new_game_loads(elf, lib, pack)
    ng_measured = check_new_game_loads(elf, lib, pack, measured=True)
    words, toks = check_upload_is_atlas(pack)
    wait_rows, rands = check_wait_facts()
    counts['fault_cases'] = check_faults(lib, pack)
    sanitized = check_sanitized(pack, OUT / 'modules_full.emml')
    report = dict(status='PASS', mode='full' if FULL else 'quick', elf_sha256=ELF_SHA256, cases=counts,
                  host_dispatches=host, measured_dispatches_route03=measured,
                  measured_dispatches_route01=r01, busy_poll_rows=busy,
                  other_modules_loaded=[hex(m) for m in done],
                  other_modules_fail_stop=[(hex(m), hex(a)) for m, a in stopped],
                  upload_words_equal_panel_capture=words, atlas_tex0_inside_upload=toks,
                  wait_rows_frozen=wait_rows, rand_calls_per_wait_frame=rands,
                  sanitized_dispatches=sanitized, seconds=round(time.time() - started, 1))
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    banner(f"{counts['read_00200780']} 00200780 + {counts['poll_00200730']} 00200730 + "
           f"{counts['dma_00200830']} 00200830 + {counts['packet_00200890_restore_00200970']} "
           f"00200890/00200970 cases",
           f"module 0x21: host {host} dispatches, measured {measured} (route 03) / {r01} (route 01)",
           f"{len(done)} other whole loads (none past the slot table)",
           f"pinned: {counts['pinned_whole_loads']} whole loads (gate, relocated pack, disc-only pack), "
           f"3 module-0x2B spad loads, {counts['packet_after_module3']} 00200890 after module 3",
           f"New Game at host speed: module 3 in {ng_host[0]} dispatches, AREA11 in {ng_host[1]} "
           f"(001FFCD0 with 8 001FB370 calls); with the recorded drive {ng_measured[0]} and {ng_measured[1]} "
           f"(the capture's reads {NEW_GAME_READS()})")
    print(f"Module loader: PASS; host-speed rows = captured rows minus {busy} busy polls; "
          f"upload = panel GS capture ({words} words, {toks} atlas TEX0 inside) "
          f"({time.time() - started:.1f} s)")


if __name__ == '__main__':
    main()
