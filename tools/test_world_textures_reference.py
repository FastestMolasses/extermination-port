#!/usr/bin/env python3
"""Disc-upload/capture proof and native world-texture delivery contract.

Default compares first/arrival AREA01 GS freezes; EM_TEST_FULL=1 checks all.
Both modes prove complete catalog upload, reused-key pixel replacement,
same-area reload, device switch and failed-catalog/upload rejection.
"""
import ctypes as C
import os
from pathlib import Path
import struct
import subprocess
import tempfile

import export_area01_world_textures as E

ROOT = E.A.ROOT
OUT = ROOT/'build/level2/world_textures'
MASK = (1 << 61)-1


class Entry(C.Structure):
    _fields_ = [('key',C.c_uint64),('width',C.c_uint32),('height',C.c_uint32),('hash',C.c_uint32)]


def catalog(path):
    b = path.read_bytes()
    out = {}
    for i in range(struct.unpack_from('<I',b,8)[0]):
        k,w,h,at,_ = struct.unpack_from('<Q4I',b,16+24*i)
        out[k&MASK] = (w,h,b[at:at+4*w*h])
    return out


def fnv(b):
    h = 2166136261
    for x in b:h = ((h^x)*16777619)&0xffffffff
    return h


def expected(paths):
    out = {}
    for p in paths:
        for k,v in catalog(p).items():
            assert k not in out or out[k] == v
            out[k] = v
    return out


def main():
    os.chdir(ROOT)
    OUT.mkdir(parents=True,exist_ok=True)
    caps = E.A.captures()
    if not os.getenv('EM_TEST_FULL'):caps = [caps[0],caps[-1]]
    data,refs,pixels = E.export(OUT/'area01_world_textures.emot',captures=caps)
    assert data == (ROOT/'assets/area01_world_textures.emot').read_bytes()
    assert 0x0006E305954234F8 in pixels, 'dynamic material missing'
    libpath = OUT/'textures.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC','-Isrc',
        'tests/world_textures_bridge.c','src/game/em_world_textures_live.c','-o',str(libpath)],check=True)
    lib = C.CDLL(str(libpath))
    lib.em_world_textures_live_ensure.argtypes = [C.c_void_p]
    lib.em_world_textures_live_bind.argtypes = [C.c_uint,C.c_uint]
    lib.texture_test_entry.argtypes = [C.c_uint,C.c_uint]
    lib.texture_test_entry.restype = C.POINTER(Entry)
    first = expected([ROOT/'assets/scene_snow/object_textures.emot',ROOT/'assets/scene_snow/page_textures.emot'])
    area = catalog(OUT/'area01_world_textures.emot')
    hashes = {11:{k:(w,h,fnv(p)) for k,(w,h,p) in first.items()},
              1:{k:(w,h,fnv(p)) for k,(w,h,p) in area.items()}}
    changed = [k for k in first.keys()&area.keys() if first[k] != area[k]]
    assert changed, 'test must exercise TEX0 address reuse with different pixels'

    def check(which,device=0):
        assert lib.em_world_textures_live_ensure(device+1) == 0
        got = {}
        for i in range(lib.texture_test_count(device)):
            e = lib.texture_test_entry(device,i).contents
            assert e.key not in got
            got[e.key] = e.width,e.height,e.hash
        assert got == hashes[which], (which,device,len(got))

    assert lib.em_world_textures_live_ensure(None) == -1
    check(11)  # default first-level contract
    before = lib.texture_test_uploads(0),lib.texture_test_resets(0)
    check(11)
    assert before == (lib.texture_test_uploads(0),lib.texture_test_resets(0))
    for a in (1,11,1,1):
        resets = lib.texture_test_resets(0)
        assert lib.em_world_textures_live_bind(a,0) == 0
        check(a)
        assert lib.texture_test_resets(0) == resets+1
    check(1,1)
    resets = lib.texture_test_resets(0)
    check(1,0)
    assert lib.texture_test_resets(0) == resets+1
    assert lib.em_world_textures_live_bind(1,1) == -1
    assert lib.em_world_textures_live_ensure(1) == -1
    assert lib.em_world_textures_live_bind(0,0) == -1
    assert lib.em_world_textures_live_ensure(1) == -1

    # Missing and malformed catalogs fail before a registry mutation. The
    # old registry is inaccessible because ensure continues to fail until
    # the next actual delivery. No fallback AREA11 upload is attempted.
    faults = 2
    small = E.O.emot({next(iter(area)):pixels[next(iter(area))]})
    def changed_word(offset,value):
        b=bytearray(small);struct.pack_into('<I',b,offset,value);return b
    bad = [None,b'bad',changed_word(4,2),changed_word(8,513),changed_word(24,0),
           changed_word(32,0),small[:-1]]
    with tempfile.TemporaryDirectory(dir=OUT) as tmp:
        os.chdir(tmp)
        path = Path('assets/area01_world_textures.emot')
        path.parent.mkdir()
        for b in bad:
            if b is None:
                if path.exists():path.unlink()
            else:path.write_bytes(b)
            assert lib.em_world_textures_live_bind(1,0) == 0
            before = lib.texture_test_uploads(0),lib.texture_test_resets(0)
            assert lib.em_world_textures_live_ensure(1) == -1
            assert lib.em_world_textures_live_ensure(1) == -1
            assert before == (lib.texture_test_uploads(0),lib.texture_test_resets(0))
            faults += 1
        # Two keys differing only in CLD must not silently replace pixels.
        key = next(iter(area))
        p = pixels[key]
        altered = bytes([p[0]^1])+p[1:]
        path.write_bytes(E.O.emot({key:p,key|(1<<61):altered}))
        assert lib.em_world_textures_live_bind(1,0) == 0
        assert lib.em_world_textures_live_ensure(1) == -1
        faults += 1
        os.chdir(ROOT)
    assert lib.em_world_textures_live_bind(1,0) == 0
    lib.texture_test_reject(3)
    resets = lib.texture_test_resets(0)
    assert lib.em_world_textures_live_ensure(1) == -1
    assert lib.texture_test_count(0) == 0 and lib.texture_test_resets(0) == resets+2
    lib.texture_test_reject(-1)
    assert lib.em_world_textures_live_ensure(1) == -1
    assert lib.em_world_textures_live_bind(11,0) == 0
    check(11)
    faults += 1
    print(f'PASS world textures: {len(caps)} exact GS freezes; AREA11 {len(first)}, AREA01 {len(area)}, '
          f'{len(changed)} reused keys with changed pixels; delivery/device cache and {faults} rejection cases')


if __name__ == '__main__':main()
