#!/usr/bin/env python3
"""Export the original AREA11 opening camera from a user's extracted bank.

The bank can be a standalone resource or embedded at --bank-offset in its
container. Offsets are resolved by the original 001C6120 directory rule.
Output is disc-derived and must stay in ignored assets/build directories.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def export(source: Path, bank_offset: int, output: Path):
    data = source.read_bytes()
    if bank_offset < 0 or bank_offset + 12 > len(data):
        raise ValueError('bank is outside source')
    count, first, second = struct.unpack_from('<3I', data, bank_offset)
    first &= ~3
    second &= ~3
    if count < 2 or first < 4 + count * 4 or second <= first + 16:
        raise ValueError('invalid camera/animation bank directory')
    start, end = bank_offset + first, bank_offset + second
    if end > len(data) or (end - start - 16) % 32:
        raise ValueError('invalid camera track extent')
    duration = struct.unpack_from('<f', data, start)[0]
    samples = (end - start - 16) // 32
    if duration != samples - 1:
        raise ValueError('camera track must include one lookahead sample')
    payload = data[start + 16:end]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(struct.pack('<4sIIf', b'EMCC', 1, samples, duration) + payload)
    info = {'source': str(source), 'source_sha256': hashlib.sha256(data).hexdigest(),
            'bank_offset': bank_offset, 'camera_offset': start,
            'duration': duration, 'samples': samples,
            'sample_sha256': hashlib.sha256(payload).hexdigest()}
    output.with_suffix('.json').write_text(json.dumps(info, indent=2) + '\n')
    return info


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--bank-offset', type=lambda s: int(s, 0), default=0)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(export(args.source, args.bank_offset, args.out), indent=2))
