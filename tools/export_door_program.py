#!/usr/bin/env python3
"""Export the user's original door script records without replacing commands."""
import hashlib
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]


def main():
    elf = (ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
    base, size, entry = 0x24dbc0, 0x3c0, 0x24de40
    offset = base - 0x100000 + 0x300
    records = elf[offset:offset + size]
    assert [struct.unpack_from('<I', records, i*64)[0] for i in range(15)] == [
        0x80000007, 10, 11, 0x80000002, 10, 11, 2, 23, 9, 0x8000000b,
        7, 0x4000000d, 7, 9, 0x40000002]
    assert struct.unpack_from('<I', records, 0x24de84 - base)[0] == 0x24dc00
    output = ROOT/'assets/scene_snow/door_original/program.emsc'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(struct.pack('<4s4I', b'EMSC', 1, base, entry, size) + records)
    print('Original door program:15 unmodified records ->', output)


if __name__ == '__main__': main()
