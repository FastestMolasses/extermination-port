#!/usr/bin/env python3
"""Lane A00FX's census functions that the port ALREADY translates, run
against the original over AREA00 inputs.

docs/AREA00_FX.md section 4. Five of the lane's 33 census functions have a
verified port translation; this file does not edit or duplicate them. It
reuses each one's own original-instruction harness (imported, unmodified)
and feeds it AREA00 inputs from the recorded route captures
(../Extermination/build/s87/route_a00/<beat>/), building every native
library under build/area00/fx/existing/:

  0015B030  em_area01_exitb.c (tools/test_area01_exitb_reference.py): the
      AREA00 owner node running 0015B030 in every AREA00 capture that has
      one (all but a00_10 and a00_s0, which are AREA01 again), as
      captured, in states 2, 3, 4 and 0xFF, and in states 0 / 1 with the
      record at +0x20 in states 0, 1, 2, 3 and 0x80 and 0015AC00's answer
      0, 1, -1 (the harness's own case shapes), each under the harness's
      lockstep check (callee entries, registers, RAM and scratchpad).
  001639E0  em_player_fall.c (tools/test_player_fall_reference.py): the
      state-7 entry over the AREA00 player record of every capture, with
      the sub-state bytes +6 / +7 over the values the harness uses, every
      callee scripted identically on both sides; the entry check of
      tools/test_area01_render_existing_reference.py (the actor bytes and
      the scratch words at every scripted call) is imported and applied.
  001D80E0  em_effect_original.c (tools/test_effect_original_reference.py):
      its spawn sweep (every sign-bit effect id through 001EFD90 / 001EFD20
      / 001EF9D0; the kind-1 records reach 001D80E0) over the AREA00 beats
      whose area byte is 0 (its area-table ids need AREA00's table).
  001D7000  em_static_world.c (tools/test_static_world_reference.py): its
      001D7000 leaf cases on every channel over AREA00 captures appended to
      its capture list.
  001EFEB0  em_security_gun_rest.c (tools/test_security_gun_rest_reference.py):
      its 001EFEB0 check (001EF9D0 answering 0 or a node) with each AREA00
      image as the base memory.

Quick mode by default; EM_TEST_FULL=1 runs every beat. At most four worker
processes.
"""
import os
import random
import struct
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
# The render lane's existing-translation test (imported for its fall entry
# check) points the fall harness's build directory at build/area01/<EM_LANE>/fall;
# this lane's is build/area00/fx/existing/fall.
os.environ['EM_LANE'] = '../area00/fx/existing'
os.environ.setdefault('EM_TEST_JOBS', '4')
import reference_mode as RM  # noqa: E402
import test_area01_render_existing_reference as RE  # noqa: E402  (imports the fall harness)
import test_player_fall_reference as FALL  # noqa: E402
import test_area01_exitb_reference as EXB  # noqa: E402
import test_effect_original_reference as EFF  # noqa: E402
import test_static_world_reference as SW  # noqa: E402
from test_player_slide_reference import read_elf  # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route_a00'
OUT = ROOT / 'build' / 'area00' / 'fx' / 'existing'
RE.OUT = OUT
PLAYER = 0x8102B0
BEATS = sorted(p.name for p in ROUTE.iterdir() if (p / 'eeMemory.bin').exists()) if ROUTE.exists() else []
MASK = 0xFFFFFFFF


def image(beat):
    return (ROUTE / beat / 'eeMemory.bin').read_bytes(), (ROUTE / beat / 'scratchpad.bin').read_bytes()


# ---- 0015B030 ----------------------------------------------------------------

def exitb_cases(beats):
    EXB.ROUTE = ROUTE
    EXB.IMAGES.clear()
    out = []
    for beat in beats:
        owners = EXB.owners(0x15B030, beat)
        assert len(owners) == 1, (beat, 'owner nodes of 0015B030', owners)
        p = owners[0]
        q = EXB.u32(EXB.image(beat)[0], p + 0x20)
        out.append(EXB.case(f'{beat} 15B030 captured', 0x15B030, [p], beat=beat))
        for st in (2, 3, 4, 0xFF):
            out.append(EXB.case(f'{beat} 15B030 s{st}', 0x15B030, [p], writes=[EXB.W8(p + 4, st)], beat=beat))
        for st in (0, 1):
            for qs in (0, 1, 2, 3, 0x80):
                for res in ((0, 1, -1) if st == 0 else (0,)):
                    w = [EXB.W8(p + 4, st), EXB.W8(q + 4, qs)]
                    out.append(EXB.case(f'{beat} 15B030 s{st} q{qs} r{res}', 0x15B030, [p], writes=w,
                                        script={0x15AC00: [res]}, beat=beat))
    return out


def check_exitb(elf):
    EXB.ELF = elf
    EXB.OUT = OUT / 'exitb'
    EXB.NATIVE = EXB.build_native()
    EXB.BRANCH_PCS = EXB.branch_pcs(EXB.ExEE(elf).mem)   # its outcome recording (as its main sets it)
    EXB.ROUTE = ROUTE
    EXB.IMAGES.clear()
    # the owner runs in every AREA00 capture but a00_10 / a00_s0 (AREA01 again there)
    owned = [b for b in BEATS if EXB.owners(0x15B030, b)]
    beats = owned if RM.FULL else [owned[0], 'a00_09_ne_room_out']
    cases = exitb_cases(beats)
    outcomes, calls = set(), 0
    for r in RM.parallel_map(EXB.run_case_safe, cases):
        assert r[0] == 'ok', ('0015B030', r[1])
        if r[1] is not None:
            outcomes |= r[1][0]
            calls += r[1][1]
    inside = {o for o in outcomes if 0x15B030 <= o[0] < 0x15B030 + 0xF8}
    return len(cases), len(beats), calls, len(inside)


# ---- 001639E0 ------------------------------------------------------------------

def fall7_case(item):
    """The render lane's fall_case (its entry check included) with the
    state-7 entry and an AREA00 player record."""
    beat, sub6, sub7, seed = item
    ram, _ = image(beat)
    case = FALL.make_case(seed)
    actor = bytearray(ram[PLAYER:PLAYER + 0x320])
    actor[6], actor[7] = sub6, sub7
    case['entry'], case['actor'] = 'state7', bytes(actor)
    if not isinstance(FALL.ORACLE, RE.EntryOracle):
        FALL.ORACLE = RE.EntryOracle(FALL.ELF)
    FALL.ORACLE.ee.outcomes = set()
    want = FALL.ORACLE.run(case, FALL.Script(seed))
    native = RE.EntryNative(FALL.NATIVE, case, FALL.Script(seed))
    result, got = native.run()
    where = (beat, sub6, sub7, seed)
    assert not FALL.ORACLE.stray, (where, 'the original stored outside the actor / scratch words', FALL.ORACLE.stray[:8])
    for k, (mine, theirs) in enumerate(zip(native.entries, FALL.ORACLE.entries)):
        assert got['log'][k] == want['log'][k], (where, 'call', k, 'arguments at entry differ', got['log'][k], want['log'][k])
        assert mine[0] == theirs[0], (where, 'call', k, want['log'][k][0], 'actor bytes differ at entry',
                                      RE.first_actor_diff(mine[0], theirs[0]))
        assert mine[1] == theirs[1], (where, 'call', k, want['log'][k][0], 'scratch differs at entry', mine[1], theirs[1])
    assert len(native.entries) == len(FALL.ORACLE.entries) == len(want['log']), (where, 'entry counts')
    assert result == 0, (where, 'native fault', result)
    assert want['log'] == got['log'], (where, 'worker calls', want['log'], got['log'])
    assert want['actor'] == got['actor'], (where, 'actor bytes differ at',
                                           [hex(k) for k in range(0x320) if want['actor'][k] != got['actor'][k]][:16])
    assert want['scratch'] == got['scratch'], (where, 'scratch')
    inside = {o for o in FALL.ORACLE.ee.outcomes if 0x1639E0 <= o[0] < 0x1639E0 + 0x160}
    return len(want['log']), inside, len(native.entries)


def check_fall(elf):
    FALL.ELF = elf
    FALL.NATIVE = FALL.build_native()
    items = [(b, s6, s7, 0x1639E0 + 23 * i + 5 * s6 + s7) for i, b in enumerate(BEATS)
             for s6 in (0, 1, 2, 3) for s7 in range(5)]
    items = RM.select(items, 40, 22, axes=(lambda it: it[0], lambda it: (it[1], it[2])))
    calls, outcomes, entries = 0, set(), 0
    for n, got, e in RM.parallel_map(fall7_case, items):
        calls += n
        outcomes |= got
        entries += e
    probe = FALL.EE(elf)
    sites = set()
    for pc in range(0x1639E0, 0x1639E0 + 0x160, 4):
        w = probe.load(pc)
        op, rs, rt = w >> 26, w >> 21 & 31, w >> 16 & 31
        if (op in (1, 4, 5, 6, 7, 20, 21, 22, 23) or (op == 17 and rs == 8)) and not (op == 4 and rs == 0 and rt == 0):
            sites.add(pc)
    seen = {o for o in outcomes if o[0] in sites}
    return len(items), calls, entries, '%d of %d' % (len(seen), 2 * len(sites))


# ---- 001D80E0 ------------------------------------------------------------------

def effect_lib():
    import ctypes as C
    import subprocess
    OUT.mkdir(parents=True, exist_ok=True)
    lib_path = OUT / 'effect.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', 'src/game/em_effect_original.c',
                    'src/game/em_point_light.c', 'src/game/em_owner_services_original.c',
                    '-o', str(lib_path)], cwd=ROOT, check=True)
    lib = C.CDLL(str(lib_path))
    P, FP = C.POINTER, EFF.FP
    lib.em_effect_original_load_tables.argtypes = [C.c_char_p, C.c_size_t, P(EFF.Tables)]
    lib.em_effect_original_001EFD90.argtypes = [P(EFF.Effect), C.c_uint32, FP, FP, P(P(EFF.Node))]
    lib.em_effect_original_001EFD20.argtypes = [P(EFF.Effect), C.c_uint32, FP, P(P(EFF.Node))]
    lib.em_effect_original_001EF9D0.argtypes = [P(EFF.Effect), C.c_uint32, FP, C.c_float, P(P(EFF.Node))]
    lib.em_effect_original_001F0460.argtypes = [P(EFF.Effect), C.c_int32, FP]
    lib.em_effect_original_001EA240.argtypes = [P(EFF.Effect), P(EFF.Node)]
    lib.em_effect_original_001CCF70.argtypes = [P(EFF.Effect), FP, P(C.c_int32)]
    lib.em_effect_original_001CD390.argtypes = [P(EFF.Effect), FP, FP]
    lib.em_effect_original_fp.argtypes = [C.c_int, C.c_uint, C.c_uint32, C.c_uint32, C.c_uint32]
    lib.em_effect_original_fp.restype = C.c_uint32
    return lib


def check_spawn(elf):
    lib = effect_lib()
    # the sweep's area-table ids need an AREA00 table: a00_10 / a00_s0 are
    # AREA01 again (area byte 1, whose table pointer is 0: the original faults)
    area0 = [b for b in BEATS if image(b)[0][0x810700] == 0]
    beats = area0 if RM.FULL else ['a00_03_padlock', 'a00_09_ne_room_out']
    total = 0
    for beat in beats:
        ram, spad = image(beat)
        counts = {}
        EFF.spawn_sweep(lib, elf, (bytearray(ram), bytearray(spad)), counts)
        total += counts['spawn'] + counts.get('spawn_sound', 0)
    return total, len(beats)


# ---- 001D7000 ------------------------------------------------------------------

def check_static(elf):
    SW.ELF = elf
    SW.rcref.ELF = elf
    SW.NATIVE = SW.build_native()
    beats = BEATS if RM.FULL else ['a00_09_ne_room_out', 'a00_10_progression_exit']
    for beat in beats:
        SW.CAPTURES.append(('a00/' + beat, ROUTE / beat / 'eeMemory.bin', ROUTE / beat / 'scratchpad.bin'))
    rng = random.Random(0xA001D700)
    n, outcomes = 0, set()
    for beat in beats:
        cases = []
        for chan in range(4):
            for _ in range(RM.pick(6, 2)):
                # the harness's own leaf shape: the channel cursor moved to its free area
                ctx = SW.rd(image(beat)[0], SW.CTX_PTR) & (SW.RAM_SIZE - 1)
                cases.append(dict(label=f'C 001D7000 ch{chan}', entry=0x1D7000, args=[chan, rng.getrandbits(32)],
                                  pokes=[(ctx + 0x10 + 4 * chan, SW.w32(SW.FREE + 0x50000))]))
        for label, cover, _, _ in SW.run_group(('a00/' + beat, cases)):
            outcomes |= cover
            n += 1
    return n, len(beats)


# ---- 001EFEB0 ------------------------------------------------------------------

def check_efeb0(elf):
    import test_security_gun_rest_reference as GUN
    GUN.OUT = OUT / 'gun'
    GUN.CONTEXT['elf'] = elf
    GUN.sdf.CONTEXT['elf'] = elf
    GUN.CONTEXT['lib'] = GUN.build()
    beats = BEATS if RM.FULL else ['a00_03_padlock', 'a00_09_ne_room_out']
    n = 0
    for beat in beats:
        ram, spad = image(beat)
        GUN.CONTEXT['base'] = ram
        GUN.sdf.CONTEXT['base'] = ram
        count, _ = GUN.check_001EFEB0()
        n += count
    return n, len(beats)


def main():
    t0 = time.time()
    elf = read_elf()
    assert BEATS, 'no AREA00 captures under ' + str(ROUTE)
    for beat in BEATS:
        ram, _ = image(beat)
        for start, size in ((0x15B030, 0xF8), (0x1639E0, 0x160), (0x1D80E0, 0x20), (0x1D7000, 0x7C),
                            (0x1EFEB0, 0x54)):
            at = start - 0x100000 + 0x300
            assert ram[start:start + size] == elf[at:at + size], ('capture code differs from the ELF', beat, hex(start))
    ex_n, ex_beats, ex_calls, ex_out = check_exitb(elf)
    fall_n, fall_calls, fall_entries, fall_out = check_fall(elf)
    sp_n, sp_beats = check_spawn(elf)
    sw_n, sw_beats = check_static(elf)
    gun_n, gun_beats = check_efeb0(elf)
    RM.banner(f'{ex_n} 0015B030 cases over {ex_beats} AREA00 captures ({ex_calls} callee calls in lockstep, '
              f'{ex_out} branch outcomes of 0015B030 seen)',
              f'{fall_n} 001639E0 cases on AREA00 player records ({fall_calls} scripted calls, {fall_entries} with '
              f'actor + scratch + arguments equal at entry, {fall_out} branch outcomes of 001639E0 seen)',
              f'{sp_n} spawn cases over {sp_beats} AREA00 beats (001D80E0 through the kind-1 records)',
              f'{sw_n} 001D7000 cases over {sw_beats} AREA00 captures',
              f'{gun_n} 001EFEB0 cases over {gun_beats} AREA00 images')
    print('elapsed %.1f s' % (time.time() - t0))
    print('area00 fx existing translations: PASS')


if __name__ == '__main__':
    main()
