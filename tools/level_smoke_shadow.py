#!/usr/bin/env python3
"""The level smoke's drop-shadow check (census L29 / L29b: em_shadow_live;
docs/LEVEL_SMOKE.md "The drop shadow", docs/SHADOW_ORIGINAL.md "Binding").

tools/test_level_smoke.py calls check_shadow(ticks, state) after the phase
checks. The tick log's "shadow" carries, per slot-0 tick,
  [post-step] = [fresh, D_008102B1, D_00810771, +0x214's record, route]
                (route: 0 none, 1 001DA6A0, 2 0015BF90, -1 reported: the
                player record does not hold the displayed pose),
  [call]      = the last shadow call (em_shadow_live_log): fresh, route,
                001DA6A0's result, kind, receivers, class-2 receivers, decal
                fans / vertices, flushed, decal flushed, cumulative calls,
                draws and decals,
  sample      = on sampled calls, the call's inputs and outputs (hex).

It checks:
  A. over the whole run, every tick whose 0015C160 ran: the route is
     0015C160's own choice for the logged gate bytes (src/func_0015C160.c),
     every 001DA6A0 that drew was flushed (its passes drawn) and every
     0015BF90 decal with fans was flushed; nothing else was drawn. The first
     control tick draws the shadow (the original's first-control frame
     holds the 001DA290 chain: playable_ee.bin, SHADOW_ORIGINAL.md).
  B. every sampled 001DA6A0 call (quick: the first, the last and two
     between; EM_TEST_FULL=1: all): the ORIGINAL 001CB590 + 001DA6A0
     execute over route 01's RAM with the port's inputs patched in (the
     player record and its 21 node records, the render context's +0x2240,
     +0x2340, +0x2380, +0x2468, the scratchpad camera 0x70003AC0, the
     camera pool's D_00810610, the area bytes, D_00817FF0 before the call).
     Its light globals D_00817F20..D_00817FF0, ctx+0x24B0, the silhouette
     VP, both box uploads (tools/test_shadow_original_reference.py
     box_checks), the UV upload and the receiver object sequence with its
     0023E8A0 re-passes equal the port's plan. The sample's views equal the
     render context of that tick's log (+0x2240, +0x2380, K = +0x23C0 =
     0x70003AC0, the zoom): the binding reads the canonical storage.
  C. every sampled 0015BF90 call: the ORIGINAL 0015BF90 -> 001F9100 ->
     001F8D30 (0011E620, 001CD390 nested as original) executes over route
     04's RAM with the port's player record, node records, 0x70003B8D,
     camera and the render context's fog / clip patched in, its 0019A570
     answered with the port's logged hit; its 001CE300 arguments equal the
     port's, and the ORIGINAL 001CE300 then writes, into page D_007635C0
     slot 0 (cleared first), exactly the port's packets (the fans and the
     TEX0 packet, byte for byte) and the mode-1 blend reference.
  D. every route snapshot the phases aligned (state['snapshots']): the
     gate bytes and the route equal the snapshot's; and in every route
     capture the mode-1 blend block the decal renderer implements
     (docs/SHADOW_DECAL.md section 4) is the captured one.
"""
from pathlib import Path
import ctypes as C
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as RM  # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route'
PLAYER = 0x8102B0
NODES, NODE_BYTES = 21, 0xD0
PAGE = 0x007635C0
BLEND_WRITES = ((0x17E, 0x00), (0x60, 0x14), (0x53001, 0x47), (0x44, 0x42), (0x00, 0x08), (0x01, 0x46))
HIT_RECORD = 0x01FF0000       # a free RAM address the segment answer's record is planted at
CHAIN_AT = 0x01F00000         # the DMA cursor of the executed 001DA6A0 chain (free RAM)


def u32(b, a):
    return struct.unpack_from('<I', b, a)[0]


def route_rule(b1, d771, w214):
    """0015C160's callee (src/func_0015C160.c)."""
    if b1 == 0 or d771 == 1:
        return 0
    return 2 if w214 else 1


# ------------------------------------------------------------------ A

def check_run(ticks, state):
    fresh = [t for t in ticks if t.get('shadow') and t['shadow'][0][0]]
    assert fresh, 'shadow: no tick of the run ran 0015C160 with the shadow bound'
    counts = {0: 0, 1: 0, 2: 0, -1: 0}
    drawn = decals = 0
    for t in fresh:
        (_, b1, d771, w214, route), call, _ = t['shadow']
        where = ('shadow', 'tick', t['tick'])
        counts[route] += 1
        if route == -1:
            assert b1 != 0 and not call[0], (where, 'a reported post-step computed a shadow', call)
            continue
        assert route == route_rule(b1, d771, w214), (where, 'route', route, (b1, d771, w214))
        if route == 0:
            assert not call[0], (where, 'a shadow call without a route', call)
            continue
        c_fresh, c_route, c_drawn, kind, receivers, cls2, fans, fan_vertices, flushed, decal_flushed = call[:10]
        assert c_fresh and c_route == route, (where, 'the shadow call of the tick', call)
        if route == 1:
            assert c_drawn in (0, 1) and flushed == c_drawn and decal_flushed == 0, (where, 'flush', call)
            assert not c_drawn or kind == 0x28, (where, 'kind', kind)
            drawn += c_drawn
        else:
            assert c_drawn == 0 and flushed == 0 and decal_flushed == (fans > 0), (where, 'decal flush', call)
            assert fans in (0, 1, 2) and (fans == 0) == (fan_vertices == 0), (where, 'fans', call)
            decals += fans > 0
    first = state.get('first_control')
    if first is not None:
        (_, b1, d771, w214, route), call, _ = ticks[first]['shadow']
        assert route == 1 and call[0] and call[2] == 1 and call[8] == 1, \
            ('shadow: the first-control frame draws no shadow (the original\'s does)', ticks[first]['tick'], call)
    return counts, drawn, decals


# ------------------------------------------------------------------ B

SO = None


def shadow_original():
    global SO
    if SO is None:
        import test_shadow_original_reference as so
        elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
        so.vu.ELF = elf
        so.ELF_BYTES[0] = elf
        so.ELF = elf
        SO = so
    return SO


def patched_route1(sample):
    so = shadow_original()
    ram = bytearray((ROUTE / '01_battery' / 'eeMemory.bin').read_bytes())
    scratch = bytearray((ROUTE / '01_battery' / 'scratchpad.bin').read_bytes())
    player = bytes.fromhex(sample['player'])
    nodes = bytes.fromhex(sample['nodes'])
    ram[PLAYER:PLAYER + 0x320] = player
    struct.pack_into('<I', ram, PLAYER + 0x214, 0)          # route 1: +0x214 == 0
    for i in range(NODES):
        a = u32(player, 0x110 + 4 * i)
        assert a and a == u32(ram, PLAYER + 0x110 + 4 * i)
        ram[a:a + NODE_BYTES] = nodes[NODE_BYTES * i:NODE_BYTES * (i + 1)]
    ctx = u32(ram, so.CONTEXT_PTR)
    for name, offset in (('clip_2240', 0x2240), ('proj_2340', 0x2340), ('view_2380', 0x2380)):
        ram[ctx + offset:ctx + offset + 0x40] = bytes.fromhex(sample[name])
    struct.pack_into('<I', ram, ctx + 0x2468, sample['zoom_2468'])
    ram[0x810610:0x810650] = bytes.fromhex(sample['view_810610'])
    ram[0x810700], ram[0x810701] = sample['area']
    ram[0x817FF0:0x818000] = bytes.fromhex(sample['ff0'])
    scratch[0x3AC0:0x3B00] = bytes.fromhex(sample['camera_3AC0'])
    return bytes(ram), bytes(scratch), ctx


def sample_route1(item):
    """B for one sample: (tick, sample, rctx of the tick)."""
    tick, sample, rctx = item
    so = shadow_original()
    elf = so.ELF_BYTES[0]
    ram, scratch, ctx = patched_route1(sample)
    raw = bytes.fromhex(sample['plan'])
    assert len(raw) == C.sizeof(so.Plan), ('plan size', len(raw), C.sizeof(so.Plan))
    plan = so.Plan.from_buffer_copy(raw)
    stats = {}
    where = ('shadow sample', tick)
    if rctx is not None:   # the binding's views are the render context's
        k = {'V': rctx[4], 'K': rctx[5], 'clip': rctx[6]}
        assert sample['view_2380'] == k['V'], (where, 'ctx+0x2380 != the render context')
        assert sample['clip_2240'] == k['clip'], (where, 'ctx+0x2240 != the render context')
        assert sample['camera_3AC0'] == k['K'], (where, '0x70003AC0 != K (ctx+0x23C0)')
        assert struct.pack('<I', sample['zoom_2468']).hex() == rctx[3], (where, 'zoom')
    o, end, _ = so.execute(elf, ram, scratch, CHAIN_AT)
    if plan.drawn == 0:
        assert end == CHAIN_AT, (where, 'the port returned early; the original built a chain')
        return tick, 0, 0
    assert end > CHAIN_AT and plan.drawn == 1, (where, 'the original drew nothing', plan.drawn)
    buf = bytearray(ram)
    for a in range(CHAIN_AT, end):
        buf[a] = o.load(a, 1)
    units = so.walk(buf, CHAIN_AT, end)
    g = lambda a, n=16: o.read(a, n)
    fb = so.fbytes
    for label, value, address, n in (('D_00817F20', plan.view_817F20, 0x817F20, 64),
                                     ('D_00817F60', plan.eye_817F60, 0x817F60, 16),
                                     ('D_00817F70', plan.light_817F70, 0x817F70, 16),
                                     ('D_00817F80', plan.right_817F80, 0x817F80, 16),
                                     ('D_00817F90', plan.up_817F90, 0x817F90, 16),
                                     ('D_00817FA0', plan.far_817FA0, 0x817FA0, 16),
                                     ('D_00817FB0', plan.near_817FB0, 0x817FB0, 16),
                                     ('D_00817FC0', plan.cross_817FC0, 0x817FC0, 16),
                                     ('D_00817FF0 after', plan.light_817F70, 0x817FF0, 16),
                                     ('ctx+0x24B0', plan.uv_24B0, ctx + 0x24B0, 64)):
        so.eq((where, label), fb(value), g(address, n), stats, 'light_bytes')
    snap, _, stopped = so.execute(elf, ram, scratch, CHAIN_AT, stop=so.C7420)
    assert stopped, (where, 'no 001C7420 call')
    so.eq((where, 'silhouette VP'), fb(plan.silhouette_vp), snap.read(0x70003AC0, 64), stats, 'light_bytes')
    so.box_checks(buf, units, plan, ram, stats)
    first = [j for j, u in enumerate(units) if u[1] == 5 and u[3] == so.RECEIVER_KERNEL][0]
    kd = so.unit_data(buf, units[first - 3])
    so.eq((where, 'receiver camera'), bytes.fromhex(sample['camera_3AC0']), kd[:64], stats, 'receiver_bytes')
    so.eq((where, 'receiver UV'), fb(plan.uv_24B0), so.unit_data(buf, units[first + 5]), stats, 'receiver_bytes')
    expected = []
    for i in range(plan.receiver_count):
        r = plan.receiver[i]
        addr = so.object_address(ram, r.id) + 0x40
        expected.append((addr, so.RECEIVER_KERNEL))
        if r.cls == 2:
            expected.append((addr, so.CLIP_KERNEL))
    assert so.receiver_sequence(units, first - 3) == expected, (where, 'receiver sequence')
    return tick, 1, plan.receiver_count


# ------------------------------------------------------------------ C

def sample_route2(item):
    """C for one sample."""
    tick, sample = item
    import test_shadow_decal_reference as SD
    import test_shadow_actor_route_reference as SAR
    if SD.ELF is None:
        SD.ELF = SAR.read_elf()
    ram = bytearray((ROUTE / '04_elevator_ride' / 'eeMemory.bin').read_bytes())
    spad = bytearray((ROUTE / '04_elevator_ride' / 'scratchpad.bin').read_bytes())
    player = bytes.fromhex(sample['player'])
    nodes = bytes.fromhex(sample['nodes'])
    where = ('shadow decal sample', tick)
    # What 0015BF90 reads of the player: +0xB0..+0xBF, +0x154 / +0x158 (the
    # node words) and +0x1F0; the node records' +0xC4.
    ram[PLAYER + 0xB0:PLAYER + 0xC0] = player[0xB0:0xC0]
    ram[PLAYER + 0x1F0] = player[0x1F0]
    for slot in (0x154, 0x158):
        a = u32(player, slot)
        assert a == u32(ram, PLAYER + slot), (where, 'node word', hex(slot))
        i = (a - u32(player, 0x110)) // NODE_BYTES
        ram[a:a + NODE_BYTES] = nodes[NODE_BYTES * i:NODE_BYTES * (i + 1)]
    ctx = u32(ram, 0x275670)
    ram[ctx + 0xA0:ctx + 0xB0] = bytes.fromhex(sample['fog_A0'])
    ram[ctx + 0x2240:ctx + 0x2280] = bytes.fromhex(sample['clip_2240'])
    spad[0x3AC0:0x3B00] = bytes.fromhex(sample['camera_3AC0'])
    spad[0x3B8D] = sample['spad3B8D']
    struct.pack_into('<I', ram, PAGE, 0)                  # page slot 0 cleared
    result, point_hex, normal_hex = sample['segment']
    point = list(struct.unpack('<4I', bytes.fromhex(point_hex)))
    normal = list(struct.unpack('<3I', bytes.fromhex(normal_hex)))
    ee = SD.DecalEE(SD.ELF, bytes(ram), bytes(spad))

    def script(frm, to):
        return result, point, HIT_RECORD, normal
    oracle = SAR.Oracle(ee, script)
    seen = []

    def submit(e):
        tag, quad_address = SAR.s32(e.r[4]), e.r[5] & 0xFFFFFFFF
        tex0, rgba = e.r[6] & 0xFFFFFFFFFFFFFFFF, e.r[7] & 0xFFFFFFFF
        quad = [e.load(quad_address + 4 * i) for i in range(16)]
        SD.run_ce300(e, tag, quad_address, tex0, rgba)
        seen.append((tag, quad, tex0, rgba))
    ee.hooks[SAR.SUBMIT] = submit
    ee.call(0x15BF90, (PLAYER,))
    segs = [c for c in oracle.calls if c[0] == 'segment']
    assert len(segs) <= 1, (where, 'segment calls', len(segs))
    submitted, tag, corners_hex, rgba, tex0_hex = sample['submit']
    packets = [bytes.fromhex(p) for p in sample['packets']]
    if not submitted:
        assert not seen and not packets, (where, 'the original submitted a decal the port did not')
        return tick, 0
    assert len(seen) == 1, (where, 'the original did not submit', len(seen))
    o_tag, o_quad, o_tex0, o_rgba = seen[0]
    assert (o_tag, o_tex0, o_rgba) == (tag, int(tex0_hex, 16), rgba), (where, 'submit args',
                                                                       (o_tag, hex(o_tex0), hex(o_rgba)))
    assert o_quad == list(struct.unpack('<16I', bytes.fromhex(corners_hex))), (where, 'submit corners')
    # page D_007635C0 slot 0, newest to oldest: the blend reference
    # (001CB900 mode 1), the TEX0 packet, the fans of pass 1 and pass 0.
    blocks, b = [], ee.load(PAGE)
    head = ee.load(PAGE + 0x4000)
    while b:
        blocks.append(b)
        if b == head:
            break
        b = ee.load(b + 0x14) & 0x0FFFFFFF
        assert len(blocks) < 8, (where, 'page walk')
    kinds = [ee.load(x) >> 28 for x in blocks]
    assert kinds[0] == 3 and ee.load(blocks[0] + 4) == u32(ram, 0x275674) + 0x720, (where, 'blend reference')
    assert all(k == 2 for k in kinds[1:]), (where, 'page blocks', kinds)
    original = []
    for x in reversed(blocks[1:]):
        count = ee.load(x + 0x10) & 0xFFFF
        original.append(bytes(ee.load(x + 0x20 + i, 1) for i in range(16 * count)))
    assert original == packets, (where, 'packets', [len(p) for p in original], [len(p) for p in packets])
    return tick, len(packets) - 1


# ------------------------------------------------------------------ D

def check_blend_blocks():
    """The mode-1 block (001CB9B0(1) = D_00275674 + 0x720) in every route
    capture: DIRECT, the A+D tag of 6 registers, the writes the decal
    renderer implements."""
    n = 0
    for folder in sorted(ROUTE.iterdir()):
        if not RM.in_scope_beat(folder.name) or not (folder / 'eeMemory.bin').exists():
            continue
        ram = (folder / 'eeMemory.bin').read_bytes()
        b = u32(ram, 0x275674) + 0x720
        assert u32(ram, b + 16) & 0x7FFF == 6, (folder.name, 'mode-1 block tag')
        for i, (value, reg) in enumerate(BLEND_WRITES):
            q = b + 32 + 16 * i
            assert (u32(ram, q), u32(ram, q + 4), u32(ram, q + 8)) == (value, 0, reg), \
                (folder.name, 'mode-1 block write', i)
        n += 1
    return n


def check_snapshots(ticks, state):
    done = []
    for beat, i in state.get('snapshots', []):
        ram = (ROUTE / beat / 'eeMemory.bin').read_bytes()
        (fresh, b1, d771, w214, route), call, _ = ticks[i]['shadow']
        where = ('shadow snapshot', beat, 'port tick', ticks[i]['tick'])
        o = (ram[0x8102B1], ram[0x810771], u32(ram, PLAYER + 0x214) != 0)
        assert fresh and (b1, d771, w214 != 0) == o, (where, 'gate bytes', (b1, d771, w214), o)
        assert route == route_rule(*o), (where, 'route', route)
        if route == 1:
            assert call[3] == struct.unpack_from('<h', ram, PLAYER + 0x96)[0], (where, 'kind')
        done.append(f'{beat[:2]} route {route}')
    return done


def sample_item(item):
    kind, x = item
    return sample_route1(x) if kind == '1' else sample_route2(x)


def check_shadow(ticks, state):
    counts, drawn, decals = check_run(ticks, state)
    samples = [(t['tick'], t['shadow'][2], t.get('rctx')) for t in ticks
               if t.get('shadow') and t['shadow'][2]]
    route1 = [(tick, s, r) for tick, s, r in samples if s['route'] == 1]
    route2 = [(tick, s) for tick, s, _ in samples if s['route'] == 2]
    assert route1, 'shadow: no sampled 001DA6A0 call'
    pick1 = route1 if RM.FULL else sorted({0, len(route1) // 3, 2 * len(route1) // 3, len(route1) - 1})
    pick1 = [route1[k] for k in pick1] if not RM.FULL else route1
    pick2 = route2 if RM.FULL else [route2[k] for k in sorted({0, len(route2) // 2, len(route2) - 1})] if route2 else []
    items = [('1', x) for x in pick1] + [('2', x) for x in pick2]
    shadow_original()                       # loaded before the workers fork
    results = RM.parallel_map(sample_item, items)
    r1 = results[:len(pick1)]
    r2 = results[len(pick1):]
    blend = check_blend_blocks()
    snaps = check_snapshots(ticks, state)
    print(f'shadow: PASS (0015C160 over the run: {counts[1]} 001DA6A0 calls ({drawn} drawn and flushed), '
          f'{counts[2]} 0015BF90 calls ({decals} decals drawn), {counts[-1]} reported (the record not the '
          f'displayed pose), {counts[0]} without a shadow; the first-control frame draws it; the ORIGINAL '
          f'001DA6A0 over the port\'s inputs builds the port\'s plan in '
          f'{RM.part(len(pick1), len(route1), "sampled calls")} ({sum(x[1] for x in r1)} drawn, '
          f'{sum(x[2] for x in r1)} receivers); the ORIGINAL 0015BF90 + 001CE300 write the port\'s packets in '
          f'{RM.part(len(pick2), len(route2), "sampled decal calls")} ({sum(x[1] for x in r2)} fans); the '
          f'mode-1 blend block equals the renderer\'s in {blend} route captures'
          f'{"; snapshots: " + ", ".join(snaps) if snaps else ""})')
