#!/usr/bin/env python3
"""src/gs/em_gs_raster (the CPU GS model) against the GS conformance captures.

docs/GS_EXACT.md section 7. The decomp's conformance harness
(../Extermination/tools/gs_conformance*.py, docs/GS_CONFORMANCE.md) sent
designed GIF packets to PCSX2's software renderer and saved the GS local
memory after each batch (build/b16/gscap*/<batch>/snap/gs.bin, ignored,
generated locally; nothing disc-derived). This test sends the SAME packet
bytes (packet.bin) to the C model in strict mode (as the Original profile
runs it), starting from zeroed local memory, ends the span (em_gs_flush,
section 3.7) and compares:

  A. every test's colour buffer and Z buffer, decoded through the swizzle
     maps the layout batch MEASURED (so the model's own address tables are
     checked too), pixel for pixel. A Z16 buffer shorter than its 64-row
     page is compared only where the test drew (its clear sprite cannot
     reach the rest of the page);
  B. every uploaded rectangle (textures, CLUTs, destinations) pixel for
     pixel through the model's own addressing, and the fence page;
  C. (EM_TEST_FULL=1) the repeat capture of the first suite;
  D. the binding helper src/gs/em_gs_frame.c: a random primitive list with
     its environment, replayed from EmGfxGsPrim / EmGfxGsEnv records, must
     leave local memory byte-identical to the same writes sent as an A+D
     GIF packet, and em_gs_read_frame_rgba must equal the measured-map
     decode of that buffer;
  E. strict mode itself (section 6): synthetic packets that must be refused
     in strict mode and draw in the default mode, and controls.

A refusal or a span fault in any capture packet fails.

Every test must match bit for bit, except the tests listed in OPEN with
their exact mismatch counts (colour words, Z words): an open item of
docs/GS_EXACT.md section 8. A count that changes in either direction fails,
so every improvement or regression has to be recorded. Default ~1.5 s
(30 batches, 906 tests); EM_TEST_FULL=1 ~1.9 s.
"""
from __future__ import annotations

import ctypes as C
import json
import os
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from reference_mode import FULL, MODE  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
B16 = Path(os.environ.get('GSCAP_ROOT', DECOMP / 'build/b16'))
CAP = B16 / 'gscap'                         # its layout batch holds the measured maps
OUT = ROOT / 'build/b16/gs_raster'
SETS = [
    ('gscap', ['layout', 'raster', 'shade', 'texture', 'pixel', 'frame', 'probe2']),
    ('gscap3', ['p3_start', 'p3_z', 'p3_stq']),
    ('gscap4', ['p4_rcp', 'p4_z', 'p4_misc']),
    ('gscap5', ['p5_s', 'p5_q']),
    ('gscap6', ['p6_cov', 'p6_tfx', 'p6_z']),
    ('gscap7', ['p7_lvl', 'p7_wrap', 'p7_z', 'p7_zc', 'p7_scope', 'p7_flush']),
    ('gscap8', ['p8_span', 'p8_class', 'p8_misc', 'p8_gif', 'p8_more', 'p8_gif2']),
]
FULL_SETS = [('gscap_repeat', SETS[0][1])]
FREEZE_VRAM, LOCALMEM, PAGE = 425, 4 << 20, 8192
PSM = {"CT32": 0x00, "CT24": 0x01, "CT16": 0x02, "CT16S": 0x0A, "T8": 0x13, "T4": 0x14,
       "Z32": 0x30, "Z24": 0x31, "Z16": 0x32, "Z16S": 0x3A}

# Open items (docs/GS_EXACT.md section 8): 'set/batch/test' -> (colour, z) mismatch counts.
OPEN: dict[str, tuple[int, int]] = {
    'gscap/pixel/ztri_geq': (0, 90),
    'gscap/probe2/gouraud_order_0_0': (0, 90),
    'gscap/probe2/gouraud_order_0_1': (0, 90),
    'gscap/probe2/gouraud_order_0_2': (0, 90),
    'gscap/probe2/gouraud_order_0_3': (0, 90),
    'gscap/probe2/gouraud_order_0_4': (0, 90),
    'gscap/probe2/gouraud_order_0_5': (0, 90),
    'gscap/probe2/gouraud_order_1_0': (0, 61),
    'gscap/probe2/gouraud_order_1_1': (0, 61),
    'gscap/probe2/gouraud_order_1_2': (0, 61),
    'gscap/probe2/gouraud_order_1_3': (0, 61),
    'gscap/probe2/gouraud_order_1_4': (0, 61),
    'gscap/probe2/gouraud_order_1_5': (0, 61),
    'gscap/shade/gouraud_line': (2, 0),
    'gscap/shade/gouraud_rand_3': (1, 0),
    'gscap/shade/gouraud_strip': (4, 0),
    'gscap/shade/z_interp_z24': (0, 390),
    'gscap/shade/z_interp_z32': (0, 1888),
    'gscap/shade/z_range16': (1, 1),
    'gscap/texture/level_class': (1, 34),
    'gscap/texture/persp_0': (3, 0),
    'gscap/texture/persp_1': (1, 0),
    'gscap/texture/persp_2': (4, 0),
    'gscap/texture/tri_uv': (1, 0),
    'gscap3/p3_start/lines_x': (3, 0),
    'gscap3/p3_start/lines_y': (4, 0),
    'gscap3/p3_stq/level_0': (0, 390),
    'gscap3/p3_stq/level_1': (1, 253),
    'gscap3/p3_stq/level_2': (1, 134),
    'gscap3/p3_stq/level_3': (0, 254),
    'gscap3/p3_stq/persp_bil_5': (1, 0),
    'gscap3/p3_stq/persp_near_3': (1, 0),
    'gscap3/p3_stq/persp_near_4': (1, 0),
    'gscap3/p3_stq/uv_tri_bil': (9, 0),
    'gscap3/p3_z/z16_c1': (0, 1),
    'gscap3/p3_z/z24_frac_large_c1': (0, 18),
    'gscap3/p3_z/z24_frac_large_s1': (0, 1),
    'gscap3/p3_z/z24_frac_large_s2': (0, 1),
    'gscap3/p3_z/z24_frac_large_s5': (0, 4),
    'gscap3/p3_z/z24_frac_large_u1': (0, 195),
    'gscap3/p3_z/z24_frac_large_u2': (0, 69),
    'gscap3/p3_z/z24_frac_small_u1': (0, 4),
    'gscap3/p3_z/z24_frac_small_u2': (0, 6),
    'gscap3/p3_z/z24_neg_c1': (0, 22),
    'gscap3/p3_z/z24_neg_s1': (0, 1),
    'gscap3/p3_z/z32_c1': (0, 253),
    'gscap3/p3_z/z32_s4': (0, 15),
    'gscap3/p3_z/z32_v1000': (0, 36),
    'gscap3/p3_z/zlines': (0, 5),
    'gscap4/p4_misc/cov_nd_0': (2, 0),
    'gscap4/p4_misc/cov_nd_2': (1, 0),
    'gscap4/p4_z/z24_7ff000_u2': (0, 22),
    'gscap5/p5_q/q_c1_0.75_1': (2, 0),
    'gscap5/p5_q/q_c1_1.25_2': (3, 0),
    'gscap5/p5_q/q_c1_3.0_8': (2, 0),
    'gscap5/p5_q/q_s1_0.75_1': (5, 0),
    'gscap5/p5_q/q_s1_1.25_2': (2, 0),
    'gscap5/p5_q/q_s1_1.5_-2': (3, 0),
    'gscap5/p5_q/q_s2_0.75_1': (2, 0),
    'gscap5/p5_q/q_s2_1.25_2': (6, 0),
    'gscap5/p5_q/q_s2_1.5_-2': (1, 0),
    'gscap5/p5_q/q_s4_0.75_1': (4, 0),
    'gscap5/p5_q/q_s4_1.5_-2': (1, 0),
    'gscap5/p5_q/q_u2_0.75_1': (2, 0),
    'gscap5/p5_q/q_v1000_0.75_1': (3, 0),
    'gscap5/p5_q/q_v1000_1.25_2': (1, 0),
    'gscap5/p5_q/sq_gen_0': (177, 0),
    'gscap5/p5_q/sq_gen_1': (60, 0),
    'gscap5/p5_q/sq_gen_2': (175, 0),
    'gscap5/p5_q/sq_gen_3': (184, 0),
    'gscap5/p5_q/sq_gen_4': (30, 0),
    'gscap5/p5_q/sq_gen_5': (123, 0),
    'gscap5/p5_q/sq_gen_6': (291, 0),
    'gscap5/p5_q/sq_gen_7': (79, 0),
    'gscap5/p5_q/sq_gen_8': (61, 0),
    'gscap5/p5_q/sq_gen_9': (326, 0),
    'gscap5/p5_s/s_c2_1': (1, 0),
    'gscap5/p5_s/s_gen_0': (10, 0),
    'gscap5/p5_s/s_gen_1': (6, 0),
    'gscap5/p5_s/s_gen_2': (12, 0),
    'gscap5/p5_s/s_gen_3': (9, 0),
    'gscap5/p5_s/s_gen_4': (107, 0),
    'gscap5/p5_s/s_gen_5': (34, 0),
    'gscap5/p5_s/s_s3_1': (3, 0),
    'gscap5/p5_s/s_s5_1': (1, 0),
    'gscap5/p5_s/s_s6_1': (2, 0),
    'gscap5/p5_s/s_v1000_1': (1, 0),
    'gscap5/p5_s/s_vhalf_1': (3, 0),
    'gscap5/p5_s/s_vhalf_4': (2, 0),
    'gscap6/p6_z/z_apex0_ABC': (0, 22),
    'gscap6/p6_z/z_apex0_ACB': (0, 22),
    'gscap6/p6_z/z_apex0_BCA': (0, 22),
    'gscap6/p6_z/z_apex0_CAB': (0, 22),
    'gscap6/p6_z/z_apex1_ABC': (0, 21),
    'gscap6/p6_z/z_apex1_ACB': (0, 21),
    'gscap6/p6_z/z_apex1_BCA': (0, 21),
    'gscap6/p6_z/z_apex1_CAB': (0, 21),
    'gscap6/p6_z/z_apex4_ABC': (0, 1),
    'gscap6/p6_z/z_apex4_ACB': (0, 1),
    'gscap6/p6_z/z_apex4_BCA': (0, 1),
    'gscap6/p6_z/z_apex4_CAB': (0, 1),
    'gscap6/p6_z/z_apex5_ABC': (0, 11),
    'gscap6/p6_z/z_apex5_ACB': (0, 11),
    'gscap6/p6_z/z_apex5_BCA': (0, 11),
    'gscap6/p6_z/z_apex5_CAB': (0, 11),
    'gscap7/p7_lvl/ct32_0': (0, 390),
    'gscap7/p7_lvl/ct32_1': (1, 253),
    'gscap7/p7_lvl/ct32_2': (1, 134),
    'gscap7/p7_lvl/ct32_3': (1, 255),
    'gscap7/p7_lvl/decal_0': (0, 390),
    'gscap7/p7_lvl/decal_1': (1, 253),
    'gscap7/p7_lvl/decal_2': (1, 134),
    'gscap7/p7_lvl/decal_3': (0, 255),
    'gscap7/p7_lvl/flat_0': (0, 390),
    'gscap7/p7_lvl/flat_1': (1, 253),
    'gscap7/p7_lvl/flat_2': (1, 134),
    'gscap7/p7_lvl/flat_3': (0, 255),
    'gscap7/p7_lvl/nofog_0': (0, 390),
    'gscap7/p7_lvl/nofog_1': (1, 253),
    'gscap7/p7_lvl/nofog_2': (1, 134),
    'gscap7/p7_lvl/nofog_3': (0, 255),
    'gscap7/p7_lvl/plain_0': (0, 390),
    'gscap7/p7_lvl/plain_1': (1, 253),
    'gscap7/p7_lvl/plain_2': (1, 134),
    'gscap7/p7_lvl/plain_3': (0, 255),
    'gscap7/p7_lvl/ref_0': (0, 390),
    'gscap7/p7_lvl/ref_1': (1, 253),
    'gscap7/p7_lvl/ref_2': (1, 134),
    'gscap7/p7_lvl/ref_3': (0, 255),
    'gscap7/p7_lvl/small_0': (0, 390),
    'gscap7/p7_lvl/small_1': (0, 253),
    'gscap7/p7_lvl/small_2': (1, 134),
    'gscap7/p7_lvl/small_3': (1, 255),
    'gscap7/p7_lvl/tris_0': (0, 390),
    'gscap7/p7_lvl/tris_1': (1, 253),
    'gscap7/p7_lvl/tris_2': (1, 134),
    'gscap7/p7_lvl/tris_3': (0, 255),
    'gscap7/p7_lvl/zalways_0': (0, 390),
    'gscap7/p7_lvl/zalways_1': (1, 253),
    'gscap7/p7_lvl/zalways_2': (1, 134),
    'gscap7/p7_lvl/zalways_3': (0, 255),
    'gscap7/p7_wrap/persp_bil_5_clamp': (1, 0),
    'gscap7/p7_wrap/persp_bil_5_repeat': (1, 0),
    'gscap7/p7_wrap/persp_near_3_clamp': (1, 0),
    'gscap7/p7_wrap/persp_near_3_repeat': (1, 0),
    'gscap7/p7_wrap/persp_near_4_clamp': (1, 0),
    'gscap7/p7_wrap/persp_near_4_repeat': (1, 0),
    'gscap7/p7_z/persp0_vz': (1, 0),
    'gscap7/p7_z/persp0_zge': (1, 0),
    'gscap7/p7_z/persp0_zw': (1, 0),
    'gscap7/p7_z/persp1_vz': (2, 0),
    'gscap7/p7_z/persp1_zge': (2, 1),
    'gscap7/p7_z/persp1_zw': (2, 1),
    'gscap7/p7_z/persp2_vz': (1, 0),
    'gscap7/p7_z/persp2_zge': (1, 0),
    'gscap7/p7_z/persp2_zw': (1, 0),
    'gscap7/p7_z/persp3_vz': (1, 0),
    'gscap7/p7_z/persp3_zge': (1, 0),
    'gscap7/p7_z/persp3_zw': (1, 0),
    'gscap7/p7_z/persp4_vz': (1, 0),
    'gscap7/p7_z/persp4_zge': (1, 0),
    'gscap7/p7_z/persp4_zw': (1, 0),
    'gscap7/p7_z/persp5_ate': (1, 0),
    'gscap7/p7_z/persp5_vz': (2, 0),
    'gscap7/p7_z/persp5_zge': (2, 1),
    'gscap7/p7_z/persp5_zw': (2, 1),
    'gscap7/p7_z/persp5_zw0': (1, 0),
    'gscap7/p7_z/strip0_zge': (0, 390),
    'gscap7/p7_z/strip0_zw': (0, 390),
    'gscap7/p7_z/strip1_zge': (0, 253),
    'gscap7/p7_z/strip1_zw': (0, 253),
    'gscap7/p7_z/strip2_vz': (1, 0),
    'gscap7/p7_z/strip2_zge': (1, 134),
    'gscap7/p7_z/strip2_zw': (1, 134),
    'gscap7/p7_z/strip3_zge': (0, 255),
    'gscap7/p7_z/strip3_zw': (0, 255),
    'gscap7/p7_zc/persp0_last1': (1, 0),
    'gscap7/p7_zc/persp0_xplane': (1, 0),
    'gscap7/p7_zc/persp0_yplane': (1, 0),
    'gscap7/p7_zc/persp1_last1': (2, 0),
    'gscap7/p7_zc/persp1_xplane': (2, 0),
    'gscap7/p7_zc/persp1_yplane': (2, 0),
    'gscap7/p7_zc/persp2_last1': (1, 0),
    'gscap7/p7_zc/persp2_xplane': (1, 0),
    'gscap7/p7_zc/persp2_yplane': (1, 0),
    'gscap7/p7_zc/persp3_last1': (1, 0),
    'gscap7/p7_zc/persp3_xplane': (1, 0),
    'gscap7/p7_zc/persp3_yplane': (1, 0),
    'gscap7/p7_zc/persp4_last1': (1, 0),
    'gscap7/p7_zc/persp4_xplane': (1, 0),
    'gscap7/p7_zc/persp4_yplane': (1, 0),
    'gscap7/p7_zc/persp5_c1': (1, 0),
    'gscap7/p7_zc/persp5_c123456': (1, 0),
    'gscap7/p7_zc/persp5_last1': (2, 0),
    'gscap7/p7_zc/persp5_xplane': (2, 0),
    'gscap7/p7_zc/persp5_yplane': (2, 0),
    'gscap7/p7_zc/strip2_last1': (1, 0),
    'gscap7/p7_zc/strip2_xplane': (1, 0),
    'gscap7/p7_zc/strip2_yplane': (1, 0),
    'gscap8/p8_gif/pk_junk_adc_ad_nop': (1, 0),
    'gscap8/p8_gif/pk_uv_xyzf2_fog': (1, 0),
    'gscap8/p8_span/s0_rev_prim_fst': (8, 0),
    'gscap8/p8_span/s1_rev_prim_fst': (5, 0),
}

SHIM = r'''
#include "gs/em_gs_raster.h"
#include <stdlib.h>
EmGs *shim_new(uint8_t *mem, int strict) { EmGs *g = calloc(1, sizeof *g); em_gs_init(g, mem); g->strict = strict; return g; }
void shim_free(EmGs *g) { em_gs_release(g); free(g); }
size_t shim_gif(EmGs *g, const uint8_t *d, size_t n) { size_t r = em_gs_gif(g, d, n); em_gs_flush(g); return r; }
unsigned shim_refusals(EmGs *g) { return g->refusals; }
const char *shim_reason(EmGs *g) { return g->reason; }
unsigned long long shim_pixels(EmGs *g) { return g->drawn_pixels; }
unsigned shim_span_faults(EmGs *g) { return g->span_faults; }
unsigned shim_px(uint8_t *mem, unsigned bp, unsigned bw, unsigned psm, unsigned x, unsigned y)
{ EmGs g; em_gs_init(&g, mem); return em_gs_read_pixel(&g, bp, bw, psm, x, y); }
#include "gs/em_gs_frame.h"
unsigned shim_replay(EmGs *g, const EmGfxGsPrim *p, const EmGfxGsEnv *e, unsigned n, unsigned long long fogcol)
{ EmGsReplay r; em_gs_replay_init(&r); em_gs_replay_fogcol(g, &r, fogcol); return em_gs_replay_prims(g, &r, p, e, n); }
void shim_read_rgba(EmGs *g, unsigned fbp, unsigned fbw, unsigned w, unsigned h, uint8_t *out)
{ em_gs_read_frame_rgba(g, fbp, fbw, w, h, out); }
unsigned shim_prim_size(void) { return sizeof(EmGfxGsPrim); }
unsigned shim_env_size(void) { return sizeof(EmGfxGsEnv); }
'''


def build() -> C.CDLL:
    OUT.mkdir(parents=True, exist_ok=True)
    shim = OUT / 'shim.c'
    shim.write_text(SHIM)
    lib = OUT / ('libgsraster.dylib' if sys.platform == 'darwin' else 'libgsraster.so')
    cmd = ['cc', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
           '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-Isrc',
           'src/gs/em_gs_raster.c', 'src/gs/em_gs_frame.c', str(shim), '-lm', '-o', str(lib)]
    subprocess.run(cmd, cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    n.shim_new.restype = C.c_void_p
    n.shim_new.argtypes = [C.c_void_p, C.c_int]
    n.shim_free.argtypes = [C.c_void_p]
    n.shim_gif.restype = C.c_size_t
    n.shim_gif.argtypes = [C.c_void_p, C.c_char_p, C.c_size_t]
    n.shim_refusals.restype = C.c_uint
    n.shim_refusals.argtypes = [C.c_void_p]
    n.shim_reason.restype = C.c_char_p
    n.shim_reason.argtypes = [C.c_void_p]
    n.shim_pixels.restype = C.c_ulonglong
    n.shim_pixels.argtypes = [C.c_void_p]
    n.shim_span_faults.restype = C.c_uint
    n.shim_span_faults.argtypes = [C.c_void_p]
    n.shim_px.restype = C.c_uint
    n.shim_px.argtypes = [C.c_void_p] + [C.c_uint] * 5
    n.shim_replay.restype = C.c_uint
    n.shim_replay.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.c_uint, C.c_ulonglong]
    n.shim_read_rgba.argtypes = [C.c_void_p] + [C.c_uint] * 4 + [C.c_void_p]
    n.shim_prim_size.restype = C.c_uint
    n.shim_env_size.restype = C.c_uint
    return n


class GsVertex(C.Structure):
    _fields_ = [('x', C.c_uint16), ('y', C.c_uint16), ('z', C.c_uint32), ('f', C.c_uint8), ('has_f', C.c_uint8),
                ('rgba', C.c_uint8 * 4), ('q', C.c_uint32), ('s', C.c_uint32), ('t', C.c_uint32),
                ('u', C.c_uint16), ('v', C.c_uint16)]


class GsPrim(C.Structure):
    _fields_ = [('prim', C.c_uint32), ('set', C.c_uint32), ('tex0', C.c_uint64), ('clamp', C.c_uint64),
                ('tex1', C.c_uint64), ('alpha', C.c_uint64), ('test', C.c_uint64), ('colclamp', C.c_uint64),
                ('count', C.c_uint32), ('v', GsVertex * 3)]


class GsEnv(C.Structure):
    _fields_ = [('frame', C.c_uint64), ('zbuf', C.c_uint64), ('xyoffset', C.c_uint64), ('scissor', C.c_uint64),
                ('prmodecont', C.c_uint64), ('dthe', C.c_uint64), ('fba', C.c_uint64), ('pabe', C.c_uint64),
                ('texa', C.c_uint64), ('scanmsk', C.c_uint64), ('set', C.c_uint32)]


def check_replay(lib, maps) -> int:
    """D: the binding helper against the packet path (random primitive list)."""
    import random
    import struct
    rnd = random.Random(1609)
    assert lib.shim_prim_size() == C.sizeof(GsPrim) and lib.shim_env_size() == C.sizeof(GsEnv)
    n = 300
    prims = (GsPrim * n)()
    envs = (GsEnv * n)()
    writes = [(0x3D, 0x304050)]                      # FOGCOL first, as shim_replay does
    f32 = lambda v: struct.unpack('<I', struct.pack('<f', v))[0]
    last = {}
    # a CT32 texture 16x16 at block 0x1C0 * 32 (uploaded identically on both paths)
    tex = bytes(rnd.getrandbits(8) for _ in range(16 * 16 * 4))
    tbp = 0x1C0 * 32
    for i in range(n):
        e, p = envs[i], prims[i]
        e.frame, e.zbuf, e.xyoffset = 0 | 8 << 16, 0x70 | 0x31 << 24, (1792 * 16) | (1936 * 16) << 32
        e.scissor = 511 << 16 | 223 << 48
        e.prmodecont, e.dthe, e.fba, e.pabe, e.texa, e.scanmsk = 1, 0, 0, 0, 0x80 << 32, 0
        e.set = 0x3FF if i == 0 else 0
        kind = rnd.choice([3, 3, 4, 6, 1])
        tme = rnd.random() < 0.5
        p.prim = kind | 1 << 3 | (tme << 4) | (rnd.random() < 0.3) << 5 | (rnd.random() < 0.3) << 6
        p.set = 0x3F
        p.tex0 = tbp | 1 << 14 | 4 << 26 | 4 << 30 | 1 << 34 | rnd.randrange(4) << 35
        p.clamp, p.tex1 = rnd.randrange(2) * 5, 0x60 if rnd.random() < 0.5 else 0
        p.alpha, p.test, p.colclamp = rnd.choice([0x44, 0x8000000068]), rnd.choice([0x5000D, 0x53001, 0x30000]), 1
        p.count = {1: 2, 3: 3, 4: 3, 6: 2}[kind]
        for k in range(p.count):
            v = p.v[k]
            v.x, v.y = (1792 + rnd.randrange(-40, 552)) * 16 + rnd.randrange(16), (1936 + rnd.randrange(-20, 244)) * 16 + rnd.randrange(16)
            v.z, v.f, v.has_f = rnd.randrange(1 << 24), rnd.randrange(256), rnd.randrange(2)
            for c in range(4):
                v.rgba[c] = rnd.randrange(256)
            v.q = f32(rnd.uniform(0.3, 2.0))
            v.s, v.t = f32(rnd.uniform(-1, 2)), f32(rnd.uniform(-1, 2))
            v.u, v.v = rnd.randrange(1 << 14), rnd.randrange(1 << 14)
        # the same writes as an A+D list
        if i == 0:
            writes += [(0x4C, e.frame), (0x4E, e.zbuf), (0x18, e.xyoffset), (0x40, e.scissor), (0x1A, 1),
                       (0x45, 0), (0x4A, 0), (0x49, 0), (0x3B, e.texa), (0x22, 0)]
        for reg, val in ((0x06, p.tex0), (0x08, p.clamp), (0x14, p.tex1), (0x42, p.alpha), (0x47, p.test),
                         (0x46, p.colclamp)):
            if last.get(reg) != val:
                writes.append((reg, val))
                last[reg] = val
        writes.append((0x00, p.prim))
        for k in range(p.count):
            v = p.v[k]
            writes += [(0x01, v.rgba[0] | v.rgba[1] << 8 | v.rgba[2] << 16 | v.rgba[3] << 24 | v.q << 32),
                       (0x02, v.s | v.t << 32), (0x03, v.u | v.v << 16)]
            writes.append((0x04, v.x | v.y << 16 | (v.z & 0xFFFFFF) << 32 | v.f << 56) if v.has_f
                          else (0x05, v.x | v.y << 16 | v.z << 32))
    upload = [(0x50, tbp << 32 | 1 << 48), (0x51, 0), (0x52, 16 | 16 << 32), (0x53, 0)]
    def packet(ws, image=None):
        out = b''
        for i in range(0, len(ws), 0x7FFF):
            part = ws[i:i + 0x7FFF]
            out += struct.pack('<QQ', len(part) | 1 << 60, 0xE)
            out += b''.join(struct.pack('<QQ', v & (2**64 - 1), r) for r, v in part)
        if image:
            out += struct.pack('<QQ', len(image) // 16 | 2 << 58, 0) + image
        return out
    up_pkt = packet(upload, tex)
    mems = []
    for path in ('packet', 'replay'):
        mem = (C.c_uint8 * LOCALMEM)()
        g = lib.shim_new(C.addressof(mem), 0)
        lib.shim_gif(g, up_pkt, len(up_pkt))
        if path == 'packet':
            pk = packet(writes)
            lib.shim_gif(g, pk, len(pk))
        else:
            lib.shim_replay(g, C.addressof(prims), C.addressof(envs), n, 0x304050)
        rgba = (C.c_uint8 * (512 * 224 * 4))()
        lib.shim_read_rgba(g, 0, 8, 512, 224, C.addressof(rgba))
        lib.shim_free(g)
        mems.append((np.frombuffer(bytes(mem), np.uint8), np.frombuffer(bytes(rgba), '<u4').reshape(224, 512)))
    same = bool((mems[0][0] == mems[1][0]).all())
    dec = decode(mems[1][0], 0, 512, 224, 'CT32', maps)
    readback = bool((dec == mems[1][1]).all())
    drew = int((mems[1][1] != 0).sum())
    print(f'  replay: {n} primitives, {drew} field pixels written, memory equal to the packet path {same}, '
          f'read_frame_rgba equal to the measured decode {readback}')
    return 0 if same and readback and drew > 10000 else 1


def check_strict(lib) -> int:
    """E: strict mode (docs/GS_EXACT.md section 6). Each case must be
    refused in strict mode with the named reason and draw in the default
    mode; the controls must draw without a refusal in both."""
    import struct

    def pkt(ws):
        return struct.pack('<QQ', len(ws) | 1 << 60, 0xE) + b''.join(
            struct.pack('<QQ', v & (2**64 - 1), r) for r, v in ws)
    f32 = lambda v: struct.unpack('<I', struct.pack('<f', v))[0]
    base = [(0x4C, 0 | 4 << 16), (0x4E, 0x40 | 0x31 << 24), (0x47, 0x30000), (0x18, 0), (0x40, 255 << 16 | 63 << 48),
            (0x1A, 1), (0x06, (0x1C0 * 32) | 1 << 14 | 4 << 26 | 4 << 30 | 1 << 35), (0x14, 0x60), (0x08, 5)]

    def vert(x, y, z=0x100, q=1.0, s=0.25, t=0.25):
        return [(0x01, 0x80808080 | f32(q) << 32), (0x02, f32(s * q) | f32(t * q) << 32),
                (0x05, int(x * 16) | int(y * 16) << 16 | z << 32)]

    def strip(x0, zs, prim=0x1C):
        w = [(0x00, prim)]
        for i, z in enumerate(zs):
            w += vert(x0 + 20 * (i // 2), 40 * (i % 2), z)
        return w
    tri = [(0x00, 0x13)] + vert(0, 0) + vert(60, 0) + vert(0, 50)
    cases = {
        # name: (writes, strict reason substring or None for a control)
        'aa1': ([(0x00, 0x03 | 0x80)] + vert(0, 0) + vert(60, 0) + vert(0, 50), 'AA1'),
        'mipmap': ([(0x14, 0x60 | 1 << 2)] + tri, 'MXL'),
        'prmode_ac0': ([(0x1A, 0), (0x1B, 0x10)] + tri, 'PRMODECONT'),
        'textured_line': ([(0x00, 0x11)] + vert(0, 0) + vert(60, 20), 'textured line'),
        'local_to_local': ([(0x50, 0x1C0 * 32 | 1 << 16 | (0x1C8 * 32) << 32 | 1 << 48), (0x51, 0),
                            (0x52, 8 | 8 << 32), (0x53, 2)], 'LOCAL->LOCAL'),
        'local_to_host': ([(0x50, 0x1C0 * 32 | 1 << 16), (0x51, 0), (0x52, 8 | 8 << 32), (0x53, 1)], 'LOCAL->HOST'),
        # a constant-Z STQ strip and a varying-Z strip split by an unmeasured
        # boundary (a TEX0 TW change alone): the grid decision is unsettled
        'span_unmeasured': (strip(0, [0x200] * 6) + [(0x06, (0x1C0 * 32) | 1 << 14 | 5 << 26 | 4 << 30 | 1 << 35)]
                            + strip(100, [0x200, 0x300, 0x400, 0x500, 0x600, 0x700]), 'span boundary'),
        # controls: the same split with both halves at one Z, or at a measured boundary (TFX)
        'span_unmeasured_same_z': (strip(0, [0x200] * 6) + [(0x06, (0x1C0 * 32) | 1 << 14 | 5 << 26 | 4 << 30 | 1 << 35)]
                                   + strip(100, [0x200] * 6), None),
        'span_measured': (strip(0, [0x200] * 6) + [(0x06, (0x1C0 * 32) | 1 << 14 | 4 << 26 | 4 << 30)]
                          + strip(100, [0x200, 0x300, 0x400, 0x500, 0x600, 0x700]), None),
        'dithered_point': ([(0x4C, 0 | 4 << 16 | 2 << 24), (0x45, 1), (0x44, 0x1234567), (0x00, 0x00)]
                           + [(0x01, 0x80406020), (0x05, 5 * 16 | 5 * 16 << 16 | 0x100 << 32)], None),
    }
    bad = 0
    for name, (ws, reason) in cases.items():
        packet = pkt(base + ws)
        got = []
        for strict in (1, 0):
            mem = (C.c_uint8 * LOCALMEM)()
            g = lib.shim_new(C.addressof(mem), strict)
            lib.shim_gif(g, packet, len(packet))
            got.append((lib.shim_refusals(g), lib.shim_reason(g).decode(), lib.shim_pixels(g)))
            lib.shim_free(g)
        (rs, why, px_s), (rn, _, px_n) = got
        if reason is None:
            ok = rs == 0 and rn == 0 and px_s > 0
        else:
            # a refused primitive draws nothing; a span fault draws under the model's rule
            ok = (bool(rs & 1) and reason in why and (px_s == 0 or name.startswith('span'))
                  and (rn == 0 or name == 'textured_line') and (px_n > 0 or name.startswith('local')))
        if not ok:
            bad += 1
            print(f'      FAIL strict case {name}: strict refusals {rs:#x} ({why}) pixels {px_s}; '
                  f'default refusals {rn:#x} pixels {px_n}')
    print(f'  strict: {len(cases)} cases, {len(cases) - bad} as expected')
    return bad


def decode(vram: np.ndarray, page: int, w: int, h: int, psm: str, maps) -> np.ndarray:
    """(h, w) pixel values of a buffer through the MEASURED maps (the
    decomp's gs_conformance.decode_buffer, restated)."""
    if psm in ('CT32', 'CT24', 'Z32', 'Z24'):
        key, pw, ph, unit = ('z32' if psm.startswith('Z') else 'ct32'), 64, 32, '<u4'
    else:
        key, pw, ph, unit = ('z16' if psm.startswith('Z') else 'ct16'), 64, 64, '<u2'
    m = maps[key]
    arr = vram.view(unit)
    per_page = PAGE // np.dtype(unit).itemsize
    y, x = np.mgrid[0:h, 0:w]
    idx = (page + (y // ph) * (w // pw) + x // pw) * per_page + m[y % ph, x % pw]
    return arr[idx]


def run_batch(lib, cap: Path, name: str, maps):
    folder = cap / name
    doc = json.loads((folder / 'batch.json').read_text())
    packet = (folder / 'packet.bin').read_bytes()
    gs_bin = (folder / 'snap/gs.bin').read_bytes()
    ref = np.frombuffer(gs_bin[FREEZE_VRAM:FREEZE_VRAM + LOCALMEM], dtype=np.uint8)
    mem = (C.c_uint8 * LOCALMEM)()
    g = lib.shim_new(C.addressof(mem), 1)          # strict, as the Original profile runs
    t0 = time.monotonic()
    used = lib.shim_gif(g, packet, len(packet))
    dt = time.monotonic() - t0
    refusals, reason = lib.shim_refusals(g), lib.shim_reason(g).decode()
    span_faults = lib.shim_span_faults(g)
    lib.shim_free(g)
    got = np.frombuffer(bytes(mem), dtype=np.uint8)
    refbuf = (C.c_uint8 * LOCALMEM).from_buffer_copy(ref.tobytes())
    res = {'batch': name, 'seconds': round(dt, 3), 'consumed': used == len(packet),
           'refusals': refusals, 'reason': reason, 'span_faults': span_faults, 'tests': {}, 'uploads_bad': {}}
    for t in doc['tests']:
        a = decode(got, t['fbp'], t['w'], t['h'], t['psm'], maps)
        b = decode(ref, t['fbp'], t['w'], t['h'], t['psm'], maps)
        bad = int((a != b).sum())
        zbad = 0
        if t['zpsm']:
            za = decode(got, t['zbp'], t['w'], t['h'], t['zpsm'], maps)
            zb = decode(ref, t['zbp'], t['w'], t['h'], t['zpsm'], maps)
            diff = za != zb
            if t['zpsm'] in ('Z16', 'Z16S') and t['h'] % 64:
                diff &= b != int(t['clear'], 16)
            zbad = int(diff.sum())
        res['tests'][t['name']] = {'colour': bad, 'z': zbad, 'pixels': t['w'] * t['h']}
        # uploaded rectangles, through the model's addressing
        for it in t['items']:
            if 'image' not in it:
                continue
            im = it['image']
            psm = PSM[im['psm']]
            nbad = 0
            for y in range(im['h']):
                for x in range(im['w']):
                    args = (im['dbp'], max(1, im['dbw'] // 64), psm, im['dx'] + x, im['dy'] + y)
                    if lib.shim_px(C.addressof(mem), *args) != lib.shim_px(C.addressof(refbuf), *args):
                        nbad += 1
            if nbad:
                res['uploads_bad'][f"{t['name']}/{im['label']}"] = nbad
    fence = doc['fence']['page']
    s = slice(fence * PAGE, (fence + 1) * PAGE)
    res['fence_bad'] = int((got[s] != ref[s]).sum())
    return res


def main() -> int:
    if not (CAP / 'layout/maps.npz').exists():
        print(f'SKIP: no conformance captures at {CAP} (decomp tools/gs_conformance.py capture + decode)')
        return 0
    print(f'test_gs_raster_reference ({MODE})')
    lib = build()
    maps = dict(np.load(CAP / 'layout/maps.npz'))
    t0 = time.monotonic()
    fails = total = exact = 0
    verbose = os.environ.get('EM_GS_VERBOSE', '') not in ('', '0')
    summary = {}
    sets = SETS + (FULL_SETS if FULL else [])
    for set_name, batches in sets:
        cap = B16 / set_name
        if not (cap / batches[0] / 'snap/gs.bin').exists():
            print(f'  {set_name}: not captured, skipped')
            continue
        for name in batches:
            r = run_batch(lib, cap, name, maps)
            bad_tests = {k: v for k, v in r['tests'].items() if v['colour'] or v['z']}
            n = len(r['tests'])
            total += n
            exact += n - len(bad_tests)
            line = (f"  {set_name}/{name:9s} tests {n:3d} exact {n - len(bad_tests):3d}  "
                    f"uploads bad {r['uploads_bad'] or 0}  fence bad {r['fence_bad']}  {r['seconds']} s")
            if r['refusals'] or r['span_faults']:
                line += f"  FAIL strict refusals {r['refusals']:#x} ({r['reason']}) span faults {r['span_faults']}"
            print(line)
            for k, v in r['tests'].items():
                key = f'{set_name}/{name}/{k}'
                got = (v['colour'], v['z'])
                want = OPEN.get(key.replace('gscap_repeat/', 'gscap/'), (0, 0))
                summary[key] = v
                if got != want:
                    fails += 1
                    print(f"      FAIL {k}: colour {got[0]} z {got[1]} of {v['pixels']}"
                          + (f" (recorded open {want})" if want != (0, 0) else ''))
                elif got != (0, 0) and verbose:
                    print(f"      open {k}: colour {got[0]} z {got[1]} of {v['pixels']}")
            if r['uploads_bad'] or r['fence_bad'] or not r['consumed'] or r['refusals'] or r['span_faults']:
                fails += 1
    fails += check_replay(lib, maps)
    fails += check_strict(lib)
    (OUT / 'last_run.json').write_text(json.dumps(summary, indent=1) + '\n')
    print(f'{exact}/{total} tests bit-exact; {fails} failures; {time.monotonic() - t0:.1f} s')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
