#!/usr/bin/env python3
"""Compare native pickup owner states with the user's original EE instructions.

Script/model/render/sound workers are explicit event boundaries. No original
instruction bytes, assets, or disassembly are included in this source file.
"""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import struct
import subprocess

from test_interaction_scan_reference import ScanOracle, ELF_SHA, DECOMP
from test_point_light_reference import RETURN, bits, signed

ROOT = Path(__file__).resolve().parents[1]
ACTOR, CHILD, DRAW = 0x910000, 0x920000, 0x990000


class Owner(C.Structure):
    _fields_ = [('callback', C.c_uint32), ('item_type', C.c_uint16)] + [
        (name, C.c_uint8) for name in ('uid', 'status', 'class_flags', 'subtype',
        'lifecycle', 'phase', 'armed', 'child_status', 'has_child', 'freed')]


START = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint16)
TICK = C.CFUNCTYPE(C.c_int, C.c_void_p)
EVENT = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_uint32)
ADD = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint16, C.c_int)


class Hooks(C.Structure):
    _fields_ = [('context', C.c_void_p), ('start', START), ('tick', TICK), ('event', EVENT)]


class Request(C.Structure):
    _fields_ = [('kind', C.c_uint8), ('index', C.c_uint8)]


class OwnerOracle(ScanOracle):
    def run(self, entry, args=(), floats=(), stop=RETURN):
        self.r[31] = RETURN
        for i, value in enumerate(args): self.r[4+i] = value
        for i, value in enumerate(floats): self.f[12+i] = bits(value)
        pc = entry
        for _ in range(100000):
            if pc == stop: return
            word = self.load(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            offset = signed(word & 65535, 16)*4
            indirect_call = op == 0 and word & 63 == 9
            if op in (2, 3) or indirect_call:
                target = self.r[rs] if indirect_call else (word & 0x3ffffff)*4
                if indirect_call: self.r[word >> 11 & 31] = pc+8
                elif op == 3: self.r[31] = pc+8
                self.plain(self.load(pc+4))
                if target in self.calls:
                    self.calls[target](self)
                    pc = pc+8 if op == 3 or indirect_call else self.r[31]
                else: pc = target
                continue
            branch = None
            if op in (4, 5, 20, 21):
                taken = (self.r[rs] == self.r[rt]) == (op in (4, 20))
                if op in (20, 21) and not taken: pc += 8; continue
                branch = pc+4+offset if taken else pc+8
            elif op in (6, 7):
                taken = signed(self.r[rs]) <= 0 if op == 6 else signed(self.r[rs]) > 0
                branch = pc+4+offset if taken else pc+8
            elif op == 1:
                assert rt in (0, 1)
                taken = signed(self.r[rs]) < 0 if rt == 0 else signed(self.r[rs]) >= 0
                branch = pc+4+offset if taken else pc+8
            elif op == 17 and rs == 8:
                taken = self.condition == bool(rt & 1)
                if rt & 2 and not taken: pc += 8; continue
                branch = pc+4+offset if taken else pc+8
            elif op == 0 and word & 63 == 8: branch = self.r[rs]
            if branch is not None:
                self.plain(self.load(pc+4)); pc = branch
            else:
                try: self.plain(word)
                except AssertionError as error: raise AssertionError(hex(pc), error) from error
                pc += 4
        raise AssertionError('original pickup routine did not return')


def controller(elf, lib, callback, lifecycle, phase, armed, child, action,
               no_grab, scripted, done, visible, item_y, player_y):
    o = OwnerOracle(elf)
    owner = Owner(callback, 0x1b, 7, 1, 0x84 if callback == 0x219550 else 0x87,
                  0, lifecycle, phase, armed, 2, child, 0)
    for offset, value in ((0, owner.status), (2, owner.class_flags), (3, owner.subtype),
                          (4, lifecycle), (5, phase), (11, armed), (0x9a, owner.uid)):
        o.save(ACTOR+offset, value, 1)
    o.save(ACTOR+0x4c, DRAW)
    o.save(ACTOR+0x2ec, CHILD if child else 0); o.save(CHILD+4, 2, 1)
    o.save(ACTOR+0xb4, bits(item_y)); o.save(0x810354, bits(player_y))
    o.save(0x8104a0, action, 1); o.save(0x8104e6, no_grab, 1)
    o.save(0x70003b92, scripted, 1)
    expected = []
    def start(r):
        assert r.r[4] == ACTOR+0x1f0, (hex(callback), hex(r.r[4]))
        entry = r.r[5]
        clip = 0 if entry in (0x248480, 0x2667e0) else r.load(
            0x2666b4 if callback == 0x219550 else 0x248354)
        expected.append(('start', entry, clip))
    def tick(r):
        expected.append(('tick',)); r.r[2] = done
    def sound(r):
        assert (r.r[4], r.r[5], r.r[6], r.f[12]) == (ACTOR, 0x194, 0, bits(300.))
        expected.append((3, 0x194))
    def persistence(r): expected.append((4, r.r[4] & 255))
    def publication(r): expected.append((1, 0)); r.r[2] = visible
    o.calls.update({0x1ba1a0: start, 0x1ba1f0: tick, 0x1fbd50: sound,
        0x1b1190: persistence, 0x1b17a0: publication,
        0x1f1180: lambda r: expected.append((0, 0)),
        0x1afc10: lambda r: expected.append((6, 0)),
        DRAW: lambda r: expected.append((2, 0))})
    o.run(callback, (ACTOR,))
    # Original child shutdown is an inline byte store between persistence
    # and publication; the host worker observes that same operation here.
    if o.load(CHILD+4, 1) != 2:
        expected.insert(expected.index((4, 7))+1, (5, o.load(CHILD+4, 1)))
    actual = []
    def event(_, kind, arg):
        actual.append((kind, arg)); return visible if kind == 1 else 1
    hooks = Hooks(None, START(lambda _, entry, clip: actual.append(('start', entry, clip)) or 1),
        TICK(lambda _: actual.append(('tick',)) or done), EVENT(event))
    result = lib.em_pickup_owner_tick(C.byref(owner), item_y, player_y, action,
                                    no_grab, scripted, C.byref(hooks))
    expected_state = tuple(o.load(ACTOR+i, 1) for i in (0, 2, 3, 4, 5, 11)) + (
        o.load(CHILD+4, 1), int((6, 0) in expected))
    actual_state = (owner.status, owner.class_flags, owner.subtype, owner.lifecycle,
                    owner.phase, owner.armed, owner.child_status, owner.freed)
    assert (actual, actual_state, result) == (expected, expected_state, 1-owner.freed), dict(
        callback=hex(callback), lifecycle=lifecycle, phase=phase, armed=armed,
        item_y=item_y, player_y=player_y, actual=actual, expected=expected,
        actual_state=actual_state, expected_state=expected_state)


def take(elf, lib, subtype, item_type, initial):
    o = OwnerOracle(elf); o.save(ACTOR+3, subtype, 1); o.save(ACTOR+0x2e, item_type, 2)
    o.save(0x8106b0, 9, 1); o.save(0x8106b1, 17, 1)
    base = 0x810cb8 if subtype == 1 else 0x810cc3
    o.save(base+item_type, initial, 1)
    expected = []
    o.calls[0x1c40b0] = lambda r: expected.append((r.r[4], r.r[5]))
    o.run(0x1b6ea0, (ACTOR,))
    maps, keys = (C.c_uint8*256)(), (C.c_uint8*256)()
    maps[item_type] = keys[item_type] = initial
    owner = Owner(); owner.item_type = item_type; owner.subtype = subtype
    request = Request(9, 17); actual = []
    add = ADD(lambda _, kind, amount: actual.append((kind, amount)) or 1)
    result = lib.em_pickup_owner_take(C.byref(owner), maps, keys, C.byref(request), add, None)
    assert result == o.r[2] == 1
    assert actual == expected
    assert (request.kind, request.index) == (o.load(0x8106b0, 1), o.load(0x8106b1, 1))
    if subtype: assert (maps if subtype == 1 else keys)[item_type] == o.load(base+item_type, 1)


def main():
    elf = (DECOMP/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    build = ROOT/'build/pickup_owner_reference'; build.mkdir(parents=True, exist_ok=True)
    library = build/'owner.dylib'
    subprocess.run(['cc', '-shared', '-fPIC', '-std=c11', '-O2', '-ffp-contract=off',
        '-I', str(ROOT/'src'), str(ROOT/'src/game/em_pickup_owner.c'),
        '-o', str(library)], check=True)
    lib = C.CDLL(str(library))
    lib.em_pickup_owner_tick.argtypes = [C.POINTER(Owner), C.c_float, C.c_float,
        C.c_uint8, C.c_uint8, C.c_uint8, C.POINTER(Hooks)]
    lib.em_pickup_owner_take.argtypes = [C.POINTER(Owner), C.POINTER(C.c_uint8),
        C.POINTER(C.c_uint8), C.POINTER(Request), ADD, C.c_void_p]
    count = 0
    for callback, lifecycle, phase, armed, child, action, no_grab, scripted, done, visible in itertools.product(
            (0x15afa0, 0x219550), (1, 2, 3, 255), (0, 1, 2), (0, 1, 4, 255),
            (0, 1), (0, 0x2d), (0, 1), (0, 1), (0, 1), (0, 1)):
        controller(elf, lib, callback, lifecycle, phase, armed, child, action,
                   no_grab, scripted, done, visible, 239.9, 229.9)
        count += 1
    for callback, player_y, offset in itertools.product((0x15afa0, 0x219550),
            (0., 229.9, -219.8, 1048576.), (5.9999, 6., 6.0001, 11.9999, 12., 12.0001, 12.9999, 13., 13.0001)):
        item_y = C.c_float(player_y+offset).value
        controller(elf, lib, callback, 1, 0, 4, 1, 0, 0, 0, 0, 1, item_y, player_y)
        count += 1
    take_count = 0
    for subtype, item_type, initial in itertools.product((0, 1, 2, 255),
            (0, 1, 8, 0x10, 0x1b, 0x1f, 0x20, 0x32, 0xff), (0, 1, 254, 255)):
        take(elf, lib, subtype, item_type, initial); take_count += 1
    report = dict(elf_sha256=ELF_SHA, controller_cases=count, take_cases=take_count,
        boundaries=['model initialization', 'script worker', 'publication visibility',
                    'render', 'sound', 'item inventory001C40B0'],
        source_sha256={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in (ROOT/'src/game/em_pickup_owner.c', ROOT/'src/game/em_pickup_owner.h', Path(__file__))})
    (build/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'Original pickup owner: {count} lifecycle/order/height cases PASS')
    print(f'Original pickup consume: {take_count} inventory-family/status cases PASS')


if __name__ == '__main__': main()
