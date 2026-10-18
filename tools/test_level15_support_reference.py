#!/usr/bin/env python3
"""Thirteen unwired level-15 boot routines, using the unchanged instruction oracle.

Internal translated callees execute on both sides, including the render chain.
EM_TEST_FULL=1 enables every failure site. Diagnostic subsets are not receipts.
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
    0x001418F0:('em_level15_support_001418F0',0x134,[U, U],None),
    0x00153EA0:('em_level15_support_00153EA0',0x30,[U],None),
    0x00185420:('em_level15_support_00185420',0xB4,[I],I),
    0x001A91C0:('em_level15_support_001A91C0',0x19C,[U, U, F],None),
    0x001BEB70:('em_level15_support_001BEB70',0xD0,[U],I),
    0x001BEC40:('em_level15_support_001BEC40',0x80,[U, U, I],None),
    0x001C2690:('em_level15_support_001C2690',0xD8,[U, U, U],None),
    0x001CB200:('em_level15_support_001CB200',0xA4,[U, U],None),
    0x001CB2B0:('em_level15_support_001CB2B0',0x8,[U],None),
    0x001D4440:('em_level15_support_001D4440',0x1F8,[I, U, U],None),
    0x001D4640:('em_level15_support_001D4640',0x10,[U, U],None),
    0x001D6580:('em_level15_support_001D6580',0x11C,[I, U, U, I],I),
    0x0020F170:('em_level15_support_0020F170',0x130,[],None),
}
OVERLAY_OF={}
OVERLAY_FILES={4:ROOT.parent/'Extermination/extract/OVERLAY/AREA03.BIN', 12:ROOT.parent/'Extermination/extract/OVERLAY/AREA15.BIN', 16:ROOT.parent/'Extermination/extract/OVERLAY/AREA19.BIN'}
HOOKS=[
    ('w_001026A0',0x001026A0,[('u', 4), ('u', 5), ('u', 6)],None),
    ('w_001028B8',0x001028B8,[('u', 4), ('u', 5), ('u', 6)],None),
    ('w_00102948',0x00102948,[('u', 4), ('u', 5)],None),
    ('w_001029C0',0x001029C0,[('u', 4)],None),
    ('w_00102BB0',0x00102BB0,[('u', 4), ('u', 5), ('f', 12)],None),
    ('w_0011E620',0x0011E620,[('f', 12), ('f', 13)],'f'),
    ('w_001281C0',0x001281C0,[('f', 12)],'i'),
    ('w_00184D20',0x00184D20,[('u', 4), ('u', 5), ('i', 6)],'i'),
    ('w_0019B4C0',0x0019B4C0,[('u', 4), ('u', 5), ('u', 6), ('u', 7)],None),
    ('w_001AFA90',0x001AFA90,[('i', 4)],'u'),
    ('w_001AFC10',0x001AFC10,[('u', 4)],None),
    ('w_001B1190',0x001B1190,[('i', 4)],None),
    ('w_001B1470',0x001B1470,[('f', 12)],'f'),
    ('w_001B3F10',0x001B3F10,[('u', 4), ('f', 12), ('f', 13)],'i'),
    ('w_001C7420',0x001C7420,[('u', 4), ('i', 5), ('i', 6)],None),
    ('w_001CAAC0',0x001CAAC0,[('u', 4), ('u', 5)],None),
    ('w_001D0D40',0x001D0D40,[('u', 4), ('u', 5), ('i', 6), ('i', 7)],None),
    ('w_001D1F20',0x001D1F20,[('i', 4)],None),
    ('w_001D1F80',0x001D1F80,[('i', 4), ('i', 5), ('i', 6)],None),
    ('w_001D1FF0',0x001D1FF0,[('i', 4), ('i', 5)],None),
    ('w_001D2090',0x001D2090,[('i', 4), ('u', 5)],None),
    ('w_001D2910',0x001D2910,[('i', 4)],'i'),
    ('w_001D6930',0x001D6930,[('i', 4), ('i', 5), ('i', 6), ('i', 7), ('u', 8)],None),
    ('w_001D6BA0',0x001D6BA0,[('i', 4), ('i', 5), ('i', 6), ('i', 7), ('i', 8), ('i', 9)],None),
    ('w_001D8C20',0x001D8C20,[('i', 4)],None),
    ('w_00207D00',0x00207D00,[('i', 4), ('i', 5)],None),
    ('w_00207E40',0x00207E40,[('i', 4), ('i', 5), ('i', 6), ('i', 7), ('u', 8), ('u', 9), ('q', 10)],None),
    ('w_0020A7A0',0x0020A7A0,[('q', 4)],None),
 ]
BEATS=('a15b_01_return','a19e_02_truck','a03_00_elevator')
BEAT=BEATS[-1]
FREE=0x1E61000
A,B,T,C0,C1,C2,D,BUF,M=tuple(FREE+n*0x1000 for n in range(1,10))


def configure():
    O.FREE=FREE
    O.FUNCS,O.HOOKS=FUNCS,HOOKS
    O.OVERLAY_OF,O.OVERLAY_FILES=OVERLAY_OF,OVERLAY_FILES
    O.OUT=ROOT/'build/levels/level15_support'
    O.SOURCES=('em_level15_support.c',)
    O.BEATS=BEATS
    O.BEAT_DIRS={b:refs.v263('s87/route_'+b.split('_')[0]+'/'+b,
        'fork route is not recorded from a04b_00; retained original captures explicitly selected') for b in BEATS}
    O.RESIDENT=dict(zip(BEATS,(12,16,4)))
    O.BY_ADDRESS={a:(n,args,res) for n,a,args,res in HOOKS}
    O.BY_NAME={n:a for n,a,_,_ in HOOKS}
    O.HOOK_NAMES=[h[0] for h in HOOKS]
    O.Hooks=type('Hooks',(C.Structure,),{'_fields_':
        [('ctx',C.c_void_p),('bytes',O.BYTES_FN)]+
        [(name,O.hook_proto(args,res)) for name,_,args,res in HOOKS]})
    O.SELF_ENTRIES=O.HEAVY=set()
    O.RETURN_SITES=O.RETURN_SITES_FULL=tuple(e for e,(_,_,_,r) in FUNCS.items() if r is not None)
    O.CONTRACT_UNREACHABLE={};O.LOCAL_STORES=set()
    O.DEFAULT_ARGS={e:[A,B,C0,C1][:len(args)] for e,(_,_,args,_) in FUNCS.items()}
    O.DEFAULT_ARGS.update({0x185420:[0x23],0x1A91C0:[A,B,O.fbits(10.0)],0x1BEC40:[A,B,0],
                          0x1D4440:[3,M,B],0x1D4640:[M,B],0x1D6580:[3,A,B,0x48],0x1CB200:[A,M]})
    O.image_for=lambda _:O.CAPTURES[BEAT][0]
    O.CAPTURES.clear();O.CAPTURES.update(O.load_captures(O.read_elf()))
    # The pinned C identifies the selector's 15-entry dispatch table.
    ram=O.CAPTURES[BEAT][0];table=0x26DAA0
    sites=[pc for pc in range(0x1A91C0,0x1A91C0+412,4)
           if (O.u32(ram,pc)&0xFC00003F)==8 and (O.u32(ram,pc)>>21&31)!=31]
    assert len(sites)==1
    elf=O.read_elf();expected=elf[0x300+table-0x100000:0x300+table-0x100000+60]
    for image,_ in O.CAPTURES.values(): assert image[table:table+60]==expected
    O.JUMP_TABLES={sites[0]:(table,15)}
    O.TABLE_BYTES=frozenset(range(table,table+60))


def cases():
    out=[];b,h,w,f=O.b8,O.h16,O.w32,O.f32b
    common=[(p,bytes(0x400)) for p in (A,B,T,C0,C1,C2,D,BUF,M)]+[
        (0x275B40,w(T)),(T+0x3C,w(C0)),(0x275B5C,w(T)),(0x275B64,h(0)),
        (0x275670,w(D)),(D+0x10,w(BUF)),(D+0x14,w(BUF)),(D+0x18,w(BUF)),(D+0x1C,w(BUF)),
        (0x275B44,w(A)),(M+4,w(3)),(A+0x44,w(M)),(0x275680,w(7))]
    def add(label,e,patches=(),queues=None,args=None,run=()):
        out.append((label,BEAT,e,tuple(args or O.DEFAULT_ARGS[e]),common+list(patches),[],O.Script(queues),set(run)))
    for timer in (0,1,0xFFFF):
        for count in (0,1,178,179,180,32767,32768,65535):
            for phase in (0.0,70.0,70.001,float('inf'),float('nan')):
                add(f'timer/{timer}/{count}/{phase}',0x1418F0,[(B+0x70,h(timer)),(B+0x78,h(count)),(B+0x5C,f(phase))])
    for flag in (0,1,128,255):
        for difficulty in (0,1,2,3,7,255):
            for result in (0,1):
                add(f'turn/{flag}/{difficulty}/{result}',0x1418F0,
                    [(A+0xA,b(flag)),(B+0x78,h(200)),(0x81050C,b(difficulty)),(C0+0xB0,f(2.)),(C0+0xB8,f(3.))],
                    {0x11E620:[O.fbits(2.)],0x1B1470:[O.fbits(0.5)],0x1B3F10:[result]})
    for flag in (0,1,127,128,255):add(f'release/{flag}',0x153EA0,[(A+0x9A,b(flag))])
    for difficulty in range(4):
        for count in (0,1,2,3,6,7,10,11,14,15,32767,65535):
            add(f'turn/boundary/{difficulty}/{count}',0x1418F0,
                [(B+0x78,h(count)),(0x81050C,b(difficulty)),(0x2753C8,bytes((3,7,11,15)))],
                {0x1B3F10:[1]})
    add('turn/worker',0x1418F0,queues={0x1B3F10:[O.Scribble(1,[(B+0x78,h(179)),(0x81050C,b(3))])]})
    for n in (0,1,2,3):
        for rejected in (0,1,2,3):
            for result in (0,1):
                add(f'search/{n}/{rejected}/{result}',0x185420,
                    [(0x275B64,h(n)),(T,w(C0)+w(C1)+w(C2))]+
                    [(p+off,b(value)) for p in (C0,C1,C2) for off,value in
                     ((0,0 if rejected==1 else 1),(2,0 if rejected==2 else 128),(0xB,1 if rejected==3 else 0))],
                    {0x184D20:[0,0,result]})
    for hit in range(3):
        add(f'search/hit/{hit}',0x185420,
            [(0x275B64,h(3)),(T,w(C0)+w(C1)+w(C2))]+
            [(p+off,b(value)) for p in (C0,C1,C2) for off,value in ((0,1),(2,128))],
            {0x184D20:[0]*hit+[O.Scribble(1,[(0x275B64,h(0)),(0x275B5C,w(0))])]})
    add('search/negative-unmapped',0x185420,[(0x275B64,h(65535)),(T,w(0x2000000))])
    for kind in tuple(range(17))+(255,):
        for mode in (0,1,2,65535):
            for raw in (0,0x411FFFFF,0x41200000,0x41200001,0x7FC00000):
                add(f'duration/{kind}/{mode}/{raw:x}',0x1A91C0,[(A+0xD,b(kind)),(A+0x94,h(mode)),(B+0x36,h(0x91AB))],args=[A,B,raw])
    for mask in range(8):
        add(f'spawn/{mask}',0x1BEB70,[(A+0xB0,O.fw(2.,8.,3.,1.)),(A+0xC0,O.fw(1.,2.,3.,4.))],
            {0x1AFA90:[p if mask&(1<<i) else 0 for i,p in enumerate((C0,C1,C2))]},run=(0x102948,))
    add('spawn/parent-alias',0x1BEB70,[(A+0xB0,O.fw(2.,8.,3.,1.))],
        {0x1AFA90:[A,A,A]},run=(0x102948,))
    for flags in (0,0x1000,0xFFFF):
        for old in (0,1,2,0xFFFF):
            for new in (0,1,2,0x10000,0xFFFFFFFF):
                add(f'notify/{flags}/{old}/{new}',0x1BEC40,[(B,h(flags)+h(old))],args=[A,B,new])
    for raw in (0,0x80000000,0x3F800000,0xBF800000,0x7F800000,0x7FC00000):
        add(f'collision/{raw:x}',0x1C2690,[(A+0x70,w(raw)),(A+0x78,f(2.))],{0x11E620:[raw],0x1B1470:[raw]})
    for entry in (0x1CB200,0x1CB2B0,0x1D4440,0x1D4640,0x1D6580):
        for result in (0,1):
            for bank in (0,1):
                add(f'draw/{entry:x}/{result}/{bank}',entry,[(D+0x9C,w(bank)),(B,O.fw(1.,0.5,0.25,1.))],
                    {0x1D2910:[result],0x1281C0:[128,64,32]},run=(0x102948,))
    for worker in (0x1D8C20,0x1C7420,0x1D2090,0x1D6930,0x1D1F80,0x1D1F20,0x1D6BA0,0x1D1FF0,0x1D2910):
        add(f'draw/worker/{worker:x}',0x1CB2B0,
            [(T+0x1C,w(BUF+0x200)),(T+0x9C,w(1))],
            {worker:[O.Scribble(1,[(0x275670,w(T)),(M+4,w(7)),(0x275680,w(9))])]})
    for slot in (0,1,2,3):
        for mode in (0,4,0x48,0xFFFFFFFF):
            add(f'packet/{slot}/{mode}',0x1D6580,[(A,bytes(range(64))),(B,bytes(range(64,128)))],args=[slot,A,B,mode],run=(0x102948,))
    add('fixed/draw',0x20F170)
    # Captured owner records are used directly; workers remain explicit.
    for beat in BEATS:
        ram=O.CAPTURES[beat][0]
        for owner in (0x153950,0x153B50):
            for node in O.owners(ram,owner):
                out.append((f'capture/release/{beat}/{node:x}',beat,0x153EA0,(node,),[],[],O.Script(),set()))
    return out


def header_checks():
    header = (ROOT/'src/game/em_level15_support.h').read_text()
    block = header.split('/* BEGIN GENERATED HOOKS */')[1].split('/* END GENERATED HOOKS */')[0]
    found = re.findall(r'int \(\*(w_[0-9A-F]{8})\)\(void \*ctx((?:, [^,)]+)*)\);', block)
    assert [n for n,_ in found] == [h[0] for h in HOOKS]
    types = {U:'uint32_t',I:'int32_t',F:'float',Q:'uint64_t'}
    for (name,params),(expected,address,args,result) in zip(found,HOOKS):
        actual = [re.sub(r'\s*\b\w+$','',p.strip()).replace(' ','') for p in params.split(',')[1:]]
        assert actual == [types[k] for k,_ in args] + ([types[result]+'*'] if result else [])
    internal = (ROOT/'src/game/em_level15_support_internal.h').read_text()
    for name,address,_,_ in HOOKS:
        bodies = re.findall(r'static inline int l15s_c_%s\(.*?\n}\n' % name[2:],internal,re.S)
        assert len(bodies)==1 and f'o->h->{name}(' in bodies[0] and f'0x{address:08X}u' in bodies[0]


def entry_guards():
    touched=[]
    def refuse(*_): touched.append(1); return -1
    keep=[O.BYTES_FN(refuse)]
    fields={'ctx':None,'bytes':keep[0]}
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
    O.NATIVE = O.build(os.environ.get("EM_LEVEL15_SUPPORT_SOURCE"))
    header_checks()
    entry_guards()
    selected = cases()
    only = os.environ.get('EM_LEVEL15_SUPPORT_ONLY')
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
    for label, errors in failures[:int(os.environ.get('EM_LEVEL15_SUPPORT_SHOW', '8'))]: print('FAIL', label, errors)
    assert not failures, f'{len(failures)} failures'
    print(f'PASS {len(selected)} cases: calls, ordered accesses, whole memory, poisoned starts and fail-stop')


if __name__ == '__main__': main()
