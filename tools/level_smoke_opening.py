#!/usr/bin/env python3
"""The level smoke's opening check: the New Game opening's actors on their
original records (the chain's OPENING step; docs/OPENING_ACTORS.md,
docs/LEVEL_SMOKE.md "The opening's actors").

tools/test_level_smoke.py calls check_opening_actors(ticks, state) after the
phase checks. The original is the decomp's opening capture
(build/startup-reference/opening_ee.bin: the opening mid-way, its camera
timeline's cursor at the camera's +0x74). The port's run is aligned on the
tick whose camera block holds the capture's timeline words +0x6C..+0x7B
(scene 0x22, bank 0x98's clip-0 track, the cursor and the head): the
opening script's 001B8FC0 kind 6 started it on the camera, and the cursor
counts the camera stage's samples since, so that tick is the capture's
moment of the script.

At that tick:
  A. the opening script's records: every record the capture's pool holds
     with the behaviour 001BB0E0 (001BAC00 spawned them: the class-9 body
     on Roger's model 0x47 with bank 0x98's clip 2, the class-8 node
     001C5C90 rides on it), and the head-sprite node its 001BA8E0 spawned
     after them, are live in the port's pool at the same record address
     (the pool's free list reused the same records);
  B. the body, the class-8 node and the player D_008102B0 each drew a
     001CAA00 unit that frame, and its pose digest (every node record's
     +0x90..+0xCF world matrix, as em_owner_draw_live's log folds them) and
     its lighting point (the +0x98 node's translation) are the capture's
     bit for bit: the clips (bank 0x98's 1 and 2) evaluated by the
     records' own stages (the player's 00183090 advance on its takeover,
     the body's 001BB0E0 anim_advance_time and 001C68C0) reach the
     original's matrices exactly;
  C. the player record's route-row bytes (+5, +1F0, +1F1, the clip +20C,
     the clock +3C, the ground owner +214, the special bank +2F3 and the
     takeover +4 = 4) equal the capture's.
Over the run:
  D. from the body's first unit to its last, it draws with its face unit
     (001CB3C0 over its +0x90) in every frame whose walk ran; neither record
     draws after that, and the last is before first control (the
     controller's completion sets the done mask its 001BB0E0 reads).
"""
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
CAPTURE = DECOMP / 'build/startup-reference/opening_ee.bin'
POOL_HEAD = 0x275BC0          # D_00275BC0, the active list's head (+0x1C next)
PLAYER = 0x8102B0
CAMERA = 0x8101E0
OPENING_ACTOR = 0x001BB0E0    # 001BAC00's default behaviour
HEAD_SPRITE = 0x001E2560
TIMELINE = (0x6C, 0x7C)       # the camera's timeline words


def fnv_words(h, words):
    for w in words:
        for b in range(4):
            h = ((h ^ ((w >> (8 * b)) & 0xFF)) * 16777619) & 0xFFFFFFFF
    return h


def u32(b, a):
    return struct.unpack_from('<I', b, a)[0]


def capture_pose(ram, record):
    """em_owner_draw_live's pose digest and lighting point over the
    capture's record: the +0x0C node records' +0x90..+0xCF, the +0x98
    node's translation."""
    h = 2166136261
    for k in range(ram[record + 0x0C]):
        h = fnv_words(h, struct.unpack_from('<16I', ram, u32(ram, record + 0x110 + 4 * k) + 0x90))
    node = u32(ram, record + 0x110 + 4 * ram[record + 0x98])
    return h, list(struct.unpack_from('<3I', ram, node + 0xC0))


def capture_list(ram):
    """The capture's active list: [(record, behaviour)] in walk order."""
    out, at = [], u32(ram, POOL_HEAD)
    while at and len(out) < 0x100:
        out.append((at, u32(ram, at + 0x10)))
        at = u32(ram, at + 0x1C)
    return out


def camera_block(tick):
    c = tick.get('camblk')
    return bytes.fromhex(c[0]) if c else None


def check_opening_actors(ticks, state):
    ram = CAPTURE.read_bytes()
    cam = ram[CAMERA:CAMERA + 0xD0]
    assert cam[4] == 3, ('opening capture: the camera is not on its timeline', cam[4])
    words = cam[TIMELINE[0]:TIMELINE[1]]
    aligned = [i for i, t in enumerate(ticks)
               if camera_block(t) and camera_block(t)[TIMELINE[0]:TIMELINE[1]] == words and
               camera_block(t)[4] == 3]
    assert len(aligned) == 1, ('opening: the capture\'s timeline words are not held by exactly one port tick',
                               [ticks[i]['tick'] for i in aligned])
    t = ticks[aligned[0]]
    where = ('opening', 'port tick', t['tick'])
    # A. the script's records and the head sprite after them.
    pool = capture_list(ram)
    actors = [a for a, cb in pool if cb == OPENING_ACTOR]
    assert len(actors) == 2, ('opening capture: the 001BB0E0 records', [hex(a) for a in actors])
    tail = [a for a, cb in pool[pool.index((actors[-1], OPENING_ACTOR)) + 1:] if cb == HEAD_SPRITE]
    assert tail, ('opening capture: no head-sprite node after the 001BB0E0 records')
    effects = t.get('effects')
    nodes = {e[0]: e[1] for e in effects[1]} if isinstance(effects, list) and len(effects) > 1 else {}
    assert nodes.get(tail[0]) == HEAD_SPRITE, (where, 'the head sprite 001BA8E0 spawned is not at the capture\'s '
                                               'record', hex(tail[0]))
    # B. the units' poses and points.
    units = {u[0]: u for u in t.get('owner_units') or []}
    compared = []
    for record in actors + [PLAYER]:
        pose, point = capture_pose(ram, record)
        u = units.get(record)
        assert u is not None, (where, 'no 001CAA00 unit of the capture\'s record', hex(record))
        assert u[9] == pose and u[8] == point, (where, 'the pose / point of', hex(record), hex(u[9]), hex(pose),
                                                u[8], point)
        compared.append(f'{record:06X} ({ram[record + 0x0C]} node(s))')
    # C. the player's route-row bytes.
    p = t.get('player')
    orig = [ram[PLAYER + 5], ram[PLAYER + 0x1F0], ram[PLAYER + 0x1F1],
            struct.unpack_from('<h', ram, PLAYER + 0x20C)[0], u32(ram, PLAYER + 0x3C),
            u32(ram, PLAYER + 0x214), ram[PLAYER + 0x2F3], ram[PLAYER + 4]]
    assert p == orig, (where, 'the player record', p, orig)
    # D. the body draws with its face unit, and the class-8 node with it,
    # in every frame from its spawn to the done mask; then neither draws
    # again before its record is freed (the pool reuses the records later,
    # so only the frames right after the stretch are judged).
    body = actors[0] if ram[actors[0] + 0x0C] > 1 else actors[1]
    node8 = actors[1] if body == actors[0] else actors[0]
    fresh = lambda tk: isinstance(tk.get('shadow'), list) and tk['shadow'] and tk['shadow'][0][0]
    unit = lambda tk, rec: next((u for u in tk.get('owner_units') or [] if u[0] == rec), None)
    first_draw = next((i for i, tk in enumerate(ticks) if unit(tk, body)), None)
    assert first_draw is not None, ('opening: the body never drew')
    last = first_draw
    while last + 1 < len(ticks) and (not fresh(ticks[last + 1]) or unit(ticks[last + 1], body)):
        last += 1
    for i in range(first_draw, last + 1):
        if not fresh(ticks[i]):
            continue
        b, e = unit(ticks[i], body), unit(ticks[i], node8)
        assert b and b[10], ('opening: the body drew without its face unit', ticks[i]['tick'])
        assert e, ('opening: the class-8 node did not draw with the body', ticks[i]['tick'])
    after = [ticks[i]['tick'] for i in range(last + 1, min(last + 4, len(ticks)))
             if fresh(ticks[i]) and (unit(ticks[i], body) or unit(ticks[i], node8))]
    assert not after, ('opening: an opening record drew after the body\'s last frame', after)
    # The done mask ends them before first control's hand-off.
    first = state.get('first_control')
    assert first is None or last < first, ('opening: the body drew at or after first control',
                                           ticks[last]['tick'], ticks[first]['tick'])
    drawn = (first_draw, last)
    print(f'opening actors: PASS (at port tick {t["tick"]}, whose camera holds the opening capture\'s timeline '
          f'words (scene 0x22, cursor {struct.unpack_from("<f", words, 8)[0]:g} of {struct.unpack_from("<f", words, 0xC)[0]:g}): '
          f'the capture\'s two 001BB0E0 records and the head sprite after them live at the same records, and '
          f'the units of {", ".join(compared)} hold the capture\'s pose and lighting point bit for bit; the '
          f'player record\'s row equals the capture\'s (+4 = 4, +2F3 = {orig[6]}); the body drew with its face '
          f'unit and the class-8 node with it on all {drawn[1] - drawn[0] + 1} ticks from port tick '
          f'{ticks[drawn[0]]["tick"]} to {ticks[drawn[1]]["tick"]}, before first control, and neither drew '
          f'after)')
