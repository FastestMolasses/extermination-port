#!/usr/bin/env python3
"""The bounded EE interpreter the player-callback oracles share.

test_point_light_reference's Oracle, extended for the extra EE opcodes the
player callbacks use (the 64-bit shifts, sltu/xor/nor, sltiu/xori, lb/lh,
abs.s/neg.s), with the player's callees hooked as recorded boundaries: SDK
cos/atan2 (host models), 001B12B0 turn, 001749A0 / 001749F0 clip requests,
001C61D0 clip length (read from the original bank header), 001FB9F0 sound,
001EFD90 effect, 00178B90 translation, 0017B660 / 0017B910 / 0017C440, the
action and Use callees and the common callback tail.

It was the harness of the retired reversal-skid oracle (the skid's one
translation is em_locomotion_display's 0017C030 / 001612D0, census L12,
tools/test_locomotion_display_reference.py). Its users:
test_player_footstep_reference, test_player_floor_reference,
test_player_stage_workers_reference and test_player_probe_reference. The
user's pinned ELF supplies every instruction; none are embedded here.
"""
import ctypes as C
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from test_point_light_reference import Oracle, bits, number, signed, fp  # noqa: E402

ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
ACTOR = 0x680000
LIBC = C.CDLL(None)
LIBC.cosf.argtypes = [C.c_float]; LIBC.cosf.restype = C.c_float
LIBC.atan2f.argtypes = [C.c_float, C.c_float]; LIBC.atan2f.restype = C.c_float

# Original callees.
HEADING, MOTOR, ANIMATION, WALK = 0x174AC0, 0x17BC40, 0x17C030, 0x1612D0
WRAP, TURN = 0x1B1470, 0x1B12B0
REQUEST, ARBITER, LENGTH = 0x1749A0, 0x1749F0, 0x1C61D0
SOUND, EFFECT, TRANSLATE = 0x1FB9F0, 0x1EFD90, 0x178B90
GAIT_MATRIX, FOOT_SOLVE, REENTRY = 0x17B660, 0x17B910, 0x17C440
ACTION, USE, USE_SCAN = 0x1607D0, 0x160220, 0x184BA0
TAIL = (0x1764E0, 0x175900, 0x1756E0, 0x1796C0)

FIELDS = {  # native field -> (actor offset, size, float)
    'speed': (0x38, 4, True), 'yaw': (0xC4, 4, True), 'target': (0x240, 4, True),
    'rate': (0x204, 4, True), 'blend': (0x208, 4, True),
    'anim_flags': (0x200, 4, False), 'ticks': (0x28, 2, False),
    'player_state': (5, 1, False), 'walk_state': (6, 1, False),
    'mode': (0x1F0, 1, False), 'variant': (0x1F1, 1, False), 'tier': (0x25C, 1, False),
    'gait': (0x23F, 1, False), 'row': (0x235, 1, False), 'special': (0x236, 1, False),
    'surface': (0x23A, 1, False), 'obstruction': (0x314, 1, False),
}


class PlayerCallbackOracle(Oracle):
    """Oracle plus the EE opcodes the player callbacks use."""

    def __init__(self, elf, bank):
        super().__init__(elf)
        for i in range(4): self.mem.pop(0x275670 + i, None)  # point-light context
        # 0017BC40's blend division truncates (the older truncation model of
        # test_point_light_reference's Oracle); the other divisions here are
        # exact.
        self.truncate_ee_division = True
        self.bank = bank
        self.log = []
        self.calls.update({
            0x11DE90: lambda o: o.ret_float(LIBC.cosf(number(o.f[12]))),
            0x11E620: lambda o: o.ret_float(LIBC.atan2f(number(o.f[12]), number(o.f[13]))),
            TURN: lambda o: (o.log.append(('turn', o.f[12])), o.ret_float(number(o.f[13]))),
            REQUEST: lambda o: o.record('request', o.r[5] & 0xffff, o.r[6], o.f[12]),
            ARBITER: lambda o: o.record('arbiter', o.r[5] & 0xffff, o.f[12], o.f[13]),
            LENGTH: lambda o: o.ret_int(self.clip_length(signed(o.r[5], 16))),
            SOUND: lambda o: o.record('sound', o.r[4], o.r[5], o.r[6], o.r[7]),
            EFFECT: lambda o: o.record('effect', o.r[4], o.r[5] - ACTOR, o.r[6] - ACTOR),
            TRANSLATE: lambda o: o.record('translate', o.r[5], o.load(ACTOR + 0x38)),
            GAIT_MATRIX: lambda o: o.record('gait_matrix'),
            FOOT_SOLVE: lambda o: o.record('foot_solve'),
            REENTRY: lambda o: o.record('reentry'),
            ACTION: lambda o: o.ret_int(0),
            USE: lambda o: o.ret_int(0),
            USE_SCAN: lambda o: o.record('use_scan'),
        })
        for address in TAIL: self.calls[address] = lambda o: None

    def clip_length(self, clip):
        header = struct.unpack_from('<I', self.bank, 4 + clip * 4)[0]
        return struct.unpack_from('<H', self.bank, header + 2)[0]

    def ret_float(self, value): self.f[0] = bits(value)
    def ret_int(self, value): self.r[2] = value & 0xffffffff
    def record(self, *entry): self.log.append(entry); self.r[2] = 0

    def plain(self, word):
        r, f = self.r, self.f
        op, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        imm = signed(word & 65535, 16)
        if op == 0:
            fn, shift = word & 63, word >> 6 & 31
            if fn == 60: r[rd] = (r[rt] << (shift + 32)) & 0xffffffffffffffff; return
            if fn == 63:
                value = r[rt] & 0xffffffffffffffff
                value = value - (1 << 64) if value >> 63 else value
                r[rd] = (value >> (shift + 32)) & 0xffffffff
                return
            if fn == 43: r[rd] = int((r[rs] & 0xffffffff) < (r[rt] & 0xffffffff)); return
            if fn == 38: r[rd] = (r[rs] ^ r[rt]) & 0xffffffff; return
            if fn == 39: r[rd] = ~(r[rs] | r[rt]) & 0xffffffff; return
            if fn == 0 and rd == 0: return  # nop
        elif op == 11: r[rt] = int((r[rs] & 0xffffffff) < (imm & 0xffffffff)); r[0] = 0; return
        elif op == 14: r[rt] = r[rs] ^ (word & 65535); r[0] = 0; return
        elif op in (32, 37):
            address = (r[rs] + imm) & 0xffffffff
            value = self.load(address, 1 if op == 32 else 2)
            r[rt] = (signed(value, 8) & 0xffffffff) if op == 32 else value
            r[0] = 0
            return
        elif op == 17 and rs == 16 and (word & 63) in (5, 7):
            x = number(f[rd])
            f[word >> 6 & 31] = bits(abs(x)) if (word & 63) == 5 else bits(-x)
            return
        super().plain(word)

    def put(self, field, value):
        offset, size, is_float = FIELDS[field]
        self.save(ACTOR + offset, bits(value) if is_float else value, size)

    def get(self, field):
        offset, size, is_float = FIELDS[field]
        value = self.load(ACTOR + offset, size)
        return number(value) if is_float else value

    def call(self, entry, *args):
        self.r[29] = 0x700000
        self.run(entry, args)
