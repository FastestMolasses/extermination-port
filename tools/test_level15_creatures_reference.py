#!/usr/bin/env python3
"""Four level-15 creature entries; all original-code comparisons remain enabled.

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
    0x0012F6C0:('em_level15_creatures_0012F6C0',0x2BC,[U, U],None),
    0x0013D490:('em_level15_creatures_0013D490',0x170,[U, U],None),
    0x00141A30:('em_level15_creatures_00141A30',0x258,[U, U],I),
    0x00153B50:('em_level15_creatures_00153B50',0x34C,[U, U],None),
    # AREA03 sub 0's creature (lane K1): the byte-matched decomp C
    0x0013D2D0:('em_level15_creatures_0013D2D0',0x1C0,[U],None),
    0x0013D850:('em_level15_creatures_0013D850',0x130,[U, U],None),
    0x00140690:('em_level15_creatures_00140690',0xD0,[U, U],None),
    0x00140C00:('em_level15_creatures_00140C00',0x30,[U, U],None),
    0x00141C90:('em_level15_creatures_00141C90',0x90,[U, U],None),
}
OVERLAY_OF={}
OVERLAY_FILES={4:ROOT.parent/'Extermination/extract/OVERLAY/AREA03.BIN'}
HOOKS=[
    ('w_001028B8',0x001028B8,[('u', 4), ('u', 5), ('u', 6)],None),
    ('w_0011DE90',0x0011DE90,[('f', 12)],'f'),
    ('w_0011E2A8',0x0011E2A8,[('f', 12)],'f'),
    ('w_00122BB8',0x00122BB8,[],'i'),
    ('w_00131ED0',0x00131ED0,[('u', 4)],None),
    ('w_00131F20',0x00131F20,[('u', 4), ('u', 5), ('u', 6)],None),
    ('w_00132490',0x00132490,[('u', 4), ('u', 5)],None),
    ('w_0013D600',0x0013D600,[('u', 4), ('u', 5)],None),
    ('w_00140760',0x00140760,[('u', 4), ('u', 5)],None),
    ('w_001408D0',0x001408D0,[('u', 4), ('u', 5)],None),
    ('w_00140A90',0x00140A90,[('u', 4), ('u', 5)],None),
    ('w_00140C30',0x00140C30,[('u', 4), ('u', 5)],None),
    ('w_00140F80',0x00140F80,[('u', 4), ('u', 5)],None),
    ('w_001418F0',0x001418F0,[('u', 4), ('u', 5)],None),
    ('w_0019AB20',0x0019AB20,[('u', 4), ('u', 5), ('u', 6), ('u', 7)],'i'),
    ('w_001AFC10',0x001AFC10,[('u', 4)],None),
    ('w_001B0D80',0x001B0D80,[('u', 4)],'i'),
    ('w_001B10B0',0x001B10B0,[('u', 4), ('i', 5), ('i', 6)],'i'),
    ('w_001B1190',0x001B1190,[('i', 4)],None),
    ('w_001B13F0',0x001B13F0,[('f', 12), ('u', 4), ('u', 5)],'i'),
    ('w_001B1560',0x001B1560,[('u', 4), ('u', 5), ('f', 12)],'i'),
    ('w_001B17A0',0x001B17A0,[('u', 4)],'i'),
    ('w_001B2140',0x001B2140,[('u', 4)],'i'),
    ('w_001B3250',0x001B3250,[('u', 4), ('u', 5), ('f', 12)],'i'),
    ('w_001B4CF0',0x001B4CF0,[('u', 4)],None),
    ('w_001B5360',0x001B5360,[('u', 4)],None),
    ('w_001C63E0',0x001C63E0,[('u', 4), ('i', 5)],None),
    ('w_001C64F0',0x001C64F0,[('u', 4), ('f', 12)],'i'),
    ('w_001C67E0',0x001C67E0,[('u', 4), ('i', 5), ('f', 12), ('f', 13)],None),
    ('w_001C68C0',0x001C68C0,[('u', 4)],None),
    ('w_001EFD90',0x001EFD90,[('i', 4), ('u', 5), ('u', 6)],None),
    ('w_001EFE00',0x001EFE00,[('i', 4), ('u', 5)],'i'),
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
    O.OUT=ROOT/'build/levels/level15_creatures'
    O.SOURCES=('em_level15_creatures.c',)
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
    O.SELF_ENTRIES={0x153B50,0x13D2D0,0x140690};O.HEAVY=set()
    O.RETURN_SITES=O.RETURN_SITES_FULL=(0x141A30,)
    O.CONTRACT_UNREACHABLE={};O.LOCAL_STORES=set()
    O.DEFAULT_ARGS={e:[A] if e==0x13D2D0 else [A,B] for e in FUNCS}
    O.image_for=lambda _:O.CAPTURES[BEAT][0]
    O.CAPTURES.clear();O.CAPTURES.update(O.load_captures(O.read_elf()))
    O.JUMP_TABLES={};O.TABLE_BYTES=frozenset()


def cases():
    out=[];b,h,w,f=O.b8,O.h16,O.w32,O.f32b
    common=[(A,bytes(0x300)),(B,bytes(0x200)),(A+0x4C,w(0x1C6960)),
            (A+0xB0,O.fw(2.,3.,4.,1.,0.1,0.2,0.3,1.)),(B+0x40,f(6.))]
    def add(label,e,patches=(),queues=None,args=(A,B)):
        out.append((label,BEAT,e,args,common+list(patches),[],O.Script(queues),set()))
    for state in (0,1,2,3,255):
        for clip in (7,8,9,10,11):
            for status,flag,timer in ((0,0,0),(0x8000,1,0),(0x1000,1,1),(0x2000,1,0),(0x7000,1,65535)):
                add(f'attack/{state}/{clip}/{status}/{flag}/{timer}',0x12F6C0,
                    [(A+6,b(state)),(A+0x2C,h(clip)),(B+0x58,h(status)),(B+0x69,b(flag)),(B+0x54,h(timer))])
    for a,c,d in ((1,0,0),(0,0,0),(0,1,0),(0,1,1)):
        for rng in (0,0xFFF,0x7FFFFFFF,0x80000000,0xFFFFFFFF):
            add(f'attack/probe/{a}/{c}/{d}/{rng}',0x12F6C0,
                [(A+6,b(2)),(A+0x2C,h(9)),(B+0x69,b(1))],
                {0x1B3250:[a],0x1B1560:[c],0x1B13F0:[d],0x122BB8:[rng]})
    for state in (0,1,255):
        for flag in (0,1,127,128,255):
            for area,global_flag,result in ((0,0,0),(1,0,0),(0,255,0),(1,255,0),(0,0,1),(1,255,1)):
                add(f'init/{state}/{flag}/{area}/{global_flag}/{result}',0x13D490,
                    [(A+4,b(state)),(A+0xD,b(flag)),(0x81070A,b(area)),(0x810808,b(global_flag))],{0x1B10B0:[result]})
    for charge in (0,1,24,25,26,100,0xFFF,0x1001,0x2001,0x4001,0x5001,0x8001,0xA001,0xFFFF):
        for state,hp,flags in ((1,100,0),(1,1,0),(1,0,2),(2,200,2),(1,32767,2),(2,65535,0)):
            for block in (0,1,2,3,4,5):
                add(f'damage/{charge}/{state}/{hp}/{flags}/{block}',0x141A30,
                    [(A+4,b(state)),(A+5,b(block)),(A+0x34,h(hp)),(A+0x36,h(charge)),(B+0x80,b(flags)),
                     (B+0x88,b(block==1)),(B+0x89,b(block==2)),(B+0x8A,b(block==3)),(B+0x8B,b(block==4)),(B+0x74,h(24))])
    for state in (0,1,2,3,4,255):
        for hp,damage in ((100,1),(1,1),(0,1),(32767,65535),(32768,1)):
            for status,result in ((0,0),(0x1000,0),(0x1000,1)):
                add(f'hurt/{state}/{hp}/{damage}/{status}/{result}',0x153B50,
                    [(A+5,b(state)),(A+0x34,h(hp)),(A+0x36,h(damage)),(B,w(status))],
                    {0x1B17A0:[result],0x1EFE00:[result],0x19AB20:[result],0x1C64F0:[0x1000]})
    for raw in (0,0x80000000,0xC0A00000,0xC0A00001,0xC0C00000,0x7F800000,0xFF800000,0x7FC00000):
        add(f'hurt/velocity/{raw:x}',0x153B50,[(A+5,b(2)),(B+4,w(raw)),(B+8,f(-0.01)),(B+12,f(2.))],
            {0x11E2A8:[O.fbits(0.3)],0x11DE90:[O.fbits(0.8)]})
    for work in (A,A+0xB4,A+0xAC):
        add(f'init/alias/{work:x}',0x13D490,args=(A,work))
    add('attack/worker/clip',0x12F6C0,queues={0x1C67E0:[O.Scribble(0,
        [(A+6,b(7)),(A+0x2C,h(10)),(B+0x58,h(0x1000)),(B+0x69,b(1)),(B+0x54,h(0))])]})
    add('attack/worker/random',0x12F6C0,[(A+6,b(2)),(B+0x69,b(1))],
        {0x122BB8:[O.Scribble(0x7FFFFFFF,[(A+0xB0,O.fw(3.,4.,5.,1.)),(B+0x40,f(7.))])]})
    add('damage/worker/effect',0x141A30,[(A+0x34,h(100)),(A+0x36,h(0x4001))],
        {0x1EFE00:[O.Scribble(0,[(A+0x34,h(1)),(A+0x36,h(0xA001))])]})
    add('damage/worker/death',0x141A30,[(A+0x34,h(1)),(A+0x36,h(1))],
        {0x1B4CF0:[O.Scribble(0,[(B+0x80,b(2)),(A+0x36,h(0))])]})
    # AREA03 sub 0's creature
    def add1(label,patches=(),queues=None):
        spad=[(a,v) for a,v in patches if a>=0x70000000]
        ram=[(a,v) for a,v in patches if a<0x70000000]
        out.append((label,BEAT,0x13D2D0,(A,),common+ram,spad,O.Script(queues),set()))
    for mode in (0,1,2,3,4,5):
        for state in (0,1,2,3,4):
            for live in (0,1):
                add1(f'tick/{mode}/{state}/{live}',[(0x70003B8D,b(mode)),(A+4,b(state)),(A+0x4C,w(0x1C6960)),
                     (A+0x1F0+0x70,h(2)),(A+0x1F0+0x76,h(3)),(A+0x1F0+0x8B,b(4)),(A+0x1F0+0x87,b(5)),
                     (A+0x1F0+0x72,h(6)),(A+0x1F0+0x88,b(7)),(A+0x1F0+0x8E,b(live))],{0x1B2140:[live]})
    add1('tick/zero',[(0x70003B8D,b(0)),(A+4,b(1))])
    for state in (0,1,2,3):
        for w56,limit,flag,w70 in ((0,0,0,0),(0,0,0,1),(5,10.,0,0),(5,1.,0,0),(5,1.,1,0),(5,10.,1,1)):
            for st30 in (0,0x1000):
                add(f'idle/{state}/{w56}/{limit}/{flag}/{w70}/{st30}',0x13D850,
                    [(A+6,b(state)),(A+0x56,h(w56)),(B+0x5C,f(limit)),(A+0xA,b(flag)),(B+0x70,h(w70)),(B+0x30,w(st30))])
    for state in (0,1,2,3):
        add(f'motion/{state}',0x140690,[(A+5,b(state)),(A+0x4C,w(0x1C6960)),(B+0x40,f(2.))],{0x1C64F0:[0x1234]})
    add('remove',0x140C00,[(A+0x9A,b(7))])
    for d,w76,w70,w54,s80,st4,st5 in ((0x80,0,1,1,2,1,0),(0,1,1,1,2,1,0),(0,0,0,1,2,1,0),(0,0,1,0,2,1,0),
                                      (0,0,1,1,0,1,0),(0,0,1,1,2,2,0),(0,0,1,1,2,1,7),(0,0,1,1,2,1,5),
                                      (0,0,1,1,2,1,6),(0,0,1,1,2,1,0x80),(0,0,1,1,3,1,1)):
        add(f'guard/{d}/{w76}/{w70}/{w54}/{s80}/{st4}/{st5}',0x141C90,
            [(A+0xD,b(d)),(B+0x76,h(w76)),(B+0x70,h(w70)),(A+0x54,h(w54)),(B+0x80,b(s80)),(A+4,b(st4)),(A+5,b(st5))])
    for worker in (0x11E2A8,0x11DE90,0x19AB20):
        add(f'hurt/worker/{worker:x}',0x153B50,[(A+5,b(2))],
            {worker:[O.Scribble(O.fbits(0.5) if worker!=0x19AB20 else 1,
                      [(B+12,f(3.)),(A+0xB0,O.fw(6.,7.,8.,1.)),(A+5,b(8))])]})
    return out


def header_checks():
    header = (ROOT/'src/game/em_level15_creatures.h').read_text()
    block = header.split('/* BEGIN GENERATED HOOKS */')[1].split('/* END GENERATED HOOKS */')[0]
    found = re.findall(r'int \(\*(w_[0-9A-F]{8})\)\(void \*ctx((?:, [^,)]+)*)\);', block)
    assert [n for n,_ in found] == [h[0] for h in HOOKS]
    types = {U:'uint32_t',I:'int32_t',F:'float',Q:'uint64_t'}
    for (name,params),(expected,address,args,result) in zip(found,HOOKS):
        actual = [re.sub(r'\s*\b\w+$','',p.strip()).replace(' ','') for p in params.split(',')[1:]]
        assert actual == [types[k] for k,_ in args] + ([types[result]+'*'] if result else [])
    internal = (ROOT/'src/game/em_level15_creatures_internal.h').read_text()
    for name,address,_,_ in HOOKS:
        bodies = re.findall(r'static inline int l15c_c_%s\(.*?\n}\n' % name[2:],internal,re.S)
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
    O.NATIVE = O.build(os.environ.get("EM_LEVEL15_CREATURES_SOURCE"))
    header_checks()
    entry_guards()
    selected = cases()
    only = os.environ.get('EM_LEVEL15_CREATURES_ONLY')
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
    for label, errors in failures[:int(os.environ.get('EM_LEVEL15_CREATURES_SHOW', '8'))]: print('FAIL', label, errors)
    assert not failures, f'{len(failures)} failures'
    print(f'PASS {len(selected)} cases: calls, ordered accesses, whole memory, poisoned starts and fail-stop')


if __name__ == '__main__': main()
