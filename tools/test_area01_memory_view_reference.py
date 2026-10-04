#!/usr/bin/env python3
"""Canonical callback boundary tests; instruction oracles are opt-in in each
existing suite with EM_AREA01_CANONICAL_VIEW=1. These synthetic fixtures check
provider authority, exact access widths/directions, partial writes and faults.
"""
import ctypes as C
import subprocess
from pathlib import Path
import area01_reference_view as AV
import test_area01_sys_reference as S
import test_area01_exita_reference as A
import test_area01_exitb_reference as B
import test_area01_room_reference as R
import test_area01_side_reference as D

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/level2/memory-view'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    libpath = OUT / 'contract.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC', '-Isrc',
                    'tests/area01_memory_view_bridge.c', '-o', str(libpath)], cwd=ROOT, check=True)
    lib = C.CDLL(str(libpath))
    lib.mv_helper.argtypes = [C.c_int, C.c_int, AV.VIEW, C.c_void_p, C.c_void_p, C.POINTER(C.c_uint32)]
    count = 0
    # Live and fallback storage deliberately differ. Refusal must never use
    # the fallback, including for writes; quadword addresses must be aligned.
    for family in range(3):
        for op in range(10 if family == 0 else 9):
            for deny in (False, True):
                live = (C.c_uint8*32)(*([0x55]*32)); fallback = (C.c_uint8*32)(*([0xCC]*32))
                calls = []
                def resolve(ctx, address, size, write):
                    calls.append((ctx, address, size, write))
                    return None if deny else C.addressof(live)
                cb = AV.VIEW(resolve); fault = (C.c_uint32*4)()
                rc = lib.mv_helper(family, op, cb, 0xBA5E, fallback, fault)
                invalid = family == 0 and op >= 8
                n = (1,4,1,2,4,8,16,16,2)[op] if not invalid else 0
                write = int(op in (2,3,4,5,7))
                assert calls == ([] if invalid else [(0xBA5E,0x1000,n,write)]), (family,op,calls)
                assert bytes(fallback) == bytes([0xCC]*32)
                if deny or invalid:
                    assert rc == -1 and tuple(fault[:3]) == (4,0xABCDEF,0xFFFFFFFF if op == 8 and not family else 0x1000)
                    assert bytes(live) == bytes([0x55]*32)
                else:
                    assert rc == 0 and tuple(fault[:3]) == (0,0,0)
                    if write: assert bytes(live)[:n] != bytes([0x55]*n)
                    elif op not in (6,): assert fault[3] == int.from_bytes(bytes([0x55]*n),'little')
                count += 1
    for module, typ, entry, base, expected in (
        (S, S.Sys, '001287F0', 0x1000, [(0x10F8,2,0),(0x10F8,2,1)]),
        (A, A.Ctx, '00128390', 0x810700, [(0x81070A,1,0)]),
        (B, B.Exitb, '001BB520', 0x1000, [(0x1030,4,1),(0x102E,1,0),(0x1034,2,1),(0x102E,2,1)]),
    ):
        native=module.build_native(); prefix='em_area01_'+('sys' if module is S else 'exita' if module is A else 'exitb')
        fn=getattr(native,prefix+'_'+entry); clear=getattr(native,prefix+'_clear_fault')
        for deny in ('none','all','write'):
            live=(C.c_uint8*0x200)(); fallback=(C.c_uint8*0x200)(*([0xCC]*0x200))
            calls=[]; worker_calls=[]
            def worker(ctx,c):
                worker_calls.append(c.contents.fn); c.contents.v0=0x1234; return 0
            worker_cb=module.WORKER(worker)
            def resolve(ctx,address,size,write):
                assert ctx == 0xBA5E
                calls.append((address,size,write))
                if deny=='all' or (deny=='write' and write): return None
                assert base<=address and address+size<=base+len(live)
                return C.addressof(live)+address-base
            cb=AV.VIEW(resolve)
            regions=(module.Region*1)(module.Region(base,len(fallback),C.cast(fallback,C.POINTER(C.c_uint8))))
            state=typ(regions,1,worker_cb,0xBA5E,0x7F010000,0,0,0,C.cast(cb,C.c_void_p).value)
            out=module.I32() if module is not B else C.c_uint64()
            def run():
                if module is S: return fn(C.byref(state),0x1000,0x1000,3,0)
                if module is A: return fn(C.byref(state),0,1,C.byref(out))
                return fn(C.byref(state),0x1000,C.byref(out))
            rc=run(); bad=next((i for i,x in enumerate(expected) if deny=='all' or deny=='write' and x[2]),None)
            assert calls==expected[:bad+1] if bad is not None else calls==expected
            assert bytes(fallback)==bytes([0xCC]*len(fallback))
            if bad is not None:
                assert rc==-1 and (state.fault,state.fault_function,state.fault_address)==(3,int(entry,16),expected[bad][0])
                saved=(calls[:],worker_calls[:],bytes(live)); assert run()==-1
                assert (calls,worker_calls,bytes(live))==saved
                clear(C.byref(state)); assert not state.fault
            else:
                assert rc==0 and not state.fault
                if module is S: assert bytes(live)[0xF8:0xFA]==b'\x03\0'
                if module is A: assert out.value==30
                if module is B: assert out.value==0x1234 and int.from_bytes(bytes(live)[0x30:0x34],'little')==0x2755E8
                # Callback-only path must work with NULL arrays as in live binding.
                state.regions=None;state.region_count=0
                assert run()==0
            count += 1
    # ROOM's duct camera exercises its only rd/wr paths, including scratch
    # stores. Callees are explicit no-op boundaries; the body has a separate
    # full original-instruction oracle with real callees.
    room_native=R.build_native()
    expected=[(0x1014,4,0),(0x1014,4,1)]+[(0x70003600+4*i,4,1) for i in range(4)]+[
        (0x1001,1,0),(0x1001,1,1),(0x1002,1,1)]
    for deny in ('none','all','write'):
        live=(C.c_uint8*0x320)();scratch=(C.c_uint8*16)()
        fallback=(C.c_uint8*0x320)(*([0xCC]*0x320));calls=[]
        def resolve_room(ctx,address,size,write):
            assert ctx==0xBA5E;calls.append((address,size,write))
            if deny=='all' or (deny=='write' and write):return None
            for base,data in ((0x1000,live),(0x70003600,scratch)):
                if size and address>=base and address+size<=base+len(data):return C.addressof(data)+address-base
            return None
        cb=AV.VIEW(resolve_room);work=R.WORKER(lambda *args:0)
        regions=(R.Region*1)(R.Region(0x1000,len(fallback),C.cast(fallback,C.POINTER(C.c_uint8))))
        state=R.Room(regions,1,work,0xBA5E,0x7F010000,0,0,0,C.cast(cb,C.c_void_p).value)
        rc=room_native.em_area01_room_00198D90(C.byref(state),0x1000,0x1000)
        bad=next((i for i,x in enumerate(expected) if deny=='all' or deny=='write' and x[2]),None)
        assert calls==(expected[:bad+1] if bad is not None else expected),(deny,calls)
        assert bytes(fallback)==bytes([0xCC]*len(fallback))
        if bad is not None:
            assert rc==-1 and (state.fault,state.fault_address)==(3,expected[bad][0])
            before=calls[:];assert room_native.em_area01_room_00198D90(C.byref(state),0x1000,0x1000)==-1
            assert calls==before
        else:
            assert rc==0 and bytes(scratch)==bytes.fromhex('00000000000000000000a04000000000')
            assert live[1]==1 and live[2]==0
        count+=1
    # SIDE's inventory leaf stores request bytes after a byte-width update.
    # A supplied canonical view remains authoritative over usable regions.
    side_native=D.build_native()
    expected=[(0x810CBA,1,0),(0x810CBA,1,1),(0x8106B0,1,1),(0x8106B1,1,1)]
    for deny in ('none','all','write'):
        live=(C.c_uint8*0x1000)();fallback=(C.c_uint8*0x1000)(*([0xCC]*0x1000));calls=[]
        def resolve_side(ctx,address,size,write):
            assert ctx==0xBA5E;calls.append((address,size,write))
            if deny=='all' or deny=='write' and write:return None
            assert 0x810000<=address and address+size<=0x811000
            return C.addressof(live)+address-0x810000
        cb=AV.VIEW(resolve_side);work=D.WORKER(lambda *args:0)
        regions=(D.Region*1)(D.Region(0x810000,len(fallback),C.cast(fallback,C.POINTER(C.c_uint8))))
        state=D.Side(regions,1,work,0xBA5E,0x7F010000,0,0,0,C.cast(cb,C.c_void_p).value)
        out=D.I32();fn=side_native.em_area01_side_001C4720
        rc=fn(C.byref(state),2,3,C.byref(out))
        bad=next((i for i,x in enumerate(expected) if deny=='all' or deny=='write' and x[2]),None)
        assert calls==(expected[:bad+1] if bad is not None else expected),(deny,calls)
        assert bytes(fallback)==bytes([0xCC]*len(fallback))
        if bad is not None:
            assert rc==-1 and state.fault_address==expected[bad][0]
            before=calls[:];assert fn(C.byref(state),2,3,C.byref(out))==-1 and calls==before
        else:
            assert rc==0 and live[0xCBA]==3 and bytes(live)[0x6B0:0x6B2]==b'\2\2'
            state.regions=None;state.region_count=0
            assert fn(C.byref(state),2,3,C.byref(out))==0 and live[0xCBA]==6
        count+=1
    lib.mv_overlay.argtypes=[C.c_int,AV.VIEW,C.c_void_p,C.c_void_p,C.POINTER(C.c_uint32)]
    for op in range(9):
        for deny in (False,True):
            live=(C.c_uint8*32)(*([0x55]*32));fallback=(C.c_uint8*32)(*([0xCC]*32));calls=[]
            def overlay_view(ctx,address,size,write):
                assert ctx==C.addressof(fallback)
                calls.append((address,size,write))
                return None if deny else C.addressof(live)
            cb=AV.VIEW(overlay_view);fault=(C.c_uint32*3)()
            rc=lib.mv_overlay(op,cb,C.addressof(fallback),fallback,fault)
            expected=[] if op>=7 else [(0x1000,(1,2,4,1,2,4,4)[op],int(op>=3))]
            if op==6 and not deny:expected.append((0x1000,4,0))
            assert calls==expected,(op,deny,calls)
            assert bytes(fallback)==bytes([0xCC]*32)
            if deny or op>=7:
                assert rc==-1 and tuple(fault[:2])==(5,0xFFFFFFFE if op==7 else 0x1000)
                assert bytes(live)==bytes([0x55]*32)
            else:
                assert rc==0 and not fault[0]
                if op==6:assert fault[2]==0x12345678
                else:assert bytes(live)==bytes([0x55]*32)
            count+=1
    print(f'area01 canonical view boundary: PASS ({count} checks; render/UI/FX/ROOM/SIDE/overlay widths, strict refusal, ctx, faults/latch, no fallback)')


if __name__=='__main__': main()
