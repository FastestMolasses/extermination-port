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
  elif op==0 and fn==39:self.r[rd]=~(self.r[rs]|self.r[rt])&0xffffffffffffffff
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
    for voice in range(48):
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
    return dict(voices=48,register_writes=48*7,unrelocated_module_words=checked,
        module_sha256=hashlib.sha256(raw).hexdigest(),captured_base=base,
        limit='Original driver and libsd execute; hardware register stores are observed, not synthesized sound')


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
    for slot,left,right in itertools.product(range(48),(0,0x800,0x1000),(0,0x1000)):
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
    report=dict(elf_sha256=ELF_SHA,dispatch_cases=dispatch_cases,pitch_cases=pitch_cases,iop=iop,
        commands=events,loaded_sample_bytes=len(adpcm),loaded_sample_sha256=hashlib.sha256(adpcm).hexdigest(),
        source=metadata,limits=['Controlled initially free track/voice allocation; no full mixer-state claim',
        '1157F0 hardware command sink is replaced; all dispatch/pitch/gain words execute',
        'No SPU2 interpolation, envelope microtiming, reverb or final output waveform comparison'])
    out=ROOT/'build/area11_sfx_reference';out.mkdir(parents=True,exist_ok=True)
    (out/'original_report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'PASS {dispatch_cases} original panel dispatch/voice/end-track cases; {pitch_cases} pitch cases; {len(adpcm)} live SPU sample bytes; 336 original IOP/libsd register writes')

if __name__=='__main__':main()
