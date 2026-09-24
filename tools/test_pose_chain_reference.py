#!/usr/bin/env python3
"""Execute the original clip clock and its chaining, compare em_pose_chain.c.

docs/PLAYER_CLIPS.md. The user's pinned ELF and the captured AREA11 EE RAM
(the playable image and the route beats, docs/FIRST_LEVEL_ROUTE.md) supply
every instruction, the player's clip bank and its node records. The native
side loads assets/player_clips_full.empx (tools/export_player_clips.py);
none of it is embedded here.

Executed, unmodified: 001749A0, 001749F0 (anim_clip_arbiter), 001C67E0
(anim_clip_init), 001C64F0 (anim_advance_time, with its chain step),
001C8480, 001C6120, 001C8710, 001C87C0, 001C8D50 and their key walks /
decoders, 001CA0A0, 001281C0 and 00128250. The interpreter is the shared EE
of test_pose_host_workers_reference.py (every float operation through
tools/ee_float_model.py).

Each case runs the same operations on both sides, starting with a
zero-blend init (which makes the node state a function of the clip and
frame only), then requests, arbiter calls and advances. After every
operation it compares the result word, record +2C, +20C, +3C, node 0 +8E
and, per node, every channel field the routines own (+0..+2C value and
velocity, +30/+40 rotation pair, +50 fraction, +54 reciprocal,
+58/+5C/+60 remaining, +66/+68/+6A key indices): the native values are
read from the decoded state (playback cursors, or the transition while
+2C has 0x8000). Every byte the original writes outside those fields must be
one of the routines' globals (D_00275BEC..BFB, the D_008111F0 scratch
channel record) or the stack / scratchpad.

Cases: the two first-level chains (slide 0x5E -> 0x5F, fall 0x73 -> 0x72)
entered with the route's own blend and frame and through zero blends; the
chain inside a split step (dt 2.5) and under the 1.2 / 0.8 clip rates; the
freeze latch (D_008106F3) on the transition and on the clip; a request that
interrupts the chain transition; a same-id request after the chain (+20C
still names the chained-from clip); loop and hold ends; and a synthetic event
table (patched into the RAM header and the native bank alike). The keys of
the chained clips (every first-level clip under EM_TEST_FULL=1) are also
decoded with the original 001C84D0 / 001C85D0 and compared with the EMPX.

EM_TEST_FULL=1 runs every route beat image and every clip decode.
"""
import ctypes as C
import hashlib
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode  # noqa: E402
import test_pose_host_workers_reference as HW  # noqa: E402
import export_player_clips as EX  # noqa: E402
from test_coll_move_reference import watch_writes  # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
LANE = ROOT / 'build/player-clips'
ASSET = ROOT / 'assets/player_clips_full.empx'
PLAYER = 0x8102B0
REQUEST, ARBITER, ADVANCE = 0x1749A0, 0x1749F0, 0x1C64F0
DEC_R, DEC_T = 0x1C84D0, 0x1C85D0
D_6F3 = 0x8106F3
GLOBALS = [(0x275BEC, 0x275BFC), (0x8111F0, 0x8111F0 + 0x6C)]
EVENT_TABLE = 0x01F00000          # zero RAM in every image (checked)
SOURCES = ('src/game/em_pose_chain.c', 'src/game/em_pose_bank.c', 'src/game/em_pose_transition.c',
           'src/game/em_stream_lanes_original.c')
F = HW.F
fnum = HW.fnum
ELF = None
LIB = None
IMAGES = {}
BANKS = {}


class Key(C.Structure):
    _fields_ = [('time', C.c_uint16), ('hold', C.c_uint16), ('value', C.c_float * 4)]


class Track(C.Structure):
    _fields_ = [('count', C.c_uint32), ('keys', C.POINTER(Key))]


class Clip(C.Structure):
    _fields_ = [('id', C.c_uint16), ('duration', C.c_uint16), ('next', C.c_int16), ('blend', C.c_uint16),
                ('tracks', (Track * 3) * 64)]


class Bank(C.Structure):
    _fields_ = [('bone_count', C.c_uint), ('clip_count', C.c_uint), ('parents', C.c_int32 * 64),
                ('clips', C.POINTER(Clip))]


class ChainBank(C.Structure):
    _fields_ = [('bank', Bank), ('bank_clip_count', C.c_uint32), ('event_first', C.c_void_p),
                ('event_count', C.c_void_p), ('events', C.c_void_p), ('lookup', C.c_void_p)]


class Cursor(C.Structure):
    _fields_ = [('index', C.c_uint), ('remaining', C.c_float), ('reciprocal', C.c_float),
                ('fraction', C.c_float), ('value', C.c_float * 3), ('velocity', C.c_float * 3)]


class Playback(C.Structure):
    _fields_ = [('bank', C.c_void_p), ('clip', C.POINTER(Clip)), ('remaining', C.c_float),
                ('flags', C.c_uint), ('nodes', (Cursor * 3) * 64)]


class Channels(C.Structure):
    _fields_ = [('translation', C.c_float * 3), ('scale', C.c_float * 3), ('rotation', C.c_float * 4)]


class Transition(C.Structure):
    _fields_ = [('count', C.c_uint), ('remaining', C.c_float), ('reciprocal', C.c_float),
                ('fraction', C.c_float), ('active', C.c_int), ('source', Channels * 64),
                ('target', Channels * 64), ('current', Channels * 64),
                ('translation_velocity', (C.c_float * 3) * 64), ('scale_velocity', (C.c_float * 3) * 64)]


class Chain(C.Structure):
    _fields_ = [('bank', C.c_void_p), ('clip', C.POINTER(Clip)), ('clip_word', C.c_uint16),
                ('requested', C.c_int16), ('clock', C.c_uint32), ('hold_frame', C.c_uint16),
                ('posed', C.c_int), ('playback', Playback), ('transition', Transition),
                ('channels', Channels * 64)]


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def build_native():
    LANE.mkdir(parents=True, exist_ok=True)
    lib = LANE / ('pose_chain' + ('.dylib' if sys.platform == 'darwin' else '.so'))
    command = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared', '-fPIC',
               '-Isrc', *SOURCES, '-lm', '-o', str(lib)]
    digest = hashlib.sha256(' '.join(command).encode())
    for path in [ROOT / s for s in SOURCES] + sorted((ROOT / 'src').rglob('*.h')):
        digest.update(path.read_bytes())
    stamp = Path(str(lib) + '.sha256')
    if not (lib.exists() and stamp.exists() and stamp.read_text() == digest.hexdigest()):
        subprocess.run(command, cwd=ROOT, check=True)
        stamp.write_text(digest.hexdigest())
    n = C.CDLL(str(lib))
    PC, PB = C.POINTER(Chain), C.POINTER(ChainBank)
    n.em_pose_chain_bank_load.argtypes = [PB, C.c_char_p]
    n.em_pose_chain_bank_free.argtypes = [PB]
    n.em_pose_chain_init.argtypes = [PC, PB, C.c_int16]
    n.em_pose_chain_request.argtypes = [PC, C.c_int, C.c_int, C.c_float, C.POINTER(C.c_int)]
    n.em_pose_chain_arbiter.argtypes = [PC, C.c_int, C.c_float, C.c_float, C.POINTER(C.c_int)]
    n.em_pose_chain_advance.argtypes = [PC, C.c_float, C.c_int, C.POINTER(C.c_int32)]
    n.em_pose_chain_frames.argtypes = [PB, C.c_int, C.POINTER(C.c_int32)]
    return n


class Mismatch(AssertionError):
    pass


# ------------------------------------------------------------------ comparison

def native_fields(chain, bones):
    """{address offset in node i: word} for every node, plus the record view."""
    nodes = []
    transition = bool(chain.clip_word & 0x8000)
    tr, pb = chain.transition, chain.playback
    for i in range(bones):
        rot, trans, scale = pb.nodes[i][0], pb.nodes[i][1], pb.nodes[i][2]
        f = {}
        if transition:
            for k in range(3):
                f[0x00 + 4 * k] = bits(tr.current[i].translation[k])
                f[0x0C + 4 * k] = bits(tr.translation_velocity[i][k])
                f[0x18 + 4 * k] = bits(tr.current[i].scale[k])
                f[0x24 + 4 * k] = bits(tr.scale_velocity[i][k])
            for k in range(4):
                f[0x30 + 4 * k] = bits(tr.source[i].rotation[k])
                f[0x40 + 4 * k] = bits(tr.target[i].rotation[k])
            f[0x50], f[0x54] = bits(tr.fraction), bits(tr.reciprocal)
            f[0x58] = f[0x5C] = f[0x60] = bits(tr.remaining)
        else:
            for k in range(3):
                f[0x00 + 4 * k] = bits(trans.value[k])
                f[0x0C + 4 * k] = bits(trans.velocity[k])
                f[0x18 + 4 * k] = bits(scale.value[k])
                f[0x24 + 4 * k] = bits(scale.velocity[k])
            keys = pb.clip.contents.tracks[i][0].keys
            for k in range(4):
                f[0x30 + 4 * k] = bits(keys[rot.index - 1].value[k])
                f[0x40 + 4 * k] = bits(keys[rot.index].value[k])
            f[0x50], f[0x54] = bits(rot.fraction), bits(rot.reciprocal)
            f[0x58], f[0x5C], f[0x60] = bits(trans.remaining), bits(scale.remaining), bits(rot.remaining)
        f['index'] = (rot.index, trans.index, scale.index)
        nodes.append(f)
    return nodes


def compare(ee, chain, where):
    mem = ee.mem
    got = dict(clip_word=chain.clip_word, requested=chain.requested & 0xFFFF, clock=chain.clock,
               hold=chain.hold_frame)
    node0 = HW.u32(mem, PLAYER + 0x110)
    want = dict(clip_word=struct.unpack_from('<H', mem, PLAYER + 0x2C)[0],
                requested=struct.unpack_from('<H', mem, PLAYER + 0x20C)[0],
                clock=HW.u32(mem, PLAYER + 0x3C), hold=struct.unpack_from('<H', mem, node0 + 0x8E)[0])
    if got != want:
        raise Mismatch(('record', where, {k: hex(v) for k, v in got.items()},
                        {k: hex(v) for k, v in want.items()}))
    bones = mem[PLAYER + 0xC]
    count = 0
    for i, fields in enumerate(native_fields(chain, bones)):
        node = HW.u32(mem, PLAYER + 0x110 + 4 * i)
        for offset, value in fields.items():
            if offset == 'index':
                actual = struct.unpack_from('<3H', mem, node + 0x66)
            else:
                actual = HW.u32(mem, node + offset)
            count += 1
            if actual != value:
                show = (lambda v: tuple(v)) if offset == 'index' else hex
                raise Mismatch(('node', where, i, offset if offset == 'index' else hex(offset),
                                'native', show(value), 'original', show(actual)))
    return count


def owned_ranges(mem):
    """Addresses the routines may write: the record fields and node channels."""
    ranges = [(PLAYER + 0x2C, PLAYER + 0x2E), (PLAYER + 0x3C, PLAYER + 0x40),
              (PLAYER + 0x20C, PLAYER + 0x20E)] + GLOBALS
    for i in range(mem[PLAYER + 0xC]):
        node = HW.u32(mem, PLAYER + 0x110 + 4 * i)
        ranges += [(node, node + 0x6C)]
        if i == 0:
            ranges.append((node + 0x8E, node + 0x90))
    return ranges


def check_writes(log, ranges, where):
    """Every RAM store the original made lies in a compared field or a global
    (stack and scratchpad stores are the routines' own scratch)."""
    for address, data in log:
        if address >= 0x70000000:
            continue
        for a in range(address, address + len(data)):
            if not any(lo <= a < hi for lo, hi in ranges):
                raise Mismatch(('original wrote a byte the test does not compare', where, hex(a)))
    log.clear()


# ------------------------------------------------------------------ cases

def run_case(case):
    ram, spad = IMAGES[case['image']]
    ram, spad = bytearray(ram), bytearray(spad)
    for at, data in case.get('patch', ()):
        ram[at:at + len(data)] = data
    ee = HW.EE(ELF, ram=ram, spad=spad)
    del ram
    log = watch_writes(ee)
    bank = BANKS[case.get('bank', 'full')]
    chain = Chain()
    requested = struct.unpack_from('<h', ee.mem, PLAYER + 0x20C)[0]
    assert LIB.em_pose_chain_init(C.byref(chain), C.byref(bank), requested) == 0
    ranges = owned_ranges(ee.mem)
    compared = 0
    flags_seen = 0
    for index, op in enumerate(case['ops']):
        where = (case['label'], index, op)
        out = C.c_int(-7)
        if op[0] == 'request':
            _, clip, flags, blend = op
            v0, _ = ee.invoke(REQUEST, (PLAYER, clip, flags), (F(blend),))
            status = LIB.em_pose_chain_request(C.byref(chain), clip, flags, blend, C.byref(out))
            original, native = v0 & 0xFFFFFFFF, out.value & 0xFFFFFFFF
        elif op[0] == 'arbiter':
            _, clip, blend, frame = op
            v0, _ = ee.invoke(ARBITER, (PLAYER, clip), (F(blend), F(frame)))
            status = LIB.em_pose_chain_arbiter(C.byref(chain), clip, blend, frame, C.byref(out))
            original, native = v0 & 0xFFFFFFFF, out.value & 0xFFFFFFFF
        elif op[0] == 'advance':
            _, dt, freeze = op
            ee.mem[D_6F3] = freeze
            v0, _ = ee.invoke(ADVANCE, (PLAYER,), (F(dt),))
            flags = C.c_int32(-7)
            status = LIB.em_pose_chain_advance(C.byref(chain), dt, freeze, C.byref(flags))
            original, native = v0 & 0xFFFF, flags.value & 0xFFFF
            flags_seen |= original
        else:
            raise AssertionError(op)
        if status != 0:
            raise Mismatch(('native fault', where, status))
        if original != native:
            raise Mismatch(('result', where, hex(original), hex(native)))
        check_writes(log, ranges, where)
        compared += compare(ee, chain, where)
    for bit in case.get('expect_flags', ()):
        if not flags_seen & bit:
            raise Mismatch(('case did not reach its path', case['label'], hex(bit), hex(flags_seen)))
    return compared


def run_case_safe(case):
    try:
        return ('ok', run_case(case))
    except Mismatch as error:
        return ('fail', str(error)[:2000])


def adv(n, dt=1.0, freeze=0):
    return [('advance', dt, freeze)] * n


def cases_for(label):
    cases = []

    def case(tag, ops, **kw):
        cases.append(dict(image=label, ops=ops, label=(label, tag), **kw))

    idle = [('request', 1, 1, 0.0)] + adv(3)
    # The route's fall: 0x73 at blend 8 from frame 10 (06/10/11/12 traces show
    # clock 8..1, then 10); held long enough to chain into 0x72 at +8E = 10.
    case('fall_chain_route', idle + [('arbiter', 0x73, 8.0, 10.0)] + adv(8 + 10 + 6),
         expect_flags=(0x4000, 0x8000))
    # The route's slide entry: 0x5E at blend 8 from frame 5 (clock 15 after it).
    case('slide_chain_route', idle + [('arbiter', 0x5E, 8.0, 5.0)] + adv(8 + 15 + 5),
         expect_flags=(0x4000,))
    # Zero-blend entries: the chain from frame 0 (+8E = 0).
    case('fall_chain_zero', [('arbiter', 0x73, 0.0, 0.0)] + adv(20 + 4), expect_flags=(0x4000,))
    case('slide_chain_zero', [('request', 0x5E, 1, 0.0)] + adv(20 + 4), expect_flags=(0x4000,))
    # The chain inside one split call, and under the clip rates 1.2 / 0.8.
    case('chain_split_step', [('arbiter', 0x73, 0.0, 12.0)] + adv(3, 2.5) + adv(2), expect_flags=(0x4000,))
    case('chain_rate_1_2', [('arbiter', 0x5E, 0.0, 3.0)] + adv(18, 1.2) + adv(2, 1.2),
         expect_flags=(0x4000,))
    case('chain_rate_0_8', [('arbiter', 0x73, 0.0, 6.0)] + adv(22, 0.8), expect_flags=(0x4000,))
    # The freeze latch on a transition and on the clip.
    case('freeze', idle + [('arbiter', 0x73, 4.0, 14.0)] + adv(2) + adv(1, freeze=1) + adv(4) +
         adv(1, freeze=1) + adv(3), expect_flags=(0x4000,))
    # Fractional init frames: the transition target is sampled at
    # float_to_int(frame) and +8E = 00128250(frame).
    case('fractional_frame', idle + [('arbiter', 0x73, 6.0, 7.5)] + adv(8) +
         [('arbiter', 0x5E, 0.0, 4.7)] + adv(17), expect_flags=(0x4000, 0x8000))
    # A request that interrupts the chain transition (route: 0x73 then 0x6E).
    # (blend 1: the transition ends on the first advance, clock 3; the chain
    # step is the fourth advance, and the request lands inside its transition.)
    case('interrupt_chain', idle + [('arbiter', 0x73, 1.0, 17.0)] + adv(4) +
         [('request', 0x6E, 0, 4.0)] + adv(6), expect_flags=(0x4000,))
    # After the chain +20C still names 0x73: the same-id request is refused;
    # a forced one restarts 0x73.
    case('same_id_after_chain', [('arbiter', 0x73, 0.0, 18.0)] + adv(4) +
         [('request', 0x73, 0, 8.0)] + adv(2) + [('request', 0x73, 1, 8.0)] + adv(3),
         expect_flags=(0x4000,))
    # Loop (clip 1) and hold (0x6E) ends.
    frames = C.c_int32()
    assert LIB.em_pose_chain_frames(C.byref(BANKS['full']), 0x6E, C.byref(frames)) == 0
    assert LIB.em_pose_chain_frames(C.byref(BANKS['full']), 1, C.byref(frames)) == 0
    case('loop_end', [('request', 1, 1, 0.0)] + adv(3) + [('arbiter', 1, 0.0, frames.value - 5.0)] + adv(8),
         expect_flags=(0x2000,))
    case('hold_end', idle + [('request', 0x6E, 0, 4.0)] + adv(4 + frames.value + 2),
         expect_flags=(0x1000,))
    # A synthetic event table on 0x73 (no player clip has one on the disc).
    header = BANK_ADDRESS + EX.header_of(BANK_BYTES, 0x73)
    table = struct.pack('<hH', 3, 0) + struct.pack('<hHhHhH', 7, 0x10, 5, 0x100, 5, 0x200)
    case('events', [('arbiter', 0x73, 0.0, 9.0)] + adv(14), bank='events', expect_flags=(0x100, 0x10),
         patch=[(header + 0x14, struct.pack('<I', EVENT_TABLE - header)), (EVENT_TABLE, table)])
    return cases


def decode_check(clips):
    """The EMPX keys equal the original 001C84D0 / 001C85D0 decodes."""
    ee = HW.leaf_ee()
    bank, native = BANK_BYTES, BANKS['full']
    by_id = {native.bank.clips[i].id: native.bank.clips[i] for i in range(native.bank.clip_count)}
    src, out = HW.SCRATCH_RAM + 0x900, HW.SCRATCH_RAM + 0x940
    decoded = 0
    for cid in clips:
        clip = by_id[cid]
        header = EX.header_of(bank, cid)
        for kind in range(3):
            table = header + HW.u32(bank, header + 8 + 4 * kind)
            for bone in range(native.bank.bone_count):
                record = table + HW.u32(bank, table + 4 * bone)
                track = clip.tracks[bone][kind]
                for k in range(track.count - 1):
                    raw = bank[record + 12 * k:record + 12 * k + 12]
                    ee.write(src, raw)
                    ee.invoke(DEC_R if kind == 0 else DEC_T, (src, out))
                    width = 4 if kind == 0 else 3
                    want = list(struct.unpack('<%dI' % width, ee.read(out, 4 * width)))
                    got = [bits(track.keys[k].value[j]) for j in range(width)]
                    flags, t = struct.unpack_from('<HH', raw, 8)
                    if got != want or track.keys[k].time != t or track.keys[k].hold != bool(flags & 0x8000):
                        raise Mismatch(('decode', hex(cid), kind, bone, k))
                    decoded += 1
    return decoded


BANK_ADDRESS = EX.BANK_ADDRESS
BANK_BYTES = None


def main():
    global ELF, LIB, BANK_BYTES
    started = time.time()
    if not ASSET.exists():
        sys.exit('assets/player_clips_full.empx missing: run python3 tools/export_player_clips.py')
    ELF = HW.SR.read_elf()
    HW.ELF = ELF
    LIB = build_native()
    BANK_BYTES = (DECOMP / 'extract/chunk28/f01_id3c.bin').read_bytes()
    full = ChainBank()
    assert LIB.em_pose_chain_bank_load(C.byref(full), str(ASSET).encode()) == 1, 'EMPX refused'
    BANKS['full'] = full
    # The event variant: the first-level clips with a synthetic table on 0x73.
    sys.path.insert(0, str(DECOMP / 'tools'))
    from export_opening_actors import OpeningClip
    payload, _ = EX.build_empx(BANK_BYTES, list(EX.FIRST_LEVEL), OpeningClip,
                               events_override={0x73: [(7, 0x10), (5, 0x100), (5, 0x200)]})
    events_path = LANE / 'events.empx'
    events_path.write_bytes(payload)
    events = ChainBank()
    assert LIB.em_pose_chain_bank_load(C.byref(events), str(events_path).encode()) == 1
    BANKS['events'] = events

    labels = ['playable_ee']
    IMAGES['playable_ee'] = ((DECOMP / 'build/startup-reference/playable_ee.bin').read_bytes(), bytes(0x4000))
    route = DECOMP / 'build/s87/route'
    quick = ('06_hill_slide', '10_cage_roof_roger')
    for beat in sorted(p for p in route.iterdir() if p.name[:2].isdigit() and p.name < '15'):
        if reference_mode.FULL or beat.name in quick:
            IMAGES[beat.name] = ((beat / 'eeMemory.bin').read_bytes(), (beat / 'scratchpad.bin').read_bytes())
            labels.append(beat.name)
    if not reference_mode.FULL:
        assert len(labels) == 1 + len(quick), ('route captures missing', labels)
    for label, (ram, _) in IMAGES.items():
        assert HW.u32(ram, PLAYER + 0x40) == BANK_ADDRESS, label
        assert ram[BANK_ADDRESS:BANK_ADDRESS + len(BANK_BYTES)] == BANK_BYTES, ('bank differs', label)
        assert ram[EVENT_TABLE:EVENT_TABLE + 0x100] == bytes(0x100), ('event scratch not zero', label)

    all_cases = [c for label in labels for c in cases_for(label)]
    # Quick: every case class on the first image, the route chains on every image.
    cases = reference_mode.select(all_cases, 26, 0xC4A1, axes=(lambda c: c['label'][1],),
                                  keep=lambda i, c: c['label'][1].endswith('_route'))
    results = reference_mode.parallel_map(run_case_safe, cases, cost=lambda c: len(c['ops']))
    failed = [(c['label'], r[1]) for c, r in zip(cases, results) if r[0] != 'ok']
    for label, error in failed[:5]:
        print('FAIL', label, error)
    assert not failed, f'{len(failed)} of {len(cases)} cases differ'
    compared = sum(r[1] for r in results)

    decode_clips = list(EX.FIRST_LEVEL) if reference_mode.FULL else [0x5E, 0x5F, 0x72, 0x73]
    decoded = decode_check(decode_clips)

    LIB.em_pose_chain_bank_free(C.byref(events))
    LIB.em_pose_chain_bank_free(C.byref(full))
    reference_mode.banner(reference_mode.part(len(cases), len(all_cases), 'cases'),
                          f'{len(labels)} images', f'{decoded:,} decoded keys ({len(decode_clips)} clips)')
    print(f'pose chain reference PASS: {compared:,} fields equal the original after every operation '
          f'({time.time() - started:.1f} s)')


if __name__ == '__main__':
    main()
