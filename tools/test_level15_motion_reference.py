#!/usr/bin/env python3
"""Three level-15 movement entries; all original-code comparisons remain enabled.

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
    0x0013D600:('em_level15_motion_0013D600',0x250,[U, U, S],None),
    0x00140C30:('em_level15_motion_00140C30',0x348,[U, U, S],None),
    0x00140F80:('em_level15_motion_00140F80',0x744,[U, U],None),
}
OVERLAY_OF={}
OVERLAY_FILES={4:ROOT.parent/'Extermination/extract/OVERLAY/AREA03.BIN'}
HOOKS=[
    ('w_001026A0',0x001026A0,[('u', 4), ('u', 5), ('u', 6)],None),
    ('w_001028B8',0x001028B8,[('u', 4), ('u', 5), ('u', 6)],None),
    ('w_00102948',0x00102948,[('u', 4), ('u', 5)],None),
    ('w_001029C0',0x001029C0,[('u', 4)],None),
    ('w_00102BB0',0x00102BB0,[('u', 4), ('u', 5), ('f', 12)],None),
    ('w_0011DE90',0x0011DE90,[('f', 12)],'f'),
    ('w_0011E0A8',0x0011E0A8,[('u', 4), ('f', 12)],None),
    ('w_0011E2A8',0x0011E2A8,[('f', 12)],'f'),
    ('w_00122BB8',0x00122BB8,[],'i'),
    ('w_001281C0',0x001281C0,[('f', 12)],'i'),
    ('w_00131ED0',0x00131ED0,[('u', 4)],None),
    ('w_0013D850',0x0013D850,[('u', 4), ('u', 5)],None),
    ('w_0013D980',0x0013D980,[('u', 4), ('u', 5)],None),
    ('w_0013DB70',0x0013DB70,[('u', 4), ('u', 5)],None),
    ('w_0013DC60',0x0013DC60,[('u', 4), ('u', 5)],None),
    ('w_0013DD40',0x0013DD40,[('u', 4), ('u', 5)],None),
    ('w_0013E6D0',0x0013E6D0,[('u', 4), ('u', 5)],None),
    ('w_0013EB90',0x0013EB90,[('u', 4), ('u', 5)],None),
    ('w_0013EDB0',0x0013EDB0,[('u', 4), ('u', 5)],None),
    ('w_0013F240',0x0013F240,[('u', 4), ('u', 5)],None),
    ('w_0013F770',0x0013F770,[('u', 4), ('u', 5)],None),
    ('w_00140440',0x00140440,[('u', 4), ('u', 5)],None),
    ('w_00141A30',0x00141A30,[('u', 4), ('u', 5)],None),
    ('w_00141C90',0x00141C90,[('u', 4), ('u', 5)],None),
    ('w_0019AB20',0x0019AB20,[('u', 4), ('u', 5), ('u', 6), ('u', 7)],'i'),
    ('w_0019B6C0',0x0019B6C0,[('u', 4), ('u', 5)],'i'),
    ('w_001B1560',0x001B1560,[('u', 4), ('u', 5), ('f', 12)],'i'),
    ('w_001B15D0',0x001B15D0,[('u', 4), ('u', 5)],'f'),
    ('w_001B17A0',0x001B17A0,[('u', 4)],'i'),
    ('w_001B2140',0x001B2140,[('u', 4)],'i'),
    ('w_001B2F70',0x001B2F70,[('u', 4), ('u', 5)],'i'),
    ('w_001B32F0',0x001B32F0,[('u', 4), ('u', 5), ('f', 12)],'i'),
    ('w_001B3390',0x001B3390,[('u', 4), ('u', 5), ('u', 6), ('f', 12)],'i'),
    ('w_001B3440',0x001B3440,[('u', 4), ('u', 5), ('u', 6), ('f', 12)],'i'),
    ('w_001B4810',0x001B4810,[('u', 4)],None),
    ('w_001C6160',0x001C6160,[('u', 4)],'i'),
    ('w_001C64F0',0x001C64F0,[('u', 4), ('f', 12)],'i'),
    ('w_001C68C0',0x001C68C0,[('u', 4)],None),
    ('w_001EFD90',0x001EFD90,[('i', 4), ('u', 5), ('u', 6)],None),
 ]
BEATS=('a19e_07_door25','a03_00_elevator')
BEAT=BEATS[-1]
FREE=0x1E61000
A,B=FREE+0x1000,FREE+0x2000


def configure():
    O.FREE=FREE
    O.FUNCS,O.HOOKS=FUNCS,HOOKS
    O.OVERLAY_OF,O.OVERLAY_FILES=OVERLAY_OF,OVERLAY_FILES
    O.OUT=ROOT/'build/levels/level15_motion'
    O.SOURCES=('em_level15_motion.c',)
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
    O.SELF_ENTRIES={0x13D600};O.HEAVY=set()
    O.RETURN_SITES=O.RETURN_SITES_FULL=()
    O.CONTRACT_UNREACHABLE={};O.LOCAL_STORES=set()
    O.DEFAULT_ARGS={e:[A,B]+([O.STACK_TOP] if e!=0x140F80 else []) for e in FUNCS}
    O.image_for=lambda _:O.CAPTURES[BEAT][0]
    O.CAPTURES.clear();O.CAPTURES.update(O.load_captures(O.read_elf()))
    # The matching C names this eleven-state dispatcher table.
    table=0x26D210; elf=O.read_elf(); expected=elf[0x300+table-0x100000:0x300+table-0x100000+44]
    for ram,_ in O.CAPTURES.values(): assert ram[table:table+44]==expected
    O.JUMP_TABLES={0x13D6A4:(table,11)};O.TABLE_BYTES=frozenset(range(table,table+44))


def cases():
    out=[];b,h,w,f=O.b8,O.h16,O.w32,O.f32b
    target=B+0x400
    common=[(A,bytes(0x300)),(B,bytes(0x800)),(A+0x4C,w(0x1C6960)),(A+0xB0,O.fw(2.,3.,4.,1.)),
            (B+0x40,f(1.)),(B+0x5C,f(20.)),(0x700031D0,w(target)),(target+0x1A,b(0x5B))]
    def add(label,e,patches=(),queues=None,run=()):
        all_patches=common+list(patches)
        out.append((label,BEAT,e,tuple(O.DEFAULT_ARGS[e]),
                    [(a,v) for a,v in all_patches if a<0x70000000],
                    [(a,v) for a,v in all_patches if a>=0x70000000],O.Script(queues),set(run)))
    math=(0x11E0A8,0x1281C0)
    for state in tuple(range(12))+(255,):
        for active,flag,alert in ((0,0,0),(1,0,0),(1,1,0),(1,0,1),(1,2,2)):
            add(f'driver/{state}/{active}/{flag}/{alert}',0x13D600,
                [(A+5,b(state)),(B+0x88,b(flag)),(B+0x8D,b(alert))],
                {0x1B2140:[active],0x1B17A0:[alert],0x1B15D0:[O.fbits(20.)]})
    for state in (0,1,2,3,127,128,129,130,255):
        for distance,hit in ((0.,0),(12.5,0),(12.5,1),(12.501,1),(float('nan'),1)):
            add(f'move/{state}/{distance}/{hit}',0x140C30,
                [(B+0x81,b(state)),(B+0x5C,f(distance)),(A+0x2C,h(0)),(B+0x4C,f(2.)),(B+0x50,f(0.5))],
                {0x1B1560:[hit],0x11E2A8:[O.fbits(0.3)],0x11DE90:[O.fbits(0.8)]})
    for kind in (0,3,4,5,8,19,21,27,35,36,0x8000,0xFFFF):
        for time,speed in ((0.,0.),(0.,1.),(7.2,1.),(7.2,4.),(1.,2.5)):
            add(f'move/table/{kind}/{time}/{speed}',0x140C30,
                [(B+0x81,b(129)),(A+0x2C,h(kind)),(A+0x3C,f(time)),(B+0x40,f(speed))],
                {0x1C6160:[8]*20,0x11E2A8:[O.fbits(0.3)]*20,0x11DE90:[O.fbits(0.8)]*20},math)
    for raw in (0,0x80000000,0x3F800000,0xBF800000,0x7F800000,0xFF800000,0x7FC00000):
        for result in (0,1):
            add(f'facing/{raw:x}/{result}',0x140F80,
                [(A+4,b(1)),(B+0x48,w(raw)),(B+0x4C,f(1.))],
                {0x1B3390:[result]*4,0x1B32F0:[result]*4,0x1B3440:[result]*5,
                 0x1B2F70:[result],0x19B6C0:[result],0x19AB20:[result]})
    for helper,n in ((0x1B3390,4),(0x1B32F0,4),(0x1B3440,5)):
        for index in range(n):
            add(f'facing/hit/{helper:x}/{index}',0x140F80,
                [(A+4,b(1)),(B+0x48,f(0.5 if helper==0x1B32F0 else -0.5))],{helper:[0]*index+[1]})
    for state,gate,height,phase in ((0,0,0.,-1.),(1,1,0.,-1.),(1,0,0.,-0.2),(1,0,0.,-0.3),(1,0,0.,-0.301),(1,0,14.,-1.)):
        for hit in (0,1):
            add(f'facing/fall/{state}/{gate}/{height}/{phase}/{hit}',0x140F80,
                [(A+4,b(state)),(B+0x85,b(gate)),(B+0x48,f(phase)),(0x700038A0,f(height))],{0x1B2F70:[hit]})
    for kind in (0,0x5A,0x5B,0x5C):
        for timer,speed in ((0,0.),(0,1.),(1,1.),(255,1.),(0,float('nan'))):
            add(f'facing/water/{kind}/{timer}/{speed}',0x140F80,
                [(B+0x48,f(-1.)),(target+0x1A,b(kind)),(B+0x87,b(timer)),(B+0x4C,f(speed))],
                {0x19B6C0:[1],0x122BB8:[0x7FFFFFFF]})
    add('driver/table',0x13D600,[(B+0x81,b(129)),(A+0x2C,h(3)),(A+0x3C,f(7.2)),(B+0x40,f(4.))],
        {0x1B2140:[1],0x1C6160:[8]*20},math)
    add('driver/worker/state',0x13D600,queues={0x1B2140:[1],0x13D850:[O.Scribble(0,
        [(B+0x88,b(3)),(B+0x8D,b(2)),(B+0x81,b(2))])]})
    add('driver/worker/motion',0x13D600,queues={0x1B2140:[1],0x1C6160:[8]*20,
        0x141A30:[O.Scribble(0,[(B+0x81,b(129)),(A+0x2C,h(3)),(A+0x3C,f(7.2)),(B+0x40,f(4.))])]},run=math)
    for helper in (0x11E2A8,0x11DE90):
        add(f'move/worker/{helper:x}',0x140C30,[(B+0x81,b(128))],
            {helper:[O.Scribble(O.fbits(0.5),[(B+0x4C,f(3.)),(A+0xB0,O.fw(6.,7.,8.,1.))])]})
    add('facing/worker/phase',0x140F80,[(B+0x48,f(1.))],
        {0x1B32F0:[O.Scribble(0,[(B+0x48,f(-1.))])]})
    add('facing/worker/flag',0x140F80,[(B+0x48,f(-1.))],
        {0x1B3390:[O.Scribble(0,[(B+0x80,b(1))])]})
    add('facing/worker/water',0x140F80,[(B+0x48,f(-1.))],
        {0x19B6C0:[O.Scribble(1,[(0x700031D0,w(target)),(0x700031B4,f(7.)),
                                (B+0x87,b(0)),(B+0x4C,f(1.))])],
         0x122BB8:[O.Scribble(0x7FFFFFFF,[(A+0xB0,O.fw(6.,7.,8.,1.)),(B+0x60,f(9.))])]})
    return out


def header_checks():
    header = (ROOT/'src/game/em_level15_motion.h').read_text()
    block = header.split('/* BEGIN GENERATED HOOKS */')[1].split('/* END GENERATED HOOKS */')[0]
    found = re.findall(r'int \(\*(w_[0-9A-F]{8})\)\(void \*ctx((?:, [^,)]+)*)\);', block)
    assert [n for n,_ in found] == [h[0] for h in HOOKS]
    types = {U:'uint32_t',I:'int32_t',F:'float',Q:'uint64_t'}
    for (name,params),(expected,address,args,result) in zip(found,HOOKS):
        actual = [re.sub(r'\s*\b\w+$','',p.strip()).replace(' ','') for p in params.split(',')[1:]]
        assert actual == [types[k] for k,_ in args] + ([types[result]+'*'] if result else [])
    internal = (ROOT/'src/game/em_level15_motion_internal.h').read_text()
    for name,address,_,_ in HOOKS:
        bodies = re.findall(r'static inline int l15m_c_%s\(.*?\n}\n' % name[2:],internal,re.S)
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
    O.NATIVE = O.build(os.environ.get("EM_LEVEL15_MOTION_SOURCE"))
    header_checks()
    entry_guards()
    selected = cases()
    only = os.environ.get('EM_LEVEL15_MOTION_ONLY')
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
    for label, errors in failures[:int(os.environ.get('EM_LEVEL15_MOTION_SHOW', '8'))]: print('FAIL', label, errors)
    assert not failures, f'{len(failures)} failures'
    print(f'PASS {len(selected)} cases: calls, ordered accesses, whole memory, poisoned starts and fail-stop')


if __name__ == '__main__': main()
