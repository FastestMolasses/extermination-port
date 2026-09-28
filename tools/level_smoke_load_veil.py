#!/usr/bin/env python3
"""The level smoke's check of the load veil drawn live (docs/
LOAD_VEIL_PARTICLES.md section 4, docs/LEVEL_SMOKE.md "The load veil").
Called by tools/test_level_smoke.py after the phases.

The tick log's "veil_draw" (em_scene_bindings.c) carries, for each frame in
which 0021B1B0 drew and step V's list was drawn by the GS frame stage
(em_load_veil_live): the frame counter, the channel-0 run [start, end) and
its bytes, the buffer index, the kicked list, the displayed FRAME_1, the
walk's counts and the brightest line colour byte. For each such frame, with
the veil block of that tick (veil_pre / veil_post of the tick whose counter
is the frame's), it checks:
- the run: the ORIGINAL 0021B1B0 executed over the opening capture (the
  veil particles test's oracle), with the block as it stood at the call (the
  tick's pre phase and base Y, its post levels), context +0x9C = the slot and
  the channel-0 cursor = the run's start, writes exactly the run the port
  sent, byte for byte, except the fourth word of each strip ST quadword
  (001DFA40's table lane 3: stale stack words the GS ignores, section 5);
  the seed it leaves is the port's post +0x14, and the ORIGINAL 0021B500
  steps the pre phase to the port's post phase;
- the frame: the walk drew the clear, the 512 lines, the two copy sprites and
  the 900 strip triangles (1,415 primitives) into the slot's frame buffer
  (bank A's FRAME_1: 0x80038 for slot 0, 0x80000 for slot 1); a level-0
  veil drew only black lines;
- the block: the seed the port leaves equals every route capture's (the
  captures hold the veil block of the New Game load, finished).
It reports the phase the load left against the captures' (the host-speed
load draws the veil on fewer ticks than the PS2's disc: the policy of
docs/PORT_PROFILES.md).
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

ROOT = Path(__file__).resolve().parents[1]
ROUTE = ROOT.parent / 'Extermination/build/s87/route'
CTX = 0x811CC0
RUN_BYTES = 0x162B0
LINES_END = 0x10 + 512 * 0x70


def strip_lane3_words(run):
    """The two lens passes' 30 strip packets (a CNT of 0x42 quadwords whose
    first holds DIRECT 0x41: the GIF tag and 32 x (ST, XYZF2)), found by
    walking the run's tags from the end of the lines; returns the offsets of
    the ST quadwords' fourth word."""
    out, strips, at = set(), 0, LINES_END
    while at < len(run):
        qwc, ident = struct.unpack_from('<H', run, at)[0], run[at + 3]
        if ident == 0x10:                                   # CNT
            if qwc == 0x42 and struct.unpack_from('<I', run, at + 0x1C)[0] == 0x50000041:
                strips += 1
                for k in range(32):
                    out.add(at + 0x3C + 0x20 * k)
            at += 0x10 + 0x10 * qwc
        elif ident == 0x30:                                 # REF
            at += 0x10
        else:
            raise AssertionError(('load veil: the run holds an unexpected tag', hex(at), hex(ident)))
    assert at == len(run) and strips == 30, ('load veil: the lens passes do not hold 30 strips', strips)
    return out


def check_load_veil(ticks, state):
    import test_load_veil_particles_reference as LVP
    draws = [t for t in ticks if t.get('veil_draw')]
    assert draws, 'load veil: no veil frame was drawn (every load runs 0021B1B0)'
    by_counter = {}
    for t in ticks:
        by_counter.setdefault(t['counter'], t)
    elf = LVP.read_elf()
    ram0 = LVP.CAPTURE.read_bytes()
    veil = struct.unpack_from('<I', ram0, LVP.VEIL_PTR)[0] & (LVP.RAM_SIZE - 1)
    captured = {q.name: (q / 'eeMemory.bin').read_bytes()[0x8214C0:0x8214C0 + 0x1C]
                for q in sorted(ROUTE.iterdir()) if (q / 'eeMemory.bin').exists() and q.name[:2].isdigit() and
                int(q.name[:2]) <= 14}
    seeds = {b[0x14:0x18] for b in captured.values()}
    assert len(seeds) == 1, ('load veil: the captures disagree on the veil seed', seeds)
    cap_seed = seeds.pop()
    black = 0
    for t in draws:
        vd = t['veil_draw']
        src = by_counter.get(vd['frame'])
        assert src, ('load veil: no tick for the veil frame', vd['frame'])
        pre, post = bytes.fromhex(src['veil_pre']), bytes.fromhex(src['veil_post'])
        run = bytes.fromhex(vd['run'])
        assert vd['end'] - vd['start'] == len(run) == RUN_BYTES, ('load veil: run size', len(run))
        assert vd['slot'] in (0, 1), ('load veil: slot', vd['slot'])
        assert vd['prims'] == 1415 and vd['types'] == [0, 512, 0, 0, 900, 0, 3, 0], \
            ('load veil: the frame list did not draw the clear, 512 lines, 2 copies and 900 triangles', vd)
        assert vd['display'] == (0x80038 if vd['slot'] == 0 else 0x80000), ('load veil: displayed FRAME_1', vd)
        lane3 = strip_lane3_words(run)
        # The ORIGINAL 0021B1B0 at that call, over the port's run bytes.
        block = bytearray(pre)
        block[8:0x14] = post[8:0x14]
        ee = LVP.VeilEE(elf, bytearray(ram0))
        ee.write(veil, bytes(block))
        ee.write(vd['start'], run)
        ee.save(CTX + 0x9C, vd['slot'])
        ee.save(CTX + 0x10, vd['start'])
        ee.invoke(0x21B1B0, [veil])
        assert ee.load(CTX + 0x10) == vd['end'], ('load veil: the original run ends elsewhere', hex(ee.load(CTX + 0x10)))
        out = bytes(ee.read(vd['start'], RUN_BYTES))
        for w in range(0, RUN_BYTES, 4):
            if out[w:w + 4] != run[w:w + 4] and w not in lane3:
                raise AssertionError(('load veil: the port sent a word the original does not write',
                                      vd['frame'], hex(w), run[w:w + 4].hex(), out[w:w + 4].hex()))
        after = bytes(ee.read(veil, 0x1C))
        assert after[0x14:0x18] == post[0x14:0x18], ('load veil: seed', after[0x14:0x18].hex(), post[0x14:0x18].hex())
        ee.invoke(0x21B500, [veil])
        assert bytes(ee.read(veil + 4, 4)) == post[4:8], ('load veil: phase step', post[4:8].hex())
        assert post[0x14:0x18] == cap_seed, ('load veil: the seed differs from the captures', post[0x14:0x18].hex())
        if struct.unpack('<f', post[8:12])[0] == 0.0:
            assert vd['max_line_rgb'] == 0, ('load veil: a level-0 veil lit a line', vd['max_line_rgb'])
            black += 1
    last = bytes.fromhex(by_counter[draws[-1]['veil_draw']['frame']]['veil_post'])
    phase = struct.unpack('<f', last[4:8])[0]
    cap_phase = struct.unpack('<f', next(iter(captured.values()))[4:8])[0]
    print(f'level smoke: load veil: {len(draws)} veil frame(s) drawn ({black} at level 0: black), each run '
          f'byte-equal to the ORIGINAL 0021B1B0 at that call, its seed equal to the {len(captured)} captures\'; '
          f'the load left phase {phase:.6f} (the captures, after the PS2 disc load: {cap_phase:.6f})')
