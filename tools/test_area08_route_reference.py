#!/usr/bin/env python3
"""AREA08's and AREA07 sub 0's overlay owners against the original instructions.

em_area08_route runs beside the ORIGINAL overlay code in the Python EE: every
call (arguments and order), every ordered access, the whole memory, poisoned
starts and the fail-stop contract, over the fork captures of the sixteenth
level. Uses test_level14_port_reference's comparator unchanged. The default run
covers every reachable word; EM_TEST_FULL=1 sweeps every failure site.
No original bytes are exported.
"""
import ctypes as C
import os
import re
import struct
from pathlib import Path
import test_level14_port_reference as O
ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
SET = DECOMP / 'build/fork_refs/levels-recordings-20261030-1/s87'
U, I, F, S = O.U, O.I, O.F, O.S
FUNCS = {
    0x008235E0: ('em_area08_route_008235E0', 0x4C, [U], I),
    0x00823630: ('em_area08_route_00823630', 0x178, [U, U], I),
    0x008237B0: ('em_area08_route_008237B0', 0xBC, [U], None),
    0x00823870: ('em_area08_route_00823870', 0x250, [U], None),
    0x00823AC0: ('em_area08_route_00823AC0', 0x730, [U], None),
    0x00824210: ('em_area08_route_00824210', 0x244, [U], None),
    0x00824460: ('em_area08_route_00824460', 0x2C, [], I),
    0x00824490: ('em_area08_route_00824490', 0x144, [U], None),
    0x008245E0: ('em_area08_route_008245E0', 0x1A4, [U], None),
    0x00824790: ('em_area08_route_00824790', 0xDC, [U], None),
    0x00824870: ('em_area08_route_00824870', 0x17C, [U], None),
    0x008249F0: ('em_area08_route_008249F0', 0x1C4, [U], None),
    0x00824BC0: ('em_area08_route_00824BC0', 0x4FC, [U, S], None),
}
# AREA07's address space is another overlay: its own pass of the same harness
FUNCS07 = {0x00823DA0: ('em_area08_route_a07_00823DA0', 0x1D8, [U], None)}
OVERLAY_FILES = {8: DECOMP/'extract/OVERLAY/AREA08.BIN', 7: DECOMP/'extract/OVERLAY/AREA07.BIN'}
HOOKS = [
    ('w_00102760', 0x00102760, [('u', 4), ('u', 5)], None),
    ('w_001028B8', 0x001028B8, [('u', 4), ('u', 5), ('u', 6)], None),
    ('w_001028D0', 0x001028D0, [('u', 4), ('u', 5), ('u', 6)], None),
    ('w_001028E8', 0x001028E8, [('u', 4), ('u', 5), ('u', 6)], None),
    ('w_00102918', 0x00102918, [('u', 4), ('u', 5), ('u', 6)], None),
    ('w_001029C0', 0x001029C0, [('u', 4)], None),
    ('w_00102B08', 0x00102B08, [('u', 4), ('u', 5), ('f', 12)], None),
    ('w_00102BB0', 0x00102BB0, [('u', 4), ('u', 5), ('f', 12)], None),
    ('w_0011A070', 0x0011A070, [('i', 4)], None),
    ('w_0011DE90', 0x0011DE90, [('f', 12)], 'f'),
    ('w_0011DF78', 0x0011DF78, [('f', 12)], 'f'),
    ('w_0011E2A8', 0x0011E2A8, [('f', 12)], 'f'),
    ('w_0011E748', 0x0011E748, [('f', 12)], 'f'),
    ('w_00122BB8', 0x00122BB8, [], 'i'),
    ('w_00182F90', 0x00182F90, [('u', 4), ('u', 5)], None),
    ('w_001AFC10', 0x001AFC10, [('u', 4)], None),
    ('w_001B0FD0', 0x001B0FD0, [('u', 4)], 'i'),
    ('w_001B10B0', 0x001B10B0, [('u', 4), ('i', 5), ('i', 6)], None),
    ('w_001B1240', 0x001B1240, [('u', 4), ('f', 12), ('f', 13)], 'f'),
    ('w_001B1470', 0x001B1470, [('f', 12)], 'f'),
    ('w_001B17A0', 0x001B17A0, [('u', 4)], None),
    ('w_001B1B70', 0x001B1B70, [('u', 4)], None),
    ('w_001B1DE0', 0x001B1DE0, [('u', 4)], None),
    ('w_001B2140', 0x001B2140, [('u', 4)], 'i'),
    ('w_001B6660', 0x001B6660, [('u', 4)], 'u'),
    ('w_001B6F00', 0x001B6F00, [('u', 4), ('u', 5), ('f', 12)], None),
    ('w_001BA1A0', 0x001BA1A0, [('u', 4), ('u', 5)], None),
    ('w_001BA1C0', 0x001BA1C0, [('u', 4), ('i', 5)], 'i'),
    ('w_001BA1F0', 0x001BA1F0, [('u', 4)], 'i'),
    ('w_001BA540', 0x001BA540, [('u', 4)], None),
    ('w_001BA580', 0x001BA580, [('u', 4), ('i', 5)], None),
    ('w_001BA8E0', 0x001BA8E0, [('u', 4), ('i', 5)], None),
    ('w_001BB520', 0x001BB520, [('u', 4)], None),
    ('w_001BB560', 0x001BB560, [('u', 4), ('u', 5), ('i', 6)], 'i'),
    ('w_001BB7C0', 0x001BB7C0, [('u', 4)], 'i'),
    ('w_001BB7F0', 0x001BB7F0, [('u', 4)], 'i'),
    ('w_001BC150', 0x001BC150, [('u', 4)], None),
    ('w_001C4760', 0x001C4760, [('i', 4), ('i', 5)], None),
    ('w_001C6380', 0x001C6380, [('u', 4)], None),
    ('w_001C63E0', 0x001C63E0, [('u', 4), ('i', 5)], None),
    ('w_001C64F0', 0x001C64F0, [('u', 4), ('f', 12)], 'i'),
    ('w_001C67E0', 0x001C67E0, [('u', 4), ('i', 5), ('f', 12), ('f', 13)], None),
    ('w_001C68C0', 0x001C68C0, [('u', 4)], None),
    ('w_001CA6F0', 0x001CA6F0, [('u', 4), ('i', 5)], None),
    ('w_001CCF70', 0x001CCF70, [('u', 4)], 'i'),
    ('w_001CD390', 0x001CD390, [('u', 4), ('u', 5)], None),
    ('w_001CFAE0', 0x001CFAE0, [('u', 4), ('i', 5), ('u', 6), ('f', 12), ('f', 13), ('f', 14), ('f', 15)], None),
    ('w_001CFBE0', 0x001CFBE0, [('i', 4), ('i', 5), ('u', 6), ('u', 7), ('i', 8)], None),
    ('w_001EFEB0', 0x001EFEB0, [('i', 4), ('u', 5)], 'u'),
    ('w_001F4BF0', 0x001F4BF0, [('u', 4), ('u', 5)], None),
    ('w_001FABB0', 0x001FABB0, [], None),
    ('w_001FA790', 0x001FA790, [('i', 4), ('i', 5)], None),
    ('w_001FAE70', 0x001FAE70, [('i', 4)], None),
    ('w_001FB0B0', 0x001FB0B0, [('i', 4)], None),
    ('w_001FB9F0', 0x001FB9F0, [('i', 4), ('i', 5), ('i', 6), ('i', 7)], None),
    ('w_001FBD50', 0x001FBD50, [('u', 4), ('i', 5), ('i', 6), ('f', 12)], 'i'),
    ('w_001FBE80', 0x001FBE80, [('u', 4), ('i', 5), ('f', 12), ('f', 13)], 'i'),
]
FOLDERS = {'a08_01_magazine': 'route_a08', 'a08_03_slider': 'route_a08', 'a08_05_terminal': 'route_a08',
           'a07_00_switch': 'route_a07'}
B0, B2, B3, A7 = FOLDERS
O.FREE = 0x1F00000                  # RAM zero in these four captures
A, P, V = O.FREE+0x1000, O.FREE+0x2000, O.FREE+0x3000
# 0x824BC0: the delay slot of the column switch's no-case branch (i is 0..4).
DEAD_WORDS = {0x824E64}


def configure(oid):
    funcs = FUNCS if oid == 8 else FUNCS07
    beats = (B0, B2, B3) if oid == 8 else (A7,)
    O.FUNCS, O.HOOKS = funcs, HOOKS
    O.OVERLAY_OF, O.OVERLAY_FILES = {entry: oid for entry in funcs}, OVERLAY_FILES
    O.OUT = ROOT/'build/levels/area08_route'
    O.SOURCES = ('em_area08_route.c',)
    O.BEATS = beats
    O.BEAT_DIRS = {b: SET/FOLDERS[b]/b for b in beats}
    O.RESIDENT = {b: oid for b in beats}
    O.BY_ADDRESS = {a: (n, args, res) for n, a, args, res in HOOKS}
    O.BY_NAME = {n: a for n, a, _, _ in HOOKS}
    O.HOOK_NAMES = [h[0] for h in HOOKS]+['w_callback']
    O.Hooks = type('Hooks', (C.Structure,), {'_fields_':
        [('ctx', C.c_void_p), ('bytes', O.BYTES_FN)] +
        [(name, O.hook_proto(args, res)) for name, _, args, res in HOOKS] +
        [('w_callback', O.CALLBACK_FN)]})
    O.SELF_ENTRIES = {0x823870, 0x824210, 0x8245E0, 0x8249F0, 0x824BC0} if oid == 8 else {0x823DA0}
    O.HEAVY = set()
    O.RETURN_SITES = O.RETURN_SITES_FULL = tuple(e for e, f in funcs.items() if f[3])
    a07 = ('w_001AFC10', 'w_001B0FD0', 'w_001B17A0', 'w_001B2140', 'w_001BA1A0', 'w_001BA1F0', 'w_001C6380')
    O.CONTRACT_UNREACHABLE = {} if oid == 8 else {h[0]: 'not called by this overlay' for h in HOOKS if h[0] not in a07}
    O.LOCAL_STORES = set()
    O.DEFAULT_ARGS = {e: [O.STACK_TOP if k == S else ((P if i else A)) for i, k in enumerate(f[2])]
                      for e, f in funcs.items()}
    O.image_for = lambda _: O.CAPTURES[beats[0]][0]
    O.CAPTURES.clear()
    O.CAPTURES.update(O.load_captures(O.read_elf()))
    O.JUMP_TABLES = {}
    O.TABLE_BYTES = frozenset()


def cases(oid):
    out = []
    b, h, w, f = O.b8, O.h16, O.w32, O.f32b
    common = [(A, bytes(0x300)), (P, bytes(0x300)), (V, bytes(0x300)),
              (A+0x4C, w(0x1C6960)), (A+0x110, w(P)), (A+0xD, b(0x54))]
    beat = B0 if oid == 8 else A7

    def add(label, entry, patches=(), script=None, on=beat):
        ram = [p for p in patches if p[0] < 0x70000000]
        spad = [p for p in patches if p[0] >= 0x70000000]
        out.append((label, on, entry, tuple(O.DEFAULT_ARGS[entry]), common+ram, spad, script or O.Script(), set()))

    def steps(entry, sweep=(0, 1, 2, 3, 4, 255), subs=(0, 1, 2, 3, 4)):
        for step in sweep:
            for sub in subs:
                for result in (0, 1):
                    add(f'state/{entry:x}/{step}/{sub}/{result}', entry,
                        [(A+4, b(step)), (A+5, b(sub)), (A+0xB, b(4))],
                        O.Script({addr: [result] for _, addr, _, res in HOOKS if res in ('i', 'u')}))
    if oid == 7:
        steps(0x823DA0)
        for z in (599.0, 600.0, 601.0):
            for pz in (599.0, 600.0, 601.0):
                for result in (0, 1):
                    for sub in (0, 1):
                        add(f'slider/{z}/{pz}/{result}/{sub}', 0x823DA0,
                            [(A+4, b(1)), (A+5, b(sub)), (A+0xB, b(4)), (A+0xB8, f(z)), (0x810358, f(pz))],
                            O.Script({0x1B2140: [result], 0x1BA1F0: [result]}))
            add(f'slider0/{z}', 0x823DA0, [(A+0xB8, f(z))])
        add('slider/nan', 0x823DA0, [(A+4, b(1)), (A+0xB, b(4)), (A+0xB8, w(0x7FC00000))])
        for beat7, (ram, _) in O.CAPTURES.items():
            for node in O.owners(ram, 0x823DA0):
                out.append((f'capture/{beat7}/{node:x}', beat7, 0x823DA0, (node,), [], [], O.Script(), set()))
        return out
    for e in (0x823870, 0x823AC0, 0x824210, 0x8245E0, 0x8249F0, 0x824BC0, 0x824790, 0x824870):
        steps(e)
    # 0x8235E0
    for raw in (-0.3, -0.2, -0.1, 0.0, 0.2):
        add(f'rise/{raw}', 0x8235E0, [(P+0x7C, f(raw))])
    add('rise/nan', 0x8235E0, [(P+0x7C, w(0x7FC00000))])
    # 0x823630
    for flag in (0, 4):
        for turn in (1.0, 1.5707964, 1.6):
            for kind in (0, 0x16):
                for door in (0, 1, 5):
                    add(f'use/{flag}/{turn}/{kind}/{door}', 0x823630,
                        [(A+0xB, b(flag)), (A+3, b(kind)), (A+0x2E, h(door)), (A+0xC4, f(0.5)), (A+0xB0, f(359.0)),
                         (A+0xB8, f(649.5)), (0x810350, O.fw(366.0, 220.0, 646.0))],
                        O.Script({0x11DF78: [O.fb(turn)], 0x1B1470: [O.fb(0.25), O.fb(0.75)],
                                  0x11E2A8: [O.fb(0.5)], 0x11DE90: [O.fb(0.25)]}))
    add('script/824460', 0x824460)
    # 0x8237B0
    for tick in (18, 19, 39, 32767):
        for phase in (0, 1, 2, 3, 4, 5, 6):
            for rnd in (0, 0x40000000, 0x7FFFFFFF):
                for flag in (0, 1):
                    add(f'noise/{tick}/{phase}/{rnd:x}/{flag}', 0x8237B0,
                        [(A+0x28, h(tick)), (A+0x2A, h(phase)), (0x81076F, b(flag))], O.Script({0x122BB8: [rnd]}))
    # 0x823870
    for flag in (0, 1, 0xFF):
        for sub in (0, 1, 2, 3, 4):
            for room in (0, 3):
                for use in (0, 4):
                    for result in (0, 1):
                        add(f'door/{flag}/{sub}/{room}/{use}/{result}', 0x823870,
                            [(A+4, b(1)), (A+5, b(sub)), (A+0xB, b(use)), (0x8107EF, b(flag)), (0x810702, b(room))],
                            O.Script({0x1BB560: [result], 0x1BB7C0: [result], 0x1BB7F0: [result], 0x1BA1F0: [result],
                                      0x11DF78: [O.fb(1.0)], 0x11E748: [O.fb(25.0)]}))
    for dist in (19.0, 20.0, 21.0):
        for bits in (0, 0x80):
            add(f'near/{dist}/{bits}', 0x823870, [(A+4, b(1)), (A+2, b(bits)), (0x810350, O.fw(1.0, 2.0, 3.0))],
                O.Script({0x11E748: [O.fb(dist)]}))
    # 0x823AC0
    for first in (0, 1):
        for count in (0, 1, 9):
            for second in (0, 1, 9):
                for pick in (0, 1, -1, 2, 3):
                    for tick in (100, 101):
                        for obj in (0, V):
                            add(f'emit/{first}/{count}/{second}/{pick}/{tick}/{obj:x}', 0x823AC0,
                                [(A+4, b(first)), (A+0x1F0, w(count)), (A+0x1F4, w(second)), (0x70003B68, w(tick))],
                                O.Script({0x1EFEB0: [obj], 0x11E748: [O.fb(3.0)]}, {0x122BB8: pick & 0xFFFFFFFF}))
    for skip in (0, 0xFF):
        add(f'emit/start/{skip}', 0x823AC0, [(0x8107F0, b(skip))])
    # 0x824210
    for lit in (0, 1):
        for flag in (0, 1, 0xFF):
            for sub in (0, 1, 2):
                for room in (0, 6):
                    for result in (0, 1):
                        add(f'owner/{lit}/{flag}/{sub}/{room}/{result}', 0x824210,
                            [(A+4, b(1)), (A+5, b(sub)), (0x81076F, b(lit)), (0x8107EF, b(flag)), (0x810702, b(room))],
                            O.Script({0x1BA1F0: [result], 0x1B6660: [V]}))
    # 0x824490
    for tick in (18, 19, 178, 1999, 2179, 3098, 3099, 3100, 3200):
        add(f'timeline/{tick}', 0x824490, [(A+0x28, h(tick)), (A+0xB0, f(1.0)), (A+0xB8, f(2.0))])
    # 0x8245E0, 0x824790, 0x824870
    for lit in (0, 1, 0xFF):
        for other in (0, 0xFF):
            for sub in (0, 1, 2, 3):
                for use in (0, 4):
                    for result in (0, 1):
                        add(f'terminal/{lit}/{other}/{sub}/{use}/{result}', 0x8245E0,
                            [(A+4, b(1)), (A+5, b(sub)), (A+0xB, b(use)), (0x810770, b(lit)), (0x81076D, b(other)),
                             (A+0xB0, f(1.0))], O.Script({0x1BA1F0: [result]}))
    for lit in (0, 1):
        for count in (0, 1):
            for sub in (0, 1, 2, 3):
                for result in (0, 1):
                    add(f'half/{lit}/{count}/{sub}/{result}', 0x824870,
                        [(A+5, b(sub)), (A+6, b(count)), (A+0xB, b(4)), (0x81077D, b(lit))],
                        O.Script({0x1BA1F0: [result]}))
    add('terminal/start/set', 0x8245E0, [], O.Script({0x1B0FD0: [0], 0x1BA1C0: [1]}))
    # 0x8249F0
    for x in (299.0, 300.0, 301.0):
        add(f'slider0/{x}', 0x8249F0, [(A+0xB0, f(x))])
        for result in (0, 1):
            for sub in (0, 1):
                add(f'slider/{x}/{result}/{sub}', 0x8249F0, [(A+4, b(1)), (A+5, b(sub)), (A+0xB, b(4)), (A+0xB0, f(x))],
                    O.Script({0x1B2140: [result], 0x1BA1F0: [result]}))
    add('slider/nan', 0x8249F0, [(A+4, b(1)), (A+0xB, b(4)), (A+0xB0, w(0x7FC00000))])
    # 0x824BC0
    for voice in (-1, 3):
        for sound in (0x910, 0x911):
            for tick in (10, 11):
                for got in (-1, 0, 2):
                    add(f'flames/{voice}/{sound:x}/{tick}/{got}', 0x824BC0,
                        [(A+4, b(1)), (A+0x2EC, w(voice)), (0x70003B68, w(tick)), (0x70003B8A, h(0)),
                         (0x281C30+4*3, w(sound)), (0x275B40, w(V)), (V+0xC, w(P)), (P+0x74, f(0.5)),
                         (A+0x1F0, w(0x12345678)), (A+0x1F4, O.fw(0.5, 1.995, 2.0, 2.5, 3.0))],
                        O.Script({0x1FBE80: [got], 0x1FBD50: [got], 0x1B1470: [O.fb(0.25)], 0x1CCF70: [1, 2, 3, 4, 5]}))
    # the captured owners
    for beat8, (ram, _) in O.CAPTURES.items():
        for owner, entries in ((0x823870, (0x823870,)), (0x823AC0, (0x823AC0,)), (0x824210, (0x824210,)),
                               (0x8245E0, (0x8245E0, 0x824790, 0x824870)), (0x8249F0, (0x8249F0,)), (0x824BC0, (0x824BC0,))):
            for node in O.owners(ram, owner):
                for entry in entries:
                    args = (node, O.STACK_TOP) if entry == 0x824BC0 else (node,)
                    out.append((f'capture/{beat8}/{entry:x}/{node:x}', beat8, entry, args, [], [], O.Script(), set()))
    return out


def header_checks():
    header = (ROOT/'src/game/em_area08_route.h').read_text()
    block = header.split('/* BEGIN GENERATED HOOKS */')[1].split('/* END GENERATED HOOKS */')[0]
    found = re.findall(r'int \(\*(w_[0-9A-F]{8})\)\(void \*ctx((?:, [^,)]+)*)\);', block)
    assert [n for n, _ in found] == [h[0] for h in HOOKS]
    types = {U: 'uint32_t', I: 'int32_t', F: 'float'}
    for (name, params), (expected, address, args, result) in zip(found, HOOKS):
        actual = [re.sub(r'\s*\b\w+$', '', p.strip()).replace(' ', '') for p in params.split(',')[1:]]
        assert actual == [types[k] for k, _ in args] + ([types[result]+'*'] if result else []), name
    internal = (ROOT/'src/game/em_area08_route_internal.h').read_text()
    for name, address, _, _ in HOOKS:
        bodies = re.findall(r'static inline int a08r_c_%s\(.*?\n}\n' % name[2:], internal, re.S)
        assert len(bodies) == 1 and f'o->h->{name}(' in bodies[0] and f'0x{address:08X}u' in bodies[0]


def run(oid):
    configure(oid)
    O.NATIVE = O.build(os.environ.get('EM_AREA08_ROUTE_SOURCE'))
    selected = cases(oid)
    only = os.environ.get('EM_AREA08_ROUTE_ONLY')
    if only:
        selected = [c for c in selected if c[0].startswith(only)]
    O.CASES[:] = selected
    assert len({c[0] for c in selected}) == len(selected), 'duplicate labels'
    results = O.run_cases(selected)
    failures = [(label, errors) for label, _, errors, _, _, _ in results if errors]
    seen = set().union(*(r[3] for r in results)) if results else set()
    for entry, (_, size, _, _) in O.FUNCS.items():
        words = O.reachable_words(O.image_for(entry), entry, size)
        missing = words - seen - DEAD_WORDS
        print(f'{entry:08X}: {len(words & seen)}/{len(words)} reachable words')
        if missing and not only:
            failures.append(('coverage', [entry, [hex(m) for m in sorted(missing)]]))
    if not only:
        failures += [('callee inventory', p) for p in O.callee_checks()]
        if not failures:
            sites, missing = O.hook_sites(results)
            if missing:
                failures.append(('unreached hooks', missing))
            for problems, runs in O.reference_mode.parallel_map(O.hook_contract_site, sites):
                if problems:
                    failures.append(('fail-stop contract', problems))
    for label, errors in failures[:int(os.environ.get('EM_AREA08_ROUTE_SHOW', '8'))]:
        print('FAIL', label, str(errors)[:600])
    assert not failures, f'{len(failures)} failures'
    print(f'PASS {len(selected)} cases: calls, ordered accesses, whole memory, poisoned starts and fail-stop')


def main():
    header_checks()
    for oid in (8, 7):
        run(oid)


if __name__ == '__main__':
    main()
