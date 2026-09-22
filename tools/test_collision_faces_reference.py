#!/usr/bin/env python3
"""Compare compact cell faces with original A4D10/A50A0 instructions.

Original bytes come from the user's local ELF. No game data is embedded.
The finite scalar oracle reuses the motor diagnostic's instruction arithmetic.
"""
import ctypes as C
import random
import math
import json
import struct
import subprocess
import tempfile
from pathlib import Path
from test_player_reentry_reference import Original, ACTOR, RETURN, bits, number, signed

ROOT=Path(__file__).resolve().parents[1]
class Face(C.Structure):
    _fields_=[('face',C.c_uint32),('origin',C.c_float*3),('extent',C.c_float*3)]
class Hit(C.Structure):
    _fields_=[('point',C.c_float*3),('normal',C.c_float*3),('delta',C.c_float*3),
              ('kind',C.c_int),('poly',C.c_int),('surf_class',C.c_uint16),('attr',C.c_uint8)]
class Cell(C.Structure):
    _fields_=[('uid',C.c_uint32),('attr',C.c_uint32),('face_count',C.c_uint32),
              ('bbox',C.c_float*6),('faces',C.POINTER(Face))]
class World(C.Structure):
    _fields_=[('vert_count',C.c_uint32),('poly_count',C.c_uint32),('index_count',C.c_uint32),
              ('flags',C.c_uint32),('bbox',C.c_float*6),('verts',C.c_void_p),('polys',C.c_void_p),
              ('indices',C.c_void_p),('edge_n',C.c_void_p),('blob',C.c_void_p),
              ('actor_cells',C.POINTER(Cell)*32),('actor_cell_count',C.c_uint)]
class FaceOriginal(Original):
    def plain(self,w):
        op,rs,rt=w>>26,w>>21&31,w>>16&31
        imm=w&65535;imm=imm if imm<32768 else imm-65536
        if op==41:self.put((self.r[rs]+imm)&0xffffffff,self.r[rt],2)
        elif op==10:self.r[rt]=int(signed(self.r[rs])<imm)
        elif op==11:self.r[rt]=int((self.r[rs]&0xffffffff)<(imm&0xffffffff))
        else:super().plain(w)
        self.r[0]=0
    def run(self,pc):
        for _ in range(500):
            if pc==RETURN:return
            assert 0x1a4d10<=pc<0x1a56a0,hex(pc)
            w=self.get(pc);op,rs,rt=w>>26,w>>21&31,w>>16&31
            imm=w&65535;imm=imm if imm<32768 else imm-65536
            branch=None;delay=True
            if op in (4,5):branch=pc+4+4*imm if (self.r[rs]==self.r[rt])==(op==4) else pc+8
            elif op==17 and rs==8:
                taken=self.condition==bool(rt&1)
                branch=pc+4+4*imm if taken else pc+8;delay=taken or not(rt&2)
            elif op==0 and w&63==8:branch=self.r[rs]
            if branch is not None:
                if delay:self.plain(self.get(pc+4))
                pc=branch
            else:self.plain(w);pc+=4
        raise AssertionError('compact face did not return')

def original(elf,face,start,end,movement):
    o=FaceOriginal(elf,0,0,0);o.r[4]=ACTOR;o.put(ACTOR+2,face.face,1)
    for k in range(3):
        o.put(ACTOR+4+4*k,bits(face.origin[k]));o.put(ACTOR+16+4*k,bits(face.extent[k]))
        o.put(0x70003190+4*k,bits(start[k]));o.put(0x700031a0+4*k,bits(end[k]))
    o.run(0x1a4d10 if movement else 0x1a50a0)
    values=[o.get(0x700031b0+4*k) for k in range(3)]+[o.get(0x700030d4+4*k) for k in range(3)]
    return o.r[2],values,o.get(0x700030ca,2)

def check_capture(native,elf):
    trace=ROOT.parent/'Extermination/build/startup-reference/collision_run_poll.json'
    asset=ROOT/'assets/scene_snow/props/panel_cell18.emcb'
    if not trace.exists() or not asset.exists():return
    cell=Cell();world=World()
    assert native.em_collision_cell_load(C.byref(cell),str(asset).encode())==0
    assert native.em_collision_load(C.byref(world),str(ROOT/'assets/scene_snow/snow.emcl').encode())==0
    assert native.em_collision_cell_bind(C.byref(world),C.byref(cell))
    assert cell.uid==18 and cell.attr==0x46 and cell.face_count==5
    # Every exported origin/extent comes from the actual cell records.
    source=(ROOT.parent/'Extermination/extract/chunk15/f12_id44.bin').read_bytes()
    base=0x39800;at=base+(struct.unpack_from('<I',source,base+4+18*4)[0]&0x3fffffff)
    for index in range(cell.face_count):
        record=at+28+index*28;face=cell.faces[index]
        assert face.face==source[record+2]
        assert bytes(face.origin)+bytes(face.extent)==source[record+4:record+28]
    rows=[r for r in json.loads(trace.read_text())['rows'] if r['frame']==4140]
    before=next(r for r in rows if r['speed']>.3 and r['position']!=r['base'] and not r['blocked'])
    after=rows[-1]
    p=before['position'];angle=number(bits(before['yaw']+struct.unpack_from('<f',elf,0x248954-0x100000+0x300)[0]))
    expected=after['base'];hit=Hit()
    for lift in (4.01,18):
        target=[number(bits(p[0]+number(bits(math.sin(angle)*4.5)))),
                number(bits(p[1]+lift)),number(bits(p[2]+number(bits(math.cos(angle)*4.5))))]
        result=native.em_collision_move_probe(C.byref(world),(C.c_float*3)(*p),(C.c_float*3)(*target),6,C.byref(hit))
        assert result==(2 if lift==18 else 0)
        if result:
            assert hit.poly==-19 and hit.attr==0x46
            corrected=[number(bits(p[k]+number(bits(hit.point[k]-target[k])))) for k in range(3)]
            assert max(abs(corrected[k]-expected[k]) for k in (0,2))<.0001,(corrected,expected)
    native.em_collision_cell_unbind(C.byref(world),18)
    assert not native.em_collision_move_probe(C.byref(world),(C.c_float*3)(*p),(C.c_float*3)(*target),6,C.byref(hit))
    native.em_collision_free(C.byref(world));native.em_collision_cell_free(C.byref(cell))
    print('captured frame4140: original panel cell18 upper-lane push reproduced within0.0001')

def main():
    elf=(ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes()
    rng=random.Random(0x1a4d10);checks=hits=0
    with tempfile.TemporaryDirectory(prefix='em_compact_face_') as folder:
        lib=Path(folder)/'collision.dylib'
        subprocess.run(['cc','-shared','-fPIC','-std=c11','-O2','-ffp-contract=off',
            '-I'+str(ROOT/'src'),str(ROOT/'src/game/em_collision.c'),'-lm','-o',str(lib)],check=True)
        native=C.CDLL(str(lib));native.em_collision_box_face.argtypes=[C.POINTER(Face),C.POINTER(C.c_float),C.POINTER(C.c_float),C.c_int,C.POINTER(Hit)]
        for movement in (0,1):
            for face_id in range(1,7):
                axis=(face_id-1)//2
                for sample in range(400):
                    base=[number(bits(rng.uniform(-500,500))) for _ in range(3)]
                    extent=[number(bits(rng.uniform(1,40)*rng.choice([-1,1]))) for _ in range(3)];extent[axis]=0
                    start=[];end=[]
                    for k in range(3):
                        # Include face edges, reversed approach, on-plane endpoints,
                        # both extent signs and nonhorizontal camera segments.
                        start.append(number(bits(base[k]+extent[k]*rng.choice([0,.25,.5,.75,1,1.2]))))
                        end.append(number(bits(start[k]+rng.uniform(-1,1))))
                    direction=1 if face_id&1 else -1
                    start[axis]=number(bits(base[axis]+direction*rng.choice([0,1,2,7,-2])))
                    end[axis]=number(bits(base[axis]-direction*rng.choice([0,1,2,7,-2])))
                    if movement:end[1]=start[1]
                    face=Face(face_id,(C.c_float*3)(*base),(C.c_float*3)(*extent));hit=Hit()
                    expected,values,cls=original(elf,face,start,end,movement)
                    actual=native.em_collision_box_face(C.byref(face),(C.c_float*3)(*start),(C.c_float*3)(*end),movement,C.byref(hit))
                    assert actual==expected,(movement,face_id,sample,base,extent,start,end,actual,expected)
                    if actual:
                        actual_values=[bits(x) for x in hit.point]+[bits(x) for x in hit.normal]
                        assert actual_values==values and hit.surf_class==cls,(movement,face_id,sample,base,extent,start,end,actual_values,values)
                        hits+=1
                    checks+=1
        check_capture(native,elf)
    print('compact face original-instruction PASS',checks,'cases,',hits,'exact hit records')
if __name__=='__main__':main()
