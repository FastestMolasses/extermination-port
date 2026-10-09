#!/usr/bin/env python3
"""Check the native SFX driver's output against an independent SPU2 model
driven by the original sequencer.

The native driver runs on the game thread (chain step AUDIO): one field is
the 001152D8 tick, the IOP exchange (the queued commands go to the IOP
driver's ring, the reply is its last status) and the field's 48 kHz
samples, the commands applied at the IOP driver's ticks (every 128
half-lines on the field clock); em_sfx_mix drains the samples. This tests
the pitch cursor, gains, loops, ADSR, the exchange's timing and the mixer
ring's partitioning; it is not a comparison with the SPU2's Gaussian
interpolator or reverb output.
"""
import json
import shutil
import struct
import subprocess
import wave
from export_area11_sfx import ROOT
from test_area11_sfx_reference import parse_emsr


def main():
    registry_runtime()


def q14(word):
    value=word&0x7FFF
    return (value-0x8000 if value&0x4000 else value)/16384


# ---- Independent SPU2 model (expected output) ------------------------------
# Written from the documented SPU2 register semantics, separately from
# src/game/em_sfx_bank.c: ADSR rate = shift*4 + step index, a step every
# 1 << max(0, shift-11) samples (x4 above 0x6000 on an exponential rise,
# capped at 0x8000), steps scaled by 1 << max(0, 11-shift), exponential
# falls scaled by level/0x8000 (floor). The voice plays its decoded source
# at pitch/4096 of 48 kHz, repeats the loop body, and ends at a non-loop end.

def envelope_rate(rate,decrease,exponential,level):
    shift,index=rate>>2,rate&3
    step=-8+index if decrease else 7-index
    wait=1<<max(0,shift-11)
    if shift<11:step*=1<<(11-shift)
    if exponential and not decrease and level>0x6000:wait*=4
    wait=min(wait,0x8000)
    if exponential and decrease:step=(step*level)>>15
    return wait,step


class Envelope:
    def __init__(self,adsr1,adsr2):
        self.adsr1,self.adsr2=adsr1,adsr2
        self.phase,self.level,self.count='attack',0,0

    def release(self):
        if self.phase=='off':return
        self.phase,self.count='release',0
        if self.level<=0:self.level,self.phase=0,'off'

    def step(self):
        a1,a2=self.adsr1,self.adsr2
        if self.phase=='decay':
            target=((a1&0xF)+1)*0x800
            if self.level<=target:self.phase,self.count='sustain',0;return
            wait,step=envelope_rate((a1>>4&0xF)*4,True,True,self.level)
        elif self.phase=='attack':wait,step=envelope_rate(a1>>8&0x7F,False,a1>>15,self.level)
        elif self.phase=='sustain':wait,step=envelope_rate(a2>>6&0x7F,a2>>14&1,a2>>15,self.level)
        elif self.phase=='release':wait,step=envelope_rate((a2&0x1F)*4,True,a2>>5&1,self.level)
        else:return
        self.count+=1
        if self.count<wait:return
        self.count=0
        self.level+=step
        if self.phase=='attack' and self.level>=0x7FFF:self.level,self.phase=0x7FFF,'decay'
        elif self.phase=='decay':
            self.level=max(self.level,0)
            if self.level<=((a1&0xF)+1)*0x800:self.phase='sustain'
        elif self.phase=='sustain':self.level=min(max(self.level,0),0x7FFF)
        elif self.phase=='release' and self.level<=0:self.level,self.phase=0,'off'


class Spu:
    def __init__(self,samples,rate):
        self.samples,self.rate=samples,rate
        self.registers={v:dict(pitch=0,volume=(0,0),sample=None,adsr=(0,0)) for v in range(48)}
        self.voices={}

    def command(self,command,voice,a,b):
        if command==6:self.registers[voice]['pitch']=a
        elif command==1:self.registers[voice]['volume']=(a,b)
        elif command==5:self.registers[voice]['sample']=a
        elif command==3:
            self.registers[voice]['adsr']=(a,b)
            if voice in self.voices:
                self.voices[voice]['envelope'].adsr1,self.voices[voice]['envelope'].adsr2=a,b
        elif command in (0xA,0xB):
            mask=a|b<<24
            for v in range(48):
                if not mask>>v&1:continue
                if command==0xA and self.registers[v]['sample'] is not None:
                    self.voices[v]=dict(sample=self.samples[self.registers[v]['sample']],
                        envelope=Envelope(*self.registers[v]['adsr']),n=0,steps=0,phase=0)
                elif command==0xB and v in self.voices and self.voices[v]['n']:
                    # A key-off before the voice's first sample since its
                    # key-on (the same exchange) is lost: measured,
                    # CAPTURES_AUDIO.md (the flame's 0x413).
                    self.voices[v]['envelope'].release()
                    if self.voices[v]['envelope'].phase=='off':del self.voices[v]

    def envx(self,voice):
        return self.voices[voice]['envelope'].level if voice in self.voices else 0

    def render(self,out,first,count):
        denominator=4096*self.rate
        for v in list(self.voices):
            voice=self.voices[v];sample=voice['sample'];envelope=voice['envelope']
            pcm,frames,loop=sample['values'],sample['frames'],sample['loop_start']
            pitch=min(self.registers[v]['pitch'],0x3FFF)
            left,right=(q14(w) for w in self.registers[v]['volume'])
            for i in range(first,first+count):
                due=voice['n']*48000//self.rate
                while voice['steps']<due and envelope.phase!='off':
                    envelope.step();voice['steps']+=1
                voice['steps']=due
                if envelope.phase=='off':del self.voices[v];break
                index,remainder=divmod(voice['phase'],denominator)
                if loop is None and index>=frames:del self.voices[v];break
                nxt=frames-1 if loop is None and index+1>=frames else index+1
                a=pcm[index] if index<frames else pcm[frames+(index-frames)%(frames-loop)]
                b=pcm[nxt] if nxt<frames else pcm[frames+(nxt-frames)%(frames-loop)]
                value=(a+(b-a)*remainder/denominator)/32768*(envelope.level/32768)
                out[2*i]+=value*left;out[2*i+1]+=value*right
                voice['phase']+=48000*pitch;voice['n']+=1
                if loop is not None:
                    body=frames-loop
                    if voice['phase']>=(frames+body)*denominator:voice['phase']-=body*denominator


def expected_mix(registry,samples,sid,first_field,request,limit=4000):
    """Original 001FB9F0 + 001152D8 per field drive the independent SPU2
    model through the exchange: the reaper reads the previous exchange's
    reply (the status of the IOP driver's last tick), the tick's commands
    run at the IOP driver's next tick (every 128 half-lines), each IOP tick
    snapshots every voice's ENVX. The play comes before field first_field.
    Returns (output from that field on, the field it settled at)."""
    from test_area11_sfx_reference import signed
    o=registry.oracle();commands=[]
    o.calls[0x1157F0]=lambda r:commands.append(tuple(r.r[i]&0xFFFFFFFF for i in range(4,8)))
    o.calls[0x1191F0]=lambda r:None
    left,right=request
    o.run(0x1FB9F0,(sid,0x1000,left&0xFFFFFFFFFFFFFFFF,right&0xFFFFFFFFFFFFFFFF))
    assert signed(o.r[2])==0,hex(sid)
    address={a:i for i,a in registry.address.items()}
    spu=Spu(samples,48000);out=[]
    half=first_field*525;done=half*572//375;base=done
    status=[0]*48;reply=[0]*48;ring=[]
    def render_to(target):
        nonlocal done
        out.extend([0.0]*(2*(target-done)))
        spu.render(out,done-base,target-done);done=target
    for field in range(limit):
        for v in range(48):o.save(0x2817C0+4*v,reply[v])
        commands.clear();o.run(0x1152D8)
        reply=status[:]
        ring+=[(c,v,address[a] if c==5 else a,b) for c,v,a,b in commands]
        end=half+525
        h=(half//128+1)*128
        while h<=end:
            render_to(h*572//375)
            for c in ring:spu.command(*c)
            ring=[]
            status=[spu.envx(v)&0x7FFF for v in range(48)]
            h+=128
        render_to(end*572//375);half=end
        allocated=any(o.load(0x27E0C0+t*0x78+0x32,2) for t in range(48))
        if field and not allocated and not spu.voices and not ring:return out,field+1
    raise AssertionError('expected mix did not settle')


def registry_runtime():
    """EMSR v2: loader refusals, scopes, the native driver's renders against
    the independent model fed by original sequencer execution."""
    import export_sfx_registry as X
    from test_area11_sfx_reference import Registry
    from export_area11_sfx import Elf,DECOMP
    installed=ROOT/'assets/sfx/sfx_registry.emsr'
    assert installed.exists(),'run tools/export_sfx_registry.py first'
    raw=installed.read_bytes()
    report=json.loads((ROOT/'assets/sfx/sfx_registry.json').read_text())
    entries,samples,ladder=parse_emsr(raw)
    for sample in samples:
        sample['values']=struct.unpack('<%dh'%(len(sample['pcm'])//2),sample['pcm'])
    out=ROOT/'build/area11_sfx_reference/runtime_registry'
    if out.exists():shutil.rmtree(out)
    (out/'assets/sfx/area11').mkdir(parents=True)
    (out/'assets/sfx/sfx_registry.emsr').write_bytes(raw)
    # Byte offsets of the v2 layout.
    first_entry=24+2*len(ladder)
    first=entries[0];assert first['state']==1 and first['ops'][0]['kind']==1
    op=first_entry+16
    second=op+32*len(first['ops'])
    invalid=[raw[:23],raw[:-1],raw+b'\0']
    for offset,value in ((0,b'X'),(4,b'\1'),(20,b'\1'),(16,struct.pack('<I',0x23F)),(8,b'\0\0\0\0'),
                         (first_entry+8,b'\x09'),(first_entry+14,b'\1'),(first_entry+9,b'\0'),
                         (first_entry+10,b'\1'),(first_entry+4,struct.pack('<h',24)),
                         (op+2,b'\x09'),(op+8,b'\0\0'),(op+8,struct.pack('<H',0x4000)),
                         (op+6,struct.pack('<H',len(samples))),(op+12,struct.pack('<I',0x7FFFFFFF)),
                         (op+5,b'\x20'),(op+5,b'\x02'),(op+27,b'\1'),(op+25,b'\1'),
                         (op+20,bytes([raw[op+20]^1])),(second-32+2,b'\1')):
        data=bytearray(raw);data[offset:offset+len(value)]=value;invalid.append(bytes(data))
    duplicate=bytearray(raw)                        # entry 1 := entry 0 scope/id
    duplicate[second:second+8]=raw[first_entry:first_entry+8];invalid.append(bytes(duplicate))
    at=second                                       # a later op tick earlier than its predecessor
    if len(first['ops'])>1:
        data=bytearray(raw);struct.pack_into('<H',data,op+32,first['ops'][0]['tick']);
        struct.pack_into('<H',data,op,first['ops'][1]['tick']+1);invalid.append(bytes(data))
    samples_at=len(raw)-sum(8+len(x['pcm']) for x in samples)
    data=bytearray(raw);struct.pack_into('<I',data,samples_at+4,samples[0]['frames']);invalid.append(bytes(data))
    data=bytearray(raw);struct.pack_into('<I',data,samples_at,0);invalid.append(bytes(data))
    for i,data in enumerate(invalid):(out/f'bad_{i}.emsr').write_bytes(data)
    (out/'scopes.txt').write_text(''.join(f"{e['id']:X} {e['scope'][0]} {e['scope'][1]} {e['state']}\n"
                                          for e in entries))
    # Refusal fixture: the same registry with 0x452 (11.0) UNSUPPORTED.
    refusal=out/'refusal';(refusal/'assets/sfx/area11').mkdir(parents=True)
    at=first_entry;rebuilt=bytearray(raw[:first_entry])
    for entry in entries:
        size=16+32*len(entry['ops'])
        if (entry['id'],entry['scope'])==(0x452,[11,0]):
            rebuilt+=struct.pack('<IhhBBHHH',0x452,11,0,3,0,1,0,0)
        else:rebuilt+=raw[at:at+size]
        at+=size
    rebuilt+=raw[at:]
    (refusal/'assets/sfx/sfx_registry.emsr').write_bytes(bytes(rebuilt))
    # Renders: a three-voice one-shot script, a looping voice keyed off
    # after 29 ticks, and the elevator script (loops, portamento, key-offs).
    elf=Elf();ram=(DECOMP/'build/startup-reference/playable_ee.bin').read_bytes()
    registry=Registry(elf,ram,report,installed)
    ids=[0x1A1,0x14D,0x452]
    requests_default=(0x1000,0x1000)
    # Fields per render: the settling length at a few IOP phases, plus four.
    frames={sid:max(expected_mix(registry,samples,sid,first,requests_default)[1] for first in (0,5,37))+4
            for sid in ids}
    executable=out/'test'
    subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-ffp-contract=off','-Isrc',
        'tests/sfx_registry_test.c','src/game/em_sfx_bank.c','src/game/em_sfx.c',
        '-o',str(executable)],cwd=ROOT,check=True)
    subprocess.run([str(executable),str(len(invalid)),','.join(hex(i) for i in ids),
                    ','.join(str(frames[i]) for i in ids),'refusal'],cwd=out,check=True)
    reports=[]
    request=tuple(map(int,(out/'reg_requests.txt').read_text().split()))
    # The first field of each render (one field clock for the fixture's
    # whole run: each render meets the IOP driver's ticks at its own phase).
    starts=list(map(int,(out/'reg_starts.txt').read_text().split()))
    assert len(starts)==2*len(ids)+1,starts
    for n,sid in enumerate(ids):
        for chunk,first in ((1,starts[2*n]),(997,starts[2*n+1])):
            data=(out/f"reg_{sid:X}_{chunk}.f32").read_bytes()
            mix,_=expected_mix(registry,samples,sid,first,requests_default)
            reports.append(compare(sid,first,chunk,data,mix,field_frames(first,frames[sid]),requests_default))
    first=starts[-1]
    mix,_=expected_mix(registry,samples,ids[0],first,request)
    positional=(out/f"reg_{ids[0]:X}_997_at.f32").read_bytes()
    reports.append(compare(ids[0],first,997,positional,mix,field_frames(first,frames[ids[0]]),request))
    (out/'report.json').write_text(json.dumps(dict(ids=ids,fields=frames,
        malformed_cases=len(invalid),scopes=len(entries),renders=reports,
        registry_sha256=report['registry_sha256'],
        model='independent Python SPU2 model driven by original 001FB9F0+001152D8 commands through the '
              'IOP exchange; ADSR/interpolation are the documented hardware model, not SPU2 output'),indent=2)+'\n')
    print(f"PASS EMSR v2 registry: {len(invalid)} malformed files refused, {len(entries)} scoped entries; "
          f"ids {', '.join(hex(i) for i in ids)} native fields = independent SPU2 model on the original "
          f"command stream through the IOP exchange at 48 kHz (1- and 997-frame drains, each render at its own "
          f"IOP phase) and a positional request; other device rates mix nothing; 001FC3C0's flame loop "
          f"(its same-exchange key-off lost: one start, one held track), stop-all and UNSUPPORTED refusal, ASan/UBSan")


def field_frames(first,fields):
    """Output frames of fields [first, first + fields) on the field clock."""
    return (first+fields)*48000*1001//60000-first*48000*1001//60000


def compare(sid,first,chunk,data,expected,frames,request):
    output=struct.unpack('<%df'%(len(data)//4),data)
    assert len(output)==2*frames,(hex(sid),len(output),frames)
    padded=list(expected)+[0.0]*(2*frames-len(expected))
    error=max(abs(a-b) for a,b in zip(output,padded[:2*frames]))
    assert error<2e-7,(hex(sid),first,chunk,error)
    assert any(output) and not any(output[-2*32:])
    return dict(id=sid,first_field=first,chunk=chunk,request=request,max_error=error,frames=frames)

if __name__=='__main__':main()
