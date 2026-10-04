#!/usr/bin/env python3
"""Export shared boot scripts required by AREA01, never captured state.

The output is private local data. The delivered overlay remains owned by the
module loader. The ordinary-door program is part of the sole boot door image,
so its patch writer and the shared interpreter observe the same bytes.
"""
import argparse
import json
from pathlib import Path
import struct
import export_area01_common as A
from export_area01_tables import walk_chain

WINDOWS=(('panels',0x246C20,0x247E20,0x246C20,
          (0x246C20,0x246F20,0x2470E0,0x2471E0,0x2476A0)),
         ('doors',0x24D8F0,0x24DF80,0x24D900,
          (0x24D900,0x24DA40,0x24DE40,0x24DEC0)),
         ('pickup',0x2482C0,0x248540,0x2482C0,(0x2482C0,0x248480)),
         ('take',0x266620,0x2668A0,0x266620,(0x266620,0x2667E0)))

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,default=A.ROOT/'build/level2/area01_boot_scripts')
    args=ap.parse_args()
    # No writes through assets/ symlinks into a different checkout.
    for parent in (args.out,*args.out.parents):
        if parent.is_symlink(): raise SystemExit(f'refusing symlink output component: {parent}')
    read=A.static_reader(A.read_elf(),A.read_overlay());caps=A.captures()
    args.out.mkdir(parents=True,exist_ok=True);report=[]
    for name,lo,hi,entry,entries in WINDOWS:
        data=read(lo,hi-lo)
        chains={hex(e):walk_chain(data,lo,e) for e in entries}
        changes={c.name:[hex(lo+i) for i in range(0,len(data),4)
                        if c.ram[lo+i:lo+i+4]!=data[i:i+4]] for c in caps}
        blob=struct.pack('<4s4I',b'EMSC',1,lo,entry,len(data))+data
        path=args.out/(name+'.emsc');path.write_bytes(blob)
        assert path.read_bytes()[20:]==read(lo,len(data))
        report.append(dict(name=name,base=hex(lo),end=hex(hi),entry=hex(entry),sha256=A.sha(data),
                           records=sum(map(len,chains.values())),chains=chains,
                           capture_mutated_words={k:v for k,v in changes.items() if v}))
    (args.out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    # Two scene-2 event tracks and the existing SDK tangent coefficients.
    # Sparse tables are read-only; timeline cursors live in the camera owner.
    tables=((0x26AAE0,0x20),(0x26AC80,0x20),(0x26C598,13*4))
    table_blob=struct.pack('<4s3I',b'EMSP',1,len(tables),0)
    table_blob+=b''.join(struct.pack('<2I',a,n) for a,n in tables)
    table_blob+=b''.join(read(a,n) for a,n in tables)
    (args.out/'timeline.emsp').write_bytes(table_blob)
    assert (args.out/'timeline.emsp').read_bytes()==table_blob
    (args.out/'timeline.json').write_text(json.dumps(dict(
        sha256=A.sha(table_blob),windows=[dict(address=hex(a),size=n,sha256=A.sha(read(a,n)))
                                        for a,n in tables]),indent=2)+'\n')
    print(f'PASS: {len(WINDOWS)} ELF windows, {sum(x[2]-x[1] for x in WINDOWS)} original bytes, '
          f'{sum(r["records"] for r in report)} reachable records, {len(caps)} captures; '
          f'{len(tables)} read-only timeline windows / {sum(n for _,n in tables)} bytes -> {args.out}')
if __name__=='__main__':main()
