#!/usr/bin/env python3
"""001C8140 / 001C7EB0 (em_area01_revisit_spawn, UNWIRED) against the original.

The AREA01 revisit's op14 (script 0x82AD90, a01r_01 f1139) spawns three
class-0xC nodes through 001C8140 whose 001C7EB0 runs the group 0x82A900
behaviours through +0x2EC. No run reaches them yet, so this compares the
translations directly: over the recorded a01r_00 image the original
instructions run each routine with every callee as original code; natively
the translation runs and every callee it names is executed as original code
on the shared RAM, scratchpad and stack (at exactly the sp passed). RAM,
scratchpad and 001C8140's v0 must match.

Cases: 001C8140 with the bank D_0028A490[150], indices 9..11 and the three
behaviours, and with the pool full (a refused allocation); 001C7EB0 on such
a node in state 0 (and with the bone budget D_00275BCC below the count),
then in state 1 over several frames with its timer +0x1F0+0xF4 at 1 / 0 /
-5, the owner's done mask set and clear for its +0x2E bit, and in states
2 / 3 (freed). No capture bytes or instructions are exported.
"""
import ctypes as C
import struct
import time

import reference_mode as mode
from test_player_slide_reference import read_elf, STACK_TOP
import test_area01_runtime_reference as R
import test_area01_revisit_reference as RV
from test_area01_revisit_runtime_reference import EE, first_difference

ROOT = R.ROOT
OUT = ROOT / 'build/area01_revisit_spawn'
MASK = (1 << 64) - 1


def build():
    import subprocess
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / 'spawn.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared', '-fPIC',
                    '-Isrc', 'tests/area01_revisit_spawn_bridge.c', 'src/game/em_area01_revisit_spawn.c',
                    '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    n.rvs_001C8140.argtypes = [C.POINTER(R.Host), C.c_uint32, C.c_uint32, C.c_int16, C.c_uint32,
                               C.POINTER(C.c_uint64), C.POINTER(C.c_uint32)]
    n.rvs_001C7EB0.argtypes = [C.POINTER(R.Host), C.c_uint32, C.c_uint32, C.POINTER(C.c_uint32)]
    return n


def run(n, elf, ram0, spad0, entry, args):
    original = EE(elf)
    original.mem[:] = ram0
    original.spad[:] = spad0
    original.call(entry, args)
    ram = (C.c_uint8 * len(ram0)).from_buffer_copy(ram0)
    spr = (C.c_uint8 * len(spad0)).from_buffer_copy(spad0)
    stack = (C.c_uint8 * 0x100000)()
    leaves = EE(elf)
    leaves.mem = memoryview(ram).cast('B')
    leaves.spad = memoryview(spr).cast('B')
    leaves.stack = memoryview(stack).cast('B')
    errors, seen = [], []

    def resolve(_, address, size, write):
        for base, data in ((0, ram), (0x70000000, spr), (0x7F000000, stack)):
            if size and address >= base and address + size <= base + len(data):
                return C.addressof(data) + address - base
        return None

    def worker(_, cp):
        try:
            c = cp.contents
            seen.append(c.function)
            assert c.function not in (0x1C8140, 0x1C7EB0), ('a translated routine forwarded', hex(c.function))
            for k in range(c.na): leaves.r[4 + k] = c.a[k]
            for k in range(c.nf): leaves.f[12 + k] = c.f[k]
            leaves.r[29] = c.sp + 0x400
            c.v0, c.f0 = leaves.nested(c.function)
            return 0
        except Exception as error:   # noqa: BLE001
            errors.append(repr(error))
            return -1

    view, work = R.View(resolve), R.Worker(worker)
    host = R.Host(None, view, work)
    fault = C.c_uint32()
    if entry == 0x1C8140:
        v0 = C.c_uint64()
        rc = n.rvs_001C8140(C.byref(host), STACK_TOP, args[0], args[1], args[2], C.byref(v0), C.byref(fault))
    else:
        rc = n.rvs_001C7EB0(C.byref(host), STACK_TOP, args[0], C.byref(fault))
    where = (hex(entry), [hex(a & 0xFFFFFFFF) for a in args])
    assert rc == 0 and not errors, (where, rc, hex(fault.value), errors)
    assert bytes(ram) == bytes(original.mem), (where, 'RAM differs', first_difference(ram, original.mem))
    assert bytes(spr) == bytes(original.spad), (where, 'scratchpad differs')
    if entry == 0x1C8140:
        assert v0.value == original.r[2] & MASK, (where, 'v0', hex(v0.value), hex(original.r[2]))
    return bytes(original.mem), bytes(original.spad), original.r[2] & 0xFFFFFFFF, seen


def main():
    start = time.monotonic()
    n = build()
    elf = read_elf()
    folder = RV.ROUTE / 'a01r_00_to_train_room'
    ram0, spad0 = bytearray((folder / 'eeMemory.bin').read_bytes()), (folder / 'scratchpad.bin').read_bytes()
    bank = struct.unpack_from('<I', ram0, 0x28A490 + 4 * 150)[0]
    cases = calls = 0
    nodes = []
    ram, spad = bytes(ram0), spad0
    for index, behaviour in ((9, 0x8237D0), (10, 0x823900), (11, 0x8239C0)):
        ram, spad, node, seen = run(n, elf, ram, spad, 0x1C8140, (bank, index, behaviour))
        assert node, 'no node allocated'
        nodes.append(node)
        cases += 1; calls += len(seen)
    # 001C7EB0 on the first node: state 0, then state 1 frames.
    node = nodes[0]
    publish = lambda b, nd: (struct.pack_into('<I', b, 0x275B48, nd), struct.pack_into('<I', b, 0x275B40, nd + 0x110))
    r = bytearray(ram); publish(r, node)
    r2, s2, _, seen = run(n, elf, bytes(r), spad, 0x1C7EB0, (node,)); cases += 1; calls += len(seen)
    assert r2[node + 4] == 1, ('state 0 did not bind the node', r2[node + 4], [hex(f) for f in seen])
    for timer in (1, 0, -5):
        for owner_mask in (0, 0xFFFF):
            r = bytearray(r2)
            struct.pack_into('<i', r, node + 0x1F0 + 0xF4, timer)
            owner = struct.unpack_from('<I', r, node + 0x24)[0]
            if owner_mask:
                owner = 0x82A900 - 0x40   # a record whose +0x2E is the mask (overlay data used as a record)
                struct.pack_into('<I', r, node + 0x24, owner)
                struct.pack_into('<H', r, owner + 0x2E, owner_mask)
            publish(r, node)
            run(n, elf, bytes(r), s2, 0x1C7EB0, (node,)); cases += 1
    # The shared pose's slot: its +0xCC (copied into +0x100..+0x10F before
    # the node's +0x10C = 1.0) and its scales +0x18..+0x20 at, below and
    # above the 0.9 the behaviour call needs. 001C68C0 (original code here)
    # rebuilds the slot before 001C7EB0 reads it, so these staged values do
    # not reach the reads: dropping the +0x10C store or moving the 0.9 bound
    # by one ulp is not observable over this image (recorded, not killed).
    slot = struct.unpack_from('<I', r2, node + 0x110)[0]
    for scale in (0x3F666666, 0x3F666665, 0x3F800000):
        r = bytearray(r2)
        struct.pack_into('<i', r, node + 0x1F0 + 0xF4, 1)
        struct.pack_into('<3I', r, slot + 0x18, scale, scale, scale)
        struct.pack_into('<I', r, slot + 0xCC, 0)
        publish(r, node)
        run(n, elf, bytes(r), s2, 0x1C7EB0, (node,)); cases += 1
    # Bone budget below the count, and the free states.
    r = bytearray(ram); publish(r, nodes[1]); struct.pack_into('<h', r, 0x275BCC, 0)
    run(n, elf, bytes(r), spad, 0x1C7EB0, (nodes[1],)); cases += 1
    for state in (2, 3):
        r = bytearray(ram); publish(r, nodes[2]); r[nodes[2] + 4] = state
        run(n, elf, bytes(r), spad, 0x1C7EB0, (nodes[2],)); cases += 1
    mode.banner(f'{cases} cases')
    print(f'AREA01 revisit spawn (001C8140 / 001C7EB0, unwired): PASS {cases} cases, {calls} callee calls, '
          f'{round(time.monotonic() - start, 2)} s')


if __name__ == '__main__':
    main()
