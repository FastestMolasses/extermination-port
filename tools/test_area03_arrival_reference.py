#!/usr/bin/env python3
"""Remaining AREA03 arrivals: seven UNWIRED original-instruction entries.

The existing comparator is unchanged. EM_TEST_FULL=1 sweeps all failure sites;
EM_AREA03_ARRIVAL_ONLY selects diagnostics and never certifies coverage.
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
    0x00824D30:('em_area03_arrival_00824D30',0x4D0,[U, U, S],I),
    0x00825430:('em_area03_arrival_00825430',0x2A8,[U],None),
    0x008256E0:('em_area03_arrival_008256E0',0x298,[U],None),
    0x00825980:('em_area03_arrival_00825980',0x470,[U],None),
    0x00825DF0:('em_area03_arrival_00825DF0',0x478,[U],None),
    0x00826340:('em_area03_arrival_00826340',0x290,[U],None),
    0x008265D0:('em_area03_arrival_008265D0',0x290,[U],None),
}
OVERLAY_OF={entry:4 for entry in FUNCS}
OVERLAY_FILES={4:ROOT.parent/'Extermination/extract/OVERLAY/AREA03.BIN'}
HOOKS=[
    ('w_001026A0',0x001026A0,[('u', 4), ('u', 5), ('u', 6)],None),
    ('w_00102738',0x00102738,[('u', 4), ('u', 5)],'f'),
    ('w_001028B8',0x001028B8,[('u', 4), ('u', 5), ('u', 6)],None),
    ('w_001028D0',0x001028D0,[('u', 4), ('u', 5), ('u', 6)],None),
    ('w_00102948',0x00102948,[('u', 4), ('u', 5)],None),
    ('w_001031E0',0x001031E0,[('u', 4), ('u', 5)],None),
    ('w_00122BB8',0x00122BB8,[],'i'),
    ('w_0019A570',0x0019A570,[('u', 4), ('u', 5), ('i', 6), ('i', 7)],'i'),
    ('w_0019AA80',0x0019AA80,[('u', 4), ('u', 5), ('i', 6)],'i'),
    ('w_001AFC10',0x001AFC10,[('u', 4)],None),
    ('w_001B0FD0',0x001B0FD0,[('u', 4)],'i'),
    ('w_001B17A0',0x001B17A0,[('u', 4)],'i'),
    ('w_001B6F00',0x001B6F00,[('u', 4), ('u', 5), ('f', 12)],None),
    ('w_001BA1A0',0x001BA1A0,[('u', 4), ('u', 5)],None),
    ('w_001BA1F0',0x001BA1F0,[('u', 4)],'i'),
    ('w_001C6380',0x001C6380,[('u', 4)],None),
    ('w_001CD520',0x001CD520,[('i', 4), ('i', 5), ('u', 6), ('q', 7), ('u', 8), ('f', 12), ('f', 13), ('f', 14)],'i'),
    ('w_001E2BA0',0x001E2BA0,[('u', 4), ('u', 5), ('u', 6), ('f', 12)],None),
    ('w_001F4BF0',0x001F4BF0,[('u', 4), ('u', 5)],None),
    ('w_001F4E20',0x001F4E20,[('u', 4), ('u', 5), ('f', 12)],None),
    ('w_001FBD50',0x001FBD50,[('u', 4), ('i', 5), ('i', 6), ('f', 12)],'i'),
 ]
BEATS=('a19e_07_door25','a03_00_elevator')
BEAT=BEATS[0]
FREE=0x1E61000
A,P,B0,B1,M,H=tuple(FREE+n for n in (0x1000,0x2000,0x3000,0x4000,0x5000,0x6000))


def configure():
    O.FREE=FREE
    O.FUNCS,O.HOOKS=FUNCS,HOOKS
    O.OVERLAY_OF,O.OVERLAY_FILES=OVERLAY_OF,OVERLAY_FILES
    O.OUT=ROOT/'build/levels/area03_arrival'
    O.SOURCES=('em_area03_arrival_probe.c','em_area03_arrival_actors.c')
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
    O.SELF_ENTRIES=set(FUNCS)-{0x824D30};O.HEAVY=set()
    O.RETURN_SITES=O.RETURN_SITES_FULL=(0x824D30,)
    O.CONTRACT_UNREACHABLE={};O.LOCAL_STORES=set()
    O.DEFAULT_ARGS={e:([A,M,O.STACK_TOP] if e==0x824D30 else [A]) for e in FUNCS}
    O.image_for=lambda _:O.CAPTURES[BEAT][0]
    O.CAPTURES.clear();O.CAPTURES.update(O.load_captures(O.read_elf()))
    # Pinned matching C identifies the two six-entry dispatch tables. Verify
    # the loaded table data before replacing native dispatch with a C switch.
    ram=O.CAPTURES[BEAT][0];data=OVERLAY_FILES[4].read_bytes();O.JUMP_TABLES={}
    for entry,size,table in ((0x825980,0x470,0x829080),(0x825DF0,0x478,0x8290A0)):
        sites=[pc for pc in range(entry,entry+size,4)
               if (O.u32(ram,pc)&0xFC00003F)==8 and (O.u32(ram,pc)>>21&31)!=31]
        assert len(sites)==1
        O.JUMP_TABLES[sites[0]]=(table,6)
        for image,_ in O.CAPTURES.values():
            assert image[table:table+24]==data[table-O.ARENA:table-O.ARENA+24]
    O.TABLE_BYTES=frozenset(a for table,n in O.JUMP_TABLES.values() for a in range(table,table+4*n))


def cases():
    out=[];b,h,w,f=O.b8,O.h16,O.w32,O.f32b
    common=[(A,bytes(0x300)),(P,bytes(0x100)),(B0,bytes(0x200)),(B1,bytes(0x200)),
            (H,bytes(0x100)),(M,O.fw(1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.)),
            (A+0x4C,w(0x1C6960)),(0x275B40,w(P)),(P+4,w(B0)),(P+8,w(B1)),(P+12,w(B1))]
    def add(label,entry,patches=(),spad=(),queues=None):
        out.append((label,BEAT,entry,tuple(O.DEFAULT_ARGS[entry]),common+list(patches),list(spad),O.Script(queues),set()))
    for entry in (0x825430,0x8256E0):
        for state in (0,1,2,3,4,255):
            for step in (0,1,2,255):
                for flag in (0,1,2):
                    for work in (0,1):
                        add(f'gate/{entry:x}/{state}/{step}/{flag}/{work}',entry,
                            [(A+4,b(state)),(A+5,b(step)),(0x275CA0,w(flag)),(A+0x1F0,w(work)),(A+0x1F4,f(30.0))])
        for raw in (0,0x80000000,0x3F800000,0x426BFFFF,0x42700000,0x7F800000,0xFF800000,0x7FC00000):
            for work in (0,1):
                add(f'gate/phase/{entry:x}/{raw:x}/{work}',entry,
                    [(A+4,b(1)),(A+5,b(1)),(A+0x1F0,w(work)),(A+0x1F4,w(raw))])
        add(f'gate/busy/{entry:x}',entry,queues={0x1B0FD0:[1]})
        for work in (0,1):
            # The first bone store aliases the phase; the second position
            # must use the phase reloaded after that store.
            add(f'gate/alias/{entry:x}/{work}',entry,
                [(A+4,b(1)),(A+5,b(1)),(A+0x1F0,w(work)),(A+0x1F4,f(30.0)),
                 (P+4,w(A+0x1F4-0x7C))])
        for flag,work in ((0,0),(0,1),(1,0),(1,1)):
            add(f'gate/worker/{entry:x}/{flag}/{work}',entry,
                [(A+4,b(1)),(0x275CA0,w(entry==0x8256E0))],
                queues={0x1B17A0:[O.Scribble(0,[(0x275CA0,w(flag)),(A+0x1F0,w(work))])]})
    for entry in (0x825980,0x825DF0,0x826340,0x8265D0):
        for state in (0,1,2,3,255):
            for step in (0,1,2,255):
                for flag,use,result in ((0,0,0),(0,4,1),(1,0,1),(1,4,0)):
                    add(f'lift/{entry:x}/{state}/{step}/{flag}/{use}/{result}',entry,
                        [(A+4,b(state)),(A+5,b(step)),(A+0xB,b(use)),(0x275CA0,w(flag))],
                        [(0x70003B92,b(flag))],{0x1B0FD0:[result],0x1BA1F0:[result],0x1B17A0:[result]})
        for timer in (0,1,118,119,120,121,32767,32768,65535):
            add(f'lift/timer/{entry:x}/{timer}',entry,[(A+4,b(1)),(A+5,b(1)),(A+0x28,h(timer))])
        for worker in (0x1BA1A0,0x1B6F00,0x1BA1F0,0x1B17A0,0x1FBD50):
            add(f'lift/worker/{entry:x}/{worker:x}',entry,
                [(A+4,b(1)),(A+5,b(worker not in (0x1BA1A0,0x1B6F00))),
                 (A+0xB,b(4)),(A+0x28,h(119)),(0x275CA0,w(0))],
                queues={worker:[O.Scribble(1,[(0x275CA0,w(1)),(0x275CA4,w(4)),
                                             (0x70003B92,b(1)),(A+0x28,h(50))])]})
    for entry in (0x825980,0x825DF0):
        for angle in (-3.0,-2.4434612,-2.268928,0.0,0.69813174,0.87266463,1.0):
            for pick in (0,1,2,3,4,5,6,0xFFFFFFFF):
                add(f'angle/{entry:x}/{angle}/{pick}',entry,
                    [(A+4,b(1)),(0x8106A0,f(angle)),(0x275CA4,w(pick))])
    def hit(result,kind,obj=H):
        return O.Scribble(result,[(0x700031D8,w(kind)),(0x700031D4,w(obj)),(0x700031D0,w(H)),
                                 (0x700031B0,O.fw(3.0,4.0,5.0,1.0))])
    for state,counter,target in ((4,0,0),(1,12,0),(1,13,0),(1,13,H),(1,0xFFFFFFFF,0)):
        for first,second,kind in ((0,0,0),(1,0,1),(1,0,2),(0,1,1),(0,1,2)):
            for subtype in (0,0xF,0x10,0x13,0x14):
                add(f'probe/{state}/{counter}/{target}/{first}/{second}/{kind}/{subtype}',0x824D30,
                    [(A+4,b(state)),(A+0x200,w(counter)),(A+0x204,w(target)),(H+3,b(subtype))],
                    queues={0x19AA80:[hit(first,kind)],0x19A570:[hit(second,kind)],0x122BB8:[0x7FFFFFFF]})
    for raw in (0,0x461C3FFF,0x461C4000,0x461C4001,0x7F800000,0x7FC00000):
        add(f'probe/distance/{raw:x}',0x824D30,[(A+4,b(4))],
            queues={0x19AA80:[hit(0,1)],0x19A570:[hit(1,1)],0x102738:[raw]})
    for beat in BEATS:
        ram=O.CAPTURES[beat][0]
        for entry in set(FUNCS)-{0x824D30}:
            for node in O.owners(ram,entry):
                out.append((f'capture/{beat}/{entry:x}/{node:x}',beat,entry,(node,),
                            [(0x275B40,w(node+0x110))],[],O.Script(),set()))
    return out


def header_checks():
    header = (ROOT/'src/game/em_area03_arrival.h').read_text()
    block = header.split('/* BEGIN GENERATED HOOKS */')[1].split('/* END GENERATED HOOKS */')[0]
    found = re.findall(r'int \(\*(w_[0-9A-F]{8})\)\(void \*ctx((?:, [^,)]+)*)\);', block)
    assert [n for n,_ in found] == [h[0] for h in HOOKS]
    types = {U:'uint32_t',I:'int32_t',F:'float',Q:'uint64_t'}
    for (name,params),(expected,address,args,result) in zip(found,HOOKS):
        actual = [re.sub(r'\s*\b\w+$','',p.strip()).replace(' ','') for p in params.split(',')[1:]]
        assert actual == [types[k] for k,_ in args] + ([types[result]+'*'] if result else [])
    internal = (ROOT/'src/game/em_area03_arrival_internal.h').read_text()
    for name,address,_,_ in HOOKS:
        bodies = re.findall(r'static inline int a03a_c_%s\(.*?\n}\n' % name[2:],internal,re.S)
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
    O.NATIVE = O.build(os.environ.get("EM_AREA03_ARRIVAL_SOURCE"))
    header_checks()
    entry_guards()
    selected = cases()
    only = os.environ.get('EM_AREA03_ARRIVAL_ONLY')
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
    for label, errors in failures[:int(os.environ.get('EM_AREA03_ARRIVAL_SHOW', '8'))]: print('FAIL', label, errors)
    assert not failures, f'{len(failures)} failures'
    print(f'PASS {len(selected)} cases: calls, ordered accesses, whole memory, poisoned starts and fail-stop')


if __name__ == '__main__': main()
