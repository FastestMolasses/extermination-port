#!/usr/bin/env python3
"""The player's one pose owner against the original, over captured RAM.

docs/PLAYER_CLIPS.md section 6. em_player_record_pose is what the game binds:
the player's record, the node records at 0x7D5840.., the bank at 0xD689C0
(assets/player_clips_full.bank) and D_00248C90's +0 column
(assets/player_clip_row0.emch), worked by em_pose_host_workers and
em_player_stage_anim_advance. This oracle runs the ORIGINAL instructions
(001749A0, 001749F0, 001C64F0 with every callee, 001C6DA0, 001C68C0; the
interpreter of test_pose_host_workers_reference.py) over each captured image
and the live module over a copy of the same record, node records and
globals, and compares after every operation:

  * the whole 0x320-byte player record, all 21 node records, D_00275BF8..BEC,
    D_008111F0.. and the scratchpad words the routines use;
  * the attach: the structural words it writes (+C, +40, +60..+6C, the +110
    array, +164) must equal every captured image's;
  * 0015BCF0's animate step: the original evaluator the byte-matched
    0015BCF0 selects (decomp src/func_0015BCF0.c: +2F3, +303 and the
    D_00248C90 +0 halfword of +20C, read here from the ELF) against
    em_player_record_pose_animate (the exported column).

Cases per image:
  * capture: re-evaluating the captured record (+B0 = +A0, as 0015BCF0's
    first copy leaves it) through the module reproduces the captured node
    world matrices byte for byte (the original's skeleton of that frame);
  * clips: every first-level clip of docs/PLAYER_CLIPS.md section 1 is
    requested the way the states request it (001749A0 with flags 1 and a
    blend of 8 or 0; 001749F0 with a source frame), then advanced by its
    D_00248C98 rate once per callback with the animate step after each,
    through its first frames; the two chained clips 0x5E and 0x73 run past
    their end into the follow-on clip (0x5F / 0x72) and its hold frame.

The quick run covers the chained clips and a sample of the others on three
images; EM_TEST_FULL=1 runs every first-level clip on the playable image and
every route beat 00..14, each through its whole length.
"""
import ctypes as C
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode  # noqa: E402
import test_pose_host_workers_reference as PH  # noqa: E402
import test_player_slide_reference as SR  # noqa: E402
from export_player_clips import FIRST_LEVEL  # noqa: E402

LANE = ROOT / 'build/player_record_pose_reference'
BANK_PATH = ROOT / 'assets/player_clips_full.bank'
ROW0_PATH = ROOT / 'assets/player_clip_row0.emch'
PLAYER, NODES, NODE_BYTES, NODE_COUNT = 0x8102B0, 0x7D5840, 0xD0, 21
TABLE, STRIDE = 0x248C90, 12
SPAD = (('spad3400', 0x3400, 16), ('spad3440', 0x3440, 16), ('spad3600', 0x3600, 4),
        ('spad3760', 0x3760, 11), ('spad3A3C', 0x3A3C, 1), ('spad38B0', 0x38B0, 4),
        ('spad3A20', 0x3A20, 1))
SOURCES = ('src/game/em_player_record_pose.c', 'src/game/em_pose_host_workers.c',
           'src/game/em_player_stage_workers.c', 'src/game/em_player_floor.c',
           'src/game/em_player_reaction.c', 'src/game/em_player_fall.c',
           'src/game/em_owner_services_original.c', 'src/game/em_stream_lanes_original.c')

BRIDGE = r'''
#include "game/em_player_record_pose.h"
#include <string.h>
static EmPlayerRecordPose rp;
static EmPlayerLiveActor actor;
static uint8_t d8106F3, d8106F1, d810707;
static EmPlayerStageScene stage_scene = { .d8106F1 = &d8106F1 };
static EmPlayerStageGlobals stage_globals = { .d810707 = &d810707 };
static uint32_t *spad_word(unsigned k) {
    uint32_t *const words[] = { rp.spad3400, rp.spad3440, rp.spad3600, rp.spad3760, &rp.spad3A3C,
                                rp.spad38B0, &rp.spad3A20 };
    return words[k];
}
static const unsigned spad_count[] = { 16, 16, 4, 11, 1, 4, 1 };
int rp_load(const char *bank, const char *row0) { return em_player_record_pose_load(&rp, bank, row0); }
/* A copy of the captured record, node records and globals. -2: the attach
 * wrote a structural word that differs from the capture. */
int rp_setup(const uint8_t *record, const uint8_t *nodes, const uint32_t *globals, const uint8_t *scratch,
             uint8_t f3, const uint32_t *spad) {
    memcpy(actor.bytes, record, EM_PLAYER_ACTOR_SIZE);
    d8106F3 = f3;
    if (em_player_record_pose_attach(&rp, &actor, &d8106F3, &stage_scene, &stage_globals) < 0) return -1;
    if (memcmp(actor.bytes, record, EM_PLAYER_ACTOR_SIZE)) return -2;
    memcpy(rp.nodes, nodes, sizeof rp.nodes);
    rp.globals.d275BF8 = globals[0]; rp.globals.d275BF4 = globals[1];
    rp.globals.d275BF0 = globals[2]; rp.globals.d275BEC = globals[3];
    memcpy(rp.globals.d8111F0, scratch, sizeof rp.globals.d8111F0);
    for (unsigned k = 0, at = 0; k < 7; at += spad_count[k], ++k)
        memcpy(spad_word(k), spad + at, 4 * spad_count[k]);
    return 0;
}
int rp_request(int clip, int flags, float blend, int *result)
{ return em_player_record_pose_request(&rp, clip, flags, blend, result); }
int rp_arbiter(int clip, float blend, float frame, int *result)
{ return em_player_record_pose_arbiter(&rp, clip, blend, frame, result); }
int rp_advance(float step, uint32_t *flags) { return em_player_record_pose_advance(&rp, step, flags); }
int rp_animate(void) { return em_player_record_pose_animate(&rp); }
void rp_export(uint8_t *record, uint8_t *nodes, uint32_t *globals, uint8_t *scratch, uint32_t *spad) {
    memcpy(record, actor.bytes, EM_PLAYER_ACTOR_SIZE);
    memcpy(nodes, rp.nodes, sizeof rp.nodes);
    globals[0] = rp.globals.d275BF8; globals[1] = rp.globals.d275BF4;
    globals[2] = rp.globals.d275BF0; globals[3] = rp.globals.d275BEC;
    memcpy(scratch, rp.globals.d8111F0, sizeof rp.globals.d8111F0);
    for (unsigned k = 0, at = 0; k < 7; at += spad_count[k], ++k)
        memcpy(spad + at, spad_word(k), 4 * spad_count[k]);
}
'''

LIB = None
ELF = None
IMAGES = {}
SPAD_WORDS = sum(count for _, _, count in SPAD)


def u32(buf, at):
    return struct.unpack_from('<I', buf, at)[0]


def build_native():
    LANE.mkdir(parents=True, exist_ok=True)
    bridge = LANE / 'bridge.c'
    bridge.write_text(BRIDGE)
    lib = LANE / ('record_pose' + ('.dylib' if sys.platform == 'darwin' else '.so'))
    command = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
               '-fPIC', '-Isrc', str(bridge), *SOURCES, '-lm', '-o', str(lib)]
    digest = hashlib.sha256(' '.join(command).encode() + BRIDGE.encode())
    for path in [ROOT / s for s in SOURCES] + sorted((ROOT / 'src').rglob('*.h')):
        digest.update(path.read_bytes())
    stamp = Path(str(lib) + '.sha256')
    if not (lib.exists() and stamp.exists() and stamp.read_text() == digest.hexdigest()):
        subprocess.run(command, cwd=ROOT, check=True)
        stamp.write_text(digest.hexdigest())
    n = C.CDLL(str(lib))
    P8, PU = C.POINTER(C.c_uint8), C.POINTER(C.c_uint32)
    n.rp_load.argtypes = [C.c_char_p, C.c_char_p]
    n.rp_setup.argtypes = [P8, P8, PU, P8, C.c_uint8, PU]
    n.rp_request.argtypes = [C.c_int, C.c_int, C.c_float, C.POINTER(C.c_int)]
    n.rp_arbiter.argtypes = [C.c_int, C.c_float, C.c_float, C.POINTER(C.c_int)]
    n.rp_advance.argtypes = [C.c_float, PU]
    n.rp_export.argtypes = [P8, P8, PU, P8, PU]
    return n


def row0(clip):
    """D_00248C90's +0 halfword of row `clip`, from the ELF (independent of
    the exported column the module reads)."""
    at = TABLE + STRIDE * clip - 0x100000 + 0x300
    return struct.unpack_from('<h', ELF, at)[0]


def rate(clip):
    at = TABLE + STRIDE * clip + 8 - 0x100000 + 0x300
    return u32(ELF, at)


def animate_original(ee):
    """0015BCF0's animate step (decomp src/func_0015BCF0.c, byte-matched)."""
    mode, hold = ee.load(PLAYER + 0x2F3, 1), ee.load(PLAYER + 0x303, 1)
    clip = PH.s16(ee.load(PLAYER + 0x20C, 2))
    if mode == 0:
        if hold:
            return
        ee.invoke(PH.EVAL if row0(clip) else PH.E68C0, (PLAYER,))
    elif mode in (3, 4):
        ee.invoke(PH.E68C0, (PLAYER,))
    else:
        ee.invoke(PH.E6960, (PLAYER,))


def native_state():
    record = (C.c_uint8 * 0x320)()
    nodes = (C.c_uint8 * (NODE_BYTES * NODE_COUNT))()
    globals_ = (C.c_uint32 * 4)()
    scratch = (C.c_uint8 * 0x6C)()
    spad = (C.c_uint32 * SPAD_WORDS)()
    LIB.rp_export(record, nodes, globals_, scratch, spad)
    return bytes(record), bytes(nodes), list(globals_), bytes(scratch), list(spad)


def original_state(ee):
    spad = []
    for _, at, count in SPAD:
        spad += [u32(ee.spad, at + 4 * k) for k in range(count)]
    return (bytes(ee.mem[PLAYER:PLAYER + 0x320]), bytes(ee.mem[NODES:NODES + NODE_BYTES * NODE_COUNT]),
            [u32(ee.mem, a) for a in (PH.D_BF8, PH.D_BF4, PH.D_BF0, PH.D_BEC)],
            bytes(ee.mem[PH.D_SCRATCH:PH.D_SCRATCH + 0x6C]), spad)


def compare(ee, where):
    names = ('record', 'node records', 'D_00275BF8..BEC', 'D_008111F0', 'scratchpad words')
    for name, a, b in zip(names, original_state(ee), native_state()):
        if a != b:
            if isinstance(a, bytes):
                at = next(i for i in range(len(a)) if a[i] != b[i])
                raise AssertionError((where, name, hex(at), a[at], b[at]))
            raise AssertionError((where, name, a, b))


def setup(label):
    """Both sides at the captured state, +B0 = +A0 (0015BCF0's first copy)."""
    ram, spad = IMAGES[label]
    ram = bytearray(ram)
    ram[PLAYER + 0xB0:PLAYER + 0xC0] = ram[PLAYER + 0xA0:PLAYER + 0xB0]
    ee = PH.EE(ELF, ram=ram, spad=bytearray(spad))
    words = []
    for _, at, count in SPAD:
        words += [u32(spad, at + 4 * k) for k in range(count)]
    buffer = (C.c_uint8 * len(ram)).from_buffer_copy(ram)
    base = C.addressof(buffer)
    status = LIB.rp_setup(C.cast(base + PLAYER, C.POINTER(C.c_uint8)),
                          C.cast(base + NODES, C.POINTER(C.c_uint8)),
                          (C.c_uint32 * 4)(*(u32(ram, a) for a in (PH.D_BF8, PH.D_BF4, PH.D_BF0, PH.D_BEC))),
                          C.cast(base + PH.D_SCRATCH, C.POINTER(C.c_uint8)), ram[PH.D_6F3],
                          (C.c_uint32 * SPAD_WORDS)(*words))
    assert status == 0, (label, 'attach', status)
    compare(ee, (label, 'setup'))
    return ee


def capture_case(label):
    """Re-evaluate the captured record through the module: the captured node
    world matrices, byte for byte."""
    ram = IMAGES[label][0]
    ee = setup(label)
    assert LIB.rp_animate() == 0, (label, 'animate')
    nodes = native_state()[1]
    assert nodes == bytes(ram[NODES:NODES + NODE_BYTES * NODE_COUNT]), (label, 'captured skeleton')
    animate_original(ee)
    compare(ee, (label, 'capture animate'))
    return 1


def clip_case(case):
    """One request of a first-level clip and its first callbacks."""
    label, kind, clip, blend, frame, ticks = case
    ee = setup(label)
    out = C.c_int(-7)
    if kind == 'request':
        v0, _ = ee.invoke(PH.REQUEST, (PLAYER, clip, 1), (PH.F(blend),))
        assert LIB.rp_request(clip, 1, blend, C.byref(out)) == 0, (case, 'native request')
    else:
        v0, _ = ee.invoke(PH.ARBITER, (PLAYER, clip), (PH.F(blend), PH.F(frame)))
        assert LIB.rp_arbiter(clip, blend, frame, C.byref(out)) == 0, (case, 'native arbiter')
    assert v0 == out.value, (case, 'result', v0, out.value)
    compare(ee, (case, 'request'))
    step = rate(clip)
    seen = set()
    for tick in range(ticks):
        v0, _ = ee.invoke(PH.ADVANCE, (PLAYER,), (step,))
        flags = C.c_uint32(0xDEADBEEF)
        assert LIB.rp_advance(PH.fnum(step), C.byref(flags)) == 0, (case, tick, 'native advance')
        assert v0 & 0xFFFF == flags.value & 0xFFFF, (case, tick, 'flags', hex(v0), hex(flags.value))
        animate_original(ee)
        assert LIB.rp_animate() == 0, (case, tick, 'native animate')
        compare(ee, (case, tick))
        seen.add(ee.load(PLAYER + 0x2C, 2) & 0x7FFF)
    return seen


def run_safe(item):
    try:
        if item[0] == 'capture':
            return ('ok', capture_case(item[1]), item)
        return ('ok', clip_case(item[1]), item)
    except AssertionError as error:
        return ('fail', str(error)[:1500], item)


def main():
    global LIB, ELF
    started = time.time()
    for path in (BANK_PATH, ROW0_PATH):
        assert path.exists(), f'{path} is missing (tools/export_player_clips.py, tools/export_player_tables.py)'
    ELF = SR.read_elf()
    PH.ELF = ELF
    LIB = build_native()
    assert LIB.rp_load(str(BANK_PATH).encode(), str(ROW0_PATH).encode()) == 0
    quick = ('05_boxes', '06_hill_slide', '10_cage_roof_roger')
    images = [('playable_ee', PH.REFERENCE / 'playable_ee.bin', None)]
    for beat in sorted(p for p in PH.ROUTE.iterdir() if p.name[:2].isdigit() and p.name[:2] != '15'):
        if reference_mode.FULL or beat.name in quick:
            images.append((beat.name, beat / 'eeMemory.bin', beat / 'scratchpad.bin'))
    for label, path, spad_path in images:
        assert path.exists(), ('captured RAM missing', path)
        IMAGES[label] = (path.read_bytes(), spad_path.read_bytes() if spad_path else bytes(0x4000))
    bank = BANK_PATH.read_bytes()
    for label, (ram, _) in IMAGES.items():
        assert ram[0xD689C0:0xD689C0 + len(bank)] == bank, ('bank differs from the capture', label)

    headers = PH.bank_headers(IMAGES['playable_ee'][0])
    chained = [c for c in FIRST_LEVEL if headers[c][1] >= 0]
    assert chained == [0x5E, 0x73], chained
    items = [('capture', label) for label in IMAGES]
    clip_cases = []
    for label in IMAGES:
        for clip in FIRST_LEVEL:
            frames, nxt, start, _ = headers[clip]
            full = frames + 4 + (headers[nxt][0] // 2 + start + 2 if nxt >= 0 else 0)
            first = 6 if nxt < 0 else frames + 12
            ticks = reference_mode.pick(full, first)
            clip_cases.append((label, 'request', clip, 8.0, 0.0, ticks))
            clip_cases.append((label, 'request', clip, 0.0, 0.0, ticks))
            clip_cases.append((label, 'arbiter', clip, 0.0, float(frames // 2), reference_mode.pick(full, 4)))
    keep = lambda _, c: c[2] in chained and c[0] == 'playable_ee'
    selected = reference_mode.select(clip_cases, 30, 0x1CE, axes=(lambda c: c[2], lambda c: c[1]),
                                     keep=keep)
    items += [('clip', c) for c in selected]
    results = reference_mode.parallel_map(run_safe, items,
                                          cost=lambda item: 1 if item[0] == 'capture' else item[1][5])
    failures = [r for r in results if r[0] == 'fail']
    for f in failures[:8]:
        print('FAIL', f[2], f[1])
    assert not failures, f'{len(failures)} of {len(items)} cases differ'
    chains_seen = set()
    for status, seen, item in results:
        if item[0] == 'clip' and item[1][2] in chained:
            chains_seen |= seen
    assert {0x5F, 0x72} <= chains_seen, ('a chain never reached its follow-on clip', sorted(chains_seen))
    clips = sorted({item[1][2] for item in items if item[0] == 'clip'})
    callbacks = sum(item[1][5] for item in items if item[0] == 'clip')
    reference_mode.banner(f'capture re-evaluation exact on {len(IMAGES)} images',
                          reference_mode.part(len(selected), len(clip_cases), 'clip cases'),
                          f'{len(clips)} of {len(FIRST_LEVEL)} first-level clips, {callbacks:,} callbacks, '
                          f'chains into 0x5F and 0x72')
    print(f'player record pose: original-instruction reference PASSED ({time.time() - started:.1f} s)')


if __name__ == '__main__':
    main()
