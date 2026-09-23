#!/usr/bin/env python3
"""Area load (WP-3 S12a): the load veil, and the live chain tick by tick,
against the executed original.

1. Veil. Executes the original 0021B180, 0021B550 and 0021B840 (001ADF50's
   load veil over the block *D_00275888) from the user's ELF and compares
   src/game/em_load_veil.c: every byte of the block the three touch, the
   0021B550 result, and the ordered callee list (001D2830 with its two
   arguments, 0021B1B0/0021B500 with the block). Cases: every state 0..4 and
   0xFF x sub 0..2 x sub2 0..4 x levels (0, tiny, mid, near 1, 1), and whole
   sequences (0021B180, a ramp of up to 140 ticks, 0021B840, the decay to
   state 3) that exercise the EE float truncation of add.s/mul.s.

2. Chain. With --log (the file EM_AREA_CHANGE_LOG writes, see
   em_scene_bindings.c), replays every logged tick of the slot-0 task in
   which the load chain runs (001ACEC0 with +8 = 0, 1, 2, or +8 = 3 and
   +9 = 0 or 5) through the S3 oracle (tools/test_scene_task_reference.py)
   extended to EXECUTE 001AD1A0 as well, with the port's worker model: the
   screen module and area reads 001FF080 complete inside the call (D_00275BD8
   = 0 after it), and 001AD230/0021B550 return what the native returned. The
   state after the tick (task +8..+0x1F, request block, area bytes,
   D_00810730, counters, scratchpad, flags, input words) and the trace
   (caller, callee, declared arguments) must be identical. A tick whose frame
   machine called 001AD010 (the area change) replays 001AD010 from the state
   at its call. Every logged veil step is replayed through the executed
   0021B550/0021B180/0021B840 too. The frame machine's own ticks (+9 = 1) are
   S2's (tools/test_scene_frame_reference.py) and are not replayed here.

3. Capture. The New Game part of the log must pass through the same
   sequence of distinct task states (+8, +9, +A, +B) as the original New Game
   load captured in PCSX2 (build/startup-reference/newgame_samples.jsonl,
   read-only), from the cleared record to state 1: the original's load waits
   last longer (disc reads), the order is the same.

The Makefile target runs the app (headless) to write the log: New Game to
first control, then EM_AREA_CHANGE_TEST's 001B0C60(0x0B, 0, 0) reload.
No original bytes are embedded or printed; only addresses and values.
"""
import argparse
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

from test_point_light_reference import Oracle, RETURN, bits, number, signed
import test_scene_task_reference as tsr
import reference_mode

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
VEIL = 0x900000
VEIL_PTR = 0x275888
VEIL_SIZE = 0x1C
VEIL_MODELLED = [i for i in range(VEIL_SIZE) if not 0x14 <= i < 0x18]
VEIL_FUNCS = {0x21B180: 0x2C, 0x21B550: 0x2E4, 0x21B840: 0x1C}
VEIL_STUBS = (0x1D2830, 0x21B1B0, 0x21B500)
CAPTURE = DECOMP / 'build/startup-reference/newgame_samples.jsonl'


# ------------------------------------------------------------------ veil oracle
class VeilOracle(Oracle):
    """Base oracle; a tail `j` to a stub returns to $ra."""

    def __init__(self, elf):
        super().__init__(elf)
        self.events = []
        self.save(VEIL_PTR, VEIL)

    def execute(self, entry):
        self.r[31] = RETURN
        pc = entry
        for _ in range(20000):
            if pc == RETURN:
                return self.r[2] & 0xFFFFFFFF
            assert any(s <= pc < s + n for s, n in VEIL_FUNCS.items()), ('pc left the veil', hex(pc))
            word = self.load(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            if op in (2, 3):
                target = (word & 0x3FFFFFF) * 4
                if op == 3:
                    self.r[31] = pc + 8
                self.plain(self.load(pc + 4))
                assert target in VEIL_STUBS, ('unexpected callee', hex(target))
                self.events.append((target, self.r[4] & 0xFFFFFFFF, self.r[5] & 0xFFFFFFFF))
                pc = pc + 8 if op == 3 else self.r[31]
                continue
            imm = signed(word & 0xFFFF, 16) * 4
            branch = None
            if op in (4, 5, 20, 21):
                taken = (self.r[rs] & 0xFFFFFFFF) == (self.r[rt] & 0xFFFFFFFF)
                taken = taken if op in (4, 20) else not taken
                if op >= 20 and not taken:
                    pc += 8
                    continue
                branch = pc + 4 + imm if taken else pc + 8
            elif op == 17 and rs == 8:
                taken = self.condition == bool(rt & 1)
                if rt & 2 and not taken:
                    pc += 8
                    continue
                branch = pc + 4 + imm if taken else pc + 8
            elif op == 0 and word & 63 == 8:
                branch = self.r[rs] & 0xFFFFFFFF
            if branch is not None:
                self.plain(self.load(pc + 4))
                pc = branch
                continue
            self.plain(word)
            pc += 4
        raise AssertionError('veil function did not return')


def veil_original(elf, block, calls):
    """Run `calls` (function addresses) in order over `block`; returns the
    block after each, the results and the callee events per call."""
    o = VeilOracle(elf)
    o.write(VEIL, block)
    out = []
    for fn in calls:
        o.events = []
        r = o.execute(fn)
        out.append((bytes(o.read(VEIL, VEIL_SIZE)), r if fn == 0x21B550 else 0, list(o.events)))
    return out


VEIL_SHIM = r'''
#include <string.h>
#include "game/em_load_veil.h"
typedef struct { unsigned n; unsigned ev[64][3]; } Rec;
static Rec rec;
static int d2830(void *c, int a, int b) { (void)c; if (rec.n < 64) { rec.ev[rec.n][0] = 0x1D2830u; rec.ev[rec.n][1] = (unsigned)a; rec.ev[rec.n][2] = (unsigned)b; } rec.n++; return 0; }
static int b1b0(void *c, EmLoadVeil *v) { (void)c; (void)v; if (rec.n < 64) { rec.ev[rec.n][0] = 0x21B1B0u; rec.ev[rec.n][1] = 0x%(veil)Xu; rec.ev[rec.n][2] = 0; } rec.n++; return 0; }
static int b500(void *c, EmLoadVeil *v) { (void)c; (void)v; if (rec.n < 64) { rec.ev[rec.n][0] = 0x21B500u; rec.ev[rec.n][1] = 0x%(veil)Xu; rec.ev[rec.n][2] = 0; } rec.n++; return 0; }
static void unpack(EmLoadVeil *v, const unsigned char *b)
{
    v->state = b[0]; v->sub = b[1]; v->sub2 = b[2]; v->b03 = b[3];
    memcpy(&v->w04, b + 4, 4); memcpy(v->level, b + 8, 12); memcpy(&v->w18, b + 0x18, 4);
}
static void pack(const EmLoadVeil *v, unsigned char *b)
{
    b[0] = v->state; b[1] = v->sub; b[2] = v->sub2; b[3] = v->b03;
    memcpy(b + 4, &v->w04, 4); memcpy(b + 8, v->level, 12); memcpy(b + 0x18, &v->w18, 4);
}
int veil_shim(unsigned char *block, unsigned fn, unsigned *events, unsigned *nev)
{
    EmLoadVeil v; unpack(&v, block);
    EmLoadVeilWorkers w = { 0, d2830, b1b0, b500 };
    unsigned at = 0; int r = 0;
    rec.n = 0;
    if (fn == 0x21B180u) r = em_load_veil_0021B180(&v, &w, &at);
    else if (fn == 0x21B550u) r = em_load_veil_0021B550(&v, &w, &at);
    else if (fn == 0x21B840u) em_load_veil_0021B840(&v);
    pack(&v, block);
    *nev = rec.n;
    memcpy(events, rec.ev, sizeof rec.ev);
    return r;
}
'''


def build_veil_native():
    out = ROOT / 'build/area_load_reference'
    out.mkdir(parents=True, exist_ok=True)
    source = out / 'veil_shim.c'
    source.write_text(VEIL_SHIM % {'veil': VEIL})
    lib = out / ('veil.dylib' if sys.platform == 'darwin' else 'veil.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
                    '-fPIC', '-Isrc', str(source), 'src/game/em_load_veil.c', '-lm', '-o', str(lib)],
                   cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    native.veil_shim.argtypes = [C.c_char_p, C.c_uint, C.POINTER(C.c_uint), C.POINTER(C.c_uint)]
    return native


def veil_native(native, block, calls):
    buf = C.create_string_buffer(bytes(block), VEIL_SIZE)
    out = []
    for fn in calls:
        events, n = (C.c_uint * 192)(), C.c_uint()
        r = native.veil_shim(buf, fn, events, C.byref(n))
        assert n.value <= 64
        evs = [(events[3 * i], events[3 * i + 1], events[3 * i + 2]) for i in range(n.value)]
        out.append((bytes(buf.raw[:VEIL_SIZE]), r, evs))
    return out


def modelled(block):
    return bytes(block[i] for i in VEIL_MODELLED)


def veil_event_key(event):
    callee, a0, a1 = event
    return (callee, a0, a1) if callee == 0x1D2830 else (callee, a0)


def compare_veil(label, original, native):
    assert len(original) == len(native)
    for k, ((ob, orr, oe), (nb, nr, ne)) in enumerate(zip(original, native)):
        assert modelled(ob) == modelled(nb), (label, 'block after call', k, ob.hex(), nb.hex())
        assert orr == nr, (label, 'result', k, orr, nr)
        assert [veil_event_key(e) for e in oe] == [veil_event_key(e) for e in ne], (label, 'callees', k, oe, ne)


def veil_block(state, sub, sub2, levels, rng):
    b = bytearray(rng.randrange(256) for _ in range(VEIL_SIZE))
    b[0], b[1], b[2] = state, sub, sub2
    for i, level in enumerate(levels):
        b[8 + 4 * i:12 + 4 * i] = struct.pack('<f', level)
    return bytes(b)


def check_veil(elf, native):
    rng = random.Random(0x21B550)
    level_sets = [(0.0, 0.0, 0.0), (0.004, 0.009, 0.0099), (0.5, 0.31, 0.77), (0.995, 0.993, 0.999),
                  (1.0, 1.0, 1.0), (0.011, 0.0101, 0.0105)]
    cases = list(itertools.product((0, 1, 2, 3, 4, 0xFF), range(3), range(5), range(len(level_sets))))
    for state, sub, sub2, li in cases:
        block = veil_block(state, sub, sub2, level_sets[li], rng)
        for fn in (0x21B550, 0x21B180, 0x21B840):
            compare_veil(('single', hex(fn), state, sub, sub2, li), veil_original(elf, block, [fn]),
                         veil_native(native, block, [fn]))
    ramps = reference_mode.pick(list(range(0, 141, 1)), [0, 1, 2, 3, 5, 20, 99, 100, 101, 140])
    for ramp in ramps:
        block = veil_block(0, 0, 0, (0.0, 0.0, 0.0), rng)
        calls = [0x21B180] + [0x21B550] * ramp + [0x21B840] + [0x21B550] * 60
        compare_veil(('sequence', ramp), veil_original(elf, block, calls), veil_native(native, block, calls))
    return len(cases) * 3, len(ramps)


# ------------------------------------------------------------------ chain replay
tsr.FUNCS[0x1AD1A0] = 0x90            # executed here (byte-matched), not stubbed
TRACE_EXTRA_ARGS = {0x200830: 0, 0x1D19D0: 0}   # D_0028A564 is a pointer the port has no value for


class ChainOracle(tsr.TaskOracle):
    def load(self, address, size=4):
        if self.recording and address == 0x28A564 and size == 4:
            return 0                   # D_0028A564 (001AD1A0's module-3 packet word)
        return super().load(address, size)

    def stub(self, caller, callee):
        super().stub(caller, callee)
        if callee == 0x1FF080:
            tsr.Oracle.save(self, 0x275BD8, 0, 1)   # the port's reads complete inside the call


def chain_original(elf, entry, snap, config):
    o = ChainOracle(elf)
    for address, offset in tsr.OFFSET.items():
        o.save(address, snap[offset], 1)
    o.save(tsr.SLOT, tsr.TASK)
    o.save(0x28A9A0, config['fade'] & 0xFFFF, 2)
    o.save(0x282157, 0, 1)
    o.config = config
    ret = o.execute(entry)
    return o.snapshot(), o.trace, ret


def trace_key(entry):
    caller, callee = entry[0], entry[1]
    if callee in TRACE_EXTRA_ARGS:
        return (caller, callee)
    return tsr.trace_key(tuple(entry))


def chain_ticks(ticks):
    """Split the logged ticks into replayable chain ticks and 001AD010 calls."""
    chain, d010 = [], []
    for t in ticks:
        pre = bytes.fromhex(t['pre'])
        s08, s09 = pre[0], pre[1]
        if s08 in (0, 1, 2) or (s08 == 3 and s09 in (0, 5)):
            chain.append(t)
        if t.get('d010'):
            d010.append(t)
    return chain, d010


def replay_chain(elf, ticks):
    chain, d010 = chain_ticks(ticks)
    for t in chain:
        pre, post = bytes.fromhex(t['pre']), bytes.fromhex(t['post'])
        assert len(pre) == tsr.SNAP, ('log layout', len(pre), tsr.SNAP)
        results = {0x1AD1A0: 0, 0x1AD230: max(t['r_001AD230'], 0), 0x21B550: max(t['r_0021B550'], 0)}
        final, trace, _ = chain_original(elf, 0x1ACEC0, pre,
                                         {'fade': t['fade'], 'busy': 0, 'results': results})
        if final != post:
            diff = [hex(a) for a, o in tsr.OFFSET.items() if final[o] != post[o]]
            raise AssertionError(('tick', t['tick'], 'state after the tick', diff[:8]))
        assert not t['overflow'], ('tick', t['tick'], 'trace overflow')
        got = [trace_key(e) for e in t['trace']]
        want = [trace_key(e) for e in trace]
        assert got == want, ('tick', t['tick'], 'trace', [tuple(map(hex, e)) for e in got],
                             [tuple(map(hex, e)) for e in want])
    for t in d010:
        pre, post = bytes.fromhex(t['d010'][0]), bytes.fromhex(t['d010'][1])
        final, trace, _ = chain_original(elf, 0x1AD010, pre,
                                         {'fade': 2, 'busy': 0, 'results': {0x1AD1A0: 0, 0x1AD230: 0,
                                                                            0x21B550: 0}})
        if final != post:
            diff = [hex(a) for a, o in tsr.OFFSET.items() if final[o] != post[o]]
            raise AssertionError(('tick', t['tick'], '001AD010', diff[:8]))
        got = [trace_key(e) for e in t['trace'] if e[0] == 0x1AD010]
        want = [trace_key(e) for e in trace]
        assert got == want, ('tick', t['tick'], '001AD010 trace', got, want)
    return len(chain), len(d010)


def replay_veil(elf, ticks):
    steps = 0
    for t in ticks:
        calls = [e[1] for e in t['trace'] if e[1] in VEIL_FUNCS]
        if not calls:
            continue
        pre, post = bytes.fromhex(t['veil_pre']), bytes.fromhex(t['veil_post'])
        run = veil_original(elf, pre, calls)
        assert modelled(run[-1][0]) == modelled(post), ('tick', t['tick'], 'veil block', calls)
        r = [res for (_, res, _), fn in zip(run, calls) if fn == 0x21B550]
        if r:
            assert r[-1] == t['r_0021B550'], ('tick', t['tick'], '0021B550 result', r[-1], t['r_0021B550'])
        steps += len(calls)
    return steps


def distinct(states):
    out = []
    for s in states:
        if not out or out[-1] != s:
            out.append(s)
    return out


def check_capture(ticks):
    captured, seen = [], False
    for line in CAPTURE.open():
        d = json.loads(line)
        task = d['tasks'][0]
        if task['fn'] != '0x1acec0':
            continue
        seen = True
        captured.append(task['user'][:8])
        if task['user'][:8] == '03010001':
            break
    assert seen and captured[-1] == '03010001', 'capture has no New Game load to state 1'
    native = [ticks[0]['pre'][:8]]
    for t in ticks:
        native.append(t['post'][:8])
        if t['post'][:8] == '03010001':
            break
    want, got = distinct(captured), distinct(native)
    assert got == want, ('New Game task-state sequence', got, want)
    load_ticks = sum(1 for t in ticks[:len(native) - 1] if bytes.fromhex(t['pre'])[1] == 5
                     and bytes.fromhex(t['pre'])[0] == 3)
    return len(want), load_ticks


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--log', type=Path, help='EM_AREA_CHANGE_LOG file of a newgame-control run')
    args = parser.parse_args()
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256
    native = build_veil_native()
    singles, sequences = check_veil(elf, native)
    reference_mode.banner(f'{singles} veil single-call cases',
                          reference_mode.part(sequences, 141, 'veil ramp sequences'))
    print(f'veil: PASS ({singles} single calls, {sequences} ramp/decay sequences identical to the '
          f'executed 0021B180/0021B550/0021B840)')
    if not args.log:
        print('area load reference: PASS (veil only; pass --log for the chain replay)')
        return 0
    ticks = [json.loads(line) for line in args.log.open()]
    assert ticks, 'empty log'
    n_chain, n_d010 = replay_chain(elf, ticks)
    steps = replay_veil(elf, ticks)
    n_states, load_ticks = check_capture(ticks)
    assert n_d010 >= 1, 'the log has no 001AD010 area change (run with EM_AREA_CHANGE_TEST=1)'
    reloads = [t for t in ticks if bytes.fromhex(t['pre'])[:2] == b'\x03\x05']
    print(f'chain: PASS ({n_chain} chain ticks and {n_d010} 001AD010 call(s) of {len(ticks)} logged ticks '
          f'identical to the executed original; {steps} veil steps replayed)')
    print(f'capture: PASS (New Game passes the {n_states} captured task states in order; 001ADF50 '
          f'took {load_ticks} ticks natively; {len(reloads)} load ticks in the whole log)')
    print('area load reference: PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
