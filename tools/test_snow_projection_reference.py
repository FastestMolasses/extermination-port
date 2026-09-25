#!/usr/bin/env python3
"""Compare snow projection C with the owner's original VU instructions.

The optional immutable opening EE dump provides snow DMA matrices and all216
captured tiles. A full VU memory dump's last effect is not assumed to be snow.
No original instruction bytes, matrix tables or captured output are embedded.
Finite binary32 arithmetic is modeled; exceptional VU arithmetic is excluded.
"""
from pathlib import Path
import argparse, ctypes as C, hashlib, json, random, struct, subprocess, tempfile
import test_snow_particles_reference as original

ROOT=Path(__file__).resolve().parents[1]

class Projection(C.Structure):
    _fields_=[('extent',C.c_float*16),('clip',C.c_float*16),
              ('screen',C.c_float*16),('fog',C.c_float*4),
              ('bias',C.c_float*4),('tag',C.c_uint32*4)]
class Projected(C.Structure):
    _fields_=[('xyzf',(C.c_uint32*4)*2),('color',C.c_uint32*4),
              ('st',(C.c_float*2)*2),('clip',C.c_float*4)]

def oracle(state,particle):
    mem=bytearray(16384);mem[0x6e0:0x7d0]=bytes(state)
    struct.pack_into('<I',mem,0x630,1)
    struct.pack_into('<I',mem,0x650,0x100)
    mem[0x880:0x890]=bytes(particle.position)
    mem[0x8a0:0x8b0]=bytes(particle.color)
    mem[0x8b0:0x8c0]=bytes(particle.half_size)
    vm=original.VU(mem);vm.stop_on_kick=True
    vm.run(0x233fc0,0x234298)
    assert len(vm.kicks)==1,vm.kicks
    base=vm.kicks[0];count=vm.read(base)[0]&0x7fff
    assert count in (0,1),count
    result={'clip':vm.clips[0],'drawn':bool(count)}
    if count:
        result.update(xyzf=[vm.read(base+4),vm.read(base+6)],
                      color=vm.read(base+2),
                      st=[vm.read(base+3)[:2],vm.read(base+5)[:2]])
    return result

def particle(position,size=(.3,.3,0,0),color=(255,128,64,128)):
    item=original.Particle();item.position[:]=position
    item.color[:]=color;item.half_size[:]=size
    return item

def synthetic_cases():
    state=Projection()
    state.extent[:]=[2,0,0,0,0,3,0,0,2048,2048,1,1,0,0,10,0]
    state.clip[:]=[1,0,0,0,0,1,0,0,0,0,1,1,0,0,-.2,0]
    state.screen[:]=[2,0,0,0,0,3,0,0,2048,2048,.9,1,0,0,10,0]
    state.fog[:]=[255,2048,255,-.85]
    struct.pack_into('<QQ',state.tag,0,0x602b400000008000,0x424216)
    cases=[]
    # Exercise inclusive CLIP boundaries and adjacent binary32 values. The
    # particle W input deliberately differs: translation always uses VF00.w.
    for depth in [.125,1.,20.,49.999,50.,51.,100.,250.,300.,400.]:
        for axis in (0,1):
            for sign in (-1,1):
                for offset in (-1,0,1):
                    point=[0.,0.,depth,7.]
                    point[axis]=sign*original.flt(original.bits(depth)+offset)
                    cases.append(('clip_boundary',state,particle(point)))
        for depth_bits in [original.bits(.1)-1,original.bits(.1),original.bits(.1)+1]:
            cases.append(('near_boundary',state,particle([0,0,original.flt(depth_bits),1])))
    # Quantization depends on each corner separately, including negative
    # fractions. Use a wide clip volume without changing screen coordinates.
    quant=Projection.from_buffer_copy(bytes(state))
    quant.clip[0]=quant.clip[5]=1.e-5
    for center in [-2048.0625,-2048.,-2047.9375,-.0625,0.,.0625,2047.9375]:
        for delta in [-.00390625,0,.00390625]:
            for size in [.0,.015625,.03125,.046875,.125]:
                cases.append(('quantization',quant,particle([center+delta,center-delta,1,1],(size,size,0,0))))
    rng=random.Random(0x233fc0)
    for _ in range(800):
        depth=rng.uniform(.11,400)
        cases.append(('synthetic_random',state,particle(
            [rng.uniform(-1.2,1.2)*depth,rng.uniform(-1.2,1.2)*depth,depth,1],
            [rng.uniform(.01,2),rng.uniform(.01,2),0,0],
            [rng.uniform(0,255)for _ in range(4)])))
    return cases

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--decomp-root',type=Path,default=ROOT.parent/'Extermination')
    p.add_argument('--reference-ee',type=Path)
    p.add_argument('--reference-tiles',type=Path)
    p.add_argument('--report',type=Path)
    args=p.parse_args();original.ELF=(args.decomp_root/'config/SCUS_971.12').read_bytes()
    cases=synthetic_cases();runtime_tiles=0;ram=None
    if args.reference_ee:
        ram=args.reference_ee.read_bytes()
        tiles_path=args.reference_tiles or args.decomp_root/'build/weather_reference/original_tiles.json'
        records=json.loads(tiles_path.read_text())
        lookup=struct.unpack_from('<80f',original.ELF,0x2342bc-0x100000+0x300)
        for entry in records:
            packet=int(entry['packet'],16)
            assert struct.unpack_from('<I',ram,packet+0x1c)[0]==0x6c090050
            assert struct.unpack_from('<I',ram,packet-0xf4)[0]==0x6c0f006e
            state=Projection.from_buffer_copy(ram[packet-0xf0:packet])
            mem=bytearray(16384)
            for i,value in enumerate(lookup):struct.pack_into('<f',mem,i*16,value)
            mem[0x500:0x590]=ram[packet+0x20:packet+0xb0]
            mem[0x590:0x5e0]=ram[packet+0xd0:packet+0x120]
            generator=original.VU(mem);generator.run(0x233828,0x233fc0)
            assert generator.vi[8]==20,generator.vi[8]
            for i in range(generator.vi[8]):
                item=original.Particle()
                for field,offset in [('position',0),('color',2),('half_size',3)]:
                    C.memmove(C.addressof(getattr(item,field)),
                              struct.pack('<4I',*generator.read(0x88+4*i+offset)),16)
                cases.append(('original_snow_dma',state,item))
            runtime_tiles+=1
    with tempfile.TemporaryDirectory(prefix='snow-projection-') as tmp:
        lib=Path(tmp)/'projection.dylib'
        subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
                        '-shared','-fPIC','-I'+str(ROOT/'src'),str(ROOT/'src/game/em_snow_projection.c'),
                        str(ROOT/'src/game/em_snow_particles.c'),'-o',str(lib)],check=True)
        api=C.CDLL(str(lib));project=api.em_snow_project
        project.argtypes=[C.POINTER(Projection),C.POINTER(original.Particle),C.POINTER(Projected)]
        project.restype=C.c_int
        counts={};guardband_only=0;compared_bytes=0
        for index,(kind,state,item) in enumerate(cases):
            expected=oracle(state,item);actual=Projected()
            drawn=project(C.byref(state),C.byref(item),C.byref(actual))
            assert drawn==expected['drawn'],('clipping',kind,index,drawn,expected)
            counts.setdefault(kind,{'visible':0,'culled':0})['visible'if drawn else'culled']+=1
            fields=['clip']+(['xyzf','color','st']if drawn else[])
            for field in fields:
                data=expected[field]
                words=data if field in ('color','clip') else [x for row in data for x in row]
                assert bytes(getattr(actual,field))==struct.pack('<'+'I'*len(words),*words),(field,kind,index,list(actual.color),expected)
                compared_bytes+=len(words)*4
            if drawn and kind=='original_snow_dma':
                # The original visible raster is half the horizontal CLIP
                # volume and four-fifths of its vertical volume.
                x,y,_,w=actual.clip
                guardband_only+=abs(x)>.5*abs(w) or abs(y)>.8*abs(w)
    report={'status':'PASS','original_projection_cases':len(cases),'cases':counts,
            'runtime_tiles':runtime_tiles,
            'projection_bytes_equal':compared_bytes,'guard_band_only_original_submissions':guardband_only,
            'original_elf_sha256':hashlib.sha256(original.ELF).hexdigest(),
            'limitations':['Finite arithmetic only; no exceptional VU DIV/FTOI emulation.',
                           'Instruction comparison is not a GS raster-output comparison.',
                           'The frame matrices (P, the 001CD370(0) clip projection, K) are the render '
                           'context\'s (em_rcl_frame_matrices over 001D2960: test_frame_render_heads_reference, '
                           'test_render_context_live_reference); this test takes them from the captured DMA.']}
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))
if __name__=='__main__':main()
