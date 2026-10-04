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


POOL, RECORD, CAPACITY = 0x7A5640, 0x2F0, 0x100   # D_007A5640, 001AF8E0


def library_tex0(capture, out):
    """The global library models (chunk27/f01_id37.bin at D_0028A56C) that
    AREA01's own owners bind: every allocated pool record's +0x44 that is
    the start of a library entry. The pickups 0015AFA0 (0x4D, 0x58), the
    companions 001C5680 and the overlay owner 00826CF0 use models the
    first level's id lists do not name (00826CF0's 0x6B is labelled Roger's
    there and is filtered below)."""
    library = (A.EXTRACT/'chunk27/f01_id37.bin').read_bytes()
    base = A.u32(capture.ram,0x28A56C)
    if capture.ram[base:base+0x40] != library[:0x40]:
        raise SystemExit(f'{capture.name}: D_0028A56C does not hold the library')
    count = A.u32(library,0)
    starts = {A.s32(library,4+4*i)>>2<<2: i for i in range(count)}
    for k in range(CAPACITY):
        r = POOL+RECORD*k
        if not A.u32(capture.ram,r+0x14):   # +0x14 self: 0 when free
            continue
        m = A.u32(capture.ram,r+0x44)
        ident = starts.get(m-base) if base <= m < base+len(library) else None
        if ident is None:
            continue
        off = m-base
        blocks = W.model_record(library,off,ident)[0]
        O.block_tex0(library,off,blocks,f'AREA01 library model {ident:#x} (owner {A.u32(capture.ram,r+0x10):08X})',out)


RESOURCES, RESOURCE_COUNT = 0x28A490, 0xAF   # D_0028A490, the 0xAF words 001AB430 fills
GLOBAL_CHUNK = 'chunk03'                     # sector 3: the boot's resident resources


def resource_bytes(capture, image, ident, m, files, who):
    """(bytes, source) of resource `ident` at `m`: ids 0x08..0x34 are the
    globally resident sector-3 files (chunk03/fNN_idXX.bin, laid out from
    0x10E99C0 in file order) and the entry must hold that file's bytes in
    the capture; other ids must lie in the AREA01 load map."""
    ram = capture.ram
    if ident in files:
        data = files[ident].read_bytes()
        if ram[m:m+len(data)] != data:
            raise SystemExit(f'{capture.name}: D_0028A490[{ident:#x}] = {m:#x} does not hold {files[ident].name}')
        return data, f'{GLOBAL_CHUNK}/{files[ident].name}'
    hit = image.locate(m)
    if hit is None:
        raise SystemExit(f'{capture.name}: {who} binds resource {ident:#x} ({m:#x}) outside sector 3 and the load map')
    return image.read(m, hit[0]+hit[3]-m), hit[4]


def resource_model_tex0(data, ident, label, out):
    """A block model, or a face resource (blocks of 0x163 qwords)."""
    blocks, qwc = A.u32(data, 0), A.u32(data, 4) & 0xFFFF
    if qwc == blocks*O.FACE_BLOCK_QWORDS:
        O.face_tex0(data, 0, label, out)
    else:
        O.block_tex0(data, 0, W.model_record(data, 0, ident)[0], label, out)


# 001BA8E0(self, type)'s face resource by type (decomp func_001BA8E0.c):
# only the types AREA01's callers pass. The control-room NPC 00825350
# calls 001BA8E0(self, self[0xD]) in its state 0
# (func_overlay_AREA01_00825310.c); its +0x0D is 0x47 in every capture,
# which binds D_0028A490[0x88] through 001CA700. Any other type is refused.
FACE_OWNERS = {0x00825350}
FACE_BY_TYPE = {0x47: 0x88, 0x48: 0x88}


def resource_tex0(capture, image, out):
    """The resource-table models (D_0028A490[id]) that AREA01's own owners
    bind: every allocated pool record's +0x44 equal to a nonzero resource
    entry. In every AREA01 capture these are 00128C10 (id 0x0F),
    001BFFD0 (0x20), 001C02E0 (0x22, a face resource: 4 blocks of 0x163
    qwords) and the talk owner 00825350 (0x47); then the faces 001BA8E0
    binds (FACE_BY_TYPE). The bytes come from the user's extract; a bound
    entry in neither source, or in neither model format, is refused."""
    ram = capture.ram
    table, entry = {}, {}
    for i in range(RESOURCE_COUNT):
        a = A.u32(ram, RESOURCES + 4*i)
        entry[i] = a
        if a: table.setdefault(a, i)
    files = {int(p.stem.split('_id')[1], 16): p for p in (A.EXTRACT/GLOBAL_CHUNK).glob('f*_id*.bin')}
    for k in range(CAPACITY):
        r = POOL+RECORD*k
        if not A.u32(ram, r+0x14):
            continue
        owner = A.u32(ram, r+0x10)
        m = A.u32(ram, r+0x44)
        ident = table.get(m)
        if ident is not None:
            data, source = resource_bytes(capture, image, ident, m, files, f'owner {owner:08X}')
            resource_model_tex0(data, ident, f'AREA01 resource {ident:#x} ({source}, owner {owner:08X})', out)
        if owner in FACE_OWNERS:
            kind = ram[r+0x0D]
            if kind not in FACE_BY_TYPE:
                raise SystemExit(f'{capture.name}: owner {owner:08X} passes 001BA8E0 type {kind:#x} (not mapped)')
            face = FACE_BY_TYPE[kind]
            data, source = resource_bytes(capture, image, face, entry[face], files, f'owner {owner:08X}')
            resource_model_tex0(data, face, f'AREA01 face {face:#x} ({source}, owner {owner:08X} 001BA8E0)', out)


# 001E9E60's two TEX0 words for the floor fields' strips (001CB950's
# register pair, picked by the field record's byte +0x5C == 1; decomp
# func_001E9E60.c, port em_area01_render_hud.c), drawn by D_002345E0's
# program for 0015A2C0's eight placements.
# 001E7D20's surface word outside area 0x13 (decomp func_001E7D20.c): the
# ripple surface D_00234B00's program draws (AREA01's pool owner 001E7D20).
FLOOR_TEX0 = (0x20048CC155422242, 0x20048E4155422256, 0x20048BA199422040)


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
    library_tex0(capture,out)
    resource_tex0(capture,image,out)
    for key in FLOOR_TEX0[:2]:
        out.setdefault(key&O.CLD_MASK,set()).add('AREA01 floor field (001E9E60)')
    out.setdefault(FLOOR_TEX0[2]&O.CLD_MASK,set()).add('AREA01 ripple surface (001E7D20)')
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
        # The floor fields' and the ripple surface's words are TCC 0 (RGB)
        # MODULATE: the page pixel path takes Af from the vertex; the texels
        # decode alike.
        floor = key in {k&O.CLD_MASK for k in FLOOR_TEX0}
        if floor and (f['tcc'] != 0 or f['tfx'] != 0 or not P.drawable(key | 1<<34)):
            raise SystemExit(f'unsupported AREA01 floor material {key:#x}')
        if not floor and (not P.drawable(key) or f['tfx'] not in (0,2)):
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
