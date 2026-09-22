#!/usr/bin/env python3
"""Compare AREA11 effect controller/particles/projection with original instructions.

Requires the user's original ELF, AREA11 overlay and immutable opening EE/VU
captures. No original data is embedded or downloaded. Rendering tests cover
finite arithmetic and GIF submission, not a complete GS rasterizer.
"""
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess

import test_point_light_reference as ee
import test_snow_particles_reference as vu
import test_snow_projection_reference as projection

ROOT = Path(__file__).resolve().parents[1]


class Effect(C.Structure):
    _fields_ = [('state',C.c_uint8),('flags',C.c_uint8),
                ('phase',C.c_float),('seed',C.c_float),
                ('sound_handle',C.c_int32),('contact_cooldown',C.c_int32),
                ('half_extent',C.c_float*3)]


def project_original(state, particle):
    mem = bytearray(16384)
    mem[0x6e0:0x7d0] = bytes(state)
    struct.pack_into('<I',mem,0x630,1)
    struct.pack_into('<I',mem,0x650,0x100)
    for field,offset in [('position',0),('color',2),('half_size',3)]:
        mem[0x880+offset*16:0x890+offset*16] = bytes(getattr(particle,field))
    vm = vu.VU(mem,0x231798); vm.stop_on_kick = True
    vm.run(0x231f30,0x232208)
    assert len(vm.kicks) == 1
    base = vm.kicks[0]; count = vm.read(base)[0] & 0x7fff
    assert count in (0,1)
    result = {'clip':vm.clips[0], 'drawn':bool(count)}
    if count:
        result.update(xyzf=[vm.read(base+4),vm.read(base+6)],
                      color=vm.read(base+2),
                      st=[vm.read(base+3)[:2],vm.read(base+5)[:2]])
    return result


def main():
    decomp = ROOT.parent/'Extermination'
    elf = (decomp/'config/SCUS_971.12').read_bytes()
    overlay = (decomp/'extract/OVERLAY/AREA11.BIN').read_bytes()
    ram = (decomp/'build/startup-reference/opening_ee.bin').read_bytes()
    captured = (decomp/'build/weather_reference/original_vu1.bin').read_bytes()
    vu.ELF = elf
    output = ROOT/'build/area11_effect_reference'; output.mkdir(parents=True,exist_ok=True)
    library = output/'area11_effect.dylib'
    sources = ['em_area11_effect.c','em_snow_particles.c','em_snow_projection.c']
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
                    '-shared','-fPIC','-Isrc',*[str(Path('src/game')/s) for s in sources],
                    '-o',str(library)],cwd=ROOT,check=True)
    api = C.CDLL(str(library))
    random_fn = C.CFUNCTYPE(C.c_uint32,C.c_void_p)
    callback_fn = C.CFUNCTYPE(None,C.c_void_p,C.c_int,C.POINTER(Effect))
    api.em_area11_effect_tick.argtypes = [C.POINTER(Effect),random_fn,callback_fn,C.c_void_p]
    api.em_area11_effect_contact.argtypes = [C.POINTER(Effect),C.c_uint8,C.c_int,C.POINTER(C.c_uint8)]
    rng = random.Random(0x8235f0); controller_cases = contact_cases = 0
    actor, owner, target = 0x500000,0x501000,0x502000
    # Runtime loader preserves the MWo3 header at823500; canonical overlay
    # assembly labels are lower by40 and are deliberately not used here.
    assert overlay[0xf0:0x2c0] == ram[0x8235f0:0x8237c0]
    assert overlay[0x4e40:0x4ed0] == ram[0x828340:0x8283d0]
    for state in range(7):
        for _ in range(40):
            item = Effect(state,rng.randrange(256),rng.uniform(-2,4),rng.random(),
                          rng.randrange(-1,48),rng.choice([0,1,60,-1]),
                          (C.c_float*3)(1,2,3))
            random_value = rng.randrange(0x80000000)
            original = ee.Oracle(elf,[random_value]); original.write(0x823500,overlay)
            original.save(actor+0x14,owner)
            def load_effect():
                original.save(actor,item.flags,1); original.save(actor+4,item.state,1)
                original.write(actor+0x1f0,bytes(item.half_extent))
                original.write(actor+0x204,struct.pack('<2f2i',item.phase,item.seed,item.sound_handle,item.contact_cooldown))
            load_effect(); expected_calls=[]; actual_calls=[]
            for address in [0x1029c0,0x102c58,0x102918]:
                original.calls[address] = lambda vm,address=address: expected_calls.append(('matrix',address))
            original.calls[0x1d04b0] = lambda vm: expected_calls.append(('draw',vm.r[5],vm.r[6],vm.f[12],vm.f[13]))
            original.calls[0x1fc3c0] = lambda vm: expected_calls.append(('sound',vm.r[6],vm.f[12],vm.f[13]))
            original.calls[0x1b17a0] = lambda vm: expected_calls.append(('contacts',vm.load(actor,1),vm.load(actor+0x210)))
            original.calls[0x1fc520] = lambda vm: expected_calls.append(('stop',))
            original.calls[0x1afc10] = lambda vm: expected_calls.append(('free',))
            original.run(0x8235f0,[actor])
            used=[]
            @random_fn
            def next_random(_): used.append(random_value); return random_value
            @callback_fn
            def callback(_,call,pointer):
                effect=pointer.contents
                if call==0:
                    actual_calls.extend(('matrix',a) for a in [0x1029c0,0x102c58,0x102918])
                elif call==1: actual_calls.append(('draw',1,0x828340,ee.bits(effect.phase),ee.bits(effect.seed)))
                elif call==2: actual_calls.append(('sound',0x413,ee.bits(100),ee.bits(4096)))
                elif call==3: actual_calls.append(('contacts',effect.flags,effect.contact_cooldown&0xffffffff))
                elif call==4: actual_calls.append(('stop',))
                elif call==5: actual_calls.append(('free',))
            api.em_area11_effect_tick(C.byref(item),next_random,callback,None)
            assert actual_calls==expected_calls,('calls',state,actual_calls,expected_calls)
            assert len(used)==original.rng_calls
            assert item.state==original.load(actor+4,1) and item.flags==original.load(actor,1)
            assert bytes(item.half_extent)==original.read(actor+0x1f0,12)
            assert struct.pack('<2f2i',item.phase,item.seed,item.sound_handle,item.contact_cooldown)==original.read(actor+0x204,16)
            controller_cases += 1
    for flags in range(256):
        for blocked in [0,1,2]:
            item = Effect(); item.contact_cooldown = -1
            original = ee.Oracle(elf); original.write(0x823500,overlay)
            original.save(actor+0x210,-1); original.save(target,flags,1); original.save(target+15,33,1)
            events=[]
            original.calls[0x21bb00] = lambda vm: vm.r.__setitem__(2,blocked)
            original.calls[0x1efe00] = lambda vm: events.append((vm.r[4],vm.r[5]))
            original.run(0x823580,[actor,target])
            reaction=C.c_uint8(33)
            accepted=api.em_area11_effect_contact(C.byref(item),flags,blocked,C.byref(reaction))
            assert bool(accepted)==bool(events)
            if accepted: assert events==[(0x80000027,target)]
            assert reaction.value==original.load(target+15,1)
            assert item.contact_cooldown==ee.signed(original.load(actor+0x210))
            contact_cases+=1
    start=lambda address: address-0x100000+0x300
    assert elf[start(0x231798):start(0x231f10)]==elf[start(0x233828):start(0x233fa0)]
    assert captured[0x500:0x590]==ram[0x828340:0x8283d0]
    assert captured[0x5a0:0x5e0]==ram[0x7a8610:0x7a8650]
    lookup=struct.unpack_from('<80f',elf,start(0x2342bc))
    assert all(captured[i*16:i*16+4]==struct.pack('<f',lookup[i]) for i in range(80))
    out=(vu.Particle*80)(); fp=C.POINTER(C.c_float)
    generate=api.em_snow_particles_generate
    generate.argtypes=[fp,fp,fp,fp,C.POINTER(vu.Particle),C.c_uint]
    descriptor=(C.c_float*36).from_buffer_copy(captured[0x500:0x590])
    params=(C.c_float*4).from_buffer_copy(captured[0x590:0x5a0])
    matrix=(C.c_float*16).from_buffer_copy(captured[0x5a0:0x5e0])
    count=generate(descriptor,(C.c_float*80)(*lookup),params,matrix,out,80)
    assert count==80
    vm=vu.VU(captured,0x231798); vm.run(0x231798,0x231f30)
    while vm.read(0x62)[0]:
        vm.vi[9]=(0x231f30-0x231798)//8; vm.run(0x231880,0x231f30)
    last=vm.vi[8]; assert last==24
    captured_bytes=0
    for i in range(last):
        for field,offset in [('position',0),('color',2),('half_size',3)]:
            expected=captured[(0x88+4*i+offset)*16:(0x89+4*i+offset)*16]
            assert bytes(getattr(out[count-last+i],field))==expected==struct.pack('<4I',*vm.read(0x88+4*i+offset))
            captured_bytes+=16
    cases=projection.synthetic_cases()
    captured_projection=projection.Projection.from_buffer_copy(captured[0x6e0:0x7d0])
    cases.extend(('captured_effect',captured_projection,out[i]) for i in range(count))
    project=api.em_effect_sprite_project
    project.argtypes=[C.POINTER(projection.Projection),C.POINTER(vu.Particle),C.POINTER(projection.Projected)]
    projection_bytes=0; submitted=0
    for kind,state,item in cases:
        expected=project_original(state,item); actual=projection.Projected()
        drawn=project(C.byref(state),C.byref(item),C.byref(actual))
        assert bool(drawn)==expected['drawn'],('clip',kind)
        submitted+=drawn
        for field in ['clip']+(['xyzf','color','st'] if drawn else []):
            value=expected[field]
            words=value if field in ('clip','color') else [x for row in value for x in row]
            assert bytes(getattr(actual,field))==struct.pack('<'+'I'*len(words),*words),(field,kind)
            projection_bytes+=len(words)*4
    report={'status':'PASS','controller_cases':controller_cases,'contact_cases':contact_cases,
            'shared_original_generator_bytes':0x778,'captured_particle_records':last,
            'captured_particle_bytes':captured_bytes,'projection_cases':len(cases),
            'projection_submissions':submitted,'projection_bytes':projection_bytes,
            'original_elf_sha256':hashlib.sha256(elf).hexdigest(),
            'original_overlay_sha256':hashlib.sha256(overlay).hexdigest(),
            'limitations':['Finite arithmetic; no GS raster-output proof.',
                           'Contact candidate selection and effect80000027 require separate integration.',
                           'Global RNG order, actor schedule and audio SPU envelopes are separate fidelity dependencies.']}
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))


if __name__=='__main__': main()
