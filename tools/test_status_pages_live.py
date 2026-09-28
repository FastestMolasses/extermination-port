#!/usr/bin/env python3
"""Replay a live status-pages trace (EM_STATUS_PAGES_TRACE, written by
em_status_pages_live) through the ORIGINAL instructions.

For every page call the port made (00214570 EQUIPMENT, 00215870 EVENT,
002160B0 HEALING, 00211970 SPR4, 00214020 DATABASE, 0020F950 MAP) and every
MAP node call (002101C0, which the pool walk 001B0000 runs for each of
MAP's 22 nodes: its own record, a0 = the node's pool record, the pool view
that record alone), the original routine runs in the EE interpreter over a
captured image of the first level
(the status-hub capture, ../Extermination/build/startup-reference/status-hub)
with the port's view bytes of that call written over it: the status block,
the request bytes, the progress block's migrated ranges, the vitals, the
pads, the message block, the scratchpad work area, the stack below the
call's sp and the other buffers em_status_pages_live.h lists. Every routine
the port runs as one translation runs as original code; every callee the
port reached through its dispatcher is hooked instead, in order:
  - its address, stack pointer, 64-bit argument registers and float
    argument registers must equal the port's (this is the binding's glue:
    the register images, the caller's s0, the v0 results);
  - the bytes the port's callee wrote are written, and v0 / f0 returned.
Any call the original makes that the port did not (or the reverse) fails.
At the end every view byte of the original's memory must equal the port's,
except the stack (the original saves its registers in its frames; the
port's translations only carve them): the stack bytes a callee reads are
the ones the port hands it, compared through that callee's arguments.

    python3 tools/test_status_pages_live.py TRACE [--image DIR] [--limit N]

A trace holds only what em_status_pages_live recorded; the callees' own
translations are verified by their own reference tests.
"""
import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import test_status_pages_reference as S  # noqa: E402
from test_player_slide_reference import EE, RETURN, read_elf  # noqa: E402

M64 = 0xFFFFFFFFFFFFFFFF
STACK_BASE, STACK_END = 0x01FF0000, 0x02000000   # em_status_pages_live's EE stack view
IMAGE = ROOT.parent / 'Extermination/build/startup-reference/status-hub'


class Reader:
    def __init__(self, data):
        self.d, self.p = data, 0

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def u32(self):
        v = struct.unpack_from('<I', self.d, self.p)[0]
        self.p += 4
        return v

    def bytes(self, n):
        v = self.d[self.p:self.p + n]
        self.p += n
        return v

    def views(self):
        return [(self.u32(), self.bytes(self.u32())) for _ in range(self.u32())]

    def done(self):
        return self.p >= len(self.d)


def parse(data):
    r = Reader(data)
    calls = []
    while not r.done():
        tag = r.u8()
        assert tag == ord('T'), ('trace: expected T', r.p, tag)
        page, a0, s0, sp = r.u32(), r.u32(), r.u32(), r.u32()
        before = r.views()
        callees = []
        while True:
            tag = r.u8()
            if tag == ord('E'):
                rc = r.u32()
                after = r.views()
                break
            assert tag == ord('C'), ('trace: expected C or E', r.p, tag)
            target, csp, na = r.u32(), r.u32(), r.u32()
            a = [r.u32() | r.u32() << 32 for _ in range(na)]
            nf = r.u32()
            f = [r.u32() for _ in range(nf)]
            crc, v0 = r.u32(), r.u32() | r.u32() << 32
            f0 = r.u32()
            writes = [(r.u32(), None) for _ in range(0)]
            n = r.u32()
            writes = []
            for _ in range(n):
                address, size = r.u32(), r.u32()
                writes.append((address, r.bytes(size)))
            callees.append(dict(target=target, sp=csp, a=a, f=f, rc=crc, v0=v0, f0=f0, writes=writes))
        calls.append(dict(page=page, a0=a0, s0=s0, sp=sp, before=before, callees=callees, rc=rc,
                          after=after))
    return calls


def replay(elf, ram, spad, call, index):
    ee = EE(elf, ram, spad)
    for address, data in call['before']:
        ee.write(address, data)
    queue = list(call['callees'])
    # The routines of the page's own lane run as original code (the port
    # calls them directly); every other callee is hooked (the port's
    # dispatcher reached it).
    family = set(S.A01) if call['page'] in S.A01 else set(S.LANE)
    hooked = (set(S.WSPEC) - family) | {c['target'] for c in queue}
    failures = []

    def hook(e):
        pc = e.pc_hooked
        if not queue:
            raise AssertionError(f'call {index}: the original called {pc:08X}; the port made no further call')
        c = queue.pop(0)
        where = f'call {index} ({call["page"]:08X}) callee {len(call["callees"]) - len(queue)}'
        assert c['target'] == pc, f'{where}: original {pc:08X}, port {c["target"]:08X}'
        assert (e.r[29] & 0xFFFFFFFF) == c['sp'], f'{where} {pc:08X}: sp {e.r[29]:#x} vs port {c["sp"]:#x}'
        regs = [4, 5, 6, 7, 8, 9, 10, 11]
        for i, value in enumerate(c['a']):
            got = e.r[regs[i]] & M64
            assert got == value, f'{where} {pc:08X}: a{i} original {got:#x}, port {value:#x}'
        for i, value in enumerate(c['f']):
            got = e.f[12 + i] & 0xFFFFFFFF
            assert got == value, f'{where} {pc:08X}: f{12 + i} original {got:#x}, port {value:#x}'
        assert c['rc'] == 0, f'{where} {pc:08X}: the port\'s callee failed'
        for address, data in c['writes']:
            e.write(address, data)
        e.r[2] = c['v0']
        e.f[0] = c['f0']

    for address in hooked:
        def make(address):
            def h(e):
                e.pc_hooked = address
                hook(e)
            return h
        ee.hooks[address] = make(address)
    ee.r[29] = call['sp']
    ee.r[4] = call['a0']
    ee.r[16] = call['s0']
    ee.r[31] = RETURN
    ee.run(call['page'])
    assert not queue, f'call {index}: the port made {len(queue)} more callee calls ({queue[0]["target"]:08X})'
    for address, data in call['after']:
        if STACK_BASE <= address < STACK_END:
            continue    # the frames' register save slots are the original's own
        got = ee.read(address, len(data))
        if got != data:
            k = next(i for i in range(len(data)) if got[i] != data[i])
            failures.append(f'call {index} ({call["page"]:08X}): byte {address + k:#x} original '
                            f'{got[k]:#04x}, port {data[k]:#04x}')
    return failures


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('trace', type=Path)
    ap.add_argument('--image', type=Path, default=IMAGE)
    ap.add_argument('--limit', type=int, default=0)
    args = ap.parse_args()
    elf = read_elf()
    ram = (args.image / 'eeMemory.bin').read_bytes()
    spad = (args.image / 'scratchpad.bin').read_bytes()
    calls = parse(args.trace.read_bytes())
    if args.limit:
        calls = calls[:args.limit]
    failures, pages, callees = [], {}, 0
    for i, call in enumerate(calls):
        failures += replay(elf, ram, spad, call, i)
        pages[call['page']] = pages.get(call['page'], 0) + 1
        callees += len(call['callees'])
    summary = ', '.join(f'{p:08X} x{n}' for p, n in sorted(pages.items()))
    print(f'status pages live: {len(calls)} page calls ({summary}), {callees} callee entries equal '
          f'to the original\'s')
    for f in failures[:20]:
        print('  ' + f)
    if failures:
        print(f'status pages live: FAIL ({len(failures)} view differences)')
        return 1
    print('status pages live: PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
