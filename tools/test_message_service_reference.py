#!/usr/bin/env python3
"""Original message service (WP-8) against the native em_message_service.

The original 001FCA10 / 001FDB80 / 001FD790 / 001FD950 / 001FD580 /
001FD6A0 / 001FAB80 / 001FC9B0 / 001B7D60 / 001FD4C0 / 001FA5A0 instructions
run in the project MIPS oracle over captured AREA11 RAM (user-local, never
copied into the repository). The native service reads the same captured
tables through ctypes. Each tick compares the 0x9C-byte D_002821B0 block,
the FC9B0 text defaults, D_008106F4/F5, the D_008106D4 flag mailbox and the
ordered worker calls: glyph draw (001FE070), face talk (001D06E0), voice
push (001FA5A0), lane stop (001FAAC0), stream stop/play (001FD470/001FA790),
mode-3 presenter (001FD0E0) and mode-4 presenters (001FCB90/001FCF90/001FCF60).

Glyph layout (001FE480/001FE530/001CC170), the voice lane 001F9CF0 that
accepts a pushed voice (D_008106F5 2 -> 1) and the stream busy bytes
D_00282155/156 are external. The harness drives them identically on both
sides and reports them as boundaries.
"""
import ctypes as C
import hashlib
import json
import struct
import subprocess
import sys

from test_door_original_reference import DoorOracle
from test_interaction_scan_reference import DECOMP, ELF_SHA
from test_point_light_reference import signed
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CAPTURES = DECOMP / 'build/startup-reference'
BLOCK, BLOCK_SIZE = 0x2821B0, 0x9C
F4, F5, MAILBOX, AREA = 0x8106F4, 0x8106F5, 0x8106D4, 0x810700
SPAD_MODE = 0x70003B8F
RECORD, STATE = 0x960000, 0x970000
SETUP_RESULT = 0x1234
SEEN = []  # every original event of the message cases, for coverage checks
(DRAW, FACE, VOICE, STOP_LANE, STREAM_STOP, STREAM_PLAY, MODE3, HELP, SETUP,
 RECORD_DRAW) = range(1, 11)
# Functions whose instructions must be identical between ELF and capture.
EXECUTED = ((0x1FCA10, 0x1FCB90), (0x1FD4C0, 0x1FDDB0), (0x1FC9B0, 0x1FCA10),
            (0x1FAB80, 0x1FABB0), (0x1FA5A0, 0x1FA5F0), (0x1B7D60, 0x1B7F88))


class MessageOracle(DoorOracle):
    """Capture-backed memory with a write overlay."""

    def __init__(self, elf, capture, scratch):
        super().__init__(elf)
        self.mem.clear()  # drop the base oracle's context word; RAM is the capture
        self.capture, self.scratch = capture, scratch
        self.events = []
        glyph = lambda value: (lambda o: o.r.__setitem__(2, value))
        self.calls.update({
            0x1FE480: glyph(0x950000), 0x1FE530: glyph(0x950000), 0x1CC170: glyph(10),
            0x1FE070: self.draw, 0x1D06E0: self.face, 0x1FA5A0: self.voice,
            0x1FAAC0: lambda o: o.events.append((STOP_LANE, signed(o.r[4]))),
            0x1FD470: lambda o: o.events.append((STREAM_STOP, signed(o.r[4]))),
            0x1FA790: lambda o: o.events.append((STREAM_PLAY, signed(o.r[4]), signed(o.r[5]))),
            0x1FD0E0: self.mode3, 0x1FCB90: self.help, 0x1FCF90: self.setup,
            0x1FCF60: lambda o: o.events.append((RECORD_DRAW, *[signed(v) for v in o.r[4:7]])),
            0x121A28: self.memset,
        })

    def load(self, address, size=4):
        value = 0
        for i in range(size):
            a = address + i
            if a in self.mem: byte = self.mem[a]
            elif a < len(self.capture): byte = self.capture[a]
            elif 0x70000000 <= a < 0x70004000 and self.scratch: byte = self.scratch[a - 0x70000000]
            else: raise AssertionError(('unmapped load', hex(a)))
            value |= byte << (8 * i)
        return value

    def plain(self, word):
        if word >> 26 == 0 and word & 63 == 62:    # dsrl32
            self.r[word >> 11 & 31] = (self.r[word >> 16 & 31] & 0xFFFFFFFFFFFFFFFF) >> ((word >> 6 & 31) + 32)
            return
        if word >> 26 == 0 and word & 63 in (10, 11):  # movz / movn
            # Reached only through 001B7D60, whose C is byte-matched; the
            # op0C cases below cross-check this against that C.
            if (self.r[word >> 16 & 31] == 0) == (word & 63 == 10):
                self.r[word >> 11 & 31] = self.r[word >> 21 & 31]
            return
        if word >> 26 == 0 and word & 63 == 39:    # nor
            self.r[word >> 11 & 31] = ~(self.r[word >> 21 & 31] | self.r[word >> 16 & 31]) & 0xFFFFFFFF
            return
        super().plain(word)

    def draw(self, o):
        # 001FD950: centred x from the two measured segments, y 0xC2.
        assert o.r[6:8] == [0x100 - (10 >> 1), 0xC2], o.r[6:8]
        table = o.r[4]
        glob = table == o.load(0x28A4E8)
        assert glob or table == o.load(0x28A594)
        o.events.append((DRAW, int(glob), o.r[5] & 0xFFFFFFFF))

    def face(self, o):
        assert o.r[4] == 0x8102B0
        o.events.append((FACE, o.r[5]))

    def voice(self, o):
        o.events.append((VOICE, signed(o.r[4])))
        o.r[2] = 1

    def mode3(self, o):
        assert o.r[4:6] == [BLOCK, 2]
        o.events.append((MODE3,))

    def help(self, o):
        o.events.append((HELP, *[signed(v) for v in o.r[4:8]]))

    def setup(self, o):
        o.events.append((SETUP, *[signed(v) for v in o.r[4:7]]))
        o.r[2] = SETUP_RESULT

    def memset(self, o):
        assert o.r[4:7] == [BLOCK, 0, BLOCK_SIZE]
        for address in range(BLOCK, BLOCK + BLOCK_SIZE): o.save(address, 0, 1)

    def defaults(self):
        return self.read(0x275C50, 4) + self.read(0x275C54, 2)

    def shared(self):
        return self.read(F4, 2) + self.read(MAILBOX, 12)


BRIDGE = r'''
#include "game/em_message_service.h"
#include <string.h>
static EmMessageService service;
static EmMessageData data;
static EmMessageTable areas[32];
static uint8_t voice_mode, stream_mode, mailbox[12];
static EmMessageShared shared;
static int32_t events[8192][5];
static int event_count, setup_result;
static int push(int a, int b, int c, int d, int e) {
    if (event_count >= 8192) return 0;
    int32_t *v = events[event_count++]; v[0]=a; v[1]=b; v[2]=c; v[3]=d; v[4]=e; return 1;
}
static int w_draw(void *c, int g, uint32_t i) {(void)c; return push(1, g, (int)i, 0, 0);}
static int w_face(void *c, int on) {(void)c; return push(2, on, 0, 0, 0);}
static int w_voice(void *c, int32_t cue) {(void)c; return push(3, cue, 0, 0, 0);}
static int w_lane(void *c, int lane) {(void)c; return push(4, lane, 0, 0, 0);}
static int w_sstop(void *c, int32_t m) {(void)c; return push(5, m, 0, 0, 0);}
static int w_splay(void *c, int l, int32_t cue) {(void)c; return push(6, l, cue, 0, 0);}
static int w_mode3(void *c, EmMessageBlock *b) {(void)c; (void)b; return push(7, 0, 0, 0, 0);}
static int w_help(void *c, int x, int y, int32_t g, uint32_t l) {(void)c; return push(8, x, y, g, (int)l);}
static int w_setup(void *c, uint32_t l, int32_t a, int32_t g, int32_t *r) {
    (void)c; *r = setup_result; return push(9, (int)l, a, g, 0);}
static int w_rdraw(void *c, uint32_t l, int x, int y) {(void)c; return push(10, (int)l, x, y, 0);}
int configure(const EmMessageRecord *g, uint32_t gc, uint32_t ac, const EmMessageStreamRow *rows,
              uint32_t rc, int32_t color, uint32_t cursor, int32_t result) {
    if (ac > 32) return 0;
    memset(&data, 0, sizeof data); memset(areas, 0, sizeof areas);
    data.global.records = g; data.global.count = gc; data.areas = areas; data.area_count = ac;
    data.streams = rows; data.stream_count = rc; data.default_color = color;
    data.default_cursor = cursor; setup_result = result;
    EmMessageWorkers w = {0, w_draw, w_face, w_voice, w_lane, w_sstop, w_splay,
                          w_mode3, w_help, w_setup, w_rdraw};
    return em_message_init(&service, &data, &w);
}
int set_area(int i, const EmMessageRecord *r, uint32_t n) {
    if (i < 0 || i >= 32) return 0;
    areas[i].records = r; areas[i].count = n; return 1;}
void set_state(const uint8_t *block, const uint8_t *defaults, const uint8_t *sh,
               int area, int mode, int b155, int b156) {
    memcpy(&service.block, block, sizeof service.block);
    memcpy(&service.text_color, defaults, 4);
    service.text_glyph = defaults[4]; service.text_flag = defaults[5];
    stream_mode = sh[0]; voice_mode = sh[1]; memcpy(mailbox, sh + 2, 12);
    shared.area = (uint8_t)area; shared.game_mode = (uint8_t)mode;
    shared.busy155 = (int8_t)b155; shared.busy156 = (int8_t)b156;
    shared.voice_mode = &voice_mode; shared.stream_mode = &stream_mode;
    shared.flag_mailbox = mailbox; service.fault = 0;
}
void set_inputs(int b155, int b156, int f5) {
    shared.busy155 = (int8_t)b155; shared.busy156 = (int8_t)b156; voice_mode = (uint8_t)f5;}
void get_state(uint8_t *block, uint8_t *defaults, uint8_t *sh) {
    memcpy(block, &service.block, sizeof service.block);
    memcpy(defaults, &service.text_color, 4);
    defaults[4] = service.text_glyph; defaults[5] = service.text_flag;
    sh[0] = stream_mode; sh[1] = voice_mode; memcpy(sh + 2, mailbox, 12);
}
int tick(void) {return em_message_tick(&service, &shared);}
int op0c(uint8_t *hs, const unsigned char *record) {return em_message_op0c(&service, hs, record);}
int stream_request(int32_t line) {return em_message_stream_request(&service, &shared, line);}
int ring_push(EmMessageVoiceRing *r, int32_t cue) {return em_message_voice_ring_push(r, cue);}
int take_events(int32_t *out) {
    int n = event_count; memcpy(out, events, sizeof events[0] * (size_t)n); event_count = 0; return n;}
const char *fault(void) {return service.fault;}
'''


class Record(C.Structure):
    _fields_ = [('duration', C.c_uint16), ('voice', C.c_int16), ('slot', C.c_uint8),
                ('wait_stream', C.c_uint8), ('unread', C.c_uint8 * 2)]


class Row(C.Structure):
    _fields_ = [('area', C.c_int32), ('word1', C.c_int32), ('line', C.c_int32), ('cue', C.c_int32)]


class Ring(C.Structure):
    _fields_ = [('slots', C.c_int32 * 16), ('head', C.c_int8)]


def build_native(out):
    (out / 'bridge.c').write_text(BRIDGE)
    library = out / 'message.dylib'
    names = ('configure', 'set_area', 'set_state', 'set_inputs', 'get_state', 'tick', 'op0c',
             'stream_request', 'ring_push', 'take_events', 'fault')
    subprocess.run(['cc', '-dynamiclib', '-Wl,-undefined,dynamic_lookup', '-Wl,-dead_strip',
                    *[f'-Wl,-exported_symbol,_{name}' for name in names],
                    '-O1', '-g', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Isrc', str(out / 'bridge.c'),
                    'src/game/em_message_service.c', '-o', str(library)], cwd=ROOT, check=True)
    lib = C.CDLL(str(library))
    lib.fault.restype = C.c_char_p
    lib.stream_request.argtypes = [C.c_int32]
    lib.ring_push.argtypes = [C.POINTER(Ring), C.c_int32]
    return lib


# D_00264DD0 is 24 words: the next object, D_00264E30, is a separate
# variable (001FE8D0 stores to D_00264E30/34/38 on their own). Word 0 is the
# global bank, word area + 1 the area bank (001FD790).
TABLE_WORDS = (0x264E30 - 0x264DD0) // 4


def table_bounds(capture):
    """(pointers, counts): each table ends where the next table the original
    points at begins. Candidates are every non-zero D_00264DD0 word plus
    the 001FEE60 stream-name tables D_00275848[2] and D_00264E40[0x17]
    (the global bank [0] is followed by the D_00275848 path strings)."""
    word = lambda a: struct.unpack_from('<I', capture, a)[0]
    pointers = [word(0x264DD0 + 4 * i) for i in range(TABLE_WORDS)]
    starts = {p for p in pointers if p}
    starts |= {word(0x275848 + 4 * i) for i in range(2)}
    starts |= {word(0x264E40 + 4 * i) for i in range(0x17)}
    counts = [(min(s for s in starts if s > p) - p) // 8 if p else 0 for p in pointers]
    return pointers, counts


class Native:
    """Captured tables handed to the native service (kept alive here)."""

    def __init__(self, lib, capture):
        self.lib = lib
        pointers, self.counts = table_bounds(capture)
        def records(pointer, count):
            array = (Record * count)()
            C.memmove(array, capture[pointer:pointer + 8 * count], 8 * count)
            return array
        self.global_table = records(pointers[0], self.counts[0])
        rows, address = [], 0x26EC60
        while struct.unpack_from('<i', capture, address)[0] != -1:
            rows.append(Row(*struct.unpack_from('<4i', capture, address)))
            address += 16
        self.rows = (Row * len(rows))(*rows)
        assert lib.configure(self.global_table, self.counts[0], TABLE_WORDS - 1, self.rows, len(rows),
                             struct.unpack_from('<i', capture, 0x26EC10)[0], 0x264D10, SETUP_RESULT)
        self.areas = {}
        for area, pointer in enumerate(pointers[1:]):
            if not pointer: continue
            count = self.counts[area + 1]
            self.areas[area] = records(pointer, count)
            assert lib.set_area(area, self.areas[area], count)

    def load(self, block, defaults, shared, area, mode, b155=0, b156=0):
        self.lib.set_state(block, defaults, shared, area, mode, b155, b156)

    def state(self):
        block, defaults, shared = (C.c_uint8 * BLOCK_SIZE)(), (C.c_uint8 * 6)(), (C.c_uint8 * 14)()
        self.lib.get_state(block, defaults, shared)
        return bytes(block), bytes(defaults), bytes(shared)

    def events(self):
        out = (C.c_int32 * (8192 * 5))()
        n = self.lib.take_events(out)
        result = []
        for i in range(n):
            kind, *args = out[i * 5:i * 5 + 5]
            width = {DRAW: 2, FACE: 1, VOICE: 1, STOP_LANE: 1, STREAM_STOP: 1, STREAM_PLAY: 2,
                     MODE3: 0, HELP: 4, SETUP: 3, RECORD_DRAW: 3}[kind]
            args = args[:width]
            if kind == DRAW: args[1] &= 0xFFFFFFFF
            result.append((kind, *args))
        return result


def load_capture(name):
    """(ee, scratchpad); a capture without its own scratchpad gets None and
    any scratchpad read then fails instead of borrowing another capture's."""
    folder = CAPTURES / name
    if folder.is_dir():
        ee, scratch = folder / 'eeMemory.bin', folder / 'scratchpad.bin'
    else:
        ee, scratch = CAPTURES / f'{name}_ee.bin', CAPTURES / f'{name}_scratchpad.bin'
    return ee.read_bytes(), scratch.read_bytes() if scratch.exists() else None


def fresh(elf, capture, scratch, native, game_mode, f5=None, block=None):
    o = MessageOracle(elf, capture, scratch)
    o.save(SPAD_MODE, game_mode, 1)
    if f5 is not None: o.save(F5, f5, 1)
    o.write(BLOCK, block if block is not None else bytes(BLOCK_SIZE))
    native.load(o.read(BLOCK, BLOCK_SIZE), o.defaults(), o.shared(), o.load(AREA, 1), game_mode,
                signed(o.load(0x282155, 1), 8), signed(o.load(0x282156, 1), 8))
    return o


def compare(o, native, where):
    block, defaults, shared = native.state()
    assert o.read(BLOCK, BLOCK_SIZE) == block, (where, o.read(BLOCK, BLOCK_SIZE).hex(), block.hex())
    assert o.defaults() == defaults, (where, o.defaults().hex(), defaults.hex())
    assert o.shared() == shared, (where, o.shared().hex(), shared.hex())
    native_events = native.events()
    assert o.events == native_events, (where, o.events, native_events)


def op_record(sub, line, delay, flag=0):
    return struct.pack('<8I', 0x0C, 0, sub, 0, 0, line, delay, flag)


def op0c_both(o, native, sub, line, delay, flag, handshake):
    record = op_record(sub, line, delay, flag)
    o.write(RECORD, record)
    o.save(STATE + 4, handshake[0], 1)
    o.run(0x1B7D60, (0, STATE, RECORD))
    expected = o.r[2]
    native_handshake = (C.c_uint8 * 1)(handshake[0])
    got = native.lib.op0c(native_handshake, record)
    assert got == expected, (sub, line, got, expected)
    assert native_handshake[0] == o.load(STATE + 4, 1)
    handshake[0] = native_handshake[0]
    return got


def message_case(elf, capture, scratch, native, line, delay, f5, game_mode,
                 busy=None, accept=3, limit=4000, patch=None, tag=None):
    """op0C sub0 posts `line`; tick until teardown. The voice lane
    (001F9CF0) is modelled only as 'F5 2 -> 1 after `accept` ticks'.
    `patch` {index: (duration, voice, slot, wait)} rewrites area-11 records
    identically in oracle RAM and the native table (synthetic cases only)."""
    o = fresh(elf, capture, scratch, native, game_mode, f5)
    saved = {}
    for index, fields in (patch or {}).items():
        address = struct.unpack_from('<I', capture, 0x264DD0 + 4 * 12)[0] + 8 * index
        o.write(address, struct.pack('<HhBB', *fields))
        saved[index] = bytes(native.areas[11][index])
        C.memmove(C.byref(native.areas[11][index]), struct.pack('<HhBB', *fields), 6)
    try:
        return run_message(o, native, line, delay, f5, game_mode, busy, accept, limit,
                           line if tag is None else tag)
    finally:
        for index, raw in saved.items():
            C.memmove(C.byref(native.areas[11][index]), raw, 8)


def run_message(o, native, line, delay, f5, game_mode, busy, accept, limit, tag):
    handshake = [0]
    assert op0c_both(o, native, 0, line, delay, 0, handshake) == 0
    compare(o, native, ('post', line))
    waiting = done_polls = ticks = 0
    for tick in range(limit):
        b155 = b156 = 0
        if busy and busy[0] <= tick < busy[1]:
            b155, b156 = busy[2], busy[3]
        voice = o.load(F5, 1)
        waiting = waiting + 1 if voice == 2 else 0
        if waiting > accept:
            voice, waiting = 1, 0
        o.save(0x282155, b155 & 0xFF, 1); o.save(0x282156, b156 & 0xFF, 1); o.save(F5, voice, 1)
        native.lib.set_inputs(b155, b156, voice)
        o.events = []
        o.run(0x1FCA10)
        assert native.lib.tick() == 0, native.lib.fault()
        SEEN.extend((tag, event) for event in o.events)
        compare(o, native, (hex(line), delay, f5, game_mode, tick))
        ticks += 1
        if op0c_both(o, native, 0, line, delay, 0, handshake):
            done_polls += 1
        if done_polls and o.load(BLOCK + 4) == 0:
            return ticks
    raise AssertionError(('message did not finish', hex(line), delay, f5))


def table_end_case(elf, capture, scratch, native, f5):
    """Real area-11 data: line 0xA1 is the table's last record (0,0,0,0),
    so 001FD790 skips it and walks to 0xA2, which is the first record of the
    next table (D_00264DD0[14]). The native service must latch its
    'record outside table' fault on exactly that tick; until then every
    tick agrees with the original."""
    tag = f'table-end-f5={f5}'
    count = native.counts[12]
    o = fresh(elf, capture, scratch, native, 2, f5)
    handshake = [0]
    assert op0c_both(o, native, 0, count - 1, 0, 0, handshake) == 0
    compare(o, native, (tag, 'post'))
    waiting = 0
    for tick in range(40):
        voice = o.load(F5, 1)
        waiting = waiting + 1 if voice == 2 else 0
        if waiting > 3:
            voice, waiting = 1, 0
        o.save(F5, voice, 1)
        native.lib.set_inputs(0, 0, voice)
        o.events = []
        o.run(0x1FCA10)
        if native.lib.tick() == -1:
            assert native.lib.fault() == b'message record outside table', native.lib.fault()
            native_current = struct.unpack_from('<I', native.state()[0], 0x34)[0]
            # The original read on past the table on this same tick.
            assert native_current == count and o.load(BLOCK + 0x34) >= count, \
                (tag, native_current, hex(o.load(BLOCK + 0x34)))
            assert native.events() == []
            assert native.lib.tick() == -1                        # latched
            assert native.lib.op0c((C.c_uint8 * 1)(1), op_record(0, 0x97, 0, 0)) == -1
            return tick + 1
        SEEN.extend((tag, event) for event in o.events)
        compare(o, native, (tag, tick))
    raise AssertionError((tag, 'native never faulted past the table'))


def captured_resume(elf, name, native, limit, busy=(0, 0)):
    """Resume the captured block and run it to teardown (phase 0), or for
    `limit` ticks when the captured request never completes by itself."""
    capture, scratch = load_capture(name)
    o = MessageOracle(elf, capture, scratch)
    # The panel capture has no scratchpad; mode 4 never reads 0x70003B8F and
    # the oracle raises if it would, so the native gets an inert value.
    game_mode = o.load(SPAD_MODE, 1) if scratch else 0xFF
    native.load(o.read(BLOCK, BLOCK_SIZE), o.defaults(), o.shared(), o.load(AREA, 1),
                game_mode, *busy)
    o.save(0x282155, busy[0], 1); o.save(0x282156, busy[1], 1)
    for tick in range(limit):
        o.events = []
        o.run(0x1FCA10)
        assert native.lib.tick() == 0, native.lib.fault()
        SEEN.extend((name, event) for event in o.events)
        compare(o, native, (name, tick))
        if o.load(BLOCK + 4) == 0:
            return tick + 1, True
    return limit, False


def mode_cases(elf, capture, scratch, native):
    count = 0
    zero = bytes(BLOCK_SIZE)
    def block(mode, phase, line=0, aux=0, aux_arg=0, mask=0, loaded=0):
        b = bytearray(zero)
        struct.pack_into('<iiI', b, 0, mode, phase, line)
        struct.pack_into('<i', b, 0x5C, loaded)
        struct.pack_into('<I', b, 0x64, mask)
        struct.pack_into('<ii', b, 0x90, aux, aux_arg)
        b[0x51] = 0xFF
        return bytes(b)
    cases = [block(3, 1), block(4, 1, 8, 5), block(4, 1, 0x21, 0x64, 7),
             block(16, 1, 5), block(0, 1), block(1, 1), block(9, 1), block(2, 0), block(2, 3),
             block(2, 2, 0x97, mask=0x803), block(4, 2, 1, 5, mask=0x1),
             block(2, 1, 0x97, loaded=2)]  # FDB80 returns 1 for sub-states other than 0/1
    for game_mode in (1, 2):
        for image in cases:
            o = fresh(elf, capture, scratch, native, game_mode, 1, image)
            o.save(MAILBOX, 0x01010101, 4)
            native.load(o.read(BLOCK, BLOCK_SIZE), o.defaults(), o.shared(), 11, game_mode)
            for tick in range(2):
                o.events = []
                o.run(0x1FCA10)
                assert native.lib.tick() == 0, native.lib.fault()
                compare(o, native, ('mode', image[:8].hex(), game_mode, tick))
                count += 1
    return count


def op0c_cases(elf, capture, scratch, native):
    count = 0
    for sub in range(8):
        for handshake in (0, 1, 2):
            for phase in (0, 1, 2):
                for mode in (0, 2):
                    for status in (0, 1, 0x8000):
                        for flag in (0, 1):
                            b = bytearray(BLOCK_SIZE)
                            struct.pack_into('<ii', b, 0, mode, phase)
                            struct.pack_into('<H', b, 0x74, status)
                            o = fresh(elf, capture, scratch, native, 2, 0, bytes(b))
                            h = [handshake]
                            op0c_both(o, native, sub, 0x97, 30, flag, h)
                            o.events = []
                            compare(o, native, ('op0c', sub, handshake, phase, mode, status, flag))
                            count += 1
    return count


def stream_cases(elf, capture, scratch, native):
    count = 0
    for area in (11, 3):
        for line in (0, 25, 102, 0x97, 9999, -1):
            o = fresh(elf, capture, scratch, native, 2, 0)
            o.save(AREA, area, 1)
            native.load(o.read(BLOCK, BLOCK_SIZE), o.defaults(), o.shared(), area, 2)
            o.events = []
            o.run(0x1FD4C0, (line & 0xFFFFFFFF,))
            got = native.lib.stream_request(line)
            assert got == o.r[2], (area, line, got, o.r[2])
            compare(o, native, ('stream', area, line))
            count += 1
    return count


def ring_cases(elf, capture, scratch, lib):
    count = 0
    for head in (0, 7, 15):
        for occupied in (False, True):
            for cue in (150, 149, -1, 0):
                o = MessageOracle(elf, capture, scratch)
                slots = [-1] * 16
                if occupied: slots[head] = 63
                for i, value in enumerate(slots): o.save(0x281CF0 + 4 * i, value & 0xFFFFFFFF)
                o.save(0x275B30, head, 1)
                o.run(0x1FA5A0, (cue & 0xFFFFFFFF,))
                ring = Ring((C.c_int32 * 16)(*slots), head)
                assert lib.ring_push(C.byref(ring), cue) == o.r[2] == 1
                assert list(ring.slots) == [signed(o.load(0x281CF0 + 4 * i)) for i in range(16)]
                assert ring.head == signed(o.load(0x275B30, 1), 8)
                count += 1
    return count


def coverage(report):
    """The comparisons above only prove agreement; these prove the voice,
    face and draw paths actually ran in the original."""
    def events(line, kind):
        return [e for l, e in SEEN if l == line and e[0] == kind]
    # Director op0C lines: 001FD580 pushes VOICE.DAT cues 150/149; records
    # 0x97/0x99 (duration 118/198) are drawn duration+1 times in each of the
    # 10 cases per line (9 delay/F5 cases + 1 busy case): FD950 also draws
    # on the zero-timer completion tick.
    assert (VOICE, 150) in events(0x97, VOICE) and (VOICE, 149) in events(0x99, VOICE)
    assert events(0x97, DRAW).count((DRAW, 0, 0x97)) == 119 * 10
    assert events(0x99, DRAW).count((DRAW, 0, 0x99)) == 199 * 10
    assert {e[1] for e in events(0x80000018, DRAW)} == {1}  # global bank
    assert {e[1] for e in events(0x97, DRAW)} == {0}        # area bank
    # 001FD6A0 hit path: later voices of one message are pushed in order.
    assert [e[1] for e in events(0x7F, VOICE)][:3] == [143, 144, 145]
    # 0x13 (166 frames, cue 1, slot 1): one voice, then 001FD6A0 scans
    # 0x14..0x17 (voice -1) and misses on the 0x18 terminal; slot 1 has no
    # face talk.
    assert events(0x13, VOICE) == [(VOICE, 1)] and not events(0x13, FACE)
    assert events(0x13, DRAW).count((DRAW, 0, 0x13)) == 167
    # Synthetic zero-duration voiced record 0x98: pushed by 001FD6A0 even
    # though 001FD790 never presents it.
    assert [e[1] for e in events('synthetic-zero', VOICE)] == [150, 151]
    assert not events('synthetic-zero', DRAW).count((DRAW, 0, 0x98))
    # Table end 0xA1 (0,0,0,0): with D_008106F5 = 0, 001FD580 pushes its
    # voice word 0 before the walk past the table; with 1/2 it does not.
    assert events('table-end-f5=0', VOICE) == [(VOICE, 0)]
    assert not events('table-end-f5=1', VOICE) and not events('table-end-f5=2', VOICE)
    assert (FACE, 1) in events(0x97, FACE) and (FACE, 0) in events(0x97, FACE)
    assert not events(0x9B, FACE)                           # game mode 1: no face
    assert events(0x98, DRAW) and not events(0x98, VOICE)   # FD580 miss on a terminal line
    assert events('mode1-0x97', DRAW) and not events('mode1-0x97', FACE)
    assert [e[1] for e in events('synthetic', VOICE)] == [150, 151]
    assert not events(0x66, VOICE)                          # stream-table claim (return 2)
    assert (STOP_LANE, 1) in events('opening', STOP_LANE)
    assert (FACE, 0) in events('roger-encounter', FACE)
    report['voice_pushes'] = sorted({e[1] for _, e in SEEN if e[0] == VOICE})


def main():
    assert sys.platform == 'darwin', 'Host harness uses the macOS dead-strip linker'
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    out = ROOT / 'build/message_service_reference'
    out.mkdir(parents=True, exist_ok=True)
    lib = build_native(out)
    capture, scratch = load_capture('opening')
    for name in ('opening', 'roger-encounter', 'panel'):
        image = load_capture(name)[0]
        for start, end in EXECUTED:
            assert image[start:end] == elf[start - 0x100000 + 0x300:end - 0x100000 + 0x300], (name, hex(start))
    assert capture[AREA] == 11
    native = Native(lib, capture)
    assert (native.areas[11][0x97].voice, native.areas[11][0x99].voice) == (150, 149)
    # Table sizes derived from the pointers above: area 11 = [12] 0x2704D0
    # up to [14] 0x2709E0; global = [0] 0x272DF0 up to D_00275848[0].
    assert (native.counts[12], native.counts[0]) == (162, 54), native.counts
    assert bytes(native.areas[11][161])[:6] == bytes(6)          # 0xA1 table end

    report = {'message_ticks': {}, 'boundaries': [
        '001FE480/001FE530/001CC170 glyph layout and 001FE070 drawing (draw worker; x/y asserted).',
        'Voice lane 001F9CF0 accepting a pushed voice (D_008106F5 2->1) is harness-driven after 3 ticks.',
        'D_00282155/156 stream busy bytes are harness inputs.',
        'Mode-3 presenter 001FD0E0 and mode-4 presenters 001FCB90/001FCF90/001FCF60 are workers.']}
    ticks = 0
    # Director op0C lines (voice cues 150/149) and a global bank line across
    # delays 0/1/30 and D_008106F5 modes 0/1/2.
    for line in (0x97, 0x99, 0x80000018):
        for delay in (0, 1, 30):
            for f5 in (0, 1, 2):
                n = message_case(elf, capture, scratch, native, line, delay, f5, 2)
                report['message_ticks'][f'{line:#x}/d{delay}/f5={f5}'] = n
                ticks += n
    # Multi-voice chain 0x7F (001FD6A0 hit path), voiced slot-1 message
    # 0x13 (cue 1, 001FD6A0 scan miss), slot-1 line 0x9B, stream-table claim
    # (001FD580 return 2), terminal-only line 0x98, busy gates. All real
    # area-11 records (indices below the table's 162).
    for line, busy, game_mode in ((0x7F, None, 2), (0x13, None, 2), (0x9B, None, 1),
                                  (0x66, None, 2), (0x97, None, 1),
                                  (0x98, None, 2),
                                  (0x97, (100, 140, 1, 0), 2), (0x99, (150, 260, 0, -1), 2)):
        tag = 'mode1-0x97' if (line, game_mode) == (0x97, 1) else line
        n = message_case(elf, capture, scratch, native, line, 0, 0, game_mode, busy, tag=tag)
        report['message_ticks'][f'{line:#x}/mode{game_mode}/busy={busy}'] = n
        ticks += n
    # SYNTHETIC table (not game data): the area tables only use +5 in {0, 1},
    # so a +5 == 2 record pins 001FD6A0's exact '== 1' test on its own record
    # (the scan past it stops on any nonzero +5).
    n = message_case(elf, capture, scratch, native, 0x97, 0, 0, 2,
                     patch={0x97: (40, 150, 0, 2), 0x98: (0, 151, 0xFF, 1)}, tag='synthetic')
    report['message_ticks']['synthetic-wait2'] = n
    ticks += n
    # SYNTHETIC table (not game data): area 11 has no voiced zero-duration
    # record inside a message (its only (0,0,0,0) record is the 0xA1 table
    # end). Record 0x98 becomes (0, 151, none, 0): 001FD6A0 pushes it as
    # the next voice while 001FD790 skips it as a 0/0 record.
    n = message_case(elf, capture, scratch, native, 0x97, 0, 0, 2,
                     patch={0x98: (0, 151, 0xFF, 0)}, tag='synthetic-zero')
    report['message_ticks']['synthetic-zero-duration'] = n
    ticks += n
    # Real data: the last area-11 record, then the walk past the table.
    report['table_end_fault_ticks'] = {
        f'f5={f5}': table_end_case(elf, capture, scratch, native, f5) for f5 in (0, 1, 2)}
    resumed = {}
    for name, limit in (('opening', 3000), ('roger-encounter', 3000), ('panel', 30)):
        resumed[name] = captured_resume(elf, name, native, limit)
    # The captured text messages complete; the captured mode-4 panel help
    # line has no completion inside the service (its caller ends it).
    assert resumed['opening'][1] and resumed['roger-encounter'][1] and not resumed['panel'][1]
    coverage(report)
    report.update(status='PASS', total_message_ticks=ticks,
                  captured_resume_ticks={k: v[0] for k, v in resumed.items()},
                  mode_dispatch_ticks=mode_cases(elf, capture, scratch, native),
                  op0c_cases=op0c_cases(elf, capture, scratch, native),
                  stream_request_cases=stream_cases(elf, capture, scratch, native),
                  voice_ring_cases=ring_cases(elf, capture, scratch, lib))
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'message_ticks'}, indent=2))


if __name__ == '__main__':
    main()
