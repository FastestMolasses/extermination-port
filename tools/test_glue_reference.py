#!/usr/bin/env python3
"""Execute the original 001FC280 (the area ambient loop) and compare
em_glue_original.c's em_glue_001FC280 (docs/GLUE_ORIGINAL.md).

The user's pinned ELF supplies every instruction and the spawn tables
D_0024D650; the route captures (../Extermination/build/s87/route) supply
the captured states. Nothing original is embedded here.

The three callees are hooked and recorded on the original side and given
the same answers on the native side: 0011A070 (stop a track), 001FB9F0
(start a sound; it answers a scripted handle) and 00119828 (a lane's
volume). The test asserts that these are exactly the routine's jal targets,
that the original stores no byte outside D_00282160..D_00282173 (stack
excluded), and that the default run takes every conditional branch of the
routine both ways.

Cases:
  - every spawn record the ELF's table reaches (area, room, entry: the walk
    of tools/export_spawn_table.py), under four cache states: D_00282160 =
    -1 (the title's 001FBC50), the record's own id (no change), another id
    (the stop branch, which the first level never reaches) and the record's
    id with event 0x30 forcing 0x44E in area 0x0B;
  - D_00810788 = 0 and 0xFF in every area (0x44E is forced only in 0x0B);
  - every route capture as captured (its area bytes, event byte and cache).
Compared: D_00282160..D_00282170, every call in order with its arguments,
and the return. Every case runs in the default run (about 1 s).
"""
import ctypes as C
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as RM  # noqa: E402
import export_spawn_table as EST  # noqa: E402
from test_player_slide_reference import EE, read_elf, s32, DECOMP  # noqa: E402

OUT = ROOT / 'build' / 'glue_reference'
ROUTE = DECOMP / 'build/s87/route'
ENTRY, SIZE = 0x1FC280, 0x140
STOP, START, VOLUME = 0x11A070, 0x1FB9F0, 0x119828
CACHE = 0x282160            # D_00282160 .. D_00282173
MASK = 0xFFFFFFFF
RAM_SIZE = 0x2000000


class GlueEE(EE):
    """The shared EE core with a record of every non-stack byte stored and
    of every conditional branch outcome."""

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.written = set()
        self.outcomes = set()

    def save(self, address, value, size=4):
        address &= MASK
        if not 0x7F000000 <= address < 0x7F100000:
            self.written.update(range(address, address + size))
        super().save(address, value, size)

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and ENTRY <= pc < ENTRY + SIZE:
            self.outcomes.add((pc, b[0]))
        return b


# ---------------------------------------------------------------- native
I32, U8, VP = C.c_int32, C.c_uint8, C.c_void_p
LOAD = C.CFUNCTYPE(VP, VP, C.c_uint32, C.c_uint32)
W_STOP = C.CFUNCTYPE(I32, VP, I32)
W_START = C.CFUNCTYPE(I32, VP, I32, I32, I32, I32, C.POINTER(I32))
W_VOLUME = C.CFUNCTYPE(I32, VP, I32, I32, I32)


class Workers(C.Structure):
    _fields_ = [('ctx', VP), ('load', LOAD), ('w_0011A070', W_STOP), ('w_001FB9F0', W_START),
                ('w_00119828', W_VOLUME)]


class Ambient(C.Structure):
    _fields_ = [('d282160', I32), ('d282164', I32), ('d282168', I32), ('d28216C', I32), ('d282170', I32)]


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('glue.dylib' if sys.platform == 'darwin' else 'glue.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-ffp-contract=off',
                    '-shared', '-fPIC', '-I', str(ROOT / 'src'), str(ROOT / 'src/game/em_glue_original.c'),
                    '-o', str(lib)], check=True)
    n = C.CDLL(str(lib))
    n.em_glue_001FC280.argtypes = [C.POINTER(Workers), C.POINTER(Ambient), U8, U8, U8, U8]
    n.em_glue_001FC280.restype = I32
    return n


def jal_targets(elf_ee):
    out = set()
    for pc in range(ENTRY, ENTRY + SIZE, 4):
        word = elf_ee.load(pc)
        if word >> 26 == 3:
            out.add((pc & 0xF0000000) | (word & 0x3FFFFFF) << 2)
    return out


# ---------------------------------------------------------------- one case
def original(elf, ram, area, room, entry, d788, cache, handle):
    o = GlueEE(elf, ram)
    o.save(0x810700, area, 1)
    o.save(0x810701, room, 1)
    o.save(0x810702, entry, 1)
    o.save(0x810788, d788, 1)
    for i, v in enumerate(cache):
        o.save(CACHE + 4 * i, v & MASK)
    o.written.clear()
    calls = []

    def stop(e):
        calls.append(('0011A070', s32(e.arg(0))))

    def start(e):
        calls.append(('001FB9F0',) + tuple(s32(e.arg(i)) for i in range(4)))
        e.ret_int(handle)

    def volume(e):
        calls.append(('00119828',) + tuple(s32(e.arg(i)) for i in range(3)))

    o.hooks.update({STOP: stop, START: start, VOLUME: volume})
    o.call(ENTRY, ())
    after = tuple(s32(o.load(CACHE + 4 * i)) for i in range(5))
    outside = sorted(a for a in o.written if not CACHE <= a < CACHE + 0x14)
    assert not outside, ('001FC280 stores outside D_00282160..73', [hex(a) for a in outside[:8]])
    return after, calls, o.outcomes


def native(n, ram_buffer, area, room, entry, d788, cache, handle):
    calls = []
    base = C.addressof(ram_buffer)

    def load(ctx, address, size):
        a = address & 0x1FFFFFF
        return base + a if a + size <= RAM_SIZE and address < 0x40000000 else None

    def stop(ctx, h):
        calls.append(('0011A070', h))
        return 0

    def start(ctx, sid, a1, a2, a3, ret):
        calls.append(('001FB9F0', sid, a1, a2, a3))
        ret[0] = handle
        return 0

    def volume(ctx, lane, a1, a2):
        calls.append(('00119828', lane, a1, a2))
        return 0

    w = Workers(None, LOAD(load), W_STOP(stop), W_START(start), W_VOLUME(volume))
    s = Ambient(*[s32(v) for v in cache])
    rc = n.em_glue_001FC280(C.byref(w), C.byref(s), area, room, entry, d788)
    assert rc == 0, ('em_glue_001FC280 faulted', area, room, entry)
    return (s.d282160, s.d282164, s.d282168, s.d28216C, s.d282170), calls


def compare(elf, n, ram, ram_buffer, case, where):
    area, room, entry, d788, cache, handle = case
    want = original(elf, ram, area, room, entry, d788, cache, handle)
    got = native(n, ram_buffer, area, room, entry, d788, cache, handle)
    assert got == want[:2], (where, case, 'native', got, 'original', want[:2])
    return want[2]


def failstop(n):
    """An unbound worker set returns -1 and changes nothing."""
    s = Ambient(-1, 0, 0, 0, 0)
    for missing in ('load', 'w_0011A070', 'w_001FB9F0', 'w_00119828'):
        fields = dict(ctx=None, load=LOAD(lambda c, a, z: None), w_0011A070=W_STOP(lambda c, h: 0),
                      w_001FB9F0=W_START(lambda c, i, a, b, d, r: 0), w_00119828=W_VOLUME(lambda c, l, a, b: 0))
        fields[missing] = type(fields[missing])()
        w = Workers(**fields)
        assert n.em_glue_001FC280(C.byref(w), C.byref(s), 0x0B, 0, 0, 0) == -1, missing
    assert (s.d282160, s.d282164) == (-1, 0)


def main():
    elf = read_elf()
    n = build_native()
    image = GlueEE(elf)                    # the ELF's data, for the table walk
    targets = jal_targets(image)
    assert targets == {STOP, START, VOLUME}, ('001FC280 calls', sorted(hex(t) for t in targets))
    ram = bytes(image.mem)
    ram_buffer = (C.c_uint8 * RAM_SIZE).from_buffer_copy(ram)
    read = EST.elf_reader((DECOMP / 'config/SCUS_971.12').read_bytes())
    areas, ranges = EST.walk(read)
    # Each entry array runs to the next one, the last to the first room
    # pointer array (the bound tools/export_spawn_table.py checks).
    starts = sorted({r for _, rs in areas.values() for r in rs})
    floor = min(min(t for t, _ in areas.values()), EST.D_0024D650)

    def records(entries):
        later = [s for s in starts if s > entries]
        return ((later[0] if later else floor) - entries) // EST.RECORD

    cases = []
    for area, (_table, rooms) in sorted(areas.items()):
        for room, entries in enumerate(rooms):
            for entry in range(records(entries)):
                word = struct.unpack_from('<I', ram, entries + entry * EST.RECORD + 0x20)[0]
                rid = s32(word) >> 16
                for kind in ('none', 'same', 'other', 'forced'):
                    if kind == 'forced' and area != 0x0B:
                        continue
                    d788 = 0xFF if kind == 'forced' else 0
                    own = 0x44E if (area == 0x0B and d788 == 0xFF) else rid
                    cache = {'none': (-1, 0, 0, 0, 0), 'same': (own, 0x21, 0x1000, 0x1000, 0x1000),
                             'other': (0x123, 0x17, 0x800, 0x900, 0xA00),
                             'forced': (-1, 0, 0, 0, 0)}[kind]
                    cases.append((area, room, entry, d788, cache, 0x40 + entry))
                # D_00810788 = 0xFF outside area 0x0B forces nothing.
                if area != 0x0B:
                    cases.append((area, room, entry, 0xFF, (-1, 0, 0, 0, 0), 0x40 + entry))
    selected = RM.select(cases, 1000, 0x1FC280,
                         axes=(lambda c: c[0], lambda c: (c[0], c[1]), lambda c: c[4][0] == -1,
                               lambda c: c[4][0] == 0x123, lambda c: c[3]),
                         keep=lambda i, c: c[0] in (0x01, 0x0B) and c[2] in (0, 4))
    outcomes = set()
    for case in selected:
        outcomes |= compare(elf, n, ram, ram_buffer, case, 'table')
    # The captured states, as captured.
    beats = sorted(p for p in ROUTE.iterdir() if (p / 'eeMemory.bin').exists())
    for beat in beats:
        cap = (beat / 'eeMemory.bin').read_bytes()
        buf = (C.c_uint8 * RAM_SIZE).from_buffer_copy(cap)
        cache = tuple(s32(struct.unpack_from('<I', cap, CACHE + 4 * i)[0]) for i in range(5))
        case = (cap[0x810700], cap[0x810701], cap[0x810702], cap[0x810788], cache, 0x55)
        outcomes |= compare(elf, n, cap, buf, case, beat.name)
    branches = {pc for pc, _ in outcomes}
    one_way = sorted(hex(pc) for pc in branches if (pc, True) not in outcomes or (pc, False) not in outcomes)
    assert not one_way, ('branches taken one way only', one_way)
    failstop(n)
    print('glue reference: PASS (' + RM.banner(RM.part(len(selected), len(cases), '001FC280 table cases'),
                                              f'{len(beats)} route captures',
                                              f'{len(branches)} branches both ways') + ')')


if __name__ == '__main__':
    main()
