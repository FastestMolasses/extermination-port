#!/usr/bin/env python3
"""The original player-record clip clock: 00183090 (the stage's commit of
the record's +1F2 / +1F4 / +1F8 request) and 001C64F0 (anim_advance_time
into +200), executed from the owner's ELF over a record and a source bank.

A library for the reference tests (test_pose_transition_reference,
test_player_pose_reference) and the status-page oracles that share its
`signed` helper. Channel sampling calls (001C8D50 / 001C8710 / 001C87C0) are
explicit ordered boundaries recorded in `calls`; the clip header lookup
001C8480 reads the bank's offset table. This is the clock the player stage
runs for every takeover (the panel, the terminal and the items included
since audit 1b item 8).
"""
from pathlib import Path

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
