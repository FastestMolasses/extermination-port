#!/usr/bin/env python3
"""Check the native dry mixer independently from its exported source PCM.

This tests rational pitch/gain, callback partitioning and lifetime; it is
not a comparison with the SPU2's Gaussian interpolator or reverb output.
"""
import json
import shutil
import struct
import subprocess
import wave
from export_area11_sfx import ROOT
from test_area11_sfx_reference import parse_emsr


def main():
    out=ROOT/'build/area11_sfx_reference/runtime'
    assets=out/'assets/sfx/area11';assets.mkdir(parents=True,exist_ok=True)
    source=ROOT/'assets/sfx/area11'
    raw=(source/'panel_sfx.emsf').read_bytes()
    (assets/'panel_sfx.emsf').write_bytes(raw)
    invalid=[raw[:19],raw[:-1],raw+b'\0']
    for offset,value in ((0,0),(4,2),(8,12),(20,0),(44+8,0),(44+14,0),(68,1)):
        data=bytearray(raw);data[offset]=value;invalid.append(data)
    for i,data in enumerate(invalid):(out/f'bad_{i}.emsf').write_bytes(data)
    executable=out/'test'
    subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-ffp-contract=off','-Isrc',
        'tests/area11_sfx_test.c','src/game/em_sfx_bank.c','src/game/em_sfx.c',
        '-o',str(executable)],cwd=ROOT,check=True)
    subprocess.run([str(executable)],cwd=out,check=True)
    with wave.open(str(source/'cue_03ef_source.wav')) as wav:
        assert wav.getframerate()==48000 and wav.getnchannels()==1
        pcm=struct.unpack('<%dh'%wav.getnframes(),wav.readframes(wav.getnframes()))
    gains=2217/16384*32767/32768/32768
    reports=[]
    for rate in (48000,44100,96000):
        a=(out/f'mix_{rate}_1.f32').read_bytes()
        b=(out/f'mix_{rate}_997.f32').read_bytes()
        assert a==b, 'callback partition changed waveform'
        output=struct.unpack('<%df'%(len(a)//4),a)
        expected_length=(len(pcm)*rate*4096+48000*862-1)//(48000*862)
        max_error=0
        for i in range(len(output)//2):
            if i<expected_length:
                index,remainder=divmod(i*48000*862,rate*4096)
                fraction=remainder/(rate*4096)
                sample=pcm[index]*(1-fraction)+pcm[min(index+1,len(pcm)-1)]*fraction
                expected=sample*gains
            else:expected=0
            assert output[2*i]==output[2*i+1]
            max_error=max(max_error,abs(output[2*i]-expected))
        assert max_error<3e-8,(rate,max_error)
        assert not any(output[expected_length*2:])
        reports.append(dict(rate=rate,source_cursor_frames=expected_length,
            callback_sizes=[1,997],max_linear_gain_error=max_error))
    (out/'report.json').write_text(json.dumps(dict(malformed_cases=len(invalid),
        rates=reports,scope='native linear dry output only; no hardware waveform claim'),indent=2)+'\n')
    print('PASS native rational pitch/gain at 44.1/48/96 kHz, callback partition invariance, ASan/UBSan')
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
                elif command==0xB and v in self.voices:
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


def expected_mix(registry,samples,sid,rate,request,limit=4000):
    """Original 001FB9F0 + 001152D8 per tick (feedback = this model's ENVX)
    drive the independent SPU2 model. Returns (output, idle frame)."""
    from test_area11_sfx_reference import signed
    o=registry.oracle();commands=[]
    o.calls[0x1157F0]=lambda r:commands.append(tuple(r.r[i]&0xFFFFFFFF for i in range(4,8)))
    o.calls[0x1191F0]=lambda r:None
    left,right=request
    o.run(0x1FB9F0,(sid,0x1000,left&0xFFFFFFFFFFFFFFFF,right&0xFFFFFFFFFFFFFFFF))
    assert signed(o.r[2])==0,hex(sid)
    address={a:i for i,a in registry.address.items()}
    spu=Spu(samples,rate);out=[];frame=0
    for tick in range(limit):
        start=tick*rate*1001//60000
        out.extend([0.0]*(2*(start-frame)))
        spu.render(out,frame,start-frame);frame=start
        for v in range(48):o.save(0x2817C0+4*v,spu.envx(v))
        commands.clear();o.run(0x1152D8)
        for c,v,a,b in commands:spu.command(c,v,address[a] if c==5 else a,b)
        allocated=any(o.load(0x27E0C0+t*0x78+0x32,2) for t in range(48))
        if tick and not allocated and not spu.voices:return out,start
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
    shutil.copy(ROOT/'assets/sfx/area11/panel_sfx.emsf',out/'assets/sfx/area11/panel_sfx.emsf')
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
    shutil.copy(ROOT/'assets/sfx/area11/panel_sfx.emsf',refusal/'assets/sfx/area11/panel_sfx.emsf')
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
    expected={};frames={}
    for sid in ids:
        for rate in (48000,44100,96000):
            mix,idle=expected_mix(registry,samples,sid,rate,requests_default)
            expected[(sid,rate)]=mix
            frames[sid]=max(frames.get(sid,0),idle+64)
    executable=out/'test'
    subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-ffp-contract=off','-Isrc',
        'tests/sfx_registry_test.c','src/game/em_sfx_bank.c','src/game/em_sfx.c',
        '-o',str(executable)],cwd=ROOT,check=True)
    subprocess.run([str(executable),str(len(invalid)),','.join(hex(i) for i in ids),
                    ','.join(str(frames[i]) for i in ids),'refusal'],cwd=out,check=True)
    reports=[]
    request=tuple(map(int,(out/'reg_requests.txt').read_text().split()))
    expected[(ids[0],48000,'at')],_=expected_mix(registry,samples,ids[0],48000,request)
    for sid in ids:
        for rate in (48000,44100,96000):
            one=(out/f"reg_{sid:X}_{rate}_1.f32").read_bytes()
            chunked=(out/f"reg_{sid:X}_{rate}_997.f32").read_bytes()
            # The 00117428 cursor carries over between renders, so the two
            # runs sum their voices in a different index order: float
            # rounding only (a few ulps), never a timing difference.
            a=struct.unpack('<%df'%(len(one)//4),one);b=struct.unpack('<%df'%(len(chunked)//4),chunked)
            assert len(a)==len(b) and max(abs(x-y) for x,y in zip(a,b))<1.2e-7,\
                ('callback partition changed the waveform',hex(sid),rate)
            reports.append(compare(sid,rate,997,one,expected[(sid,rate)],frames[sid],requests_default))
    positional=(out/f"reg_{ids[0]:X}_48000_997_at.f32").read_bytes()
    reports.append(compare(ids[0],48000,997,positional,expected[(ids[0],48000,'at')],frames[ids[0]],request))
    (out/'report.json').write_text(json.dumps(dict(ids=ids,frames=frames,
        malformed_cases=len(invalid),scopes=len(entries),renders=reports,
        registry_sha256=report['registry_sha256'],
        model='independent Python SPU2 model driven by original 001FB9F0+001152D8 commands; '
              'ADSR/interpolation are the documented hardware model, not SPU2 output'),indent=2)+'\n')
    print(f"PASS EMSR v2 registry: {len(invalid)} malformed files refused, {len(entries)} scoped entries; "
          f"ids {', '.join(hex(i) for i in ids)} native mix = independent SPU2 model on the original "
          f"command stream at 44.1/48/96 kHz (1- and 997-frame callbacks within 1.2e-7) and a positional request; "
          f"001FC3C0 flame cadence, stop-all and UNSUPPORTED refusal, ASan/UBSan")


def compare(sid,rate,chunk,data,expected,frames,request):
    output=struct.unpack('<%df'%(len(data)//4),data)
    padded=list(expected)+[0.0]*(2*frames-len(expected))
    error=max(abs(a-b) for a,b in zip(output,padded[:2*frames]))
    assert error<2e-7,(hex(sid),rate,chunk,error)
    assert any(output) and not any(output[-2*32:])
    return dict(id=sid,rate=rate,chunk=chunk,request=request,max_error=error,frames=frames)

if __name__=='__main__':main()
