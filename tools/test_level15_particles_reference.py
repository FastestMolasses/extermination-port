#!/usr/bin/env python3
"""Level-15 particle entry; all original-code comparisons remain enabled.

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
FUNCS={0x0015A750:('em_level15_particles_0015A750',0x3A0,[U,S,I],None)}
OVERLAY_OF={}
OVERLAY_FILES={4:ROOT.parent/'Extermination/extract/OVERLAY/AREA03.BIN',16:ROOT.parent/'Extermination/extract/OVERLAY/AREA19.BIN'}
HOOKS=[
    ('w_00102918',0x00102918,[('u', 4), ('u', 5), ('u', 6)],None),
    ('w_001029C0',0x001029C0,[('u', 4)],None),
    ('w_001CCF70',0x001CCF70,[('u', 4)],'i'),
    ('w_001CFAE0',0x001CFAE0,[('u', 4), ('i', 5), ('u', 6), ('f', 12), ('f', 13), ('f', 14), ('f', 15)],None),
    ('w_001CFBE0',0x001CFBE0,[('i', 4), ('u', 5), ('u', 6), ('u', 7), ('i', 8)],None),
]
ORIGINAL_START=O.start
BEATS=('a19e_01_ceiling','a03_00_elevator')
BEAT=BEATS[-1]
FREE=0x1E61000
A,B=FREE+0x1000,FREE+0x2000


def configure():
    def start_with_context(ee,entry,args):
        # The original's out-of-range path retains incoming s1 as the count.
        # This explicit native-only context does not replace any original code.
        ee.r[17]=O.sx32(args[2])
        return ORIGINAL_START(ee,entry,args[:2])
    O.start=start_with_context
    O.FREE=FREE
    O.FUNCS,O.HOOKS=FUNCS,HOOKS
    O.OVERLAY_OF,O.OVERLAY_FILES=OVERLAY_OF,OVERLAY_FILES
    O.OUT=ROOT/'build/levels/level15_particles'
    O.SOURCES=('em_level15_particles.c',)
    O.BEATS=BEATS
    O.BEAT_DIRS={b:refs.v263('s87/route_'+b.split('_')[0]+'/'+b,
        'fork route is not recorded from a04b_00; retained original captures explicitly selected') for b in BEATS}
    O.RESIDENT={b:4 if b.startswith('a03') else 16 for b in BEATS}
    O.BY_ADDRESS={a:(n,args,res) for n,a,args,res in HOOKS}
    O.BY_NAME={n:a for n,a,_,_ in HOOKS}
    O.HOOK_NAMES=[h[0] for h in HOOKS]
    O.Hooks=type('Hooks',(C.Structure,),{'_fields_':
        [('ctx',C.c_void_p),('bytes',O.BYTES_FN)]+
        [(name,O.hook_proto(args,res)) for name,_,args,res in HOOKS]+[('w_callback',O.CALLBACK_FN)]})
    O.SELF_ENTRIES=set();O.HEAVY=set()
    O.RETURN_SITES=O.RETURN_SITES_FULL=()
    O.CONTRACT_UNREACHABLE={};O.LOCAL_STORES=set()
    O.DEFAULT_ARGS={0x15A750:[A,O.STACK_TOP,0]}
    O.image_for=lambda _:O.CAPTURES[BEAT][0]
    O.CAPTURES.clear();O.CAPTURES.update(O.load_captures(O.read_elf()))
    table=0x26D390; size=7*4; elf=O.read_elf()
    expected=elf[0x300+table-0x100000:0x300+table-0x100000+size]
    for ram,_ in O.CAPTURES.values(): assert ram[table:table+size]==expected
    O.JUMP_TABLES={0x15A804:(table,7)};O.TABLE_BYTES=frozenset(range(table,table+size))


def cases():
    out=[];h,w,f=O.h16,O.w32,O.f32b
    common=[(A,bytes(0x300)),(A+0x20,f(1.)),(A+0x1F4,w(0x12345678))]
    def add(label,patches=(),queues=None,count=0,run=()):
        out.append((label,BEAT,0x15A750,(A,O.STACK_TOP,count),common+list(patches),[],O.Script(queues),set(run)))
    for kind in range(7):
        for rng in (0,0xFFFF0000,0x89ABCDEF):
            for phase in (0.,1.4999,1.99,2.,float('nan')):
                for dt in (0.,60.):
                    add(f'emit/{kind}/{rng}/{phase}/{dt}',[(A+0x54,h(kind)),(A+0x1F4,w(rng)),(A+0x1F0,f(phase)),(A+0x20,f(dt))],
                        {0x1CCF70:[0x13579]},count=7)
    for kind in (7,35,0x8000,0xFFFF):
        for count in (-3,0,1,2,5):
            add(f'incoming/{kind}/{count}',[(A+0x54,h(kind)),(A+0x1F0,f(2.))],count=count)
    for kind in range(7):
        add(f'worker/{kind}',[(A+0x54,h(kind))],
            {0x1029C0:[O.Scribble(0,[(0x700036D4,f(5.))])],
             0x102918:[O.Scribble(0,[(0x700036D4,f(8.))])],
             0x1CCF70:[O.Scribble(0x567,[(A+0x1F4,w(0x87654321)),(A+0x20,f(30.))])],
             0x1CFAE0:[O.Scribble(0,[(O.STACK_TOP-0x60,bytes(range(0x60))),
                                     (A+0x1F0,f(1.2)),(A+0x54,h((kind+1)%7))])],
             0x1CFBE0:[O.Scribble(0,[(A+0x1F0,f(1.5)),(A+0x1F4,w(0xAAAAAAAA)),(A+0x20,f(90.))])]})
        add(f'matrix/{kind}',[(A+0x54,h(kind)),(A+0xB0,O.fw(2.,3.,4.,1.))],run=(0x1029C0,0x102918))
    for beat,(ram,_) in O.CAPTURES.items():
        for node in O.owners(ram,0x15A2C0):
            out.append((f'captured/{beat}/{node:x}',beat,0x15A750,(node,O.STACK_TOP,0),[],[],O.Script(),set()))
    return out


def header_checks():
    header = (ROOT/'src/game/em_level15_particles.h').read_text()
    block = header.split('/* BEGIN GENERATED HOOKS */')[1].split('/* END GENERATED HOOKS */')[0]
    found = re.findall(r'int \(\*(w_[0-9A-F]{8})\)\(void \*ctx((?:, [^,)]+)*)\);', block)
    assert [n for n,_ in found] == [h[0] for h in HOOKS]
    types = {U:'uint32_t',I:'int32_t',F:'float',Q:'uint64_t'}
    for (name,params),(expected,address,args,result) in zip(found,HOOKS):
        actual = [re.sub(r'\s*\b\w+$','',p.strip()).replace(' ','') for p in params.split(',')[1:]]
        assert actual == [types[k] for k,_ in args] + ([types[result]+'*'] if result else [])
    internal = (ROOT/'src/game/em_level15_particles_internal.h').read_text()
    for name,address,_,_ in HOOKS:
        bodies = re.findall(r'static inline int l15s_c_%s\(.*?\n}\n' % name[2:],internal,re.S)
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
    O.NATIVE = O.build(os.environ.get("EM_LEVEL15_PARTICLES_SOURCE"))
    header_checks()
    entry_guards()
    selected = cases()
    only = os.environ.get('EM_LEVEL15_PARTICLES_ONLY')
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
    for label, errors in failures[:int(os.environ.get('EM_LEVEL15_PARTICLES_SHOW', '8'))]: print('FAIL', label, errors)
    assert not failures, f'{len(failures)} failures'
    print(f'PASS {len(selected)} cases: calls, ordered accesses, whole memory, poisoned starts and fail-stop')


if __name__ == '__main__': main()
