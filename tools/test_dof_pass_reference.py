#!/usr/bin/env python3
"""001DDE10's depth-of-field pass against the original's GS memory.

docs/CHAIN_PAGE.md section 6.2, docs/GS_EXACT.md sections 3.4 and 7. The
decomp's fork capture (../Extermination/build/dof_capture, its manifest.json;
the project's PCSX2 fork, software renderer; generated locally from the
user's own disc, never committed, nothing of it embedded here) cut the
original's DMA chain at the pass's two boundaries in 7 frames (5 of the
opening cutscene, 2 of AREA11 play) plus a repeat run of one, and kept for
each: the GS local memory exactly before and after the pass, the GS
registers at its start (the last value of each one frame T's stream wrote:
analysis.json), the pass's GIF bytes (from the EE chain and from the GS
dump), and the EE RAM and scratchpad after 001DDE10 ran.

A. The model. The captured GIF bytes through the CPU GS model (em_gs_raster,
   strict, as the Original profile runs it) from the BEFORE memory and
   registers. Its memory against the AFTER memory: the field (512 x 224 at
   the slot's FBP), the copy buffer (256 x 256 at 0x258000), the Z buffer
   (Z24 at the slot's ZBP: untouched, equal to BEFORE and AFTER) and every
   other word. Per blend k: its Z and alpha and the field pixels its Z test
   passes; the field's exact pixels by how many blends reached them. Again
   through 3 row-band EmGs in lockstep (EM_TEST_FULL: 8), as the workers
   draw.
B. The port's packets. em_render_context_001DDE10 over the captured EE
   image with the live binding's workers (tests/dof_pass_reference_bridge.c),
   from the state before it ran: the channel-3 cursor back at the pass's
   CALL target; the cutscene frames recompute their pairs from the captured
   point (0022EBE0 != 0 snaps them); every eased value (the pairs in play,
   D_00275690 and D_00275694) moved once before the capture point, so its
   value before that step is found by inverting it with the port's EE float
   model (each candidate checked by running the translation; most had
   converged: the four depths of each play frame and D_00275690 in the
   first wide shot had not). Every EE and scratchpad byte the
   run leaves must equal the capture's (the packets, the pairs at +0x24F0,
   +0x2450..+0x2467, D_00275690 / D_00275694, the cursor), its 001CB760 the
   slot block's CALL. The DMA walk of what it built (bank A's environment
   REF transferred, VIF NOP / FLUSH dropped, DIRECT data kept) must equal the
   captured stream byte for byte, from the EE chain and from the GS dump.
C. The Original profile's path over the port-built image: em_chain_page's
   pass mode (8 sprites, 32 DIRECT packets, marks 1, 3, 5, 7, 8), then
   em_gs_world (em_gs_world_page_pass: the field declared, each primitive
   with its environment, the kick's environment packet again at each mark;
   the head is bank A's block) from the BEFORE memory and registers
   (em_gs_world_memory_restore: the fields, the Z buffer and the copy
   buffer as drawn, the rest as uploads, as in the game), with 1 and 3
   workers (EM_TEST_FULL: 1, 2, 3, 8), and with 3 (EM_TEST_FULL: 3, 8)
   workers with every block counted as an upload (the workers' ordering
   must not depend on residency): every word equal to the AFTER memory.
D. The capture's own repeat (EM_TEST_FULL): open_w0300_run1 equal to
   open_w0300 (memory both sides, streams).

Quick (default): open_w0300 (cutscene, four blends, half-line field FBP
0x38), open_wide_t0720 (whole-line field FBP 0, no blend passes its Z
test: the copy alone), play_first_control (play, alphas 24..72).
EM_TEST_FULL=1: every capture. Everything must be bit-exact: a difference
fails, with its counts.
"""
from __future__ import annotations

import ctypes as C
import hashlib
import json
import os
import struct
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from reference_mode import FULL, MODE  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
CAP = Path(os.environ.get('DOF_CAPTURE_ROOT', ROOT.parent / 'Extermination/build/dof_capture'))
OUT = ROOT / 'build/dof_pass_reference'
QUICK = ['open_w0300', 'open_wide_t0720', 'play_first_control']
REPEAT = ('open_w0300', 'open_w0300_run1')
MEM = 4 << 20
GS_REG = {'PRIM': 0x00, 'RGBAQ': 0x01, 'ST': 0x02, 'UV': 0x03, 'XYZF2': 0x04, 'XYZ2': 0x05, 'TEX0_1': 0x06,
          'TEX0_2': 0x07, 'CLAMP_1': 0x08, 'CLAMP_2': 0x09, 'FOG': 0x0A, 'XYZF3': 0x0C, 'XYZ3': 0x0D,
          'TEX1_1': 0x14, 'TEX1_2': 0x15, 'TEX2_1': 0x16, 'TEX2_2': 0x17, 'XYOFFSET_1': 0x18, 'XYOFFSET_2': 0x19,
          'PRMODECONT': 0x1A, 'PRMODE': 0x1B, 'TEXCLUT': 0x1C, 'SCANMSK': 0x22, 'MIPTBP1_1': 0x34,
          'MIPTBP1_2': 0x35, 'MIPTBP2_1': 0x36, 'MIPTBP2_2': 0x37, 'TEXA': 0x3B, 'FOGCOL': 0x3D, 'TEXFLUSH': 0x3F,
          'SCISSOR_1': 0x40, 'SCISSOR_2': 0x41, 'ALPHA_1': 0x42, 'ALPHA_2': 0x43, 'DIMX': 0x44, 'DTHE': 0x45,
          'COLCLAMP': 0x46, 'TEST_1': 0x47, 'TEST_2': 0x48, 'PABE': 0x49, 'FBA_1': 0x4A, 'FBA_2': 0x4B,
          'FRAME_1': 0x4C, 'FRAME_2': 0x4D, 'ZBUF_1': 0x4E, 'ZBUF_2': 0x4F}
VERTEX_REGS = {'PRIM', 'RGBAQ', 'ST', 'UV', 'XYZF2', 'XYZ2', 'XYZF3', 'XYZ3', 'FOG'}
# The state the pass reads before writing it (its first copy's TEX1_1, the
# FBA / PABE / SCANMSK the pass never writes before its first sprite): the
# capture must hold them (written in frame T's stream before the pass).
READ_BEFORE_WRITTEN = ('TEX1_1', 'FBA_1', 'PABE', 'SCANMSK')
SOURCES = ['tests/dof_pass_reference_bridge.c', 'src/gs/em_gs_raster.c', 'src/gs/em_gs_world.c',
           'src/gs/em_gs_frame.c', 'src/game/em_chain_page.c', 'src/game/em_render_context.c',
           'src/game/em_load_veil_particles.c', 'src/game/em_sdk_math_original.c',
           'src/game/em_player_stage_workers.c', 'src/game/em_player_equipment.c',
           'src/game/em_status_ui_leftovers.c', 'src/game/em_effect_original.c']

FAILS: list[str] = []


def fail(msg: str) -> None:
    FAILS.append(msg)
    print(f'  FAIL {msg}')


def u32(b, a: int) -> int:
    return struct.unpack_from('<I', b, a)[0]


def put32(b, a: int, v: int) -> None:
    struct.pack_into('<I', b, a, v & 0xFFFFFFFF)


def f32bits(x: float) -> int:
    return struct.unpack('<I', struct.pack('<f', x))[0]


# ---------------------------------------------------------------- the library
def build() -> C.CDLL:
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('libdofpass.dylib' if sys.platform == 'darwin' else 'libdofpass.so')
    # the main build's flags (Makefile CFLAGS: no floating-point contraction
    # option), so the model's binary32 arithmetic is compiled as the game's
    cmd = ['cc', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
           '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-Isrc', *SOURCES, '-lm', '-lpthread',
           '-o', str(lib)]
    # rebuilt when the command or any source or header under src/ changed
    digest = hashlib.sha256(' '.join(cmd).encode())
    for path in sorted(set(SOURCES) | {str(p.relative_to(ROOT)) for p in (ROOT / 'src').rglob('*.h')}):
        digest.update(path.encode() + b'\0' + (ROOT / path).read_bytes())
    stamp = lib.with_suffix('.sha256')
    if not lib.exists() or not stamp.exists() or stamp.read_text() != digest.hexdigest():
        subprocess.run(cmd, cwd=ROOT, check=True)
        stamp.write_text(digest.hexdigest())
    n = C.CDLL(str(lib))
    P = C.POINTER
    n.dof_map32.argtypes = [C.c_uint32, C.c_uint32, C.c_uint32, C.c_uint32, C.c_int, C.c_void_p]
    n.dof_replay.argtypes = [C.c_void_p, P(C.c_uint32), P(C.c_uint64), C.c_uint32, C.c_char_p, C.c_uint64,
                             C.c_uint32, P(C.c_uint64), C.c_char_p, C.c_uint32]
    n.dof_replay.restype = C.c_int
    n.dof_build.argtypes = [C.c_void_p, C.c_void_p, P(C.c_uint32)]
    n.dof_build.restype = C.c_int
    n.dof_ee.argtypes = [C.c_uint32, C.c_uint32, C.c_uint32]
    n.dof_ee.restype = C.c_uint32
    n.dof_live.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32, C.c_char_p, C.c_char_p,
                           P(C.c_uint32), P(C.c_uint64), C.c_uint32, C.c_uint32, C.c_void_p, P(C.c_uint32),
                           C.c_char_p, C.c_uint32]
    n.dof_live.restype = C.c_int
    return n


def map32(lib, bp: int, bw: int, w: int, h: int, z: int) -> np.ndarray:
    out = np.zeros(w * h, dtype=np.uint32)
    lib.dof_map32(bp, bw, w, h, z, out.ctypes.data)
    return out.reshape(h, w)


# ---------------------------------------------------------------- a capture
class Capture:
    def __init__(self, fid: str):
        self.fid = fid
        self.dir = CAP / fid
        self.analysis = json.loads((self.dir / 'analysis.json').read_text())
        self.capture = json.loads((self.dir / 'capture.json').read_text())
        self.before = np.fromfile(self.dir / 'before/vram.bin', dtype=np.uint32)
        self.after = np.fromfile(self.dir / 'after/vram.bin', dtype=np.uint32)
        self.gif = (self.dir / 'pass_gif_from_ee_chain.bin').read_bytes()
        self.gif_dump = (self.dir / 'pass_gif_from_gs_dump.bin').read_bytes()
        if self.before.size * 4 != MEM or self.after.size * 4 != MEM:
            raise SystemExit(f'{fid}: a GS memory image is not 4 MiB')
        regs = self.analysis['gs_regs_at_boundaries']['written_before_pass']
        self.regs = []
        for name, value in regs.items():
            if name in VERTEX_REGS or name not in GS_REG:
                continue
            self.regs.append((GS_REG[name], int(value, 16)))
        self.reg_names = set(regs)
        slot = self.capture['variants'][0]['slot_block']
        self.block, self.target = int(slot['B'], 16), int(slot['call_addr'], 16)

    def ee(self) -> tuple[bytearray, bytearray]:
        return bytearray((self.dir / 'ee_T.bin').read_bytes()), bytearray((self.dir / 'spr_T.bin').read_bytes())


def reg_arrays(regs):
    r = (C.c_uint32 * max(1, len(regs)))(*[a for a, _ in regs])
    v = (C.c_uint64 * max(1, len(regs)))(*[b for _, b in regs])
    return r, v, len(regs)


def field_of(ram, slot: int) -> tuple[int, int, int, int]:
    """FRAME_1 / ZBUF_1 / SCISSOR_1 of bank A's block of the slot (the draw
    environment 001D1F20 REFs: its A+D pairs at +0x20, +0x30, +0x50)."""
    gs = u32(ram, 0x275674)
    blk = gs + 0x20 + 0x190 * slot
    if (ram[blk + 0x28], ram[blk + 0x38], ram[blk + 0x58]) != (0x4C, 0x4E, 0x40):
        raise SystemExit(f'bank A block {slot} is not FRAME_1 / ZBUF_1 / SCISSOR_1 at +0x20 / +0x30 / +0x50')
    frame = u32(ram, blk + 0x20) | u32(ram, blk + 0x24) << 32
    zbuf = u32(ram, blk + 0x30) | u32(ram, blk + 0x34) << 32
    scissor = u32(ram, blk + 0x50) | u32(ram, blk + 0x54) << 32
    return gs + 0x20, frame, zbuf, scissor


# ---------------------------------------------------------------- GIF / DMA
def vif_direct(data: bytes) -> bytes:
    """The DIRECT data of a VIF1 transfer (NOP / FLUSHE / FLUSH / FLUSHA
    pass; any other code fails)."""
    out, at = bytearray(), 0
    while at + 4 <= len(data):
        code = u32(data, at)
        cmd, at = (code >> 24) & 0x7F, at + 4
        if cmd in (0x00, 0x10, 0x11, 0x13):
            continue
        if cmd not in (0x50, 0x51):
            raise ValueError(f'VIF code {code:#010x}')
        qw = (code & 0xFFFF) or 0x10000
        at = (at + 15) & ~15
        out += data[at:at + 16 * qw]
        at += 16 * qw
    return bytes(out)


def pass_stream(ram, target: int) -> bytes:
    """The pass's GIF bytes as the DMAC and VIF1 send them: the CALLed list
    from `target` to its RET (CNT and REF tags, REF data transferred)."""
    out, cur = bytearray(), target
    for _ in range(4096):
        lo, hi = u32(ram, cur), u32(ram, cur + 4)
        qwc, tid, addr = lo & 0xFFFF, (lo >> 28) & 7, hi & 0x7FFFFFFF
        if tid == 1:
            data, nxt = cur + 16, cur + 16 + 16 * qwc
        elif tid == 3:
            data, nxt = addr, cur + 16
        elif tid == 6:
            data, nxt = cur + 16, None
        else:
            raise ValueError(f'DMA tag {lo:#010x} at {cur:#x} in the pass')
        out += vif_direct(bytes(ram[data:data + 16 * qwc]))
        if nxt is None:
            return bytes(out)
        cur = nxt
    raise ValueError('the pass list does not return')


def blends(stream: bytes) -> list[tuple[int, int, int]]:
    """The blend sprites (PRIM 0x156) of the stream: (Z, alpha, OFY-free
    window Y0) of each, from its PACKED RGBAQ / XYZF2 (Z = the word's bits
    4..27, GS_EXACT.md 2.1)."""
    out, off = [], 0
    while off + 16 <= len(stream):
        lo, hi = struct.unpack_from('<QQ', stream, off)
        off += 16
        nloop, pre, prim, flg = lo & 0x7FFF, (lo >> 46) & 1, (lo >> 47) & 0x7FF, (lo >> 58) & 3
        nreg = (lo >> 60) or 16
        if flg != 0:
            off += 16 * nloop if flg == 2 else ((nloop * nreg + 1) // 2) * 16
            continue
        regs = [(hi >> (4 * r)) & 15 for r in range(nreg)]
        words = [struct.unpack_from('<QQ', stream, off + 16 * i) for i in range(nloop * nreg)]
        off += 16 * nloop * nreg
        if pre and prim == 0x156 and regs == [1, 3, 4, 3, 4]:
            alpha = (words[0][1] >> 32) & 0xFF
            z = (words[2][1] >> 4) & 0xFFFFFF
            out.append((z, alpha, words[2][0] & 0xFFFF))
    return out


# ---------------------------------------------------------------- A
def part_a(lib, cap: Capture, regions, report) -> None:
    F, K, Z = regions
    for name in READ_BEFORE_WRITTEN:
        if name not in cap.reg_names:
            fail(f'{cap.fid}: the capture does not hold {name} at the pass\'s start')
    r, v, n = reg_arrays(cap.regs)
    for bands in [1, 3] + ([8] if FULL else []):
        mem = (C.c_uint8 * MEM).from_buffer_copy(cap.before.tobytes())
        out, why = (C.c_uint64 * 6)(), C.create_string_buffer(128)
        lib.dof_replay(mem, r, v, n, cap.gif, len(cap.gif), bands, out, why, 128)
        model = np.frombuffer(bytes(mem), dtype=np.uint32)
        if out[0] != len(cap.gif) or out[1] or out[2] or out[3]:
            fail(f'{cap.fid}: the model took {out[0]} of {len(cap.gif)} bytes, refusals {out[1]:#x} '
                 f'({why.value.decode()}), refused {out[2]}, span faults {out[3]} ({bands} band(s))')
        exact = {}
        for name, m in (('field', F), ('copy', K)):
            exact[name] = int((model[m] == cap.after[m]).sum())
        z_model, z_after, z_before = model[Z] & 0xFFFFFF, cap.after[Z] & 0xFFFFFF, cap.before[Z] & 0xFFFFFF
        exact['z'] = int(((z_model == z_after) & (z_model == z_before)).sum())
        rest = np.ones(MEM // 4, dtype=bool)
        rest[F.ravel()] = rest[K.ravel()] = rest[Z.ravel()] = False
        exact['rest'] = int((model[rest] != cap.after[rest]).sum())
        if bands == 1:
            report['field'], report['copy'], report['z'], report['rest'] = (exact['field'], exact['copy'],
                                                                             exact['z'], exact['rest'])
            report['pixels'] = int(out[5])
        if exact['field'] != F.size or exact['copy'] != K.size or exact['z'] != Z.size or exact['rest']:
            fail(f'{cap.fid} ({bands} band(s)): field {exact["field"]}/{F.size}, copy {exact["copy"]}/{K.size}, '
                 f'Z untouched {exact["z"]}/{Z.size}, other words differing {exact["rest"]}')
        elif bands > 1:
            report.setdefault('bands', []).append(bands)
    # per blend: its Z test (GEQUAL against the Z buffer, which the pass never
    # writes) decides the pixels it blends; the field's pixels by blend count
    zbuf = (cap.before[Z] & 0xFFFFFF).astype(np.int64)
    bl = blends(cap.gif)
    if len(bl) != 4:
        fail(f'{cap.fid}: {len(bl)} blend sprites in the stream (4 expected)')
        return
    hits = [zk >= zbuf for zk, _a, _y in bl]
    count = sum(h.astype(np.int64) for h in hits)
    report['blends'] = [(zk, a, int(h.sum())) for (zk, a, _y), h in zip(bl, hits)]
    report['by_count'] = [int((count == c).sum()) for c in range(5)]
    changed = cap.after[F] != cap.before[F]
    report['changed'] = int(changed.sum())
    if bool((changed & (count == 0)).any()):
        fail(f'{cap.fid}: the original changed field pixels no blend\'s Z test passes')


# ---------------------------------------------------------------- B
CTX_WRITES = [(0x1C, 4), (0x2450, 0x18), (0x24F0, 0x24)]   # the context bytes 001DDE10 writes


def build_once(lib, ram, spad) -> tuple[int, list[int]]:
    out = (C.c_uint32 * 7)()
    rc = lib.dof_build((C.c_char * len(ram)).from_buffer(ram), (C.c_char * len(spad)).from_buffer(spad), out)
    return rc, list(out)


def ee(lib, op: str, a: int, b: int) -> int:
    return lib.dof_ee({'add': 0, 'sub': 1, 'mul': 2, 'div': 3}[op], a & 0xFFFFFFFF, b & 0xFFFFFFFF)


def neighbours(bits: int, radius: int):
    """Positive binary32 words around `bits`, nearest first."""
    yield bits
    for d in range(1, radius + 1):
        for c in (bits - d, bits + d):
            if 0 < c < 0x7F800000:
                yield c


class Probe:
    """Runs 001DDE10 on the working image and puts back every byte it may
    write (the context fields, the two eased globals, the packet area)."""

    def __init__(self, lib, ram, spad, cap):
        self.lib, self.ram, self.spad = lib, ram, spad
        self.ctx = u32(ram, 0x275670)
        self.keep = [(self.ctx + o, n) for o, n in CTX_WRITES] + [(0x275690, 8), (cap.target, 0x800)]
        self.saved = [(a, bytes(ram[a:a + n])) for a, n in self.keep]

    def run(self, pre: dict[int, int]):
        for a, v in pre.items():
            put32(self.ram, a, v)
        rc, out = build_once(self.lib, self.ram, self.spad)
        got = {a: u32(self.ram, a) for a in list(pre) + [self.ctx + 0x2460, 0x275690, 0x275694] +
               [self.ctx + 0x24F0 + 4 * k for k in range(8)]}
        for a, data in self.saved:
            self.ram[a:a + len(data)] = data
        return rc, out, got


def pre_state(lib, cap: Capture, ram, spad) -> dict[int, int] | None:
    """The values 001DDE10 read before it ran, as far as its run changed
    them: the channel-3 cursor, and the eased values. In a cutscene frame
    (0022EBE0 != 0, stored at +0x2510) the pairs are snapped from the
    captured point and D_00275690 / D_00275694 are only written; in a play
    frame (0) all three eases moved once, so each is inverted with the EE
    float model: D_00275690 among the words whose quotient 16777215 / x is the
    captured +0x2460 and whose step gives the captured value; D_00275694
    among those whose step gives its captured value; each pair's value among
    those whose 0.15 step towards the target the run computes gives the
    captured value. Every candidate is checked by running the translation."""
    ctx = u32(ram, 0x275670)
    rp = ctx + 0x24F0
    post = {a: u32(ram, a) for a in [0x275690, 0x275694, ctx + 0x2460] + [rp + 4 * k for k in range(8)]}
    v = u32(ram, rp + 0x20)
    probe = Probe(lib, ram, spad, cap)
    pre = {ctx + 0x1C: cap.target}
    # D_00275690 (x += 0.05 (far - x)) and D_00275694 (x = 0.05 (bias - x)):
    # the run's step must give the captured values (and, in play, +0x2460 =
    # 16777215 / D_00275690 before the step); the search starts at the
    # captured value (a converged ease) and at the algebraic inverse for
    # each target 001DDE10 eases towards (8500 / 30500 / 40500, 50 / 1500)
    f = lambda bits: struct.unpack('<f', struct.pack('<I', bits))[0]
    starts = {0x275690: [post[0x275690]] + [f32bits((f(post[0x275690]) - 0.05 * far) / 0.95)
                                           for far in (8500.0, 30500.0, 40500.0)],
              0x275694: [post[0x275694]] + [f32bits(bias - f(post[0x275694]) / 0.05) for bias in (50.0, 1500.0)]}
    found = {}
    for addr in (0x275690, 0x275694):
        for cand in (c for s in starts[addr] for c in neighbours(s, 256)):
            rc, out, got = probe.run({**pre, addr: cand, rp + 0x20: v})
            if rc != 0:
                fail(f'{cap.fid}: em_render_context_001DDE10 faulted at {out[1]:08X} (code {out[2]}) in a probe')
                return None
            ok = got[addr] == post[addr]
            if addr == 0x275690 and v == 0:
                ok = ok and got[ctx + 0x2460] == post[ctx + 0x2460]
            if ok:
                found[addr] = cand
                break
        else:
            fail(f'{cap.fid}: no {addr:08X} before the step gives the captured {post[addr]:#010x}')
            return None
    pre.update(found)
    if v != 0:
        pre[rp + 0x20] = 1                      # snapped: the earlier value plays no part
        return pre
    # play: the targets the run eases towards (its snap with +0x2510 set
    # gives them), then each pair's earlier value
    rc, out, got = probe.run({**pre, rp + 0x20: 1})
    targets = [got[rp + 4 * k] for k in range(8)]
    pre[rp + 0x20] = 0
    for k in range(8):
        want, a = post[rp + 4 * k], targets[k]
        guess = f32bits((struct.unpack('<f', struct.pack('<I', want))[0] -
                         0.15 * struct.unpack('<f', struct.pack('<I', a))[0]) / 0.85)
        for cand in neighbours(guess, 1 << 12):
            # 001DE6A8: old + 0.15 * (target - old) (em_render_context.c F_0_15)
            step = ee(lib, 'add', cand, ee(lib, 'mul', 0x3E19999A, ee(lib, 'sub', a, cand)))
            if step == want:
                pre[rp + 4 * k] = cand
                break
        else:
            fail(f'{cap.fid}: no value of pair {k} before the 0.15 step gives the captured {want:#010x}')
            return None
    return pre


def part_b(lib, cap: Capture, report) -> bytearray | None:
    ram, spad = cap.ee()
    captured_ram, captured_spad = bytes(ram), bytes(spad)
    ctx = u32(ram, 0x275670)
    # the captured stream is the captured EE chain's pass (the capture checked
    # the GS dump against it; checked again here)
    walked = pass_stream(ram, cap.target)
    if walked != cap.gif or cap.gif_dump != cap.gif:
        fail(f'{cap.fid}: the captured image\'s pass walk ({len(walked)} bytes) or the GS dump\'s '
             f'({len(cap.gif_dump)}) differs from pass_gif_from_ee_chain.bin ({len(cap.gif)})')
    if u32(ram, cap.block) != 0x50000000 or u32(ram, cap.block + 4) & 0x7FFFFFFF != cap.target:
        fail(f'{cap.fid}: the slot block {cap.block:#x} is not the CALL of {cap.target:#x}')
    pre = pre_state(lib, cap, ram, spad)
    if pre is None:
        return None
    for a, val in pre.items():
        put32(ram, a, val)
    rc, out = build_once(lib, ram, spad)
    if rc != 0:
        fail(f'{cap.fid}: em_render_context_001DDE10 faulted at {out[1]:08X} (code {out[2]})')
        return None
    if out[3:7] != [1, 0x7635C0, 0xFFF000, cap.target]:
        fail(f'{cap.fid}: its 001CB760 calls {out[3]} (table {out[4]:#x}, id {out[5]:#x}, address {out[6]:#x}); '
             f'one with D_007635C0, 0xFFF000 and {cap.target:#x} expected')
    diff_ram = np.nonzero(np.frombuffer(bytes(ram), np.uint8) != np.frombuffer(captured_ram, np.uint8))[0]
    diff_spad = np.nonzero(np.frombuffer(bytes(spad), np.uint8) != np.frombuffer(captured_spad, np.uint8))[0]
    if diff_ram.size or diff_spad.size:
        fail(f'{cap.fid}: the port\'s run leaves {diff_ram.size} EE bytes and {diff_spad.size} scratchpad bytes '
             f'other than the capture\'s (first at {int(diff_ram[0]) if diff_ram.size else int(diff_spad[0]):#x})')
    built = pass_stream(ram, cap.target)
    if built != cap.gif:
        n = next((i for i in range(min(len(built), len(cap.gif))) if built[i] != cap.gif[i]), min(len(built), len(cap.gif)))
        fail(f'{cap.fid}: the port-built pass sends {len(built)} GIF bytes, the capture {len(cap.gif)}; '
             f'first difference at byte {n}')
    report['packet_bytes'] = u32(ram, ctx + 0x1C) - cap.target
    report['gif_bytes'] = len(built)
    report['mode'] = 'cutscene' if u32(ram, ctx + 0x2510) else 'play'
    # the eased values whose value before the step differs from the captured one
    report['inverted'] = sum(1 for a, val in pre.items()
                             if (a in (0x275690, 0x275694) or ctx + 0x24F0 <= a < ctx + 0x2510)
                             and val != u32(captured_ram, a))
    return ram


# ---------------------------------------------------------------- C
# The buffers the frame draws (GS_EXACT.md section 9): both fields (FBP 0 and
# 0x38, 512 x 224), the Z buffer (ZBP 0x70) and the 256 x 256 copy / shadow
# buffer at 0x258000: in the live game no upload writes them (their blocks
# are never resident), so the workers order their texture reads against the
# frame's draws. The rest of local memory counts as uploads.
DRAWN_BLOCKS = [(0x0000, 0x0700), (0x0700, 0x0E00), (0x0E00, 0x1500), (0x2580, 0x2980)]


def part_c(lib, cap: Capture, ram, report) -> None:
    ctx = u32(ram, 0x275670)
    slot = u32(ram, ctx + 0x9C)
    bank, _frame, _zbuf, _scissor = field_of(ram, slot)
    resident = bytearray(b'\x01' * (MEM // 256))
    for lo, hi in DRAWN_BLOCKS:
        resident[lo:hi] = bytes(hi - lo)
    before = cap.before.tobytes()
    r, v, n = reg_arrays(cap.regs)
    # as in the game (the drawn buffers never uploaded), and with every block
    # counted as an upload (the workers' ordering must cover drawn blocks
    # whatever their residency: em_gs_world.c mark_drawn / texture_entry)
    runs = [(w, bytes(resident)) for w in ([1, 2, 3, 8] if FULL else [1, 3])] + \
           [(w, b'\x01' * (MEM // 256)) for w in ([3, 8] if FULL else [3])]
    for workers, res in runs:
        mem = (C.c_uint8 * MEM)()
        out, why = (C.c_uint32 * 16)(), C.create_string_buffer(192)
        rc = lib.dof_live((C.c_char * len(ram)).from_buffer(ram), cap.target, bank, slot, before, res,
                          r, v, n, workers, mem, out, why, 192)
        walk = (out[3], out[4], out[5], out[6], list(out[8:8 + out[7]]))
        if walk != (8, 1, 8, 32, [1, 3, 5, 7, 8]):
            fail(f'{cap.fid}: the pass walk gives {out[3]} primitives, {out[4]} pass, {out[5]} pass primitives, '
                 f'{out[6]} DIRECT packets, marks {walk[4]} (8, 1, 8, 32, [1, 3, 5, 7, 8] expected; walk fault '
                 f'{out[1]} at {out[2]:#x})')
        label = f'{workers} workers' + (', every block an upload' if res.count(0) == 0 else '')
        if rc != 0:
            fail(f'{cap.fid}: the GS world ({label}) failed: {why.value.decode()}')
            continue
        if out[14] != workers:
            fail(f'{cap.fid}: the GS world ran {out[14]} workers, {workers} asked')
        model = np.frombuffer(bytes(mem), dtype=np.uint32)
        bad = int((model != cap.after).sum())
        if bad:
            fail(f'{cap.fid}: the Original profile\'s path ({label}) leaves {bad} words other than the captured '
                 f'AFTER memory')
        else:
            report.setdefault('workers' if res.count(0) else 'workers_up', []).append(workers)


# ---------------------------------------------------------------- D
def part_d() -> str:
    a, b = (CAP / REPEAT[0], CAP / REPEAT[1])
    names = ['before/vram.bin', 'after/vram.bin', 'pass_gif_from_ee_chain.bin', 'pass_gif_from_gs_dump.bin']
    same = [n for n in names if (a / n).read_bytes() == (b / n).read_bytes()]
    if len(same) != len(names):
        fail(f'the repeat capture {REPEAT[1]} differs from {REPEAT[0]} in {sorted(set(names) - set(same))}')
    return f'{REPEAT[1]} equal to {REPEAT[0]} in {len(same)} of {len(names)} files'


# ---------------------------------------------------------------- main
def main() -> int:
    t0 = time.monotonic()
    if not (CAP / 'manifest.json').exists():
        print(f'test_dof_pass_reference: FAIL: no capture at {CAP} (the decomp\'s fork capture of 001DDE10\'s pass, '
              f'build/dof_capture/manifest.json; regenerate it with its capture.py)')
        return 1
    manifest = json.loads((CAP / 'manifest.json').read_text())
    frames = [f['id'] for f in manifest['frames']]
    if REPEAT[1] not in frames and (CAP / REPEAT[1]).exists():
        frames.append(REPEAT[1])
    ids = frames if FULL else [f for f in QUICK if f in frames]
    if len(ids) != (len(frames) if FULL else len(QUICK)):
        print(f'test_dof_pass_reference: FAIL: the manifest lacks {sorted(set(QUICK) - set(ids))}')
        return 1
    lib = build()
    print(f'test_dof_pass_reference: mode {MODE}: {len(ids)} of {len(frames)} captures (decomp build/dof_capture)')
    for fid in ids:
        cap = Capture(fid)
        report: dict = {}
        ram0, _spad0 = cap.ee()
        ctx = u32(ram0, 0x275670)
        slot = u32(ram0, ctx + 0x9C)
        _bank, frame, zbuf, scissor = field_of(ram0, slot)
        fbp, fbw, zbp = frame & 0x1FF, (frame >> 16) & 0x3F, zbuf & 0x1FF
        rows = ((scissor >> 48) & 0x7FF) + 1
        copy_bp = u32(ram0, 0x27568C) >> 8
        if (frame >> 24) & 0x3F != 0 or (zbuf >> 24) & 0xF != 1 or rows != 224 or fbw != 8 or copy_bp != 0x2580:
            fail(f'{fid}: the field {frame:#x} / Z {zbuf:#x} / {rows} rows / copy {copy_bp:#x} is not the expected shape')
        regions = (map32(lib, fbp * 32, fbw, 512, 224, 0), map32(lib, copy_bp, 4, 256, 256, 0),
                   map32(lib, zbp * 32, fbw, 512, 224, 1))
        del ram0
        part_a(lib, cap, regions, report)
        ram = part_b(lib, cap, report)
        if ram is not None:
            part_c(lib, cap, ram, report)
        xyo = cap.analysis['gs_regs_at_boundaries']['written_before_pass']['XYOFFSET_1']
        ofy = (int(xyo, 16) >> 32 & 0xFFFF) / 16
        print(f'  {fid}: {report.get("mode", "?")}, field FBP {fbp:#x} OFY {ofy}: '
              f'field {report.get("field")}/{512 * 224}, copy {report.get("copy")}/{256 * 256}, '
              f'Z untouched {report.get("z")}/{512 * 224}, other words differing {report.get("rest")}; '
              f'bands {report.get("bands", [])} equal')
        if 'blends' in report:
            print('      blends (Z, alpha, pixels its Z test passes): ' +
                  ', '.join(f'{z}/{a}/{p}' for z, a, p in report['blends']) +
                  f'; field pixels by blends reached 0..4: {report["by_count"]}, changed by the original '
                  f'{report["changed"]}')
        if 'packet_bytes' in report:
            print(f'      port packets: {report["packet_bytes"]} EE bytes at the CALL target, every EE / scratchpad '
                  f'byte equal to the capture\'s ({report["inverted"]} eased values different before their step), '
                  f'{report["gif_bytes"]} GIF bytes equal to the captured stream; the Original profile\'s path '
                  f'equal to AFTER with {report.get("workers", [])} workers and, every block an upload, with '
                  f'{report.get("workers_up", [])}')
    if FULL and (CAP / REPEAT[1]).exists():
        print('  ' + part_d())
    dt = time.monotonic() - t0
    if FAILS:
        print(f'test_dof_pass_reference: FAIL ({len(FAILS)} problems, {dt:.1f} s)')
        return 1
    print(f'test_dof_pass_reference: PASS ({len(ids)} captures bit-exact through the model, the port\'s packets '
          f'and the Original profile\'s path; {dt:.1f} s)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
