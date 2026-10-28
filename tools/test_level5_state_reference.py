#!/usr/bin/env python3
"""Full unwired area dispatcher against original instructions.

Uses the unchanged level-14 oracle's calls, ordered memory accesses, whole
RAM, poisoned starts and fail-stop checks. Workers are explicit boundaries;
this verifies dispatch, not the overlay worker implementations.
"""
import ctypes as C
import os
from pathlib import Path
import test_level14_port_reference as O
import reference_root as refs
ROOT=Path(__file__).resolve().parents[1]
ENTRY=0x1E7780
WORKERS=(0x823580,0x8236E0,0x823740,0x8237C0,0x823900,0x823970,0x823990,0x823A50,
         0x823B10,0x823B80,0x8241F0,0x824290,0x824420,0x824E00,0x8250C0,0x826180)
FUNCS={ENTRY:('em_level5_state_001E7780',0x4E0,[],None),
       0x1D0660:('em_level5_state_001D0660',0x24,[],None)}
HOOKS=[(f'w_{a:08X}',a,[(O.U,4),(O.U,5)],None) for a in WORKERS]+[
       ('w_001F0310',0x1F0310,[],None)]
BEAT='a03_00_elevator'
FREE=0x1E61000
RECORD=FREE+0x1000


def configure():
    O.FREE=FREE; O.FUNCS=FUNCS; O.HOOKS=HOOKS
    O.OVERLAY_OF={}; O.OVERLAY_FILES={4:ROOT.parent/'Extermination/extract/OVERLAY/AREA03.BIN'}
    O.OUT=ROOT/'build/level-oracle-audit/level5-state'
    O.SOURCES=('em_level5_state.c',)
    O.BEATS=(BEAT,); O.BEAT_DIRS={BEAT:refs.v263('s87/route_a03/'+BEAT,
        'later route is not yet recorded on the fork; retained original capture explicitly selected')}
    O.RESIDENT={BEAT:4}
    O.BY_ADDRESS={a:(n,args,res) for n,a,args,res in HOOKS}
    O.BY_NAME={n:a for n,a,_,_ in HOOKS}; O.HOOK_NAMES=[h[0] for h in HOOKS]
    O.Hooks=type('Hooks',(C.Structure,),{'_fields_':
        [('ctx',C.c_void_p),('bytes',O.BYTES_FN)]+
        [(name,O.hook_proto(args,res)) for name,_,args,res in HOOKS]})
    O.SELF_ENTRIES=O.HEAVY=set(); O.RETURN_SITES=O.RETURN_SITES_FULL=()
    O.CONTRACT_UNREACHABLE={}; O.LOCAL_STORES=set(); O.DEFAULT_ARGS={e:[] for e in FUNCS}
    O.JUMP_TABLES={}; O.DEAD_WORDS={}; O.TABLE_BYTES=frozenset()
    O.CAPTURES.clear(); O.CAPTURES.update(O.load_captures(O.read_elf()))
    O.image_for=lambda _:O.CAPTURES[BEAT][0]


def cases():
    out=[]
    record_poison=bytes([0x5A])*0x30000
    def add(label,key,count=None,base=RECORD,entry=ENTRY,change_key=None):
        patches=[(0x810700,bytes((key>>8,key&255))), (0x275C18,bytes([0xA7])*24),
                 (RECORD,record_poison)]
        writes=[] if count is None else [(0x275C20,O.w32(base)),(0x275C2C,O.w32(count)),
                                          (0x810700,b'\xff\xff')]
        queues={a:[O.Scribble(0,writes)] for a in WORKERS} if writes else None
        if change_key is not None:
            queues=queues or {}
            queues[0x1F0310]=[O.Scribble(0,[(0x810700,bytes((change_key>>8,change_key&255))),
                                          (0x275C18,bytes([0xCE])*24)])]
        out.append((label,BEAT,entry,(),patches,[],O.Script(queues),set()))
    # Every possible area byte, each used sub-area and the gaps either side.
    for area in range(256):
        for sub in (*range(9),127,254,255): add(f'key/{area}/{sub}',area*256+sub)
    # Every worker, positive/negative counts and a moved pointer. Unmapped
    # and misaligned records must preserve the preceding original stores.
    keys=(0,0x100,0x200,0x300,0x400,0x600,0x700,0x800,0xB00,0xD00,0xE00,
          0x1000,0x1100,0x1300,0x1400,0x1500)
    for key in keys:
        for count in (0,1,2,3,0xFFFFFFFF,0x80000000): add(f'count/{key}/{count}',key,count)
        for base in (RECORD+4,0x1FFFFA8,0x2000000,RECORD+1,0xFFFFFFAC):
            add(f'pointer/{key}/{base}',key,2,base)
        add(f'chain/{key}',key,3,entry=0x1D0660)
        add(f'chain/changed/{key}',0xFFFF,2,entry=0x1D0660,change_key=key)
    add('chain/default',0xFFFF,entry=0x1D0660)
    if O.reference_mode.FULL:
        # Complete 16-bit selector domain, with no duplicates of above.
        for area in range(256):
            for sub in range(9,254):
                if sub!=127: add(f'key/{area}/{sub}',area*256+sub)
    return out


def main():
    configure(); O.NATIVE=O.build(os.environ.get('EM_LEVEL5_STATE_SOURCE'))
    touched=[]
    def refuse(*_):
        touched.append(1)
        return -1
    keep=[O.BYTES_FN(refuse)]
    fields={'ctx':None,'bytes':keep[0]}
    for name,_,args,result in HOOKS:
        fields[name]=O.hook_proto(args,result)(refuse); keep.append(fields[name])
    hooks=O.Hooks(**fields)
    for symbol,_,_,_ in FUNCS.values():
        for table,fault in ((C.byref(hooks),O.Fault(0x1234,7)),(None,O.Fault()),(C.byref(hooks),None)):
            assert getattr(O.NATIVE,symbol)(table,C.byref(fault) if fault else None)==-1
            assert not touched
            if fault is not None: assert (fault.address,fault.code)==((0x1234,7) if table else (0,0))
    selected=cases(); only=os.environ.get('EM_LEVEL5_STATE_ONLY')
    if only: selected=[c for c in selected if c[0].startswith(only)]
    O.CASES[:]=selected
    results=O.run_cases(selected)
    failures=[(label,errors) for label,_,errors,_,_,_ in results if errors]
    seen=set().union(*(r[3] for r in results))
    words=set().union(*(O.reachable_words(O.image_for(e),e,f[1]) for e,f in FUNCS.items()))
    missing=words-seen
    print(f'001E7780 + 001D0660: {len(words&seen)}/{len(words)} reachable words')
    if not only:
        if missing: failures.append(('coverage',sorted(missing)))
        failures.extend(('callee inventory',p) for p in O.callee_checks())
        if not failures:
            sites,missing=O.hook_sites(results)
            if missing: failures.append(('unreached hooks',missing))
            # Also refuse accesses in the record loop, after each worker.
            sites += [(i,[]) for i,c in enumerate(O.CASES)
                      if (c[0].startswith('count/') and c[0].endswith('/3')) or c[0].startswith('chain/')]
            for problems,runs in O.reference_mode.parallel_map(O.hook_contract_site,sites):
                if problems: failures.append(('fail-stop',problems))
    for label,errors in failures[:8]: print('FAIL',label,errors)
    assert not failures, f'{len(failures)} failures'
    print(f'PASS {len(selected)} cases: original calls, ordered accesses, whole memory, poisoned starts and fail-stop')


if __name__=='__main__': main()
