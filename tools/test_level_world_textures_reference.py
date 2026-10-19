#!/usr/bin/env python3
"""Private catalog coverage, disc residency, original GS texels and EMOT layout.

Quick mode checks AREA03's two snapshots. Full mode checks every profile in a
fresh process, including every original page replay and captured mode variant.
No later-area live selector is enabled by these data checks.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

import export_level_world_textures as X
import level_asset_profiles as P


def decode(data):
    assert len(data) >= 16, 'short header'
    magic, version, count, reserved = struct.unpack_from('<4sIII', data)
    assert magic == b'EMOT' and version == 1 and reserved == 0 and count, 'header'
    at, result = 16+24*count, {}
    assert at <= len(data), 'short index'
    for i in range(count):
        key, width, height, offset, reserved = struct.unpack_from('<QIIII', data, 16+24*i)
        assert key not in result and key >> 61 == 0 and reserved == 0, 'duplicate/CLD key'
        assert width == 1 << (key >> 26 & 15) and height == 1 << (key >> 30 & 15), 'dimensions'
        assert 0 < width <= 1024 and 0 < height <= 1024 and offset == at, 'extent'
        size = width*height*4
        assert at+size <= len(data), 'short texels'
        result[key] = data[at:at+size]
        at += size
    assert at == len(data), 'trailing data'
    return result


def reject(call):
    try:
        call()
    except (AssertionError, ValueError, struct.error, SystemExit):
        return
    raise AssertionError('mutation was accepted')


def check(profile, full):
    results = []
    for sub in P.SUBS[profile]:
        C = P.configure(profile, sub)
        import export_area01_world_textures as E
        caps = C.captures()
        expected, _ = X.collect(C, caps, replay=full)
        out = P.output(profile, sub)
        report = json.loads((out/'world_texture_catalog.json').read_text())
        entries = report['textures']
        keys = {int(row['tex0']): row for row in entries}
        assert len(keys) == len(entries) == report['reference_keys'], 'complete unique references'
        assert set(expected) <= set(keys), 'bank/resource/page references absent'
        if full:
            assert set(expected) == set(keys), 'complete replay catalog'
        for key, labels in expected.items():
            assert labels <= set(keys[key]['users']), 'source attribution'
        fixed = {k for k, row in keys.items() if row['storage'] == 'disc'}
        assert fixed == set(keys)-{0} and len(fixed) == report['fixed_textures'], 'fixed/runtime partition'
        assert set(keys)-fixed == ({0} if 0 in expected else set()), 'unresolved key retention'
        loader = E.G.FirstLevel(E.G.Disc(), C.EXTRACT)
        variants = {(v['mode'], v['costume']): v for v in report['variants']}
        assert set(variants) == {(c.ram[0x810707], c.ram[0x810C60]) for c in caps}, 'mode variants'
        checked = mutants = 0
        for pair, variant in variants.items():
            data = (out/variant['file']).read_bytes()
            assert hashlib.sha256(data).hexdigest() == variant['sha256'], 'receipt hash'
            pixels = decode(data)
            assert set(pixels) == fixed, 'no truncated fixed catalog'
            assert pixels == E.O.disc_texels(loader.world(C.AREA, sub, *pair), fixed), 'disc upload replay'
            matching = [c for c in caps if (c.ram[0x810707], c.ram[0x810C60]) == pair]
            E.O.cross_check(pixels, [c.gs for c in matching])
            checked += len(matching)
            reject(lambda: decode(data[:-1])); mutants += 1
            bad = bytearray(data); struct.pack_into('<I', bad, 32, 0)
            reject(lambda: decode(bad)); mutants += 1
            changed = dict(pixels); key = next(iter(changed)); original = changed[key]
            changed[key] = bytes([original[0] ^ 1])+original[1:]
            reject(lambda: E.O.cross_check(changed, [matching[0].gs])); mutants += 1
        assert checked == len(caps), 'every selected freeze'
        results.append(dict(profile=profile, sub=sub, textures=len(fixed), references=len(keys),
                            variants=len(variants), captures=checked, mutants=mutants, page_replay=full))
        print(f'{profile}/{sub}: PASS {len(fixed)} textures, {checked} original freezes, '
              f'{len(variants)} variants, {mutants} rejected mutations', flush=True)
    target = P.ROOT/'build/levels/world-texture-reference'/profile
    target.mkdir(parents=True, exist_ok=True)
    (target/('full.json' if full else 'quick.json')).write_text(json.dumps(results, indent=2)+'\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--profile', choices=P.SUBS)
    args = ap.parse_args()
    full = os.getenv('EM_TEST_FULL', '') not in ('', '0')
    if args.profile:
        check(args.profile, full)
    else:
        for profile in P.SUBS if full else ('03',):
            subprocess.run([sys.executable, __file__, '--profile', profile], cwd=P.ROOT, check=True)


if __name__ == '__main__':
    main()
