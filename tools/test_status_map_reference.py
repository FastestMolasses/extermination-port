#!/usr/bin/env python3
"""The MAP page's model bank against the ORIGINAL instructions
(docs/STATUS_PAGES.md section 7).

em_status_models serves 002101C0's model callees over the export of
tools/export_status_map.py, and em_status_pages_live gives 002101C0 the
word D_0028A570 from the EMSP relocation record of
tools/export_status_pages.py. This test ties both exports to the original:

  1. D_0028A570: the EMSP's relocation record for module 0x1E is slot 0x38
     at 0x0028A570 = D_0028A490 + 4 * 0x38, valued D_0028A748 + the slot's
     entry offset in the disc descriptor (001FF830 state 0's default case
     takes D_0028A748 as the destination; state 7 relocates), and
     D_0028A748 is the loader cursor every AREA11 capture holds (route
     beats 00..14 and the status-hub capture).
  2. 001C6120(D_0028A570, code): the original instructions run over the
     disc's bank placed at that address, for every code of 002101C0's
     table (0x2658C0, from the ELF), every directory code, and the masks
     the original applies (bit 15 and bits above 16 set): the result must
     be D_0028A570 + the model's EMMP offset, which is what
     em_status_models_call returns.
  3. 001C6150(model): the original's byte at +8 must be the EMMP node count;
     the EMMP radius is the model's +0x20 word (001D8270's gate reads it)
     and the EMMP skeleton records (parent, bind) are the model's records at
     +0xC (001C62C0 reads them).

Runs in about a second (the disc read and a few hundred instructions).
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import export_disc_textures_gs as G  # noqa: E402
import export_module_loader as EML  # noqa: E402
from test_player_slide_reference import EE, RETURN, read_elf  # noqa: E402

MODULE, SLOT, D_0028A490, D_0028A748 = 0x1E, 0x38, 0x0028A490, 0x0028A748
TABLE = 0x002658C0
CAPTURES = [ROOT.parent / 'Extermination/build/s87/route' / b for b in (
    '00_panel_no_battery', '01_battery', '03_panel_power', '08_truck_crossing', '14_roger_encounter')]
CAPTURES.append(ROOT.parent / 'Extermination/build/startup-reference/status-hub')


def u32(b, a):
    return struct.unpack_from('<I', b, a)[0]


def emsp_relocations(path):
    data = path.read_bytes()
    assert data[:4] == b'EMSP' and u32(data, 4) == 2, 'status_pages.emsp is not version 2 (re-export)'
    p = 12
    windows = u32(data, p)
    p += 4
    for _ in range(windows):
        p += 8 + u32(data, p + 4)
    p += 4 << 20                 # the world image
    p += (4 << 20) // 256 // 8   # its covered-block bitmap
    steps = u32(data, p)
    p += 4
    for _ in range(steps):
        p += 8 + u32(data, p + 4)
    count = u32(data, p)
    p += 4
    out = [struct.unpack_from('<III', data, p + 12 * i) for i in range(count)]
    assert p + 12 * count == len(data)
    return out


def emmp(path):
    data = path.read_bytes()
    assert data[:4] == b'EMMP' and u32(data, 4) == 1
    count, p, models = u32(data, 8), 12, {}
    for _ in range(count):
        code, offset, nodes, radius = struct.unpack_from('<IIII', data, p)
        p += 16
        records = []
        for _ in range(nodes):
            parent = struct.unpack_from('<i', data, p)[0]
            records.append((parent, data[p + 4:p + 0x44]))
            p += 0x44
        models[code] = (offset, nodes, radius, records)
    assert p == len(data)
    return models


def main():
    checks = 0
    elf = read_elf()
    disc = G.Disc()
    block = G.top_block(disc, MODULE)
    table = G.module_table_index(block, 'module')
    slots = dict(block.slot_entries(table))
    _label, region, size = G.module_slot(disc, block, SLOT, table)
    bank = disc.read(block.offset + region, size)

    # 1. D_0028A570
    seed = EML.AREA11_SEEDS[6]
    for cap in CAPTURES:
        ram = (cap / 'eeMemory.bin').read_bytes()
        assert u32(ram, D_0028A748) == seed, (cap.name, hex(u32(ram, D_0028A748)))
        checks += 1
    relocs = emsp_relocations(ROOT / 'assets/status_pages/status_pages.emsp')
    assert relocs == [(MODULE, D_0028A490 + 4 * SLOT, seed + slots[SLOT])], relocs
    bank_address = seed + slots[SLOT]
    checks += 1

    # 2. 001C6120 over the bank at D_0028A570
    models = emmp(ROOT / 'assets/status_map/map_models.emmp')
    base = EE(elf)                         # the ELF loaded, the bank placed
    base.write(bank_address, bank)
    ram = bytes(base.mem)
    elf_codes = [struct.unpack_from('<I', elf, TABLE - 0x100000 + 0x300 + 4 * i)[0] for i in range(22)]
    codes = sorted(set(elf_codes) | set(models) | {c | 0x8000 for c in models} |
                   {c | 0x10000 for c in models})
    for code in codes:
        ee = EE(elf, ram, bytes(0x4000))
        ee.r[4], ee.r[5], ee.r[31] = bank_address, code, RETURN
        ee.run(0x001C6120)
        got = ee.r[2] & 0xFFFFFFFF
        want = bank_address + models[code & 0x7FFF][0]
        assert got == want, (hex(code), hex(got), hex(want))
        checks += 1

    # 3. 001C6150, the radius and the skeleton records
    for code, (offset, nodes, radius, records) in sorted(models.items()):
        model = bank_address + offset
        ee = EE(elf, ram, bytes(0x4000))
        ee.r[4], ee.r[31] = model, RETURN
        ee.run(0x001C6150)
        assert (ee.r[2] & 0xFF) == nodes, (code, ee.r[2], nodes)
        m = bank[offset:]
        assert u32(m, 0x20) == radius, code
        skeleton = u32(m, 0xC)
        for i, (parent, bind) in enumerate(records):
            rec = skeleton + 0x50 * i
            assert struct.unpack_from('<h', m, rec + 4)[0] == parent and m[rec + 0x10:rec + 0x50] == bind
        checks += 3
    print(f'status map reference: D_0028A570 = {bank_address:#x} (module 0x1E slot 0x38), '
          f'{len(codes)} 001C6120 codes, {len(models)} models (001C6150, radius, skeleton), '
          f'{checks} checks against the original: PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
