#!/usr/bin/env python3
"""Original-instruction oracles for the seven census rows marked unverified.

docs/FIRST_LEVEL_CENSUS.md (recount 2026-09-24) listed seven functions whose
port code nothing original had checked: 0015AC00, 0015CF90, 001B1190,
001C5680, 001C5760, 001CF470 and 0020DFA0. This test runs each ORIGINAL
routine from the user's pinned ELF (tools/test_player_fall_reference.py
FallEE: every COP1 and VU0 macro operation on the measured EE float model)
over synthetic records and over the captured RAM of route beats 00..14, and
compares the result with the port's existing code. Findings and the fixes
they call for: docs/CENSUS_UNVERIFIED.md.

The port code is used as it is, never edited:
  * em_pickup.c, em_props.c and em_status_models.c are compiled whole inside
    small host harnesses (`#include` of the module, so its static functions
    and state are reachable); only the storage getters the modules call
    (em_scene_state, em_random_next, em_game_terminal_powered, g) are
    supplied by the harness, the logic under test is the module's own.
  * Two pieces of live code sit inside large functions that cannot run in
    isolation: the 0015CF90 lines of em_player_0015BCF0 (em_player_frame.c)
    and the 0015AC00 switches of em_area11_interaction_host_pickup_state0.
    The harness copies those exact source lines (located by their text; the
    test fails when the text moves) into a function and compiles them
    against the real headers.

Hooked callees are recorded boundaries, never simulated as claims about the
callee. Known divergences are pinned in EXPECTED: the run fails when a new
one appears or when a pinned one disappears (then update the doc).

EM_TEST_FULL=1 runs every type byte / uid / triangle; the default run keeps
every case class and boundary plus a fixed-seed sample. No original
instruction bytes, assets or disassembly are included in this file.
"""
import ctypes as C
import hashlib
import json
import random
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))

import reference_mode as RM  # noqa: E402
import ee_float_model as M  # noqa: E402
import test_render_verify_rest_reference as RVR  # noqa: E402
from test_player_slide_reference import (read_elf, sx32, s32, bits, number,  # noqa: E402
                                         DECOMP, ELF_SHA256)

ROUTE = DECOMP / 'build/s87/route'
BUILD = ROOT / 'build/census_unverified_reference'
SRC = ROOT / 'src'
MANIFEST = ROOT / 'assets/scene_snow/scene.txt'

STACK_LO, STACK_HI = 0x7F000000, 0x7F100000
F1, F15, F2, F4 = 0x3F800000, 0x3FC00000, 0x40000000, 0x40800000

# Pinned divergences (docs/CENSUS_UNVERIFIED.md). Key -> short reason.
EXPECTED = {
    '0015AC00/early-return': 'bone-slot refusal (001B0FD0/001B1020 != 0) keeps the original in state 0; '
                             'the host state 0 has no such path (port convention: slots always available)',
    '0015AC00/scale-219550': 'em_pickup_add applies the 0015AC00 scale to 00219550 items too; 00219550 '
                             'state 0 never writes +0x60 (latent: every AREA11 00219550 item is model 0x72)',
    '0015CF90/c.le-daz': 'health <= 0 is a native compare; the EE compare reads a positive denormal as 0 '
                         'and a negative NaN pattern as a large negative number',
    '001B1190/area>0x16': 'taken_byte refuses areas past 0x16; the original writes D_00810860 + area*32 '
                          'for any D_00810700 (latent: not reachable in the first level)',
    '001C5680/init-refused': '001C2360 returning 1 (bone slots exhausted) keeps the original child in '
                             'state 0; the port aggregate always initializes',
    '001C5680/status2': 'a child +4 == 2 frees the original child; the pickup-light aggregate keeps drawing '
                        'unless its owner status is 3 (latent: nothing writes 2)',
    '001C5680/missing-7A-draw': 'the 0x825940 child (+0xD 0x7A) is spawned but never drawn: the port draws '
                                'one 001F54E0 fewer per frame and every later RNG consumer shifts',
    '001C5760/free': 'tick_indicators frees only 001C5680 nodes on +4 2/3; the original 001C5680 and '
                     '001C5760 both free on any +4 > 1 (measured 2, 3, 4, 0x80; latent in AREA11)',
    '001C5760/alt-matrix': '+0xA != 0 re-runs 001C6380 before each draw; em_props has no such path '
                           '(latent: AREA11 spawns +0xA 0)',
    '001CF470/missing': 'no port translation of 001CF470 (nor of its caller 001CE300)',
    # One key per CONFIGURE callee the port does not run, so a partial fix
    # retires exactly its own key.
    '0020DFA0/missing-0020E020': 'CONFIGURE does not reset the trail D_00821300/D_00275C90 (request-opened '
                                 'pages skip both port trail resets)',
    '0020DFA0/missing-0021BAC0': 'CONFIGURE does not save the fog block (0021BAC0(0))',
    '0020DFA0/missing-0021B9A0': 'CONFIGURE does not program the fog (0021B9A0(5, 0.0, 1e6); no translation)',
    '0020DFA0/missing-0021B970': '001D2610\'s fog range 0021B970(ctx+0xF8, ctx+0xFC) is not re-set',
}

# 001CF470 reference (no port translation yet): the original's fan-size
# histogram per mode and the first 128 bits of its SHA-256 fan digest
# (report.json keeps the whole digest), pinned so the oracle cannot drift.
CF470_REFERENCE = {
    'quick': ({0: 22, 3: 10, 4: 7, 6: 1}, 'de32ba29d0d16da2dd826454bcb73b8f'),
    'full': ({0: 214, 3: 88, 4: 71, 5: 21, 6: 6},
             'fbd75a9b75b6899bc21993c4958841ca'),
}


def fbits(value):
    return bits(C.c_float(value).value)


def w32(buf, at):
    return struct.unpack_from('<I', buf, at)[0]


# ============================================================ the oracle core
class Oracle(RVR.RvrEE):
    """The render lane's RvrEE (FallEE + the clip flag, its CFC2/CTC2 and two
    MMI word interleaves) plus the VU0 macro absolute value, and a record of
    every non-stack byte stored."""

    def macro(self, word):
        op = word & 63
        if op >= 60 and ((word >> 6 & 31) << 2 | (op & 3)) == 0x1D:          # abs, per masked lane
            fs, ft, mask = word >> 11 & 31, word >> 16 & 31, word >> 21 & 15
            src = [v & 0xFFFFFFFF for v in self.vf[fs]]
            for lane in range(4):
                if mask & (8 >> lane) and ft:
                    self.vf[ft][lane] = M.vu_abs(src[lane])
            return
        super().macro(word)

    def mmi(self, word, pc):
        fn, sub, rt, rd = word & 63, word >> 6 & 31, word >> 16 & 31, word >> 11 & 31
        if fn == 0x09 and sub in (0x1E, 0x1F):     # word exchange / three-word rotate of rt
            value = (self.r[rt] & 0xFFFFFFFFFFFFFFFF) | (self.rh[rt] << 64)
            w = [(value >> (32 * i)) & 0xFFFFFFFF for i in range(4)]
            w = [w[2], w[1], w[0], w[3]] if sub == 0x1E else [w[1], w[2], w[0], w[3]]
            out = sum(v << (32 * i) for i, v in enumerate(w))
            if rd:
                self.r[rd], self.rh[rd] = out & 0xFFFFFFFFFFFFFFFF, out >> 64
            return
        super().mmi(word, pc)

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.written = {}
        store = self.save

        def save(address, value, size=4):
            a = address & 0xFFFFFFFF
            if not STACK_LO <= a < STACK_HI:
                for i in range(size):
                    self.written[a + i] = (value >> (8 * i)) & 0xFF
            store(address, value, size)
        self.save = save

    def words(self, address, count):
        return tuple(self.load(address + 4 * i) for i in range(count))

    def written_ranges(self):
        """The stored bytes as sorted (start, length) runs."""
        out, run = [], None
        for a in sorted(self.written):
            if run and a == run[0] + run[1]:
                run[1] += 1
            else:
                run = [a, 1]
                out.append(run)
        return [tuple(r) for r in out]


def beats():
    return sorted(p for p in ROUTE.iterdir() if p.is_dir() and RM.in_scope_beat(p.name))


def beat_image(path):
    return (path / 'eeMemory.bin').read_bytes(), (path / 'scratchpad.bin').read_bytes()


def pool_actors(ram, callbacks):
    """The captured pool nodes whose +0x10 behaviour is one of `callbacks`,
    in walk order: the list D_00275BC0, linked through +0x1C (the walk
    tools/test_actor_census_reference.py uses)."""
    out, node, seen = [], w32(ram, 0x275BC0), 0
    while node:
        if w32(ram, node + 0x10) in callbacks:
            out.append(node)
        node = w32(ram, node + 0x1C)
        seen += 1
        assert seen <= 0x100, 'pool list does not terminate'
    return out


# ================================================================ harnesses
def source(rel):
    return (SRC / rel).read_text()


def extract(rel, pattern, what):
    m = re.search(pattern, source(rel), re.S)
    assert m, (rel, what, 'the live code moved; update the extraction')
    return m.group(1)


PICKUP_HARNESS = r'''
#include "game/em_pickup.c"
static EmSceneState h_scene;
EmSceneState *em_scene_state(void) { return &h_scene; }
static uint32_t h_rand[256]; static int h_rand_n, h_rand_i;
uint32_t em_random_next(void)
{ uint32_t v = h_rand_i < h_rand_n ? h_rand[h_rand_i] : 0x5A5A5Au; h_rand_i++; return v; }
void h_rand_script(const uint32_t *v, int n) { memcpy(h_rand, v, (size_t)n * 4); h_rand_n = n; h_rand_i = 0; }
int h_rand_calls(void) { return h_rand_i; }
void h_scene_reset(void) { memset(&h_scene, 0, sizeof h_scene); }
int h_progress_get(uint32_t a) { uint8_t *p = em_scene_progress_at(&h_scene, a, 1); return p ? *p : -1; }
int h_progress_set(uint32_t a, uint8_t v)
{ uint8_t *p = em_scene_progress_at(&h_scene, a, 1); if (!p) return -1; *p = v; return 0; }
float h_scale(const char *file) { return pickup_model_scale(file); }
int h_persist(int uid, uint32_t argument)
{
    memset(&s, 0, sizeof s); s.n = 1; s.p[0].used = 1; s.p[0].uid = uid; s.p[0].model = -1;
    return original_event(&s.p[0], EM_PICKUP_OWNER_PERSIST, argument);
}
void h_lights_reset(void) { memset(&s, 0, sizeof s); }
int h_light_add(int uid, int used, int bound, int child_status, const float color[4])
{
    int i = s.n++; Pickup *p = &s.p[i]; memset(p, 0, sizeof *p);
    p->used = used; p->uid = uid; p->model = -1; p->original_bound = bound;
    p->original.child_status = (uint8_t)child_status;
    PickupLight *l = &s.lights[s.n_lights]; memset(l, 0, sizeof *l);
    l->owner = i; memcpy(l->color, color, sizeof l->color); return s.n_lights++;
}
void h_owner_set(int light, int used, int child_status)
{ Pickup *p = &s.p[s.lights[light].owner]; p->used = used; p->original.child_status = (uint8_t)child_status; }
void h_light_set_initialized(int light, int v) { s.lights[light].initialized = v; }
void h_lights_tick(void) { em_pickup_lights_tick(); }
int h_light_get(int i, float tint[4])
{ memcpy(tint, s.lights[i].tint, 16); return s.lights[i].visible | s.lights[i].initialized << 1; }
void h_effect_color(uint32_t r, const float c[4], float t[4]) { em_effect_color(r, c, t); }
'''

PROPS_HARNESS = r'''
#include "game/em_props.c"
EmGameState g;
static int h_powered_v;
int em_game_terminal_powered(void) { return h_powered_v; }
static uint32_t h_rand[256]; static int h_rand_n, h_rand_i;
uint32_t em_random_next(void)
{ uint32_t v = h_rand_i < h_rand_n ? h_rand[h_rand_i] : 0x5A5A5Au; h_rand_i++; return v; }
void h_rand_script(const uint32_t *v, int n) { memcpy(h_rand, v, (size_t)n * 4); h_rand_n = n; h_rand_i = 0; }
int h_rand_calls(void) { return h_rand_i; }
void h_ind_reset(void) { memset(indicators, 0, sizeof indicators); h_powered_v = 0; }
void h_ind_setup(int slot, int enabled, int initialized)
{ indicators[slot].mesh = (EmGfxMesh *)(uintptr_t)0x10; indicators[slot].enabled = enabled;
  indicators[slot].initialized = initialized; }
void h_set_powered(int v) { h_powered_v = v; }
void h_ind_set_level(int slot, int level) { indicators[slot].level = level; }
void h_panel_complete(void) { em_props_panel_complete(); }
void h_ind_tick(void) { em_props_indicators_tick(); }
int h_ind_get(int slot, float tint[4])
{ memcpy(tint, indicators[slot].tint, 16); return indicators[slot].visible | indicators[slot].initialized << 1; }
'''

MODELS_HARNESS = r'''
#include "game/em_status_models.c"
int h_configure(const uint32_t *before, uint32_t *after, int *configured)
{
    EmStatusModels *m = calloc(1, sizeof *m);
    if (!m) return -2;
    memcpy(m->view, before, sizeof m->view);
    int rc = em_status_models_configure(m);
    memcpy(after, m->view, sizeof m->view);
    *configured = m->configured;
    free(m);
    return rc;
}
'''


def frame_harness():
    block = extract('game/em_player_frame.c',
                    r'\n(    uint8_t \*d810707 = em_scene_progress_at\(scene, 0x00810707u, 1\);\n'
                    r'.*?        scene->req\[EM_SCENE_REQ_B9\] = 1;\n)', '0015CF90 lines')
    # em_ee_float.h too, so the block still compiles once it uses em_ee_c_le.
    return ('#include <string.h>\n#include "game/em_game_internal.h"\n#include "game/em_scene_state.h"\n'
            '#include "game/em_ee_float.h"\n'
            'EmGameState g;\nstatic EmSceneState h_scene;\n'
            'static int h_block(EmSceneState *scene)\n{\n' + block + '    return 0;\n}\n'
            'int h_cf90(int infected, uint32_t health, uint8_t b9, uint8_t init707, uint8_t *o707, uint8_t *ob9)\n'
            '{\n    memset(&h_scene, 0, sizeof h_scene);\n    g.pd_infected = infected;\n'
            '    memcpy(&g.status.health, &health, 4);\n    h_scene.req[EM_SCENE_REQ_B9] = b9;\n'
            '    uint8_t *p = em_scene_progress_at(&h_scene, 0x00810707u, 1);\n    if (p) *p = init707;\n'
            '    int rc = h_block(&h_scene);\n    *o707 = p ? *p : 0;\n'
            '    *ob9 = h_scene.req[EM_SCENE_REQ_B9];\n    return rc;\n}\n')


def host_harness():
    rel = 'game/em_area11_interaction_host.c'
    scale = extract(rel, r'\n(    float scale = 1\.0f;\n    switch \(param\) \{.*?\n    \}\n)', '0015AC00 scale switch')
    variant = extract(rel, r'\n(    int16_t variant;\n    switch \(model & 0xF\) \{.*?\n    \}\n)',
                      '0015AC00 001F1110 variant switch')
    return ('#include <stdint.h>\n'
            'void h_state0(uint8_t model, uint8_t param, float *scale_out, int *variant_out)\n{\n'
            + scale + variant + '    *scale_out = scale;\n    *variant_out = variant;\n}\n')


def compile_all():
    BUILD.mkdir(parents=True, exist_ok=True)
    flags = ['cc', '-shared', '-fPIC', '-std=c11', '-O2', '-ffp-contract=off', '-w',
             '-I', str(SRC), '-undefined', 'dynamic_lookup']
    units = {
        'pickup': (PICKUP_HARNESS, []),
        'props': (PROPS_HARNESS, []),
        'models': (MODELS_HARNESS, [str(SRC / 'game/em_owner_services_original.c')]),
        'frame': (frame_harness(), []),
        'host': (host_harness(), []),
    }
    libs, system = {}, C.CDLL(None)
    for name, (text, extra) in units.items():
        c_path, lib_path = BUILD / f'{name}_harness.c', BUILD / f'{name}_harness.dylib'
        c_path.write_text(text)
        objects = []
        for i, unit in enumerate([str(c_path)] + extra):
            obj = BUILD / f'{name}_{i}.o'
            subprocess.run(flags[:-2] + ['-c', unit, '-o', str(obj)], check=True)
            objects.append(obj)
        # The module's other callees are never reached by these cases; each
        # is linked as a trap that aborts loudly if it ever is.
        undefined = set()
        for obj in objects:
            out = subprocess.run(['nm', '-u', '-j', str(obj)], check=True, capture_output=True, text=True).stdout
            undefined |= {s.strip() for s in out.split() if s.strip()}
        for obj in objects:
            out = subprocess.run(['nm', '-g', '-j', '--defined-only', str(obj)], check=True, capture_output=True,
                                 text=True).stdout
            undefined -= {s.strip() for s in out.split()}
        traps = sorted(s[1:] for s in undefined if s.startswith('_') and not hasattr(system, s[1:]))
        stub = BUILD / f'{name}_traps.c'
        stub.write_text('#include <stdio.h>\n#include <stdlib.h>\n' + ''.join(
            f'void {s}(void) {{ fprintf(stderr, "harness trap: {s}\\n"); abort(); }}\n' for s in traps))
        subprocess.run(flags[:-2] + [str(stub)] + [str(o) for o in objects] + ['-o', str(lib_path)], check=True)
        libs[name] = C.CDLL(str(lib_path))
    L = libs
    F4A = C.c_float * 4
    L['pickup'].h_scale.restype = C.c_float
    L['pickup'].h_scale.argtypes = [C.c_char_p]
    L['pickup'].h_persist.argtypes = [C.c_int, C.c_uint32]
    L['pickup'].h_progress_get.argtypes = [C.c_uint32]
    L['pickup'].h_progress_set.argtypes = [C.c_uint32, C.c_uint8]
    L['pickup'].h_light_add.argtypes = [C.c_int, C.c_int, C.c_int, C.c_int, F4A]
    L['pickup'].h_light_get.argtypes = [C.c_int, F4A]
    L['pickup'].h_effect_color.argtypes = [C.c_uint32, F4A, F4A]
    for lib in (L['pickup'], L['props']):
        lib.h_rand_script.argtypes = [C.POINTER(C.c_uint32), C.c_int]
    L['props'].h_ind_get.argtypes = [C.c_int, F4A]
    L['models'].h_configure.argtypes = [C.POINTER(C.c_uint32), C.POINTER(C.c_uint32), C.POINTER(C.c_int)]
    L['frame'].h_cf90.argtypes = [C.c_int, C.c_uint32, C.c_uint8, C.c_uint8,
                                  C.POINTER(C.c_uint8), C.POINTER(C.c_uint8)]
    L['host'].h_state0.argtypes = [C.c_uint8, C.c_uint8, C.POINTER(C.c_float), C.POINTER(C.c_int)]
    return L


def f4(values):
    return (C.c_float * 4)(*values)


def tint_bits(t):
    return tuple(fbits(x) for x in t)


class Result:
    def __init__(self, row):
        self.row = row
        self.checks = {}        # label -> case count (all equal)
        self.divergences = {}   # key -> list of example details

    def ok(self, label, n=1):
        self.checks[label] = self.checks.get(label, 0) + n

    def diverge(self, key, detail):
        self.divergences.setdefault(key, [])
        if len(self.divergences[key]) < 4:
            self.divergences[key].append(detail)


# ============================================================ 0015AC00
ACTOR = 0x910000
GROUP2 = {0x6D, 0x6C, 0x59, 0x57, 0x56, 0x55, 0x4F, 0x4E, 0x4D, 0x45, 0x42, 0x41, 0x40}


def run_15ac00(elf, model_id, byte3, rv):
    o = Oracle(elf)
    rng = random.Random(model_id * 977 + byte3 * 31 + rv)
    for off in range(0, 0x100, 4):
        o.save(ACTOR + off, rng.getrandbits(32))
    o.save(ACTOR + 0x0D, model_id, 1)
    o.save(ACTOR + 0x03, byte3, 1)
    o.written.clear()
    calls = []
    o.hooks[0x1B0FD0] = lambda e: (calls.append(('001B0FD0', e.arg(0))), e.ret_int(rv))
    o.hooks[0x1B1020] = lambda e: (calls.append(('001B1020', e.arg(0), e.arg(1), s32(e.r[6]), s32(e.r[7]))),
                                   e.ret_int(rv))
    o.hooks[0x1C6380] = lambda e: calls.append(('001C6380', e.arg(0)))
    o.hooks[0x1F1110] = lambda e: calls.append(('001F1110', e.arg(0), s32(e.r[5])))
    o.call(0x15AC00, (ACTOR,))
    scale = o.words(ACTOR + 0x60, 3)
    assert len(set(scale)) == 1
    return s32(o.r[2]), calls, scale[0], o


def part_15ac00(elf, L):
    res = Result('0015AC00')
    ids = RM.select(range(256), 70, 0x15AC, axes=(lambda t: t == 0x5B, lambda t: t in GROUP2, lambda t: t == 0x34),
                    keep=lambda i, t: t in (0, 0x34, 0x3F, 0x40, 0x5B, 0x6D, 0x6E, 0xFF) or t in GROUP2)
    subs = (0x00, 0x01, 0x02, 0x03, 0x0F, 0x31, 0xA2, 0xF0)
    for model_id in ids:
        for byte3 in subs:
            for rv in (0, 1):
                v0, calls, scale_word, o = run_15ac00(elf, model_id, byte3, rv)
                # em_pickup.c: the scale keyed by the exported model file name.
                native = fbits(L['pickup'].h_scale(b'props/item_%02x.emdl' % model_id))
                if native == scale_word:
                    res.ok('em_pickup.c pickup_model_scale == +0x60/+0x64/+0x68')
                else:
                    res.diverge('0015AC00/scale', (hex(model_id), hex(native), hex(scale_word)))
                # host state 0 (the live INIT for AREA11's 0015AFA0 owner).
                sc, var = C.c_float(), C.c_int()
                L['host'].h_state0(byte3, model_id, C.byref(sc), C.byref(var))
                if fbits(sc.value) == scale_word:
                    res.ok('host state-0 scale == +0x60')
                else:
                    res.diverge('0015AC00/host-scale', (hex(model_id), hex(fbits(sc.value)), hex(scale_word)))
                aura = [c for c in calls if c[0] == '001F1110']
                if rv:
                    # The original returns 1 before 001C6380 / +0 / +8 / +0x30 / 001F1110.
                    assert v0 == 1 and not aura and ('001C6380', ACTOR) not in calls
                    res.diverge('0015AC00/early-return', (hex(model_id), hex(byte3)))
                    continue
                assert v0 == 0 and len(aura) == 1 and calls[-2] == ('001C6380', ACTOR)
                if aura[0][2] == var.value:
                    res.ok('host state-0 001F1110 variant == original a1')
                else:
                    res.diverge('0015AC00/host-variant', (hex(model_id), hex(byte3), var.value, aura[0][2]))
                expect_init = ('001B0FD0', ACTOR) if byte3 & 15 == 1 else ('001B1020', ACTOR, model_id, -1, 0)
                assert calls[0] == expect_init, calls
                if byte3 & 15 == 1:
                    assert o.words(ACTOR + 0x80, 3) == (F4, F4, F4)
                else:
                    assert not any(ACTOR + 0x80 <= a < ACTOR + 0x8C for a in o.written)
                assert o.load(ACTOR, 1) == 1 and o.load(ACTOR + 8, 1) == 3 and o.load(ACTOR + 0x30) == 0x275488
                res.ok('original write set / call order read (+0x60..68, +0x80..88 on nibble 1, +0, +8, +0x30)')
    # Capture: the AREA11 item owners and what the port's pickup instances get.
    manifest = {}
    for line in MANIFEST.read_text().splitlines():
        m = re.match(r'pickup (0x[0-9a-f]+) \S+ \S+ \S+ \S+ (0x[0-9a-f]+) (\S+\.emdl)(.*)', line)
        if m and 'prop' not in m.group(4):
            manifest[int(m.group(2), 16)] = m.group(3)
    for beat in beats():
        ram, _ = beat_image(beat)
        area = ram[0x810700]
        for base in pool_actors(ram, {0x15AFA0, 0x219550}):
            uid = area << 8 | ram[base + 0x9A]
            if uid not in manifest:
                continue
            captured = w32(ram, base + 0x60)
            native = fbits(L['pickup'].h_scale(manifest[uid].encode()))
            owner = w32(ram, base + 0x10)
            if native == captured:
                res.ok('capture: em_pickup scale == captured +0x60 (beats 00..14)')
            else:
                res.diverge('0015AC00/capture-scale', (beat.name, hex(uid), hex(native), hex(captured)))
            if owner == 0x219550 and ram[base + 0x0D] in GROUP2 | {0x5B}:
                res.diverge('0015AC00/scale-219550', (beat.name, hex(uid)))
    # The latent 00219550 case: its state 0 (run whole, callees recorded)
    # never stores +0x60..+0x6B, whatever the model id; em_pickup_add scales
    # every non-prop item by the 0015AC00 switch.
    for model_id in (0x40, 0x5B, 0x6D, 0x72):
        o = Oracle(elf)
        for off in range(0, 0x100, 4):
            o.save(ACTOR + off, 0)
        o.save(ACTOR + 0x0D, model_id, 1)
        o.save(ACTOR + 0x2E, 0x10, 2)
        o.save(ACTOR + 0x60, F1); o.save(ACTOR + 0x64, F1); o.save(ACTOR + 0x68, F1)
        for callee in (0x1B0FD0, 0x1B1020, 0x1C6380, 0x1A2370, 0x1C5570):
            o.hooks[callee] = lambda e: e.ret_int(0)
        o.written.clear()
        o.call(0x219550, (ACTOR,))
        assert o.load(ACTOR, 1) == 1 and not any(ACTOR + 0x60 <= a < ACTOR + 0x6C for a in o.written)
        native = fbits(L['pickup'].h_scale(b'props/item_%02x.emdl' % model_id))
        if native == F1:
            res.ok('00219550 state 0 leaves +0x60 at 1.0; em_pickup scale agrees')
        elif model_id in GROUP2 or model_id == 0x5B:
            # The pinned latent case: only a model the 0015AC00 switch scales.
            res.diverge('0015AC00/scale-219550', dict(model=hex(model_id), native=hex(native), original=hex(F1)))
        else:
            res.diverge('0015AC00/scale-219550-unscaled-model',
                        dict(model=hex(model_id), native=hex(native), original=hex(F1)))
    return res


# ============================================================ 0015CF90
PLAYER = 0x930000
HEALTH = (0x42C80000, 0x3F800000, 0x00800000, 0x00000001, 0x007FFFFF, 0x00000000, 0x80000000,
          0x80000001, 0x80800000, 0xBF800000, 0xC2C80000, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000,
          0xFF800000, 0x7FC00000, 0xFFC00000)


def run_15cf90(o, player):
    o.written.clear()
    o.call(0x15CF90, (player,))
    return o.load(0x810707, 1), o.load(0x8106B9, 1), o.written_ranges()


def native_cf90(L, infected, health, b9):
    o707, ob9 = C.c_uint8(), C.c_uint8()
    rc = L['frame'].h_cf90(infected, health, b9, 0xEE, C.byref(o707), C.byref(ob9))
    assert rc == 0, 'em_scene_progress_at refused D_00810707'
    return o707.value, ob9.value


DAZ_LE = {0x00000001, 0x007FFFFF, 0xFFC00000}   # health patterns where c.le differs from native <=


def part_15cf90(elf, L):
    res = Result('0015CF90')
    ranges_seen = set()
    cases = [(h, inf, b9) for h in HEALTH for inf in (0, 1, 0x7F, 0x80, 0xFF) for b9 in (0, 1, 2, 0xFF)]
    # Quick mode keeps every compare boundary: the three c.le patterns and
    # +/-0.0 and -1.0 under every latch value (a wrong compare or a dropped
    # latch check shows there), plus a covering sample of the rest.
    boundary = DAZ_LE | {0x00000000, 0x80000000, 0xBF800000, 0x00800000}
    selected = RM.select(cases, 120, 0xCF90, axes=(lambda c: c[0], lambda c: c[1], lambda c: c[2]),
                         keep=lambda i, c: c[0] in boundary and c[1] in (0, 0xFF))
    expected_diverging = {(h, b9) for h, _, b9 in selected if h in DAZ_LE and b9 == 0}
    diverging = set()
    for health, infected, b9 in selected:
        o = Oracle(elf)
        o.save(PLAYER + 0x220, health)
        o.save(PLAYER + 0x228, 0x41200000)
        o.save(PLAYER + 0x234, infected, 1)
        o.save(PLAYER + 0x235, 0x5A, 1)
        o.save(0x8106B9, b9, 1)
        o.save(0x810707, 0xEE, 1)
        e707, eb9, ranges = run_15cf90(o, PLAYER)
        ranges_seen.add(tuple(ranges))
        n707, nb9 = native_cf90(L, infected, health, b9)
        if (n707, nb9) == (e707, eb9):
            res.ok('D_00810707 and D_008106B9 (synthetic health/latch/infected)')
            continue
        diverging.add((health, b9))
        detail = dict(health=hex(health), infected=infected, b9=b9, native=(n707, nb9), original=(e707, eb9))
        if health in DAZ_LE and b9 == 0 and eb9 == 1 and nb9 == 0 and n707 == e707:
            res.diverge('0015CF90/c.le-daz', detail)
        else:
            res.diverge('0015CF90/synthetic', detail)
    # The pinned key covers exactly the three c.le patterns with the latch clear.
    if diverging == expected_diverging:
        res.ok('the diverging (health, B9) set is exactly {1, 0x7FFFFF, 0xFFC00000} x {B9 = 0}')
    elif diverging:     # (none at all = the em_ee_c_le fix landed: the pinned key reports as gone)
        res.diverge('0015CF90/synthetic', dict(diverging=sorted((hex(h), b) for h, b in diverging),
                                               expected=sorted((hex(h), b) for h, b in expected_diverging)))
    for beat in beats():
        ram, spad = beat_image(beat)
        o = Oracle(elf, ram, spad)
        player = 0x8102B0        # the player record (em_player_frame.c: 0015BCF0's a0)
        infected, health, b9 = ram[player + 0x234], w32(ram, player + 0x220), ram[0x8106B9]
        # The previous frame's 0015CF90 left its copies: the capture is consistent.
        assert (ram[0x810707], w32(ram, 0x810858)) == (infected, health), beat.name
        e707, eb9, ranges = run_15cf90(o, player)
        ranges_seen.add(tuple(ranges))
        if native_cf90(L, infected, health, b9) == (e707, eb9):
            res.ok('capture: D_00810707/D_008106B9 over the player of beats 00..14')
        else:
            res.diverge('0015CF90/capture', beat.name)
    stores = sorted({r for rs in ranges_seen for r in rs})
    assert stores in ([(0x810706, 2), (0x810858, 8)], [(0x8106B9, 1), (0x810706, 2), (0x810858, 8)]), stores
    res.ok('original stores exactly D_00810706/707, D_00810858/85C and (conditionally) D_008106B9')
    return res


# ============================================================ 001B1190
def run_1b1190(elf, area, a0, before=None):
    o = Oracle(elf)
    for a in range(0x810860, 0x812860, 4):
        o.save(a, 0)
    if before:
        for a, v in before.items():
            o.save(a, v, 1)
    o.save(0x810700, area, 1)
    o.written.clear()
    o.call(0x1B1190, (a0,))
    return {a: v for a, v in o.written.items()}, o


def part_1b1190(elf, L):
    res = Result('001B1190')
    P = L['pickup']
    areas = (0x00, 0x01, 0x0B, 0x16, 0x17, 0x80, 0xFF)
    puids = RM.select(range(256), 48, 0x1190, keep=lambda i, u: u in (0, 1, 7, 8, 31, 32, 33, 0x9A, 0xFF))
    for area in areas:
        for puid in puids:
            written, o = run_1b1190(elf, area, puid)
            P.h_scene_reset()
            assert P.h_persist(area << 8 | puid, puid) == 1
            native = {a: P.h_progress_get(a) for a in range(0x810860, 0x810B40)}
            native_set = {a: v for a, v in native.items() if v}
            if area > 0x16:
                # 0x810860 + area*32 lies past the canonical taken bits.
                assert not native_set
                if written:
                    res.diverge('001B1190/area>0x16', dict(area=hex(area), puid=puid,
                                                           original=sorted(hex(a) for a in written)))
                continue
            orig_set = {a: v for a, v in written.items() if v}
            if native_set == orig_set:
                res.ok('taken bit (area 0..0x16 x puid, incl. puid 0 = no write)')
            else:
                res.diverge('001B1190/bits', dict(area=area, puid=puid, native=native_set, original=orig_set))
    # A second persist over existing bits (read-modify-write keeps the others).
    written, o = run_1b1190(elf, 0x0B, 0x21, {0x8109C4: 0x81})
    P.h_scene_reset(); P.h_progress_set(0x8109C4, 0x81); P.h_persist(0x0B21, 0x21)
    assert P.h_progress_get(0x8109C4) == o.load(0x8109C4, 1) == 0x83
    res.ok('read-modify-write keeps the other bits')
    # Capture: beat 00 -> 01 is the battery take (uid 1); the port's PERSIST
    # over beat 00's bits must give beat 01's bits, like the original does.
    paths = beats()
    ram0, _ = beat_image(paths[0])
    ram1, _ = beat_image(paths[1])
    area = ram0[0x810700]
    assert ram1[0x810700] == area == 0x0B
    P.h_scene_reset()
    for a in range(0x810860, 0x810B40):
        P.h_progress_set(a, ram0[a])
    assert P.h_persist(area << 8 | 1, 1) == 1
    native = bytes(P.h_progress_get(a) for a in range(0x810860, 0x810B40))
    o = Oracle(elf, ram0)
    o.call(0x1B1190, (1,))
    original = bytes(o.load(a, 1) for a in range(0x810860, 0x810B40))
    assert native == original == ram1[0x810860:0x810B40], 'battery take bits'
    res.ok('capture: beat 00 + PERSIST(uid 1) == beat 01 taken bits (port == original == capture)')
    # The port keys the area on the manifest uid, the original on D_00810700.
    uid_areas = {int(m, 16) >> 8 for m in re.findall(r'^pickup \S+ \S+ \S+ \S+ \S+ (0x[0-9a-f]+)',
                                                    MANIFEST.read_text(), re.M) if int(m, 16)}
    captured_areas = {beat_image(p)[0][0x810700] for p in paths}
    assert uid_areas == captured_areas == {0x0B}, (uid_areas, captured_areas)
    res.ok('AREA11 manifest uid area byte == captured D_00810700 (0x0B) in every beat')
    return res


# ============================================================ 001C5680 / 001C5760
CHILD = 0x940000
DRAW_METHOD = 0x1CACB0


def child_frames(elf, callback, color_words, frames, alt=0, stop_at=None, status_write=3, init_rv=None,
                 parity=None):
    """Tick a synthetic child `frames` times. Before frame stop_at the owner
    writes +4 = status_write. Returns one record per frame."""
    o = Oracle(elf)
    for off in range(0, 0x100, 4):
        o.save(CHILD + off, 0)
    o.save(CHILD + 0x0A, alt, 1)
    for i, w in enumerate(color_words):
        o.save(CHILD + 0xA0 + 4 * i, w)
    o.save(CHILD + 0x4C, DRAW_METHOD)
    init = 0x1C2360 if callback == 0x1C5680 else 0x1C22A0
    out, freed = [], False
    for f in range(frames):
        ev = []
        rv = init_rv[f] if init_rv else 0
        o.hooks[init] = lambda e, rv=rv: (ev.append('init'), e.ret_int(rv))
        o.hooks[0x1C6380] = lambda e: ev.append('matrix')
        o.hooks[0x1F54E0] = lambda e: ev.append(('draw', e.arg(0) - CHILD, e.arg(1) - CHILD,
                                                 o.words(e.arg(1), 4)))
        o.hooks[0x1AFC10] = lambda e: ev.append('free')
        if stop_at is not None and f == stop_at:
            o.save(CHILD + 4, status_write, 1)
        o.spad[0x3B68:0x3B6C] = struct.pack('<I', parity[f] if parity else f & 1)
        if freed:
            out.append(('gone',))
            continue
        o.written.clear()
        o.call(callback, (CHILD,))
        outside = [a for a in o.written if not (CHILD + 4 <= a < CHILD + 5 or CHILD + 0x80 <= a < CHILD + 0x90)]
        assert not outside, [hex(a) for a in outside]
        freed = 'free' in ev
        out.append(tuple(ev))
    return out


def native_light_frames(L, color, frames, stop_at=None, status_write=3, rand=None):
    P = L['pickup']
    P.h_lights_reset()
    light = P.h_light_add(0x0B01, 1, 1, 1, f4(color))
    out = []
    for f in range(frames):
        if stop_at is not None and f == stop_at:
            P.h_owner_set(light, 1, status_write)
        r = rand[f]
        P.h_rand_script((C.c_uint32 * 1)(r), 1)
        P.h_lights_tick()
        tint = (C.c_float * 4)()
        flags = P.h_light_get(light, tint)
        out.append((P.h_rand_calls(), flags & 1, tint_bits(tint)))
    return out


def native_indicator_frames(L, slot, frames, stop_at=None, rand=None):
    Q = L['props']
    Q.h_ind_reset()
    Q.h_ind_setup(slot, 1, 0)
    out = []
    for f in range(frames):
        if stop_at is not None and f == stop_at:
            assert slot == 0
            Q.h_panel_complete()
        Q.h_rand_script((C.c_uint32 * 1)(rand[f]), 1)
        Q.h_ind_tick()
        tint = (C.c_float * 4)()
        flags = Q.h_ind_get(slot, tint)
        out.append((Q.h_rand_calls(), flags & 1, tint_bits(tint)))
    return out


def compare_child(res, L, label, original, native, rand):
    for f, (ev, nat) in enumerate(zip(original, native)):
        draws = [e for e in ev if isinstance(e, tuple) and e and e[0] == 'draw']
        n_calls, visible, tint = nat
        if not draws:
            same = n_calls == 0 and not visible
        else:
            (_, a0, a1, colour), = draws
            assert (a0, a1) == (0, 0x80)
            expect = (C.c_float * 4)()
            L['pickup'].h_effect_color(rand[f], f4([number(w) for w in colour]), expect)
            same = n_calls == 1 and visible and tint == tint_bits(expect)
        if same:
            res.ok(label)
        else:
            return (f, ev, nat)
    return None


def part_children(elf, L):
    r80, r60 = Result('001C5680'), Result('001C5760')
    rng = random.Random(0x1C56)
    frames = 6
    rand = [rng.getrandbits(31) for _ in range(frames)]
    # Colours: the captured +0xA0 of each AREA11 child kind (beat 00).
    ram0, _ = beat_image(beats()[0])
    kinds = {}
    for base in pool_actors(ram0, {0x1C5680, 0x1C5760}):
        kinds.setdefault(ram0[base + 0x0D], (w32(ram0, base + 0x10), struct.unpack_from('<4I', ram0, base + 0xA0),
                                             ram0[base + 0x0A]))
    manifest = MANIFEST.read_text()
    light = re.search(r'^pickup_light 0x0b01 \S+ (\S+) (\S+) (\S+) (\S+)', manifest, re.M)
    light_colour = [float(light.group(i)) for i in range(1, 5)]
    assert tuple(fbits(x) for x in light_colour) == kinds[0x73][1], 'manifest light colour != captured +0xA0'
    r80.ok('capture: manifest pickup_light colour == captured 001C5680 child +0xA0 (model 0x73)')
    # Pickup light children (001C5680, model 0x73): steady, stop before the
    # first tick, stop after one draw.
    for stop in (None, 0, 1, 3):
        orig = child_frames(elf, 0x1C5680, kinds[0x73][1], frames, stop_at=stop)
        nat = native_light_frames(L, light_colour, frames, stop_at=stop, rand=rand)
        bad = compare_child(r80, L, 'pickup light: init frame, draws, colour, stop (+4 = 3)', orig, nat, rand)
        if bad:
            r80.diverge('001C5680/light', (stop, bad))
    # 001F54E0 itself (run whole on a drawn child; not this row, but the RNG
    # accounting below rests on it) draws exactly one 00122BB8 value and
    # calls the child's +0x4C method once.
    for colour in (kinds[0x73][1], kinds[0x7A][1], kinds[0x75][1]):
        o = Oracle(elf)
        for i, w in enumerate(colour):
            o.save(CHILD + 0x80 + 4 * i, w)
        o.save(CHILD + 0x4C, DRAW_METHOD)
        seen = []
        o.hooks[0x122BB8] = lambda e: (seen.append('rand'), e.ret_int(0x1234567))
        o.hooks[DRAW_METHOD] = lambda e: seen.append('draw')
        o.call(0x1F54E0, (CHILD, CHILD + 0x80))
        assert seen == ['rand', 'draw'], seen
    r80.ok('001F54E0 draws one 00122BB8 value and calls +0x4C once (0x73, 0x7A and 0x75 colours)')
    # Parity of 0x70003B68 does not change anything outside the stack.
    even = child_frames(elf, 0x1C5680, kinds[0x73][1], frames, parity=[0] * frames)
    odd = child_frames(elf, 0x1C5680, kinds[0x73][1], frames, parity=[1] * frames)
    assert even == odd
    r80.ok('0x70003B68 parity: identical non-stack writes and 001F54E0 arguments')
    # Status 2 (nothing in the port writes it): the original frees.
    orig = child_frames(elf, 0x1C5680, kinds[0x73][1], frames, stop_at=2, status_write=2)
    nat = native_light_frames(L, light_colour, frames, stop_at=2, status_write=2, rand=rand)
    if compare_child(Result('x'), L, 'x', orig, nat, rand):
        r80.diverge('001C5680/status2', 'original frees at +4 == 2; the aggregate keeps drawing')
    # Init refused (bone slots): the original retries, the port never fails.
    orig = child_frames(elf, 0x1C5680, kinds[0x73][1], frames, init_rv=[1, 1, 0, 0, 0, 0])
    nat = native_light_frames(L, light_colour, frames, rand=rand)
    if compare_child(Result('x'), L, 'x', orig, nat, rand):
        r80.diverge('001C5680/init-refused', 'draws start two frames later in the original')
    # The panel indicator (001C5680, model 0x75) through em_props slot 0.
    panel = kinds[0x75]
    assert panel[0] == 0x1C5680 and panel[1] == tuple(fbits(x) for x in (1, 0, 0, 1))
    for stop in (None, 0, 2):
        orig = child_frames(elf, 0x1C5680, panel[1], frames, stop_at=stop)
        nat = native_indicator_frames(L, 0, frames, stop_at=stop, rand=rand)
        bad = compare_child(r80, L, 'panel indicator (em_props slot 0): init, draws, colour, completion', orig, nat,
                            rand)
        if bad:
            r80.diverge('001C5680/panel', (stop, bad))
    # The elevator indicator (001C5760, model 0x10, +0xA 0) through em_props
    # slot 1, unpowered (its +0xA0 is the 00827B10 parent's; level 0 here).
    elev = kinds[0x10]
    assert elev[0] == 0x1C5760 and elev[2] == 0 and elev[1] == tuple(fbits(x) for x in (1, 0, 0, 0.25))
    orig = child_frames(elf, 0x1C5760, elev[1], frames)
    nat = native_indicator_frames(L, 1, frames, rand=rand)
    bad = compare_child(r60, L, 'elevator indicator (em_props slot 1, unpowered): init, draws, colour', orig, nat,
                        rand)
    if bad:
        r60.diverge('001C5760/elevator', bad)
    for status in (2, 3, 4):
        orig = child_frames(elf, 0x1C5760, elev[1], 4, stop_at=2, status_write=status)
        assert orig[2] == ('free',) and orig[3] == ('gone',)
    r60.ok('original 001C5760 frees on +4 = 2, 3, 4 (read)')
    # The proposed tick_indicators fix (+4 > 1 frees both kinds) also covers
    # 001C5680: measure that it frees on +4 = 2, 3, 4 and 0x80 as well, and
    # keeps drawing on 0 and 1.
    for callback in (0x1C5680, 0x1C5760):
        for status in (2, 3, 4, 0x80):
            orig = child_frames(elf, callback, elev[1], 4, stop_at=2, status_write=status)
            assert orig[2] == ('free',) and orig[3] == ('gone',), (hex(callback), status, orig)
        for status in (0, 1):
            orig = child_frames(elf, callback, elev[1], 4, stop_at=2, status_write=status)
            draw = orig[1]
            assert draw[0][0] == 'draw' and orig[3] == draw, (hex(callback), status, orig)
            # +4 = 1 keeps drawing; +4 = 0 re-runs the initializing frame.
            assert orig[2] == (draw if status == 1 else ('init', 'matrix')), (hex(callback), status, orig)
    r60.ok('original 001C5680 and 001C5760 free on +4 = 2, 3, 4, 0x80; +4 = 1 keeps drawing, 0 re-initializes')
    bindings = source('game/em_area11_bindings.c')
    if 'if (actor->callback == 0x001C5680u && (actor->u04[0] == 3 || actor->u04[0] == 2))' in bindings:
        r60.diverge('001C5760/free', 'tick_indicators: only 001C5680 nodes on +4 3/2 free themselves')
    orig = child_frames(elf, 0x1C5760, elev[1], frames, alt=1)
    assert all(ev == ('matrix', ev[1]) for ev in orig[1:]) and orig[1][1][0] == 'draw'
    r60.diverge('001C5760/alt-matrix', 'alt 1: 001C6380 before every draw')
    # One whole frame of every child in a captured pool, in walk order,
    # against the port's two aggregates set up from the manifest.
    taken_uids_by_beat = {}
    for beat in beats():
        ram, _ = beat_image(beat)
        children = pool_actors(ram, {0x1C5680, 0x1C5760})
        draws = []
        for i, base in enumerate(children):
            o = Oracle(elf, ram)
            ev = []
            init = 0x1C2360 if w32(ram, base + 0x10) == 0x1C5680 else 0x1C22A0
            o.hooks[init] = lambda e: (ev.append('init'), e.ret_int(0))
            o.hooks[0x1C6380] = lambda e: ev.append('matrix')
            o.hooks[0x1F54E0] = lambda e: ev.append(('draw', o.words(e.arg(1), 4)))
            o.hooks[0x1AFC10] = lambda e: ev.append('free')
            o.call(w32(ram, base + 0x10), (base,))
            draws += [(ram[base + 0x0D], e[1]) for e in ev if isinstance(e, tuple)]
        # The port: lights for the manifest owners not taken in this beat,
        # the panel indicator while its child exists, the elevator always.
        area = ram[0x810700]
        taken = {u for u in range(256) if ram[0x810860 + area * 32 + (u >> 3)] >> (u & 7) & 1}
        taken_uids_by_beat[beat.name] = taken
        P, Q = L['pickup'], L['props']
        P.h_lights_reset()
        order = []
        for m in re.finditer(r'^pickup_light (0x[0-9a-f]+) \S+ (\S+) (\S+) (\S+) (\S+)', manifest, re.M):
            uid = int(m.group(1), 16)
            if uid & 0xFF in taken:
                continue      # em_pickup_add refuses a taken uid; its light is never added
            colour = [float(m.group(i)) for i in range(2, 6)]
            idx = P.h_light_add(uid, 1, 1, 1, f4(colour))
            P.h_light_set_initialized(idx, 1)
            order.append(('light', idx))
        Q.h_ind_reset()
        panel_live = any(ram[b + 0x0D] == 0x75 for b in children)
        Q.h_ind_setup(0, int(panel_live), 1)
        Q.h_ind_setup(1, 1, 1)
        # The elevator slot's colour follows the 00827B10 parent's +0x28 level
        # (not this row): start the port's level where the capture has it,
        # powered when it holds 128, so the child draw itself is compared.
        parents = pool_actors(ram, {0x827B10})
        assert len(parents) == 1 and ram[parents[0] + 0x28] in (0, 128), beat.name
        level = ram[parents[0] + 0x28]
        Q.h_ind_set_level(1, level)
        Q.h_set_powered(int(level == 128))
        rand = [rng.getrandbits(31) for _ in range(16)]
        P.h_rand_script((C.c_uint32 * 16)(*rand), 16)
        P.h_lights_tick()
        n_lights = P.h_rand_calls()
        Q.h_rand_script((C.c_uint32 * 16)(*rand[n_lights:]), 16 - n_lights)
        Q.h_ind_tick()
        n_native = n_lights + Q.h_rand_calls()
        # What the port actually drew, in its draw order: every light whose
        # visible bit the tick set, then slot 0, then slot 1 (em_props), each
        # with the tint it computed and the RNG value it consumed.
        native_draws = []
        for i in range(len(order)):
            tint = (C.c_float * 4)()
            if P.h_light_get(i, tint) & 1:
                native_draws.append((0x73, tint_bits(tint)))
        for slot, kind in ((0, 0x75), (1, 0x10)):
            tint = (C.c_float * 4)()
            if Q.h_ind_get(slot, tint) & 1:
                native_draws.append((kind, tint_bits(tint)))
        assert len(native_draws) == n_native, (beat.name, len(native_draws), n_native)
        # The original's draws the port models (every kind but 0x7A), with the
        # colour each one handed 001F54E0, run through em_effect_color with
        # the RNG value the port's draw at that position consumed.
        modelled = [(k, c) for k, c in draws if k != 0x7A]
        expect = []
        for j, (kind, colour) in enumerate(modelled[:len(native_draws)]):
            t = (C.c_float * 4)()
            L['pickup'].h_effect_color(rand[j], f4([number(w) for w in colour]), t)
            expect.append((kind, tint_bits(t)))
        if len(modelled) == len(native_draws) and expect == native_draws:
            r80.ok('capture frame: the port draws the original\'s kinds in order with the same colours, 0x7A aside')
        else:
            r80.diverge('001C5680/capture-frame', dict(beat=beat.name,
                                                       original=[hex(k) for k, _ in modelled],
                                                       native=[hex(k) for k, _ in native_draws],
                                                       first_bad=next((j for j, (a, b) in
                                                                       enumerate(zip(expect, native_draws))
                                                                       if a != b), None)))
            continue
        n7a = sum(1 for k, _ in draws if k == 0x7A)
        if n_native == len(draws):
            r80.ok('capture frame: one 001F54E0 per live child')
        elif n7a == 1 and len(draws) - n_native == 1:
            r80.diverge('001C5680/missing-7A-draw', dict(beat=beat.name, original=len(draws), native=n_native,
                                                         undrawn=[(hex(k), [hex(w) for w in c])
                                                                  for k, c in draws if k == 0x7A]))
        else:
            r80.diverge('001C5680/capture-count', dict(beat=beat.name, original=len(draws), native=n_native,
                                                       n7a=n7a))
    # The harness above runs only the two aggregates. The pin holds while
    # the live code draws nothing else for these nodes; once a 0x7A draw is
    # bound (per-node step or a third aggregate entry), this fails so the
    # harness is extended to run it and the pin is retired.
    bindings = source('game/em_area11_bindings.c')
    head = re.search(r'if \(node->head\) \{\s*em_pickup_lights_tick\(\);\s*em_props_indicators_tick\(\);\s*\}',
                     bindings)
    enemy = re.search(r'static int tick_enemy_00825940\(.*?\n\}\n', bindings, re.S)
    if not head or not enemy or re.search(r'001F54E0|em_effect', enemy.group(0)):
        r80.diverge('001C5680/7A-binding-changed', 'tick_indicators / tick_enemy_00825940 no longer match '
                                                   'the two-aggregate shape this harness runs')
    return [r80, r60]


# ============================================================ 001CF470
def part_1cf470(elf):
    res = Result('001CF470')
    defined = [p for p in SRC.rglob('*.c') if re.search(r'\b\w*(?:001CF470|1cf470)\w*\s*\([^;]*\)\s*\{',
                                                          p.read_text(errors='ignore'), re.I)]
    if not defined:
        res.diverge('001CF470/missing', 'no function named for 001CF470 in src/')
    beat = [p for p in beats() if p.name.startswith('02_')][0]
    ram, spad = beat_image(beat)
    ctx = w32(ram, 0x275670)
    mtx = ctx + 0x2240
    player = w32(ram, 0x275B44)
    px, py, pz = (number(w) for w in struct.unpack_from('<3I', ram, player + 0xB0))
    rng = random.Random(0xCF470)
    n = RM.pick(400, 40)
    histogram, digest = {}, hashlib.sha256()
    for i in range(n):
        radius = (4.0, 40.0, 400.0, 4000.0)[i % 4]
        o = Oracle(elf, ram, spad)
        for v in range(3):
            base = 0x8112C0 + 0x50 * v
            for k in range(0x50 // 4):
                o.save(base + 4 * k, 0)
            pos = (px + rng.uniform(-radius, radius), py + rng.uniform(-radius, radius) * 0.25,
                   pz + rng.uniform(-radius, radius), 1.0)
            for k, x in enumerate(pos):
                o.save(base + 4 * k, fbits(x))
            o.save(base + 0x30, fbits(float(v & 1)))
            o.save(base + 0x34, fbits(float(v >> 1)))
        o.written.clear()
        o.call(0x1CF470, (0x8112C0, mtx))
        count = s32(o.r[2])
        assert 0 <= count <= 16, count
        histogram[count] = histogram.get(count, 0) + 1
        digest.update(struct.pack('<i', count) + o.read(0x8117C0, 0x50 * count))
    histogram = dict(sorted(histogram.items()))
    res.reference = digest.hexdigest()
    want_hist, want_digest = CF470_REFERENCE[RM.MODE]
    assert histogram == want_hist, ('001CF470 reference fan sizes drifted', histogram, want_hist)
    assert res.reference[:32] == want_digest, ('001CF470 reference digest drifted', res.reference)
    res.ok(f'original executed over beat 02: fan sizes {histogram} and digest equal the pinned reference', n)
    return res


# ============================================================ 0020DFA0
def part_20dfa0(elf, L):
    res = Result('0020DFA0')
    # The whole routine on beat 01 (the BATTERY page opens there).
    beat = [p for p in beats() if p.name.startswith('01_')][0]
    ram, spad = beat_image(beat)
    o = Oracle(elf, ram, spad)
    rng = random.Random(0xDFA0)
    before = tuple(rng.getrandbits(32) | 0x3F000000 for _ in range(16))
    for i, w in enumerate(before):
        o.save(0x810610 + 4 * i, w)
    calls = []
    o.hooks[0x1AFE60] = lambda e: calls.append(('001AFE60',))
    o.hooks[0x20E020] = lambda e: calls.append(('0020E020',))
    o.hooks[0x21BAC0] = lambda e: calls.append(('0021BAC0', s32(e.r[4])))
    o.hooks[0x21B9A0] = lambda e: calls.append(('0021B9A0', s32(e.r[4]), e.f[12] & 0xFFFFFFFF,
                                                e.f[13] & 0xFFFFFFFF))
    o.hooks[0x1D2610] = lambda e: calls.append(('001D2610', e.f[12] & 0xFFFFFFFF))
    o.written.clear()
    o.call(0x20DFA0, ())
    after = o.words(0x810610, 16)
    assert calls == [('001AFE60',), ('0020E020',), ('0021BAC0', 0), ('0021B9A0', 5, 0, 0x49742400),
                     ('001D2610', 0)], calls
    assert [r for r in o.written_ranges()] == [(0x810610, 0x40)], o.written_ranges()
    got, configured = (C.c_uint32 * 16)(), C.c_int()
    rc = L['models'].h_configure((C.c_uint32 * 16)(*before), got, C.byref(configured))
    if rc == 1 and configured.value == 1 and tuple(got) == after:
        res.ok('em_status_models_configure == D_00810610..D_0081064F (identity, D_00810624 = -1.0)')
    else:
        res.diverge('0020DFA0/view', dict(native=[hex(x) for x in got], original=[hex(x) for x in after]))
    # 001D2610(0.0), run whole (0011E398 and 001B0070 original), gives the
    # zoom the host hard-codes; its 0021B970 is hooked and recorded.
    o = Oracle(elf, ram, spad)
    fog = []
    o.hooks[0x21B970] = lambda e: fog.append((e.f[12] & 0xFFFFFFFF, e.f[13] & 0xFFFFFFFF))
    o.call(0x1D2610, (), (0.0,))
    zoom = o.load(0x70003B60)
    ctx = w32(ram, 0x275670)
    assert o.load(ctx + 0x2468) == zoom
    host = source('game/em_area11_interaction_host.c')
    case = re.search(r'case EM_STATUS_PAGE_CONFIGURE:(.*?)\n    case ', host, re.S).group(1)
    port_zoom = re.search(r'g\.cam\.zoom = (0x[0-9a-fA-F.p+-]+)f;', case)
    if port_zoom and fbits(float.fromhex(port_zoom.group(1))) == zoom:
        res.ok('001D2610(0.0) zoom (0x70003B60 / ctx+0x2468) == the host\'s g.cam.zoom constant')
    else:
        res.diverge('0020DFA0/zoom', dict(original=hex(zoom), port=port_zoom and port_zoom.group(1)))
    assert fog == [(w32(ram, ctx + 0xF8), w32(ram, ctx + 0xFC))], fog
    # What the live CONFIGURE handler performs, read from its text.
    # The trail reset may also land in em_status_runtime.c page_worker, as a
    # CONFIGURE case that resets the trail before forwarding to the host.
    runtime = source('game/em_status_runtime.c')
    worker = re.search(r'static int page_worker\(.*?\n\}\n', runtime, re.S)
    assert worker, 'em_status_runtime.c page_worker moved; update the probe'
    worker_case = re.search(r'case EM_STATUS_PAGE_CONFIGURE:(.*?)(?:\n    case |\n    default:)', worker.group(0),
                            re.S)
    performed = {'001AFE60': 'em_status_models_clear' in case,
                 '001029C0-configure': 'em_status_models_configure' in case,
                 '001D2610-zoom': 'g.cam.zoom' in case,
                 '0020E020': bool(re.search(r'trail_reset\s*\(|0020E020\w*\(', case)) or
                             bool(worker_case and 'em_item_trail_reset(' in worker_case.group(1)),
                 '0021BAC0': bool(re.search(r'0021BAC0\w*\(', case)),
                 '0021B9A0': bool(re.search(r'0021B9A0\w*\(', case)),
                 '0021B970': bool(re.search(r'0021B970\w*\(', case))}
    for callee, done in performed.items():
        if done:
            res.ok(f'CONFIGURE runs {callee}')
        else:
            res.diverge(f'0020DFA0/missing-{callee}', 'not run on the CONFIGURE path')
    return res


# ============================================================ main
def main():
    elf = read_elf()
    L = compile_all()
    results = [part_15ac00(elf, L), part_15cf90(elf, L), part_1b1190(elf, L)]
    results += part_children(elf, L)
    results += [part_1cf470(elf), part_20dfa0(elf, L)]
    found = {k for r in results for k in r.divergences}
    report = {'elf_sha256': ELF_SHA256, 'mode': RM.MODE, 'rows': {}}
    for r in results:
        row = {'checks': r.checks, 'divergences': {k: [str(x) for x in v] for k, v in r.divergences.items()}}
        if hasattr(r, 'reference'):
            row['reference_sha256'] = r.reference
        report['rows'][r.row] = row
        verdict = 'verified' if not r.divergences else 'diverges: ' + ', '.join(sorted(r.divergences))
        print(f'{r.row}: {verdict}')
        for label, n in r.checks.items():
            print(f'    PASS {n:6d}  {label}')
        for key, examples in r.divergences.items():
            print(f'    DIVERGE {key}: {EXPECTED.get(key, "NOT PINNED")}')
            print(f'        e.g. {examples[0]}')
    report['source_sha256'] = {'src/' + rel: hashlib.sha256((SRC / rel).read_bytes()).hexdigest() for rel in (
        'game/em_pickup.c', 'game/em_props.c', 'game/em_status_models.c', 'game/em_player_frame.c',
        'game/em_area11_interaction_host.c', 'game/em_area11_bindings.c', 'game/em_status_runtime.c')}
    report['source_sha256']['tools/' + Path(__file__).name] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    (BUILD / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    RM.banner(f'{sum(sum(r.checks.values()) for r in results):,} equal comparisons over 7 rows')
    new, gone = sorted(found - set(EXPECTED)), sorted(set(EXPECTED) - found)
    if new or gone:
        print('unpinned divergences:', new)
        print('pinned divergences no longer seen (update docs/CENSUS_UNVERIFIED.md and EXPECTED):', gone)
        sys.exit(1)
    print('census unverified rows: every result matches docs/CENSUS_UNVERIFIED.md')


if __name__ == '__main__':
    main()
