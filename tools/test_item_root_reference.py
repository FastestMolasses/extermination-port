#!/usr/bin/env python3
"""Original D930 table1/EE50 state and ordered-worker comparison.

The pad normalization and soft-double ABI are explicit boundaries. Draw,
sound, child-page and asynchronous asset workers remain explicit calls.

The draw (docs/STATUS_FIELD.md section 3e): 0020F170 and 0020F2A0 execute
(tools/export_item_root.py's executor, every leaf intercepted) for each
hover byte t[0x11] in 0..7 and 0xFF; their leaf calls, the background
0020A7A0(tex0), 00207D00(slot, mode), 00207E40(slot, x, y, w, h, rgba,
tex0) and 0020AC70 with the two floats it reads at D_700038A0 and its
reset flag, in order, must equal em_item_root_draw_0020F170 /
em_item_root_draw_0020F2A0's.
"""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import struct
import subprocess
import sys

from test_panel_message_reference import Original as Base
from clip_clock_oracle import signed
from test_player_reentry_reference import bits, number

ROOT = Path(__file__).resolve().parents[1]
UI, RETURN = 0x900000, 0xBADF00D
FIELDS = [('state', UI + 4, 1), ('step', UI + 5, 1), ('next_state', UI + 6, 1),
          ('screen', UI + 0x10, 1), ('hover', UI + 0x11, 1),
          ('selected', UI + 0x15, 1), ('module', UI + 0x16, 1),
          ('asset_busy', 0x275BD8, 1), ('message_mode', 0x2821B0, 4),
          ('message_phase', 0x2821B4, 4), ('message_line', 0x2821B8, 4),
          ('message_group', 0x282240, 4)]


class State(C.Structure):
    _fields_ = [(name, C.c_uint8 if size == 1 else C.c_uint32) for name, _, size in FIELDS]


class Original(Base):
    def __init__(self, elf, state, buttons=0, magnitude=0, angle=0):
        super().__init__(elf, 0)
        for name, address, size in FIELDS:
            self.put(address, getattr(state, name), size)
        self.put(0x810E74, buttons, 2)
        self.magnitude, self.angle = magnitude, angle
        self.events = []

    def run(self, entry):
        self.r[4:6] = [UI, 1]
        self.r[31] = RETURN
        pc = entry
        for _ in range(1600):
            if pc == RETURN:
                return State(*(self.get(a, n) for _, a, n in FIELDS)), self.events
            if pc == 0x1B62C0:
                assert self.r[4] == 0x700038A0
                self.put(0x700038A8, bits(self.magnitude))
                self.put(0x700038AC, bits(self.angle))
                pc = self.r[31] & 0xFFFFFFFF
                continue
            if pc == 0x128350:
                value = number(self.f[12])
                self.r[2] = struct.unpack('<Q', struct.pack('<d', value))[0]
                pc = self.r[31] & 0xFFFFFFFF
                continue
            if pc == 0x100130:
                left, right = (struct.unpack('<d', struct.pack('<Q', x & 0xFFFFFFFFFFFFFFFF))[0]
                               for x in self.r[4:6])
                assert left == number(bits(self.magnitude)) and right == 0.8
                self.r[2] = int(left >= right)
                pc = self.r[31] & 0xFFFFFFFF
                continue
            if pc == 0x1FB9F0:
                assert self.r[4:8] == [5, 0x1000, 0x1000, 0x1000]
                self.events.append((7, 5))
                pc = self.r[31] & 0xFFFFFFFF
                continue
            workers = {0x20E020: (0, 0), 0x20F170: (1, 0), 0x20F2A0: (2, 0),
                       0x20CD60: (3, 0), 0x20CD40: (4, 0),
                       0x214570: (6, 4), 0x2149F0: (6, 5),
                       0x215870: (6, 6), 0x2160B0: (6, 7)}
            if pc in workers or pc == 0x1FF080:
                if pc == 0x1FF080:
                    assert self.r[4] == 0
                    event = (5, self.r[5])
                else:
                    event = workers[pc]
                    if event[0] in (2, 6):
                        assert self.r[4] == UI
                self.events.append(event)
                pc = self.r[31] & 0xFFFFFFFF
                continue
            assert (0x20D930 <= pc < 0x20DF98 or 0x20EE50 <= pc < 0x20F170), hex(pc)
            word = self.get(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            target = None
            if op in (1, 4, 5, 6, 7, 20, 21) or (op == 17 and rs == 8):
                if op == 17:
                    taken = self.condition == bool(rt & 1)
                elif op == 1:
                    assert rt in (0, 1)
                    taken = (signed(self.r[rs]) < 0) == (rt == 0)
                elif op in (4, 5, 20, 21):
                    taken = (self.r[rs] == self.r[rt]) == (op in (4, 20))
                elif op == 6:
                    taken = signed(self.r[rs]) <= 0
                else:
                    taken = signed(self.r[rs]) > 0
                target = pc + 4 + signed(word & 65535, 16) * 4 if taken else pc + 8
                if (op < 20 and not (op == 17 and rt & 2)) or taken:
                    self.plain(self.get(pc + 4))
                pc = target
                continue
            if op in (2, 3):
                if op == 3:
                    self.r[31] = pc + 8
                target = (word & 0x3FFFFFF) << 2
            elif op == 0 and word & 63 == 8:
                target = self.r[rs] & 0xFFFFFFFF
            if target is not None:
                self.plain(self.get(pc + 4))
                pc = target
            else:
                try:
                    self.plain(word)
                except AssertionError as error:
                    raise AssertionError(hex(pc), error) from error
                pc += 4
        raise AssertionError('Original ITEM worker failed to return')


class Leaves(C.Structure):
    pass


Background = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint64)
Blend = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int32, C.c_int32)
Sprite = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int32, C.c_int32, C.c_int32, C.c_int32, C.c_int32, C.c_uint32,
                     C.c_uint64)
TrailLeaf = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_float, C.c_float, C.c_int32)
Leaves._fields_ = [('context', C.c_void_p), ('background', Background), ('blend', Blend), ('sprite', Sprite),
                   ('trail', TrailLeaf)]


def original_draw(elf, hover):
    """0020F170 then 0020F2A0's leaf calls, raw, from the executor of
    tools/export_item_root.py (its commands mirrored; its blend-mode
    bookkeeping turned back into the 00207D00 calls)."""
    from export_item_root import Original as Drawer
    raw = []

    class Commands(list):
        def append(self, c):
            super().append(c)
            if c['kind'] == 'background':
                raw.append(('bg', c['tex0']))
            elif c['kind'] == 'sprite':
                raw.append(('sprite', 1, signed(c['x']), signed(c['y']), signed(c['w']), signed(c['h']),
                            c['rgba'], c['tex0']))
            else:
                raw.append(('trail', c['x'], c['y'], 0))
            owner._quiet = c['kind'] != 'sprite'   # its own mode reset follows: not a call

    class Raw(Drawer):
        @property
        def mode(self):
            return self.__dict__.get('_mode', 0)

        @mode.setter
        def mode(self, value):
            if self.__dict__.get('_quiet'):
                self.__dict__['_quiet'] = False
            elif 'commands' in self.__dict__:
                raw.append(('blend', 1, value))
            self.__dict__['_mode'] = value

    owner = Raw(elf, hover)
    owner.commands = Commands()
    owner._quiet = False
    owner.collect(0x20F170)
    owner.collect(0x20F2A0)
    return raw


def native_draw(native, hover):
    raw = []
    leaves = Leaves(None, Background(lambda _, tex0: (raw.append(('bg', tex0)), 1)[1]),
                    Blend(lambda _, slot, mode: (raw.append(('blend', slot, mode)), 1)[1]),
                    Sprite(lambda _, *a: (raw.append(('sprite', *a)), 1)[1]),
                    TrailLeaf(lambda _, x, y, reset: (raw.append(('trail', x, y, reset)), 1)[1]))
    assert native.em_item_root_draw_0020F170(C.byref(leaves)) == 1
    assert native.em_item_root_draw_0020F2A0(C.byref(leaves), hover) == 1
    # a refusing worker stops the draw
    for stop in range(1, len(raw) + 1):
        seen = []

        def take(entry, n=stop):
            seen.append(entry)
            return 0 if len(seen) == n else 1
        cut = Leaves(None, Background(lambda _, tex0: take(('bg', tex0))),
                     Blend(lambda _, slot, mode: take(('blend', slot, mode))),
                     Sprite(lambda _, *a: take(('sprite', *a))),
                     TrailLeaf(lambda _, x, y, reset: take(('trail', x, y, reset))))
        ok = native.em_item_root_draw_0020F170(C.byref(cut)) == 1 and \
            native.em_item_root_draw_0020F2A0(C.byref(cut), hover) == 1
        assert not ok and len(seen) == stop, (hover, stop, len(seen))
    return raw


def main():
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
    out = ROOT / 'build/item_root_reference'
    out.mkdir(parents=True, exist_ok=True)
    library = out / ('item.dylib' if sys.platform == 'darwin' else 'item.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
        '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-Isrc',
        'src/game/em_item_root.c', 'src/game/em_menu_hover.c', '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    Worker = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(State), C.c_int, C.c_uint)
    native.em_item_root_tick.argtypes = [C.POINTER(State), C.c_uint, Worker, C.c_void_p]
    native.em_item_root_hover.argtypes = [C.POINTER(State), C.c_float, C.c_float]
    native.em_item_root_draw_0020F170.argtypes = [C.POINTER(Leaves)]
    native.em_item_root_draw_0020F2A0.argtypes = [C.POINTER(Leaves), C.c_uint8]
    draw_calls = 0
    for hover in (*range(8), 0xFF):
        want, got = original_draw(elf, hover), native_draw(native, hover)
        assert got == want, (hover, next((i, a, b) for i, (a, b) in enumerate(zip(got, want)) if a != b)
                             if len(got) == len(want) else (len(got), len(want)))
        lit = sum(1 for c in want if c[0] == 'sprite' and c[6] == 0x805FFF6E)
        assert (lit > 0) == (1 <= hover <= 5), (hover, lit)   # region `hover` lit, no other
        draw_calls += len(want)
    dispatch_checks = hover_checks = 0
    for state, step, busy, hover, selection, buttons in itertools.product(
            range(9), (0, 1), (0, 1), (0, 3, 5), range(7), (0, 0x20, 0x40, 0x60)):
        initial = State(state, step, 5, 0, hover, selection, 0x21, busy, 9, 9, 9, 9)
        expected, events = Original(elf, initial, buttons).run(0x20EE50)
        got = State.from_buffer_copy(initial)
        actual = []
        worker = Worker(lambda _, __, event, arg: (actual.append((event, arg)), 1)[1])
        assert native.em_item_root_tick(C.byref(got), buttons, worker, None) == 0
        assert bytes(got) == bytes(expected), (list(bytes(initial)), list(bytes(got)), list(bytes(expected)))
        assert actual == events, (state, step, actual, events)
        dispatch_checks += 1
    angles = [-3.2, 0, 3.2]
    for threshold in (-2.0071287, -0.5235988, 0.5235988, 2.0071287, 3.1415927):
        value = bits(threshold)
        angles.extend(number(value + delta) for delta in (-1, 0, 1))
    magnitudes = [0, 1] + [number(bits(0.8) + d) for d in (-1, 0, 1)]
    for magnitude, angle, old in itertools.product(magnitudes, angles, range(6)):
        initial = State(1, 0, 0, 0, old, 0, 0, 0, 0, 0, 0, 0)
        expected, events = Original(elf, initial, magnitude=magnitude, angle=angle).run(0x20D930)
        got = State.from_buffer_copy(initial)
        result = native.em_item_root_hover(C.byref(got), magnitude, angle)
        assert bytes(got) == bytes(expected), (magnitude, angle, old, got.hover, expected.hover)
        assert result == len(events)
        hover_checks += 1
    # Failed child/load/draw workers cannot be reported as successful ticks.
    for phase in (0, 1, 3, 4, 5, 6, 7):
        state = State(phase, 0, 5, 0, 3, 3, 0x21, 1, 0, 0, 0, 0)
        worker = Worker(lambda *_: -1)
        assert native.em_item_root_tick(C.byref(state), 0, worker, None) == -1
    report = {'dispatch_cases': dispatch_checks, 'hover_cases': hover_checks,
              'draw_hovers': 9, 'draw_leaf_calls_equal': draw_calls,
              'state_and_ordered_workers': 'PASS', 'double_0_8_threshold': 'PASS',
              'boundaries': 'pad normalization, drawings, sounds, child pages, asset I/O'}
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
