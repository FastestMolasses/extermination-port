#!/usr/bin/env python3
"""Four AREA01 route gaps, exercised on the return from AREA00 (UNWIRED).

Reuse the level-14 comparator without changing its assertions: original text
identity, ordered accesses, memory at every call, final whole memory, poisoned
starts, exact float bits, worker replay and per-call/per-access fail-stop sweeps.
The imported harness is configured only in this process. Native arm64 macOS;
all instruction/data inputs remain local. EM_TEST_FULL=1 sweeps every failure
site. EM_AREA00_RETURN_ONLY is a diagnostic subset, never certification.
"""
import ctypes as C
import os
import re
from pathlib import Path
import struct
import test_level14_port_reference as O
import reference_root as refs

ROOT = Path(__file__).resolve().parents[1]
U, I, F, S = O.U, O.I, O.F, O.S
FUNCS = {
    0x823A90: ('em_area00_return_00823A90', 0x238, [U, U, U], I),
    0x823CD0: ('em_area00_return_00823CD0', 0x40C, [U], None),
    0x8240E0: ('em_area00_return_008240E0', 0x258, [U, U], None),
    0x826010: ('em_area00_return_00826010', 0x190, [U, S], None),
}
OVERLAY_OF = {entry: 2 for entry in FUNCS}
OVERLAY_FILES = {2: ROOT.parent / 'Extermination/extract/OVERLAY/AREA01.BIN'}
HOOKS = [
    ('w_001026A0', 1058464, [('u', 4), ('u', 5), ('u', 6)], None),
    ('w_00102718', 1058584, [('u', 4), ('u', 5), ('u', 6)], None),
    ('w_001027E0', 1058784, [('u', 4), ('u', 5)], None),
    ('w_001028D0', 1059024, [('u', 4), ('u', 5), ('u', 6)], None),
    ('w_00102958', 1059160, [('u', 4), ('u', 5)], None),
    ('w_001029C0', 1059264, [('u', 4)], None),
    ('w_001031E0', 1061344, [('u', 4), ('u', 5)], None),
    ('w_00103230', 1061424, [('u', 4), ('u', 5), ('f', 12)], None),
    ('w_00122BB8', 1190840, [], 'i'),
    ('w_001287F0', 1214448, [('u', 4), ('u', 5), ('i', 6), ('f', 12)], None),
    ('w_001288D0', 1214672, [('u', 4), ('u', 5)], None),
    ('w_00128AB0', 1215152, [('u', 4), ('u', 5)], 'i'),
    ('w_00129780', 1218432, [('u', 4), ('u', 5), ('i', 6)], 'i'),
    ('w_0019A570', 1680752, [('u', 4), ('u', 5), ('i', 6), ('i', 7)], 'i'),
    ('w_001AFC10', 1768464, [('u', 4)], None),
    ('w_001B0CD0', 1772752, [('u', 4), ('i', 5)], None),
    ('w_001B1240', 1774144, [('u', 4), ('f', 12), ('f', 13)], 'f'),
    ('w_001B12B0', 1774256, [('f', 12), ('f', 13), ('f', 14)], 'f'),
    ('w_001B1470', 1774704, [('f', 12)], 'f'),
    ('w_001B17A0', 1775520, [('u', 4)], None),
    ('w_001BA1C0', 1810880, [('u', 4), ('i', 5)], 'i'),
    ('w_001C2770', 1845104, [('u', 4), ('u', 5), ('i', 6)], 'i'),
    ('w_001C3D60', 1850720, [('u', 4), ('u', 5)], None),
    ('w_001C63E0', 1860576, [('u', 4), ('i', 5)], None),
    ('w_001C64F0', 1860848, [('u', 4), ('f', 12)], 'i'),
    ('w_001C6960', 1861984, [('u', 4)], None),
    ('w_001C69A0', 1862048, [('u', 4)], None),
    ('w_001CA6F0', 1877744, [('u', 4), ('i', 5)], None),
    ('w_001EFD90', 2030992, [('i', 4), ('u', 5), ('u', 6)], None),
    ('w_001FB9F0', 2079216, [('i', 4), ('i', 5), ('i', 6), ('i', 7)], None),
    ('w_001FBD50', 2080080, [('u', 4), ('i', 5), ('i', 6), ('f', 12)], None),
    ('w_00824340', 8536896, [('u', 4), ('u', 5)], None),
    ('w_00824770', 8537968, [('u', 4), ('u', 5)], None),
    ('w_00824D50', 8539472, [('u', 4), ('u', 5)], None),
]

BEAT = 'a01r_01_event'
BEATS = (BEAT, 'a01r_00_to_train_room', 'a00_10_progression_exit')
A, E, P, V = O.FREE + 0x1000, O.FREE + 0x11F0, O.FREE + 0x2000, O.FREE + 0x3000


def configure():
    O.FUNCS, O.HOOKS = FUNCS, HOOKS
    O.OVERLAY_OF, O.OVERLAY_FILES = OVERLAY_OF, OVERLAY_FILES
    O.OUT = ROOT / 'build/levels/area00_return'
    O.SOURCES = ('em_area00_return.c',)
    O.BEATS = BEATS
    O.BEAT_DIRS = {b: refs.capture('s87/route_a00' if b.startswith('a00_') else 's87/route_a01r') / b for b in BEATS}
    O.RESIDENT = {b: 2 for b in BEATS}
    O.BY_ADDRESS = {a: (n, args, res) for n, a, args, res in HOOKS}
    O.BY_NAME = {n: a for n, a, _, _ in HOOKS}
    O.HOOK_NAMES = [h[0] for h in HOOKS] + ['w_callback']
    O.Hooks = type('Hooks', (C.Structure,), {'_fields_':
        [('ctx', C.c_void_p), ('bytes', O.BYTES_FN)] +
        [(name, O.hook_proto(args, res)) for name, _, args, res in HOOKS] +
        [('w_callback', O.CALLBACK_FN)]})
    O.SELF_ENTRIES, O.HEAVY = {0x823CD0}, set()
    O.RETURN_SITES = O.RETURN_SITES_FULL = (0x823A90,)
    O.CONTRACT_UNREACHABLE = {}
    O.LOCAL_STORES = set()
    O.DEFAULT_ARGS = {e: [O.STACK_TOP if k == S else A for k in kinds] for e, (_, _, kinds, _) in FUNCS.items()}
    O.image_for = lambda _: O.CAPTURES[BEAT][0]
    O.CAPTURES.clear()
    O.CAPTURES.update(O.load_captures(O.read_elf()))
    # Table locations from matching decomp C; find their dispatch sites without
    # reproducing any instruction text. The order follows the two nested
    # switches in the owner; the turn routine uses direct comparisons.
    ram = O.CAPTURES[BEAT][0]
    sites = []
    for start, length in ((0x823CD0, 0x40C), (0x8240E0, 0x258)):
        sites += [pc for pc in range(start, start + length, 4)
                  if (O.u32(ram, pc) & 0xFC00003F) == 8 and (O.u32(ram, pc) >> 21 & 31) != 31]
    assert len(sites) == 2
    O.JUMP_TABLES = dict(zip(sites, ((0x82CBD0, 6), (0x82CBA0, 10))))
    O.TABLE_BYTES = frozenset(a for table, n in O.JUMP_TABLES.values() for a in range(table, table + 4*n))


def cases():
    out = []
    b, h, w, f = O.b8, O.h16, O.w32, O.f32b
    common = [(A, bytes(0x300)), (P, bytes(0x300)), (A + 0x4C, w(0x1C6380)),
              (A + 0x1C, w(P)), (A + 0xD, b(0x23)), (0x275B40, w(V)),
              (V + 0xC, w(V + 0x100)), (E + 0x70, O.fw(0.0, 0.0, 1.0)),
              (E + 0x80, O.fw(0.0, 1.0, 0.0))]
    def add(label, entry, patches=(), values=None, script=None):
        args = values or {0x823A90: (A, A + 0xB0, E + 0x50), 0x823CD0: (A,),
                          0x8240E0: (A, E), 0x826010: (A, O.STACK_TOP)}[entry]
        out.append((label, BEAT, entry, args, common + list(patches), [], script or O.Script(), set()))
    for z in (-586.0, -585.0, -584.0, -546.0, -545.0, -544.0):
        for y in (34.0, 35.0, 36.0, 49.0, 50.0, 51.0):
            add(f'range/{z}/{y}', 0x823A90, [(A + 0xB8, f(z)), (A + 0xB4, f(y))])
    for state in range(7):
        for step in range(11):
            for active in (0, 1):
                add(f'owner/{state}/{step}/{active}', 0x823CD0,
                    [(A + 4, b(state)), (A + 5, b(step)), (A + 1, b(active)), (E + 0xD8, f(active))])
    for fn in (0x1BA1C0, 0x128AB0, 0x129780):
        for result in (1, -1, 0x1234):
            add(f'owner/result/{fn:x}/{result}', 0x823CD0,
                [(A + 5, b(1 if fn == 0x129780 else 0))], script=O.Script({fn: [result]}))
    for count in (0, 1, 255):
        add(f'owner/story/{count}', 0x823CD0,
            [(A+4,b(1)),(A+5,b(1)),(0x8107DF,b(2)),(E+0xE3,b(count))])
    for state in (4, 5):
        for timer in (508.0, 509.0, 510.0, 511.0):
            add(f'owner/timer/{state}/{timer}',0x823CD0,[(A+4,b(state)),(A+0x3C,f(timer))])
    for step in range(6):
        for result in (0, 1, 8, -1):
            for timer in (0, 1, 2, 0x8000):
                for inside in (0, 1):
                    add(f'turn/{step}/{result}/{timer}/{inside}',0x8240E0,
                        [(A+6,b(step)),(E+0xD0,h(timer)),(A+0xB8,f(-560.0 if inside else 0.0)),
                         (A+0xB4,f(40.0)), (E+0xE8,f(inside))],
                        script=O.Script({0x1C2770:[result],0x122BB8:[inside,0xF0]}))
    for timer in (0, 4, 5, 179, 180, 185, 32767, 65529, 65535):
        for skip in (0, 1, 255):
            for hit in (0, 1):
                add(f'probe/{timer}/{skip}/{hit}',0x826010,
                    [(A+0x2A,h(timer)),(A+0x240,b(skip))],script=O.Script({0x19A570:[hit],0x122BB8:[0x7FFFFFFF]}))
    # Original owner records still present on the captured transition.
    for beat, (ram, _) in O.CAPTURES.items():
        for owner in (0x823CD0, 0x825BE0, 0x825D30):
            for node in O.owners(ram, owner):
                entries = (0x823CD0, 0x823A90, 0x8240E0) if owner == 0x823CD0 else (0x826010,)
                for entry in entries:
                    args = {0x823CD0:(node,), 0x823A90:(node,node+0xB0,node+0x240),
                            0x8240E0:(node,node+0x1F0), 0x826010:(node,O.STACK_TOP)}[entry]
                    out.append((f'capture/{beat}/{entry:x}/{node:x}', beat, entry, args,
                                [], [], O.Script(), set()))
    # Callees may change fields subsequently read by their caller.
    for entry, fn, patches, changes in (
        (0x823CD0, 0x128AB0, [], [(A+5,b(9))]),
        (0x823CD0, 0x1B17A0, [(A+4,b(1))], [(A+5,b(6))]),
        (0x823CD0, 0x824340, [(A+4,b(1)),(A+5,b(2))], [(A+5,b(7)),(A+1,b(1)),(E+0xFA,b(255))]),
        (0x823CD0, 0x1C64F0, [(A+4,b(1)),(A+5,b(6))], [(A+1,b(1)),(A+0x4C,w(0x1C6380))]),
        (0x823CD0, 0x1C6960, [(A+4,b(5))], [(A+0x3C,f(510.0))]),
        (0x8240E0, 0x1B12B0, [(A+6,b(1))], [(E+0xE8,w(1))]),
        (0x826010, 0x1B1470, [(A+0x2A,h(5))], [(A+0xC4,f(3.0))]),
    ):
        add(f'scribble/{entry:x}/{fn:x}',entry,patches,
            script=O.Script({fn:[O.Scribble(1,changes)],0x19A570:[1]}))
    # Results crossing a hook retain the original signed and float bit lanes.
    for raw in (0, 1, 0x80000000, 0x7FFFFFFF, 0xFFFFFFFF, 0x7F800001, 0xFFC00000):
        add(f'float/range/{raw:x}',0x823A90,script=O.Script({0x1B1240:[raw]}))
        for step in (1,3):
            add(f'float/turn/{step}/{raw:x}',0x8240E0,[(A+6,b(step)),(E+0xE8,w(raw))],
                script=O.Script({0x1B12B0:[raw]}))
        add(f'probe/random/{raw:x}',0x826010,[(A+0x2A,h(185))],script=O.Script({0x122BB8:[raw]}))
    return out


def header_checks():
    header = (ROOT/'src/game/em_area00_return.h').read_text()
    block = header.split('/* BEGIN GENERATED HOOKS */')[1].split('/* END GENERATED HOOKS */')[0]
    found = re.findall(r'int \(\*(w_[0-9A-F]{8})\)\(void \*ctx((?:, [^,)]+)*)\);', block)
    assert [n for n,_ in found] == [h[0] for h in HOOKS]
    types = {U:'uint32_t',I:'int32_t',F:'float'}
    for (name,params),(expected,address,args,result) in zip(found,HOOKS):
        actual = [re.sub(r'\s*\b\w+$','',p.strip()).replace(' ','') for p in params.split(',')[1:]]
        assert actual == [types[k] for k,_ in args] + ([types[result]+'*'] if result else [])
    internal = (ROOT/'src/game/em_area00_return_internal.h').read_text()
    for name,address,_,_ in HOOKS:
        bodies = re.findall(r'static inline int a00r_c_%s\(.*?\n}\n' % name[2:],internal,re.S)
        assert len(bodies)==1 and f'o->h->{name}(' in bodies[0] and f'0x{address:08X}u' in bodies[0]


def entry_guards():
    touched=[]
    def refuse(*_): touched.append(1); return -1
    keep=[O.BYTES_FN(refuse), O.CALLBACK_FN(refuse)]
    fields={'ctx':None,'bytes':keep[0],'w_callback':keep[1]}
    for name,_,args,result in HOOKS:
        fields[name]=O.hook_proto(args,result)(refuse); keep.append(fields[name])
    hooks=O.Hooks(**fields)
    for entry,(symbol,_,_,kind) in FUNCS.items():
        for table,fault,with_result in ((C.byref(hooks),O.Fault(0x1234,7),True),
                                       (None,O.Fault(),True),(C.byref(hooks),None,True),
                                       (C.byref(hooks),O.Fault(),False)):
            if not with_result and kind is None: continue
            out=C.c_int32(0x5A5A5A5A); touched.clear()
            args=[table]+O.native_args(entry,O.DEFAULT_ARGS[entry])
            if kind is not None: args.append(C.byref(out) if with_result else None)
            rc=getattr(O.NATIVE,symbol)(*args,C.byref(fault) if fault is not None else None)
            assert rc==-1 and not touched and out.value==0x5A5A5A5A
            if fault is not None:
                assert (fault.address,fault.code)==((0x1234,7) if table is not None and with_result else (0,0))


def main():
    configure()
    O.NATIVE = O.build(os.environ.get("EM_AREA00_RETURN_SOURCE"))
    header_checks()
    entry_guards()
    selected = cases()
    only = os.environ.get('EM_AREA00_RETURN_ONLY')
    if only: selected = [c for c in selected if c[0].startswith(only)]
    O.CASES[:] = selected
    assert len({c[0] for c in selected}) == len(selected)
    results = O.run_cases(selected)
    failures = [(label, errors) for label, _, errors, _, _, _ in results if errors]
    seen = set().union(*(r[3] for r in results))
    for entry, (_, size, _, _) in FUNCS.items():
        words = O.reachable_words(O.image_for(entry), entry, size)
        missing = words - seen
        print(f'{entry:08X}: {len(words & seen)}/{len(words)} reachable words')
        if missing and not only: failures.append(('coverage', [entry, sorted(missing)]))
    if not only:
        failures += [('callee inventory', p) for p in O.callee_checks()]
        if not failures:
            sites, missing = O.hook_sites(results)
            if missing: failures.append(('unreached hooks', missing))
            for problems, runs in O.reference_mode.parallel_map(O.hook_contract_site, sites):
                if problems: failures.append(('fail-stop contract', problems))
    for label, errors in failures[:int(os.environ.get('EM_AREA00_RETURN_SHOW', '8'))]: print('FAIL', label, errors)
    assert not failures, f'{len(failures)} failures'
    print(f'PASS {len(selected)} cases: calls, ordered accesses, whole memory, poisoned starts and fail-stop')


if __name__ == '__main__': main()
