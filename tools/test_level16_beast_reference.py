#!/usr/bin/env python3
"""The AREA04 sub 1 creature (behaviour 00147390, kind 7 nodes [15] and [17])
and its state family, against the ORIGINAL instructions. Unwired.

The harness is tools/test_level14_port_reference.py (`O`), configured as
test_level15_motion_reference.py configures it: calls between the family's
functions run as original code at the top level of the oracle, everything
else is a hook; the helpers in MATH run as original code nested in the oracle.
Compared per case: every call and its arguments, memory at every call entry,
the memory accesses between calls one for one in order, memory after the last
store, the result, a poisoned re-run, coverage of every reachable original
word, the callee inventory and the fail-stop contract. The start image is the
fork recording a04c_01_door73 (AREA04 sub 1, overlay 5 resident); the designed
node sits in zero RAM at FREE, and two cases run the image's live nodes.

EM_TEST_FULL=1 runs the long random sweep and the whole contract.
EM_LEVEL16_BEAST_ONLY=<label prefix> runs a subset (no coverage or contract);
EM_LEVEL16_BEAST_SOURCE=<dir> tests another copy of the module sources;
EM_LEVEL16_BEAST_SHOW=<n> prints n failures; EM_LEVEL16_BEAST_TRACE=<label>
prints the original's calls and accesses of matching cases (no instruction
text). Reports hold addresses and counts only.
"""
import ctypes as C
import os
import random
import re
from pathlib import Path
import test_level14_port_reference as O
import reference_root as refs
ROOT=Path(__file__).resolve().parents[1]
U,I,F,Q,S=O.U,O.I,O.F,O.Q,O.S
FUNCS={
    0x00147390:('em_level16_beast_00147390',528,[U, S],None),
    0x001475A0:('em_level16_beast_001475A0',348,[U, U],None),
    0x00147700:('em_level16_beast_00147700',600,[U, U, S],None),
    0x00147960:('em_level16_beast_00147960',484,[U, U],None),
    0x00147B50:('em_level16_beast_00147B50',1492,[U, U, S],None),
    0x00148130:('em_level16_beast_00148130',720,[U, U],None),
    0x00148400:('em_level16_beast_00148400',280,[U, U],None),
    0x00148520:('em_level16_beast_00148520',440,[U, U],None),
    0x001486E0:('em_level16_beast_001486E0',1112,[U, U],None),
    0x00148B40:('em_level16_beast_00148B40',4112,[U, U],None),
    0x00149B50:('em_level16_beast_00149B50',1396,[U, U, S],None),
    0x0014A0D0:('em_level16_beast_0014A0D0',628,[U, U],None),
    0x0014A350:('em_level16_beast_0014A350',708,[U, U],None),
    0x0014A620:('em_level16_beast_0014A620',272,[U, U],None),
    0x0014A730:('em_level16_beast_0014A730',612,[U, U],None),
    0x0014A9A0:('em_level16_beast_0014A9A0',792,[U, U],None),
    0x0014ACC0:('em_level16_beast_0014ACC0',392,[U, U],None),
    0x0014AE50:('em_level16_beast_0014AE50',48,[U],None),
    0x0014AE80:('em_level16_beast_0014AE80',276,[U, U],None),
    0x0014AFA0:('em_level16_beast_0014AFA0',2056,[U, U],None),
    0x0014B7B0:('em_level16_beast_0014B7B0',856,[U, U, S],I),
    0x0014BB10:('em_level16_beast_0014BB10',288,[U, U],None),
    0x0014BC30:('em_level16_beast_0014BC30',488,[U, U],I),
    0x0014BE20:('em_level16_beast_0014BE20',648,[U, U],I),
    0x0014C0B0:('em_level16_beast_0014C0B0',124,[U, U],None),
    0x0014C130:('em_level16_beast_0014C130',240,[U],I),
}
HOOKS=[
    ('w_001000E0',0x001000E0,[('q', 4), ('q', 5)],'i'),
    ('w_00102760',0x00102760,[('u', 4), ('u', 5)],None),
    ('w_00102948',0x00102948,[('u', 4), ('u', 5)],None),
    ('w_00102958',0x00102958,[('u', 4), ('u', 5)],None),
    ('w_001026A0',0x001026A0,[('u', 4), ('u', 5), ('u', 6)],None),
    ('w_001028B8',0x001028B8,[('u', 4), ('u', 5), ('u', 6)],None),
    ('w_001029C0',0x001029C0,[('u', 4)],None),
    ('w_00102BB0',0x00102BB0,[('u', 4), ('u', 5), ('f', 12)],None),
    ('w_001031E0',0x001031E0,[('u', 4), ('u', 5)],None),
    ('w_0011DE90',0x0011DE90,[('f', 12)],'f'),
    ('w_0011DF78',0x0011DF78,[('f', 12)],'f'),
    ('w_0011E2A8',0x0011E2A8,[('f', 12)],'f'),
    ('w_0011E620',0x0011E620,[('f', 12), ('f', 13)],'f'),
    ('w_0011E748',0x0011E748,[('f', 12)],'f'),
    ('w_00122BB8',0x00122BB8,[],'i'),
    ('w_001281C0',0x001281C0,[('f', 12)],'i'),
    ('w_00128350',0x00128350,[('f', 12)],'q'),
    ('w_00131ED0',0x00131ED0,[('u', 4)],None),
    ('w_00182F90',0x00182F90,[('u', 4), ('u', 5)],None),
    ('w_0019A440',0x0019A440,[('u', 4), ('u', 5), ('i', 6)],'i'),
    ('w_0019A570',0x0019A570,[('u', 4), ('u', 5), ('i', 6), ('i', 7)],'i'),
    ('w_0019AB20',0x0019AB20,[('u', 4), ('u', 5), ('u', 6), ('i', 7)],'i'),
    ('w_0019AD00',0x0019AD00,[('u', 4), ('u', 5), ('i', 6)],'i'),
    ('w_0019B6C0',0x0019B6C0,[('u', 4), ('u', 5)],'i'),
    ('w_001A7B80',0x001A7B80,[('u', 4)],'i'),
    ('w_001AFC10',0x001AFC10,[('u', 4)],None),
    ('w_001B0D80',0x001B0D80,[('u', 4)],None),
    ('w_001B10B0',0x001B10B0,[('u', 4), ('i', 5), ('i', 6)],'i'),
    ('w_001B1190',0x001B1190,[('i', 4)],None),
    ('w_001B1240',0x001B1240,[('u', 4), ('f', 12), ('f', 13)],'f'),
    ('w_001B12B0',0x001B12B0,[('f', 12), ('f', 13), ('f', 14)],'f'),
    ('w_001B13F0',0x001B13F0,[('u', 4), ('u', 5), ('f', 12)],'i'),
    ('w_001B1470',0x001B1470,[('f', 12)],'f'),
    ('w_001B1560',0x001B1560,[('u', 4), ('u', 5), ('f', 12)],'i'),
    ('w_001B15D0',0x001B15D0,[('u', 4), ('u', 5)],'f'),
    ('w_001B17A0',0x001B17A0,[('u', 4)],'i'),
    ('w_001B1EA0',0x001B1EA0,[('i', 4), ('u', 5), ('u', 6), ('i', 7)],'i'),
    ('w_001B2140',0x001B2140,[('u', 4)],'i'),
    ('w_001B2B10',0x001B2B10,[('u', 4), ('u', 5), ('u', 6)],None),
    ('w_001B2BF0',0x001B2BF0,[('u', 4), ('u', 5), ('u', 6), ('f', 12)],'i'),
    ('w_001B2F70',0x001B2F70,[('u', 4), ('u', 5)],'i'),
    ('w_001B3250',0x001B3250,[('u', 4), ('u', 5), ('f', 12)],'i'),
    ('w_001B32F0',0x001B32F0,[('u', 4), ('u', 5), ('f', 12)],'i'),
    ('w_001B3390',0x001B3390,[('u', 4), ('u', 5), ('u', 6), ('f', 12)],'i'),
    ('w_001B3440',0x001B3440,[('u', 4), ('u', 5), ('u', 6), ('f', 12)],'i'),
    ('w_001B3580',0x001B3580,[('u', 4), ('u', 5)],'f'),
    ('w_001B37D0',0x001B37D0,[('u', 4), ('f', 12), ('f', 13)],'f'),
    ('w_001B3C30',0x001B3C30,[('u', 4), ('u', 5), ('u', 6), ('f', 12), ('f', 13)],None),
    ('w_001B3F10',0x001B3F10,[('u', 4), ('f', 12), ('f', 13)],'i'),
    ('w_001B4810',0x001B4810,[('u', 4)],None),
    ('w_001B4CF0',0x001B4CF0,[('u', 4)],None),
    ('w_001B5360',0x001B5360,[('u', 4)],None),
    ('w_001B55E0',0x001B55E0,[('u', 4), ('i', 5)],None),
    ('w_001C63E0',0x001C63E0,[('u', 4), ('i', 5)],None),
    ('w_001C64F0',0x001C64F0,[('u', 4), ('f', 12)],'i'),
    ('w_001C67E0',0x001C67E0,[('u', 4), ('i', 5), ('f', 12), ('f', 13)],None),
    ('w_001C68C0',0x001C68C0,[('u', 4)],None),
    ('w_001C81C0',0x001C81C0,[('u', 4), ('i', 5), ('i', 6), ('u', 7)],None),
    ('w_001C82A0',0x001C82A0,[('u', 4), ('u', 5)],None),
    ('w_001C9D50',0x001C9D50,[('u', 4), ('u', 5), ('u', 6), ('f', 12)],None),
    ('w_001E8B90',0x001E8B90,[('u', 4), ('f', 12)],None),
    ('w_001EFD90',0x001EFD90,[('i', 4), ('u', 5), ('u', 6)],None),
    ('w_001EFE00',0x001EFE00,[('i', 4), ('u', 5)],'i'),
    ('w_001EFEB0',0x001EFEB0,[('i', 4), ('u', 5)],None),
    ('w_001FBD50',0x001FBD50,[('u', 4), ('i', 5), ('i', 6), ('f', 12)],None),
    ('w_0021BE40',0x0021BE40,[('u', 4), ('u', 5)],'i'),
    ('w_0021BED0',0x0021BED0,[('u', 4)],'i'),
    ('w_0021BF90',0x0021BF90,[('u', 4), ('u', 5)],None),
    ('w_0021C040',0x0021C040,[('u', 4), ('u', 5)],None),
]
BEAT='a04c_01_door73'
BEATS=(BEAT,)
FREE=0x1E61000
A=FREE+0x1000            # the designed node
ST=A+0x1F0               # its work block
AUX=FREE+0x3000          # the records its pointers reach
MODE,CONTACT,FLAG_70A=0x70003B8D,0x700031D0,0x81070A
PLAYER,TARGET,FLAG_6BD,FLAG_4E4=0x8102B0,0x810360,0x8106BD,0x8104E4
JUMP_PC,JUMP_TABLE,JUMP_COUNT=0x147754,0x26D2C0,10
CALLBACK=0x1CAA00        # the image's +0x4C method of kind 7 nodes
MATH=frozenset((0x1000E0,0x102760,0x102948,0x102958,0x1026A0,0x1028B8,0x1029C0,0x102BB0,0x1031E0,
                0x11DE90,0x11DF78,0x11E2A8,0x11E620,0x11E748,0x1281C0,0x128350,
                0x1B1240,0x1B12B0,0x1B1470))
# Words the static walk reaches and no input can: 0014BE20 stores zero to
# +0x36 and loads it straight back, so the test of 0x5000 never passes.
DEAD={0x14C07C,0x14C084}


def configure():
    O.FREE=FREE
    O.FUNCS,O.HOOKS=FUNCS,HOOKS
    O.OVERLAY_OF,O.OVERLAY_FILES={},{5:ROOT.parent/'Extermination/extract/OVERLAY/AREA04.BIN'}
    O.OUT=ROOT/'build/levels/level16_beast'
    O.SOURCES=('em_level16_beast.c',)
    O.BEATS=list(BEATS)
    O.BEAT_DIRS={BEAT:refs.capture('levels-recordings-20261030-1/s87/route_a04c/'+BEAT)}
    O.RESIDENT={BEAT:5}
    O.BY_ADDRESS={a:(n,args,res) for n,a,args,res in HOOKS}
    O.BY_NAME={n:a for n,a,_,_ in HOOKS}
    O.HOOK_NAMES=[h[0] for h in HOOKS]+['w_callback']
    O.Hooks=type('Hooks',(C.Structure,),{'_fields_':
        [('ctx',C.c_void_p),('bytes',O.BYTES_FN)]+
        [(name,O.hook_proto(args,res)) for name,_,args,res in HOOKS]+[('w_callback',O.CALLBACK_FN)]})
    O.SELF_ENTRIES={0x147390,0x147700,0x14A620};O.HEAVY=set()
    O.RETURN_SITES=O.RETURN_SITES_FULL=()
    O.CONTRACT_UNREACHABLE={};O.LOCAL_STORES=set()
    O.DEFAULT_ARGS={e:{1:[A],2:[A,ST],3:[A,ST,O.STACK_TOP]}[len(k)] if e not in (0x147390,) else [A,O.STACK_TOP]
                    for e,(_,_,k,_) in FUNCS.items()}
    O.image_for=lambda _:O.CAPTURES[BEAT][0]
    O.CAPTURES.clear();O.CAPTURES.update(O.load_captures(O.read_elf()))
    elf=O.read_elf(); table=JUMP_TABLE; n=JUMP_COUNT*4
    expected=elf[0x300+table-0x100000:0x300+table-0x100000+n]
    for ram,_ in O.CAPTURES.values(): assert ram[table:table+n]==expected
    O.JUMP_TABLES={JUMP_PC:(table,JUMP_COUNT)};O.TABLE_BYTES=frozenset(range(table,table+n))


def cases():
    out=[];b,h,w,f=O.b8,O.h16,O.w32,O.f32b
    contact=AUX+0x800; bone=AUX+0x400; spin=AUX+0xC00; spin2=AUX+0xD00
    common=[(A,bytes(0x2F0)),(A+0x4C,w(CALLBACK)),(A+0xB0,O.fw(2.,3.,4.,1.)),(A+0xC0,O.fw(0.,0.,0.,0.)),
            (A+0xC,b(2)),(A+0x13C,w(AUX)),(A+0x17C,w(bone)),(A+0x180,w(bone)),(A+0x40,w(AUX+0x200)),
            (AUX,bytes(0x1000)),(AUX+0xB0,O.fw(5.,6.,7.,1.)),(bone+0x90,O.fw(1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,3.,4.,5.,1.)),
            (contact+0x1A,b(0x5B)),(contact+0x24,O.fw(0.,1.,0.)),(CONTACT,w(contact)),(0x700031B4,f(7.)),
            (0x275B40,w(spin)),(spin+4,w(spin2)),(spin2+0x74,f(0.25)),(MODE,b(0))]
    labels=set()
    def add(label,e,patches=(),queues=None,run=MATH,args=None):
        assert label not in labels,label
        labels.add(label)
        all_patches=common+list(patches)
        out.append((label,BEAT,e,tuple(args or O.DEFAULT_ARGS[e]),
                    [(a,v) for a,v in all_patches if a<0x70000000],
                    [(a,v) for a,v in all_patches if a>=0x70000000],O.Script(queues),set(run)))
    BIT=(ST+0x20,w(0x1000)); NOBIT=(ST+0x20,w(0))
    # ---- 00147390: the mode byte, the phases, the timers
    for mode in (0,1,2,3,4,5):
        for phase in (0,1,2,3,4):
            for active in (0,1):
                add(f'beh/{mode}/{phase}/{active}',0x147390,[(MODE,b(mode)),(A+4,b(phase))],{0x1B2140:[active]})
    for name,at,pack in (('38',ST+0x38,w),('8C',ST+0x8C,b),('74',ST+0x74,h),('70',ST+0x70,h),('89',ST+0x89,b),
                         ('8E',ST+0x8E,b),('85',ST+0x85,b),('8A',ST+0x8A,b)):
        for value in (0,1,2,0xFF):
            add(f'beh/timer/{name}/{value}',0x147390,[(A+4,b(4)),(at,pack(value))])
    add('beh/live/15',0x147390,args=[0x7A8250,O.STACK_TOP],queues={0x1B2140:[1]})
    add('beh/live/17',0x147390,args=[0x7A8830,O.STACK_TOP],queues={0x1B2140:[1]})
    for state in range(11):
        add(f'beh/chain/{state}',0x147390,[(A+4,b(1)),(A+5,b(state)),(A+6,b(2)),BIT,(ST+0x38,w(1))],{0x1B2140:[1]})
    add('beh/chain/grab',0x147390,[(A+4,b(1)),(A+5,b(7)),(A+6,b(2)),BIT,(0x810354,f(3.))],
        {0x1B2140:[1],0x19A440:[0]*3,0x1B1EA0:[1]})
    add('beh/chain/way',0x147390,[(A+4,b(1)),(A+5,b(1)),(A+6,b(1)),(A+0xB4,f(3.))],
        {0x1B2140:[1],0x1B2BF0:[4]})
    # ---- 001475A0: placement
    for mark in (0,0xFF):
        for d in (0,0x80):
            for hard in (0,1):
                for loaded in (0,1):
                    add(f'place/{mark}/{d}/{hard}/{loaded}',0x1475A0,
                        [(0x810808,b(mark)),(A+0xD,b(d)),(FLAG_70A,b(hard))],{0x1B10B0:[loaded]})
    # ---- 00147700: the common frame
    for state in range(11):
        add(f'live/{state}',0x147700,[(A+5,b(state))],{0x1B2140:[1]})
    add('live/inactive',0x147700,queues={0x1B2140:[0]})
    for v85,v86,v88,v83,anim in ((0,0,0,0,0),(1,0,0,0,9),(0,1,0,0,0x109),(0,0,1,0,8),(0,0,0,1,9),(2,2,2,2,9)):
        add(f'live/flags/{v85}/{v86}/{v88}/{v83}/{anim}',0x147700,
            [(A+5,b(10)),(ST+0x85,b(v85)),(ST+0x86,b(v86)),(ST+0x88,b(v88)),(ST+0x83,b(v83))],
            {0x1B2140:[1],0x1C64F0:[anim]})
    add('live/worker/flags',0x147700,[(A+5,b(10))],{0x1B2140:[O.Scribble(1,[(ST+0x85,b(1)),(ST+0x88,b(1)),(ST+0x83,b(1))])]})
    # ---- 00147960: idle
    for row in (0,1):
        for col in range(16):
            for pose in (0,2):
                add(f'idle/roll/{row}/{col}/{pose}',0x147960,[(A+0x56,h(row*25)),(A+0x2C,h(pose)),(ST+0x68,f(100.))],
                    {0x122BB8:[col<<7,0x7F<<18]})
    add('idle/roll/code3',0x147960,[(0x245B20,b(3))],{0x122BB8:[0]})
    add('idle/roll/pose8000',0x147960,[(0x245B20,b(0)),(A+0x2C,h(0x8000))],{0x122BB8:[0]})
    for state,value in ((1,0),(1,1),(1,5),(2,0),(2,0x1000),(3,0)):
        add(f'idle/state/{state}/{value}',0x147960,[(A+6,b(state)),(ST+0x28,w(value)),(ST+0x20,w(value)),(A+0x56,h(25)),(ST+0x68,f(100.))])
    for timer,dist,hit in ((25,25.,0),(25,25.5,0),(25,100.,1),(-3,1.,0),(0,0.,0),(0,0.,1)):
        add(f'idle/tail/{timer}/{dist}/{hit}',0x147960,[(A+6,b(3)),(A+0x56,h(timer)),(ST+0x68,f(dist)),(A+0xA,b(hit))])
    add('idle/tail/alert',0x147960,[(A+6,b(3)),(ST+0x38,w(1)),(ST+0x68,f(100.))])
    add('idle/tail/bit2',0x147960,[(A+6,b(3)),(A+0xA,b(2))])
    add('idle/tail/bit2s',0x147960,[(A+6,b(3)),(A+0x56,h(25)),(ST+0x68,f(100.)),(A+0xA,b(2))])
    add('idle/tail/lost',0x147960,[(A+6,b(3)),(ST+0x38,w(1)),(ST+0x3C,w(0x77)),(ST+0x68,f(100.)),(A+0xA,b(1))])
    # ---- 00147B50: walking
    for pose in (2,0x8002,3,6,0x7FFE):
        add(f'walk/start/{pose}',0x147B50,[(A+0x2C,h(pose))],{0x122BB8:[0x12345678,0x7F00FF00]})
    add('walk/state3',0x147B50,[(A+6,b(3))])
    for yaw,goal in ((1.,1.),(1.,2.)):
        add(f'walk/turn/{yaw}/{goal}',0x147B50,[(A+6,b(2)),(A+0xC4,f(yaw)),(ST+0x44,f(goal))],run=MATH)
    W1=(A+6,b(1))
    add('walk/ledge',0x147B50,[W1,(A+0xB4,f(50.)),(0x700038D0,f(1.))],
        {0x1B2BF0:[5],0x122BB8:[1<<14],0x19A570:[0],0x19AD00:[0]})
    def way(n,hit,first=0.,patches=()):
        add(f'walk/blocked/{n}/{hit}/{first}',0x147B50,[W1,(ST+0x84,b(n)),(ST+0x44,f(first))]+list(patches),
            {0x1B2BF0:[0],0x1000E0:[hit],0x122BB8:[0xABCDE<<12]},run=MATH-{0x1000E0})
    for n,hit in ((0,0),(0,1),(1,1),(2,1),(0x7F,1),(0xFD,1)): way(n,hit)
    add('walk/blocked/real/near',0x147B50,[W1,(A+0xC4,f(0.)),(ST+0x44,f(3.)),(ST+0x84,b(2))],{0x1B2BF0:[0]})
    add('walk/blocked/real/far',0x147B50,[W1,(A+0xC4,f(0.)),(ST+0x44,f(1.)),(ST+0x84,b(2))],{0x1B2BF0:[0]})
    CLEAR=[W1,(A+0xB4,f(3.)),(0x700038D0,f(0.))]       # 3 - 45 < 0: way clear
    Q0={0x1B2BF0:[1]}
    def chain(label,patches,queues,run=MATH):
        add('walk/chain/'+label,0x147B50,CLEAR+[(ST+0x38,w(1))]+list(patches),{**Q0,**queues},run)
    for far in (0,1):
        for los in (0,1):
            for clear in (0,1):
                for dist in (50.,100.,150.):
                    chain(f'near/{far}/{los}/{clear}/{dist}',[(0x8104D8,f(dist))],
                          {0x1B13F0:[far],0x1B1560:[los],0x19A440:[clear]*3})
    add('walk/hit',0x147B50,CLEAR+[(A+0xA,b(1))],{**Q0,0x1B13F0:[0]})
    add('walk/hit/lost',0x147B50,CLEAR+[(A+0xA,b(1)),(ST+0x38,w(1)),(ST+0x3C,w(0x77)),(ST+0x68,f(100.))],{**Q0,0x1B13F0:[0]})
    for ahead in (0,1):
        for los in (0,1):
            for near in (0,1):
                for wall in (0,1):
                    chain(f'tail/{ahead}/{los}/{near}/{wall}',[],
                          {0x1B13F0:[0,near],0x122BB8:[1<<19 if ahead else 0],0x1B1560:[1,1,1] if ahead else [los],
                           0x1B3250:[wall]})
    for count in (0,1):
        for roam in (0,1):
            for gate,reach in ((0,0),(0,1),(1,0)):
                add(f'walk/roam/{count}/{roam}/{gate}/{reach}',0x147B50,
                    CLEAR+[(ST+0x28,w(count)),(ST+0x82,b(roam)),(0x70003B68,w(0x40)),(0x70003B8A,h(gate)),(ST,O.fw(1.,2.,3.,1.)),
                           (ST+0x68,f(100.)),(A+0x13C,w(AUX))],{**Q0,0x1B13F0:[reach]})
    add('walk/roam/signed',0x147B50,CLEAR+[(ST+0x82,b(1)),(0x70003B68,w(0x3)),(0x70003B8A,h(0xFFFD))],Q0)
    # ---- 00148130: the leap
    add('leap/0',0x148130)
    for bit in (0,1):
        add(f'leap/1/{bit}',0x148130,[(A+6,b(1)),(ST+0x20,w(bit*0x1000))])
    add('leap/9',0x148130,[(A+6,b(9))])
    for rate in (0.5,0.6,0.7,float('nan')):
        add(f'leap/2/clamp/{rate}',0x148130,[(A+6,b(2)),(ST+0x4C,f(rate))],{0x1B3250:[1]})
    for wall,bump,line,lost in ((1,0,0,0),(0,1,0,0),(0,0,0,0),(0,0,1,0),(0,0,1,1)):
        add(f'leap/2/{wall}/{bump}/{line}/{lost}',0x148130,[(A+6,b(2)),(ST+0x81,b(bump))],
            {0x1B3250:[wall],0x1B1560:[line],0x1B13F0:[lost]})
    for rate in (-1.,0.,1.):
        for bit in (0,1):
            add(f'leap/3/{rate}/{bit}',0x148130,[(A+6,b(3)),(ST+0x4C,f(rate)),(ST+0x20,w(bit*0x1000))])
    # ---- 00148400 / 00148520: the jump
    for state in (0,1,2):
        for bit in (0,1):
            for flag in (0,1,3,0xFF):
                add(f'jump/{state}/{bit}/{flag}',0x148400,[(A+6,b(state)),(ST+0x20,w(bit*0x1000)),(ST+0x8D,b(flag))])
    for state in (0,1,2,3):
        for bit in (0,1):
            for flag in (0,1,3):
                add(f'air/{state}/{bit}/{flag}',0x148520,[(A+6,b(state)),(ST+0x20,w(bit*0x1000)),(ST+0x8D,b(flag))])
    for dirty,hold in ((1,0),(1,1),(1,2),(0,1)):
        add(f'air/held/{dirty}/{hold}',0x148520,[(ST+0x86,b(dirty)),(FLAG_6BD,b(hold))])
    for rate,landed in ((0.,0),(0.,2),(0.1,2),(-1.,2),(-1.,0)):
        add(f'air/land/{rate}/{landed}',0x148520,[(A+6,b(1)),(ST+0x48,f(rate)),(ST+0x81,b(landed))])
    # ---- 001486E0: the ground spray
    add('spray/0',0x1486E0)
    for bit in (0,1):
        add(f'spray/1/{bit}',0x1486E0,[(A+6,b(1)),(ST+0x20,w(bit*0x1000))],{0x122BB8:[0x12345678,0x7ABCDE12,0x7FFFFFFF]})
        add(f'spray/3/{bit}',0x1486E0,[(A+6,b(3)),(ST+0x20,w(bit*0x1000))])
    add('spray/4',0x1486E0,[(A+6,b(4))])
    for left in (0,1):
        add(f'spray/2/bit/{left}',0x1486E0,[(A+6,b(2)),BIT,(ST+0x30,w(left))])
    for t0,t1,hard in ((0,0,0),(0,0,1),(1,1,0),(0,1,1),(1,0,1),(1,0,0),(2,3,0)):
        add(f'spray/2/{t0}/{t1}/{hard}',0x1486E0,[(A+6,b(2)),(ST+0x28,w(t0)),(ST+0x2C,w(t1)),(FLAG_70A,b(hard))])
    # ---- 00148B40: the aimed spray
    add('aim/9',0x148B40,[(A+6,b(9))])
    for d in (0,0x80):
        add(f'aim/0/{d}',0x148B40,[(A+0xD,b(d))])
        add(f'aim/3/{d}',0x148B40,[(A+6,b(3)),(ST+0x20,w(0x1000 if d else 0))])
    for bit in (0,1):
        add(f'aim/1/{bit}',0x148B40,[(A+6,b(1)),(ST+0x20,w(bit*0x1000))],{0x122BB8:[0xFF,0xF0]})
    S2=(A+6,b(2))
    for d in (0,0x80):
        for hard in (0,1):
            add(f'aim/2/fx/{d}/{hard}',0x148B40,[S2,(A+0xD,b(d)),(FLAG_70A,b(hard)),(ST+0x68,f(100.)),(ST+0x30,w(2))])
            add(f'aim/2/fx1/{d}/{hard}',0x148B40,[S2,(A+0xD,b(d)),(FLAG_70A,b(hard)),(ST+0x68,f(100.)),(ST+0x28,w(1)),(ST+0x2C,w(1))])
    for pitch in (-0.3,0.3):
        for yaw in (-0.3,0.3):
            add(f'aim/2/blend/{pitch}/{yaw}',0x148B40,[S2,(ST+0x54,f(pitch)),(ST+0x5C,f(pitch)),(ST+0x58,f(yaw)),(ST+0x64,f(0.01)),
                                                       (ST+0x68,f(100.)),(ST+0x30,w(1))])
    add('aim/2/bones/0',0x148B40,[S2,(A+0xC,b(0)),(ST+0x68,f(100.))])
    add('aim/2/bones/3',0x148B40,[S2,(A+0xC,b(3)),(ST+0x68,f(100.))])
    add('aim/2/aim/flag',0x148B40,[S2,(ST+0x58,f(-0.5)),(ST+0x60,f(0.)),(ST+0x68,f(100.))])
    for yaw,inside in ((0.9,1),(0.9,0),(-0.9,0)):
        add(f'aim/2/turnlimit/{yaw}/{inside}',0x148B40,[S2,(ST+0x58,f(yaw)),(ST+0x64,f(0.01)),(ST+0x68,f(100.))],{0x1B1560:[inside]})
    for high,pitch in ((1000.,0),(-1000.,0)):
        add(f'aim/2/pitch/{high}',0x148B40,[S2,(A+0xB4,f(high)),(ST+0x68,f(100.))])
    for frame in (0.,23.,24.,25.):
        add(f'aim/2/frame/{frame}',0x148B40,[S2,(A+0x3C,f(frame)),(ST+0x68,f(100.))])
    for weight in (0.5,1.5):
        add(f'aim/2/weight/{weight}',0x148B40,[S2,(ST+0x54,f(weight)),(ST+0x5C,f(weight)),(ST+0x58,f(weight)),(ST+0x64,f(0.)),(ST+0x68,f(100.))])
    for flag,hit in ((1,0),(0,0),(0,1)):
        add(f'aim/2/near/{flag}/{hit}',0x148B40,[S2,(ST+0x68,f(20.)),(FLAG_4E4,b(flag))],{0x19A570:[hit]})
    for left in (0,1):
        for d in (0,0x80):
            add(f'aim/2/left/{left}/{d}',0x148B40,[S2,(ST+0x68,f(100.)),(ST+0x30,w(left)),(A+0xD,b(d))])
    PI4F=0.7853981852531433
    for pitch in (-0.25,0.25):
        add(f'aim/2/zero58/{pitch}',0x148B40,[S2,(ST+0x54,f(pitch)),(ST+0x5C,f(pitch)),(ST+0x58,f(0.25)),(ST+0x64,f(-0.25)),(ST+0x68,f(100.))])
    add('aim/2/equal60',0x148B40,[S2,(ST+0x58,f(0.25)),(ST+0x68,f(100.))],{0x1B1470:[O.fbits(0.25)]},run=MATH-{0x1B1470})
    add('aim/2/zero54',0x148B40,[S2,(ST+0x64,f(0.5)),(ST+0x68,f(100.))],{0x1B12B0:[0]},run=MATH-{0x1B12B0})
    add('aim/2/limit/equal',0x148B40,[S2,(ST+0x64,f(0.5)),(ST+0x68,f(100.))],{0x11DF78:[O.fbits(PI4F),0,0]},run=MATH-{0x11DF78})
    for v in (PI4F,-PI4F,0.8,-0.8):
        add(f'aim/2/pitchclamp/{v}',0x148B40,[S2,(ST+0x64,f(0.5)),(ST+0x68,f(100.))],{0x1B1470:[O.fbits(v)]},run=MATH-{0x1B1470})
    for wy,wp in ((1.0,1.0),(1.5,1.5),(0.5,1.5)):
        add(f'aim/2/weights/{wy}/{wp}',0x148B40,[S2,(ST+0x64,f(0.5)),(ST+0x68,f(100.))],{0x11DF78:[0,O.fbits(wy),O.fbits(wp)]},run=MATH-{0x11DF78})
    # ---- 00149B50: the grab
    add('grab/0',0x149B50); add('grab/9',0x149B50,[(A+6,b(9))])
    for state in (1,4):
        for bit in (0,1):
            add(f'grab/{state}/{bit}',0x149B50,[(A+6,b(state)),(ST+0x20,w(bit*0x1000))])
    for frame in (6.,7.):
        add(f'grab/2/frame/{frame}',0x149B50,[(A+6,b(2)),(A+0x3C,f(frame))])
    G=[(A+6,b(2)),BIT,(0x810354,f(3.)),(A+0xB4,f(3.))]
    add('grab/2/held',0x149B50,G,{0x21BED0:[1]})
    add('grab/2/blocked',0x149B50,G,{0x19A440:[1]})
    add('grab/2/clearflag',0x149B50,G+[(A+0xD,b(1))])
    add('grab/2/high',0x149B50,G+[(A+0xB4,f(10.))])
    add('grab/2/edge',0x149B50,G+[(A+0xB4,f(9.))],{0x1B1EA0:[0]})
    add('grab/2/miss',0x149B50,G,{0x1B1EA0:[0]})
    add('grab/2/hit',0x149B50,G,{0x1B1EA0:[1]})
    for d in (0,0x80):
        for hard in (0,1):
            add(f'grab/3/{d}/{hard}',0x149B50,[(A+6,b(3)),(A+0x3C,f(14.)),(A+0xD,b(d)),(FLAG_70A,b(hard))])
    for hold in (0,1,2):
        for bit in (0,1):
            add(f'grab/3/hold/{hold}/{bit}',0x149B50,[(A+6,b(3)),(A+0x3C,f(14.)),(FLAG_6BD,b(hold)),(ST+0x20,w(bit*0x1000))])
    add('grab/3/frame13',0x149B50,[(A+6,b(3)),(A+0x3C,f(13.))])
    # ---- 0014A0D0: the lunge
    add('lunge/0',0x14A0D0); add('lunge/9',0x14A0D0,[(A+6,b(9))])
    for state in (1,3):
        for bit in (0,1):
            add(f'lunge/{state}/{bit}',0x14A0D0,[(A+6,b(state)),(ST+0x20,w(bit*0x1000))])
    for frame in (6.,7.):
        add(f'lunge/2/frame/{frame}',0x14A0D0,[(A+6,b(2)),(A+0x3C,f(frame)),(ST+0x28,w(1))])
    add('lunge/2/done',0x14A0D0,[(A+6,b(2)),(ST+0x28,w(1)),BIT])
    add('lunge/2/immune',0x14A0D0,[(A+6,b(2))],{0x21BE40:[1]})
    add('lunge/2/miss',0x14A0D0,[(A+6,b(2))],{0x1A7B80:[0]})
    for d in (0,0x80):
        add(f'lunge/2/hit/{d}',0x14A0D0,[(A+6,b(2)),(A+0xD,b(d))],{0x1A7B80:[1]})
    # ---- 0014A350: the mutation
    add('mutate/0',0x14A350); add('mutate/9',0x14A350,[(A+6,b(9))])
    for bit in (0,1):
        add(f'mutate/1/{bit}',0x14A350,[(A+6,b(1)),(ST+0x20,w(bit*0x1000))],{0x122BB8:[0xFF<<11]})
        for scale,bits in ((1.15,1),(1.14,1),(1.15,0),(1.16,0)):
            add(f'mutate/3/{bit}/{scale}/{bits}',0x14A350,[(A+6,b(3)),(ST+0x20,w(bit*bits*0x1000)),(A+0x60,f(scale)),(A+0x64,f(scale)),(A+0x68,f(scale))])
    add('mutate/3/exact',0x14A350,[(A+6,b(3)),(A+0x60,f(1.15))],run=MATH-{0x1028B8})
    for left in (0,2):
        add(f'mutate/2/{left}',0x14A350,[(A+6,b(2)),(ST+0x28,w(left))])
    for bit in (0,1):
        for hard in (0,1):
            add(f'mutate/4/{bit}/{hard}',0x14A350,[(A+6,b(4)),(ST+0x20,w(bit*0x1000)),(FLAG_70A,b(hard))])
    # ---- 0014A620: hurt
    for state in (0,1,2,3):
        for dirty,hold in ((0,0),(1,0),(1,1)):
            add(f'hurt/{state}/{dirty}/{hold}',0x14A620,[(A+5,b(state)),(A+6,b(9)),(ST+0x86,b(dirty)),(FLAG_6BD,b(hold))],{0x1C64F0:[0x1000]})
    # ---- 0014A730: the flinch
    for hit in (0,0x8000):
        for carry in (0,1):
            for bit in (0,1):
                add(f'flinch/0/{hit}/{carry}/{bit}',0x14A730,[(A+0x36,h(hit)),(ST+0x87,b(carry)),(ST+0x20,w(bit*0x1000)),(ST+0x68,f(5.))],{0x122BB8:[3<<17]})
    for r,n in ((0,0),(1,1),(2,2),(-1,3),(-2,4)):
        add(f'flinch/0/roll/{n}',0x14A730,queues={0x122BB8:[(r<<17)&0xFFFFFFFF]})
    for carry in (0,1):
        for bit in (0,1):
            for dist in (20.,21.):
                add(f'flinch/1/{carry}/{bit}/{dist}',0x14A730,[(A+6,b(1)),(ST+0x87,b(carry)),(ST+0x20,w(bit*0x1000)),(ST+0x68,f(dist))])
    add('flinch/3',0x14A730,[(A+6,b(3))])
    for frame in (45.,46.):
        for bit in (0,1):
            for dist in (20.,21.):
                add(f'flinch/2/{frame}/{bit}/{dist}',0x14A730,[(A+6,b(2)),(A+0x3C,f(frame)),(ST+0x87,b(1)),(ST+0x20,w(bit*0x1000)),(ST+0x68,f(dist))])
    # ---- 0014A9A0: thrown
    add('thrown/3',0x14A9A0,[(A+6,b(3))])
    for side in (0,1):
        for back in (0,1):
            add(f'thrown/0/{side}/{back}',0x14A9A0,[(A+0x70,f(1.)),(A+0x78,f(1.)),(A+0xC4,f(3.*back))],
                {0x122BB8:[0x7F<<19,side<<12]})
    for v in (1.5707963705062866,1.5):
        add(f'thrown/0/side/{v}',0x14A9A0,queues={0x11DF78:[O.fbits(v)]},run=MATH-{0x11DF78})
    for flag,bit,health in ((0,0,0),(2,0,0),(2,1,0),(2,1,5)):
        add(f'thrown/1/{flag}/{bit}/{health}',0x14A9A0,[(A+6,b(1)),(ST+0x81,b(flag)),(ST+0x20,w(bit*0x1000)),(A+0x34,h(health)),(ST+0x44,f(0.1))])
    for bit in (0,1):
        for dist in (20.,21.):
            add(f'thrown/2/{bit}/{dist}',0x14A9A0,[(A+6,b(2)),(ST+0x20,w(bit*0x1000)),(ST+0x68,f(dist))])
    # ---- 0014ACC0: death
    for state in (2,3):
        add(f'death/{state}',0x14ACC0,[(A+6,b(state))])
    for carry in (0,1):
        add(f'death/0/{carry}',0x14ACC0,[(ST+0x87,b(carry)),(A+0x9A,b(5))])
    for clip,frame in ((0x31,54.),(0x31,53.),(0x8031,54.),(0x30,54.)):
        add(f'death/1/{clip}/{frame}',0x14ACC0,[(A+6,b(1)),(A+0x2C,h(clip)),(A+0x3C,f(frame))])
    for done in (0,1):
        add(f'death/1/end/{done}',0x14ACC0,[(A+6,b(1)),BIT],{0x1EFE00:[done]})
    # ---- 0014AE50, 0014AE80
    add('corpse',0x14AE50,[(A+0x9A,b(9))])
    for flag,pause,dist,hit in ((1,0,1.,0),(0,1,1.,0),(0,0,1.,1),(0,0,1.,0),(0,0,10.,1),(0,0,11.,1),(0x80,0,1.,1)):
        add(f'motion/{flag}/{pause}/{dist}/{hit}',0x14AE80,[(ST+0x80,b(flag)),(ST+0x8C,b(pause)),(ST+0x68,f(dist)),(ST+0x48,f(1.)),(ST+0x4C,f(1.)),(ST+0x50,f(0.5)),(A+0xC4,f(0.5))],
            {0x1B1560:[hit]})
    add('motion/worker',0x14AE80,[(ST+0x4C,f(1.)),(A+0xC4,f(0.5)),(ST+0x68,f(50.))],run=MATH-{0x11E2A8,0x11DE90},
        queues={0x11E2A8:[O.Scribble(O.fbits(0.5),[(ST+0x4C,f(3.)),(A+0xB0,O.fw(6.,7.,8.,1.)),(A+0xC4,f(1.5))])],
                0x11DE90:[O.Scribble(O.fbits(0.25),[(ST+0x4C,f(4.)),(A+0xB8,f(9.))])]})
    # ---- 0014AFA0: the collision
    for speed in (-1.,0.,1.):
        for rate in (-1.,-0.1,-0.05,0.,0.5):
            for wall in (0,1):
                add(f'coll/{speed}/{rate}/{wall}',0x14AFA0,[(ST+0x4C,f(speed)),(ST+0x48,f(rate))],{0x1B32F0:[wall,1]})
    for n in range(4):
        add(f'coll/ahead/{n}',0x14AFA0,[(ST+0x48,f(0.5))],{0x1B32F0:[0]*(n+1)+[1]})
    for hits in ((1,),(0,1),(0,0,1),(0,0,0,1),(0,0,0,0)):
        add(f'coll/side/{len(hits)}/{sum(hits)}',0x14AFA0,[(ST+0x48,f(-1.))],{0x1B3390:list(hits)})
    for n in range(5):
        add(f'coll/floor/{n}',0x14AFA0,[(ST+0x48,f(-1.))],{0x1B3440:[0]*n+[1]})
    for phase,state,got,y,rate in ((1,0,1,0.,-1.),(1,3,1,0.,-1.),(1,4,1,0.,-1.),(0,0,1,0.,-1.),(1,0,0,0.,-1.),(1,0,1,20.,-1.),(1,0,1,20.,-0.5),(1,0,1,9.,-1.),(1,0,1,10.,-1.)):
        add(f'coll/fall/{phase}/{state}/{got}/{y}/{rate}',0x14AFA0,[(A+4,b(phase)),(A+5,b(state)),(A+0xB4,f(y)),(ST+0x48,f(rate)),(0x700038A0,f(0.))],{0x1B2F70:[got]})
    for found,kind,timer,speed in ((0,0x5B,0,1.),(1,0x5A,0,1.),(1,0x5B,1,1.),(1,0x5B,0,0.),(1,0x5B,0,1.)):
        add(f'coll/water/{found}/{kind}/{timer}/{speed}',0x14AFA0,[(ST+0x48,f(-1.)),(contact+0x1A,b(kind)),(ST+0x8E,b(timer)),(ST+0x4C,f(speed))],
            {0x19B6C0:[found],0x122BB8:[7<<9]})
    for hit in (0,1):
        add(f'coll/ceiling/{hit}',0x14AFA0,[(ST+0x48,f(1.))],{0x19AB20:[hit]})
    # ---- 0014B7B0: the way ahead
    def probe(label,flags,patches=(),queues=None):
        add('wayp/'+label,0x14B7B0,[(A+0xB4,f(3.)),(0x700038D0,f(1.))]+list(patches),{0x1B2BF0:[flags],**(queues or {})})
    probe('none',0); probe('wall',4); probe('other',0xFFFFFFF0)
    probe('edge/clear',1,[(A+0xB4,f(3.))]); probe('edge/drop',1,[(A+0xB4,f(100.))])
    probe('slope/high',5,[(0x700038D0,f(100.))]); probe('slope/46',5,[(A+0xB4,f(50.)),(contact+0x1A,b(0x46))])
    probe('slope/equal',5,[(A+0xB4,f(85.)),(0x700038D0,f(100.))],{0x122BB8:[0]})
    probe('edge/equal',1,[(A+0xB4,f(46.)),(0x700038D0,f(1.))])
    for roll,ray,reach in ((0,0,0),(1,1,0),(1,0,1),(1,0,0),(3,0,0)):
        probe(f'slope/{roll}/{ray}/{reach}',5,[(A+0xB4,f(50.))],{0x122BB8:[roll<<14],0x19A570:[ray],0x19AD00:[reach]})
    # ---- 0014BB10: alert
    for armed in (0,1):
        for hit in (0,1):
            for dist in (50.,51.):
                for count in (0,0x76,0x77,0x78,0xFFFFFFFF):
                    add(f'alert/{armed}/{hit}/{dist}/{count}',0x14BB10,[(ST+0x38,w(armed)),(A+0xA,b(hit)),(ST+0x68,f(dist)),(ST+0x3C,w(count))],{0x1B3F10:[1]})
    for seen in (0,1):
        for level in (0,1,2,3,4):
            for count in (0,59,60,0xFF):
                add(f'alert/view/{seen}/{level}/{count}',0x14BB10,[(0x81050C,b(level)),(ST+0x3C,w(count))],{0x1B3F10:[seen]})
    # ---- 0014BC30: spray or pounce
    add('range/calm',0x14BC30)
    SEEN=[(ST+0x38,w(1))]
    add('range/roll',0x14BC30,SEEN,{0x122BB8:[0]})
    add('range/immune',0x14BC30,SEEN,{0x122BB8:[1<<19],0x21BE40:[1]})
    for flag,near,far,line,d,roll in ((0,1,0,0,0,0),(1,0,0,0,0,0),(0,0,0,0,0,0),(0,0,1,0,0,0),(0,0,1,1,0,0),(1,0,1,1,0,0),(1,0,1,1,0x80,3<<18),(1,0,1,1,0x80,2<<18),(1,0,1,1,0x80,7<<18)):
        add(f'range/{flag}/{near}/{far}/{line}/{d}/{roll}',0x14BC30,SEEN+[(FLAG_4E4,b(flag)),(A+0xD,b(d))],
            {0x122BB8:[1<<19,roll],0x1B13F0:[near,far] if flag!=1 else [far],0x1B1560:[line]})
    # ---- 0014BE20: damage
    D=[(A+0x34,h(100)),(A+0x36,h(10))]
    add('dmg/dead',0x14BE20,[(A+0x36,h(10))]); add('dmg/none',0x14BE20,[(A+0x34,h(100))])
    add('dmg/busy85',0x14BE20,D+[(ST+0x85,b(1))]); add('dmg/busy86',0x14BE20,D+[(ST+0x86,b(1))])
    for health in (1,10,11,100,0x7FFF):
        for hit in (10,0x8000|10,0x4000|10,0x2000|10,0xE000|0xFFF,0x1000|10):
            for phase in (1,2):
                for spare in (0,1):
                    add(f'dmg/{health}/{hit:x}/{phase}/{spare}',0x14BE20,[(A+0x34,h(health)),(A+0x36,h(hit)),(A+4,b(phase)),(ST+0x70,h(spare)),(ST+0x72,h(spare*20)),(ST+0x89,b(spare))])
    for carry,bump,hit in ((1,0,10),(0,0,10),(0,2,10),(0,2,0x2000|10)):
        add(f'dmg/kill/{carry}/{bump}/{hit:x}',0x14BE20,[(A+0x34,h(5)),(A+0x36,h(hit)),(ST+0x87,b(carry)),(ST+0x81,b(bump))])
    for carry in (0,1):
        add(f'dmg/down/{carry}',0x14BE20,[(A+0x34,h(100)),(A+0x36,h(0x2000|10)),(ST+0x87,b(carry)),(A+4,b(1))])
    for pile,bump in ((24,0),(25,0),(25,2),(0,0),(0,2)):
        add(f'dmg/stagger/{pile}/{bump}',0x14BE20,[(A+0x34,h(100)),(A+0x36,h(10)),(A+4,b(1)),(ST+0x70,h(1)),(ST+0x72,h(pile)),(ST+0x81,b(bump))])
    add('dmg/stagger/heavy/2',0x14BE20,[(A+0x34,h(100)),(A+0x36,h(0x8000|1)),(A+4,b(1)),(ST+0x81,b(2))])
    add('dmg/wrap',0x14BE20,[(A+0x34,h(0x7000)),(A+0x36,h(0x8000|0xFFF)),(A+4,b(1))])
    # ---- 0014C0B0: the mutation gate
    base=dict(d=0,t=0,a=1,h=1,f=2,p=1,s=2)
    def gate(label,**kw):
        v={**base,**kw}
        add('gate/'+label,0x14C0B0,[(A+0xD,b(v['d'])),(ST+0x74,h(v['t'])),(ST+0x38,w(v['a'])),(A+0x54,h(v['h'])),(ST+0x81,b(v['f'])),(A+4,b(v['p'])),(A+5,b(v['s']))])
    gate('go'); gate('d',d=0x80); gate('t',t=1); gate('a',a=0); gate('h',h=0); gate('f',f=1); gate('p',p=2); gate('s',s=3); gate('s0',s=0); gate('s80',s=0x80)
    # ---- 0014C130: clear reach
    add('reach/flag',0x14C130,[(A+0xD,b(1))])
    for n in range(4):
        add(f'reach/{n}',0x14C130,queues={0x19A440:[0]*n+[1]})
    # ---- bulk: seeded random states over every entry
    rng=random.Random(0x147390)
    boolean=(0x19A440,0x19A570,0x19AB20,0x19AD00,0x19B6C0,0x1A7B80,0x1B10B0,0x1B13F0,0x1B1560,0x1B1EA0,0x1B2F70,0x1B3250,
             0x1B32F0,0x1B3390,0x1B3440,0x1B3F10,0x1EFE00,0x21BE40,0x21BED0)
    entries=sorted(FUNCS)
    floats=(-1000.,-20.,-1.,-0.5,-0.05,0.,0.05,0.5,1.,7.,14.,20.,45.,54.,100.)
    for i in range(O.reference_mode.pick(4000,350)):
        e=entries[i%len(entries)]
        p=[(A+4,b(rng.choice((0,1,1,2,3)))),(A+5,b(rng.randrange(11))),(A+6,b(rng.randrange(6))),
           (A+0xA,b(rng.randrange(2))),(A+0xD,b(rng.choice((0,0,0x80,1,0x81)))),(A+0x2C,h(rng.choice((0,2,0x31,0x8031)))),
           (A+0x34,h(rng.choice((0,1,100)))),(A+0x36,h(rng.choice((0,10,0xE00A,0x8005)))),(A+0x3C,f(rng.choice((0.,7.,14.,24.,45.,54.)))),
           (A+0x54,h(rng.randrange(2))),(A+0x56,h(rng.choice((0,0,25)))),(A+0x60,f(rng.choice((1.,1.15)))),(A+0xC4,f(rng.choice((0.,1.,-3.))))]
        for off in (0x20,0x28,0x2C,0x30,0x38,0x3C):
            p.append((ST+off,w(rng.choice((0,0,1,0x1000,0x78)))))
        for off in (0x44,0x48,0x4C,0x50,0x54,0x58,0x5C,0x60,0x64,0x68):
            p.append((ST+off,f(rng.choice(floats))))
        for off in (0x70,0x72,0x74):
            p.append((ST+off,h(rng.choice((0,1,30)))))
        for off in (0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8C,0x8D,0x8E):
            p.append((ST+off,b(rng.choice((0,0,1,2,3,0x80,0xFF)))))
        p+=[(FLAG_70A,b(rng.randrange(2))),(FLAG_6BD,b(rng.randrange(3))),(FLAG_4E4,b(rng.randrange(2))),(0x700038D0,f(rng.choice(floats))),
            (0x70003B68,w(rng.randrange(0x80))),(0x70003B8A,h(rng.randrange(2))),(contact+0x1A,b(rng.choice((0x46,0x5B,0)))),
            (MODE,b(rng.choice((0,0,1,4))))]
        q={a:[rng.randrange(2) for _ in range(8)] for a in boolean}
        q[0x122BB8]=[rng.getrandbits(32) for _ in range(8)]
        q[0x1B2140]=[rng.randrange(4)>0]
        q[0x1B2BF0]=[rng.choice((0,1,4,5))]
        q[0x1C64F0]=[rng.choice((0,9,0x1000,0x1009))]
        q={a:[int(v) for v in values] for a,values in q.items()}
        add(f'random/{i}',e,p,q)
    return out


def header_checks():
    header=(ROOT/'src/game/em_level16_beast.h').read_text()
    block=header.split('/* BEGIN GENERATED HOOKS */')[1].split('/* END GENERATED HOOKS */')[0]
    found=re.findall(r'int \(\*(w_[0-9A-F]{8})\)\(void \*ctx((?:, [^,)]+)*)\);',block)
    assert [n for n,_ in found]==[h[0] for h in HOOKS]
    types={U:'uint32_t',I:'int32_t',F:'float',Q:'uint64_t'}
    for (name,params),(expected,address,args,result) in zip(found,HOOKS):
        actual=[re.sub(r'\s*\b\w+$','',p.strip()).replace(' ','') for p in params.split(',')[1:]]
        assert actual==[types[k] for k,_ in args]+([types[result]+'*'] if result else [])
    internal=(ROOT/'src/game/em_level16_beast_internal.h').read_text()
    for name,address,_,_ in HOOKS:
        bodies=re.findall(r'static inline int l16b_c_%s\(.*?\n}\n'%name[2:],internal,re.S)
        assert len(bodies)==1 and f'o->h->{name}(' in bodies[0] and f'0x{address:08X}u' in bodies[0]
    for entry,(symbol,_,kinds,result) in FUNCS.items():
        assert re.search(r'^int %s\(.*\);$'%symbol,header,re.M),symbol


def entry_guards():
    touched=[]
    def refuse(*_): touched.append(1); return -1
    keep=[O.BYTES_FN(refuse),O.CALLBACK_FN(refuse)]
    fields={'ctx':None,'bytes':keep[0],'w_callback':keep[1]}
    for name,_,args,result in HOOKS:
        fields[name]=O.hook_proto(args,result)(refuse); keep.append(fields[name])
    hooks=O.Hooks(**fields)
    for entry,(symbol,_,_,kind) in FUNCS.items():
        for table,fault,with_result in ((C.byref(hooks),O.Fault(0x1234,7),True),
                                       (None,O.Fault(),True),(C.byref(hooks),None,True),
                                       (C.byref(hooks),O.Fault(),False)):
            if not with_result and kind is None: continue
            out=C.c_int32(0x5A5A5A5A); touched.clear()
            args=[table]+O.native_args(entry,O.DEFAULT_ARGS[entry])
            if kind is not None: args.append(C.byref(out) if with_result else None)
            rc=getattr(O.NATIVE,symbol)(*args,C.byref(fault) if fault is not None else None)
            assert rc==-1 and not touched and out.value==0x5A5A5A5A
            if fault is not None:
                assert (fault.address,fault.code)==((0x1234,7) if table is not None and with_result else (0,0))


def trace(label_prefix,selected):
    for case in selected:
        if not case[0].startswith(label_prefix): continue
        label,beat,entry,args,patches,spad,script,run=case
        ram0,spad0=O.CAPTURES[beat]
        ram,sp=O.patched(ram0,patches),O.patched(spad0,spad)
        oracle=O.Oracle(ram,sp,entry,(args,O.callbacks_of(entry,args,ram)),O.copy_script(script),run,set())
        print('==',label)
        for t in oracle.trace:
            print(' ',t[0],*(hex(x) if isinstance(x,int) else x for x in t[1:]))


def main():
    configure()
    O.NATIVE=O.build(os.environ.get('EM_LEVEL16_BEAST_SOURCE'))
    header_checks()
    entry_guards()
    selected=cases()
    only=os.environ.get('EM_LEVEL16_BEAST_ONLY')
    if only: selected=[c for c in selected if c[0].startswith(only)]
    O.CASES[:]=selected
    assert len({c[0] for c in selected})==len(selected)
    if os.environ.get('EM_LEVEL16_BEAST_TRACE'):
        trace(os.environ['EM_LEVEL16_BEAST_TRACE'],selected); return
    results=O.run_cases(selected)
    failures=[(label,errors) for label,_,errors,_,_,_ in results if errors]
    seen=set().union(*(r[3] for r in results))
    for entry,(_,size,_,_) in FUNCS.items():
        words=O.reachable_words(O.image_for(entry),entry,size)
        missing=words-seen-DEAD
        print(f'{entry:08X}: {len(words&seen)}/{len(words-DEAD)} reachable words')
        if missing and not only: failures.append(('coverage',[hex(entry),[hex(m) for m in sorted(missing)]]))
    if not only:
        assert not DEAD&seen,'a word called dead ran'
        failures+=[('callee inventory',p) for p in O.callee_checks()]
        if not failures:
            sites,missing=O.hook_sites(results)
            if missing: failures.append(('unreached hooks',missing))
            for problems,runs in O.reference_mode.parallel_map(O.hook_contract_site,sites):
                if problems: failures.append(('fail-stop contract',problems))
    for label,errors in failures[:int(os.environ.get('EM_LEVEL16_BEAST_SHOW','8'))]: print('FAIL',label,errors)
    assert not failures,f'{len(failures)} failures'
    O.reference_mode.banner(f'{len(selected)} cases')
    print(f'PASS {len(selected)} cases: calls, ordered accesses, whole memory, poisoned starts and fail-stop')


if __name__=='__main__': main()
