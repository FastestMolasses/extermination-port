#!/usr/bin/env python3
"""Explicit RCL scratch handoff: aliases, cross-view writes and restoration.
The pose/draw instruction oracle is test_area01_model_draw_reference.py.
"""
import ctypes as C
import json
import time
import test_render_context_live_reference as R

def main():
    start=time.time();R.OUT=R.ROOT/'build/level2/scratch-views'
    lib=R.build_native();lib.em_rcl_scratch_3400_bind.argtypes=[C.c_void_p]
    field=(C.c_uint8*2)()
    assert lib.em_rcl_init(str(R.EXPORT).encode(),field)==0
    source=(C.c_uint32*32)(*[0x12340000+i for i in range(32)])
    address=C.addressof(source)
    assert lib.em_rcl_scratch_3400_bind(address)==0
    ptr=lib.em_rcl_bytes(0x70003400,128)
    assert C.cast(ptr,C.c_void_p).value==address
    assert C.string_at(ptr,128)==bytes(source)
    for offset in (0,16,64,112):
        data=bytes((offset+i)&255 for i in range(16))
        assert lib.em_rcl_poke(0x70003400+offset,data,16)==0
        assert C.string_at(address+offset,16)==data
    source[7]=0xAABBCCDD
    assert bytes(lib.em_rcl_bytes(0x7000341C,4)[:4])==b'\xDD\xCC\xBB\xAA'
    assert lib.em_rcl_scratch_3400_bind(address+1)==-1
    assert C.cast(lib.em_rcl_bytes(0x70003400,128),C.c_void_p).value==address
    before=bytes(source)
    assert lib.em_rcl_scratch_3400_bind(None)==0
    restored=lib.em_rcl_bytes(0x70003400,128)
    assert C.cast(restored,C.c_void_p).value!=address
    assert C.string_at(restored,128)==before
    source[7]=0
    assert C.string_at(restored,128)==before
    other=(C.c_uint32*32).from_buffer_copy(C.string_at(restored,128))
    assert lib.em_rcl_scratch_3400_bind(C.addressof(other))==0
    assert C.string_at(lib.em_rcl_bytes(0x70003400,128),128)==before
    assert lib.em_rcl_scratch_3400_bind(None)==0
    report=dict(status='PASS',handoffs=4,cross_view_writes=5,invalid_alias_refusals=1,
                restored_bytes=128,seconds=round(time.time()-start,2))
    (R.OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('AREA01 scratch views:',json.dumps(report,sort_keys=True))
if __name__=='__main__':main()
