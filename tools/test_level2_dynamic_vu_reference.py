#!/usr/bin/env python3
"""AREA01 dynamic VU programs and native chain-page presentation.

Executes the original microcode, compares every emitted GIF byte, all VU
memory after each batch, and the native page's GS primitives/state. Reads
only local user-owned ELF and captures. Full corpus: EM_TEST_FULL=1.
"""
import ctypes as C
import os
import random
import struct
import subprocess
import sys
import time
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as RM
import test_chain_page_reference as P
import test_shadow_original_reference as S
import test_static_world_draw_reference as W
import chain_page_model as M
os.environ.setdefault('EM_TEST_JOBS', '4')
OUT = ROOT / 'build/area01/dynamic_vu'
ARRIVAL = ROOT.parent / 'Extermination/build/s87/route/15_level_exit'
ROUTE = ROOT.parent / 'Extermination/build/s87/route_a01'
NORMAL, CLIP = 0x237450, 0x237720
PAGE = 0x1EF0000
SEEDS = {}

# The shared oracle documents finite overflow saturation, but its Python
# float32 pack can raise before that clamp on dead look-ahead arithmetic.
# Apply the documented clamp before packing; no native output is consulted.
_vfp = S.vfp
def finite_vfp(value):
    if abs(value) > S.number(0x7F7FFFFF):
        return S.number(0xFF7FFFFF if value < 0 else 0x7F7FFFFF), 1, 0
    return _vfp(value)
S.vfp = finite_vfp

SHIM = r'''
#include "game/em_vu1_level_kernel.h"
#include "game/em_vu1_shadow_clip.h"
static unsigned nk, nw, meta[4*4096], words[4*128*4096];
void em_chain_page_test_kick(unsigned program, unsigned at, const void *bytes, unsigned count)
{
    if (nk >= 4096 || nw + 4*count > sizeof words/sizeof words[0]) abort();
    unsigned *k = meta + 4*nk++;
    k[0] = program; k[1] = at; k[2] = nw; k[3] = count;
    memcpy(words + nw, bytes, 16*count); nw += 4*count;
}
void kicks_reset(void) { nk = nw = 0; }
unsigned kicks_count(void) { return nk; }
const unsigned *kicks_meta(void) { return meta; }
const unsigned *kicks_words(void) { return words; }
int one_batch(unsigned program, void *data, unsigned top, const unsigned *vf, unsigned *fault)
{
    nk = nw = 0;
    if (program == 0x237450) {
        EmVu1ObjQword mem[1024]; EmVu1LvlBatch b; EmVu1LvlState s = {0};
        memcpy(mem, data, sizeof mem);
        s.known = s.known_rgbaq = s.known_w = 1;
        memcpy(s.e_prev, vf+15*4, 8); memcpy(s.s_prev, vf+16*4, 8); s.w_prev = vf[9*4+3];
        memcpy(s.rgbaq.w, vf+14*4, 16); memcpy(s.st.w, vf+2*4, 16);
        memcpy(s.tex0.w, vf+27*4, 16); memcpy(s.xyzf.w, vf+7*4, 16);
        int rc = em_vu1_dynamic_kernel_batch(&s, mem, top, &b);
        memcpy(data, mem, sizeof mem); *fault = b.fault;
        if (!rc) {
            EmVu1ObjQword packet[13];
            for (unsigned i=0;i<13;i++) packet[i] = mem[(b.kick+i)&1023];
            em_chain_page_test_kick(program,b.kick,packet,13);
        }
        return rc;
    }
    EmVu1Qword mem[1024]; EmVu1ClipResult b;
    memcpy(mem,data,sizeof mem);
    unsigned regs[32][4]; memcpy(regs,vf,sizeof regs);
    int rc = em_vu1_dynamic_clip_run(mem,top,&b,regs);
    memcpy(data,mem,sizeof mem); *fault=b.fault;
    if (!rc) for (unsigned i=0;i<b.kicks;i++)
        em_chain_page_test_kick(program,b.kick[i].addr,&b.qw[b.kick[i].first],b.kick[i].count);
    return rc;
}
'''


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    src, lib = OUT / 'shim.c', OUT / 'dynamic.dylib'
    src.write_text(P.SHIM + SHIM)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-DEM_CHAIN_PAGE_TEST_HOOK', '-shared', '-fPIC', '-Isrc', str(src),
                    'src/game/em_chain_page.c', 'src/game/em_gs_blocks_original.c',
                    'src/game/em_load_veil_particles.c', '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    n.shim_page.argtypes = [C.POINTER(P.Region), C.c_uint, C.c_uint32, C.POINTER(C.c_uint32), C.c_uint,
                           C.POINTER(P.GsPrim), C.POINTER(P.PrimQ), C.c_uint, C.POINTER(P.Counts), C.POINTER(C.c_uint32)]
    n.one_batch.argtypes = [C.c_uint, C.c_void_p, C.c_uint, C.c_void_p, C.c_void_p]
    n.kicks_meta.restype = n.kicks_words.restype = C.POINTER(C.c_uint32)
    return n


def native_kicks():
    meta, words = N.kicks_meta(), N.kicks_words()
    out = []
    for i in range(N.kicks_count()):
        program, at, first, count = meta[4*i:4*i+4]
        out.append((program, at, struct.pack('<%dI' % (4*count), *words[first:first+4*count])))
    return out


def check_programs():
    # The exact differences between existing translated programs and these
    # variants are the loop bound, output offsets and clipping work origin.
    specs = [(0x237180, NORMAL, 79, {5:(124,8),43:(131,15),44:(130,14),45:(129,13),46:(132,16),
                                    67:(131,15),68:(130,14),69:(129,13),70:(132,16),71:(132,16),73:(132,16)}),
             (0x239C90, CLIP, 1183, {1:(32,3),82:(30,1),114:(1185,1069)})]
    for old, new, count, changes in specs:
        a, b = S.VU1(ELF), S.VU1(ELF)
        S.load_program(a, ELF, old); S.load_program(b, ELF, new)
        diff = {}
        for i in range(count):
            lo, up = struct.unpack_from('<II', a.code, 8*i)
            ln, un = struct.unpack_from('<II', b.code, 8*i)
            if (lo, up) != (ln, un):
                assert up == un and lo & ~0x7FF == ln & ~0x7FF, (hex(new), i)
                diff[i] = (lo & 0x7FF, ln & 0x7FF)
        assert diff == changes, (hex(new), diff)
        assert W.vif_codes(ELF, old) == W.vif_codes(ELF, new)


def batch(vu, program, top, resume=False):
    mem = (C.c_uint8 * 16384).from_buffer_copy(vu.mem)
    vf = (C.c_uint32 * 128)(*(w for r in vu.v for w in r))
    fault = C.c_uint()
    rc = N.one_batch(program, mem, top, vf, C.byref(fault))
    assert rc == 0, ('native kernel fault', hex(program), top, fault.value)
    want_native = native_kicks()
    vu.top, vu.kicks, vu.events = top, [], []
    vu.run(vu.resume if resume else 0)
    expected = [(program, e[1], e[3]) for e in vu.events if e[0] == 'kick']
    assert bytes(mem) == bytes(vu.mem), ('dmem', hex(program), top,
                                       next(i for i in range(16384) if mem[i] != vu.mem[i]))
    assert want_native == expected, ('kernel packet', hex(program), top)
    return expected


def unit_roots(ram):
    p = M.Page(M.ram_reader(ram)); p.dma(M.latest_start(ram))
    stack, roots = [], []
    for a, tid, qwc, addr in p.transfers:
        if tid == 5:
            if addr in (NORMAL, CLIP):
                assert stack
                roots.append(stack[-1])
            stack.append(addr)
        elif tid == 6:
            stack.pop()
    return roots


def page_for(ram, roots):
    b = bytearray(ram)
    struct.pack_into('<4I', b, PAGE, 0x20000000, PAGE+0x40, 0, 0)
    for i, root in enumerate(roots):
        struct.pack_into('<4I', b, PAGE+0x40+16*i, 0x50000000, root, 0, 0)
    struct.pack_into('<4I', b, PAGE+0x40+16*len(roots), 0x20000000, PAGE+0x20, 0, 0)
    return b


def original_page(ram):
    p = M.Page(M.ram_reader(ram)); stream = p.dma(PAGE)
    words = [(a+i, struct.unpack_from('<I', b, i)[0]) for a,b in stream for i in range(0,len(b),4)]
    class Gs(M.Gs):
        def ad(self, reg, data, path):
            if reg == 0x4E:
                assert data >> 32 & 1, 'the page requires masked depth writes'
                self.zbuf = data
                return
            return super().ad(reg, data, path)
    vu = S.VU1(ELF); gs = Gs(); program = 0; base=offset=tops=dbf=0; cl=wl=4
    kicks, counts, i = [], Counter(), 0
    while i < len(words):
        at,v=words[i]; i+=1
        cmd,num,imm=v>>24&127,v>>16&255,v&65535
        if cmd >= 0x60:
            assert cmd == 0x6C and not imm&0x4000
            n=num or 256; dst=(imm&1023)+(tops if imm&0x8000 else 0)
            for k in range(n):
                d=(dst+(k//wl)*cl+k%wl)&1023
                struct.pack_into('<4I',vu.mem,16*d,*(x[1] for x in words[i+4*k:i+4*k+4]))
            i+=4*n
        elif cmd==0x4A:
            n=num or 256; first=words[i][0]
            for target,size in [(NORMAL,0x2D0),(CLIP,0x2570)]:
                if target <= first < target+size: program=target
            code=b''.join(struct.pack('<I',x[1]) for x in words[i:i+2*n])
            assert code == ELF[first-0x100000+0x300:first-0x100000+0x300+len(code)]
            vu.code[imm*8:imm*8+len(code)]=code; i+=2*n
        elif cmd==0x50:
            assert words[i][0]%16==0
            raw=b''.join(struct.pack('<I',x[1]) for x in words[i:i+4*imm]); i+=4*imm
            M.gs_feed(gs,raw,'direct')
        elif cmd in (0x14,0x17):
            top=tops; dbf^=1; tops=base+(offset if dbf else 0)
            SEEDS.setdefault(program, (bytes(vu.mem), top))
            k=batch(vu,program,top,cmd==0x17); kicks+=k; counts[program]+=1
            for _,a,raw in k: M.gs_feed(gs,raw,'dynamic')
        elif cmd==1: cl,wl=imm&255,imm>>8
        elif cmd==2: offset,dbf,tops=imm&1023,0,base
        elif cmd==3: base=imm&1023
        elif cmd==0x20: i+=1
        else: assert cmd in (0,5,0x10,0x11,0x13), (hex(at),hex(v))
    return kicks,gs.prims,counts


def compare_page(ram, roots, label):
    data=page_for(ram,roots)
    kicks,prims,counts=original_page(data)
    N.kicks_reset()
    rc,np,nq,nc,out=P.native_page(N,[(0,data)],PAGE,[],8192)
    assert rc==0,(label,'native page fault',out)
    got=native_kicks()
    assert len(got)==len(kicks),(label,'kick counts',len(got),len(kicks))
    for i,(a,b) in enumerate(zip(got,kicks)):
        assert a==b,(label,'page packet',i,hex(a[0]),a[:2],b[:2],
                     next((j for j in range(min(len(a[2]),len(b[2]))) if a[2][j]!=b[2][j]),None))
    P.compare_prims(prims,np,nq,out[2],label)
    assert nc.mscal_dynamic==counts[NORMAL] and nc.mscal_dynamic_clip==counts[CLIP]
    return len(kicks),len(prims),counts


def captured(path):
    ram=(path/'eeMemory.bin').read_bytes(); roots=unit_roots(ram)
    if not roots:return 0,0,Counter()
    result=compare_page(ram,roots,path.name)
    print(path.name, 'PASS', len(roots),'units',result[:2],flush=True)
    return result


def synthetic_input(index, program):
    rng = random.Random(0x237450 + index)
    mem, top = SEEDS[program]
    vu = S.VU1(ELF); vu.mem[:] = mem; S.load_program(vu, ELF, program)
    # An invertible projection: p=(x*w,y*w,w) projects to (x,y,100/w).
    # Rows and positions are synthetic; templates/materials are captured.
    matrix = ((1,0,0,0),(0,1,0,0),(0,0,0,1),(0,0,100,0))
    for r,row in enumerate(matrix): struct.pack_into('<4f',vu.mem,16*r,*row)
    for r in range(1,32):
        vu.v[r] = [W.fbits(rng.uniform(-10,10)) for _ in range(4)]
    for i in range(3):
        if index < 8:
            pts = [(-20,2000,1),(2080,1900,1),(2060,2100,1)]
            if index & 2: pts = [(y,x,w) for x,y,w in pts]
            if index & 4: pts = [(4090 if x<0 else x,y,w) for x,y,w in pts]
            x,y,w = pts[i]
        else:
            x,y = rng.uniform(-2000,6000),rng.uniform(-2000,6000)
            w = rng.choice([-0.2,0.05,0.1,0.2,1,20]) if index%3==0 else rng.uniform(.2,20)
        word = W.fbits(-1 if index&1 else 1)
        if i<2: word |= 0x8000
        base = (top+4*i)&1023
        struct.pack_into('<3fI',vu.mem,16*(base+3),x*w,y*w,w,word)
        struct.pack_into('<4f',vu.mem,16*(base+1),rng.uniform(-2,2),rng.uniform(-2,2),1,0)
        struct.pack_into('<4f',vu.mem,16*(base+2),*(rng.uniform(0,2) for _ in range(4)))
    return vu,top


def synthetic(item):
    index, program = item
    vu,top=synthetic_input(index,program)
    got=batch(vu,program,top)
    return Counter(batches=1,kicks=len(got),clip_packets=sum(len(b)>16 for _,_,b in got) if program==CLIP else 0)


def mixed_page(ram):
    data=bytearray(ram); roots=[]; cursor=PAGE+0x1000
    # Each synthetic unit uses the captured templates and class-1 GS REF.
    # It uploads explicit VU input then runs the actual CALLed program.
    for index,program in [(0,NORMAL),(0,CLIP),(1,NORMAL),(1,CLIP),(0,NORMAL)]:
        vu,top=synthetic_input(index,program)
        mem=bytearray(vu.mem)
        mem[16*0x190:16*0x190+192]=mem[16*top:16*top+192]
        roots.append(cursor)
        struct.pack_into('<4I',data,cursor,0x50000000,program,0,0);cursor+=16
        struct.pack_into('<4I',data,cursor,0x30000009,0x815990,0,0);cursor+=16
        payload=bytearray()
        for dst in range(0,1024,256):
            payload+=struct.pack('<I',0x6C000000|dst)+mem[16*dst:16*(dst+256)]
        payload+=struct.pack('<I',0x14000000)
        payload+=bytes(-len(payload)%16)
        struct.pack_into('<4I',data,cursor,0x10000000|len(payload)//16,0,0,0);cursor+=16
        data[cursor:cursor+len(payload)]=payload;cursor+=len(payload)
        struct.pack_into('<4I',data,cursor,0x60000000,0,0,0);cursor+=16
    result=compare_page(data,roots,'synthetic mixed page')
    assert result[1]>0
    return result


def failstops(ram):
    roots=unit_roots(ram); source=page_for(ram,roots)
    checks=[]
    # A different program is not silently treated as either known variant.
    p=M.Page(M.ram_reader(source));p.dma(PAGE)
    normal_call=next(a for a,t,n,d in p.transfers if t==5 and d==NORMAL)
    b=bytearray(source);struct.pack_into('<I',b,normal_call+4,0x237180)
    checks.append(('unknown program',[(0,b)],5))
    b=bytearray(source);word=struct.unpack_from('<Q',b,0x8159E0)[0]
    struct.pack_into('<Q',b,0x8159E0,word&~(1<<32))
    checks.append(('unmasked depth writes',[(0,b)],7))
    checks.append(('missing program window',[(0,source[:NORMAL]),(NORMAL+0x2D0,source[NORMAL+0x2D0:])],2))
    b=bytearray(source);last=0x237750+4*0x808-4
    word=struct.unpack_from('<I',b,last)[0]
    struct.pack_into('<I',b,last,(word&~0xFF0000)|(158<<16))
    checks.append(('incomplete clip upload',[(0,b)],5))
    for label,regions,want in checks:
        N.kicks_reset();rc,_,_,_,out=P.native_page(N,regions,PAGE,[],8192)
        assert rc<0 and out[0]==want,(label,rc,out,want)
    return len(checks)


def main():
    global ELF,N
    start=time.time(); ELF=(ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes()
    check_programs(); N=build()
    paths=[ARRIVAL]+sorted(p for p in ROUTE.glob('a01_*') if (p/'eeMemory.bin').exists())
    chosen=RM.select(paths,5,0x237450,keep=lambda i,p:p==ARRIVAL or p.name in ('a01_04_return_north','a01_05_npc_bridge_talk'))
    total=Counter()
    for kicks,prims,counts in RM.parallel_map(captured,chosen):
        total['kicks']+=kicks; total['primitives']+=prims; total.update(counts)
    # Seed both kernels in this process before forking the synthetic sweep.
    seed_ram=(ROUTE/'a01_04_return_north/eeMemory.bin').read_bytes()
    original_page(page_for(seed_ram,unit_roots(seed_ram)))
    mixed=mixed_page(seed_ram)
    fault_count=failstops(seed_ram)
    synthetic_counts=Counter()
    for result in RM.parallel_map(synthetic,[(i,p) for i in range(RM.pick(1200,40)) for p in (NORMAL,CLIP)]):
        synthetic_counts.update(result)
    assert synthetic_counts['clip_packets']>0, synthetic_counts
    RM.banner(RM.part(len(chosen),len(paths),'AREA01 captures'))
    print('PASS',dict(total),'synthetic',dict(synthetic_counts),'mixed page',mixed[:2],
          fault_count,'fail-stop contracts',f'({time.time()-start:.1f}s)')

if __name__=='__main__':main()
