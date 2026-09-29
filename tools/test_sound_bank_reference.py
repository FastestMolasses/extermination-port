#!/usr/bin/env python3
"""The sound-bank upload against the ORIGINAL instructions and the captures
(docs/IOP_STREAM.md "The sound-bank transfer"; src/game/em_ee_sound_lib.c,
src/game/em_sound_bank.c, em_iop_stream's command 0x20 / 0x544 / heap / SIF).

The user's pinned boot ELF supplies every instruction; the user's captures
(the title save state, startup-reference slot 01, and route beat 00) supply
the state; the AREA11 sound bank is read from the user's disc image. Nothing
original is embedded or printed here; build/ holds counts only.

A. Library. 00119400, 001193A8, 00119528, 001194B8, 00119450, 001195A8 and
   001199F0 execute unmodified (with 001157F0 and 00121A28 as their original
   code) over the title RAM, on random handle tables, voice records, SShd
   headers, counts and acknowledgements and every argument edge, against
   em_ee_sound_lib: every byte of D_0027C6C0, D_002819C0 and
   D_0027F740 + 0x48, the return value and each queued command (the queue
   full at 255 included) must be equal; the original stores nothing else
   outside its stack and the queue.
B. Chain. The ORIGINAL 001FB370 (with 001FB3E0, 001FB910, the library and
   001157F0 as original code) over the title RAM, with the AREA11 bank at
   0x13351C0 (where the New Game's module 3 leaves D_0028A73C), one call per
   frame, against em_sound_bank on em_iop_stream. Only the kernel and RPC
   leaves are hooked on the original side (sceSifSetDChain, FlushCache,
   sceSifSetDma, sceSifDmaStat, 0010F8F8, 0010F968), answered by an
   independent model of the port's stated host-speed rules (the DMA done at
   its first query, first-fit heap after the measured boot blocks); the
   IOP's acknowledgement D_002817C0 + 0x1C0 comes from an independent model
   of the exchange (commands queued in a frame run in the next field, and
   their count reaches the EE at the exchange after that). After every call
   the result and every modelled byte (D_00282150 .. D_002821A7,
   D_00281D30, D_00281D50, D_00275B18 / 1C, D_0027C6C0, D_002819C0, the
   count and the acknowledgement), the queued commands and the IOP heap
   block must be equal.
C. Captures. EM_SOUND_BANK_SEEDS equal the title capture byte for byte (the
   boot's and the title's uploads, which the port does not run); after the
   chain every modelled global equals route capture 00 (the kernel's DMA id
   D_002821A0 and D_002819C0, which the SFX sequencer 00116DB8 consumes
   later, excepted and said so); the native SPU RAM at the bucket's base
   and the IOP RAM at the bank's heap block hold exactly what route 00's
   SPU2 and IOP RAM hold there; the wait before the acknowledgement leaves
   D_00275B18 = route 00's (the timing mutants below leave another value).
D. Mutants of the exchange model (the acknowledgement one field earlier or
   later) each end with a D_00275B18 the capture refutes.

Default run ~3 s; EM_TEST_FULL=1 runs the exhaustive library sweep.
"""
import ctypes as C
import os
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from reference_mode import FULL, banner, part, pick  # noqa: E402
from test_player_slide_reference import EE, read_elf  # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
OUT = ROOT / 'build' / os.environ.get('EM_LANE', 'sound_bank_reference')
TITLE_STATE = DECOMP / 'build/startup-reference/portable-data/sstates/SCUS-97112 (0AE679AF).01.p2s'
ROUTE00 = DECOMP / 'build/s87/route/00_panel_no_battery/state.p2s'
ISO = DECOMP / 'Extermination-rebuilt.iso'
MASK = 0xFFFFFFFF

D_27C6C0, D_2819C0, D_27F740, D_2817C0, D_27CCC0, D_27F7C0 = \
    0x27C6C0, 0x2819C0, 0x27F740, 0x2817C0, 0x27CCC0, 0x27F7C0
BANK_AT = 0x13351C0          # D_0028A73C after the New Game's module 3 (MODULE_LOADER.md 1.8)
BANK_END = 0x1335F40         # D_0028A73C after the bank (route 00)
LIB = {'00119400': 0x119400, '001193A8': 0x1193A8, '00119528': 0x119528, '001194B8': 0x1194B8,
       '00119450': 0x119450, '001195A8': 0x1195A8, '001199F0': 0x1199F0}
KERNEL = (0x10BC00, 0x10BAA0, 0x10BBE0, 0x10BBC0, 0x10F8F8, 0x10F968)
BASES = (0x15040, 0x15040, 0x1A0000, 0x122000, 0x132000)
HEAP_BOOT = ((0x85B00, 0x28100), (0xADC00, 0x10100), (0xBDD00, 0x10100), (0xCDE00, 0x10100),
             (0xDDF00, 0x900))
STACK = 0x7F0F0000

BRIDGE = r'''
#include <string.h>
#include "game/em_sound_bank.h"
static EmIopStream *S;
static EmSoundBank B;
static uint16_t VST[48], VH[48];
static int voice_cb(void *c, int32_t v, uint16_t *st, uint16_t *h)
{ (void)c; if (v < 0 || v >= 48) return -1; *st = VST[v]; *h = VH[v]; return 0; }
void nb_voices(const uint16_t *st, const uint16_t *h) { memcpy(VST, st, sizeof VST); memcpy(VH, h, sizeof VH); }
int nb_init(const int32_t *base)
{
    uint32_t out[5];
    if (S) em_iop_stream_destroy(S);
    if (!(S = em_iop_stream_create())) return -1;
    if (em_iop_stream_boot_buffers(S, out) || em_iop_stream_boot_driver(S)) return -2;
    if (em_sound_bank_init(&B, S, base)) return -3;
    em_sound_bank_set_voice(&B, voice_cb, 0);
    return 0;
}
int nb_field(void) { return em_iop_stream_field(S); }
int nb_call(uint32_t at, const uint8_t *bytes, uint32_t size, uint32_t *res)
{ return em_sound_bank_001FB370(&B, at, bytes, size, res); }
int nb_fault(uint32_t *at) { int32_t code = 0; em_sound_bank_failed(&B, at, &code); return code; }
/* The modelled bytes in the original's order (see DUMP in the test). */
void nb_dump(uint32_t *o)
{
    const EmSlgBankLoad *s = &B.load;
    int k = 0;
    o[k++] = (uint8_t)s->d282150; o[k++] = (uint8_t)s->d282151; o[k++] = (uint8_t)s->d282159;
    o[k++] = (uint8_t)s->d28215C;
    o[k++] = (uint32_t)s->d282190; o[k++] = (uint32_t)s->d282194; o[k++] = (uint32_t)s->d282198;
    o[k++] = s->d28219C; o[k++] = (uint32_t)s->d2821A0; o[k++] = s->d2821A4;
    for (int i = 0; i < 8; i++) o[k++] = (uint32_t)s->d281D30[i];
    for (int i = 0; i < 120; i++) o[k++] = (uint32_t)s->d281D50[i];
    o[k++] = (uint32_t)s->d275B18; o[k++] = (uint32_t)s->d275B1C;
    for (int i = 0; i < 128; i++) for (int j = 0; j < 3; j++) o[k++] = B.lib.d27C6C0[i][j];
    for (int i = 0; i < 256; i += 4)
        o[k++] = (uint32_t)B.lib.d2819C0[i] | (uint32_t)B.lib.d2819C0[i + 1] << 8 |
                 (uint32_t)B.lib.d2819C0[i + 2] << 16 | (uint32_t)B.lib.d2819C0[i + 3] << 24;
    o[k++] = (uint32_t)B.lib.d27F788;
    o[k++] = (uint32_t)em_iop_stream_ee_status(S)[112];
}
int nb_queue(uint32_t *out) { uint32_t n; const uint32_t (*q)[4] = em_iop_stream_ee_queue(S, &n); memcpy(out, q, n * 16u); return (int)n; }
void nb_ram(uint32_t spu, uint32_t iop, uint32_t size, uint8_t *spu_out, uint8_t *iop_out)
{ memcpy(spu_out, em_iop_stream_spu_ram(S) + spu, size); memcpy(iop_out, em_iop_stream_iop_ram(S) + iop, size); }
uint32_t nb_heap_next(void) { return em_iop_stream_heap_next(S); }

/* ---- the library alone ---- */
static EmIopStream *L;
static uint32_t WIN_BASE, WIN_SIZE;
static const uint8_t *WIN;
static int32_t ACK;
static int l_q(void *c, int32_t cmd, int32_t a1, int32_t a2, int32_t a3)
{ (void)c; (void)em_iop_stream_001157F0(L, cmd, a1, a2, a3); return em_iop_stream_fault(L)->code ? -1 : 0; }
static int l_ack(void *c, int32_t *v) { (void)c; *v = ACK; return 0; }
static int l_word(void *c, uint32_t a, uint32_t *v)
{
    (void)c;
    if (a < WIN_BASE || a - WIN_BASE > WIN_SIZE - 4) return -1;
    memcpy(v, WIN + (a - WIN_BASE), 4);
    return 0;
}
int nl_call(int fn, uint32_t *table, uint8_t *vol, int32_t *count, int32_t ack, const uint8_t *win,
            uint32_t win_base, uint32_t win_size, int prefill, const int32_t *a, int32_t *ret,
            uint32_t *queue, uint32_t *nq)
{
    EmEeSoundLib lib;
    EmEeSoundLibFault f = {0, 0};
    EmEeSoundLibWorkers w = {0, l_q, l_ack, l_word, voice_cb};
    int r = -9;
    if (L) em_iop_stream_destroy(L);
    L = em_iop_stream_create();
    for (int i = 0; i < prefill; i++) em_iop_stream_001157F0(L, 0x3D, i, 0, 0);
    memcpy(lib.d27C6C0, table, sizeof lib.d27C6C0);
    memcpy(lib.d2819C0, vol, sizeof lib.d2819C0);
    lib.d27F788 = *count;
    ACK = ack; WIN = win; WIN_BASE = win_base; WIN_SIZE = win_size;
    switch (fn) {
    case 0: r = em_ee_sound_lib_00119400(&lib, &w, a[0], a[1], a[2], a[3], &f); *ret = 0; break;
    case 1: r = em_ee_sound_lib_001193A8(&lib, &w, a[0], a[1], a[2], ret, &f); break;
    case 2: r = em_ee_sound_lib_00119528(&lib, &w, (uint32_t)a[0], (uint32_t)a[1], ret, &f); break;
    case 3: r = em_ee_sound_lib_001194B8(&lib, &w, a[0], (uint32_t)a[1], a[2], ret, &f); break;
    case 4: r = em_ee_sound_lib_00119450(&lib, &w, a[0], ret, &f); break;
    case 5: r = em_ee_sound_lib_001195A8(&lib, &w, (uint32_t)a[0], ret, &f); break;
    case 6: r = em_ee_sound_lib_001199F0(&lib, a[0], a[1], ret, &f); break;
    }
    memcpy(table, lib.d27C6C0, sizeof lib.d27C6C0);
    memcpy(vol, lib.d2819C0, sizeof lib.d2819C0);
    *count = lib.d27F788;
    { const uint32_t (*q)[4] = em_iop_stream_ee_queue(L, nq); memcpy(queue, q, *nq * 16u); }
    return r < 0 ? 100 + f.code : 0;
}
'''

DUMP_WORDS = 4 + 6 + 8 + 120 + 2 + 384 + 64 + 2


def u32(b, o): return struct.unpack_from('<I', b, o)[0]


def s32(v):
    v &= MASK
    return v - (1 << 32) if v & 0x80000000 else v


# ------------------------------------------------------------------ inputs

def extract(state, name):
    """eeMemory / iopMemory / SPU2 of a save state, cached under build/ (the
    decomp's venv has zstd)."""
    cache = OUT / 'states' / name
    if not (cache / 'SPU2.bin').exists():
        cache.mkdir(parents=True, exist_ok=True)
        code = ('import sys\nfrom pathlib import Path\nfrom tools.parse_pcsx2_state import extract_zstd_entry\n'
                'o = Path(sys.argv[1]); p = Path(sys.argv[2])\n'
                'for m in ("eeMemory.bin", "iopMemory.bin", "SPU2.bin"): (o / m).write_bytes(extract_zstd_entry(p, m))\n')
        subprocess.run([str(DECOMP / '.venv/bin/python'), '-c', code, str(cache), str(state)], cwd=DECOMP,
                       check=True)
    return {m: (cache / m).read_bytes() for m in ('eeMemory.bin', 'iopMemory.bin', 'SPU2.bin')}


def area11_bank():
    """The AREA11 sound bank as 001FF590(0xAB, 0) reads it (entry 0 of
    INDEX.IDX sector 0x0F), from the user's disc image."""
    import export_module_loader as X
    disc = X.Disc(ISO)
    h = disc.sectors(disc.index[0] + 0x0F, 1)
    base, off, size = u32(h, 4), u32(h, 0x20), u32(h, 0x24)
    lsn, sectors = X.read_span(disc.data, base + off, size)
    return disc.sectors(lsn, sectors)[:size]


class Native:
    def __init__(self):
        OUT.mkdir(parents=True, exist_ok=True)
        (OUT / 'bridge.c').write_text(BRIDGE)
        lib = OUT / 'sound_bank.dylib'
        srcs = ['src/game/em_sound_bank.c', 'src/game/em_ee_sound_lib.c', 'src/game/em_iop_stream.c',
                'src/game/em_startup_load_gaps_sound.c', 'src/game/em_stream_lanes_original.c',
                'src/game/em_sfx_bank.c']
        subprocess.run(['cc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
                        '-fPIC', '-Isrc', *srcs, str(OUT / 'bridge.c'), '-o', str(lib)], cwd=ROOT, check=True)
        self.lib = C.CDLL(str(lib))

    def dump(self):
        out = (C.c_uint32 * DUMP_WORDS)()
        self.lib.nb_dump(out)
        return list(out)


# ------------------------------------------------------------------ oracle

class SoundEE(EE):
    """The EE core with every non-stack store recorded."""

    def __init__(self, elf, ram):
        super().__init__(elf, ram)
        self.stores = set()

    def save(self, address, value, size=4):
        a = address & MASK
        if not 0x7F000000 <= a < 0x7F100000:
            self.stores.update(range(a, a + size))
        super().save(address, value, size)

    def write(self, address, data):
        a = address & MASK
        if not 0x7F000000 <= a < 0x7F100000:
            self.stores.update(range(a, a + len(data)))
        super().write(address, data)

    def mmi(self, word, pc):
        """Adds PCPYH (the C runtime memset 00121A28 broadcasts with it;
        001195A8's entry clear)."""
        if word & 63 == 0x29 and (word >> 6 & 31) == 0x1B:
            rt, rd = word >> 16 & 31, word >> 11 & 31
            lo, hi = self.r[rt] & 0xFFFF, self.rh[rt] & 0xFFFF
            if rd:
                self.r[rd] = lo * 0x0001000100010001
                self.rh[rd] = hi * 0x0001000100010001
            return
        super().mmi(word, pc)



def oracle_dump(o):
    out = [o.load(0x282150, 1), o.load(0x282151, 1), o.load(0x282159, 1), o.load(0x28215C, 1)]
    out += [o.load(0x282190 + 4 * i) for i in range(6)]
    out += [o.load(0x281D30 + 4 * i) for i in range(8)]
    out += [o.load(0x281D50 + 4 * i) for i in range(120)]
    out += [o.load(0x275B18), o.load(0x275B1C)]
    out += [o.load(D_27C6C0 + 4 * i) for i in range(384)]
    out += [o.load(D_2819C0 + 4 * i) for i in range(64)]
    out += [o.load(D_27F740 + 0x48), o.load(D_2817C0 + 0x1C0)]
    return out


DUMP_NAMES = (['D_00282150', 'D_00282151', 'D_00282159', 'D_0028215C'] +
              [f'D_{0x282190 + 4 * i:08X}' for i in range(6)] + [f'D_00281D30[{i}]' for i in range(8)] +
              [f'D_00281D50[{i}]' for i in range(120)] + ['D_00275B18', 'D_00275B1C'] +
              [f'D_0027C6C0 word {i}' for i in range(384)] + [f'D_002819C0 word {i}' for i in range(64)] +
              ['D_0027F740+0x48', 'D_002817C0+0x1C0'])


def first_diff(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x & MASK != y & MASK:
            return DUMP_NAMES[i], hex(x & MASK), hex(y & MASK)
    return None


# ------------------------------------------------------------ A. library

def queue_entries(o, before):
    """The commands the original 001157F0 queued since `before` entries."""
    idx, n = o.load(D_27F740 + 0x3C), o.load(D_27F740 + 0x40)
    base = D_27F7C0 + (idx << 12)
    return n, [tuple(o.load(base + 16 * k + 4 * j) for j in range(4)) for k in range(before, n)]


def library_case(elf, ram, native, rng, fn, args, prefill, voices, table, vol, count, ack, header):
    o = SoundEE(elf, ram)
    # State on both sides.
    for i in range(128):
        for j in range(3):
            o.save(D_27C6C0 + 12 * i + 4 * j, table[3 * i + j])
    o.write(D_2819C0, bytes(vol))
    o.save(D_27F740 + 0x48, count)
    o.save(D_2817C0 + 0x1C0, ack)
    for v, (st, h) in enumerate(voices):
        o.save(D_27CCC0 + 0x6A * v, st, 2)
        o.save(D_27CCC0 + 0x6A * v + 0x22, h, 2)
    o.save(D_27F740 + 0x40, prefill)
    win_base = 0x1400000
    o.write(win_base, header)
    o.stores.clear()
    before_count = prefill
    o.r[29] = STACK
    o.call(LIB[fn], args)
    ret = s32(o.r[2])
    n_o, q_o = queue_entries(o, before_count)
    # Native.
    ntable = (C.c_uint32 * 384)(*table)
    nvol = (C.c_uint8 * 256)(*vol)
    ncount = C.c_int32(count)
    nret = C.c_int32(0)
    nq = (C.c_uint32 * (4 * 256))()
    nqn = C.c_uint32(0)
    st = (C.c_uint16 * 48)(*[v[0] for v in voices])
    hh = (C.c_uint16 * 48)(*[v[1] for v in voices])
    native.lib.nb_voices(st, hh)
    fcode = native.lib.nl_call(list(LIB).index(fn), ntable, nvol, C.byref(ncount), ack, header, win_base,
                               len(header), prefill, (C.c_int32 * 4)(*[s32(a) for a in args] + [0] * (4 - len(args))),
                               C.byref(nret), nq, C.byref(nqn))
    label = (fn, [hex(a & MASK) for a in args], prefill)
    assert fcode == 0, (label, 'native fault', fcode)
    q_n = [tuple(nq[4 * k + j] for j in range(4)) for k in range(prefill, nqn.value)]
    if fn != '00119400':   # 00119400's value is 001157F0's (no caller reads it); the port returns 0
        assert ret == nret.value, (label, 'return', ret, nret.value)
    assert [tuple(x & MASK for x in e) for e in q_o] == q_n, (label, 'queued', q_o, q_n)
    assert n_o == nqn.value, (label, 'queue count', n_o, nqn.value)
    assert [o.load(D_27C6C0 + 4 * i) for i in range(384)] == list(ntable), (label, 'D_0027C6C0')
    assert o.read(D_2819C0, 256) == bytes(nvol), (label, 'D_002819C0')
    assert s32(o.load(D_27F740 + 0x48)) == ncount.value, (label, 'D_0027F740+0x48')
    allowed = set(range(D_27C6C0, D_27C6C0 + 0x600)) | set(range(D_2819C0, D_2819C0 + 0x100)) | \
        set(range(D_27F740, D_27F740 + 0x50)) | set(range(D_27F7C0, D_27F7C0 + 0x2000))
    bad = sorted(a for a in o.stores if a not in allowed)
    assert not bad, (label, 'original store outside the model', hex(bad[0]))


def library_cases(elf, ram, native, rng):
    title_table = [u32(ram, D_27C6C0 + 4 * i) for i in range(384)]
    ssh = bytearray(0x40)
    struct.pack_into('<4I', ssh, 0, 0xBB0, 0x492D0, 0, 0x64685353)
    bad = bytearray(ssh)
    struct.pack_into('<I', bad, 0xC, 0x64685354)
    cases = []
    for fn, arglists in (
            ('00119400', [(0x20, 0xDE800, 0x1A0000, 0x492D0), (0x21, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF),
                          (0x3D, 0x12345678, 0x9ABCDEF0, 0x0FEDCBA9), (0, 0, 0, 0)]),
            ('001193A8', [(0xDE800, 0x1A0000, 0x492D0), (0x1FFFFF, 0x1FFFFF, 0xFFFFFF), (0, 0, 0)]),
            ('00119528', [(0x1400000, 0x1A0000), (0x1400000, 7), (0x1400000, 0xFFFFFFFF)]),
            ('001194B8', [(0xDE800, 0x1400000, 0x1A0000), (0x85B00, 0x1400000, 0x15047)]),
            ('00119450', [(0,), (2,), (-1,), (0x80000000,)]),
            ('001195A8', [(h,) for h in (0, 1, 3, 4, 0x7F, 0x80, 0xFFFFFFFF)]),
            ('001199F0', [(h, v) for h in (0, 4, 0x7F, 0x80, -1) for v in (0, 0x64, 0x7F, 0x80, -1)])):
        for args in arglists:
            for variant in range(3 if fn in ('00119528', '001194B8', '001195A8', '00119450') else 1):
                cases.append((fn, args, variant))
    sweep = []
    for k in range(pick(600, 60)):
        fn = rng.choice(list(LIB))
        sweep.append((fn, None, k))
    total = len(cases) + 600
    run = cases + sweep
    for fn, args, variant in run:
        if args is None:
            args = {'00119400': lambda: tuple(rng.getrandbits(32) for _ in range(4)),
                    '001193A8': lambda: tuple(rng.getrandbits(32) for _ in range(3)),
                    '00119528': lambda: (0x1400000, rng.getrandbits(32)),
                    '001194B8': lambda: (rng.getrandbits(21), 0x1400000, rng.getrandbits(24)),
                    '00119450': lambda: (rng.choice((0, 0, 2, -5)),),
                    '001195A8': lambda: (rng.choice((0, 1, 2, 3, 4, 5, 0x7F, 0x80, rng.getrandbits(32))),),
                    '001199F0': lambda: (rng.choice((0, 4, 0x7F, 0x80, -1)), rng.randrange(-3, 0x83))}[fn]()
        table = list(title_table)
        if variant == 1:   # a full table (no free entry among 0..0x7E)
            for i in range(128):
                table[3 * i] = 1 if i < 0x7F else 0
        elif variant == 2 or args is None:
            for i in range(128):
                table[3 * i] = rng.choice((0, 0, 1, 1, 2))
                table[3 * i + 1] = rng.getrandbits(32)
                table[3 * i + 2] = rng.getrandbits(32)
        vol = [rng.getrandbits(8) for _ in range(256)]
        count = rng.choice((5, 6, 0x7FFFFFFF, 0xFFFFFF, rng.getrandbits(31)))
        ack = rng.choice((count, count, count - 1, rng.getrandbits(31)))
        prefill = rng.choice((0, 0, 0, 254, 255)) if fn in ('00119400', '001193A8', '001194B8') else 0
        voices = [(rng.choice((0, 1, 1, 2)), rng.choice((0xFFFF, 0, 1, 3, 4, 0x7F))) for _ in range(48)]
        header = bytes(ssh if variant != 1 or rng.random() < 0.5 else bad)
        if fn in ('00119528', '001194B8') and rng.random() < 0.25:
            header = bytes(bad)
        library_case(elf, bytearray(RAM['title']['eeMemory.bin']), native, rng, fn, args, prefill,
                     voices, table, vol, count & MASK, ack & MASK, header)
    return len(run), total


# ------------------------------------------------------------ B. chain

class HostModel:
    """The oracle side's independent model of the port's stated host-speed
    rules: sceSifSetDma copies at once and returns ids 1, 2, ...;
    sceSifDmaStat answers -1 (done); 0010F8F8 / 0010F968 are first fit in
    0x100 units above the measured boot blocks; the exchange delivers the
    IOP's count one field after the field that ran the command
    (`ack_fields` = 2 is the stated rule; 1 and 3 are the mutants)."""

    def __init__(self, ack_fields=2):
        self.blocks = list(HEAP_BOOT)
        self.iop = bytearray(0x200000)
        self.spu = bytearray(0x200000)
        self.ids = 0
        self.ack_fields = ack_fields
        self.pending = []          # (the field whose exchange shows it, count)
        self.status112 = 5
        self.fields = 0

    def alloc(self, size):
        n = (size + 0xFF) & ~0xFF
        at = 0x85B00
        for a, s in sorted(self.blocks):
            if a + s <= at:
                continue
            if a >= at and a - at >= n:
                break
            at = a + s
        self.blocks.append((at, n))
        return at

    def free(self, at):
        self.blocks = [b for b in self.blocks if b[0] != at]

    def hooks(self, o, bank):
        def set_dchain(ee): ee.ret_int(0)
        def flush(ee): ee.ret_int(0)

        def set_dma(ee):
            desc, count = ee.r[4] & MASK, ee.r[5] & MASK
            src, dst, size, attr = (ee.load(desc + 4 * i) for i in range(4))
            assert count == 1 and attr == 0 and size % 16 == 0 and dst % 16 == 0
            self.iop[dst:dst + size] = ee.read(src, size)
            self.ids += 1
            ee.ret_int(self.ids)

        def dma_stat(ee):
            assert 1 <= (ee.r[4] & MASK) <= self.ids
            ee.ret_int(-1)   # done

        def alloc(ee): ee.ret_int(self.alloc(ee.r[4] & MASK))
        def free(ee): self.free(ee.r[4] & MASK); ee.ret_int(0)
        for a, f in zip(KERNEL, (set_dchain, flush, set_dma, dma_stat, alloc, free)):
            o.hooks[a] = f

    def field(self, o):
        """The top of a frame: the commands queued in the last frame run in
        this field; a command's count reaches the EE copy D_002817C0 +
        0x1C0 at the exchange `ack_fields` fields after the one that ran
        it (2: the next field's exchange, the stated rule)."""
        self.fields += 1
        idx, count = o.load(D_27F740 + 0x3C), o.load(D_27F740 + 0x40)
        base = D_27F7C0 + (idx << 12)
        for k in range(count):
            w = [o.load(base + 16 * k + 4 * j) for j in range(4)]
            assert w[0] == 0x20, ('a command outside the bank upload', hex(w[0]))
            iop = (w[1] & 0xFF) << 16 | w[2] >> 16
            spu = (w[2] & 0xFFFF) << 8 | w[3] >> 24
            size = w[3] & 0xFFFFFF
            self.spu[spu:spu + size] = self.iop[iop:iop + size]
            self.pending.append((self.fields + self.ack_fields - 1, (w[1] & 0xFFFFFF00) >> 8))
        o.save(D_27F740 + 0x40, 0)
        for at, c in self.pending:
            if at <= self.fields:
                self.status112 = c
        self.pending = [(at, c) for at, c in self.pending if at > self.fields]
        o.save(D_2817C0 + 0x1C0, self.status112)
        return count


def run_chain(elf, native, bank, ack_fields=2, compare=True):
    o = SoundEE(elf, bytearray(RAM['title']['eeMemory.bin']))
    o.write(BANK_AT, bank)
    model = HostModel(ack_fields)
    model.hooks(o, bank)
    if compare:
        assert native.lib.nb_init((C.c_int32 * 5)(*BASES)) == 0
        assert native.dump() == oracle_dump(o), ('seeds differ from the title capture',
                                                 first_diff(native.dump(), oracle_dump(o)))
    buf = (C.c_uint8 * len(bank)).from_buffer_copy(bank)
    calls, result = 0, 0
    while result == 0:
        calls += 1
        assert calls < 200, 'the upload did not finish'
        queued = model.field(o)
        o.r[29] = STACK
        o.call(0x1FB370, (BANK_AT,))
        result = o.r[2] & MASK
        if compare:
            assert native.lib.nb_field() == 0
            nres = C.c_uint32(0)
            assert native.lib.nb_call(BANK_AT, buf, len(bank), C.byref(nres)) == 0, \
                ('native fault', native.lib.nb_fault(C.byref(C.c_uint32())))
            assert nres.value == result, (calls, hex(nres.value), hex(result))
            n, o_dump = native.dump(), oracle_dump(o)
            # The kernel's DMA id: both sides count their own from 1.
            assert n == o_dump, (calls, first_diff(n, o_dump))
            q = (C.c_uint32 * 1024)()
            nq = native.lib.nb_queue(q)
            idx, count = o.load(D_27F740 + 0x3C), o.load(D_27F740 + 0x40)
            oq = [o.load(D_27F7C0 + (idx << 12) + 4 * k) for k in range(4 * count)]
            assert list(q[:4 * nq]) == oq, (calls, 'queued commands', list(q[:4 * nq]), oq)
            del queued
    return o, model, calls, result


def check_chain(elf, native, bank):
    o, model, calls, result = run_chain(elf, native, bank)
    assert result == BANK_END, hex(result)
    route = RAM['route00']
    ee = route['eeMemory.bin']
    # C. The end state against route 00.
    r00 = SoundEE(elf, bytearray(ee))
    n = native.dump()
    ref = oracle_dump(r00)
    skipped = {'D_002821A0'} | {f'D_002819C0 word {i}' for i in range(64)}
    for name, a, b in zip(DUMP_NAMES, n, ref):
        if name in skipped:
            continue
        assert a & MASK == b & MASK, ('route 00', name, hex(a & MASK), hex(b & MASK))
    # The IOP heap: the bank's block is free again (first fit gave 0xDE800).
    assert native.lib.nb_heap_next() == 0xDE800, hex(native.lib.nb_heap_next())
    # SPU and IOP RAM against route 00's.
    size = u32(bank, 0x20)
    spu_n, iop_n = (C.c_uint8 * size)(), (C.c_uint8 * size)()
    native.lib.nb_ram(0x1A0000, 0xDE800, size, spu_n, iop_n)
    spu00, iop00 = route['SPU2.bin'], route['iopMemory.bin']
    assert bytes(spu_n) == spu00[0x10004 + 0x1A0000:0x10004 + 0x1A0000 + size], 'SPU RAM differs from route 00'
    assert bytes(iop_n) == iop00[0xDE800:0xDE800 + size], 'IOP RAM differs from route 00'
    assert bytes(model.spu[0x1A0000:0x1A0000 + size]) == bytes(spu_n), 'the oracle model\'s SPU RAM'
    # The title capture did not hold the bank there (the load put it there).
    assert RAM['title']['SPU2.bin'][0x10004 + 0x1A0000:0x10004 + 0x1A0000 + size] != bytes(spu_n)
    return calls, size, n[DUMP_NAMES.index('D_00275B18')]


def check_mutants(elf, native, bank, wait):
    out = []
    for fields in (1, 3):
        o, _m, calls, _r = run_chain(elf, native, bank, ack_fields=fields, compare=False)
        b18 = o.load(0x275B18)
        assert b18 != wait, ('the exchange mutant', fields, 'leaves the captured D_00275B18', hex(b18))
        out.append((fields, calls, b18))
    return out


RAM = {}


def main():
    elf = read_elf()
    RAM['title'] = extract(TITLE_STATE, 'title_slot01')
    RAM['route00'] = extract(ROUTE00, 'route00')
    for name, ram in RAM.items():
        lo, hi = 0x100000, 0x240000
        assert ram['eeMemory.bin'][lo:hi] == elf[0x300:0x300 + hi - lo], (name, 'captured code differs')
    native = Native()
    rng = random.Random(0x1FB370)
    bank = area11_bank()
    ran, total = library_cases(elf, bytearray(RAM['title']['eeMemory.bin']), native, rng)
    calls, size, wait = check_chain(elf, native, bank)
    assert wait == 0x4E == u32(RAM['route00']['eeMemory.bin'], 0x275B18), hex(wait)
    mutants = check_mutants(elf, native, bank, wait)
    banner(part(ran, total, 'library cases'),
           f'the chain: {calls} 001FB370 calls on the AREA11 bank (0x{size:X} bytes uploaded), every modelled '
           'byte equal to the original after each; seeds equal the title capture; the end state equal to '
           'route 00 (SPU RAM at 0x1A0000 and IOP RAM at 0xDE800 included)',
           'exchange mutants refuted by D_00275B18: ' +
           ', '.join(f'ack after {f} field(s) -> {c} calls, 0x{b:X}' for f, c, b in mutants))
    print('test_sound_bank_reference: PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
