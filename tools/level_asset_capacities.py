#!/usr/bin/env python3
"""Measure private recorded-area catalogs against explicit native bounds.

Pool occupancies are maxima over retained snapshots, not whole-route peaks.
The SFX union compares full normalized sample bytes and operations, detects
scope conflicts, and reports the hypothetical all-scope size without changing
the live registry. Nothing in this report certifies native later-area drawing.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

import level_asset_profiles as P

OUT = P.ROOT/'build/levels/capacities'


def sfx_path(profile, sub):
    if profile in ('13x', '19x'):
        return P.ROOT/f'build/area{profile}/assets/tree/sub{sub}/sfx/sfx_registry.emsr'
    if profile == '03':
        return P.output(profile, sub)/'sfx/sfx_registry.emsr'
    if profile == '01':
        return P.ROOT/'assets/area01/sfx/sfx_registry.emsr'
    return P.ROOT/f'assets/area{profile}/sub{sub}/sfx/sfx_registry.emsr'


def sfx(data):
    magic, version, ns, ne, nl, reserved = struct.unpack_from('<4sIIIII', data)
    assert magic == b'EMSR' and version == 2 and not reserved and nl == 0x240
    at = 24+2*nl
    ladder = data[24:at]
    entries = []
    for _ in range(ne):
        row = data[at:at+16]
        assert len(row) == 16
        ident, area, sub, state, count, reason, bank, zero = struct.unpack('<IhhBBHHH', row)
        assert not zero and count <= 32
        at += 16
        ops = [data[at+32*i:at+32*(i+1)] for i in range(count)]
        at += count*32
        assert all(len(op) == 32 for op in ops)
        entries.append(((area, sub, ident), (state, reason, bank), ops))
    samples = []
    for _ in range(ns):
        frames, loop = struct.unpack_from('<II', data, at)
        size = 8+2*(frames+(frames-loop if loop != 0xFFFFFFFF else 0))
        assert frames and (loop == 0xFFFFFFFF or loop < frames) and at+size <= len(data)
        samples.append(hashlib.sha256(data[at:at+size]).hexdigest())
        at += size
    assert at == len(data)
    normalized = {}
    for key, header, ops in entries:
        normalized_ops = []
        for op in ops:
            if op[2] == 1:  # EM_SFX_OP_KEY_ON: replace the local sample index by its complete content hash.
                index = struct.unpack_from('<H', op, 6)[0]
                assert index < len(samples)
                normalized_ops.append((op[:6].hex(), samples[index], op[8:].hex()))
            else:
                normalized_ops.append(op.hex())
        assert key not in normalized
        normalized[key] = hashlib.sha256(repr((header, normalized_ops)).encode()).hexdigest()
    return dict(samples=ns, entries=ne, bytes=len(data), sample_hashes=sorted(set(samples)),
                entry_hashes={','.join(map(str,k)):v for k,v in normalized.items()},
                ladder_sha256=hashlib.sha256(ladder).hexdigest())


def measure(profile):
    rows = []
    for sub in P.SUBS[profile]:
        C = P.configure(profile, sub)
        caps = C.captures()
        actors, effects = [], []
        for cap in caps:
            # 001AF8E0/001AFB70 original actor arena; the existing resource
            # readers use the same allocated-self marker at +0x14.
            used = sum(C.u32(cap.ram, 0x7A5640+0x2F0*i+0x14) != 0 for i in range(256))
            assert used+C.u16(cap.ram, 0x275BC8) == 256, 'actor free-count agrees with node markers'
            actors.append(used)
            # 001F40C0's live entity short, not placement-row count.
            effects.append(sum(C.u16(cap.ram, 0x7709C0+0x90*i+0x80) == 0 for i in range(128)))
        textures = json.loads((P.output(profile, sub)/'world_texture_catalog.json').read_text())
        shadow = json.loads((P.output(profile, sub)/'shadow_receivers.json').read_text())
        sound = sfx(sfx_path(profile, sub).read_bytes())
        rows.append(dict(profile=profile, area=C.AREA, sub=sub, captures=len(caps),
                         texture_references=textures['reference_keys'], fixed_textures=textures['fixed_textures'],
                         unresolved_textures=textures['unresolved_keys'], texture_variants=len(textures['variants']),
                         texture_bytes=max(v['bytes'] for v in textures['variants']),
                         actor_snapshot_max=max(actors), effect_snapshot_max=max(effects),
                         shadow_objects=shadow['objects'], shadow_blocks=shadow['blocks'],
                         shadow_bytes=(P.output(profile, sub)/'shadow_receivers.emsr').stat().st_size,
                         sfx=sound))
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT/f'{profile}.json').write_text(json.dumps(rows, indent=2)+'\n')
    print('PASS capacity measurements', profile, len(rows), 'subareas', flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--profile', choices=P.SUBS)
    args = ap.parse_args()
    if args.profile:
        measure(args.profile)
        return
    for profile in P.SUBS:
        subprocess.run([sys.executable, __file__, '--profile', profile], cwd=P.ROOT, check=True)
    rows = [r for profile in P.SUBS for r in json.loads((OUT/f'{profile}.json').read_text())]
    registries = [sfx((P.ROOT/'assets/sfx/sfx_registry.emsr').read_bytes())] + [r['sfx'] for r in rows]
    samples, entries, conflicts, ladders = set(), {}, set(), set()
    for registry in registries:
        samples.update(registry['sample_hashes']); ladders.add(registry['ladder_sha256'])
        for key, value in registry['entry_hashes'].items():
            if key in entries and value != entries[key]:
                conflicts.add(key)
            entries[key] = value
    assert len(ladders) == 1, 'same original pitch ladder'
    report = dict(scope='retained snapshots and complete private catalogs; not whole-route runtime high-water proof',
                  bounds=dict(texture=1024, sfx_entries=4096, sfx_samples=1024, actor=256,
                              effect_entity=128, selected_receivers=512),
                  all_scope_sfx=dict(entries=len(entries), samples=len(samples), conflicting_entries=len(conflicts)),
                  profiles=rows)
    (OUT/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print('all-scope SFX:',len(entries),'entries,',len(samples),'distinct samples,',len(conflicts),'scope conflicts')


if __name__ == '__main__':
    main()
