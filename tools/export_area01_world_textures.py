#!/usr/bin/env python3
"""Export AREA01 sub-0 world textures from original disc upload delivery.

Collect TEX0 references from the delivered static/dynamic/model banks and
shared player/effect sources. Reuse the existing GS upload replay, texture
decoder, residency check and EMOT serializer. No captured pixel supplies
an exported texel; captures are independent exact comparisons only.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

import export_area01_common as A
import export_area01_level as L
import export_disc_textures_gs as G
import export_object_textures as O
import export_page_textures as P
import export_world_models as W


def collect(image, capture, elf):
    out = {}
    table = A.u32(capture.ram, 0x28A59C)
    span = image.read(table, image.span()[1]-table)
    count = A.u32(span,0)
    assert 0 < count < 256
    for i in range(count):
        off = A.s32(span,4+4*i)>>2<<2
        blocks = W.model_record(span,off,i)[0]
        O.block_tex0(span,off,blocks,f'AREA01 world model {i}',out)
    for i,address,blocks in L.bank_objects(image.read,A.u32(capture.ram,0x28A5A0))[1:]:
        data = image.read(address,0x40+0x820*blocks)
        O.block_tex0(data,0,blocks,f'AREA01 static object {i}',out)
    for i,address in L.dynamic_entries(image.read,A.u32(capture.ram,0x28A5A4)):
        data = image.read(address,0x860)
        blocks = A.u32(data,0)
        assert blocks == 1, 'AREA01 dynamic record extent'
        # The original dynamic kernel reads three vertices of each block.
        for v in range(3):
            key = struct.unpack_from('<Q',data,0x50+64*v)[0]&O.CLD_MASK
            out.setdefault(key,set()).add(f'AREA01 dynamic record {i}')
    shared = {}
    O.player_tex0(A.EXTRACT,shared)
    for key,labels in shared.items():
        labels = {s for s in labels if not s.lower().startswith('roger')}
        # Existing equipment exporter documents the dormant model-0x36
        # zero TEX0 records; they are not valid texture references.
        if labels and P.drawable(key):out.setdefault(key,set()).update(labels)
    page = P.tex0_set(elf,(A.EXTRACT/'OVERLAY/AREA11.BIN').read_bytes())
    for key,labels in page.items():
        labels = {s for s in labels if 'flame descriptor' not in s}
        if labels:out.setdefault(key,set()).update(labels)
    for key in out:
        f = O.tex0_fields(key)
        if not P.drawable(key) or f['tfx'] not in (0,2):
            raise SystemExit(f'unsupported AREA01 material {key:#x}: {sorted(out[key])}')
    return out


def export(out, iso=None, captures=None):
    caps = A.captures() if captures is None else captures
    image = A.LoadedImage(A.build_load_map(caps[0])[0])
    refs = collect(image,caps[0],A.read_elf())
    loader = G.FirstLevel(G.Disc(iso),A.EXTRACT)
    world = loader.world(A.AREA,A.SUB)
    pixels = O.disc_texels(world,refs)
    freezes = [c.gs for c in caps]
    if not all(freezes):raise SystemExit('an AREA01 capture lacks its GS freeze')
    O.cross_check(pixels,freezes)
    data = O.emot(pixels)
    # Refuse any symlink ancestor: never redirect into the main checkout.
    if any(p.is_symlink() for p in out.parents):raise SystemExit(f'{out}: symlink parent')
    out.parent.mkdir(parents=True,exist_ok=True)
    if out.is_symlink():out.unlink()
    out.write_bytes(data)
    report = out.with_suffix('.json')
    if report.is_symlink():report.unlink()
    report.write_text(json.dumps(dict(area=A.AREA,sub=A.SUB,count=len(pixels),bytes=len(data),
        captures=[c.name for c in caps],sources=loader.sources,
        textures=[dict(tex0=f'{k:#018x}',users=sorted(refs[k]),bytes=len(pixels[k]),
                       sha256=hashlib.sha256(pixels[k]).hexdigest()) for k in sorted(pixels)]),indent=1)+'\n')
    return data,refs,pixels


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,default=A.ROOT/'assets/area01_world_textures.emot')
    ap.add_argument('--iso',type=Path)
    args=ap.parse_args()
    data,refs,pixels=export(args.out,args.iso)
    print(f'wrote {args.out}: {len(refs)} textures, {len(data)} bytes; disc residency and every AREA01 GS freeze exact')

if __name__=='__main__':main()
