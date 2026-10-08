#!/usr/bin/env python3
"""AREA01's kind-6 VU1 program (001E3D90's near-fire layer) and its page walk.

001CFBE0 kind 6 (the fire owner 001E3D90's third layer, requested once its
projected size D_00275C00 exceeds 0x100) CALLs the program packet
D_0023D930: VIF set-up, MPGs of 256 (ELF 0x0023D958) and 130 (ELF
0x0023E160) instructions to micro 0 and 0x100, the 128-word lookup and 17
constant rows to dmem 0x6E..0x7E (rows 125 / 126: the GS window's min and
max x, y). Its instructions are the sprite program's through micro 0x10A
except the batch size (one source particle per batch) and the emission:
each visible particle is a screen-space square cut to the window and drawn
as a 5 x 5 grid of SPRITEs (em_vu1_page_programs.h emvup_kind6_particle).

This test executes the ORIGINAL microcode on the shared VU1 machine
(tools/chain_page_model.py VuOracle, through test_chain_page_reference's
logging Oracle) and compares the native translation
(em_vu1_kind6_program_mscal) and the native chain page (em_chain_page.c):

  A. captured pages: every kind-6 run (its GS state REF, the CALL of the
     packet and 001CFBE0's four packets) of each AREA01 capture's latest
     page, re-linked as REF transfers into a page of their own; every MSCAL
     compared (all registers, all data memory, every kicked GIF byte, the
     producer of every Q / MAC / clip read), then the native page's GS
     primitives, kicks and MSCAL count against the model page's;
  B. synthetic MSCALs: captured kind-6 MSCAL memories with perturbed
     descriptors, counts, ages, tile matrices (squares crossing each edge of
     the window, wholly outside it, behind the near plane) and random
     starting registers; every conditional branch of the program must be
     reached both ways (except micro 0x045, the batch loop, whose count is
     always 1 here, so it is never taken);
  C. fail-stops: an exponent-255 operand on a live lane (both sides fault),
     the second MPG without the first, a truncated second MPG, the program
     packet unmapped.

Reads only the user's own ELF and AREA01 captures. EM_TEST_FULL=1 runs every
capture and the full synthetic sweep.
"""
import collections
import hashlib
import random
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as RM  # noqa: E402
import test_chain_page_reference as P  # noqa: E402
import chain_page_model as M  # noqa: E402

S87 = ROOT.parent / 'Extermination/build/s87'
KIND6 = M.PROGRAM_KIND6
PAGE = 0x1EF0000
# Every capture whose latest page holds a kind-6 run (the others hold none).
CAPTURES = ('route_a01/a01_00_train_room', 'route_a01/a01_01_tunnel', 'route_a01/a01_07_level_exit',
            'route_a01/a01_s1_sentry_doc', 'route_a01u/a01u_02_progression_exit')
QUICK = ('route_a01/a01_00_train_room', 'route_a01/a01_07_level_exit')
BRANCHES = (0x026, 0x02A, 0x03D, 0x049, 0x059, 0x07E, 0x081, 0x084, 0x087, 0x0B2, 0x0B5, 0x0B8, 0x0BB,
            0x0F7, 0x0F9, 0x125, 0x146, 0x148, 0x14D, 0x150, 0x153, 0x156, 0x170, 0x178)


def fail(msg):
    raise AssertionError(msg)


# ------------------------------------------------------------- captures ---

def kind6_runs(ram):
    """Each kind-6 run of the capture's latest page: [(data address, qwc) |
    ('call', packet)]: the GS state REF before the CALL, the CALL, then
    001CFBE0's packets 4, 3, 2 and 1 (16, 1, 9 and 7 qwords; the last ends
    with the MSCAL)."""
    p = M.Page(M.ram_reader(ram))
    p.dma(M.latest_start(ram))
    t, runs = p.transfers, []
    for i, (_at, tid, _qwc, addr) in enumerate(t):
        if tid != 5 or addr != KIND6:
            continue
        k = i - 1
        while not t[k][2]:
            k -= 1
        if t[k][1] != 3 or t[k][2] != 8:
            fail(f'the kind-6 CALL at {t[i][0]:#x} does not follow a GS state REF of 8 qwords')
        run, depth, got, j = [(t[k][3], 8), ('call', KIND6)], 1, [], i + 1
        while len(got) < 4:
            at2, tid2, qwc2, addr2 = t[j]
            j += 1
            if tid2 == 5:
                depth += 1
            elif tid2 == 6:
                depth -= 1
            elif depth == 0 and qwc2:
                got.append((addr2 if tid2 == 3 else at2 + 16, qwc2))
        if [q for _a, q in got] != [16, 1, 9, 7]:
            fail(f'the kind-6 run at {t[i][0]:#x} is not 001CFBE0\'s four packets: {got}')
        runs.append(run + got)
    return runs


def page_for(ram, runs):
    """A page of its own: NEXT to the body, each run's REFs and CALL, then
    the end link (base + 0x20)."""
    b = bytearray(ram)
    struct.pack_into('<4I', b, PAGE, 0x20000000, PAGE + 0x40, 0, 0)
    cur = PAGE + 0x40
    for run in runs:
        for item in run:
            if item[0] == 'call':
                struct.pack_into('<4I', b, cur, 0x50000000, item[1], 0, 0)
            else:
                struct.pack_into('<4I', b, cur, 0x30000000 | item[1], item[0], 0, 0)
            cur += 16
    struct.pack_into('<4I', b, cur, 0x20000000, PAGE + 0x20, 0, 0)
    return bytes(b)


def captured(lib, stats, branches, seeds, name):
    ram = (S87 / name / 'eeMemory.bin').read_bytes()
    runs = kind6_runs(ram)
    if not runs:
        fail(f'{name}: no kind-6 run in the latest page')
    data = page_for(ram, runs)
    pg = P.ComparedPage(M.ram_reader(data), lib, stats, name, [])
    pg.run(PAGE)
    branches.update(pg.branches)
    seeds.extend(mem for program, mem in pg.seeds if program == KIND6)
    rc, prims, q, counts, out = P.native_page(lib, [(0, data)], PAGE, [], 8192)
    if rc or out[0]:
        fail(f'{name}: the native page faulted ({out[0]} at {out[1]:#x})')
    P.compare_prims(pg.gs.prims, prims, q, out[2], name)
    if counts.mscal_kind6 != pg.mscals[KIND6] or counts.mscal_kind6 != len(runs):
        fail(f'{name}: kind-6 MSCALs native {counts.mscal_kind6}, original {pg.mscals[KIND6]}, runs {len(runs)}')
    if counts.kicks != len(pg.kicks) or counts.direct != len(pg.directs):
        fail(f'{name}: native kicks {counts.kicks} directs {counts.direct}, original {len(pg.kicks)} '
             f'{len(pg.directs)}')
    if counts.kind6_prims != out[2]:
        fail(f'{name}: {counts.kind6_prims} of {out[2]} primitives counted as the kind-6 program\'s')
    if counts.stale_q:
        fail(f'{name}: {counts.stale_q} vertices without their GIF tag\'s Q')
    stats['captures'] += 1
    stats['runs'] += len(runs)
    stats['page_prims'] += out[2]
    stats['page_kicks'] += counts.kicks
    return f'{name.split("/")[-1]}: {len(runs)} runs, {counts.kicks} kicks, {out[2]} sprites'


# ------------------------------------------------------------ synthetic ---

def kind6_case(rng, mem0):
    """A captured kind-6 MSCAL's memory, perturbed: the sprite program's
    descriptor perturbations (counts, ages, rates, tile matrix), then the
    tile matrix moved so squares cross each window edge, leave the window
    or go behind the near plane, the size rows, and now and then the window
    rows themselves."""
    mem, count = P.sprite_case(rng, (mem0,), None)
    if count > 60:                                       # one kick per drawn particle: the shim holds 64
        count = rng.randint(1, 60)
        P.set_row(mem, 88, [count] + P.row(mem, 88)[1:])
    t = P.row(mem, 93)
    mode = rng.randrange(6)
    if mode:
        shift = (0.0, 5.0, 20.0, 60.0, 200.0, 2000.0)[mode]
        set_t = [P.f32(P.num(t[k]) + rng.uniform(-1, 1) * shift) for k in range(3)]
        P.set_row(mem, 93, set_t + [t[3]])
    if rng.random() < 0.5:                               # the size rows 84 / 85 (the record's +3, by age)
        for r in (84, 85):
            P.set_row(mem, r, [P.f32(rng.choice([0.0, 0.5, 4.0, 40.0, 400.0]) * rng.random()) for _ in range(4)])
    if rng.random() < 0.2:                               # the window: a narrower or shifted one
        lo, hi = P.row(mem, 125), P.row(mem, 126)
        cx, cy = 2048.0 + rng.uniform(-200, 200), 2048.0 + rng.uniform(-100, 100)
        hw, hh = rng.uniform(1, 300), rng.uniform(1, 150)
        P.set_row(mem, 125, [P.f32(cx - hw), P.f32(cy - hh), lo[2], lo[3]])
        P.set_row(mem, 126, [P.f32(cx + hw), P.f32(cy + hh), hi[2], hi[3]])
    return mem, count


def synthetic(lib, elf, stats, branches, seeds, cases, seed):
    rng = random.Random(seed)
    for n in range(cases):
        mem, _count = kind6_case(rng, rng.choice(seeds))
        vu = P.Oracle()
        P.load_program(vu, elf, KIND6)
        vu.mem[:] = mem
        for i in range(1, 32):
            vu.v[i] = [P.finite(rng) for _ in range(4)]
        vu.vi = [0] + [rng.getrandbits(16) for _ in range(15)]
        vu.accw = [P.finite(rng) for _ in range(4)]
        vu.q, vu.iw, vu.r, vu.cf = P.finite(rng), P.finite(rng), rng.getrandbits(23), rng.getrandbits(24)
        vu.p = P.finite(rng)
        where = f'synthetic kind-6 case {n}'
        if P.compare_mscal(lib, vu, KIND6, where, stats):
            P.check_timing(vu, KIND6, where)
            stats['synthetic'] += 1
        for key, k in vu.branch.items():
            branches[(KIND6,) + key] += k


# ----------------------------------------------------------- fail-stops ---

def failstops(lib, elf, stats, seeds):
    # An exponent-255 word on a live lane: the phase (row 89 x) and the
    # colour blend (row 86 z); both sides fault.
    for r, lane in ((89, 0), (86, 2)):
        mem = bytearray(seeds[0])
        w = P.row(mem, r)
        w[lane] = 0x7F800000
        P.set_row(mem, r, w)
        vu = P.Oracle()
        P.load_program(vu, elf, KIND6)
        vu.mem[:] = mem
        before = stats['operand_faults']
        if P.compare_mscal(lib, vu, KIND6, f'operand fault row {r}', stats) or \
                stats['operand_faults'] != before + 1:
            fail(f'an exponent-255 word in row {r} did not fault')
        stats['failstops'] += 1
    # The page: the packet's MPGs out of order or truncated, the packet unmapped.
    ram = (S87 / QUICK[0] / 'eeMemory.bin').read_bytes()
    runs = kind6_runs(ram)[:1]
    good = bytearray(page_for(ram, runs))
    o = KIND6 + 16                                    # the packet's CNT data
    vif = [struct.unpack_from('<I', good, o + 4 * k)[0] for k in range(16)]
    first = next(k for k, v in enumerate(vif) if v >> 24 & 0x7F == 0x4A)
    second = KIND6 + 16 + 4 * first + 4 + 8 * 256    # then VIF NOPs up to the second MPG
    while struct.unpack_from('<I', good, second)[0] == 0:
        second += 4
    if struct.unpack_from('<I', good, second)[0] != 0x4A820100:
        fail('the kind-6 packet\'s second MPG is not where the walk expects it')

    def expect(data, want, label):
        rc, _prims, _q, _counts, out = P.native_page(lib, [(0, bytes(data))], PAGE, [], 8192)
        if out[0] != want or not rc:
            fail(f'{label}: native fault {out[0]} at {out[1]:#x} (rc {rc}), want {want}')
        stats['failstops'] += 1

    bad = bytearray(good)                             # the first MPG becomes NOPs: part 2 alone
    struct.pack_into('<I', bad, KIND6 + 16 + 4 * first, 0)
    for k in range(2 * 256):
        struct.pack_into('<I', bad, KIND6 + 16 + 4 * first + 4 + 4 * k, 0)
    expect(bad, 5, 'the second MPG without the first')
    bad = bytearray(good)                             # 129 instructions, then a NOP pair
    struct.pack_into('<I', bad, second, 0x4A810100)
    struct.pack_into('<II', bad, second + 4 + 8 * 129, 0, 0)
    expect(bad, 5, 'a truncated second MPG')
    rc, _p, _q, _c, out = P.native_page(lib, [(0, bytes(good[:KIND6])), (KIND6 + 0xF70, bytes(good[KIND6 + 0xF70:]))],
                                        PAGE, [], 8192)
    if out[0] != 2 or out[1] != KIND6 or not rc:
        fail(f'the unmapped packet: native fault {out[0]} at {out[1]:#x}, want 2 at {KIND6:#x}')
    stats['failstops'] += 1


# ----------------------------------------------------------------- main ---

def main():
    elf = (P.DECOMP / 'config/SCUS_971.12').read_bytes()
    if hashlib.sha256(elf).hexdigest() != P.ELF_SHA:
        fail('boot ELF is not the pinned build')
    lib = P.build_lib()
    stats, branches, seeds = P.Stats(), collections.Counter(), []
    lines = [captured(lib, stats, branches, seeds, name) for name in (CAPTURES if RM.FULL else QUICK)]
    n_syn = RM.pick(1200, 120)
    synthetic(lib, elf, stats, branches, seeds, n_syn, 0x6D930)
    for site in BRANCHES:
        for taken in (True, False):
            if not branches[(KIND6, site, taken)]:
                fail(f'kind 6: the branch at micro {site:#05x} was never {"taken" if taken else "not taken"}')
    if branches[(KIND6, 0x045, True)]:
        fail('kind 6: the batch loop (micro 0x045) was taken with one particle per batch')
    failstops(lib, elf, stats, seeds)
    if stats['synthetic'] < n_syn // 2:
        fail(f'too few synthetic cases ran: {stats}')
    for line in lines:
        print('kind-6 VU: ' + line)
    RM.banner(f"{stats['captures']} of {len(CAPTURES)} captures with kind-6 runs",
              f"{stats['mscal_kind6']} kind-6 MSCALs compared ({stats['synthetic']} synthetic, "
              f"{stats['operand_faults']} operand faults), {stats['kicks']} XGKICKs, "
              f"{stats['kicked_packet_bytes']:,} packet bytes",
              f"{stats['failstops']} fail-stops")
    print(f"level2 kind-6 VU reference: PASS ({stats['runs']} captured runs, {stats['page_prims']} sprites, "
          f"{stats['page_kicks']} kicks; every branch both ways)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
