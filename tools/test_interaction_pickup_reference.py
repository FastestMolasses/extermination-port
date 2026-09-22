#!/usr/bin/env python3
"""Raw original183EF0 pickup and B1630 publication oracle; no GPU needed."""
import ctypes as C
import hashlib
import itertools
import json
import math
import random
import struct
import subprocess

from test_interaction_scan_reference import ROOT,DECOMP,ELF_SHA,ScanOracle,PLAYER,ACTORS,DESC
from test_point_light_reference import bits,number


class Math(C.Structure):
    _fields_=[('low',C.c_float*4),('high',C.c_float*4),('coefficients',C.c_float*11)]
class Pickup(C.Structure):
    _fields_=[('identity',C.c_size_t),('flags',C.c_uint8),('subtype',C.c_uint8),
        ('selector',C.c_uint8),('callback',C.c_uint32),('position',C.c_float*3),
        ('angles',C.c_float*3),('descriptor',C.c_float*2)]
class Player(C.Structure):
    _fields_=[('position',C.c_float*3),('yaw',C.c_float),('action',C.c_uint8),
              ('target',C.c_float*3)]
class Hit(C.Structure):
    _fields_=[('hit',C.c_int),('flags',C.c_uint16),('kind',C.c_uint32),('owner',C.c_size_t)]
RAY=C.CFUNCTYPE(C.c_int,C.c_void_p,C.POINTER(C.c_float),C.POINTER(C.c_float),
               C.c_uint,C.POINTER(Hit))


def pickup_case(elf,native,coefficients,pickup,player,hit):
    o=ScanOracle(elf)
    o.save(ACTORS+2,pickup.flags,1);o.save(ACTORS+3,pickup.subtype,1)
    o.save(ACTORS+8,pickup.selector,1);o.save(ACTORS+16,pickup.callback)
    o.save(ACTORS+0x30,DESC);o.write(DESC,bytes(pickup.descriptor))
    o.write(ACTORS+0xb0,bytes(pickup.position)+struct.pack('<f',1))
    o.write(ACTORS+0xc0,bytes(pickup.angles))
    o.write(PLAYER+0xa0,bytes(player.position)+struct.pack('<f',1))
    o.save(PLAYER+0xc4,bits(player.yaw));o.save(PLAYER+0x1f0,player.action,1)
    o.write(0x8105e0,bytes(player.target)+struct.pack('<f',1))
    o.save(0x70003b98,bits(91.25));expected_queries=[]
    def query(r):
        expected_queries.append((r.read(r.r[4],16),r.read(r.r[5],16),r.r[6]))
        r.r[2]=hit.hit;r.save(0x700031d0,0x950000)
        r.save(0x95001a,hit.flags,2);r.save(0x700031d8,hit.kind)
        r.save(0x700031d4,ACTORS if hit.owner==pickup.identity else 0x999999)
    o.calls[0x19a910]=query
    o.run(0x183ef0,(PLAYER,ACTORS))
    actual_queries=[]
    def native_query(_,start,end,mode,result):
        actual_queries.append((C.string_at(start,16),C.string_at(end,16),mode))
        result[0]=hit
        return 1
    score=C.c_float(91.25)
    result=native.em_interaction_pickup_candidate(C.byref(pickup),C.byref(player),
        C.byref(coefficients),RAY(native_query),None,C.byref(score))
    actual=(result,bits(score.value),actual_queries)
    expected=(o.r[2],o.load(0x70003b98),expected_queries)
    assert actual==expected,dict(pickup=(pickup.flags,pickup.subtype,pickup.selector,
        list(pickup.position),list(pickup.angles)),player=(list(player.position),
        player.yaw,player.action,list(player.target)),hit=(hit.hit,hit.flags,hit.kind,hit.owner),
        actual=actual,expected=expected)


def visible_case(elf,native,position,anchor,forward):
    o=ScanOracle(elf)
    o.write(0x8105d0,struct.pack('<3f',*anchor))
    o.write(0x810600,struct.pack('<3f',*forward))
    o.run(0x1b1630,floats=position)
    result=native.em_interaction_visible((C.c_float*3)(*position),
        (C.c_float*3)(*anchor),(C.c_float*3)(*forward))
    assert result==o.r[2],(position,anchor,forward,result,o.r[2])
    return result


def snapshot_recipe(elf,native,name):
    path=DECOMP/'build/startup-reference'/name;ram=path.read_bytes()
    def u32(address):return struct.unpack_from('<I',ram,address)[0]
    pointer=u32(0x275bc0);accepted=[];owners=[]
    anchor=struct.unpack_from('<3f',ram,0x8105d0)
    forward=struct.unpack_from('<3f',ram,0x810600)
    while pointer:
        if u32(pointer+16) in (0x219550,0x15afa0,0x159210,0x827b10,0x1bc350,0x8237e0):
            position=list(struct.unpack_from('<3f',ram,pointer+0xb0))
            callback=u32(pointer+16)
            if callback==0x1bc350:position[1]=number(bits(position[1]+10))
            result=visible_case(elf,native,position,anchor,forward)
            # Initial Roger path can force its DRAW flag after publication.
            if callback!=0x8237e0:assert result==ram[pointer+1]
            if result:accepted.append(pointer)
            owners.append(dict(owner=f'{pointer:08X}',uid=ram[pointer+0x9a],
                callback=f'{u32(pointer+16):08X}',visible=result))
        pointer=u32(pointer+0x1c)
    count=struct.unpack_from('<h',ram,0x275b64)[0]
    captured=[u32(u32(0x275b5c)+4*i) for i in range(count)]
    assert list(reversed(accepted))==captured
    return dict(snapshot=name,sha256=hashlib.sha256(ram).hexdigest(),
        anchor=anchor,forward=forward,owners=owners,
        reconstructed_list=[f'{p:08X}' for p in reversed(accepted)])


def additional_owner_publication(elf):
    ram=(DECOMP/'build/startup-reference/playable_ee.bin').read_bytes()
    overlay=(DECOMP/'extract/OVERLAY/AREA11.BIN').read_bytes()
    cases=[]
    def return_zero(oracle):oracle.r[2]=0
    def base(source,near):
        o=ScanOracle(elf);o.write(0x823500,overlay)
        o.write(ACTORS,ram[source:source+0x2f0]);o.save(ACTORS+0x14,ACTORS)
        o.write(0x810700,ram[0x810700:0x810900])
        o.write(0x8105d0,ram[0x8105d0:0x810610])
        if near:o.write(0x8105d0,ram[source+0xb0:source+0xbc])
        o.save(0x275b60,0x28ab30);o.save(0x275b68,0,2)
        for target in (0x1ba580,0x1c64f0,0x1c68c0,0x1b1ca0):
            o.calls[target]=lambda _:None
        return o
    # Door method return is outside publication scope; stop at its JALR.
    method=next(a for a in range(0x1bc300,0x1bc350,4)
                if (struct.unpack_from('<I',elf,a-0x100000+0x300)[0]&0xfc00003f)==9)
    for near in (0,1):
        o=base(0x7a70b0,near);o.run(0x1bc300,(ACTORS,),stop=method)
        assert o.load(0x275b68,2)==near
        cases.append(dict(owner='door',near=near,published=near,cull_y_bias=10))
    # Execute the actual three Roger controller branches, with script,
    # animation and automatic polygon trigger as explicit boundaries.
    for near,(story,suppressed,alternate,armed,start,stop) in itertools.product((0,1),(
            (0,1,0,0,None,None),(0,0,0,0,None,0x823b48),
            (0,0,1,0,0x828990,0x8239f4),(1,0,0,0,None,0x823c28),
            (1,0,0,4,0x828810,0x823c28),(0x80,0,0,0,0x828a10,0x823cc8))):
        o=base(0x7a8830,near);calls=[]
        o.save(0x8107d8,story,1);o.save(0x810791,suppressed,1)
        o.save(0x810793,alternate,1);o.save(0x810813,0,1)
        o.save(ACTORS+5,0,1);o.save(ACTORS+11,armed,1)
        o.calls[0x1b1ea0]=return_zero
        o.calls[0x1ba1f0]=return_zero
        o.calls[0x1ba1a0]=lambda r:calls.append(r.r[5])
        if stop is None:o.run(0x8237e0,(ACTORS,))
        else:o.run(0x8237e0,(ACTORS,),stop=stop)
        assert calls==([] if start is None else [start])
        expected=int(near and not suppressed)
        assert o.load(0x275b68,2)==expected
        cases.append(dict(owner='Roger',story=story,suppressed=suppressed,
            alternate=alternate,armed=armed,near=near,published=expected,script=start))
    return cases


def main():
    elf=(DECOMP/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest()==ELF_SHA
    output=ROOT/'build/interaction_scan_reference';output.mkdir(parents=True,exist_ok=True)
    library=output/'interaction_pickup.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
        '-shared','-fPIC','-Isrc','src/game/em_interaction_scan.c','-lm','-o',str(library)],cwd=ROOT,check=True)
    native=C.CDLL(str(library))
    native.em_interaction_pickup_candidate.argtypes=[C.POINTER(Pickup),C.POINTER(Player),
        C.POINTER(Math),RAY,C.c_void_p,C.POINTER(C.c_float)]
    native.em_interaction_visible.argtypes=[C.POINTER(C.c_float)]*3
    coefficients=Math.from_buffer_copy(elf[0x26c5d8-0x100000+0x300:0x26c624-0x100000+0x300])
    rng=random.Random(0x183ef0);cases=0
    for flags,subtype,selector,distance,yaw,hitflags,own in itertools.product(
        (0x84,0x87),(0,1,2),(3,4),(6.99999,7.,7.00001,10.,10.00001),
        (0.,1.57079637,3.14159274),(0,0x2000,0x800),(0,1)):
        pickup=Pickup(123,flags,subtype,selector,0x219550,(C.c_float*3)(distance,0,0),
                      (C.c_float*3)(0,.3,0),(C.c_float*2)(10,3.5))
        player=Player((C.c_float*3)(0,0,0),yaw,0,(C.c_float*3)(0,0,1))
        hit=Hit(1,hitflags,2,123 if own else 456)
        pickup_case(elf,native,coefficients,pickup,player,hit);cases+=1
    for case in range(1500):
        flags=rng.choice((0x84,0x87));subtype=rng.randrange(3)
        pickup=Pickup(123,flags,subtype,rng.choice((3,4)),
            rng.choice((0x219550,0x15afa0)),
            (C.c_float*3)(rng.uniform(-11,11),rng.uniform(-5,24),rng.uniform(-11,11)),
            (C.c_float*3)(rng.uniform(-1,1),rng.uniform(-math.pi,math.pi),rng.uniform(-1,1)),
            (C.c_float*2)(10,3.5))
        player=Player((C.c_float*3)(0,0,0),rng.uniform(-math.pi,math.pi),
                      rng.choice((0,0,0,0x2d)),(C.c_float*3)(1,2,10))
        hit=Hit(rng.randrange(2),rng.choice((0,0x2000,0x800,0x2800)),
                rng.randrange(4),rng.choice((123,456)))
        pickup_case(elf,native,coefficients,pickup,player,hit);cases+=1
    visibility_cases=0
    for distance,facing in itertools.product((0,34.99999,35,44.99999,45,349.9999,350,350.0001),
                                             (-1,0,.6999999,.7,.7000001,1)):
        visible_case(elf,native,(0,0,distance),(0,0,0),(0,0,facing));visibility_cases+=1
    for _ in range(500):
        visible_case(elf,native,[rng.uniform(-360,360) for _ in range(3)],
                     [rng.uniform(-20,20) for _ in range(3)],
                     [rng.uniform(-1,1) for _ in range(3)]);visibility_cases+=1
    snapshots=[snapshot_recipe(elf,native,name) for name in ('opening_ee.bin','playable_ee.bin')]
    additional=additional_owner_publication(elf)
    report=dict(elf_sha256=ELF_SHA,pickup_cases=cases,visibility_cases=visibility_cases,
        snapshots=snapshots,additional_owner_publication=additional,
        boundaries=['LOS is an injected original query-family6 boundary.',
        'Finite EE/VU arithmetic model; no new physical EE rounding claim.',
        'Eleven-owner recipe includes the door+10Y cull point and Roger actual cull, not its forced draw byte.'])
    (output/'pickup_report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Original183EF0 pickup + SDK atan2 + mode6 query contract: {cases} cases PASS')
    print(f'OriginalB1630 + VU normalize/dot: {visibility_cases} cases PASS')
    print('Original opening/playable previous-frame publication lists reconstructed exactly PASS')
    print(f'Original Roger/door publication and script selection: {len(additional)} cases PASS')


if __name__=='__main__':main()
