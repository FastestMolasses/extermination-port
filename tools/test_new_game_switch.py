#!/usr/bin/env python3
"""EM_NEW_GAME=1 against the title's New Game (make test-new-game-switch).

The developer switch EM_NEW_GAME=1 (src/game/em_new_game_switch.h,
docs/STARTUP.md "Developer switches") skips the startup frontend
and enters New Game as 001AC070 state 4 does. This test proves that the
game state it starts the AREA11 opening with is the frontend route's.

Five headless runs write a state image at the opening's first frame
(EM_NEW_GAME_STATE_TEST, before any of that frame's work):
  title+N  EM_STARTUP_TEST=newgame with the title held N more frames
           (EM_STARTUP_MENU_WAIT=N, N = 0..3): the warning, Sony and Deep
           Space screens, the E900 movie, the title menu and its New Game;
  switch   EM_NEW_GAME=1.
An image holds every writable static section of the executable (every
module's static state), the module loader object and its modelled original
bytes, the IOP object, the field parity D_00810E80, the frontend's service
state and the title sequencer's pending sounds, and the process's mapped
regions.

The field parity at the opening depends on the frame the player presses New
Game, and every buffer the parity selects (render context, chain pages,
stream lists) differs with it. So the switch is compared with the title run
of its own parity (title+k), and the dwell reference is the next run of
that parity (title+k+2): the same route two frames later.

A word that differs between title+k and switch is accepted only as:
  - a pointer in both runs (into the image after its slide, or into a mapped
    region): where an allocation landed, not what the game holds;
  - host-only state, given by the image's "host:" blocks: the frontend's
    own state (em_frontend.c f, em_startup_audio.c s), which the game reads
    only through the service state and the title sequencer's pending
    sounds (those are compared and must be equal), the sfx driver's host
    clock and the frame pacing's wall-clock deadline (em_frame.c);
  - the IOP driver's command ring and its byte counters: the ring must be
    drained in every run (nothing still to run); its history and position
    count the commands issued since the boot, two more on the title route
    (001AC3B0 state 0's 001FBC50 before the E900 movie; the original adds
    two on every pass of the title's idle loop);
  - a word that also differs between title+k and title+k+2: a clock that
    counts the frames since the boot (DWELL: the main-loop counter, the field
    clocks of the stream lanes and the loader, the IOP clock). DWELL pins
    each symbol's words; a new symbol or more words fail the test until
    reviewed.
Anything else fails, with its symbol.

Also: the refusals (EM_NEW_GAME=2, with EM_SKIP_STARTUP=1, with a
frontend fixture). EM_TEST_FULL=1 adds newgame-control on both routes: the
same PASS line (30-tick displacement, end position, census).
"""
import bisect
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from reference_mode import FULL, banner  # noqa: E402

BIN = 'build/extermination'

# Symbols (nm names, or the heap blocks) whose words count the frames since
# the boot: the most words each may hold that differ between two title runs
# of the same parity, and why the game may start from any value of them.
DWELL = {
    '_s_frame': (1, 'em_frame.c: the main-loop counter 0x70003B64 (read only as a same-frame stamp)'),
    '_S': (3, 'em_stream_live.c: the field counter D_00810E90 and a lane\'s field stamp; em_chain_page_live.c: '
              'the page log\'s main-loop stamp (S.log.frame)'),
    '_R': (1, 'em_render_context_live.c: step V\'s kick count (R.kicks), one per frame'),
    'loader': (1, 'the module loader\'s drive clock: fields since the boot'),
    'iop': (4, 'the IOP clock (half lines, ticks, samples) and its SPU2 DMA stamp (ch0_tick)'),
}
WAITS = 4  # title+0 .. title+3: two runs of each parity

def run(name, env, path):
    e = dict(os.environ, EM_UNCAPPED='1', EM_HEADLESS='1', EM_NEW_GAME_STATE_TEST=path)
    for k in ('EM_STARTUP_TEST', 'EM_NEW_GAME', 'EM_SKIP_STARTUP', 'EM_STARTUP_MENU_WAIT'):
        e.pop(k, None)
    e.update(env)
    return subprocess.Popen([BIN], env=e, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def load(path):
    b = open(path, 'rb').read()
    if b[:8] != b'EMNGSTAT' or struct.unpack_from('<I', b, 8)[0] != 1:
        raise SystemExit(f'FAIL: {path} is not a version-1 state image')
    off, blocks = 12, {}
    while off < len(b):
        name = b[off:off + 24].split(b'\0')[0].decode()
        addr, size = struct.unpack_from('<QQ', b, off + 24)
        off += 40
        blocks[name] = (addr, b[off:off + size])
        off += size
    ref, lo, hi = struct.unpack('<QQQ', blocks['meta'][1])
    r = blocks['regions'][1]
    regs = sorted(struct.unpack_from('<QQ', r, i) for i in range(0, len(r), 16))
    return dict(blocks=blocks, ref=ref, lo=lo, hi=hi, regs=regs, starts=[a for a, _ in regs])


def symbols():
    out = subprocess.run(['nm', '-n', BIN], capture_output=True, text=True, check=True).stdout
    syms = []
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3:
            syms.append((int(p[0], 16), p[2]))
    ref = [a for a, n in syms if n in ('_em_new_game_state_test_before_frame', 'em_new_game_state_test_before_frame')]
    if len(ref) != 1:
        raise SystemExit('FAIL: em_new_game_state_test_before_frame not in the symbol table')
    return syms, ref[0]


SYMS, REF = symbols()
ADDRS = [a for a, _ in SYMS]


def sym(link):
    i = bisect.bisect_right(ADDRS, link) - 1
    return SYMS[i][1] if i >= 0 else hex(link)


def kind(run, v):
    """('z',), ('img', link address), ('ptr',) or ('val', v)."""
    if v == 0:
        return ('z',)
    if run['lo'] <= v < run['hi']:
        return ('img', v - (run['ref'] - REF))
    i = bisect.bisect_right(run['starts'], v) - 1
    if i >= 0 and run['regs'][i][0] <= v < run['regs'][i][1]:
        return ('ptr',)
    return ('val', v)


def differing(a, b, name, masked=()):
    """Offsets (8-aligned at runtime) of the words of block `name` that differ
    between runs a and b, pointers in both excepted, masked ranges skipped."""
    (aa, da), (ab, db) = a['blocks'][name], b['blocks'][name]
    if len(da) != len(db):
        raise SystemExit(f'FAIL: block {name} has {len(da)} bytes in one run, {len(db)} in the other')
    out, i, n = set(), 0, len(da)
    while i < n:
        if da[i] == db[i] or any(lo <= i < hi for lo, hi in masked):
            i += 1
            continue
        q = max(i - (aa + i) % 8, 0)
        va = int.from_bytes(da[q:q + 8].ljust(8, b'\0'), 'little')
        vb = int.from_bytes(db[q:q + 8].ljust(8, b'\0'), 'little')
        ka, kb = kind(a, va), kind(b, vb)
        if not ((ka[0] == 'ptr' and kb[0] == 'ptr') or (ka[0] == 'img' and kb[0] == 'img' and ka[1] == kb[1])):
            out.add(q)
        i = q + 8
    return out


def host_masks(run, name):
    """The frontend's host-state ranges inside static block `name` (block offsets)."""
    base, data = run['blocks'][name]
    masks = []
    for key, (_, payload) in run['blocks'].items():
        if key.startswith('host:'):
            at, size = struct.unpack('<QQ', payload)
            if base <= at < base + len(data):
                masks.append((at - base, at - base + size))
    return masks


def ring(run):
    ring_off, ring_size, w_off, r_off = struct.unpack('<QQQQ', run['blocks']['iop_ring'][1])
    iop = run['blocks']['iop'][1]
    w, r = struct.unpack_from('<I', iop, w_off)[0], struct.unpack_from('<I', iop, r_off)[0]
    mix_off, mix_size = struct.unpack('<QQ', run['blocks']['iop_mixer'][1])
    return [(ring_off, ring_off + ring_size), (w_off, w_off + 4), (r_off, r_off + 4),
            (mix_off, mix_off + mix_size)], w, r


def main():
    if not os.path.exists(BIN):
        raise SystemExit(f'FAIL: {BIN} missing; run make all')
    failures = []
    # The refusals: before any window, exit 1.
    for env, why in (({'EM_NEW_GAME': '2'}, 'expected 0 or 1'),
                     ({'EM_NEW_GAME': '1', 'EM_SKIP_STARTUP': '1'}, 'EM_SKIP_STARTUP'),
                     ({'EM_NEW_GAME': '1', 'EM_STARTUP_TEST': 'skip'}, 'tests the frontend')):
        e = dict(os.environ, EM_HEADLESS='1', **env)
        e.pop('EM_NEW_GAME_STATE_TEST', None)
        p = subprocess.run([BIN], env=e, capture_output=True, text=True, timeout=30)
        if p.returncode != 1 or why not in p.stderr:
            failures.append(f'refusal {env}: exit {p.returncode}, stderr {p.stderr.strip()[:200]!r}')

    with tempfile.TemporaryDirectory(prefix='new_game_switch_', dir='build') as tmp:
        names = tuple(f'title+{n}' for n in range(WAITS)) + ('switch',)
        envs = tuple({'EM_STARTUP_TEST': 'newgame', 'EM_STARTUP_MENU_WAIT': str(n)} for n in range(WAITS)) + \
            ({'EM_NEW_GAME': '1'},)
        paths = [os.path.join(tmp, f'{i}.bin') for i in range(len(names))]
        procs = [run(n, e, p) for n, e, p in zip(names, envs, paths)]
        controls = []
        if FULL:
            controls = [run(n, e, '') for n, e in (('title control', {'EM_STARTUP_TEST': 'newgame-control'}),
                                                   ('switch control', {'EM_NEW_GAME': '1',
                                                                       'EM_STARTUP_TEST': 'newgame-control'}))]
        logs = []
        for n, p in zip(names, procs):
            out = p.communicate(timeout=300)[0]
            logs.append(out)
            if p.returncode != 0 or 'new game state test: image written' not in out:
                raise SystemExit(f'FAIL: the {n} run (exit {p.returncode}):\n' + out[-2000:])
        if 'movie skipped' not in logs[-1] or 'no frontend' not in logs[-1] or 'startup: screen' in logs[-1]:
            failures.append('switch run: the frontend ran, or the intro movie was not skipped')
        runs = [load(p) for p in paths]
    titles, switch = runs[:-1], runs[-1]

    parity = [struct.unpack('<I', r['blocks']['frame_parity'][1])[0] for r in runs]
    same = [k for k in range(WAITS) if parity[k] == parity[-1]]
    if len(same) != 2 or same[1] != same[0] + 2:
        raise SystemExit(f'FAIL: the title runs\' parities {parity[:-1]} do not alternate (switch {parity[-1]})')
    k = same[0]
    title, title2 = titles[k], titles[k + 2]
    print(f'new game switch: field parity {parity[-1]}: switch against title+{k}, dwell from title+{k + 2}')

    statics = [n for n in title['blocks'] if n in ('__data', '__bss', '__common', '.data', '.bss')]
    if not statics:
        failures.append('no static sections in the image')
    # The game-facing state of the frontend, and the title's pending sounds.
    sv = [struct.unpack('<5i', r['blocks']['frontend_service'][1]) for r in runs]
    if any(v != sv[0] for v in sv) or sv[0][4] != 0 or sv[0][2] != 0:
        failures.append(f'frontend service {{installed, selector, movie open, failed, pending sounds}}: {sv}')
    # The IOP command ring: drained in every run.
    ring_masks = None
    for n, r in zip(names, runs):
        ring_masks, w, rd = ring(r)
        if w != rd:
            failures.append(f'{n}: the IOP command ring is not drained (write {w}, read {rd})')
    accepted, dwell_seen, counts = {}, {}, {}
    for name in statics + ['loader', 'loader_snapshot', 'iop']:
        masks = host_masks(title, name) if name in statics else ring_masks if name == 'iop' else ()
        if name in statics and any(host_masks(r, name) != masks for r in runs):
            failures.append(f'{name}: the host-state ranges differ between the runs')
        diff = differing(title, switch, name, masks)
        dwell = differing(title, title2, name, masks)
        counts[name] = (len(diff), len(diff & dwell))
        base = title['blocks'][name][0] - (title['ref'] - REF)
        for q in sorted(diff):
            owner = sym(base + q) if name in statics else name
            (aa, da), (_, db) = title['blocks'][name], switch['blocks'][name]
            if q in dwell:
                dwell_seen.setdefault(owner, []).append(f'+0x{q:x} {da[q:q + 8].hex()}/{db[q:q + 8].hex()}')
                continue
            accepted.setdefault(owner, []).append(f'+0x{q:x} title {da[q:q + 8].hex()} switch {db[q:q + 8].hex()}')
    for owner, rows in accepted.items():
        failures.append(f'{owner}: {len(rows)} word(s) differ outside the title dwell: ' + '; '.join(rows[:4]))
    for owner in sorted(dwell_seen):
        limit = DWELL.get(owner, (0, ''))[0]
        if len(dwell_seen[owner]) > limit:
            failures.append(f'{owner}: {len(dwell_seen[owner])} title-dwell word(s), {limit} reviewed (DWELL): ' +
                            '; '.join(dwell_seen[owner][:4]))

    passes = []
    for c, n in zip(controls, ('title control', 'switch control')):
        out = c.communicate(timeout=600)[0]
        line = [l for l in out.splitlines() if l.startswith('newgame control test: ')]
        if c.returncode != 0 or not line or 'PASS' not in line[-1]:
            failures.append(f'{n}: ' + (line[-1] if line else out[-500:]))
        else:
            passes.append(line[-1])
    if len(passes) == 2:
        print('new game switch: ' + passes[1])
        if passes[0] != passes[1]:
            failures.append(f'newgame-control differs: title {passes[0]!r} / switch {passes[1]!r}')

    print('new game switch: ' + ', '.join(f'{k} {d} differing words ({w} title-dwell)' for k, (d, w) in counts.items()))
    print('new game switch: title-dwell symbols: ' + ', '.join(f'{k} {len(v)}' for k, v in sorted(dwell_seen.items())))
    banner(f'{WAITS + 1} runs (title+0..{WAITS - 1}, switch) and 3 refusals' + (', newgame-control on both routes' if FULL else ''))
    if failures:
        for f in failures:
            print('FAIL: ' + f)
        sys.exit(1)
    print('new game switch: PASS (the opening starts from the title route\'s state)')


if __name__ == '__main__':
    main()
