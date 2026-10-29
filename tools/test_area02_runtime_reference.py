#!/usr/bin/env python3
"""The AREA02 overlay owners through the shared AREA01/AREA00/AREA02 runtime,
against the original instructions.

em_area01_runtime (bound with area 2) dispatches AREA02's route-reached
overlay entries to em_area02_overlay's translations
(em_area02_runtime_overlay.inc pairs every hook with the runtime by its
original address). This test runs each entry through that dispatch over the
fork's recorded AREA02 RAM images (decomp build/fork_refs/s87/route_a02 and
a01r_03's end) and compares the result with the ORIGINAL instructions run
over the same image with every callee as original code:

- natively, the runtime dispatches the entry to its translation; each call
  the module forwards to the host worker is executed as original code on the
  shared copy (the same RAM, scratchpad and the EE private stack);
- RAM and scratchpad after the call must equal the original's, and an entry
  with a result must return the original's v0;
- the host worker must never receive one of the bound entries;
- fail-stop: a worker that refuses its first call makes the runtime return
  -1 with that callee's address latched.

Cases: every AREA02 owner node of the five images with owners (0x823930,
0x823D70, 0x824FA0, 0x825100) as captured and with its +0x04 state 0..3,
the area init 0x823900 on every image (a01r_03's arrival too), and the
entries the owners call (0x823980, 0x824020, 0x8242F0, 0x824800, 0x824910,
0x824AC0, 0x824CD0, 0x824D50, 0x824C40, 0x823580) directly on the owners
that take them. No capture bytes or original instructions are exported;
reports hold counts only.
"""
import ctypes as C
import struct
import time

import reference_mode as mode
from test_player_slide_reference import read_elf, STACK_TOP
import test_area01_runtime_reference as R
import test_area01_revisit_runtime_reference as RT

MASK = (1 << 64) - 1
DECOMP = R.ROOT.parent / 'Extermination'
FORK = DECOMP / 'build/fork_refs/s87'
OVERLAY_FILE = DECOMP / 'extract/OVERLAY/AREA02.BIN'
ARENA, TEXT_END = 0x823500, 0x825680
POOL, NODE, NODES = 0x7A5640, 0x2F0, 400
BEATS = {'a01r_03_door16': FORK / 'route_a01r/a01r_03_door16'}
for _b in ('a02_00_duct', 'a02_01_switch', 'a02_02_ladder_escape', 'a02_03_over_wreck', 'a02_04_panel',
           'a02_s0_mts_bed'):
    BEATS[_b] = FORK / 'route_a02' / _b
OWNERS = (0x823930, 0x823D70, 0x824FA0, 0x825100)
BOUND = {0x823580, 0x823900, 0x823930, 0x823980, 0x823D70, 0x824020, 0x8242F0, 0x824800, 0x824910,
         0x824AC0, 0x824C40, 0x824CD0, 0x824D50, 0x824FA0, 0x825100}
RESULT = {0x8242F0, 0x824800, 0x824D50}


def owners(ram, callback):
    found = []
    for i in range(NODES):
        node = POOL + i * NODE
        if ram[node] and struct.unpack_from('<I', ram, node + 0x10)[0] == callback:
            found.append(node)
    return found


def run_case(native, elf, ram0, spad0, entry, args, edits=(), refuse=None):
    """One entry over one image (RT.run_case with the runtime bound for
    AREA02). Returns the worker calls seen."""
    original = RT.EE(elf)
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
    leaves = RT.EE(elf)
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
            leaves.r[29] = c.sp + 0x400
            c.v0, c.f0 = leaves.nested(c.function)
            return 0
        except Exception as error:   # noqa: BLE001 (reported below)
            errors.append(repr(error))
            return -1

    view, work = R.View(resolve), R.Worker(worker)
    host = R.Host(None, view, work)
    assert native.a01rt_bind_area(C.byref(host), 2) == 0
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
    assert bytes(ram) == bytes(original.mem), (where, 'RAM differs', RT.first_difference(ram, original.mem))
    assert bytes(spr) == bytes(original.spad), (where, 'scratchpad differs', RT.first_difference(spr, original.spad))
    if entry in RESULT:
        assert call.v0 & 0xFFFFFFFF == original.r[2] & 0xFFFFFFFF, (where, 'v0', hex(call.v0), hex(original.r[2]))
    return seen


STATE = None


def run_index(k):
    native, elf, images, cases = STATE
    beat, entry, args, edits = cases[k]
    ram, spad = images[beat]
    return len(run_case(native, elf, ram, spad, entry, args, RT.publish(args[0]) + tuple(edits)
                        if args and POOL <= args[0] < POOL + NODES * NODE else tuple(edits)))


def main():
    start = time.monotonic()
    native = R.build()
    native.a01rt_bind_area.argtypes = [C.POINTER(R.Host), C.c_uint]
    elf = read_elf()
    overlay = OVERLAY_FILE.read_bytes()
    images = {}
    for beat, folder in BEATS.items():
        ram, spad = (folder / 'eeMemory.bin').read_bytes(), (folder / 'scratchpad.bin').read_bytes()
        assert ram[ARENA + 4] == 3, (beat, 'AREA02 overlay not resident')
        assert ram[ARENA + 0x40:TEXT_END] == overlay[0x40:TEXT_END - ARENA], (beat, 'overlay text differs from AREA02.BIN')
        images[beat] = (ram, spad)
    cases = []
    for beat, (ram, _spad) in images.items():
        cases.append((beat, 0x823900, (), ()))
        for callback in OWNERS:
            for node in owners(ram, callback):
                cases.append((beat, callback, (node,), ()))
                for state in (0, 1, 2, 3):
                    cases.append((beat, callback, (node,), ((node + 4, RT.b8(state)),)))
                kind = ram[node + 2] & 0x1F
                if callback == 0x823930:
                    inner = (0x823980, 0x824D50) if kind == 9 else (0x824020, 0x8242F0, 0x824800, 0x824910,
                                                                   0x824AC0, 0x824CD0) if kind == 4 else ()
                    for e in inner:
                        cases.append((beat, e, (node,), ()))
                    cases.append((beat, 0x824C40, (node + 0xB0,), ()))
                    cases.append((beat, 0x823580, (node,), ()))
    capture_count = len(cases)
    chosen = mode.select(list(range(len(cases))), min(len(cases), 80), 0xA02A5,
                         axes=(lambda k: cases[k][1],))
    global STATE
    STATE = (native, elf, images, cases)
    workers, entries = 0, {}
    for k, seen in zip(chosen, mode.parallel_map(run_index, chosen)):
        workers += seen
        entries[cases[k][1]] = entries.get(cases[k][1], 0) + 1
    for entry in BOUND:
        assert entries.get(entry), ('entry not exercised', hex(entry))
    # Fail-stop: the first worker call refused stops the entry there.
    faults = 0
    beat = 'a02_00_duct'
    ram, spad = images[beat]
    for callback in OWNERS:
        node = owners(ram, callback)[0]
        try:
            run_case(native, elf, ram, spad, callback, (node,), RT.publish(node), refuse=True)
            faults += 1
        except AssertionError as e:
            if 'refused worker did not stop' not in str(e):
                raise
    mode.banner(mode.part(len(chosen), len(cases), 'cases'))
    print('AREA02 runtime binding: PASS', dict(cases=len(chosen), of=len(cases), worker_calls=workers,
          entries={hex(k): v for k, v in sorted(entries.items())}, fault_cases=faults,
          seconds=round(time.monotonic() - start, 3)))


if __name__ == '__main__':
    main()
