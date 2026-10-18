#!/usr/bin/env python3
"""Complete recorded bank/page texture references and disc-backed texels.

Every source reference stays in the JSON catalog, including zero-key geometry
that needs a live GS read or an untextured draw class. Such entries cannot be
represented by fixed EMOT pixels and explicitly block native readiness. Each
recorded texture mode/costume pair gets its own disc-derived EMOT; every texel
is compared with all matching original GS freezes. Captures supply references
and independent comparisons, never exported pixels.
"""
import argparse
import hashlib
import json
import struct

from level_asset_audit import private_assets
import level_asset_profiles as P
import level_texture_page as Page


def resource_bytes(E, capture, image, ident, address, files, who):
    data, label = E.resource_bytes(capture, image, ident, address, files, who)
    # Some later resources span consecutive delivered files. The original's
    # own header, not the extractor's file boundary, defines the model extent.
    blocks, qwc, bones, extent = struct.unpack_from('<4I', data)
    if qwc == blocks * E.O.BLOCK_QWORDS and extent == 0x40 + 16*qwc and bones < 256:
        needed = extent + 0x50*bones
        if needed > len(data):
            data = image.read(address, needed)
            assert data == capture.ram[address:address+needed], 'cross-file model delivery'
    return data, label


def resources(E, capture, image, keys):
    """Bound resource models, using the original node and resource tables."""
    C, ram = E.A, capture.ram
    entries = {i: C.u32(ram, E.RESOURCES+4*i) for i in range(E.RESOURCE_COUNT)}
    ids = {}
    for ident, address in entries.items():
        if address:
            ids.setdefault(address, ident)
    files = {int(p.stem.split('_id')[1], 16): p
             for p in (C.EXTRACT/E.GLOBAL_CHUNK).glob('f*_id*.bin')}
    for k in range(E.CAPACITY):
        node = E.POOL + E.RECORD*k
        if not C.u32(ram, node+0x14):
            continue
        owner, address = C.u32(ram, node+0x10), C.u32(ram, node+0x44)
        ident = ids.get(address)
        if ident is not None:
            data, label = resource_bytes(E, capture, image, ident, address, files, 'bound owner')
            E.resource_model_tex0(data, ident, f'resource {ident:#x} ({label}, owner {owner:08X})', keys)
        if C.AREA == 1 and owner in E.FACE_OWNERS:
            kind = ram[node+0x0D]
            assert kind in E.FACE_BY_TYPE, 'unestablished face type'
            ident = E.FACE_BY_TYPE[kind]
            data, label = resource_bytes(E, capture, image, ident, entries[ident], files, 'bound face')
            E.resource_model_tex0(data, ident, f'face {ident:#x} ({label})', keys)


def collect(C, caps, replay=True):
    import export_area01_world_textures as E
    keys, elf = {}, C.read_elf()
    cap = caps[0]
    image = C.LoadedImage(C.build_load_map(cap)[0])
    table = C.u32(cap.ram, 0x28A59C)
    rows = sorted(image.map)
    k = next(i for i, row in enumerate(rows) if row[0] <= table < row[0]+row[3])
    end = rows[k][0] + rows[k][3]
    for row in rows[k+1:]:
        if row[0] != end:
            break
        end = row[0] + row[3]
    bank = image.read(table, end-table)
    count = C.u32(bank, 0)
    assert 0 < count < 256, 'model bank count'
    for ident in range(count):
        off = C.s32(bank, 4+4*ident) >> 2 << 2
        E.O.block_tex0(bank, off, E.W.model_record(bank, off, ident)[0], f'world model {ident}', keys)
    objects = E.L.bank_objects(image.read, C.u32(cap.ram, 0x28A5A0))
    for ident, address, blocks in objects[1:]:
        E.O.block_tex0(image.read(address, 0x40+0x820*blocks), 0, blocks, f'static object {ident}', keys)
    # func_001D5370.c's area/sub arms. Elsewhere this global is stale.
    dynamic = []
    if (C.AREA, C.SUB) in ((1, 0), (4, 0), (6, 0), (15, 0)):
        dynamic = E.L.dynamic_entries(image.read, C.u32(cap.ram, 0x28A5A4))
        for ident, address in dynamic:
            data = image.read(address, 0x860)
            assert C.u32(data, 0) == 1, 'dynamic model extent'
            for vertex in range(3):
                key = struct.unpack_from('<Q', data, 0x50+64*vertex)[0] & E.O.CLD_MASK
                keys.setdefault(key, set()).add(f'dynamic record {ident}')
    E.L.check_kicks(caps, objects, dynamic)
    for capture in caps:
        image = C.LoadedImage(C.build_load_map(capture)[0])
        E.library_tex0(capture, keys)
        resources(E, capture, image, keys)
    shared = {}
    E.O.player_tex0(C.EXTRACT, shared)
    for key, labels in shared.items():
        labels = {s for s in labels if not s.lower().startswith('roger')}
        if labels and E.P.drawable(key):
            keys.setdefault(key, set()).update(labels)
    for key, labels in E.P.tex0_set(elf, (C.EXTRACT/'OVERLAY/AREA11.BIN').read_bytes()).items():
        labels = {s for s in labels if 'flame descriptor' not in s}
        if labels:
            keys.setdefault(key, set()).update(labels)
    if C.AREA == 0:
        import export_area00_world_textures as A0
        overlay = A0.A0.read_overlay()
        for address in A0.PACKETS:
            key = struct.unpack_from('<Q', overlay, address-0x823500+0x70)[0] & E.O.CLD_MASK
            keys.setdefault(key, set()).add(f'AREA00 packet {address:08X}')
        keys.setdefault(E.FLOOR_TEX0[2] & E.O.CLD_MASK, set()).add('AREA00 ripple surface')
    if C.AREA == 1:
        for key in E.FLOOR_TEX0:
            keys.setdefault(key & E.O.CLD_MASK, set()).add('AREA01 floor/ripple surface')
    page_rows = []
    if replay:
        for capture in caps:
            page, skip = Page.captured(capture)
            for address, micro, count in page.mpg:
                off = address - 0x100000 + 0x300
                assert capture.ram[address:address+8*count] == elf[off:off+8*count], 'uploaded original program'
            drawn = set()
            for prim, state, _ in page.gs.prims:
                if prim & 16:
                    key = state[0] & E.O.CLD_MASK
                    keys.setdefault(key, set()).add(f'page {capture.name}')
                    drawn.add(key)
            page_rows.append(dict(capture=capture.name, textures=len(drawn),
                                  skipped_owner_units=len(skip), batches=sum(page.mscal_counts.values())))
    return keys, dict(models=C.u32(bank, 0), static_objects=len(objects)-1,
                      dynamic_entries=len(dynamic), pages=page_rows)


def export(profile, sub, iso=None):
    C = P.configure(profile, sub)
    import export_area01_world_textures as E
    caps = C.captures()
    keys, inventory = collect(C, caps)
    fixed = {k: labels for k, labels in keys.items() if k != 0}
    for key in fixed:
        assert E.P.drawable(key | (1 << 34)) and E.O.tex0_fields(key)['tfx'] in (0, 2), 'unsupported fixed material'
    loader = E.G.FirstLevel(E.G.Disc(iso), C.EXTRACT)
    variants, rows = {}, []
    for capture in caps:
        pair = (capture.ram[0x810707], capture.ram[0x810C60])
        if pair not in variants:
            variants[pair] = E.O.disc_texels(loader.world(C.AREA, sub, *pair), fixed)
        E.O.cross_check(variants[pair], [capture.gs])
    out = P.output(profile, sub)
    out.mkdir(parents=True, exist_ok=True)
    for (mode, costume), pixels in variants.items():
        name = f'world_textures_mode{mode}_costume{costume}.emot'
        data = E.O.emot(pixels)
        path = out/name
        assert not path.is_symlink(), 'private texture file must not be a symlink'
        path.write_bytes(data)
        rows.append(dict(mode=mode, costume=costume, file=name, bytes=len(data),
                         sha256=hashlib.sha256(data).hexdigest(),
                         captures=[c.name for c in caps if (c.ram[0x810707], c.ram[0x810C60]) == (mode, costume)]))
    report = dict(profile=profile, area=C.AREA, sub=sub, reference_keys=len(keys), fixed_textures=len(fixed),
                  unresolved_keys=len(keys)-len(fixed), native_ready=False,
                  scope='all delivered bank references, bound library/resources, shared producers, and recorded pages',
                  limitation='zero references retained; draw-class/live-GS binding and unrecorded routes need proof',
                  capture_count=len(caps), inventory=inventory, variants=rows,
                  textures=[dict(tex0=str(k), users=sorted(keys[k]),
                                 storage='disc' if k else 'requires-draw-class-or-live-GS') for k in sorted(keys)])
    target = out/'world_texture_catalog.json'
    assert not target.is_symlink(), 'private catalog must not be a symlink'
    target.write_text(json.dumps(report, indent=2)+'\n')
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--profile', choices=P.SUBS, required=True)
    ap.add_argument('--iso')
    args = ap.parse_args()
    private_assets(P.ROOT)
    for sub in P.SUBS[args.profile]:
        r = export(args.profile, sub, args.iso)
        print(f'{args.profile}/{sub}: PASS {r["fixed_textures"]} fixed textures, '
              f'{r["unresolved_keys"]} unresolved references, {len(r["variants"])} variants, '
              f'{r["capture_count"]} exact GS freezes', flush=True)


if __name__ == '__main__':
    main()
