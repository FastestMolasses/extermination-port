#!/usr/bin/env python3
"""New Game / game-over option 0 reset: native mirror vs original 001AF2C0.

Executes the original func_001AF2C0 (and its callees 00121A28, 001AF470,
001C40B0) from the user's ELF over captured AREA11 RAM taken after the
opening, with the progress/status fields dirtied the way a death in
AREA11 leaves them. The port's mirror (game_state_new_game in
src/game/em_game_internal.h, applied by em_game_install_new and by the
game-over option-0 restart) must produce the same values for every field
EmGameState mirrors.

The inventory part of 001AF2C0 is mirrored by em_pickup_reset
(src/game/em_pickup.c). A shim that includes em_pickup.c dirties the
native inventory the same way, runs em_pickup_reset, and every
em_pickup-owned field (item counts 0x00..0x3F at D_00810C64, magazine
packs C63, C60, CA4, CA6, battery CB2/CB7, key item CC3) must equal the
executed original.

It also executes the AREA11 opening controller's completion slice
(overlay 0x00823F6C..0x00823F84, resident in the capture) to show that
D_00810811 is the opening-complete byte the port now names
opening_complete.

No original bytes are embedded or printed; only addresses and values.
"""
import ctypes as C
import hashlib
from pathlib import Path
import struct
import subprocess
import sys

from test_item_sdk_math_reference import Original, ELF_SHA
from test_point_light_reference import bits, number

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
CAPTURE = DECOMP / 'build/startup-reference/playable_ee.bin'
ACTOR = 0x960000
MASK64 = (1 << 64) - 1


class Probe(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in (
        'health_bits', 'infection_bits', 'mag', 'reserve', 'battery',
        'battery_max', 'opening_complete', 'event_39', 'key_item_zero',
        'cine_step', 'terminal_powered')]


class CaptureOriginal(Original):
    """Original-instruction oracle whose untouched memory is captured RAM."""

    def __init__(self, elf, ram):
        super().__init__(elf)
        self.ram = ram

    def load(self, address, size=4):
        if (not any(address + i in self.mem for i in range(size)) and
                not 0x100000 <= address < 0x275b00 and
                address + size <= len(self.ram)):
            return int.from_bytes(self.ram[address:address + size], 'little')
        return super().load(address, size)

    def plain(self, word):
        op, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        fn, sa = word & 63, word >> 6 & 31
        if op == 28 and fn == 0x29 and sa == 0x1B:      # PCPYH
            value = 0
            for half in range(2):
                h = self.r[rt] >> (64 * half) & 0xFFFF
                value |= (h * 0x0001000100010001) << (64 * half)
            self.r[rd] = value
        elif op == 28 and fn == 0x09 and sa == 0x0E:    # PCPYLD
            self.r[rd] = (self.r[rs] & MASK64) << 64 | (self.r[rt] & MASK64)
        elif op == 28 and fn == 0x29 and sa == 0x0E:    # PCPYUD
            self.r[rd] = (self.r[rt] >> 64 & MASK64) << 64 | (self.r[rs] >> 64 & MASK64)
        elif op == 28 and fn == 0x29 and sa == 0x12:    # POR
            self.r[rd] = self.r[rs] | self.r[rt]
        else:
            super().plain(word)
        self.r[0] = 0


def jal_target(o, pc):
    word = o.load(pc)
    return (word & 0x3ffffff) * 4 if word >> 26 == 3 else None


def check_opening_complete(elf, ram):
    """Overlay 00823E80's completion slice stores D_00810811 = 0xFF."""
    o = CaptureOriginal(elf, ram)
    assert o.load(0x810700, 1) == 11, 'capture is not AREA11'
    assert o.load(0x810811, 1) == 0xFF, 'capture is not after the opening'
    # The slice follows the script-finished poll and precedes 001C4760.
    assert jal_target(o, 0x823F5C) == 0x1BA1F0
    assert jal_target(o, 0x823F84) == 0x1C4760
    o.save(0x810811, 0, 1)
    o.save(ACTOR + 0x2E, 0, 2)
    o.r[17] = ACTOR
    o.run(0x823F6C, stop=0x823F84)
    assert o.load(0x810811, 1) == 0xFF
    assert o.load(ACTOR + 0x2E, 2) == 0xFFFF
    return 'overlay 0x823F6C..84: D_00810811=0xFF, controller+0x2E=0xFFFF'


def run_original(elf, ram):
    o = CaptureOriginal(elf, ram)
    dirty = {  # the death-in-AREA11 state the native probe also starts from
        0x810858: (bits(0.0), 4), 0x81085C: (bits(60.0), 4),
        0x810C62: (4, 1), 0x810CB4: (120, 2), 0x810CB2: (8, 2),
        0x810CB7: (12, 1), 0x810811: (0xFF, 1), 0x810791: (0xFF, 1),
        0x810CC3: (1, 1), 0x810813: (0x20, 1),
        0x81084C: (o.load(0x81084C, 1) | 0x80, 1),
    }
    dirty.update(PICKUP_DIRTY)
    for address, (value, size) in dirty.items():
        o.save(address, value, size)
    o.run(0x1AF2C0)
    return o


def build_native():
    out = ROOT / 'build/continue_reset_reference'
    out.mkdir(parents=True, exist_ok=True)
    lib = out / ('probe.dylib' if sys.platform == 'darwin' else 'probe.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-shared', '-fPIC', '-Isrc', 'tests/continue_reset_probe.c',
                    '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    native.continue_reset_probe.argtypes = [C.POINTER(Probe)]
    probe = Probe()
    native.continue_reset_probe(C.byref(probe))
    return probe


# Inventory bytes dirtied on both sides before the reset (original address:
# value); the native shim writes the same values into em_pickup's mirror.
PICKUP_DIRTY = {
    0x810C60: (3, 1), 0x810C63: (7, 1), 0x810C64: (9, 1), 0x810C74: (7, 1),
    0x810C7F: (1, 1), 0x810CA4: (2, 1), 0x810CA6: (4, 1),
}
PICKUP_SHIM = r"""
#include <assert.h>
#include "game/em_pickup.c"
/* Model/GPU/player boundaries em_pickup_reset never reaches: fail-stop. */
#define UNREACHED() (fprintf(stderr, "pickup probe: %s reached\n", __func__), abort())
uint32_t em_random_next(void) { UNREACHED(); }
void em_game_player_interact_anim(int clip) { (void)clip; UNREACHED(); }
int em_model_load(EmModel *m, const char *path) { (void)m; (void)path; UNREACHED(); }
void em_model_free(EmModel *m) { (void)m; UNREACHED(); }
int em_model_clip_index(const EmModel *m, uint32_t clip) { (void)m; (void)clip; UNREACHED(); }
void em_model_palette_at(const EmModel *m, uint32_t clip, double time, float *out)
{ (void)m; (void)clip; (void)time; (void)out; UNREACHED(); }
EmGfxMesh *em_gfx_mesh_create(EmGfx *gfx, const float *verts, uint32_t count,
                              const uint32_t *indices, uint32_t index_count,
                              const EmGfxTexDesc *texs, uint32_t tex_count,
                              const uint8_t *texels, uint32_t flags)
{ (void)gfx; (void)verts; (void)count; (void)indices; (void)index_count;
  (void)texs; (void)tex_count; (void)texels; (void)flags; UNREACHED(); }
void em_gfx_mesh_destroy(EmGfx *gfx, EmGfxMesh *mesh) { (void)gfx; (void)mesh; UNREACHED(); }
void em_gfx_draw_skinned_additive(EmGfx *gfx, EmGfxMesh *mesh, const float *viewproj,
                                  const float *palette, uint32_t count, const float rgba[4])
{ (void)gfx; (void)mesh; (void)viewproj; (void)palette; (void)count; (void)rgba; UNREACHED(); }
void pickup_probe(uint8_t *count, uint8_t *out)
{
    /* the same dirty state as PICKUP_DIRTY, then the reset */
    g.status = 3; g.mag_packs = 7; g.count[0x00] = 9; g.count[0x10] = 7;
    g.count[0x1B] = 1; g.primary = 2; g.secondary = 4;
    g.battery_charge = 8; g.battery_capacity = 12; g.keys[0] = 1;
    em_pickup_reset();
    memcpy(count, em_pickup_items(), 256);
    uint8_t status, primary, secondary;
    em_pickup_equipment_read(&status, &primary, &secondary);
    out[0] = em_pickup_mag_packs(); out[1] = status; out[2] = primary;
    out[3] = secondary; out[4] = (uint8_t)em_pickup_battery_charge();
    out[5] = (uint8_t)(em_pickup_battery_charge() >> 8);
    out[6] = (uint8_t)em_pickup_battery_capacity(); out[7] = em_pickup_keys()[0];
}
"""


def build_native_pickup():
    """em_pickup_reset through a shim linked with the pickup-owner sources
    (the same set as the Makefile's PICKUP_ORIGINAL_TEST_SRC)."""
    out = ROOT / 'build/continue_reset_reference'
    out.mkdir(parents=True, exist_ok=True)
    source = out / 'pickup_probe.c'
    source.write_text(PICKUP_SHIM)
    lib = out / ('pickup_probe.dylib' if sys.platform == 'darwin' else 'pickup_probe.so')
    owners = ['src/game/em_pickup_owner.c', 'src/game/em_pickup_program.c',
              'src/game/em_script.c', 'src/game/em_interaction_runtime.c',
              'src/game/em_interaction_frame.c', 'src/game/em_interaction_animation.c']
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-shared', '-fPIC', '-Isrc', str(source), *owners,
                    '-lm', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    count, values = (C.c_uint8 * 256)(), (C.c_uint8 * 8)()
    native.pickup_probe(count, values)
    return bytes(count), bytes(values)


def check_pickup_reset(o):
    """em_pickup_reset vs the executed 001AF2C0 inventory writes."""
    count, values = build_native_pickup()
    fields = [(f'item count C64[{i:#04x}]', o.load(0x810C64 + i, 1), count[i])
              for i in range(0x40)]
    fields += [
        ('magazine packs D_00810C63', o.load(0x810C63, 1), values[0]),
        ('D_00810C60', o.load(0x810C60, 1), values[1]),
        ('primary D_00810CA4', o.load(0x810CA4, 1), values[2]),
        ('secondary D_00810CA6', o.load(0x810CA6, 1), values[3]),
        ('battery D_00810CB2', o.load(0x810CB2, 2), values[4] | values[5] << 8),
        ('battery max D_00810CB7', o.load(0x810CB7, 1), values[6]),
        ('key item 0 D_00810CC3', o.load(0x810CC3, 1), values[7]),
    ]
    # The seeds themselves, so a zero-only mirror cannot pass.
    assert [o.load(0x810C64 + i, 1) for i in (0, 5, 7, 0x10, 0x17)] == [1, 1, 1, 2, 1]
    assert o.load(0x810C63, 1) == 2 and o.load(0x810CA4, 1) == 0xFF
    failed = 0
    for name, original, native in fields:
        if original != native:
            failed += 1
            print(f'BAD {name}: original {original:#x}, native {native:#x}')
    print(f'{"ok " if not failed else "BAD"} em_pickup_reset: {len(fields)} '
          'inventory fields vs executed 001AF2C0')
    return failed


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    ram = CAPTURE.read_bytes()
    print(check_opening_complete(elf, ram))

    o = run_original(elf, ram)
    probe = build_native()
    fields = [
        ('health D_00810858', o.load(0x810858), probe.health_bits),
        ('D_0081085C', o.load(0x81085C), probe.infection_bits),
        ('magazine D_00810C62', o.load(0x810C62, 1), probe.mag),
        ('reserve D_00810CB4', o.load(0x810CB4, 2), probe.reserve),
        ('battery D_00810CB2>>1', o.load(0x810CB2, 2) >> 1, probe.battery),
        ('battery max D_00810CB7>>1', o.load(0x810CB7, 1) >> 1, probe.battery_max),
        ('opening complete D_00810811', o.load(0x810811, 1), probe.opening_complete),
        ('event 0x39 D_00810791', o.load(0x810791, 1), probe.event_39),
        ('key item 0 D_00810CC3', o.load(0x810CC3, 1), probe.key_item_zero),
        ('director step D_00810813', o.load(0x810813, 1), probe.cine_step),
        ('terminal power D_0081084C&0x80', o.load(0x81084C, 1) >> 7, probe.terminal_powered),
    ]
    failed = 0
    for name, original, native in fields:
        ok = original == native
        failed += not ok
        print(f"{'ok ' if ok else 'BAD'} {name}: original {original:#x}, native {native:#x}")
    assert number(o.load(0x810858)) == 100.0
    failed += check_pickup_reset(o)

    # Original writes neither EmGameState nor em_pickup mirrors (area bytes,
    # equipment CA5/CA7, D20..D23). Reported, not asserted here.
    print('not mirrored by this check:')
    print('  area bytes 700..705 =', o.read(0x810700, 6).hex(),
          '| CA5, CA7 =', o.load(0x810CA5, 1), o.load(0x810CA7, 1),
          '| D20..D23 =', o.read(0x810D20, 4).hex())
    if failed:
        print(f'continue reset reference: {failed} field(s) differ — FAIL')
        return 1
    print(f'continue reset reference: {len(fields)} EmGameState fields and '
          'the em_pickup inventory match executed 001AF2C0 — PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
