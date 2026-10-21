#!/usr/bin/env python3
"""The options binding (em_options_live) on the card screen past the slot
choice, against the ORIGINAL code, both over the host memory card.

docs/SAVE_GAME.md section 6 and docs/OPTIONS.md section 4. The card screen
00227300 and the code below it are translated in em_card_original /
em_save_original and checked routine by routine by
tools/test_card_screen_reference.py; this test checks the live binding
that runs them in the game: 00225D20's call of 00227300 dispatched to the
translation, the libmc SDK answered by em_memcard over the binding's views,
sprintf 00122D10 and strcmp 00123020 over the views, the slot rows' text
styles and line configs, the card code's buffers held by the binding, and
the place names read from the area-title module.

Both sides run whole card screens frame by frame from the fork recordings'
RAM (c10 opt_07 and dmg_05, the card screen's load row): the ORIGINAL
00225AC0(0) (its card screen state 1, the slot screen 00225D20 in its state
2, so every frame runs 00227300) in the reference interpreter, every callee
original code except the seven draw and cue leaves (0020A7A0, 00207D00 /
00207E40 / 00207F80, 001CC1E0, 001FC770, 001FB9F0: each recorded with the
values the original passes and answered 0; all seven are void or their
result is discarded at these call sites), the libmc SDK answered by
em_memcard (tools/test_memcard_host's own build) on a host card folder of
its own; and the game's binding
(tests/options_card_live_fixture.c: em_options_live_00225AC0 over the same
bytes) on a second folder holding the same files. Each frame both get the
same pad words, made on the original alone from the screen's state
(test_card_screen_reference.policy). After every frame these must be equal:
00225AC0's result, the card record D_00810040 (0xD4 bytes), the card
helpers' globals, the progress block's canonical bytes, the binding's own
card buffers (GetDir's table D_00821500, the header D_00821980, the staging
blocks D_008219D0 / D_00822050 / D_00822690, the slot path D_00822010, the
row text D_00275C94..9F), the frame's draw and cue calls in order (the
original's arguments, its text and style bytes, against what the binding
hands to its draw and sound callees: em_options_live_set_trace, logged
inside wrappers from the parameters they forward) and, at the end, the two
host folders.

LOAD GAME's read (00228B60) writes the whole progress block D_00810700,
which the port does not yet hold in one storage (em_scene_state.h: only its
canonical ranges): there the binding must fail-stop on the very frame the
original issues that Read, with every frame before it equal.

Every flow on both recordings, about 9 s warm, 30 to 50 s on a first run (it
builds the fixture); no EM_TEST_FULL variant.
"""
import ctypes as C
import hashlib
import os
import re
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
os.environ.setdefault('EM_LANE', 'card')
import test_card_screen_reference as CS  # noqa: E402
import test_memcard_host as MH  # noqa: E402
import test_save_reference as S  # noqa: E402
import test_area01_render_reference as R  # noqa: E402
import test_area11_interaction_host as IH  # noqa: E402
from test_player_slide_reference import RETURN, STACK_TOP  # noqa: E402

MASK = 0xFFFFFFFF
MC = 0x810040
OUT = ROOT / 'build' / 'options_card_live'
EMSP = ROOT / 'assets/status_pages/status_pages.emsp'
EMMD = ROOT / 'assets/message/message_data.emmd'
DIR = 'BASCUS-97112-DS00-00'
E70, E74, E78 = 0x810E70, 0x810E74, 0x810E78
PACKET = CS.PACKET
OWN = ((0x821500, 0x480), (0x821980, 0x48), (0x8219D0, 0x640), (0x822010, 0x40), (0x822050, 0x640),
       (0x822690, 0x640), (0x275C94, 12))
GLOBALS = ((0x275C58, 0x18), (0x264E30, 0x0C), (0x275840, 0x08))
READ = 0x114630
FRAMES = 600
CFLAGS = ['-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-Isrc']
MODULES = IH.MODULES        # the interaction host's options binding and everything it links
EXPORTS = ['init', 'message_size', 'card', 'globals', 'memcard_fault', 'frame', 'read', 'seed', 'calls',
           'trace']


def build_fixture():
    obj = OUT / 'obj'
    obj.mkdir(parents=True, exist_ok=True)
    sources = ['src/game/%s.c' % m for m in MODULES] + ['src/em_model.c', 'src/em_input.c',
                                                         'src/platform/linux/em_gamepad_linux.c',
                                                         'tests/options_card_live_fixture.c']
    headers = sorted(str(p.relative_to(ROOT)) for p in (ROOT / 'src').rglob('*.h'))
    hh = hashlib.sha256(repr(CFLAGS).encode())
    for h in headers:
        hh.update(h.encode())
        hh.update((ROOT / h).read_bytes())
    objs = []
    for src in sources:
        o = obj / (Path(src).stem + '.o')
        stamp = o.with_suffix('.sha256')
        key = hashlib.sha256(hh.digest() + (ROOT / src).read_bytes()).hexdigest()
        if not (o.exists() and stamp.exists() and stamp.read_text() == key):
            subprocess.run(['cc'] + CFLAGS + ['-c', src, '-o', str(o)], cwd=ROOT, check=True)
            stamp.write_text(key)
        objs.append(str(o))
    lib = OUT / 'fixture.dylib'
    exports = OUT / 'exports.txt'
    exports.write_text(''.join('_fx_%s\n' % n for n in EXPORTS))
    subprocess.run(['cc', '-shared', '-Wl,-dead_strip', '-Wl,-exported_symbols_list,' + str(exports),
                    '-o', str(lib), *objs, '-lm'], cwd=ROOT, check=True)
    fx = C.CDLL(str(lib))
    P8, P16 = C.POINTER(C.c_uint8), C.POINTER(C.c_uint16)
    fx.fx_init.argtypes = [C.c_char_p, C.c_char_p]
    fx.fx_card.argtypes = [C.c_char_p, P8, P8, P8]
    fx.fx_globals.argtypes = [P8, P8, P8]
    fx.fx_memcard_fault.restype = C.c_char_p
    fx.fx_frame.argtypes = [P8, C.c_uint32, P8, P8, P8, P8, P8, P16, C.POINTER(C.c_int16), C.c_uint8,
                            C.c_int16, C.c_uint16, C.c_uint16, C.c_uint16, C.c_uint16, C.c_uint8,
                            C.c_uint8, C.c_uint8, C.c_uint32, C.POINTER(C.c_uint32)]
    fx.fx_read.argtypes = [C.c_uint32, P8, C.c_uint32]
    fx.fx_seed.argtypes = [C.c_uint32, P8, C.c_uint32]
    fx.fx_calls.argtypes = [C.c_uint32]
    fx.fx_calls.restype = C.c_uint
    fx.fx_trace.argtypes = [P8, C.c_uint32]
    fx.fx_trace.restype = C.c_int
    return fx


def canonical_ranges():
    """em_scene_progress_canonical's table (the progress block's bytes the
    port holds in one storage), read from the header the binding compiles."""
    text = (ROOT / 'src/game/em_scene_state.h').read_text()
    body = text[text.index('em_scene_progress_canonical'):]
    body = body[:body.index('};')]
    return [(int(a, 16), int(b, 16)) for a, b in re.findall(r'\{0x([0-9A-Fa-f]+)u,\s*0x([0-9A-Fa-f]+)u\}', body)]


# The draw and cue calls the binding passes on (em_options_live_set_trace's
# layout, em_options_live.h): on the oracle's side each is answered 0 and
# recorded with the values the original passes (the text up to its NUL, the
# style record's 8 bytes at its pointer, 001FC770's config words), so each
# frame's sequence must equal what the binding decodes and hands to
# em_page_draw / em_message_live / the host's sound. The packets and sounds
# themselves are those modules' own tests'. 001CC170 (the text width, which
# places text) and the cue wrappers 0020CD40 / 60 / A0 run as original code;
# the cue is their 001FB9F0(id, a1, a2, a3).
MASK64 = (1 << 64) - 1
WORDS = {0x207D00: 2, 0x207E40: 6, 0x207F80: 6, 0x1FB9F0: 4}
TEXT_MAX = 256


def text_bytes(e, address):
    out = bytearray()
    while len(out) < TEXT_MAX:
        b = e.load(address + len(out), 1)
        if not b:
            break
        out.append(b)
    return struct.pack('<I', len(out)) + bytes(out)


def style_bytes(e, address):
    if not address:
        return b'\0'
    return b'\1' + bytes(e.load(address + i, 1) for i in range(8))


def recorder(callee):
    def hook(e):
        a = [e.r[4 + i] for i in range(8)]
        if callee == 0x20A7A0:
            rec = struct.pack('<Q', a[0] & MASK64)
        elif callee in WORDS:
            rec = struct.pack('<%dI' % WORDS[callee], *(x & MASK for x in a[:WORDS[callee]]))
            if callee == 0x207E40:
                rec += struct.pack('<Q', a[6] & MASK64)
        elif callee == 0x1CC1E0:
            rec = struct.pack('<5I', *(x & MASK for x in a[:5])) + text_bytes(e, a[5] & MASK)
            rec += style_bytes(e, a[6] & MASK)
        else:                          # 0x1FC770(x, y, text, cfg)
            rec = struct.pack('<2I', a[0] & MASK, a[1] & MASK) + text_bytes(e, a[2] & MASK)
            cfg = a[3] & MASK
            if cfg:
                rec += b'\1' + struct.pack('<5I', *(e.load(cfg + 4 * k, 4) for k in range(5)))
                rec += style_bytes(e, e.load(cfg + 0x14, 4))
            else:
                rec += b'\0'
        e.trace.append((callee, rec))
        key = {0x1CC1E0: a[6] & MASK, 0x1FC770: a[3] & MASK}.get(callee, 0)
        COVER[(callee, key)] = COVER.get((callee, key), 0) + 1
        e.r[2] = 0
    return hook


DRAWS = (0x20A7A0, 0x207D00, 0x207E40, 0x207F80, 0x1CC1E0, 0x1FC770, 0x1FB9F0)
COVER = {}                             # (callee, style or config address) -> calls compared


def live_trace(fx):
    buf = (C.c_uint8 * (1 << 16))()
    n = fx.fx_trace(buf, len(buf))
    assert n >= 0, 'the binding\'s draw log overflowed'
    raw, out, i = bytes(buf[:n]), [], 0
    while i < n:
        callee, size = struct.unpack_from('<2I', raw, i)
        i += 8
        assert size != 0xFFFFFFFF, ('a draw record overflowed', hex(callee))
        out.append((callee, raw[i:i + size]))
        i += size
    return out


def arr(data):
    return (C.c_uint8 * len(data)).from_buffer(bytearray(data))


class Live:
    """The binding's storage for one flow, from the image."""

    def __init__(self, fx, ram, spad):
        self.fx = fx
        sp = lambda a, n: bytes(spad[a - 0x70000000:a - 0x70000000 + n])  # noqa: E731
        self.task_address = struct.unpack('<I', sp(0x70003B6C, 4))[0]
        self.settings = bytearray(ram[0x810118:0x810128])
        self.task = bytearray(ram[self.task_address + 8:self.task_address + 0x20])
        self.mc = bytearray(ram[MC:MC + 0xD4])
        self.progress = bytearray(ram[0x810700:0x810D40])
        self.message = bytearray(ram[0x2821B0:0x2821B0 + fx.fx_message_size()])
        self.busy = bytearray(ram[0x275BD8:0x275BD9])
        self.masks = (C.c_uint16 * 8)(*struct.unpack('<8H', sp(0x70003B74, 16)))
        self.offset = (C.c_int16 * 2)(*struct.unpack('<2h', sp(0x70003B94, 4)))
        self.read_phase = ram[0x282157]
        self.fade = struct.unpack_from('<h', ram, 0x28A9A0)[0]
        self.held = struct.unpack_from('<H', ram, E70)[0]
        self.pad_mode = struct.unpack_from('<H', ram, 0x810E6A)[0]
        self.pad_phase = ram[0x810E50]
        self.b90, self.b93 = sp(0x70003B90, 1)[0], sp(0x70003B93, 1)[0]
        self.bufs = [self.settings, self.task, self.mc, self.progress, self.message, self.busy]
        self.ptrs = [(C.c_uint8 * len(b)).from_buffer(b) for b in self.bufs]

    def frame(self, e74, e78):
        r = C.c_uint32(0)
        p = self.ptrs
        rc = self.fx.fx_frame(p[0], self.task_address, p[1], p[2], p[3], p[4], p[5], self.masks, self.offset,
                              self.read_phase, self.fade, self.held, e74, e78, self.pad_mode, self.pad_phase,
                              self.b90, self.b93, 0, C.byref(r))
        return rc, r.value

    def own(self, address, size):
        out = (C.c_uint8 * size)()
        assert self.fx.fx_read(address, out, size) == 0, hex(address)
        return bytes(out)

    def globals(self):
        bufs = [(C.c_uint8 * n)() for _a, n in GLOBALS]
        self.fx.fx_globals(*bufs)
        return [bytes(b) for b in bufs]


def write_card(root, port, files):
    for p in (0, 1):
        (Path(root) / ('slot%d' % (p + 1))).mkdir(parents=True, exist_ok=True)
    for name, data in (files or {}).items():
        path = Path(root) / ('slot%d' % (port + 1)) / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def flows():
    """(beat, knobs): the load-mode whole screens of test_card_screen_reference
    the host card can serve (its card always reports a formatted PS2 card;
    a failing or missing card is not something em_memcard answers)."""
    out = []
    for beat in ('o7', 'd5'):
        def add(end, **k):
            out.append((beat, dict(k, end=end)))
        for slot in range(5):
            for port in (0, 1):
                if port and slot not in (1, 4):
                    continue
                add('read', port=port, present=(0, 1, 2, 3, 4), goal={'slot': slot})
        add(2, present=(1, 3), goal={'slot': 1, 'yes': False, 'then': 0x10})      # No, then Triangle
        add(1, present=(2,), goal={'slot': 0, 'pick': 0x20})                       # Circle on the list
        add(2, present=(2,), goal={'slot': 0, 'pick': 0x10})                       # Triangle on the list
        add(2, present=None, goal={'slot': 0, 'pick': 0x10})                       # no directory
        add(1, present=(2,), drop=('icon.sys',), goal={'err': 0x40})               # the card refused
        add(2, present=(2,), drop=('icon.sys',), goal={'err': 0x10})
        add(1, present=(2,), drop=('EX_DATA.03',), goal={'err': 0x40})             # one save file missing
        add('read', port=1, present=(0, 3), goal={'slot': 3})                      # rows with saves 0 and 3
    return out


def run_flow(fx, tmp, tag, beat, knobs, ranges, stats):
    port, present = knobs.get('port', 0), knobs['present']
    files = None if present is None else CS.card_files(beat, set(present), knobs.get('drop', ()))
    roots = [Path(tmp) / tag / side for side in ('oracle', 'live')]
    for r in roots:
        write_card(r, port, files)
    b = CS.rec(2, port) + CS.helpers() + CS.records(beat, ()) + [(MC, 1, 1), (MC + 0x16, 0, 1)]
    ram = bytearray(R.IMAGES[beat][0])
    for at, v, size in b:
        ram[at:at + size] = (v & ((1 << 8 * size) - 1)).to_bytes(size, 'little')
    spad = bytes(R.IMAGES[beat][1])
    ee = MH.original_ee(bytes(ram), spad, roots[0])
    for a in DRAWS:
        ee.hooks[a] = recorder(a)
    ee.trace = []
    live = Live(fx, ram, spad)
    g = [arr(bytes(ram[a:a + n])) for a, n in GLOBALS]
    assert fx.fx_card(str(roots[1]).encode(), *g) == 1, tag
    for a, n in OWN:                   # the binding's buffers start as the recording holds them
        assert fx.fx_seed(a, arr(bytes(ram[a:a + n])), n) == 0, hex(a)
    packet = ee.load(PACKET)
    goal = knobs['goal']
    then, confirming, reads = None, False, 0
    for frame in range(FRAMES):
        e74, e78 = CS.policy(ee, goal, frame)
        if then is not None:
            e74, e78 = (0, 0) if frame & 1 else (then, 0)
        for at, v, n in ((E74, e74, 2), (E78, e78, 2), (PACKET, packet, 4)):
            ee.save(at, v, n)
        ee.r[4], ee.r[29], ee.r[31] = 0, STACK_TOP, RETURN
        calls0 = len(ee.calls)
        ee.trace = []
        ee.run(0x225AC0)
        want = ee.r[2] & MASK
        read_now = [c for c in ee.calls[calls0:] if c[0] == READ and c[1][1] == 0x810700]
        rc, got = live.frame(e74, e78)
        if read_now:
            # LOAD GAME's read of the slot into D_00810700: the binding stops here
            fault = fx.fx_memcard_fault()
            assert knobs['end'] == 'read', (tag, 'LOAD GAME read on a flow that should not read', frame)
            assert rc == -1 and fault and b'outside the caller' in fault, (tag, frame, rc, fault)
            assert bytes(ee.mem[0x810700:0x810D40]) == files[DIR + '/EX_DATA.%02d' % goal['slot']], tag
            reads += 1
            return frame + 1, 'read'
        assert rc == 0, (tag, 'the binding faulted', frame, fx.fx_memcard_fault())
        assert got == want, (tag, frame, 'result', got, want)
        drawn = live_trace(fx)
        if drawn != ee.trace:
            k = next((i for i, (x, y) in enumerate(zip(drawn, ee.trace)) if x != y), min(len(drawn), len(ee.trace)))
            raise AssertionError((tag, frame, 'draw / cue call %d differs' % k, len(drawn), len(ee.trace),
                                  [(hex(c), r.hex()) for c, r in drawn[k:k + 1]],
                                  [(hex(c), r.hex()) for c, r in ee.trace[k:k + 1]]))
        stats['draws'] += len(drawn)
        stats['cues'] += sum(1 for c, _r in drawn if c == 0x1FB9F0)
        mc = bytes(ee.mem[MC:MC + 0xD4])
        assert bytes(live.mc) == mc, (tag, frame, 'D_00810040', [(hex(i), live.mc[i], mc[i])
                                                                  for i in range(0xD4) if live.mc[i] != mc[i]][:8])
        for (a, n), got_g in zip(GLOBALS, live.globals()):
            assert got_g == bytes(ee.mem[a:a + n]), (tag, frame, hex(a), got_g.hex(), bytes(ee.mem[a:a + n]).hex())
        for a, n in OWN:
            assert live.own(a, n) == bytes(ee.mem[a:a + n]), (tag, frame, 'buffer', hex(a))
        for lo, hi in ranges:
            assert bytes(live.progress[lo - 0x810700:hi - 0x810700]) == bytes(ee.mem[lo:hi]), (tag, frame, hex(lo))
        # the declined confirmation, back on the cursor: the goal's exit press
        on_list = ee.load(MC + 1, 1) == 2
        if on_list and ee.load(MC + 2, 1) == 1:
            confirming = True
        elif on_list and ee.load(MC + 2, 1) == 0 and confirming and goal.get('yes') is False:
            then = goal.get('then', 0x10)
        if ee.load(MC + 0x15, 1) != 2 or want:
            # 00227300 returned: 1 -> the slot screen's card choice, 2 -> out
            ended = 1 if ee.load(MC + 0x15, 1) == 1 else 2
            assert ended == knobs['end'], (tag, frame, 'ended with', ended, 'not', knobs['end'])
            o, n = MH.card_files(roots[0], port), MH.card_files(roots[1], port)
            assert o == n, (tag, 'the host cards differ')
            return frame + 1, ended
    raise AssertionError((tag, 'no end in %d frames' % FRAMES))


def main():
    t0 = time.time()
    for asset in (EMSP, EMMD, ROOT / 'assets/area_title.emat'):
        if not asset.exists():
            raise SystemExit(f'{asset} is missing (run the exporters: docs/OPTIONS.md section 9)')
    MH.LIB = MH.build_lib()
    CS.install()
    S.ELF = R.ELF = S.read_elf()
    import test_area01_ui_reference as U
    U.ELF = S.ELF
    S.load_images()
    fx = build_fixture()
    cwd = os.getcwd()
    os.chdir(ROOT)                     # the binding opens assets/area_title.emat relative to it
    try:
        assert fx.fx_init(str(EMSP).encode(), str(EMMD).encode()) == 1, 'the fixture did not start'
    finally:
        os.chdir(cwd)
    items = flows()
    chosen = items                     # all of them
    ranges = canonical_ranges()
    frames, ends, stats = 0, {}, {'draws': 0, 'cues': 0}
    with tempfile.TemporaryDirectory(prefix='em_options_card_') as tmp:
        for i, (beat, knobs) in enumerate(chosen):
            t1 = time.time()
            n, how = run_flow(fx, tmp, 'flow%d' % i, beat, knobs, ranges, stats)
            print('  flow %d %s %s: %d frames, %s (%.1f s)' % (i, beat, knobs['goal'], n, how, time.time() - t1),
                  flush=True)
            frames += n
            ends[how] = ends.get(how, 0) + 1
    # every draw variant the binding decodes was compared: the slot rows'
    # style D_00275898 (a .data window) and no style, 001FC770's configs
    # D_00264CB0, D_002671A0 and 0, the page draws and the cues
    for key in ((0x1CC1E0, 0), (0x1CC1E0, 0x275898), (0x1FC770, 0), (0x1FC770, 0x264CB0),
                (0x1FC770, 0x2671A0), (0x207D00, 0), (0x207E40, 0), (0x207F80, 0), (0x20A7A0, 0), (0x1FB9F0, 0)):
        assert COVER.get(key), ('no flow compared', hex(key[0]), hex(key[1]))
    print(f'{len(chosen)} whole card screens on both sides ({frames} frames equal; ends {ends}; '
          f'{stats["draws"]} draw and cue calls equal, {stats["cues"]} of them cues)')
    print('elapsed %.1f s' % (time.time() - t0))
    print('options card live: PASS')


if __name__ == '__main__':
    main()
