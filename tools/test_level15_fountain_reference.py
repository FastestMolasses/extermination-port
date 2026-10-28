#!/usr/bin/env python3
"""Two level-15 hazard entries; all original-code comparisons remain enabled.

Unwired. EM_TEST_FULL=1 sweeps all failure sites; diagnostic subsets grant no proof.
"""
import ctypes as C
import os
import re
from pathlib import Path
import test_level14_port_reference as O
import reference_root as refs
ROOT=Path(__file__).resolve().parents[1]
U,I,F,Q,S=O.U,O.I,O.F,O.Q,O.S
FUNCS={
    0x001BECC0:('em_level15_fountain_001BECC0',0x6FC,[U],None),
    0x001BF3C0:('em_level15_fountain_001BF3C0',0x1F0,[U],None),
}
OVERLAY_OF={}
OVERLAY_FILES={4:ROOT.parent/'Extermination/extract/OVERLAY/AREA03.BIN'}
HOOKS=[
    ('w_00187EC0',0x00187EC0,[('i', 4), ('i', 5)],None),
    ('w_001AF890',0x001AF890,[('u', 4)],None),
    ('w_001AFC10',0x001AFC10,[('u', 4)],None),
    ('w_001B1020',0x001B1020,[('u', 4), ('i', 5), ('i', 6), ('i', 7)],'i'),
    ('w_001B1190',0x001B1190,[('i', 4)],None),
    ('w_001B17A0',0x001B17A0,[('u', 4)],None),
    ('w_001BE5F0',0x001BE5F0,[('u', 4), ('u', 5), ('u', 6)],'i'),
    ('w_001BEAC0',0x001BEAC0,[('u', 4), ('u', 5), ('i', 6), ('i', 7)],'u'),
    ('w_001BEB70',0x001BEB70,[('u', 4)],None),
    ('w_001BEC40',0x001BEC40,[('u', 4), ('u', 5), ('i', 6)],None),
    ('w_001C1500',0x001C1500,[('u', 4), ('i', 5), ('f', 12), ('f', 13), ('f', 14)],None),
    ('w_001C1570',0x001C1570,[('u', 4)],'i'),
    ('w_001C62C0',0x001C62C0,[('u', 4)],None),
    ('w_001C6380',0x001C6380,[('u', 4)],None),
    ('w_001D0C80',0x001D0C80,[('u', 4), ('u', 5)],'i'),
    ('w_001D0D40',0x001D0D40,[('u', 4), ('u', 5), ('i', 6), ('i', 7)],None),
    ('w_001D0D60',0x001D0D60,[('u', 4), ('f', 12)],'i'),
    ('w_001EFD90',0x001EFD90,[('i', 4), ('u', 5), ('u', 6)],None),
    ('w_001EFE00',0x001EFE00,[('i', 4), ('u', 5)],None),
    ('w_001FBD50',0x001FBD50,[('u', 4), ('i', 5), ('i', 6), ('f', 12)],None),
 ]
BEATS=('a19e_07_door25','a03_00_elevator')
BEAT=BEATS[-1]
FREE=0x1E61000
A,B=FREE+0x1000,FREE+0x2000


def configure():
    O.FREE=FREE
    O.FUNCS,O.HOOKS=FUNCS,HOOKS
    O.OVERLAY_OF,O.OVERLAY_FILES=OVERLAY_OF,OVERLAY_FILES
    O.OUT=ROOT/'build/levels/level15_fountain'
    O.SOURCES=('em_level15_fountain.c',)
    O.BEATS=BEATS
    O.BEAT_DIRS={b:refs.v263('s87/route_'+b.split('_')[0]+'/'+b,
        'fork route is not recorded from a04b_00; retained original captures explicitly selected') for b in BEATS}
    O.RESIDENT={b:4 for b in BEATS}
    O.BY_ADDRESS={a:(n,args,res) for n,a,args,res in HOOKS}
    O.BY_NAME={n:a for n,a,_,_ in HOOKS}
    O.HOOK_NAMES=[h[0] for h in HOOKS]+['w_callback']
    O.Hooks=type('Hooks',(C.Structure,),{'_fields_':
        [('ctx',C.c_void_p),('bytes',O.BYTES_FN)]+
        [(name,O.hook_proto(args,res)) for name,_,args,res in HOOKS]+[('w_callback',O.CALLBACK_FN)]})
    O.SELF_ENTRIES=set(FUNCS);O.HEAVY=set()
    O.RETURN_SITES=O.RETURN_SITES_FULL=()
    O.CONTRACT_UNREACHABLE={};O.LOCAL_STORES=set()
    O.DEFAULT_ARGS={e:[A] for e in FUNCS}
    O.image_for=lambda _:O.CAPTURES[BEAT][0]
    O.CAPTURES.clear();O.CAPTURES.update(O.load_captures(O.read_elf()))
    O.JUMP_TABLES={};O.TABLE_BYTES=frozenset()


def cases():
    out=[];b,h,w,f=O.b8,O.h16,O.w32,O.f32b
    common=[(A,bytes(0x300)),(B,bytes(0x300)),(A+0x4C,w(0x1C6960)),(A+0x20,w(B)),
            (A+0x34,h(60)),(A+0x64,f(1.)),(0x8102B0,b(1)),(0x8102E8,f(0.5))]
    def add(label,e,patches=(),queues=None):
        out.append((label,BEAT,e,(A,),common+list(patches),[],O.Script(queues),set()))
    scenarios=((0.,0.,0,0,0),(0.5,0.4,1,0,1),(3.5,0.3,1,1,1),(11.7,0.5,1,0,2),(12.5,0.5,0,1,1),(float('nan'),float('nan'),1,0,1))
    for state in (0,1,2,3,4,255):
        for step in (0,1,2,3,4,255):
            for idx,(height,phase,hit,busy,pstate) in enumerate(scenarios):
                add(f'parent/{state}/{step}/{idx}',0x1BECC0,
                    [(A+4,b(state)),(A+5,b(step)),(A+0x38,f(height)),(0x8102E8,f(phase)),(0x8102B0,b(pstate))],
                    {0x1BEAC0:[B if hit else 0],0x1D0C80:[busy],0x1BE5F0:[hit],0x1C1570:[hit],0x1D0D60:[0x1000]})
    for step in (0,1,2,3):
        for timer in (0,1,2,40,32767,32768,65535):
            add(f'parent/timer/{step}/{timer}',0x1BECC0,[(A+4,b(1)),(A+5,b(step)),(A+0x28,h(timer))],{0x1BE5F0:[1]})
    for cooldown in (0,1,2,60,0xFFFFFFFF):
        for damage in (0,1,59,60,61,0x1001,0x4001,0x5001,0x8001,0xFFFF):
            add(f'parent/damage/{cooldown}/{damage}',0x1BECC0,
                [(A+4,b(1)),(A+0x36,h(damage)),(A+0x20C,w(cooldown))])
    for raw in (0,0x80000000,0x3C23D70A,0x3C23D709,0x3C23D70B,0x3F800000,0x7F800000,0xFF800000,0x7FC00000):
        for done in (0,1):
            add(f'parent/fade/{raw:x}/{done}',0x1BECC0,
                [(A+4,b(2)),(A+0x38,w(raw)),(A+0x64,w(raw))],{0x1C1570:[done]})
    for state in (0,1,2,3,4,255):
        for parent in (0,1,2,3):
            for kind in (0,1,2,3,255):
                for height in (0.,5.9,6.1,9.1,12.1,float('nan')):
                    add(f'child/{state}/{parent}/{kind}/{height}',0x1BF3C0,
                        [(A+4,b(state)),(A+0xD,b(kind)),(A+0xA4,f(3.)),(B+4,b(parent)),(B+0x38,f(height))])
    add('parent/worker/create',0x1BECC0,queues={
        0x1BEAC0:[O.Scribble(B,[(A+0x20,w(0)),(A+0xB4,f(9.))])],
        0x1D0C80:[O.Scribble(0,[(0x28A504,w(0x12345678))])]})
    add('parent/worker/rise',0x1BECC0,[(A+4,b(1))],{
        0x1BE5F0:[1],0x187EC0:[O.Scribble(0,[(0x8102E8,f(0.8)),(A+5,b(7))])]})
    add('parent/worker/hit',0x1BECC0,[(A+4,b(1)),(A+5,b(2))],{
        0x1BE5F0:[1],0x1EFD90:[O.Scribble(0,[(A+0x28,h(1)),(0x8102E8,f(0.))])]})
    add('parent/worker/damage',0x1BECC0,[(A+4,b(1)),(A+0x36,h(0x4001))],{
        0x1EFE00:[O.Scribble(0,[(A+0x36,h(2)),(A+0x34,h(1))])]})
    add('parent/worker/fade',0x1BECC0,[(A+4,b(2))],{
        0x1C1570:[O.Scribble(0,[(A+0x64,f(0.005)),(A+0x38,f(0.2))])]})
    add('child/worker/parent',0x1BF3C0,queues={0x1B1020:[O.Scribble(0,
        [(A+0x20,w(B+0x100)),(B+0x104,b(3)),(B+4,b(1)),(B+0x38,f(8.)),(A+0xD,b(2))])]})
    for beat in BEATS:
        ram=O.CAPTURES[beat][0]
        for entry in FUNCS:
            for node in O.owners(ram,entry):
                out.append((f'capture/{beat}/{entry:x}/{node:x}',beat,entry,(node,),[],[],O.Script(),set()))
    return out


def header_checks():
    header = (ROOT/'src/game/em_level15_fountain.h').read_text()
    block = header.split('/* BEGIN GENERATED HOOKS */')[1].split('/* END GENERATED HOOKS */')[0]
    found = re.findall(r'int \(\*(w_[0-9A-F]{8})\)\(void \*ctx((?:, [^,)]+)*)\);', block)
    assert [n for n,_ in found] == [h[0] for h in HOOKS]
    types = {U:'uint32_t',I:'int32_t',F:'float',Q:'uint64_t'}
    for (name,params),(expected,address,args,result) in zip(found,HOOKS):
        actual = [re.sub(r'\s*\b\w+$','',p.strip()).replace(' ','') for p in params.split(',')[1:]]
        assert actual == [types[k] for k,_ in args] + ([types[result]+'*'] if result else [])
    internal = (ROOT/'src/game/em_level15_fountain_internal.h').read_text()
    for name,address,_,_ in HOOKS:
        bodies = re.findall(r'static inline int l15f_c_%s\(.*?\n}\n' % name[2:],internal,re.S)
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
    O.NATIVE = O.build(os.environ.get("EM_LEVEL15_FOUNTAIN_SOURCE"))
    header_checks()
    entry_guards()
    selected = cases()
    only = os.environ.get('EM_LEVEL15_FOUNTAIN_ONLY')
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
    for label, errors in failures[:int(os.environ.get('EM_LEVEL15_FOUNTAIN_SHOW', '8'))]: print('FAIL', label, errors)
    assert not failures, f'{len(failures)} failures'
    print(f'PASS {len(selected)} cases: calls, ordered accesses, whole memory, poisoned starts and fail-stop')


if __name__ == '__main__': main()
