#!/usr/bin/env python3
"""The live spawn subset's contact status against successful 0015C420.

Model, slot and effect callees are recorded boundaries. The original body
executes every spawn-kind branch and its unconditional status publication.
This does not assert parity for the other fields of the narrow spawn subset.
"""
import ctypes as C
import json
import subprocess

import export_area01_common as A
from test_player_fall_reference import FallEE

OUT = A.ROOT / 'build/level2-crashes/spawn-contact-reference'
PLAYER, SIZE = 0x8102B0, 0x320
BRIDGE = r'''
#include "game/em_player.c"
EmGameState g;
unsigned spawn_contact(const uint8_t *raw) {
    memcpy(live.a.bytes,raw,EM_PLAYER_ACTOR_SIZE);
    player_states_spawn_values();
    return live.a.bytes[0];
}
'''


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    source, library = OUT/'bridge.c', OUT/'bridge.dylib'
    source.write_text(BRIDGE)
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
                    '-shared','-fPIC','-Isrc','-Wl,-dead_strip','-Wl,-undefined,dynamic_lookup',
                    '-Wl,-exported_symbol,_spawn_contact',str(source),'-o',str(library)],
                   cwd=A.ROOT,check=True)
    native = C.CDLL(str(library))
    native.spawn_contact.argtypes = [C.c_void_p]
    elf, records, cases = A.read_elf(), A.captures(), 0
    for cap in records:
        for initial in (0,1,3,255):
            for kind in range(6):
                o = FallEE(elf,cap.ram,cap.spad)
                o.save(PLAYER,initial,1)
                o.save(PLAYER+0xE,kind,1)
                o.save(0x275BCC,256,2)  # successful model-slot allocation
                raw = o.read(PLAYER,SIZE)
                events = []
                def boundary(fn):
                    def call(ee):
                        events.append(fn)
                        ee.r[2] = 0x01E00000 if fn == 0x1AF780 else 0
                    return call
                for fn in (0x1CA6F0,0x1AF780,0x1CB5B0,0x1C63E0,0x1C68C0,
                           0x18A880,0x15C310,0x1F0120,0x1EFE00):
                    o.hooks[fn] = boundary(fn)
                o.call(0x15C420,(PLAYER,))
                assert o.r[2] == 0 and 0x1F0120 in events
                assert native.spawn_contact(C.create_string_buffer(raw)) == o.load(PLAYER,1) == 1
                cases += 1
    report = dict(status='PASS',captures=len(records),cases=cases,
                  original_store='0015C6A4',scope='successful spawn status for six spawn kinds and four initial states')
    (OUT/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Player spawn contact original reference PASS',report)


if __name__ == '__main__':
    main()
