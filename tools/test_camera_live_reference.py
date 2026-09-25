#!/usr/bin/env python3
"""Execute the camera commit 0018C0D0, its transpose leaf 00102798 and the
lock-on grab test 00193660 from the user's pinned ELF and compare
src/game/em_camera_commit_original.c (docs/CAMERA_LIVE.md section 4).

The original runs whole: every callee it reaches executes its own original
instructions on the EE model (tools/test_render_verify_rest_reference.py's
RvrEE: every COP1, VU0 macro and MMI form through tools/ee_float_model.py):
001028D0, 0011E748 (sqrtf), 0011DF78, 00102760, 001031E0, 00102CD0 with its
leaves, 00102948, 00102798, 0011E620 (atan2f) and 001B1240 for the commit;
001028D0, 00102738 and 0011E748 for the grab test. The native side binds
the same callees to their port translations (em_sdk_math_original,
em_census_standins' look-at, em_script_host_workers' 001B1240), with the
SDK tables loaded from the same ELF.

Compared after every case: the whole camera block D_008101E0 (0xD0 bytes),
the vector pool D_008105D0..D_008106A3 (the eye, target, up, forward, the
view D_00810610 and its transpose D_00810650, D_00810690..D_008106A0) and
the scratchpad words 0x700038A0..0x700038CF; for 00193660 its v0,
0x700038A0..AC and 0x70003A20. Cases:
- random camera blocks and vectors near the routine's tests: the argument
  0 / 1, the top mode +4 (3 or not) and the action +6 (1, 2, 0xA, others),
  both horizontal distances below 0.001 (the forced 0.001 and the +0.001
  nudges), up vectors off (0, -1, 0);
- the camera state of every in-scope route capture and the reference
  captures, with both arguments.
Default run: the captures and a fixed-seed sample; EM_TEST_FULL=1 the whole
sweep. Nothing from the ELF or the captures is printed or copied beyond
compared values.
"""
import ctypes as C
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as RM  # noqa: E402
import test_render_verify_rest_reference as rvr  # noqa: E402
from test_player_slide_reference import read_elf, STACK_TOP  # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
REF = DECOMP / 'build/startup-reference'
ROUTE = DECOMP / 'build/s87/route'
OUT = ROOT / 'build' / 'camera_live_reference'

COMMIT, GRAB, TRANSPOSE = 0x18C0D0, 0x193660, 0x102798
CAM, POOL, POOL_SIZE = 0x8101E0, 0x8105D0, 0xD4
SCRATCH, SCRATCH_SIZE = 0x700038A0, 0x1A0
PLAYER_A4, SDK_MODE = 0x810354, 0x26C5D0
MATRIX_IN, MATRIX_OUT = 0x01F00000, 0x01F00100
M32 = 0xFFFFFFFF

SHIM = r'''
#include <string.h>
#include "game/em_camera_commit_original.h"
#include "game/em_census_standins.h"
#include "game/em_script_host_workers.h"
#include "game/em_sdk_math_original.h"

static EmSdkMathTables T;
static int32_t MODE_WORD = 1;
static EmSdkMathWorld W = { &MODE_WORD };
static EmSdkMathWorkers K;
static EmScriptHostWorkers H;

int shim_init(const uint8_t *elf, size_t size)
{
    if (em_sdk_math_original_load_tables(elf, size, &T) != 0) return -1;
    memset(&H, 0, sizeof H);
    H.world.sdk_tables = &T;
    H.world.sdk_world = &W;
    H.world.sdk_workers = &K;
    return 0;
}

static int w_sqrt(void *c, uint32_t x, uint32_t *out)
{
    (void)c; float xf, r; uint32_t f = 0; memcpy(&xf, &x, 4);
    if (em_sdk_math_original_0011E748(&T, &W, &K, xf, &r, &f) < 0) return -1;
    memcpy(out, &r, 4); return 0;
}
static int w_atan2(void *c, uint32_t y, uint32_t x, uint32_t *out)
{
    (void)c; float yf, xf, r; uint32_t f = 0; memcpy(&yf, &y, 4); memcpy(&xf, &x, 4);
    if (em_sdk_math_original_0011E620(&T, &W, &K, yf, xf, &r, &f) < 0) return -1;
    memcpy(out, &r, 4); return 0;
}
static int w_heading(void *c, const uint32_t o[3], uint32_t x, uint32_t z, uint32_t *out)
{
    (void)c; return em_script_host_001B1240(&H, o, x, z, out);
}
static int w_lookat(void *c, uint32_t out[16], const uint32_t p[4], const uint32_t f[4], const uint32_t u[4])
{
    (void)c; return em_cs_00102CD0(out, p, f, u);
}
static const EmCameraCommitWorkers WK = { NULL, w_sqrt, w_atan2, w_heading, w_lookat };

/* cam: 0xD0 bytes; pool: 53 words from D_008105D0; scratch: 0x68 words
 * from 0x700038A0; a4: the player's +A4 bits. */
int shim_commit(uint8_t *cam, uint32_t *pool, uint32_t *scratch, uint32_t a4, int mode, uint32_t *fault)
{
    static EmCameraFollowRecord rec;
    static EmPlayerLiveActor player;
    static EmCamLeftScratch s;
    memcpy(rec.bytes, cam, sizeof rec.bytes);
    memset(&player, 0, sizeof player);
    em_live_set_u32(&player, 0xA4, a4);
    memcpy(s.w, scratch, sizeof s.w);
    EmCameraCommitWorld w = { &rec, &player, pool, pool + 4, pool + 8, pool + 12, pool + 16, pool + 32,
                              pool + 48, pool + 49, pool + 50, pool + 51, pool + 52, &s, &WK, 0 };
    int rc = em_camera_commit_0018C0D0(&w, mode);
    memcpy(cam, rec.bytes, sizeof rec.bytes);
    memcpy(scratch, s.w, sizeof s.w);
    *fault = w.fault;
    return rc;
}

int shim_grab(const uint32_t *eye, const uint32_t *target, uint32_t *s38A0, uint32_t *s3A20, int32_t *result)
{
    return em_camera_commit_00193660(&WK, eye, target, s38A0, s3A20, result);
}

void shim_transpose(uint32_t *out, const uint32_t *m) { em_camera_commit_00102798(out, m); }
'''

SOURCES = ['src/game/em_camera_commit_original.c', 'src/game/em_census_standins.c',
           'src/game/em_effect_original.c', 'src/game/em_owner_services_original.c',
           'src/game/em_render_verify_rest.c', 'src/game/em_sdk_soft_float.c',
           'src/game/em_message_draw_original.c', 'src/game/em_script_host_workers.c',
           'src/game/em_player_stage_workers.c', 'src/game/em_sdk_math_original.c', 'src/game/em_script.c']

U32 = C.c_uint32


def build_native(elf):
    OUT.mkdir(parents=True, exist_ok=True)
    ext = 'dylib' if sys.platform == 'darwin' else 'so'
    lib = OUT / f'camera_live.{ext}'
    shim = OUT / 'shim.c'
    shim.write_text(SHIM)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-fPIC',
                    '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-Isrc', str(shim), *SOURCES,
                    '-lm', '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    n.shim_init.argtypes = [C.c_char_p, C.c_size_t]
    assert n.shim_init(elf, len(elf)) == 0, 'SDK tables'
    n.shim_commit.argtypes = [C.c_char_p, C.POINTER(U32), C.POINTER(U32), U32, C.c_int, C.POINTER(U32)]
    n.shim_grab.argtypes = [C.POINTER(U32)] * 4 + [C.POINTER(C.c_int32)]
    n.shim_transpose.argtypes = [C.POINTER(U32), C.POINTER(U32)]
    return n


def f2b(v):
    return struct.unpack('<I', struct.pack('<f', v))[0]


def words(buf):
    return list(struct.unpack(f'<{len(buf) // 4}I', buf))


# ======================================================================
# The cases
# ======================================================================

def captures():
    out = []
    for name in ('panel', 'status-hub', 'roger-encounter'):
        ram, spad = REF / name / 'eeMemory.bin', REF / name / 'scratchpad.bin'
        if ram.exists() and spad.exists():
            out.append((name, ram, spad))
    for beat in sorted(p.name for p in ROUTE.iterdir() if p.is_dir()) if ROUTE.exists() else []:
        if RM.in_scope_beat(beat) and (ROUTE / beat / 'scratchpad.bin').exists():
            out.append((beat, ROUTE / beat / 'eeMemory.bin', ROUTE / beat / 'scratchpad.bin'))
    return out


def random_case(rng):
    cam = bytearray(rng.getrandbits(8) for _ in range(0xD0))
    cam[4] = rng.choice((0, 1, 2, 3, 3))
    cam[6] = rng.choice((0, 1, 2, 0xA, 5, 8))
    vec = lambda s: [rng.uniform(-s, s) for _ in range(3)]
    eye = [rng.uniform(100, 400), rng.uniform(150, 300), rng.uniform(100, 450)]
    tgt = [e + d for e, d in zip(eye, vec(60))]
    kind = rng.randrange(6)
    if kind == 1:                                     # actual pair: horizontal below 0.001
        tgt[0], tgt[2] = eye[0] + rng.uniform(-4e-4, 4e-4), eye[2] + rng.uniform(-4e-4, 4e-4)
    des_eye = [e + d for e, d in zip(eye, vec(3))]
    des_tgt = [e + d for e, d in zip(tgt, vec(3))]
    if kind == 2:                                     # desired pair: horizontal below 0.001
        des_tgt[0], des_tgt[2] = des_eye[0] + rng.uniform(-4e-4, 4e-4), des_eye[2] + rng.uniform(-4e-4, 4e-4)
    up = [0.0, -1.0, 0.0]
    if kind == 3:
        roll = rng.uniform(-0.5, 0.5)
        up = [0.0, -rng.choice((1, -1)) * abs(struct.unpack('<f', struct.pack('<f', 1 - roll * roll / 2))[0]),
              -roll]
    ws = lambda: rng.choice((1.0, 0.0, 1.0))
    struct.pack_into('<4f', cam, 0x10, *des_eye, ws())
    struct.pack_into('<4f', cam, 0x20, *des_tgt, ws())
    pool = [0] * (POOL_SIZE // 4)
    pool[0:4] = [f2b(v) for v in eye + [ws()]]
    pool[4:8] = [f2b(v) for v in tgt + [ws()]]
    pool[8:12] = [f2b(v) for v in up + [1.0]]
    for i in range(12, len(pool)):
        pool[i] = rng.getrandbits(32) & 0x3FFFFFFF      # stale outputs (finite)
    scratch = [rng.getrandbits(32) & 0x3FFFFFFF for _ in range(SCRATCH_SIZE // 4)]
    a4 = f2b(eye[1] - rng.uniform(5, 30))
    return bytes(cam), pool, scratch, a4, rng.randrange(2)


# ======================================================================
# Original and native runs
# ======================================================================

def original_commit(elf, ram, spad, case):
    cam, pool, scratch, a4, mode = case
    e = rvr.RvrEE(elf, ram, spad)
    e.write(CAM, cam)
    e.write(POOL, struct.pack(f'<{len(pool)}I', *pool))
    e.write(SCRATCH, struct.pack(f'<{len(scratch)}I', *scratch))
    e.save(PLAYER_A4, a4)
    e.save(SDK_MODE, 1)
    e.r[29] = STACK_TOP
    try:
        e.call(COMMIT, (CAM, mode))
    except AssertionError as refused:
        return None, refused
    return (e.read(CAM, 0xD0), words(e.read(POOL, POOL_SIZE)), words(e.read(SCRATCH, SCRATCH_SIZE))), None


def native_commit(n, case):
    cam, pool, scratch, a4, mode = case
    c = C.create_string_buffer(cam, 0xD0)
    p = (U32 * len(pool))(*pool)
    s = (U32 * len(scratch))(*scratch)
    fault = U32(0)
    rc = n.shim_commit(c, p, s, a4, mode, C.byref(fault))
    return (bytes(c.raw[:0xD0]), list(p), list(s)), rc, fault.value


def compare_commit(elf, n, ram, spad, case, where):
    want, refused = original_commit(elf, ram, spad, case)
    got, rc, fault = native_commit(n, case)
    if refused is not None:
        return 'refused'
    assert rc == 0, (where, 'native commit faulted', hex(fault))
    assert got[0] == want[0], (where, 'camera block', [hex(o) for o in range(0xD0) if got[0][o] != want[0][o]])
    assert got[1] == want[1], (where, 'pool', [hex(POOL + 4 * i) for i in range(len(want[1])) if got[1][i] != want[1][i]])
    assert got[2] == want[2], (where, 'scratch',
                               [hex(SCRATCH + 4 * i) for i in range(len(want[2])) if got[2][i] != want[2][i]])
    return 'ok'


def captured_case(ram, spad, mode):
    cam = ram[CAM:CAM + 0xD0]
    pool = words(ram[POOL:POOL + POOL_SIZE])
    scratch = words(spad[SCRATCH - 0x70000000:SCRATCH - 0x70000000 + SCRATCH_SIZE])
    a4 = struct.unpack_from('<I', ram, PLAYER_A4)[0]
    return cam, pool, scratch, a4, mode


def grab_case(elf, n, rng):
    eye = [f2b(rng.uniform(100, 400)) for _ in range(3)] + [f2b(1.0)]
    d = rng.choice((1.0, 4.0, 5.49, 5.5, 5.51, 8.0, 60.0))
    t = [struct.unpack('<f', struct.pack('<I', w))[0] for w in eye[:3]]
    dirv = [rng.uniform(-1, 1) for _ in range(3)]
    norm = sum(v * v for v in dirv) ** 0.5 or 1.0
    tgt = [f2b(a + d * v / norm) for a, v in zip(t, dirv)] + [f2b(rng.choice((1.0, 0.0)))]
    e = rvr.RvrEE(elf)
    e.write(0x8105D0, struct.pack('<4I', *eye))
    e.write(0x8105E0, struct.pack('<4I', *tgt))
    e.save(SDK_MODE, 1)
    e.r[29] = STACK_TOP
    e.call(GRAB, ())
    want = (e.r[2] & M32, words(e.read(0x700038A0, 16)), e.load(0x70003A20))
    s38A0, s3A20, res = (U32 * 4)(), U32(0), C.c_int32(0)
    assert n.shim_grab((U32 * 4)(*eye), (U32 * 4)(*tgt), s38A0, C.byref(s3A20), C.byref(res)) == 0
    got = (res.value & M32, list(s38A0), s3A20.value)
    assert got == want, ('00193660', got, want)
    return d


def transpose_case(elf, n, rng):
    m = [rng.getrandbits(32) for _ in range(16)]
    e = rvr.RvrEE(elf)
    e.write(MATRIX_IN, struct.pack('<16I', *m))
    e.r[29] = STACK_TOP
    e.call(TRANSPOSE, (MATRIX_OUT, MATRIX_IN))
    want = words(e.read(MATRIX_OUT, 64))
    out = (U32 * 16)()
    n.shim_transpose(out, (U32 * 16)(*m))
    assert list(out) == want, ('00102798', m)
    # in place (out == m), as 0018C0D0 never does but the routine allows
    e.call(TRANSPOSE, (MATRIX_IN, MATRIX_IN))
    inplace = (U32 * 16)(*m)
    n.shim_transpose(inplace, inplace)
    assert list(inplace) == words(e.read(MATRIX_IN, 64)), ('00102798 in place', m)


def main():
    elf = read_elf()
    n = build_native(elf)
    rng = random.Random(0x18C0D0)
    stats = {'random': 0, 'refused': 0, 'captured': 0, 'grab': 0, 'transpose': 0}
    for case in [random_case(rng) for _ in range(RM.pick(6000, 400))]:
        r = compare_commit(elf, n, None, None, case, ('random', case[4], case[0][4], case[0][6]))
        stats['random' if r == 'ok' else 'refused'] += 1
    assert stats['refused'] * 20 < stats['random'], ('too many refused cases', stats)
    for name, ram_path, spad_path in captures():
        ram, spad = ram_path.read_bytes(), spad_path.read_bytes()
        for mode in (0, 1):
            r = compare_commit(elf, n, ram, spad, captured_case(ram, spad, mode), (name, mode))
            assert r == 'ok', (name, mode, 'the original refused a captured state')
            stats['captured'] += 1
    distances = set()
    for _ in range(RM.pick(2000, 120)):
        distances.add(grab_case(elf, n, rng))
        stats['grab'] += 1
    for _ in range(RM.pick(200, 20)):
        transpose_case(elf, n, rng)
        stats['transpose'] += 1
    print(f'camera live reference: PASS ({RM.MODE}: 0018C0D0 on {stats["random"]} random states '
          f'({stats["refused"]} refused by the EE model, not compared) and {stats["captured"]} captured states '
          f'(both arguments), camera block, pool and scratch word for word; 00193660 on {stats["grab"]} cases '
          f'around 5.5; 00102798 on {stats["transpose"]} matrices, also in place)')


if __name__ == '__main__':
    main()
