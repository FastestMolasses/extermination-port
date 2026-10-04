#!/usr/bin/env python3
"""AREA01 floor-field and ripple VU1 programs and their chain-page walk.

001E9E60 (0015A2C0's eight floor fields) hands 001CB760 the static packet
D_002345E0: VIF set-up and one MPG of 153 instructions (ELF 0x00234610),
then six 24-qword batches UNPACKed to TOPS (MSCAL 0, then MSCNT).
001E7D20 (the ripple surface) hands it D_00234B00: the same set-up, an MPG
of 145 instructions (ELF 0x00234B30), then 30 batches of 96 qwords.
This test executes the ORIGINAL microcode on the shared VU1 machine
(tools/chain_page_model.py VuOracle) and compares the native translations
(em_vu1_floor_program_mscal / em_vu1_ripple_program_mscal,
em_vu1_page_programs.h) and the native chain page (em_chain_page.c) with it:

  * captured pages: every floor-field and ripple run of each AREA01
    capture's latest page, re-linked as REF transfers in a page of their
    own, through the model page and the native page (every kicked GIF
    byte, every GS primitive, the batch counts);
  * synthetic batches: captured constants with perturbed points, camera
    rows, colour and fog rows and random starting registers (clip paths,
    the FCAND history, divisions by tiny and zero lanes), MSCAL then
    MSCNT, comparing all data memory and every register the program
    writes (VF, VI, ACC, Q, I, P and the clip history);
  * fail-stops: an MSCNT before any floor batch, a FLG UNPACK outside the
    floor program, a truncated MPG and the missing program packet fault.

Reads only the user's own ELF and AREA01 captures. EM_TEST_FULL=1 runs every
capture and the full synthetic sweep.
"""
import ctypes as C
import os
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

OUT = ROOT / 'build/area01/floor_vu'
ARRIVAL = ROOT.parent / 'Extermination/build/s87/route/15_level_exit'
ROUTE = ROOT.parent / 'Extermination/build/s87/route_a01'
FLOOR, RIPPLE = M.PROGRAM_FLOOR, M.PROGRAM_RIPPLE
# program: (batches per run, batch transfer qwc, points per batch, MPG word)
SHAPE = {FLOOR: (6, 0x1A, 24, 0x4A990000), RIPPLE: (30, 0x62, 96, 0x4A910000)}
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
int shim_floor(EmVu1PRegs *r, EmVu1PQword *dmem, Kicks *k, unsigned top, int ripple)
{
    k->n = 0;
    return ripple ? em_vu1_ripple_program_mscal(r, dmem, kick_cb, k, top)
                  : em_vu1_floor_program_mscal(r, dmem, kick_cb, k, top);
}
'''


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    src, lib = OUT / 'shim.c', OUT / 'floor.dylib'
    src.write_text(P.SHIM + SHIM)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-DEM_CHAIN_PAGE_TEST_HOOK', '-shared', '-fPIC', '-Isrc', str(src),
                    'src/game/em_chain_page.c', 'src/game/em_gs_blocks_original.c',
                    'src/game/em_load_veil_particles.c', '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    n.shim_page.argtypes = [C.POINTER(P.Region), C.c_uint, C.c_uint32, C.POINTER(C.c_uint32), C.c_uint,
                            C.POINTER(P.GsPrim), C.POINTER(P.PrimQ), C.c_uint, C.POINTER(P.Counts),
                            C.POINTER(C.c_uint32)]
    n.shim_floor.argtypes = [C.POINTER(P.Regs), C.c_void_p, C.POINTER(P.Kicks), C.c_uint, C.c_int]
    n.kicks_meta.restype = n.kicks_words.restype = C.POINTER(C.c_uint32)
    return n


# ------------------------------------------------------------- captures ---

def floor_runs(ram, program=FLOOR):
    """Each run of `program` in the capture's latest page: [(data address,
    qwc)] of the transfers from the packet's CALL through its last batch."""
    count, size = SHAPE[program][:2]
    p = M.Page(M.ram_reader(ram))
    p.dma(M.latest_start(ram))
    runs, cur = [], None
    depth = 0
    for at, tid, qwc, addr in p.transfers:
        if tid == 5 and addr == program and depth == 0:
            cur, batches = [('call', program)], 0
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
        data = addr if tid == 3 else at + 16
        cur.append((data, qwc))
        if qwc == size:
            batches += 1
            if batches == count:
                cur = None
    for r in runs:
        if sum(q == size for _a, q in r[1:]) != count:
            raise AssertionError(f'a run of {program:#x} without {count} batches')
    return runs


def page_for(ram, runs, call=True):
    """A page of its own: NEXT to the body, each run's CALL of D_002345E0
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
    want = [(prog, at, raw) for prog, at, raw in model.kicks]
    assert len(got) == len(want), (label, 'kicks', len(got), len(want))
    for i, (a, b) in enumerate(zip(got, want)):
        assert a[:2] == b[:2] and a[2] == b[2][:len(a[2])] and len(a[2]) == len(b[2]), \
            (label, 'kick', i, a[:2], b[:2])
    P.compare_prims(model.gs.prims, prims, q, out[2], label)
    assert counts.mscal_floor == model.mscal_counts.get(FLOOR, 0), (label, counts.mscal_floor)
    assert counts.mscal_ripple == model.mscal_counts.get(RIPPLE, 0), (label, counts.mscal_ripple)
    return len(got), out[2]


def captured(path):
    ram = (path / 'eeMemory.bin').read_bytes()
    floors, ripples = floor_runs(ram, FLOOR), floor_runs(ram, RIPPLE)
    if not floors and not ripples:
        return path.name, 0, 0, 0, 0
    kicks, prims = compare_page(page_for(ram, floors + ripples), path.name)
    return path.name, len(floors), len(ripples), kicks, prims


# ------------------------------------------------------------ synthetic ---

def seed_blocks(ram, program):
    """The first run's constant rows (UNPACK to 0x3F8), camera rows
    (UNPACK to 0) and first batch's points, from the capture."""
    run = floor_runs(ram, program)[0]
    size, n = SHAPE[program][1], SHAPE[program][2]
    consts = matrix = points = None
    for data, qwc in run[1:]:
        v = struct.unpack_from('<I', ram, data + 12)[0]
        if qwc == 9 and v == 0x6C0803F8:
            consts = ram[data + 16:data + 16 * 9]
        elif qwc == 5 and v == 0x6C040000:
            matrix = ram[data + 16:data + 16 * 5]
        elif qwc == size and points is None:
            points = ram[data + 16:data + 16 * (n + 1)]
    assert consts and matrix and points
    return consts, matrix, points


def fbits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def synthetic_case(index, program):
    rng = random.Random(program + index)
    consts, matrix, points = SEED[program]
    n = SHAPE[program][2]
    mem = bytearray(16384)
    mem[16 * 0x3F8:16 * 0x400] = consts
    mem[0:64] = matrix
    top = rng.choice((0x20, 0x1B0))
    other = 0x1B0 if top == 0x20 else 0x20
    pts = bytearray(points)
    for k in range(n):
        x, y, z, w = struct.unpack_from('<4f', pts, 16 * k)
        mode = index % 5
        if mode == 1:
            y += rng.uniform(-40, 40)
        elif mode == 2:
            x, z = x + rng.uniform(-300, 300), z + rng.uniform(-300, 300)
        elif mode == 3 and rng.random() < 0.3:
            y = struct.unpack('<f', struct.pack('<I', rng.choice((0, 0x80000000, 1, 0x00400000))))[0]
        elif mode == 4:
            w = rng.uniform(-2, 2)
        struct.pack_into('<4f', pts, 16 * k, x, y, z, w)
    mem[16 * top:16 * (top + n)] = pts
    mem[16 * other:16 * (other + n)] = pts[::-1] if index & 1 else pts
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


def synthetic(item):
    index, program = item
    mem, top, other, regs, vi, cf = synthetic_case(index, program)
    vu = M.VuOracle(ELF)
    S.load_program(vu, ELF, program)
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
        rc = N.shim_floor(C.byref(r), dmem, C.byref(kicks), t, program == RIPPLE)
        if fault:
            assert rc == 2, (item, step, 'the original faults (not established), native rc', rc, fault)
            out.append('fault')
            return out
        assert rc == 0, (item, step, 'native rc', rc)
        got = [(kicks.at[i], None) for i in range(kicks.n)]
        assert [a for a, _ in got] == [a for a, _ in want], (item, step, 'kick addresses')
        for i, (at, raw) in enumerate(want):
            snap = bytes(kicks.mem[i * 16384:(i + 1) * 16384])
            n = len(raw)
            have = b''.join(snap[16 * ((at + q) & 1023):16 * ((at + q) & 1023) + 16] for q in range(n // 16))
            assert have == raw, (item, step, 'kicked packet')
        assert bytes(dmem) == bytes(vu.mem), (item, step, 'dmem',
                                              next(i for i in range(16384) if dmem[i] != vu.mem[i]) // 16)
        a, b = regs_of_native(r), regs_of_oracle(vu)
        for name, x, y in zip(('vf', 'vi', 'acc', 'q', 'i', 'p', 'cf'), a, b):
            assert x == y, (item, step, 'register', name, x, y)
        assert entry != RESUME or vu.resume == RESUME
        out.append(sum(1 for at, raw in want))
    return out


# ----------------------------------------------------------- fail-stops ---

def failstops(ram):
    runs = floor_runs(ram, FLOOR)
    data = page_for(ram, runs[:1])
    checks = []
    # MSCNT before any batch of the program: the first batch's MSCAL becomes MSCNT.
    b = bytearray(data)
    first_batch = next(d for d, q in runs[0][1:] if q == 0x1A)
    struct.pack_into('<I', b, first_batch + 0x190, 0x17000000)
    checks.append(('MSCNT before a floor batch', b, 5))
    # A FLG UNPACK while no program accepts it: the constant rows' UNPACK gains FLG.
    b = bytearray(data)
    consts = next(d for d, q in runs[0][1:] if q == 9)
    word = struct.unpack_from('<I', b, consts + 12)[0]
    struct.pack_into('<I', b, consts + 12, word | 0x8000)
    checks.append(('FLG UNPACK without the program', page_for(b, [[('call', 0), (consts, 9)]], call=False), 4))
    # A truncated MPG (152 instructions) is not the program.
    b = bytearray(data)
    mpg = FLOOR + 0x2C
    w = struct.unpack_from('<I', b, mpg)[0]
    assert w == 0x4A990000
    struct.pack_into('<I', b, mpg, 0x4A980000)
    checks.append(('truncated floor MPG', b, 5))
    # The ripple program's MSCNT before any batch of its own.
    ripples = floor_runs(ram, RIPPLE)
    b = bytearray(page_for(ram, ripples[:1]))
    first_batch = next(d for d, q in ripples[0][1:] if q == 0x62)
    struct.pack_into('<I', b, first_batch + 0x610, 0x17000000)
    checks.append(('MSCNT before a ripple batch', b, 5))
    out = 0
    for label, page, want in checks:
        N.kicks_reset()
        regions = [(0, bytes(page))]
        rc, _p, _q, _c, res = P.native_page(N, regions, PAGE, [], 8192)
        assert rc < 0 and res[0] == want, (label, rc, res, want)
        out += 1
    # The packet unmapped: the CALL target is unreadable.
    regions = [(0, bytes(data[:FLOOR])), (FLOOR + 0x510, bytes(data[FLOOR + 0x510:]))]
    rc, _p, _q, _c, res = P.native_page(N, regions, PAGE, [], 8192)
    assert rc < 0 and res[0] == 2 and res[1] == FLOOR, ('missing program packet', rc, res)
    return out + 1


def main():
    global ELF, N, SEED
    start = time.time()
    ELF = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    for program, shape in SHAPE.items():
        assert struct.unpack_from('<I', ELF, program + 0x2C - 0x100000 + 0x300)[0] == shape[3]
    N = build()
    paths = [ARRIVAL] + sorted(p for p in ROUTE.glob('a01_*') if (p / 'eeMemory.bin').exists())
    chosen = RM.select(paths, 3, 0x2345E0, keep=lambda i, p: p == ARRIVAL)
    total = dict(fields=0, ripples=0, kicks=0, prims=0)
    for name, fields, ripples, kicks, prims in RM.parallel_map(captured, chosen):
        print(f'{name}: PASS {fields} floor fields, {ripples} ripple surfaces, {kicks} kicks, '
              f'{prims} primitives', flush=True)
        total['fields'] += fields
        total['ripples'] += ripples
        total['kicks'] += kicks
        total['prims'] += prims
    assert total['fields'] > 0 and total['ripples'] > 0
    seed_ram = (ARRIVAL / 'eeMemory.bin').read_bytes()
    SEED = {program: seed_blocks(seed_ram, program) for program in SHAPE}
    faults = failstops(seed_ram)
    kinds = {'batches': 0, 'faults': 0}
    items = [(i, program) for i in range(RM.pick(1500, 40)) for program in SHAPE]
    for res in RM.parallel_map(synthetic, items):
        kinds['batches'] += sum(1 for x in res if x != 'fault')
        kinds['faults'] += sum(1 for x in res if x == 'fault')
    RM.banner(RM.part(len(chosen), len(paths), 'AREA01 captures'))
    print('PASS', total, 'synthetic', kinds, faults, 'fail-stop contracts', f'({time.time() - start:.1f}s)')


if __name__ == '__main__':
    main()
