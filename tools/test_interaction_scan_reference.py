#!/usr/bin/env python3
"""Compare use arbitration and elevator eligibility with original EE instructions.

No source C or copied disassembly is the oracle. The user's pinned ELF is
read locally; generated source receipts and runtime data remain in build/.
Calls to183EF0 are controlled only for the general arbitration tests. The
elevator tests execute183EF0 plus original SDK sqrt/fabs/angle-wrap bodies.
"""
import ctypes as C
import hashlib
import itertools
import json
import math
from pathlib import Path
import random
import struct
import subprocess

from test_point_light_reference import Oracle, bits, number, signed, fp

ROOT=Path(__file__).resolve().parents[1]
DECOMP=ROOT.parent/'Extermination'
ELF_SHA='ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
PLAYER,ACTORS,LIST,DESC=0x900000,0x910000,0x930000,0x940000


class ScanOracle(Oracle):
    def plain(self,word):
        op,rs,rt,rd=word>>26,word>>21&31,word>>16&31,word>>11&31
        fn=word&63
        if op==0 and fn==4:
            self.r[rd]=(self.r[rt]<<(self.r[rs]&31))&0xffffffff
        elif op==0 and fn==43:
            self.r[rd]=int((self.r[rs]&0xffffffff)<(self.r[rt]&0xffffffff))
        elif op==11:
            self.r[rt]=int((self.r[rs]&0xffffffff)<(signed(word&65535,16)&0xffffffff))
        elif op==17 and rs==16 and fn==26:
            self.scalar_accumulator=fp(number(self.f[rd])*number(self.f[rt]))
        elif op==17 and rs==16 and fn==28:
            self.f[word>>6&31]=bits(fp(self.scalar_accumulator+
                                        fp(number(self.f[rd])*number(self.f[rt]))))
        elif op==17 and rs==16 and fn==24:
            self.scalar_accumulator=fp(number(self.f[rd])+number(self.f[rt]))
        elif op==17 and rs==16 and fn==7:
            self.f[word>>6&31]=self.f[rd]^0x80000000
        elif op==0 and fn==38:
            self.r[rd]=self.r[rs]^self.r[rt]
        elif op==55:
            self.r[rt]=self.load((self.r[rs]+signed(word&65535,16))&0xffffffff,8)
        elif op==63:
            self.save((self.r[rs]+signed(word&65535,16))&0xffffffff,self.r[rt],8)
        elif op==37:
            self.r[rt]=self.load((self.r[rs]+signed(word&65535,16))&0xffffffff,2)
        else:
            super().plain(word)
        self.r[0]=0


class State(C.Structure):
    _fields_=[('selector',C.c_uint8),('fade_wait',C.c_int16),
              ('inhibited',C.c_uint8),('score',C.c_float)]


class Candidate(C.Structure):
    _fields_=[('owner',C.c_void_p),('status',C.c_uint8),
              ('class_flags',C.c_uint8),('armed',C.POINTER(C.c_uint8))]


class List(C.Structure):
    _fields_=[('pending',Candidate*32),('active',Candidate*32),
              ('pending_count',C.c_size_t),('active_count',C.c_size_t)]


class Panel(C.Structure):
    _fields_=[(name,C.c_uint8) for name in ('status','phase','charged','armed','child')]+[('cost',C.c_uint16)]


PREDICATE=C.CFUNCTYPE(C.c_int,C.c_void_p,C.POINTER(Candidate),C.POINTER(C.c_float))


def arbitration(elf,native,rows,gates=(0,0,0),score=123.25):
    oracle=ScanOracle(elf)
    oracle.save(0x70003b8d,gates[0],1)
    oracle.save(0x28a9a0,gates[1],2)
    oracle.save(0x8106ef,gates[2],1)
    oracle.save(0x70003b98,bits(score))
    oracle.save(0x275b5c,LIST);oracle.save(0x275b64,len(rows),2)
    visited=[]
    for i,(status,flags,armed,result,value) in enumerate(rows):
        actor=ACTORS+i*0x400
        oracle.save(LIST+4*i,actor)
        oracle.save(actor,status,1);oracle.save(actor+2,flags,1)
        oracle.save(actor+11,armed,1)
    def predicate(o):
        assert o.r[4]==PLAYER
        index=(o.r[5]-ACTORS)//0x400;visited.append(index)
        _,_,_,result,value=rows[index]
        if value is not None:o.save(0x70003b98,bits(value))
        o.r[2]=result&0xffffffff
    oracle.calls[0x183ef0]=predicate
    oracle.run(0x184ba0,(PLAYER,))
    state=State(*gates,score)
    arms=[C.c_uint8(row[2]) for row in rows]
    entries=(Candidate*len(rows))(*(Candidate(i+1,row[0],row[1],C.pointer(arms[i]))
                                       for i,row in enumerate(rows)))
    actual_visits=[]
    def native_predicate(_,candidate,shared):
        index=candidate.contents.owner-1;actual_visits.append(index)
        value=rows[index][4]
        if value is not None:shared[0]=value
        return rows[index][3]
    winner=C.c_size_t()
    result=native.em_interaction_scan(C.byref(state),entries,len(rows),
                                     PREDICATE(native_predicate),None,C.byref(winner))
    expected_arms=[oracle.load(ACTORS+i*0x400+11,1) for i in range(len(rows))]
    winners=[i for i,(before,after) in enumerate(zip(rows,expected_arms))
             if before[2]!=after]
    expected_winner=winners[0] if winners else C.c_size_t(-1).value
    actual=(result,state.selector,bits(state.score),[a.value for a in arms],
            actual_visits,winner.value)
    expected=(oracle.r[2],oracle.load(0x70003b8d,1),oracle.load(0x70003b98),
              expected_arms,visited,expected_winner)
    assert actual==expected,dict(rows=rows,gates=gates,actual=actual,expected=expected)


def elevator(elf,native,descriptor,position,yaw,action):
    oracle=ScanOracle(elf)
    oracle.save(ACTORS+2,0x84,1);oracle.save(ACTORS+8,1,1)
    oracle.save(ACTORS+0x30,DESC);oracle.save(PLAYER+0x1f0,action,1)
    oracle.write(DESC,struct.pack('<6f',*descriptor))
    oracle.write(PLAYER+0xa0,struct.pack('<3f',*position))
    oracle.save(PLAYER+0xc4,bits(yaw));oracle.save(0x70003b98,bits(-543.25))
    # Full original SDK helper execution, including the ordinary positive
    # argument wrapper. All generated squared distances are finite.
    oracle.run(0x183ef0,(PLAYER,ACTORS))
    score=C.c_float(-543.25)
    result=native.em_interaction_elevator_candidate((C.c_float*6)(*descriptor),
                (C.c_float*3)(*position),yaw,action,C.byref(score))
    actual=(result,bits(score.value));expected=(oracle.r[2],oracle.load(0x70003b98))
    assert actual==expected,dict(descriptor=descriptor,position=position,
                                yaw=yaw,action=action,actual=actual,expected=expected)


def collection_order(elf,native):
    total=0
    for count in range(41):
        oracle=ScanOracle(elf)
        oracle.save(0x275b60,0x28ab30)
        oracle.save(0x275b68,0,2)
        listing=List();armed=C.c_uint8()
        # Distinct proxy and canonical addresses prove which is published.
        for i in range(count):
            oracle.save(ACTORS+i*0x400+0x14,0xa00000+i*0x400)
            oracle.run(0x1b1de0,(ACTORS+i*0x400,))
            candidate=Candidate(0xa00000+i*0x400,1,0x84,C.pointer(armed))
            native.em_interaction_list_push(C.byref(listing),C.byref(candidate))
        for target in (0x1a9d20,0x1a8da0,0x1a9f60,0x1aa140,0x1a7870,
                       0x1a8be0,0x1a9000,0x1a97b0,0x1a9b10):
            oracle.calls[target]=lambda _:None
        oracle.run(0x1aad00)
        size=min(count,32)
        assert oracle.load(0x275b64,2)==size
        base=oracle.load(0x275b5c)
        assert [oracle.load(base+i*4) for i in range(size)]==[
            0xa00000+i*0x400 for i in reversed(range(size))]
        assert oracle.load(0x275b60)==0x28ab30 and oracle.load(0x275b68,2)==0
        native.em_interaction_list_publish(C.byref(listing))
        assert listing.active_count==size and listing.pending_count==0
        assert [listing.active[i].owner for i in range(size)]==[
            oracle.load(base+i*4) for i in range(size)]
        total+=1
    return total


def panel_scores(elf,native):
    count=0
    for dx,dy,dz,yaw in itertools.product((0.,9.49999,9.5,9.50001),
            (-20.00001,-20.,0.,20.,20.00001),(0.,.001),
            (-1.,-.7853982,0.,.7853982,1.)):
        o=ScanOracle(elf)
        o.save(ACTORS+2,0x84,1);o.save(ACTORS+3,0x24,1)
        o.save(ACTORS+0x30,DESC);o.write(DESC,struct.pack('<3f',9.5,20,10))
        o.save(ACTORS+0xc4,0xc0490fdb)
        o.write(PLAYER+0xa0,struct.pack('<3f',dx,dy,dz))
        o.save(PLAYER+0xc4,bits(yaw));o.save(0x70003b98,bits(123.25))
        o.run(0x183ef0,(PLAYER,ACTORS))
        state=Panel(1,0,0,0,1,2);score=C.c_float(123.25)
        result=native.em_panel_candidate(C.byref(state),(C.c_float*3)(0,0,0),
            number(0xc0490fdb),(C.c_float*3)(dx,dy,dz),yaw,C.byref(score))
        assert (result,bits(score.value))==(o.r[2],o.load(0x70003b98))
        count+=1
    return count


def frame_order(elf):
    oracle=ScanOracle(elf);events=[]
    targets=(0x1cb590,0x15bcf0,0x1cb5a0,0x1d1c50,0x1c1d00,0x1afd70,
             0x15c160,0x1f0360,0x18b9c0,0x1aad00,0x1d1ea0)
    for target in targets:
        oracle.calls[target]=lambda o,t=target:events.append(t)
    oracle.run(0x1ae5e0)
    assert events==[0x1cb590,0x15bcf0,0x1cb5a0,0x1d1c50,0x1c1d00,
        0x1afd70,0x15c160,0x1f0360,0x1cb590,0x18b9c0,0x1cb5a0,
        0x1aad00,0x1d1ea0]
    return [f'{value:08X}' for value in events]


def caller_gate(elf):
    cases=0
    for pressed,mask,accepted in itertools.product((0,0x20,0x40,0x240,0xffff),
                                                   (0x40,0x200),(0,1)):
        o=ScanOracle(elf);events=[]
        o.save(0x810e74,pressed,2);o.save(0x70003b76,mask,2)
        o.save(0x810700,11,1);o.save(PLAYER+0x236,1,1)
        o.save(PLAYER+5,1,1);o.save(PLAYER+6,1,1)
        o.save(PLAYER+0x1f0,1,1);o.save(PLAYER+0x38,123)
        o.save(PLAYER+0x21c,456);o.save(PLAYER+0x25c,7,1)
        def candidate(r):
            assert r.r[4]==PLAYER
            events.append('scan');r.r[2]=accepted
        def transition(r):
            assert r.r[4]==PLAYER and r.f[12]==0
            events.append('transition0')
        def fallback(r):events.append('fallback');r.r[2]=0
        o.calls[0x184ba0]=candidate;o.calls[0x174a50]=transition
        o.calls[0x15d4c0]=fallback;o.calls[0x15fdf0]=fallback
        o.run(0x160220,(PLAYER,))
        use=bool(pressed&mask)
        assert o.r[2]==int(use and accepted)
        if use and accepted:
            assert events==['scan','transition0']
            assert [o.load(PLAYER+i,1) for i in (5,6,0x1f0,0x25c)]==[0x25,0,0,0]
            assert o.load(PLAYER+0x38)==o.load(PLAYER+0x21c)==0
        else:
            assert events==(['scan','fallback','fallback'] if use else [])
            assert o.load(PLAYER+5,1)==o.load(PLAYER+6,1)==1
        cases+=1
    return cases


def snapshot_proof(path):
    ram=path.read_bytes()
    def u32(address):return struct.unpack_from('<I',ram,address)[0]
    count=struct.unpack_from('<h',ram,0x275b64)[0]
    entries=[]
    for i in range(count):
        ptr=u32(u32(0x275b5c)+i*4)
        entries.append(dict(index=i,owner=f'{ptr:08X}',callback=f'{u32(ptr+16):08X}',
            flags=list(ram[ptr:ptr+12]),position=struct.unpack_from('<3f',ram,ptr+0xb0)))
    elevator_owner=next(int(row['owner'],16) for row in entries
                        if row['callback']=='00827B10')
    assert ram[elevator_owner+2]==0x84 and ram[elevator_owner+8]==1
    address=u32(elevator_owner+0x30)
    assert address==0x82ab10
    descriptor=struct.unpack_from('<6f',ram,address)
    return dict(file=str(path),sha256=hashlib.sha256(ram).hexdigest(),entries=entries,
                elevator_descriptor_address=f'{address:08X}',elevator_descriptor=descriptor)


def main():
    elf=(DECOMP/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest()==ELF_SHA
    out=ROOT/'build/interaction_scan_reference';out.mkdir(parents=True,exist_ok=True)
    lib=out/'interaction_scan.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
        '-shared','-fPIC','-Isrc','src/game/em_interaction_scan.c','src/game/em_panel.c','-lm','-o',str(lib)],
        cwd=ROOT,check=True)
    native=C.CDLL(str(lib))
    native.em_interaction_scan.argtypes=[C.POINTER(State),C.POINTER(Candidate),
        C.c_size_t,PREDICATE,C.c_void_p,C.POINTER(C.c_size_t)]
    native.em_interaction_elevator_candidate.argtypes=[C.POINTER(C.c_float),
        C.POINTER(C.c_float),C.c_float,C.c_uint8,C.POINTER(C.c_float)]
    native.em_interaction_list_push.argtypes=[C.POINTER(List),C.POINTER(Candidate)]
    native.em_interaction_list_publish.argtypes=[C.POINTER(List)]
    native.em_panel_candidate.argtypes=[C.POINTER(Panel),C.POINTER(C.c_float),C.c_float,
        C.POINTER(C.c_float),C.c_float,C.POINTER(C.c_float)]
    rng=random.Random(0x184ba0);scan_cases=0
    # Gated calls must not reset score, visit candidates or touch armed bytes.
    for gates in itertools.product((0,1,3,255),(0,1,-1),(0,1,255)):
        arbitration(elf,native,[(1,0x84,0,2,1.)],gates);scan_cases+=1
    # Every combination of outer eligibility bits and armed byte.
    for status,flags,armed in itertools.product((0,1,2,3,255),(0,4,0x80,0x84,255),(0,1,4,5,255)):
        arbitration(elf,native,[(status,flags,armed,1,7.)]);scan_cases+=1
    directed=[[],[(1,0x84,0,1,10000.)],[(1,0x84,0,1,9999.999)],
        [(1,0x84,0,1,4.),(1,0x84,0,1,4.)],
        [(1,0x84,0,1,1.),(1,0x84,0,2,99999.),(1,0x84,0,2,0.)],
        [(1,0x84,0,0,4.),(1,0x84,0,1,None)],
        [(1,0x84,0,1,None)],[(1,0x84,0,1,math.nan)],
        [(1,0x84,0,1,3.),(1,0x84,0,0,4.),(1,0x84,0,1,None)],
        [(1,0x84,0,-1,-5.),(1,0x84,0,0,7.)]]
    for rows in directed:arbitration(elf,native,rows);scan_cases+=1
    for case in range(1500):
        rows=[(rng.choice((0,1,1,3)),rng.choice((4,0x84,0x87)),
            rng.choice((0,0,0,4)),rng.choice((0,0,1,1,2,-1)),
            rng.choice((None,0.,3.,5.,10000.,number(bits(rng.uniform(-5,15000))))))
            for _ in range(case%33)]
        arbitration(elf,native,rows);scan_cases+=1

    snapshots=[snapshot_proof(DECOMP/'build/startup-reference'/name)
               for name in ('opening_ee.bin','playable_ee.bin')]
    descriptor=list(snapshots[-1]['elevator_descriptor'])
    candidate_cases=0
    # Both original floor heights, precise radius/height and angle borders.
    for floor,dx,dy,dz,angle,action in itertools.product((190.,230.),
        (0.,4.99999,5.,5.00001),(0.,19.99999,20.,20.00001,-20.,-20.00001),
        (0.,.001),(-math.pi/4,-math.pi/4-1e-6,0.,math.pi/4,math.pi/4+1e-6),
        (0,0x2d)):
        d=list(descriptor);d[1]=floor
        p=(d[0]+dx,d[1]+dy,d[2]+dz)
        yaw=d[5]-number(0x40490fdb)+angle
        elevator(elf,native,d,p,yaw,action);candidate_cases+=1
    for case in range(1000):
        d=list(descriptor);d[1]=rng.choice((190.,230.))
        p=[d[i]+rng.uniform(-9 if i!=1 else -30,9 if i!=1 else 30) for i in range(3)]
        elevator(elf,native,d,p,rng.uniform(-math.pi,math.pi),rng.choice((0,1,0x2d,255)))
        candidate_cases+=1
    collection_cases=collection_order(elf,native)
    caller_cases=caller_gate(elf)
    panel_cases=panel_scores(elf,native)
    report=dict(elf_sha256=ELF_SHA,arbitration_cases=scan_cases,
        elevator_candidate_cases=candidate_cases,collection_cases=collection_cases,
        caller_gate_cases=caller_cases,
        panel_score_cases=panel_cases,
        ordinary_frame_calls=frame_order(elf),snapshots=snapshots,
        boundaries=['Native finite EE arithmetic model, not a new physical EE precision proof.',
            'Only elevator class4/selector1 predicate is translated here.',
            'Collection visibility and other class predicates remain separate host work.',
            'No compiled PS2 byte-match claim; this is a portable semantic translation.'])
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Original00184BA0: {scan_cases} arbitration/state/call-order cases PASS')
    print(f'Original00183EF0 + SDK math: {candidate_cases} elevator predicate/score cases PASS')
    print(f'Original001B1DE0 +001AAD00: {collection_cases} bounded reverse-owner publications PASS')
    print('Original001AE5E0 frame ordering and two captured interactive lists PASS')
    print(f'Original00160220 +001798D0: {caller_cases} Use edge/takeover cases PASS')
    print(f'Original00183EF0 panel score-write order: {panel_cases} cases PASS')


if __name__=='__main__':main()
