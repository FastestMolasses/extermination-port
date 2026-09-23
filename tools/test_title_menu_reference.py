#!/usr/bin/env python3
"""Execute the original title prompt 001AC480 and compare em_startup's menu.

001AC480 and its compositor 001AC7F0 run from the owner's local ELF. The
leaf services they call are recorded instead of executed: 00119828 (effect
level), 001FF080 (screen-module load; its busy flag D_00275BD8 is cleared
by the test when the synthetic load finishes), 001FB370 (resource poll),
001AEE10/001AEDE0 (fade in/out), 001FB9F0 (cue) and the GS draw leaves
001ABF90/00207D00/00207E40. The same per-tick inputs (held/pressed pad
words, transition substate D_0028A9A0, load readiness) drive em_startup's
title state (major 2) through a small C harness; each tick's service calls,
sub-state, cursor, idle timer, first-draw flag and exit verdict must agree.

It also executes anim_frame_top_a's first (state 0) tick for every single
held bit to confirm EM_STARTUP_ATTRACT_EXIT, the attract demo's button exit.

Scope: the cold-title context (D_00275BDC = 0, cursor starts at 0). The
from-death Continue prompt (D_00275BDC = 1) is not modelled by em_startup.
No original instruction bytes or data are embedded in or printed by it.
"""
import ctypes as C
import hashlib
from pathlib import Path
import random
import subprocess
import tempfile

from test_interaction_scan_reference import ELF_SHA
from test_input_block_reference import PadOracle

ROOT = Path(__file__).resolve().parents[1]
SLOT = 0x28A750
ENTRY = 0x1AC480
BUSY, FROM_DEATH, FADE = 0x275BD8, 0x275BDC, 0x28A9A0
HELD, PRESSED = 0x810E70, 0x810E74

HARNESS = r'''
#include "game/em_startup.h"
#include <string.h>
static EmStartup s;
static EmStartupEvent events[32];
static unsigned count;
static int module_ready, resources_ready;
static void notify(void *user, const EmStartupEvent *event)
{
    (void)user;
    if (count < 32) events[count++] = *event;
    if ((event->kind == EM_STARTUP_SCREEN_MODULE && module_ready) ||
        (event->kind == EM_STARTUP_TITLE_RESOURCES && resources_ready))
        em_startup_complete(&s, event->serial, 1);
}
void h_reset(void)
{
    em_startup_init(&s, notify, 0);
    s.flow = 1;          /* FLOW_TITLE */
    s.major = 2;         /* 001AC070 state 2: 001AC480 */
    s.cycle_mode = 1;
}
unsigned h_tick(unsigned held, unsigned pressed, int fade, int module, int resources)
{
    count = 0;
    module_ready = module;
    resources_ready = resources;
    if (s.pending_serial && s.pending_result == 0 &&
        ((s.pending_kind == EM_STARTUP_SCREEN_MODULE && module) ||
         (s.pending_kind == EM_STARTUP_TITLE_RESOURCES && resources)))
        em_startup_complete(&s, s.pending_serial, 1);
    EmStartupInput input = { (unsigned short)held, (unsigned short)pressed, fade, 0 };
    em_startup_tick(&s, &input);
    return count;
}
void h_event(unsigned i, int out[3])
{
    out[0] = events[i].kind; out[1] = events[i].id; out[2] = events[i].value;
}
unsigned h_attract_exit(void) { return EM_STARTUP_ATTRACT_EXIT; }
void h_state(unsigned out[5])
{
    out[0] = s.major; out[1] = s.sub; out[2] = s.aux; out[3] = s.cursor; out[4] = s.timer;
}
'''

# em_startup.h event kinds used by the title prompt.
FADE_IN, FADE_OUT, EFFECT_LEVEL, CUE = 8, 9, 12, 13
SCREEN_MODULE, TITLE_RESOURCES = 1, 4


class Title:
    def __init__(self, elf, native):
        self.native = native
        self.o = PadOracle(elf)
        self.o.save(0x70003B6C, SLOT)
        self.o.save(FROM_DEATH, 0, 1)
        self.events = []
        self.resources = 0
        rec = self.events.append
        calls = self.o.calls
        calls[0x119828] = lambda o: rec(('level', o.r[4] & 0xFF, o.r[5] & 0xFFFF))
        calls[0x1FF080] = lambda o: rec(('module', o.r[4] & 0xFF, o.r[5] & 0xFFFF))
        calls[0x1FB370] = lambda o: o.r.__setitem__(2, self.resources)
        calls[0x1AEE10] = lambda o: rec(('fade_in', o.r[4] & 0xFFFF))
        calls[0x1AEDE0] = lambda o: rec(('fade_out', o.r[4] & 0xFFFF))
        calls[0x1FB9F0] = lambda o: (rec(('cue', o.r[4] & 0xFFFF)), o.r.__setitem__(2, 0))
        for draw in (0x1ABF90, 0x207D00, 0x207E40):
            calls[draw] = lambda o: None
        native.h_reset()
        self.ticks = 0

    def tick(self, held, pressed, fade, module, resources):
        swap = lambda m: (m << 8 | m >> 8) & 0xFFFF
        self.events.clear()
        self.resources = resources
        if module:
            self.o.save(BUSY, 0, 1)
        self.o.save(FADE, fade, 2)
        self.o.save(HELD, held, 2)
        self.o.save(PRESSED, pressed, 2)
        self.o.run(ENTRY)
        verdict = self.o.r[2] & 0xFFFFFFFF
        expected_state = (self.o.load(SLOT + 9, 1), self.o.load(SLOT + 0xF, 1),
                          self.o.load(SLOT + 0x16, 2), self.o.load(SLOT + 0xA, 1))
        count = self.native.h_tick(swap(held), swap(pressed), fade, module, resources)
        actual_events = []
        for i in range(count):
            out = (C.c_int * 3)()
            self.native.h_event(i, out)
            kind, ident, value = out
            if kind == EFFECT_LEVEL:
                actual_events.append(('level', ident, value))
            elif kind == SCREEN_MODULE:
                actual_events.append(('module', 0, ident))
            elif kind == FADE_IN:
                actual_events.append(('fade_in', value))
            elif kind == FADE_OUT:
                actual_events.append(('fade_out', value))
            elif kind == CUE:
                actual_events.append(('cue', ident))
            elif kind != TITLE_RESOURCES:      # FB370 poll: no original call record
                actual_events.append(('other', kind, ident, value))
        expected_events = list(self.events)
        state = (C.c_uint * 5)()
        self.native.h_state(state)
        major, sub, aux, cursor, timer = state
        context = dict(tick=self.ticks, held=hex(held), pressed=hex(pressed), fade=fade,
                       module=module, resources=resources)
        assert actual_events == expected_events, dict(context, actual=actual_events,
                                                      expected=expected_events)
        self.ticks += 1
        if verdict:
            # 001AC070 dispatches 1 (confirm) to state 4+cursor and 3 (timeout)
            # to attract/movie; em_startup leaves major 2 in the same tick.
            if verdict == 1:
                assert major == 4 + expected_state[1], dict(context, major=major)
            else:
                assert verdict == 3 and major in (1, 3), dict(context, verdict=verdict,
                                                             major=major)
            return verdict
        assert major == 2, dict(context, major=major)
        actual_state = (sub, cursor, timer, aux if sub >= 2 else expected_state[3])
        assert actual_state == expected_state, dict(context, actual=actual_state,
                                                    expected=expected_state)
        return 0


def attract_exit(elf, native):
    swap = lambda m: (m << 8 | m >> 8) & 0xFFFF
    mask = swap(native.h_attract_exit())
    for bit in [0] + [1 << i for i in range(16)]:
        o = PadOracle(elf)
        o.save(0x70003B6C, SLOT)
        for leaf in (0x1D1EF0, 0x1AF2C0, 0x1FF080):
            o.calls[leaf] = lambda oracle: None
        o.save(HELD, bit, 2)
        o.run(0x1ACA20)                       # anim_frame_top_a, state 0
        verdict = o.r[2] & 0xFFFFFFFF
        assert verdict == (2 if bit & mask else 0), (hex(bit), verdict, hex(mask))
        # A returned 2 leaves the state byte; a pass advances to state 1.
        assert o.load(SLOT + 9, 1) == (0 if verdict else 1)
    return 17


def main():
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    with tempfile.TemporaryDirectory(prefix='em_title_menu_') as tmp:
        harness = Path(tmp) / 'harness.c'
        harness.write_text(HARNESS)
        library = Path(tmp) / 'title.so'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'src'), str(ROOT / 'src/game/em_startup.c'),
                        str(harness), '-o', str(library)], check=True)
        native = C.CDLL(str(library))
        native.h_tick.restype = C.c_uint
        native.h_tick.argtypes = [C.c_uint, C.c_uint, C.c_int, C.c_int, C.c_int]
        native.h_event.argtypes = [C.c_uint, C.POINTER(C.c_int)]
        native.h_state.argtypes = [C.POINTER(C.c_uint)]
        native.h_attract_exit.restype = C.c_uint
        attract = attract_exit(elf, native)
        rng = random.Random(0x1AC480)
        ticks = verdicts = 0
        outcomes = {1: 0, 3: 0}
        # Original swapped-layout pad words the prompt decodes, plus noise.
        buttons = (0x0800, 0x0040, 0x4000, 0x1000, 0x2000, 0x8000, 0x0010, 0x0020,
                   0x0080, 0x0100, 0x0004, 0x0840, 0x5000, 0x4040, 0x1800)

        def run(script):
            nonlocal ticks, verdicts
            title = Title(elf, native)
            for held, pressed, fade, module, resources in script:
                verdict = title.tick(held, pressed, fade, module, resources)
                ticks += 1
                if verdict:
                    verdicts += 1
                    outcomes[verdict] += 1
                    return verdict
            return 0

        # Loading gates in every order, then a timeout: 1200 idle ticks.
        for module_at, resources_at in ((0, 0), (3, 0), (0, 4), (5, 2), (2, 7)):
            script = [(0, 0, 0, int(t >= module_at), int(t >= resources_at))
                      for t in range(12)]
            script += [(0, 0, 0, 1, 1)] * 1200 + [(0, 0, 0, 1, 1), (0, 0, 3, 1, 1),
                                                   (0, 0, 2, 1, 1)]
            assert run(script) == 3
        # Timeout cancelled by a held button before black, then confirm.
        script = [(0, 0, 0, 1, 1)] * 1205 + [(0x0004, 0, 3, 1, 1), (0, 0, 1, 1, 1)]
        script += [(0x4000, 0x4000, 0, 1, 1), (0x0040, 0x0040, 0, 1, 1), (0, 0, 3, 1, 1),
                   (0, 0, 2, 1, 1)]
        assert run(script) == 1
        # Cursor clamps: DOWN past the last entry, UP past the first.
        script = [(0, 0, 0, 1, 1)] * 3
        for word in (0x4000,) * 4 + (0x1000,) * 4 + (0x4000, 0x0800):
            script += [(word, word, 0, 1, 1), (0, 0, 0, 1, 1)]
        script += [(0, 0, 2, 1, 1)]
        assert run(script) == 1
        # Random pad/fade sequences.
        for _ in range(400):
            script = [(0, 0, 0, 1, 1)] * rng.randrange(2, 5)
            held = 0
            for _ in range(rng.randrange(20, 120)):
                roll = rng.random()
                if roll < 0.45:
                    new = 0
                elif roll < 0.9:
                    new = rng.choice(buttons)
                else:
                    new = rng.randrange(65536)
                pressed = new & ~held & 0xFFFF
                if rng.random() < 0.1:
                    pressed = rng.randrange(65536) & new
                held = new
                fade = rng.choices((0, 1, 2, 3), (0.75, 0.08, 0.09, 0.08))[0]
                script.append((held, pressed, fade, 1, 1))
            run(script)
    print(f'title menu reference: PASS ({ticks} original 001AC480 ticks, '
          f'{outcomes[1]} confirms, {outcomes[3]} timeouts; attract exit gate '
          f'{attract} anim_frame_top_a cases)')


if __name__ == '__main__':
    main()
