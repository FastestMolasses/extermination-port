#!/usr/bin/env python3
"""Round-trip user's ELF table export and reject damaged containers."""
import ctypes as C
import os
from pathlib import Path
import struct
import subprocess
import sys
from export_aim_fire_tables import BASE,SIZE,SPANS
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/aim-fire/tables-test'
def child(lib,path,good):
    os.environ['EM_AIM_FIRE_TABLES']=path
    n=C.CDLL(lib);n.em_aim_fire_tables_bytes.argtypes=[C.c_uint32,C.c_uint32];n.em_aim_fire_tables_bytes.restype=C.c_void_p
    result=n.em_aim_fire_tables_load()
    if not good:
        assert result==-1 and not n.em_aim_fire_tables_bytes(BASE,1)
        return
    assert result==0
    data=Path(path).read_bytes();offset=8+8*len(SPANS)
    for address,size in SPANS:
        pointer=n.em_aim_fire_tables_bytes(address,size)
        assert pointer and C.string_at(pointer,size)==data[offset:offset+size]
        assert not n.em_aim_fire_tables_bytes(address-1,1)
        assert not n.em_aim_fire_tables_bytes(address,size+1)
        offset+=size
    assert not n.em_aim_fire_tables_bytes(0xFFFFFFFF,2)
    assert n.em_aim_fire_tables_load()==0

def main():
    OUT.mkdir(parents=True,exist_ok=True);lib=OUT/'tables.dylib';path=OUT/'tables.emaf'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC','-Isrc','src/game/em_aim_fire_tables.c','-o',str(lib)],cwd=ROOT,check=True)
    subprocess.run([sys.executable,'tools/export_aim_fire_tables.py','--output',str(path)],cwd=ROOT,check=True)
    data=path.read_bytes();cases=[('valid',data,True),('truncated',data[:-1],False),('trailing',data+b'x',False),('signature',b'BAD!'+data[4:],False)]
    for offset in (4,8,12,16,20,24,28,32,36,40+0x248B70-BASE):
        bad=bytearray(data);struct.pack_into('<I',bad,offset,0xFFFFFFFF);cases.append((f'field{offset}',bytes(bad),False))
    for name,contents,good in cases:
        path.write_bytes(contents)
        subprocess.run([sys.executable,__file__,'--child',str(lib),str(path),str(int(good))],check=True,cwd=ROOT)
    path.unlink();lib.unlink()
    print(f'PASS aim/fire tables: {len(cases)} valid/damaged containers; all4 readonly windows')
if __name__=='__main__':
    if len(sys.argv)>1: child(sys.argv[2],sys.argv[3],bool(int(sys.argv[4])))
    else: main()
