#!/usr/bin/env python3
"""The status pages' GS lists through the CPU GS model, against the original.

docs/STATUS_FIELD.md, docs/GS_EXACT.md sections 3.4 and 8.8. Two sets of the
decomp's fork captures (the project's PCSX2 fork, software renderer;
generated locally from the user's own disc, never committed, nothing of them
embedded here):

A. The status pages. ../Extermination/build/status_field_capture/<beat>/
   f<T>/ (decomp build/status_field_capture/capture.py, its capture.json):
   route beat 01 (the battery take: the ITEM / BATTERY page with the take
   notice) and 03 (the panel's BATTERY page), and the ITEM root from the
   fork state slot12_status_root (ROOT_FRAMES) and its children EQUIPMENT
   and EVENT, open and leaving with Circle (PAGE_FRAMES), replayed in the fork to
   frame T, then at step V (001D21E0) of iterations T and T + 1 the EE RAM,
   the GS memory after list T (vram_after.bin) and, in a second run with
   list T's first tag made END, the GS memory list T starts from
   (vram_before.bin), and the displayed field at loop top T + 2. The test
   walks list T from its start D_0028F700 + (slot << 14) in the EE image
   (DMA tags CNT, NEXT, REF, REFS, CALL, RET, END; VIF NOP / FLUSH* skipped,
   DIRECT data kept; any other VIF code refuses the frame), takes the GS
   registers list T starts with from list T - 1 (the last value of every
   state register it wrote: list T writes the rest itself), and runs list T
   through em_gs_raster from the BEFORE memory:
   in both modes, not strict and strict (the Original profile's mode):
   nothing refused, every word of the 4 MiB equals the AFTER memory, and the
   drawn field (list T's FRAME_1) equals the displayed field.
B. The column probes (GS_EXACT.md 3.4): ../Extermination/build/
   status_field_capture/probe_cols/p10_cols (decomp build/
   status_field_capture/probe_cols.py, the gs_conformance harness): 512
   designed UV sprites of height 1 (start inside a scissor at 0 and left of
   one at 37, first vertex right, U decreasing; nearest over a 64-texel
   ramp, bilinear over a 16-texel one); probe_open/p11_open and p12_open
   (probe_open.py, probe_open2.py): 1,024 more, bilinear, one parameter a
   test (position, start value, width, gradient, sub-pixel start, row, the
   scissor, corner order; the step's truncation remainder over the start
   column mod 4 and the sub-pixel start); probe_open/p13_holdout
   (probe_holdout.py, captured after the rule was derived from the other
   three: random sprites of every kind, starts whose binary32 association
   matters, XYOFFSET X at 1025 and 1024.5 pixels). Each packet through the
   model in both modes: nothing refused, every pixel of every test equal.

Quick (default): A at 01_battery f0300 and one ITEM root frame, and B.
EM_TEST_FULL=1: every captured frame. A difference fails.
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
CAP = Path(os.environ.get('STATUS_FIELD_CAPTURE_ROOT', ROOT.parent / 'Extermination/build/status_field_capture'))
OUT = ROOT / 'build/status_field_reference'
LIST_BASE = 0x0028F700
# the ITEM root (STATUS_FIELD.md 3e): the fork state slot12_status_root, its
# own stick (0x7F) and the port's neutral 0x80 held from frame 0, and with
# D_002655A0 set to the port's at loop top 39 (capture.py CAPTURE_WRITE)
ROOT_FRAMES = [('slot12_status_root', 30), ('slot12_status_root_pad8080', 40),
               ('slot12_status_root_pad8080_bg3467', 40), ('slot12_status_root_pad8080_bg3467', 41)]
# the ITEM children EQUIPMENT and EVENT (STATUS_FIELD.md 3f): from the same
# state, the stick to the region, Cross, the page open (capture.py
# CAPTURE_INPUTS), and with D_002655A0 set to the port's (CAPTURE_WRITE)
PAGE_FRAMES = [('slot12_status_root_equipment', 60), ('slot12_status_root_event', 62),
               ('slot12_status_root_equipment_bg3421', 60), ('slot12_status_root_equipment_bg3421', 61),
               ('slot12_status_root_event_bg3511', 62), ('slot12_status_root_event_bg3511', 63),
               ('slot12_status_root_equipment_circle', 72)]
QUICK = [('01_battery', 300), ('slot12_status_root_pad8080_bg3467', 40), ('slot12_status_root_equipment', 60)]
FRAMES = [('01_battery', 230), ('01_battery', 300), ('01_battery', 400),
          ('03_panel_power', 440), ('03_panel_power', 500)] + ROOT_FRAMES + PAGE_FRAMES
# The column probes (B), each a batch folder under CAP.
PROBES = ['probe_cols/p10_cols', 'probe_open/p11_open', 'probe_open/p12_open', 'probe_open/p13_holdout',
          'probe_open/p14_rows', 'probe_open/p15_rows_holdout']
SOURCES = ['tests/status_field_reference_bridge.c', 'src/gs/em_gs_raster.c']
# the state registers list T may inherit from list T - 1 (PRIM and the vertex
# registers excluded: list T sets them before every primitive)
STATE_REGS = {0x06, 0x07, 0x08, 0x09, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x22, 0x34, 0x35,
              0x36, 0x37, 0x3B, 0x3D, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B,
              0x4C, 0x4D, 0x4E, 0x4F}

FAILS: list[str] = []


def fail(msg: str) -> None:
    FAILS.append(msg)
    print(f'  FAIL {msg}')


def build() -> C.CDLL:
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('libstatusfield.dylib' if sys.platform == 'darwin' else 'libstatusfield.so')
    # the main build's flags (no fused multiply-add)
    cmd = ['cc', '-O2', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror', '-fPIC',
           '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-Isrc', *SOURCES, '-lm', '-o', str(lib)]
    digest = hashlib.sha256(' '.join(cmd).encode())
    for path in sorted(set(SOURCES) | {str(p.relative_to(ROOT)) for p in (ROOT / 'src/gs').glob('*.h')}):
        digest.update(path.encode() + b'\0' + (ROOT / path).read_bytes())
    stamp = lib.with_suffix('.sha256')
    if not lib.exists() or not stamp.exists() or stamp.read_text() != digest.hexdigest():
        subprocess.run(cmd, cwd=ROOT, check=True)
        stamp.write_text(digest.hexdigest())
    n = C.CDLL(str(lib))
    P = C.POINTER
    n.sf_map32.argtypes = [C.c_uint32, C.c_uint32, C.c_uint32, C.c_uint32, C.c_void_p]
    n.sf_replay.argtypes = [C.c_void_p, P(C.c_uint32), P(C.c_uint64), C.c_uint32, C.c_char_p, C.c_uint64, C.c_int,
                            P(C.c_uint64), C.c_char_p, C.c_uint32]
    n.sf_replay.restype = C.c_int
    return n


def field_words(lib, fbp: int, w: int, h: int) -> np.ndarray:
    m = np.zeros(w * h, dtype=np.uint32)
    lib.sf_map32(fbp * 32, w // 64, w, h, m.ctypes.data)
    return m


def replay(lib, mem: bytearray, regs: dict[int, int], gif: bytes, strict: int):
    keys = sorted(regs)
    R = (C.c_uint32 * len(keys))(*keys)
    V = (C.c_uint64 * len(keys))(*[regs[k] for k in keys])
    out = (C.c_uint64 * 6)()
    reason = C.create_string_buffer(256)
    buf = (C.c_char * len(mem)).from_buffer(mem)
    if lib.sf_replay(C.addressof(buf), R, V, len(keys), gif, len(gif), strict, out, reason, 256) != 0:
        raise RuntimeError('sf_replay refused its arguments')
    del buf
    return {'consumed': out[0], 'refusals': out[1], 'refused': out[2], 'span_faults': out[3], 'prims': out[4],
            'pixels': out[5], 'reason': reason.value.decode()}


# ---------------------------------------------------------------- A: the lists
def walk(ram: bytes, start: int) -> list[bytes]:
    """The data a VIF1 source chain sends from tag `start` (TTE off)."""
    out, stack, t = [], [], start
    for _ in range(200000):
        lo = struct.unpack_from('<Q', ram, t & 0x1FFFFFF)[0]
        qwc, tid, addr = lo & 0xFFFF, (lo >> 28) & 7, (lo >> 32) & 0x7FFFFFFF
        if tid == 1:
            data, nxt = t + 16, t + 16 + qwc * 16
        elif tid == 2:
            data, nxt = t + 16, addr
        elif tid in (3, 4):
            data, nxt = addr, t + 16
        elif tid == 5:
            data, nxt = t + 16, addr
            stack.append(t + 16 + qwc * 16)
        elif tid == 6:
            data, nxt = t + 16, (stack.pop() if stack else None)
        elif tid == 7:
            data, nxt = t + 16, None
        else:
            raise RuntimeError(f'DMA tag id {tid} at {t:#x}')
        a = data & 0x1FFFFFF
        out.append(ram[a:a + qwc * 16])
        if nxt is None:
            return out
        t = nxt
    raise RuntimeError('the chain does not end')


def direct(blocks: list[bytes]) -> bytes:
    """The GIF data of the chain's VIF codes: NOP / FLUSHE / FLUSH / FLUSHA
    pass, DIRECT / DIRECTHL kept; anything else (a VU1 upload) refuses."""
    buf, o, gif = b''.join(blocks), 0, []
    while o + 4 <= len(buf):
        w = struct.unpack_from('<I', buf, o)[0]
        o += 4
        cmd = (w >> 24) & 0x7F
        if cmd in (0x00, 0x10, 0x11, 0x13):
            continue
        if cmd not in (0x50, 0x51):
            raise RuntimeError(f'VIF code {w:#010x}: not a DIRECT-only list')
        n = ((w & 0xFFFF) or 0x10000) * 16
        o = (o + 15) & ~15
        gif.append(buf[o:o + n])
        o += n
    return b''.join(gif)


def state_writes(gif: bytes) -> dict[int, int]:
    """The last value of every state register a GIF stream writes (A+D in
    PACKED, and REGLIST register descriptors)."""
    last, o = {}, 0
    while o + 16 <= len(gif):
        lo, hi = struct.unpack_from('<QQ', gif, o)
        o += 16
        nloop, flg, nreg = lo & 0x7FFF, (lo >> 58) & 3, ((lo >> 60) & 0xF) or 16
        d = [(hi >> (4 * i)) & 0xF for i in range(nreg)]
        if flg == 0:
            for _ in range(nloop):
                for x in d:
                    a, b = struct.unpack_from('<QQ', gif, o)
                    o += 16
                    if x == 0xE and (b & 0xFF) in STATE_REGS:
                        last[b & 0xFF] = a
                    elif x in STATE_REGS:
                        last[x] = a
        elif flg == 1:
            n = nloop * nreg
            for i in range(n):
                if d[i % nreg] in STATE_REGS:
                    last[d[i % nreg]] = struct.unpack_from('<Q', gif, o + 8 * i)[0]
            o += ((n + 1) // 2) * 16
        else:
            o += nloop * 16
    return last


def check_frame(lib, beat: str, T: int) -> None:
    d = CAP / beat / f'f{T:04d}'
    info = json.loads((d / 'capture.json').read_text())
    full = info['full']
    slot, start = full['slot_9C'], int(full['list_start'], 16)
    if start != LIST_BASE + (slot << 14):
        fail(f'{beat} f{T}: list start {start:#x} is not D_0028F700 + (slot {slot} << 14)')
        return
    ram_t, ram_t1 = (d / 'ee_V_T.bin').read_bytes(), (d / 'ee_V_T1.bin').read_bytes()
    prev = direct(walk(ram_t, LIST_BASE + ((slot ^ 1) << 14)))
    cur = direct(walk(ram_t1, start))
    regs = state_writes(prev)
    frame = state_writes(cur).get(0x4C)
    if frame is None:
        fail(f'{beat} f{T}: list T writes no FRAME_1')
        return
    fbp, fbw = frame & 0x1FF, (frame >> 16) & 0x3F
    before, after = (d / 'vram_before.bin').read_bytes(), (d / 'vram_after.bin').read_bytes()
    want = np.frombuffer(after, '<u4')
    words = field_words(lib, fbp, 512, 224)
    disp = np.frombuffer((d / 'displayed_field_T2.rgba').read_bytes(), '<u4')
    changed = int((np.frombuffer(before, '<u4') != want).sum())
    for strict in (0, 1):
        mem = bytearray(before)
        r = replay(lib, mem, regs, cur, strict)
        got = np.frombuffer(bytes(mem), '<u4')
        off = int((got != want).sum())
        field_off = int((got[words] != want[words]).sum())
        tag = 'strict' if strict else 'model'
        line = (f'  {beat} f{T:04d} {tag}: list {len(cur)} GIF bytes, {r["prims"]} primitives drawn, {r["refused"]} '
                f'refused; FBP {fbp:#x} FBW {fbw}: words off {off} of {len(want)} ({field_off} in the field; the list '
                f'changed {changed})')
        print(line)
        if r['consumed'] != len(cur) or r['span_faults']:
            fail(f'{beat} f{T} {tag}: consumed {r["consumed"]} of {len(cur)}, span faults {r["span_faults"]}')
        if r['refused'] or off:
            fail(f'{beat} f{T} {tag}: the model leaves {off} words off ({r["refused"]} refused: {r["reason"]})')
        if not (got[words] == disp).all():
            fail(f'{beat} f{T} {tag}: the drawn field is not the displayed field at loop top T + 2')


# ---------------------------------------------------------------- B: the probes
def check_probe(lib, rel: str) -> None:
    pb = CAP / rel
    doc = json.loads((pb / 'batch.json').read_text())
    pkt = (pb / 'packet.bin').read_bytes()
    for strict in (0, 1):
        mem = bytearray(4 << 20)
        r = replay(lib, mem, {}, pkt, strict)
        words = np.frombuffer(bytes(mem), '<u4')
        counts = {}
        for t in doc['tests']:
            m = field_words(lib, t['fbp'], t['w'], t['h'])
            want = np.load(pb / f"{t['name']}.npz")['color']
            off = int((words[m].reshape(t['h'], t['w']) != want).sum())
            if off:
                counts[t['name']] = off
        tag = 'strict' if strict else 'model'
        print(f'  {doc["batch"]} {tag}: {len(doc["tests"])} tests, {r["prims"]} primitives drawn, {r["refused"]} '
              f'refused; pixels off by test {counts}')
        if counts:
            fail(f'{doc["batch"]} {tag}: pixels off {counts}')
        if r['refused'] or r['span_faults'] or r['consumed'] != len(pkt):
            fail(f'{doc["batch"]} {tag}: refused {r["refused"]} ({r["reason"]}), span faults {r["span_faults"]}, '
                 f'consumed {r["consumed"]} of {len(pkt)}')


def main() -> int:
    t0 = time.monotonic()
    missing = [p for p in [CAP / b / f'f{T:04d}' / 'capture.json' for b, T in FRAMES] +
               [CAP / rel / 'packet.bin' for rel in PROBES] if not p.exists()]
    if missing:
        print(f'test_status_field_reference: REFUSED (no capture at {missing[0]}; decomp '
              f'build/status_field_capture/capture.py, probe_cols.py, probe_open.py, probe_open2.py and probe_holdout.py record them; '
              f'the ITEM root frames: STATUS_FIELD.md section 4)')
        return 2
    lib = build()
    frames = FRAMES if FULL else QUICK
    print(f'mode {MODE}: {len(frames)} of {len(FRAMES)} status page frames, and {len(PROBES)} column probes')
    for beat, T in frames:
        check_frame(lib, beat, T)
    for rel in PROBES:
        check_probe(lib, rel)
    dt = time.monotonic() - t0
    if FAILS:
        print(f'test_status_field_reference: FAIL ({len(FAILS)} failures; {dt:.1f} s)')
        return 1
    print(f'test_status_field_reference: PASS ({len(frames)} status page lists bit-exact through the model in both '
          f'modes, {len(PROBES)} column probes exact, nothing refused; {dt:.1f} s)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
