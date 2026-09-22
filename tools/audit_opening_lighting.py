#!/usr/bin/env python3
"""Audit opening face lighting against original VU instructions and DMA data.

This reads the owner's original ELF, extracted face models and immutable EE
snapshot. It writes aggregate comparisons only; no original bytes are embedded.
The fragment comparison isolates interpolation/normalization differences using
captured light matrices. It is not a complete raster or screenshot comparison.
"""
from pathlib import Path
import argparse, ctypes as C, hashlib, json, math, random, struct, subprocess, sys, tempfile
import test_snow_particles_reference as original

ROOT=Path(__file__).resolve().parents[1]

class Matrices(C.Structure):
    _fields_=[('normal',C.c_float*16),('color',C.c_float*16)]

def vector(matrix,point):
    return [sum(matrix[c*4+r]*point[c] for c in range(len(point)))for r in range(4)]

def inverse(m):
    a=[list(row)+[float(i==j)for j in range(3)]for i,row in enumerate(m)]
    for i in range(3):
        row=max(range(i,3),key=lambda j:abs(a[j][i]));a[i],a[row]=a[row],a[i]
        scale=a[i][i];assert abs(scale)>1.e-9
        a[i]=[x/scale for x in a[i]]
        for j in range(3):
            if i!=j:
                scale=a[j][i];a[j]=[x-scale*y for x,y in zip(a[j],a[i])]
    return [row[3:]for row in a]

def normalize(v):
    length=math.sqrt(sum(x*x for x in v));assert length>0
    return [x/length for x in v]

def shade_oracle(normal,normal_matrix,color_matrix,body=False):
    vm=original.VU(bytearray(16384));vm.color_only=True
    vm.v[10][:3]=list(map(original.bits,normal))
    for i in range(3):vm.v[24+i]=list(map(original.bits,normal_matrix[i*4:i*4+4]))
    for i in range(4):vm.v[20+i]=list(map(original.bits,color_matrix[i*4:i*4+4]))
    vm.v[9][0]=0x4b0000ff
    vm.v[17][0]=0x4b0000ff
    ranges=([(0x23c878,0x23c890),(0x23c8b0,0x23c8b8),
             (0x23c8d8,0x23c8e8),(0x23c8f0,0x23c900),(0x23c920,0x23c928)]if body else
            [(0x23c5d8,0x23c5f0),(0x23c610,0x23c618),
             (0x23c638,0x23c650),(0x23c658,0x23c660),(0x23c688,0x23c690)])
    for begin,end in ranges:vm.run(begin,end)
    return vm.v[14]

def shade_formula(normal,normal_matrix,color_matrix):
    fp=original.fp
    intensity=[]
    for row in range(4):
        value=fp(normal_matrix[row]*normal[0])
        value=fp(value+fp(normal_matrix[4+row]*normal[1]))
        value=fp(value+fp(normal_matrix[8+row]*normal[2]))
        intensity.append(max(value,0.))
    result=[]
    for row in range(4):
        value=fp(color_matrix[row]*intensity[0])
        value=fp(value+fp(color_matrix[4+row]*intensity[1]))
        value=fp(value+fp(color_matrix[8+row]*intensity[2]))
        value=fp(value+color_matrix[12+row])
        result.append(original.bits(min(value,original.flt(0x4b0000ff))))
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--decomp-root',type=Path,default=ROOT.parent/'Extermination')
    p.add_argument('--reference-ee',type=Path)
    p.add_argument('--report',type=Path)
    args=p.parse_args();sys.path.insert(0,str(args.decomp_root/'tools'))
    import export_opening_faces as faces
    import export_level as level
    original.ELF=(args.decomp_root/'config/SCUS_971.12').read_bytes()
    ee=args.reference_ee or args.decomp_root/'build/startup-reference/opening_ee.bin'
    ram=ee.read_bytes();results={};verified=0;matrix_bytes=0
    temp=tempfile.TemporaryDirectory(prefix='original-lighting-')
    library=Path(temp.name)/'lighting.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
                    '-shared','-fPIC','-I'+str(ROOT/'src'),str(ROOT/'src/game/em_lighting.c'),
                    '-o',str(library)],check=True)
    api=C.CDLL(str(library));F=C.POINTER(C.c_float)
    api.em_lighting_vertex.argtypes=[C.POINTER(C.c_uint32),F,C.POINTER(Matrices)]
    api.em_lighting_matrices.argtypes=[C.POINTER(Matrices),F,F,F,F]
    rig_directions=(C.c_float*12).from_buffer_copy(ram[0x817c80:0x817cb0])
    rig_colors=(C.c_float*12).from_buffer_copy(ram[0x817cb0:0x817ce0])
    rig_ambient=(C.c_float*4)(32,32,32,0)
    _,matched,rig=level.lightrig_read(level.BootElf(args.decomp_root/'config/SCUS_971.12'),11,0)
    assert matched
    for light in (1,2):
        expected=ram[0x817c80+light*16:0x817c8c+light*16]
        assert struct.pack('<3f',*rig['lights'][light]['dir'])==expected,('rig direction',light)
        assert all(original.bits(float(format(x,'.9g')))==original.bits(x)for x in rig['lights'][light]['dir'])
    def native(normal,nmat,cmat):
        matrices=Matrices();matrices.normal[:]=nmat;matrices.color[:]=cmat
        result=(C.c_uint32*4)()
        api.em_lighting_vertex(result,(C.c_float*3)(*normal),C.byref(matrices))
        return list(result)
    rng=random.Random(0x23c5d8)
    for _ in range(1000):
        n=[original.flt(original.bits(rng.uniform(-2,2)))for _ in range(3)]
        normal_matrix=[original.flt(original.bits(rng.uniform(-1,1)))for _ in range(16)]
        colors=[original.flt(original.bits(rng.uniform(0,500)))for _ in range(12)]
        colors+=[original.fp(8388608+rng.uniform(-10,400))for _ in range(4)]
        expected=shade_oracle(n,normal_matrix,colors)
        assert expected==shade_oracle(n,normal_matrix,colors,True)==native(n,normal_matrix,colors)
    for name,source,offset,model,actor,_ in faces.FACES:
        data=(args.decomp_root/'extract'/source).read_bytes()
        size=struct.unpack_from('<I',data,offset+12)[0];raw=data[offset:offset+size]
        assert raw==ram[model:model+size]
        face=struct.unpack_from('<I',ram,actor+0x90)[0]
        weights=ram[face+0x40:face+0x60];needle=struct.pack('<I',model+64)
        matches=[]
        for i in range(4,len(ram)-4,16):
            if ram[i:i+4]!=needle or struct.unpack_from('<I',ram,i-4)[0]>>28!=3:continue
            tag=i-4
            for setup in range(tag-0x200,tag,16):
                if struct.unpack_from('<4I',ram,setup)!=(0,0x11000000,0x01000101,0x6c0403f5):continue
                start=setup+16
                if ram[start+0x100:start+0x120]==weights:matches.append((tag,start))
        assert len(matches)==1,(name,matches)
        tag,start=matches[0]
        colors=struct.unpack_from('<16f',ram,start)
        screen=struct.unpack_from('<16f',ram,start+0x60)
        normal_matrix=struct.unpack_from('<16f',ram,start+0xa0)
        node=struct.unpack_from('<I',ram,actor+0x110+7*4)[0]
        bone=struct.unpack_from('<16f',ram,node+0x90)
        built=Matrices()
        assert api.em_lighting_matrices(C.byref(built),(C.c_float*16)(*bone),
                                        rig_directions,rig_colors,rig_ambient)
        assert bytes(built.normal)[:48]==struct.pack('<12f',*normal_matrix[:12])
        assert bytes(built.color)==struct.pack('<16f',*colors)
        matrix_bytes+=112
        # Original 001C7900 normalizes each bone basis column before folding
        # directions into it. Reconstruct world directions algebraically to
        # isolate the shader's interpolation/normalization stage.
        columns=[normalize(bone[i*4:i*4+3])for i in range(3)]
        basis=[[columns[c][r]for c in range(3)]for r in range(3)]
        inverse_basis=inverse(basis)
        directions=[[sum(normal_matrix[k*4+i]*inverse_basis[k][j]for k in range(3))
                     for j in range(3)]for i in range(3)]
        for block in faces.records(raw):
            for record in block:
                n=struct.unpack_from('<3f',record,32)
                expected=shade_oracle(n,normal_matrix,colors)
                assert expected==shade_formula(n,normal_matrix,colors)==shade_oracle(n,normal_matrix,colors,True)==native(n,normal_matrix,colors)
                verified+=1
        sections,_,deltas=faces.mesh(raw)
        positions,normals,indices,_,_,_=sections[0]
        old_colors=[];world_normals=[];depths=[];lengths=[];screens=[]
        weight_values=struct.unpack('<8f',weights)
        for i,normal in enumerate(normals):
            lengths.append(math.sqrt(sum(x*x for x in normal)))
            old_colors.append([x&255 for x in shade_formula(normal,normal_matrix,colors)])
            world_normals.append([sum(basis[r][c]*normal[c]for c in range(3))for r in range(3)])
            # RN morph is the existing face export fixture's stated arithmetic
            # dependency. Here it is used only for interpolation depth weights.
            position=[positions[i][c]+sum(deltas[i][3*k+c]*weight_values[k]for k in range(7))for c in range(3)]
            point=vector(screen,position+[1]);depths.append(point[3])
            screens.append([point[0]/point[3],point[1]/point[3]]if point[3] else[1.e9,1.e9])
        errors=[];plain_errors=[];examples=[]
        for i in range(0,len(indices),3):
            ids=indices[i:i+3]
            if any(depths[j]<=0 for j in ids):continue
            xy=[screens[j]for j in ids]
            if max(v[0]for v in xy)<1792 or min(v[0]for v in xy)>2304 or max(v[1]for v in xy)<1936 or min(v[1]for v in xy)>2160:continue
            area=(xy[1][0]-xy[0][0])*(xy[2][1]-xy[0][1])-(xy[1][1]-xy[0][1])*(xy[2][0]-xy[0][0])
            if abs(area)<1.e-9:continue
            old=[sum(old_colors[j][c]for j in ids)/3 for c in range(3)]
            for perspective in (False,True):
                weighted=[sum(world_normals[j][c]/(depths[j]if perspective else 1)for j in ids)for c in range(3)]
                length=math.sqrt(sum(x*x for x in weighted))
                if length<1.e-8:continue
                unit=[x/length for x in weighted]
                intensity=[max(sum(a*b for a,b in zip(unit,d)),0.)for d in directions]
                new=[min(colors[12+c]-8388608+sum(intensity[k]*colors[4*k+c]for k in range(3)),255.)for c in range(3)]
                error=max(abs(a-b)for a,b in zip(old,new))
                (errors if perspective else plain_errors).append(error)
                if perspective:examples.append({'triangle':i//3,'gs_color':old,'fragment_color':new,'difference':error})
        examples.sort(key=lambda item:item['difference'],reverse=True)
        results[name]={'actor':hex(actor),'body_camera_fill':bool(ram[actor+2]&0x20),
                       'body_light_reference_bone':ram[actor+0x98],
                       'face_dma_tag':hex(tag),'face_color_matrix':hex(start),
                       'face_normal_matrix':hex(start+0xa0),'vertices':len(normals),'triangles':len(indices)//3,
                       'source_normal_length_range':[min(lengths),max(lengths)],
                       'triangles_in_front_intersecting_viewport':len(errors),
                       'centroid_max_color_difference':max(errors,default=0),
                       'centroid_mean_color_difference':sum(errors)/max(len(errors),1),
                       'centroid_differences_over_one':sum(e>1 for e in errors),
                       'affine_normal_comparison_max':max(plain_errors,default=0),'examples':examples[:3]}
    report={'status':'PASS','original_face_and_body_record_light_comparisons':verified*2,
            'random_matrix_comparisons':1000,'captured_matrix_builder_bytes_equal':matrix_bytes,
            'original_rig_direction_bytes_equal':24,
            'actors':results,'original_elf_sha256':hashlib.sha256(original.ELF).hexdigest(),
            'limitations':['Original-instruction finite arithmetic comparison; not captured GS raster colors.',
                           'Centroid comparison isolates shader interpolation and normalization using captured matrices.',
                           'Occlusion, textures, fog and native upstream camera/bone rounding are not included.']}
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))
    temp.cleanup()
if __name__=='__main__':main()
