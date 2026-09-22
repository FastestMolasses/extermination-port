#!/usr/bin/env python3
"""Export the two original pickup program pairs into ignored local assets."""
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
ELF_SHA = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'


def main():
    elf = (ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    rows = []
    for callback, base in ((0x15AFA0, 0x2482C0), (0x219550, 0x266620)):
        payload = elf[base-0x100000+0x300:base-0x100000+0x300+0x280]
        target = ROOT/f'assets/scene_snow/pickup_{callback:08x}.emsc'
        data = struct.pack('<4s4I', b'EMSC', 1, base, base, len(payload))+payload
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        rows.append(dict(path=str(target.relative_to(ROOT)), base=f'{base:08X}',
            size=len(data), sha256=hashlib.sha256(data).hexdigest()))
    out = ROOT/'build/pickup_owner_reference'; out.mkdir(parents=True, exist_ok=True)
    (out/'export.json').write_text(json.dumps(dict(elf_sha256=ELF_SHA, files=rows), indent=2)+'\n')
    print('Exported two original pickup script pairs (660 bytes each).')


if __name__ == '__main__': main()
