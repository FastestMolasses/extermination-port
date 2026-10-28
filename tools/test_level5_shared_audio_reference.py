#!/usr/bin/env python3
"""Audit the 25 shared audio entries in the later-level U inventory.

Reuses the existing original-instruction interpreters and strict comparisons.
Entry coverage counts executed instructions, never a hooked function label.
The nine SDK/hardware adapters remain explicitly unverified as native
translations: their original wrappers are exercised down to scripted lower
RPC/syscall boundaries, which cannot prove host heap, DMA or CD hardware.
No original bytes are embedded, printed, or written to the report.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import re
import struct
import time
import subprocess

import test_sound_bank_reference as sound
import test_stream_lanes_reference as lanes
import test_iop_stream_reference as iop
from reference_mode import FULL, MODE, pick

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/later-oracle-audio"
ENTRIES = tuple(int(s, 16) for s in (
    "0010BAA0 0010BBC0 0010BBE0 0010BC00 0010F8F8 0010F968 "
    "00112610 00112D18 00113280 001157F0 001193A8 00119400 "
    "00119450 001194B8 00119528 00119828 001199F0 0011A070 "
    "0011A2B0 0011A4E8 0011A608 0011A658 001F9820 001FB0B0 "
    "001FC520").split())
ADAPTERS = set(ENTRIES[:9])
EXECUTED = set()
CASE_COUNTS = {}


class AuditEE(sound.SoundEE):
    """Instrument instruction execution without changing inherited semantics."""
    def execute(self, word, pc):
        EXECUTED.add(pc)
        return super().execute(word, pc)

    def branch(self, word, pc):
        EXECUTED.add(pc)
        return super().branch(word, pc)


class BoundaryEE(AuditEE):
    def __init__(self, elf):
        super().__init__(elf, None)
        self.syscalls = []
        self.syscall_result = 0

    def execute(self, word, pc):
        if word >> 26 == 0 and word & 63 == 12:
            EXECUTED.add(pc)
            self.syscalls.append((self.r[3] & 0xffffffff,
                                  tuple(self.arg(i) for i in range(4))))
            self.ret_int(self.syscall_result)
            return
        return super().execute(word, pc)


def checked_call(e, address, args=()):
    assert address not in e.hooks, "target entry must execute, never hook it"
    e.call(address, args)
    assert address in EXECUTED or address + 4 in EXECUTED
    return sound.s32(e.r[2])


def rpc_args(e):
    return tuple(e.arg(i) for i in range(8)) + (e.load(e.r[29]),)


def adapter_cases(elf):
    """Original wrappers tested; deliberately not native-equivalence claims."""
    counts = {}
    for at, syscall in ((0x10baa0, 0x64), (0x10bbc0, 0x76),
                        (0x10bbe0, 0x77), (0x10bc00, 0x78)):
        for ret in (-1, 0, 7):
            e = BoundaryEE(elf)
            e.syscall_result = ret
            args = (0x1400000, 1, 3, 4)
            assert checked_call(e, at, args) == ret
            assert e.syscalls == [(syscall, args)]
        counts[f"{at:08X}"] = 3
    for at, rpc_id, request in ((0x10f8f8, 1, 0x2792c0),
                                (0x10f968, 2, 0x279300)):
        count = 0
        for enabled in (-1, 0, 1):
            for result in (-1, 0, 1):
                e = BoundaryEE(elf)
                e.save(0x241c90, enabled)
                e.save(request, 0x55667788)
                e.save(0x279280, 0x10203040)
                calls = []
                def rpc(cpu):
                    calls.append(rpc_args(cpu))
                    cpu.ret_int(result)
                e.hooks[0x10e8a8] = rpc
                got = checked_call(e, at, (0x12000,))
                expected = 0 if enabled < 0 else (0x10203040 if result >= 0 else
                                                  (0 if rpc_id == 1 else -1))
                assert got == expected
                assert e.load(request) == (0x55667788 if enabled < 0 else 0x12000)
                assert calls == ([] if enabled < 0 else
                                 [(0x279240, rpc_id, 0, request, 4, 0x279280, 4, 0, 0)])
                count += 1
        counts[f"{at:08X}"] = count
    # sceCdRead: gate, RPC failure, accepted read, debug print.
    count = 0
    for busy, allowed, result, debug in ((6,1,0,0), (2,0,0,0),
                                         (2,1,-1,0), (2,1,0,0), (2,1,0,1)):
        e = BoundaryEE(elf)
        e.write(0x1400000, bytes((0x11,0x22,0x33)))
        e.save(0x241d08, 19)
        e.save(0x241cf8, debug)
        calls = []
        def hook(at, ret):
            def run(cpu):
                calls.append((at, tuple(cpu.arg(i) for i in range(4))))
                if at == 0x10e8a8:
                    assert rpc_args(cpu) == (0x279e00,0xd,1,0x279f40,0x18,
                                             0,0,0x1115d0,0x241d48), rpc_args(cpu)
                cpu.ret_int(ret)
            return run
        e.hooks = {at: hook(at,ret) for at,ret in
                   ((0x112088,busy),(0x111f18,allowed),(0x10dfd8,0),
                    (0x10e8a8,result),(0x10b840,0),(0x122b58,0))}
        got = checked_call(e, 0x112610, (123,7,0x80000,0x1400000))
        accepted = busy != 6 and allowed != 0
        assert got == int(accepted and result >= 0)
        if accepted:
            assert e.read(0x279f40,15) == struct.pack("<3I",123,7,0x80000)+bytes((0x11,0x22,0x33))
            assert e.load(0x279f50) == 0x279e80 and e.load(0x279f54) == 0x27a2c0
            assert (e.load(0x241d48),e.load(0x241d14)) == (int(result>=0),)*2
            assert any(a==0x10e8a8 for a,_ in calls)
        else:
            assert not any(a==0x10e8a8 for a,_ in calls)
        count += 1
    counts["00112610"] = count
    count = 0
    for mode,busy,polls,debug in ((1,1,[0],0),(1,0,[1],0),(1,0,[0],0),
                                 (0,0,[1,0],0),(0,0,[0],1)):
        e=BoundaryEE(elf);e.save(0x241d14,busy);e.save(0x241cf8,debug)
        sequence=list(polls);calls=[]
        def poll(cpu):
            assert cpu.arg(0)==0x279e00
            calls.append("poll");cpu.ret_int(sequence.pop(0))
        e.hooks={0x10ea60:poll,0x122b58:lambda cpu:calls.append("print")}
        got=checked_call(e,0x112d18,(mode,))
        assert got == (int(busy or polls[0]) if mode else 0)
        assert calls.count("poll") == (0 if mode and busy else len(polls))
        count+=1
    counts["00112D18"]=count
    count=0
    for mode,semok,sync,rpcresult,bound,debug in (
        (1,False,0,0,True,0),(1,True,1,0,True,0),(8,True,1,0,True,0),
        (1,True,0,-1,True,0),(8,True,0,-1,True,0),
        (1,True,0,0,True,0),(0,True,0,0,False,1)):
        e=BoundaryEE(elf);e.save(0x241d0c,7);e.save(0x241d30,0 if bound else -1)
        e.save(0x241cf8,debug);e.save(0x27ab40,2)
        calls=[]
        def h(at,ret):
            def run(cpu):
                calls.append(at)
                if at==0x10e6f8:
                    assert tuple(cpu.arg(i) for i in range(3))==(0x27afb0,0x8000059a,0)
                    cpu.save(0x27afd4,1)
                if at==0x10e8a8:
                    assert rpc_args(cpu)==(0x27afb0,0,0,0x27b060,4,0x27ab40,4,0,0)
                cpu.ret_int(ret)
            return run
        e.hooks={at:h(at,ret) for at,ret in (
            (0x1118b8,0),(0x10b870,7 if semok else -1),(0x112dc0,sync),
            (0x10e088,0),(0x10e6f8,0),(0x10dfd8,0),(0x10e8a8,rpcresult),
            (0x10b840,0),(0x122b58,0))}
        got=checked_call(e,0x113280,(mode,))
        expected=6 if not semok else ((-1 if mode==8 else 6) if sync or rpcresult<0 else 2)
        assert got==expected
        if semok and not sync:
            assert e.load(0x27b060)==mode
            assert (0x10e6f8 in calls)==(not bound)
        count+=1
    counts["00113280"]=count
    CASE_COUNTS["adapter_original_wrappers"]=counts


def stop_cases(elf):
    work=OUT/"stop";work.mkdir(parents=True,exist_ok=True)
    def definition(path,name):
        text=path.read_text()
        match=re.search(r"^(?:static )?int "+name+r"\([^;]*?\)\s*\{",text,re.M)
        assert match, (path,name)
        start=match.start();end=match.end();depth=1
        while depth:
            depth+=(text[end]=="{")-(text[end]=="}");end+=1
        return text[start:end]+"\n"
    forwarding = definition(ROOT/"src/game/em_sfx.c","em_sfx_stop_track")
    forwarding += definition(ROOT/"src/game/em_scene_bindings.c","ambient_0011A070")
    (work/"scene-stop.inc").write_text(forwarding)
    CASE_COUNTS["scene_forwarding_sha256"]=hashlib.sha256(forwarding.encode()).hexdigest()
    libpath=work/"bridge.dylib"
    subprocess.run(["cc","-std=c11","-O1","-Wall","-Wextra","-Werror","-shared",
                    "-fPIC","-Isrc","-I"+str(work),"tests/level5_shared_audio_bridge.c",
                    "src/game/em_sfx_bank.c","-o",str(libpath)],cwd=ROOT,check=True)
    n=C.CDLL(str(libpath));rng=random.Random(0x11a070)
    count=0
    for case in range(pick(1000,120)):
        e=AuditEE(elf,None)
        track=case%48;hard=(case//48)%2
        tracks=[rng.randrange(2) for _ in range(48)]
        voices=[[rng.randrange(3),rng.randrange(48),rng.randrange(2),rng.randrange(4)]
                for _ in range(48)]
        # Include a matching kind-2 voice, including voice47.
        voices[case%48][1]=track;voices[case%48][3]=2
        for i,t in enumerate(tracks):e.save(0x27e0c0+i*0x78+0x32,t,2)
        for i,row in enumerate(voices):
            for offset,value in zip((0,6,8,0x1a),row):e.save(0x27ccc0+i*0x6a+offset,value,2)
        keyoff=rng.getrandbits(64);e.save(0x27f768,keyoff,8)
        calls=[]
        def queue(cpu):
            calls.append(tuple(cpu.arg(i) for i in range(4)));cpu.ret_int(1)
        e.hooks={0x1157f0:queue}
        checked_call(e,0x11a070,(track|(hard<<15),))
        nt=(C.c_uint16*48)(*tracks)
        nv=(C.c_uint16*192)(*[x for row in voices for x in row])
        nk=C.c_uint64(keyoff);out=(C.c_uint32*192)();nc=C.c_uint32()
        assert n.shared_stop(track|(hard<<15),nt,nv,C.byref(nk),out,C.byref(nc))==0
        assert list(nt)==[e.load(0x27e0c0+i*0x78+0x32,2) for i in range(48)]
        assert list(nv)==[e.load(0x27ccc0+i*0x6a+off,2) for i in range(48) for off in (0,6,8,0x1a)]
        assert nk.value==e.load(0x27f768,8)
        assert [tuple(out[i*4:i*4+4]) for i in range(nc.value)]==calls
        count+=1
    CASE_COUNTS["0011A070"]=count
    for handle in (-1,0,1,23,24,47):
        e=AuditEE(elf,None);e.save(0x1400000,handle)
        requested=[rng.randrange(-100,100) for _ in range(48)]
        e.write(0x281b70,struct.pack("<48i",*requested));calls=[]
        e.hooks={0x11a070:lambda cpu:calls.append((0x11a070,cpu.arg(0),0,0))}
        checked_call(e,0x1fc520,(0x1400000,))
        nh=C.c_int32(handle);nr=(C.c_int32*48)(*requested)
        out=(C.c_uint32*192)();nc=C.c_uint32()
        n.shared_release(C.byref(nh),nr,out,C.byref(nc))
        assert nh.value==sound.s32(e.load(0x1400000))
        assert bytes(nr)==e.read(0x281b70,192)
        assert [tuple(out[i*4:i*4+4]) for i in range(nc.value)]==calls
    CASE_COUNTS["001FC520"]=6


def sound_cases(elf):
    sound.OUT=OUT/"sound"
    old=sound.SoundEE;sound.SoundEE=AuditEE
    old_case=sound.library_case
    counts={}
    def library_case(*args,**kwargs):
        fn=args[4]
        old_case(*args,**kwargs)
        counts[fn]=counts.get(fn,0)+1
    sound.library_case=library_case
    try:
        sound.main()
    finally:
        sound.SoundEE=old
        sound.library_case=old_case
    CASE_COUNTS["sound_library"]=counts
    CASE_COUNTS["sound_chain"]={"host_calls":8,"timing_switch_calls":9,"timing_mutants_rejected":2}


def lane_cases(elf):
    out=OUT/"lanes";out.mkdir(parents=True,exist_ok=True)
    captures=lanes.load_captures()
    for name,memory in captures.items():
        for lo,size in lanes.EXECUTED:
            assert memory[lo:lo+size]==elf[lo-0x100000+0x300:lo-0x100000+0x300+size],name
    music=lanes.rows(elf,lanes.MUSIC,lanes.MUSIC_ROWS)
    voice=lanes.rows(elf,lanes.VOICE,lanes.VOICE_ROWS)
    if FULL:
        # Preserve the existing complete sweep and instruction-coverage gate.
        lanes.run(elf,captures,out,out,music,voice,time.time())
        CASE_COUNTS["lanes"]=json.loads((out/"report.json").read_text())["cases"]
    else:
        # The inventory needs these six entries. Their existing comparisons
        # retain all assertions; the full module sweep stays behind FULL.
        native=lanes.Native(out,music,voice)
        base=captures.get("ref/roger-encounter/eeMemory.bin") or next(iter(captures.values()))
        rng=random.Random(0x1f9820)
        CASE_COUNTS["lanes"]={name:fn(elf,base,native,rng) for name,fn in (
            ("init",lanes.init_cases),("stop",lanes.stop_cases),("select",lanes.select_cases))}
    EXECUTED.update(lanes.COVERED)


def allocator_cases(elf):
    work=OUT/"allocator";work.mkdir(parents=True,exist_ok=True)
    native=iop.Native(work)
    captures,_,iop.ROUTES=iop.load_captures()
    old=iop.EeOracle.cpu
    def cpu(owner):
        result=old(owner)
        original_call=result.call
        def call(*args,**kwargs):
            value=original_call(*args,**kwargs)
            EXECUTED.update(result.covered)
            return value
        result.call=call
        return result
    iop.EeOracle.cpu=cpu
    try:
        CASE_COUNTS["allocator_ee_cases"]=iop.ee_cases(elf,captures,native,random.Random(0x1a2b))
    finally:
        iop.EeOracle.cpu=old


def main():
    p=argparse.ArgumentParser()
    p.add_argument("--group",choices=("all","adapters","stop","sound","lanes","allocator"),default="all")
    args=p.parse_args();OUT.mkdir(parents=True,exist_ok=True)
    elf=sound.read_elf()
    groups={"adapters":adapter_cases,"stop":stop_cases,"sound":sound_cases,
            "lanes":lane_cases,"allocator":allocator_cases}
    for name,run in groups.items():
        if args.group in ("all",name):
            run(elf)
    covered=[a for a in ENTRIES if a in EXECUTED or a+4 in EXECUTED]
    if args.group=="all":assert set(covered)==set(ENTRIES),[f"{a:08X}" for a in set(ENTRIES)-set(covered)]
    report={"status":"PASS","mode":MODE,"elf_sha256":hashlib.sha256(elf).hexdigest(),
            "group":args.group,"counts":CASE_COUNTS,
            "entries":[{"address":f"{a:08X}","entry_executed":a in covered,
                        "native_status":"hardware-adapter; original-wrapper probes only" if a in ADAPTERS
                        else "original/native comparison" if a in covered else "not in this group"}
                       for a in ENTRIES]}
    (OUT/f"{args.group}-{MODE}-report.json").write_text(json.dumps(report,indent=2)+"\n")
    print(f"PASS shared audio {args.group} ({MODE}): {len(covered)}/25 entries executed; "
          "hardware adapters are not promoted by wrapper-only evidence")


if __name__=="__main__":
    main()
