#!/usr/bin/env python3
"""Check tools/ee_float_model.py against the recorded ORIGINAL results.

The vectors were recorded by single-stepping instruction instances of the
original boot ELF in PCSX2 under the user's saved configuration
(docs/EE_FLOAT_MODEL.md). They are machine-derived and live in the ignored
../Extermination/build/startup-reference/ee_float/vectors/; a missing recording is a hard
failure, never a skip.

It also re-checks free-running (block-compiled, not single-stepped) runs of
two original routines recorded the same way (blockcheck.json): 001026A0
(VMULAx, VMADDAy, VMADDAz, VMADDw: the 227-caller matrix * vector) and the
straight NEG/ADD/MUL/DIV prefix of 00102EA8 up to 00102F24.

Default: every op's boundary vectors (any Inf/NaN/denormal/zero operand or
result, every overflow/underflow) plus a fixed-seed sample of the rest.
EM_TEST_FULL=1: every recorded vector.
"""
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ee_float_model as model  # noqa: E402
from reference_mode import banner, part, select  # noqa: E402


def _boundary(rec) -> bool:
    words = []
    for key in ('fs', 'ft', 'acc', 'fd'):
        if isinstance(rec.get(key), int):
            words.append(rec[key])
    for key in ('vs', 'vt', 'acc_in', 'vd', 'acc'):
        if isinstance(rec.get(key), list):
            words.extend(rec[key])
    for key in ('q_in', 'q'):
        if isinstance(rec.get(key), int):
            words.append(rec[key])
    return any(((w >> 23) & 0xFF) in (0, 0xFF) or (w & 0x7FFFFFFF) == 0x7F7FFFFF for w in words)


def apply_matrix_001026A0(mat, vec):
    """001026A0: ACC = c0*v.x; ACC += c1*v.y; ACC += c2*v.z; out = ACC + c3*v.w."""
    col = [mat[0:4], mat[4:8], mat[8:12], mat[12:16]]
    acc = [model.vu_lane('vmulabc', 15, 0, col[0][k], vec[0]) for k in range(4)]
    acc = [model.vu_lane('vmaddabc', 15, 1, col[1][k], vec[1], acc[k]) for k in range(4)]
    acc = [model.vu_lane('vmaddabc', 15, 2, col[2][k], vec[2], acc[k]) for k in range(4)]
    return [model.vu_lane('vmaddbc', 15, 3, col[3][k], vec[3], acc[k]) for k in range(4)]


def prefix_00102EA8(f, mem):
    """00102EA8..00102F20: the FPU operations in program order (f1 is
    loaded from the caller's stack word)."""
    f = dict(f)
    f[0] = model.ee_neg(f[17])
    f[20] = model.ee_neg(f[18])
    f[1] = mem
    f[0] = model.ee_add(f[0], f[18])
    f[21] = model.ee_mul(f[1], f[19])
    f[20] = model.ee_mul(f[20], f[19])
    f[17] = model.ee_mul(f[17], f[1])
    f[19] = model.ee_neg(f[19])
    f[21] = model.ee_mul(f[21], f[0])
    f[20] = model.ee_add(f[20], f[17])
    f[19] = model.ee_add(f[19], f[1])
    f[21] = model.ee_div(f[21], f[19])
    f[20] = model.ee_div(f[20], f[19])
    return {str(k): f[k] for k in (0, 1, 17, 19, 20, 21)}


def check_blocks(failures) -> int:
    path = model.VECTORS.parent / 'blockcheck.json'
    if not path.exists():
        raise SystemExit(f'missing {path} (record it with ../Extermination/tools/ee_float/blockcheck.py)')
    runs = json.loads(path.read_text())
    count = 0
    for rec in select(runs['vu_apply_matrix'], 60, seed=0x1026A0):
        if apply_matrix_001026A0(rec['M'], rec['v']) != rec['out']:
            failures.append(('001026A0', rec, apply_matrix_001026A0(rec['M'], rec['v'])))
        count += 1
    for rec in select(runs['ee_prefix'], 60, seed=0x102EA8):
        got = prefix_00102EA8({int(k): v for k, v in rec['f'].items()}, rec['mem'])
        if got != rec['out']:
            failures.append(('00102EA8', rec, got))
        count += 1
    return count


def main() -> int:
    start = time.monotonic()
    recs = model.load_vectors()
    by_op = {}
    for rec in recs:
        by_op.setdefault(rec['op'], []).append(rec)
    ran = 0
    failures = []
    for op, group in sorted(by_op.items()):
        chosen = select(group, 400, seed=len(op) * 7919 + len(group),
                        keep=lambda _i, rec: _boundary(rec))
        for rec in chosen:
            want, got = model.measured(rec), model.predict(rec)
            if want != got:
                failures.append((op, rec, got))
        ran += len(chosen)
    blocks = check_blocks(failures)
    banner(part(ran, len(recs), 'recorded emulator vectors'),
           f'{len(by_op)} instructions', f'{blocks} free-running block runs')
    for op, rec, got in failures[:20]:
        print('MISMATCH', op, rec, 'model', got)
    elapsed = time.monotonic() - start
    if failures:
        print(f'FAIL: {len(failures)} vectors differ from the recorded original ({elapsed:.1f}s)')
        return 1
    print(f'PASS: ee_float_model matches every checked original result ({elapsed:.1f}s)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
