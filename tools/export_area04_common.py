#!/usr/bin/env python3
"""Shared inputs of the AREA04 asset exporters (docs/AREA04_ASSETS.md).

AREA04 reuses the AREA01 exporters' machinery by import, unchanged, the way
the AREA00 and AREA02 exporters do (export_area00_common,
export_area02_common): export_area01_common (the load map built from the
loader's own descriptors and cursors, LoadedImage, compare_load_map, the
EMSC writer) and, through it, everything the AREA01 level / table / sound
exporters and the AREA01 checker compute. Those modules read their area
constants as attributes of export_area01_common at call time, so
`configure(sub)` points them at AREA04 (area 4, the given sub,
OVERLAY/AREA04.BIN = MWo3 id 5 at 0x823500) and at the AREA04 captures;
nothing in those files is edited.

Nothing here is original data: every byte is read from the user's own files
(the pinned boot ELF, the AREA04 overlay and the extracted chunk08 blocks
under ../Extermination/extract/, the disc image for the descriptors and the
GS upload replay), and every check compares them with the recorded AREA04
captures (FIFTH_LEVEL_ROUTE.md):
  * the AREA04 arrival: the end of beat a02_05
    (../Extermination/build/s87/route_a02/a02_05_progression_exit/, area
    bytes 04 00 00, overlay id 5 resident);
  * the a04 beats a04_00 .. a04_04 and the side beats a04_s0 .. a04_s3
    (build/s87/route_a04/); a04_05 ends in AREA22 and is excluded by its
    area byte.

Which sub each capture loaded is measured, not assumed: its D_00810701
selects the nested block, and `loaded_sub_proof` compares the whole
resident load map of each sub with the capture's RAM. Every AREA04 capture
is sub 0 (door [40] is a room move inside the sub); sub 1 (chunk08.n1) is
not loaded by any capture and is not exported.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area01_common as C  # noqa: E402

ROOT, DECOMP = C.ROOT, C.DECOMP
OVERLAY_PATH = C.EXTRACT / 'OVERLAY/AREA04.BIN'
OVERLAY_SIZE, OVERLAY_ID = 0x9400, 5          # 37,888 bytes, MWo3 id 5 at 0x823500
AREA = 4
SUBS = (0,)                                   # sub 1 is not loaded by any capture
ALL_SUBS = (0, 1)                             # the nested blocks of the descriptor
OUT = ROOT / 'assets/area04'
SCRATCH = ROOT / 'build/area04/assets'
ROUTE_A02 = DECOMP / 'build/s87/route_a02'
ROUTE_A04 = DECOMP / 'build/s87/route_a04'
ARRIVAL = ROUTE_A02 / 'a02_05_progression_exit'

# SHA-256 of the user's extract/OVERLAY/AREA04.BIN (a hash, not disc data):
# the overlay reference is as fixed as the pinned ELF
OVERLAY_SHA256 = '49cd7be912804c00bf11241ee3cf020690677fac2451d990fb30687b5d15f771'


def read_overlay():
    ov = C.read_overlay()
    if C.sha(ov) != OVERLAY_SHA256:
        raise SystemExit(f'{OVERLAY_PATH}: not the pinned AREA04 overlay')
    return ov


def sub_out(sub):
    return OUT / f'sub{sub}'


def configure(sub):
    """Point export_area01_common (and so every AREA01 exporter and checker
    function that reads it) at AREA04 sub `sub`. Returns the module."""
    C.OVERLAY_PATH = OVERLAY_PATH
    C.OVERLAY_SIZE, C.OVERLAY_ID = OVERLAY_SIZE, OVERLAY_ID
    C.AREA, C.SUB = AREA, sub
    C.OUT = sub_out(sub)
    C.SCRATCH = SCRATCH / f'sub{sub}'
    C.ROUTE_A01 = ROUTE_A04
    C.ARRIVAL = ARRIVAL
    C.captures = lambda limit=None: captures(sub, limit)
    return C


def capture_paths():
    """The arrival first, then the a04 beats in order."""
    return [ARRIVAL] + sorted(p for p in ROUTE_A04.glob('a04_*') if (p / 'eeMemory.bin').exists())


def all_captures():
    """[(Capture, sub)] of every AREA04 capture. A capture belongs to AREA04
    when its area byte is 4 and the AREA04 overlay (MWo3 id 5) is resident;
    its sub is D_00810701."""
    out = []
    for p in capture_paths():
        if not (p / 'eeMemory.bin').exists():
            continue
        c = C.Capture(p)
        if c.area[0] != AREA:
            continue
        if c.ram[C.OVERLAY_ARENA:C.OVERLAY_ARENA + 4] != b'MWo3' or C.u32(c.ram, C.OVERLAY_ARENA + 4) != OVERLAY_ID:
            raise SystemExit(f'{p}: area 4 without the AREA04 overlay resident')
        out.append((c, c.area[1]))
    return out


def captures(sub, limit=None):
    out = [c for c, s in all_captures() if s == sub]
    if limit:
        out = out[:limit]
    if not out:
        raise SystemExit(f'no AREA04 sub-{sub} capture found')
    return out


def loaded_sub_proof(pairs):
    """For every capture, the number of differing 16-byte rows of each
    nested block's load map (built with the capture's own cursors) against
    its RAM: the capture's own sub must be the smaller by far. Returns
    {name: {sub: rows}}."""
    out = {}
    for cap, _sub in pairs:
        rows = {}
        for s in ALL_SUBS:
            ram = bytearray(cap.ram)
            ram[0x810701] = s
            proxy = C.Capture.__new__(C.Capture)
            proxy.path, proxy.name, proxy.ram, proxy.spad, proxy.gs = cap.path, cap.name, bytes(ram), cap.spad, cap.gs
            lmap, _info = C.build_load_map(proxy)
            rows[s] = sum(r['differing_rows'] for r in C.compare_load_map(C.LoadedImage(lmap), proxy))
        out[cap.name] = rows
    return out
