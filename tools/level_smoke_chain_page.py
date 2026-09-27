#!/usr/bin/env python3
"""The level smoke's check of the chain page drawn live (WP-13,
em_chain_page_live; docs/CHAIN_PAGE.md, docs/LEVEL_SMOKE.md "The chain
page"). Called by tools/test_level_smoke.py after the phases.

The tick log's "page" (em_scene_bindings.c) carries, for the page drawn at
the tick's frame close: the counts em_chain_page returned, the digest of the
primitives it handed the renderer, the glow markers' primitives and, on
sampled pages, every (address, bytes) the walk read. It checks:
- every drawn page: the only CALL walked over is this frame's 001DDE10
  four-sprite CALL; the lane program ran 0 or 6 times, 6 in exactly as
  many pages as the barrel (001F0360) ran frames, and no lane drew (no
  tristrip: no route slot is active);
- sampled pages (the first, then every 250th, at most 40): the ORIGINAL VU1
  microcode, the DMA, VIF and GIF walk and the GS vertex queue
  (tools/chain_page_model.py) over the port's own page bytes draw exactly
  the primitives the port drew (the digest) with the same counts, and the
  blend presets the page REFs (001D0F20's bank, D_00275674 + 0x6A0) hold
  the route captures' bytes;
- the aligned route snapshots whose camera equals the capture's (10, 14):
  the glow markers (001CD520's sprites) the port drew equal the ones the
  capture's own page draws (the original microcode and GS walk over the
  capture's latest page), vertex for vertex; the colour is not compared (it
  follows rand(), 001F4D40), nor the Q of a vertex that takes the frame's Q
  on either side.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import chain_page_model as M  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
ROUTE = ROOT.parent / 'Extermination/build/s87/route'
MARKER_TEX0 = 0x00045B0599421EF0
CLD_MASK = ~(7 << 61) & (2 ** 64 - 1)
VIEW_EXACT = ('10_cage_roof_roger', '14_roger_encounter')
PRESETS, PRESETS_SIZE = 0x814220 + 0x6A0, 0x500
STATE_BITS = ((0, 0x01), (1, 0x02), (2, 0x04), (3, 0x08), (4, 0x10), (5, 0x20))


def fnv(h, w):
    for k in range(4):
        h ^= (w >> (8 * k)) & 0xFF
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def digest(prims):
    """em_chain_page_live.c digest() over model primitives."""
    h = 2166136261
    for prim, state, verts in prims:
        setbits = 0
        vals = []
        for idx, bit in STATE_BITS:
            v = state[idx]
            if v is not None:
                setbits |= bit
            vals.append(v or 0)
        h = fnv(h, prim)
        h = fnv(h, setbits)
        for v in vals:
            h = fnv(h, v & 0xFFFFFFFF)
            h = fnv(h, v >> 32)
        h = fnv(h, len(verts))
        for x, y, z, f, rgba, q, s, t, u, v, qk in verts:
            for w in (x, y, z, f or 0, 1 if f is not None else 0,
                      rgba[0] | rgba[1] << 8 | rgba[2] << 16 | rgba[3] << 24, q, s, t, u, v, qk):
                h = fnv(h, w)
    return h


def markers_of(prims):
    out = []
    for prim, state, verts in prims:
        if prim & 7 != 6 or state[0] is None or state[0] & CLD_MASK != MARKER_TEX0:
            continue
        a, b = verts
        out.append([a[0], a[1], b[0], b[1], b[2], b[3], a[6], a[7], a[5] if a[10] else 0, a[10],
                    b[6], b[7], b[5] if b[10] else 0, b[10]])
    return out


def same_markers(port, orig):
    if len(port) != len(orig):
        return False
    for p, o in zip(port, orig):
        keep = [0, 1, 2, 3, 4, 5, 6, 7, 10, 11]
        if [p[k] for k in keep] != [o[k] for k in keep]:
            return False
        if p[9] and o[9] and p[8] != o[8]:
            return False
        if p[13] and o[13] and p[12] != o[12]:
            return False
    return True


def capture_page(beat):
    ram = (ROUTE / beat / 'eeMemory.bin').read_bytes()
    start = M.latest_start(ram)
    probe = M.Page(M.ram_reader(ram))
    probe.dma(start)
    skip = {a for (_c, tid, _q, a) in probe.transfers if tid == 5 and M.ARENA <= a < 0x800000}
    page = M.Page(M.ram_reader(ram), skip_calls=skip)
    page.run(start)
    return page, ram


def rewalk(p):
    """The model over the port's dumped page bytes."""
    mem = {}
    for address, data in p[18]:
        assert data is not None, ('chain page: a sampled read has no bytes', address)
        mem[address] = bytes.fromhex(data)

    def read(a, n):
        for base, b in mem.items():
            if base <= a and a + n <= base + len(b):
                return b[a - base:a - base + n]
        raise M.ModelError(f'the sampled page has no bytes at {a:#x}+{n:#x}')

    page = M.Page(read, skip_calls=[p[3]] if p[3] else [])
    page.run(p[2])
    return page, read


def check_chain_page(ticks, state):
    rows = [t for t in ticks if t.get('page')]
    assert rows, 'chain page: no tick of the run carries the page'
    drawn = [t for t in rows if t['page'][0] == 1]
    assert drawn, 'chain page: no page was drawn'
    types, sampled, presets, stale, skipped = [0] * 8, 0, 0, 0, 0
    banks = {}
    for q in sorted(ROUTE.iterdir()):
        if (q / 'eeMemory.bin').exists() and q.name[:2].isdigit() and int(q.name[:2]) <= 14:
            with (q / 'eeMemory.bin').open('rb') as f:
                f.seek(PRESETS)
                banks[q.name] = f.read(PRESETS_SIZE)
    for t in drawn:
        p = t['page']
        (_now, _pages, start, four, _tr, _qw, direct, ml, ms, kicks, prims, by_type, skip, stale_q,
         _cyc, decal, dig, markers, sample) = p
        where = ('chain page', 'tick', t['tick'])
        assert skip == (1 if four else 0), (where, 'CALLs walked over', skip, four)
        assert ml in (0, 6), (where, 'lane MSCALs', ml)
        for k in range(8):
            types[k] += by_type[k]
        stale += stale_q
        skipped += skip
        if sample is None:
            continue
        page, read = rewalk(p)
        want = (digest(page.gs.prims), len(page.gs.prims), len(page.directs), len(page.kicks))
        got = (dig, prims, direct, kicks)
        assert want == got, (where, 'the original walk over the port\'s page draws', want, 'the port', got)
        sampled += 1
        for address, _data in sample:
            if PRESETS <= address < PRESETS + PRESETS_SIZE:
                b = read(address, 0x80)
                for beat, bank in banks.items():
                    o = address - PRESETS
                    assert bank[o:o + 0x80] == b, (where, 'blend preset', hex(address), beat)
                presets += 1
                break
    assert sampled, 'chain page: no page was sampled'
    lanes = sum(1 for t in drawn if t['page'][7] == 6)
    fx = [t for t in ticks if t.get('effects')]
    barrel = fx[-1]['effects'][0][4] if fx else 0
    assert lanes == barrel, ('chain page: pages with the six lane MSCALs', lanes, 'barrel frames', barrel)
    assert types[4] == 0, ('chain page: a lane drew (no route slot is active)', types[4])
    exact = []
    for beat, i in state.get('snapshots', []):
        if beat not in VIEW_EXACT:
            continue
        p = ticks[i].get('page')
        assert p and p[0] == 1, ('chain page', beat, 'no page drawn at the aligned tick')
        page, _ram = capture_page(beat)
        orig = markers_of(page.gs.prims)
        assert same_markers(p[17], orig), ('chain page', beat, 'glow markers', p[17], orig)
        exact.append(f'{beat[:2]} ({len(orig)} glow marker(s))')
    print(f'chain page: PASS ({len(drawn)} pages drawn ({lanes} with the barrel\'s six lane MSCALs): '
          f'{types[6]} sprites, {types[5] + types[4]} triangles, '
          f'{types[2]} lines; {skipped} 001DDE10 CALLs walked over; {stale} vertices with the frame\'s Q; '
          f'{sampled} sampled pages re-walked with the original microcode equal the port\'s primitives, '
          f'{presets} of them with a blend preset equal to the route captures\'; aligned: '
          f'{", ".join(exact) if exact else "none camera-exact"})')
