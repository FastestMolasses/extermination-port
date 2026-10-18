#!/usr/bin/env python3
"""Level-15 perception entry; all original-code comparisons remain enabled.

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
FUNCS={0x00184D20:('em_level15_perception_00184D20',0x6FC,[U,U,I],I)}
OVERLAY_OF={}
OVERLAY_FILES={4:ROOT.parent/'Extermination/extract/OVERLAY/AREA03.BIN',
               16:ROOT.parent/'Extermination/extract/OVERLAY/AREA19.BIN'}
HOOKS=[
    ('w_0011DF78',0x0011DF78,[('f',12)],'f'),
    ('w_0011E620',0x0011E620,[('f',12),('f',13)],'f'),
    ('w_0011E748',0x0011E748,[('f',12)],'f'),
    ('w_001B1470',0x001B1470,[('f',12)],'f'),
]
# The matching C retains type=b[2]&31 before any worker. Its entry gate
# accepts only 4 or 6, so shape 1's later type==10 block is impossible.
# These five non-branch words belong only to that block. Every class byte
# is exercised below; worker writes to b[2] cannot change the retained type.
DEAD_WORDS={0x18510C,0x185114,0x185118,0x185120,0x185128}

BEATS=('a19e_02_truck','a03_00_elevator')
BEAT=BEATS[-1]
FREE=0x1E61000
A,B=FREE+0x1000,FREE+0x2000


def configure():
    O.FREE=FREE
    O.FUNCS,O.HOOKS=FUNCS,HOOKS
    O.OVERLAY_OF,O.OVERLAY_FILES=OVERLAY_OF,OVERLAY_FILES
    O.OUT=ROOT/'build/levels/level15_perception'
    O.SOURCES=('em_level15_perception.c',)
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
    O.RETURN_SITES=O.RETURN_SITES_FULL=tuple(FUNCS)
    O.CONTRACT_UNREACHABLE={};O.LOCAL_STORES=set()
    O.DEFAULT_ARGS={0x184D20:[A,B,0x20]}
    O.image_for=lambda _:O.CAPTURES[BEAT][0]
    O.CAPTURES.clear();O.CAPTURES.update(O.load_captures(O.read_elf()))
    table=0x26D830; size=6*4; elf=O.read_elf()
    expected=elf[0x300+table-0x100000:0x300+table-0x100000+size]
    for ram,_ in O.CAPTURES.values(): assert ram[table:table+size]==expected
    O.JUMP_TABLES={0x184FDC:(table,6)};O.TABLE_BYTES=frozenset(range(table,table+size))


def cases():
    out=[];b,w,f=O.b8,O.w32,O.f32b;Q=B+0x400
    common=[(A,bytes(0x200)),(B,bytes(0x800)),(B+2,b(4)),(B+3,b(0x36)),(B+0x30,w(Q)),
            (A+0xA0,O.fw(2.,3.,4.,1.)),(B+0xB0,O.fw(6.,7.,8.,1.)),
            (Q,O.fw(30.,20.,10.,40.,50.,0.5)),(A+0xC4,f(0.2)),(B+0xC4,f(0.8))]
    def add(label,patches=(),queues=None,event=0x20,run=()):
        out.append((label,BEAT,0x184D20,(A,B,event),common+list(patches),[],O.Script(queues),set(run)))
    for kind in range(256):
        add(f'class/{kind}',[(B+2,b(kind))])
    for kind in (4,6):
        for event in tuple(range(0x1A,0x2A))+(-1,0x7FFFFFFF):
            for sub in (0,0x12,0x13,0x14,0x21,0x22,0x23,0x24,0x25,0x26,0x2C,0x2F,0x35,0x36,0x49,255):
                add(f'gate/{kind}/{event}/{sub}',[(B+2,b(kind)),(B+3,b(sub))],event=event)
    for shape in (0,1,2,3,4,5,6,255):
        for horizontal,vertical,angle in ((0.,0.,0.),(7.,0.,0.),(7.001,0.,0.),(30.,20.,0.),
                                         (30.001,0.,0.),(0.,50.001,0.),(0.,0.,0.7853982),
                                         (0.,0.,1.5707964),(0.,0.,2.),(float('nan'),0.,0.)):
            add(f'shape/{shape}/{horizontal}/{vertical}/{angle}',[(B+8,b(shape))],
                {0x11E748:[O.fbits(horizontal),O.fbits(vertical)],0x11DF78:[O.fbits(angle)]*2,
                 0x1B1470:[O.fbits(angle)],0x11E620:[O.fbits(0.4)]})
        for height in (-40.,-17.1,-17.,-0.1,0.,20.,20.001,float('nan')):
            add(f'height/{shape}/{height}',[(B+8,b(shape)),(A+0xA4,f(height)),(B+0xB4,f(0.))],
                {0x11E748:[O.fbits(8.),O.fbits(8.)],0x11DF78:[O.fbits(abs(height)),0],0x11E620:[O.fbits(0.4)]})
        for x,z in ((0.,0.),(14.,0.),(14.001,0.),(10.,10.),(0.001,13.999),(float('nan'),1.)):
            add(f'position/{shape}/{x}/{z}',[(B+8,b(shape)),(A+0xA0,f(x)),(A+0xA8,f(z)),(B+0xB0,f(0.)),(B+0xB8,f(0.))],
                {0x11E748:[O.fbits(8.)]*2,0x11DF78:[O.fbits(4.)]*2})
    for result in (3.999,4.,4.001,float('nan')):
        add(f'fixed-height/{result}',[(B+8,b(5))],{0x11DF78:[O.fbits(result)]})
    for shape in range(6):
        add(f'worker/shape/{shape}',[(B+8,b(shape))],
            {0x11E748:[O.Scribble(O.fbits(8.),[(B+2,b(10)),(B+0x30,w(Q+0x40)),
                (Q+0x40,O.fw(40.,30.,20.,50.,60.,0.2)),(A+0xA4,f(3.))]),
                O.Scribble(O.fbits(1.),[(B+0x30,w(Q+0x80)),(Q+0x80,O.fw(10.,20.,30.,40.,50.,0.3))])],
             0x11DF78:[O.Scribble(O.fbits(0.5),[(B+8,b(4)),(B+0xC4,f(2.))])],
             0x1B1470:[O.Scribble(O.fbits(0.1),[(B+0x30,w(Q+0xC0))])]})
    # Real original helpers additionally exercise arithmetic on varied operands.
    for shape in range(6):
        for x,z in ((0.01,0.02),(3.14,7.28),(14.,0.),(5.337,6.148)):
            add(f'math/{shape}/{x}/{z}',[(B+8,b(shape)),(A+0xA0,f(x)),(A+0xA8,f(z))],
                run=(0x11DF78,0x11E620,0x11E748,0x1B1470))
    return out


def header_checks():
    header = (ROOT/'src/game/em_level15_perception.h').read_text()
    block = header.split('/* BEGIN GENERATED HOOKS */')[1].split('/* END GENERATED HOOKS */')[0]
    found = re.findall(r'int \(\*(w_[0-9A-F]{8})\)\(void \*ctx((?:, [^,)]+)*)\);', block)
    assert [n for n,_ in found] == [h[0] for h in HOOKS]
    types = {U:'uint32_t',I:'int32_t',F:'float',Q:'uint64_t'}
    for (name,params),(expected,address,args,result) in zip(found,HOOKS):
        actual = [re.sub(r'\s*\b\w+$','',p.strip()).replace(' ','') for p in params.split(',')[1:]]
        assert actual == [types[k] for k,_ in args] + ([types[result]+'*'] if result else [])
    internal = (ROOT/'src/game/em_level15_perception_internal.h').read_text()
    for name,address,_,_ in HOOKS:
        bodies = re.findall(r'static inline int l15p_c_%s\(.*?\n}\n' % name[2:],internal,re.S)
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
    O.NATIVE = O.build(os.environ.get("EM_LEVEL15_PERCEPTION_SOURCE"))
    header_checks()
    entry_guards()
    selected = cases()
    only = os.environ.get('EM_LEVEL15_PERCEPTION_ONLY')
    if only: selected = [c for c in selected if c[0].startswith(only)]
    O.CASES[:] = selected
    assert len({c[0] for c in selected}) == len(selected)
    results = O.run_cases(selected)
    failures = [(label, errors) for label, _, errors, _, _, _ in results if errors]
    seen = set().union(*(r[3] for r in results))
    for entry, (_, size, _, _) in FUNCS.items():
        words = O.reachable_words(O.image_for(entry), entry, size)
        assert not DEAD_WORDS & seen
        assert DEAD_WORDS <= words
        words -= DEAD_WORDS
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
    for label, errors in failures[:int(os.environ.get('EM_LEVEL15_PERCEPTION_SHOW', '8'))]: print('FAIL', label, errors)
    assert not failures, f'{len(failures)} failures'
    print(f'PASS {len(selected)} cases: calls, ordered accesses, whole memory, poisoned starts and fail-stop')


if __name__ == '__main__': main()
