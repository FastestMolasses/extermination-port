#!/usr/bin/env python3
"""The slot-0xFFF ramp passes against the original's GS memory.

docs/CHAIN_PAGE.md section 6.3. A ramp list (001DF180's, which 001DF5A0
hands 001CB760 at slot 0xFFF; 001DF110's on the same slot) is a pass of the
page as 001DDE10's depth-of-field pass is: a channel list that sets its own
drawing environment and ends with 001D1F20's REF of the kicked slot's draw
environment and its RET. The port's chain page walks every slot-0xFFF CALL
of a frame in pass mode and the Original profile draws each with the GS
model (em_chain_page_live, em_gs_world_page_pass).

The capture (DOF_RAMP_CAPTURE_ROOT, default ../Extermination/build/
ramp_capture: its capture_ramp.py and analyze_ramp.py; the project's PCSX2
fork, software renderer; generated locally from the user's own disc, never
committed, nothing of it embedded here) replays a route beat open loop to a
frame whose page holds a ramp CALL, and cuts the original's DMA chain at
that CALL's two boundaries (the depth-of-field capture's method,
build/dof_capture), keeping the GS local memory exactly before and after the
ramp pass, the GS registers last written before it, its GIF bytes (from the
EE chain and from the GS dump) and the EE RAM at the kick.

A. The model. The captured GIF bytes through the CPU GS model (em_gs_raster,
   strict) from the BEFORE memory and registers: all 4 MiB equal to the
   AFTER memory, with 1 and 3 row bands (EM_TEST_FULL: 8).
C. The Original profile's path: em_chain_page's pass mode over the captured
   EE image (the ramp CALL followed from a slot block of its own), then
   em_gs_world (the field declared, each primitive with its environment, the
   kick's environment packet at each mark) from the BEFORE memory, with 1
   and 3 workers (EM_TEST_FULL: 1, 2, 3, 8) and with 3 workers every block
   an upload: all 4 MiB equal to AFTER.

The C side is tests/dof_pass_reference_bridge.c (dof_replay, dof_live: both
take any pass target).

What must hold: every word outside the drawn field equal to the original's
(the copy buffer, the Z buffer, the rest of memory), and the field's words
equal except the model's one open texture class, GS_EXACT.md 8.2 (the
perspective STQ residue on a constant-Z span: 001DF180's grid strips are
STQ triangle strips with Q = 1 and S / T planes, bilinear, one Z, the p5_s
class): each such word one bilinear-weight step off (colour only, alpha
equal) on at most RESIDUE_MAX of the pass's drawn pixels, and the Original
profile's path leaving exactly the model's words (the same residue, word for
word). Anything else fails.
"""
from __future__ import annotations

import ctypes as C
import json
import os
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_dof_pass_reference as D  # noqa: E402
from reference_mode import FULL, MODE  # noqa: E402

ROOT = D.ROOT
CAP = Path(os.environ.get('DOF_RAMP_CAPTURE_ROOT', ROOT.parent / 'Extermination/build/ramp_capture'))
MEM = D.MEM
RESIDUE_MAX = 0.02      # GS_EXACT.md 8.2: 0.2 .. 2.0 % of the pixels of the class


def residue(cap, model, field) -> tuple[int, str]:
    """The words `model` leaves other than AFTER: their count, or -1 and why
    when one is outside the 8.2 class (outside the field, or alpha)."""
    bad = np.nonzero(model != cap.after)[0]
    if not bad.size:
        return 0, ''
    if not np.isin(bad, field).all():
        return -1, f'{int((~np.isin(bad, field)).sum())} differing words outside the field'
    if ((model[bad] ^ cap.after[bad]) >> 24).any():
        return -1, 'a differing word with another alpha'
    return int(bad.size), ''


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
        self.regs = [(D.GS_REG[n], int(v, 16)) for n, v in regs.items()
                     if n not in D.VERTEX_REGS and n in D.GS_REG]
        slot = self.capture['variants'][0]['slot_block']
        self.block, self.target = int(slot['B'], 16), int(slot['call_addr'], 16)
        self.chain = self.capture['variants'][0]['slot_chain']

    def ram(self) -> bytearray:
        return bytearray((self.dir / 'ee_T.bin').read_bytes())


def field_words(lib, cap: Capture) -> np.ndarray:
    ram = cap.ram()
    ctx = D.u32(ram, 0x275670)
    _bank, frame, _zbuf, scissor = D.field_of(ram, D.u32(ram, ctx + 0x9C))
    rows = ((scissor >> 48) & 0x7FF) + 1
    return D.map32(lib, (frame & 0x1FF) * 32, (frame >> 16) & 0x3F, 512, rows, 0).ravel()


def part_a(lib, cap: Capture, report) -> None:
    if cap.gif != cap.gif_dump:
        D.fail(f'{cap.fid}: the pass GIF bytes from the EE chain and from the GS dump differ')
    r, v, n = D.reg_arrays(cap.regs)
    for bands in [1, 3] + ([8] if FULL else []):
        mem = (C.c_uint8 * MEM).from_buffer_copy(cap.before.tobytes())
        out, why = (C.c_uint64 * 6)(), C.create_string_buffer(128)
        lib.dof_replay(mem, r, v, n, cap.gif, len(cap.gif), bands, out, why, 128)
        model = np.frombuffer(bytes(mem), dtype=np.uint32)
        if out[0] != len(cap.gif) or out[1] or out[2] or out[3]:
            D.fail(f'{cap.fid}: the model took {out[0]} of {len(cap.gif)} bytes, refusals {out[1]:#x} '
                   f'({why.value.decode()}), refused {out[2]}, span faults {out[3]} ({bands} band(s))')
            continue
        if bands == 1:
            report['pixels'] = int(out[5])
            report['model'] = model.copy()
        off, why_r = residue(cap, model, report['field'])
        if off < 0 or off > RESIDUE_MAX * report['pixels']:
            D.fail(f'{cap.fid} ({bands} band(s)): {why_r or f"{off} field words off, above the 8.2 bound"}')
        elif not np.array_equal(model, report['model']):
            D.fail(f'{cap.fid}: {bands} band(s) draw other words than one')
        else:
            report['residue'] = off
            report.setdefault('bands', []).append(bands)


def part_c(lib, cap: Capture, report) -> None:
    ram = cap.ram()
    ctx = D.u32(ram, 0x275670)
    slot = D.u32(ram, ctx + 0x9C)
    bank, _frame, _zbuf, _scissor = D.field_of(ram, slot)
    resident = bytearray(b'\x01' * (MEM // 256))
    for lo, hi in D.DRAWN_BLOCKS:
        resident[lo:hi] = bytes(hi - lo)
    before = cap.before.tobytes()
    r, v, n = D.reg_arrays(cap.regs)
    runs = [(w, bytes(resident)) for w in ([1, 2, 3, 8] if FULL else [1, 3])] + \
           [(w, b'\x01' * (MEM // 256)) for w in ([3, 8] if FULL else [3])]
    for workers, res in runs:
        mem = (C.c_uint8 * MEM)()
        out, why = (C.c_uint32 * 16)(), C.create_string_buffer(192)
        rc = lib.dof_live((C.c_char * len(ram)).from_buffer(ram), cap.target, bank, slot, before, res,
                          r, v, n, workers, mem, out, why, 192)
        report['walk'] = (out[3], out[4], out[5], out[6], list(out[8:8 + min(out[7], 5)]))
        label = f'{workers} workers' + (', every block an upload' if res.count(0) == 0 else '')
        if out[0] != 0 or out[4] != 1 or out[3] != out[5]:
            D.fail(f'{cap.fid}: the pass walk failed: fault {out[1]} at {out[2]:#x}, {out[3]} primitives, '
                   f'{out[4]} passes, {out[5]} pass primitives')
            return
        if rc != 0:
            D.fail(f'{cap.fid}: the GS world ({label}) failed: {why.value.decode()}')
            continue
        if out[14] != workers:
            D.fail(f'{cap.fid}: the GS world ran {out[14]} workers, {workers} asked')
        model = np.frombuffer(bytes(mem), dtype=np.uint32)
        if 'model' not in report or not np.array_equal(model, report['model']):
            bad = int((model != cap.after).sum())
            D.fail(f'{cap.fid}: the Original profile\'s path ({label}) leaves other words than the model '
                   f'({bad} differ from the captured AFTER memory)')
        else:
            report.setdefault('workers' if res.count(0) else 'workers_up', []).append(workers)


def main() -> int:
    t0 = time.monotonic()
    ids = sorted(p.parent.name for p in CAP.glob('*/analysis.json')) if CAP.exists() else []
    if not ids:
        print(f'test_ramp_pass_reference: FAIL: no capture at {CAP} (the decomp\'s fork capture of a ramp pass, '
              f'build/ramp_capture; regenerate it with its capture_ramp.py and analyze_ramp.py)')
        return 1
    lib = D.build()
    print(f'test_ramp_pass_reference: mode {MODE}: {len(ids)} capture(s) (decomp build/ramp_capture)')
    for fid in ids:
        cap = Capture(fid)
        report: dict = {'field': field_words(lib, cap)}
        part_a(lib, cap, report)
        part_c(lib, cap, report)
        print(f'  {fid}: ramp CALL {cap.target:#x} (slot chain {cap.chain}), {len(cap.gif)} GIF bytes, '
              f'{report.get("pixels")} pixels, {report.get("residue")} field words of the 8.2 residue; '
              f'model equal with {report.get("bands", [])} band(s); walk '
              f'(primitives, passes, pass primitives, DIRECT, marks) {report.get("walk")}; the Original '
              f'profile\'s path equal with {report.get("workers", [])} workers and, every block an upload, '
              f'{report.get("workers_up", [])}')
    dt = time.monotonic() - t0
    if D.FAILS:
        print(f'test_ramp_pass_reference: FAIL ({len(D.FAILS)} problems, {dt:.1f} s)')
        return 1
    print(f'test_ramp_pass_reference: PASS ({len(ids)} capture(s): every word outside the GS_EXACT.md 8.2 '
          f'residue equal through the model and the Original profile\'s path; {dt:.1f} s)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
