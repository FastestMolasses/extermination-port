#!/usr/bin/env python3
"""The AREA01 revisit owners through the AREA01 runtime, against the original.

em_area01_runtime_revisit.inc binds the revisit entries (0x825950, 0x823CD0
and the 0x824340 / 0x824770 / 0x824D50 steps it calls, 0x825910, 0x826010,
0x8237D0 / 0x823900 / 0x8239C0) into em_area01_runtime's dispatch, and every
hook they reach back into the runtime by its original address. This test runs
each entry through that dispatch over the recorded revisit RAM images
(docs/AREA01_REVISIT.md section 3) and compares the result with the ORIGINAL
instructions run over the same image with every callee as original code:

- natively, the runtime dispatches the entry to its translation; each call
  the composition forwards to its host worker is executed as original code on
  the shared copy (the same RAM, scratchpad and the EE private stack);
- RAM and scratchpad after the call must equal the original's, and an entry
  with a result must return the original's v0;
- the host worker must never receive one of the bound entries (they must be
  translated, not forwarded);
- fail-stop: a worker that refuses its first call makes the runtime return
  -1 with that callee's address latched.

Cases: every 0x825950 and 0x823CD0 node of the six images as captured, then
designed cases on the a01r_00 image: each event stage of D_008107DF with the
owner's +0x04 / +0x05 steps, the second character with D_00810845 bit 5, the
creatures' +0x05 / +0x06 steps with D_008107DF 0 and 2, 0x825910 at the
505.0 boundary, 0x826010 across its counter, and the group 0x82A900
behaviours on a spare node. No capture bytes or original instructions are
exported; reports hold counts only.
"""
import ctypes as C
import struct
import time

import reference_mode as mode
from test_player_fall_reference import FallEE
from test_player_slide_reference import read_elf, STACK_TOP
import test_area01_runtime_reference as R
import test_area01_revisit_reference as RV
import test_area00_fx_reference as FX

MASK = (1 << 64) - 1
BOUND = {0x825950, 0x823CD0, 0x824340, 0x824770, 0x824D50, 0x825910, 0x826010,
         0x8237D0, 0x823900, 0x8239C0}
RESULT = {0x825910}


class EE(FX.FXEE):
    """The AREA00 effects lane's interpreter (measured COP1 / VU0 with the
    clip test, the EE access rules, the MMI byte ops) without its journal,
    plus the two MMI forms the vector helpers execute (PEXTLW: rd = rs.w1
    rt.w1 rs.w0 rt.w0, high to low; PEXTUW the same over the upper words),
    as test_anim_runtime_rest_reference.EE."""

    def __init__(self, elf):
        super().__init__(elf, journal=False)

    def mmi(self, word, pc):
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        fn, sub = word & 63, word >> 6 & 31
        if fn == 0x08 and sub == 0x12:
            a, b = self.r[rs] & MASK, self.r[rt] & MASK
        elif fn == 0x28 and sub == 0x12:
            a, b = self.rh[rs] & MASK, self.rh[rt] & MASK
        else:
            return super().mmi(word, pc)
        if rd:
            self.r[rd] = (b & 0xFFFFFFFF) | (a & 0xFFFFFFFF) << 32
            self.rh[rd] = (b >> 32) | (a >> 32) << 32


def u32(ram, address):
    return struct.unpack_from('<I', ram, address)[0]


def run_case(native, elf, ram0, spad0, entry, args, edits=(), refuse=None):
    """One entry over one image. `edits` are (address, bytes) applied to the
    start image on both sides. Returns the worker calls seen."""
    original = EE(elf)
    original.mem[:] = ram0
    original.spad[:] = spad0
    for address, data in edits:
        original.write(address, data)
    baseline, spad_start = bytes(original.mem), bytes(original.spad)
    if refuse is None:
        original.call(entry, args)
    ram = (C.c_uint8 * len(baseline)).from_buffer_copy(baseline)
    spr = (C.c_uint8 * len(spad_start)).from_buffer_copy(spad_start)
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
            assert c.function not in BOUND, ('bound entry forwarded', hex(c.function))
            if refuse is not None:
                return -1
            for k in range(c.na): leaves.r[4 + k] = c.a[k]
            for k in range(c.nf): leaves.f[12 + k] = c.f[k]
            # EE.nested lowers sp by 0x400 first: the callee runs exactly at
            # the sp the runtime passed, so a nested sp above a translation's
            # live stack locals would let the callee's frame overwrite them.
            leaves.r[29] = c.sp + 0x400
            c.v0, c.f0 = leaves.nested(c.function)
            return 0
        except Exception as error:   # noqa: BLE001 (reported below)
            errors.append(repr(error))
            return -1

    view, work = R.View(resolve), R.Worker(worker)
    host = R.Host(None, view, work)
    assert native.a01rt_bind(C.byref(host)) == 0
    call = R.Call(function=entry, sp=STACK_TOP, na=len(args))
    for i, value in enumerate(args): call.a[i] = value
    call.v0 = 0x5A5A5A5A
    rc = native.a01rt_call(C.byref(call))
    where = (hex(entry), [hex(a) for a in args], [(hex(a), d.hex()) for a, d in edits])
    if refuse is not None:
        assert rc == -1 and seen, (where, 'refused worker did not stop the entry', rc, seen)
        assert native.a01rt_fault() == seen[0], (where, hex(native.a01rt_fault()), hex(seen[0]))
        assert len(seen) == 1, (where, 'a call after the fault', [hex(s) for s in seen])
        return seen
    assert rc == 0, (where, errors, hex(native.a01rt_fault()))
    assert not errors, (where, errors)
    assert bytes(ram) == bytes(original.mem), (where, 'RAM differs', first_difference(ram, original.mem))
    assert bytes(spr) == bytes(original.spad), (where, 'scratchpad differs', first_difference(spr, original.spad))
    if entry in RESULT:
        assert call.v0 & MASK == original.r[2] & MASK, (where, 'v0', hex(call.v0), hex(original.r[2]))
    return seen


def first_difference(a, b):
    a, b = bytes(a), bytes(b)
    for i in range(len(a)):
        if a[i] != b[i]:
            return hex(i)
    return None


def publish(node):
    """What the pool walk publishes before a behaviour runs: the current actor
    D_00275B48 (001CB590) and its bone-slot array D_00275B40 = node + 0x110
    (001CB5B0); docs/AREA01_REVISIT.md section 3."""
    return ((0x275B48, struct.pack('<I', node)), (0x275B40, struct.pack('<I', node + 0x110)))


def b8(v): return bytes((v & 0xFF,))
def h16(v): return struct.pack('<H', v & 0xFFFF)
def f32(v): return struct.pack('<f', v)


STATE = None


def run_index(k):
    """One designed or captured case (a forked worker; STATE is inherited)."""
    native, elf, images, cases = STATE
    beat, entry, args, edits = cases[k]
    ram, spad = images[beat]
    return len(run_case(native, elf, ram, spad, entry, args, publish(args[0]) + tuple(edits)))


def main():
    start = time.monotonic()
    native = R.build()
    elf = read_elf()
    images = {}
    for beat in RV.BEATS:
        folder = RV.ROUTE / beat
        ram, spad = (folder / 'eeMemory.bin').read_bytes(), (folder / 'scratchpad.bin').read_bytes()
        assert ram[RV.ARENA + 4] == RV.OVERLAY_ID, (beat, 'AREA01 overlay not resident')
        assert ram[RV.ARENA + 0x40:RV.ARENA + 0x2000] == \
            RV.OVERLAY_FILE.read_bytes()[0x40:0x2000], (beat, 'overlay text differs from AREA01.BIN')
        images[beat] = (ram, spad)

    cases, workers, entries = [], 0, {}
    # Captured: every live 0x825950 / 0x823CD0 node as recorded.
    for beat, (ram, spad) in images.items():
        for callback in (0x825950, 0x823CD0):
            for node in RV.owners(ram, callback):
                cases.append((beat, callback, (node,), ()))
        for node in RV.owners(ram, 0x825950):
            cases.append((beat, 0x825910, (node, node + 0x1F0, 0x82ACD0), ()))
            cases.append((beat, 0x826010, (node,), ()))
        # The first-visit light owner 001C50B0 takes its state-1 path on the
        # revisit (D_0081075D is 0xFF after AREA00): its colour local at
        # sp - 0x10 is the composition's stack (em_area01_math_core).
        for node in RV.owners(ram, 0x1C50B0):
            for state in (0, 1):
                cases.append((beat, 0x1C50B0, (node,), ((node + 4, bytes((state,))),)))
    capture_count = len(cases)

    beat = 'a01r_00_to_train_room'
    ram, _ = images[beat]
    g5, g7 = 0x7A64F0, 0x7A6AD0
    assert u32(ram, g5 + 0x10) == 0x825950 and ram[g5 + 0xD] == 0x47, 'g[5] moved'
    assert u32(ram, g7 + 0x10) == 0x825950 and ram[g7 + 0xD] == 0x4B, 'g[7] moved'
    creatures = RV.owners(ram, 0x823CD0)
    assert len(creatures) == 5, ('creatures', [hex(c) for c in creatures])
    designed = []
    # The event owner: its states, stages and steps.
    for state in (0, 1, 2, 3):
        for stage in (0, 1, 2, 0x10, 0x40, 0x80, 0xFF):
            for step in ((0, 1, 2) if state == 1 else (0,)):
                designed.append((g5, [(g5 + 4, b8(state)), (g5 + 5, b8(step)), (0x8107DF, b8(stage))]))
    # The second character: state 0 with D_00810845 bit 5 both ways; state 1
    # before and at the end stage.
    for bit in (0, 0x20):
        designed.append((g7, [(g7 + 4, b8(0)), (0x810845, b8(bit))]))
    for stage in (0x80, 0xFF):
        designed.append((g7, [(0x8107DF, b8(stage))]))
    for node, edits in designed:
        cases.append((beat, 0x825950, (node,), tuple(edits)))
    # The creatures: 0x823CD0 states and the revisit steps it calls.
    for i, node in enumerate(creatures):
        for sub, steps in ((1, (0,)), (2, (0, 1, 2, 3, 4, 5, 6, 7, 8)), (6, (0, 1, 2)), (7, (0, 1))):
            for step in steps:
                for stage in (0, 2):
                    if (i + step + stage) % 2 and sub == 2 and step > 1:
                        continue   # every step on half of the creatures, both stages on the rest
                    cases.append((beat, 0x823CD0, (node,),
                                   ((node + 4, b8(1)), (node + 5, b8(sub)), (node + 6, b8(step)),
                                    (0x8107DF, b8(stage)))))
        for state in (0, 2, 4, 5):
            cases.append((beat, 0x823CD0, (node,), ((node + 4, b8(state)),)))
    # 0x825910 around its compare, and 0x826010 across its six-frame counter.
    for value in (504.0, 505.0, 505.00003, 506.0, -1.0):
        cases.append((beat, 0x825910, (g5, g5 + 0x1F0, 0x82ACD0), ((g5 + 0x3C, f32(value)),)))
    for count in (5, 11, 179, 180, 185):
        for wait in (0, 1):
            cases.append((beat, 0x826010, (g5,), ((g5 + 0x2A, h16(count)), (g5 + 0x240, b8(wait)))))
    # The group 0x82A900 behaviours, on g[6]'s node as a spare record.
    spare = RV.owners(ram, 0x826CF0)
    assert spare, 'no 0x826CF0 node'
    for entry in (0x8237D0, 0x823900, 0x8239C0):
        for state in (0, 1):
            for count in (8, 9, 19):
                cases.append((beat, entry, (spare[0],), ((spare[0] + 4, b8(state)), (spare[0] + 0x28, h16(count)))))

    # Quick: every captured case, one of each entry and a fixed-seed sample
    # of the designed ones (EM_TEST_FULL=1: all).
    chosen = mode.select(list(range(len(cases))), min(len(cases), 72), 0xA01A5,
                         axes=(lambda k: cases[k][1],), keep=lambda i, k: k < capture_count)
    global STATE
    STATE = (native, elf, images, cases)
    for k, seen in zip(chosen, mode.parallel_map(run_index, chosen)):
        workers += seen
        entries[cases[k][1]] = entries.get(cases[k][1], 0) + 1
    for entry in BOUND:
        assert entries.get(entry if entry not in (0x824340, 0x824770, 0x824D50) else 0x823CD0), \
            ('entry not exercised', hex(entry))

    # Fail-stop: the first worker call refused stops the entry there.
    faults = 0
    ram, spad = images[beat]
    for entry, args, edits in ((0x825950, (g5,), ()), (0x823CD0, (creatures[0],), ()),
                               (0x823CD0, (creatures[0],), ((creatures[0] + 4, b8(1)), (creatures[0] + 5, b8(2)))),
                               (0x826010, (g5,), ((g5 + 0x2A, h16(5)), (g5 + 0x240, b8(0)))),
                               (0x8237D0, (spare[0],), ((spare[0] + 4, b8(1)), (spare[0] + 0x28, h16(9))))):
        run_case(native, elf, ram, spad, entry, args, publish(args[0]) + edits, refuse=True)
        faults += 1
    mode.banner(mode.part(len(chosen), len(cases), 'cases'))
    result = dict(cases=len(chosen), of=len(cases), capture_cases=capture_count, worker_calls=workers,
                  entries={hex(k): v for k, v in sorted(entries.items())}, fault_cases=faults,
                  seconds=round(time.monotonic() - start, 3))
    print('AREA01 revisit runtime binding: PASS', result)


if __name__ == '__main__':
    main()
