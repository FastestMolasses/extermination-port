#!/usr/bin/env python3
"""The Original profile's GS memory against the disc model of the uploads
(docs/GS_EXACT.md section 9, docs/DISC_TEXTURES.md section 2).

The CPU GS model's local memory is the boot library image
(tools/export_gs_memory.py, assets/gs_library.emgm), then the area load's
uploads, which the loader's area consumer hands to em_gs_world_upload_chain
as the buffers 00200830 / 00200890 send. This test replays the New Game's
upload sequence of tools/export_disc_textures_gs.py (FirstLevel.world():
the library, 001AD1A0's slot 0x35, AREA11's A sections at 001FFCD0 state 4,
the player texture packet at state 7) over the user's disc, sends every
area-load buffer through the C model (src/gs/em_gs_world.c, its own chain
walk, VIF / GIF rules and the GS model's transfers), and requires:

  A. the installed library image equals the disc model's library uploads
     (the export's own bytes and runs);
  B. after the area load, every block the disc model's uploads wrote holds
     the same 256 bytes in the C model's memory, and the C model's resident
     blocks are exactly those blocks;
  C. the C model refuses a chain tag it does not take (REF), a VIF code it
     does not take (UNPACK), a register other than the transfer set, and a
     transfer other than host-to-local PSMCT32.

The disc model is the one DISC_TEXTURES test B proves equal to every route
capture's uploaded blocks. About 3 s.
"""
from __future__ import annotations

import ctypes as C
import os
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import export_disc_textures_gs as G  # noqa: E402
import export_gs_memory as X  # noqa: E402

OUT = ROOT / 'build' / 'gs_memory_reference'
BLOCKS = (4 << 20) // 256

SHIM = r'''
#include "gs/em_gs_world.h"
EmGsWorld *shim_new(void) { return em_gs_world_create(); }
void shim_free(EmGsWorld *w) { em_gs_world_destroy(w); }
int shim_load(EmGsWorld *w, const char *p) { return em_gs_world_memory_load(w, p); }
int shim_upload(EmGsWorld *w, const unsigned char *b, size_t n) { return em_gs_world_upload_chain(w, b, n); }
const unsigned char *shim_mem(EmGsWorld *w) { return em_gs_world_memory(w); }
int shim_resident(EmGsWorld *w, unsigned b) { return em_gs_world_resident(w, b); }
const char *shim_fault(EmGsWorld *w) { return em_gs_world_fault(w); }
'''


def build() -> C.CDLL:
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'shim.c').write_text(SHIM)
    lib = OUT / 'libgsworld.dylib'
    subprocess.run(['cc', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC', '-dynamiclib', '-Isrc',
                    'src/gs/em_gs_world.c', 'src/gs/em_gs_raster.c', 'src/gs/em_gs_frame.c', str(OUT / 'shim.c'),
                    '-lm', '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    n.shim_new.restype = C.c_void_p
    n.shim_free.argtypes = [C.c_void_p]
    n.shim_load.argtypes = [C.c_void_p, C.c_char_p]
    n.shim_upload.argtypes = [C.c_void_p, C.c_char_p, C.c_size_t]
    n.shim_mem.restype = C.POINTER(C.c_uint8)
    n.shim_mem.argtypes = [C.c_void_p]
    n.shim_resident.argtypes = [C.c_void_p, C.c_uint]
    n.shim_fault.restype = C.c_char_p
    n.shim_fault.argtypes = [C.c_void_p]
    return n


def chain(gif: bytes) -> bytes:
    """A one-tag CNT chain (then END) carrying a DIRECT of `gif`."""
    vif = struct.pack('<4I', 0, 0, 0x11000000, 0x50000000 | (len(gif) // 16)) + gif
    qwc = len(vif) // 16
    return struct.pack('<IIII', 0x10000000 | qwc, 0, 0, 0) + vif + struct.pack('<IIII', 0x70000000, 0, 0, 0)


def ad(pairs) -> bytes:
    tag = struct.pack('<QQ', len(pairs) | 0x8000 | (1 << 60), 0xE)
    return tag + b''.join(struct.pack('<QQ', v, r) for r, v in pairs)


def main() -> int:
    t0 = time.monotonic()
    lib = build()
    os.environ['EM_GS_THREADS'] = '2'
    fl = G.FirstLevel(G.Disc(), G.EXTRACT)
    # A: the library image
    library = X.library(fl)
    image = OUT / 'gs_library.emgm'
    image.write_bytes(X.emgm(library))
    installed = ROOT / 'assets' / 'gs_library.emgm'
    assert installed.exists(), 'assets/gs_library.emgm is missing (python3 tools/export_gs_memory.py)'
    assert installed.read_bytes() == image.read_bytes(), 'assets/gs_library.emgm is stale: re-run the exporter'
    w = lib.shim_new()
    assert w and lib.shim_load(w, str(image).encode()) == 0, 'the library image does not load'

    # B: the area load's buffers through the C model, beside the disc model
    sent = []

    class Tee(G.GSImage):
        def upload(self, caller, label, buf):
            log = super().upload(caller, label, buf)
            if caller.startswith('001FFCD0'):
                rc = lib.shim_upload(w, buf, len(buf))
                assert rc == 0, (label, 'the C model refused the upload', lib.shim_fault(w))
                sent.append((caller, label, len(buf)))
            return log

    world = Tee()
    fl.library(world)
    fl.library_slot(world, '001AD1A0: 00200830(D_0028A564)')
    fl.area(world, G.FIRST_LEVEL_AREA, 0, 0, 0)
    assert sent, 'no area upload was sent'
    mem = bytes(C.cast(lib.shim_mem(w), C.POINTER(C.c_uint8 * (4 << 20))).contents)
    bad = [b for b in sorted(world.covered) if mem[b * 256:(b + 1) * 256] != bytes(world.lm[b * 256:(b + 1) * 256])]
    resident = {b for b in range(BLOCKS) if lib.shim_resident(w, b)}
    assert not bad, (len(bad), 'blocks differ from the disc model, first', hex(bad[0]))
    assert resident == world.covered, ('resident blocks differ', len(resident ^ world.covered))
    lib.shim_free(w)

    # C: the refusals
    good_bb = (0x2A00 << 32) | (4 << 48)
    cases = {
        'REF tag': struct.pack('<IIII', 0x30000001, 0x100000, 0, 0),
        'UNPACK': (struct.pack('<IIII', 0x10000001, 0, 0, 0) + struct.pack('<4I', 0, 0, 0, 0x6C010000) +
                   struct.pack('<IIII', 0x70000000, 0, 0, 0)),
        'register': chain(ad([(0x47, 0x30000)])),
        'transfer': chain(ad([(0x50, good_bb | (2 << 56)), (0x51, 0), (0x52, 64 | (32 << 32)), (0x53, 0)])),
    }
    why = {'REF tag': b'tag other than CNT', 'UNPACK': b'VIF code', 'register': b'GIF data other than',
           'transfer': b'host-to-local PSMCT32'}
    for name, buf in cases.items():
        v = lib.shim_new()
        assert lib.shim_load(v, str(image).encode()) == 0
        assert lib.shim_upload(v, buf, len(buf)) != 0, (name, 'was not refused')
        assert why[name] in lib.shim_fault(v), (name, 'refused for another reason', lib.shim_fault(v))
        lib.shim_free(v)
    print(f'gs memory: PASS (the library image equals the disc model ({len(library.covered)} blocks); '
          f'{len(sent)} area-load buffers through the C model leave all {len(world.covered)} uploaded blocks '
          f'equal to the disc model and exactly them resident; {len(cases)} refusals; '
          f'{time.monotonic() - t0:.1f} s)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
