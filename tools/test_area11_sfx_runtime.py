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
from export_sfx_registry import volume_words


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


def expected_mix(entry,samples,rate,frames,request):
    out=[0.0]*(2*frames)
    for event in entry['events']:
        pcm=struct.unpack('<%dh'%(len(samples[event['sample']])//2),samples[event['sample']])
        start=event['tick']*rate*1001//60000
        numerator,denominator=48000*event['pitch'],4096*rate
        length=(len(pcm)*denominator+numerator-1)//numerator
        words=volume_words(event['scalar'],event['pan'],*request)
        gains=[q14(w) for w in words]
        for n in range(min(length,frames-start)):
            index,remainder=divmod(n*numerator,denominator)
            nxt=min(index+1,len(pcm)-1)
            value=(pcm[index]+(pcm[nxt]-pcm[index])*remainder/denominator)/32768
            out[2*(start+n)]+=value*gains[0];out[2*(start+n)+1]+=value*gains[1]
    return out


def registry_runtime():
    """EMSR: loader refusals, per-area scopes, rational multi-event mix."""
    installed=ROOT/'assets/sfx/sfx_registry.emsr'
    assert installed.exists(),'run tools/export_sfx_registry.py first'
    raw=installed.read_bytes()
    report=json.loads((ROOT/'assets/sfx/sfx_registry.json').read_text())
    entries,samples=parse_emsr(raw)
    out=ROOT/'build/area11_sfx_reference/runtime_registry'
    if out.exists():shutil.rmtree(out)
    (out/'assets/sfx/area11').mkdir(parents=True)
    (out/'assets/sfx/sfx_registry.emsr').write_bytes(raw)
    shutil.copy(ROOT/'assets/sfx/area11/panel_sfx.emsf',out/'assets/sfx/area11/panel_sfx.emsf')
    first=entries[0];assert first['state']==1
    event=20+16                                    # first entry's first event
    invalid=[raw[:19],raw[:-1],raw+b'\0']
    for offset,value in ((0,b'X'),(4,b'\2'),(16,b'\1'),(20+8,b'\x09'),(20+12,b'\1'),
                         (20+9,b'\0'),(20+10,b'\1'),(20+4,struct.pack('<h',24)),
                         (event+4,b'\0\0'),(event+4,struct.pack('<H',0x4000)),
                         (event+2,struct.pack('<H',len(samples))),(event+8,struct.pack('<I',0x7FFFFFFF)),
                         (event+16,b'\x20'),(event+16,b'\x02'),(event+17,b'\1'),(8,b'\0\0\0\0')):
        data=bytearray(raw);data[offset:offset+len(value)]=value;invalid.append(bytes(data))
    duplicate=bytearray(raw)                        # entry 1 := entry 0 scope/id
    second=20+16+20*len(first['events'])
    duplicate[second:second+8]=raw[20:28];invalid.append(bytes(duplicate))
    for i,data in enumerate(invalid):(out/f'bad_{i}.emsr').write_bytes(data)
    (out/'scopes.txt').write_text(''.join(f"{e['id']:X} {e['scope'][0]} {e['scope'][1]} {e['state']}\n"
                                          for e in entries))
    audible=[e for e in entries if e['state']==1 and e['scope']==[-1,-1]]
    chosen=max(audible,key=lambda e:(len({v['tick'] for v in e['events']}),len(e['events']),-e['id']))
    frames=0
    for rate in (48000,44100,96000):
        for v in chosen['events']:
            length=(len(samples[v['sample']])//2*4096*rate+48000*v['pitch']-1)//(48000*v['pitch'])
            frames=max(frames,v['tick']*rate*1001//60000+length+64)
    executable=out/'test'
    subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-ffp-contract=off','-Isrc',
        'tests/sfx_registry_test.c','src/game/em_sfx_bank.c','src/game/em_sfx.c',
        '-o',str(executable)],cwd=ROOT,check=True)
    subprocess.run([str(executable),str(len(invalid)),hex(chosen['id']),str(frames)],cwd=out,check=True)
    reports=[]
    for rate,chunk,suffix,request in [(r,c,'',(0x1000,0x1000)) for r in (48000,44100,96000) for c in (1,997)]+\
            [(48000,997,'_at',tuple(map(int,(out/'reg_requests.txt').read_text().split())))]:
        data=(out/f"reg_{chosen['id']:X}_{rate}_{chunk}{suffix}.f32").read_bytes()
        output=struct.unpack('<%df'%(len(data)//4),data)
        expected=expected_mix(chosen,samples,rate,frames,request)
        error=max(abs(a-b) for a,b in zip(output,expected))
        assert error<2e-7,(rate,chunk,suffix,error)
        assert any(output) and not any(output[-2*32:])
        reports.append(dict(rate=rate,chunk=chunk,request=request,max_error=error))
    (out/'report.json').write_text(json.dumps(dict(id=chosen['id'],events=chosen['events'],
        malformed_cases=len(invalid),scopes=len(entries),renders=reports,
        registry_sha256=report['registry_sha256']),indent=2)+'\n')
    print(f"PASS EMSR registry: {len(invalid)} malformed files refused, {len(entries)} scoped entries, "
          f"id 0x{chosen['id']:X} ({len(chosen['events'])} A0 voices) = independent tick/pitch/Q14 mix at 44.1/48/96 kHz "
          f"and a positional request, ASan/UBSan")

if __name__=='__main__':main()
