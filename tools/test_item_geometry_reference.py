#!/usr/bin/env python3
"""Compare status arc vertices with original2082B0, VU helpers and SDK bodies."""
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
from test_item_sdk_math_reference import Original, ELF_SHA
from test_point_light_reference import number, bits

ROOT = Path(__file__).resolve().parents[1]
DESCRIPTOR, CONTEXT, PACKET = 0x980000, 0x990000, 0xa00000

class Vertex(C.Structure):
    _fields_ = [('rgba', C.c_uint32), ('x', C.c_uint16), ('y', C.c_uint16)]

def original_arc(elf, descriptor):
    o = Original(elf)
    o.write(DESCRIPTOR, struct.pack('<24f', *descriptor))
    o.save(0x275670, CONTEXT)
    o.save(CONTEXT + 0x14, PACKET)
    o.run(0x2082b0, (1, DESCRIPTOR))
    pairs = o.load(PACKET + 0x40, 8) & 0x7fff
    assert o.load(PACKET + 0x30, 8) == 0x14c
    assert o.load(CONTEXT + 0x14) == PACKET + (pairs * 2 + 5) * 16
    vertices = []
    for i in range(pairs * 2):
        at = PACKET + 0x50 + i * 16
        rgba, xy = o.load(at), o.load(at + 8)
        assert o.load(at + 12) & 0xffffff == 0xffffff
        vertices.append((rgba, xy & 65535, xy >> 16))
    return vertices

def main():
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    out = ROOT / 'build/item_geometry_reference'
    out.mkdir(parents=True, exist_ok=True)
    library = out / ('geometry.dylib' if sys.platform == 'darwin' else 'geometry.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-ffp-contract=off', '-fPIC', '-dynamiclib' if sys.platform == 'darwin' else '-shared',
        '-Isrc', 'src/game/em_item_geometry.c', 'src/game/em_item_sdk_math.c',
        'src/game/em_interaction_scan.c', '-lm', '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.em_item_geometry_arc.argtypes = [C.POINTER(C.c_float), C.POINTER(Vertex),
                                           C.c_size_t, C.POINTER(C.c_size_t)]
    templates = [struct.unpack_from('<24f', elf, at - 0x100000 + 0x300)
                 for at in range(0x2651b0, 0x265510, 0x60)]
    cases = []
    for template in templates:
        for start, end in [(-44,44),(46,134),(136,224),(226,314),(-180,180),
                            (180,540),(468,528),(528,588),(0,0),(88,-88)]:
            d = list(template)
            d[:4] = [35584,33536,start,end]
            cases.append(d)
    rng = random.Random(0x2082b0)
    for _ in range(250):
        d = list(rng.choice(templates))
        start = rng.uniform(-180,528)
        d[:4] = [rng.uniform(28000,40000),rng.uniform(30000,37000),start,
                 min(588,start+rng.uniform(-88,90))]
        cases.append(d)
    for _ in range(160):
        d = list(rng.choice(cases))
        d[8:] = [rng.uniform(0,255) for _ in range(16)]
        cases.append(d)
    count = 0
    for case, descriptor in enumerate(cases):
        values = (C.c_float * 24)(*descriptor)
        wanted = original_arc(elf, list(values))
        output, size = (Vertex * 256)(), C.c_size_t()
        assert native.em_item_geometry_arc(values, output,256,C.byref(size)) == 1, case
        actual = [(v.rgba,v.x,v.y) for v in output[:size.value]]
        assert actual == wanted, (case, [(i,a,b) for i,(a,b) in enumerate(zip(actual,wanted)) if a!=b][:8])
        count += len(actual)
    report = {'original_arc_cases':len(cases),'exact_RGBA_XYZ_vertices':count,
              'original_SDK_and_VU_helpers':'PASS','boundaries':'bounded EE arithmetic model; final rasterization'}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))

if __name__ == '__main__':
    main()
