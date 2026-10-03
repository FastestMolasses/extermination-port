#!/usr/bin/env python3
"""The level smoke's check of the AREA11 overlay owners chain step A11FIX
bound or re-bound (docs/AREA11_EFFECT.md, docs/SECURITY_GUN.md,
docs/OPENING_ORIGINAL.md), against the route snapshots 00..14
(../Extermination/build/s87/route/<beat>/eeMemory.bin):

  * the flame 008235F0: from its first call on, on every tick of the run,
    its record holds the captures' +0x04, +0x30 (its own +0x1F0), +0x34
    (0x823580) and half extents (7, 15, 7), and +0x00 = 1 (2 only while
    its +0x210 contact cooldown runs, which no route reaches);
  * the flag-0x30 manager 00823CE0: from its first call on, +0x00 / +0x04 /
    +0x05 / +0x2E equal every capture's (lifecycle 1, waiting);
  * the opening controller 00823E80: after the opening, +0x00 / +0x04 /
    +0x05 / +0x2E equal every capture's (+0x05 = 2, the done mask 0xFFFF);
  * at the camera-exact aligned snapshots (10 and 14: the publication goes
    through 001B1630's view cone, so it follows the camera), the flame is on
    the published class-0xD list exactly when the capture's list block
    (D_00275B9C / D_00275BA4) holds it (the captures: beats 10 and 11 only);
    at the other aligned snapshots the agreement is only counted;
  * the flame's loop 0x413 (001FC3C0): no track requests it before first
    control (the opening: the captured handle is -1), and some track does on
    the main line once the route reaches the flame (the decomp's audio
    capture holds it in every frame of route 11).

Only addresses and values are printed."""
import struct

from test_level_smoke import ROUTE

FLAME, FLAG30, OPENING = 0x8235F0, 0x823CE0, 0x823E80
CLASS_D_CURSOR, CLASS_D_COUNT = 0x275B9C, 0x275BA4


def captured():
    """Per beat: {address: [cb, +0, +4, +5, +0x2E, +0x30, +0x34, [+0x1F0..+0x1F8]]}
    and the published class-0xD entries."""
    out = {}
    for beat in sorted(p.name for p in ROUTE.iterdir() if (p / 'eeMemory.bin').exists() and p.name[:2] < '15'):
        m = (ROUTE / beat / 'eeMemory.bin').read_bytes()
        u32 = lambda a: struct.unpack_from('<I', m, a)[0]
        rows, a = {}, u32(0x275BC0)
        while a:
            cb = u32(a + 0x10)
            if cb in (FLAME, FLAG30, OPENING):
                rows[a] = [cb, m[a], m[a + 4], m[a + 5], struct.unpack_from('<H', m, a + 0x2E)[0], u32(a + 0x30),
                           u32(a + 0x34), [u32(a + 0x1F0 + 4 * k) for k in range(3)] if cb == FLAME else [0, 0, 0]]
            a = u32(a + 0x1C)
        n = struct.unpack_from('<i', m, CLASS_D_COUNT)[0]
        cur = u32(CLASS_D_CURSOR)
        out[beat] = (rows, {u32(cur + 4 * j) for j in range(n)})
    return out


def port_row(r):
    """The tick log's overlay11 row in the captured layout."""
    return [r[1], r[2], r[3], r[4], r[5], r[6], r[7], list(r[8]) if r[1] == FLAME else [0, 0, 0]]


def check_overlay11(ticks, state):
    caps = captured()
    static = {}
    for beat, (rows, _) in caps.items():
        assert {r[0] for r in rows.values()} == {FLAME, FLAG30, OPENING}, \
            ('overlay11: a snapshot without the flame, the manager or the opening controller', beat)
        for a, r in rows.items():
            assert static.setdefault(a, r) == r, ('overlay11: the snapshots disagree', beat, hex(a), r, static[a])
    flame = next(a for a, r in static.items() if r[0] == FLAME)
    f = static[flame]
    assert f[1:3] == [1, 1] and f[5] == flame + 0x1F0 and f[6] == 0x823580, ('overlay11: the captured flame', f)
    first = state['first_control']
    seen, started, held_before, held_after = {}, set(), 0, 0
    for i, t in enumerate(ticks):
        rows = t.get('overlay11')
        if rows is None:
            continue
        if i < first:
            held_before += t.get('sfx413', 0) > 0
        else:
            held_after += t.get('sfx413', 0) > 0
        for r in rows:
            a, row = r[0], port_row(r)
            where = ('overlay11', 'port tick', t['tick'], hex(a))
            assert a in static, (where, 'no captured record of this owner at this address')
            if row[0] == OPENING:
                if row[3] == 2:   # the opening has ended: the captures' state from then on
                    assert row == static[a], (where, 'the opening controller after the opening', row, static[a])
                    seen[OPENING] = seen.get(OPENING, 0) + 1
                continue
            if row[2] == 0:
                assert a not in started, (where, 'back in state 0 after its first call')
                continue
            started.add(a)
            if row[0] == FLAME:
                assert r[9] == 0 and row[1] == 1, (where, 'the flame in its contact cooldown (no route touches it)',
                                                   row, r[9])
            assert row == static[a], (where, 'differs from the snapshots\' record', row, static[a])
            seen[row[0]] = seen.get(row[0], 0) + 1
    assert seen.get(FLAME, 0) >= 100 and seen.get(FLAG30, 0) >= 100, ('overlay11: too few ticks checked', seen)
    assert held_before == 0, ('overlay11: the flame loop 0x413 requested before first control', held_before)
    from level_smoke_static_world import VIEW_EXACT_MAIN_LINE
    compared, published, agree, other = [], [], 0, 0
    for beat, i in state.get('snapshots', []):
        rows, class_d = caps[beat]
        port = {r[0]: r[10] for r in ticks[i].get('overlay11', [])}
        assert flame in port, ('overlay11: no flame at the aligned tick', beat)
        same = bool(port[flame]) == (flame in class_d)
        if beat not in VIEW_EXACT_MAIN_LINE:
            other += 1
            agree += same
            continue
        assert same, ('overlay11: the flame\'s class-0xD publication differs from the camera-exact capture', beat,
                      'port tick', ticks[i]['tick'], port[flame], sorted(map(hex, class_d)))
        compared.append(beat)
        if port[flame]:
            published.append(beat)
    print(f'overlay11: PASS (the flame on {seen.get(FLAME, 0)} tick(s) and the flag-0x30 manager on '
          f'{seen.get(FLAG30, 0)} equal all {len(caps)} route snapshots\' records (+0x30 = the record\'s +0x1F0, '
          f'+0x34 = 0x823580; lifecycle 1 waiting); the opening controller after the opening on '
          f'{seen.get(OPENING, 0)}; the flame\'s class-0xD publication equals the capture at '
          f'{len(compared)} camera-exact snapshot(s) ({", ".join(published) if published else "none"} published) '
          f'and at {agree} of {other} other aligned one(s); '
          f'the loop 0x413 requested on 0 tick(s) before first control and {held_after} after)')
    return held_after
