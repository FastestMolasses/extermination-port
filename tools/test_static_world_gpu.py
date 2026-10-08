#!/usr/bin/env python3
"""The Metal pixel path of em_gfx_gs_opaque (em_gfx_metal.m) against the
model of the GS pixel path tools/test_object_unit_gpu.py documents, over
captured static-world runs (docs/STATIC_WORLD.md section 7).

For route beat 05_boxes (EM_TEST_FULL=1: also 08_truck_crossing and
14_roger_encounter): the capture's channel-0 static run is walked by
em_static_world_draw and drawn by em_gfx_gs_opaque in the headless GPU
fixture tests/static_world_gpu_test.c with the frame's fog and the object
texture export, and the frame is captured. Independently, the triangles of
the same run are taken from the ORIGINAL VU1 microcode of the level and clip
kernels (the reference interpreter and the GS walk of
tools/test_static_world_draw_reference.py) and rasterized with the
documented pixel path: screen-linear RGBA, F and S, T, Q with the per-pixel
divide, GS bilinear with 4-bit weights and REPEAT, TFX MODULATE with TCC 1
(Cv = Ct Cf >> 7, Av = At Af >> 7), the alpha test Av > 0, the measured fog
blend and the nearest depth. Pixels within 1.5 output pixels of their
triangle's edges and depth ties are not compared.

Asserted: at least 99 % of the compared pixels agree within 2 in every
channel and at least 90 % exactly. Mac only, headless (EM_HEADLESS=1); the
capture BMP stays under build/static_world_gpu/. The report holds counts
only.
"""
import json
import os
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_object_unit_gpu as G  # noqa: E402
import test_shadow_original_reference as sh  # noqa: E402
import test_static_world_draw_reference as dr  # noqa: E402
from reference_mode import FULL, banner  # noqa: E402
from test_player_slide_reference import read_elf  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
OUT = ROOT / 'build/static_world_gpu'
BEATS = ('05_boxes', '08_truck_crossing', '14_roger_encounter')   # the default run: 05 only


def u32(b, a): return struct.unpack_from('<I', b, a)[0]
def f32(w): return struct.unpack('<f', struct.pack('<I', w))[0]


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    binary = OUT / 'fixture'
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Isrc',
                    'tests/static_world_gpu_test.c', 'src/gfx/metal/em_gfx_metal.m', 'src/platform/mac/em_platform_mac.m',
                    'src/gs/em_gs_world.c', 'src/gs/em_gs_raster.c', 'src/gs/em_gs_frame.c',
                    'src/game/em_lighting.c', 'src/game/em_packet_chain_original.c',
                    'src/game/em_status_ui_leftovers.c', 'src/game/em_object_unit.c',
                    'src/game/em_static_world_draw.c',
                    '-framework', 'Cocoa', '-framework', 'Metal', '-framework', 'QuartzCore', '-lm',
                    '-o', str(binary)], cwd=ROOT, check=True)
    return binary


def main():
    assert sys.platform == 'darwin', 'the Metal backend fixture'
    elf = read_elf()
    binary = build()
    tex = G.textures()
    report = {}
    for beat in (BEATS if FULL else BEATS[:1]):
        p = DECOMP / 'build/s87/route' / beat
        ram = (p / 'eeMemory.bin').read_bytes()
        start, end, tags = dr.static_run(ram)
        ctx = u32(ram, 0x275670)
        idx = u32(ram, ctx + 0x9C)
        fog_row = struct.unpack_from('<4I', ram, 0x816440 + 0x80 * idx + 0x10 + 16 * 4)
        fogc = list(ram[0x814220 + idx * 0x30 + 0x360:0x814220 + idx * 0x30 + 0x363])
        bmp = OUT / f'{beat}.bmp'
        env = dict(os.environ, EM_HEADLESS='1')
        subprocess.run([str(binary), str(p / 'eeMemory.bin'), repr(f32(fog_row[2])), repr(f32(fog_row[3])),
                        str(fogc[0]), str(fogc[1]), str(fogc[2]), str(bmp), f'{start:x}', f'{end:x}'],
                       cwd=ROOT, env=env, check=True, timeout=120)
        got = G.read_bmp(bmp)
        H, W = got.shape[:2]
        assert W * 3 == H * 4, ('the capture is not the 4:3 frame', W, H)
        # the original microcode's triangles of the same run
        kicks = []
        units = [(a, tid, qwc, addr) for a, tid, qwc, addr, _ in tags]
        for kernel, top, _b, _k, _s, events in sh.kernel_replay(elf, ram, units):
            kicks += [(kernel, top, e[3]) for e in events if e[0] == 'kick']
        tris = [(tex0, 0, 0, verts) for _prim, tex0, verts in dr.gs_triangles(kicks)]
        assert all((t[0] >> 35) & 3 == 0 for t in tris), (beat, 'a static-world TEX0 that is not MODULATE')
        with np.errstate(invalid='ignore'):     # uncovered pixels: -inf depths
            want, compare = G.model(tris, tex, fogc, W, H)
        diff = np.abs(got - want).max(axis=2)[compare]
        n = int(compare.sum())
        exact, close = float((diff == 0).mean()), float((diff <= 2).mean())
        assert n > 1000, (beat, 'too few interior pixels', n)
        assert close >= 0.99 and exact >= 0.90, (beat, 'pixels', n, 'exact', exact, 'within 2', close,
                                                  'max', int(diff.max()))
        report[beat] = dict(triangles=len(tris), pixels_compared=n, exact=round(exact, 4),
                            within_2=round(close, 4), max_difference=int(diff.max()), size=[W, H])
    (OUT / 'report.json').write_text(json.dumps(report, indent=1) + '\n')
    banner(f'{len(report)} of {len(BEATS)} route beats')
    print('static world GPU: PASS ' + json.dumps(report))


if __name__ == '__main__':
    sys.exit(main())
