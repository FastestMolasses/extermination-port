#!/usr/bin/env python3
"""AREA19 route gaps: original code identity, ordered memory and worker replay.

Standalone and UNWIRED. Uses the existing level-14 comparator unchanged.
EM_TEST_FULL=1 sweeps every failure site; the default still covers every
reachable word. EM_AREA19_ROUTE_ONLY is diagnostic, never certification.
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
    0x824690: ('em_area19_route_00824690', 0x3F4, [U,S], None),
    0x827DD0: ('em_area19_route_00827DD0', 0x15A0, [U], None),
}
OVERLAY_OF = {entry:16 for entry in FUNCS}
OVERLAY_FILES = {16:ROOT.parent/'Extermination/extract/OVERLAY/AREA19.BIN'}
HOOKS = [
    ('w_001026A0',0x001026A0,[('u', 4), ('u', 5), ('u', 6)],None),
    ('w_00102760',0x00102760,[('u', 4), ('u', 5)],None),
    ('w_001028D0',0x001028D0,[('u', 4), ('u', 5), ('u', 6)],None),
    ('w_00102918',0x00102918,[('u', 4), ('u', 5), ('u', 6)],None),
    ('w_00102948',0x00102948,[('u', 4), ('u', 5)],None),
    ('w_00102958',0x00102958,[('u', 4), ('u', 5)],None),
    ('w_001029C0',0x001029C0,[('u', 4)],None),
    ('w_00102C58',0x00102C58,[('u', 4), ('u', 5), ('u', 6)],None),
    ('w_0011DBB8',0x0011DBB8,[('f', 12)],'f'),
    ('w_0011DF78',0x0011DF78,[('f', 12)],'f'),
    ('w_0011E2A8',0x0011E2A8,[('f', 12)],'f'),
    ('w_0011E520',0x0011E520,[('f', 12)],'f'),
    ('w_0011E748',0x0011E748,[('f', 12)],'f'),
    ('w_00122BB8',0x00122BB8,[],'i'),
    ('w_0019B6C0',0x0019B6C0,[('u', 4), ('u', 5), ('u', 6)],'i'),
    ('w_001A2370',0x001A2370,[('u', 4), ('u', 5)],None),
    ('w_001AFA90',0x001AFA90,[('i', 4)],'u'),
    ('w_001AFC10',0x001AFC10,[('u', 4)],None),
    ('w_001B0FD0',0x001B0FD0,[('u', 4)],'i'),
    ('w_001B1470',0x001B1470,[('f', 12)],'f'),
    ('w_001B17A0',0x001B17A0,[('u', 4)],'i'),
    ('w_001C6380',0x001C6380,[('u', 4)],None),
    ('w_001CCF70',0x001CCF70,[('u', 4)],'i'),
    ('w_001CFA60',0x001CFA60,[('u', 4), ('u', 5), ('f', 12), ('f', 13)],None),
    ('w_001CFBE0',0x001CFBE0,[('i', 4), ('i', 5), ('u', 6), ('u', 7), ('i', 8)],None),
    ('w_001E8B90',0x001E8B90,[('u', 4), ('f', 12)],None),
    ('w_001EFD90',0x001EFD90,[('i', 4), ('u', 5), ('u', 6)],'i'),
    ('w_001FBD50',0x001FBD50,[('u', 4), ('i', 5), ('i', 6), ('f', 12)],'i'),
    ('w_001FC3C0',0x001FC3C0,[('u', 4), ('u', 5), ('i', 6), ('f', 12), ('f', 13)],None),
    ('w_001FC520',0x001FC520,[('u', 4)],None),
    ('w_00829370',0x00829370,[('u', 4), ('u', 5)],'i'),
    ('w_00829840',0x00829840,[('u', 4)],None),
 ]
BEATS = ('a19_00_duct',)
BEAT = BEATS[0]
A = O.FREE+0x1000


def configure():
    O.FUNCS, O.HOOKS = FUNCS, HOOKS
    O.OVERLAY_OF, O.OVERLAY_FILES = OVERLAY_OF, OVERLAY_FILES
    O.OUT = ROOT/'build/levels/area19_route'
    O.SOURCES = ('em_area19_route_flame.c','em_area19_route_turret.c')
    O.BEATS = BEATS
    O.BEAT_DIRS = {b:refs.v263('s87/route_a19/'+b,
        'fork route is not recorded from a04b_00; retained original captures explicitly selected') for b in BEATS}
    O.RESIDENT = {b:16 for b in BEATS}
    O.BY_ADDRESS = {a:(n,args,res) for n,a,args,res in HOOKS}
    O.BY_NAME = {n:a for n,a,_,_ in HOOKS}
    O.HOOK_NAMES = [h[0] for h in HOOKS]+['w_callback']
    O.Hooks = type('Hooks',(C.Structure,),{'_fields_':
        [('ctx',C.c_void_p),('bytes',O.BYTES_FN)] +
        [(name,O.hook_proto(args,res)) for name,_,args,res in HOOKS] +
        [('w_callback',O.CALLBACK_FN)]})
    O.SELF_ENTRIES, O.HEAVY = {0x827DD0}, set()
    O.RETURN_SITES = O.RETURN_SITES_FULL = ()
    O.CONTRACT_UNREACHABLE = {}
    O.LOCAL_STORES = set()
    O.DEFAULT_ARGS = {0x824690:[A,O.STACK_TOP],0x827DD0:[A]}
    O.image_for = lambda _:O.CAPTURES[BEAT][0]
    O.CAPTURES.clear()
    O.CAPTURES.update(O.load_captures(O.read_elf()))
    O.JUMP_TABLES = {}
    O.TABLE_BYTES = frozenset()
    # Reuse the existing adversarial AREA01 turret fixtures as DESIGNED
    # inputs to the AREA19 twin. Only original AREA19 text/header is loaded
    # into these private images; no fixture is claimed to be an AREA19 capture.
    A1.CAPTURES.clear()
    A1.CAPTURES.update(A1.load_captures(O.read_elf()))
    A1.Scribble = O.Scribble
    data=OVERLAY_FILES[16].read_bytes()
    length=0x40+struct.unpack_from('<I',data,12)[0]
    for beat,(ram,spad) in A1.CAPTURES.items():
        designed=bytearray(ram)
        designed[O.ARENA:O.ARENA+length]=data[:length]
        assert designed[O.ARENA:O.ARENA+length]==data[:length]
        O.CAPTURES['designed/'+beat]=(bytes(designed),spad)


def cases():
    out=[]
    b,h,w,f=O.b8,O.h16,O.w32,O.f32b
    def adapt(label,beat,entry,args,patches=(),spad=(),queues=None,default=None,run_set=A1.RUN):
        if entry!=0x826D40:return
        remap={0x8282F0:0x829370,0x8287C0:0x829840}
        queues={remap.get(a,a):v for a,v in (queues or {}).items()}
        default={remap.get(a,a):v for a,v in (default or {}).items()}
        out.append((f'twin/{len(out)}/{label}','designed/'+beat,0x827DD0,tuple(args),list(patches),list(spad),
                    O.Script(queues,default),set(run_set)))
    A1.targeted_cases(adapt)
    # AREA19's additional missed-probe effects depend on the kind BEFORE
    # the worker runs, even when the worker changes the hit-record bytes.
    template=next(c for c in out if 'kind 3 hit 0' in c[0])
    for kind in (6,8,0,255):
        label,beat,entry,args,patches,spad,script,run_set=template
        hit=O.FREE+0x4000
        out.append((f'miss-kind/{kind}',beat,entry,args,patches+[(hit,bytes(0x100)),(hit+0x1A,b(kind))],
                    spad+[(0x700031D0,w(hit)),(0x700031D8,w(0))],
                    O.Script({0x829370:[1],0x19B6C0:[O.Scribble(0,[(hit+0x1A,b(1))])]}),run_set))
    common=[(A,bytes(0x300))]
    def add(label,patches=(),queues=None,run_set=()):
        out.append((label,BEAT,0x824690,(A,O.STACK_TOP),common+list(patches),[],O.Script(queues),set(run_set)))
    for state in (0,1,2,3,4,255):
        for flag in (0,1,2,255):
            for rnd in (0,0x40000000,0x40000080,0x7FFFFFFF,0xFFFFFFFF,0x80000000):
                add(f'flame/state/{state}/{flag}/{rnd:x}',[(A+4,b(state)),(0x810775,b(flag))],
                    {0x122BB8:[rnd]*6})
    for raw in (0,0x80000000,0x3FFE0000,0x3FFFFFFF,0x40000000,0x7F800000,0xFF800000,0x7FC00000,0xFFFFFFFF):
        add(f'flame/phase/{raw:x}',[(A+4,b(1)),(A+0x1F0,w(raw)),(A+0x1F4,w(raw))])
    for fn,changes in (
        (0x1026A0,[(0x810775,b(1)),(A+0x1F0,f(1.999)),(A+0x1F8,f(0.25))]),
        (0x1CFA60,[(O.STACK_TOP-0x70,bytes(range(96)))]),
        (0x1CFBE0,[(A+0x1F0,f(1.999)),(A+0x1F4,f(1.999))]),
        (0x102948,[(O.STACK_TOP-0x10,O.fw(3.0,4.0,5.0,6.0))]),
    ):
        add(f'flame/scribble/{fn:x}',[(A+4,b(1))],
            {fn:[O.Scribble(None,changes)],0x122BB8:[0x7FFFFFFF]*6})
    for beat in BEATS:
        ram=O.CAPTURES[beat][0]
        for entry in FUNCS:
            for node in O.owners(ram,entry):
                args=(node,O.STACK_TOP) if entry==0x824690 else (node,)
                patches=[(0x275B40,w(node+0x110))] if entry==0x827DD0 else []
                out.append((f'capture/{beat}/{entry:x}/{node:x}',beat,entry,args,patches,[],O.Script(),set()))
    return out


def header_checks():
    header = (ROOT/'src/game/em_area19_route.h').read_text()
    block = header.split('/* BEGIN GENERATED HOOKS */')[1].split('/* END GENERATED HOOKS */')[0]
    found = re.findall(r'int \(\*(w_[0-9A-F]{8})\)\(void \*ctx((?:, [^,)]+)*)\);', block)
    assert [n for n,_ in found] == [h[0] for h in HOOKS]
    types = {U:'uint32_t',I:'int32_t',F:'float'}
    for (name,params),(expected,address,args,result) in zip(found,HOOKS):
        actual = [re.sub(r'\s*\b\w+$','',p.strip()).replace(' ','') for p in params.split(',')[1:]]
        assert actual == [types[k] for k,_ in args] + ([types[result]+'*'] if result else [])
    internal = (ROOT/'src/game/em_area19_route_internal.h').read_text()
    for name,address,_,_ in HOOKS:
        bodies = re.findall(r'static inline int a19r_c_%s\(.*?\n}\n' % name[2:],internal,re.S)
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
    O.NATIVE = O.build(os.environ.get("EM_AREA19_ROUTE_SOURCE"))
    header_checks()
    entry_guards()
    selected = cases()
    only = os.environ.get('EM_AREA19_ROUTE_ONLY')
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
    for label, errors in failures[:int(os.environ.get('EM_AREA19_ROUTE_SHOW', '8'))]: print('FAIL', label, errors)
    assert not failures, f'{len(failures)} failures'
    print(f'PASS {len(selected)} cases: calls, ordered accesses, whole memory, poisoned starts and fail-stop')


if __name__ == '__main__': main()
