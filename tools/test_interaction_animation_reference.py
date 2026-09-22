#!/usr/bin/env python3
"""Run original clip request/initialization/clock instructions against native C.

The source bank supplies duration/terminal metadata. Channel sampling calls
are explicit ordered boundaries: this proves timing, flags and sample cursors;
exporter captured-matrix checks independently cover baked palettes.
"""
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

from test_player_reentry_reference import Original as Base, bits, number

ROOT = Path(__file__).resolve().parents[1]
ACTOR, NODE, BANK, RETURN = 0x600000, 0x620000, 0x900000, 0xBADF00D


def signed(value, width=32):
    value &= (1 << width)-1
    return value-(1 << width) if value >> (width-1) else value


class Original(Base):
    def __init__(self, elf, bank):
        super().__init__(elf, 0, 0, 0)
        self.r[28] = 0x27D370
        self.bank = bank
        self.sample_frame = None
        self.put(ACTOR+0x40, BANK)
        self.put(ACTOR+0x110, NODE)
        self.put(ACTOR+0xC, 21, 1)
        self.put(ACTOR+0x20C, 0, 2)
        self.put(ACTOR+0x2C, 0, 2)
        self.put(ACTOR+0x3C, bits(80))

    def get(self, address, size=4):
        if BANK <= address < BANK+len(self.bank):
            return int.from_bytes(self.bank[address-BANK:address-BANK+size], 'little')
        return super().get(address, size)

    def plain(self, word):
        op, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        address = (self.r[rs]+signed(word & 65535, 16)) & 0xFFFFFFFF
        if op == 0 and word & 63 in (2, 35, 36, 37, 42, 43):
            fn = word & 63
            if fn == 2: self.r[rd] = (self.r[rt] & 0xFFFFFFFF) >> (word >> 6 & 31)
            elif fn == 35: self.r[rd] = (self.r[rs]-self.r[rt]) & 0xFFFFFFFF
            elif fn == 36: self.r[rd] = self.r[rs] & self.r[rt]
            elif fn == 37: self.r[rd] = self.r[rs] | self.r[rt]
            elif fn == 42: self.r[rd] = int(signed(self.r[rs]) < signed(self.r[rt]))
            else: self.r[rd] = int((self.r[rs] & 0xFFFFFFFF) < (self.r[rt] & 0xFFFFFFFF))
        elif op in (12, 13):
            self.r[rt] = self.r[rs] & (word & 65535) if op == 12 else self.r[rs] | (word & 65535)
        elif op == 33: self.r[rt] = signed(self.get(address, 2), 16) & 0xFFFFFFFF
        elif op == 41: self.put(address, self.r[rt], 2)
        elif op == 17 and rs == 0: self.r[rt] = self.f[rd]
        else: super().plain(word)
        self.r[0] = 0

    def run(self, entry, arguments=(ACTOR,), floats=()):
        for i, value in enumerate(arguments): self.r[4+i] = value
        for i, value in enumerate(floats): self.f[12+i] = bits(value)
        self.r[31] = RETURN
        pc = entry
        for _ in range(4000):
            if pc == RETURN: return self.r[2]
            if pc == 0x1C8480: # clip header lookup, verified bank boundary
                assert self.r[4] == BANK
                clip = self.r[5] & 0x7FFF
                header = self.get(BANK+4+clip*4)
                self.put(0x275BF8, BANK+header)
                pc = self.r[31] & 0xFFFFFFFF
                continue
            if pc == 0x128250: # original start-time conversion (only0 here)
                assert number(self.f[12]) == 0
                self.r[2] = 0
                pc = self.r[31] & 0xFFFFFFFF
                continue
            if pc in (0x1C8D50, 0x1C8710, 0x1C87C0):
                self.calls.append((pc, number(self.f[12]), number(self.f[13])))
                if pc == 0x1C8710:
                    self.sample_frame = number(self.f[12])
                elif pc == 0x1C87C0:
                    assert self.sample_frame is not None
                    self.sample_frame += number(self.f[12])
                pc = self.r[31] & 0xFFFFFFFF
                continue
            assert (0x183090 <= pc < 0x183190 or 0x1C64F0 <= pc < 0x1C68C0), hex(pc)
            word = self.get(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            target = None
            if op in (4, 5, 20, 21) or (op == 17 and rs == 8):
                if op == 17:
                    taken = self.condition == bool(rt & 1)
                    likely = bool(rt & 2)
                else:
                    taken = (self.r[rs] == self.r[rt]) == (op in (4, 20))
                    likely = op >= 20
                target = pc+4+signed(word & 65535, 16)*4 if taken else pc+8
                if taken or not likely: self.plain(self.get(pc+4))
                pc = target
                continue
            if op in (2, 3):
                if op == 3: self.r[31] = pc+8
                target = (word & 0x3FFFFFF) << 2
            elif op == 1:
                assert rt in (0, 1)
                taken = signed(self.r[rs]) < 0 if rt == 0 else signed(self.r[rs]) >= 0
                target = pc+4+signed(word & 65535, 16)*4 if taken else pc+8
            elif op == 0 and word & 63 == 8:
                target = self.r[rs] & 0xFFFFFFFF
            if target is not None:
                self.plain(self.get(pc+4))
                pc = target
            else:
                try: self.plain(word)
                except AssertionError as error: raise AssertionError(hex(pc), error) from error
                pc += 4
        raise AssertionError('Original animation did not return')

    def request(self, clip, blend):
        self.put(ACTOR+0x1F2, clip, 2)
        self.put(ACTOR+0x1F4, bits(1))
        self.put(ACTOR+0x1F8, bits(blend))

    def tick(self):
        if self.run(0x183090):
            self.put(ACTOR+0x200, self.run(0x1C64F0, floats=(1.0,)))


BRIDGE = r'''
#include "game/em_interaction_animation.h"
#include <string.h>
static EmModel model;
static EmInteractionAnimation animation;
static float palette[22*16];
int load(const char *path) {return em_model_load(&model,path);}
void close_model(void) {em_model_free(&model);}
int start(unsigned clip,float blend) {
    em_interaction_animation_clear(&animation);
    memset(palette,0xA5,sizeof palette);
    return em_interaction_animation_request(&animation,&model,clip,1,blend);
}
int request(unsigned clip,float rate,float blend) {
    return em_interaction_animation_request(&animation,&model,clip,rate,blend);
}
int tick(unsigned *values) {
    int result=em_interaction_animation_tick(&animation,&model,palette);
    values[0]=animation.current_clip;values[1]=animation.frame;
    values[2]=animation.flags;memcpy(values+3,&animation.remaining,4);
    values[4]=animation.transition;values[5]=em_interaction_animation_done(&animation);
    return result;
}
const float *pose(void) {return palette;}
'''


def main():
    decomp = ROOT.parent/'Extermination'
    elf = (decomp/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
    bank = (decomp/'extract/chunk28/f01_id3c.bin').read_bytes()
    output = ROOT/'build/interaction_animation_reference'
    output.mkdir(parents=True, exist_ok=True)
    source = output/'bridge.c'
    source.write_text(BRIDGE)
    library = output/('animation.dylib' if sys.platform == 'darwin' else 'animation.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
        '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-Isrc', str(source),
        'src/game/em_interaction_animation.c', 'src/em_model.c', '-lm', '-o', str(library)],
        cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.load.argtypes = [C.c_char_p]
    native.start.argtypes = [C.c_uint, C.c_float]
    native.request.argtypes = [C.c_uint, C.c_float, C.c_float]
    native.tick.argtypes = [C.POINTER(C.c_uint)]
    native.pose.restype = C.POINTER(C.c_float)
    assert native.load(str(ROOT/'assets/player.emdl').encode()) == 0
    rows = []
    comparisons = 0
    for clip, duration in ((0x47, 200), (0x15C, 121), (0x40, 45), (0x41, 45), (0x42, 45)):
        header = struct.unpack_from('<I', bank, 4+clip*4)[0]
        assert struct.unpack_from('<HHh', bank, header) == (21, duration, -2)
        assert struct.unpack_from('<I', bank, header+0x14)[0] == 0
        for blend in (0, 1):
            reference = Original(elf, bank)
            reference.request(clip, blend)
            assert native.start(clip, blend)
            first_done = None
            for tick in range(duration+5):
                if tick in (3, duration+2):
                    # Re-requesting a running/finished current clip does not restart.
                    assert native.request(clip, 1, blend)
                    reference.request(clip, blend)
                reference.tick()
                values = (C.c_uint*6)()
                result = native.tick(values)
                expected = [reference.get(ACTOR+0x20C, 2),
                            int(reference.sample_frame or 0), reference.get(ACTOR+0x200),
                            reference.get(ACTOR+0x3C), bool(reference.get(ACTOR+0x2C, 2) & 0x8000),
                            bool(reference.get(ACTOR+0x200) & 0x1000)]
                assert list(values) == expected, (clip, blend, tick, list(values), expected)
                assert result == (0 if tick == 0 and blend else 1)
                if values[5] and first_done is None: first_done = tick
                comparisons += 1
            rows.append({'clip':clip, 'blend':blend, 'first_end_flag_tick':first_done})
    assert not native.request(999, 1, 0)
    assert not native.request(71, 0.5, 0)
    assert not native.request(71, 1, 8)
    native.close_model()
    report = {'status':'PASS', 'original_clock_comparisons':comparisons, 'cases':rows,
              'limits':'Bone/channel sampling is an intercepted boundary; exporters separately verify captured palettes.'}
    (output/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
