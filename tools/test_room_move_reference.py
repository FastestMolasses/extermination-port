#!/usr/bin/env python3
"""Room move (WP-3 S12b): the AREA11 door through B8 = 2 and 0x1AE040 state 4,
against the executed original and the original route capture.

1. 0018AB00. Executes the original 0018AB00 (0x1AE040 state 4's second callee)
   from the user's ELF and compares em_sf_0018AB00 (src/game/em_scene_task.c):
   the byte it stores to D_008106C6 and that it stores nothing else, over
   D_00810CA4 x D_00810CA7 (quick: every CA4 in {0, 1, 2, 3, 0xFF} with every
   CA7, every CA7 in {7, 8, 9, 10} with every CA4, plus a fixed-seed sample;
   EM_TEST_FULL=1: all 65,536 pairs).

2. Live run (--log, an EM_AREA_CHANGE_LOG from the fence door's room move;
   since census L18 the level smoke's fence_door phase runs these checks,
   tools/test_level_smoke.py check_fence_door, over its own tick log from the
   Use scan on). The log holds one line per slot-0 task tick with the state
   at the tick start, the original's main-loop-top sample.
   a. Tick sequence (design row S12b): the rows from the first one with
      B8 = 2 (the door's 001BC150 ran in the tick before) must match the
      original route capture 09_fence_door (../Extermination/build/s87/route,
      read-only; nothing from it is copied) row for row from its first B8 = 2
      row (f407) through four rows after the re-place: B5..B9 and the eight
      bytes of the 0x28A9A0 fade block (substate, colour, mode, level, step),
      that is the 001AEDE0(4, 0) ramp, the full-black rows, 001AD010's tick
      and the 001AEE10(4, 0) fade-in. At the re-place row the player position
      and heading must equal the capture's (0x810350, +0xC4; the capture keeps
      five decimals).
   b. The fade-2 tick calls 001AD010 and leaves +B = 4; the 001AD010 call is
      replayed through the executed original (the S3 oracle of
      test_area_load_reference.py) from the logged state at the call. The
      next tick is state 4: its trace holds 001AFCF0, 0018AB00, 001B07C0(1),
      001C1DC0, 0018D7B0(0x8101E0, 1), 0018C0D0(0x8101E0, 1), 001AEE10(4, 0),
      001FAE70(0), 001C5C50 in that order, then 001AE7E0 and 001AE5E0 in the
      same tick (state 4 falls into state 1), and ends with +B = 1.
   c. Pool: one 001E55F0 and one 001C5930 node before the commit and after
      the re-place (the old ones left: the title on B8, the weather at B8 = 2
      with fade 2; state 4 spawned their successors), no title node between
      (it leaves on B8 and state 4 respawns it). The capture's snapshot after
      the room move also has exactly one of each.
   d. The door is back in phase 0 with its armed byte clear at the re-place
      row (001BC290 saw B8 clear; the tick log's "door" is the door record's
      +0x05, +0x0B and +0x04).
   e. D_008106C8 after state 4 equals the capture's (001B0250 over entry 2),
      and D_008106C6 equals it too (0018AB00).

No original bytes are embedded or printed; only addresses and values.
"""
import argparse
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

from test_point_light_reference import Oracle
import reference_mode
import test_scene_task_reference as tsr

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
ROUTE = DECOMP / 'build/s87/route/09_fence_door'
F_0018AB00 = 0x18AB00
REQ = 24                      # the request block's offset in the logged snapshot
STATE4 = [0x1AFCF0, 0x18AB00, 0x1B07C0, 0x1C1DC0, 0x18D7B0, 0x18C0D0, 0x1AEE10, 0x1FAE70, 0x1C5C50]
STATE4_ARGS = {0x1B07C0: (1,), 0x18D7B0: (0x8101E0, 1), 0x18C0D0: (0x8101E0, 1),
               0x1AEE10: (4, 0), 0x1FAE70: (0,)}
AFTER_REPLACE = 4

SHIM = r'''
#include <string.h>
#include "game/em_scene_task.h"
int shim_0018AB00(unsigned ca4, unsigned ca7, unsigned char *req_out)
{
    static EmSceneState s;
    memset(&s, 0xA5, sizeof s);
    memset(&s.fault, 0, sizeof s.fault);
    s.progress.bytes[0x00810CA4u - EM_SCENE_PROGRESS_BASE] = (unsigned char)ca4;
    s.progress.bytes[0x00810CA7u - EM_SCENE_PROGRESS_BASE] = (unsigned char)ca7;
    int r = em_sf_0018AB00(&s);
    memcpy(req_out, s.req, EM_SCENE_REQ_SIZE);
    return r;
}
'''


class StoreOracle(Oracle):
    def __init__(self, elf):
        self.recording = False
        self.stores = []
        super().__init__(elf)

    def save(self, address, value, size=4):
        if self.recording:
            self.stores.append((address, size))
        super().save(address, value, size)


def original_0018AB00(elf, ca4, ca7):
    o = StoreOracle(elf)
    o.save(0x810CA4, ca4, 1)
    o.save(0x810CA7, ca7, 1)
    o.save(0x8106C6, 0xA5, 1)
    o.recording = True
    o.run(F_0018AB00)
    o.recording = False
    stack = [a for a, _ in o.stores if 0x700000 - 0x200 <= a < 0x700000]
    return o.load(0x8106C6, 1), [s for s in o.stores if s[0] not in stack]


def build_native():
    out = ROOT / 'build/room_move_reference'
    out.mkdir(parents=True, exist_ok=True)
    ext = 'dylib' if sys.platform == 'darwin' else 'so'
    link = '-dynamiclib' if sys.platform == 'darwin' else '-shared'
    (out / 'shim.c').write_text(SHIM)
    lib = out / f'room_move.{ext}'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC', link, '-Isrc',
                    'src/game/em_scene_task.c', str(out / 'shim.c'), '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    native.shim_0018AB00.argtypes = [C.c_uint, C.c_uint, C.c_char_p]
    return native


def check_0018AB00(elf, native):
    pairs = list(itertools.product(range(256), range(256)))
    keep = lambda _, p: p[0] in (0, 1, 2, 3, 0xFF) or p[1] in (7, 8, 9, 10)
    cases = reference_mode.select(pairs, 2600, 0x18AB00, keep=keep)
    for ca4, ca7 in cases:
        want, stores = original_0018AB00(elf, ca4, ca7)
        assert stores == [(0x8106C6, 1)], ('0018AB00 stores', ca4, ca7, stores)
        buf = C.create_string_buffer(0x48)
        assert native.shim_0018AB00(ca4, ca7, buf) == 0, ('native 0018AB00 faulted', ca4, ca7)
        got = buf.raw
        assert got[0x16] == want, ('D_008106C6', ca4, ca7, got[0x16], want)
        assert all(b == 0xA5 for i, b in enumerate(got) if i != 0x16), ('request block', ca4, ca7)
    return len(cases), len(pairs)


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def bits_f32(word):
    return struct.unpack('<f', struct.pack('<I', word))[0]


def check_capture_sequence(ticks, rows):
    pre = [bytes.fromhex(t['pre']) for t in ticks]
    c = next(i for i, p in enumerate(pre) if p[REQ + 8] != 0)
    o = next(i for i, r in enumerate(rows) if bytes.fromhex(r['req'])[8] != 0)
    o_place = next(i for i in range(o, len(rows)) if bytes.fromhex(rows[i]['req'])[8] == 0)
    span = o_place - o + AFTER_REPLACE
    assert c + span < len(ticks), 'the log ends before the re-place'
    for k in range(span + 1):
        t, r, p = ticks[c + k], rows[o + k], pre[c + k]
        want_req = bytes.fromhex(r['req'])[5:10]
        got_req = p[REQ + 5:REQ + 10]
        assert got_req == want_req, ('row', k, 'B5..B9', got_req.hex(), want_req.hex())
        want_fade = bytes.fromhex(r['fade'])[:8]
        assert bytes.fromhex(t['fade8']) == want_fade, ('row', k, 'fade', t['fade8'], want_fade.hex())
    t, r = ticks[c + o_place - o], rows[o_place]
    pos = [bits_f32(w) for w in t['pos']]
    assert all(abs(a - b) <= 1e-4 for a, b in zip(pos, r['pos'])), ('re-place position', pos, r['pos'])
    assert abs(bits_f32(t['yaw']) - r['yaw']) <= 1e-5, ('re-place heading', bits_f32(t['yaw']), r['yaw'])
    return c, c + o_place - o, span + 1, rows[o]['f'], rows[o_place]['f']


def calls(tick):
    return [(e[1], tuple(e[2:6])) for e in tick['trace']]


def check_state4(elf, ticks, c, place):
    import test_area_load_reference as tal
    fade2 = place - 2
    pre = [bytes.fromhex(t['pre']) for t in ticks]
    post = [bytes.fromhex(t['post']) for t in ticks]
    for i in range(c, fade2):
        assert ticks[i]['fade'] != 2 and not ticks[i]['d010'], ('tick', ticks[i]['tick'], 'early 001AD010')
    t = ticks[fade2]
    assert t['fade'] == 2 and t['d010'] and post[fade2][3] == 4, ('fade-2 tick', t['tick'])
    d_pre, d_post = bytes.fromhex(t['d010'][0]), bytes.fromhex(t['d010'][1])
    final, trace, _ = tal.chain_original(elf, 0x1AD010, d_pre,
                                         {'fade': 2, 'busy': 0,
                                          'results': {0x1AD1A0: 0, 0x1AD230: 0, 0x21B550: 0}})
    if final != d_post:
        diff = [hex(a) for a, o in tsr.OFFSET.items() if final[o] != d_post[o]]
        raise AssertionError(('001AD010 against the executed original', diff[:8]))
    got = [tal.trace_key(e) for e in t['trace'] if e[0] == 0x1AD010]
    assert got == [tal.trace_key(e) for e in trace], ('001AD010 trace', got)
    s4 = ticks[fade2 + 1]
    assert pre[fade2 + 1][3] == 4 and post[fade2 + 1][3] == 1, ('state-4 tick', s4['tick'])
    seq = calls(s4)
    idx, at = [], 0
    for fn in STATE4 + [0x1AE7E0, 0x1AE5E0]:
        at = next(i for i in range(at, len(seq)) if seq[i][0] == fn)
        want = STATE4_ARGS.get(fn)
        if want:
            assert seq[at][1][:len(want)] == want, ('state-4 call', hex(fn), seq[at][1], want)
        idx.append(at)
        at += 1
    assert fade2 + 2 == place
    return s4['tick'], post[fade2 + 1]


def check_nodes_and_door(ticks, c, place):
    for i in range(max(0, c - 5), c):
        assert (ticks[i]['weather'], ticks[i]['title']) == (1, 1), ('before the commit', ticks[i]['tick'])
    for i in range(c + 1, place):
        assert ticks[i]['title'] == 0 and ticks[i]['weather'] == 1, ('during the fade', ticks[i]['tick'])
    for i in range(place, min(len(ticks), place + AFTER_REPLACE + 1)):
        assert (ticks[i]['weather'], ticks[i]['title']) == (1, 1), ('after the re-place', ticks[i]['tick'])
    door = ticks[place]['door']
    assert door[0] == 0 and door[1] == 0, ('door at the re-place', door)


def capture_nodes_and_flags():
    ram = (ROUTE / 'eeMemory.bin').read_bytes()
    u32 = lambda a: struct.unpack_from('<I', ram, a)[0]
    counts, seen, a = {}, set(), u32(0x275BC0)
    while a and a not in seen and len(seen) < 512:
        seen.add(a)
        cb = u32(a + 0x10)
        counts[cb] = counts.get(cb, 0) + 1
        a = u32(a + 0x1C)
    return counts.get(0x1E55F0, 0), counts.get(0x1C5930, 0), u32(0x8106C8), ram[0x8106C6], ram[0x810702]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--log', type=Path, help='EM_AREA_CHANGE_LOG of a run through the fence door\'s room move')
    args = parser.parse_args()
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256
    native = build_native()
    n, total = check_0018AB00(elf, native)
    reference_mode.banner(f'{n} of {total} 0018AB00 cases')
    print(f'0018AB00: PASS ({n} CA4/CA7 pairs: D_008106C6 and the lone store identical to the executed original)')
    if not args.log:
        print('room move reference: PASS (0018AB00; the live room move is the level smoke\'s fence_door phase)')
        return 0
    assert (ROUTE / 'trace.json').exists(), f'route capture missing: {ROUTE} (docs/FIRST_LEVEL_ROUTE.md)'
    rows = json.loads((ROUTE / 'trace.json').read_text())['rows']
    ticks = [json.loads(line) for line in args.log.open()]
    c, place, span, f_commit, f_place = check_capture_sequence(ticks, rows)
    s4_tick, s4_post = check_state4(elf, ticks, c, place)
    check_nodes_and_door(ticks, c, place)
    weather, title, c8, c6, e702 = capture_nodes_and_flags()
    assert (weather, title) == (1, 1), ('capture nodes', weather, title)
    got_c8 = struct.unpack_from('<I', s4_post, REQ + 0x18)[0]
    assert got_c8 == c8 and s4_post[REQ + 0x16] == c6, ('C8/C6 after state 4', hex(got_c8), hex(c8),
                                                       s4_post[REQ + 0x16], c6)
    assert s4_post[24 + 0x48 + 2] == e702 == 2, ('D_00810702', s4_post[24 + 0x48 + 2], e702)
    print(f'sequence: PASS ({span} rows from the B8 = 2 row, ticks {ticks[c]["tick"]}..'
          f'{ticks[c + span - 1]["tick"]}, identical to capture rows f{f_commit}..f{f_commit + span - 1} '
          f'in B5..B9 and the fade block; re-place at +{place - c} as f{f_place}, position and heading '
          f'as captured)')
    print(f'state 4: PASS (001AD010 replayed through the executed original; tick {s4_tick} runs the nine '
          f'state-4 calls then 001AE7E0/001AE5E0, +B 4 -> 1; D_008106C8 {c8:#010x} and D_008106C6 {c6} '
          f'as captured)')
    print('pool: PASS (one weather and one title node before and after, as in the capture; the title '
          'leaves on B8 and state 4 respawns both; the door closed and control returned at the re-place)')
    print('room move reference: PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
