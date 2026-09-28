#!/usr/bin/env python3
"""Shared inputs of the AREA00 asset exporters (docs/AREA00_ASSETS.md).

AREA00 reuses the AREA01 exporters' machinery by import, unchanged:
export_area01_common (the load map built from the loader's own descriptors
and cursors, LoadedImage, compare_load_map, the EMSC writer) and, through
it, everything the AREA01 level / table / sound exporters and the AREA01
checker compute. Those modules read their area constants as attributes of
export_area01_common at call time, so `configure(sub)` points them at
AREA00 (area 0, the given sub, OVERLAY/AREA00.BIN = MWo3 id 1 at 0x823500)
and at the AREA00 captures; nothing in those files is edited.

Nothing here is original data: every byte is read from the user's own files
(the pinned boot ELF, the AREA00 overlay and the extracted chunk04 blocks
under ../Extermination/extract/, the disc image for the GS upload replay),
and every check compares them with the recorded AREA00 captures:
  * the AREA00 arrival: the end of SECOND_LEVEL_ROUTE.md beat a01_07
    (../Extermination/build/s87/route_a01/a01_07_level_exit/, area bytes
    00 00 00, overlay id 1 resident);
  * the AREA00 route beats a00_00 .. a00_09 (build/s87/route_a00/); a00_10
    and a00_s0 end in AREA01 and are excluded by their area byte.

Two sub-states were loaded on the route (measured, not assumed): the
captures group by D_00810701, and each group's whole resident load map
equals RAM only for its own nested block (chunk04.n0 for sub 0: the arrival
and a00_00 .. a00_07; chunk04.n1 for sub 1: a00_08 and a00_09, after the
switch reloads the area). The n1 map against a sub-0 capture, and the n0 map
against a sub-1 capture, differ in about 225,000 16-byte rows
(`loaded_sub_proof`).
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area01_common as C  # noqa: E402

ROOT, DECOMP = C.ROOT, C.DECOMP
OVERLAY_PATH = C.EXTRACT / 'OVERLAY/AREA00.BIN'
OVERLAY_SIZE, OVERLAY_ID = 0x9F80, 1          # 40,832 bytes, MWo3 id 1 at 0x823500
AREA = 0
SUBS = (0, 1)
OUT = ROOT / 'assets/area00'
SCRATCH = ROOT / 'build/area00/assets'
ROUTE_A00 = DECOMP / 'build/s87/route_a00'
ARRIVAL = DECOMP / 'build/s87/route_a01/a01_07_level_exit'

# SHA-256 of the user's extract/OVERLAY/AREA00.BIN (a hash, not disc data):
# the overlay reference is as fixed as the pinned ELF
OVERLAY_SHA256 = '86fb5d594ca8963d13404933cfd7bd2fbe2268b2468a83aa399fd2c1558b8784'


def read_overlay():
    ov = C.read_overlay()
    if C.sha(ov) != OVERLAY_SHA256:
        raise SystemExit(f'{OVERLAY_PATH}: not the pinned AREA00 overlay')
    return ov


def sub_out(sub):
    return OUT / f'sub{sub}'


def configure(sub):
    """Point export_area01_common (and so every AREA01 exporter and checker
    function that reads it) at AREA00 sub `sub`. Returns the module."""
    C.OVERLAY_PATH = OVERLAY_PATH
    C.OVERLAY_SIZE, C.OVERLAY_ID = OVERLAY_SIZE, OVERLAY_ID
    C.AREA, C.SUB = AREA, sub
    C.OUT = sub_out(sub)
    C.SCRATCH = SCRATCH / f'sub{sub}'
    C.ROUTE_A01 = ROUTE_A00
    C.ARRIVAL = ARRIVAL
    C.captures = lambda limit=None: captures(sub, limit)
    return C


def all_captures():
    """[(Capture, sub)] of every AREA00 capture, the arrival first, then the
    a00 beats in order. A capture belongs to AREA00 when its area byte is 0
    and the AREA00 overlay (MWo3 id 1) is resident; its sub is D_00810701."""
    paths = [ARRIVAL] + sorted(p for p in ROUTE_A00.glob('a00_*') if (p / 'eeMemory.bin').exists())
    out = []
    for p in paths:
        if not (p / 'eeMemory.bin').exists():
            continue
        c = C.Capture(p)
        if c.area[0] != AREA:
            continue
        if c.ram[C.OVERLAY_ARENA:C.OVERLAY_ARENA + 4] != b'MWo3' or C.u32(c.ram, C.OVERLAY_ARENA + 4) != OVERLAY_ID:
            raise SystemExit(f'{p}: area 0 without the AREA00 overlay resident')
        out.append((c, c.area[1]))
    return out


def captures(sub, limit=None):
    out = [c for c, s in all_captures() if s == sub]
    if limit:
        out = out[:limit]
    if not out:
        raise SystemExit(f'no AREA00 sub-{sub} capture found')
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
