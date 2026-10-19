"""Original page-program replay for later-level texture discovery.

This exporter-only Page extends the existing strict VIF parser with the
001E9280 surface packet. It executes the packet's original VU instructions;
no native VU implementation or shared smoke model is changed. The parser
and MSCAL method retain chain_page_model's checks, with one extra accepted
MPG source and TOPS-relative batch program.
"""
import struct
import chain_page_model as M
from chain_page_model import (
    u32, fail, gs_feed, AGAIN_FORGETS,
    FLOOR_MPG, RIPPLE_MPG, LANE_MPG, SPRITE_MPG, SNOW_MPG, STREAK_MPG,
    KIND2_MPG, KIND6_MPG, PROGRAM_FLOOR, PROGRAM_RIPPLE, PROGRAM_LANE,
    PROGRAM_SPRITE, PROGRAM_SNOW, PROGRAM_STREAK, PROGRAM_KIND2, PROGRAM_KIND6,
)

# func_001E9280.c hands 001CB760 this packet, with six successive
# TOPS-relative strips: one initial call followed by five continuations.
# The upload identity is checked against the user's pinned boot ELF.
PROGRAM_SURFACE = 0x234FE0
SURFACE_MPG = (0x235010, 142, 0)
BATCHED = M.BATCHED + (PROGRAM_SURFACE,)


class Page(M.Page):
    def run(self, start):
        stream = self.dma(start)
        # one flat VIF stream with each word's source address (and whether
        # it was transferred inside the pass)
        words, wpass = [], []
        for e, (src, data) in enumerate(stream):
            if src is None or src in ('PASS', 'AGAIN'):     # a unit's CALL, a pass mark
                words.append((src, data))
                wpass.append(src is not None)
                continue
            for i in range(0, len(data), 4):
                words.append((src + i, u32(data, i)))
                wpass.append(e in self.pass_entries)
        i, n = 0, len(words)
        while i < n:
            at, v = words[i]
            i += 1
            if at == 'PASS':                                # the pass starts: its own environment
                self.gs.env = {}
                self.pass_first = len(self.gs.prims)
                continue
            if at == 'AGAIN':                               # the frame's draw environment again
                if len(self.again) >= 16:
                    fail('more than 16 environment-again marks in the pass')
                self.again.append(len(self.gs.prims))
                self.gs.env = {}
                for k in AGAIN_FORGETS:
                    self.gs.state.pop(k, None)
                continue
            self.gs.pass_regs = wpass[i - 1]
            if at is None:
                tex0, prim = self.units[v]
                self.gs.state.update({'TEX0_1': tex0, 'CLAMP_1': 0, 'TEX1_1': 0x60, 'ALPHA_1': 0x8000000068,
                                      'TEST_1': 0x53001, 'COLCLAMP': 1})
                self.gs.prim, self.gs.queue = prim, []
                self.cycle_set, self.cl, self.wl = True, 4, 4
                self.program, self.parts = None, 0
                continue
            cmd, num, imm = v >> 24 & 0x7F, v >> 16 & 0xFF, v & 0xFFFF
            if v >> 31:
                fail(f'VIF code {v:#010x} at {at:#x}: interrupt bit')
            if cmd >= 0x60:
                vn, vl, cnt = cmd >> 2 & 3, cmd & 3, num or 256
                # FLG (TOPS-relative) occurs only in the established surface batches.
                if cmd & 0x10 or vl != 0 or vn not in (0, 3) or imm & 0x4000 or \
                        (imm & 0x8000 and self.program not in BATCHED):
                    fail(f'VIF UNPACK {v:#010x} at {at:#x} (only V4-32 / V1-32 without mask, USN, '
                         'and FLG only for the established surface programs)')
                if not self.cycle_set:
                    # The cycle is the frame's (the lane program packet's
                    # STCYCL sits in its DMA tag, which is not transferred):
                    # taken as CL == WL, the only setting under which the
                    # program finds its constant rows 0..13 in one block.
                    self.cycle_inherited += 1
                elif self.wl == 0 or self.wl > self.cl:
                    fail(f'VIF UNPACK at {at:#x} with the cycle {self.cl}/{self.wl}')
                comps = vn + 1
                if i + comps * cnt > n:
                    fail('VIF UNPACK runs past the stream')
                dst = (imm & 0x3FF) + (self.tops if imm & 0x8000 else 0)
                for k in range(cnt):
                    d = ((dst + (k // self.wl) * self.cl + k % self.wl) if self.cycle_set else dst + k) & 1023
                    q = list(struct.unpack_from('<4I', self.vu.mem, 16 * d))
                    for c in range(4):
                        q[c] = words[i + comps * k + (c if comps == 4 else 0)][1]
                    struct.pack_into('<4I', self.vu.mem, 16 * d, *q)
                    self.known[d] = 1
                i += comps * cnt
                continue
            if cmd == 0x4A:                                 # MPG
                cnt = num or 256
                if i + 2 * cnt > n:
                    fail('VIF MPG runs past the stream')
                code = b''.join(struct.pack('<I', words[i + k][1]) for k in range(2 * cnt))
                self.vu.code[imm * 8:imm * 8 + 8 * cnt] = code
                first = words[i][0]
                self.mpg.append((first, imm, cnt))
                if (first, cnt, imm) == SURFACE_MPG:
                    self.program, self.parts, self.floor_ran = PROGRAM_SURFACE, 1, False
                elif (first, cnt, imm) == FLOOR_MPG:
                    self.program, self.parts, self.floor_ran = PROGRAM_FLOOR, 1, False
                elif (first, cnt, imm) == RIPPLE_MPG:
                    self.program, self.parts, self.floor_ran = PROGRAM_RIPPLE, 1, False
                elif (first, cnt, imm) == LANE_MPG:
                    self.program, self.parts = PROGRAM_LANE, 1
                elif (first, cnt, imm) in (SPRITE_MPG[0], SNOW_MPG[0], STREAK_MPG[0], KIND2_MPG[0], KIND6_MPG[0]):
                    self.program, self.parts, self.first = None, 1, first
                elif (first, cnt, imm) == SPRITE_MPG[1] and self.parts == 1 and self.program is None \
                        and self.first == SPRITE_MPG[0][0]:
                    self.program, self.parts = PROGRAM_SPRITE, 2
                elif (first, cnt, imm) == SNOW_MPG[1] and self.parts == 1 and self.program is None \
                        and self.first == SNOW_MPG[0][0]:
                    self.program, self.parts = PROGRAM_SNOW, 2
                elif (first, cnt, imm) == STREAK_MPG[1] and self.parts == 1 and self.program is None \
                        and self.first == STREAK_MPG[0][0]:
                    self.program, self.parts = PROGRAM_STREAK, 2
                elif (first, cnt, imm) == KIND2_MPG[1] and self.parts == 1 and self.program is None \
                        and self.first == KIND2_MPG[0][0]:
                    self.program, self.parts = PROGRAM_KIND2, 2
                elif (first, cnt, imm) == KIND6_MPG[1] and self.parts == 1 and self.program is None \
                        and self.first == KIND6_MPG[0][0]:
                    self.program, self.parts = PROGRAM_KIND6, 2
                else:
                    fail(f'MPG of {cnt} instructions from {first:#x} to micro {imm:#x} (not a page program)')
                i += 2 * cnt
                continue
            if cmd == 0x50:                                 # DIRECT
                cnt = imm or 65536
                while i < n and words[i][0] & 0xF:
                    fail(f'VIF DIRECT at {at:#x} not followed by an aligned qword')
                data = b''.join(struct.pack('<I', words[i + k][1]) for k in range(4 * cnt))
                used = gs_feed(self.gs, data, f'DIRECT at {at:#x}')
                # the pass: PATH2 goes on with the next GIF tag until the
                # DIRECT's data is used up (001D6930's two packets)
                while self.gs.pass_regs and used < cnt:
                    used += gs_feed(self.gs, data[16 * used:], f'DIRECT at {at:#x}')
                if used != cnt:
                    fail(f'DIRECT at {at:#x}: {cnt} qwords, the GIF packet uses {used}')
                self.directs.append((words[i][0], data))
                i += 4 * cnt
                continue
            if cmd == 0x20:                                 # STMASK: one data word
                i += 1
                continue
            if cmd == 0x01:
                self.cl, self.wl, self.cycle_set = imm & 0xFF, imm >> 8 & 0xFF, True
                continue
            if cmd == 0x03:
                self.base = imm & 0x3FF
                continue
            if cmd == 0x02:
                self.offset, self.dbf, self.tops = imm & 0x3FF, 0, self.base
                continue
            if cmd == 0x05:
                if imm & 3:
                    fail(f'VIF STMOD {imm} (only 0 occurs)')
                continue
            if cmd == 0x14:                                 # MSCAL
                self.mscal(imm, at)
                continue
            if cmd == 0x17:                                 # MSCNT (the floor / ripple program)
                self.mscal(0, at, cont=True)
                continue
            if cmd in (0x00, 0x10, 0x11, 0x13):
                continue
            fail(f'VIF code {v:#010x} at {at:#x} (not in the page)')
        return self

    def mscal(self, imm, at, cont=False):
        if self.program is None or imm != 0:
            fail(f'MSCAL {imm:#x} at {at:#x}: no page program loaded, or not entry 0')
        if cont and not (self.program in BATCHED and self.floor_ran):
            fail(f'MSCNT at {at:#x}: not after an established surface batch')
        self.mscal_counts[self.program] = self.mscal_counts.get(self.program, 0) + 1
        top = self.tops
        self.dbf ^= 1
        self.tops = self.base + (self.offset if self.dbf else 0)
        vu = self.vu
        vu.kicks, vu.events, vu.top, vu.watch = [], [], top, set()
        vu.pending, vu.cycle, vu.q_ready, vu.p_ready = [], 0, 0, 0
        vu.ready = [[0] * 4 for _ in range(32)]
        vu.run(vu.resume if cont else imm * 8)
        if self.program in BATCHED:
            self.floor_ran = True
        for e in vu.events:
            if e[0] == 'kick':
                raw = e[3]
                self.kicks.append((self.program, e[1], raw))
                gs_feed(self.gs, raw, f'XGKICK {e[1]:#x} of {self.program:#x}')


def captured(capture):
    """All textured page primitives; owner units have separate model readers."""
    read = M.ram_reader(capture.ram)
    start = M.latest_start(capture.ram)
    probe = M.Page(read)
    probe.dma(start)
    skip = {a for _, tid, _, a in probe.transfers
            if tid == 5 and M.ARENA <= a < 0x800000}
    page = Page(read, skip_calls=skip)
    page.run(start)
    return page, skip
