#!/usr/bin/env python3
"""Check the native dry mixer independently from its exported source PCM.

This tests rational pitch/gain, callback partitioning and lifetime; it is
not a comparison with the SPU2's Gaussian interpolator or reverb output.
"""
import json
import struct
import subprocess
import wave
from export_area11_sfx import ROOT


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

if __name__=='__main__':main()
