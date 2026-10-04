#!/usr/bin/env python3
"""AREA01 shadow receivers from disc delivery; captures only verify bytes.
Reuse the existing EMSR serializer and object-block validation. No original
bytes are embedded; output belongs in ignored assets/area01.
"""
import importlib.util
import json
from pathlib import Path
import struct
import export_area01_common as A

spec=importlib.util.spec_from_file_location('shadow_export',A.DECOMP/'tools/export_shadow_receivers.py')
S=importlib.util.module_from_spec(spec);spec.loader.exec_module(S)

def build():
    caps=A.captures();image=A.LoadedImage(A.build_load_map(caps[0])[0])
    table=A.u32(caps[0].ram,0x28A5A0)
    bank=image.read(table,image.span()[1]-table)
    library=(A.EXTRACT/S.LIBRARY_FILE).read_bytes()
    slots=A.u32(bank,0);grid_at=S.entry(bank,0,0)
    rows,stride=struct.unpack_from('<2I',bank,grid_at)
    floats=struct.unpack_from('<6f',bank,grid_at+8)
    grid=struct.unpack_from(f'<{rows*stride*4}i',bank,grid_at+0x20)
    objects=[];boxes=[]
    for ident in range(1,slots):
        at=S.entry(bank,0,ident)
        objects.append((ident,at)+S.object_record(bank,at,f'AREA01 object {ident}'))
    for ident in S.BOX_MODELS:
        at=S.entry(library,0,ident)
        boxes.append((ident,at)+S.object_record(library,at,f'box {ident}'))
    assert {i for i in grid if i>0}<={o[0] for o in objects}
    compared=0
    for cap in caps:
        delivered=A.u32(cap.ram,0x28A5A0);lib=A.u32(cap.ram,0x28A56C)
        assert delivered==table,(cap.name,'static table identity')
        n=0x20+len(grid)*4
        assert cap.ram[table+grid_at:table+grid_at+n]==bank[grid_at:grid_at+n]
        compared+=n
        for ident,at,count,lo,hi,blocks in objects:
            n=0x40+count*0x820
            assert cap.ram[table+at:table+at+n]==bank[at:at+n],(cap.name,ident)
            compared+=n
        for ident,at,count,lo,hi,blocks in boxes:
            n=0x40+count*0x820
            assert cap.ram[lib+at:lib+at+n]==library[at:at+n],(cap.name,ident)
            compared+=n
    data=S.serialize(dict(rows=rows,stride=stride,floats=floats,grid=grid,slots=slots,objects=objects,boxes=boxes))
    report=dict(captures=len(caps),table=hex(table),objects=len(objects),blocks=sum(o[2] for o in objects),
                grid_words=len(grid),box_models=len(boxes),compared_bytes=compared,bytes=len(data))
    return data,report
def main():
    data,report=build();out=A.ROOT/'assets/area01_shadow_receivers.emsr'
    if any(p.is_symlink() for p in out.parents):raise SystemExit('symlink asset parent')
    out.parent.mkdir(parents=True,exist_ok=True)
    if out.is_symlink():out.unlink()
    out.write_bytes(data)
    report_path=out.with_suffix('.json')
    if report_path.is_symlink():report_path.unlink()
    report_path.write_text(json.dumps(report,indent=2)+'\n')
    print('AREA01 shadow receiver export:',json.dumps(report,sort_keys=True))
if __name__=='__main__':main()
