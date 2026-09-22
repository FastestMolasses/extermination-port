#!/usr/bin/env python3
"""Original CDC0 panel route and E0C0 exit, with ordered worker boundaries."""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import subprocess
import sys

from test_item_root_reference import State as Item, FIELDS as ITEM_FIELDS
from test_panel_message_reference import Original as Base
from test_interaction_animation_reference import signed

ROOT = Path(__file__).resolve().parents[1]
UI, RETURN = 0x810130, 0xBADF00D
FIELDS = [('active', UI, 1), ('phase', UI + 1, 1), ('step', UI + 2, 1),
          ('transition_step', UI + 3, 1), ('saved_status', UI + 12, 1),
          ('current_status', 0x810C60, 1), ('request', 0x8106B0, 1),
          ('request_kind', 0x8106B1, 1), ('status_request', 0x8106C5, 1),
          ('restore_textures', 0x8106CC, 1), ('inventory_primary', 0x810CA4, 1),
          ('inventory_secondary', 0x810CA6, 1), ('saved_module', UI + 8, 4)]
ITEM_FIELDS = [(n, a - 0x900000 + UI if 0x900000 <= a < 0x900020 else a, s)
               for n, a, s in ITEM_FIELDS]


class State(C.Structure):
    _fields_ = [(n, C.c_uint8 if s == 1 else C.c_int32) for n, _, s in FIELDS] + [('item', Item)]


class Original(Base):
    def __init__(self, elf, state, buttons):
        super().__init__(elf, 0)
        for name, address, size in FIELDS:
            self.put(address, getattr(state, name), size)
        for name, address, size in ITEM_FIELDS:
            self.put(address, getattr(state.item, name), size)
        self.put(0x810E74, buttons, 2)
        self.events = []

    def run(self):
        self.r[31] = RETURN
        pc = 0x20CDC0
        workers = {0x1AED80: (0, (0,)), 0x20DFA0: (2, ()),
                   0x1AFEB0: (3, ()), 0x1AFE60: (4, ()),
                   0x200970: (6, (1,)), 0x20EE50: (7, (UI,)),
                   0x15C7B0: (9, (0x8102B0,)), 0x21BAE0: (10, (0,)),
                   0x20CD60: (12, ())}
        for _ in range(1800):
            if pc == RETURN:
                result = State(*(self.get(a, s) for _, a, s in FIELDS),
                               Item(*(self.get(a, s) for _, a, s in ITEM_FIELDS)))
                return result, self.events, signed(self.r[2])
            if pc in workers:
                event, args = workers[pc]
                assert tuple(self.r[4:4 + len(args)]) == args, (hex(pc), self.r[4:8], args)
                self.events.append((event, 1 if event == 6 else 0))
                pc = self.r[31] & 0xFFFFFFFF
                continue
            if pc == 0x1FB9F0:
                assert self.r[4] in (0xB, 0xD) and self.r[5:8] == [0x1000] * 3
                self.events.append((1 if self.r[4] == 0xB else 11, 0))
                pc = self.r[31] & 0xFFFFFFFF
                continue
            if pc == 0x1FF080:
                assert self.r[4] == 0
                self.events.append((5, self.r[5]))
                pc = self.r[31] & 0xFFFFFFFF
                continue
            assert (0x20CDC0 <= pc < 0x20D930 or 0x20E080 <= pc < 0x20E250 or
                    0x1FEF70 <= pc < 0x1FEFF0), hex(pc)
            word = self.get(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            target = None
            if op in (1, 4, 5, 6, 7, 20, 21):
                if op == 1:
                    assert rt in (0, 1)
                    taken = (signed(self.r[rs]) < 0) == (rt == 0)
                elif op in (4, 5, 20, 21):
                    taken = (self.r[rs] == self.r[rt]) == (op in (4, 20))
                elif op == 6:
                    taken = signed(self.r[rs]) <= 0
                else:
                    taken = signed(self.r[rs]) > 0
                target = pc + 4 + signed(word & 65535, 16) * 4 if taken else pc + 8
                if op < 20 or taken:
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
        raise AssertionError('Original status page failed to return')


def main():
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
    out = ROOT / 'build/status_page_reference'
    out.mkdir(parents=True, exist_ok=True)
    library = out / ('page.dylib' if sys.platform == 'darwin' else 'page.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
        '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-Isrc',
        'src/game/em_status_page.c', '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    Worker = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(State), C.c_int, C.c_uint)
    native.em_status_page_tick.argtypes = [C.POINTER(State), C.c_uint, Worker, C.c_void_p]
    checks = 0
    cases = []
    for current, primary, secondary, kind in itertools.product((1, 2), (0, 2), range(6), (0x40, 0x82, 0xFF)):
        cases.append((0, 0, 0, 0, 1, 0, 0, 0, current, primary, secondary, 0, kind))
    for phase, step, transition, busy, request, status, buttons, screen in itertools.product(
            (3, 4), range(3), range(3), (0, 1), (0, 1), (0, 0xFF), (0, 0x810), (0, 0x63)):
        if phase == 3 and step == 1 and transition == 0 and screen != 0:
            continue
        cases.append((phase, step, transition, busy, request, status, buttons, screen, 1, 0, 0, 0, 0x82))
    for step, busy, current, primary, secondary, saved in itertools.product(
            range(4), (0, 1), (1, 2), (0, 2), range(6), (-1, 0x32, 0x35)):
        cases.append((5, step, 0, busy, 1, 0xFF, 0, 0, current, primary, secondary, saved, 0x82))
    for phase, step, transition, busy, request, status, buttons, screen, current, primary, secondary, saved, kind in cases:
        initial = State(1, phase, step, transition, 1, current, request, kind, status, 0,
                        primary, secondary, saved, Item(2, 0, 5, screen, 3, 3, 0x21, busy, 4, 1, 7, 1))
        expected, events, wanted = Original(elf, initial, buttons).run()
        got = State.from_buffer_copy(initial)
        actual = []
        worker = Worker(lambda _, __, event, arg: (actual.append((event, arg)), 1)[1])
        result = native.em_status_page_tick(C.byref(got), buttons, worker, None)
        assert bytes(got) == bytes(expected), (phase, step, transition, list(bytes(got)), list(bytes(expected)))
        assert actual == events and result == wanted, (phase, step, actual, events, result, wanted)
        checks += 1
    # The next transition after root Back is the actual hub worker, not exit.
    state = State(1, 1, 0, 0, 1, 1, 0, 0x82, 0, 0, 0, 0, -1, Item())
    actual = []
    worker = Worker(lambda _, __, event, arg: (actual.append((event, arg)), -1)[1])
    assert native.em_status_page_tick(C.byref(state), 0, worker, None) == -1
    assert state.phase == 1 and actual == [(8, 1)]
    report = {'original_state_and_call_cases': checks, 'status_entry_and_exit': 'PASS',
              'module_reload_and_busy_gates': 'PASS', 'root_back_requires_actual_hub': 'PASS',
              'boundaries': 'UI camera/draw, sound, module I/O, ITEM/child/hub workers'}
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
