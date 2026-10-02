#!/usr/bin/env python3
"""The level smoke's check of the chain page drawn live (WP-13,
em_chain_page_live; docs/CHAIN_PAGE.md, docs/LEVEL_SMOKE.md "The chain
page"). Called by tools/test_level_smoke.py after the phases.

The tick log's "page" (em_scene_bindings.c) carries, for the page drawn at
the tick's frame close: the counts em_chain_page returned, the digest of the
primitives it handed the renderer, the glow markers' primitives, on sampled
pages every (address, bytes) the walk read, then the snow program's MSCALs,
the weather list 001E0D70 CALLed and the reads of the flame's descriptor.
Its "snow" and "flame" records carry the weather's last closed channel-3
list (em_snow_runtime) and the flame's last 001D04B0 (em_effects_live). It
checks:
- every drawn page: the only CALL walked over is this frame's 001DDE10
  four-sprite CALL; the lane program ran 0 or 6 times, 6 in exactly as
  many pages as the barrel (001F0360) ran frames, and no lane drew (no
  tristrip: no route slot is active);
- the weather (001E55F0): a page holds the weather's CALL exactly when the
  weather closed a list in that frame, at that list's start (context
  +0x2520, which 001E0D70 CALLs), and then runs its 108 tile MSCALs of the
  snow program D_00233800 (none otherwise); every tile of a list carries the
  same packet 3;
- the flame (008235F0): a page reads the flame's descriptor D_00828340
  exactly once when the flame's 001D04B0 ran in that frame, and never
  otherwise;
- sampled pages (the first, then every 250th, at most 40): the ORIGINAL VU1
  microcode, the DMA, VIF and GIF walk and the GS vertex queue
  (tools/chain_page_model.py) over the port's own page bytes draw exactly
  the primitives the port drew (the digest) with the same counts (the
  weather's CALL is walked: the ORIGINAL snow program runs every tile);
  the blend presets the page REFs (001D0F20's bank, D_00275674 + 0x6A0),
  the three program packets and the flame's descriptor the page reads hold
  the route captures' bytes;
- the aligned route snapshots whose camera equals the capture's (10, 14):
  the glow markers (001CD520's sprites) the port drew equal the ones the
  capture's own page draws (the original microcode and GS walk over the
  capture's latest page), vertex for vertex; the colour is not compared (it
  follows rand(), 001F4D40); the Q is (each GIF tag's, 1.0 at the tag's
  start on both sides: docs/GS_EXACT.md 2.1). There too the weather's tile packet 3 (P, the clip
  projection, K, the fog 001E67C0's 0021B9A0 left, the depth bias and the
  GIF tag row) equals the capture's weather list's, and the flame's
  001CFBE0 packet 4 (the same rows for the sprite program) and packet 1
  (the owner's matrix and the MSCAL; its phase and seed words, which follow
  the owner's age and rand(), are masked) equal the capture's flame packets.
"""
from __future__ import annotations

import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import chain_page_model as M  # noqa: E402
from reference_mode import parallel_map  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
ROUTE = ROOT.parent / 'Extermination/build/s87/route'
MARKER_TEX0 = 0x00045B0599421EF0
CLD_MASK = ~(7 << 61) & (2 ** 64 - 1)
VIEW_EXACT = ('10_cage_roof_roger', '14_roger_encounter')
PRESETS, PRESETS_SIZE = 0x814220 + 0x6A0, 0x500
FLAME_DESCRIPTOR = 0x828340
PROGRAM_PACKETS = ((0x231770, 0xDD0), (0x233290, 0x570), (0x233800, 0xDE0), (0x230800, 0xF70), (0x232540, 0xD50))
SNOW_TILES = 108
STATE_BITS = ((0, 0x01), (1, 0x02), (2, 0x04), (3, 0x08), (4, 0x10), (5, 0x20))


def fnv(h, w):
    for k in range(4):
        h ^= (w >> (8 * k)) & 0xFF
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def fnv_bytes(b):
    h = 2166136261
    for x in b:
        h = ((h ^ x) * 16777619) & 0xFFFFFFFF
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
    """The capture's latest page walked with the original microcode: every
    arena CALL of a producer the port does not run walked over, and the
    weather's list too (its snow is compared by its packets)."""
    ram = (ROUTE / beat / 'eeMemory.bin').read_bytes()
    start = M.latest_start(ram)
    skip = M.arena_skips(M.ram_reader(ram), start, weather=False)
    page = M.Page(M.ram_reader(ram), skip_calls=skip)
    page.run(start)
    return page, ram


def capture_packets(ram):
    """The capture's weather tile-0 packet 3 (the 0x100 bytes after its CNT
    tag) as FNV-1a, and the flame's 001CFBE0 packet 1 (0x70, phase and seed
    words masked) and packet 4 (0x100) as CRC-32."""
    read = M.ram_reader(ram)
    probe = M.Page(read)
    probe.dma(M.latest_start(ram))
    lists = sorted(M.weather_lists(read, probe.transfers))
    assert len(lists) == 1, ('chain page: the capture holds', len(lists), 'weather lists')
    p3 = read(lists[0] + 0x30, 0x100)
    tr = probe.transfers
    at = [k for k, t in enumerate(tr) if t[1] == 3 and t[3] == FLAME_DESCRIPTOR]
    assert len(at) == 1, ('chain page: the capture holds', len(at), 'flame descriptor REFs')
    k = at[0]
    p4 = [t for t in tr[:k] if t[2] == 16][-1]
    p1 = [t for t in tr[k + 1:] if t[2] == 7][0]
    b1 = bytearray(read(p1[0] + 16, 0x70))
    b1[0x10:0x14] = bytes(4)
    b1[0x1C:0x20] = bytes(4)
    return fnv_bytes(p3), zlib.crc32(bytes(b1)) & 0xFFFFFFFF, zlib.crc32(read(p4[0] + 16, 0x100)) & 0xFFFFFFFF


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

    # The class-2 object units (001CABA0's, CHAIN_PAGE.md section 6) are
    # walked over with the GS state they leave (the port logs their CALLs,
    # last TEX0 and PRIM): the primitives around them are compared
    # (test_object_unit_reference.py runs the units' own triangles).
    units = {u[0]: (u[3] | u[4] << 32, u[5]) for u in (p[22] if len(p) > 22 else [])}
    page = M.Page(read, skip_calls=[p[3]] if p[3] else [], units=units)
    page.run(p[2])
    return page, read


def rewalk_one(p):
    """One sampled page, in a forked worker: the model's digest and counts,
    and the bytes the page read of the checked original data."""
    page, read = rewalk(p)
    watched = []
    for address, data in p[18]:
        n = len(data) // 2
        if (PRESETS <= address < PRESETS + PRESETS_SIZE and n == 0x80) or \
                (address == FLAME_DESCRIPTOR and n == 0x90) or \
                any(a <= address and address + n <= a + size for a, size in PROGRAM_PACKETS):
            watched.append((address, read(address, n)))
    return ((digest(page.gs.prims), len(page.gs.prims), len(page.directs), len(page.kicks)),
            (page.mscal_counts.get(M.PROGRAM_SNOW, 0), page.mscal_counts.get(M.PROGRAM_STREAK, 0),
             page.mscal_counts.get(M.PROGRAM_KIND2, 0)), watched)


def check_chain_page(ticks, state):
    rows = [t for t in ticks if t.get('page')]
    assert rows, 'chain page: no tick of the run carries the page'
    drawn = [t for t in rows if t['page'][0] == 1]
    assert drawn, 'chain page: no page was drawn'
    types, presets, stale, skipped = [0] * 8, 0, 0, 0
    unit_calls = unit_prims = 0
    snow_pages = flame_pages = streak_pages = streak_tris = flare_pages = lane_strip_tris = 0
    direct_strip_tris = kind2_pages = kind2_lines = 0
    aim_tick = ticks[state['aim_from']]['tick'] if 'aim_from' in state else None
    route_ram = {}
    for q in sorted(ROUTE.iterdir()):
        if (q / 'eeMemory.bin').exists() and q.name[:2].isdigit() and int(q.name[:2]) <= 14:
            route_ram[q.name] = q / 'eeMemory.bin'

    def captured(address, size):
        out = {}
        for beat, path in route_ram.items():
            with path.open('rb') as f:
                f.seek(address)
                out[beat] = f.read(size)
        return out

    samples = []
    for t in drawn:
        p = t['page']
        (_now, _pages, start, four, _tr, _qw, direct, ml, ms, kicks, prims, by_type, skip, stale_q,
         _cyc, decal, dig, markers, sample, snow_mscals, weather, overlay_reads, units, _dig_units,
         streak_mscals, streak_prims, flare_sprites, lane_strips, kind2_mscals, kind2_prims, direct_strips) = p
        where = ('chain page', 'tick', t['tick'])
        assert skip == (1 if four else 0), (where, 'CALLs walked over', skip, four)
        assert ml in (0, 6), (where, 'lane MSCALs', ml)
        snow = t.get('snow')
        closed = bool(snow) and snow[0] == 1
        if closed:
            assert weather == snow[2] and snow[3] == SNOW_TILES and snow[5] == 1, \
                (where, 'the weather list the page CALLed', weather, 'closed', snow)
            assert snow_mscals == SNOW_TILES, (where, 'snow MSCALs', snow_mscals)
            snow_pages += 1
        else:
            assert weather == 0 and snow_mscals == 0, (where, 'a weather CALL without a closed list',
                                                       weather, snow_mscals)
        flame = t.get('flame')
        called = bool(flame) and flame[0] == 1
        assert overlay_reads == (1 if called else 0), (where, 'flame descriptor reads', overlay_reads,
                                                        'flame', flame)
        flame_pages += called
        for k in range(8):
            types[k] += by_type[k]
        # A class-2 unit's triangles: pass 0's strips (type 4) and the clip
        # pass's lists (type 3); not lanes.
        types[4] -= sum(u[2] for u in units)
        types[3] -= sum(u[1] - u[2] for u in units)
        # The streak program's quads (the impact effect 0x80000060): strip
        # triangles, two per particle, only on pages that ran it.
        assert streak_prims == 0 or streak_mscals, (where, 'streak primitives without its MSCAL', streak_prims)
        types[4] -= streak_prims
        # The lanes' strips: 001F0720's lanes draw an active ring-decal slot
        # (001F0460's, the shots' impact marks); no route beat fires, so
        # only a side run's ticks from its first shot on may hold one.
        assert lane_strips == 0 or (ml and aim_tick is not None and t['tick'] >= aim_tick), \
            (where, 'lane strips outside a side run\'s shots', lane_strips)
        types[4] -= lane_strips
        lane_strip_tris += lane_strips
        # DIRECT strips: the cable strand's strip packets (0021A500 ->
        # 001CE860) once the knife parted it; only on a side run's ticks.
        assert direct_strips == 0 or (aim_tick is not None and t['tick'] >= aim_tick), \
            (where, 'DIRECT strips outside a side run', direct_strips)
        types[4] -= direct_strips
        direct_strip_tris += direct_strips
        # The kind-2 program (0021AAC0's sparks): its primitives only on
        # pages that ran it.
        assert kind2_prims == 0 or kind2_mscals, (where, 'kind-2 primitives without its MSCAL', kind2_prims)
        kind2_pages += kind2_mscals > 0
        kind2_lines += kind2_prims
        streak_pages += streak_mscals > 0
        streak_tris += streak_prims
        flare_pages += flare_sprites > 0
        unit_calls += len(units)
        unit_prims += sum(u[1] for u in units)
        stale += stale_q
        assert stale_q == 0, ('chain page', 'a vertex without its GIF tag\'s Q', stale_q)
        skipped += skip
        if sample is not None:
            samples.append((t['tick'], p))
    assert samples, 'chain page: no page was sampled'
    results = parallel_map(rewalk_one, [p for _t, p in samples],
                           cost=lambda p: p[19])
    watched_checked = 0
    streak_sampled = flare_sampled = strip_sampled = kind2_sampled = 0
    for (tick, p), (want, (snow_mscals, streak_mscals, kind2_mscals), watched) in zip(samples, results):
        where = ('chain page', 'tick', tick)
        nunit = sum(u[1] for u in p[22])
        got = (p[23] if p[22] else p[16], p[10] - nunit, p[6], p[9])
        assert want == got, (where, 'the original walk over the port\'s page draws', want, 'the port', got)
        assert snow_mscals == p[19], (where, 'the original walk ran', snow_mscals, 'snow MSCALs; the port', p[19])
        assert streak_mscals == p[24], (where, 'the original walk ran', streak_mscals, 'streak MSCALs; the port',
                                        p[24])
        assert kind2_mscals == p[28], (where, 'the original walk ran', kind2_mscals, 'kind-2 MSCALs; the port',
                                       p[28])
        streak_sampled += streak_mscals > 0
        kind2_sampled += kind2_mscals > 0
        flare_sampled += p[26] > 0
        strip_sampled += p[27] > 0
        for address, b in watched:
            for beat, orig in captured(address, len(b)).items():
                assert orig == b, (where, 'page bytes read at', hex(address), 'differ from', beat)
            watched_checked += 1
            if PRESETS <= address < PRESETS + PRESETS_SIZE:
                presets += 1
    lanes = sum(1 for t in drawn if t['page'][7] == 6)
    fx = [t for t in ticks if t.get('effects')]
    barrel = fx[-1]['effects'][0][4] if fx else 0
    assert lanes == barrel, ('chain page: pages with the six lane MSCALs', lanes, 'barrel frames', barrel)
    assert types[4] == 0, ('chain page: strip triangles of no known source (no lane, unit, streak or strand)',
                           types[4])
    assert snow_pages and flame_pages, ('chain page: no page with the weather\'s list or the flame',
                                        snow_pages, flame_pages)
    exact = []
    for beat, i in state.get('snapshots', []):
        if beat not in VIEW_EXACT:
            continue
        p = ticks[i].get('page')
        assert p and p[0] == 1, ('chain page', beat, 'no page drawn at the aligned tick')
        page, ram = capture_page(beat)
        orig = markers_of(page.gs.prims)
        assert same_markers(p[17], orig), ('chain page', beat, 'glow markers', p[17], orig)
        snow, flame = ticks[i].get('snow'), ticks[i].get('flame')
        assert snow and snow[0] == 1 and flame and flame[0] == 1, \
            ('chain page', beat, 'no weather list or flame draw at the aligned tick', snow, flame)
        p3, f1, f4 = capture_packets(ram)
        assert snow[4] == p3, ('chain page', beat, 'the weather tile packet 3', snow[4], 'capture', p3)
        assert (flame[4], flame[5]) == (f1, f4), ('chain page', beat, 'the flame packets 1 / 4',
                                                  flame[4:6], 'capture', (f1, f4))
        exact.append(f'{beat[:2]} ({len(orig)} glow marker(s), the snow packet 3 and the flame packets 1 / 4)')
    print(f'chain page: PASS ({len(drawn)} pages drawn ({lanes} with the barrel\'s six lane MSCALs, '
          f'{snow_pages} with the weather\'s {SNOW_TILES} snow tiles, {flame_pages} with the flame): '
          f'{types[6]} sprites, {types[5] + types[4]} triangles, '
          f'{types[2]} lines; {streak_pages} page(s) with the streak program ({streak_tris} triangles, '
          f'{streak_sampled} re-walked with its original microcode); {flare_pages} page(s) with the gun '
          f'lamp\'s flare ({flare_sampled} re-walked); {lane_strip_tris} lane triangles of the shots\' ring '
          f'decals ({strip_sampled} page(s) re-walked); {kind2_pages} page(s) with the kind-2 program '
          f'({kind2_lines} primitives, {kind2_sampled} re-walked); {direct_strip_tris} DIRECT strip triangles '
          f'(the parted cable strand); '
          f'{skipped} 001DDE10 CALLs walked over; {unit_calls} class-2 object unit(s) '
          f'(001CABA0) with {unit_prims} triangles; every vertex with its GIF tag\'s Q; '
          f'{len(samples)} sampled pages re-walked with the original microcode equal the port\'s primitives '
          f'({watched_checked} reads of the presets, program packets and flame descriptor equal the route '
          f'captures\'); aligned: {", ".join(exact) if exact else "none camera-exact"})')
