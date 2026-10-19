#!/usr/bin/env python3
"""AREA15 route gaps: original code identity, ordered memory and worker replay.

Standalone and UNWIRED. Uses the existing level-14 comparator unchanged.
EM_TEST_FULL=1 sweeps every failure site; the default still covers every
reachable word. EM_AREA15_ROUTE_ONLY is diagnostic, never certification.
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
    0x00823B40: ('em_area15_route_00823B40', 0x138, [U], None),
    0x00823C80: ('em_area15_route_00823C80', 0x1B4, [U], None),
    0x00823F60: ('em_area15_route_00823F60', 0x110, [U, U, U], I),
    0x00824240: ('em_area15_route_00824240', 0x110, [U], None),
    0x00824350: ('em_area15_route_00824350', 0x84, [U], None),
    0x008243E0: ('em_area15_route_008243E0', 0x124, [U], None),
}
OVERLAY_OF = {entry: 12 for entry in FUNCS}
OVERLAY_FILES = {12: ROOT.parent/'Extermination/extract/OVERLAY/AREA15.BIN'}
HOOKS = [
    ('w_00102948', 0x00102948, [('u', 4), ('u', 5)], None),
    ('w_001AEDE0', 0x001AEDE0, [('i', 4), ('i', 5)], None),
    ('w_001B17A0', 0x001B17A0, [('u', 4)], None),
    ('w_001BA1A0', 0x001BA1A0, [('u', 4), ('u', 5)], None),
    ('w_001BA1C0', 0x001BA1C0, [('u', 4), ('i', 5)], 'i'),
    ('w_001BA1F0', 0x001BA1F0, [('u', 4)], 'i'),
    ('w_001BA580', 0x001BA580, [('u', 4), ('i', 5)], None),
    ('w_001C4760', 0x001C4760, [('i', 4), ('i', 5)], None),
    ('w_001C47A0', 0x001C47A0, [('i', 4), ('i', 5)], None),
    ('w_001C64F0', 0x001C64F0, [('u', 4), ('f', 12)], 'i'),
    ('w_001C67E0', 0x001C67E0, [('u', 4), ('i', 5), ('f', 12), ('f', 13)], None),
    ('w_001C68C0', 0x001C68C0, [('u', 4)], None),
    ('w_001CA6F0', 0x001CA6F0, [('u', 4), ('i', 5)], None),
    ('w_001FABB0', 0x001FABB0, [], None),
    ('w_001FAE70', 0x001FAE70, [('i', 4)], None),
    ('w_001FBC50', 0x001FBC50, [], None),
    ('w_00823E40', 0x00823E40, [('u', 4)], None),
 ]
BEATS = ('a19d_20_door50', 'a15_01_door51')
BEAT = BEATS[0]
A, P, V = O.FREE+0x1000, O.FREE+0x2000, O.FREE+0x3000


def configure():
    O.FUNCS, O.HOOKS = FUNCS, HOOKS
    O.OVERLAY_OF, O.OVERLAY_FILES = OVERLAY_OF, OVERLAY_FILES
    O.OUT = ROOT/'build/levels/area15_route'
    O.SOURCES = ('em_area15_route.c',)
    O.BEATS = BEATS
    O.BEAT_DIRS = {b:refs.v263('s87/route_'+b.split('_')[0]+'/'+b,
        'fork route is not recorded from a04b_00; retained original captures explicitly selected') for b in BEATS}
    O.RESIDENT = {b:12 for b in BEATS}
    O.BY_ADDRESS = {a:(n,args,res) for n,a,args,res in HOOKS}
    O.BY_NAME = {n:a for n,a,_,_ in HOOKS}
    O.HOOK_NAMES = [h[0] for h in HOOKS]+['w_callback']
    O.Hooks = type('Hooks',(C.Structure,),{'_fields_':
        [('ctx',C.c_void_p),('bytes',O.BYTES_FN)] +
        [(name,O.hook_proto(args,res)) for name,_,args,res in HOOKS] +
        [('w_callback',O.CALLBACK_FN)]})
    O.SELF_ENTRIES, O.HEAVY = {0x823B40,0x8243E0}, set()
    O.RETURN_SITES = O.RETURN_SITES_FULL = (0x823F60,)
    O.CONTRACT_UNREACHABLE = {}
    O.LOCAL_STORES = set()
    O.DEFAULT_ARGS = {e:([A,P,V] if e==0x823F60 else [A]) for e in FUNCS}
    O.image_for = lambda _:O.CAPTURES[BEAT][0]
    O.CAPTURES.clear()
    O.CAPTURES.update(O.load_captures(O.read_elf()))
    O.JUMP_TABLES = {}
    O.TABLE_BYTES = frozenset()


def cases():
    out=[]
    b,h,w,f=O.b8,O.h16,O.w32,O.f32b
    common=[(A,bytes(0x300)),(P,bytes(0x300)),(V,bytes(0x300)),
            (A+0x4C,w(0x1C6960)),(0x8106C0,w(A)),(A+0xD,b(0x54)),
            (0x810350,O.fw(893.0,291.0,940.0))]
    def add(label,entry,patches=(),script=None):
        out.append((label,BEAT,entry,tuple(O.DEFAULT_ARGS[entry]),common+list(patches),[],script or O.Script(),set()))
    for entry in FUNCS:
        for step in (0,1,2,3,4,255):
            for subtype in (0,0x50,0x54,255):
                for result in (0,1,-1):
                    add(f'state/{entry:x}/{step}/{subtype}/{result}',entry,
                        [(A+5,b(step)),(P+4,b(step)),(A+0xD,b(subtype)),(A+0xB,b(4))],
                        O.Script({addr:[result] for _,addr,_,res in HOOKS if res}))
    for x in (892.0,893.0,894.0):
        for y in (290.0,291.0,292.0,329.0,330.0):
            for z in (939.0,940.0,941.0):
                add(f'range/{x}/{y}/{z}',0x823B40,[(0x810350,O.fw(x,y,z)),(A+0x28,h(10))])
    for timer in (0,1,2,32767,32768,65535):
        add(f'timer/{timer}',0x823B40,[(A+0x28,h(timer))])
        for sub in (0,1,2,255):
            add(f'countdown/{timer}/{sub}',0x823C80,[(A+5,b(2)),(A+6,b(sub)),(A+0x28,h(timer))])
    for pointer in (0,A):
        for state in (0,1,2,3,4,255):
            add(f'camera/{pointer:x}/{state}',0x823F60,[(0x8106C0,w(pointer)),(A+4,b(state))])
    for raw in (0,0x80000000,0x7F800000,0xFF800000,0x7FC00000,0xFFFFFFFF):
        for coord in range(3):
            add(f'float/{raw:x}/{coord}',0x823B40,[(0x810350+4*coord,w(raw)),(A+0x28,h(10))])
        add(f'anim/result/{raw:x}',0x824240,[(A+5,b(1))],O.Script({0x1C64F0:[raw]}))
    for flag in (0,1,2,3,4,255):
        add(f'prompt/{flag}',0x8243E0,[(A+0xB,b(flag))])
    for entry,fn,patches,changes in (
        (0x823B40,0x1C64F0,[],[(A+0xD,b(0)),(A+0x28,h(1))]),
        (0x823C80,0x1BA1A0,[],[(A+5,b(9))]),
        (0x823C80,0x1BA1F0,[(A+5,b(1))],[(A+6,b(9))]),
        (0x823F60,0x102948,[],[(0x8106C0,w(P)),(P+4,b(3))]),
        (0x824240,0x1BA1F0,[(A+5,b(1))],[(0x28A624,w(0x12345678)),(A+0xD,b(255))]),
        (0x824240,0x1BA580,[(A+5,b(1))],[(A+5,b(9))]),
        (0x824350,0x1BA1F0,[(A+5,b(1))],[(A+5,b(9))]),
        (0x8243E0,0x1BA1F0,[(A+5,b(1))],[(A+0xD,b(0x50))]),
        (0x8243E0,0x1BA580,[],[(A+0x4C,w(0x1C6960))]),
    ):
        add(f'scribble/{entry:x}/{fn:x}',entry,patches,O.Script({fn:[O.Scribble(1,changes)]}))
    # Captured owning actors dispatch these subroutines. Keep their actual
    # actor bytes and select only original handlers, without inventing state.
    for beat,(ram,_) in O.CAPTURES.items():
        for owner,entries in ((0x823850,(0x823B40,0x823C80)),(0x824070,(0x824240,0x824350,0x8243E0))):
            for node in O.owners(ram,owner):
                for entry in entries:
                    out.append((f'capture/{beat}/{entry:x}/{node:x}',beat,entry,(node,),[],[],O.Script(),set()))
    return out


def header_checks():
    header = (ROOT/'src/game/em_area15_route.h').read_text()
    block = header.split('/* BEGIN GENERATED HOOKS */')[1].split('/* END GENERATED HOOKS */')[0]
    found = re.findall(r'int \(\*(w_[0-9A-F]{8})\)\(void \*ctx((?:, [^,)]+)*)\);', block)
    assert [n for n,_ in found] == [h[0] for h in HOOKS]
    types = {U:'uint32_t',I:'int32_t',F:'float'}
    for (name,params),(expected,address,args,result) in zip(found,HOOKS):
        actual = [re.sub(r'\s*\b\w+$','',p.strip()).replace(' ','') for p in params.split(',')[1:]]
        assert actual == [types[k] for k,_ in args] + ([types[result]+'*'] if result else [])
    internal = (ROOT/'src/game/em_area15_route_internal.h').read_text()
    for name,address,_,_ in HOOKS:
        bodies = re.findall(r'static inline int a15r_c_%s\(.*?\n}\n' % name[2:],internal,re.S)
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
    O.NATIVE = O.build(os.environ.get("EM_AREA15_ROUTE_SOURCE"))
    header_checks()
    entry_guards()
    selected = cases()
    only = os.environ.get('EM_AREA15_ROUTE_ONLY')
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
    for label, errors in failures[:int(os.environ.get('EM_AREA15_ROUTE_SHOW', '8'))]: print('FAIL', label, errors)
    assert not failures, f'{len(failures)} failures'
    print(f'PASS {len(selected)} cases: calls, ordered accesses, whole memory, poisoned starts and fail-stop')


if __name__ == '__main__': main()
