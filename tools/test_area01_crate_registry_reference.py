#!/usr/bin/env python3
"""Borrowed AREA01 crate registry and taken bits versus original 001551B0.

Only the new registry/taken boundary is native in addition to the existing
crate/SDK translations. Model/collision/draw/sound/effect workers remain
explicit scripted boundaries, as in test_crate_original_reference.py.
Captured records are oracle fixtures; runtime uses boot export and loader.
"""
import ctypes as C
import hashlib
from pathlib import Path
import struct
import subprocess

import reference_mode as mode
import test_crate_original_reference as T

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/level2/crate-registry'
VIEW=C.CFUNCTYPE(C.c_void_p,C.c_void_p,C.c_uint32,C.c_uint32,C.c_int)
BASE=0x7AAE60
SELECTOR=0x24A852
TABLE_WORD=0x24D824
TABLE=0x829848
GROUP=0x829360
END=GROUP+4*0x2C+2

def u32(b,a):return struct.unpack_from('<I',b,a)[0]
def build():
    OUT.mkdir(parents=True,exist_ok=True)
    sources=['tests/area01_crate_registry_bridge.c','src/game/em_crate_original.c',
             'src/game/em_actor_roster.c','src/game/em_owner_services_original.c',
             'src/game/em_owner_draw_original.c','src/game/em_roger_actor_original.c',
             'src/game/em_startup_load_gaps.c','src/game/em_item_sdk_math.c',
             'src/game/em_interaction_scan.c','src/game/em_item_trail.c','src/game/em_effect_original.c']
    target=OUT/'bridge.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
                    '-shared','-fPIC','-Isrc','-Wl,-dead_strip','-Wl,-exported_symbol,_cr_registry_*',
                    *sources,'-lm','-o',str(target)],cwd=ROOT,check=True)
    n=C.CDLL(str(target))
    n.cr_registry_bind.argtypes=[VIEW,C.c_void_p]
    n.cr_registry_view.restype=C.POINTER(T.Registry)
    n.cr_registry_progress.argtypes=[C.c_uint8,C.c_void_p]
    n.cr_registry_taken.argtypes=[C.c_uint8]
    n.cr_registry_tick.argtypes=[C.POINTER(T.Crate),C.POINTER(T.Input),C.POINTER(T.Hooks)]
    n.cr_registry_group.argtypes=[C.c_uint8,C.c_int16];n.cr_registry_group.restype=C.c_void_p
    n.cr_registry_fault.restype=C.c_uint32
    return n

class Fixture:
    def __init__(self,ram):
        # Exactly the two newly exported boot spans and the module-owned
        # initialized overlay data span, no captured RAM arena in native code.
        self.regions=[]
        for a,b in ((SELECTOR,SELECTOR+2),(TABLE_WORD,TABLE_WORD+4),(0x828A00,0x82CD00)):
            buf=(C.c_uint8*(b-a)).from_buffer_copy(ram[a:b]);self.regions.append((a,b,buf))
        self.access=[];self.deny=None
        @VIEW
        def view(_,a,z,w):
            self.access.append((a,z,w))
            if w or self.deny==a:return None
            for lo,hi,buf in self.regions:
                if lo<=a and a+z<=hi:return C.addressof(buf)+a-lo
            return None
        self.view=view
    def pointer(self,a):
        for lo,hi,b in self.regions:
            if lo<=a<hi:return C.addressof(b)+a-lo
        raise AssertionError(hex(a))
    def write(self,a,data):C.memmove(self.pointer(a),data,len(data))
    def bind(self,n):n.cr_registry_bind(self.view,None)

def tick(n,elf,ram,f,mask=0,busy=False,flags=None,link=None,sub=0):
    w=T.World(elf,ram,BASE,None,1,sub);o=w.o
    c=T.crate_from(o,BASE);c.state=0
    if flags is not None:c.placement=flags
    if link is not None:c.link=link
    assert c.model==6
    progress=bytearray(ram[0x810758:0x810B60])
    # Explicit counterfactual taken-bit controls over this actual group.
    word_at=0x810860+0x20+3*4
    word=u32(ram,word_at)
    word=(word&~(15<<16))|(mask<<16)
    struct.pack_into('<I',progress,word_at-0x810758,word)
    p=(C.c_uint8*len(progress)).from_buffer_copy(progress)
    n.cr_registry_progress(1,p)
    for k,v in enumerate(progress):o.save(0x810758+k,v,1)
    T.store_fields(o,BASE,c,T.FIELDS);w.seed(o)
    fixed=dict(allocate=[int(busy)],probe=[(4,0,0.0)],place=[list(c.world)])
    logs,children=[],[]
    T.crate_workers(o,T.Script(17,**fixed),logs,BASE,children)
    # Execute actual original 001B11E0, retaining only its ordered call log.
    del o.calls[0x1B11E0]
    o.watch[0x1B11E0]=lambda e:logs.append(('taken',e.arg(0)&255))
    o.r[16]=0x7AB150;o.writes.clear();o.run(T.CRATE,(BASE,))
    expected=T.oracle_image(o,BASE,T.FIELDS)
    actual_log,spawned=[],[]
    h,keep=T.native_hooks(T.Script(17,**fixed),actual_log,c,spawned)
    @T.BYTE
    def taken(_,puid):
        actual_log.append(('taken',puid));return n.cr_registry_taken(puid)
    h.taken=taken
    table=T.rattle_table(o)
    inp=T.Input(1,sub,1,None,0,None,C.cast(table,C.POINTER(T.RATTLE)),7)
    f.access.clear();result=n.cr_registry_tick(C.byref(c),C.byref(inp),C.byref(h))
    assert result==1
    assert T.image(c,T.FIELDS)==expected,(mask,busy,flags,link,T.image(c,T.FIELDS),expected)
    assert actual_log==logs,(actual_log,logs)
    assert not children and not spawned
    needed=not busy and c.link>=0 and (flags if flags is not None else 0x1A01)&1
    expected_access=[(SELECTOR,2,0),(TABLE_WORD,4,0),(TABLE+8,4,0)]
    expected_access += [(GROUP+k*0x2C,2,0) for k in range(5)]
    expected_access += [(GROUP,4*0x2C+2,0)]
    assert f.access==(expected_access if needed else []),f.access
    assert not n.cr_registry_fault()
    allowed={BASE+x for x in T.field_ranges(T.FIELDS)}|set(range(T.BONE+0x90,T.BONE+0xD0))
    stray=[a for a in o.writes if a not in allowed and not T.SCRATCH[0]<=a<T.SCRATCH[1]
           and not T.STACK-0x2000<=a<T.STACK]
    assert not stray,[hex(a) for a in stray[:8]]
    T.check_code(elf,ram,o.pcs)
    return len(logs)

def boundaries(n,ram):
    f=Fixture(ram);f.bind(n)
    ptr=n.cr_registry_group(1,0)
    assert ptr==f.pointer(GROUP)
    assert C.string_at(ptr,END-GROUP)==ram[GROUP:END]
    # Re-read changed bytes and different providers: no cached registry copy.
    f.write(GROUP+2,b'\x7f');assert C.string_at(n.cr_registry_group(1,0)+2,1)==b'\x7f'
    g=Fixture(ram);g.bind(n);assert n.cr_registry_group(1,0)==g.pointer(GROUP)
    assert C.string_at(n.cr_registry_group(1,0)+2,1)==b'\x70'
    # 0 -> 1 count fallback: change only the selected table word.
    g.write(SELECTOR,b'\0\0');g.write(TABLE+4,struct.pack('<I',GROUP))
    assert n.cr_registry_group(1,0)==g.pointer(GROUP)
    assert (TABLE+4,4,0) in g.access
    refusals=0
    for address in (SELECTOR,TABLE_WORD,TABLE+8,GROUP,GROUP+0x2C,GROUP+4*0x2C):
        f=Fixture(ram);f.deny=address;f.bind(n)
        assert not n.cr_registry_group(1,0)
        assert n.cr_registry_fault()==address
        before=list(f.access)
        assert not n.cr_registry_group(1,0) and f.access==before
        refusals+=1
    f=Fixture(ram);f.bind(n);n.cr_registry_reset()
    assert not n.cr_registry_view() and not n.cr_registry_group(1,0)
    assert not n.cr_registry_fault() and not f.access
    f.bind(n);n.cr_registry_bind(VIEW(),None)
    assert not n.cr_registry_view() and not n.cr_registry_group(1,0)
    return refusals

def main():
    mode.banner('AREA01 crate registry')
    elf=(T.DECOMP/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest()==T.ELF_SHA
    paths=sorted((T.DECOMP/'build/s87/route_a01').glob('*/eeMemory.bin'))
    paths+=sorted((T.DECOMP/'build/s87/side_a01').glob('*/eeMemory.bin'))
    paths=[p for p in paths if p.read_bytes()[0x810700]==1]
    assert paths
    n=build();ticks=calls=0
    for path in paths:
        ram=path.read_bytes();assert u32(ram,TABLE_WORD)==TABLE
        assert struct.unpack_from('<h',ram,SELECTOR)[0]==2 and u32(ram,TABLE+8)==GROUP
        assert ram[GROUP:END:0x2C]==bytes([1,1,1,1,255])
        # Actual placement7, the sole first-visit crate with the registry bit.
        assert ram[0x82BE68+6:0x82BE68+8]==b'\x01\x1a'
        f=Fixture(ram);f.bind(n)
        for mask in (range(16) if mode.FULL else (0,1,7,15)):
            calls+=tick(n,elf,ram,f,mask);ticks+=1
        for args in (dict(busy=True),dict(flags=0x1A00),dict(link=-1)):
            calls+=tick(n,elf,ram,f,**args);ticks+=1
    denied=boundaries(n,ram)
    print(f'AREA01 crate registry: PASS {ticks} original owner ticks / {calls} worker calls '
          f'across {len(paths)} captures, {denied} strict refusals, canonical alias/rebind/reset')
if __name__=='__main__':main()
