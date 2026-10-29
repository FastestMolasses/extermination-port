#!/usr/bin/env python3
"""AREA02 catalogs from disc uploads; original GS captures verify every texel.

export_area00_world_textures.py's method for AREA02 (export_area02_common):
both recorded subareas are exported independently. The static/model banks,
resource and library models and the shared player/page sources supply TEX0
references. AREA02's overlay code hands no source record to 001CFBE0 (its
sprites go through 001CD520 with an inline GIF tag), so there are no AREA02
packet descriptors, and the AREA00 page replay's two surface materials are
not added (no AREA02 capture is known to draw them; a draw that needs one
fails). No captured texels supply an output.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import export_area02_common as A0
import export_area01_world_textures as E
import reference_root as refs


# No AREA02 overlay owner hands a source record to 001CFBE0.
PACKETS = ()


def collect(image, caps, elf, overlay):
    out = {}
    cap = caps[0]
    table = E.A.u32(cap.ram, 0x28A59C)
    bank = image.read(table, image.span()[1] - table)
    count = E.A.u32(bank, 0)
    assert 0 < count < 256
    for ident in range(count):
        off = E.A.s32(bank, 4 + 4*ident) >> 2 << 2
        blocks = E.W.model_record(bank, off, ident)[0]
        E.O.block_tex0(bank, off, blocks, f'AREA02 model {ident}', out)
    for ident, address, blocks in E.L.bank_objects(image.read, E.A.u32(cap.ram, 0x28A5A0))[1:]:
        data = image.read(address, 0x40 + 0x820*blocks)
        E.O.block_tex0(data, 0, blocks, f'AREA02 static object {ident}', out)
    # As in AREA00, D_0028A5A4 (the dynamic list) is not read here.
    for capture in caps:
        E.library_tex0(capture, out)
        E.resource_tex0(capture, image, out)
    # A zero TEX0 is not a texture (AREA00's model 0 has one): a draw using
    # that key must fail, not acquire substitute pixels.
    out.pop(0, None)
    shared = {}
    E.O.player_tex0(E.A.EXTRACT, shared)
    for key, labels in shared.items():
        labels = {s for s in labels if not s.lower().startswith('roger')}
        if labels and E.P.drawable(key):
            out.setdefault(key, set()).update(labels)
    page = E.P.tex0_set(elf, (E.A.EXTRACT/'OVERLAY/AREA11.BIN').read_bytes())
    for key, labels in page.items():
        labels = {s for s in labels if 'flame descriptor' not in s}
        if labels:
            out.setdefault(key, set()).update(labels)
    for address in PACKETS:
        key = struct.unpack_from('<Q', overlay, address - 0x823500 + 0x70)[0] & E.O.CLD_MASK
        out.setdefault(key, set()).add(f'AREA02 packet {address:08X}')
    for key in out:
        fields = E.O.tex0_fields(key)
        surface = key in {E.FLOOR_TEX0[i] & E.O.CLD_MASK for i in (0, 2)}
        if surface:
            valid = E.P.drawable(key | (1 << 34)) and fields['tcc'] == 0 and fields['tfx'] == 0
        else:
            valid = E.P.drawable(key) and fields['tfx'] in (0, 2)
        if not valid:
            raise ValueError(f'unsupported AREA02 material {key:#x}: {sorted(out[key])}')
    return out


def export(sub, out, iso=None, full=True):
    A = A0.configure(sub)
    all_caps = A0.captures(sub)
    image = A.LoadedImage(A.build_load_map(all_caps[0])[0])
    keys = collect(image, all_caps, A.read_elf(), A0.read_overlay())
    loader = E.G.FirstLevel(E.G.Disc(iso), A.EXTRACT)
    pixels = E.O.disc_texels(loader.world(2, sub), keys)
    caps = all_caps if full else [all_caps[0], all_caps[-1]]
    if not all(c.gs for c in caps):
        raise ValueError('AREA02 capture lacks its GS freeze')
    E.O.cross_check(pixels, [c.gs for c in caps])
    data = E.O.emot(pixels)
    if any(p.is_symlink() for p in out.parents):
        raise ValueError(f'{out}: symlink parent')
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.is_symlink():
        out.unlink()
    out.write_bytes(data)
    report = dict(area=2, sub=sub, reference=refs.label(), count=len(keys), bytes=len(data),
                  captures=[c.name for c in caps], sources=loader.sources,
                  textures=[dict(tex0=f'{key:#018x}', users=sorted(keys[key]),
                                 sha256=hashlib.sha256(pixels[key]).hexdigest()) for key in sorted(keys)])
    path = out.with_suffix('.json')
    if path.is_symlink():
        path.unlink()
    path.write_text(json.dumps(report, indent=2) + '\n')
    return data, report


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', type=Path, default=A0.ROOT/'assets')
    ap.add_argument('--iso', type=Path)
    args = ap.parse_args()
    for sub in A0.SUBS:
        _, report = export(sub, args.out/f'area02_sub{sub}_world_textures.emot', args.iso)
        print(f'AREA02/{sub}: {report["count"]} textures, {len(report["captures"])} exact GS freezes')


if __name__ == '__main__':
    main()
