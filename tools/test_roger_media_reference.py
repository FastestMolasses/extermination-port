#!/usr/bin/env python3
"""Validate Roger media resources and original stream/message callbacks.

The original game's instructions execute in the project's small test oracle.
Device I/O, glyph layout/drawing, RNG output and player-face calls are explicit
boundaries. No original instructions, dialogue text or audio are embedded here.
"""
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import wave

from export_opening_media import load_tool
from export_roger_media import DECOMP, ROOT, TEXT_OFFSET, message_records
from test_door_original_reference import DoorOracle
from test_interaction_scan_reference import ELF_SHA


class MediaOracle(DoorOracle):
    def plain(self, word):
        if word >> 26 == 0:
            function = word & 63
            if function == 62:
                self.r[word >> 11 & 31] = (self.r[word >> 16 & 31] & 0xFFFFFFFFFFFFFFFF) >> ((word >> 6 & 31) + 32)
                return
            if function == 39:
                self.r[word >> 11 & 31] = ~(self.r[word >> 21 & 31] | self.r[word >> 16 & 31]) & 0xFFFFFFFF
                return
        super().plain(word)


class Line(C.Structure):
    _fields_ = [('line', C.c_uint16), ('duration', C.c_uint16), ('voice', C.c_int16),
                ('speaker', C.c_uint8), ('terminal', C.c_uint8), ('skew', C.c_uint8),
                ('text', C.c_char_p)]


class Dialogue(C.Structure):
    _fields_ = [('lines', C.POINTER(Line)), ('count', C.c_uint), ('next', C.c_uint),
                ('displayed', C.c_uint), ('remaining', C.c_uint), ('active', C.c_int),
                ('loaded', C.c_int)]


def message_oracle(elf):
    original = MediaOracle(elf)
    original.save(0x810700, 11, 1)
    original.save(0x70003B8F, 2, 1)
    original.save(0x2821B0, 2)
    original.save(0x2821B4, 1)
    original.save(0x8106F4, 1, 1)
    original.draw = -1
    original.events = []

    def draw(o):
        assert o.r[4] == o.load(0x28A594)
        assert o.r[6:8] == [251, 194]
        o.draw = o.r[5]

    def clear(o):
        assert o.r[4:7] == [0x2821B0, 0, 0x9C]
        for address in range(0x2821B0, 0x28224C):
            o.save(address, 0, 1)

    original.calls.update({
        0x1FE480: lambda o: o.r.__setitem__(2, 0x950000),
        0x1FE530: lambda o: o.r.__setitem__(2, 0x950000),
        0x1CC170: lambda o: o.r.__setitem__(2, 10),
        0x1FE070: draw,
        0x1D06E0: lambda o: original.events.append(('player_talk', o.r[4], o.r[5])),
        0x1FAAC0: lambda o: original.events.append(('stop_lane', o.r[4])),
        0x121A28: clear,
    })
    return original


def resource_check(elf, records, report):
    path = ROOT / 'assets/scene_snow/roger/encounter.emod'
    blob = path.read_bytes()
    header = struct.unpack_from('<4s7I', blob)
    assert header[:3] == (b'EMOD', 1, len(records))
    # 001FD950 draws at GS y 0xC2 (checked in dialogue_check); the EMOD y is
    # the port's full-height canvas row, two rows per half-height GS unit.
    assert header[6] == 2 * 0xC2 == report['y']
    assert header[3:6] == (report['line_height'], report['fill'], report['outline'])
    strings = 32 + len(records) * 20
    for index, record in enumerate(records):
        packed = struct.unpack_from('<HHhBBIIB3x', blob, 32 + index * 20)
        line, duration, voice, speaker, terminal, offset, length, skew = packed
        assert (line, duration, voice, speaker, terminal, skew) == tuple(
            record[key] for key in ('line', 'duration', 'voice', 'speaker', 'terminal', 'skew'))
        assert blob[strings + offset:strings + offset + length + 1] == record['text'] + b'\0'
    for stream in report['streams']:
        with wave.open(str(path.with_name(stream['name'] + '.wav')), 'rb') as wav:
            assert (wav.getnchannels(), wav.getframerate(), wav.getsampwidth(), wav.getnframes()) == (
                2, 48000, 2, stream['pcm_frames'])
            assert hashlib.sha256(wav.readframes(wav.getnframes())).hexdigest() == stream['pcm_sha256']

    capture = (DECOMP / 'build/startup-reference/roger-encounter/eeMemory.bin').read_bytes()
    source = (DECOMP / 'extract/chunk15/f12_id44.bin').read_bytes()
    pointer = struct.unpack_from('<I', capture, 0x28A594)[0]
    text_offset, _, records_size, _ = struct.unpack_from('<4I', source, TEXT_OFFSET)
    string_table = TEXT_OFFSET + text_offset + records_size
    strings_offset, _, byte_count, _ = struct.unpack_from('<4I', source, string_table)
    size = string_table - TEXT_OFFSET + strings_offset + byte_count
    assert source[TEXT_OFFSET:TEXT_OFFSET + size] == capture[pointer:pointer + size]
    assert struct.unpack_from('<I', capture, 0x282178)[0] == report['streams'][0]['cue'] == 29
    assert capture[0x8106F4:0x8106F6] == b'\0\0'
    assert struct.unpack_from('<I', capture, 0x8106C8)[0] == 0x20081910
    assert struct.unpack_from('<I', capture, 0x810D38)[0] == 0 and capture[0x8104E4] == 0
    return capture, size


def dialogue_check(elf, records, capture, build):
    output = build / 'dialogue.dylib'
    names = ('em_opening_dialogue_start', 'em_opening_dialogue_tick', 'em_opening_dialogue_talk_mask')
    subprocess.run(['cc', '-dynamiclib', '-Wl,-undefined,dynamic_lookup', '-Wl,-dead_strip',
                    *[f'-Wl,-exported_symbol,_{name}' for name in names], '-O2', '-Isrc',
                    'src/game/em_opening_media.c', '-o', str(output)], cwd=ROOT, check=True)
    native = C.CDLL(str(output))
    native.em_opening_dialogue_start.argtypes = [C.POINTER(Dialogue), C.POINTER(Line), C.c_uint]
    native.em_opening_dialogue_tick.argtypes = [C.POINTER(Dialogue)]
    native.em_opening_dialogue_talk_mask.argtypes = [C.POINTER(Dialogue)]
    lines = (Line * len(records))(*[Line(**record) for record in records])
    callbacks = sum(record['duration'] + 1 for record in records)
    comparisons = 0
    capture_fields = (0x34, 0x50, 0x51, 0x5C, 0x60, 0x64, 0x68, 0x6C, 0x70)
    for delay in (0, 1, 30):
        dialogue = Dialogue()
        native.em_opening_dialogue_start(C.byref(dialogue), lines, len(lines))
        original = message_oracle(elf)
        original.save(0x2821BC, delay)
        player_events = []
        for tick in range(delay + callbacks):
            original.draw = -1
            original.events.clear()
            original.run(0x1FCA10)
            if tick < delay:
                assert original.draw == -1
                continue
            native.em_opening_dialogue_tick(C.byref(dialogue))
            assert original.draw == dialogue.displayed
            # D_002821B0 +0x60 record, +0x6C timer, +0x5C loaded state.
            assert original.load(0x282210) == dialogue.next
            assert original.load(0x28221C) == dialogue.remaining
            assert original.load(0x28220C) == dialogue.loaded
            assert original.load(0x282214) == native.em_opening_dialogue_talk_mask(C.byref(dialogue))
            assert original.load(0x8106F4, 1) == 0
            player_events.extend(original.events)
            if delay == 0 and tick == 51:
                for offset in capture_fields:
                    width = 1 if offset in (0x50, 0x51) else 4
                    assert original.load(0x2821B0 + offset, width) == int.from_bytes(
                        capture[0x2821B0 + offset:0x2821B0 + offset + width], 'little'), hex(offset)
            comparisons += 1
        assert original.load(0x2821B4) == 2 and not dialogue.active
        assert [event[2] for event in player_events] == [1, 0, 1, 0]
        original.events.clear()
        original.run(0x1FCA10)
        assert original.load(0x2821B0) == original.load(0x2821B4) == 0
        assert original.events == [('stop_lane', 1), ('stop_lane', 2)]
    return comparisons


def handshake_check(elf):
    count = 0
    for key, expected in ((0, 29), (25, 54), (102, 63), (9999, None)):
        original = MediaOracle(elf)
        original.save(0x810700, 11, 1)
        events = []
        original.calls.update({
            0x1FD470: lambda o: events.append(('stop', o.r[4] & 0xFFFFFFFF)),
            0x1FA790: lambda o: events.append(('stream', o.r[4], o.r[5])),
        })
        original.run(0x1FD4C0, (key,))
        assert events == ([] if expected is None else [('stop', 0xFFFFFFFF), ('stream', 0, expected)])
        assert original.r[2] == int(expected is not None)
        assert original.load(0x8106F4, 1) == (0 if expected is None else 2)
        count += 1

    overlay = (DECOMP / 'extract/OVERLAY/AREA11.BIN').read_bytes()
    for phase in range(4):
        for ready in (0, 1, 2):
            original = MediaOracle(elf)
            state, command = 0x940000, 0x950000
            for index, value in enumerate(overlay[0x828410 - 0x823500:0x828450 - 0x823500]):
                original.save(command + index, value, 1)
            original.save(state + 4, phase, 1)
            original.save(0x810700, 11, 1)
            original.save(0x70003B8F, 2, 1)
            original.save(0x28A9A0, 2, 2)
            original.save(0x8106F4, ready, 1)
            events = []
            def worker(name, arguments):
                return lambda o: events.append((name, tuple(o.r[4:4 + arguments])))
            for address, name, arguments in ((0x1BA510, 'activity_clear', 0),
                    (0x1AEDE0, 'fade_out', 2), (0x1FD470, 'stop', 1),
                    (0x1FA790, 'stream', 2), (0x119828, 'volume', 3),
                    (0x1AEB60, 'bars', 1), (0x1B81D0, 'take_player', 1),
                    (0x1D2610, 'zoom', 0), (0x1AEE10, 'fade_in', 2)):
                original.calls[address] = worker(name, arguments)
            original.run(0x1B82D0, (0x910000, state, command))
            if phase == 0:
                assert events[1] == ('fade_out', (4, 0))
                assert events[3:] == [('stream', (0, 29)), ('volume', (0, 0, 0)), ('volume', (1, 0, 0))]
            elif phase == 1:
                assert events == [('bars', (255,))]
            elif phase == 2:
                assert events == [('take_player', (0x8102B0,)), ('zoom', ())]
            else:
                assert events == ([('fade_in', (16, 0))] if ready == 1 else [])
                assert original.r[2] == int(ready == 1)
                assert original.load(state + 12, 1) == int(ready == 1)
            count += 1

    for random_result in (0, 1, 0x7F0000, 0x12345678, 0x7FFFFFFF):
        original = MediaOracle(elf)
        original.save(0x8106C8, 0x20081910)
        original.save(0x810700, 11, 1)
        original.save(0x282154, 2, 1)
        original.save(0x282178, 29)
        events = []
        original.calls.update({
            0x1FC280: lambda o: None,
            0x122BB8: lambda o: o.r.__setitem__(2, random_result),
            0x1FAAC0: lambda o: events.append(('stop', o.r[4])),
            0x1FABF0: lambda o: events.append(('play', tuple(o.r[4:8]))),
        })
        original.run(0x1FAE70, (0,))
        assert events == [('stop', 0), ('play', (0, 25, 270 + ((random_result >> 16) & 127), 1))]
        count += 1
    return count


def main():
    elf_bytes = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf_bytes).hexdigest() == ELF_SHA
    audio = load_tool(DECOMP / 'tools/audio_export.py', '_roger_test_audio')
    elf = audio.ElfImage(DECOMP / 'config/SCUS_971.12')
    _, records = message_records(elf, (DECOMP / 'extract/chunk15/f12_id44.bin').read_bytes())
    report = json.loads((ROOT / 'assets/scene_snow/roger/media.json').read_text())
    capture, size = resource_check(elf, records, report)
    build = ROOT / 'build/roger_media'
    build.mkdir(parents=True, exist_ok=True)
    callbacks = dialogue_check(elf_bytes, records, capture, build)
    handshakes = handshake_check(elf_bytes)
    result = dict(status='PASS', original_message_callbacks=callbacks,
                  original_handshake_cases=handshakes, lines=len(records),
                  capture_exact_text_bytes=size, capture_exact_message_fields=9,
                  capture_message_tick=52, source_music_cue=29,
                  boundaries=['Glyph/device I/O and RNG values are explicit call boundaries.',
                              'Native comparison uses the existing pure dialogue clock, not a new live media owner.'])
    (build / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
