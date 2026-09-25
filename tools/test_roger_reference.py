#!/usr/bin/env python3
"""Execute original AREA11 Roger controllers and class10 candidate gate."""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import random
import struct
import subprocess
from test_pickup_owner_reference import OwnerOracle,ROOT,DECOMP,ELF_SHA,ACTOR,DRAW
from test_interaction_pickup_reference import Math,Player
from test_point_light_reference import bits
from test_item_sdk_math_reference import Original as SdkOracle

class RogerOracle(OwnerOracle,SdkOracle):
    pass


class Roger(C.Structure):
    _fields_=[(name,C.c_uint8) for name in ('status','rendered','flags','lifecycle',
        'phase','armed','kind','freed')]+[('animation_result',C.c_uint16),('yaw',C.c_float)]
class Story(C.Structure):
    _fields_=[(name,C.c_uint8) for name in ('progress','suppressed','alternate','auxiliary')]
START=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint32)
WORK=C.CFUNCTYPE(C.c_int,C.c_void_p)
INIT=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint16,C.c_float,C.c_float)
TICK=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_float,C.POINTER(C.c_uint16))
EVENT=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_int,C.c_uint)
class Hooks(C.Structure):
    _fields_=[('context',C.c_void_p),('start',START),('script',WORK),('trigger',WORK),
             ('init',INIT),('tick',TICK),('publish',WORK),('event',EVENT)]


def controller(elf,overlay,native,lifecycle,phase,armed,progress,suppressed,
               alternate,auxiliary,triggered,done,visible):
    original=RogerOracle(elf)
    original.write(0x8237e0,overlay[0x2e0:0x7e0])
    original.save(ACTOR,1,1);original.save(ACTOR+1,9,1);original.save(ACTOR+2,0xAA,1)
    original.save(ACTOR+4,lifecycle,1);original.save(ACTOR+5,phase,1)
    original.save(ACTOR+11,armed,1);original.save(ACTOR+13,0x47,1)
    original.save(ACTOR+0x40,0x990000);original.save(0x28a5b8,0xAA0000)
    original.save(ACTOR+0x4c,DRAW);original.save(ACTOR+0xc4,bits(.7))
    original.save(ACTOR+0x1fe,0xCAFE,2)
    for address,value in zip((0x8107d8,0x810791,0x810793,0x810813),
                             (progress,suppressed,alternate,auxiliary)):
        original.save(address,value,1)
    expected=[]
    def start(o):
        assert o.r[4]==ACTOR+0x1f0
        expected.append(('start',o.r[5]))
    def script(o): expected.append(('script',));o.r[2]=done
    def trigger(o):
        assert (o.r[4],o.r[5],o.r[6],o.r[7])==(0,0x810350,0x82ab80,4)
        expected.append(('trigger',));o.r[2]=triggered
    def init(o):
        if o.load(ACTOR+0x40)==0xAA0000:expected.append((1,0x4A))
        expected.append(('init',o.r[5],o.f[12],o.f[13]))
    def advance(o):expected.append(('tick',o.f[12]));o.r[2]=0x1234
    def publication(o):
        expected.append(('publish',));o.save(ACTOR+1,visible,1);o.r[2]=visible
    def group(o):
        assert (o.r[4],o.r[5],o.r[6])==(1,0,4)
        expected.append((7,1))
    original.calls.update({0x1ba1a0:start,0x1ba1f0:script,0x1b1ea0:trigger,
        0x1c67e0:init,0x1c64f0:advance,0x1b17a0:publication,
        0x1ba580:lambda o:expected.append((4,o.r[5])),
        0x1c68c0:lambda o:expected.append((5,0)),
        0x1fabb0:lambda o:expected.append((0,0)),
        0x1fae70:lambda o:expected.append((2,o.r[4])),
        0x1aee10:lambda o:expected.append((3,o.r[4])),
        0x1b0c60:group,0x1ba540:lambda o:expected.append((8,0)),
        0x1afc10:lambda o:expected.append((9,0)),
        DRAW:lambda o:expected.append((6,o.load(ACTOR+1,1)))})
    original.run(0x8237e0,(ACTOR,))
    actual=[]
    def tick(_,rate,result):actual.append(('tick',bits(rate)));result[0]=0x1234;return 1
    hooks=Hooks(None,START(lambda _,entry:actual.append(('start',entry)) or 1),
        WORK(lambda _:actual.append(('script',)) or done),
        WORK(lambda _:actual.append(('trigger',)) or triggered),
        INIT(lambda _,clip,blend,start:actual.append(('init',clip,bits(blend),bits(start))) or 1),
        TICK(tick),WORK(lambda _:actual.append(('publish',)) or visible),
        EVENT(lambda _,event,arg:actual.append((event,arg)) or 1))
    roger=Roger(1,9,0xAA,lifecycle,phase,armed,0x47,0,0xCAFE,.7)
    story=Story(progress,suppressed,alternate,auxiliary)
    result=native.em_roger_tick(C.byref(roger),C.byref(story),C.byref(hooks))
    expected_state=[original.load(ACTOR+i,1) for i in (0,1,2,4,5,11,13)]+[
        int((9,0) in expected),original.load(ACTOR+0x1fe,2),original.load(ACTOR+0xc4)]
    actual_state=[roger.status,roger.rendered,roger.flags,roger.lifecycle,roger.phase,roger.armed,
                  roger.kind,roger.freed,roger.animation_result,bits(roger.yaw)]
    assert (actual,actual_state,result)==(expected,expected_state,1-roger.freed), dict(
        initial=(lifecycle,phase,armed,progress,suppressed,alternate,auxiliary,triggered,done,visible),
        actual=actual,expected=expected,actual_state=actual_state,expected_state=expected_state)
    assert bytes(story)==bytes(original.load(a,1) for a in (0x8107d8,0x810791,0x810793,0x810813))


def candidate(elf,native,math,position,yaw,action):
    original=RogerOracle(elf);player=0x930000;desc=0x940000
    original.save(ACTOR+2,0xAA,1);original.save(ACTOR+8,0,1)
    original.save(ACTOR+0x30,desc);original.write(desc,struct.pack('<2f',10,20))
    original.write(ACTOR+0xb0,struct.pack('<3f',0,0,0))
    original.write(player+0xa0,struct.pack('<3f',*position));original.save(player+0xc4,bits(yaw))
    original.save(player+0x1f0,action,1);original.save(0x70003b98,bits(-91.25))
    original.run(0x183ef0,(player,ACTOR))
    state=Player((C.c_float*3)(*position),yaw,action,(C.c_float*3)())
    score=C.c_float(-91.25)
    result=native.em_roger_candidate((C.c_float*2)(10,20),(C.c_float*3)(),
        C.byref(state),C.byref(math),C.byref(score))
    assert (result,bits(score.value))==(original.r[2],original.load(0x70003b98))


def main():
    elf=(DECOMP/'config/SCUS_971.12').read_bytes()
    overlay=(DECOMP/'extract/OVERLAY/AREA11.BIN').read_bytes()
    assert hashlib.sha256(elf).hexdigest()==ELF_SHA
    out=ROOT/'build/roger_reference';out.mkdir(parents=True,exist_ok=True)
    library=out/'roger.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',
        '-ffp-contract=off','-Isrc','src/game/em_roger.c','src/game/em_interaction_scan.c',
        '-o',str(library)],cwd=ROOT,check=True)
    native=C.CDLL(str(library));native.em_roger_tick.argtypes=[C.POINTER(Roger),C.POINTER(Story),C.POINTER(Hooks)]
    native.em_roger_candidate.argtypes=[C.POINTER(C.c_float),C.POINTER(C.c_float),
        C.POINTER(Player),C.POINTER(Math),C.POINTER(C.c_float)]
    math=Math.from_buffer_copy(elf[0x26c5d8-0x100000+0x300:0x26c5d8-0x100000+0x300+76])
    cases=0
    for lifecycle,phase,armed,progress,suppressed,alternate,auxiliary,triggered,done,visible in itertools.product(
        (1,2,3,255),(0,1,2),(0,4),(0,1,0x80),(0,1,2),(0,1),(0,0x10),(0,1),(0,1),(0,1)):
        controller(elf,overlay,native,lifecycle,phase,armed,progress,suppressed,alternate,
                   auxiliary,triggered,done,visible);cases+=1
    predicates=0
    for dx,dy,dz,yaw,action in itertools.product((0.,9.99999,10.,10.00001),
        (-20.00001,-20.,0.,20.,20.00001),(.001,1.),(-3.14,-.7853982,0.,.7853982,3.14),(0,0x2D)):
        candidate(elf,native,math,(dx,dy,dz),yaw,action);predicates+=1
    rng=random.Random(0x8237e0)
    for _ in range(500):
        candidate(elf,native,math,[rng.uniform(-15,15) for _ in range(3)],rng.uniform(-4,4),0);predicates+=1
    report=dict(elf_sha256=ELF_SHA,overlay_sha256=hashlib.sha256(overlay).hexdigest(),
        controller_cases=cases,predicate_cases=predicates,
        boundaries=['model initialization','script execution',
                    'controller uses injected polygon result; 001B1EA0 over 0x82AB80 is test_director_original_reference part 2','pose/face workers',
                    'publication visibility','draw','audio','group removal'])
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Original Roger: {cases} controller, {predicates} predicate cases PASS')


if __name__=='__main__':main()
