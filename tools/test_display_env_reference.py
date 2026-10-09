#!/usr/bin/env python3
"""Main-loop steps R and U against the original (docs/GS_EXACT.md section 11).

The user's pinned ELF and the captured route RAM (build/s87/route) supply
every instruction and state; none are embedded here. Executed unmodified
and compared with their translations:

  A. 001002E0 (em_sdk_001002E0): every byte of the five dwords it writes,
     over a covering set of SDK mode halfwords (interlaced / not, field /
     frame, NTSC / PAL, the boot's and the ELF's GS revision) and argument
     values (halfword edges, upper bits that the sign-extension drops, the
     options' offsets). Its two ends the port does not run are compared as
     the faults they are: a zero width (the original's divide-by-zero break;
     native -1, the same three dwords written before it) and a video mode
     other than NTSC / PAL (the original calls the message printer 00122B58,
     hooked; native -1 after the same three dwords).
  B. 00100550 (em_sdk_00100550): the GS privileged register stores, address,
     value and order, for GS revision 1 (circuit 1) and others (circuit 2).
  C. Step R live: the original 001AB4E0 with 001002E0 and 00100268 running
     as original code, against em_slg_001AB4E0 with the native worker the
     live binding uses (em_sdk_001002E0 over the same D_00241010), at the
     offsets 0, the clamps +-20, the OPTIONS recording's kept 2 / 3 and
     random halfwords. Over captured RAM the native environments at the
     captured offset also equal the captured D_00810EA0..EF byte for byte.
  D. Step U live: the original 00100550 over the environment D_00810E80
     selects, against em_sdk_00100550; at the captured state DISPLAY2 is
     the measured DX 636 / DY 50 (0x001BF9FF0203227C).

Branch coverage: every conditional branch of 001002E0 and 00100550 is taken
both ways by the unit cases.

Default run (~5 s): the covering set plus a fixed-seed sample, one route
beat. EM_TEST_FULL=1: the whole sweep and every route beat.
"""
import ctypes as C
import random
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as rm  # noqa: E402
import test_startup_load_gaps_reference as G  # noqa: E402
from test_player_slide_reference import read_elf, s32  # noqa: E402

OUT = ROOT / 'build' / 'display_env_reference'
MASK = 0xFFFFFFFF
ENV0 = 0x810EA0
MODE = 0x241010
PRINTER = 0x122B58
SIZES = {0x1002E0: 0x270, 0x100550: 0xBC}
U8, I32, U32, U64, VP = C.c_uint8, C.c_int32, C.c_uint32, C.c_uint64, C.c_void_p
P, F = C.POINTER, C.CFUNCTYPE
STORE = F(I32, VP, U32, U64)
DISPLAY2_DEFAULT = 0x001BF9FF0203227C


class GsEE(G.GapEE):
    """GapEE with the GS privileged registers as a store log."""

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.gs = []

    def save(self, address, value, size=4):
        a = address & MASK
        if 0x12000000 <= a < 0x12002000:
            self.gs.append((a, value & ((1 << (8 * size)) - 1), size))
            return
        super().save(address, value, size)


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('display_env.dylib' if sys.platform == 'darwin' else 'display_env.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', 'src/game/em_sdk_display_original.c',
                    'src/game/em_startup_load_gaps.c', 'src/game/em_startup_load_gaps_sound.c', 'src/game/em_task.c',
                    '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    n.em_sdk_001002E0.argtypes = [P(U8), P(U8), I32, I32, I32, I32, I32]
    n.em_sdk_00100550.argtypes = [P(U8), P(U8), STORE, VP]
    n.em_slg_001AB4E0.argtypes = [P(G.DisplayWorkers), VP, I32, I32]
    return n


COVERAGE = set()


def note(o):
    COVERAGE.update((pc, t) for pc, t in o.outcomes if any(r <= pc < r + s for r, s in SIZES.items()))
    o.outcomes = set()


def mode_bytes(m):
    return b''.join((v & 0xFFFF).to_bytes(2, 'little') for v in m)


def run_original(o, entry, args):
    o.written = set()
    o.r[29] = 0x7F0F0000
    try:
        o.call(entry, args)
    except AssertionError as e:            # the break of a zero division
        return e.args[0]
    return None


# ---------------------------------------------------------------- A. 001002E0

def case_002e0(elf, native, m, args, o=None):
    o = o or GsEE(elf)
    o.write(MODE, mode_bytes(m))
    seed = bytes((7 * i + 3) & 0xFF for i in range(0x28))
    o.write(ENV0, seed)
    printed = []
    o.hooks[PRINTER] = lambda ee: printed.append(ee.arg(0))
    trap = run_original(o, 0x1002E0, (ENV0,) + tuple(a & MASK for a in args))
    note(o)
    env = (U8 * 0x28).from_buffer_copy(seed)
    mb = (U8 * 8).from_buffer_copy(mode_bytes(m))
    rc = native.em_sdk_001002E0(mb, env, *[s32(a & MASK) for a in args])
    want = o.read(ENV0, 0x28)
    if trap is not None:
        assert trap[0] == 'SPECIAL' and trap[1] == 13, ('001002E0 trap', m, args, trap)
        assert rc == -1 and bytes(env) == want, ('001002E0 zero width', m, args)
        return 'trap'
    if printed:
        assert printed == [0x26AE80] and rc == -1, ('001002E0 printer', m, args, printed, rc)
        assert bytes(env)[:0x18] == want[:0x18] and want[0x20:] == bytes(8), ('001002E0 printer bytes', m, args)
        assert bytes(env)[0x18:] == seed[0x18:], ('001002E0 wrote past the fault', m, args)
        return 'printer'
    assert rc == 0 and bytes(env) == want, ('001002E0', m, args, bytes(env).hex(), want.hex())
    G.check_written(o, [(ENV0, ENV0 + 0x28)], ('001002E0', m, args))
    return 'ok'


MODES = [(1, 2, 1, 0x1B), (1, 2, 1, 3), (1, 2, 0, 3), (0, 2, 0, 3), (0, 2, 1, 3), (2, 2, 1, 3), (1, 3, 1, 3),
         (1, 3, 0, 3), (0, 3, 1, 3), (1, 0, 1, 3), (1, 1, 1, 1), (-1, 2, 1, 3), (1, -2, 1, 3)]
EDGES = [0, 1, -1, 2, 20, -20, 0x7FFF, -0x8000, 0x1FF, 0x200, 0xE0, 0x1C0, 0x10000 + 5, -0x10000 - 3]


def args_002e0():
    rng = random.Random(0x1002E0)
    out = [(0, 0x200, 0xE0, x, y) for x in (0, 20, -20, 2) for y in (0, 40, -40, 6)]
    out += [(0, 0, 0xE0, 0, 0), (0, 0x10000, 0xE0, 3, 4)]               # zero widths
    out += [(p, w, h, x, y) for p in (0, 2, 0x1F) for w in (0x200, 1, -1, 0x7FFF, -0x8000, 63, 64, 0x10200)
            for h in (0xE0, 0, -1) for x, y in ((0, 0), (0x7FFF, -0x8000))]
    out += [tuple(rng.choice(EDGES) if rng.random() < 0.5 else rng.getrandbits(32) for _ in range(5))
            for _ in range(400)]
    return out


# ---------------------------------------------------------------- B. 00100550

def case_00550(elf, native, rev, env_bytes, o=None):
    o = o or GsEE(elf)
    o.write(MODE, mode_bytes((1, 2, 1, rev)))
    o.write(ENV0, env_bytes)
    o.gs = []
    assert run_original(o, 0x100550, (ENV0,)) is None
    note(o)
    got = []

    def store(_c, address, value):
        got.append((address, value, 8))
        return 0
    cb = STORE(store)
    mb = (U8 * 8).from_buffer_copy(mode_bytes((1, 2, 1, rev)))
    assert native.em_sdk_00100550(mb, (U8 * 0x28).from_buffer_copy(env_bytes), cb, None) == 0
    assert got == o.gs, ('00100550 stores', rev, got, o.gs)
    G.check_written(o, [], ('00100550', rev))
    return got


# ---------------------------------------------------------------- C/D. steps R and U

def native_r(native, mode, env, dx, dy):
    """em_slg_001AB4E0 with the live worker: em_sdk_001002E0 over `mode`."""
    mb = (U8 * 8).from_buffer_copy(mode)

    def w(_c, p, psm, width, height, x, y):
        return native.em_sdk_001002E0(mb, p, psm, width, height, x, y)
    w = G.workers(G.DisplayWorkers, {'w_001002E0': w})
    assert native.em_slg_001AB4E0(C.byref(w), C.addressof(env), dx, dy) == 0


def step_ru(elf, native, ram, spad, offsets, where):
    o = GsEE(elf, ram, spad)
    mode = o.read(MODE, 8)
    for x, y in offsets:
        o.save(0x70003B94, x & 0xFFFF, 2)
        o.save(0x70003B96, y & 0xFFFF, 2)
        seed = bytes(random.Random(x * 977 + y).randbytes(0x50))
        o.write(ENV0, seed)
        assert run_original(o, 0x1AB4E0, (x & MASK, y & MASK)) is None
        env = (U8 * 0x50).from_buffer_copy(seed)
        native_r(native, mode, env, x, y)
        assert o.read(ENV0, 0x50) == bytes(env), ('step R', where, x, y)
        G.check_written(o, [(ENV0, ENV0 + 0x50)], ('step R', where, x, y))
        for buffer in (0, 1):
            o.gs = []
            assert run_original(o, 0x100550, (ENV0 + 40 * buffer,)) is None
            got = []
            cb = STORE(lambda _c, a, v: got.append((a, v, 8)) or 0)
            assert native.em_sdk_00100550((U8 * 8).from_buffer_copy(mode),
                                          (U8 * 0x28).from_buffer_copy(bytes(env)[40 * buffer:40 * buffer + 40]),
                                          cb, None) == 0
            assert got == o.gs, ('step U', where, x, y, buffer)
            regs = {a: v for a, v, _ in got}
            dx = 636 + 5 * x
            dy = 50 + 2 * y
            if -20 <= x <= 20 and -20 <= y <= 20:
                assert regs[0x120000A0] & 0xFFF == dx and regs[0x120000A0] >> 12 & 0x7FF == dy, \
                    ('DISPLAY2 position', where, x, y, hex(regs[0x120000A0]))
            if (x, y) == (0, 0):
                assert regs[0x120000A0] == DISPLAY2_DEFAULT and regs[0x120000E0] == 0, ('default', where)
    return mode


def captured_envs(elf, native, beat):
    """The captured environments equal the native step R at the captured
    offset (the route captures' 0, 0)."""
    ram, spad = G.captured(beat)
    o = GsEE(elf, ram, spad)
    x, y = s32(o.load(0x70003B94, 2) << 16) >> 16, s32(o.load(0x70003B96, 2) << 16) >> 16
    env = (U8 * 0x50)()
    native_r(native, o.read(MODE, 8), env, x, y)
    assert bytes(env) == o.read(ENV0, 0x50), ('captured D_00810EA0', beat, bytes(env).hex(), o.read(ENV0, 0x50).hex())
    return ram, spad, (x, y)


def main():
    elf = read_elf()
    native = build_native()
    # A
    cases = [(m, a) for m in MODES for a in args_002e0()]
    cases = rm.select(cases, 900, 0x1002E0, axes=(lambda c: c[0], lambda c: c[1][1] & 0xFFFF == 0),
                      keep=lambda i, c: (c[1][3:] in ((20, 40), (-20, -40), (2, 6), (0, 0)) and
                                         c[1][:3] == (0, 0x200, 0xE0)) or c[1][1] & 0xFFFF == 0)
    o = GsEE(elf)
    kinds = {}
    for m, a in cases:
        k = case_002e0(elf, native, m, a, o)
        kinds[k] = kinds.get(k, 0) + 1
    assert set(kinds) == {'ok', 'trap', 'printer'}, kinds
    # B
    rng = random.Random(0x100550)
    envs = [rng.randbytes(0x28) for _ in range(rm.pick(200, 20))]
    n550 = 0
    for rev in (1, 3, 0x1B, 0x101, -1):
        for e in envs:
            stores = case_00550(elf, native, rev, e, o)
            n550 += 1
        assert [a for a, _, _ in stores] == ([0x12000000, 0x12000070, 0x12000080, 0x120000C0] if rev == 1 else
                                             [0x12000000, 0x12000020, 0x12000090, 0x120000A0, 0x120000E0])
    missing = []
    for r, s in SIZES.items():
        for pc in G.conditional_branches(o, r, s):
            for t in (True, False):
                if (pc, t) not in COVERAGE:
                    missing.append((hex(pc), t))
    assert not missing, ('branch outcomes never taken', missing)
    # C / D
    beats = rm.select(G.route_beats(), 1, 0x1AB4E0)
    offsets = [(0, 0), (20, 0), (-20, 0), (0, 20), (0, -20), (2, 3), (-20, 20)]
    offsets += [(s32(rng.getrandbits(16) << 16) >> 16, s32(rng.getrandbits(16) << 16) >> 16)
                for _ in range(rm.pick(40, 4))]
    for beat in beats:
        ram, spad, xy = captured_envs(elf, native, beat)
        assert xy == (0, 0), ('captured offset', beat, xy)
        step_ru(elf, native, ram, spad, offsets, beat)
    # The live binding's D_00241010 is the render context's (the ELF's
    # .data; GS revision 3 where the boot stored 0x1B): the same stores.
    ram, spad = G.captured(beats[0]) if beats else (None, None)
    o = GsEE(elf, ram, spad)
    o.write(MODE, elf[MODE - 0x100000 + 0x300:MODE - 0x100000 + 0x308])
    step_ru(elf, native, o.mem, o.spad, offsets[:7], 'ELF D_00241010')
    rm.banner(f'001002E0 {len(cases)} cases ({kinds["ok"]} written, {kinds["trap"]} zero-width traps, '
              f'{kinds["printer"]} message-printer exits)', f'00100550 {n550}',
              f'steps R / U at {len(offsets)} offsets', rm.part(len(beats), len(G.route_beats()), 'route beats'))
    print(f'Display environments: PASS ({sum(len(G.conditional_branches(o, r, s)) for r, s in SIZES.items())} '
          'conditional branches, every outcome taken)')


if __name__ == '__main__':
    main()
