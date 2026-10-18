#!/usr/bin/env python3
"""AREA13 route gaps: original code identity, ordered memory and worker replay.

Standalone and UNWIRED. Uses the existing level-14 comparator unchanged.
EM_TEST_FULL=1 sweeps every failure site; the default still covers every
reachable word. EM_AREA13_ROUTE_ONLY is diagnostic, never certification.
"""
import ctypes as C
import os
import re
from pathlib import Path
import test_level14_port_reference as O
import reference_root as refs
ROOT = Path(__file__).resolve().parents[1]
U, I, F, S = O.U, O.I, O.F, O.S
FUNCS = {
    0x823580: ('em_area13_route_00823580', 0x158, [U], None),
    0x823830: ('em_area13_route_00823830', 0x104, [U], None),
    0x823A40: ('em_area13_route_00823A40', 0x174, [U], None),
    0x824A80: ('em_area13_route_00824A80', 0x12C, [U], None),
}
OVERLAY_OF = {entry: 10 for entry in FUNCS}
OVERLAY_FILES = {10: ROOT.parent/'Extermination/extract/OVERLAY/AREA13.BIN'}
HOOKS = [
    ('w_001AEDB0', 0x001AEDB0, [('i', 4)], None),
    ('w_001AFC10', 0x001AFC10, [('u', 4)], None),
    ('w_001B1020', 0x001B1020, [('u', 4), ('i', 5), ('i', 6), ('i', 7)], None),
    ('w_001B1EA0', 0x001B1EA0, [('i', 4), ('u', 5), ('u', 6), ('i', 7)], 'i'),
    ('w_001B6660', 0x001B6660, [('u', 4)], None),
    ('w_001BA1A0', 0x001BA1A0, [('u', 4), ('u', 5)], None),
    ('w_001BA1C0', 0x001BA1C0, [('u', 4), ('i', 5)], 'i'),
    ('w_001BA1F0', 0x001BA1F0, [('u', 4)], 'i'),
    ('w_001BBDA0', 0x001BBDA0, [('u', 4), ('u', 5)], None),
    ('w_001BBE40', 0x001BBE40, [('u', 4), ('u', 5), ('i', 6)], 'i'),
    ('w_001BC0E0', 0x001BC0E0, [('u', 4), ('u', 5)], 'i'),
    ('w_001BC240', 0x001BC240, [('u', 4), ('u', 5)], None),
    ('w_001BC290', 0x001BC290, [('u', 4), ('u', 5)], 'i'),
    ('w_001BC300', 0x001BC300, [('u', 4)], None),
    ('w_001C4760', 0x001C4760, [('i', 4), ('i', 5)], None),
    ('w_001C6380', 0x001C6380, [('u', 4)], None),
    ('w_001EFD20', 0x001EFD20, [('i', 4), ('u', 5)], None),
    ('w_001FAE70', 0x001FAE70, [('i', 4)], None),
 ]
BEATS = ('a13_00_door8', 'a13_01_door14', 'a13_02_door17', 'a13_03_item27', 'a13_04_hatch')
BEAT = BEATS[0]
A, P = O.FREE + 0x1000, O.FREE + 0x2000


def configure():
    O.FUNCS, O.HOOKS = FUNCS, HOOKS
    O.OVERLAY_OF, O.OVERLAY_FILES = OVERLAY_OF, OVERLAY_FILES
    O.OUT = ROOT/'build/levels/area13_route'
    O.SOURCES = ('em_area13_route.c',)
    O.BEATS = BEATS
    route = refs.v263('s87/route_a13', 'fork route is not recorded from a04b_00; retained original captures explicitly selected')
    O.BEAT_DIRS = {b: route/b for b in BEATS}
    O.RESIDENT = {b: 10 for b in BEATS}
    O.BY_ADDRESS = {a:(n,args,res) for n,a,args,res in HOOKS}
    O.BY_NAME = {n:a for n,a,_,_ in HOOKS}
    O.HOOK_NAMES = [h[0] for h in HOOKS] + ['w_callback']
    O.Hooks = type('Hooks', (C.Structure,), {'_fields_':
        [('ctx', C.c_void_p), ('bytes', O.BYTES_FN)] +
        [(name,O.hook_proto(args,res)) for name,_,args,res in HOOKS] +
        [('w_callback',O.CALLBACK_FN)]})
    O.SELF_ENTRIES, O.HEAVY = {0x823A40}, set()
    O.RETURN_SITES = O.RETURN_SITES_FULL = ()
    O.CONTRACT_UNREACHABLE = {}
    O.LOCAL_STORES = set()
    O.DEFAULT_ARGS = {e:[A] for e in FUNCS}
    O.image_for = lambda _: O.CAPTURES[BEAT][0]
    O.CAPTURES.clear()
    O.CAPTURES.update(O.load_captures(O.read_elf()))
    O.JUMP_TABLES = {}
    O.TABLE_BYTES = frozenset()


def cases():
    out=[]
    b,h,w,f=O.b8,O.h16,O.w32,O.f32b
    common=[(A,bytes(0x300)),(P,bytes(0x300)),(A+0x1C,w(P)),
            (A+0x4C,w(0x1C6960)),(A+0xD,b(255)),(0x810354,f(170.0)),(0x810702,b(2))]
    def add(label,entry,patches=(),script=None):
        out.append((label,BEAT,entry,(A,),common+list(patches),[],script or O.Script(),set()))
    for entry in FUNCS:
        for state in (0,1,2,3,4,255):
            for step in (0,1,2,3,4,255):
                for result in (0,1,-1):
                    script=O.Script({address:[result] for _,address,_,kind in HOOKS if kind})
                    add(f'state/{entry:x}/{state}/{step}/{result}',entry,
                        [(A+4,b(state)),(A+5,b(step))],script)
    for flag in (0,1,2,255):
        add(f'door/flag/{flag}',0x823580,[(A+4,b(1)),(0x810774,b(flag))])
    for raw in (0,0x80000000,0x4329FFFF,0x432A0000,0x432A0001,0x7F800000,0xFF800000,0x7FC00000,0xFFFFFFFF):
        for mode in (0,1,2,3,255):
            for entry in (0x823830,0x823A40):
                add(f'range/{entry:x}/{raw:x}/{mode}',entry,
                    [(A+4,b(1)),(0x810354,w(raw)),(0x810702,b(mode))],O.Script({0x1B1EA0:[1]}))
    for entry,fn,step,changes in (
        (0x823580,0x1BBDA0,0,[(A+0x1C,w(P+0x100))]),
        (0x823580,0x1BBE40,0,[(A+5,b(255))]),
        (0x823580,0x1BC0E0,1,[(A+5,b(7))]),
        (0x823580,0x1BC240,2,[(A+5,b(9))]),
        (0x823830,0x1BA1F0,1,[(A+5,b(7)),(0x8107F1,b(99))]),
        (0x823A40,0x1B1EA0,0,[(A+5,b(7)),(A+0x28,h(99))]),
        (0x823A40,0x1C6380,0,[(A+0x4C,w(0x1C6960))]),
        (0x824A80,0x1BA1F0,1,[(A+4,b(7))]),
    ):
        add(f'scribble/{entry:x}/{fn:x}',entry,
            [(A+4,b(0 if fn==0x1BBDA0 else 1)),(A+5,b(step))],
            O.Script({fn:[O.Scribble(1,changes)]}))
    for beat,(ram,_) in O.CAPTURES.items():
        for entry in FUNCS:
            for node in O.owners(ram,entry):
                out.append((f'capture/{beat}/{entry:x}/{node:x}',beat,entry,(node,),[],[],O.Script(),set()))
    return out


def header_checks():
    header = (ROOT/'src/game/em_area13_route.h').read_text()
    block = header.split('/* BEGIN GENERATED HOOKS */')[1].split('/* END GENERATED HOOKS */')[0]
    found = re.findall(r'int \(\*(w_[0-9A-F]{8})\)\(void \*ctx((?:, [^,)]+)*)\);', block)
    assert [n for n,_ in found] == [h[0] for h in HOOKS]
    types = {U:'uint32_t',I:'int32_t',F:'float'}
    for (name,params),(expected,address,args,result) in zip(found,HOOKS):
        actual = [re.sub(r'\s*\b\w+$','',p.strip()).replace(' ','') for p in params.split(',')[1:]]
        assert actual == [types[k] for k,_ in args] + ([types[result]+'*'] if result else [])
    internal = (ROOT/'src/game/em_area13_route_internal.h').read_text()
    for name,address,_,_ in HOOKS:
        bodies = re.findall(r'static inline int a13r_c_%s\(.*?\n}\n' % name[2:],internal,re.S)
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
    O.NATIVE = O.build(os.environ.get("EM_AREA13_ROUTE_SOURCE"))
    header_checks()
    entry_guards()
    selected = cases()
    only = os.environ.get('EM_AREA13_ROUTE_ONLY')
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
    for label, errors in failures[:int(os.environ.get('EM_AREA13_ROUTE_SHOW', '8'))]: print('FAIL', label, errors)
    assert not failures, f'{len(failures)} failures'
    print(f'PASS {len(selected)} cases: calls, ordered accesses, whole memory, poisoned starts and fail-stop')


if __name__ == '__main__': main()
