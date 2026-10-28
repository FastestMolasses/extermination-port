#!/usr/bin/env python3
"""The status pages' GS packets as the live game builds them, against the
original's (docs/STATUS_FIELD.md section 5, step 3; src/game/em_status_gs_page.c).

The headless game runs the level smoke's route to the battery take
(EM_STARTUP_TEST=newgame-level, EM_LEVEL_SMOKE_UNTIL=battery) with
EM_STATUS_GS_DUMP: every frame in which the BATTERY page or a glyph run built
slot-1 packets leaves the bytes between the frame head's slot-1 cursor and
step V's. Inputs (user-local, never copied here, nothing of them embedded):
the decomp's fork captures of the status pages
(../Extermination/build/status_field_capture, test_status_field_reference's
FRAMES) and the user's font export.

1. Every dumped frame decodes completely into the builders' packets
   (test_ui_packet_reference.decode; the skewed glyph strips by shape), so the
   cursor ran from packet to packet with nothing else in between.
2. Every BATTERY page frame has the captured frames' structure: the
   0020A7A0 background (00207D00 and 513 sprites), the page's sprites and
   text, the take notice's glyph runs or the list.
3. Route 01's captured frames f300 and f400: a port frame decodes into the
   captured chain's calls exactly (the background's phase included), at the
   frame the original's own counts place it: the notice's timeline (route
   01's state 5 step 3 from f219, the port's first BATTERY frame) less the
   hub frames of the smoke's START visit (each one 0020A7A0 on the shared
   D_002655A0, which no screen resets; route 01's capture session had no such
   visit): first + (T - 219) - hub_draws. f230's frame by that rule lies
   before the port's page, so a port frame must equal it but for the
   background. EM_TEST_FULL=1 runs on to route 03's panel BATTERY page: its
   captured f440 and f500 must equal a port frame but for the background,
   and each captured background must equal one port BATTERY frame's exactly
   (the shared D_002655A0 at the same step count of a different history),
   those two frames as many page frames apart as f440..f500. For each matched frame the bytes the native builders write
   (run twice over two fills from the port's calls) equal the port's dumped
   bytes and the captured chain's at every written byte.
4. The captured list with the port's page bytes in place (their unwritten
   bytes as the port's arena held them) through the CPU GS model: the drawn
   field equals the field the original displayed (f300; EM_TEST_FULL=1 f400
   too).

5. The GS field (STATUS_FIELD.md section 3d): the run's EM_PRESENT_LOG
   presents every BATTERY page frame as a GS field (the Original profile:
   the kicked list through the CPU GS model) and nothing else of the run
   differently; the fields of the port frames that equal f300 and f400 (3)
   hash equal to the field the original displayed (fnv64 of the 512 x 224 x 4
   bytes); 001CCB10's CLUT kick (em_uip_001CCB10_chain, the boot upload that
   the glyph strips read) writes the bytes every capture holds at GS block
   0x1B67. EM_TEST_FULL=1 also runs the game again to the battery take with
   frame captures at those two frames (EM_FB_CAPTURE_TICKS) and checks the
   presented picture, the overlay pass included, at every field pixel's place
   (test_fb2_pixels.shown_field) equal to the original's displayed field.

6. The ITEM root (STATUS_FIELD.md section 3e; EM_TEST_FULL=1, or --item
   alone): the level smoke's status_pages side run (EM_LEVEL_SMOKE_UNTIL=
   status_pages) with the dump. Every dumped frame decodes (the fans of a
   moving stick by their shape); every ITEM root frame is 0020F170 and
   0020F2A0's packets after the background (the five sprites and the
   blend, 00207D00(1, 1) and the sixteen 001D66A0 fans, 00207D00(1, 0),
   the region sprites, 00207D00(1, 3) and the last sprite) and is
   presented as a GS field; every one at rest with hover 0 equals the
   fork capture slot12_status_root_pad8080 f0040 (the original's ITEM root
   with the port's neutral stick 0x80 held) in every call after the
   background. The captures slot12_status_root_pad8080_bg3467 f0040 /
   f0041 are the same with D_002655A0 set to the port's at loop top 39
   (capture.py CAPTURE_WRITE: the only write): the port frame whose dumped
   D_002655A0 (before its 0020A7A0) is those bytes is N, and frames N + 1
   and N + 2 must equal the two captures in every call (the background
   included), their written bytes equal the builders' and the capture's,
   and their presented fields hash equal to the fields the original
   displayed; a second run with frame captures at those two frames checks
   the presented picture, the overlay pass included, at every field
   pixel's place.

7. The ITEM children EQUIPMENT and EVENT (STATUS_FIELD.md section 3f; the
   same run as 6): every frame of a bound page (the dump's flag 8) is
   presented as a GS field and is either the original's page after the
   background (equal in every call after it to the fork capture
   slot12_status_root_equipment f0060 or slot12_status_root_event f0062:
   the same state, the stick to the region, Cross, the page open; capture.py
   CAPTURE_INPUTS) or empty: the frame whose Circle leaves the page (its
   code returns before any leaf), whose field must hash equal to the
   displayed field of slot12_status_root_equipment_circle f0072 (the
   original's Circle frame, which builds no slot-1 packet either; the
   first one's picture is compared in the second run too). The
   captures <page>_bg<N> are the open page with D_002655A0 set to the port's
   of frame N at loop top T - 1 (CAPTURE_WRITE): frames N + 1 and N + 2
   (chosen of the captures' field parity: FRAME_1 FBP 0x38, then 0) must
   equal them in every call, their written bytes equal the builders' and
   the capture's, and their presented fields and pictures (the second run)
   equal the displayed fields. A DATABASE frame and the empty frames next
   to one are section 8's.

8. DATABASE (00214020; STATUS_FIELD.md section 3g; the same run as 6): a
   bound page frame whose calls after the background (0020A7A0's 513
   calls, or 514 while its layer 2 burst runs: background_calls) open with
   002131B0's blend 0 and first sprite is DATABASE's; an empty one is the
   page of the bound frame next to it. Its frames form three visits (runs
   of consecutive frames, the side run's order): the first hub visit (the
   list, right, Cross on a blank entry, Circle), the key 0x32 take's (its
   record and list) and the last hub visit, whose steps are those of the
   original's visit (fork state slot14_status_hub, capture.py
   CAPTURE_INPUTS: the stick down, Cross, Cross on id 0's record, Cross,
   right, up at the top (the scroll, 00213CC0's two SCISSOR_1 packets),
   Cross on a blank entry, Circle; every list of loop tops 33..182 by
   capture_lists.py). Every page of the first visit after the background
   is a page of the original's visit, and the last visit's distinct pages
   are exactly the original's; the take's frames by structure only (no
   capture). Every DATABASE frame is presented as a GS field and every
   one with packets opens with the background and 002131B0's packets; the
   empty ones (Circle, the take's first frame) present the displayed field
   of the original's Circle frame (f0182, no slot-1 packet). The
   written-background pairs (the list, the record, the scroll, the blank
   entry's state 3) as in 7: every call, the written bytes, the presented
   field and picture.

About 16 s (the game run; EM_TEST_FULL=1 about 2.5 to 5.5 min: three more runs; --item alone, sections 6 to 8, about 2 min); --dump FILE --run-log FILE reuse a run.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from reference_mode import FULL, MODE  # noqa: E402
import test_ui_packet_reference as t  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/status_gs_page'
BIN = ROOT / 'build/extermination'
BLOCKS = 0x814220
EXACT = [('01_battery', 300), ('01_battery', 400)]
BACKGROUND = 514          # 0020A7A0: 00207D00 then 513 sprites
NOTICE_START = 219        # route 01: the ITEM root's state 5 step 3 (the notice) from f219
UNTIL = 'panel' if FULL else 'battery'   # EM_TEST_FULL=1: on to route 03's panel BATTERY page
BATTERY_BEATS = ('01_battery', '03_panel_power')
ROOT_REST = ('slot12_status_root_pad8080', 40)
ROOT_INJECTED = [('slot12_status_root_pad8080_bg3467', 40), ('slot12_status_root_pad8080_bg3467', 41)]
LIT = 0x805FFF6E          # 0020F2A0's lit region colour
# the ITEM children (section 7): the open pages, their written-background
# pairs (D_002655A0 of port frame N; frames N + 1, N + 2) and the Circle frame
PAGES_REST = {'EQUIPMENT': ('slot12_status_root_equipment', 60), 'EVENT': ('slot12_status_root_event', 62)}
PAGES_INJECTED = {'EQUIPMENT': [('slot12_status_root_equipment_bg3421', 60), ('slot12_status_root_equipment_bg3421', 61)],
                  'EVENT': [('slot12_status_root_event_bg3511', 62), ('slot12_status_root_event_bg3511', 63)]}
PAGES_CIRCLE = ('slot12_status_root_equipment_circle', 72)
# DATABASE (section 8): the original's hub visit (slot14_status_hub, capture.py
# CAPTURE_INPUTS: the stick down, Cross, Cross on id 0 (its record), Cross,
# right, up (the scroll), Cross on a blank entry, Circle), every list of it
# (the light captures lists/fTTTT.bin, DB_VISIT), its Circle frame, and the
# written-background pairs (D_002655A0 of port frame N; frames N + 1, N + 2)
DB_BEAT = 'slot14_status_hub_database'
DB_BASE = (DB_BEAT, 45)            # a full capture of the same session (the fonts, the RAM under the lists)
DB_VISIT = (33, 182)               # loop top 33: the page opens (state 0); 182: Circle (no slot-1 packet)
DB_CIRCLE = (DB_BEAT, 182)
DB_INJECTED = {'list': [(DB_BEAT + '_bg6989', 44), (DB_BEAT + '_bg6989', 45)],
               'record': [(DB_BEAT + '_bg7019', 70), (DB_BEAT + '_bg7019', 71)],
               'scroll': [(DB_BEAT + '_bg7095', 128), (DB_BEAT + '_bg7095', 129)],
               'blank': [(DB_BEAT + '_bg7109', 156), (DB_BEAT + '_bg7109', 157)]}

FAILS = t.FAILS


def fail(msg: str) -> None:
    FAILS.append(msg)
    print(f'  FAIL: {msg}')


def run_game(dump: Path, until: str = UNTIL, extra: dict | None = None, tag: str = 'run') -> None:
    env = dict(os.environ, EM_UNCAPPED='1', EM_STARTUP_TEST='newgame-level', EM_LEVEL_SMOKE_UNTIL=until,
               EM_STATUS_GS_DUMP=str(dump), EM_AREA_CHANGE_LOG=str(OUT / 'ticks.jsonl'),
               EM_PRESENT_LOG=str(OUT / 'present.log'))
    env.update(extra or {})
    log = OUT / f'{tag}.log'
    with log.open('w') as f:
        rc = subprocess.run([str(BIN)], cwd=ROOT, env=env, stdout=f, stderr=subprocess.STDOUT).returncode
    text = log.read_text(errors='replace')
    if rc != 0 or 'level smoke: PASS' not in text:
        tail = '\n'.join(text.splitlines()[-8:])
        raise SystemExit(f'test_status_gs_page: FAIL (the game run ended {rc}):\n{tail}')


def fnv64(b: bytes) -> int:
    """em_gfx_metal.m's fnv64 (EM_PRESENT_LOG's field hash): 8-byte words, then the tail bytes."""
    h, m = 0xCBF29CE484222325, (1 << 64) - 1
    w = np.frombuffer(b[:len(b) // 8 * 8], '<u8')
    for v in w.tolist():
        h = ((h ^ v) * 0x100000001B3) & m
    for c in b[len(b) // 8 * 8:]:
        h = ((h ^ c) * 0x100000001B3) & m
    return h


def present_log(path: Path) -> dict[int, tuple]:
    """{tag (the frame counter): (kind, FRAME_1, XYOFFSET_1, field hash)} of the committed frames."""
    out = {}
    for line in path.read_text().splitlines():
        p = line.split()
        if len(p) >= 3:
            out[int(p[1])] = (p[2], int(p[3], 16), int(p[4], 16), int(p[5], 16)) if p[2] == 'field' else (p[2],)
    return out


def check_clut(lib, rlib, caps) -> int:
    """001CCB10's CLUT kick against the captures' GS block 0x1B67."""
    import ctypes as C
    lib.em_uip_001CCB10_chain.restype = C.c_size_t
    lib.em_uip_001CCB10_chain.argtypes = [C.c_void_p, C.c_size_t]
    kicks, kick = 0x1B80 - 0x1B00 + 2, 16 + 0x170
    buf = (C.c_uint8 * (kicks * kick))()
    if lib.em_uip_001CCB10_chain(C.addressof(buf), len(buf)) != len(buf):
        fail('001CCB10: em_uip_001CCB10_chain wrote no chain')
        return 0
    chain = bytes(buf)
    blocks = []
    for k in range(kicks):
        tag = struct.unpack_from('<Q', chain, k * kick)[0]
        bb = struct.unpack_from('<Q', chain, k * kick + 16 + 0x20)[0]
        if tag & 0xFFFF != 0x17 or (tag >> 28) & 7 != (7 if k == kicks - 1 else 1):
            fail(f'001CCB10: kick {k} is not behind a 0x17-quadword CNT / END tag')
        blocks.append((bb >> 32) & 0x3FFF)
    if blocks != list(range(0x1B00, 0x1B81)) + [0x1B67]:
        fail('001CCB10: the kicks are not blocks 0x1B00..0x1B80, then 0x1B67')
    entries = np.frombuffer(chain[(kicks - 1) * kick + 16 + 0x70:kicks * kick], '<u4')
    words = np.zeros(64, dtype=np.uint32)
    rlib.sf_map32(0x1B67, 1, 8, 8, words.ctypes.data)
    equal = 0
    for key, c in caps.items():
        vram = np.frombuffer((c['d'] / 'vram_before.bin').read_bytes(), '<u4')
        if (vram[words] == entries).all():
            equal += 1
        else:
            fail(f'{key[0]} f{key[1]:04d}: GS block 0x1B67 is not 001CCB10\'s CLUT')
    return equal


def records(dump: Path, backgrounds: dict | None = None) -> list[tuple[int, int, int, int, bytes]]:
    """The dump's records (em_status_gs_page.h); flag 4's D_002655A0 bytes
    go to `backgrounds` by frame."""
    d = dump.read_bytes()
    out, i = [], 0
    while i < len(d):
        if d[i:i + 4] != b'SGPF':
            raise SystemExit(f'test_status_gs_page: FAIL (dump record at {i} has no SGPF head)')
        fc, st, en, fl = struct.unpack_from('<4I', d, i + 4)
        i += 20
        out.append((fc, st, en, fl, d[i:i + en - st]))
        i += en - st
        if fl & 4:
            if backgrounds is not None:
                backgrounds[fc] = d[i:i + 0x60]
            i += 0x60
    return out


def capture(beat: str, T: int) -> dict:
    d = t.CAP / beat / f'f{T:04d}'
    info = json.loads((d / 'capture.json').read_text())
    ram = (d / 'ee_V_T1.bin').read_bytes()
    first, end = t.find_chain(ram, int(info['full']['list_start'], 16))
    nibs = struct.unpack_from('<16H', ram, t.NIBBLES)
    inv = {v: i for i, v in enumerate(nibs)}
    tall = struct.unpack_from('<I', ram, t.FONT_TALL_PTR)[0] & 0x1FFFFFF
    small = struct.unpack_from('<I', ram, t.FONT_SMALL_PTR)[0] & 0x1FFFFFF
    fonts = (ram[tall:tall + t.FONT_TALL_SIZE], ram[small:small + t.FONT_SMALL_SIZE])
    blocks = struct.unpack_from('<I', ram, t.GS_BLOCKS_PTR)[0]
    calls = t.decode(ram, first + 0x30, end - 0x30, blocks, *fonts, inv)
    return dict(d=d, info=info, ram=ram, first=first, end=end, inv=inv, fonts=fonts, blocks=blocks,
                calls=[(w, a) for _, w, a in calls])


def written_mask(lib, ram: bytes, context: int, start: int, port: bytes, calls) -> np.ndarray:
    """The bytes the native builders write when they replay `calls` from
    `start` (two fills; a byte both runs set to the same value is written)."""
    n = t.Native(lib, ram, context, BLOCKS)
    cell = context + 0x14
    builds = []
    for fill in (0xA5, 0x5A):
        buf = bytearray([fill]) * len(port)
        cbuf = bytearray(struct.pack('<I', start))
        for idx, (at, which, args) in enumerate(calls):
            if struct.unpack('<I', cbuf)[0] != at:
                raise RuntimeError(f'call {idx}: cursor {struct.unpack("<I", cbuf)[0]:#x}, packet at {at:#x}')
            rc, why = n.call(which, args, [(cell, cbuf), (start, buf)])
            if rc != 0:
                raise RuntimeError(f'call {idx} ({t.NAME[which]}) refused: {why}')
        builds.append(np.frombuffer(bytes(buf), np.uint8))
    return builds[0] == builds[1], builds[0]


def light(base: dict, beat: str, T: int) -> list:
    """The calls of a light list capture (build/status_field_capture/database/
    capture_lists.py: lists/fTTTT.bin, the list's 16 KB slot and its slot-1
    chain at loop top T) over the RAM of a full capture of the same session."""
    raw = (t.CAP / beat / 'lists' / f'f{T:04d}.bin').read_bytes()
    if raw[:4] != b'SFLL':
        raise SystemExit(f'test_status_gs_page: FAIL ({beat} lists f{T:04d} has no SFLL head)')
    start, first, end, _ = struct.unpack_from('<4I', raw, 4)
    ram = bytearray(base['ram'])
    ram[start:start + 0x4000] = raw[20:20 + 0x4000]
    ram[first:end] = raw[20 + 0x4000:]
    f2, e2 = t.find_chain(ram, start)
    if (f2, e2) != (first, end):
        raise SystemExit(f'test_status_gs_page: FAIL ({beat} lists f{T:04d}: the chain is not where it was read)')
    calls = t.decode(ram, first + 0x30, end - 0x30, base['blocks'], *base['fonts'], base['inv'])
    return [(w, a) for _, w, a in calls]


def background_calls(cs: list) -> int:
    """The number of 0020A7A0's calls a frame opens with: 00207D00(1, 0),
    layers 0 and 1's 512 sprites on the page's tile TEX0, then layer 2's one
    sprite on it only while its burst runs (em_status_background.h; the
    original-instruction oracle test_status_background_reference). 0: the
    frame does not open with the background."""
    if len(cs) < 513 or cs[0] != (t.B_7D00, (1, 0)) or cs[1][0] != t.B_7E40:
        return 0
    tex0 = cs[1][1][6]
    if any(w != t.B_7E40 or a[6] != tex0 or a[3:5] != (0x100, 0x80) for w, a in cs[1:513]):
        return 0
    return 514 if len(cs) > 513 and cs[513][0] == t.B_7E40 and cs[513][1][6] == tex0 else 513


def database(cs: list) -> bool:
    """A DATABASE (00214020) frame: after the background, 002131B0's blend 0
    and its first frame sprite (0x7000, 0x7BA0, 256 x 128; EQUIPMENT's and
    EVENT's plates start at 0x7B40)."""
    n = background_calls(cs)
    return (n > 0 and len(cs) > n + 1 and cs[n] == (t.B_7D00, (1, 0)) and cs[n + 1][0] == t.B_7E40
            and cs[n + 1][1][1:5] == (0x7000, 0x7BA0, 0x100, 0x80))


def status_pages(lib, rlib, report: dict) -> None:
    """Sections 6 and 7 of the docstring: one status_pages run, then one
    more with frame captures at the frames whose pictures are compared."""
    dump = OUT / 'item_chains.bin'
    run_game(dump, 'status_pages', tag='run_item')
    bgs: dict[int, bytes] = {}
    recs = records(dump, bgs)
    ref = capture(*ROOT_REST)
    context = struct.unpack_from('<I', ref['ram'], t.CONTEXT_PTR)[0]
    img = bytearray(0x490000)
    port, shaped = [], 0
    for fc, st, en, fl, b in recs:
        img[st:en] = b
        try:
            calls = t.decode(img, st, en, BLOCKS, *ref['fonts'], ref['inv'], skewed=True)
        except RuntimeError as e:
            fail(f'6: frame {fc}: {e}')
            continue
        shaped += sum(1 for _, w, a in calls if a is None)
        port.append((fc, st, en, fl, b, calls))
    print(f'  6: status_pages run: {len(port)} of {len(recs)} frames decode completely ({shaped} calls by shape)')
    plog = present_log(OUT / 'present.log')
    shots = item_root(lib, port, bgs, plog, ref, context, report)
    shots += item_pages(lib, port, bgs, plog, ref, context, report)
    shots += database_page(lib, port, bgs, plog, ref, context, report)
    # the presented pictures, the overlay pass included
    import test_fb2_pixels as F
    lines = [json.loads(x) for x in (OUT / 'ticks.jsonl').open()]
    tick_of = {x['counter']: x['tick'] for x in lines if 'tick' in x}
    spec = []
    for section, where, fc, c, slot in shots:
        spec.append(f'{tick_of[fc]}:{OUT / f"present_item_{fc}.bmp"}')
    if shots:
        run_game(OUT / 'item_chains2.bin', 'status_pages', {'EM_FB_CAPTURE_TICKS': ';'.join(spec)}, 'run_item2')
        (OUT / 'item_chains2.bin').unlink(missing_ok=True)
    for section, where, fc, c, slot in shots:
        shot = OUT / f'present_item_{fc}.bmp'
        shown, _ = F.shown_field(F.read_bmp(shot), shot)
        disp = np.frombuffer((c['d'] / 'displayed_field_T2.rgba').read_bytes(), np.uint8).reshape(224, 512, 4)
        off = int((shown[..., :3] != disp[..., :3]).any(axis=2).sum())
        print(f'  {section}: {where}: the presented picture at every field pixel\'s place: {off} of '
              f'{512 * 224} pixels off the displayed field')
        if off:
            fail(f'{section}: {where}: the presented picture differs from the displayed field in {off} pixels')
        slot[where]['pixels_off'] = off
        for ext in ('', '.gsfield', '.present'):
            Path(str(shot) + ext).unlink(missing_ok=True)
    dump.unlink(missing_ok=True)


def injected(lib, ref, context, plog, section: str, keys, frames, by_fc, res: dict):
    """The captures `keys` (CAPTURE_WRITE of D_002655A0) against port frames
    `frames`: every call, the written bytes, the presented field hash."""
    caps = [capture(*k) for k in keys]
    out = []
    for (beat, T), c, fc in zip(keys, caps, frames):
        where = f'{beat} f{T:04d}'
        _, st, en, fl, b, calls = by_fc[fc]
        if [(w2, a) for _, w2, a in calls] != c['calls']:
            fail(f'{section}: {where}: port frame {fc} is not the captured chain call for call')
            continue
        mask, built = written_mask(lib, ref['ram'], context, st, b, calls)
        got = np.frombuffer(b, np.uint8)
        capb = np.frombuffer(c['ram'][c['first'] + 0x30:c['end'] - 0x30], np.uint8)
        off_port, off_cap = int((mask & (built != got)).sum()), int((mask & (got != capb)).sum())
        if off_port or off_cap:
            fail(f'{section}: {where}: frame {fc}: {off_port} written bytes off the builders\', {off_cap} off the '
                 f'capture')
        disp = (c['d'] / 'displayed_field_T2.rgba').read_bytes()
        got_f = plog.get(fc, ('none',))
        field_ok = got_f[0] == 'field' and got_f[3] == fnv64(disp)
        if not field_ok:
            fail(f'{section}: {where}: port frame {fc}\'s presented field is not the displayed field')
        print(f'  {section}: {where} = port frame {fc}: every call ({len(calls)}), the written bytes '
              f'({int(mask.sum())}) equal the builders\' and the capture\'s; presented field equal to the displayed '
              f'field: {field_ok}')
        res[where] = {'frame': fc, 'written': int(mask.sum()), 'field_equal': field_ok}
        out.append((section, where, fc, c, res))
    return out


def written_bg(section: str, caps0: dict, bgs: dict) -> int | None:
    """The port frame N whose D_002655A0 (before its 0020A7A0) the capture
    wrote at loop top T - 1 (exactly one)."""
    w = caps0['info'].get('write') or {}
    if int(w.get('addr', '0'), 16) != 0x2655A0:
        fail(f'{section}: {caps0["d"]}: its capture wrote no D_002655A0')
        return None
    state = bytes.fromhex(w['bytes'])
    hits = [fc for fc, b in bgs.items() if b == state]
    if len(hits) != 1:
        fail(f'{section}: the port frames holding the written D_002655A0 are {hits}, not one')
        return None
    return hits[0]


def item_root(lib, port, bgs, plog, ref, context, report: dict) -> list:
    """Section 6 of the docstring."""
    items = [p for p in port if p[3] & 2]
    print(f'  6: {len(items)} ITEM root frames')
    if not items:
        fail('6: no ITEM root frame was dumped')
        return []
    # the structure
    head = [t.B_7E40, t.B_7E40, t.B_7D00, t.B_7E40, t.B_7E40, t.B_7D00] + [t.B_1D66A0] * 16 + [t.B_7D00]
    glyphs = (t.B_CC8A0, t.B_CC3B0, t.B_CBC20)
    at_rest = equal_rest = 0
    want_rest = ref['calls']
    for fc, st, en, fl, b, calls in items:
        ws = [w for _, w, _ in calls]
        page = ws[BACKGROUND:]
        cut = next((i for i, w in enumerate(page) if w in glyphs), len(page))
        lit = any(a is not None and w == t.B_7E40 and a[5] == LIT for _, w, a in calls[BACKGROUND:])
        tail = [t.B_7E40] * (11 if lit else 10) + [t.B_7D00, t.B_7E40]
        if (ws[:1] != [t.B_7D00] or ws[1:BACKGROUND] != [t.B_7E40] * (BACKGROUND - 1) or
                page[:cut] != head + tail or any(w not in glyphs for w in page[cut:])):
            fail(f'6: frame {fc}: not the background, then 0020F170 and 0020F2A0\'s packets')
            continue
        if not lit and all(a is not None for _, _, a in calls):
            at_rest += 1
            if [(w, a) for _, w, a in calls][BACKGROUND:] == want_rest[BACKGROUND:]:
                equal_rest += 1
            else:
                fail(f'6: frame {fc}: at rest with hover 0, but not the captured ITEM root after the background')
    print(f'  6: every ITEM root frame is the background, 0020F170\'s and 0020F2A0\'s packets; {equal_rest} of '
          f'{at_rest} at rest with hover 0 equal {ROOT_REST[0]} f{ROOT_REST[1]:04d} after the background')
    res = {'frames': len(items), 'at_rest_hover0': at_rest, 'equal_after_background': equal_rest, 'injected': {}}
    report['item_root'] = res
    # the captures with the port's D_002655A0
    first = capture(*ROOT_INJECTED[0])
    if (first['info'].get('write') or {}).get('at_loop_top') != ROOT_INJECTED[0][1] - 1:
        fail(f'6: {ROOT_INJECTED[0][0]}: its capture wrote no D_002655A0 at loop top {ROOT_INJECTED[0][1] - 1}')
        return []
    n0 = written_bg('6', first, bgs)
    by_fc = {p[0]: p for p in items}
    if n0 is None or n0 + 1 not in by_fc or n0 + 2 not in by_fc:
        fail(f'6: the port frame holding the written D_002655A0 is {n0}, not an ITEM root frame N with N + 1 '
             f'and N + 2 ITEM root frames')
        return []
    kinds: dict[str, int] = {}
    for fc in by_fc:
        kinds[plog.get(fc, ('none',))[0]] = kinds.get(plog.get(fc, ('none',))[0], 0) + 1
    if kinds != {'field': len(by_fc)}:
        fail(f'6: the ITEM root frames are presented as {kinds}, not all as GS fields')
    res['presented'] = kinds
    print(f'  6: N = {n0} holds the written D_002655A0')
    return injected(lib, ref, context, plog, '6', ROOT_INJECTED, (n0 + 1, n0 + 2), by_fc, res['injected'])


def page_of(port) -> dict[int, str]:
    """{frame: 'database' or 'item'} for the bound page frames (flag 8): a
    DATABASE frame by its calls (database()); an empty one is the page of
    the bound frame just before it (a Circle frame leaving the page) or,
    with none there, of the one just after it (the first frame of a page a
    take opens: DATABASE's state 0 with the request returns before any
    leaf)."""
    bound = {fc: [(w, a) for _, w, a in calls] for fc, st, en, fl, b, calls in port if fl & 8}
    kind = {fc: ('database' if database(cs) else 'item') for fc, cs in bound.items() if cs}
    out = {}
    for fc, cs in bound.items():
        out[fc] = kind.get(fc) or kind.get(fc - 1) or kind.get(fc + 1) or 'item'
    return out


def item_pages(lib, port, bgs, plog, ref, context, report: dict) -> list:
    """Section 7 of the docstring."""
    pages = [p for p in port if p[3] & 8 and page_of(port).get(p[0]) == 'item']
    res: dict = {'frames': len(pages), 'injected': {}}
    report['item_pages'] = res
    print(f'  7: {len(pages)} EQUIPMENT / EVENT frames')
    if not pages:
        fail('7: no EQUIPMENT / EVENT frame was dumped')
        return []
    rest = {name: capture(*key)['calls'] for name, key in PAGES_REST.items()}
    circle = capture(*PAGES_CIRCLE)
    if circle['calls']:
        fail(f'7: {PAGES_CIRCLE[0]} f{PAGES_CIRCLE[1]:04d}: the original\'s Circle frame builds '
             f'{len(circle["calls"])} slot-1 packets, not none')
    counts = {name: 0 for name in rest}
    empty = []
    disp_circle = fnv64((circle['d'] / 'displayed_field_T2.rgba').read_bytes())
    for fc, st, en, fl, b, calls in pages:
        cs = [(w, a) for _, w, a in calls]
        if not cs:
            empty.append(fc)
            got = plog.get(fc, ('none',))
            if got[0] != 'field' or got[3] != disp_circle:
                fail(f'7: frame {fc}: an empty page frame whose presented field is not the original\'s Circle frame')
            continue
        if cs[:1] != [(t.B_7D00, (1, 0))] or [w for w, _ in cs[1:BACKGROUND]] != [t.B_7E40] * (BACKGROUND - 1):
            fail(f'7: frame {fc}: does not open with 0020A7A0\'s 00207D00 and 513 sprites')
            continue
        name = next((n for n, want in rest.items() if cs[BACKGROUND:] == want[BACKGROUND:]), None)
        if name is None:
            fail(f'7: frame {fc}: not the captured EQUIPMENT or EVENT page after the background')
            continue
        counts[name] += 1
    kinds: dict[str, int] = {}
    for p in pages:
        k = plog.get(p[0], ('none',))[0]
        kinds[k] = kinds.get(k, 0) + 1
    if kinds != {'field': len(pages)}:
        fail(f'7: the EQUIPMENT / EVENT frames are presented as {kinds}, not all as GS fields')
    if not all(counts.values()) or not empty:
        fail(f'7: the run showed EQUIPMENT / EVENT / the Circle frame {counts} / {len(empty)} times')
    print(f'  7: every one presented as a GS field; equal to the captured page after the background: {counts}; '
          f'{len(empty)} empty (the Circle frames {empty}), each presenting the field of {PAGES_CIRCLE[0]} '
          f'f{PAGES_CIRCLE[1]:04d}')
    res.update({'presented': kinds, 'equal_after_background': counts, 'circle_frames': empty})
    by_fc = {p[0]: p for p in pages}
    where = f'{PAGES_CIRCLE[0]} f{PAGES_CIRCLE[1]:04d}'
    res['injected'][where] = {'frame': empty[0] if empty else None}
    shots = [('7', where, empty[0], circle, res['injected'])] if empty else []
    for name, keys in PAGES_INJECTED.items():
        first = capture(*keys[0])
        if (first['info'].get('write') or {}).get('at_loop_top') != keys[0][1] - 1:
            fail(f'7: {keys[0][0]}: its capture wrote no D_002655A0 at loop top {keys[0][1] - 1}')
            continue
        n0 = written_bg('7', first, bgs)
        if n0 is None or n0 + 1 not in by_fc or n0 + 2 not in by_fc:
            fail(f'7: {name}: the port frame holding the written D_002655A0 is {n0}, not a page frame N with '
                 f'N + 1 and N + 2 page frames')
            continue
        print(f'  7: {name}: N = {n0} holds the written D_002655A0')
        shots += injected(lib, ref, context, plog, '7', keys, (n0 + 1, n0 + 2), by_fc, res['injected'])
    return shots


def database_page(lib, port, bgs, plog, ref, context, report: dict) -> list:
    """Section 8 of the docstring."""
    which = page_of(port)
    frames = [p for p in port if which.get(p[0]) == 'database']
    res: dict = {'frames': len(frames), 'injected': {}}
    report['database'] = res
    print(f'  8: {len(frames)} DATABASE frames')
    if not frames:
        fail('8: no DATABASE frame was dumped')
        return []
    base = capture(*DB_BASE)
    visit_o = {T: light(base, DB_BEAT, T) for T in range(DB_VISIT[0], DB_VISIT[1] + 1)}
    circle = capture(*DB_CIRCLE)
    if circle['calls'] or visit_o[DB_VISIT[1]]:
        fail(f'8: {DB_CIRCLE[0]} f{DB_CIRCLE[1]:04d}: the original\'s Circle frame builds slot-1 packets')
    if visit_o[DB_BASE[1]] != base['calls']:
        fail(f'8: the light capture f{DB_BASE[1]:04d} is not the full capture of the same frame')
    # the visits: runs of consecutive DATABASE frames (the side run's order:
    # the first hub visit, the key 0x32 take's, the last hub visit, whose
    # steps are the original's visit's)
    visits: list[list] = []
    for p in frames:
        if visits and p[0] == visits[-1][-1][0] + 1:
            visits[-1].append(p)
        else:
            visits.append([p])
    if len(visits) < 2:
        fail(f'8: {len(visits)} DATABASE visits, not the first and the last hub visit')
        return []
    after = lambda cs: tuple(cs[background_calls(cs):])
    o_pages = {after(cs) for T, cs in visit_o.items() if cs}

    def pages_of(visit):
        out: dict = {}
        for fc, st, en, fl, b, calls in visit:
            if calls:
                out.setdefault(after([(w, a) for _, w, a in calls]), []).append(fc)
        return out
    first_p, last_p = pages_of(visits[0]), pages_of(visits[-1])
    for name, pp in (('first', first_p), ('last', last_p)):
        unmatched = [fcs for k, fcs in pp.items() if k not in o_pages]
        if unmatched:
            fail(f'8: frames of the {name} hub visit equal no frame of the original\'s visit after the background: '
                 f'{unmatched}')
    missing = [T for T, cs in visit_o.items() if cs and after(cs) not in last_p]
    if missing:
        fail(f'8: frames of the original\'s visit no frame of the last hub visit equals after the background: '
             f'{missing}')
    if not all(database([(w, a) for _, w, a in calls]) for *_, calls in frames if calls):
        fail('8: a DATABASE frame does not open with the background and 002131B0\'s first packets')
    kinds: dict[str, int] = {}
    for p in frames:
        k = plog.get(p[0], ('none',))[0]
        kinds[k] = kinds.get(k, 0) + 1
    if kinds != {'field': len(frames)}:
        fail(f'8: the DATABASE frames are presented as {kinds}, not all as GS fields')
    empty = [p[0] for p in frames if not p[5]]
    disp_circle = fnv64((circle['d'] / 'displayed_field_T2.rgba').read_bytes())
    bad = [fc for fc in empty if plog.get(fc, ('none',))[0] != 'field' or plog[fc][3] != disp_circle]
    if bad:
        fail(f'8: empty DATABASE frames {bad} do not present the field of {DB_CIRCLE[0]} f{DB_CIRCLE[1]:04d}')
    rest = sum(1 for v in visits[1:-1] for p in v if p[5])
    circles = [p[0] for v in (visits[0], visits[-1]) for p in v if not p[5]]
    opening = [p[0] for v in visits[1:-1] for p in v if not p[5]]
    span = lambda v: f'{v[0][0]}..{v[-1][0]}'
    print(f'  8: the first hub visit ({span(visits[0])}): its {len(first_p)} distinct pages after the background '
          f'are pages of the original\'s visit; the last ({span(visits[-1])}): its {len(last_p)} are exactly the '
          f'{len(o_pages)} of the original\'s visit (loop tops {DB_VISIT[0]}..{DB_VISIT[1]}, every list); every '
          f'frame presented as a GS field; {len(empty)} empty (the Circle frames {circles}, the take\'s first '
          f'frame {opening}) presenting the field of {DB_CIRCLE[0]} f{DB_CIRCLE[1]:04d}; {rest} more (the key '
          f'0x32 take\'s record and list, {", ".join(span(v) for v in visits[1:-1])}) by structure only')
    res.update({'visits': [span(v) for v in visits], 'first_distinct_pages': len(first_p),
                'last_distinct_pages': len(last_p), 'original_distinct_pages': len(o_pages), 'presented': kinds,
                'circle_frames': circles, 'take_first_frames': opening, 'by_structure': rest})
    by_fc = {p[0]: p for p in frames}
    where = f'{DB_CIRCLE[0]} f{DB_CIRCLE[1]:04d}'
    res['injected'][where] = {'frame': empty[0] if empty else None}
    shots = [('8', where, empty[0], circle, res['injected'])] if empty else []
    for name, keys in DB_INJECTED.items():
        first = capture(*keys[0])
        if (first['info'].get('write') or {}).get('at_loop_top') != keys[0][1] - 1:
            fail(f'8: {keys[0][0]}: its capture wrote no D_002655A0 at loop top {keys[0][1] - 1}')
            continue
        n0 = written_bg('8', first, bgs)
        if n0 is None or n0 + 1 not in by_fc or n0 + 2 not in by_fc:
            fail(f'8: {name}: the port frame holding the written D_002655A0 is {n0}, not a DATABASE frame N with '
                 f'N + 1 and N + 2 DATABASE frames')
            continue
        print(f'  8: {name}: N = {n0} holds the written D_002655A0')
        shots += injected(lib, ref, context, plog, '8', keys, (n0 + 1, n0 + 2), by_fc, res['injected'])
    return shots


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--dump', type=Path, help='reuse an EM_STATUS_GS_DUMP file instead of running the game')
    ap.add_argument('--run-log', type=Path, help='the reused run\'s log (its hub_draws count)')
    ap.add_argument('--item', action='store_true',
                    help='sections 6 to 8 (the status_pages run: the ITEM root, EQUIPMENT, EVENT and DATABASE) only')
    args = ap.parse_args()
    if args.item:
        OUT.mkdir(parents=True, exist_ok=True)
        t0 = time.monotonic()
        report = {'mode': MODE}
        status_pages(t.build(), t.sf.build(), report)
        for name in ('ticks.jsonl', 'present.log'):
            (OUT / name).unlink(missing_ok=True)
        (OUT / 'result_item.json').write_text(json.dumps(report, indent=2) + '\n')
        dt = time.monotonic() - t0
        if FAILS:
            print(f'test_status_gs_page --item: FAIL ({len(FAILS)} failures; {dt:.1f} s)')
            return 1
        print(f'test_status_gs_page --item: PASS ({dt:.1f} s)')
        return 0
    if (args.dump is None) != (args.run_log is None):
        ap.error('--dump and --run-log go together')
    t0 = time.monotonic()
    missing = [b for b, T in t.FRAMES if not (t.CAP / b / f'f{T:04d}' / 'capture.json').exists()]
    if missing:
        print(f'test_status_gs_page: REFUSED (no capture of {missing[0]}; decomp '
              f'build/status_field_capture/capture.py)')
        return 2
    OUT.mkdir(parents=True, exist_ok=True)
    dump = args.dump
    if dump is None:
        if not BIN.exists():
            print('test_status_gs_page: REFUSED (no build/extermination: make all)')
            return 2
        dump = OUT / 'chains.bin'
        run_game(dump)
    log = (args.run_log or OUT / 'run.log').read_text(errors='replace')
    m = re.search(r'level smoke: status: PASS .*hub_draws=(\d+)', log)
    if not m:
        print('test_status_gs_page: REFUSED (the run log has no status hub_draws count)')
        return 2
    hub_draws = int(m.group(1))
    recs = records(dump)
    caps = {key: capture(*key) for key in t.FRAMES
            if key[0] == '01_battery' or (UNTIL == 'panel' and key[0] in BATTERY_BEATS)}
    ref = next(iter(caps.values()))
    lib, rlib = t.build(), t.sf.build()
    context = struct.unpack_from('<I', ref['ram'], t.CONTEXT_PTR)[0]
    print(f'mode {MODE}: {len(recs)} dumped frames ({sum(1 for r in recs if r[3] & 1)} with the BATTERY page)')

    # 1. every frame decodes
    img = bytearray(0x490000)
    port = []
    skewed = 0
    kinds_page: dict[str, int] = {}
    for fc, st, en, fl, b in recs:
        img[st:en] = b
        try:
            calls = t.decode(img, st, en, BLOCKS, *ref['fonts'], ref['inv'], skewed=True)
        except RuntimeError as e:
            fail(f'frame {fc}: {e}')
            continue
        skewed += sum(1 for _, w, a in calls if a is None)
        port.append((fc, st, en, fl, b, calls))
    print(f'  1: {len(port)} of {len(recs)} frames decode completely into the builders\' packets '
          f'({skewed} skewed glyph strips by shape)')

    # 2. the BATTERY frames' structure
    pages = [p for p in port if p[3] & 1]
    shapes: dict[str, int] = {}
    for fc, st, en, fl, b, calls in pages:
        names = [t.NAME[w] for _, w, _ in calls]
        if names[:1] != ['00207D00'] or names[1:BACKGROUND] != ['00207E40'] * (BACKGROUND - 1):
            fail(f'frame {fc}: the page does not open with 0020A7A0\'s 00207D00 and 513 sprites')
            continue
        shape = 'with tall glyph runs' if '001CC3B0' in names else 'without'
        shapes[shape] = shapes.get(shape, 0) + 1
        for n in names:
            kinds_page[n] = kinds_page.get(n, 0) + 1
    print(f'  2: {len(pages)} BATTERY frames open with the background (00207D00, 513 sprites): {shapes}')
    if not pages:
        fail('no BATTERY page frame was dumped')

    # 3. the captured frames
    report = {'mode': MODE, 'frames': len(recs), 'pages': len(pages), 'shapes': shapes, 'matched': {}}
    matched = {}
    first_page = pages[0][0] if pages else 0
    for key, c in caps.items():
        want = c['calls']
        exact = [p for p in pages if [(w, a) for _, w, a in p[5]] == want]
        rest = [p for p in pages if [(w, a) for _, w, a in p[5]][BACKGROUND:] == want[BACKGROUND:]]
        where = f'{key[0]} f{key[1]:04d}'
        route01 = key[0] == '01_battery'
        at = first_page + (key[1] - NOTICE_START) - hub_draws if route01 else None
        if key in EXACT and [p[0] for p in exact] != [at]:
            fail(f'{where}: the port frames that decode into the captured chain\'s {len(want)} calls are '
                 f'{[p[0] for p in exact]}, not {at} (first BATTERY frame {first_page} + {key[1] - NOTICE_START} '
                 f'- {hub_draws} hub frames)')
            continue
        if route01 and key not in EXACT and at >= first_page:
            fail(f'{where}: by the counts its port frame {at} is a BATTERY frame: it must match exactly')
        if not rest:
            fail(f'{where}: no port frame equals the captured page after the background')
            continue
        p = exact[0] if exact else rest[0]
        fc, st, en, fl, b, calls = p
        mask, built = written_mask(lib, ref['ram'], context, st, b, calls)
        got = np.frombuffer(b, np.uint8)
        capb = np.frombuffer(c['ram'][c['first'] + 0x30:c['end'] - 0x30], np.uint8)
        if len(capb) != len(got):
            fail(f'{where}: port frame {fc} holds {len(got)} bytes, the capture {len(capb)}')
            continue
        off_port = int((mask & (built != got)).sum())
        bg = sum(0x10 if w == t.B_7D00 else 0x80 for w, _ in c['calls'][:BACKGROUND])
        span = slice(0, None) if exact else slice(bg, None)
        off_cap = int((mask[span] & (got[span] != capb[span])).sum())
        if off_port:
            fail(f'{where}: frame {fc}: {off_port} written bytes differ from the native builders\'')
        if off_cap:
            fail(f'{where}: frame {fc}: {off_cap} written bytes differ from the capture')
        kind = ('every call' if exact else
                f'every call after the background ({len(want) - BACKGROUND}; ' +
                (f'its own frame {at} precedes the page)' if route01 else 'the background below)'))
        print(f'  3: {where} = port frame {fc} ({len(exact)} such frames): {kind}; the written bytes '
              f'({int(mask[span].sum())}) equal the builders\' and the capture\'s')
        matched[key] = (p, bool(exact))
        report['matched'][where] = {'frame': fc, 'exact': bool(exact), 'written': int(mask[span].sum())}
    # route 03: the background equals the port's at the step count of its own
    # history (D_002655A0 is shared and never reset): one port BATTERY frame
    # holds each captured background exactly, and between f440 and f500 the
    # port stepped it as often as the original (one per page frame)
    k03 = [k for k in caps if k[0] == '03_panel_power']
    if k03:
        hits = {}
        for key in k03:
            bgc = caps[key]['calls'][:BACKGROUND]
            hits[key] = [p[0] for p in pages if [(w, a) for _, w, a in p[5][:BACKGROUND]] == bgc]
            if len(hits[key]) != 1:
                fail(f'{key[0]} f{key[1]:04d}: {len(hits[key])} port frames hold its background exactly')
        if all(len(h) == 1 for h in hits.values()) and len(k03) == 2:
            a, b = hits[k03[0]][0], hits[k03[1]][0]
            steps = sum(1 for p in pages if a < p[0] <= b)
            if steps != k03[1][1] - k03[0][1]:
                fail(f'route 03: the port steps 0020A7A0 {steps} times between the frames holding f440\'s and '
                     f'f500\'s backgrounds, the original {k03[1][1] - k03[0][1]}')
            else:
                print(f'  3: route 03\'s backgrounds equal the port\'s at frames {a} and {b} (its history: the '
                      f'shared D_002655A0), {steps} page frames apart as f440..f500')
            report['route03_background'] = {'f440': a, 'f500': b, 'steps': steps}
    if all(k in matched for k in EXACT):
        print(f'  3: f300 and f400 at the port frames the counts give: first BATTERY frame {first_page} + '
              f'(T - {NOTICE_START}) - {hub_draws} hub frames of the START visit')

    # 4. drawn
    for key in (EXACT if FULL else EXACT[:1]):
        if key not in matched:
            continue
        c, (p, _) = caps[key], matched[key]
        chain = c['ram'][c['first']:c['first'] + 0x30] + p[4] + c['ram'][c['end'] - 0x30:c['end']]
        r = t.raster_check(rlib, c['d'], c['info'], c['ram'], c['first'], chain,
                           f'{key[0]} f{key[1]:04d} (port frame {p[0]})')
        report['matched'][f'{key[0]} f{key[1]:04d}']['raster'] = r

    # 5. the GS field
    plog_path = OUT / 'present.log'
    if args.dump is None:
        plog = present_log(plog_path)
        bat = {p[0] for p in pages}
        kinds: dict[str, int] = {}
        for fc in bat:
            kinds[plog.get(fc, ('none',))[0]] = kinds.get(plog.get(fc, ('none',))[0], 0) + 1
        if kinds != {'field': len(bat)}:
            fail(f'5: the BATTERY frames are presented as {kinds}, not all as GS fields')
        hashed = {}
        for key in EXACT:
            if key not in matched:
                continue
            fc = matched[key][0][0]
            disp = (caps[key]['d'] / 'displayed_field_T2.rgba').read_bytes()
            got = plog.get(fc, ('none',))
            ok = got[0] == 'field' and got[3] == fnv64(disp)
            hashed[f'{key[0]} f{key[1]:04d}'] = {'frame': fc, 'equal': ok}
            if not ok:
                fail(f'5: {key[0]} f{key[1]:04d}: port frame {fc}\'s presented field is not the displayed field')
        clut = check_clut(lib, rlib, caps)
        same = ', '.join(f'{k} (port frame {v["frame"]})' for k, v in hashed.items() if v['equal'])
        print(f'  5: {kinds.get("field", 0)} of {len(bat)} BATTERY frames presented as GS fields; the fields of '
              f'{same} equal the displayed field; 001CCB10\'s CLUT equals GS block 0x1B67 in {clut} captures')
        report['field'] = {'presented': kinds, 'equal': hashed, 'clut_captures': clut}
        if FULL and all(k in matched for k in EXACT):
            # the presented pictures: frame captures at those frames, the overlay pass included
            import test_fb2_pixels as F
            lines = [json.loads(x) for x in (OUT / 'ticks.jsonl').open()]
            tick_of = {x['counter']: x['tick'] for x in lines if 'tick' in x}
            spec, shots = [], {}
            for key in EXACT:
                fc = matched[key][0][0]
                shot = OUT / f'present_{key[1]:04d}.bmp'
                spec.append(f'{tick_of[fc]}:{shot}')
                shots[key] = shot
            run_game(OUT / 'chains2.bin', 'battery', {'EM_FB_CAPTURE_TICKS': ';'.join(spec)}, 'run2')
            (OUT / 'chains2.bin').unlink(missing_ok=True)
            for key, shot in shots.items():
                port = F.read_bmp(shot)
                shown, _ = F.shown_field(port, shot)
                disp = np.frombuffer((caps[key]['d'] / 'displayed_field_T2.rgba').read_bytes(),
                                     np.uint8).reshape(224, 512, 4)
                off = int((shown[..., :3] != disp[..., :3]).any(axis=2).sum())
                print(f'  5: {key[0]} f{key[1]:04d}: the presented picture at every field pixel\'s place: '
                      f'{off} of {512 * 224} pixels off the displayed field')
                if off:
                    fail(f'5: {key[0]} f{key[1]:04d}: the presented picture differs from the displayed field '
                         f'in {off} pixels')
                for ext in ('', '.gsfield', '.present'):
                    Path(str(shot) + ext).unlink(missing_ok=True)
    (OUT / 'ticks.jsonl').unlink(missing_ok=True)
    plog_path.unlink(missing_ok=True)
    if FULL and args.dump is None:
        status_pages(lib, rlib, report)
        (OUT / 'ticks.jsonl').unlink(missing_ok=True)
        plog_path.unlink(missing_ok=True)

    (OUT / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    dt = time.monotonic() - t0
    if args.dump is None:
        dump.unlink(missing_ok=True)   # about 40 MB; result.json keeps the summary
    if FAILS:
        print(f'test_status_gs_page: FAIL ({len(FAILS)} failures; {dt:.1f} s)')
        return 1
    print(f'test_status_gs_page: PASS ({len(recs)} live frames decode into the builders\' packets; route 01 '
          f'f300 / f400 built byte for byte by the live page and presented as the displayed field'
          f'{"; the ITEM root, EQUIPMENT, EVENT and DATABASE (sections 6 to 8)" if FULL and args.dump is None else ""}; '
          f'{dt:.1f} s)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
