#!/usr/bin/env python3
"""Actual Dennis face resources plus original allocation/talk/update words."""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import struct
import subprocess
from test_face_allocation_reference import Captured,PLAYER,state
from test_opening_face_reference import Face
from test_interaction_scan_reference import ELF_SHA
from test_point_light_reference import signed,bits,number

ROOT=Path(__file__).resolve().parents[1]


class OriginalFace(Captured):
    def plain(self,word):
        op,rs,rt,rd,fn=word>>26,word>>21&31,word>>16&31,word>>11&31,word&63
        if op==17 and rs==16 and fn in (0,1,2,3):
            # Match the existing face branch oracle's host IEEE arithmetic.
            # This deliberately does NOT claim physical EE float equality.
            a,b=number(self.f[rd]),number(self.f[rt])
            self.f[word>>6&31]=bits((a+b,a-b,a*b)[fn] if fn<3 else a/b)
        elif op==0 and fn==26:
            left=signed(self.r[word>>21&31]);right=signed(self.r[word>>16&31])
            assert right
            quotient=abs(left)//abs(right)*(-1 if (left<0)!=(right<0) else 1)
            self.lo=quotient&0xFFFFFFFFFFFFFFFF
            self.hi=(left-quotient*right)&0xFFFFFFFFFFFFFFFF
        elif op==0 and fn in (16,18):
            self.r[rd]=self.hi if fn==16 else self.lo
        elif op==0 and fn==39:self.r[rd]=~(self.r[rs]|self.r[rt])&0xFFFFFFFFFFFFFFFF
        else:super().plain(word)
        self.r[0]=0


def call_order(elf,ram):
    stages=0
    for guard,mode,changed in itertools.product((0,1,2,3),(0,1,2,3,4,255),(0,1)):
        o=OriginalFace(elf,ram);o.save(0x70003B8F,guard,1)
        o.save(PLAYER+0x2F3,mode,1);o.save(PLAYER+0x1F2,1,2)
        o.save(PLAYER+0x20C,1-changed,2);o.save(0x8106D4,0xA5,1)
        events=[]
        o.calls.update({0x1D0C70:lambda r:events.append('face'),
                        0x1C63E0:lambda r:events.append('foreign body'),
                        0x1C67E0:lambda r:events.append('ordinary body')})
        o.run(0x183090,(PLAYER,))
        expected=['face'] if guard==2 else []
        if mode in (1,3):expected+=['foreign body']
        elif not mode and changed:expected+=['ordinary body']
        assert events==expected and o.load(0x8106D4,1)==0xA5
        stages+=1
    dialogue=0
    for guard,timer,speaker,mask in itertools.product((1,2),(0,1,3),(0,1,255),(0,1,2,3)):
        o=OriginalFace(elf,ram);message=0x920000
        o.save(0x70003B8F,guard,1);o.save(message+0x34,0)
        o.save(message+0x6C,timer);o.save(message+0x51,speaker,1);o.save(message+0x64,mask)
        events=[]
        def zero(r):r.r[2]=0
        o.calls.update({0x1FE480:zero,0x1FE530:zero,0x1CC170:zero,
            0x1FE070:lambda r:None,
            0x1D06E0:lambda r:events.append((r.r[4],r.r[5]))})
        o.run(0x1FD950,(message,))
        expected=[]
        if guard==2:
            if timer and speaker==0:expected=[(PLAYER,1)]
            elif not timer and mask&1:expected=[(PLAYER,0)]
        assert events==expected,(guard,timer,speaker,mask,events)
        dialogue+=1
    return stages,dialogue


def main():
    out=ROOT/'build/player_face_host';out.mkdir(parents=True,exist_ok=True)
    directory=ROOT/'assets/scene_snow/opening'
    mesh=(directory/'player_face.emdl').read_bytes()
    morph=(directory/'player_face.emfm').read_bytes()
    bones,vertices,indices,frames,_,textures,flags,clips=struct.unpack_from('<8I',mesh,4)
    assert mesh[:4]==b'EMD3' and bones==22 and frames==1 and not flags
    vert_at=36+bones*4+textures*16+clips*16
    index_at=vert_at+vertices*40
    for i in range(7):
        bad=out/f'bad_{i}'/'opening';bad.mkdir(parents=True,exist_ok=True)
        a,b=bytearray(mesh),bytearray(morph)
        if i==0:b[:4]=b'BAD!'
        elif i==1:b=b[:-1]
        elif i==2:struct.pack_into('<I',b,20,0x7FC00000)
        elif i==3:struct.pack_into('<I',a,index_at,0xFFFFFFFF)
        elif i==4:struct.pack_into('<I',a,vert_at+32,8)
        elif i==5:a=a[:-300]
        elif i==6:struct.pack_into('<I',b,12,8)
        (bad/'player_face.emdl').write_bytes(a)
        (bad/'player_face.emfm').write_bytes(b)
    executable=out/'test'
    subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-ffp-contract=off','-Isrc',
        'tests/player_face_host_test.c','src/game/em_player_face_host.c',
        'src/game/em_face_model.c','src/game/em_opening_face.c','src/em_model.c',
        '-o',str(executable)],cwd=ROOT,check=True)
    subprocess.run([str(executable),str(out)],cwd=ROOT,check=True)
    decomp=ROOT.parent/'Extermination'
    elf=(decomp/'config/SCUS_971.12').read_bytes();assert hashlib.sha256(elf).hexdigest()==ELF_SHA
    ram=(decomp/'build/startup-reference/playable_ee.bin').read_bytes()
    stages,dialogue=call_order(elf,ram)
    o=OriginalFace(elf,ram);o.save(0x70003B8F,1,1)
    o.run(0x1B81D0,(PLAYER,))
    draws=0
    def random_word(original):
        nonlocal draws
        draws+=1;original.r[2]=(draws*0x94720123)&0x7FFFFFFF
    o.calls[0x122BB8]=random_word
    actual=(out/'states.bin').read_bytes();stride=12+C.sizeof(Face)
    assert len(actual)==401*stride
    attached=True
    for event in range(401):
        if event:
            frame=event-1
            if frame in (5,80,150,300):o.run(0x1D06E0,(PLAYER,1))
            if frame in (50,125,200,350):o.run(0x1D06E0,(PLAYER,0))
            if frame==128:o.run(0x1B81D0,(PLAYER,))
            if frame==250:o.run(0x1CA770,(PLAYER,));attached=False
            if frame==280:o.run(0x1B81D0,(PLAYER,));attached=True
            if attached:o.run(0x1D0C70,(PLAYER,))
        frame,active,count=struct.unpack_from('<3I',actual,event*stride)
        assert (frame,active,count)==((event-1)&0xFFFFFFFF,int(attached),draws)
        face=o.load(PLAYER+0x90)
        expected=state(o,face) if face else bytes(C.sizeof(Face))
        assert actual[event*stride+12:(event+1)*stride]==expected,(event,frame,draws,
            [(i,hex(struct.unpack_from('<I',actual,event*stride+12+i)[0]),hex(struct.unpack_from('<I',expected,i)[0])) for i in range(0,len(expected),4) if actual[event*stride+12+i:event*stride+16+i]!=expected[i:i+4]])
    report=dict(callbacks=400,state_bytes=C.sizeof(Face),rng_calls=draws,
        original_body_order_cases=stages,original_dialogue_cases=dialogue,
        malformed_assets=7,elf_sha256=ELF_SHA,scope='original instruction flow and state under shared host IEEE float32 and controlled 31-bit RNG; physical EE/VU rounding and live frame order separate')
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS original B81D0/D06E0/D0C70/CA770 state and RNG order:',json.dumps(report))

if __name__=='__main__':main()
