#!/usr/bin/env python3
"""Original 00209DF0 normal-status hub stream versus the native adapter.

The original drawer executes from the local ELF over the preserved status-hub
EE RAM, with its health/battery/ammunition workers, number formatter and
208750 markers executed too. Every 00207D00/207E40/207F80/2082B0/1CBA50/
1CC1E0 call is compared in order. The trail is checked against executed
001B62C0/0020AC70/0020E020 on the hub base, help lines against 001FCB90, and
the UI+20 clock ownership against 0020E060, 001AF690 and 0020AE40. The
rendered stream is compared with executed 002082B0 arcs and the original
records. Boundaries: SDK transcendental values (host libm on both sides),
byte string copy/append/length/memset workers, glyph metrics and pixels.
"""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import struct
import subprocess
import sys

from export_status_hub import Original, Presenter, ELF_SHA, UI
from test_item_geometry_reference import original_arc
from test_item_trail_reference import (Original as TrailOriginal, Stick, Trail, Math, Unary, Atan,
                                       HOST, BASE, CONTEXT, PACKET)
from test_point_light_reference import bits, number

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'assets/scene_snow/panel'
KINDS = {1: 'blend', 2: 'sprite', 3: 'rectangle', 4: 'arc', 5: 'marker', 6: 'trail', 7: 'text'}
STYLES = {(0, 12, 12): 1, (0, 16, 16): 3, (0, 12, 16): 7, (0, 10, 10): 8, (1, 10, 20): 5}


class Ammo(C.Structure):
    _fields_ = [('primary', C.c_uint8), ('secondary', C.c_uint8), ('amount', C.c_int16 * 5),
                ('reserve', C.c_int16)]


class Display(C.Structure):
    _fields_ = [('health', C.c_float), ('infection', C.c_float), ('warning', C.c_uint8),
                ('battery_equipped', C.c_uint8), ('charge', C.c_uint16), ('capacity', C.c_uint8),
                ('hover', C.c_uint8), ('ammo', Ammo)]


class Command(C.Structure):
    _fields_ = [('kind', C.c_int), ('mode', C.c_uint), ('xy', C.c_int32 * 4), ('rgba', C.c_uint32),
                ('tex0', C.c_uint64), ('proportional', C.c_int), ('null_style', C.c_int),
                ('arc', C.POINTER(C.c_float)), ('marker', C.POINTER(C.c_uint32)),
                ('trail_xy', C.c_float * 2), ('trail', C.POINTER(C.c_int32)),
                ('trail_count', C.c_uint), ('text', C.c_char_p)]


BASE_CASE = dict(hover=0, infection=0.0, clock=0, health=100.0, warning=0, equipped=1, charge=48,
                 capacity=48, primary=0, secondary=1, amounts=(12, 0, 0, 0, 0), reserve=60)


def library():
    out = ROOT / 'build/status_hub_ui_reference'
    out.mkdir(parents=True, exist_ok=True)
    path = out / ('hub_ui.dylib' if sys.platform == 'darwin' else 'hub_ui.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-fPIC', '-dynamiclib' if sys.platform == 'darwin' else '-shared',
                    '-DHUB_UI_LIBRARY', '-Isrc', 'tests/status_hub_ui_test.c',
                    'src/game/em_status_hub_ui.c', 'src/game/em_status_draw.c',
                    'src/game/em_item_trail.c', 'src/game/em_item_geometry.c',
                    'src/game/em_item_sdk_math.c', 'src/game/em_interaction_scan.c', '-lm',
                    '-o', str(path)], cwd=ROOT, check=True)
    native = C.CDLL(str(path))
    native.em_status_hub_ui_load.restype = C.c_void_p
    native.em_status_hub_ui_load.argtypes = [C.c_char_p, C.c_char_p, C.POINTER(Math)]
    native.em_status_hub_ui_free.argtypes = [C.c_void_p]
    native.em_status_hub_ui_prepare.argtypes = [C.c_void_p, C.POINTER(Display), C.POINTER(Stick),
                                                C.POINTER(C.c_uint32), C.POINTER(Trail)]
    native.em_status_hub_ui_render.argtypes = [C.c_void_p, C.c_void_p, C.c_int]
    native.em_status_hub_ui_command_count.argtypes = [C.c_void_p]
    native.em_status_hub_ui_command_count.restype = C.c_uint
    native.em_status_hub_ui_command.argtypes = [C.c_void_p, C.c_uint, C.POINTER(Command)]
    native.em_status_hub_ui_help_count.argtypes = [C.c_void_p, C.c_uint]
    native.em_status_hub_ui_help_count.restype = C.c_uint
    native.em_status_hub_ui_help.argtypes = [C.c_void_p, C.c_uint, C.c_uint, C.POINTER(Command)]
    native.em_item_trail_reset.argtypes = [C.POINTER(Trail)]
    native.hub_test_reset.argtypes = [C.c_int] * 3
    native.hub_test_log.restype = C.c_char_p
    return native, out


def original_canonical(command):
    kind, mode = command['kind'], command['mode']
    if kind == 'blend':
        return ('blend', mode)
    if kind == 'sprite':
        return ('sprite', mode, *command['xywh'], command['rgba'], command['tex0'])
    if kind == 'rectangle':
        return ('rectangle', mode, *command['xyxy'], command['rgba'])
    if kind == 'arc':
        return ('arc', mode, struct.pack('<24f', *command['descriptor']))
    if kind == 'line_strips':
        return ('marker', mode, tuple(v for strip in command['strips'] for vertex in strip for v in vertex))
    if kind == 'analog_trail':
        # 0020AC70 itself selects mode1 before drawing its fans.
        return ('trail', 1, *command['xy'])
    if kind == 'text':
        return ('text', mode, int(command['proportional']), *command['xywh'],
                command['value'].encode('latin1'), command['style'][0] | command['style'][1] << 32,
                int(command['null_style']))
    raise AssertionError(kind)


def native_canonical(command):
    kind = KINDS[command.kind]
    xy = [value & 0xffffffff for value in command.xy]
    if kind == 'blend':
        return ('blend', command.mode)
    if kind == 'sprite':
        return ('sprite', command.mode, *xy, command.rgba, command.tex0)
    if kind == 'rectangle':
        return ('rectangle', command.mode, *xy, command.rgba)
    if kind == 'arc':
        return ('arc', command.mode, C.string_at(command.arc, 96))
    if kind == 'marker':
        return ('marker', command.mode, tuple(command.marker[i] for i in range(9 * 16 * 6)))
    if kind == 'trail':
        return ('trail', command.mode, command.trail_xy[0], command.trail_xy[1])
    return ('text', command.mode, command.proportional, *xy, command.text, command.tex0,
            command.null_style)


def original_stream(elf, ram, case):
    o = Original(elf, ram, case['hover'], case['infection'], True, True)
    o.save(UI + 0x20, case['clock'])
    o.save(0x810858, bits(case['health']))
    o.save(0x8104e4, case['warning'], 1)
    o.save(0x810c7f, case['equipped'], 1)
    o.save(0x810cb2, case['charge'], 2)
    o.save(0x810cb7, case['capacity'], 1)
    o.save(0x810ca4, case['primary'], 1)
    o.save(0x810ca6, case['secondary'], 1)
    o.write(0x810ca8, struct.pack('<5h', *case['amounts']))
    o.save(0x810cb4, case['reserve'] & 0xffff, 2)
    o.run(0x209df0, (UI,))
    return o.load(UI + 0x20), o.commands


def display(case):
    return Display(case['health'], case['infection'], case['warning'], case['equipped'],
                   case['charge'], case['capacity'], case['hover'],
                   Ammo(case['primary'], case['secondary'], (C.c_int16 * 5)(*case['amounts']),
                        case['reserve']))


def native_stream(native, ui):
    commands = []
    for index in range(native.em_status_hub_ui_command_count(ui)):
        command = Command()
        assert native.em_status_hub_ui_command(ui, index, C.byref(command))
        commands.append(command)
    return commands


def cases():
    yield from (dict(BASE_CASE, hover=h, infection=i) for h, i in itertools.product(
        range(5), (0.0, 0.5, 9.99, 35.5, 99.99, 100.0)))
    # Every 209DF0 formatter value, including truncation just below 100.
    yield from (dict(BASE_CASE, infection=n + (0.75 if n % 2 else 0.0)) for n in range(1, 100))
    for health, warning in itertools.product((0.0, 35.0, 35.01, 60.0, 60.5, 100.0), (0, 1)):
        yield dict(BASE_CASE, hover=1, health=health, warning=warning)
    for clock in (59, 60, 0x7fffffff, 0xffffffff):
        yield dict(BASE_CASE, hover=3, health=50.0, clock=clock)
    yield dict(BASE_CASE, equipped=0)
    for charge, capacity in itertools.product((0, 1, 13, 47, 255), (12, 198)):
        yield dict(BASE_CASE, hover=4, charge=charge, capacity=capacity)
    for primary, secondary, amounts, reserve in (
            (0, 0, (0, 0, 0, 0, 0), 0), (0, 1, (99, 0, 0, 0, 0), 9999), (0, 2, (0, 7, 0, 0, 0), 5),
            (0, 3, (0, 120, 0, 0, 0), 60), (0, 4, (0, 0, 99, 99, 0), 60),
            (0, 4, (0, 0, 1, 5, 0), 60), (0, 4, (0, 0, 0, 0, 0), 60),
            (2, 0, (0, 0, 0, 0, 30), 60), (2, 7, (0, 0, 0, 0, 999), 60),
            (1, 4, (0, 0, 12, 34, 0), 1), (255, 3, (0, 45, 0, 0, 0), 60)):
        yield dict(BASE_CASE, hover=2, infection=35.5, primary=primary, secondary=secondary,
                   amounts=amounts, reserve=reserve)


def hub_trail_tick(o, x, y):
    """Original 20AC70 at the 209DF0 scratch base (432,272), then its packets."""
    o.axes(x, y)
    o.write(BASE, struct.pack('<2f', 432, 272))
    o.save(0x275670, CONTEXT)
    o.save(CONTEXT + 0x14, PACKET)
    o.run(0x20AC70, (0, BASE, 0))
    assert o.load(CONTEXT + 0x14) == PACKET + 16 * 0x870
    triangles = []
    for slot in range(16):
        packet = PACKET + slot * 0x870
        assert (o.load(packet + 0x20, 8) >> 47 & 0x7FF) == 0x4C
        previous = None
        for vertex in range(33):
            record = packet + 0x30 + vertex * 0x40
            center = [o.load(record + 0x10), o.load(record + 0x14)]
            outer = [o.load(record + 0x30), o.load(record + 0x34)]
            if previous is not None:
                triangles.append(tuple(center + previous + outer) + (o.load(record),))
            previous = outer
    return o.read(0x821300, 256) + o.read(0x275C90, 4), triangles


def expected_render(elf, commands, triangles, help_calls, atlas, white):
    """Renderer calls from the original records: sprites/rectangles/text use the
    documented GS-to-canvas mapping; arcs execute original 002082B0."""
    def canvas(x, y):
        return x / 16 - 1792, (y / 16 - 1936) * 2

    def colour(rgba, textured):
        return [(rgba >> shift & 255) / (128 if textured else 255) for shift in (0, 8, 16)] + [(rgba >> 24) / 128]

    u, v = white[0] + .5, white[1] + .5
    lines = [('C', 512, 448)]
    for command in commands:
        kind, mode = command['kind'], command['mode']
        if kind == 'sprite':
            x, y = canvas(*command['xywh'][:2])
            su, sv, sw, sh = atlas[command['tex0']]
            lines.append(('Q', mode, x, y, *command['xywh'][2:], su, sv, su + sw, sv + sh,
                          *colour(command['rgba'], True)))
        elif kind == 'rectangle':
            x0, y0, x1, y1 = command['xyxy']
            x, y = canvas(x0, y0)
            lines.append(('Q', mode, x, y, (x1 - x0) / 16, (y1 - y0) / 8, u, v, u, v,
                          *colour(command['rgba'], False)))
        elif kind == 'arc':
            vertices = original_arc(elf, command['descriptor'])
            for a, b, c in zip(vertices, vertices[1:], vertices[2:]):
                points = [p for rgba, x, y in (a, b, c) for p in canvas(x, y)]
                colours = [q for rgba, _, _ in (a, b, c) for q in colour(rgba, False)]
                lines.append(('T', mode, *points, *colours, u, v))
        elif kind == 'line_strips':
            for strip in command['strips']:
                for a, b in zip(strip, strip[1:]):
                    lines.append(('L', mode, a, b))
        elif kind == 'analog_trail':
            assert command['xy'] == [432.0, 272.0]
            for triangle in triangles:
                points = [p for i in range(3) for p in canvas(triangle[2 * i], triangle[2 * i + 1])]
                k = triangle[6] / 255
                lines.append(('T', 1, *points, k, k, k, 0, 0, 0, 0, 0, 0, 0, 0, 0, u, v))
        elif kind == 'text':
            lines.append(text_line(command))
    lines += [text_line(command) for command in help_calls]
    lines.append(('C', 640, 448))
    return lines


def text_line(command):
    x, y, w, h = command['xywh']
    style = STYLES[(int(command['proportional']), w, h)]
    rgb = 0x808080 if command['null_style'] else command['style'][0] & 0xffffff
    return ('X', style, x - 1792, (y - 1936) * 2, rgb, command['value'])


def compare_render(log, expected):
    actual = [line for line in log.split('\n') if line]
    position = 0
    for want in expected:
        if want[0] == 'L':
            # One-GS-pixel parallelogram per original segment: its first
            # edge is the exact original endpoint pair and colours.
            _, mode, a, b = want
            for half in range(2):
                fields = actual[position].split()
                position += 1
                assert fields[0] == 'T' and int(fields[1]) == mode, fields
                values = list(map(float, fields[2:]))
                ax, ay = a[4] / 16 - 1792, (a[5] / 16 - 1936) * 2
                bx, by = b[4] / 16 - 1792, (b[5] / 16 - 1936) * 2
                assert values[0:2] == [ax, ay], (values, a)
                corner = (bx, by) if half == 0 else None
                if corner:
                    assert values[2:4] == [bx, by]
                ca = [a[i] / (128 if i == 3 else 255) for i in range(4)]
                assert all(abs(p - q) < 1e-6 for p, q in zip(values[6:10], ca))
            continue
        fields = actual[position].split(' ', 5) if want[0] == 'X' else actual[position].split()
        position += 1
        assert fields[0] == want[0], (position, fields, want)
        if want[0] == 'X':
            assert int(fields[1]) == want[1] and float(fields[2]) == want[2] and \
                float(fields[3]) == want[3] and int(fields[4], 16) == want[4] and fields[5] == want[5], (fields, want)
        else:
            values = list(map(float, fields[1:]))
            assert len(values) == len(want) - 1, (fields, want)
            assert all(abs(p - q) <= 1e-6 * max(1, abs(q)) for p, q in zip(values, want[1:])), (position, fields, want)
    assert position == len(actual), actual[position:position + 3]
    return position


def mutated_loads(native, math, out):
    records = (ASSETS / 'status_hub.emhs').read_bytes()
    atlas = (ASSETS / 'status_hub_atlas.emha').read_bytes()
    cases = []
    for cut in (0, 4, 23, 24, 100, 407, 408, 500, len(records) // 3, len(records) // 2, len(records) - 1):
        cases.append((records[:cut], atlas))
    cases.append((b'EMHX' + records[4:], atlas))
    cases.append((records[:4] + struct.pack('<I', 1) + records[8:], atlas))  # the old WIP layout
    cases.append((records[:12] + struct.pack('<I', 9) + records[16:], atlas))
    cases.append((records + b'\0', atlas))
    cases.append((records, atlas[:-1]))
    cases.append((records, atlas + b'\0'))
    count = struct.unpack_from('<I', atlas, 16)[0]
    tokens = [struct.unpack_from('<Q', atlas, 28 + i * 24)[0] for i in range(count)]
    for token in (0x20045325554221b2, 0x2004518555422196, 0x20045ec555422186):
        at = 28 + tokens.index(token) * 24
        cases.append((records, atlas[:at] + struct.pack('<Q', token ^ 1) + atlas[at + 8:]))
    white = struct.unpack_from('<I', atlas, 20)[0]
    wx, wy = struct.unpack_from('<2I', atlas, 28 + white * 24 + 8)
    texel = 28 + count * 24 + (wy * 1024 + wx) * 4
    cases.append((records, atlas[:texel] + b'\0\0\0\0' + atlas[texel + 4:]))
    rejected = 0
    for index, (r, a) in enumerate(cases):
        rp, ap = out / f'bad{index}.emhs', out / f'bad{index}.emha'
        rp.write_bytes(r)
        ap.write_bytes(a)
        assert not native.em_status_hub_ui_load(str(rp).encode(), str(ap).encode(), C.byref(math)), index
        rejected += 1
    return rejected


def main():
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    ram = (ROOT.parent / 'Extermination/build/startup-reference/status-hub/eeMemory.bin').read_bytes()
    native, out = library()
    math = Math(None, Unary(lambda _, x: HOST.sinf(x)), Unary(lambda _, x: HOST.cosf(x)),
                Atan(lambda _, y, x: HOST.atan2f(y, x)), Unary(lambda _, x: HOST.sqrtf(x)))
    records, atlas_path = str(ASSETS / 'status_hub.emhs').encode(), str(ASSETS / 'status_hub_atlas.emha').encode()

    def load():
        ui = native.em_status_hub_ui_load(records, atlas_path, C.byref(math))
        assert ui
        return ui

    # 1. Ordered 209DF0 streams across hover, infection and equipment.
    checked = streams = 0
    for case in cases():
        want_clock, wanted = original_stream(elf, ram, case)
        ui = load()
        clock, trail, stick = C.c_uint32(case['clock']), Trail(), Stick()
        assert native.em_status_hub_ui_prepare(ui, C.byref(display(case)), C.byref(stick),
                                               C.byref(clock), C.byref(trail)) == 1, case
        got = [native_canonical(c) for c in native_stream(native, ui)]
        want = [original_canonical(c) for c in wanted]
        assert clock.value == want_clock, (case, clock.value, want_clock)
        assert got == want, (case, next((i, a, b) for i, (a, b) in enumerate(zip(got, want)) if a != b)
                             if len(got) == len(want) else (len(got), len(want)))
        native.em_status_hub_ui_free(ui)
        streams += 1
        checked += len(got)

    # 2. Unsupported inherited-TEX0 selectors are rejected before any draw.
    ui = load()
    bad = display(dict(BASE_CASE, secondary=5))
    clock = C.c_uint32()
    assert not native.em_status_hub_ui_prepare(ui, C.byref(bad), C.byref(Stick()), C.byref(clock), C.byref(Trail()))
    assert not native.em_status_hub_ui_prepare(ui, C.byref(display(BASE_CASE)), C.byref(Stick()),
                                               C.byref(clock), C.byref(Trail()))
    assert not native.em_status_hub_ui_command_count(ui)
    native.em_status_hub_ui_free(ui)

    # 3. Original help presenter lines.
    ui = load()
    presenter = Presenter(elf, ram)
    help_calls, help_checked = [], 0
    for line in range(10):
        wanted = presenter.help_line(line)
        help_calls.append(wanted)
        assert native.em_status_hub_ui_help_count(ui, line) == len(wanted)
        for index, want in enumerate(wanted):
            command = Command()
            assert native.em_status_hub_ui_help(ui, line, index, C.byref(command))
            assert native_canonical(command)[2:] == original_canonical(want)[2:], (line, index)
            help_checked += 1
    assert not native.em_status_hub_ui_help_count(ui, 10)
    native.em_status_hub_ui_free(ui)

    # 4. Shared trail through the adapter versus executed 1B62C0/20AC70/20E020,
    #    and one complete rendered frame versus the original records.
    atlas_bytes = (ASSETS / 'status_hub_atlas.emha').read_bytes()
    count, white_index = struct.unpack_from('<2I', atlas_bytes, 16)
    atlas = {}
    for i in range(count):
        token, x, y, w, h = struct.unpack_from('<Q4I', atlas_bytes, 28 + i * 24)
        atlas[token] = (x, y, w, h)
    white = atlas.pop(0)
    trail_original, trail, clock, ui = TrailOriginal(elf), Trail(), C.c_uint32(), load()
    frames = [(128, 128)] * 2 + [(0, 0), (255, 0), (255, 255), (0, 255)] * 3 + ['reset'] + \
             [(200, 60), (128, 128), (10, 250)] * 2
    trail_triangles = rendered = resets = 0
    case = dict(BASE_CASE, hover=2, infection=35.5)
    for frame in frames:
        if frame == 'reset':
            trail_original.run(0x20E020)
            native.em_item_trail_reset(C.byref(trail))
            assert bytes(trail) == trail_original.read(0x821300, 256) + trail_original.read(0x275C90, 4) == bytes(260)
            resets += 1
            continue
        stick = Stick.from_buffer_copy(trail_original.stick(*frame))
        state, triangles = hub_trail_tick(trail_original, *frame)
        assert native.em_status_hub_ui_prepare(ui, C.byref(display(case)), C.byref(stick),
                                               C.byref(clock), C.byref(trail)) == 1
        command = next(c for c in native_stream(native, ui) if KINDS[c.kind] == 'trail')
        got = [tuple(command.trail[i * 7 + j] & 0xffffffff for j in range(7)) for i in range(command.trail_count)]
        assert bytes(trail) == state and got == triangles, frame
        trail_triangles += len(got)
        if frame == (255, 255) and not rendered:
            case_clock = clock.value - 1
            _, wanted = original_stream(elf, ram, dict(case, clock=case_clock))
            wanted = [c for c in wanted if c['kind'] != 'blend']
            native.hub_test_reset(1, 1, -1)
            assert native.em_status_hub_ui_render(ui, C.c_void_p(1), 6) == 1
            log = native.hub_test_log().decode('latin1')
            assert log.startswith('U 1024 ') and log.split('\n')[1] == 'I'
            body = '\n'.join(log.split('\n')[2:])
            expected = expected_render(elf, wanted, triangles, help_calls[6], atlas, white)
            rendered += compare_render(body, expected)
    native.em_status_hub_ui_free(ui)

    # 5. UI+20 ownership: shared by 0020AE40 flag-8 pages, cleared by UI memsets.
    clock_checks = 0
    for flags, delta in ((8, 1), (2, 0), (1, 0)):
        o = Original(elf, ram, 0, 0, True)
        o.save(UI + 0x20, 41)
        o.run(0x20AE40, (UI, 0x265FF0, flags))
        assert o.load(UI + 0x20) == 41 + delta, flags
        clock_checks += 1
    for entry in (0x20E060, 0x1AF690):
        o = Presenter(elf, ram)
        o.save(UI + 0x20, 41)
        o.run(entry)
        assert o.read(UI, 0xA0) == bytes(0xA0), hex(entry)
        clock_checks += 1

    rejected = mutated_loads(native, math, out)
    fixture = out / 'status_hub_ui_test'
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-fsanitize=address,undefined', '-Isrc', 'tests/status_hub_ui_test.c',
                    'src/game/em_status_hub_ui.c', 'src/game/em_status_draw.c',
                    'src/game/em_item_trail.c', 'src/game/em_item_geometry.c',
                    'src/game/em_item_sdk_math.c', 'src/game/em_interaction_scan.c', '-lm',
                    '-o', str(fixture)], cwd=ROOT, check=True)
    result = subprocess.run([str(fixture), records, atlas_path], cwd=ROOT, check=True,
                            capture_output=True, text=True)
    assert result.stdout.strip() == 'PASS', result.stdout + result.stderr
    report = {'original_209DF0_streams': streams, 'exact_ordered_commands': checked,
              'original_help_calls': help_checked, 'hub_trail_frames': len(frames) - resets,
              'original_trail_triangles': trail_triangles, 'original_20E020_resets': resets,
              'rendered_calls_vs_original': rendered, 'ui_clock_ownership_checks': clock_checks,
              'rejected_malformed_resources': rejected, 'asan_ubsan_lifecycle': 'PASS',
              'boundaries': 'SDK transcendental values; string copy/append/length/memset '
                            'workers; glyph metrics, line coverage and final GS/Metal pixels'}
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
