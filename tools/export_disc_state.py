#!/usr/bin/env python3
"""export_disc_state.py - the EE state of the first world frame of AREA11,
built from the user's own disc by executing the original code.

Library for tools/export_disc_textures.py (parts `interaction` and
`background`), tools/export_interaction_scan.py and
tools/test_disc_assets_reference.py (docs/DISC_TEXTURES.md section 9.4).

Two first-level assets were read from a PCSX2 capture because their values
are written at run time: `interaction.emis` (the use-owners' status,
selector and +0x30 descriptor pointer, written by each owner's first tick)
and `background.embg` (render channel 3's GS draw state, built by 001E1E60
every world frame). Here the original instructions of the pinned boot ELF
and of the AREA11 overlay are executed, in the order a New Game reaches
them, over the EE memory the disc loads leave (export_disc_textures_gs.
first_level_memory: the ELF image, every resident region and the resource
table D_0028A490):

  1. the boot's render builder sub_EXTERMINATION 001D0F20 (the packet arena,
     the GS register blocks at D_00275674, the render flags);
  2. the game task in slot 0: 001AB740(0, 001ACEC0), the slot 001AB6A0
     hands the task in scratchpad 0x70003B6C;
  3. New Game: 001AD230 (its 001AF2C0 reset), then 001AD360 step 4 (the
     area bytes D_00810700..702 = 0x0B, 0, 0 and render flag 3);
  4. the area load: the AREA11 overlay at 0x823500 (the extract's
     OVERLAY/AREA11.BIN, its pinned MWo3 header checked) and the init the
     area dispatcher calls, 008237C0;
  5. 0x1AE040 state 0, the whole gameplay bring-up (001AFCA0, 001AFCF0,
     001B07C0(0), 001B6990, 001D19E0, 001C1DC0, 00199C50, 001AEE40(4),
     001FAE70(1), 001C5C50, 001D1EF0);
  6. the first world frame: main-loop step B 001D1AE0(D_00810E80), then
     0x1AE040 state 1 through the end of 001AE5E0's actor walk 001AFD70(0)
     (the player tick, 001D1C50, 001C1D00 with 001E0CF0 -> 001E1E60, and
     every owner's first tick);
  7. the nodes the first tick leaves in state 3 (record 13, 008257A0) are
     freed by their own next tick: the walk 001AFD70(0) once more with
     every other behaviour held.

Boundaries (asserted, reported in `DiscState.boundaries`):
  - the flame's behaviour 008235F0 (AREA11 place 7) is not executed in
    step 6: its draw reaches VU0 VMINI, which the measured VU model
    (tools/ee_float_model.py) does not define. Neither asset reads it.
  - step 7 holds every behaviour except the state-3 nodes' own.
  - the DMA controller: a kick (CHCR with STR) is recorded and completes
    at once (host speed); a read of CHCR returns it with STR clear, MADR /
    QWC / TADR return what was written. Only from-memory kicks are
    accepted (they write no EE RAM); any other hardware access faults.

Each step is original code; nothing here writes a value the original
writes, except the task-slot bytes the task router (001ACEC0 -> 001AD250
-> 001AD4D0, not executed) leaves for each entry: +0xA = 4 for 001AD360
step 4, +0xA = +0xB = 0 for the frame machine's state 0.

Runs natively on arm64 macOS (pure Python), about 10 s.
"""
from __future__ import annotations

import hashlib
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import export_disc_textures_gs as G  # noqa: E402
import test_render_verify_rest_reference as rvr  # noqa: E402

MASK64 = (1 << 64) - 1
OVERLAY_ARENA, OVERLAY_SIZE, OVERLAY_ID = 0x823500, 0x7800, 9
AREA = (0x0B, 0, 0)                      # 001AD360 step 4 (checked after it runs)

BOOT_RENDER = 0x1D0F20                   # sub_EXTERMINATION
TASK_INSTALL, GAME_TASK, TASK_TABLE = 0x1AB740, 0x1ACEC0, 0x28A750
TASK_POINTER = 0x70003B6C
NEW_GAME_RESET, NEW_GAME_AREA = 0x1AD230, 0x1AD360
OVERLAY_INIT = 0x8237C0
FRAME_MACHINE = 0x1AE040
STEP_B, BUFFER_INDEX = 0x1D1AE0, 0x810E80
WORLD_FRAME, WALK = 0x1AE5E0, 0x1AFD70
LIST_HEAD = 0x275BC0
FLAME = 0x8235F0                         # AREA11 place 7: not executed (VU0 VMINI)
CTX_POINTER = 0x275670

# The DMAC channels modelled (the three that send EE memory to the VUs / GS)
# and their registers CHCR +0, MADR +0x10, QWC +0x20, TADR +0x30. Every other
# hardware address faults.
DMAC_CHANNELS = {0x10008000: 'VIF0', 0x10009000: 'VIF1', 0x1000A000: 'GIF'}
WORLD_FRAME_SIZE = 0xD0                  # 001AE5E0 .. 001AE6B0 (the next routine)


class HardwareAccess(Exception):
    pass


class Stop(Exception):
    pass


class DiscEE(rvr.RvrEE):
    """The shared EE core (COP1 / VU0 macro on the measured model, VCLIPW,
    the CLIP register, PEXTLW / PEXTUW) plus PCPYH, the DMAC registers
    above and a call stack for diagnostics."""

    def __init__(self, elf: bytes, ram: bytearray):
        super().__init__(elf, ram, None)
        self.dmac = {}
        self.kicks = []
        self.hardware = []
        self.cstack = []

    # ---- hardware ----------------------------------------------------
    def _dmac(self, address):
        base, reg = address & ~0xFF, address & 0xFF
        return (base, reg) if base in DMAC_CHANNELS and reg in (0, 0x10, 0x20, 0x30) else None

    def _hw(self, address):
        a = address & 0xFFFFFFFF
        return 0x10000000 <= a < 0x20000000

    def load(self, address, size=4):
        if self._hw(address):
            a = address & 0xFFFFFFFF
            d = self._dmac(a)
            self.hardware.append(('read', a))
            if d is None or size != 4:
                raise HardwareAccess(f'read {a:#x} at {self.where()}')
            value = self.dmac.get(a, 0)
            return value & ~0x100 if d[1] == 0 else value
        return super().load(address, size)

    def save(self, address, value, size=4):
        if self._hw(address):
            a = address & 0xFFFFFFFF
            d = self._dmac(a)
            self.hardware.append(('write', a))
            if d is None or size != 4:
                raise HardwareAccess(f'write {a:#x} at {self.where()}')
            value &= 0xFFFFFFFF
            self.dmac[a] = value
            if d[1] == 0 and value & 0x100:
                base = d[0]
                if base == 0x10009000 and not value & 1:
                    raise HardwareAccess(f'{DMAC_CHANNELS[base]} kick to memory at {self.where()}')
                self.kicks.append(dict(channel=DMAC_CHANNELS[base], chcr=value,
                                       madr=self.dmac.get(base + 0x10, 0), qwc=self.dmac.get(base + 0x20, 0),
                                       tadr=self.dmac.get(base + 0x30, 0), caller=self.where()))
            return
        super().save(address, value, size)

    def write(self, address, data):
        if self._hw(address):
            raise HardwareAccess(f'block write {address & 0xFFFFFFFF:#x} at {self.where()}')
        super().write(address, data)

    def where(self):
        return ' <- '.join(f'{t:#x}' for t, _ in reversed(self.cstack[-6:])) or 'top'

    # ---- MMI ---------------------------------------------------------
    def mmi(self, word, pc):
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        fn, sub = word & 63, word >> 6 & 31
        if (fn, sub) == (0x29, 0x1B):                                          # pcpyh
            b = (self.r[rt] & MASK64) | (self.rh[rt] << 64)
            lo, hi = b & 0xFFFF, (b >> 64) & 0xFFFF
            v = sum(lo << (16 * i) for i in range(4)) | sum(hi << (64 + 16 * i) for i in range(4))
            if rd:
                self.r[rd] = v & MASK64
                self.rh[rd] = v >> 64
            return
        super().mmi(word, pc)

    # ---- the run loop, with a call stack -----------------------------
    def run(self, pc):
        hooks, load, limit = self.hooks, self.load, self.limit
        steps = 0
        base = len(self.cstack)
        try:
            while True:
                if pc == rvr.RETURN:
                    return
                while len(self.cstack) > base and self.cstack[-1][1] == pc:
                    self.cstack.pop()
                hook = hooks.get(pc)
                if hook is not None:
                    hook(self)
                    pc = self.r[31] & 0xFFFFFFFF
                    continue
                word = load(pc)
                steps += 1
                if steps > limit:
                    raise AssertionError(('step limit', hex(pc)))
                op = word >> 26
                if op in (2, 3):
                    if op == 3:
                        self.r[31] = pc + 8
                    self.execute(load(pc + 4), pc + 4)
                    target = (pc & 0xF0000000) | ((word & 0x3FFFFFF) << 2)
                    if op == 3:
                        self.cstack.append((target, pc + 8))
                    pc = target
                    continue
                if op == 0 and word & 63 in (8, 9):
                    target = self.r[word >> 21 & 31] & 0xFFFFFFFF
                    if word & 63 == 9:
                        rd = word >> 11 & 31
                        if rd:
                            self.r[rd] = pc + 8
                        self.cstack.append((target, pc + 8))
                    self.execute(load(pc + 4), pc + 4)
                    pc = target
                    continue
                b = self.branch(word, pc)
                if b is None:
                    self.execute(word, pc)
                    pc += 4
                    continue
                taken, target, likely = b
                if taken:
                    self.execute(load(pc + 4), pc + 4)
                    pc = target
                elif likely:
                    pc += 8
                else:
                    self.execute(load(pc + 4), pc + 4)
                    pc += 8
        finally:
            self.steps += steps
            del self.cstack[base:]

    def u32(self, address):
        return struct.unpack_from('<I', self.mem, address & 0x1FFFFFF)[0]

    def nodes(self):
        out, node = [], self.u32(LIST_HEAD)
        while node:
            out.append(node)
            node = self.u32(node + 0x1C)
            if len(out) > 0x100:
                raise AssertionError('the actor list does not end')
        return out


@dataclass
class DiscState:
    mem: bytes
    spad: bytes
    steps: list = field(default_factory=list)
    boundaries: list = field(default_factory=list)
    kicks: list = field(default_factory=list)
    hardware: list = field(default_factory=list)
    first_tick_nodes: list = field(default_factory=list)
    freed: list = field(default_factory=list)
    field_writers: dict = field(default_factory=dict)
    snapshots: dict = field(default_factory=dict)
    elf_sha256: str = ''
    overlay_sha256: str = ''

    def image(self) -> bytes:
        """32 MB EE RAM in the layout of a capture's eeMemory.bin."""
        return self.mem


def overlay_bytes(extract: Path = G.EXTRACT) -> bytes:
    data = (extract / 'OVERLAY/AREA11.BIN').read_bytes()
    if len(data) != OVERLAY_SIZE or data[:4] != b'MWo3' or \
            struct.unpack_from('<2I', data, 4) != (OVERLAY_ID, OVERLAY_ARENA):
        raise SystemExit(f'{extract}/OVERLAY/AREA11.BIN: not the SCUS-97112 AREA11 overlay')
    return data


def jal_return(e: DiscEE, routine: int, size: int, callee: int) -> int:
    """The return address of the one `jal callee` inside routine (read from
    the executed ELF, so the stop point is the original call site)."""
    word = (3 << 26) | (callee >> 2)
    sites = [a for a in range(routine, routine + size, 4) if e.u32(a) == word]
    if len(sites) != 1:
        raise SystemExit(f'{routine:#x}: {len(sites)} calls of {callee:#x}')
    return sites[0] + 8


def first_frame(disc: G.Disc, elf: bytes, extract: Path = G.EXTRACT,
                watch_fields: bool = False, snapshots: bool = False) -> DiscState:
    """Steps 1..7 of the module docstring. watch_fields records, for every
    store to a node's +0x00, +0x08 or +0x30 during the first walk, the
    behaviour running at the time (`field_writers[(node, offset)]`);
    snapshots keeps the EE image before step 6 and before step 7
    (`snapshots['before_first_frame' | 'before_second_tick']`, for the
    test's controls)."""
    if hashlib.sha256(elf).hexdigest() != G.ELF_SHA256:
        raise SystemExit('not the pinned SCUS-97112 boot ELF')
    overlay = overlay_bytes(extract)
    table = G.ResourceTable(disc)
    mem = G.first_level_memory(elf, table)
    mem[OVERLAY_ARENA:OVERLAY_ARENA + OVERLAY_SIZE] = overlay
    e = DiscEE(elf, mem)
    state = DiscState(mem=b'', spad=b'', elf_sha256=G.ELF_SHA256,
                      overlay_sha256=hashlib.sha256(overlay).hexdigest())

    def step(label, entry, args=()):
        e.call(entry, list(args))
        state.steps.append(label)

    # 1. the boot's render builder
    step('001D0F20 boot render builder', BOOT_RENDER)
    # 2. the game task in slot 0, the slot the dispatcher hands it
    step('001AB740(0, 001ACEC0)', TASK_INSTALL, [0, GAME_TASK])
    e.save(TASK_POINTER, TASK_TABLE)
    slot = TASK_TABLE
    # 3. New Game
    step('001AD230 (001AF2C0)', NEW_GAME_RESET)
    e.mem[slot + 0xA] = 4
    step('001AD360 step 4', NEW_GAME_AREA)
    if tuple(e.mem[0x810700:0x810703]) != AREA:
        raise SystemExit(f'001AD360 left the area bytes {e.mem[0x810700:0x810703].hex()}')
    # 4. the area load's overlay and its init
    step('008237C0 overlay init', OVERLAY_INIT)
    # 5. the frame machine's state 0
    e.mem[slot + 0xA] = 0
    e.mem[slot + 0xB] = 0
    step('0x1AE040 state 0', FRAME_MACHINE)
    if e.mem[slot + 0xB] != 1:
        raise SystemExit('0x1AE040 state 0 did not advance to state 1')
    # 6. the first world frame through the actor walk
    buffer_index = struct.unpack_from('<h', e.mem, BUFFER_INDEX)[0]
    if snapshots:
        state.snapshots['before_first_frame'] = bytes(e.mem)
    step(f'001D1AE0({buffer_index})', STEP_B, [buffer_index])
    stop_at = jal_return(e, WORLD_FRAME, WORLD_FRAME_SIZE, WALK)

    def stop(_ee):
        raise Stop()

    def held(label):
        def hook(ee):
            state.boundaries.append(label)
            ee.r[2] = 0
        return hook
    e.hooks[stop_at] = stop
    e.hooks[FLAME] = held('008235F0 first tick (flame, place 7): VU0 VMINI not in the measured model')
    if watch_fields:
        _watch(e, state)
    try:
        e.call(FRAME_MACHINE)
        raise SystemExit('0x1AE040 state 1 returned before the actor walk ended')
    except Stop:
        pass
    finally:
        del e.hooks[stop_at]
        del e.hooks[FLAME]
        e.cstack.clear()
        if watch_fields:
            del e.save
    state.steps.append('0x1AE040 state 1 (001AE5E0) through 001AFD70(0)')
    nodes = e.nodes()
    state.first_tick_nodes = nodes
    if snapshots:
        state.snapshots['before_second_tick'] = bytes(e.mem)
    # 7. the state-3 nodes' own next tick
    terminal = [n for n in nodes if e.mem[n + 4] == 3]
    term_cb = {e.u32(n + 16) for n in terminal}
    live_cb = {e.u32(n + 16) for n in nodes if e.mem[n + 4] != 3}
    if term_cb & live_cb:
        raise SystemExit(f'state-3 behaviours shared with live nodes: {sorted(map(hex, term_cb & live_cb))}')
    if terminal:
        saved = dict(e.hooks)
        for cb in live_cb:
            e.hooks[cb] = lambda ee: ee.r.__setitem__(2, 0)
        e.call(WALK, [0])
        e.hooks = saved
        state.steps.append('001AFD70(0): the state-3 nodes only')
        state.boundaries.append('second walk: every behaviour but the state-3 nodes held')
        left = set(e.nodes())
        if any(n in left for n in terminal):
            raise SystemExit('a state-3 node survived its own tick')
        state.freed = terminal
    state.mem, state.spad = bytes(e.mem), bytes(e.spad)
    state.kicks, state.hardware = e.kicks, sorted(set(e.hardware))
    return state


def _watch(e: DiscEE, state: DiscState):
    """Record which behaviour stores each node's +0x00 / +0x08 / +0x30."""
    plain = e.save
    pool_lo, pool_hi = 0x7A5640, 0x7A5640 + 0x100 * 0x2F0

    def save(address, value, size=4):
        a = address & 0x1FFFFFFF
        if pool_lo <= a < pool_hi:
            node = pool_lo + (a - pool_lo) // 0x2F0 * 0x2F0
            off = a - node
            for f, n in ((0, 1), (8, 1), (0x30, 4)):
                if off <= f < off + size or f <= off < f + n:
                    walker = [t for t, _ in e.cstack]
                    behaviour = next((walker[i + 1] for i, t in enumerate(walker[:-1]) if t == WALK), None)
                    state.field_writers.setdefault((node, f), []).append(behaviour)
        plain(address, value, size)
    e.save = save


def main(argv=None) -> int:
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--iso', type=Path)
    ap.add_argument('--disc', type=Path)
    ap.add_argument('--extract', type=Path, default=G.EXTRACT)
    ap.add_argument('--out', type=Path, default=ROOT / 'build/disc_state/first_frame_ee.bin',
                    help='write the 32 MB EE image here (ignored build/)')
    args = ap.parse_args(argv)
    elf = G.ELF_PATH.read_bytes()
    s = first_frame(G.Disc(args.iso, args.disc), elf, args.extract)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(s.image())
    for label in s.steps:
        print('executed:', label)
    for label in s.boundaries:
        print('boundary:', label)
    print(f'{len(s.kicks)} DMA kicks, {len(s.first_tick_nodes)} nodes after the first tick, '
          f'{len(s.freed)} freed by their own tick; image {args.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
