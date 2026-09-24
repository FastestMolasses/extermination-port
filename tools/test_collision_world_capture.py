#!/usr/bin/env python3
"""The live collision world's cells against the original route captures
(census L07; docs/ACTOR_COLLISION.md section 7).

The port's AREA11 owners publish their collision cells through the translated
001A2370 (re-transform by the owner's 001C6380 matrix) and 001B1B70. This test
runs the headless level smoke through the elevator phase (New Game -> first
control -> battery -> refusal -> panel -> elevator ride;
EM_LEVEL_SMOKE_UNTIL=elevator) with EM_COLL_WORLD_DUMP, which writes the
cell directory (*0x70003250) and the published class-4 list after the first
001AAD00 of the area and after the last one, and compares them with the
directories the original holds in the route captures
(../Extermination/build/s87/route/<beat>/eeMemory.bin, the table named by the
scratchpad word 0x70003250):

  * first dump (after every owner's state 0) vs beat 00_panel_no_battery:
    the terminal's cell (uid 4) at the upper floor and the item cells
    (uids 19, 21..25) must equal the original's bytes;
  * last dump (after the ride's completion) vs beat 04_elevator_ride: uid 4
    at the lower floor (0x827E54's 001A2370), the items unchanged (the
    battery's cell keeps its last transform after the take), and the last
    frame's published class-4 list equal to the original's entries of the
    owners the port runs (panel, terminal, items, and the crates and drums
    on their original owners since census L25: the crates publish their
    cells, uids 7..10, on every rest tick; the drums, uids 5 and 6, when
    001B17A0 finds them visible or the player is within 50 units), in the
    original order.

Every other uid must equal the disc directory in the port. The original moves
two of them that the port's owners do not publish yet: the truck (uid 14,
00823FF0, census L23) and 0x825940's plate (uid 15, L24); both are listed, not
hidden. Both published class-4 lists are printed. List membership depends on
each side's camera (001B1630's cone), so the check is made at the smoke's
last frame, 25 frames after the ride's scripted camera released.

Nothing disc-derived is written outside build/; only uids and byte counts are
printed.
"""
import os
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ROUTE = ROOT.parent / 'Extermination/build/s87/route'
DISC = ROOT / 'assets/scene_snow/area11_cells.bin'
OUT = ROOT / 'build/collision_world'
PORT_MOVED = {4, 19, 21, 22, 23, 24, 25}     # the terminal and the item owners
PORT_OWNERS = {4, 18, 5, 6, 7, 8, 9, 10}    # terminal, panel, drums, crates (+ items 19..26)
NOT_PUBLISHED = {14: 'truck 00823FF0 (census L23)', 15: "0x825940's plate (census L24)"}


def hulls(image):
    count = struct.unpack_from('<i', image, 0)[0]
    words = [struct.unpack_from('<I', image, 4 + 4 * u)[0] for u in range(count)]
    ends = sorted({w for w in words if w} | {len(image)})
    out = {}
    for uid, word in enumerate(words):
        if word:
            out[uid] = (word, next(e for e in ends if e > word))
    return out


def read_dump(path):
    data = path.read_bytes()
    assert data[:4] == b'EMCW' and struct.unpack_from('<I', data, 4)[0] == 1, path
    size = struct.unpack_from('<I', data, 8)[0]
    image = data[12:12 + size]
    at = 12 + size
    n = struct.unpack_from('<I', data, at)[0]
    entries = [struct.unpack_from('<I', data, at + 4 + 4 * j)[0] for j in range(n)]
    return image, [(e & 0xFFFF) >> 8 for e in entries]


def captured(beat, size):
    ram = (ROUTE / beat / 'eeMemory.bin').read_bytes()
    spad = (ROUTE / beat / 'scratchpad.bin').read_bytes()
    table = struct.unpack_from('<I', spad, 0x3250)[0]
    image = ram[table:table + size]
    head = struct.unpack_from('<I', ram, 0x275B7C)[0]
    count = struct.unpack_from('<h', ram, 0x275B84)[0]
    uids = [struct.unpack_from('<H', ram, struct.unpack_from('<I', ram, head + 4 * j)[0] + 0xE)[0] >> 8
            for j in range(count)]
    return image, uids


def compare(label, port, disc, original):
    spans = hulls(disc)
    failures = []
    for uid, (lo, hi) in sorted(spans.items()):
        p, d, o = port[lo:hi], disc[lo:hi], original[lo:hi]
        if uid in PORT_MOVED:
            if p != o:
                failures.append(f'{label}: uid {uid} differs from the original ({hi - lo} bytes)')
        elif uid in NOT_PUBLISHED:
            if p != d:
                failures.append(f'{label}: uid {uid} moved in the port')
        elif p != d or o != d:
            failures.append(f'{label}: uid {uid} is not the disc hull (port {p == d}, original {o == d})')
    if port[:4 + 4 * len(spans)] != disc[:4 + 4 * len(spans)]:
        failures.append(f'{label}: the directory header changed')
    moved = sorted(u for u in PORT_MOVED if disc[slice(*spans[u])] != port[slice(*spans[u])])
    return failures, moved


# The box records' bytes the port models (census L25): status, class, model,
# the state bytes +0x04..+0x08, +0x09 (bone slots held), +0x0A..+0x10, the
# owner words +0x28..+0x3B, +0x2E, +0x52..+0x57, the +0x60 scale, the
# position/rotation +0xB0..+0xCF, the world matrix +0xD0..+0x10F and the
# +0x1F0 block. (+0x01 is the walk's per-call byte; +0x14..+0x1C and +0x44/
# +0x110 are pointers.)
BOX_SPANS = ((0x00, 0x01), (0x02, 0x14), (0x28, 0x3C), (0x52, 0x58), (0x60, 0x70),
             (0xB0, 0x110), (0x1F0, 0x2F0))


def compare_boxes(path):
    """Every live crate and drum record vs route 04's record at the same
    address (the boxes are static after their state 0 on this route)."""
    if not path.exists():
        return ['no box dump (EM_BOX_DUMP)']
    data = path.read_bytes()
    assert data[:8] == b'EMBX\x01\x00\x00\x00', path
    count = struct.unpack_from('<I', data, 8)[0]
    ram = (ROUTE / '04_elevator_ride' / 'eeMemory.bin').read_bytes()
    failures, seen = [], []
    at = 12
    for _ in range(count):
        callback, address = struct.unpack_from('<II', data, at)
        image = data[at + 8:at + 8 + 0x2F0]
        at += 8 + 0x2F0
        original = ram[address:address + 0x2F0]
        if struct.unpack_from('<I', original, 0x10)[0] != callback:
            failures.append(f'box {address:#x}: the original record there is not {callback:#x}')
            continue
        diff = [f'+{o:#x}' for lo, hi in BOX_SPANS for o in range(lo, hi) if image[o] != original[o]]
        if diff:
            failures.append(f'box {callback:#x} at {address:#x}: bytes differ from route 04 at '
                            f'{", ".join(diff[:12])}{" ..." if len(diff) > 12 else ""}')
        seen.append((callback, address))
    crates = sorted(a for c, a in seen if c == 0x1551B0)
    drums = sorted(a for c, a in seen if c == 0x156620)
    if crates != [0x7A7980, 0x7A7C70, 0x7A7F60, 0x7A8250] or drums != [0x7A99D0, 0x7A9CC0]:
        failures.append(f'live boxes {[hex(a) for a in crates]} / {[hex(a) for a in drums]}, '
                        'expected the four crates and two drums of the captures')
    print(f'boxes: {len(seen)} live records (crates {[hex(a) for a in crates]}, drums '
          f'{[hex(a) for a in drums]}) compared with route 04 in {len(BOX_SPANS)} spans')
    return failures


def main():
    # --binary PATH: a private build (lane builds and negative controls).
    binary = Path(sys.argv[sys.argv.index('--binary') + 1]) if '--binary' in sys.argv \
        else ROOT / 'build/extermination'
    if not binary.exists():
        sys.exit('build/extermination is missing (make all)')
    OUT.mkdir(parents=True, exist_ok=True)
    dump = OUT / 'world.bin'
    for stale in (dump, Path(str(dump) + '.first')):
        if stale.exists():
            stale.unlink()
    boxes = OUT / 'boxes.bin'
    if boxes.exists():
        boxes.unlink()
    env = dict(os.environ, EM_UNCAPPED='1', EM_STARTUP_TEST='newgame-level',
               EM_COLL_WORLD_DUMP=str(dump), EM_AREA_CHANGE_LOG=str(OUT / 'ticks.jsonl'),
               EM_BOX_DUMP=str(boxes))
    # Through the elevator phase: the last dump is then the frame after the
    # ride's release window, which beat 04 was captured at.
    env['EM_LEVEL_SMOKE_UNTIL'] = 'elevator'
    run = subprocess.run([str(binary)], cwd=ROOT, env=env, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True)
    (OUT / 'run.log').write_text(run.stdout)
    if run.returncode != 0 or 'level smoke: PASS' not in run.stdout:
        sys.exit('collision world capture: FAIL (the level smoke run failed; build/collision_world/run.log)')
    disc = DISC.read_bytes()
    first, first_list = read_dump(Path(str(dump) + '.first'))
    last, last_list = read_dump(dump)
    assert len(first) == len(last) == len(disc)
    beat00, list00 = captured('00_panel_no_battery', len(disc))
    beat04, list04 = captured('04_elevator_ride', len(disc))
    failures, moved_first = compare('first dump vs 00_panel_no_battery', first, disc, beat00)
    more, moved_last = compare('last dump vs 04_elevator_ride', last, disc, beat04)
    failures += more
    # The ride moved the terminal's cell: the two dumps differ exactly there.
    spans = hulls(disc)
    changed = sorted(u for u, (lo, hi) in spans.items() if first[lo:hi] != last[lo:hi])
    if changed != [4]:
        failures.append(f'first and last dumps differ in uids {changed}, expected [4] (the ride)')
    if moved_first != sorted(PORT_MOVED) or moved_last != sorted(PORT_MOVED):
        failures.append(f'the port re-transformed {moved_first} / {moved_last}, expected {sorted(PORT_MOVED)}')
    # The last frame's published class-4 list: the original's entries whose
    # owners the port runs (the panel, the terminal, the items, and since
    # census L25 the drums, uids 5 and 6, and the crates, uids 7..10), in the
    # original order (newest push first = reverse pool-walk order).
    ported = [u for u in list04 if u in PORT_OWNERS or 19 <= u <= 26]
    if last_list != ported:
        failures.append(f'published class-4 list {last_list}, expected the original\'s ported '
                        f'owners {ported} (beat 04)')
    print(f'first dump: uids {moved_first} re-transformed, equal to 00_panel_no_battery; '
          f'published class-4 uids port {first_list}, original 00 {list00}')
    print(f'last dump: uid 4 at the lower floor, equal to 04_elevator_ride; '
          f'published class-4 uids port {last_list}, original 04 {list04}')
    failures += compare_boxes(boxes)
    print('not published by the port yet: ' +
          ', '.join(f'uid {u} ({why})' for u, why in NOT_PUBLISHED.items()))
    if failures:
        print('collision world capture: FAIL')
        for line in failures:
            print('  ' + line)
        return 1
    print('collision world capture: PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
