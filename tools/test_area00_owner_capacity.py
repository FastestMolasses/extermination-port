#!/usr/bin/env python3
"""Opt-in check of the shared owner-view capacity against AREA00's captures.

Compiles em_area11_boxes.c's view table under ASan/UBSan (first-free slot
order, refusal past the bound, freed and generation reuse) and counts the
crates and drums every retained AREA00 capture holds. --require-supported
fails when the bound is below that recorded minimum. Since 2026-10-19 the
bound is the actor pool's 256 (LEVEL3_BINDING.md). This is no gameplay
certification. Run natively on arm64 macOS.
"""
import argparse
import json
import os
import pathlib
import struct
import subprocess

import export_area00_common as A

OUT = A.ROOT / 'build/levels/area00-owner-capacity'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--require-supported', action='store_true',
                        help='fail if the current owner table cannot cover the recorded minimum')
    parser.add_argument('--out', help='a new receipt folder (default: build/levels/area00-owner-capacity)')
    args = parser.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT
    if not out.is_absolute():
        out = A.ROOT / out
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'capacity'
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-ffp-contract=off', '-Wl,-dead_strip',
                    '-Isrc', 'tests/area00_owner_capacity_test.c', '-o', str(binary)],
                   cwd=A.ROOT, check=True)
    result = subprocess.run([str(binary)], check=True, capture_output=True, text=True,
                            env=dict(os.environ, UBSAN_OPTIONS='halt_on_error=1'))
    report = json.loads(result.stdout)
    rows = []
    for capture, sub in A.all_captures():
        counts = {0x1551B0: 0, 0x156620: 0}
        for slot in range(256):
            address = 0x7A5640 + slot * 0x2F0
            callback = struct.unpack_from('<I', capture.ram, address + 0x10)[0]
            if capture.ram[address] and callback in counts:
                counts[callback] += 1
        rows.append(dict(capture=capture.name, sub=sub, crates=counts[0x1551B0],
                         drums=counts[0x156620], minimum_views=sum(counts.values())))
    report['captures'] = rows
    report['maximum_observed_minimum'] = max(r['minimum_views'] for r in rows)
    report['capacity_sufficient_for_recorded_minimum'] = report['maximum_observed_minimum'] <= report['box_capacity']
    report['result'] = ('recorded minimum fits; no gameplay certification' if
                        report['capacity_sufficient_for_recorded_minimum'] else
                        'capacity blocker reproduced; no gameplay certification')
    (out / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Owner-view bound: {report["box_capacity"]}; '
          f'AREA00 requires at least {report["maximum_observed_minimum"]} views '
          f'({"fits" if report["capacity_sufficient_for_recorded_minimum"] else "does NOT fit"}). '
          'ASan/UBSan refusal and slot reuse checks pass.')
    return int(args.require_supported and not report['capacity_sufficient_for_recorded_minimum'])


if __name__ == '__main__':
    raise SystemExit(main())
