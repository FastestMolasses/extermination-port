#!/usr/bin/env python3
"""Original208AD0/209280/209860 status draw preparation versus readable native C."""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import struct
import subprocess
import sys
from export_status_hub import Original, ELF_SHA, UI
from test_point_light_reference import bits, number
from status_ammo_source_probe import compile_readable_ammo
from reference_mode import MODE, banner, part, select

ROOT=Path(__file__).resolve().parents[1]
class Data(C.Structure):
    _fields_=[('arcs',(C.c_float*24)*4),('white',C.c_uint64),('red',C.c_uint64),
              ('label_width',C.c_int),('label',C.c_char_p),('warning_max',C.c_char_p),
              ('normal_max',C.c_char_p),('separator',C.c_char_p)]
Blend=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint)
Rect=C.CFUNCTYPE(C.c_int,C.c_void_p,*([C.c_int]*4),C.c_uint32)
Text=C.CFUNCTYPE(C.c_int,C.c_void_p,*([C.c_int]*5),C.c_char_p,C.c_uint64)
Arc=C.CFUNCTYPE(C.c_int,C.c_void_p,C.POINTER(C.c_float))
Sprite=C.CFUNCTYPE(C.c_int,C.c_void_p,*([C.c_int]*4),C.c_uint32,C.c_uint64)
class Workers(C.Structure):
    _fields_=[('context',C.c_void_p),('blend',Blend),('rectangle',Rect),('text',Text),('arc',Arc),('sprite',Sprite)]

class BatteryData(C.Structure):
    _fields_=[('white',C.c_uint64),('label',C.c_char_p),('separator',C.c_char_p)]

class AmmoData(C.Structure):
    _fields_=[('white',C.c_uint64),('label',C.c_char_p),('percent',C.c_char_p)]

class AmmoInventory(C.Structure):
    _fields_=[('primary',C.c_uint8),('secondary',C.c_uint8),('amount',C.c_int16*5),('reserve',C.c_int16)]


def canonical(command):
    kind=command['kind']
    if kind=='text':return (kind,int(command['proportional']),*command['xywh'],command['value'],
                            command['style'][0]|command['style'][1]<<32)
    if kind=='rectangle':return (kind,*command['xyxy'],command['rgba'])
    if kind=='arc':return (kind,struct.pack('<24f',*command['descriptor']))
    if kind=='sprite':return (kind,*command['xywh'],command['rgba'],command['tex0'])
    raise AssertionError(kind)


def capture_calls(o):
    calls=[]
    # Unlike export metadata, this comparison preserves every mode call.
    o.calls[0x207d00]=lambda a:calls.append(('blend',a.r[5]))
    # Compare the formatter's bytes, including out-of-range inventory probes;
    # an overfull original numeric field can emit a non-ASCII byte.
    o.calls[0x1cba50]=lambda a:calls.append(('text',0,
        *[v&0xffffffff for v in a.r[5:9]],a.string_bytes(a.r[9]).decode('latin1'),
        a.load(a.r[10],8)))
    for address in (0x207f80,0x2082b0,0x207e40):
        previous=o.calls[address]
        def hook(a,worker=previous):
            worker(a);calls.append(canonical(a.commands[-1]))
        o.calls[address]=hook
    return calls


def expected(elf,ram,counter,health,warning,x,y):
    o=Original(elf,ram,0,0,True)
    o.save(UI+0x20,counter);o.save(0x810858,bits(health));o.save(0x8104e4,warning,1)
    calls=capture_calls(o)
    o.run(0x208ad0,(UI,x,y))
    return o.load(UI+0x20),calls


def main():
    elf=(ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest()==ELF_SHA
    ram=(ROOT.parent/'Extermination/build/startup-reference/status-hub/eeMemory.bin').read_bytes()
    out=ROOT/'build/status_draw_reference';out.mkdir(parents=True,exist_ok=True)
    lib=out/('draw.dylib' if sys.platform=='darwin' else 'draw.so')
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
                    '-fPIC','-dynamiclib' if sys.platform=='darwin' else '-shared','-Isrc',
                    'src/game/em_status_draw.c','-o',str(lib)],cwd=ROOT,check=True)
    native=C.CDLL(str(lib))
    native.em_status_health_draw.argtypes=[C.POINTER(C.c_uint32),C.c_float,C.c_uint8,
                                           C.c_int,C.c_int,C.POINTER(Data),C.POINTER(Workers)]
    native.em_status_battery_draw.argtypes=[C.c_uint16,C.c_uint8,C.c_uint8,C.c_int,C.c_int,
                                           C.c_uint64,C.c_int,C.POINTER(BatteryData),C.POINTER(Workers)]
    native.em_status_ammo_draw.argtypes=[C.POINTER(AmmoInventory),C.c_int,C.c_int,
                                        C.POINTER(AmmoData),C.POINTER(Workers)]
    readable=compile_readable_ammo(out)
    readable.canonical_ammo_draw.argtypes=native.em_status_ammo_draw.argtypes
    o=Original(elf,ram,0,0,True)
    d=Data()
    for i,at in enumerate(range(0x265390,0x265510,0x60)):
        d.arcs[i]=(C.c_float*24).from_buffer_copy(o.read(at,96))
    d.white,d.red=o.load(0x265510,8),o.load(0x265528,8)
    d.label=o.string_bytes(o.load(0x267298))
    o.run(0x1cc170,(o.load(0x267298),));d.label_width=o.r[2]
    d.warning_max,d.normal_max,d.separator=[o.string_bytes(p) for p in (0x273558,0x273560,0x273568)]
    calls=[];failure=[-1,0]
    def emit(event):
        calls.append(event)
        return failure[1] if len(calls)-1==failure[0] else 1
    workers=Workers(None,Blend(lambda _,m:emit(('blend',m))),
        Rect(lambda _,x,y,x1,y1,c:emit(('rectangle',x,y,x1,y1,c))),
        Text(lambda _,p,x,y,w,h,s,c:emit(('text',p,x,y,w,h,s.decode('latin1'),c))),
        Arc(lambda _,a:emit(('arc',C.string_at(a,96)))),
        Sprite(lambda _,x,y,w,h,c,t:emit(('sprite',x,y,w,h,c,t))))
    healths=[0,.01,34.99,35,number(bits(35)+1),59.99,60,number(bits(60)+1),99.99,100]
    health_all=list(itertools.product((0,1,2,59,60,61,0x7fffffff,0xfffffffe,0xffffffff),
                            healths,(0,1,2),((208,196),(0,0),(512,448),(-100,-77))))
    # Quick: every health threshold with every warning, every counter wrap
    # point and every position, the last case (it sizes the failure sweep),
    # plus a fixed-seed sample of the product.
    cases=select(health_all,360,0x208AD0,axes=(lambda c:(c[1],c[2]),lambda c:c[0],lambda c:c[3]),
                 keep=lambda i,c:i==len(health_all)-1)
    checked=commands=0
    for counter,health,warning,(x,y) in cases:
        health=number(bits(health));want_counter,wanted=expected(elf,ram,counter,health,warning,x,y)
        actual_counter=C.c_uint32(counter);calls.clear()
        assert native.em_status_health_draw(C.byref(actual_counter),health,warning,x,y,C.byref(d),C.byref(workers))==1
        assert actual_counter.value==want_counter and calls==wanted,(counter,health,warning,x,y,
            [(i,a,b) for i,(a,b) in enumerate(zip(calls,wanted)) if a!=b][:2])
        checked+=1;commands+=len(calls)
    failures=0
    for at in range(len(calls)):
        for result in (0,-1,2):
            failure[:]=[at,result];calls.clear();counter=C.c_uint32()
            assert native.em_status_health_draw(C.byref(counter),100,0,208,196,C.byref(d),C.byref(workers))==0
            assert len(calls)==at+1
            failures+=1
    failure[:]=[-1,0]
    battery_data=BatteryData(d.white,o.string_bytes(o.load(0x26729c)),d.separator)
    battery_cases=0
    # The full sweep rotates the capacity with the case index; a quick run
    # keeps each case's full-sweep capacity, every charge/capacity edge,
    # every compact mode, position and equipped state.
    battery_all=[(charge,compact,xy,equipped,(12,36,48,198,255)[index%5]) for index,(charge,compact,xy,equipped)
                 in enumerate(itertools.product((0,1,11,12,13,35,36,47,48,49,198,255),(0,1,2),
                                                ((16,118),(-100,-77)),(0,1)))]
    battery_selected=select(battery_all,72,0x209280,axes=(lambda c:c[0],lambda c:c[4],lambda c:(c[1],c[3]),lambda c:c[2]))
    for charge,compact,(x,y),equipped,capacity in battery_selected:
        original=Original(elf,ram,0,0,True)
        original.save(0x810cb2,charge,2);original.save(0x810cb7,capacity,1)
        original.save(0x810c7f,equipped,1)
        wanted=capture_calls(original)
        original.run(0x209280,(UI,x,y,0x123456789abcdef0,compact))
        calls.clear()
        assert native.em_status_battery_draw(charge,capacity,equipped,x,y,0x123456789abcdef0,
                   compact,C.byref(battery_data),C.byref(workers))==1
        assert calls==wanted,('battery',charge,capacity,compact,x,y,equipped,
              [(i,a,b) for i,(a,b) in enumerate(zip(calls,wanted)) if a!=b][:2])
        battery_cases+=1;commands+=len(calls)
    ammo_data=AmmoData(d.white,o.string_bytes(o.load(0x2672a0)),o.string_bytes(0x273570))
    ammo_cases=invalid_selectors=0
    # Quick: every rejected selector (native only), every primary/secondary
    # pair, every amount edge and both positions, plus a fixed-seed sample.
    ammo_all=list(itertools.product(
            (0,1,2,255),(0,1,2,3,4,5,255),(-32768,-999,-1,0,1,9,60,99,100,999,9999,32767),
            ((16,190),(-100,-77))))
    ammo_selected=select(ammo_all,320,0x209860,axes=(lambda c:(c[0],c[1]),lambda c:c[2],lambda c:c[3]),
                         keep=lambda i,c:c[0]!=2 and c[1]>4)
    for primary,secondary,n,(x,y) in ammo_selected:
        original=Original(elf,ram,0,0,True)
        inv=AmmoInventory(primary,secondary,(C.c_int16*5)(n,n, n//100,n%100,n),n)
        if primary != 2 and secondary > 4:
            calls.clear()
            assert native.em_status_ammo_draw(C.byref(inv),x,y,C.byref(ammo_data),C.byref(workers))==0
            assert not calls
            invalid_selectors+=1
            continue
        original.save(0x810ca4,primary,1);original.save(0x810ca6,secondary,1)
        original.write(0x810ca8,bytes(inv.amount));original.save(0x810cb4,inv.reserve,2)
        wanted=capture_calls(original)
        original.run(0x209860,(UI,x,y))
        calls.clear()
        assert native.em_status_ammo_draw(C.byref(inv),x,y,C.byref(ammo_data),C.byref(workers))==1
        assert calls==wanted,('ammo',primary,secondary,n,x,y,
              [(i,a,b) for i,(a,b) in enumerate(zip(calls,wanted)) if a!=b][:2])
        calls.clear()
        assert readable.canonical_ammo_draw(C.byref(inv),x,y,C.byref(ammo_data),C.byref(workers))==1
        assert calls==wanted,('readable source',primary,secondary,n,x,y,
              [(i,a,b) for i,(a,b) in enumerate(zip(calls,wanted)) if a!=b][:2])
        ammo_cases+=1;commands+=len(calls)
    # Original invalid selectors use incoming s0 as TEX0. They cannot be
    # represented as a defined, asset-backed native sprite operation.
    inherited_textures=0
    for selector,seed in itertools.product((5,255),(0,0x98765432,0x20045ec555422186)):
        original=Original(elf,ram,0,0,True)
        original.r[16]=seed
        original.save(0x810ca4,0,1);original.save(0x810ca6,selector,1)
        wanted=capture_calls(original)
        original.run(0x209860,(UI,16,190))
        assert wanted[-1][0]=='sprite' and wanted[-1][-1]==seed
        inherited_textures+=1
    def failed_workers(invoke):
        nonlocal failures
        calls.clear();failure[:]=[-1,0]
        assert invoke()==1
        count=len(calls)
        for at,result in itertools.product(range(count),(0,-1,2)):
            calls.clear();failure[:]=[at,result]
            assert invoke()==0 and len(calls)==at+1
            failures+=1
        failure[:]=[-1,0]
    failed_workers(lambda:native.em_status_battery_draw(48,48,1,16,118,0x1234,0,
                          C.byref(battery_data),C.byref(workers)))
    inv=AmmoInventory(0,4,(C.c_int16*5)(1,2,3,4,5),60)
    failed_workers(lambda:native.em_status_ammo_draw(C.byref(inv),16,190,
                          C.byref(ammo_data),C.byref(workers)))
    banner(part(checked,len(health_all),'health cases'),part(len(battery_selected),len(battery_all),'battery cases'),
           part(ammo_cases+invalid_selectors,len(ammo_all),f'ammo cases ({invalid_selectors} rejected selectors)'),
           f'{inherited_textures} inherited-TEX0 and {failures} failed-worker boundaries in full')
    report={'mode':MODE,'original_health_cases':checked,'exact_ordered_commands':commands,
             'original_battery_cases':battery_cases,'original_ammo_cases':ammo_cases,
             'canonical_ammo_source_cases':ammo_cases,
             'rejected_invalid_selectors':invalid_selectors,'original_inherited_TEX0_cases':inherited_textures,
             'failed_worker_boundaries':failures,'state_and_geometry_text_arguments':'PASS',
             'boundaries':'original string copy/length workers; final geometry/font/GSMetal renderer'}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':main()
