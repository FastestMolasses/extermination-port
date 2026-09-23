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


# ---- EMSR registry: every exported id AREA11 can play (WP-14) -------------

REQUESTS=((0x1000,0x1000),(0x800,-0x400),(0,0x1000),(-0x1000,0x7FF),(0x1001,0x200))
TRACK_TABLE,VOICE_TABLE=0x27E0C0,0x27CCC0


def parse_emsr(blob):
    """Independent reader of the registry the native loader consumes."""
    magic,version,sample_count,entry_count,reserved=struct.unpack_from('<4sIIII',blob)
    assert (magic,version,reserved)==(b'EMSR',1,0)
    at=20;entries=[]
    for _ in range(entry_count):
        sid,area,sub,state,count,reason,zero=struct.unpack_from('<IhhBBHI',blob,at);at+=16
        assert zero==0
        events=[]
        for _ in range(count):
            events.append(dict(zip(('tick','sample','pitch','pan','scalar','adsr1','adsr2','flags'),
                struct.unpack_from('<HHHHIHHB',blob,at))));at+=20
        entries.append(dict(id=sid,scope=[area,sub],state=state,reason=reason,events=events))
    samples=[]
    for _ in range(sample_count):
        frames=u32(blob,at);at+=4
        samples.append(blob[at:at+2*frames]);at+=2*frames
    assert at==len(blob)
    return entries,samples


def native_bank():
    out=ROOT/'build/area11_sfx_reference';out.mkdir(parents=True,exist_ok=True)
    library=out/'libem_sfx_bank.dylib'
    subprocess.run(['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-shared','-fPIC',
        '-Isrc','src/game/em_sfx_bank.c','-o',str(library)],cwd=ROOT,check=True)
    lib=C.CDLL(str(library))
    lib.em_sfx_volume_words.argtypes=[C.c_uint32,C.c_uint16,C.c_int32,C.c_int32,C.POINTER(C.c_uint16)]
    lib.em_sfx_request_word.argtypes=[C.c_float];lib.em_sfx_request_word.restype=C.c_int32
    def words(scalar,pan,left,right):
        pair=(C.c_uint16*2)()
        lib.em_sfx_volume_words(scalar,pan,left,right,pair)
        return list(pair)
    return lib,words


def registry_oracle(elf,ram):
    import export_sfx_registry as X
    out=ROOT/'build/area11_sfx_reference/registry'
    report=X.export(out)
    blob=(out/'sfx_registry.emsr').read_bytes()
    installed=ROOT/'assets/sfx/sfx_registry.emsr'
    if installed.exists():
        assert installed.read_bytes()==blob,'assets/sfx/sfx_registry.emsr is stale: re-run the exporter'
    entries,samples=parse_emsr(blob)
    assert len(entries)==len(report['entries'])
    for mine,theirs in zip(entries,report['entries']):
        voices=[e for e in theirs.get('events',[]) if e['kind']=='voice']
        assert (mine['id'],mine['scope'],mine['state'])==(theirs['id'],theirs['scope'],theirs['state'])
        assert [(e['tick'],e['sample'],e['pitch'],e['pan'],e['scalar'],e['adsr1'],e['adsr2'],e['flags'])
                for e in mine['events']]==[(e['tick'],e['sample'],e['pitch'],e['pan'],e['scalar'],
                e['adsr1'],e['adsr2'],e['flags']) for e in voices]
    # Bank binding: both captures register the same handles; each RAM header
    # equals the bound container bank except the per-track bend bytes.
    xelf=X.Elf();bindings=X.area_bindings(xelf)[(11,0)]['groups']
    handles={}
    for capture in ('opening_ee.bin','playable_ee.bin'):
        data=(DECOMP/'build/startup-reference'/capture).read_bytes()
        assert data[0x810700:0x810702]==bytes([11,0])
        assert struct.unpack_from('<H',data,0x27F740+0x3A)[0]==60      # tick divisor
        assert struct.unpack_from('<H',data,0x27F778)[0]==0             # stereo
        for group,banks in bindings.items():
            for index,bank in enumerate(banks):
                handle=u32(data,0x281D50+4*(group*0x14+index))
                use,header,spu=struct.unpack_from('<3I',data,0x27C6C0+12*handle)
                assert use==1
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
    spu_checked=0
    sample_address={}
    for sample in report['samples']:
        for (name,row),(handle,base,body) in handles.items():
            bank=[b for g in bindings.values() for b in g if (b.name,b.row)==(name,row)][0]
            if name==sample['container'] and body<=sample['offset']<body+bank.body_size:
                data=(DECOMP/name).read_bytes()
                raw=data[sample['offset']:sample['offset']+sample['adpcm_bytes']]
                address=base+sample['offset']-body
                assert spu[0x10004+address:0x10004+address+len(raw)]==raw
                sample_address[sample['index']]=address
                spu_checked+=1
                break
    lib,native_words=native_bank()
    area11=[e for e in report['entries'] if e['scope'] in ([-1,-1],[11,0])]
    cases=voices_checked=0;summary=[]
    for entry in area11:
        for left,right in REQUESTS:
            o=original(elf.data,ram,0)
            for (name,row),(handle,base,body) in handles.items():
                bank=[b for g in bindings.values() for b in g if (b.name,b.row)==(name,row)][0]
                header=u32(ram,0x27C6C0+12*handle+4)
                o.write(header,ram[header:header+bank.header_end-bank.hd])
            o.write(0x27F778,ram[0x27F778:0x27F77A])
            o.run(0x1FB9F0,(entry['id'],0x1000,left&0xFFFFFFFFFFFFFFFF,right&0xFFFFFFFFFFFFFFFF))  # 64-bit sign-extended GPRs
            track=signed(o.r[2])
            if entry['state']==X.STATE_ABSENT:
                assert track==-1,hex(entry['id']);cases+=1;continue
            assert track==0,(hex(entry['id']),track)
            commands=[];tick=[0]
            o.calls[0x1157F0]=lambda r:commands.append((tick[0],)+tuple(r.r[i]&0xFFFFFFFF for i in range(4,8)))
            o.calls[0x1191F0]=lambda r:None
            limit=1500 if entry['state']==X.STATE_UNSUPPORTED else 80
            for tick[0] in range(limit):
                o.run(0x1152D8)
                if o.load(TRACK_TABLE+0x34,2)==0:break
            else:
                assert entry['state']==X.STATE_UNSUPPORTED,('track did not end',hex(entry['id']))
            if entry['state']==X.STATE_UNSUPPORTED:
                summary.append(dict(id=entry['id'],reason=entry['reason'],ticks=tick[0]+1,
                    ended=o.load(TRACK_TABLE+0x34,2)==0,
                    key_on=sum(bin(c[3]|c[4]<<24).count('1') for c in commands if c[1]==0xA),
                    key_off=sum(bin(c[3]|c[4]<<24).count('1') for c in commands if c[1]==0xB),
                    commands=len(commands)))
                cases+=1;break
            request=(left,right) if all(-0x1000<=x<=0x1000 for x in (left,right)) else (0x1000,0x1000)
            assert not [c for c in commands if c[1]==0xB],hex(entry['id'])
            live={}
            exported=[e for e in entry['events'] if e['kind']=='voice']
            # Every exported voice must fall inside the executed track, and
            # each must be matched by exactly one started original voice.
            assert all(e['tick']<=tick[0] for e in exported),(hex(entry['id']),tick[0])
            case_voices=0
            for t in range(tick[0]+1):
                these=[c[1:] for c in commands if c[0]==t]
                on=0
                for c in these:
                    if c[0]==0xA:on|=c[2]|c[3]<<24
                started=[]
                for v in range(48):
                    if not on>>v&1:continue
                    first={cmd:next(c for c in these if c[0]==cmd and c[1]==v) for cmd in (6,1,5,3)}
                    started.append((first[6][2],first[1][2],first[1][3],first[5][2],first[3][2],first[3][3]))
                    live[v]=started[-1]
                for c in these:          # re-sends must repeat the voice's values
                    if c[0]==6:assert c[2]==live[c[1]][0]
                    if c[0]==1:assert (c[2],c[3])==live[c[1]][1:3]
                expected=[]
                for e in entry['events']:
                    if e['kind']!='voice' or e['tick']!=t:continue
                    words=X.volume_words(e['scalar'],e['pan'],*request)
                    assert native_words(e['scalar'],e['pan'],left,right)==list(words)
                    expected.append((e['pitch'],*words,sample_address[e['sample']]+0,e['adsr1'],e['adsr2']))
                assert sorted(started)==sorted(expected),(hex(entry['id']),t,started,expected)
                voices_checked+=len(started);case_voices+=len(started)
            assert case_voices==len(exported),(hex(entry['id']),case_voices,len(exported))
            cases+=1
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
        voices_checked=voices_checked,spu_samples_checked=spu_checked,requests=REQUESTS,
        unsupported=summary,registry_sha256=report['registry_sha256'],
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
    registry=registry_oracle(elf,ram)
    report=dict(elf_sha256=ELF_SHA,dispatch_cases=dispatch_cases,pitch_cases=pitch_cases,iop=iop,registry=registry,
        commands=events,loaded_sample_bytes=len(adpcm),loaded_sample_sha256=hashlib.sha256(adpcm).hexdigest(),
        source=metadata,limits=['Controlled initially free track/voice allocation; no full mixer-state claim',
        '1157F0 hardware command sink is replaced; all dispatch/pitch/gain words execute',
        'No SPU2 interpolation, envelope microtiming, reverb or final output waveform comparison'])
    out=ROOT/'build/area11_sfx_reference';out.mkdir(parents=True,exist_ok=True)
    (out/'original_report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'PASS {dispatch_cases} original panel dispatch/voice/end-track cases; {pitch_cases} pitch cases; {len(adpcm)} live SPU sample bytes; 336 original IOP/libsd register writes')
    print(f"PASS registry: {registry['area11_entries']} AREA11-playable entries x {len(REQUESTS)} requests "
          f"({registry['cases']} executed cases, {registry['voices_checked']} A0 voices: pitch/volume/address/ADSR words "
          f"= original 001FB9F0+001152D8+00115850 commands), {registry['spu_samples_checked']} samples = live SPU RAM, "
          f"{len(registry['unsupported'])} unsupported ids recorded")

if __name__=='__main__':main()
