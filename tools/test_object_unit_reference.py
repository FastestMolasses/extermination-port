#!/usr/bin/env python3
"""The object-unit path (src/game/em_object_unit.c: em_object_unit_parse +
em_object_unit_run, the CPU half of em_gfx_object_unit) against the ORIGINAL
VU1 microcode, over every captured AREA11 owner draw.

For every owner with draw method 001CAA00 and a bank model in the s87 route
beats 00..14 (the owner-draw oracle's set, docs/OWNER_DRAW.md section 5 D):

1. The ORIGINAL 001CAA00 runs over the captured RAM and scratchpad (the EE
   interpreter of tools/test_owner_draw_reference.py) and writes its DMA
   unit.
2. Oracle: the unit's DMA tags go through DMAC and VIF1 exactly as the
   hardware takes them (CNT payloads inline, REF targets from the captured
   RAM, the CALLed kernel packets' MPG blocks from the pinned ELF, STCYCL,
   BASE / OFFSET / TOPS, UNPACK V4-32, MSCAL / MSCNT) onto one VU1 data
   memory. The object kernel's batches run on the object-kernel test's
   interpreter (VU0 lane rules), the clip program's on the clip test's
   interpreter; each executes the ORIGINAL instructions. Every XGKICK is
   decoded here, independently: object-kernel packets as PRE + PACKED
   TEX0/ST/RGBAQ/XYZF2 strips (vertex i >= 2 without ADC draws i-2, i-1, i
   with its TEX0), clip packets as the TEX0_1 writes and PACKED triangle
   lists.
3. Native: em_object_unit_parse over the same unit bytes (REF targets
   resolved from the same RAM), then em_object_unit_run.
4. The two triangle lists must be equal: count, order, pass, block, TEX0
   (all 64 bits), and per vertex X, Y, Z, F, R, G, B, A, S, T, Q.

Also: the parsed pieces equal the unit's own uploads (colour, nodes, skin
records, model REF), and every refusal path of the parser fires on a unit
mutated to break exactly its rule.

F. The face units (001CB3C0's, CALL 0x0023C480: Roger's and Dennis's faces,
   docs/VU1_FACE_MORPH.md): every distinct intact face unit in the display
   lists of the route beats 00..14 (the colour, node and weights CNTs, the
   GS state, the arena, the CALL, the skin record, the face blocks), run the
   same way: the ORIGINAL face program on the face test's interpreter against
   em_object_unit_parse + em_object_unit_run, every triangle equal. The
   default run takes one drawing Roger unit and one Dennis unit when
   present; EM_TEST_FULL=1 all.

No original instruction bytes, disassembly or data are written by this
file; the report holds counts, ids and hashes only.
"""
import ctypes as C
import hashlib
import json
import struct
import subprocess
import sys
import time
from pathlib import Path

import test_owner_draw_reference as tod
import test_vu1_face_morph_reference as vf
import test_vu1_object_clip_reference as vc
import test_vu1_object_kernel_reference as vk
from reference_mode import FULL, banner, part, select, parallel_map
from test_shadow_original_reference import VU1

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
OUT = ROOT / 'build/object_unit_reference'
ELF_SHA = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
KERNEL, CLIP_KERNEL, FACE_KERNEL = 0x23C750, 0x2354A0, 0x23C480
FACES = {0x018C8740: 'Roger', 0x011749C0: 'Dennis'}     # face resources (VU1_FACE_MORPH.md)
ARENA = (0x28F700, 0x7635C0)
MAX_TRIS = 20000


def u32(b, a): return struct.unpack_from('<I', b, a)[0]


SHIM = r'''
#include <string.h>
#include "game/em_object_unit.h"
static const unsigned char *g_ram;
static unsigned g_size;
static const uint8_t *resolve(void *ctx, uint32_t a, uint32_t n)
{ (void)ctx; if (a > g_size || n > g_size - a) return NULL; return g_ram + a; }
int shim_gs(const unsigned char *bytes, const char **why) { return em_object_unit_gs_state_check(bytes, why); }
int shim_run(const unsigned char *ram, unsigned ram_size, const unsigned char *unit, unsigned size,
             EmObjectUnitTriangle *out, unsigned cap, unsigned *object_tris, unsigned *pieces,
             const char **why)
{
    static EmObjectUnitPieces p;
    static EmObjectUnitResult r;
    g_ram = ram; g_size = ram_size;
    if (em_object_unit_parse(unit, size, resolve, NULL, &p, why)) return -1;
    pieces[0] = p.unit.node_count; pieces[1] = p.unit.block_count; pieces[2] = p.unit.clip;
    pieces[3] = p.model_address; pieces[4] = p.skin_address[0]; pieces[5] = p.skin_address[1];
    pieces[6] = p.fog_off; pieces[7] = p.bytes;
    memcpy(pieces + 8, p.color, 64); memcpy(pieces + 24, p.constants, 112);
    memcpy(pieces + 52, p.clip_constants, 112);
    memcpy(pieces + 80, p.nodes, 128u * p.unit.node_count);
    if (em_object_unit_run(&p.unit, &r)) { *why = r.why; return -2; }
    if (r.count > cap) { *why = "shim capacity"; return -3; }
    memcpy(out, r.tri, sizeof *out * r.count);
    *object_tris = r.object_triangles;
    return (int)r.count;
}
'''


class Vertex(C.Structure):
    _fields_ = [('x', C.c_uint16), ('y', C.c_uint16), ('z', C.c_uint32), ('f', C.c_uint8),
                ('rgba', C.c_uint8 * 4), ('s', C.c_uint32), ('t', C.c_uint32), ('q', C.c_uint32)]


class Triangle(C.Structure):
    _fields_ = [('tex0', C.c_uint64), ('pass_', C.c_uint16), ('block', C.c_uint16), ('v', Vertex * 3)]


LIB = ELF = None
CAP = {}


def build_library():
    OUT.mkdir(parents=True, exist_ok=True)
    src, so = OUT / 'shim.c', OUT / 'shim.so'
    src.write_text(SHIM)
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror',
                    '-ffp-contract=off', f'-I{ROOT / "src"}', str(src), str(ROOT / 'src/game/em_object_unit.c'),
                    '-o', str(so)], check=True)
    lib = C.CDLL(str(so))
    lib.shim_run.restype = C.c_int
    return lib


# ------------------------------------------------------------ the oracle

def decode_strip(raw, block):
    """An object-kernel packet: PRE + PACKED, REGS TEX0 ST RGBAQ XYZF2."""
    lo, hi = struct.unpack_from('<QQ', raw, 0)
    nloop, pre, prim, flg, nreg = lo & 0x7FFF, lo >> 46 & 1, lo >> 47 & 0x7FF, lo >> 58 & 3, lo >> 60
    assert pre and flg == 0 and nreg == 4 and hi == 0x4126 and prim == 0x03C, ('object packet', hex(lo), hex(hi))
    verts = []
    for i in range(nloop):
        d = [struct.unpack_from('<4I', raw, 16 + 64 * i + 16 * k) for k in range(4)]
        verts.append(dict(tex0=d[0][0] | d[0][1] << 32, s=d[1][0], t=d[1][1], q=d[1][2],
                          rgba=tuple(w & 0xFF for w in d[2]), x=d[3][0] & 0xFFFF, y=d[3][1] & 0xFFFF,
                          z=d[3][2] >> 4 & 0xFFFFFF, f=d[3][3] >> 4 & 0xFF, adc=d[3][3] >> 15 & 1))
    return [(verts[i]['tex0'], 0, block, tuple(vkey(verts[i - 2 + c]) for c in range(3)))
            for i in range(2, nloop) if not verts[i]['adc']]


def vkey(v): return (v['x'], v['y'], v['z'], v['f'], v['rgba'], v['s'], v['t'], v['q'])


class ClipDecoder:
    """The clip program's kicks: TEX0_1 writes (dmem 1018) and PACKED
    triangle lists with ST (Q from ST.z), RGBAQ, XYZF2 (ADC clear)."""

    def __init__(self):
        self.tex0 = None

    def feed(self, raw, block):
        out, q = [], 0
        while q < len(raw):
            lo, hi = struct.unpack_from('<QQ', raw, q); q += 16
            nloop, eop, pre, prim, flg, nreg = (lo & 0x7FFF, lo >> 15 & 1, lo >> 46 & 1, lo >> 47 & 0x7FF,
                                                lo >> 58 & 3, (lo >> 60) or 16)
            assert flg == 0
            regs = [(hi >> (4 * r)) & 15 for r in range(nreg)]
            tri, st = [], None
            for _ in range(nloop):
                for r in regs:
                    w = struct.unpack_from('<4I', raw, q); q += 16
                    if r == 6:
                        self.tex0 = w[0] | w[1] << 32
                    elif r == 2:
                        st = w[:3]
                    elif r == 1:
                        rgba = tuple(x & 0xFF for x in w)
                    elif r == 4:
                        assert pre and prim == 0x03B and not (w[3] >> 15) & 1, ('clip packet', hex(lo))
                        tri.append((w[0] & 0xFFFF, w[1] & 0xFFFF, w[2] >> 4 & 0xFFFFFF, w[3] >> 4 & 0xFF,
                                    rgba, st[0], st[1], st[2]))
                        if len(tri) == 3:
                            out.append((self.tex0, 1, block, tuple(tri)))
                            tri = []
                    else:
                        raise AssertionError(('clip register', r))
            assert not tri
            if eop:
                break
        return out


def oracle(ram, at, used):
    """The unit's tags through DMAC/VIF1 onto one VU1; the triangles its
    object-kernel and clip-program kicks draw, in GS order."""
    mem = bytearray(16384)
    vus = {KERNEL: vk.Oracle(ELF), CLIP_KERNEL: VU1(ELF), FACE_KERNEL: vf.FaceOracle(ELF)}
    for k, vu in vus.items():
        vc.load_kernel(vu, k)
        vu.mem = mem
    st = dict(base=0, offset=0, tops=0, dbf=0, cl=1, wl=1)
    kernel, blocks, tris, clip = None, {KERNEL: 0, CLIP_KERNEL: 0, FACE_KERNEL: 0}, [], ClipDecoder()

    def vif(src, i, e):
        nonlocal tris
        while i < e:
            v = u32(src, i)
            cmd, imm, num = (v >> 24) & 0x7F, v & 0xFFFF, (v >> 16) & 0xFF
            if cmd >= 0x60:
                assert cmd == 0x6C and st['wl'] <= st['cl'], ('unpack', hex(v))
                cnt = num or 256
                dst = (imm & 0x3FF) + (st['tops'] if imm & 0x8000 else 0)
                for k in range(cnt):
                    d = (dst + (k // st['wl']) * st['cl'] + k % st['wl']) & 1023
                    mem[d * 16:d * 16 + 16] = src[i + 4 + 16 * k:i + 20 + 16 * k]
                i += 4 + 16 * cnt
                continue
            if cmd in (0x50, 0x51):                 # DIRECT: GIF state, not VU memory
                i = (i + 4 + 15) // 16 * 16 + 16 * imm
                continue
            if cmd in (0x14, 0x15, 0x17):
                top = st['tops']
                st['dbf'] ^= 1
                st['tops'] = st['base'] + (st['offset'] if st['dbf'] else 0)
                vu = vus[kernel]
                vu.top, vu.kicks, vu.events, vu.watch = top, [], [], set()
                vu.mem = mem
                vu.run(vu.resume if cmd == 0x17 else 8 * imm)
                block = blocks[kernel]
                blocks[kernel] += 1
                for ev in vu.events:
                    if ev[0] != 'kick':
                        continue
                    raw = ev[3]                     # the packet as the XGKICK snapshotted it
                    tris += clip.feed(raw, block) if kernel == CLIP_KERNEL else decode_strip(raw, block)
                i += 4
                continue
            if cmd == 0x01: st['cl'], st['wl'] = imm & 0xFF, (imm >> 8) & 0xFF
            elif cmd == 0x03: st['base'] = imm & 0x3FF
            elif cmd == 0x02: st['offset'], st['dbf'], st['tops'] = imm & 0x3FF, 0, st['base']
            elif cmd == 0x20: i += 4
            else: assert cmd in (0x00, 0x10, 0x11, 0x13), ('vif', hex(v))
            i += 4

    for a, tid, qwc, addr in vc.unit_tags(ram, at, at + used):
        if tid == 5:
            kernel = addr
            st.update(vc.load_kernel(vus[addr], addr)[0])
            st['dbf'], st['tops'] = 0, st['base']
        elif tid == 1:
            vif(ram, a + 16, a + 16 + 16 * qwc)
        elif tid == 3:
            vif(ram, addr, addr + 16 * qwc)
        else:
            raise AssertionError(('tag', tid))
    return tris, blocks


# ------------------------------------------------------------ one owner

def native(ram, unit):
    out = (Triangle * MAX_TRIS)()
    objs, pieces = C.c_uint(0), (C.c_uint32 * (80 + 32 * 54))()
    why = C.c_char_p()
    n = LIB.shim_run(ram, len(ram), unit, len(unit), out, MAX_TRIS, C.byref(objs), pieces, C.byref(why))
    return n, out, objs.value, pieces, (why.value.decode() if why.value else None)


def ntris(out, n):
    return [(t.tex0, t.pass_, t.block, tuple((v.x, v.y, v.z, v.f, tuple(v.rgba), v.s, v.t, v.q) for v in t.v))
            for t in out[:n]]


def case(item):
    name, owner = item
    ram, spr = CAP[name]
    o, ctx = tod.original_draw(ram, spr, owner)
    used = o.load(ctx + 0x10) - tod.CAP_DL
    res = dict(capture=name, owner=hex(owner), behaviour=hex(u32(ram, owner + 0x10)), unit=used)
    if not used:
        return res
    unit = o.read(tod.CAP_DL, used)
    img = bytearray(ram)
    img[tod.CAP_DL:tod.CAP_DL + used] = unit
    img = bytes(img)
    want, blocks = oracle(img, tod.CAP_DL, used)
    n, out, objs, pieces, why = native(img, unit)
    assert n >= 0, ('native refused', name, hex(owner), n, why)
    got = ntris(out, n)
    if got != want:
        k = next((i for i, (a, b) in enumerate(zip(got, want)) if a != b), min(len(got), len(want)))
        raise AssertionError(('triangles differ', name, hex(owner), len(got), len(want), k,
                              got[k] if k < len(got) else None, want[k] if k < len(want) else None))
    assert objs == sum(1 for t in want if t[1] == 0)
    # the pieces are the unit's own uploads
    tags = vc.unit_tags(img, tod.CAP_DL, tod.CAP_DL + used)
    model = u32(img, owner + 0x44)
    nodes = pieces[0]
    assert pieces[3] == model + 0x40 and pieces[1] == blocks[KERNEL] and pieces[7] == used
    assert list(pieces[8:24]) == list(struct.unpack_from('<16I', unit, 0x20)), 'colour'
    assert list(pieces[80:80 + 32 * nodes]) == list(struct.unpack_from(f'<{32 * nodes}I', unit, 0x80)), 'nodes'
    assert list(pieces[24:52]) == list(struct.unpack_from('<28I', img, pieces[4] + 16)), 'skin record 0'
    assert pieces[2] == (blocks[CLIP_KERNEL] > 0)
    if pieces[2]:
        assert list(pieces[52:80]) == list(struct.unpack_from('<28I', img, pieces[5] + 16)), 'skin record 1'
    assert [t[1] for t in tags].count(5) == 1 + pieces[2]
    res.update(nodes=nodes, blocks=pieces[1], clip=bool(pieces[2]), triangles=len(want),
               object_triangles=objs, clip_triangles=len(want) - objs,
               sha=hashlib.sha256(repr(want).encode()).hexdigest()[:16], _unit=unit)
    return res


# ------------------------------------------------------------ F. faces

def face_units():
    """(capture, start, end) of every distinct intact face unit: a CALL
    0x0023C480 tag with the face unit's tags before and after it."""
    out, seen = [], set()
    needle = struct.pack('<I', FACE_KERNEL)
    for beat, (ram, _spr) in CAP.items():
        at = ARENA[0]
        while True:
            i = ram.find(needle, at, ARENA[1])
            if i < 0: break
            at = i + 1
            p = i - 4
            if p % 16 or (u32(ram, p) >> 28) & 7 != 5 or u32(ram, p) & 0xFFFF: continue
            start, end = p - 0x160, p + 0x30
            shape = [(u32(ram, a) >> 28 & 7, u32(ram, a) & 0xFFFF) for a in
                     (start, start + 0x60, start + 0x100, start + 0x140, start + 0x150, p + 0x10, p + 0x20)]
            if shape[:6] != [(1, 5), (1, 9), (1, 3), (3, 9), (3, 1), (3, 8)] or shape[6][0] != 3: continue
            if u32(ram, p + 0x24) - 0x40 not in FACES: continue
            key = hashlib.sha256(ram[start:end]).hexdigest()
            if key in seen: continue
            seen.add(key)
            out.append((beat, start, end))
    return out


def face_case(item):
    beat, start, end = item
    ram = CAP[beat][0]
    want, blocks = oracle(ram, start, end - start)
    unit = ram[start:end]
    n, out, objs, pieces, why = native(ram, unit)
    assert n >= 0, ('native refused a face unit', beat, hex(start), n, why)
    got = ntris(out, n)
    if got != want:
        k = next((i for i, (a, b) in enumerate(zip(got, want)) if a != b), min(len(got), len(want)))
        raise AssertionError(('face triangles differ', beat, hex(start), len(got), len(want), k))
    face = u32(ram, end - 0x10 + 4) - 0x40
    assert pieces[3] == face + 0x40 and pieces[1] == blocks[FACE_KERNEL] and pieces[0] == 1 and not pieces[2]
    assert list(pieces[8:24]) == list(struct.unpack_from('<16I', unit, 0x20)), 'face colour'
    assert list(pieces[80:112]) == list(struct.unpack_from('<32I', unit, 0x80)), 'face node'
    assert list(pieces[24:52]) == list(struct.unpack_from('<28I', ram, pieces[4] + 16)), 'face skin record'
    return dict(capture=beat, unit=hex(start), face=FACES[face], blocks=pieces[1], triangles=len(want),
                sha=hashlib.sha256(repr(want).encode()).hexdigest()[:16])


# ------------------------------------------------------------ refusals

def refusals(name, owner, unit):
    """Each parser rule broken alone must be refused with its reason."""
    ram = CAP[name][0]
    img = bytearray(ram)
    img[tod.CAP_DL:tod.CAP_DL + len(unit)] = unit
    tags = vc.unit_tags(bytes(img), tod.CAP_DL, tod.CAP_DL + len(unit))
    rel = [(a - tod.CAP_DL, tid, qwc, addr) for a, tid, qwc, addr in tags]
    gs = next(addr for _a, tid, qwc, addr in rel if tid == 3 and qwc == 9)
    skin = next(addr for _a, tid, qwc, addr in rel if tid == 3 and qwc == 8)
    cases = []

    def mutate(label, reason, unit_edit=None, ram_edit=None):
        u, r = bytearray(unit), bytearray(img)
        if unit_edit: unit_edit(u)
        if ram_edit: ram_edit(r)
        r[tod.CAP_DL:tod.CAP_DL + len(u)] = u
        n, _o, _objs, _p, why = native(bytes(r), bytes(u))
        assert n == -1 and why and reason in why, (label, n, why)
        cases.append(label)

    def put(buf, off, v): struct.pack_into('<I', buf, off, v)
    mutate('colour unpack', 'colour CNT', lambda u: put(u, 0x1C, 0x6C0403F6))
    mutate('node order', 'node CNT', lambda u: put(u, 0x7C, u32(u, 0x7C) | 1))
    mutate('gs address', 'set 1 class 0', lambda u: put(u, next(a for a, t, q, _x in rel if t == 3 and q == 9) + 4,
                                                        0x815360 + 0x90))
    mutate('arena address', 'arena REF', lambda u: put(u, next(a for a, t, q, _x in rel if t == 3 and q == 1) + 4,
                                                        0x814230))
    # the GS state bytes themselves (checked in every capture by main())
    for label, off, word, reason in (('gs TEST', 0x40, 0x5000F, 'class-0 set'), ('gs ZMSK', 0x54, 1, 'class-0 set'),
                                     ('gs tag', 0x10, 0x8006, 'A+D'), ('gs DIRECT', 0x0C, 0x50000009, 'DIRECT 8')):
        b = bytearray(img[gs:gs + 144])
        put(b, off, word)
        why = C.c_char_p()
        assert LIB.shim_gs(bytes(b), C.byref(why)) == -1 and reason in why.value.decode(), (label, why.value)
        cases.append(label)
    mutate('skin codes', 'skin record', ram_edit=lambda r: put(r, skin + 12, 0x6C0703FA))
    mutate('call target', 'CALL', lambda u: put(u, next(a for a, t, _q, _x in rel if t == 5) + 4, 0x23C480))
    mutate('truncated', 'ends inside', lambda u: u.__delitem__(slice(len(u) - 16, None)))
    mutate('tag id', 'other than CNT', lambda u: put(u, rel[-1][0], (u32(u, rel[-1][0]) & 0x8FFFFFFF) | 0x70000000))
    mutate('model qwc', '0x82-qword', lambda u: put(u, rel[-1][0], u32(u, rel[-1][0]) - 1))
    return cases


# ------------------------------------------------------------ main

def main():
    global LIB, ELF
    started = time.time()
    ELF = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(ELF).hexdigest() == ELF_SHA, 'not the pinned SCUS-97112 ELF'
    tod.ELF = vc.ELF = ELF
    LIB = build_library()
    base = DECOMP / 'build/s87/route'
    for beat in tod.BEATS:
        p = base / beat
        if (p / 'eeMemory.bin').exists() and (p / 'scratchpad.bin').exists():
            ram = (p / 'eeMemory.bin').read_bytes()
            assert ram[0x810700] == 11, (beat, 'not AREA11')
            CAP[beat] = (ram, (p / 'scratchpad.bin').read_bytes())
    assert CAP, 'no s87 route captures'
    for beat, (ram, _spr) in CAP.items():
        why = C.c_char_p()
        # 001D1F80(0, 1, 0)'s REF target and the arena qword, as 001D0F20 built them at boot
        assert LIB.shim_gs(ram[0x815360:0x815360 + 144], C.byref(why)) == 0, (beat, why.value)
        assert struct.unpack_from('<4I', ram, 0x814220) == (0, 0, 0, 0x11000000), (beat, 'arena qword')
        assert u32(ram, 0x275674) == 0x814220, (beat, 'D_00275674')
    tod.CAP.update(CAP)
    tod.LIB = tod.build_library()
    tod.load_bank()
    owners = [o for beat in CAP for o in tod.capture_owners(beat)]
    classes = dict(zip(owners, parallel_map(tod.flag_class, owners)))

    def behaviour(i): return u32(CAP[i[0]][0], i[1] + 0x10)
    chosen = select(owners, 40, 0x0B7E, axes=(behaviour, lambda i: (behaviour(i), classes[i])),
                    keep=lambda _i, it: classes[it] == 'clip')
    results = parallel_map(case, chosen, cost=lambda it: 3 if classes[it] == 'clip' else 1)
    drawn = [r for r in results if r['unit']]
    for it, r in zip(chosen, results):
        want = 'culled' if not r['unit'] else ('clip' if r.get('clip') else 'plain')
        assert want == classes[it], ('001CA7B0 class and parsed unit disagree', r['capture'], r['owner'])
    assert any(r['clip'] for r in drawn) and any(not r['clip'] for r in drawn), 'both passes'
    faces = face_units()
    assert any(FACES[u32(CAP[b][0], e - 0x10 + 4) - 0x40] == 'Roger' for b, _s, e in faces), 'no Roger face unit'

    def face_of(it): return FACES[u32(CAP[it[0]][0], it[2] - 0x10 + 4) - 0x40]
    # quick: one Roger unit that draws (beats 08..14) and one Dennis unit when present
    picks = [next((f for f in faces if face_of(f) == 'Roger' and f[0][:2] >= '08'), faces[0])]
    picks += [f for f in faces if face_of(f) == 'Dennis'][:1]
    face_results = parallel_map(face_case, faces if FULL else picks)
    sample = next(r for r in drawn if r['clip'])
    refused = refusals(sample['capture'], int(sample['owner'], 16), sample['_unit'])
    tally = {}
    for r in drawn:
        t = tally.setdefault(r['behaviour'], {'units': 0, 'clip_units': 0, 'triangles': 0, 'clip_triangles': 0})
        t['units'] += 1
        t['clip_units'] += r['clip']
        t['triangles'] += r['triangles']
        t['clip_triangles'] += r['clip_triangles']
    line = banner(part(len(chosen), len(owners), 'captured owner draws'),
                  f"{len(drawn)} units ({sum(r['clip'] for r in drawn)} clip), "
                  f"{sum(r['triangles'] for r in drawn)} triangles equal the original microcode's",
                  f'{len(refused)} parser refusals',
                  part(len(face_results), len(faces), 'face units') +
                  f" ({sum(r['triangles'] for r in face_results)} triangles)")
    for r in results: r.pop('_unit', None)
    report = dict(status='PASS', mode=line, elf_sha256=ELF_SHA, by_behaviour=tally, refusals=refused,
                  faces=face_results,
                  seconds=round(time.time() - started, 1))
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'report.json').write_text(json.dumps(dict(report, units=results), indent=1) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    sys.exit(main())
