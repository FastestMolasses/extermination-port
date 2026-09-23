#!/usr/bin/env python3
"""Check the exported AREA11 level materials against the original GS state.

FIRST_LEVEL_AUDIT R10/R25 (docs/LEVEL_MATERIALS.md). The original sets the
GS state of a level triangle outside the level records: the records carry
only TEX0, the level kernel copies its GIF tag (PRIM) from VU1 dmem 0x3FC,
and the chain REFs an env packet with TEST_1/ALPHA_1/TEX1_1/CLAMP_1 before
every unit. This test rebuilds that state from the ORIGINAL captures:

1. It walks both display lists of every AREA11 EE capture as the DMAC,
   VIF1 and GIF would (DMA tags, VIF codes, PACKED/A+D GIF writes), and
   records the GS registers and the uploaded kernel templates in force at
   every MSCAL/MSCNT kick.
2. For every level-kernel kick (0x00237180 and its clip kernel 0x00239C90)
   it reads the TEX0 of every record in the kicked block, so each captured
   texture key gets the exact registers it was drawn with.
3. It rebuilds the texture order of each zone EMDL from its chunk15 source
   (independently of the exporter) and requires every exported record
   (<zone>.gsmat.json) and every packed code in the EMDL tex entries to
   equal those captured registers exactly.
4. It checks the kernel instructions that copy the template, the env
   packet address arithmetic, and the object-kernel (actor) draws that the
   Metal backend gives the same class-0 state by default.
5. It compiles the em_gfx.h/em_model.h field macros and checks they decode
   the codes the same way.

No original data is embedded: every value is read from the user's pinned
ELF, captures and generated assets. Everything runs in about a second, so
quick and full mode are the same run.
"""
from pathlib import Path
import argparse, hashlib, json, os, struct, subprocess, sys, tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from reference_mode import MODE

ROOT = Path(__file__).resolve().parents[1]
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
CAPTURES = ('playable_ee.bin', 'opening_ee.bin', 'handoff_ee.bin',
            'roger-encounter/eeMemory.bin', 'elevator/clip47_ee.bin',
            'elevator/completed_ee.bin')
LIST_HEADS = (0x28F700, 0x293700)      # the two display-list buffers
LEVEL_KERNEL, CLIP_KERNEL = 0x237180, 0x239C90
OBJECT_KERNELS = (0x23C750, 0x23C480)
ZONES = (('00_zone_main', 'f12_id44.bin'), ('01_zone_e1', 'f13_id50.bin'),
         ('02_zone_e2', 'f14_id5a.bin'), ('03_zone_e3', 'f15_id47.bin'),
         ('04_zone_e4', 'f16_id88.bin'), ('05_movables', 'f17_id93.bin'))
GS = dict(PRIM=0x00, TEX0_1=0x06, CLAMP_1=0x08, TEX1_1=0x14, ALPHA_1=0x42,
          COLCLAMP=0x46, TEST_1=0x47, PABE=0x49, FBA_1=0x4A, ZBUF_1=0x4E,
          PRMODECONT=0x1A, DTHE=0x45)
TEX0_KEY_MASK = ~(7 << 61) & (2**64 - 1)   # CLD is per-draw cache control
FLAG_VCOLOR, FLAG_GSMAT = 1, 8


def fail(msg):
    raise SystemExit('FAIL: ' + msg)


# --- DMA / VIF1 / GIF walk --------------------------------------------------

def dma_walk(ram, start, limit=500000):
    """(tag address, id, qwc, addr, data address) in DMAC source-chain order
    (TTE off: the tag itself is not transferred)."""
    a, stack = start, []
    for _ in range(limit):
        w0, addr = struct.unpack_from('<2I', ram, a)
        tid, qwc = (w0 >> 28) & 7, w0 & 0xFFFF
        if tid == 0:
            yield a, tid, qwc, addr, addr
            return
        if tid == 1:
            yield a, tid, qwc, addr, a + 16; a += 16 * (qwc + 1)
        elif tid == 2:
            yield a, tid, qwc, addr, a + 16; a = addr
        elif tid in (3, 4):
            yield a, tid, qwc, addr, addr; a += 16
        elif tid == 5:
            yield a, tid, qwc, addr, a + 16
            if len(stack) >= 2: fail(f'CALL nesting at {a:#x}')
            stack.append(a + 16 * (qwc + 1)); a = addr
        elif tid == 6:
            yield a, tid, qwc, addr, a + 16
            if not stack: return
            a = stack.pop()
        else:
            yield a, tid, qwc, addr, a + 16
            return
    fail(f'display list at {start:#x} does not end')


class Frame:
    """One display list replayed: GS register writes (with the address that
    wrote them) and a snapshot at every VU1 kick."""

    def __init__(self, ram, head):
        self.ram, self.gs, self.src_of, self.tpl = ram, {}, {}, {}
        self.kicks, self.cl, self.wl = [], 4, 4
        stream, self.segs, kernel = bytearray(), [], None
        for tag, tid, qwc, addr, data in dma_walk(ram, head):
            if tid == 5: kernel = addr
            if qwc:
                self.segs.append((len(stream), data, kernel, tag, tid, addr, qwc))
                stream += ram[data:data + 16 * qwc]
        self.stream, self.cursor = bytes(stream), 0
        self.run()

    def where(self, off):
        """(segment, EE address) of stream offset `off`. Offsets only grow
        during the walk, so a forward cursor replaces a search."""
        segs, k = self.segs, self.cursor
        if segs[k][0] > off: k = 0
        while k + 1 < len(segs) and segs[k + 1][0] <= off: k += 1
        self.cursor = k
        return segs[k], segs[k][1] + off - segs[k][0]

    def write(self, reg, value, address):
        self.gs[reg] = value; self.src_of[reg] = address

    def gif(self, pos, end):
        s = self.stream
        while pos + 16 <= end:
            lo, hi = struct.unpack_from('<QQ', s, pos)
            tag_at = self.where(pos)[1]; pos += 16
            nloop, flg, nreg = lo & 0x7FFF, (lo >> 58) & 3, (lo >> 60) or 16
            if (lo >> 46) & 1: self.write(GS['PRIM'], (lo >> 47) & 0x7FF, tag_at)
            regs = [(hi >> (4 * i)) & 15 for i in range(nreg)]
            if flg == 0:
                for _ in range(nloop):
                    for r in regs:
                        d0, d1 = struct.unpack_from('<QQ', s, pos)
                        at = self.where(pos)[1]; pos += 16
                        if r == 0xE: self.write(d1 & 0xFF, d0, at)
                        elif r == 0: self.write(0, d0 & 0x7FF, at)
                        elif r in (6, 7, 8, 9): self.write(r, d0, at)
            elif flg == 1:
                pos = (pos + 8 * nloop * nreg + 15) & ~15
            else:
                pos += 16 * nloop

    def run(self):
        s, i, n = self.stream, 0, len(self.stream)
        while i + 4 <= n:
            code = struct.unpack_from('<I', s, i)[0]; i += 4
            cmd, num, imm = (code >> 24) & 0x7F, (code >> 16) & 0xFF, code & 0xFFFF
            if cmd == 0x01:
                self.cl, self.wl = imm & 0xFF, imm >> 8
            elif cmd in (0x14, 0x15, 0x17):
                seg, at = self.where(i - 4)
                self.kicks.append(dict(kernel=seg[2], tag=seg[3], tid=seg[4],
                                       ref=(seg[5], seg[6]), at=at,
                                       gs=dict(self.gs), src=dict(self.src_of),
                                       tpl=dict(self.tpl)))
            elif cmd == 0x20: i += 4
            elif cmd in (0x30, 0x31): i += 16
            elif cmd == 0x4A: i += 8 * (num or 256)
            elif cmd in (0x50, 0x51):
                if i % 16: fail(f'DIRECT payload misaligned at {self.where(i)[1]:#x}')
                end = i + 16 * (imm or 65536)
                self.gif(i, end); i = end
            elif cmd >= 0x60:
                vn, vl, cnt = (cmd >> 2) & 3, cmd & 3, num or 256
                size = ((32 >> vl) * (vn + 1) * cnt + 31) // 32 * 4
                if vn == 3 and vl == 0 and not (imm >> 15) & 1:
                    for k in range(cnt):
                        a = (imm & 0x3FF) + ((k // self.wl) * self.cl + k % self.wl
                                             if self.wl <= self.cl else k)
                        if (a & 0x3FF) >= 0x3F5:
                            self.tpl[a & 0x3FF] = s[i + 16 * k:i + 16 * k + 16]
                i += size
            elif cmd not in (0x00, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                             0x10, 0x11, 0x13):
                fail(f'unknown VIF code {code:#010x} at {self.where(i - 4)[1]:#x}')


# --- records ----------------------------------------------------------------

def records(d, lo=0, hi=None):
    """Level/object render records ([TEX0][ST][colour][pos+W], 64 bytes) or
    None at every run of non-record rows (block headers, VIF codes)."""
    o, n, skip = lo, len(d) if hi is None else hi, False
    while o + 64 <= n:
        tex_row = d[o + 8:o + 16] == bytes(8) and d[o:o + 8] != bytes(8)
        z, w = struct.unpack_from('<2f', d, o + 0x18)
        pw = struct.unpack_from('<f', d, o + 0x3C)[0]
        if tex_row and z == 1.0 and w == 0.0 and abs(abs(pw) - 1.0) < 0.25:
            yield (struct.unpack_from('<Q', d, o)[0],
                   struct.unpack_from('<4f', d, o + 0x20),
                   struct.unpack_from('<I', d, o + 0x3C)[0])
            o += 64; skip = False
        else:
            if not skip: yield None
            skip = True; o += 16


def level_rgbaq_a(w):
    """RGBAQ A of the level kernel: ADDy of 65536.0, low byte of the word."""
    return struct.unpack('<I', struct.pack('<f', 65536.0 + w))[0] & 0xFF


def kick_keys(d, alpha_of):
    """{TEX0 key: set of vertex A} over every GS tristrip kick; the kick
    vertex's TEX0 is the one in force when the GS draws the triangle."""
    out, run = {}, []
    for rec in records(d):
        if rec is None:
            run = []; continue
        q, attr, wbits = rec
        run.append(alpha_of(attr))
        run = run[-3:]
        if not wbits & 0x8000 and len(run) == 3:
            out.setdefault(q & TEX0_KEY_MASK, set()).update(run)
    return out


def emdl_texture_order(d):
    """The exporter's texture slots, rebuilt independently: first kick of
    each TEX0 key with a PSMCT32/PSMT8/PSMT4 16..1024-texel texture."""
    order, seen, run = [], set(), []
    for rec in records(d):
        if rec is None:
            run = []; continue
        q, _attr, wbits = rec
        run = (run + [q])[-3:]
        if wbits & 0x8000 or len(run) < 3: continue
        key = q & TEX0_KEY_MASK
        if key in seen: continue
        seen.add(key)
        psm, tw, th = (key >> 20) & 0x3F, (key >> 26) & 0xF, (key >> 30) & 0xF
        if psm in (0x00, 0x13, 0x14) and 4 <= tw <= 10 and 4 <= th <= 10:
            order.append(key)
    return order


def pack_code(test, prim, alpha, tex0, tex1, clamp):
    return ((test & 0x3FFF) | ((prim >> 6) & 1) << 14 | (alpha & 0xFF) << 15
            | ((tex0 >> 34) & 1) << 23 | ((tex0 >> 35) & 3) << 24
            | ((tex1 >> 5) & 1) << 26 | ((tex1 >> 6) & 7) << 27
            | (clamp & 3) << 30)


def template_prim(tpl, dmem):
    raw = tpl.get(dmem)
    if raw is None: return None
    lo = struct.unpack_from('<Q', raw)[0]
    return (lo >> 47) & 0x7FF if (lo >> 46) & 1 else None


# --- ELF checks ---------------------------------------------------------------

def vu_lower(elf, address):
    return struct.unpack_from('<I', elf, address - 0x100000 + 0x300)[0]


def check_kernels(elf):
    """The level kernel loads its output GIF tag from dmem 1020 (0x3FC) and
    stores it at the head of the kicked buffer; the clip kernel loads
    dmem 1017/1018 (0x3F9/0x3FA). Decoded from the instruction fields."""
    def lq(word):   # VU lower LQ: op 0, it = dest vf, is = base vi, imm11
        imm = word & 0x7FF
        return (word >> 25, (word >> 16) & 31, (word >> 11) & 31,
                imm - 0x800 if imm & 0x400 else imm)
    op, vf, vi, imm = lq(vu_lower(elf, 0x2373F0))
    if (op, vf, vi, imm) != (0, 1, 0, 1020): fail('level kernel template load')
    op, vf, vi, imm = lq(vu_lower(elf, 0x2373F8))
    if (op, vf, vi, imm) != (1, 13, 1, 132): fail('level kernel template store')
    got = sorted(lq(vu_lower(elf, a))[3] for a in (0x23A090, 0x23A0B0))
    if got != [1017, 1018]: fail('clip kernel template loads')


def c_decode(codes):
    """Decode codes with the port's header macros (compiled natively)."""
    src = ('#include "em_gfx.h"\n#include "em_model.h"\n#include <stdio.h>\n'
           '#include <stdlib.h>\nint main(int c,char**v){\n'
           ' printf("%u %u\\n",(unsigned)EM_GFX_MESH_GSMAT,(unsigned)EM_MODEL_FLAG_GSMAT);\n'
           ' for(int i=1;i<c;i++){unsigned x=(unsigned)strtoul(v[i],0,16);'
           'unsigned t=EM_GFX_GSMAT_TEST(x);\n'
           ' printf("%u %u %u %u %u %u %u %u %u %u %u %u\\n",EM_GFX_GS_TEST_ATE(t),'
           'EM_GFX_GS_TEST_ATST(t),EM_GFX_GS_TEST_AREF(t),EM_GFX_GS_TEST_AFAIL(t),'
           'EM_GFX_GSMAT_ABE(x),EM_GFX_GSMAT_ALPHA(x),EM_GFX_GSMAT_TCC(x),'
           'EM_GFX_GSMAT_TFX(x),EM_GFX_GSMAT_MMAG(x),EM_GFX_GSMAT_MMIN(x),'
           'EM_GFX_GSMAT_WRAP(x),t);}return 0;}\n')
    with tempfile.TemporaryDirectory(prefix='em-gsmat-') as tmp:
        c, exe = Path(tmp) / 'd.c', Path(tmp) / 'd'
        c.write_text(src)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Werror', '-I' + str(ROOT / 'src'),
                        str(c), '-o', str(exe)], check=True)
        out = subprocess.run([str(exe)] + [f'{x:08X}' for x in codes], check=True,
                             capture_output=True, text=True).stdout.split('\n')
    flags = tuple(map(int, out[0].split()))
    return flags, [tuple(map(int, line.split())) for line in out[1:] if line]


# --- main ---------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--decomp-root', type=Path, default=ROOT.parent / 'Extermination')
    ap.add_argument('--scene', type=Path, default=ROOT / 'assets/scene_snow')
    ap.add_argument('--report', type=Path,
                    default=ROOT / 'build/level_material_reference/report.json')
    args = ap.parse_args()
    decomp, cap_dir = args.decomp_root, args.decomp_root / 'build/startup-reference'
    elf = (decomp / 'config/SCUS_971.12').read_bytes()
    if hashlib.sha256(elf).hexdigest() != ELF_SHA256:
        fail('config/SCUS_971.12 is not the pinned SCUS-97112 ELF')
    check_kernels(elf)

    level_state, level_keys, n_level, n_object = {}, {}, 0, 0
    object_state, object_combos, env_src, block_tex = {}, {}, {}, {}
    for name in CAPTURES:
        ram = (cap_dir / name).read_bytes()
        if ram[0x810700] != 11:
            fail(f'{name} is not an AREA11 capture')
        arena = struct.unpack_from('<I', ram, 0x27D370 - 0x7CFC)[0]
        if arena + 0xBA0 + 0x5A0 != 0x815360:
            fail(f'{name}: env arena base {arena:#x}')
        for head in LIST_HEADS:
            frame = Frame(ram, head)
            for k in frame.kicks:
                gs, kern = k['gs'], k['kernel']
                if kern in (LEVEL_KERNEL, CLIP_KERNEL):
                    prim = template_prim(k['tpl'], 0x3FC if kern == LEVEL_KERNEL else 0x3F9)
                    if prim is None or not (prim >> 4) & 1:
                        continue        # untextured shadow-volume boxes (SHADOW_ORIGINAL)
                    if k['tid'] != 3: fail(f'{name}: level kick outside a REF')
                    state = (template_prim(k['tpl'], 0x3FC), gs[GS['TEST_1']],
                             gs[GS['ALPHA_1']], gs[GS['TEX1_1']], gs[GS['CLAMP_1']],
                             (gs[GS['ZBUF_1']] >> 32) & 1, gs[GS['PRMODECONT']] & 1,
                             gs[GS['FBA_1']], gs[GS['PABE']], gs[GS['COLCLAMP']] & 1)
                    level_state[state] = level_state.get(state, 0) + 1
                    for reg in ('TEST_1', 'ALPHA_1', 'TEX1_1', 'CLAMP_1'):
                        env_src.setdefault(reg, set()).add(k['src'][GS[reg]])
                    addr, qwc = k['ref']
                    block = ram[addr:addr + 16 * qwc]
                    for key, alphas in kick_keys(block, lambda a: level_rgbaq_a(a[3])).items():
                        level_keys.setdefault(key, set()).add(state)
                    n_level += 1
                elif kern in OBJECT_KERNELS:
                    prim = template_prim(k['tpl'], 0x3FC)
                    if prim is None or not (prim >> 4) & 1 or (prim >> 6) & 1:
                        continue        # untextured or blended (effects) classes
                    state = (prim, gs[GS['TEST_1']], gs[GS['ALPHA_1']],
                             gs[GS['TEX1_1']], gs[GS['CLAMP_1']])
                    object_state[state] = object_state.get(state, 0) + 1
                    colour = k['tpl'].get(0x3F8)
                    af = struct.unpack_from('<I', colour, 12)[0] & 0xFF if colour else None
                    block = (name, *k['ref'])      # models are drawn many times
                    if block not in block_tex:
                        addr, qwc = k['ref']
                        tex = {}
                        for rec in records(ram[addr:addr + 16 * qwc]):
                            if rec is None: continue
                            c = ((rec[0] >> 34) & 1, (rec[0] >> 35) & 3)
                            tex[c] = tex.get(c, 0) + 1
                        block_tex[block] = tex
                    for (tcc, tfx), count in block_tex[block].items():
                        combo = (tcc, tfx, af)
                        object_combos[combo] = object_combos.get(combo, 0) + count
                    n_object += 1

    # Every textured level kick carries one texture-independent state.
    if len(level_state) != 1:
        fail(f'level kicks carry {len(level_state)} GS states: {level_state}')
    (prim, test, alpha, tex1, clamp, zmsk, ac, fba, pabe, colclamp), = level_state
    if any(v != {0x815360 + off} for v, off in (
            (env_src['TEX1_1'], 0x30), (env_src['TEST_1'], 0x40),
            (env_src['ALPHA_1'], 0x60), (env_src['CLAMP_1'], 0x70))):
        fail(f'level env registers not from the D_00815360 packet: {env_src}')
    if (zmsk, ac, fba, pabe, colclamp) != (0, 1, 0, 0, 1):
        fail('level kicks: unexpected ZMSK/PRMODECONT/FBA/PABE/COLCLAMP')
    if len(object_state) != 1 or next(iter(object_state))[1:] != (test, alpha, tex1, clamp):
        fail(f'textured opaque object-kernel kicks differ from level class 0: {object_state}')
    # As = At for every captured actor record: modulate needs Af 128,
    # highlight needs Af 0 (the port's default class-0 path assumes it).
    for (tcc, tfx, af) in object_combos:
        if tcc != 1 or (tfx, af) not in ((0, 128), (2, 0)):
            fail(f'object record TCC {tcc} TFX {tfx} Af {af} outside As = At')

    # Exported materials.
    codes, n_tex, n_observed = [], 0, 0
    for zone, source in ZONES:
        emdl = (args.scene / f'{zone}.emdl').read_bytes()
        side = json.loads((args.scene / f'{zone}.gsmat.json').read_text())
        bc, tc, flags = (struct.unpack_from('<I', emdl, 4)[0],
                         *struct.unpack_from('<2I', emdl, 24))
        if flags & (FLAG_VCOLOR | FLAG_GSMAT) != FLAG_VCOLOR | FLAG_GSMAT:
            fail(f'{zone}: flags {flags:#x} lack VCOLOR|GSMAT')
        src = (decomp / 'extract/chunk15' / source).read_bytes()
        order = emdl_texture_order(src)
        alphas = kick_keys(src, lambda a: level_rgbaq_a(a[3]))
        if len(order) != tc or len(side['textures']) != tc:
            fail(f'{zone}: {tc} textures, rebuilt {len(order)}, json {len(side["textures"])}')
        for i, (key, rec) in enumerate(zip(order, side['textures'])):
            word = struct.unpack_from('<I', emdl, 36 + 4 * bc + 16 * i + 12)[0]
            want = pack_code(test, prim, alpha, key, tex1, clamp)
            got = dict(tex0=int(rec['tex0'], 16), tcc=rec['tcc'], tfx=rec['tfx'],
                       a=sorted(rec['rgbaq_a']), prim=int(rec['prim'], 16),
                       test=int(rec['test_1'], 16), alpha=int(rec['alpha_1'], 16),
                       tex1=int(rec['tex1_1'], 16), clamp=int(rec['clamp_1'], 16),
                       code=int(rec['code'], 16))
            exp = dict(tex0=key, tcc=(key >> 34) & 1, tfx=(key >> 35) & 3,
                       a=sorted(alphas[key]), prim=prim, test=test, alpha=alpha,
                       tex1=tex1, clamp=clamp, code=want)
            if got != exp or word != want or rec['index'] != i:
                fail(f'{zone} texture {i}: exported {got} / {word:#x}, original {exp}')
            if key in level_keys:
                n_observed += 1
                if level_keys[key] != set(level_state):
                    fail(f'{zone} texture {i}: captured state differs')
            codes.append(word); n_tex += 1

    (flag_gfx, flag_model), decoded = c_decode(sorted(set(codes)))
    if flag_gfx != FLAG_GSMAT or flag_model != FLAG_GSMAT:
        fail('em_gfx.h/em_model.h GSMAT flag values')
    for word, fields in zip(sorted(set(codes)), decoded):
        t = word & 0x3FFF
        py = (t & 1, (t >> 1) & 7, (t >> 4) & 0xFF, (t >> 12) & 3, (word >> 14) & 1,
              (word >> 15) & 0xFF, (word >> 23) & 1, (word >> 24) & 3, (word >> 26) & 1,
              (word >> 27) & 7, (word >> 30) & 3, t)
        if fields != py: fail(f'header macros decode {word:#x} as {fields}')

    report = dict(
        level_state=dict(prim=f'{prim:#x}', test_1=f'{test:#x}', alpha_1=f'{alpha:#x}',
                         tex1_1=f'{tex1:#x}', clamp_1=f'{clamp:#x}', zbuf_zmsk=zmsk,
                         prmodecont_ac=ac, fba=fba, pabe=pabe, colclamp=colclamp),
        level_kicks=n_level, object_kicks=n_object,
        object_record_combos={f'tcc{a} tfx{b} af{c}': n for (a, b, c), n in object_combos.items()},
        textures=n_tex, textures_seen_in_captures=n_observed,
        captured_texture_keys=len(level_keys), captures=list(CAPTURES))
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=1) + '\n')
    # No sampled sweep exists: quick and full mode run every case.
    print(f'mode {MODE} (same cases in both modes): {len(CAPTURES)} captures x '
          f'{len(LIST_HEADS)} display lists, {n_level} level kicks, '
          f'{n_object} object kicks, {n_tex} exported textures')
    print(f'level class: PRIM {prim:#x} TEST_1 {test:#x} ALPHA_1 {alpha:#x} '
          f'TEX1_1 {tex1:#x} CLAMP_1 {clamp:#x}')
    print(f'{n_observed} of {n_tex} exported textures drawn in the captures; all '
          f'{n_tex} codes equal the captured class exactly')
    print('PASS')


if __name__ == '__main__':
    main()
