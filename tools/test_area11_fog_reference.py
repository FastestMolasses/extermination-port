#!/usr/bin/env python3
"""Check AREA11 distance fog against original instructions and captures.

Evidence chain, all from the owner's files (nothing original is embedded):
  1. The AREA11 record (key 0x0B00) in the boot ELF's light/fog table.
  2. Original 001D8FD0 (and the 0021B970/0021B920/0021BA80/0021B8E0 chain it
     calls) executed in the point-light test's MIPS oracle: the render-ctx
     fog block it writes must equal the block in captured AREA11 EE RAM.
  3. GS FOGCOL in the captured opening GS state (PCSX2 GS freeze v9).
  4. Native: src/gfx/metal/em_fog_gs.h (used by the Metal backend) must
     reproduce the executed coefficients bit for bit, and its per-vertex F
     must equal the F field produced by executing the fog instructions of
     the original VU1 skinning kernel at 0023C780 (VU slice from the snow
     particle test's VU interpreter).
  5. The generated scene_snow manifest carries exactly one fog line equal to
     the record (export_level.py --lightrig).
The Metal shader evaluates the same F formula with ordinary float rounding;
that last step is not executed here.
"""
import argparse
import ctypes as C
import json
import struct
import subprocess
import tempfile
from pathlib import Path

import test_point_light_reference as plr
import test_snow_particles_reference as snow

ROOT = Path(__file__).resolve().parents[1]
CONTEXT = plr.CONTEXT
M64 = (1 << 64) - 1
RIG_TABLE, RIG_COUNT, RIG_SIZE = 0x251C50, 45, 0x78
KERNEL = 0x23C780
# the kernel's fog slice: mulAz.w ACC; maddbcw.w vf08; minibcx.w vf08;
# maxbcx.w vf06; ftoi4 vf07 (XYZF2 packing of F)
FOG_SLICE = (0x23C8A0, 0x23C8A8, 0x23C8C8, 0x23C8E8, 0x23C928)
bits, number = plr.bits, plr.number


class FogOracle(plr.Oracle):
    """Adds the EE ops the fog chain uses that the base oracle omits."""

    def plain(self, word):
        r = self.r
        op, rs, rt, rd, sa = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31, word >> 6 & 31
        if op == 0:
            fn = word & 63
            if fn == 4: r[rd] = (r[rt] << (r[rs] & 31)) & 0xffffffff; return   # sllv
            if fn == 37: r[rd] = r[rs] | r[rt]; r[0] = 0; return                # or (64-bit)
            if fn == 43: r[rd] = int((r[rs] & M64) < (r[rt] & M64)); r[0] = 0; return
            if fn == 56: r[rd] = (r[rt] << sa) & M64; r[0] = 0; return         # dsll
            if fn == 60: r[rd] = (r[rt] << (sa+32)) & M64; r[0] = 0; return    # dsll32
            if fn in (59, 63):                                                  # dsra/dsra32
                r[rd] = (plr.signed(r[rt] & M64, 64) >> (sa+(32 if fn == 63 else 0))) & M64
                r[0] = 0; return
        if op == 11:                                                            # sltiu
            r[rt] = int((r[rs] & M64) < (plr.signed(word & 0xffff, 16) & M64)); r[0] = 0; return
        if op in (55, 63):                                                      # ld/sd
            address = (r[rs]+plr.signed(word & 0xffff, 16)) & 0xffffffff
            if op == 63: self.save(address, r[rt] & M64, 8)
            elif rt: r[rt] = self.load(address, 8)
            return
        if op == 17 and rs == 16 and word & 63 == 7:                            # neg.s
            self.f[sa] = self.f[rd] ^ 0x80000000; return
        return super().plain(word)


def record(elf, key):
    for i in range(RIG_COUNT):
        offset = RIG_TABLE+i*RIG_SIZE-0x100000+0x300
        if struct.unpack_from('<I', elf, offset)[0] == key:
            return struct.unpack_from('<2f', elf, offset+4)+struct.unpack_from('<3i', elf, offset+0xC)
    raise AssertionError(('no fog record', hex(key)))


def ctx_block(read, base):
    return read(base+0xA0, 0x60)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp-root', type=Path, default=ROOT.parent/'Extermination')
    parser.add_argument('--scene', type=Path, default=ROOT/'assets/scene_snow/scene.txt')
    args = parser.parse_args()
    decomp = args.decomp_root.resolve()
    elf = (decomp/'config/SCUS_971.12').read_bytes()
    reference = decomp/'build/startup-reference'

    near, far, red, green, blue = record(elf, 0x0B00)
    assert (near, far, red, green, blue) == (-209.0, 304.0, 48, 48, 48), (near, far, red, green, blue)

    # 2. execute original 001D8FD0 for area 0x0B sub 0 (normal, non-0x80 path)
    oracle = FogOracle(elf)
    oracle.save(0x810700, 0x0B, 1); oracle.save(0x810701, 0, 1)
    oracle.run(0x1D8FD0)
    executed = ctx_block(oracle.read, CONTEXT)
    coefficients = struct.unpack_from('<4f', executed, 0)
    fogcol = struct.unpack_from('<Q', executed, 0x10)[0]
    assert fogcol == red | green << 8 | blue << 16, hex(fogcol)
    assert struct.unpack_from('<2f', executed, 0x18) == (near, far)
    assert executed[0x40:0x60] == executed[0:0x20], 'saved copy (+0xE0) differs'
    captures = 0
    for name in ('opening_ee.bin', 'playable_ee.bin', 'handoff_ee.bin'):
        path = reference/name
        if not path.exists(): continue
        ram = path.read_bytes()
        assert ram[0x810700:0x810702] == b'\x0b\x00', name
        base = struct.unpack_from('<I', ram, 0x275670)[0] & 0x1ffffff
        live = ram[base+0xA0:base+0x100]
        assert live[0:0x20] == executed[0:0x20], (name, 'active fog block')
        assert live[0x40:0x60] == executed[0x40:0x60], (name, 'saved fog block')
        captures += 1
    assert captures, 'no AREA11 EE RAM capture found'

    # 3. GS FOGCOL in the opening GS freeze (version 9: u32 version, then
    # 64-bit PRMODE, PRMODECONT, TEXCLUT, SCANMSK, TEXA, FOGCOL, ...)
    gs = (reference/'opening_gs.bin').read_bytes()
    assert struct.unpack_from('<I', gs, 0)[0] == 9, 'unexpected GS freeze version'
    assert struct.unpack_from('<Q', gs, 0x0C)[0] == 1, 'PRMODECONT anchor'
    assert struct.unpack_from('<Q', gs, 0x44)[0] == 1, 'COLCLAMP anchor'
    gs_fogcol = struct.unpack_from('<Q', gs, 0x2C)[0]
    assert gs_fogcol == fogcol, (hex(gs_fogcol), hex(fogcol))

    # 4. native helper vs executed original arithmetic
    shim = ('#include "gfx/metal/em_fog_gs.h"\n'
            'void coefficients(float n,float f,float*o){em_fog_gs_coefficients(n,f,o);}\n'
            'float color_unit(float c){return em_fog_gs_color_unit(c);}\n'
            'float factor(const float*c,float w){return em_fog_gs_factor(c,w);}\n')
    with tempfile.TemporaryDirectory(prefix='em-fog-') as tmp:
        source, lib = Path(tmp)/'fog.c', Path(tmp)/'fog.dylib'
        source.write_text(shim)
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                        '-shared', '-fPIC', '-I'+str(ROOT/'src'), str(source), '-o', str(lib)], check=True)
        native = C.CDLL(str(lib))
        native.coefficients.argtypes = [C.c_float, C.c_float, C.POINTER(C.c_float)]
        native.color_unit.argtypes = [C.c_float]; native.color_unit.restype = C.c_float
        native.factor.argtypes = [C.POINTER(C.c_float), C.c_float]; native.factor.restype = C.c_float
        out = (C.c_float*2)()
        native.coefficients(near, far, out)
        assert bytes(out) == executed[8:16], (list(out), coefficients)
        for channel in (red, green, blue):
            assert native.color_unit(channel) == C.c_float(channel/255.0).value

        snow.ELF = elf
        a, b = coefficients[2], coefficients[3]
        samples = [0.1, 1.0, 50.0, 100.0, 303.9, 304.0, 304.1, 500.0, 5000.0, -209.0, -208.9]
        for k in range(256):                     # every F integer boundary
            w = (k - a) / b
            for step in (-2, -1, 0, 1, 2):
                samples.append(number(bits(w)+step) if w > 0 else w)
        checked = 0
        for w in samples:
            w = C.c_float(w).value
            vm = snow.VU(bytearray(16384), program_start=KERNEL); vm.color_only = True
            vm.v[27] = [bits(x) for x in coefficients]
            vm.v[4] = [0, 0, 0, bits(w)]
            for pc in FOG_SLICE:
                vm.run(pc, pc+8)
            gs_f = (vm.v[7][3] >> 4) & 0xff
            assert native.factor(out, w) == gs_f, (w, native.factor(out, w), gs_f)
            checked += 1

    # 5. manifest
    lines = [line.split() for line in args.scene.read_text().splitlines() if line.startswith('fog ')]
    assert len(lines) == 1, ('fog lines', lines)
    assert [float(x) for x in lines[0][1:]] == [near, far, red, green, blue], lines[0]

    print(json.dumps({'status': 'PASS', 'record': [near, far, red, green, blue],
                      'coefficients': coefficients, 'fogcol': hex(fogcol),
                      'ram_captures': captures, 'gs_fogcol': hex(gs_fogcol),
                      'vu_fog_cases': checked}))


if __name__ == '__main__':
    main()
