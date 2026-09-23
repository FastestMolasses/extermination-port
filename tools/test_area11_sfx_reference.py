#!/usr/bin/env python3
"""Run original AREA11 panel sound dispatch and integer voice setup words.

Only 1157F0 (the hardware command sink) is replaced. Controlled free track
and voice tables exercise allocation without impersonating a live mix.
"""
import hashlib
import itertools
import json
from pathlib import Path
import struct
import subprocess
from export_area11_sfx import ROOT,DECOMP,ELF_SHA,Elf,source,pitch,stereo_gain,u32
from test_roger_reference import RogerOracle
from test_point_light_reference import signed
import ctypes as C
from reference_mode import FULL, MODE, banner, parallel_map, part, pick

class SfxOracle(RogerOracle):
 def plain(self,w):
  op,rs,rt,rd=w>>26,w>>21&31,w>>16&31,w>>11&31;fn=w&63
  if op==0 and fn in (24,25):
   a=signed(self.r[rs]) if fn==24 else self.r[rs]&0xffffffff
   b=signed(self.r[rt]) if fn==24 else self.r[rt]&0xffffffff
   value=a*b;self.lo=signed(value)&0xffffffffffffffff;self.hi=signed(value>>32)&0xffffffffffffffff
   if rd:self.r[rd]=self.lo
  elif op==0 and fn in (16,18):self.r[rd]=self.hi if fn==16 else self.lo
  elif op==0 and fn in (26,27):
   a=signed(self.r[rs]) if fn==26 else self.r[rs]&0xffffffff
   b=signed(self.r[rt]) if fn==26 else self.r[rt]&0xffffffff
   assert b
   q=abs(a)//abs(b)*(-1 if (a<0)!=(b<0) else 1)
   self.lo=signed(q)&0xffffffffffffffff;self.hi=signed(a-q*b)&0xffffffffffffffff
  elif op==0 and fn in (20,22,23):
   shift=self.r[rs]&63
   value=(self.r[rt]<<shift) if fn==20 else (self.r[rt]&0xffffffffffffffff)>>shift if fn==22 else signed(self.r[rt],64)>>shift
   self.r[rd]=value&0xffffffffffffffff
  elif op==55:self.r[rt]=self.load((self.r[rs]+signed(w&65535,16))&0xffffffff,8)
  elif op==63:self.save((self.r[rs]+signed(w&65535,16))&0xffffffff,self.r[rt],8)
  elif op==28 and fn in (24,25):
   a=signed(self.r[rs]) if fn==24 else self.r[rs]&0xffffffff
   b=signed(self.r[rt]) if fn==24 else self.r[rt]&0xffffffff
   value=a*b;self.lo1=signed(value)&0xffffffffffffffff;self.hi1=signed(value>>32)&0xffffffffffffffff
   if rd:self.r[rd]=self.lo1
  elif op==28 and fn in (16,18):self.r[rd]=self.hi1 if fn==16 else self.lo1
  elif op==28 and fn in (26,27):                       # DIV1 / DIVU1
   a=signed(self.r[rs]) if fn==26 else self.r[rs]&0xffffffff
   b=signed(self.r[rt]) if fn==26 else self.r[rt]&0xffffffff
   assert b
   q=abs(a)//abs(b)*(-1 if (a<0)!=(b<0) else 1)
   self.lo1=signed(q)&0xffffffffffffffff;self.hi1=signed(a-q*b)&0xffffffffffffffff
  elif op==0 and fn==39:self.r[rd]=~(self.r[rs]|self.r[rt])&0xffffffffffffffff
  elif op==28 and fn==41 and w>>6&31==0x1B:           # PCPYH (memset 00121A28)
   value=0
   for half in range(2):
    h=self.r[rt]>>(64*half)&0xffff
    value|=(h*0x0001000100010001)<<(64*half)
   self.r[rd]=value
  elif op==28 and fn==9 and w>>6&31==0x0E:            # PCPYLD
   self.r[rd]=(self.r[rs]&0xffffffffffffffff)<<64|(self.r[rt]&0xffffffffffffffff)
  else:super().plain(w)
  self.r[0]=0


def original(elf,ram,slot=0):
    o=SfxOracle(elf)
    for address,size in [(0x810700,4),(0x281D50,400),(0x27C6C0,128*12),
                         (0x13351F0,3376),(0x27F740,128)]:
        o.write(address,ram[address:address+size])
    o.save(0x27F770,slot);o.save(0x27F774,0)
    for i in range(slot):o.save(0x27E0C0+i*0x78+0x2E,1,2)
    return o


def iop_registers(elf):
    """Original shipped driver plus captured original libsd, no call hooks."""
    directory=DECOMP/'build/area11_sfx_reference'
    module=directory/'SNDN2DRV.IRX'
    memory=directory/'opening_iop.bin'
    if not module.exists() or not memory.exists():
        code="""from pathlib import Path
import io,pycdlib
from tools.parse_pcsx2_state import extract_zstd_entry
d=Path('build/area11_sfx_reference');d.mkdir(parents=True,exist_ok=True)
i=pycdlib.PyCdlib();i.open('Extermination-rebuilt.iso');b=io.BytesIO()
i.get_file_from_iso_fp(b,iso_path='/IRX/SNDN2DRV.IRX;1');i.close()
(d/'SNDN2DRV.IRX').write_bytes(b.getvalue())
p=Path('build/startup-reference/portable-data/sstates/SCUS-97112 (0AE679AF).02.p2s')
(d/'opening_iop.bin').write_bytes(extract_zstd_entry(p,'iopMemory.bin'))
"""
        subprocess.run([str(DECOMP/'.venv/bin/python'),'-c',code],cwd=DECOMP,check=True)
    raw=module.read_bytes();ram=memory.read_bytes()
    assert raw[:7]==b'\x7fELF\x01\x01\x01'
    table=u32(raw,32);stride,count,names=struct.unpack_from('<3H',raw,46)
    headers=[struct.unpack_from('<10I',raw,table+i*stride) for i in range(count)]
    strings=raw[headers[names][4]:headers[names][4]+headers[names][5]]
    sections={strings[h[0]:].split(b'\0')[0].decode():raw[h[4]:h[4]+h[5]] for h in headers}
    text=sections['.text'];relocations=sections['.rel.text']
    search=text[0x634:0x64C]
    match=ram.find(search);assert match>=0 and ram.find(search,match+1)<0
    base=match-0x634
    relocated={u32(relocations,i) for i in range(0,len(relocations),8)}
    imports={i for i in range(0x3130,len(text)-4,4)
             if u32(text,i)==0x03E00008 and u32(text,i+4)&0xFFFF0000==0x24000000}
    import_headers={i for i in range(0x3130,len(text)-8,4) if u32(text,i)==0x41E00000}
    # The IOP loader publishes next-table links and marks imports linked.
    # Those metadata words and the patched JR->J import stubs are not code
    # equality candidates; every actual non-relocated body word is checked.
    loader_metadata={i+j for i in import_headers for j in (4,8)}
    for i in import_headers:assert u32(ram,base+i+8)==u32(text,i+8)|0x20000
    checked=0
    for i in range(0,len(text),4):
        if i in relocated or i in imports or i in loader_metadata:continue
        assert text[i:i+4]==ram[base+i:base+i+4],hex(i)
        checked+=1
    # Import table proves libsd ordinal 5 is the parameter setter. Its
    # captured jump now executes the original resident implementation.
    assert text[0x313C:0x3144]==b'libsd\0\0\0'
    assert u32(text,0x3150)==0x24000005
    class Iop(SfxOracle):
        def save(self,address,value,size=4):
            if address&0x1FFFF000==0x1F900000:
                self.hardware.append((address&0x1FFFFFFF,value&((1<<(8*size))-1),size))
            super().save(address,value,size)
    o=Iop(elf);o.hardware=[];o.write(0,ram)
    # Quick: both ends of each SPU2 core (0/1, 23 | 24/25, 47) and one
    # mid voice per core; the full run sets up all 48.
    voices=range(48) if FULL else (0,1,11,23,24,25,36,47)
    for voice in voices:
        o.hardware=[]
        for command in ((6,voice,862,0),(1,voice,2217,2217),
                        (5,voice,0x1E2010,0),(3,voice,0x80FF,0x5FD0)):
            o.write(0x990000,struct.pack('<4I',*command))
            o.run(base+0x634,(0x990000,))
        core=0x1F900000+0x400*(voice//24)
        row=core+16*(voice%24)
        address=core+0x1C0+12*(voice%24)
        assert o.hardware==[(row+4,862,2),(row,2217,2),(row+2,2217,2),
            (address,15,2),(address+2,0x1008,2),(row+6,0x80FF,2),(row+8,0x5FD0,2)],o.hardware
    # Switch commands the sequencer flush queues (0xC effect send, 0xA KON,
    # 0xB KOFF): word a = voices 0..23 (core 0), word b = voices 24..47
    # (core 1), each split into its low/high register halves. A KON and a
    # KOFF of one flush reach the SPU2 in that order, back to back.
    switches=0
    for low,high in ((1,0),(0x800001,0x800001),(0xFFFFFF,0x5A5A5A),(0,0x10)):
        o.hardware=[]
        for command,registers in ((0xC,(0x18C,0x194)),(0xA,(0x1A0,)),(0xB,(0x1A4,))):
            o.write(0x990000,struct.pack('<4I',command,0,low,high))
            o.run(base+0x634,(0x990000,))
            expected=[]
            for core,word in ((0x1F900000,low),(0x1F900400,high)):
                for register in registers:
                    expected+=[(core+register,word&0xFFFF,2),(core+register+2,word>>16,2)]
            order=[(core+register+half) for core in (0x1F900000,0x1F900400)
                   for register in registers for half in (0,2)]
            assert sorted(o.hardware[-len(expected):])==sorted(expected),(hex(command),o.hardware)
            assert [h[0] for h in o.hardware[-len(expected):]]==order,(hex(command),o.hardware)
            switches+=len(expected)
        kon=[i for i,h in enumerate(o.hardware) if h[0] in (0x1F9001A0,0x1F9005A0)]
        koff=[i for i,h in enumerate(o.hardware) if h[0] in (0x1F9001A4,0x1F9005A4)]
        assert max(kon)<min(koff)
    return dict(voices=len(voices),register_writes=len(voices)*7,switch_register_writes=switches,
        unrelocated_module_words=checked,
        module_sha256=hashlib.sha256(raw).hexdigest(),captured_base=base,
        limit='Original driver and libsd execute; hardware register stores are observed, not synthesized sound')


# ---- EMSR registry: every exported id AREA11 can play (WP-14) -------------

REQUESTS=((0x1000,0x1000),(0x800,-0x400),(0,0x1000),(-0x1000,0x7FF),(0x1001,0x200))
TRACK_TABLE,VOICE_TABLE=0x27E0C0,0x27CCC0
STREAM_VOICES=0xF          # voices 0..3: kind 3 (stream) in every AREA11 capture
OP_FIELDS=('tick','kind','note','prog','flags','sample','pitch','pan','scalar','adsr1','adsr2',
           'center','fine','range','alloc','priority','length','depth')
# D_0027CCC0 fields the native driver keeps, in shim_voice order.
VOICE_FIELDS=(0,2,6,8,0xA,0xC,0x1A,0x1C,0x1E,0x20,0x44,0x4E,0x60,0x62,0x64,0x3E)
COMMANDS={1,3,5,6,0xA,0xB,0xC,0xD}


def parse_emsr(blob):
    """Independent reader of the EMSR v2 registry the native loader consumes."""
    magic,version,sample_count,entry_count,ladder_count,reserved=struct.unpack_from('<4sIIIII',blob)
    assert (magic,version,reserved)==(b'EMSR',2,0)
    at=24
    ladder=struct.unpack_from(f'<{ladder_count}H',blob,at);at+=2*ladder_count
    entries=[]
    for _ in range(entry_count):
        sid,area,sub,state,count,reason,bank,zero=struct.unpack_from('<IhhBBHHH',blob,at);at+=16
        assert zero==0
        ops=[]
        for _ in range(count):
            values=struct.unpack_from('<HBBBBHHHIHHBbBBBBBBI',blob,at);at+=32
            assert values[18]==0 and values[19]==0
            ops.append(dict(zip(OP_FIELDS,values[:18])))
        entries.append(dict(id=sid,scope=[area,sub],state=state,reason=reason,bank=bank,ops=ops))
    samples=[]
    for _ in range(sample_count):
        frames,loop=struct.unpack_from('<II',blob,at);at+=8
        total=frames+(0 if loop==0xFFFFFFFF else frames-loop)
        samples.append(dict(frames=frames,loop_start=None if loop==0xFFFFFFFF else loop,
                            pcm=blob[at:at+2*total]));at+=2*total
    assert at==len(blob)
    return entries,samples,ladder


SHIM=r"""
#include "game/em_sfx_bank.h"
#include <stdlib.h>
#include <string.h>
typedef struct {
    EmSfxRegistry reg; EmSfxDriver drv; unsigned n; uint32_t cmds[8192][4];
    int32_t zero[EM_SFX_VOICES];
    int status, in_range, start; int32_t left, right; unsigned logged; int32_t log[16][5];
} Shim;
static void sink(void *c, int cmd, int v, uint32_t a, uint32_t b)
{
    Shim *s = c;
    if (s->n < 8192) {
        s->cmds[s->n][0] = (uint32_t)cmd; s->cmds[s->n][1] = (uint32_t)v;
        s->cmds[s->n][2] = a; s->cmds[s->n][3] = b;
    }
    s->n++;
}
Shim *shim_new(const char *path, uint64_t streams, uint32_t cursor, uint32_t serial,
               uint64_t effect, int zero_feedback)
{
    Shim *s = calloc(1, sizeof *s);
    if (!s || !em_sfx_registry_load(&s->reg, path)) { free(s); return NULL; }
    em_sfx_driver_init(&s->drv, &s->reg, streams);
    s->drv.cursor = cursor; s->drv.serial = serial;
    s->drv.effect = s->drv.effect_sent = effect;
    if (zero_feedback) s->drv.feedback = s->zero;
    return s;
}
void shim_free(Shim *s) { em_sfx_registry_free(&s->reg); free(s); }
int shim_start(Shim *s, unsigned id, int area, int sub, int l, int r)
{
    const EmSfxEntry *e = em_sfx_registry_find(&s->reg, id, area, sub);
    if (!e) e = em_sfx_registry_find(&s->reg, id, -1, -1);
    return e ? em_sfx_driver_start(&s->drv, e, l, r) : -2;
}
void shim_request(Shim *s, int t, int l, int r) { em_sfx_driver_request(&s->drv, t, l, r); }
void shim_stop(Shim *s, int t, int hard) { em_sfx_driver_stop(&s->drv, t, hard, sink, s); }
void shim_tick(Shim *s) { em_sfx_driver_tick(&s->drv, sink, s); }
unsigned shim_count(Shim *s) { return s->n; }
uint32_t *shim_commands(Shim *s) { return &s->cmds[0][0]; }
void shim_clear(Shim *s) { s->n = 0; }
int shim_envx(Shim *s, int v) { return em_sfx_driver_envx(&s->drv, v); }
long shim_render(Shim *s, float *out, unsigned frames, unsigned rate)
{ return em_sfx_driver_render(&s->drv, out, frames, rate); }
int shim_busy(Shim *s) { return em_sfx_driver_busy(&s->drv); }
int shim_allocated(Shim *s)
{ int n = 0; for (int i = 0; i < EM_SFX_TRACKS; ++i) n += s->drv.tracks[i].allocated; return n; }
unsigned shim_no_voice(Shim *s) { return s->drv.no_voice; }
void shim_track(Shim *s, int t, int *o)
{
    const EmSfxTrack *k = &s->drv.tracks[t];
    o[0] = k->allocated; o[1] = k->running; o[2] = k->ended; o[3] = k->porta_note;
}
void shim_voice(Shim *s, int i, int *o)
{
    const EmSfxVoice *v = &s->drv.voices[i];
    const int f[] = {v->state, v->note, v->owner, v->release, v->serial, v->sustain, v->kind,
                     v->age, v->priority, v->alloc, v->porta, v->base, v->depth, v->remaining,
                     v->length, v->prog};
    memcpy(o, f, sizeof f);
}
uint32_t shim_cursor(Shim *s) { return s->drv.cursor; }
uint32_t shim_serial(Shim *s) { return s->drv.serial; }
/* 00117428 on a caller-supplied voice table. */
int shim_allocate(Shim *s, const int *fields, uint32_t *cursor, int alloc, int priority, int bank)
{
    for (int i = 0; i < EM_SFX_VOICES; ++i) {
        EmSfxVoice *v = &s->drv.voices[i]; const int *f = fields + 8 * i;
        v->state = (uint16_t)f[0]; v->release = (uint16_t)f[1]; v->serial = (uint16_t)f[2];
        v->kind = (uint16_t)f[3]; v->priority = (uint16_t)f[4]; v->alloc = (uint16_t)f[5];
        v->bank = (uint16_t)f[6];
    }
    s->drv.cursor = *cursor;
    const int key = em_sfx_driver_allocate(&s->drv, (unsigned)alloc, (unsigned)priority, (unsigned)bank);
    *cursor = s->drv.cursor;
    return key;
}
static void note(Shim *s, int a, int b, int c, int d, int e)
{ if (s->logged < 16) { int32_t *l = s->log[s->logged++]; l[0]=a; l[1]=b; l[2]=c; l[3]=d; l[4]=e; } }
static int op_status(void *c, int t) { Shim *s = c; note(s, 1, t, 0, 0, 0); return s->status; }
static int op_gains(void *c, int32_t *l, int32_t *r)
{ Shim *s = c; note(s, 2, 0, 0, 0, 0); *l = s->left; *r = s->right; return s->in_range; }
static void op_request(void *c, int t, int32_t l, int32_t r) { note(c, 3, t, l, r, 0); }
static void op_stop(void *c, int t) { note(c, 4, t, 0, 0, 0); }
static int op_start(void *c, unsigned id, int32_t l, int32_t r)
{ Shim *s = c; note(s, 5, (int)id, l, r, 0); return s->start; }
int shim_loop(Shim *s, int32_t *requested, const int32_t *snapshot, int32_t *handle, unsigned id,
              int32_t frame, int ordinal, int status, int in_range, int32_t left, int32_t right,
              int start, int release, int32_t *log)
{
    const EmSfxLoopOps ops = {s, op_status, op_gains, op_request, op_stop, op_start};
    s->status = status; s->in_range = in_range; s->left = left; s->right = right; s->start = start;
    s->logged = 0;
    if (release) em_sfx_service_release(&ops, requested, handle);
    else em_sfx_service_step(&ops, requested, snapshot, handle, id, frame, (int16_t)ordinal);
    memcpy(log, s->log, sizeof s->log);
    return (int)s->logged;
}
/* ENVX after each of `count` SPU samples from key-on. */
void shim_envelope(unsigned adsr1, unsigned adsr2, unsigned count, int32_t *out)
{
    EmSfxEnvelope e;
    em_sfx_envelope_key_on(&e, (uint16_t)adsr1, (uint16_t)adsr2);
    for (unsigned i = 0; i < count; ++i) { out[i] = e.level; em_sfx_envelope_step(&e); }
}
"""


def native_driver():
    out=ROOT/'build/area11_sfx_reference';out.mkdir(parents=True,exist_ok=True)
    (out/'driver_shim.c').write_text(SHIM)
    library=out/'libem_sfx_driver.dylib'
    subprocess.run(['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-shared','-fPIC','-Isrc',
        str(out/'driver_shim.c'),'src/game/em_sfx_bank.c','-o',str(library)],cwd=ROOT,check=True)
    lib=C.CDLL(str(library))
    P=C.c_void_p
    lib.shim_new.restype=P
    lib.shim_new.argtypes=[C.c_char_p,C.c_uint64,C.c_uint32,C.c_uint32,C.c_uint64,C.c_int]
    for name in ('shim_free','shim_tick','shim_clear'):getattr(lib,name).argtypes=[P]
    lib.shim_start.argtypes=[P,C.c_uint,C.c_int,C.c_int,C.c_int,C.c_int]
    lib.shim_request.argtypes=[P,C.c_int,C.c_int,C.c_int]
    lib.shim_stop.argtypes=[P,C.c_int,C.c_int]
    lib.shim_count.argtypes=[P];lib.shim_count.restype=C.c_uint
    lib.shim_commands.argtypes=[P];lib.shim_commands.restype=C.POINTER(C.c_uint32)
    lib.shim_envx.argtypes=[P,C.c_int]
    lib.shim_render.argtypes=[P,P,C.c_uint,C.c_uint];lib.shim_render.restype=C.c_long
    for name in ('shim_busy','shim_allocated'):getattr(lib,name).argtypes=[P]
    lib.shim_no_voice.argtypes=[P];lib.shim_no_voice.restype=C.c_uint
    lib.shim_track.argtypes=[P,C.c_int,C.POINTER(C.c_int)]
    lib.shim_voice.argtypes=[P,C.c_int,C.POINTER(C.c_int)]
    lib.shim_cursor.argtypes=[P];lib.shim_cursor.restype=C.c_uint32
    lib.shim_serial.argtypes=[P];lib.shim_serial.restype=C.c_uint32
    lib.shim_allocate.argtypes=[P,C.POINTER(C.c_int),C.POINTER(C.c_uint32),C.c_int,C.c_int,C.c_int]
    lib.shim_loop.argtypes=[P,C.POINTER(C.c_int32),C.POINTER(C.c_int32),C.POINTER(C.c_int32),C.c_uint,
        C.c_int32,C.c_int,C.c_int,C.c_int,C.c_int32,C.c_int32,C.c_int,C.c_int,C.POINTER(C.c_int32)]
    lib.shim_envelope.argtypes=[C.c_uint,C.c_uint,C.c_uint,C.POINTER(C.c_int32)]
    lib.em_sfx_volume_words.argtypes=[C.c_uint32,C.c_uint16,C.c_int32,C.c_int32,C.POINTER(C.c_uint16)]
    lib.em_sfx_request_word.argtypes=[C.c_float];lib.em_sfx_request_word.restype=C.c_int32
    def words(scalar,pan,left,right):
        pair=(C.c_uint16*2)()
        lib.em_sfx_volume_words(scalar,pan,left,right,pair)
        return list(pair)
    return lib,words


def tick_frames(tick,rate=48000):
    return (tick+1)*rate*1001//60000-tick*rate*1001//60000


class Registry:
    """The bank binding, sample addresses and a controlled driver start state."""
    def __init__(self,elf,ram,report,path):
        import export_sfx_registry as X
        self.elf,self.ram,self.report,self.path=elf,ram,report,path
        bindings=X.area_bindings(X.Elf())[(11,0)]['groups']
        self.banks=[]
        for group,banks in bindings.items():
            for index,bank in enumerate(banks):
                handle=u32(ram,0x281D50+4*(group*0x14+index))
                use,header,spu=struct.unpack_from('<3I',ram,0x27C6C0+12*handle)
                self.banks.append((handle,header,spu<<3,bank))
        self.address={}
        for sample in report['samples']:
            for handle,header,base,bank in self.banks:
                if bank.name==sample['container'] and bank.body<=sample['offset']<bank.body+bank.body_size:
                    self.address[sample['index']]=base+sample['offset']-bank.body

    def oracle(self):
        """Original state: captured banks and D_0027F740, empty tracks, the
        voice table as the driver initialises it with voices 0..3 streaming."""
        o=original(self.elf.data,self.ram,0)
        for handle,header,base,bank in self.banks:
            o.write(header,self.ram[header:header+bank.header_end-bank.hd])
        o.write(0x27F778,self.ram[0x27F778:0x27F77A])
        for v in range(48):
            record=bytearray(0x6A)
            for offset in (6,0x22,0x24,0x26):struct.pack_into('<H',record,offset,0xFFFF)
            struct.pack_into('<H',record,0x4E,0x78)
            if STREAM_VOICES>>v&1:
                struct.pack_into('<H',record,0,1);struct.pack_into('<H',record,0x1A,3)
            o.write(VOICE_TABLE+v*0x6A,bytes(record))
        o.write(TRACK_TABLE,bytes(0x78*48))
        return o


def lockstep(lib,registry,scenario,feedback='model',observe=None,settle=None):
    """Run original 001FB9F0/0011A218/0011A070 + 001152D8 and the native
    driver side by side, tick by tick. scenario = {tick: [('start', id, l, r)
    | ('request', track, l, r) | ('stop', track, hard)]}. The D_002817C0
    feedback both sides see is the native SPU2 model's ENVX ('model') or 0
    ('zero'). Every 001157F0 command, every track (+0x32/+0x34/+0x3E/+0x58),
    every voice field the driver keeps and the allocation cursor/serial
    must agree after every tick."""
    o=registry.oracle()
    cursor=o.load(0x27F740+0x30);serial=o.load(0x27F740+0x34);effect=o.load(0x27F740,8)
    assert o.load(0x27F740+8,8)==effect and o.load(0x27F740+0x10,8)==o.load(0x27F740+0x18,8)==0
    assert o.load(0x27F740+0x20,8)==0 and o.load(0x27F740+0x28,8)==0
    s=lib.shim_new(str(registry.path).encode(),STREAM_VOICES,cursor,serial,effect,feedback=='zero')
    assert s
    got=[]
    o.calls[0x1157F0]=lambda r:got.append(tuple(r.r[i]&0xFFFFFFFF for i in range(4,8)))
    o.calls[0x1191F0]=lambda r:None
    stats=dict(ticks=0,commands=0,key_on=0,key_off=0,pitch=0,volume=0,starts=0,refused=0)
    last=max(scenario) if scenario else 0
    buffer=(C.c_int*16)()
    for tick in range(20000):
        got.clear();lib.shim_clear(s)
        for action in scenario.get(tick,()):
            if action[0]=='start':
                _,sid,left,right=action
                o.run(0x1FB9F0,(sid,0x1000,left&0xFFFFFFFFFFFFFFFF,right&0xFFFFFFFFFFFFFFFF))
                track=signed(o.r[2]);native=lib.shim_start(s,sid,11,0,left,right)
                assert track==native,('start',hex(sid),tick,track,native)
                stats['starts']+=track>=0;stats['refused']+=track<0
            elif action[0]=='request':
                _,track,left,right=action
                o.run(0x11A218,(track,left&0xFFFFFFFFFFFFFFFF,right&0xFFFFFFFFFFFFFFFF))
                lib.shim_request(s,track,left,right)
            else:
                _,track,hard=action
                o.run(0x11A070,(track|hard<<15,));lib.shim_stop(s,track,hard)
        for v in range(48):
            o.save(0x2817C0+4*v,0 if feedback=='zero' else lib.shim_envx(s,v)&0xFFFFFFFF)
        o.run(0x1152D8);lib.shim_tick(s)
        count=lib.shim_count(s);raw=lib.shim_commands(s)
        native=[tuple(raw[4*i+j] for j in range(4)) for i in range(count)]
        native=[(c,v,registry.address[a],b) if c==5 else (c,v,a,b) for c,v,a,b in native]
        assert native==got,(tick,got,native)
        assert {c[0] for c in got}<=COMMANDS,got
        for c in got:
            stats['commands']+=1
            stats['key_on']+=bin(c[2]|c[3]<<24).count('1') if c[0]==0xA else 0
            stats['key_off']+=bin(c[2]|c[3]<<24).count('1') if c[0]==0xB else 0
            stats['pitch']+=c[0]==6;stats['volume']+=c[0]==1
        if observe:observe(tick,got)
        for t in range(48):
            lib.shim_track(s,t,buffer)
            base=TRACK_TABLE+t*0x78
            expected=tuple(o.load(base+off,2) for off in (0x32,0x34,0x3E,0x58))
            assert tuple(buffer[:4])==expected,(tick,'track',t,tuple(buffer[:4]),expected)
        for v in range(48):
            lib.shim_voice(s,v,buffer)
            expected=tuple(o.load(VOICE_TABLE+v*0x6A+off,2) for off in VOICE_FIELDS)
            assert tuple(x&0xFFFF for x in buffer[:16])==expected,(tick,'voice',v,tuple(buffer[:16]),expected)
        assert lib.shim_cursor(s)==o.load(0x27F740+0x30) and lib.shim_serial(s)==o.load(0x27F740+0x34)
        lib.shim_render(s,None,tick_frames(tick),48000)
        if tick>last and (settle(tick,lib,s) if settle else not lib.shim_busy(s)):break
    else:raise AssertionError('scenario did not settle')
    stats['ticks']=tick+1;stats['no_voice']=lib.shim_no_voice(s)
    lib.shim_free(s)
    return stats


# Pass A/B/C lockstep runs are module-level functions so a quick run can
# spread them over forked workers (each run is independent: its own oracle
# and native driver). A full run calls them serially, in the original order.
_SFX={}


def _registry_case(job):
    """Pass A: one entry and request pair, zero feedback. Returns A0 voices."""
    entry,left,right=job
    X,lib,registry=_SFX['X'],_SFX['lib'],_SFX['registry']
    sample_address,native_words=_SFX['sample_address'],_SFX['native_words']
    if entry['state']==X.STATE_ABSENT:
        o=registry.oracle()
        o.run(0x1FB9F0,(entry['id'],0x1000,left&0xFFFFFFFFFFFFFFFF,right&0xFFFFFFFFFFFFFFFF))
        assert signed(o.r[2])==-1,hex(entry['id'])
        shim=lib.shim_new(str(registry.path).encode(),STREAM_VOICES,0,0,0,1)
        assert lib.shim_start(shim,entry['id'],11,0,left,right)==-1
        lib.shim_free(shim)
        return 0
    assert entry['state']==X.STATE_AUDIBLE,hex(entry['id'])
    request=(left,right) if all(-0x1000<=x<=0x1000 for x in (left,right)) else (0x1000,0x1000)
    ops=[e for e in entry['events'] if 'op' in e]
    key_ons=[e for e in ops if e['op']==X.OP_KEY_ON]
    portamento=any(e['op']==X.OP_PORTAMENTO for e in ops)
    live={};started_total=[0];offs=[0]
    def observe(tick,commands,entry=entry,request=request,key_ons=key_ons,
                portamento=portamento,live=live,started_total=started_total,offs=offs):
        on=0
        for c in commands:
            if c[0]==0xA:on|=c[2]|c[3]<<24
            if c[0]==0xB:offs[0]|=c[2]|c[3]<<24
        started=[]
        for v in range(48):
            if not on>>v&1:continue
            first={cmd:next(c for c in commands if c[0]==cmd and c[1]==v) for cmd in (6,1,5,3)}
            started.append((first[6][2],first[1][2],first[1][3],first[5][2],first[3][2],first[3][3]))
            live[v]=started[-1]
        for c in commands:
            if c[0]==6 and not portamento:assert c[2]==live[c[1]][0]
            if c[0]==1:assert (c[2],c[3])==live[c[1]][1:3]
        expected=[]
        for e in key_ons:
            if e['tick']!=tick:continue
            words=X.volume_words(e['scalar'],e['pan'],*request)
            assert native_words(e['scalar'],e['pan'],left,right)==list(words)
            expected.append((e['pitch'],*words,sample_address[e['sample']],e['adsr1'],e['adsr2']))
        assert sorted(started)==sorted(expected),(hex(entry['id']),tick,started,expected)
        started_total[0]+=len(started)
    last=max(e['tick'] for e in ops)
    lockstep(lib,registry,{0:[('start',entry['id'],left,right)]},'zero',observe,
             settle=lambda tick,lib,s,last=last:tick>last+3 and not lib.shim_allocated(s))
    assert started_total[0]==len(key_ons),(hex(entry['id']),started_total[0],len(key_ons))
    # Key-offs only where the script keys off a sustained tone.
    sustained_offs=any(e['op']==X.OP_KEY_OFF and any(k['note']==e['note'] and k['sustained']
                       for k in key_ons) for e in ops)
    assert bool(offs[0])==sustained_offs,hex(entry['id'])
    return started_total[0]


def _model_case(entry):
    """Pass B: one audible entry with the SPU2 model's ENVX feedback."""
    return lockstep(_SFX['lib'],_SFX['registry'],{0:[('start',entry['id'],0x800,-0x400)]})


def _scenario_case(scenario):
    """Pass C: one concurrent scenario with model feedback."""
    return lockstep(_SFX['lib'],_SFX['registry'],scenario)


def _sfx_job(tagged):
    kind,job=tagged
    return {'A':_registry_case,'B':_model_case,'C':_scenario_case}[kind](job)


def _sfx_cost(tagged):
    """Rough tick count, so the longest lockstep runs start first."""
    kind,job=tagged
    if kind=='C':return 1000+max(job)
    entry=job[0] if kind=='A' else job
    return max([e['tick'] for e in entry.get('events',()) if 'op' in e] or [0])


def registry_oracle(elf,ram):
    import export_sfx_registry as X
    import random
    out=ROOT/'build/area11_sfx_reference/registry'
    report=X.export(out)
    blob=(out/'sfx_registry.emsr').read_bytes()
    installed=ROOT/'assets/sfx/sfx_registry.emsr'
    if installed.exists():
        assert installed.read_bytes()==blob,'assets/sfx/sfx_registry.emsr is stale: re-run the exporter'
    entries,samples,ladder=parse_emsr(blob)
    assert len(entries)==len(report['entries'])
    assert list(ladder)==[struct.unpack_from('<H',elf.data,0x241D70-0x100000+0x300+2*i)[0]
                          for i in range(len(ladder))]
    for mine,theirs in zip(entries,report['entries']):
        ops=[e for e in theirs.get('events',[]) if 'op' in e]
        assert (mine['id'],mine['scope'],mine['state'])==(theirs['id'],theirs['scope'],theirs['state'])
        assert [tuple(op[f] for f in OP_FIELDS) for op in mine['ops']]==[
            (e['tick'],e['op'],e.get('note',0),e.get('prog',0),e.get('flags',0),e.get('sample',0),
             e.get('pitch',0),e.get('pan',0),e.get('scalar',0),e.get('adsr1',0),e.get('adsr2',0),
             e.get('center',0),e.get('fine',0),e.get('range',0),e.get('alloc',0),e.get('priority',0),
             e.get('length',0),e.get('depth',0)) for e in ops]
    # Bank binding: both captures register the same handles; each RAM header
    # equals the bound container bank except the per-track bend bytes.
    xelf=X.Elf();bindings=X.area_bindings(xelf)[(11,0)]['groups']
    handles={}
    for capture in ('opening_ee.bin','playable_ee.bin'):
        data=(DECOMP/'build/startup-reference'/capture).read_bytes()
        assert data[0x810700:0x810702]==bytes([11,0])
        assert struct.unpack_from('<H',data,0x27F740+0x3A)[0]==60      # tick divisor
        assert struct.unpack_from('<H',data,0x27F778)[0]==0             # stereo
        for v in range(4):                                              # stream voices
            record=data[VOICE_TABLE+v*0x6A:VOICE_TABLE+(v+1)*0x6A]
            assert struct.unpack_from('<3H',record,0)[0]==1 and struct.unpack_from('<H',record,0x1A)[0]==3
            assert struct.unpack_from('<H',record,6)[0]==0xFFFF
        for group,banks in bindings.items():
            for index,bank in enumerate(banks):
                handle=u32(data,0x281D50+4*(group*0x14+index))
                use,header,spu=struct.unpack_from('<3I',data,0x27C6C0+12*handle)
                assert use==1 and handle<0x80
                expected=bank.data[bank.hd:bank.header_end]
                actual=data[header:header+len(expected)]
                state=u32(expected,0x20)
                mutable={state+0x10+16*t+0xA for t in range(48)}
                assert all(a==b or i in mutable for i,(a,b) in enumerate(zip(actual,expected))),(capture,group,index)
                handles[(bank.name,bank.row)]=(handle,spu<<3,bank.body)
        for group in range(6):
            for index in range(0x14):
                if group in bindings and index<len(bindings[group]):continue
                handle=u32(data,0x281D50+4*(group*0x14+index))
                assert handle in (0,3),(group,index,handle)   # 0 = unregistered, 3 = group-3 music
    spu=(DECOMP/'build/area11_sfx_reference/original_spu2.bin').read_bytes()
    spu_checked=loops_checked=0
    sample_address={}
    for sample in report['samples']:
        for (name,row),(handle,base,body) in handles.items():
            bank=[b for g in bindings.values() for b in g if (b.name,b.row)==(name,row)][0]
            if name==sample['container'] and body<=sample['offset']<body+bank.body_size:
                data=(DECOMP/name).read_bytes()
                raw=data[sample['offset']:sample['offset']+sample['adpcm_bytes']]
                address=base+sample['offset']-body
                live=spu[0x10004+address:0x10004+address+len(raw)]
                assert live==raw
                sample_address[sample['index']]=address
                spu_checked+=1
                # Loop points come from the loaded blocks' flags: the SPU2
                # repeats from the last 0x04 block when the end block also
                # has 0x02 (the driver writes no loop address; see COMMANDS).
                flags=[live[i+1] for i in range(0,len(live),16)]
                assert flags[-1]&1 and not any(f&1 for f in flags[:-1])
                starts=[i for i,f in enumerate(flags) if f&4]
                expected=starts[-1]*28 if flags[-1]&2 else None
                assert sample['loop_start']==expected,(sample['index'],sample['loop_start'],expected)
                assert samples[sample['index']]['loop_start']==expected
                loops_checked+=expected is not None
                break
    lib,native_words=native_driver()
    registry=Registry(elf,ram,report,out/'sfx_registry.emsr')
    assert registry.address==sample_address
    area11=[e for e in report['entries'] if e['scope'] in ([-1,-1],[11,0])]
    # Pass A: the old register-word check, now in lockstep. Feedback 0 (the
    # driver reaps a voice two ticks after it may be reaped) and five
    # request pairs: absent ids return -1; every exported key-on starts
    # exactly one voice whose 6/1/5/3 words equal the exported ones;
    # re-sent words repeat unless a portamento moves the pitch.
    _SFX.update(X=X,lib=lib,registry=registry,sample_address=sample_address,native_words=native_words)
    # Quick: every entry (absent ones with all five requests); each audible
    # entry with one request pair, rotating so every pair, including the two
    # out-of-range pairs, is exercised.
    jobs=[(entry,left,right) for number,entry in enumerate(area11)
          for left,right in (REQUESTS if FULL or entry['state']==X.STATE_ABSENT else (REQUESTS[number%len(REQUESTS)],))]
    # Pass B: every audible AREA11 entry alone with the SPU2 model's ENVX as
    # the reaper feedback, until every voice has ended and every track is free.
    audible_entries=[e for e in area11 if e['state']==X.STATE_AUDIBLE]
    # Pass C: concurrent scenarios.
    scenarios={}
    # 001FC3C0-style flame re-trigger, requests on a live loop, soft and hard stops.
    scenarios['services']={0:[('start',0x413,0x1000,0x1000),('start',0x14D,0x900,0x300),
                              ('start',0x452,0x1000,0x1000)],
                           10:[('request',1,-0x200,0x700)],20:[('start',0x413,0x400,0x800)],
                           24:[('stop',1,0)],40:[('start',0x413,0x1000,0x1000),('start',0x453,0x800,0x800)],
                           60:[('stop',2,1),('start',0x14D,0x1000,0x1000)],70:[('stop',3,0)]}
    # Voice and track exhaustion: 00117428 refuses once 44 voices are busy
    # (it never steals kind-2 voices) and the 49th track is refused; the
    # starved instances' key-offs fall back to other tracks' voices.
    # (Seven starts per tick keep one original tick inside the oracle's
    # instruction budget; every track is still allocated at the 49th.)
    scenarios['exhaustion']={tick:[('start',0x14D,0x1000,0x1000)]*7 for tick in range(7)}
    scenarios['exhaustion'][8]=[('start',0x413,0x1000,0x1000)]
    rng=random.Random(0x5FD0)
    audible=[e['id'] for e in area11 if e['state']==X.STATE_AUDIBLE]
    plan={}
    for tick in range(400):
        actions=[]
        if rng.random()<0.2:
            left,right=rng.choice(REQUESTS)
            actions.append(('start',rng.choice(audible+[0x413,0x14D,0x452]),left,right))
        if rng.random()<0.05:actions.append(('request',rng.randrange(8),rng.randrange(-0x1000,0x1001),
                                             rng.randrange(-0x1000,0x1001)))
        if rng.random()<0.03:actions.append(('stop',rng.randrange(8),int(rng.random()<0.3)))
        if actions:plan[tick]=actions
    # Quick: the same generated plan cut to its first 80 ticks.
    scenarios['random']=plan if FULL else {tick:actions for tick,actions in plan.items() if tick<80}
    # Passes A, B and C in that order. A full run executes them serially; a
    # quick run spreads the independent lockstep runs over forked workers,
    # longest first, and receives the results in the same order.
    tagged=[('A',job) for job in jobs]+[('B',e) for e in audible_entries]+[('C',x) for x in scenarios.values()]
    if FULL:
        results=[_sfx_job(job) for job in tagged]
    else:
        results=parallel_map(_sfx_job,tagged,cost=_sfx_cost)
    voices=results[:len(jobs)]
    cases,voices_checked=len(voices),sum(voices)
    model=dict(entries=0,ticks=0,commands=0,key_on=0,key_off=0,pitch=0,volume=0)
    for stats in results[len(jobs):len(jobs)+len(audible_entries)]:
        model['entries']+=1
        for key in ('ticks','commands','key_on','key_off','pitch','volume'):model[key]+=stats[key]
    concurrent=dict(zip(scenarios,results[len(jobs)+len(audible_entries):]))
    # 48 tracks start (the 49th 0x14D and the 0x413 are refused -1), all 44
    # non-stream voices key on and the remaining key-ons find no voice.
    exhaustion=concurrent['exhaustion']
    assert (exhaustion['starts'],exhaustion['refused'],exhaustion['key_on'])==(48,2,44)
    assert exhaustion['no_voice']==2*48-44
    # 00117428 directly on random voice tables (cursor included).
    allocations=0
    shim=lib.shim_new(str(registry.path).encode(),0,0,0,0,0)
    for case in range(pick(1500,150)):
        o=SfxOracle(elf.data)
        fields=[]
        for v in range(48):
            state=rng.choice((0,0,1,1,1));kind=rng.choice((0,1,2,2,3))
            row=[state,rng.choice((0,1)),rng.randrange(0x10000),kind,rng.randrange(0,16),
                 rng.choice((0,0,3,7)),rng.choice((0,2,4)),0]
            fields.append(row)
            base=VOICE_TABLE+v*0x6A
            for offset,value in zip((0,8,0xA,0x1A,0x1E,0x20,0x22),row):o.save(base+offset,value,2)
        cursor=rng.randrange(0x100000000)
        alloc,priority,bank=rng.choice((0,0,3,7)),rng.randrange(0,16),rng.choice((2,4))
        o.save(0x27F740+0x30,cursor)
        o.run(0x117428,(alloc,priority,bank))
        cursor_native=C.c_uint32(cursor)
        flat=(C.c_int*(8*48))(*[x for row in fields for x in row])
        key=lib.shim_allocate(shim,flat,C.byref(cursor_native),alloc,priority,bank)
        assert key==signed(o.r[2]) and cursor_native.value==o.load(0x27F740+0x30),(case,key,signed(o.r[2]))
        allocations+=1
    lib.shim_free(shim)
    # 001FC3C0 (with the original 001FBDB0/001FBD50) and 001FC520 against
    # the native loop service: every callee call, the handle and D_00281B70.
    loop_cases=0
    shim=lib.shim_new(str(registry.path).encode(),0,0,0,0,0)
    for case in range(pick(3000,300)):
        release=case%5==4
        ids=(0x411,0x412,0x413)
        requested=[rng.choice((-1,-1)+ids) for _ in range(48)]
        snapshot=[rng.choice((-1,)+ids) if rng.random()<0.5 else requested[i] for i in range(48)]
        handle=rng.choice((-1,-1,rng.randrange(48)))
        sid=rng.choice(ids);frame=rng.randrange(-0x80000000,0x80000000);ordinal=rng.randrange(-0x8000,0x8000)
        if rng.random()<0.5:frame-=(frame+ordinal)%10
        status=rng.choice((0,1,2,2));in_range=rng.choice((0,1,1))
        left,right=rng.randrange(-0x1000,0x1001),rng.randrange(-0x1000,0x1001)
        start=rng.choice((-1,rng.randrange(48)))
        o=SfxOracle(elf.data);calls=[]
        for i in range(48):o.save(0x281B70+4*i,requested[i]&0xFFFFFFFF);o.save(0x281C30+4*i,snapshot[i]&0xFFFFFFFF)
        o.save(0x70003B68,frame&0xFFFFFFFF);o.save(0x70003B8A,ordinal&0xFFFF,2);o.save(0x990100,handle&0xFFFFFFFF)
        def status_call(r):
            assert r.r[4]==1;calls.append((1,signed(r.r[5]),0,0,0));r.r[2]=status
        def gains_call(r):
            assert r.r[4]==0x990000 and struct.unpack('<f',struct.pack('<I',r.f[12]))[0]==100.0
            assert struct.unpack('<f',struct.pack('<I',r.f[13]))[0]==4096.0
            calls.append((2,0,0,0,0));r.save(r.r[5],left&0xFFFFFFFF);r.save(r.r[6],right&0xFFFFFFFF)
            r.r[2]=in_range
        def start_call(r):
            assert r.r[5]==0x1000
            calls.append((5,signed(r.r[4]),signed(r.r[6]),signed(r.r[7]),0));r.r[2]=start&0xFFFFFFFFFFFFFFFF
        o.calls.update({0x119890:status_call,0x1FBF50:gains_call,
                        0x11A218:lambda r:calls.append((3,signed(r.r[4]),signed(r.r[5]),signed(r.r[6]),0)),
                        0x11A070:lambda r:calls.append((4,signed(r.r[4]),0,0,0)),0x1FB9F0:start_call})
        if release:o.run(0x1FC520,(0x990100,))
        else:o.run(0x1FC3C0,(0x990000,0x990100,sid),floats=(100.0,4096.0))
        native_requested=(C.c_int32*48)(*requested);native_snapshot=(C.c_int32*48)(*snapshot)
        native_handle=C.c_int32(handle);log=(C.c_int32*80)()
        count=lib.shim_loop(shim,native_requested,native_snapshot,C.byref(native_handle),sid,frame,
                            ordinal,status,in_range,left,right,start,release,log)
        native=[tuple(log[5*i:5*i+5]) for i in range(count)]
        assert native==calls,(case,native,calls)
        assert native_handle.value==signed(o.load(0x990100))
        assert list(native_requested)==[signed(o.load(0x281B70+4*i)) for i in range(48)]
        loop_cases+=1
    lib.shim_free(shim)
    # PCSX2 consistency of the envelope model (emulator evidence, not SPU2
    # hardware): every captured AREA11 kind-2 voice's D_002817C0 ENVX word
    # equals the model's level at some SPU sample in the window its +0x1C
    # age allows (key-on then up to one tick of feedback latency).
    envx_checked=[]
    xbindings=X.area_bindings(xelf)[(11,0)]['groups']
    for capture in ('opening_ee.bin','playable_ee.bin','handoff_ee.bin','elevator/clip47_ee.bin',
                    'panel/root/eeMemory.bin','status-hub/eeMemory.bin'):
        data=(DECOMP/'build/startup-reference'/capture).read_bytes()
        assert data[0x810700:0x810702]==bytes([11,0])
        for v in range(48):
            base=VOICE_TABLE+v*0x6A
            state,kind=struct.unpack_from('<H',data,base)[0],struct.unpack_from('<H',data,base+0x1A)[0]
            if state!=1 or kind!=2:continue
            handle,note,prog,age=(struct.unpack_from('<H',data,base+o)[0] for o in (0x22,2,0x3E,0x1C))
            bank=[b for g,banks in xbindings.items() for i,b in enumerate(banks)
                  if u32(data,0x281D50+4*(g*0x14+i))==handle][0]
            hd=bank.hd;state_at=hd+bank.u32(hd+0x20);programs=hd+bank.u32(hd+0x24)
            program=programs+bank.u16(state_at+0x312+2*prog)
            tone=bank.data[program+8+16*(note-bank.u8(program+6)):][:16]
            adsr1,adsr2=struct.unpack_from('<2H',tone,6)
            observed=struct.unpack_from('<i',data,0x2817C0+4*v)[0]
            low,high=(age-1)*48048//60,age*48048//60+1
            levels=(C.c_int32*high)()
            lib.shim_envelope(adsr1,adsr2,high,levels)
            hits=[n for n in range(max(low,0),high) if levels[n]==observed]
            assert hits,(capture,v,hex(adsr1),hex(adsr2),age,observed,levels[low],levels[high-1])
            envx_checked.append(dict(capture=capture,voice=v,adsr=[adsr1,adsr2],age=age,envx=observed,
                                     samples=[hits[0],hits[-1]]))
    # The PCPYH/PCPYLD extension serves the original memset 00121A28 that
    # 00118EC0 uses to free tracks; check that routine on known fills.
    for fill,length,offset in ((0xAB,0x78,0),(0x5C,0x21,0),(0x11,7,3),(0,0x6A,6)):
        o=SfxOracle(elf.data);o.write(0x990000,bytes(range(256))*2)
        o.run(0x121A28,(0x990000+offset,fill,length))
        memory=o.read(0x990000,512)
        assert memory[offset:offset+length]==bytes([fill])*length
        assert memory[:offset]==(bytes(range(256))*2)[:offset]
        assert memory[offset+length:]==(bytes(range(256))*2)[offset+length:]
    # 001FBF50 float_to_int: executed original (software __fixsfsi).
    for value in (4095.9,-4095.9,0.75,-0.75,2217.5,-1.0,0.0):
        o=SfxOracle(elf.data);o.run(0x1281C0,floats=(value,))
        assert signed(o.r[2])==lib.em_sfx_request_word(C.c_float(value/4096.0)),value
        assert signed(o.r[2])==int(value),value
    return dict(entries=len(report['entries']),area11_entries=len(area11),cases=cases,
        voices_checked=voices_checked,spu_samples_checked=spu_checked,loop_samples=loops_checked,
        requests=REQUESTS,model_feedback=model,concurrent=concurrent,allocations=allocations,
        loop_service_cases=loop_cases,envx=envx_checked,registry_sha256=report['registry_sha256'],
        office_scope='2.1 entries exported from a coverage-matched region; no capture, not executed')


def main():
    elf=Elf();ram=(DECOMP/'build/startup-reference/playable_ee.bin').read_bytes()
    adpcm,metadata=source();container=(DECOMP/'extract/chunk15/f00_id43.bin').read_bytes()
    expected_header=container[0x30:0xD60]
    for capture in ('opening_ee.bin','playable_ee.bin'):
        data=(DECOMP/'build/startup-reference'/capture).read_bytes()
        assert data[0x25F396:0x25F398]==bytes([255,0])
        assert data[0x25F410:0x25F414]==bytes([2,0,1,0])
        assert u32(data,0x281DF0)==4
        assert struct.unpack_from('<3I',data,0x27C6C0+4*12)==(1,0x13351F0,0x34000)
        actual=data[0x13351F0:0x13351F0+len(expected_header)]
        differences=[i for i,(a,b) in enumerate(zip(actual,expected_header)) if a!=b]
        assert differences in ([],[1400]),differences
        if differences:assert actual[1400]==64 and expected_header[1400]==0
    events=[];dispatch_cases=0
    dispatch_all=list(itertools.product(range(48),(0,0x800,0x1000),(0,0x1000)))
    # Quick: every track slot once, the six request pairs rotating over the
    # slots (slot 0 keeps 0x1000/0x1000, whose commands the report records).
    dispatch_run=dispatch_all if FULL else [c for i,c in enumerate(dispatch_all) if i%6==(i//6+5)%6]
    for slot,left,right in dispatch_run:
        o=original(elf.data,ram,slot)
        o.run(0x1FB9F0,(0x3EE,0x1000,left,right));assert signed(o.r[2])==-1
        o.run(0x1FB9F0,(0x3EF,0x1000,left,right));assert o.r[2]==slot
        track=0x27E0C0+slot*0x78
        assert o.load(track+0xC)==0x1335374
        assert [o.load(track+p,2) for p in (0x24,0x28,0x26)]==[4,1,0]
        o.run(0x117088,(track,))
        commands=[]
        o.calls[0x1157F0]=lambda r:commands.append(tuple(r.r[i]&0xFFFFFFFF for i in range(4,8)))
        o.run(0x115850,(track,))
        program=container[metadata['program_offset']:metadata['program_offset']+8]
        tone=container[metadata['tone_offset']:metadata['tone_offset']+16]
        context=[o.load(0x281AC0+i*4) for i in range(11)]
        master=o.load(context[2],1)
        channel=bytes(o.load(context[3]+i,1) for i in range(16))
        velocity=bytes(o.load(context[6]+i,1) for i in range(130))
        gains=stereo_gain(elf,program,tone,master,channel,velocity,100,left,right)
        assert commands==[(6,slot,862,0),(1,slot,*gains),(5,slot,0x1E2010,0),
                          (3,slot,0x80FF,0x5FD0)],(commands,gains,left,right,master,list(channel),context)
        assert o.load(0x27F760,8)==1<<slot
        assert o.load(track+8)==4
        o.run(0x118E60,(track,));assert o.load(track+8)==6 and o.load(track+0x20)==0
        o.run(0x117088,(track,));assert o.load(track,1)==255 and o.load(track+2,1)==0x2F
        voice_before=o.load(0x27CCC0+slot*0x6A,0x6A)
        o.run(0x117C28,(track,))
        assert o.load(track+0x34,2)==0 and o.load(track+0x3E,2)==1
        assert o.load(0x27CCC0+slot*0x6A,0x6A)==voice_before
        assert len(commands)==4
        dispatch_cases+=1
        if slot==0 and left==right==0x1000:events=commands
    pitch_cases=0
    for center,note,fine,bend,range_ in itertools.product((33,58),(21,33,58,69),(-8,0,7),
                                                       (32,64,96),(0,2,12)):
        o=SfxOracle(elf.data)
        o.run(0x117918,(center,note,fine&0xFFFFFFFF,bend,range_))
        expected=pitch(elf,center,note,fine,bend,range_)[0]
        assert o.r[2]&0xFFFFFFFF==expected&0xFFFFFFFF,(center,note,fine,bend,range_)
        pitch_cases+=1
    original_spu=DECOMP/'build/area11_sfx_reference/original_spu2.bin'
    if not original_spu.exists():
        code="from pathlib import Path;from tools.parse_pcsx2_state import extract_zstd_entry;p=Path('build/startup-reference/portable-data/sstates/SCUS-97112 (0AE679AF).02.p2s');q=Path('build/area11_sfx_reference/original_spu2.bin');q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes(extract_zstd_entry(p,'SPU2.bin'))"
        subprocess.run([str(DECOMP/'.venv/bin/python'),'-c',code],cwd=DECOMP,check=True)
    spu=original_spu.read_bytes();at=0x10004+0x1E2010
    assert spu[at:at+len(adpcm)]==adpcm
    iop=iop_registers(elf.data)
    registry=registry_oracle(elf,ram)
    report=dict(mode=MODE,elf_sha256=ELF_SHA,dispatch_cases=dispatch_cases,pitch_cases=pitch_cases,iop=iop,registry=registry,
        commands=events,loaded_sample_bytes=len(adpcm),loaded_sample_sha256=hashlib.sha256(adpcm).hexdigest(),
        source=metadata,limits=['Controlled initially free track/voice allocation; no full mixer-state claim',
        '1157F0 hardware command sink is replaced; all dispatch/pitch/gain words execute',
        'SPU2 ADSR is a documented-semantics model checked only against PCSX2 ENVX feedback; no SPU2 output capture',
        'No SPU2 interpolation, reverb or final output waveform comparison'])
    banner(part(dispatch_cases,len(dispatch_all),'dispatch cases (every slot, every request pair)'),
           part(pitch_cases,216,'pitch cases'),part(iop['voices'],48,'IOP voices'),
           part(registry['cases'],335,'registry entry x request cases (every entry, every request)'),
           part(registry['allocations'],1500,'00117428 tables'),part(registry['loop_service_cases'],3000,'loop-service cases'),
           f"random scenario {registry['concurrent']['random']['ticks']} ticks; SPU/bank captures, "
           f"{len(registry['envx'])} ENVX capture words, model-feedback entries, services and exhaustion scenarios in full")
    out=ROOT/'build/area11_sfx_reference';out.mkdir(parents=True,exist_ok=True)
    (out/'original_report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f"PASS {dispatch_cases} original panel dispatch/voice/end-track cases; {pitch_cases} pitch cases; {len(adpcm)} live SPU sample bytes; {iop['register_writes']} original IOP/libsd register writes")
    model=registry['model_feedback'];concurrent=registry['concurrent']
    print(f"PASS registry: {registry['area11_entries']} AREA11-playable entries x {len(REQUESTS)} requests "
          f"({registry['cases']} executed cases, {registry['voices_checked']} A0 voices: pitch/volume/address/ADSR words "
          f"= original 001FB9F0+001152D8+00115850 commands), {registry['spu_samples_checked']} samples = live SPU RAM "
          f"({registry['loop_samples']} loop points from the loaded block flags)")
    print(f"PASS sequencer lockstep (original 001152D8 vs native driver, every command/track/voice field per tick): "
          f"{model['entries']} entries with SPU2-model ENVX feedback ({model['ticks']} ticks, {model['key_on']} key-ons, "
          f"{model['key_off']} key-offs, {model['pitch']} pitch and {model['volume']} volume words); scenarios "
          + ', '.join(f"{k} {v['ticks']} ticks/{v['starts']} starts/{v['key_off']} key-offs" for k,v in concurrent.items()))
    print(f"PASS 00117428 x{registry['allocations']} random voice tables, 001FC3C0/001FC520 x{registry['loop_service_cases']} "
          f"service cases, {len(registry['envx'])} captured ENVX feedback words = ADSR model (PCSX2 consistency, not hardware), "
          f"{iop['switch_register_writes']} KON/KOFF/effect register writes from the original IOP driver")

if __name__=='__main__':main()
