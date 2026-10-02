#!/usr/bin/env python3
"""Check the AREA15 export (assets/area15/, docs/AREA15_ASSETS.md) against
the fourteenth level's captures, load each file through the port's own
loader, and run a control for every guard line of this lane's checks.

Targets (export_area15_common): AREA15 sub 0 (route_a19d/a19d_20_door50,
area bytes 0F 00 01) and sub 1 (route_a15/a15_01_door51, 0F 01 00), one
capture each (they are the whole set: default and EM_TEST_FULL=1 run the
same checks). Missing exports, ELF, ISO, overlay, captures or census print
SKIPPED and exit 0.

Per sub the checked tree is a view (symlinks): sub<s>/ from assets/area15/,
its tables.json as the side report, the four shared side files. On it run:

  AREA13 lane  test_area13_assets_reference.run_checks (imported UNCHANGED:
               load map + labels + relocations, level, collision, cells
               through the ORIGINAL 001A2370 / 0x219F50 / 0019C6F0, roster,
               spawn, doors, scripts, overlay data, messages, world models,
               sound, ctx through the ORIGINAL 001D8FD0 / 001D1C50) with
               export_area15_common's rules installed, its port loaders and
               its canary; four of its functions replaced by name:
               stale_problems (sub 0 relocates id 0x45; sub 1 keeps sub 0's
               value), level_problems (sub 0's dynamic list), census_problems
               (AREA15's call sites and owners) and canary_plants (AREA15's
               owners instead of the AREA13 / AREA19 creature and writer)
  lane         captures, cursors, the id-0x45 word, the split, the sub
               proof, the 001A2370 / 0019C6F0 rules (pinned per capture,
               read from the committed C and, for the 001A2370 matrix
               argument, from the original overlay's instruction words),
               the canary's owner plants, the a15 census delta
  controls     at least one changed input per guard line of the lane's
               checks and of the exporter rules, each required to report
               that guard's own message (or to be accepted where marked)
"""
from __future__ import annotations

import contextlib
import copy
import hashlib
import io
import json
import os
import re
import shutil
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area15_common as A15  # noqa: E402  (first: registers the AREA15 target)
import export_area15_level as LVL  # noqa: E402
import export_area15_split as SPLIT  # noqa: E402
import test_area13_assets_reference as T13  # noqa: E402

A13, C, LV, TB = A15.A13, A15.C, A15.LV, A15.TB
E1, E2, T01, T, L = T13.E1, T13.E2, T13.T01, T13.T, T13.L
FULL, MODE = T13.FULL, T13.MODE
FAILS, check = T13.FAILS, T13.check

OUT = Path(os.environ.get('EM_AREA15_ASSETS') or A15.OUT).resolve()
BUILD = C.ROOT / 'build/area15/assets/test'
CENSUS = C.DECOMP / 'build/s87/census/a15_delta.json'
SUBS = (0, 1)
CAPTURE = {s: A15.SUBS[s]['capture'] for s in SUBS}
AREA_BYTES = {0: (0x0F, 0, 1), 1: (0x0F, 1, 0)}
EXCLUDED = {0: [f'a19d_{k:02d}_{n}' for k, n in enumerate((
    'beastA', 'ceiling', 'lockB', 'beastB', 'alcove', 'valve37', 'g3', 'cage', 'west', 'westdeck', 'area82E050',
    'bar858', 'post', 'bar1195', 'roof', 'deckB', 'ladder694', 'seal_far', 'seal', 'flights'))],
    1: ['a15_00_door14']}
# SHA-256 of INDEX.IDX sector 19 (top + both nested blocks): a hash, not disc data
DESCRIPTOR_SHA256 = '6c61a60d9d7be56890d792d722238206f08e70a69d653b16aa19fe6908af6c5c'
# D_0028A73C / D_0028A740 / D_0028A598 / D_0028A59C / D_0028A5A0 / D_0028A5A8 (measured)
CURSORS = {0: (0x133C1C0, 0x1820440, 0x1B41440, 0x1820440, 0x184C440, 0x1B6EC40),
           1: (0x133C1C0, 0x18211C0, 0x1A4A9C0, 0x18211C0, 0x18AC9C0, 0x1A869C0)}
CURSOR_WORDS = (0x28A73C, 0x28A740, 0x28A598, 0x28A59C, 0x28A5A0, 0x28A5A8)
D_0028A5A4 = 0x28A5A4
GP = 0x27D370                                        # the boot ELF's $gp (crt0)
ROSTER_LIVE = {'a19d_20_door50': (18, 15), 'a15_01_door51': (13, 11)}
MODEL_OWNERS = {'a19d_20_door50': 17, 'a15_01_door51': 12}
BINDING = {0: {(1, 0): ('global', 0), (1, 1): ('global', 1), (1, 2): ('global', 2), (2, 0): ('area', 0)},
           1: {(1, 0): ('global', 0), (1, 1): ('global', 1), (1, 2): ('global', 2), (2, 0): ('area', 0),
               (4, 0): ('area', 1), (4, 1): ('area', 2)}}
REFUSED = {0: {(3, 0), (4, 0), (4, 1), (4, 2)}, 1: {(3, 0), (4, 2)}}
# Every jal / j / address word of 001A2370, 0x219F50 and 0019C6F0 in the ELF
# and the AREA15 module, by the function that holds it (runtime addresses)
SITES = {0x1A2370: {0x156620: (0x156EF0,), 0x219550: (0x219668,), 0x219F50: (0x21A104,),
                    0x825320: (0x8253F0,), 0x825430: (0x825C50,), 0x825D10: (0x826540,), 0x826600: (0x8267DC,)},
         0x219F50: {0x219870: (0x2198E4,)},
         0x19C6F0: {0x826600: (0x826688, 0x826818)}}
# sizes from the committed C headers (overlay) and the AREA13 checker (boot)
FUNC_SIZE = {**T13.FUNC_SIZE, 0x825320: 0x110, 0x825430: 0x8D4, 0x825D10: 0x8E4, 0x826600: 0x244}
OWNERS = {0: (0x825320, 0x825430, 0x825D10), 1: (0x826600,)}         # each sub's AREA15 owners
FLAG_CALLS = {'a19d_20_door50': [], 'a15_01_door51': [[0x22, 1]]}
# the hull proofs of every capture, by uid: the owner behaviour (or 0x219F50)
PROOF_OWNERS = {'a19d_20_door50': {0: 0x825320, 1: 0x825320, 2: 0x825320, 3: 0x825D10, 4: 0x825430, 14: 0x219550},
                'a15_01_door51': {5: 0x219F50, 6: 0x219F50, 7: 0x219F50, 8: 0x826600, 13: 0x219550,
                                  14: 0x219550, 15: 0x219550, 16: 0x219550, 17: 0x219550}}
# the argument each AREA15 001A2370 owner passes, as its committed C writes it
C_ARGS = {0x825320: ('func_overlay_AREA15_008252E0.c', 'func_001A2370(self, self + 0xD0)'),
          0x826600: ('func_overlay_AREA15_008265C0.c', 'func_001A2370(self, self + 0xD0)'),
          0x825430: ('func_overlay_AREA15_008253F0.c', 'func_001A2370(self, D_00275B40[0] + 0x90)'),
          0x825D10: ('func_overlay_AREA15_00825CD0.c', 'func_001A2370(self, D_00275B40[0] + 0x90)')}
CENSUS_BEATS = tuple(EXCLUDED[0] + [CAPTURE[0]] + EXCLUDED[1] + [CAPTURE[1]])
OTHER_OVERLAY = 0x10                                 # the only other overlay of the a15 delta: AREA19


def configure(s, view=None):
    """Point the AREA13 checker at AREA15 sub `s` (idempotent)."""
    A15.install(s)
    T13.A = view or BUILD / f'view{s}-{os.getpid()}'
    T13.BUILD = BUILD
    T13.CANARY = BUILD / f'canary-{os.getpid()}'
    T01.BUILD = BUILD
    T13.QUICK['area15'] = (CAPTURE[s],)
    T13.EXCLUDED['area15'] = EXCLUDED[s]
    T13.DESCRIPTOR_SHA256['area15'] = DESCRIPTOR_SHA256
    T13.GRID['area15'] = CURSORS[s][2]
    T13.ROSTER_LIVE.update(ROSTER_LIVE)
    T13.MODEL_OWNERS.update(MODEL_OWNERS)
    T13.BINDING['area15'] = BINDING[s]
    T13.REFUSED['area15'] = REFUSED[s]
    T13.SITES['area15'] = SITES
    T13.CALLBACKS['area15'] = {}
    T13.SUB1['area15'] = ()
    T13.census_problems = census_sites
    T13.canary_plants = canary_plants
    T13.stale_problems = stale_problems
    T13.level_problems = level_problems


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build_view(out, s, view):
    """A tree of symlinks in the AREA13 lane's layout for sub `s`."""
    if view.exists():
        shutil.rmtree(view)
    for p in sorted((out / f'sub{s}').rglob('*')):
        if p.is_file():
            d = view / p.relative_to(out)
            d.parent.mkdir(parents=True, exist_ok=True)
            d.symlink_to(p)
    for name in SPLIT.SIDE + ('loaded_sub_proof.json',):
        (view / name).symlink_to(out / name)
    (view / 'tables.json').symlink_to(out / f'sub{s}/tables.json')


# ---------------------------------------------------------------------------
# Replacements inside the AREA13 checker


def stale_problems(K, caps):
    """The D_0028A5A4 word. Sub 0: the nested list relocates id 0x45 and
    D_0028A5A4 is that address (export_area15_level.dynamic_list_problems).
    Sub 1: no list relocates id 0x45 and D_0028A5A4 holds sub 0's relocated
    address, in the capture and in the capture before the load (a15_00,
    AREA19 sub 1)."""
    out = []
    for cap in caps:
        lists = A13.relocation_lists(cap)
        where = A13.relocation_problems(cap)[1]
        nested45 = any(i == 0x45 for i, _o in lists['nested'])
        top45 = any(i == 0x45 for i, _o in lists['top'])
        if top45:
            out.append(f'{cap.name}: the top list relocates id 0x45')
        if K.t.sub == 0:
            out += LVL.dynamic_list_problems([cap], where if nested45 else {})
        else:
            if nested45:
                out.append(f'{cap.name}: the descriptor relocates id 0x45')
            if C.u32(cap.ram, D_0028A5A4) != SUB0_LIST:
                out.append(f'{cap.name}: D_0028A5A4 = {C.u32(cap.ram, D_0028A5A4):#x}, not sub 0\'s list '
                           f'{SUB0_LIST:#x}')
    if K.t.sub == 1:
        prev = E1.previous_word(K.t.name, D_0028A5A4)
        if prev != SUB0_LIST:
            out.append(f'{E1.PREVIOUS[K.t.name].name}: D_0028A5A4 = {prev if prev is None else hex(prev)}, '
                       f'not sub 0\'s list {SUB0_LIST:#x}')
    return out


SUB0_LIST = 0x1B18440                                # chunk19.n0 +0x2F8000 at D_0028A740 0x1820440


def level_problems(K, caps, sub):
    """Sub 1: the AREA13 checker's level_problems. Sub 0: the same checks
    with the dynamic list: dynamic_objects.emsc = *D_0028A5A4 over its
    extent (export_area01's level_bank_problems), every kernel-0x00237450
    kick inside one of its entries (check_kicks with the list)."""
    if K.t.sub != 0:
        return _ORIGINAL_LEVEL_PROBLEMS(K, caps, sub)
    out, el, image = [], K.el, K.image
    lvl = sub / 'level'
    bank_img = (lvl / 'static_bank.emsc').read_bytes()
    out += T13.bank_problems(bank_img, caps)
    base, _entry, length = struct.unpack_from('<3I', bank_img, 8)
    bank = bank_img[20:20 + length]
    dpath = lvl / 'dynamic_objects.emsc'
    if not dpath.exists():
        return out + [f'{K.t.name}: no dynamic-list file although D_0028A5A4 is the relocated id 0x45'], {}
    dyn_img = dpath.read_bytes()
    out += [p for p in T01.level_bank_problems(bank_img, dyn_img, caps) if 'static bank' not in p
            and 'D_0028A5A0' not in p]
    dbase, _e, dlen = struct.unpack_from('<3I', dyn_img, 8) if len(dyn_img) >= 20 else (0, 0, 0)
    for cap in caps:
        if dbase != C.u32(cap.ram, D_0028A5A4) or cap.ram[dbase:dbase + dlen] != dyn_img[20:20 + dlen]:
            out.append(f'{cap.name}: {K.t.name}: a dynamic-list file that is not this capture\'s *D_0028A5A4 '
                       f'(window {dbase:#x}+{dlen:#x})')
    read = lambda a, n: bank[a - base:a - base + n]
    kicks, states = [], {}
    try:
        objects = L.bank_objects(read, base)
    except (SystemExit, struct.error, ValueError) as error:
        return out + [f'level: {error}'], {}
    try:
        dyn = L.dynamic_entries(lambda a, n: dyn_img[20 + a - dbase:20 + a - dbase + n], dbase)
        kicks, states, _touched = L.check_kicks(caps, objects, dyn)
    except (SystemExit, struct.error, ValueError) as error:
        out.append(f'level: {error}')
        dyn = []
    prim = el.level_template_prim(el.BootElf(C.ELF_PATH))
    if kicks:
        out += T01.gs_state_problems(states, T01.level_gs_state(el, prim))

    class BankImage:
        def read(self, a, n):
            return read(a, n)

    if bank_img not in T13._ZONES:
        T13._ZONES[bank_img] = L.build_zones(el, BankImage(), objects, lambda a: image.locate(a)[4])
    zones = T13._ZONES[bank_img]
    try:
        gs_caps = T01.gs_captures(caps)
    except SystemExit as error:
        return out + [str(error)], {}
    files = sorted(str(x) for x in lvl.glob('*.emdl'))
    if len(files) != len(zones):
        out.append(f'{K.t.name}: {len(files)} zone files for {len(zones)} source files')
    pairs = T13.zone_pairs(files, zones, image)
    out += T13.zone_name_problems(K.t.name, pairs)
    for path, (label, (b, _ids, bad, _recs)) in pairs:
        if bad:
            out.append(f'{label}: records with a matrix slot')
        out += T01.zone_problems(el, f'{K.t.name}/{Path(path).name}', Path(path).read_bytes(), b, gs_caps, prim)
    out += T13.background_problems(K, caps, sub)
    return out, dict(objects=len(objects), zones=len(files), kicks=sum(k['level_kicks'] for k in kicks),
                     dynamic_kicks=sum(k['dynamic_kicks'] for k in kicks), dynamic_entries=len(dyn))


def census_sites(K, caps, placements=None, groups=None, sites=None, sizes=None):
    """The call-site census for AREA15: every jal / j / address word of
    001A2370, 0x219F50 and 0019C6F0 in the ELF and the module is a SITES
    entry inside its function; the owners are exactly the modelled ones
    (the pickup, the drum, 0x219870 / 0x219F50 and export_area15_common's
    AREA15 owners); each AREA15 owner's a1 is the one the original
    instructions build (ov_arg_problems); the other sub's AREA15 owners
    have no live node and no record in this sub's roster."""
    out = []
    sites = SITES if sites is None else sites
    sizes = FUNC_SIZE if sizes is None else sizes
    text = K.elf[C.ELF_OFFSET:C.ELF_OFFSET + C.ELF_FILESZ]
    for callee, owners in sites.items():
        got = []
        for word in ((3 << 26) | (callee >> 2), (2 << 26) | (callee >> 2), callee):
            got += T13.word_sites(text, C.ELF_VADDR, word) + T13.word_sites(K.ov, C.u32(K.ov, 8), word)
        if sorted(got) != sorted(a for ss in owners.values() for a in ss):
            out.append(f'{callee:#x} call sites {[hex(a) for a in sorted(got)]} != the census')
        for owner, ss in owners.items():
            if not all(owner <= a < owner + sizes[owner] for a in ss):
                out.append(f'{callee:#x}: a site of {owner:#x} lies outside it')
    modelled = {E1.PICKUP, E1.DRUM, E1.SCALED_CALLER, E1.SCALED_OWNER, A15.FLAG_OWNER, *A15.SELF_MATRIX_OWNERS,
                *A15.BONE0_OWNERS}
    owners = {o for owner in sites.values() for o in owner}
    if owners != modelled:
        out.append(f'owners {sorted(hex(o) for o in owners)} != the modelled {sorted(hex(o) for o in modelled)}')
    out += ov_arg_problems(K.ov, C.u32(K.ov, 8))
    for o in OWNERS[1 - K.t.sub]:
        for cap in caps:
            if any(C.u32(cap.ram, a + 0x10) == o for _s, a in T.pool_nodes(cap.ram)):
                out.append(f'{cap.name}: a live node of the other sub\'s owner {o:#x}')
        if placements is not None and (any(C.u32(r, 0x24) == o for r in placements) or
                                       any(C.u32(r, 0x28) == o for _a, recs in groups for r in recs)):
            out.append(f'the sub-{K.t.sub} roster names the other sub\'s owner {o:#x}')
    return out


def ov_arg_problems(ov, base):
    """The a1 each AREA15 001A2370 owner passes, read from the ORIGINAL
    instructions of the user's overlay (fields of the words, no text): at
    its one jal 001A2370, a SELF_MATRIX_OWNERS owner builds a1 as R + 0xD0
    in the word before the jal and copies the same R into a0 in the delay
    slot (a SPECIAL or MMI register copy); a BONE0_OWNERS owner builds a1 as R + 0x90 in the delay slot,
    where the word before the jal loads R from 0(R) and one of the two
    words before that loads R from the gp-relative word D_00275B40."""
    out = []
    jal = (3 << 26) | (0x1A2370 >> 2)
    # a word load of R from the gp-relative word D_00275B40 (R left open)
    gp_word = (0x23 << 26) | (28 << 21) | ((0x275B40 - GP) & 0xFFFF)
    word = lambda a: struct.unpack_from('<I', ov, a - base)[0]
    field = lambda w, lo: (w >> lo) & 31
    addiu = lambda w, imm: w >> 26 == 9 and field(w, 16) == 5 and w & 0xFFFF == imm
    for owner in (*A15.SELF_MATRIX_OWNERS, *A15.BONE0_OWNERS):
        sites = [a for a in T13.word_sites(ov, base, jal) if owner <= a < owner + FUNC_SIZE[owner]]
        if len(sites) != 1:
            out.append(f'{owner:#x}: {len(sites)} jal 001A2370 in the original, not 1')
            continue
        at = sites[0]
        before, slot = word(at - 4), word(at + 4)
        if owner in A15.SELF_MATRIX_OWNERS:
            if not addiu(before, 0xD0):
                out.append(f'{owner:#x}: the original\'s a1 at {at:#x} is not a register + 0xD0')
            elif field(slot, 21) != field(before, 21) or field(slot, 11) != 4 or slot >> 26 not in (0, 0x1C):
                out.append(f'{owner:#x}: the original\'s a0 at {at:#x} is not the register a1 is built from')
            continue
        if not addiu(slot, 0x90):
            out.append(f'{owner:#x}: the original\'s a1 at {at:#x} is not a register + 0x90')
            continue
        r = field(slot, 21)
        if before != (0x23 << 26) | (r << 21) | (r << 16):
            out.append(f'{owner:#x}: the original\'s a1 base at {at:#x} is not loaded through itself')
        elif not any(word(at - 4 * k) == gp_word | (r << 16) for k in (2, 3)):
            out.append(f'{owner:#x}: the original\'s a1 base at {at:#x} is not read from D_00275B40')
    return out


def canary_plants(K, cap, tree, tag):
    """RAM plants for the canary copy of `cap` -> (copy, {section: text}):
    the AREA13 checker's plants without its creature, 0x219870 and writer
    plants (no outdoor creature and no writer window in AREA15; 0x219870 is
    planted only where a node lives), and with a plant per live AREA15
    001A2370 owner (a byte of the matrix it passes) and, in sub 0, of the
    dynamic list."""
    t, ram = K.t, cap.ram
    saved = dict(creature=E1.CREATURES.get(t.name), writers=E2.WRITERS.get(t.name), node_of=T13.node_of)
    owner = next(a for _s, a in T.pool_nodes(ram) if C.u32(ram, a + 0x10) in OWNERS[t.sub])
    scaled = T13.node_of(ram, E1.SCALED_OWNER)
    # the original's creature plant flips *(node + 0x11C) + 0x90 + 0x33 of
    # the node node_of returns for CREATURES; point it at an AREA15 owner and
    # undo that byte below. Its writer plant needs a window: one is lent for
    # the call and its plant undone below.
    E1.CREATURES[t.name] = -1
    E2.WRITERS[t.name] = ((0x829000, 0x829004, 0),)
    T13.node_of = lambda r, behaviour, uid=None: (owner if behaviour == -1 else
                                                  (scaled or owner) if behaviour == E1.SCALED_OWNER
                                                  else saved['node_of'](r, behaviour, uid))
    try:
        fake, want = _ORIGINAL_CANARY_PLANTS(K, cap, tree, tag)
    finally:
        E1.CREATURES[t.name] = saved['creature']
        E2.WRITERS[t.name] = saved['writers']
        T13.node_of = saved['node_of']
    edited = bytearray(fake.ram)
    for at in (C.u32(ram, owner + 0x11C) + 0x90 + 0x33, 0x829001):
        if 0 <= at < len(ram):
            edited[at] = ram[at]
    want.pop('creature matrix')
    want.pop('writer window')
    if scaled is None:
        edited[owner + 0xD0 + 0x33] = ram[owner + 0xD0 + 0x33]
        want.pop('0x219870 node')
    PLANTED[t.sub] = {}
    for _s, a in T.pool_nodes(ram):
        b = C.u32(ram, a + 0x10)
        if b in OWNERS[t.sub] and ram[a + 4] == 1:
            PLANTED[t.sub][ram[a + 0x0F]] = b
            m = A15.owner_matrix(ram, a)
            edited[m + 0x33] ^= 0x04
            want[f'{b:#x} node {a:#x} matrix'] = f'{tag}: hull {ram[a + 0x0F]} differs and no owner derivation'
    if t.sub == 0:
        dyn = C.u32(ram, D_0028A5A4)
        edited[dyn + 0x10 + 0x40] ^= 1
        want['dynamic list'] = f'{tag}: dynamic list differs from RAM'
    fake.ram = bytes(edited)
    return fake, want


PLANTED = {}                                   # sub -> {hull: owner} planted by the last canary_plants


def plant_problems(s, planted):
    """The canary planted one matrix per live AREA15 owner hull: exactly the
    PROOF_OWNERS hulls of the sub's capture whose owner is an AREA15 owner."""
    want = {h: o for h, o in PROOF_OWNERS[CAPTURE[s]].items() if o in OWNERS[s]}
    hexed = lambda d: {h: hex(o) for h, o in sorted((d or {}).items())}
    return [] if planted == want else [f'sub {s}: canary owner plants {hexed(planted)} != {hexed(want)}']


_ORIGINAL_CANARY_PLANTS = T13.__dict__.get('_ORIGINAL_A15_CANARY_PLANTS') or T13.canary_plants
_ORIGINAL_LEVEL_PROBLEMS = T13.__dict__.get('_ORIGINAL_A15_LEVEL_PROBLEMS') or T13.level_problems
T13._ORIGINAL_A15_CANARY_PLANTS = _ORIGINAL_CANARY_PLANTS
T13._ORIGINAL_A15_LEVEL_PROBLEMS = _ORIGINAL_LEVEL_PROBLEMS


# ---------------------------------------------------------------------------
# This lane's checks


def capture_problems(s, every, tree, excluded=None, before=None):
    """The pinned capture, its area bytes, the excluded folders of its
    group, the capture before the load (AREA19, its module resident), the
    reports made over the capture, level.json's sub and previous capture."""
    out = []
    names = tuple(c.name for c in every)
    if names != (CAPTURE[s],):
        out.append(f'sub {s}: captures {names} != {(CAPTURE[s],)}')
    for cap in every:
        if tuple(cap.ram[0x810700:0x810703]) != AREA_BYTES[s]:
            out.append(f'{cap.name}: area bytes {tuple(cap.ram[0x810700:0x810703])} != {AREA_BYTES[s]}')
    excluded = sorted(n for n, _a in A13.excluded_captures(A15.TARGET)) if excluded is None else excluded
    if excluded != EXCLUDED[s]:
        out.append(f'sub {s}: excluded captures {excluded} != {EXCLUDED[s]}')
    if before is None:
        before = (A15.previous(s) / 'eeMemory.bin').read_bytes()
    if before[0x810700] != 0x13:
        out.append(f'sub {s}: the capture before the load is not in AREA19 (area byte {before[0x810700]:#x})')
    if before[C.OVERLAY_ARENA:C.OVERLAY_ARENA + 4] != b'MWo3':
        out.append(f'sub {s}: the capture before the load has no module header at {C.OVERLAY_ARENA:#x}')
    if C.u32(before, C.OVERLAY_ARENA + 4) != OTHER_OVERLAY:
        out.append(f'sub {s}: the capture before the load has overlay id {C.u32(before, C.OVERLAY_ARENA + 4):#x} '
                   f'resident, not AREA19\'s {OTHER_OVERLAY:#x}')
    try:
        level = json.loads((tree / f'sub{s}/level/level.json').read_text())
        reports = {
            'level.json': level['captures'],
            'cells.json': [r['capture'] for r in json.loads((tree / f'sub{s}/cells.json').read_text())['cells']
                           ['captures']],
            'tables.json': json.loads((tree / f'sub{s}/tables.json').read_text())['captures'],
            'banks.json': json.loads((tree / f'sub{s}/sfx/banks.json').read_text())['captures']}
        prev = (level.get('dynamic_list') or level.get('stale_d_0028a5a4'))['previous_capture']
        lsub = level['sub']
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        return out + [f'sub {s} reports: {error!r}']
    for name, got in reports.items():
        if tuple(got) != (CAPTURE[s],):
            out.append(f'sub {s}: {name} was made over {got}')
    if lsub != s:
        out.append(f'sub {s}: level.json sub {lsub}')
    if LV.PREVIOUS.get('area15') != A15.previous(s):
        out.append(f"sub {s}: export_area13_level.PREVIOUS['area15'] is {LV.PREVIOUS.get('area15')}")
    if prev != A15.SUBS[s]['previous']:
        out.append(f'sub {s}: level.json previous capture {prev} != {A15.SUBS[s]["previous"]}')
    return out


def cursor_problems(s, cap, other, before):
    """The six words pinned per sub; the top list's relocation words equal
    in both subs' captures (one top block); the top cursor the previous
    area's (AREA19's D_0028A73C in the capture before the load)."""
    out = []
    got = tuple(C.u32(cap.ram, a) for a in CURSOR_WORDS)
    for a, g, w in zip(CURSOR_WORDS, got, CURSORS[s]):
        if g != w:
            out.append(f'{cap.name}: D_{a:08X} = {g:#x}, not {w:#x}')
    for ident, _off in A13.relocation_lists(cap)['top']:
        at = A13.D_0028A490 + 4 * ident
        if C.u32(cap.ram, at) != C.u32(other.ram, at):
            out.append(f'{cap.name}: top-list D_0028A490[{ident:#x}] differs from {other.name}\'s')
    if C.u32(before, 0x28A73C) != got[0]:
        out.append(f'{cap.name}: D_0028A73C {got[0]:#x} is not the previous capture\'s {C.u32(before, 0x28A73C):#x}')
    return out


def split_problems(out_tree, record=None):
    """area15.json against assets/area15/: every file listed with its hash
    and size, nothing else; the side files once at the root; each sub's
    capture; sub1_same_as_sub0 recomputed."""
    out = []
    try:
        record = record or json.loads((out_tree / SPLIT.MANIFEST).read_text())
        files = record['files']
        caps = record['captures']
        same = record['sub1_same_as_sub0']
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        return [f'{SPLIT.MANIFEST}: {error!r}']
    if caps != {f'sub{s}': [CAPTURE[s]] for s in SUBS}:
        out.append(f'{SPLIT.MANIFEST} captures {caps}')
    present = {p.relative_to(out_tree).as_posix() for p in out_tree.rglob('*') if p.is_file()}
    if present != set(files) | {SPLIT.MANIFEST}:
        out.append(f'assets/area15 holds {sorted(present - set(files) - {SPLIT.MANIFEST})}, lacks '
                   f'{sorted(set(files) - present)}')
    for rel, row in files.items():
        p = out_tree / rel
        if p.exists() and (sha(p.read_bytes()) != row.get('sha256') or p.stat().st_size != row.get('bytes')):
            out.append(f'{rel}: hash')
    top = sorted(r for r in files if '/' not in r)
    if top != sorted(SPLIT.SIDE + ('loaded_sub_proof.json',)):
        out.append(f'root files {top}')
    for s in SUBS:
        if f'sub{s}/tables.json' not in files:
            out.append(f'sub{s}/tables.json not placed')
    want = sorted(r for r in files if r.startswith('sub1/') and files.get('sub0/' + r[5:], {}).get('sha256')
                  == files[r]['sha256'])
    if sorted(same) != want:
        out.append(f'sub1_same_as_sub0 {same} != {want}')
    return out


_SUB_PROOF = {}


def sub_proof_problems(caps, proof):
    out = []
    if sorted(proof) != sorted(c.name for c in caps):
        out.append(f'loaded_sub_proof.json captures {sorted(proof)}')
    for cap in caps:
        k = (cap.name, id(cap.ram), cap.ram[0x810701])
        if k not in _SUB_PROOF:
            _SUB_PROOF[k] = A13.loaded_sub_proof([cap])[cap.name]
        rows, sub = _SUB_PROOF[k], cap.ram[0x810701]
        if not all(rows[sub] * 100 < rows[x] for x in rows if x != sub):
            out.append(f'{cap.name}: its sub-{sub} map does not fit RAM far better ({rows})')
        if proof.get(cap.name) != {f'sub{x}': v for x, v in rows.items()}:
            out.append(f'{cap.name}: loaded_sub_proof.json {proof.get(cap.name)} != {rows}')
    return out


_ROWS = {}


def recording_verify_directory(original):
    """Wrap export_area13_level.verify_directory to keep each call's rows."""
    def wrapped(elf, disc, caps):
        rows, problems, calls = original(elf, disc, caps)
        _ROWS[tuple(c.name for c in caps)] = copy.deepcopy(rows)
        return rows, problems, calls
    return wrapped


def proof_owner(text):
    """The behaviour a cells.json proof names (0x219F50 for a scaled hull)."""
    m = re.fullmatch(r'001A2370\(node 0x[0-9a-f]+, behaviour (0x[0-9a-f]+)\)', text)
    if m:
        return int(m.group(1), 16)
    return 0x219F50 if re.fullmatch(r'0x219F50\(node 0x[0-9a-f]+\)', text) else None


def c_arg_problems(sources=None):
    """Each AREA15 001A2370 owner's committed C passes the argument
    export_area15_common models; [8]'s C calls 0019C6F0(0x22, 1) in its
    state 0 and (0x22, 0) in its states 2 / 3 and nowhere else."""
    out = []
    root = C.DECOMP / 'src/overlays/AREA15'
    strip = lambda text: re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)
    read = lambda n: strip(sources[n] if sources is not None else (root / n).read_text())
    for owner, (name, arg) in C_ARGS.items():
        text = read(name)
        calls = text.count('func_001A2370(') - 1                       # less the extern
        if calls != 1:
            out.append(f'{owner:#x}: {name} makes {calls} 001A2370 calls, not 1')
        if arg not in text:
            out.append(f'{owner:#x}: {name} does not pass {arg}')
        want = int(name[-10:-2], 16) + 0x40
        if want != owner:
            out.append(f'{owner:#x}: {name} runs at {want:#x}')
    text = read(C_ARGS[A15.FLAG_OWNER][0])
    cases = re.split(r'\n\s*case ', text)
    calls = {c.split(':', 1)[0].strip(): re.findall(r'func_0019C6F0\((0x[0-9A-Fa-f]+), (\d)\)', c) for c in cases[1:]}
    if calls != {'0': [('0x22', '1')], '1': [], '2': [], '3': [('0x22', '0')]}:
        out.append(f'[8] 0019C6F0 calls by case {calls} are not FLAG_STATES\' reading')
    return out


def rule_problems(caps, records, rows, sources=None):
    """Flag calls pinned per capture; cells.json proofs = the rows
    recomputed in this run; each hull's owner pinned; no orphan or
    underived hull; the C arguments."""
    out = []
    for cap in caps:
        try:
            calls = [list(c) for c in A15.flag_calls(cap.ram)]
        except ValueError as error:
            calls = repr(error)
        if calls != FLAG_CALLS.get(cap.name):
            out.append(f'{cap.name}: flag calls {calls} != {FLAG_CALLS.get(cap.name)}')
        if rows is None or cap.name not in rows:
            out.append(f'{cap.name}: no recomputed directory rows')
            continue
        stored = {r['capture']: r.get('proofs') for r in records[cap.name]['cells']['captures']}
        got = {str(u): p for u, p in rows[cap.name]['proofs'].items()}
        if stored != {cap.name: got}:
            out.append(f'{cap.name}: cells.json proofs differ from the recomputed directory')
        owners = {int(u): proof_owner(p) for u, p in got.items()}
        if owners != PROOF_OWNERS.get(cap.name):
            out.append(f'{cap.name}: hull owners {owners} != {PROOF_OWNERS.get(cap.name)}')
        if any(p.startswith('orphan') or p == 'underived' for p in got.values()):
            out.append(f'{cap.name}: an orphan or underived hull')
    return out + c_arg_problems(sources)


def pin_problems(s, read=None):
    """export_area15_common's per-sub pins against the original's table
    walks over the pinned ELF and overlay (walk_roster, spawn_rows,
    D_0024E140[0x0F]) and against what install(s) put into
    export_area13_tables."""
    out = []
    read = read or T01.disc_reader()
    v = A15.SUBS[s]
    groups, paddr, _places = E2.R.walk_roster(read, A15.TARGET.area, s)
    if paddr != v['placements']:
        out.append(f'sub {s}: placement pin {v["placements"]:#x} != walk_roster\'s {paddr:#x}')
    if tuple((a, len(r)) for a, r in groups) != v['groups']:
        out.append(f'sub {s}: group pin {v["groups"]} != walk_roster\'s')
    _table, entries, count = E2.spawn_rows(read)
    if (entries, count) != v['spawn']:
        out.append(f'sub {s}: spawn pin ({v["spawn"][0]:#x}, {v["spawn"][1]}) != spawn_rows\' ({entries:#x}, {count})')
    if C.u32(read(0x24E140 + 4 * A15.TARGET.area, 4), 0) != A15.DOOR_ROW:
        out.append(f'door row pin {A15.DOOR_ROW:#x} != D_0024E140[0x0F]')
    installed = (E2.PLACEMENTS.get('area15'), E2.GROUPS.get('area15'), E2.SPAWN_SUB0.get('area15'),
                 E2.DOOR_ROW.get('area15'))
    if installed != (v['placements'], v['groups'], v['spawn'], A15.DOOR_ROW):
        out.append(f'sub {s}: export_area13_tables holds {installed}, not the sub-{s} pins')
    if E2.EXPLICIT_MODELS or 0x823580 in E2.DOOR_BEHAVIOURS:
        out.append('export_area13_tables keeps another module\'s EXPLICIT_MODELS / door 0x823580')
    return out


def census_problems(census):
    out = []
    try:
        funcs = {int(f['addr'], 16): f for f in census['functions']}
        beats = [b['beat'] for b in census['per_beat']]
        s = census['summary']
        if s.get('passes') != ['A19D', 'A15'] or tuple(beats) != CENSUS_BEATS:
            out.append('census: not the passes A19D / A15 over the 23 beats')
        runs = s.get('replay_runs', {})
        if s.get('beats_missing') != [] or s.get('beats_incomplete') != [] or sorted(runs) != sorted(beats) or \
                not all(r.get('completed') and r.get('error') is None for r in runs.values()):
            out.append('census: a beat missing, incomplete or not replayed to completion')
        b = lambda a: funcs.get(a, {}).get('beats') or []
        if not {CAPTURE[0], CAPTURE[1]} <= set(b(0x1FFCD0)):
            out.append(f'census: 001FFCD0 did not run in both AREA15 loads ({b(0x1FFCD0)})')
        for o in OWNERS[0]:
            if CAPTURE[0] not in b(o) or CAPTURE[1] in b(o):
                out.append(f'census: the sub-0 owner {o:#x} ran in {b(o)}')
        for o in OWNERS[1]:
            if b(o) != [CAPTURE[1]]:
                out.append(f'census: the sub-1 owner {o:#x} ran in {b(o)}')
        if census['unattributed_hits']:
            out.append('census: unattributed hits')
        if any(h.get('overlay_id') != OTHER_OVERLAY for h in census['overlay_hits_other_overlay']):
            out.append('census: an other-overlay hit outside AREA19')
    except (KeyError, TypeError, AttributeError, ValueError) as error:
        out.append(f'census: {error!r}')
    return out


def lane_checks(caps, every, out_tree, befores, census, rows):
    """Every lane check over both subs. `caps` / `every`: {sub: [Capture]}."""
    out = []
    for s in SUBS:
        configure(s, T13.A)
        out += capture_problems(s, every[s], out_tree, before=befores[s])
        out += cursor_problems(s, caps[s][0], caps[1 - s][0], befores[s])
        out += [f'sub {s}: {p}' for p in stale_problems(SimpleK(s), caps[s])]
        out += pin_problems(s)
    out += split_problems(out_tree)
    try:
        proof = json.loads((out_tree / 'loaded_sub_proof.json').read_text())
        records = {CAPTURE[s]: json.loads((out_tree / f'sub{s}/cells.json').read_text()) for s in SUBS}
    except (OSError, ValueError) as error:
        return out + [f'lane inputs: {error!r}']
    out += sub_proof_problems([caps[s][0] for s in SUBS], proof)
    out += rule_problems([caps[s][0] for s in SUBS], records, rows)
    out += census_problems(census)
    return out


class SimpleK:
    def __init__(self, s):
        self.t = A15.TARGET
        A15.install(s)


# ---------------------------------------------------------------------------
# Controls


class Expect:
    """A control: `problems` must contain one with `want` (the guard's own
    message), or be empty when accept=True."""

    def __init__(self):
        self.count = 0
        self.names = []

    def __call__(self, problems, what, want=None, accept=False):
        self.count += 1
        self.names.append(what)
        if accept:
            check(not problems, f'control: {what} rejected ({problems[:2]})')
        else:
            check(any(want in p for p in problems) if want else bool(problems),
                  f'control: {what} missed (want "{want}", got {problems[:3]})')


def light_copy(src, dst):
    """A copy of the tree `src` at `dst`: files up to 64 KB copied, larger
    ones symlinked to the source (a control that changes a large file
    replaces its link first: replace_file)."""
    if dst.exists():
        shutil.rmtree(dst)

    def copy_or_link(a, b):
        if os.path.getsize(a) <= 0x10000:
            shutil.copyfile(a, b)
        else:
            os.symlink(os.path.realpath(a), b)
    shutil.copytree(src, dst, copy_function=copy_or_link)
    return dst


def replace_file(path, data):
    """Write `data` at `path` without writing through a symlink."""
    path = Path(path)
    if path.is_symlink() or path.exists():
        path.unlink()
    path.write_bytes(data)


def raised(fn, *args):
    """[message] of a ValueError / SystemExit fn raises, else []."""
    try:
        fn(*args)
    except (ValueError, SystemExit) as error:
        return [str(error)]
    return []


def lane_controls(caps, every, befores, census, rows, expect):
    edited = lambda cap, edits: T13.ram_copy(cap, cap.name, edits)
    zero, one = caps[0][0], caps[1][0]
    tmp = BUILD / f'ctl-{os.getpid()}'
    n0 = expect.count

    def tampered(change):
        light_copy(OUT, tmp)
        change(tmp)
        return tmp

    def edit_json(rel, fn, manifest=True):
        def change(tree):
            p = tree / rel
            d = json.loads(p.read_text())
            fn(d)
            replace_file(p, json.dumps(d).encode())
            if manifest:
                rec = json.loads((tree / SPLIT.MANIFEST).read_text())
                rec['files'][rel] = dict(sha256=sha(p.read_bytes()), bytes=p.stat().st_size)
                replace_file(tree / SPLIT.MANIFEST, json.dumps(rec).encode())
        return change

    try:
        # -- plant_problems (the canary's owner plants, recorded by canary_plants)
        for s in SUBS:
            expect(plant_problems(s, PLANTED.get(s)), f'sub {s}: the canary\'s owner plants as made', accept=True)
            fewer = dict(PLANTED.get(s) or {})
            fewer.pop(max(fewer), None)
            expect(plant_problems(s, fewer), f'sub {s}: a canary owner plant dropped', want='canary owner plants')
        # -- capture_problems: one control per guard
        for s in SUBS:
            configure(s, T13.A)
            cp = lambda ev, tree=OUT, ex=None, bf=befores[s]: capture_problems(s, ev, tree, ex, bf)
            expect(cp(every[s]), f'sub {s}: captures as recorded', accept=True)
            expect(cp([]), f'sub {s}: the capture missing', want=f'sub {s}: captures ()')
            expect(cp([edited(every[s][0], [T13.flip(every[s][0].ram, 0x810702)])]),
                   f'sub {s}: the entry byte changed', want='area bytes')
            expect(cp(every[s], ex=EXCLUDED[s][:-1]), f'sub {s}: an excluded folder missing',
                   want='excluded captures')
            bad = bytearray(befores[s])
            bad[0x810700] = 0x0F
            expect(cp(every[s], bf=bytes(bad)), f'sub {s}: the capture before the load read as AREA15',
                   want='is not in AREA19')
            bad = bytearray(befores[s])
            bad[C.OVERLAY_ARENA] ^= 0x01
            expect(cp(every[s], bf=bytes(bad)), f'sub {s}: the capture before the load without MWo3',
                   want='has no module header')
            bad = bytearray(befores[s])
            struct.pack_into('<I', bad, C.OVERLAY_ARENA + 4, 0x0F)
            expect(cp(every[s], bf=bytes(bad)), f'sub {s}: the capture before the load with overlay id 0x0F',
                   want='not AREA19\'s 0x10')
            for rel, path in ((f'sub{s}/level/level.json', ('captures',)),
                              (f'sub{s}/cells.json', ('cells', 'captures')),
                              (f'sub{s}/tables.json', ('captures',)), (f'sub{s}/sfx/banks.json', ('captures',))):
                def cut(d, path=path):
                    for k in path[:-1]:
                        d = d[k]
                    d[path[-1]] = d[path[-1]][:-1]
                expect(cp(every[s], tampered(edit_json(rel, cut))), f'sub {s}: {rel} made over no capture',
                       want=f'{Path(rel).name} was made over')
            expect(cp(every[s], tampered(edit_json(f'sub{s}/level/level.json', lambda d: d.__setitem__('sub', 1 - s)))),
                   f'sub {s}: level.json naming the other sub', want='level.json sub')
            key = 'dynamic_list' if s == 0 else 'stale_d_0028a5a4'
            expect(cp(every[s], tampered(edit_json(f'sub{s}/level/level.json', lambda d: d[key].__setitem__(
                'previous_capture', 'a19d_18_seal')))), f'sub {s}: level.json naming another previous capture',
                want='level.json previous capture')
            expect(cp(every[s], tampered(edit_json(f'sub{s}/level/level.json', lambda d: d.pop(key)))),
                   f'sub {s}: level.json without its D_0028A5A4 record', want='reports')
            saved = LV.PREVIOUS['area15']
            LV.PREVIOUS['area15'] = A15.previous(1 - s)
            try:
                expect(cp(every[s]), f"sub {s}: PREVIOUS['area15'] not re-pointed", want="PREVIOUS['area15']")
            finally:
                LV.PREVIOUS['area15'] = saved
        # -- cursor_problems
        for s in SUBS:
            cap, other = caps[s][0], caps[1 - s][0]
            expect(cursor_problems(s, cap, other, befores[s]), f'sub {s}: cursors as captured', accept=True)
            for a in CURSOR_WORDS:
                expect(cursor_problems(s, edited(cap, [(a, struct.pack('<I', C.u32(cap.ram, a) + 0x10))]), other,
                                       befores[s]), f'sub {s}: D_{a:08X} + 0x10', want=f'D_{a:08X} = ')
            ident = A13.relocation_lists(cap)['top'][-1][0]
            at = A13.D_0028A490 + 4 * ident
            expect(cursor_problems(s, cap, edited(other, [(at, struct.pack('<I', C.u32(other.ram, at) + 0x800))]),
                                   befores[s]), f'sub {s}: the other sub\'s last top-list word moved',
                   want='top-list D_0028A490')
            bad = bytearray(befores[s])
            struct.pack_into('<I', bad, 0x28A73C, C.u32(befores[s], 0x28A73C) + 0x80)
            expect(cursor_problems(s, cap, other, bytes(bad)), f'sub {s}: the previous capture\'s top cursor moved',
                   want='is not the previous capture')
        # -- stale_problems / dynamic_list_problems (the D_0028A5A4 word)
        configure(0, T13.A)
        K0 = SimpleK(0)
        expect(stale_problems(K0, [zero]), 'sub 0: D_0028A5A4 as captured', accept=True)
        expect(stale_problems(K0, [edited(zero, [T13.flip(zero.ram, D_0028A5A4 + 1)])]),
               'sub 0: D_0028A5A4 not the relocated id 0x45', want='is not the relocated id 0x45')
        where = A13.relocation_problems(zero)[1]
        expect(LVL.dynamic_list_problems([zero], {k: v for k, v in where.items() if k != 0x45}),
               'sub 0: a nested list without id 0x45', want='relocates no id 0x45')
        expect(LVL.dynamic_list_problems([zero], where), 'sub 0: the id-0x45 rule as captured', accept=True)
        saved_lists = A13.relocation_lists

        def with_top45(cap, check_iso=True):
            got = saved_lists(cap, check_iso)
            return dict(top=got['top'] + [(0x45, 0)], nested=got['nested'])
        A13.relocation_lists = with_top45
        try:
            expect(stale_problems(K0, [zero]), 'sub 0: a top list relocating id 0x45', want='the top list relocates')
        finally:
            A13.relocation_lists = saved_lists
        configure(1, T13.A)
        K1 = SimpleK(1)
        expect(stale_problems(K1, [one]), 'sub 1: D_0028A5A4 as captured', accept=True)
        expect(stale_problems(K1, [edited(one, [T13.flip(one.ram, D_0028A5A4 + 1)])]),
               'sub 1: D_0028A5A4 not sub 0\'s list', want="not sub 0's list")

        def with_nested45(cap, check_iso=True):
            got = saved_lists(cap, check_iso)
            return dict(top=got['top'], nested=got['nested'] + [(0x45, 0)])
        A13.relocation_lists = with_nested45
        try:
            expect(stale_problems(K1, [one]), 'sub 1: a nested list relocating id 0x45',
                   want='the descriptor relocates id 0x45')
        finally:
            A13.relocation_lists = saved_lists
        saved_prev = LV.PREVIOUS['area15']
        LV.PREVIOUS['area15'] = A15.previous(0)
        try:
            expect(stale_problems(K1, [one]), 'sub 1: the capture before the load read as a19d_19',
                   want="a19d_19_flights: D_0028A5A4 = ")
        finally:
            LV.PREVIOUS['area15'] = saved_prev
        # -- split_problems
        expect(split_problems(OUT), 'the placement as exported', accept=True)

        def flip_file(rel, at=0x40):
            def change(tree):
                p = tree / rel
                b = bytearray(p.read_bytes())
                b[at] ^= 1
                replace_file(p, bytes(b))
            return change
        expect(split_problems(tampered(lambda t: (t / SPLIT.MANIFEST).unlink())), 'the manifest missing',
               want=f'{SPLIT.MANIFEST}: ')
        expect(split_problems(tampered(edit_json(SPLIT.MANIFEST, lambda d: d['captures'].__setitem__(
            'sub0', ['a19d_19_flights']), manifest=False))), 'the manifest naming another capture',
            want=f'{SPLIT.MANIFEST} captures')
        expect(split_problems(tampered(lambda t: (t / 'extra.bin').write_bytes(b'x'))), 'an extra file',
               want="holds ['extra.bin']")
        expect(split_problems(tampered(lambda t: (t / 'sub1/world_models.emwm').unlink())), 'a sub-1 file missing',
               want="lacks ['sub1/world_models.emwm']")
        expect(split_problems(tampered(flip_file('sub0/level/static_bank.emsc'))), 'a static-bank byte',
               want='sub0/level/static_bank.emsc: hash')

        def side_in_sub(tree):
            rec = json.loads((tree / SPLIT.MANIFEST).read_text())
            shutil.move(tree / 'scripts.emsc', tree / 'sub0/scripts.emsc')
            rec['files']['sub0/scripts.emsc'] = rec['files'].pop('scripts.emsc')
            replace_file(tree / SPLIT.MANIFEST, json.dumps(rec).encode())
        expect(split_problems(tampered(side_in_sub)), 'a side file placed in sub0/ (listed there)',
               want='root files')

        def tables_dropped(tree):
            rec = json.loads((tree / SPLIT.MANIFEST).read_text())
            rec['files'].pop('sub1/tables.json')
            (tree / 'sub1/tables.json').unlink()
            replace_file(tree / SPLIT.MANIFEST, json.dumps(rec).encode())
        expect(split_problems(tampered(tables_dropped)), 'sub1/tables.json not placed (and not listed)',
               want='sub1/tables.json not placed')
        expect(split_problems(tampered(edit_json(SPLIT.MANIFEST, lambda d: d['sub1_same_as_sub0'].pop(),
                                                 manifest=False))), 'sub1_same_as_sub0 short',
               want='sub1_same_as_sub0')
        # export_area15_split.tree_files (the exporter's guards)
        tree = A15.TREE
        if tree.exists():
            work = BUILD / f'tree-{os.getpid()}'

            def tree_case(change):
                light_copy(tree, work)
                change(work)
                return raised(SPLIT.tree_files, work)
            expect(tree_case(lambda w: None), 'tree_files on the export tree', accept=True)
            expect(tree_case(lambda w: (w / 'stray.bin').write_bytes(b'x')), 'tree_files: a stray file',
                   want='outside the parts')
            expect(tree_case(lambda w: (w / 'part1/scripts.emsc').unlink()), 'tree_files: a part\'s side file missing',
                   want="lacks ['part1/scripts.emsc']")
            expect(tree_case(lambda w: (w / 'loaded_sub_proof.json').unlink()), 'tree_files: the proof missing',
                   want="lacks ['loaded_sub_proof.json']")
            expect(tree_case(lambda w: shutil.rmtree(w / 'part0/sub0')), 'tree_files: sub0/ missing',
                   want="lacks ['part0/sub0/']")
            expect(tree_case(flip_file('part1/overlay_data.emsc', 0x20)), 'tree_files: the side files differ',
                   want="['overlay_data.emsc'] differ")
            shutil.rmtree(work)
        else:
            print('  tree_files controls: SKIPPED (no export tree)')
        # -- sub_proof_problems
        proof = json.loads((OUT / 'loaded_sub_proof.json').read_text())
        both = [zero, one]
        expect(sub_proof_problems(both, proof), 'the sub proof as exported', accept=True)
        expect(sub_proof_problems(both, {k: v for k, v in proof.items() if k != one.name}),
               'the sub proof without a15_01', want='loaded_sub_proof.json captures')
        bad = copy.deepcopy(proof)
        bad[zero.name]['sub1'] += 1
        expect(sub_proof_problems(both, bad), 'the sub proof a19d_20 sub1 + 1', want=f'{zero.name}: loaded_sub_proof')
        expect(sub_proof_problems([edited(one, [(0x810701, b'\x00')])], {one.name: proof[one.name]}),
               'a15_01 read as sub 0', want='does not fit RAM far better')
        # -- rule_problems
        records = {CAPTURE[s]: json.loads((OUT / f'sub{s}/cells.json').read_text()) for s in SUBS}
        expect(rule_problems(both, records, rows), 'the rules as exported', accept=True)
        eight = T13.node_of(one.ram, A15.FLAG_OWNER)
        expect(rule_problems([edited(one, [(eight + 4, b'\x00')])], records, rows), '[8] in state 0 (no call)',
               want='flag calls [] !=')
        expect(rule_problems(both, records, {k: v for k, v in rows.items() if k != zero.name}),
               'no recomputed rows for a19d_20', want=f'{zero.name}: no recomputed directory rows')
        rec2 = copy.deepcopy(records)
        rec2[zero.name]['cells']['captures'][0]['proofs']['3'] = rec2[zero.name]['cells']['captures'][0]['proofs']['4']
        expect(rule_problems(both, rec2, rows), 'cells.json naming hull 4\'s proof for hull 3',
               want='cells.json proofs differ')
        rows2 = copy.deepcopy(rows)
        rows2[one.name]['proofs'][8] = rows2[one.name]['proofs'][13]
        rec3 = copy.deepcopy(records)
        rec3[one.name]['cells']['captures'][0]['proofs']['8'] = rows2[one.name]['proofs'][8]
        expect(rule_problems(both, rec3, rows2), 'hull 8 owned by the pickup (record and rows)', want='hull owners')
        rows3, rec4 = copy.deepcopy(rows), copy.deepcopy(records)
        orphan = 'orphan: equal to a derivation of it in another capture'
        rows3[one.name]['proofs'][13] = orphan
        rec4[one.name]['cells']['captures'][0]['proofs']['13'] = orphan
        expect(rule_problems(both, rec4, rows3), 'hull 13 an orphan (record and rows)',
               want='an orphan or underived hull')
        bad_src = {n: (C.DECOMP / 'src/overlays/AREA15' / n).read_text() for n, _a in C_ARGS.values()}
        name0, arg0 = C_ARGS[0x825430]
        bad_src[name0] = bad_src[name0].replace(arg0, 'func_001A2370(self, self + 0xD0)')
        expect(rule_problems(both, records, rows, bad_src), 'rule_problems with 0x825430\'s C passing node + 0xD0',
               want='0x825430: func_overlay_AREA15_008253F0.c does not pass')
        # -- pin_problems
        for s in SUBS:
            configure(s, T13.A)
            expect(pin_problems(s), f'sub {s}: the pins as installed', accept=True)
            for key, val, want in (('placements', A15.SUBS[s]['placements'] + 0x28, 'placement pin'),
                                   ('groups', ((A15.SUBS[s]['groups'][0][0], 1),), 'group pin'),
                                   ('spawn', (A15.SUBS[s]['spawn'][0], A15.SUBS[s]['spawn'][1] + 1), 'spawn pin')):
                saved = A15.SUBS[s][key]
                A15.SUBS[s][key] = val
                try:
                    expect(pin_problems(s), f'sub {s}: the {key} pin changed (not installed)', want=want)
                finally:
                    A15.SUBS[s][key] = saved
            saved = E2.SPAWN_SUB0['area15']
            E2.SPAWN_SUB0['area15'] = A15.SUBS[1 - s]['spawn']
            try:
                expect(pin_problems(s), f'sub {s}: export_area13_tables holding the other sub\'s spawn pin',
                       want='export_area13_tables holds')
            finally:
                E2.SPAWN_SUB0['area15'] = saved
        saved_row = A15.DOOR_ROW
        A15.DOOR_ROW = saved_row + 4
        try:
            expect(pin_problems(1), 'the door row pin + 4', want='door row pin')
        finally:
            A15.DOOR_ROW = saved_row
        saved_doors = E2.DOOR_BEHAVIOURS
        E2.DOOR_BEHAVIOURS = saved_doors + (0x823580,)
        try:
            expect(pin_problems(1), 'the AREA13 / AREA19 overlay door 0x823580 left installed', want='door 0x823580')
        finally:
            E2.DOOR_BEHAVIOURS = saved_doors
        saved_em = E2.EXPLICIT_MODELS
        E2.EXPLICIT_MODELS = {0x826850: (2, 0xD)}
        try:
            expect(pin_problems(1), 'the AREA13 hatch rule left installed', want='EXPLICIT_MODELS')
        finally:
            E2.EXPLICIT_MODELS = saved_em
        configure(1, T13.A)
        hatch_like = T13.node_of(one.ram, 0x826850)
        got = E2.explicit_model_nodes(edited(one, [(hatch_like + 4, b'\x02')]).ram)
        expect([] if got == [] else [str(got)], 'sub 1: a 0x826850 node in state 2 is no AREA13 hatch', accept=True)
        # -- c_arg_problems
        root = C.DECOMP / 'src/overlays/AREA15'
        srcs = {n: (root / n).read_text() for n, _a in C_ARGS.values()}
        expect(c_arg_problems(srcs), 'the C arguments as committed', accept=True)
        for owner, (name, arg) in C_ARGS.items():
            s2 = dict(srcs)
            s2[name] = srcs[name].replace(arg, arg.replace('0x', '0x1'))
            expect(c_arg_problems(s2), f'{owner:#x}: its C passing another argument', want=f'{owner:#x}: {name} does not pass')
            s2 = dict(srcs)
            s2[name] = srcs[name] + '\nvoid extra_call(void *self) { func_001A2370(self, self + 0x100); }\n'
            expect(c_arg_problems(s2), f'{owner:#x}: its C with a second 001A2370 call',
                   want=f'{owner:#x}: {name} makes 2 001A2370 calls')
        s2 = dict(srcs)
        n8 = C_ARGS[A15.FLAG_OWNER][0]
        s2[n8] = srcs[n8].replace('func_0019C6F0(0x22, 0)', 'func_0019C6F0(0x22, 1)')
        expect(c_arg_problems(s2), '[8]\'s C calling (0x22, 1) in states 2 / 3', want='[8] 0019C6F0 calls by case')
        saved_args = dict(C_ARGS)
        C_ARGS[0x825330] = C_ARGS.pop(0x825320)
        try:
            expect(c_arg_problems(srcs), 'an owner address that is not its file\'s runtime address',
                   want='0x825330: func_overlay_AREA15_008252E0.c runs at 0x825320')
        finally:
            C_ARGS.clear()
            C_ARGS.update(saved_args)
        # -- the exporter rules: flag_calls
        configure(1, T13.A)
        expect(raised(A15.flag_calls, edited(one, [(eight + 4, b'\x02')]).ram), '[8] in state 2: refused',
               want='is not decided')
        expect(raised(A15.flag_calls, edited(one, [(eight + 4, b'\x03')]).ram), '[8] in state 3: refused',
               want='in state 3: its last 0019C6F0 call')
        free = next(x for x in range(T.POOL_SLOTS) if not one.ram[T.POOL_BASE + x * T.POOL_STRIDE])
        nf = T.POOL_BASE + free * T.POOL_STRIDE
        expect(raised(A15.flag_calls, edited(one, [(nf, one.ram[eight:eight + T.POOL_STRIDE])]).ram),
               'two [8] nodes: refused', want='2 live nodes')
        got = A15.flag_calls(edited(one, [(eight + 4, b'\x00')]).ram)
        expect([] if got == () else [str(got)], '[8] in state 0: no call', accept=True)
        got = A15.flag_calls(one.ram)
        expect([] if got == ((0x22, 1),) else [str(got)], '[8] in state 1: (0x22, 1)', accept=True)
        got = A15.flag_calls(edited(one, [(0x810700, b'\x13')]).ram)
        expect([] if got == () else [str(got)], 'a capture read as AREA19: the AREA13 lane\'s rule (no call)',
               accept=True)
        # owner_matrix
        configure(0, T13.A)
        swing = T13.node_of(zero.ram, 0x825430)
        rot = T13.node_of(zero.ram, 0x825320)
        pick = T13.node_of(zero.ram, E1.PICKUP)
        om = A15.owner_matrix
        expect([] if om(zero.ram, rot) == rot + 0xD0 else ['x'], 'owner_matrix 0x825320: node + 0xD0', accept=True)
        expect([] if om(zero.ram, swing) == C.u32(zero.ram, swing + 0x110) + 0x90 else ['x'],
               'owner_matrix 0x825430: *(node + 0x110) + 0x90', accept=True)
        expect([] if om(zero.ram, pick) == pick + 0xD0 else ['x'], 'owner_matrix of the pickup: the AREA13 lane\'s',
               accept=True)
        expect([] if om(edited(zero, [(0x810700, b'\x13')]).ram, rot) is None else ['a matrix'],
               'owner_matrix of 0x825320 outside AREA15: none', accept=True)
        expect([] if om(edited(zero, [(0x810700, b'\x13')]).ram, swing) is None else ['a matrix'],
               'owner_matrix of 0x825430 outside AREA15: none', accept=True)
        # -- census_problems
        expect(census_problems(census), 'the census as recorded', accept=True)

        def fn(addr):
            return lambda d: next(f for f in d['functions'] if f['addr'] == addr)
        edits = [
            ('a third pass', lambda d: d['summary']['passes'].append('A19C'), 'not the passes'),
            ('a beat dropped', lambda d: d['per_beat'].pop(), 'not the passes'),
            ('a beat missing', lambda d: d['summary']['beats_missing'].append(CAPTURE[1]), 'a beat missing'),
            ('a beat incomplete', lambda d: d['summary']['beats_incomplete'].append(CAPTURE[1]), 'a beat missing'),
            ('a replay error', lambda d: d['summary']['replay_runs'][CAPTURE[0]].__setitem__('error', 'x'),
             'a beat missing'),
            ('a replay not completed', lambda d: d['summary']['replay_runs'][CAPTURE[1]].__setitem__('completed', False),
             'a beat missing'),
            ('001FFCD0 not in a15_01', lambda d: fn('0x1ffcd0')(d)['beats'].remove(CAPTURE[1]), '001FFCD0'),
            ('0x825320 not in a19d_20', lambda d: fn('0x825320')(d)['beats'].remove(CAPTURE[0]), 'sub-0 owner 0x825320'),
            ('0x825d10 also in a15_01', lambda d: fn('0x825d10')(d)['beats'].append(CAPTURE[1]), 'sub-0 owner 0x825d10'),
            ('[8] also in a15_00', lambda d: fn('0x826600')(d)['beats'].insert(0, 'a15_00_door14'),
             'sub-1 owner 0x826600'),
            ('an unattributed hit', lambda d: d['unattributed_hits'].append('0x00500000'), 'unattributed hits'),
            ('an other-overlay hit of AREA13', lambda d: d['overlay_hits_other_overlay'].append(dict(overlay_id=10)),
             'outside AREA19'),
            ('no functions list', lambda d: d.pop('functions'), 'census: KeyError')]
        for what, f, want in edits:
            d = copy.deepcopy(census)
            f(d)
            expect(census_problems(d), f'census: {what}', want=want)
        # -- lane_checks itself
        expect(lane_checks(caps, every, OUT, befores, census, rows), 'lane_checks as exported', accept=True)
        hit = copy.deepcopy(census)
        hit['unattributed_hits'].append('0x00500000')
        expect(lane_checks(caps, every, OUT, befores, hit, rows), 'lane_checks with an unattributed census hit',
               want='unattributed hits')
        expect(lane_checks(caps, every, tampered(lambda t: (t / 'loaded_sub_proof.json').unlink()), befores, census,
                           rows), 'lane_checks without the proof', want='lane inputs')
        saved = A15.SUBS[0]['spawn']
        A15.SUBS[0]['spawn'] = (saved[0], saved[1] + 1)
        try:
            expect(lane_checks(caps, every, OUT, befores, census, rows), 'lane_checks with the sub-0 spawn pin + 1',
                   want='sub 0: spawn pin')
        finally:
            A15.SUBS[0]['spawn'] = saved
    finally:
        if tmp.exists():
            shutil.rmtree(tmp)
    return expect.count - n0


def census_site_controls(K, caps, expect):
    """census_sites (the replacement inside the AREA13 checker) on changed
    inputs, one per guard line."""
    n0 = expect.count
    s = K.t.sub
    expect(census_sites(K, caps), f'sub {s}: the call-site census as captured', accept=True)
    ov = bytearray(K.ov)
    at = 0x829000 - C.OVERLAY_ARENA
    ov[at:at + 4] = struct.pack('<I', (3 << 26) | (0x1A2370 >> 2))
    real = K.ov
    K.ov = bytes(ov)
    try:
        expect(census_sites(K, caps), f'sub {s}: a stray jal 001A2370 in the module', want='0x1a2370 call sites')
        for form, w in (('j', (2 << 26) | (0x1A2370 >> 2)), ('address word', 0x1A2370)):
            ov2 = bytearray(real)
            ov2[at:at + 4] = struct.pack('<I', w)
            K.ov = bytes(ov2)
            expect(census_sites(K, caps), f'sub {s}: a stray {form} 001A2370 in the module',
                   want='0x1a2370 call sites')
    finally:
        K.ov = real
    base = C.u32(real, 8)
    expect(ov_arg_problems(real, base), f'sub {s}: the original 001A2370 arguments', accept=True)
    sites = {o: next(a for a in SITES[0x1A2370][o]) for o in (*A15.SELF_MATRIX_OWNERS, *A15.BONE0_OWNERS)}

    def ov_edit(at, fn):
        ov2 = bytearray(real)
        struct.pack_into('<I', ov2, at - base, fn(struct.unpack_from('<I', real, at - base)[0]))
        return bytes(ov2)
    for o in A15.SELF_MATRIX_OWNERS:
        x = sites[o]
        expect(ov_arg_problems(ov_edit(x - 4, lambda w: w + 4), base), f'sub {s}: {o:#x} building a1 as R + 0xD4',
               want=f'{o:#x}: the original\'s a1 at {x:#x} is not a register + 0xD0')
        expect(ov_arg_problems(ov_edit(x + 4, lambda w: w ^ (1 << 21)), base),
               f'sub {s}: {o:#x} copying another register into a0', want=f'{o:#x}: the original\'s a0 at {x:#x}')
        expect(ov_arg_problems(ov_edit(x, lambda w: 0), base), f'sub {s}: {o:#x} without its jal',
               want=f'{o:#x}: 0 jal 001A2370 in the original')
    for o in A15.BONE0_OWNERS:
        x = sites[o]
        expect(ov_arg_problems(ov_edit(x + 4, lambda w: (w & ~0xFFFF) | 0xD0), base),
               f'sub {s}: {o:#x} building a1 as R + 0xD0', want=f'{o:#x}: the original\'s a1 at {x:#x} is not a register + 0x90')
        expect(ov_arg_problems(ov_edit(x - 4, lambda w: w + 4), base), f'sub {s}: {o:#x} loading R from 4(R)',
               want=f'{o:#x}: the original\'s a1 base at {x:#x} is not loaded through itself')
        k = next(k for k in (2, 3) if (struct.unpack_from('<I', real, x - 4 * k - base)[0] >> 21) & 31 == 28)
        expect(ov_arg_problems(ov_edit(x - 4 * k, lambda w: w + 4), base), f'sub {s}: {o:#x} reading R from gp + 4',
               want=f'{o:#x}: the original\'s a1 base at {x:#x} is not read from D_00275B40')
    sizes = {**FUNC_SIZE, 0x825430: 0x100}
    expect(census_sites(K, caps, sizes=sizes), f'sub {s}: 0x825430 sized 0x100',
           want='a site of 0x825430 lies outside it')
    saved = A15.BONE0_OWNERS
    A15.BONE0_OWNERS = (0x825430,)
    try:
        expect(census_sites(K, caps), f'sub {s}: 0x825D10 not modelled', want='!= the modelled')
    finally:
        A15.BONE0_OWNERS = saved
    cap = caps[0]
    node = next(a for _s, a in T.pool_nodes(cap.ram) if C.u32(cap.ram, a + 0x10) in OWNERS[s])
    other = OWNERS[1 - s][0]
    fake = T13.ram_copy(cap, cap.name, [(node + 0x10, struct.pack('<I', other))])
    expect(census_sites(K, [fake]), f'sub {s}: a live node of the other sub\'s owner',
           want='a live node of the other sub')
    places, groups = T13.roster_records((T13.A / f'sub{s}/roster.emro').read_bytes())
    p2 = [bytearray(r) for r in places]
    struct.pack_into('<I', p2[0], 0x24, other)
    expect(census_sites(K, caps, [bytes(r) for r in p2], groups), f'sub {s}: a placement of the other sub\'s owner',
           want='roster names the other sub')
    g2 = [(a, [bytearray(r) for r in recs]) for a, recs in groups]
    struct.pack_into('<I', g2[0][1][0], 0x28, other)
    expect(census_sites(K, caps, places, [(a, [bytes(r) for r in recs]) for a, recs in g2]),
           f'sub {s}: a group record of the other sub\'s owner', want='roster names the other sub')
    return expect.count - n0


def level_controls(K, caps, expect):
    """The sub-0 level_problems replacement on changed inputs."""
    n0 = expect.count
    sub = T13.sub_dir(K.t)
    real = sub / 'level/dynamic_objects.emsc'
    expect(level_problems(K, caps, sub)[0], 'sub 0: the level as exported', accept=True)
    tmp = light_copy(sub, BUILD / f'lvl-{os.getpid()}')
    try:
        (tmp / 'level/dynamic_objects.emsc').unlink()
        expect(level_problems(K, caps, tmp)[0], 'sub 0: no dynamic-list file', want='no dynamic-list file')
        blob = bytearray(real.read_bytes())
        blob[20 + 0x10 + 5] ^= 1
        replace_file(tmp / 'level/dynamic_objects.emsc', bytes(blob))
        expect(level_problems(K, caps, tmp)[0], 'sub 0: a dynamic-list byte', want='a dynamic-list file that is not')
        dyn = bytes(real.read_bytes())
        base, entry, length = struct.unpack_from('<3I', dyn, 8)
        fake = T13.ram_copy(caps[0], caps[0].name, [(base, struct.pack('<I', 0))])
        replace_file(tmp / 'level/dynamic_objects.emsc', C.emsc(base, fake.ram[base:base + 0x10], entry))
        expect(level_problems(K, [fake], tmp)[0], 'sub 0: the list emptied in RAM and file (its kicks outside it)',
               want='outside the dynamic list')
    finally:
        shutil.rmtree(tmp)
    return expect.count - n0


def flag_record_canary(K, cap, since):
    """The AREA13 canary's flag-call-record section plants an empty call
    list into cells.json, which a capture without a 0019C6F0 call (sub 0)
    already records, so that section cannot report there. Its one failure
    line (exactly that text, since `since`) is dropped and replaced by the
    same comparison with a non-empty planted list: cells_problems must then
    report the capture's flag calls. Returns 1 when the line was dropped."""
    line = f'canary {K.t.name}: section flag call record did not report "{cap.name}: flag calls"'
    dropped = [k for k in range(since, len(FAILS)) if line in FAILS[k]]
    for k in reversed(dropped):
        del FAILS[k]
    sub = T13.sub_dir(K.t)
    rec = json.loads((sub / 'cells.json').read_text())
    rec['cells']['flag_calls'][cap.name] = [[0x22, 1]]
    cells = (sub / f'{K.t.name}_cells.bin').read_bytes()
    got = T13.cells_problems(K, cells, [cap], rec)
    check(any(f'{cap.name}: flag calls' in p for p in got),
          f'canary {K.t.name}: a planted flag call [0x22, 1] not reported ({got[:2]})')
    return len(dropped)


# ---------------------------------------------------------------------------


def main():
    need = [OUT / SPLIT.MANIFEST, C.ELF_PATH, C.ISO_PATH, A15.TARGET.overlay_path, CENSUS] + \
        [A15.SUBS[s]['route'] / n / 'eeMemory.bin' for s in SUBS for n in (CAPTURE[s], A15.SUBS[s]['previous'])]
    missing = [str(p) for p in need if not p.exists()]
    if missing:
        print('area15 assets reference: SKIPPED, missing local inputs:', missing)
        return 0
    t0 = time.time()
    views = {s: BUILD / f'view{s}-{os.getpid()}' for s in SUBS}
    Ks, caps, every, rows = {}, {}, {}, {}
    try:
        for s in SUBS:
            build_view(OUT, s, views[s])
        el = T13.L.load_export_level()
        T13.BUILD = T01.BUILD = BUILD              # the loaders and the background shim are built privately
        lib = T01.build_loaders()
        bg = T13.background_lib()
        for s in SUBS:
            configure(s, views[s])
            K, cs = T13.target_setup('area15', el)
            Ks[s], caps[s], every[s] = K, cs, K.every
            print(f'area15 assets reference ({MODE}) AREA15 sub {s}: {len(cs)} of {len(K.every)} captures')
            loaded = T13.check_loaders(lib, bg, K.t, views[s])
            if s == 0:
                f = lib.em_script_image_load
                f.restype = T13.CT.c_int
                dpath = views[s] / 'sub0/level/dynamic_objects.emsc'
                loaded['dynamic_objects.emsc'] = f(T13.CT.create_string_buffer(16 << 20), str(dpath).encode()) == 1
                BUILD.mkdir(parents=True, exist_ok=True)
                cut = BUILD / f'dyncut-{os.getpid()}.emsc'
                blob = dpath.read_bytes()
                cut.write_bytes(C.emsc(C.u32(blob, 8), blob[20:52], C.u32(blob, 12)))
                with T01.quiet_fds():
                    refused = f(T13.CT.create_string_buffer(16 << 20), str(cut).encode()) != 1
                cut.unlink()
                check(refused, 'loader control: a cut dynamic list accepted')
            for lname, ok in loaded.items():
                check(ok, f'sub {s}: port loader rejects {lname}')
            files = [k for k in loaded if k not in ('cells refused (bit 29)', 'cells (bit 29 cleared)')]
            print(f'  loaders: {sum(loaded[k] for k in files)}/{len(files)} files accepted by the port loaders')
            _ROWS.clear()
            original = LV.verify_directory
            LV.verify_directory = recording_verify_directory(original)
            try:
                problems, V = T13.run_checks(K, cs)
            finally:
                LV.verify_directory = original
            for p in problems:
                check(False, f'sub {s}: {p}')
            for name, r in _ROWS.items():
                for row in r:
                    rows[row['capture']] = row
            lv = V.get('level', {})
            print(f"  AREA13 lane's checks: {lv.get('objects')} bank objects -> {lv.get('zones')} zone EMDL(s), "
                  f"{lv.get('kicks')} level kicks, {lv.get('dynamic_kicks', 0)} dynamic kicks; collision, cells, "
                  f"tables, sfx, ctx: {len(problems)} problems")
        befores = {s: (A15.previous(s) / 'eeMemory.bin').read_bytes() for s in SUBS}
        census = json.loads(CENSUS.read_text())
        T13.A = views[0]
        lp = lane_checks(caps, every, OUT, befores, census, rows)
        for p in lp:
            check(False, f'lane: {p}')
        print(f'  lane checks (captures, cursors, D_0028A5A4, split, sub proof, rules, census): {len(lp)} problems')
        if FAILS:
            print(f'  canary and controls: skipped, {len(FAILS)} check(s) already failed')
        else:
            expect = Expect()
            for s in SUBS:
                configure(s, views[s])
                T13.use(Ks[s])
                n = len(FAILS)
                buf = io.StringIO()
                with contextlib.redirect_stdout(buf):
                    sections = T13.canary(Ks[s], lib, bg, caps[s][0])
                note, dropped = '', ''
                if s == 0:
                    dropped = (f'FAIL: canary {Ks[s].t.name}: section flag call record did not report '
                               f'"{caps[s][0].name}: flag calls"')
                    sections -= flag_record_canary(Ks[s], caps[s][0], n)
                    note = ' (its flag-call-record plant replaced, see flag_record_canary)'
                for line in buf.getvalue().splitlines():
                    if line != dropped:
                        print(line)
                print(f'  canary sub {s}: {sections} sections each reported their planted difference{note}')
                for p in plant_problems(s, PLANTED.get(s)):
                    check(False, f'lane: {p}')
            if not FAILS:
                n = 0
                for s in SUBS:
                    configure(s, views[s])
                    T13.use(Ks[s])
                    n += census_site_controls(Ks[s], caps[s], expect)
                    if s == 0:
                        n += level_controls(Ks[s], caps[s], expect)
                T13.A = views[0]
                n += lane_controls(caps, every, befores, census, rows, expect)
                print(f'  controls: {n} changed inputs, each reported by its own guard (or accepted where marked)')
    finally:
        for v in views.values():
            if v.exists():
                shutil.rmtree(v)
    print(f'area15 assets reference: {"FAIL" if FAILS else "PASS"} ({len(FAILS)} failures, '
          f'{time.time() - t0:.1f} s)')
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
