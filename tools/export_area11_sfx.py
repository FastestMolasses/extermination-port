#!/usr/bin/env python3
"""Export original AREA11 panel cue source and exact sound-driver parameters.

The WAV is decoded source data at the SPU's base clock. It must be played
with the exported pitch ratio and voice gains, not registered as a48kHz cue.
Hardware ADSR/interpolation/reverb are not flattened into an invented WAV.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys
import wave

ROOT=Path(__file__).resolve().parents[1]
DECOMP=ROOT.parent/'Extermination'
ELF_SHA='ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'

def u16(data,offset):return struct.unpack_from('<H',data,offset)[0]
def u32(data,offset):return struct.unpack_from('<I',data,offset)[0]

class Elf:
    def __init__(self):
        self.data=(DECOMP/'config/SCUS_971.12').read_bytes()
        assert hashlib.sha256(self.data).hexdigest()==ELF_SHA
    def read(self,address,size):
        assert 0x100000<=address and address+size<=0x275B00
        offset=address-0x100000+0x300
        return self.data[offset:offset+size]
    def u16(self,address):return u16(self.read(address,2),0)
    def u32(self,address):return u32(self.read(address,4),0)

def resolve(elf,cue,area=11,sub=0):
    """Original FB9F0 area-tabled range; absent entries return None."""
    cue&=0x7FFF
    if not 0x3E8<=cue<0x5DC:raise ValueError('outside the audited panel range')
    remaps=elf.u32(0x264A70+4*area)
    if not remaps:return None
    remaps=elf.u32(remaps+4*sub)
    if not remaps:return None
    remap=elf.read(remaps+cue-0x3E8,1)[0]
    if remap==255:return None
    records=elf.u32(0x264B30+4*area)
    if not records:return None
    records=elf.u32(records+4*sub)
    if not records:return None
    address=records+4*remap
    return dict(record_address=address,record=list(struct.unpack('<4b',elf.read(address,4))),
                remap_address=remaps+cue-0x3E8,remap=remap)

def pitch(elf,center,note,fine,bend,bend_range):
    """Exact integer117918 lookup followed by115850's44100/48000 scale."""
    base=((bend-64)*bend_range>>2)+0xD0
    if center<=note:
        difference=note-center
        value=elf.u16(0x241D70+2*((difference%12)*16+fine+base))<<(difference//12)
    else:
        difference=center-note
        value=elf.u16(0x241D70+2*((12-difference%12)*16+fine+base))>>(difference//12+1)
    return value,(value*44100)//48000

def stereo_gain(elf,program,tone,master,channel,velocity_table,velocity,left=4096,right=4096):
    """Audited default AREA11 A0 state, 117BA0 plus179E0 integer arithmetic."""
    #115850 explicitly calls117BA0(4,0): A0 uses the tone pan directly.
    # The nested program/channel pan table belongs to the other event path.
    pan=tone[12]
    packed=elf.u16(0x242630+2*(pan>>2))
    scalar=(channel[14]*channel[3]*tone[11]*velocity_table[velocity+2]*program[1]*master)>>27
    gains=[((scalar*(packed>>8)*left)>>19),((scalar*(packed&255)*right)>>19)]
    gains=[((value&65535)>>1) for value in gains]
    if tone[10]:gains=[((tone[10]<<8)|(value>>7))&65535 for value in gains]
    return gains

def source():
    elf=Elf();assert resolve(elf,0x3EE) is None
    mapping=resolve(elf,0x3EF);assert mapping['record']==[2,0,1,0]
    data=(DECOMP/'extract/chunk15/f00_id43.bin').read_bytes()
    total,header,_,count=struct.unpack_from('<4I',data)
    image,size=struct.unpack_from('<2I',data,0x10)
    body_size,bank_header,group,_=struct.unpack_from('<4I',data,0x20)
    assert count==1 and group==2 and header==bank_header==0x30
    assert image+size==total and body_size==size and total<=len(data)
    assert data[header+12:header+16]==b'SShd'
    table=header+u32(data,header+0x1C)
    script_group,script_index=mapping['record'][2:]
    group_table=table+u16(data,table+2+script_group*2)
    script=table+u16(data,group_table+2+script_index*2)
    assert data[script:script+9]==bytes.fromhex('A02164018000FF2F00')
    # 80 00 is a variable-length delta ofzero consumed by118E60, not a
    # synthesized note-off or an extra timed sample event.
    note,velocity,program_index=data[script+1:script+4]
    programs=header+u32(data,header+0x24)
    program_at=programs+u16(data,programs+2+2*program_index)
    program=data[program_at:program_at+8]
    assert program[0]==255 and program[6]<=note<=program[7]
    tone_index=note-program[6];tone_at=program_at+8+16*tone_index
    tone=data[tone_at:tone_at+16]
    center,fine=tone[2],struct.unpack('<b',tone[3:4])[0]
    #115850 sets per-sequencer bend64 before117918 for A0 events.
    bend=64;range_=program[4] if tone[15]&16 else tone[13]
    ladder,spu_pitch=pitch(elf,center,note,fine,bend,range_)
    state=header+u32(data,header+0x20)
    master=data[state];channel=data[state+16:state+32]
    velocity_at=header+u32(data,header+0x14)
    gain=stereo_gain(elf,program,tone,master,channel,data[velocity_at:velocity_at+130],velocity)
    sample_offset=u16(tone,4)<<3;sample_at=image+sample_offset
    end=sample_at
    while end+16<=total:
        assert data[end]>>4<=4 and data[end]&15<=12
        flags=data[end+1];end+=16
        if flags&1:break
    else:raise ValueError('sample lacks original end flag')
    adpcm=data[sample_at:end]
    assert len(adpcm)==1312 and adpcm[-15]==1
    report=dict(area=11,sub=0,absent_cues=['03EE'],cue='03EF',**mapping,
        source='chunk15/f00_id43.bin',container_bytes=total,header_offset=header,
        container_sha256=hashlib.sha256(data[:total]).hexdigest(),
        script_offset=script,program_offset=program_at,tone_offset=tone_at,
        note=note,velocity=velocity,program=program_index,tone=tone_index,
        center=center,fine=fine,bend=bend,bend_range=range_,
        pitch_ladder_value=ladder,spu_pitch=spu_pitch,
        base_clock_hz=48000,effective_rate_numerator=48000*spu_pitch,effective_rate_denominator=4096,
        voice_gain=gain,adsr1=u16(tone,6),adsr2=u16(tone,8),tone_flags=tone[15],
        reverb=bool(tone[15]&0x80),sample_offset=sample_offset,source_sample_at=sample_at,
        adpcm_bytes=len(adpcm),source_samples=len(adpcm)//16*28,
        loop_end=bool(adpcm[-15]&2),loop_start_frames=[i//16 for i in range(0,len(adpcm),16) if adpcm[i+1]&4],
        adpcm_sha256=hashlib.sha256(adpcm).hexdigest(),
        boundaries=['WAV is decoded source, not a flattened hardware output',
                    'Exact pitch/gain metadata requires native playback support',
                    'SPU ADSR, Gaussian interpolation and reverb output not reproduced by current WAV mixer',
                    'Legacy soundmap rate15480 uses incorrect ladder anchor and bend assumption'])
    return adpcm,report

def main():
    adpcm,report=source()
    sys.path.insert(0,str(DECOMP/'tools'))
    from audio_export import decode_adpcm
    pcm=decode_adpcm(adpcm)
    out=ROOT/'assets/sfx/area11';out.mkdir(parents=True,exist_ok=True)
    raw=out/'cue_03ef.adpcm';raw.write_bytes(adpcm)
    wav=out/'cue_03ef_source.wav'
    with wave.open(str(wav),'wb') as w:
        w.setnchannels(1);w.setsampwidth(2);w.setframerate(48000);w.writeframes(pcm)
    # EMSF v1 is a scoped bank, not a global id-to-WAV override. The silent
    # entry preserves the original FF remap; the audible entry retains its
    # integer pitch, Q14 voice gains and authored ADSR registers.
    assert len(pcm)==report['source_samples']*2 and pcm[:82]==bytes(82)
    bank=out/'panel_sfx.emsf'
    header=struct.pack('<4sIIII',b'EMSF',1,11,0,2)
    absent=struct.pack('<IIHhhHHHI',0x3EE,1,0,0,0,0,0,0,0)
    cue=struct.pack('<IIHhhHHHI',0x3EF,2,report['spu_pitch'],
        *report['voice_gain'],report['adsr1'],report['adsr2'],0,report['source_samples'])
    bank.write_bytes(header+absent+cue+pcm)
    report['voice_gain_denominator']=16384
    report['steady_envelope_numerator']=32767
    report['steady_envelope_denominator']=32768
    report['silent_prefix_source_samples']=41
    report['assets']={str(p.relative_to(ROOT)):dict(bytes=p.stat().st_size,
        sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in (raw,wav,bank)}
    (out/'panel_sfx.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Exported original AREA11 panel3EF source+pitch862/gain2217/ADSR metadata;3EE is originally absent.')

if __name__=='__main__':main()
