#!/usr/bin/env python3
"""The level smoke's check of the static world and the background channel
(001C1D00 on the render context, em_static_world_live; docs/STATIC_WORLD.md
section 7, docs/LEVEL_SMOKE.md "The static world"). Called by
tools/test_level_smoke.py after the phases.

The tick log's "static" (em_scene_bindings.c) carries the live draw's log
(the last run's start / end, the FNV-1a of its bytes and of its triangles,
its batches, triangles and culled vertices) and the number of 001C1D00
calls; "static_sample" carries, for every 400th 001C1D00 call, that call's
inputs (the render context, the scratchpad, D_00810610, D_00810700..702,
D_008101D0, D_00253560, D_00817240 and the skin records before it ran) and
its output (the channel-0 run, the channel-3 list, D_00253560 after). It
checks:
- every tick that ran 001C1D00 drew its run in the same tick (the draw
  count follows the call count one for one), and every drawn run drew
  triangles;
- sampled calls (default: the first two; EM_TEST_FULL=1: all): the ORIGINAL
  001C1D00, executed over a route snapshot with the port's inputs laid over
  it, writes exactly the port's channel-0 run (every byte it writes, and
  the cursor after it; a tag's bytes +2 and +8..+15 are never written and
  keep the arena's earlier bytes on both sides), the port's channel-3 list
  and D_00253560..EF; and the run,
  replayed with the ORIGINAL VU1 microcode of the level and clip kernels
  (tools/test_static_world_draw_reference.py), draws exactly the triangles
  the port drew (their FNV-1a, the live draw's digest of that tick);
- the aligned route snapshots whose frame view equals the port's (context
  +0x2380 and the zoom): the port's run has the capture's own run's length,
  and its triangles are the ones the capture's run draws through the
  original microcode (their FNV-1a). A main-line run that reached Roger
  must have compared VIEW_EXACT_MAIN_LINE (beats 10 and 14, the snapshots
  whose view the port reproduces bit for bit): losing one fails the check,
  so a camera regression cannot silently drop the capture-exact comparison.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import reference_mode as RM  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
ROUTE = ROOT.parent / 'Extermination/build/s87/route'
CTX, CTX_SIZE = 0x811CC0, 0x2540
SPAD_3A40, SKIN = 0x70003A40, 0x816440
BASE_BEAT = '05_boxes'   # the snapshot the samples are laid over
# The route snapshots whose frame view (context +0x2380 and the zoom) the
# main-line run reproduces exactly (C8 static world: 10 with 2457 and 14
# with 847 triangles); tools/test_level_smoke.py requires them once the run
# reaches Roger.
VIEW_EXACT_MAIN_LINE = ('10_cage_roof_roger', '14_roger_encounter')


def fnv(h, w):
    for k in range(4):
        h ^= (w >> (8 * k)) & 0xFF
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def run_digest(data):
    """em_static_world_live.c: FNV-1a over the run's little-endian words."""
    h = 2166136261
    for i in range(0, len(data) - 3, 4):
        h = fnv(h, struct.unpack_from('<I', data, i)[0])
    return h


def prim_digest(tris):
    """em_static_world_live.c prim_digest() over (prim, tex0, vertices)."""
    h = 2166136261
    for prim, tex0, verts in tris:
        h = fnv(h, prim)
        h = fnv(h, tex0 & 0xFFFFFFFF)
        h = fnv(h, tex0 >> 32)
        for x, y, z, f, rgba, s, t, q in verts:
            h = fnv(h, x)
            h = fnv(h, y)
            h = fnv(h, z)
            h = fnv(h, f)
            h = fnv(h, rgba[0] | rgba[1] << 8 | rgba[2] << 16 | rgba[3] << 24)
            h = fnv(h, s)
            h = fnv(h, t)
            h = fnv(h, q)
    return h


def run_tags(ram, start, end):
    """The run's DMA tags as (address, id, qwc, target) for kernel_replay."""
    out, a = [], start
    while a < end:
        w0, addr = struct.unpack_from('<II', ram, a)
        tid, qwc = (w0 >> 28) & 7, w0 & 0xFFFF
        assert tid in (1, 3, 5), ('static world: a DMA tag outside the run\'s forms', hex(a), hex(w0))
        out.append((a, tid, qwc, addr))
        a += 16 * (qwc + 1) if tid == 1 else 16
    assert a == end, ('static world: the run does not end on a tag', hex(a), hex(end))
    return out


def original_draw(ram, start, end):
    """The triangles the run draws through the ORIGINAL VU1 microcode (the
    reference interpreter) and the GS walk of the draw reference test."""
    import test_shadow_original_reference as sh
    import test_static_world_draw_reference as dr
    from test_player_slide_reference import read_elf
    elf = read_elf()
    kicks = []
    for kernel, top, _b, _k, _s, events in sh.kernel_replay(elf, ram, run_tags(ram, start, end)):
        kicks += [(kernel, top, e[3]) for e in events if e[0] == 'kick']
    return dr.gs_triangles(kicks)


def replay_sample(item):
    """The original 001C1D00 over the base snapshot with the sample's inputs
    laid over it; then the draw of the run it wrote."""
    tick, sm = item
    import test_render_context_reference as rcref
    import test_static_world_reference as swr
    from test_player_slide_reference import read_elf
    elf = read_elf()
    rcref.ELF = elf
    swr.ELF = elf
    ram = (ROUTE / BASE_BEAT / 'eeMemory.bin').read_bytes()
    spad = (ROUTE / BASE_BEAT / 'scratchpad.bin').read_bytes()
    (runs, ctx, sp, cam, area, st, d253560, d817240, skin, ch0_start, run, ch3_start, ch3, d253560_post) = sm
    ee = swr.SwEE(elf, ram, spad)
    for address, data in ((CTX, ctx), (SPAD_3A40, sp), (0x810610, cam), (0x810700, area), (0x8101D0, st),
                          (0x253560, d253560), (0x817240, d817240), (SKIN, skin)):
        ee.write(address, data)
    mark = len(ee.dirty)
    rc = ee.call_entry(0x1C1D00, [0x8101D0])
    written = set()
    for kind, at, size in ee.dirty[mark:]:
        if kind == 'ram':
            written.update(range(at, at + size))
    where = ('static world', 'port tick', tick, '001C1D00 call', runs)

    def same(label, start, data):
        """Every byte the original wrote in [start, start + len) equals the
        port's; the ones it leaves (a tag's +2 and +8..+15, never written:
        they keep the arena's earlier bytes) are the only ones not
        compared."""
        skipped = 0
        for k in range(len(data)):
            if start + k in written:
                assert ee.mem[start + k] == data[k], (where, label, 'differs from the original at byte', k)
            else:
                assert k % 16 == 2 or k % 16 >= 8, (where, label, 'the original leaves byte', k, 'unwritten')
                skipped += 1
        return skipped

    end = struct.unpack_from('<I', ee.mem, CTX + 0x10)[0]
    assert end == ch0_start + len(run), (where, 'the original run ends at', hex(end), 'the port\'s at',
                                         hex(ch0_start + len(run)))
    unwritten = same('channel-0 run', ch0_start, run)
    head = struct.unpack_from('<I', ee.mem, CTX + 0x1D8)[0]
    end3 = struct.unpack_from('<I', ee.mem, CTX + 0x1C)[0]
    assert head == ch3_start and end3 == ch3_start + len(ch3), (where, 'channel-3 list start / end differ')
    unwritten += same('channel-3 list', ch3_start, ch3)
    assert bytes(ee.mem[0x253560:0x2535F0]) == d253560_post, (where, 'D_00253560..EF differ')
    tris = original_draw(bytes(ee.mem), ch0_start, end)
    return dict(tick=tick, runs=runs, rc=rc, bytes=len(run), ch3=len(ch3), triangles=len(tris),
                digest=prim_digest(tris), run_digest=run_digest(run), unwritten=unwritten)


def capture_run(beat):
    """The capture's own static run and the triangles it draws."""
    import test_static_world_draw_reference as dr
    ram = (ROUTE / beat / 'eeMemory.bin').read_bytes()
    start, end, _tags = dr.static_run(ram)
    tris = original_draw(ram, start, end)
    return ram[CTX + 0x2380:CTX + 0x23C0], ram[CTX + 0x2468:CTX + 0x246C], end - start, \
        prim_digest(tris), len(tris)


def check_static_world(ticks, state, require_exact=()):
    rows = [(i, t) for i, t in enumerate(ticks) if t.get('static')]
    assert rows, 'static world: no tick of the run carries the static log'
    prev_calls = prev_draws = None
    ran = drawn = 0
    last_draw = {}
    for i, t in rows:
        (frame, draws, start, end, dig, pdig, b0, b1, t0, t1, culled, calls) = t['static']
        where = ('static world', 'port tick', t['tick'])
        if prev_calls is not None and calls != prev_calls:
            assert calls == prev_calls + 1, (where, '001C1D00 ran more than once in a tick', prev_calls, calls)
            assert draws == prev_draws + 1, (where, 'a 001C1D00 run was not drawn in its tick', draws, prev_draws)
            assert t0 + t1 > 0 and b0 > 0, (where, 'a drawn run drew no triangle', b0, t0, t1)
            ran += 1
            drawn += t0 + t1
        prev_calls, prev_draws = calls, draws
        last_draw[i] = (start, end, dig, pdig, t0 + t1)
    assert ran >= 100, ('static world: too few 001C1D00 runs checked', ran)
    samples = [(t['tick'], [x if isinstance(x, int) else bytes.fromhex(x) for x in t['static_sample']], i)
               for i, t in rows if t.get('static_sample')]
    assert samples, 'static world: no sampled 001C1D00 call'
    chosen = samples if RM.FULL else samples[:2]
    results = RM.parallel_map(replay_sample, [(tick, sm) for tick, sm, _ in chosen])
    for (tick, sm, i), r in zip(chosen, results):
        start, end, dig, pdig, count = last_draw[i]
        where = ('static world', 'port tick', tick)
        assert (start, end) == (sm[9], sm[9] + len(sm[10])), (where, 'the drawn run is not the sampled call\'s')
        assert dig == r['run_digest'], (where, 'the drawn run\'s bytes differ from the sampled run\'s')
        assert pdig == r['digest'] and count == r['triangles'], (
            where, 'the port\'s triangles differ from the original microcode\'s', count, r['triangles'])
    exact, compared = [], set()
    for beat, i in state.get('snapshots', []):
        t = ticks[i]
        rctx = t.get('rctx')
        if not rctx or not t.get('static') or i not in last_draw:
            continue
        v, zoom, size, digest, ntris = capture_run(beat)
        if bytes.fromhex(rctx[4]) != v or bytes.fromhex(rctx[3]) != zoom:
            continue
        start, end, dig, pdig, count = last_draw[i]
        assert end - start == size, ('static world', beat, 'the port\'s run length differs from the capture\'s')
        assert pdig == digest and count == ntris, (
            'static world', beat, 'the port\'s triangles differ from the ones the capture\'s own run draws')
        exact.append(f'{beat[:2]} ({count} triangles)')
        compared.add(beat)
    missing = [b for b in require_exact if b not in compared]
    assert not missing, ('static world: the view-exact route snapshots were not compared (the port\'s view '
                         'no longer equals the capture\'s, or the snapshot is missing)', missing)
    print(f'static world: PASS ({ran} 001C1D00 runs, each drawn in its tick, {drawn} triangles; '
          f'{len(chosen)} of {len(samples)} sampled calls re-executed: the original 001C1D00 over the port\'s '
          f'inputs wrote the port\'s channel-0 run, channel-3 list and D_00253560 byte for byte, and the '
          f'original microcode drew the port\'s triangles '
          f'({", ".join(str(r["triangles"]) for r in results)}); view-exact route snapshots: '
          f'{", ".join(exact) if exact else "none"})')
