#!/usr/bin/env python3
"""The VU1 page programs' two arithmetic paths against each other, whole
programs over the captured pages (docs/PERFORMANCE.md Q2).

src/game/em_vu1_page_programs.h runs each MSCAL on one of two paths
(em_vu1p_mscal's `host`): 0, the model's integer lanes (em_ee_float.h), and
1, the live one, whose multiplies, adds, subtracts and divides of finite
operands run on the host FPU in the round-toward-zero / flush-to-zero
environment (em_vu_host_lanes.h), whose exponent-255 operations run on the
model, and which leaves that environment for the EFU's double arithmetic
and for every XGKICK consumer. The header claims the two write the same
words for every input. The per-operation equality is
tools/test_vu_host_lanes.py's; this test runs whole programs both ways:

Every MSCAL below runs on the host path first (its kicks go to the real
consumer), then on the model path from a copy of the same registers and
data memory, with the host run's kick results replayed. They must agree on
the result code, all 16 KiB of data memory, every register (VF, VI, ACC, Q,
I, R, P, the clip history), the number and address of the kicks and the
data memory at each kick (bytewise for the first 64 of an MSCAL, by a
64-bit FNV-1a beyond). Every kick consumer must run in the caller's FP
environment and the caller's environment must be restored on return.

A. AREA11: every route capture's latest page (tools/test_chain_page_reference
   part A's walk: the depth-of-field pass in pass mode, the non-port CALLs
   walked over) with the weather's 108 snow MSCALs: quick, one beat in
   three; EM_TEST_FULL=1, all of them.
B. AREA01: every kind-6 run (tools/test_level2_kind6_vu_reference) and every
   floor-field and ripple run (tools/test_level2_floor_vu_reference) of the
   AREA01 captures, re-linked into pages of their own.
C. The background grid program: the channel-3 list's upload of each
   AREA11 capture (tools/test_background_reference replay) and views swept
   over it.
D. Synthetic MSCALs: the reference tests' generators (lane, sprite, snow,
   streak, kind 2, kind 6, floor, ripple) over the memories captured in A
   and B, random starting registers.
E. Fuzz, every program: captured memories and registers with random words
   replaced by exponent-255 words, denormals, signed zeros and extreme
   finite values, and consumers that refuse a kick (the faults must be
   identical too).

Reads only the user's own ELF and captures; prints counts only.
"""
import ctypes as C
import random
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as RM  # noqa: E402
import test_chain_page_reference as P  # noqa: E402
import chain_page_model as M  # noqa: E402

OUT = ROOT / 'build/vu_page_host_differential'
NAMES = ('lane', 'sprite', 'snow', 'kind6', 'streak', 'kind2', 'grid', 'floor', 'ripple')
LANE, SPRITE, SNOW, KIND6, STREAK, KIND2, GRID, FLOOR, RIPPLE = range(9)

PRELUDE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "game/em_vu1_page_programs.h"

/* The caller's FP environment, read where the kick consumer runs. */
static uint64_t fp_env(void)
{
#if EM_VU_HOST_LANES && defined(__aarch64__)
    uint64_t v;
    __asm__ volatile("mrs %0, fpcr" : "=r"(v) : : "memory");
    return v;
#elif EM_VU_HOST_LANES
    uint32_t v;
    __asm__ volatile("stmxcsr %0" : "=m"(v) : : "memory");
    return v;
#else
    return 0;
#endif
}

#define DIFF_KEEP 64u
#define DIFF_KICKS 131072u   /* more than any MSCAL can kick (0x10000 batches) */
typedef struct {
    uint32_t mscals[9], kicks[9], faults[9], kick_faults[9];
    uint32_t mismatches, first_program, first_what, first_detail;
    uint32_t env_bad, seeds[9], overflow;
} DiffStats;
DiffStats g_diff;

static struct {
    EmVu1PKick kick;
    void *ctx;
    uint32_t n, replayed, replay_bad;
    uint64_t env;
    uint32_t at[DIFF_KICKS];
    int rc[DIFF_KICKS];
    uint64_t hash[DIFF_KICKS];
    EmVu1PQword mem[DIFF_KEEP][1024];
} R;

static uint64_t fnv(const void *p, size_t n)
{
    const uint8_t *b = p;
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 0x100000001B3ull;
    return h;
}

static int rec_kick(void *ctx, const EmVu1PQword *dmem, uint32_t at)
{
    (void)ctx;
    if (fp_env() != R.env) g_diff.env_bad++;
    const int rc = R.kick(R.ctx, dmem, at);
    if (R.n >= DIFF_KICKS) g_diff.overflow++;
    else {
        R.at[R.n] = at;
        R.rc[R.n] = rc;
        R.hash[R.n] = fnv(dmem, 16384);
        if (R.n < DIFF_KEEP) memcpy(R.mem[R.n], dmem, 16384);
    }
    R.n++;
    return rc;
}

static int replay_kick(void *ctx, const EmVu1PQword *dmem, uint32_t at)
{
    (void)ctx;
    const uint32_t i = R.replayed++;
    if (i >= R.n || i >= DIFF_KICKS) { R.replay_bad |= 1u; return 1; }
    if (R.at[i] != at) R.replay_bad |= 2u;
    if (i < DIFF_KEEP ? memcmp(R.mem[i], dmem, 16384) != 0 : R.hash[i] != fnv(dmem, 16384)) R.replay_bad |= 4u;
    return R.rc[i];
}

#define SEED_KEEP 48u
static EmVu1PRegs g_seed_regs[9][SEED_KEEP];
static EmVu1PQword g_seed_mem[9][SEED_KEEP][1024];
static uint32_t g_seed_top[9][SEED_KEEP];

static EmVu1PRegs g_first_regs;
static EmVu1PQword g_first_mem[1024];
static uint32_t g_first_top;

static void mismatch(unsigned program, uint32_t what, uint32_t detail)
{
    if (!g_diff.mismatches++) {
        g_diff.first_program = program;
        g_diff.first_what = what;
        g_diff.first_detail = detail;
    }
}

static int diff_mscal(unsigned program, EmVu1PRegs *r, EmVu1PQword *dmem, EmVu1PKick kick, void *ctx,
                      uint32_t top)
{
    static EmVu1PRegs r0;
    static EmVu1PQword m0[1024];
    if (program < 9 && g_diff.seeds[program] < SEED_KEEP) {
        const uint32_t s = g_diff.seeds[program]++;
        g_seed_regs[program][s] = *r;
        memcpy(g_seed_mem[program][s], dmem, sizeof m0);
        g_seed_top[program][s] = top;
    }
    r0 = *r;
    memcpy(m0, dmem, sizeof m0);
    if (!g_diff.mismatches) {
        g_first_regs = *r;
        memcpy(g_first_mem, dmem, sizeof m0);
        g_first_top = top;
    }
    R.kick = kick;
    R.ctx = ctx;
    R.n = 0;
    R.env = fp_env();
    const int rc_host = em_vu1p_mscal(program, 1, r, dmem, rec_kick, NULL, top);
    if (fp_env() != R.env) g_diff.env_bad++;
    R.replayed = 0;
    R.replay_bad = 0;
    const int rc_model = em_vu1p_mscal(program, 0, &r0, m0, replay_kick, NULL, top);
    if (program < 9) {
        g_diff.mscals[program]++;
        g_diff.kicks[program] += R.n;
        if (rc_host == EM_VU1P_FAULT_OPERAND) g_diff.faults[program]++;
        if (rc_host == EM_VU1P_FAULT_KICK) g_diff.kick_faults[program]++;
    }
    if (rc_host != rc_model) mismatch(program, 1, (uint32_t)rc_host << 8 | (uint32_t)rc_model);
    else if (R.replay_bad || R.replayed != R.n) mismatch(program, 2, R.replay_bad << 16 | R.replayed);
    else if (memcmp(&r0, r, sizeof r0)) {
        uint32_t at = 0;
        while (at < sizeof r0 && ((const uint8_t *)&r0)[at] == ((const uint8_t *)r)[at]) ++at;
        mismatch(program, 3, at);
    } else if (memcmp(m0, dmem, sizeof m0)) {
        uint32_t at = 0;
        while (at < sizeof m0 && ((const uint8_t *)m0)[at] == ((const uint8_t *)dmem)[at]) ++at;
        mismatch(program, 4, at / 16);
    }
    return rc_host;
}

#define em_vu1_lane_program_mscal(r, d, k, x) diff_mscal(EM_VU1P_LANE, r, d, k, x, 0)
#define em_vu1_sprite_program_mscal(r, d, k, x) diff_mscal(EM_VU1P_SPRITE, r, d, k, x, 0)
#define em_vu1_snow_program_mscal(r, d, k, x) diff_mscal(EM_VU1P_SNOW, r, d, k, x, 0)
#define em_vu1_kind6_program_mscal(r, d, k, x) diff_mscal(EM_VU1P_KIND6, r, d, k, x, 0)
#define em_vu1_streak_program_mscal(r, d, k, x) diff_mscal(EM_VU1P_STREAK, r, d, k, x, 0)
#define em_vu1_kind2_program_mscal(r, d, k, x) diff_mscal(EM_VU1P_KIND2, r, d, k, x, 0)
#define em_vu1_grid_program_mscal(r, d, k, x) diff_mscal(EM_VU1P_GRID, r, d, k, x, 0)
#define em_vu1_floor_program_mscal(r, d, k, x, t) diff_mscal(EM_VU1P_FLOOR, r, d, k, x, t)
#define em_vu1_ripple_program_mscal(r, d, k, x, t) diff_mscal(EM_VU1P_RIPPLE, r, d, k, x, t)

#include "game/em_chain_page.c"
'''

EPILOGUE = r'''
/* A direct MSCAL: the consumer accepts `limit` kicks, then refuses. */
typedef struct { uint32_t n, limit; } Count;
static int count_kick(void *ctx, const EmVu1PQword *dmem, uint32_t at)
{
    (void)dmem; (void)at;
    Count *k = ctx;
    return k->n++ >= k->limit;
}
int diff_direct(unsigned program, EmVu1PRegs *r, EmVu1PQword *dmem, uint32_t top, uint32_t limit)
{
    Count k = { 0, limit };
    return diff_mscal(program, r, dmem, count_kick, &k, top);
}
uint32_t diff_seed(unsigned program, unsigned i, EmVu1PRegs *r, EmVu1PQword *dmem)
{
    if (program >= 9 || i >= g_diff.seeds[program] || i >= SEED_KEEP) return 0xFFFFFFFFu;
    *r = g_seed_regs[program][i];
    memcpy(dmem, g_seed_mem[program][i], 16384);
    return g_seed_top[program][i];
}
const DiffStats *diff_stats(void) { return &g_diff; }
/* The starting state of the first MSCAL that differed (a failure's repro). */
uint32_t diff_first(EmVu1PRegs *r, EmVu1PQword *dmem)
{
    *r = g_first_regs;
    memcpy(dmem, g_first_mem, 16384);
    return g_first_top;
}
int diff_host_lanes(void) { return EM_VU_HOST_LANES; }
/* ns per MSCAL of `program` on one path over `reps` runs from the same
 * state (the kicks accepted and dropped). */
double diff_bench(unsigned program, int host, const EmVu1PRegs *r0, const EmVu1PQword *m0, uint32_t top,
                  unsigned reps)
{
    static EmVu1PRegs r;
    static EmVu1PQword m[1024];
    struct timespec a, b;
    double total = 0;
    for (unsigned i = 0; i < reps; ++i) {
        r = *r0;
        memcpy(m, m0, sizeof m);
        Count k = { 0, 0xFFFFFFFFu };
        clock_gettime(CLOCK_MONOTONIC, &a);
        (void)em_vu1p_mscal(program, host, &r, m, count_kick, &k, top);
        clock_gettime(CLOCK_MONOTONIC, &b);
        total += (double)(b.tv_sec - a.tv_sec) * 1e9 + (double)(b.tv_nsec - a.tv_nsec);
    }
    return total / reps;
}
'''


class Stats(C.Structure):
    _fields_ = [('mscals', C.c_uint32 * 9), ('kicks', C.c_uint32 * 9), ('faults', C.c_uint32 * 9),
                ('kick_faults', C.c_uint32 * 9), ('mismatches', C.c_uint32), ('first_program', C.c_uint32),
                ('first_what', C.c_uint32), ('first_detail', C.c_uint32), ('env_bad', C.c_uint32),
                ('seeds', C.c_uint32 * 9), ('overflow', C.c_uint32)]


DMEM = C.c_uint8 * 16384
WHAT = {1: 'result code (host << 8 | model)', 2: 'kicks (flags << 16 | replayed)', 3: 'register byte',
        4: 'data memory qword'}


def fail(msg):
    raise AssertionError(msg)


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    src, lib = OUT / 'shim.c', OUT / 'differential.dylib'
    src.write_text(PRELUDE + P.SHIM + EPILOGUE)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-D_POSIX_C_SOURCE=200809L', '-shared', '-fPIC', '-I' + str(ROOT / 'src'), str(src),
                    str(ROOT / 'src/game/em_gs_blocks_original.c'), str(ROOT / 'src/game/em_load_veil_particles.c'),
                    '-o', str(lib)], check=True)
    n = C.CDLL(str(lib))
    Pp = C.POINTER
    n.shim_page.argtypes = [Pp(P.Region), C.c_uint, C.c_uint32, Pp(C.c_uint32), C.c_uint32, Pp(P.GsPrim),
                            Pp(P.PrimQ), C.c_uint32, Pp(P.Counts), Pp(C.c_uint32), Pp(P.GsEnv), Pp(C.c_uint32)]
    n.shim_sizes.restype = C.c_uint
    n.shim_env_size.restype = C.c_uint
    n.diff_direct.argtypes = [C.c_uint, Pp(P.Regs), DMEM, C.c_uint32, C.c_uint32]
    n.diff_seed.argtypes = [C.c_uint, C.c_uint, Pp(P.Regs), DMEM]
    n.diff_seed.restype = C.c_uint32
    n.diff_stats.restype = Pp(Stats)
    n.diff_first.argtypes = [Pp(P.Regs), DMEM]
    n.diff_first.restype = C.c_uint32
    n.diff_bench.argtypes = [C.c_uint, C.c_int, Pp(P.Regs), DMEM, C.c_uint32, C.c_uint]
    n.diff_bench.restype = C.c_double
    s = n.shim_sizes()
    if (s & 0xFFF, s >> 12 & 0xFFF, s >> 24) != (C.sizeof(P.GsPrim), C.sizeof(P.Regs), C.sizeof(P.Counts)):
        fail('ctypes layout differs from the headers')
    return n


def check(lib, where):
    st = lib.diff_stats().contents
    if st.mismatches:
        regs, mem = P.Regs(), DMEM()
        top = lib.diff_first(C.byref(regs), mem)
        (OUT / 'first_mismatch.bin').write_bytes(bytes(regs) + bytes(mem) + struct.pack('<II', top, st.first_program))
        fail(f'{where}: {st.mismatches} MSCALs differ between the host and the model paths; the first: '
             f'{NAMES[st.first_program] if st.first_program < 9 else st.first_program} '
             f'{WHAT.get(st.first_what)} {st.first_detail:#x}')
    if st.overflow:
        fail(f'{where}: an MSCAL kicked more often than the recorder holds')
    if st.env_bad:
        fail(f'{where}: {st.env_bad} kicks or returns saw an FP environment other than the caller\'s')


def page(lib, data, start, skip, pinfo, where):
    rc, _prims, _q, counts, out = P.native_page(lib, [(0, data)], start, skip, 8192, pass_mode=pinfo or (0, 0, 0))
    if rc or out[0]:
        fail(f'{where}: the native page faulted ({out[0]} at {out[1]:#x})')
    check(lib, where)
    return counts


# ------------------------------------------------------------ A. AREA11 ---
def part_a(lib, tally):
    beats = RM.select(P.BEATS, max(1, len(P.BEATS) // 3), 0xD1FF, keep=lambda i, b: i == 0)
    for beat in beats:
        ram = (P.ROUTE / beat / 'eeMemory.bin').read_bytes()
        start = M.latest_start(ram)
        pinfo = P.pass_of(ram, start)
        skip = [a for a in P.page_skip(ram, start, True) if not pinfo or a != pinfo[0]]
        counts = page(lib, ram, start, skip, pinfo, beat)
        if counts.mscal_snow != 108:
            fail(f'{beat}: {counts.mscal_snow} snow MSCALs, not the weather\'s 108')
        tally['area11_pages'] += 1
    return len(beats)


# ------------------------------------------------------------ B. AREA01 ---
def part_b(lib, tally):
    import test_level2_kind6_vu_reference as K
    import test_level2_floor_vu_reference as F
    names = RM.select(list(K.CAPTURES), len(K.QUICK), 0x6D3, keep=lambda i, n: n in K.QUICK)
    for name in names:
        ram = (K.S87 / name / 'eeMemory.bin').read_bytes()
        runs = K.kind6_runs(ram)
        if not runs:
            fail(f'{name}: no kind-6 run')
        page(lib, K.page_for(ram, runs), K.PAGE, [], None, name)
        tally['kind6_pages'] += 1
    paths = [F.ARRIVAL] + sorted(p for p in F.ROUTE.glob('a01_*') if (p / 'eeMemory.bin').exists())
    for path in RM.select(paths, 3, 0x2345E0, keep=lambda i, p: p == F.ARRIVAL):
        ram = (path / 'eeMemory.bin').read_bytes()
        runs = F.floor_runs(ram, F.FLOOR) + F.floor_runs(ram, F.RIPPLE)
        if runs:
            page(lib, bytes(F.page_for(ram, runs)), F.PAGE, [], None, path.name)
            tally['floor_pages'] += 1
    seed_ram = (F.ARRIVAL / 'eeMemory.bin').read_bytes()
    F.SEED = {program: F.seed_blocks(seed_ram, program) for program in F.SHAPE}
    return F


# -------------------------------------------------------------- C. grid ---
def grid_image(dmem):
    image = bytearray(16384)
    for a, q in dmem.items():
        image[16 * a:16 * a + 16] = q
    return image


def direct(lib, program, regs, image, top=0, limit=0xFFFFFFFF, where=''):
    mem = DMEM.from_buffer_copy(bytes(image))
    rc = lib.diff_direct(program, C.byref(regs), mem, top, limit)
    check(lib, where or NAMES[program])
    return rc, regs, mem


def part_c(lib, tally):
    import test_background_reference as B
    reference = ROOT.parent / 'Extermination/build/startup-reference'
    template = None
    for name in B.CAPTURES:
        ram = (reference / name).read_bytes()
        ctx = B.u32(ram, B.CTX_PTR) & 0x1FFFFFF
        _calls, kicks = B.replay(ram, B.u32(ram, ctx + 0x1D8))
        template = kicks[0][2]
        regs = P.Regs()
        regs.vf[0][3] = 0x3F800000
        rc, _r, _m = direct(lib, GRID, regs, grid_image(template), where=f'grid {name}')
        if rc:
            fail(f'grid {name}: the program faulted ({rc})')
        tally['grid_captures'] += 1

    def sweep(_native, dmem, label):
        regs = P.Regs()
        regs.vf[0][3] = 0x3F800000
        direct(lib, GRID, regs, grid_image(dmem), where=label)
        return 0
    B.compare_grid = sweep
    for s in range(RM.pick(64, 4)):
        B.sweep_case((s, template))
        tally['grid_sweeps'] += 1
    return template


# --------------------------------------------------------- D. synthetic ---
def seeds_of(lib, program):
    out = []
    for i in range(48):
        regs, mem = P.Regs(), DMEM()
        top = lib.diff_seed(program, i, C.byref(regs), mem)
        if top == 0xFFFFFFFF:
            break
        out.append((regs, bytes(mem), top))
    return out


def random_regs(rng, special=0.0):
    regs = P.Regs()
    for i in range(1, 32):
        for k in range(4):
            regs.vf[i][k] = word(rng, special)
    regs.vf[0][3] = 0x3F800000
    for i in range(1, 16):
        regs.vi[i] = rng.getrandbits(16)
    for k in range(4):
        regs.acc[k] = word(rng, special)
    regs.q, regs.i, regs.p = word(rng, special), word(rng, special), word(rng, special)
    regs.r, regs.cf = rng.getrandbits(23), rng.getrandbits(24)
    return regs


SPECIAL = (0x7F800000, 0xFF800000, 0x7FC00000, 0x7F800001, 0xFFFFFFFF, 0x00000000, 0x80000000, 0x00000001,
           0x807FFFFF, 0x00800000, 0x80800000, 0x7F7FFFFF, 0xFF7FFFFF, 0x3F800000, 0x00400000)


def word(rng, special):
    if rng.random() < special:
        return rng.choice(SPECIAL)
    return P.finite(rng)


def part_d(lib, elf, F, tally):
    gens = {LANE: P.lane_case, SPRITE: P.sprite_case, SNOW: P.sprite_case}
    n = RM.pick(400, 25)
    rng = random.Random(0xD1FF0)
    sprite_seeds = seeds_of(lib, SPRITE)
    for program in (LANE, SPRITE, SNOW, STREAK, KIND2, KIND6):
        if program in (STREAK, KIND2):
            src = [P.streak_seed(elf, (mem,), M.PROGRAM_STREAK if program == STREAK else M.PROGRAM_KIND2)[0]
                   for _r, mem, _t in sprite_seeds]
        else:
            src = [mem for _r, mem, _t in seeds_of(lib, program)]
        if program == LANE and not src:
            src = [mem for _r, mem, _t in seeds_of(lib, SPRITE)]   # the route draws no lane slot: any image
        if not src:
            fail(f'no captured {NAMES[program]} MSCAL to start from')
        for _ in range(n):
            mem0 = rng.choice(src)
            if program == KIND6:
                import test_level2_kind6_vu_reference as K
                mem, _count = K.kind6_case(rng, mem0)
            elif program == STREAK:
                mem, _count = P.streak_case(rng, (mem0,), elf)
            elif program == KIND2:
                mem, _count = P.kind2_case(rng, (mem0,), elf)
            else:
                mem, _count = gens[program](rng, (mem0,), elf)
            direct(lib, program, random_regs(rng), mem, where=f'synthetic {NAMES[program]}')
            tally['synthetic'] += 1
    for program, prog_id in ((F.FLOOR, FLOOR), (F.RIPPLE, RIPPLE)):
        for index in range(RM.pick(300, 15)):
            mem, top, other, vf, vi, cf = F.synthetic_case(index, program)
            regs = P.Regs()
            for i in range(32):
                for k in range(4):
                    regs.vf[i][k] = vf[i][k]
            for i in range(16):
                regs.vi[i] = vi[i]
            regs.cf = cf
            rc, regs, dm = direct(lib, prog_id, regs, mem, top, where=f'synthetic {NAMES[prog_id]}')
            if rc == 0:
                direct(lib, prog_id, regs, bytes(dm), other, where=f'synthetic {NAMES[prog_id]} MSCNT')
            tally['synthetic'] += 1


# -------------------------------------------------------------- E. fuzz ---
def part_e(lib, grid_template, tally):
    rng = random.Random(0xF022)
    pools = {p: seeds_of(lib, p) for p in range(9)}
    pools[LANE] = pools[LANE] or pools[SPRITE]
    pools[STREAK] = pools[STREAK] or pools[SPRITE]
    pools[KIND2] = pools[KIND2] or pools[SPRITE]
    pools[GRID] = pools[GRID] or [(None, bytes(grid_image(grid_template)), 0)]
    for program in range(9):
        if not pools[program]:
            fail(f'no captured {NAMES[program]} MSCAL to fuzz from')
        for _ in range(RM.pick(1500, 60)):
            _r, mem0, top = rng.choice(pools[program])
            mem = bytearray(mem0)
            for _k in range(rng.choice((1, 2, 4, 16, 64))):
                at = rng.randrange(4096) if rng.random() < 0.3 else rng.randrange(80, 130) * 4 + rng.randrange(4)
                struct.pack_into('<I', mem, 4 * at, rng.choice(SPECIAL) if rng.random() < 0.7
                                 else rng.getrandbits(32))
            regs = random_regs(rng, special=rng.choice((0.0, 0.02, 0.2)))
            if program in (FLOOR, RIPPLE):
                top = rng.choice((0x20, 0x1B0, rng.randrange(1024)))
            limit = rng.choice((0xFFFFFFFF, 0xFFFFFFFF, 0, 1, 3))
            direct(lib, program, regs, mem, top, limit, where=f'fuzz {NAMES[program]}')
            tally['fuzz'] += 1


def bench(lib):
    """ns per MSCAL, the model path against the host path, over captured
    starting states (the first of each program that has one)."""
    out = []
    for program in range(9):
        seeds = seeds_of(lib, program)
        if not seeds:
            continue
        regs, mem, top = seeds[min(len(seeds) - 1, 1)]
        reps = 400
        model = lib.diff_bench(program, 0, C.byref(regs), DMEM.from_buffer_copy(mem), top, reps)
        host = lib.diff_bench(program, 1, C.byref(regs), DMEM.from_buffer_copy(mem), top, reps)
        model = min(model, lib.diff_bench(program, 0, C.byref(regs), DMEM.from_buffer_copy(mem), top, reps))
        host = min(host, lib.diff_bench(program, 1, C.byref(regs), DMEM.from_buffer_copy(mem), top, reps))
        out.append(f'{NAMES[program]} {model / 1000:.1f} -> {host / 1000:.1f} us')
    return out


def main():
    t0 = time.time()
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    lib = build()
    if lib.diff_host_lanes() != 1 and sys.platform == 'darwin':
        fail('EM_VU_HOST_LANES is 0 on this Mac: the host path would be the model itself')
    tally = {'area11_pages': 0, 'kind6_pages': 0, 'floor_pages': 0, 'grid_captures': 0, 'grid_sweeps': 0,
             'synthetic': 0, 'fuzz': 0}
    n_beats = part_a(lib, tally)
    F = part_b(lib, tally)
    template = part_c(lib, tally)
    part_d(lib, elf, F, tally)
    part_e(lib, template, tally)
    st = lib.diff_stats().contents
    for program in range(9):
        if not st.mscals[program]:
            fail(f'no {NAMES[program]} MSCAL ran')
    if sum(st.faults) == 0 or sum(st.kick_faults) == 0:
        fail('the fuzz reached no operand fault or no refused kick')
    RM.banner(RM.part(n_beats, len(P.BEATS), 'AREA11 route pages with the weather'),
              f"{tally['kind6_pages']} kind-6 and {tally['floor_pages']} floor / ripple AREA01 pages, "
              f"{tally['grid_captures']} grid uploads + {tally['grid_sweeps']} swept views, "
              f"{tally['synthetic']} synthetic and {tally['fuzz']} fuzzed MSCALs")
    print('MSCALs per program (kicks, operand faults, refused kicks): ' +
          ', '.join(f'{NAMES[p]} {st.mscals[p]} ({st.kicks[p]}, {st.faults[p]}, {st.kick_faults[p]})'
                    for p in range(9)))
    if '--bench' in sys.argv:
        print('model -> host, per MSCAL: ' + '; '.join(bench(lib)))
    print(f'PASS: the host and model paths agree on every MSCAL ({sum(st.mscals)}; {time.time() - t0:.1f}s)')


if __name__ == '__main__':
    main()
