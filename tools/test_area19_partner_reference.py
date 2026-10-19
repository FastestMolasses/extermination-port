#!/usr/bin/env python3
"""AREA19 partner entry using the existing AREA01 native definition; all original-code comparisons remain enabled.

Unwired. EM_TEST_FULL=1 sweeps all failure sites; diagnostic subsets grant no proof.
"""
import ctypes as C
import os
import re
from pathlib import Path
import test_level14_port_reference as O
import test_area01_overlay_reference as A01
import reference_root as refs
ROOT=Path(__file__).resolve().parents[1]
U,I,F,Q,S=O.U,O.I,O.F,O.Q,O.S
FUNCS={0x008298D0:('em_area01_ovl_00828850',0x198,[U],None)}
OVERLAY_OF={0x008298D0:16}
OVERLAY_FILES={16:ROOT.parent/'Extermination/extract/OVERLAY/AREA19.BIN'}
HOOKS=A01.HOOKS
USED={0x1B0FD0,0x1B11E0,0x1EFE00,0x1FBD50,0x1C6380,0x1B17A0,0x1B1190,0x1AFC10}
BEATS=('a19d_00_beastA','a19e_02_truck')
BEAT=BEATS[-1]
FREE=0x1E61000
A,B=FREE+0x1000,FREE+0x2000


def configure():
    O.FREE=FREE
    O.FUNCS,O.HOOKS=FUNCS,HOOKS
    O.OVERLAY_OF,O.OVERLAY_FILES=OVERLAY_OF,OVERLAY_FILES
    O.OUT=ROOT/'build/levels/area19_partner'
    O.SOURCES=('em_area01_overlay.c','em_area01_overlay_826d40.c')
    O.BEATS=BEATS
    O.BEAT_DIRS={b:refs.v263('s87/route_'+b.split('_')[0]+'/'+b,
        'fork route is not recorded from a04b_00; retained original captures explicitly selected') for b in BEATS}
    O.RESIDENT={b:16 for b in BEATS}
    O.BY_ADDRESS={a:(n,args,res) for n,a,args,res in HOOKS}
    O.BY_NAME={n:a for n,a,_,_ in HOOKS}
    O.HOOK_NAMES=[n for n,a,_,_ in HOOKS if a in USED]+['w_callback']
    O.Hooks=type('Hooks',(C.Structure,),{'_fields_':
        [('ctx',C.c_void_p),('bytes',O.BYTES_FN)]+
        [(name,O.hook_proto(args,res)) for name,_,args,res in HOOKS]+[('w_callback',O.CALLBACK_FN),('view',C.c_void_p)]})
    O.SELF_ENTRIES=set(FUNCS);O.HEAVY=set()
    O.RETURN_SITES=O.RETURN_SITES_FULL=()
    O.CONTRACT_UNREACHABLE={};O.LOCAL_STORES=set()
    O.DEFAULT_ARGS={0x8298D0:[A]}
    O.image_for=lambda _:O.CAPTURES[BEAT][0]
    O.CAPTURES.clear();O.CAPTURES.update(O.load_captures(O.read_elf()))
    O.JUMP_TABLES={};O.TABLE_BYTES=frozenset()


def cases():
    out=[];b,h,w=O.b8,O.h16,O.w32
    common=[(A,bytes(0x300)),(B,bytes(0x300)),(A+0x18,w(B)),(A+0x4C,w(0x1C6960))]
    def add(label,patches=(),queues=None):
        out.append((label,BEAT,0x8298D0,(A,),common+list(patches),[],O.Script(queues),set()))
    for state in range(256):
        add(f'state/{state}',[(A+4,b(state))])
    for result in (0,1,-1,2,0x7FFFFFFF,0x80000000):
        for ident in (0,127,128,255):
            add(f'init/{result}/{ident}',[(A+0x9A,b(ident))],{0x1B0FD0:[result],0x1B11E0:[1]})
            add(f'flag/{result}/{ident}',[(A+0x9A,b(ident))],{0x1B11E0:[result]})
            add(f'effect/{result}/{ident}',[(A+4,b(1)),(A+0x36,h(1)),(A+0x9A,b(ident))],{0x1EFE00:[result]})
    for timer in (0,8,9,10,11,32767,32768,65535):
        for ident in (0,127,128,255):
            add(f'timer/{timer}/{ident}',[(A+4,b(2)),(A+0x28,h(timer)),(A+0x9A,b(ident))])
    for damage in (0,1,32767,32768,65535):
        for alias in (B,A,A+0x14):
            add(f'alias/{damage}/{alias:x}',[(A+4,b(1)),(A+0x36,h(damage)),(A+0x18,w(alias))],{0x1EFE00:[1]})
    add('worker/partner',queues={0x1B11E0:[O.Scribble(1,[(A+0x18,w(B+0x40))])]})
    add('worker/effect',[(A+4,b(1)),(A+0x36,h(1))],{0x1EFE00:[O.Scribble(1,[(A+0x28,h(99)),(A+4,b(4))])]})
    add('worker/timer',[(A+4,b(2)),(A+0x28,h(9))],{0x1FBD50:[O.Scribble(0,[(A+0x9A,b(255))])]})
    for beat,(ram,_) in O.CAPTURES.items():
        for node in O.owners(ram,0x8298D0):
            out.append((f'captured/{beat}/{node:x}',beat,0x8298D0,(node,),[],[],O.Script(),set()))
    return out


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
    O.NATIVE = O.build(os.environ.get("EM_AREA19_PARTNER_SOURCE"))
    entry_guards()
    selected = cases()
    only = os.environ.get('EM_AREA19_PARTNER_ONLY')
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
    for label, errors in failures[:int(os.environ.get('EM_AREA19_PARTNER_SHOW', '8'))]: print('FAIL', label, errors)
    assert not failures, f'{len(failures)} failures'
    print(f'PASS {len(selected)} cases: calls, ordered accesses, whole memory, poisoned starts and fail-stop')


if __name__ == '__main__': main()
