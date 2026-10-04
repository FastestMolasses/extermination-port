#!/usr/bin/env python3
"""The AREA01 render lane's main-line functions that the port ALREADY
translates, run against the original over AREA01 inputs.

docs/AREA01_RENDER.md, section "Existing translations". Five of the lane's
21 census functions have port translations written for the first level;
this file does not edit or duplicate them. It reuses each one's own
original-instruction harness (imported, unmodified) and feeds it AREA01
inputs from the recorded route captures (../Extermination/build/s87/
route_a01/<beat>/), building every native library under this lane's
private build/area01/<EM_LANE>/ directory (EM_LANE defaults to 'render'):

  00163D50, 00164220  em_player_fall.c (tools/test_player_fall_reference.py):
      the state-8 routine 00163B40 with its sub-state byte +6 forced to 4
      (00163D50) and 2 (00164220) over the AREA01 player record of every
      beat (+7 over 0..4), every callee scripted identically on both
      sides; the 0x320 actor bytes, the scratch words and the call log
      must match at the end. The fall harness replays each scripted
      callee's writes, which could erase a wrong native store, so this file
      adds an entry check: at EVERY scripted call the native side's whole
      state (the 0x320 actor bytes and the five scratch words, the only
      memory em_player_fall.c can reach) must equal the original's at the
      same call entry, and the call's logged arguments must be equal. It
      also audits that the original routines themselves store only into
      those regions (the private stack aside), which is what makes that
      comparison the complete state.
  001647D0  em_player_hang.c (tools/test_player_hang_reference.py): its
      random_case / run_case with the AREA01 RAM image and the AREA01
      player record as the captured record (the node words resolve in the
      AREA01 RAM); code and tables checked against the ELF first.
  001F0460  em_effect_original.c (tools/test_effect_original_reference.py):
      its decal_sweep (every preset / ring start) over AREA01 beats.
  001F4BF0  em_status_scene_original.c (tools/test_status_scene_reference.py):
      its glow check with the words the AREA01 captures hold at
      0x700038B0 and the two words 00158590 writes there.

Quick mode by default; EM_TEST_FULL=1 runs more seeds. At most four worker
processes unless EM_TEST_JOBS says otherwise. Measured times: docs/AREA01_RENDER.md
section 4.
"""
import ctypes as C
import os
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
# Every library builds under build/area01/<EM_LANE> (default 'render'), as in
# tools/test_area01_render_reference.py, so a reviewer or a mutation sweep can
# point this test at its own scratch directory. The fall harness builds under
# build/<its EM_LANE>, so that variable is re-pointed below that directory.
LANE = os.environ.get('EM_LANE', 'render')
os.environ['EM_LANE'] = 'area01/%s/fall' % LANE   # test_player_fall_reference builds under build/<EM_LANE>
os.environ.setdefault('EM_TEST_JOBS', '4')        # at most four worker processes (shared machine)
import reference_mode as RM  # noqa: E402
import test_player_fall_reference as FALL  # noqa: E402
import test_player_hang_reference as HANG  # noqa: E402
import test_effect_original_reference as EFF  # noqa: E402
import test_status_scene_reference as SCN  # noqa: E402
from test_player_slide_reference import read_elf  # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route_a01'
OUT = ROOT / 'build' / 'area01' / LANE
PLAYER = 0x8102B0
BEATS = sorted(p.name for p in ROUTE.iterdir() if (p / 'eeMemory.bin').exists()) if ROUTE.exists() else []
MASK = 0xFFFFFFFF


def image(beat):
    return (ROUTE / beat / 'eeMemory.bin').read_bytes(), (ROUTE / beat / 'scratchpad.bin').read_bytes()


def compile_lib(name, sources):
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / name
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
                    '-fPIC', '-Isrc'] + sources + ['-o', str(lib)], cwd=ROOT, check=True)
    return C.CDLL(str(lib))


# ---- 00163D50 / 00164220 -------------------------------------------------

FALL_SCRATCH = [(0x700038A0, 0x10), (0x70003A20, 4)]


class EntryOracle(FALL.UnitOracle):
    """The fall harness's oracle plus a state snapshot at every scripted call
    entry (before the call's scripted writes) and an audit of the routines'
    own stores."""

    def __init__(self, elf):
        self.entries, self.stray, self.in_hook = [], [], False
        super().__init__(elf)
        ee, base = self.ee, self.ee.save

        def save(address, value, size=4):
            if not self.in_hook:
                a = address & MASK
                inside = (FALL.ACTOR <= a and a + size <= FALL.ACTOR + 0x320) or \
                    any(s <= a and a + size <= s + n for s, n in FALL_SCRATCH) or 0x7F000000 <= a < 0x7F100000
                if not inside:
                    self.stray.append((hex(a), size))
            base(address, value, size)
        ee.save = save

    def hook(self, name):
        inner = super().hook(name)

        def run(ee):
            self.entries.append((bytes(ee.read(FALL.ACTOR, 0x320)),
                                 tuple(ee.load(s + 4 * i) for s, n in FALL_SCRATCH for i in range(n // 4))))
            self.in_hook = True
            try:
                inner(ee)
            finally:
                self.in_hook = False
        return run

    def run(self, case, script):
        self.entries, self.stray, self.in_hook = [], [], True     # the harness's own setup stores are not audited
        ee, base_call = self.ee, self.ee.call

        def call(*args, **kwargs):
            self.in_hook = False
            return base_call(*args, **kwargs)
        ee.call = call
        try:
            return super().run(case, script)
        finally:
            del ee.call
            self.in_hook = True


class EntryNative(FALL.NativeRun):
    """The fall harness's native run plus the same snapshot at every worker
    call, taken before the scripted writes are applied."""

    def __init__(self, *args, **kwargs):
        self.entries = []
        super().__init__(*args, **kwargs)

    def call(self, name, entry):
        self.entries.append((bytes(self.live.bytes), tuple(self.scratch.s38A0) + (self.scratch.s3A20,)))
        return super().call(name, entry)


def first_actor_diff(a, b):
    return [hex(k) for k in range(len(a)) if a[k] != b[k]][:16]


def fall_case(item):
    beat, sub6, sub7, seed = item
    ram, _ = image(beat)
    case = FALL.make_case(seed)
    actor = bytearray(ram[PLAYER:PLAYER + 0x320])
    actor[6], actor[7] = sub6, sub7
    case['entry'], case['actor'] = 'state8', bytes(actor)
    if not isinstance(FALL.ORACLE, EntryOracle):
        FALL.ORACLE = EntryOracle(FALL.ELF)
    FALL.ORACLE.ee.outcomes = set()
    want = FALL.ORACLE.run(case, FALL.Script(seed))
    native = EntryNative(FALL.NATIVE, case, FALL.Script(seed))
    result, got = native.run()
    where = (beat, sub6, sub7, seed)
    assert not FALL.ORACLE.stray, (where, 'the original stored outside the actor / scratch words', FALL.ORACLE.stray[:8])
    # The entry check, call by call, before the logs and the end state.
    for k, (mine, theirs) in enumerate(zip(native.entries, FALL.ORACLE.entries)):
        assert got['log'][k] == want['log'][k], (where, 'call', k, 'arguments at entry differ', got['log'][k], want['log'][k])
        assert mine[0] == theirs[0], (where, 'call', k, want['log'][k][0], 'actor bytes differ at entry',
                                      first_actor_diff(mine[0], theirs[0]))
        assert mine[1] == theirs[1], (where, 'call', k, want['log'][k][0], 'scratch differs at entry', mine[1], theirs[1])
    assert len(native.entries) == len(FALL.ORACLE.entries) == len(want['log']), (where, 'entry counts')
    assert result == 0, (where, 'native fault', result)
    assert want['log'] == got['log'], (where, 'worker calls', want['log'], got['log'])
    assert want['actor'] == got['actor'], (where, 'actor bytes differ at',
                                           [hex(k) for k in range(0x320) if want['actor'][k] != got['actor'][k]][:16])
    assert want['scratch'] == got['scratch'], (where, 'scratch')
    inside = {o for o in FALL.ORACLE.ee.outcomes if 0x163D50 <= o[0] < 0x163D50 + 316 or
              0x164220 <= o[0] < 0x164220 + 388}
    return len(want['log']), inside, len(native.entries)


def check_fall(elf):
    FALL.ELF = elf
    FALL.NATIVE = FALL.build_native()
    items = [(b, s6, s7, 0x163D50 + 17 * i + s7) for i, b in enumerate(BEATS) for s6 in (2, 4) for s7 in range(5)]
    items = RM.select(items, 60, 21, axes=(lambda it: it[0], lambda it: (it[1], it[2])))
    calls, outcomes, entries = 0, set(), 0
    for n, got, e in RM.parallel_map(fall_case, items):
        calls += n
        outcomes |= got
        entries += e
    probe = FALL.EE(elf)
    sites = set()
    for start, size in ((0x163D50, 316), (0x164220, 388)):
        for pc in range(start, start + size, 4):
            w = probe.load(pc)
            op, rs, rt = w >> 26, w >> 21 & 31, w >> 16 & 31
            if (op in (1, 4, 5, 6, 7, 20, 21, 22, 23) or (op == 17 and rs == 8)) and not (op == 4 and rs == 0 and rt == 0):
                sites.add(pc)
    seen = {o for o in outcomes if o[0] in sites}
    return len(items), calls, '%d of %d' % (len(seen), 2 * len(sites)), entries


# ---- 001647D0 --------------------------------------------------------------

def hang_native():
    lib = compile_lib('hang.dylib', ['src/game/em_player_hang.c'])
    lib.em_player_hang_state.argtypes = [C.c_void_p, HANG.A]
    lib.em_player_hang_0017F240.argtypes = [HANG.A, C.c_int]
    lib.em_player_hang_vadd.argtypes = [C.c_void_p, HANG.FP, HANG.FP, HANG.FP]
    return lib


def check_hang(elf):
    HANG.ELF, HANG.NATIVE = elf, hang_native()
    beats = [b for b in BEATS if not b.startswith('a01_07')]
    beats = beats if RM.FULL else beats[:3]
    total, from_capture = 0, 0
    for beat in beats:
        ram, _ = image(beat)
        for address, size in ((HANG.HANG, HANG.HANG_END - HANG.HANG), (HANG.F240, HANG.F240_END - HANG.F240),
                              (HANG.VADD, 0x14)) + HANG.TABLES:
            at = address - 0x100000 + 0x300
            assert ram[address:address + size] == elf[at:at + size], ('capture differs from the ELF', beat, hex(address))
        HANG.RAM, HANG.CAPTURED, HANG._EE = ram, ram[PLAYER:PLAYER + 0x320], None
        seeds = [0xA01647D0 + 1000 * BEATS.index(beat) + i for i in range(RM.pick(1500, 120))]
        for seed in RM.parallel_map(HANG.run_case, seeds):
            total += 1
        from_capture += sum(1 for s in seeds if random.Random(s).random() < 0.3)
    return total, from_capture, len(beats)


# ---- 001F0460 --------------------------------------------------------------

def decal_lib():
    lib = compile_lib('effect.dylib', ['src/game/em_effect_original.c', 'src/game/em_point_light.c',
                                     'src/game/em_owner_services_original.c'])
    P, FP = C.POINTER, EFF.FP
    lib.em_effect_original_load_tables.argtypes = [C.c_char_p, C.c_size_t, P(EFF.Tables)]
    lib.em_effect_original_001F0460.argtypes = [P(EFF.Effect), C.c_int32, FP]
    return lib


def check_decal(elf):
    lib = decal_lib()
    beats = ('a01_00_train_room', 'a01_02_shaft_landing', 'a01_06_return_south')
    counts = {}
    total = 0
    for beat in beats if RM.FULL else beats[:2]:
        ram, spad = image(beat)
        EFF.decal_sweep(lib, elf, (bytearray(ram), bytearray(spad)), counts)
        total += counts['decal']
    return total


# ---- 001F4BF0 --------------------------------------------------------------

def glow_lib():
    lib = compile_lib('status_scene.dylib', ['src/game/em_status_scene_original.c'])
    lib.em_status_scene_glow_001F4BF0.argtypes = [C.c_uint32, C.c_uint32 * 4, C.POINTER(SCN.Workers),
                                                  C.POINTER(SCN.Fault)]
    return lib


def check_glow(elf):
    lib = glow_lib()
    wordsets = {(0x80, 0, 0, 0x80), (0, 0x80, 0, 0x80)}
    for beat in BEATS:
        _, spad = image(beat)
        wordsets.add(struct.unpack_from('<4I', spad, 0x38B0))
    rng = random.Random(0xA01F4BF0)
    rands = [0, 1, 0x7FFFFF, 0x7FFFFFFF] + [rng.getrandbits(31) for _ in range(RM.pick(40, 6))]
    n = 0
    for words in sorted(wordsets):
        for r in rands:
            o = SCN.fresh(elf)
            got = []
            for i, c in enumerate(words): o.save(0x700038B0 + 4 * i, c)
            o.calls.update({0x122BB8: lambda x, r=r: x.r.__setitem__(2, r),
                            0x1CD520: lambda x: got.append((x.r[4] & MASK, x.r[5] & MASK, x.r[6] & MASK,
                                                            x.r[7] & (2**64 - 1), x.r[8] & MASK,
                                                            x.f[12] & MASK, x.f[13] & MASK, x.f[14] & MASK))})
            o.run(0x1F4BF0, (0x700038A0, 0x700038B0))
            native = []

            def sprite(_, a0, a1, pos, tex0, rgb, f12, f13, f14):
                native.append((a0 & MASK, a1 & MASK, pos, tex0, rgb, SCN.bits(f12), SCN.bits(f13), SCN.bits(f14)))
                return 0
            fault = SCN.Fault()
            w = SCN.workers(w_00122BB8=lambda _, out, r=r: out.__setitem__(0, SCN.signed(r)) or 0, w_001CD520=sprite)
            assert lib.em_status_scene_glow_001F4BF0(0x700038A0, (C.c_uint32 * 4)(*words), C.byref(w),
                                                     C.byref(fault)) == 0, (words, r, fault.code)
            assert native == got, (words, hex(r), native, got)
            n += 1
    return n, len(wordsets)


def main():
    elf = read_elf()
    assert BEATS, 'no AREA01 captures under ' + str(ROUTE)
    fall_n, fall_calls, fall_outcomes, fall_entries = check_fall(elf)
    hang_n, hang_captured, hang_beats = check_hang(elf)
    decal_n = check_decal(elf)
    glow_n, glow_wordsets = check_glow(elf)
    RM.banner(f'{fall_n} 00163D50 / 00164220 cases on AREA01 player records ({fall_calls} scripted calls, '
              f'{fall_entries} with actor + scratch + arguments equal to the original\'s at entry, '
              f'{fall_outcomes} conditional-branch outcomes of the two routines seen)',
              f'{hang_n} 001647D0 cases over {hang_beats} AREA01 RAM images (~{hang_captured} from the AREA01 record)',
              f'{decal_n} 001F0460 decal cases over AREA01 beats',
              f'{glow_n} 001F4BF0 cases ({glow_wordsets} 0x700038B0 words from AREA01)')
    print('area01 render existing translations: PASS')


if __name__ == '__main__':
    main()
