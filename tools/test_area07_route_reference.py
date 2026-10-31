#!/usr/bin/env python3
"""AREA07 route gaps: original code identity, ordered memory and worker replay.

Standalone and UNWIRED. Uses the existing level-14 comparator unchanged.
EM_TEST_FULL=1 sweeps every failure site; the default still covers every
reachable word. EM_AREA07_ROUTE_ONLY is diagnostic, never certification.
"""
import ctypes as C
import os
import re
import struct
import test_area01_overlay_reference as A1
from pathlib import Path
import test_level14_port_reference as O
import reference_root as refs
ROOT = Path(__file__).resolve().parents[1]
U, I, F, S = O.U, O.I, O.F, O.S
FUNCS = {
    0x00823F80: ('em_area07_route_00823F80',0x14F0,[U],None),
    0x008259D0: ('em_area07_route_008259D0',0x198,[U],None),
}
FUNCS_MUZZLE = {0x00825940: ('em_area07_route_00825940',0x84,[U, S],None)}
OVERLAY_OF = {entry:7 for entry in (*FUNCS, *FUNCS_MUZZLE)}
OVERLAY_FILES = {7:ROOT.parent/'Extermination/extract/OVERLAY/AREA07.BIN'}
HOOKS = [
    ('w_001026A0',0x001026A0,[('u', 4), ('u', 5), ('u', 6)],None),
    ('w_00102760',0x00102760,[('u', 4), ('u', 5)],None),
    ('w_001028D0',0x001028D0,[('u', 4), ('u', 5), ('u', 6)],None),
    ('w_00102948',0x00102948,[('u', 4), ('u', 5)],None),
    ('w_00102958',0x00102958,[('u', 4), ('u', 5)],None),
    ('w_0011DBB8',0x0011DBB8,[('f', 12)],'f'),
    ('w_0011DF78',0x0011DF78,[('f', 12)],'f'),
    ('w_0011E2A8',0x0011E2A8,[('f', 12)],'f'),
    ('w_0011E520',0x0011E520,[('f', 12)],'f'),
    ('w_0011E748',0x0011E748,[('f', 12)],'f'),
    ('w_00122BB8',0x00122BB8,[],'i'),
    ('w_001A2370',0x001A2370,[('u', 4), ('u', 5)],None),
    ('w_001AFA90',0x001AFA90,[('i', 4)],'u'),
    ('w_001AFC10',0x001AFC10,[('u', 4)],None),
    ('w_001B0FD0',0x001B0FD0,[('u', 4)],'i'),
    ('w_001B1190',0x001B1190,[('i', 4)],None),
    ('w_001B11E0',0x001B11E0,[('i', 4)],'i'),
    ('w_001B1470',0x001B1470,[('f', 12)],'f'),
    ('w_001B17A0',0x001B17A0,[('u', 4)],'i'),
    ('w_001BA1C0',0x001BA1C0,[('u', 4), ('i', 5)],'i'),
    ('w_001C6380',0x001C6380,[('u', 4)],None),
    ('w_001EFD90',0x001EFD90,[('i', 4), ('u', 5), ('u', 6)],'i'),
    ('w_001EFE00',0x001EFE00,[('i', 4), ('u', 5)],'i'),
    ('w_001FBD50',0x001FBD50,[('u', 4), ('i', 5), ('i', 6), ('f', 12)],'i'),
    ('w_00825470',0x00825470,[('u', 4), ('u', 5)],'i'),
    ('w_00825940',0x00825940,[('u', 4)],None),
 ]
BEATS = ('a07b_01_sub2','a07b_02_duct')
BEAT = BEATS[0]
SET2 = ROOT.parent/'Extermination/build/fork_refs/levels-recordings-20261031-1/s87/route_a07b'
# This 256 KiB window is zero in both AREA07 inputs; the loader asserts it.
FREE = 0x1F00000
A = FREE+0x1000


def configure(funcs=FUNCS):
    O.FREE = FREE
    O.FUNCS,O.HOOKS = funcs,HOOKS
    O.OVERLAY_OF,O.OVERLAY_FILES = OVERLAY_OF,OVERLAY_FILES
    O.OUT = ROOT/'build/levels/area07_route'
    O.SOURCES = ('em_area07_route.c',)
    O.BEATS = BEATS
    O.BEAT_DIRS = {b:SET2/b for b in BEATS}
    O.RESIDENT = {b:7 for b in BEATS}
    O.BY_ADDRESS = {a:(n,args,res) for n,a,args,res in HOOKS if not (funcs is FUNCS_MUZZLE and a==0x825940)}
    O.BY_NAME = {n:a for n,a,_,_ in HOOKS}
    O.HOOK_NAMES = [h[0] for h in HOOKS]+['w_callback']
    O.Hooks = type('Hooks',(C.Structure,),{'_fields_':
        [('ctx',C.c_void_p),('bytes',O.BYTES_FN)] +
        [(name,O.hook_proto(args,res)) for name,_,args,res in HOOKS] +
        [('w_callback',O.CALLBACK_FN)]})
    O.SELF_ENTRIES,O.HEAVY = ({0x823F80,0x8259D0} if funcs is FUNCS else set()),set()
    O.RETURN_SITES = O.RETURN_SITES_FULL = ()
    used = ('w_001AFA90','w_00102948','w_00102958','w_001026A0')
    O.CONTRACT_UNREACHABLE = ({'w_001026A0':'the muzzle pass'} if funcs is FUNCS else
                              {h[0]:'not this pass' for h in HOOKS if h[0] not in used})
    O.LOCAL_STORES = set()
    O.DEFAULT_ARGS = {e:[O.STACK_TOP if k==S else A for k in kinds] for e,(_,_,kinds,_) in funcs.items()}
    O.image_for = lambda _:O.CAPTURES[BEAT][0]
    O.CAPTURES.clear();O.CAPTURES.update(O.load_captures(O.read_elf()))
    O.JUMP_TABLES = {};O.TABLE_BYTES = frozenset()
    # Designed twin fixtures retain AREA01 actor data and load original
    # AREA07 text. They are not represented as original AREA07 captures.
    A1.CAPTURES.clear();A1.CAPTURES.update(A1.load_captures(O.read_elf()))
    A1.Scribble=O.Scribble
    data=OVERLAY_FILES[7].read_bytes();length=0x40+struct.unpack_from('<I',data,12)[0]
    for beat,(ram,spad) in A1.CAPTURES.items():
        designed=bytearray(ram);designed[O.ARENA:O.ARENA+length]=data[:length]
        assert designed[O.ARENA:O.ARENA+length]==data[:length]
        O.CAPTURES['designed/'+beat]=(bytes(designed),spad)


def cases():
    out=[]
    b,h,w,f=O.b8,O.h16,O.w32,O.f32b
    def adapt(label,beat,entry,args,patches=(),spad=(),queues=None,default=None,run_set=A1.RUN):
        if entry not in (0x826D40,0x828850):return
        remap={0x8282F0:0x825470,0x8287C0:0x825940}
        queues={remap.get(a,a):v for a,v in (queues or {}).items()}
        default={remap.get(a,a):v for a,v in (default or {}).items()}
        out.append((f'twin/{len(out)}/{label}','designed/'+beat,
                    0x823F80 if entry==0x826D40 else 0x8259D0,tuple(args),list(patches),list(spad),
                    O.Script(queues,default),set(run_set)))
    A1.targeted_cases(adapt)
    # flag 0x15: state 0x64, and the three tests of states 4, 1, 2
    common=[(A,bytes(0x300)),(A+0x1000,bytes(0x300)),(A+0x4C,w(0x1C6960)),(A+0x220,w(A+0x1000)),(0x275B40,w(A+0x2000)),
            (A+0x2008,w(A+0x3000)),(A+0x200C,w(A+0x3000)),(A+0x110,w(A+0x3000)),(A+0x11C,w(A+0x3000)),(A+0x111C,w(A+0x3000))]
    for state in (0,1,2,4,0x64,3):
        for flag in (0,1):
            for comp in (0,A+0x1000):
                out.append((f'flag/{state}/{flag}/{comp:x}',BEAT,0x823F80,(A,),
                            common+[(A+4,b(state)),(A+0x220,w(comp if state!=1 else A+0x1000)),(A+0x208,w(0)),(A+0x204,w(A))],
                            [],O.Script({0x1B0FD0:[0],0x1BA1C0:[flag]}),set()))
    for phase in (-2.0,-1.5707964,-1.0,-1.6,-1.55):
        for left in (0,1,5):
            for lane in (0,1):
                out.append((f'state2/{phase}/{left}/{lane}',BEAT,0x823F80,(A,),
                            common+[(A+4,b(2)),(A+0x1FC,f(phase)),(A+0x21C,w(left)),(A+0x224,w(lane)),(A+0x1F8,f(0.044879895))],
                            [],O.Script(),set()))
    for beat in BEATS:
        ram=O.CAPTURES[beat][0]
        for entry in (0x823F80,0x8259D0):
            for node in O.owners(ram,entry):
                out.append((f'capture/{beat}/{entry:x}/{node:x}',beat,entry,(node,),
                            [(0x275B40,w(node+0x110))],[],O.Script(),set()))
    return out


def header_checks():
    header = (ROOT/'src/game/em_area07_route.h').read_text()
    block = header.split('/* BEGIN GENERATED HOOKS */')[1].split('/* END GENERATED HOOKS */')[0]
    found = re.findall(r'int \(\*(w_[0-9A-F]{8})\)\(void \*ctx((?:, [^,)]+)*)\);', block)
    assert [n for n,_ in found] == [h[0] for h in HOOKS]
    types = {U:'uint32_t',I:'int32_t',F:'float'}
    for (name,params),(expected,address,args,result) in zip(found,HOOKS):
        actual = [re.sub(r'\s*\b\w+$','',p.strip()).replace(' ','') for p in params.split(',')[1:]]
        assert actual == [types[k] for k,_ in args] + ([types[result]+'*'] if result else [])
    internal = (ROOT/'src/game/em_area07_route_internal.h').read_text()
    for name,address,_,_ in HOOKS:
        bodies = re.findall(r'static inline int a07r_c_%s\(.*?\n}\n' % name[2:],internal,re.S)
        assert len(bodies)==1 and f'o->h->{name}(' in bodies[0] and f'0x{address:08X}u' in bodies[0]


def entry_guards():
    touched=[]
    def refuse(*_): touched.append(1); return -1
    keep=[O.BYTES_FN(refuse), O.CALLBACK_FN(refuse)]
    fields={'ctx':None,'bytes':keep[0],'w_callback':keep[1]}
    for name,_,args,result in HOOKS:
        fields[name]=O.hook_proto(args,result)(refuse); keep.append(fields[name])
    hooks=O.Hooks(**fields)
    for entry,(symbol,_,_,kind) in O.FUNCS.items():
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


def muzzle_cases():
    w=O.w32
    return [(f'muzzle/{got:x}',BEAT,0x825940,(A+0x400,O.STACK_TOP),[(A+0x400,bytes(0x80)),(A,bytes(0x300))],[],
             O.Script({0x1AFA90:[got]}),set()) for got in (0,A)]


def main():
    run(FUNCS)
    run(FUNCS_MUZZLE)


def run(funcs):
    configure(funcs)
    O.NATIVE = O.build(os.environ.get("EM_AREA07_ROUTE_SOURCE"))
    header_checks()
    entry_guards()
    selected = cases() if funcs is FUNCS else muzzle_cases()
    only = os.environ.get('EM_AREA07_ROUTE_ONLY')
    if only: selected = [c for c in selected if c[0].startswith(only)]
    O.CASES[:] = selected
    assert len({c[0] for c in selected}) == len(selected)
    results = O.run_cases(selected)
    failures = [(label, errors) for label, _, errors, _, _, _ in results if errors]
    seen = set().union(*(r[3] for r in results))
    for entry, (_, size, _, _) in funcs.items():
        words = O.reachable_words(O.image_for(entry), entry, size)
        missing = words - seen
        print(f'{entry:08X}: {len(words & seen)}/{len(words)} reachable words')
        if missing and not only: failures.append(('coverage', [entry, sorted(missing)]))
    if not only:
        failures += [('callee inventory', p) for p in O.callee_checks()]
        if not failures:
            sites, missing = O.hook_sites(results)
            missing = [m for m in missing if not (funcs is FUNCS_MUZZLE and m=='w_callback')]
            if missing: failures.append(('unreached hooks', missing))
            for problems, runs in O.reference_mode.parallel_map(O.hook_contract_site, sites):
                if problems: failures.append(('fail-stop contract', problems))
    for label, errors in failures[:int(os.environ.get('EM_AREA07_ROUTE_SHOW', '8'))]: print('FAIL', label, errors)
    assert not failures, f'{len(failures)} failures'
    print(f'PASS {len(selected)} cases: calls, ordered accesses, whole memory, poisoned starts and fail-stop')


if __name__ == '__main__': main()
