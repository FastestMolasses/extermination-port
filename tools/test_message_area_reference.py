#!/usr/bin/env python3
"""Live message-bank switches and original-instruction AREA01 service checks.

Reads only user-local captures/exports. The native glyph boundary is a test
sink; layout still runs in the existing native draw/glyph translations.
The service oracle stubs drawing, so the compared results are the complete
request block, shared mailbox/mode bytes and non-draw worker calls.
"""
import ctypes as C
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import test_message_service_reference as M


def main():
    out = ROOT / 'build/level2/message-area'
    out.mkdir(parents=True, exist_ok=True)
    library = out / 'bridge.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-shared', '-fPIC', '-Isrc', 'tests/message_area_bridge.c',
                    'src/game/em_message_service.c', 'src/game/em_message_draw_original.c',
                    'src/game/em_message_glyph_original.c', '-o', str(library)],
                   cwd=ROOT, check=True)
    lib = C.CDLL(str(library))
    lib.area_test_install.argtypes = [C.c_char_p]
    lib.area_test_switch.argtypes = [C.c_char_p, C.c_uint32]
    lib.area_test_case.argtypes = [C.c_char_p, C.c_char_p, C.c_int, C.c_int, C.c_int]
    lib.area_test_bank.argtypes = [C.c_int, C.POINTER(C.c_uint32)]
    lib.area_test_bank.restype = C.POINTER(C.c_ubyte)
    lib.em_message_live_block.restype = C.c_void_p
    lib.em_message_live_fault.restype = C.c_char_p
    a11 = os.fsencode(ROOT / 'assets/message/message_data.emmd')
    a01 = os.fsencode(ROOT / 'assets/area01/message_data.emmd')
    assert lib.area_test_install(a11) == 1
    decomp = ROOT.parent / 'Extermination'
    elf = (decomp / 'config/SCUS_971.12').read_bytes()
    cap = decomp / 'build/s87/route_a01/a01_05_npc_bridge_talk'
    ram = (cap / 'eeMemory.bin').read_bytes()
    spad = (cap / 'scratchpad.bin').read_bytes()
    for lo, hi in M.EXECUTED:
        assert ram[lo:hi] == elf[lo-0x100000+0x300:hi-0x100000+0x300]
    # Preservation probe: prime glyph work and draw-buffer sentinels, then put the recorded
    # pending request prefix into the block. The rest of this block is the
    # end snapshot; this is a composite fixture, not a captured whole state.
    assert lib.area_test_prime() == 1
    block = lib.em_message_live_block()
    pending = bytearray(ram[M.BLOCK:M.BLOCK+M.BLOCK_SIZE])
    trace = json.loads((cap / 'trace.json').read_text())
    prefix = bytes.fromhex(trace['rows'][300]['msg'])
    assert struct.unpack_from('<iiI', prefix) == (2, 1, 0x0A)
    pending[:len(prefix)] = prefix
    C.memmove(block, bytes(pending), M.BLOCK_SIZE)
    for path, area in ((a01, 1), (a11, 11), (a01, 1), (a01, 1)):
        assert lib.area_test_switch(path, area) == 1, lib.em_message_live_fault()
    bank_bytes = 0
    for global_, address in ((0, 0x28A594), (1, 0x28A4E8)):
        size = C.c_uint32()
        p = lib.area_test_bank(global_, C.byref(size))
        at = struct.unpack_from('<I', ram, address)[0]
        assert C.string_at(p, size.value) == ram[at:at+size.value]
        bank_bytes += size.value
    # AREA01's recorded dialogue lines and the shaft door's global refusal.
    # Full mode additionally exercises each exported area-table starting row.
    emmd = Path(os.fsdecode(a01)).read_bytes()
    count = struct.unpack_from('<I', emmd, 16)[0]
    lines = list(range(count)) if os.getenv('EM_TEST_FULL') == '1' else [0x0A, 0x40]
    lines += [0x80000008]
    ticks = cases = 0
    for line in lines:
        for voice_mode in (0, 1, 2):
            for mode in (1, 2):
                o = M.MessageOracle(elf, ram, spad)
                b = bytearray(M.BLOCK_SIZE)
                struct.pack_into('<iiI', b, 0, 2, 1, line)
                b[0x51] = 0xFF
                o.write(M.BLOCK, b)
                o.save(M.SPAD_MODE, mode, 1)
                o.save(M.F5, voice_mode, 1)
                o.save(0x282155, 0, 1); o.save(0x282156, 0, 1)
                for tick in range(3):
                    lib.area_test_case(o.read(M.BLOCK, M.BLOCK_SIZE), o.shared(), mode, 0, 0)
                    o.events = []
                    o.run(0x1FCA10)
                    assert lib.em_message_live_tick() == 0, (line, lib.em_message_live_fault())
                    assert C.string_at(block, M.BLOCK_SIZE) == o.read(M.BLOCK, M.BLOCK_SIZE), (line, tick)
                    sh = (C.c_ubyte * 14)()
                    lib.area_test_shared(sh)
                    assert bytes(sh) == o.shared(), (line, tick, 'shared')
                    ev = (C.c_int * (128 * 5))()
                    n = lib.area_test_events(ev)
                    actual = [tuple(ev[i*5:(i+1)*5]) for i in range(n)]
                    expected = [tuple(e)+(0,)*(5-len(e)) for e in o.events if e[0] != M.DRAW]
                    assert actual == expected, (line, tick, actual, expected)
                    ticks += 1
                cases += 1
    assert lib.em_message_live_help_draw(1, 2, 3, 4) == 0  # presenter retained
    assert lib.area_test_switch(os.fsencode(out/'missing.emmd'), 11) == 0
    assert lib.em_message_live_fault()
    assert lib.em_message_live_tick() == -1
    assert lib.area_test_switch(a11, 11) == 0  # fault stays latched
    lib.em_message_live_shutdown()
    print(f'message area: PASS 4 state-preserving selections, {bank_bytes} capture-equal bank bytes, '
          f'{cases} cases / {ticks} original-instruction ticks, missing-bank fail-stop')


if __name__ == '__main__':
    main()
