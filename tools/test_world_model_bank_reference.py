#!/usr/bin/env python3
"""Area-reset world-bank selection against original instructions and resources.

Live input is EMWM only. The oracle reads the user's pinned ELF/captures;
001C6120, 001B0EA0 and 001B0FD0 run their original instructions. The
existing module-loader harness additionally delivers AREA11 -> AREA01 ->
AREA11 from disc, comparing calls/state/memory to the original loader.
Its sound-transfer boundary returns the end of the supplied bank container
(the separately verified 001FB3E0 expression); no sound transfer is claimed.
Reports contain counts only. Original bytes stay in ignored build/ inputs.
"""
import ctypes as C
import json
from pathlib import Path
import struct
import subprocess
import time

import test_module_loader_reference as ML
import test_owner_services_reference as OS
from test_owner_draw_reference import WorldModels
from reference_mode import FULL, banner

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
OUT = ROOT / 'build/level2/oracles/model_bank'
CAPS = {11: DECOMP / 'build/s87/route/03_panel_power',
        1: DECOMP / 'build/s87/route_a01/a01_00_train_room'}
BANKS = {11: ROOT / 'assets/scene_snow/world_models.emwm',
         1: ROOT / 'assets/area01/world_models.emwm'}


def u32(b, at): return struct.unpack_from('<I', b, at)[0]


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    libpath = OUT / 'bank.dylib'
    sources = ['tests/world_model_bank_bridge.c', 'src/game/em_owner_services_original.c',
               'src/game/em_owner_draw_original.c', 'src/game/em_roger_actor_original.c',
               'src/game/em_startup_load_gaps.c']
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', '-Wl,-dead_strip',
                    '-Wl,-exported_symbol,_bank_test_*', *sources, '-o', str(libpath)],
                   cwd=ROOT, check=True)
    lib = C.CDLL(str(libpath))
    lib.bank_test_bind.argtypes = [C.c_char_p, C.c_uint32]
    lib.bank_test_view.restype = C.POINTER(WorldModels)
    lib.bank_test_lookup.argtypes = [C.c_uint32, C.c_uint32, C.POINTER(C.c_uint32)]
    lib.bank_test_owner.argtypes = [C.c_uint32, C.c_int]
    lib.bank_test_owner.restype = C.POINTER(OS.Owner)
    lib.bank_test_owner_method.restype = C.c_uint32
    lib.bank_test_fields.argtypes = [C.POINTER(C.c_uint32)] * 3
    lib.bank_test_seed_library.argtypes = [C.c_uint32, C.c_void_p, C.c_uint32, C.c_uint32]
    lib.bank_test_library.restype = C.c_void_p
    lib.bank_test_library_word.restype = C.c_uint32
    return lib


def original_owner(elf, ram, model_id, door):
    o = OS.EE(elf, ram)
    actor = 0x600000
    o.put(actor, bytes(0x300))
    o.put(actor + 0x0D, bytes([model_id]))
    o.put32(0x275B48, actor)
    o.put32(0x275B44, actor)
    o.put32(0x275BCC, 0x480)
    o.put32(0x275BD0, 0x7D4640)
    o.put(0x7D5840, bytes(0x480 * 0xD0))
    o.put(0x7D4640, struct.pack('<1152I', *(0x7D5840 + k * 0xD0 for k in range(0x480))))
    o.run(0x1B0EA0 if door else 0x1B0FD0, (actor,))
    assert o.g(2) & OS.M32 == 0
    return o, actor


def check_bank(lib, elf, area, ram, counts):
    data = BANKS[area].read_bytes()
    token, size = struct.unpack_from('<II', data, 8)
    assert u32(ram, 0x28A490 + 4 * 0x43) == token
    assert data[32:] == ram[token:token + size]
    bank = lib.bank_test_view().contents
    assert bank.table_address == token and bank.span_size == size
    assert C.string_at(bank.span, size) == data[32:]
    for ident in range(bank.model_count):
        m = bank.models[ident]
        assert m.address == token + (u32(ram, token + 4 + 4 * ident) & ~3)
        assert C.string_at(m.bytes, m.size) == ram[m.address:m.address + m.size]
        for k in range(m.model.bone_count):
            r = m.address + u32(ram, m.address + 0xC) + 0x50 * k
            assert m.model.skeleton[k].parent == struct.unpack_from('<h', ram, r + 4)[0]
            assert bytes(m.model.skeleton[k].bind) == ram[r + 0x10:r + 0x50]
            counts['skeleton_records'] += 1
        for masked in (ident, ident | 0x8000, ident | 0x10000, ident | 0xFFFF8000):
            o = OS.EE(elf, ram)
            o.run(0x1C6120, (token, masked))
            out = C.c_uint32()
            assert lib.bank_test_lookup(token, masked, C.byref(out)) == 0
            assert out.value == o.g(2) & OS.M32 == m.address
            counts['original_lookups'] += 1
        for door in (0, 1):
            lib.bank_test_reset()
            owner = lib.bank_test_owner(ident, door)
            assert owner
            v = owner.contents
            assert C.addressof(v.model.contents) == C.addressof(m.model)
            o, a = original_owner(elf, ram, ident, door)
            assert (v.lifecycle, v.bones_held, v.bone_count) == tuple(o.load(a + x, 1) for x in (4, 9, 0xC))
            assert lib.bank_test_owner_method() == o.load(a + 0x4C) == 0x1CAA00
            fields = [C.c_uint32() for _ in range(3)]
            assert lib.bank_test_fields(*(C.byref(x) for x in fields)) == (0 if door else 1)
            if not door:
                assert [x.value for x in fields] == [o.load(a + x) for x in (0x40, 0x44, 0x4C)]
            assert lib.bank_test_slot_count() == o.load(0x275BCC, 2)
            for k in range(v.bones_held):
                slot = o.load(a + 0x110 + k * 4)
                b = v.bone[k].contents
                assert bytes(b.bind) == o.read(slot, 64)
                assert b.parent == struct.unpack('<h', o.read(slot + 0x64, 2))[0]
                assert bytes(b.rot) == o.read(slot + 0x70, 12)
                assert bytes(b.trans) == o.read(slot + 0x7C, 12)
                assert bytes(b.scale) == o.read(slot + 0x88, 6)
                assert bytes(b.world) == o.read(slot + 0x90, 64)
                counts['original_bones'] += 1
            counts['original_owner_binds'] += 1
    lib.bank_test_reset()
    counts['bank_model_views'] += bank.model_count


class BankLoad(ML.WholeLoad):
    """The proven sound-bank completion boundary, derived from delivered bytes."""
    def n_bank(self, _, address, data, size, result):
        assert size >= 0x14
        result[0] = (address + u32(C.string_at(data, 0x14), 0x10) + 0x40) & ~0x3F
        self.bank_calls += 1
        return 0

    def _hooks(self):
        super()._hooks()
        def bank(o):
            address = o.r[4] & ML.MASK
            self.expected.append((0x1FB370, address, 0, 0, 0, o.snapshot()))
            self.o_bank_calls += 1
            o.r[2] = (address + o.load(address + 0x10) + 0x40) & ~0x3F
        self.o.hook(0x1FB370, bank)


def loader_banks(lib, elf, counts):
    ML.OUT = OUT / 'loader'
    ml = ML.build()
    ram = (CAPS[11] / 'eeMemory.bin').read_bytes()
    spad = (CAPS[11] / 'scratchpad.bin').read_bytes()
    w = BankLoad(elf, ml, ram, spad, ROOT / 'assets/module_loader/modules.emml', seed_from_pack=True)
    try:
        counts['loader_module3_dispatches'] = len(w.run(3))
        counts['loader_area_dispatches'] = []
        for area in (11, 1, 11, 11):
            if len(counts['loader_area_dispatches']) == 3:
                counts['loader_reload_module3_dispatches'] = len(w.run(3))
            w.o.save(0x810700, area, 1)
            w.area[0x810700].value = area
            w.o.save(0x810701, 0, 1)
            w.area[0x810701].value = 0
            rows = w.run(0, limit=100, area=True)
            counts['loader_area_dispatches'].append(len(rows))
            st = ml.em_module_loader_state(w.ml).contents
            token = st.d28A490[0x43]
            data = BANKS[area].read_bytes()
            assert token == w.o.load(0x28A59C)
            if len(counts['loader_area_dispatches']) == 3:
                # Direct return without the global module reload relocates
                # AREA11 in the original too. A fixed-address export must
                # refuse that token, rather than silently use stale handles.
                assert token != u32(data, 8)
                lib.bank_test_reset()
                assert lib.bank_test_bind(str(BANKS[area]).encode(), token) == -1
                counts['loader_relocated_export_rejections'] = 1
                continue
            assert token == u32(data, 8), (area, hex(token), hex(u32(data, 8)))
            size = u32(data, 12)
            p = ml.em_module_loader_memory(w.ml, token, size)
            assert p and C.string_at(p, size) == data[32:] == w.o.read(token, size)
            lib.bank_test_reset()
            assert lib.bank_test_bind(str(BANKS[area]).encode(), token) == 0
            assert C.string_at(lib.bank_test_view().contents.span, size) == C.string_at(p, size)
        counts['loader_sound_boundaries'] = w.bank_calls
    finally:
        w.close()


def main():
    start = time.time()
    elf = ML.read_elf_bytes()
    lib = build()
    counts = dict(original_lookups=0, original_owner_binds=0, original_bones=0,
                  skeleton_records=0, bank_model_views=0, rejected_binds=0)
    # The unchanged first-level caller still gets AREA11 on first use.
    assert lib.bank_test_view().contents.table_address == u32(BANKS[11].read_bytes(), 8)
    for area in (11, 1, 11):
        ram = (CAPS[area] / 'eeMemory.bin').read_bytes()
        token = u32(ram, 0x28A59C)
        lib.bank_test_reset()
        assert lib.bank_test_bind(str(BANKS[area]).encode(), token) == 0
        check_bank(lib, elf, area, ram, counts)
        # Transactional failure never changes an existing bank or its bytes.
        before = C.string_at(C.addressof(lib.bank_test_view().contents), C.sizeof(WorldModels))
        malformed = OUT / 'malformed.emwm'
        malformed.write_bytes(b'not an EMWM')
        bad = [(None, token), (b'', token), (str(BANKS[area]).encode(), 0),
               (str(BANKS[area]).encode(), token + 4), (str(malformed).encode(), token),
               (str(OUT / 'absent.emwm').encode(), token)]
        for path, word in bad:
            assert lib.bank_test_bind(path, word) == -1
            assert C.string_at(C.addressof(lib.bank_test_view().contents), C.sizeof(WorldModels)) == before
            counts['rejected_binds'] += 1
        assert lib.bank_test_owner(0, 0)
        for target in (11, 1):
            assert lib.bank_test_bind(str(BANKS[target]).encode(), u32(BANKS[target].read_bytes(), 8)) == -1
            counts['rejected_binds'] += 1
        lib.bank_test_reset()
        # The global library is a separate stable bank. Seed it through the
        # original model parser, then select another world bank without reset.
        m = lib.bank_test_view().contents.models[0]
        keep = C.create_string_buffer(C.string_at(m.bytes, m.size))
        assert lib.bank_test_seed_library(m.address, keep, m.size, 0x12345678) == 0
        library = C.string_at(lib.bank_test_library(), C.sizeof(WorldModels))
        assert lib.bank_test_bind(str(BANKS[area]).encode(), token) == 0
        assert C.string_at(lib.bank_test_library(), C.sizeof(WorldModels)) == library
        assert lib.bank_test_library_word() == 0x12345678
    loader_banks(lib, elf, counts)
    if FULL:
        import export_area01_common as area01
        for cap in area01.captures():
            lib.bank_test_reset()
            assert lib.bank_test_bind(str(BANKS[1]).encode(), u32(cap.ram, 0x28A59C)) == 0
            check_bank(lib, elf, 1, cap.ram, counts)
        counts['additional_area01_captures'] = len(area01.captures())
    counts['seconds'] = round(time.time() - start, 2)
    counts['mode'] = 'full' if FULL else 'quick'
    counts['status'] = 'PASS'
    (OUT / 'report.json').write_text(json.dumps(counts, indent=2) + '\n')
    banner(f"{counts['original_lookups']} original lookups; {counts['original_owner_binds']} original owner binds",
           f"{counts['original_bones']} bone-slot records; {counts['rejected_binds']} rejected binds")
    print('World model bank: PASS', json.dumps(counts, sort_keys=True))


if __name__ == '__main__':
    main()
