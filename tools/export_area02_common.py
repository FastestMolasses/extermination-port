#!/usr/bin/env python3
"""Shared inputs of the AREA02 asset exporters (docs/AREA02_ASSETS.md).

AREA02 reuses the AREA01 exporters' machinery by import, unchanged, the way
the AREA00 exporters do (export_area00_common): export_area01_common (the
load map built from the loader's own descriptors and cursors, LoadedImage,
compare_load_map, the EMSC writer) and, through it, everything the AREA01
level / table / sound exporters and the AREA01 checker compute. Those
modules read their area constants as attributes of export_area01_common at
call time, so `configure(sub)` points them at AREA02 (area 2, the given sub,
OVERLAY/AREA02.BIN = MWo3 id 3 at 0x823500) and at the AREA02 captures;
nothing in those files is edited.

Nothing here is original data: every byte is read from the user's own files
(the pinned boot ELF, the AREA02 overlay and the extracted chunk06 blocks
under ../Extermination/extract/, the disc image for the descriptors and the
GS upload replay), and every check compares them with the recorded AREA02
captures (FOURTH_LEVEL_ROUTE.md):
  * the AREA02 arrival: the end of beat a01r_03
    (../Extermination/build/s87/route_a01r/a01r_03_door16/, area bytes
    02 01 01, overlay id 3 resident), sub 1;
  * a02_s0 (the bed, from the arrival), sub 1;
  * a02_00 .. a02_04 (after the duct's sub change), sub 0;
  a02_05 ends in AREA04 and is excluded by its area byte.

Which sub each capture loaded is measured, not assumed: its D_00810701
selects the nested block, and `loaded_sub_proof` compares the whole
resident load map of each sub with the capture's RAM.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area01_common as C  # noqa: E402

ROOT, DECOMP = C.ROOT, C.DECOMP
OVERLAY_PATH = C.EXTRACT / 'OVERLAY/AREA02.BIN'
OVERLAY_SIZE, OVERLAY_ID = 0x5D80, 3          # 23,936 bytes, MWo3 id 3 at 0x823500
AREA = 2
SUBS = (0, 1)                                 # sub 2 is not loaded by any capture
OUT = ROOT / 'assets/area02'
SCRATCH = ROOT / 'build/area02/assets'
ROUTE_A01R = DECOMP / 'build/s87/route_a01r'
ROUTE_A02 = DECOMP / 'build/s87/route_a02'
ARRIVAL = ROUTE_A01R / 'a01r_03_door16'

# SHA-256 of the user's extract/OVERLAY/AREA02.BIN (a hash, not disc data):
# the overlay reference is as fixed as the pinned ELF
OVERLAY_SHA256 = '10740d88df2a328c7e966a94525e9ddc1dbbb35f4d6b08aa46e629dbf6587f93'


def read_overlay():
    ov = C.read_overlay()
    if C.sha(ov) != OVERLAY_SHA256:
        raise SystemExit(f'{OVERLAY_PATH}: not the pinned AREA02 overlay')
    return ov


def sub_out(sub):
    return OUT / f'sub{sub}'


def configure(sub):
    """Point export_area01_common (and so every AREA01 exporter and checker
    function that reads it) at AREA02 sub `sub`. Returns the module."""
    C.OVERLAY_PATH = OVERLAY_PATH
    C.OVERLAY_SIZE, C.OVERLAY_ID = OVERLAY_SIZE, OVERLAY_ID
    C.AREA, C.SUB = AREA, sub
    C.OUT = sub_out(sub)
    C.SCRATCH = SCRATCH / f'sub{sub}'
    C.ROUTE_A01 = ROUTE_A02
    C.ARRIVAL = ARRIVAL
    C.captures = lambda limit=None: captures(sub, limit)
    return C


def capture_paths():
    """The arrival first, then the a02 beats in order."""
    return [ARRIVAL] + sorted(p for p in ROUTE_A02.glob('a02_*') if (p / 'eeMemory.bin').exists())


def all_captures():
    """[(Capture, sub)] of every AREA02 capture. A capture belongs to AREA02
    when its area byte is 2 and the AREA02 overlay (MWo3 id 3) is resident;
    its sub is D_00810701."""
    out = []
    for p in capture_paths():
        if not (p / 'eeMemory.bin').exists():
            continue
        c = C.Capture(p)
        if c.area[0] != AREA:
            continue
        if c.ram[C.OVERLAY_ARENA:C.OVERLAY_ARENA + 4] != b'MWo3' or C.u32(c.ram, C.OVERLAY_ARENA + 4) != OVERLAY_ID:
            raise SystemExit(f'{p}: area 2 without the AREA02 overlay resident')
        out.append((c, c.area[1]))
    return out


def captures(sub, limit=None):
    out = [c for c, s in all_captures() if s == sub]
    if limit:
        out = out[:limit]
    if not out:
        raise SystemExit(f'no AREA02 sub-{sub} capture found')
    return out


def loaded_sub_proof(pairs):
    """For every capture, the number of differing 16-byte rows of each sub's
    load map (built with the capture's own cursors) against its RAM: the
    capture's own sub must be the smaller by far. Returns {name: {sub: rows}}."""
    out = {}
    for cap, _sub in pairs:
        rows = {}
        for s in SUBS:
            ram = bytearray(cap.ram)
            ram[0x810701] = s
            proxy = C.Capture.__new__(C.Capture)
            proxy.path, proxy.name, proxy.ram, proxy.spad, proxy.gs = cap.path, cap.name, bytes(ram), cap.spad, cap.gs
            lmap, _info = C.build_load_map(proxy)
            rows[s] = sum(r['differing_rows'] for r in C.compare_load_map(C.LoadedImage(lmap), proxy))
        out[cap.name] = rows
    return out
