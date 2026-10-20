#!/usr/bin/env python3
"""AREA00's surface VU1 program and its chain-page walk (2026-10-19).

001E9280 (the surface owner 0015AB00's packet builder, NEARMISS C in the
decomp) hands 001CB760 the static packet D_00234FE0: the floor program's
VIF set-up and one MPG of 142 instructions (ELF 0x00235010), then six
24-qword batches UNPACKed to TOPS (MSCAL 0, then MSCNT). This test executes
the ORIGINAL microcode on the shared VU1 machine (tools/chain_page_model.py
VuOracle) and compares the native translation
(em_vu1_surface_program_mscal, em_vu1_page_programs.h) and the native chain
page (em_chain_page.c) with it:

  * captured pages: every surface run of each retained AREA00 fork
    snapshot's latest page (and AREA01 a01_07's, whose last page already
    draws AREA00), re-linked as REF transfers in a page of their own,
    through the model page and the native page (every kicked GIF byte,
    every GS primitive, the batch counts);
  * synthetic batches: captured constants with perturbed points (their w
    is the colour's alpha), camera rows, colour and fog rows and random
    starting registers (clip paths, the FCAND history, divisions by tiny
    and zero lanes, alpha below 0 and above 255, exponent-255 words), MSCAL
    then MSCNT, comparing all data memory and every register the program
    writes (VF, VI, ACC, Q, I, P and the clip history);
    the live (host FPU) path and the model path (em_ee_float.h's integer
    lanes) must both equal the original, faults included;
  * fail-stops: an MSCNT before any surface batch, a truncated MPG and the
    missing program packet fault.

Reads only the user's own ELF and fork snapshots (no capture data is
stored). EM_TEST_FULL=1 runs every snapshot and the full synthetic sweep.
Runs natively on arm64 macOS.
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
import test_shadow_original_reference as S  # noqa: E402
import chain_page_model as M  # noqa: E402
import reference_root as refs  # noqa: E402  (EM_REFERENCE: fork by default)

OUT = ROOT / 'build/area00/surface_vu'
ROUTE_A00 = refs.capture('s87/route_a00')
LEVEL_EXIT = refs.capture('s87/route_a01') / 'a01_07_level_exit'
SURFACE = M.PROGRAM_SURFACE
BATCHES, BATCH_QWC, POINTS, MPG_WORD = 6, 0x1A, 24, 0x4A8E0000
PAGE = 0x1EF0000
RESUME = 47 * 8                     # micro 0x02F: after the end bit's delay slot

SHIM = r'''
static unsigned nk, nw, meta[4*256], words[4*64*256];
void em_chain_page_test_kick(unsigned program, unsigned at, const void *bytes, unsigned count)
{
    if (nk >= 256 || nw + 4*count > sizeof words/sizeof words[0]) abort();
    unsigned *k = meta + 4*nk++;
    k[0] = program; k[1] = at; k[2] = nw; k[3] = count;
    memcpy(words + nw, bytes, 16*count); nw += 4*count;
}
void kicks_reset(void) { nk = nw = 0; }
unsigned kicks_count(void) { return nk; }
const unsigned *kicks_meta(void) { return meta; }
const unsigned *kicks_words(void) { return words; }
int shim_surface(EmVu1PRegs *r, EmVu1PQword *dmem, Kicks *k, unsigned top)
{
    k->n = 0;
    return em_vu1_surface_program_mscal(r, dmem, kick_cb, k, top);
}
int shim_surface_model(EmVu1PRegs *r, EmVu1PQword *dmem, Kicks *k, unsigned top)
{
    k->n = 0;
    return em_vu1p_mscal(EM_VU1P_SURFACE, 0, r, dmem, kick_cb, k, top);
}
'''


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    src, lib = OUT / 'shim.c', OUT / 'surface.dylib'
    src.write_text(P.SHIM + SHIM)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-DEM_CHAIN_PAGE_TEST_HOOK', '-shared', '-fPIC', '-Isrc', str(src),
                    'src/game/em_chain_page.c', 'src/game/em_gs_blocks_original.c',
                    'src/game/em_load_veil_particles.c', '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    n.shim_page.argtypes = [C.POINTER(P.Region), C.c_uint, C.c_uint32, C.POINTER(C.c_uint32), C.c_uint,
                            C.POINTER(P.GsPrim), C.POINTER(P.PrimQ), C.c_uint, C.POINTER(P.Counts),
                            C.POINTER(C.c_uint32)]
    n.shim_surface.argtypes = [C.POINTER(P.Regs), C.c_void_p, C.POINTER(P.Kicks), C.c_uint]
    n.shim_surface_model.argtypes = n.shim_surface.argtypes
    n.kicks_meta.restype = n.kicks_words.restype = C.POINTER(C.c_uint32)
    return n


# ------------------------------------------------------------- captures ---

def surface_runs(ram):
    """Each run of D_00234FE0 in the snapshot's latest page: the CALL, then
    [(data address, qwc)] of the transfers through its last batch."""
    p = M.Page(M.ram_reader(ram))
    p.dma(M.latest_start(ram))
    runs, cur, depth, batches = [], None, 0, 0
    for at, tid, qwc, addr in p.transfers:
        if tid == 5 and addr == SURFACE and depth == 0:
            cur, batches = [('call', SURFACE)], 0
            runs.append(cur)
            depth += 1
            continue
        if tid == 5:
            depth += 1
        elif tid == 6:
            depth -= 1
            continue
        if cur is None or depth != 0 or not qwc:
            continue
        cur.append((addr if tid == 3 else at + 16, qwc))
        if qwc == BATCH_QWC:
            batches += 1
            if batches == BATCHES:
                cur = None
    for r in runs:
        if sum(q == BATCH_QWC for _a, q in r[1:]) != BATCHES:
            raise AssertionError(f'a run of {SURFACE:#x} without {BATCHES} batches')
    return runs


def page_for(ram, runs, call=True):
    """A page of its own: NEXT to the body, each run's CALL of D_00234FE0
    and its transfers as REFs, then the end link (base + 0x20)."""
    b = bytearray(ram)
    struct.pack_into('<4I', b, PAGE, 0x20000000, PAGE + 0x40, 0, 0)
    cur = PAGE + 0x40
    for run in runs:
        if call:
            struct.pack_into('<4I', b, cur, 0x50000000, run[0][1], 0, 0)
            cur += 16
        for data, qwc in run[1:]:
            struct.pack_into('<4I', b, cur, 0x30000000 | qwc, data, 0, 0)
            cur += 16
    struct.pack_into('<4I', b, cur, 0x20000000, PAGE + 0x20, 0, 0)
    return b


def native_kicks():
    meta, words = N.kicks_meta(), N.kicks_words()
    out = []
    for i in range(N.kicks_count()):
        program, at, first, count = meta[4*i:4*i+4]
        out.append((program, at, struct.pack('<%dI' % (4*count), *words[first:first+4*count])))
    return out


def compare_page(data, label):
    model = M.Page(M.ram_reader(data), ELF)
    model.run(PAGE)
    N.kicks_reset()
    rc, prims, q, counts, out = P.native_page(N, [(0, bytes(data))], PAGE, [], 8192)
    assert rc == 0, (label, 'native page fault', out)
    got = native_kicks()
    want = list(model.kicks)
    assert len(got) == len(want), (label, 'kicks', len(got), len(want))
    for i, (a, b) in enumerate(zip(got, want)):
        assert a[:2] == b[:2] and a[2] == b[2][:len(a[2])] and len(a[2]) == len(b[2]), \
            (label, 'kick', i, a[:2], b[:2])
    P.compare_prims(model.gs.prims, prims, q, out[2], label)
    assert counts.mscal_surface == model.mscal_counts.get(SURFACE, 0) > 0, (label, counts.mscal_surface)
    assert counts.mscal_floor == 0 and counts.mscal_ripple == 0, label
    return len(got), out[2], counts.mscal_surface


def captured(path):
    ram = (path / 'eeMemory.bin').read_bytes()
    runs = surface_runs(ram)
    if not runs:
        return path.name, 0, 0, 0, 0
    kicks, prims, batches = compare_page(page_for(ram, runs), path.name)
    return path.name, len(runs), batches, kicks, prims


# ------------------------------------------------------------ synthetic ---

def seed_blocks(ram):
    """The first run's constant rows (UNPACK to 0x3F8), camera rows
    (UNPACK to 0) and first batch's points, from the snapshot."""
    run = surface_runs(ram)[0]
    consts = matrix = points = None
    for data, qwc in run[1:]:
        v = struct.unpack_from('<I', ram, data + 12)[0]
        if qwc == 9 and v == 0x6C0803F8:
            consts = ram[data + 16:data + 16 * 9]
        elif qwc == 5 and v == 0x6C040000:
            matrix = ram[data + 16:data + 16 * 5]
        elif qwc == BATCH_QWC and points is None:
            points = ram[data + 16:data + 16 * (POINTS + 1)]
    assert consts and matrix and points
    return consts, matrix, points


def fbits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def wbits(rng):
    """A point w (the colour's alpha before the clamp to 0..255)."""
    return rng.choice((rng.uniform(-40, 0), rng.uniform(0, 255), rng.uniform(255, 600),
                       struct.unpack('<f', struct.pack('<I', rng.choice((0, 0x80000000, 1, 0x00400000,
                                                                         0x437F0000, 0x437F0001))))[0]))


def synthetic_case(index):
    rng = random.Random(SURFACE + index)
    consts, matrix, points = SEED
    mem = bytearray(16384)
    mem[16 * 0x3F8:16 * 0x400] = consts
    mem[0:64] = matrix
    top = rng.choice((0x20, 0x1B0))
    other = 0x1B0 if top == 0x20 else 0x20
    pts = bytearray(points)
    raw_w = {}
    for k in range(POINTS):
        x, y, z, w = struct.unpack_from('<4f', pts, 16 * k)
        mode = index % 6
        if mode == 1:
            y += rng.uniform(-40, 40)
        elif mode == 2:
            x, z = x + rng.uniform(-300, 300), z + rng.uniform(-300, 300)
        elif mode == 3 and rng.random() < 0.3:
            y = struct.unpack('<f', struct.pack('<I', rng.choice((0, 0x80000000, 1, 0x00400000))))[0]
        elif mode == 4:
            w = wbits(rng)
        elif mode == 5 and rng.random() < 0.2:          # exponent-255 alpha: only MOVE / MAX / MINI / FTOI read it
            raw_w[k] = rng.choice((0x7F800000, 0xFF800000, 0x7FC00000, 0x7F800001))
        struct.pack_into('<4f', pts, 16 * k, x, y, z, w)
        if k in raw_w:
            struct.pack_into('<I', pts, 16 * k + 12, raw_w[k])
    mem[16 * top:16 * (top + POINTS)] = pts
    mem[16 * other:16 * (other + POINTS)] = pts[::-1] if index & 1 else pts
    if index % 7 == 3:                       # the camera rows: push vertices out of the clip volume
        for r in range(4):
            row = list(struct.unpack_from('<4f', mem, 16 * r))
            row = [a * rng.uniform(0.2, 3) for a in row]
            struct.pack_into('<4f', mem, 16 * r, *row)
    if index % 3 == 1:                       # colour, fog and texture rows
        for r in (0x3F9, 0x3FD, 0x3FA, 0x3F8):
            row = list(struct.unpack_from('<4f', mem, 16 * r))
            row = [a * rng.uniform(-2, 2) for a in row]
            struct.pack_into('<4f', mem, 16 * r, *row)
    if index % 11 == 5:                      # the light at a vertex's height: n.y == 0
        row = list(struct.unpack_from('<4f', mem, 16 * 0x3FB))
        row[1] = struct.unpack_from('<f', mem, 16 * top + 4)[0]
        struct.pack_into('<4f', mem, 16 * 0x3FB, *row)
    regs = [[fbits(rng.uniform(-50, 50)) for _ in range(4)] for _ in range(32)]
    regs[0] = [0, 0, 0, 0x3F800000]
    vi = [rng.randrange(0x10000) for _ in range(16)]
    vi[0] = 0
    return mem, top, other, regs, vi, rng.randrange(1 << 24)


def oracle_batch(vu, top, entry):
    vu.top, vu.kicks, vu.events, vu.watch = top, [], [], set()
    vu.pending, vu.cycle, vu.q_ready, vu.p_ready = [], 0, 0, 0
    vu.ready = [[0] * 4 for _ in range(32)]
    vu.run(entry)
    return [(e[1], e[3]) for e in vu.events if e[0] == 'kick']


def regs_of_oracle(vu):
    return ([list(r) for r in vu.v], list(vu.vi), list(vu.accw), vu.q, vu.iw, vu.p, vu.cf)


def regs_of_native(r):
    return ([[r.vf[i][k] for k in range(4)] for i in range(32)], list(r.vi), list(r.acc), r.q, r.i, r.p, r.cf)


def synthetic(index):
    mem, top, other, regs, vi, cf = synthetic_case(index)
    vu = M.VuOracle(ELF)
    S.load_program(vu, ELF, SURFACE)
    vu.mem[:] = mem
    vu.v = [list(r) for r in regs]
    vu.vi = list(vi)
    vu.cf = cf
    r = P.Regs()
    for i in range(32):
        for k in range(4):
            r.vf[i][k] = regs[i][k]
    for i in range(16):
        r.vi[i] = vi[i]
    r.cf = cf
    dmem = (C.c_uint8 * 16384).from_buffer_copy(mem)
    kicks = P.Kicks()
    out = []
    for step, (t, entry) in enumerate(((top, 0), (other, RESUME))):
        try:
            want = oracle_batch(vu, t, entry)
            fault = None
        except M.ModelError as e:
            want, fault = None, str(e)
        # The model path from copies of the same state, then the live path.
        r2, dmem2, kicks2 = P.Regs(), (C.c_uint8 * 16384).from_buffer_copy(dmem), P.Kicks()
        C.memmove(C.byref(r2), C.byref(r), C.sizeof(r))
        rc2 = N.shim_surface_model(C.byref(r2), dmem2, C.byref(kicks2), t)
        rc = N.shim_surface(C.byref(r), dmem, C.byref(kicks), t)
        assert rc == rc2 and bytes(dmem) == bytes(dmem2) and bytes(r) == bytes(r2), (index, step, 'host / model path')
        assert kicks.n == kicks2.n and all(kicks.at[i] == kicks2.at[i] for i in range(kicks.n)), (index, step)
        assert bytes(kicks.mem)[:16384 * kicks.n] == bytes(kicks2.mem)[:16384 * kicks.n], (index, step)
        if fault:
            assert rc == 2, (index, step, 'the original faults (not established), native rc', rc, fault)
            out.append('fault')
            return out
        assert rc == 0, (index, step, 'native rc', rc)
        got = [kicks.at[i] for i in range(kicks.n)]
        assert got == [a for a, _ in want], (index, step, 'kick addresses')
        for i, (at, raw) in enumerate(want):
            snap = bytes(kicks.mem[i * 16384:(i + 1) * 16384])
            have = b''.join(snap[16 * ((at + q) & 1023):16 * ((at + q) & 1023) + 16] for q in range(len(raw) // 16))
            assert have == raw, (index, step, 'kicked packet')
        assert bytes(dmem) == bytes(vu.mem), (index, step, 'dmem',
                                              next(i for i in range(16384) if dmem[i] != vu.mem[i]) // 16)
        a, b = regs_of_native(r), regs_of_oracle(vu)
        for name, x, y in zip(('vf', 'vi', 'acc', 'q', 'i', 'p', 'cf'), a, b):
            assert x == y, (index, step, 'register', name, x, y)
        assert entry != RESUME or vu.resume == RESUME
        out.append(len(want))
    return out


# ----------------------------------------------------------- fail-stops ---

def failstops(ram):
    runs = surface_runs(ram)
    data = page_for(ram, runs[:1])
    checks = []
    # MSCNT before any batch of the program: the first batch's MSCAL becomes MSCNT.
    b = bytearray(data)
    first_batch = next(d for d, q in runs[0][1:] if q == BATCH_QWC)
    struct.pack_into('<I', b, first_batch + 0x190, 0x17000000)
    checks.append(('MSCNT before a surface batch', b, 5))
    # A truncated MPG (141 instructions) is not the program.
    b = bytearray(data)
    mpg = SURFACE + 0x2C
    assert struct.unpack_from('<I', b, mpg)[0] == MPG_WORD
    struct.pack_into('<I', b, mpg, MPG_WORD - 0x10000)
    checks.append(('truncated surface MPG', b, 5))
    out = 0
    for label, page, want in checks:
        N.kicks_reset()
        rc, _p, _q, _c, res = P.native_page(N, [(0, bytes(page))], PAGE, [], 8192)
        assert rc < 0 and res[0] == want, (label, rc, res, want)
        out += 1
    # The packet unmapped: the CALL target is unreadable (the live fault
    # before the packet was exported).
    size = M.PROGRAM_PACKET_SIZE[SURFACE]
    regions = [(0, bytes(data[:SURFACE])), (SURFACE + size, bytes(data[SURFACE + size:]))]
    rc, _p, _q, _c, res = P.native_page(N, regions, PAGE, [], 8192)
    assert rc < 0 and res[0] == 2 and res[1] == SURFACE, ('missing program packet', rc, res)
    return out + 1


def main():
    global ELF, N, SEED
    start = time.time()
    ELF = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    assert struct.unpack_from('<I', ELF, SURFACE + 0x2C - 0x100000 + 0x300)[0] == MPG_WORD
    N = build()
    paths = sorted(p for p in ROUTE_A00.glob('a00_*') if (p / 'eeMemory.bin').exists()) + [LEVEL_EXIT]
    chosen = RM.select(paths, 3, 0x234FE0, keep=lambda i, p: i == 0)
    total = dict(runs=0, batches=0, kicks=0, prims=0)
    for name, runs, batches, kicks, prims in RM.parallel_map(captured, chosen):
        print(f'{name}: PASS {runs} surface runs, {batches} batches, {kicks} kicks, {prims} primitives', flush=True)
        total['runs'] += runs
        total['batches'] += batches
        total['kicks'] += kicks
        total['prims'] += prims
    assert total['runs'] > 0 and total['prims'] > 0
    seed_ram = (paths[0] / 'eeMemory.bin').read_bytes()
    SEED = seed_blocks(seed_ram)
    faults = failstops(seed_ram)
    kinds = {'batches': 0, 'faults': 0}
    for res in RM.parallel_map(synthetic, list(range(RM.pick(1500, 60)))):
        kinds['batches'] += sum(1 for x in res if x != 'fault')
        kinds['faults'] += sum(1 for x in res if x == 'fault')
    RM.banner(RM.part(len(chosen), len(paths), 'AREA00 surface snapshots'))
    print('PASS', total, 'synthetic', kinds, faults, 'fail-stop contracts', f'({time.time() - start:.1f}s)')


if __name__ == '__main__':
    main()
