#!/usr/bin/env python3
"""Export the native SFX registry through the original A0 trigger path.

Every id the port registry carries (tools/gen_sfx_registry.py presets in the
decomp repo, plus the AREA11 panel pair, then its FIRST_LEVEL_GROUPS census
of every id the first level can request, docs/SFX_REGISTRY_FIRST_LEVEL.md)
and AREA01 sub-0's and AREA00 sub-0/1's complete area-paged sound tables are
resolved per scene area exactly as the original does it:

  001FB9F0   id -> sound record (global tables or area remap/record tables)
  00119EA0   record -> registered bank handle -> trigger script
  001152D8   script events + deltas (00117088 fetch, 00118E60 delta)
  00115850   A0 note -> program/tone, bend 0x40 stored before 00117918
  00117918   integer pitch ladder D_00241D70, then *44100/48000
  001179E0   Q14 volume words from the 6-way scalar, pan word and the
             requested track gains (+0x48/+0x4C, 0011A218)

Output (ignored, local): assets/sfx/sfx_registry.emsr (binary, decoded source
PCM + integer driver parameters) and sfx_registry.json (provenance). Nothing
from the disc is embedded in this file. Scripts, tones or samples that need
driver features the native mixer does not reproduce are exported as
UNSUPPORTED with a reason rather than approximated.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ELF_SHA = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
GLOBAL_CONTAINER = 'extract/chunk00/f05_id05.bin'
AREA11_CONTAINER = 'extract/chunk15/f00_id43.bin'
# The AREA11 host submits these two ids (docs/AREA11_PANEL_SFX.md).
AREA11_EXTRA_IDS = (0x3EE, 0x3EF)
# The level exit's arrival (route beat 15, docs/FIRST_LEVEL_EXIT.md): AREA01
# sub 0, whose spawn entry 4 names the ambient loop 0x44E that 001FC280
# starts with 001FB9F0 at the arrival's 001FAE70(1). Its bank binding comes
# from the first level's own AREA01 captures (the beat-15 arrival and the
# EXIT lane's exit_01), through export_area01_sfx.area_binding.
ARRIVAL_SCOPE = (1, 0)
ARRIVAL_IDS = (0x44E,)
ARRIVAL_CAPTURES = ('build/s87/route/15_level_exit', 'build/c10/exit/exit_01_movie_arrival')
# The same finite tables already exported by export_area01_sfx.py. Keep
# ARRIVAL_IDS first: extending coverage must not renumber existing samples.
# These are table coverage, not a claim that every id occurs on the route.
AREA01_IDS = tuple(range(0x3E8, 0x5DC)) + tuple(range(0x7D0, 0x9C4))
# 001C2770's failed-grab cue: a global record reached by an AREA01-censused
# caller but absent from the older first-level list (LEVEL2_AUDIO.md ("AREA01 SFX resources")).
AREA01_SHARED_IDS = (0x1AC,)
# Complete original remap-table domains, appended after every existing
# scope so their addition does not renumber the installed samples.
AREA00_SCOPES = ((0, 0), (0, 1))
AREA00_IDS = tuple(range(0x3E8, 0x5DC)) + tuple(range(0x7D0, 0x9C4))
# AREA02 (export_area02_sfx.sub_binding: each sub's capture-checked
# binding; docs/AREA02_PRETRANS.md). Sub 1 replaces the office fixture's
# coverage match (chunk04.n0, AREA00's container; no capture) with the
# AREA02 sub-1 captures' binding (chunk06.n1). Both AREA02 scopes are
# resolved last, so every other scope keeps its samples and indices.
AREA02_SCOPES = ((2, 0), (2, 1))
AREA02_IDS = AREA00_IDS
# AREA04 sub 0 (export_area04_sfx.sub_binding: the capture-checked binding
# of the a02_05 arrival and the a04 captures; docs/AREA04_ASSETS.md),
# resolved after AREA02 so every earlier scope keeps its samples.
AREA04_SCOPES = ((4, 0),)
AREA04_IDS = AREA00_IDS
# The attachments' banks (001FEF70's modules 0x32..0x35, extract/chunk50..53,
# loaded when one is fitted): they take group 3 bank 0, the slot of the weapon
# cues 0x5DC..0x5E1 (the attached weapon's shot and holster), whose header has
# no SShd magic in every area's capture without one. The scope's area is 32 +
# module - 0x32, a scope of its own (em_sfx_set_attachment), resolved last.
ATTACHMENT_SCOPES = tuple((32 + i, 0) for i in range(4))
ATTACHMENT_IDS = tuple(range(0x5DC, 0x5E2))
# The middle of the route to the first boss (lane C1): AREA22 sub 0
# (export_area22_sfx.scope_binding) and AREA06 sub 0
# (export_area06_sfx.sub_binding), each its captures' binding; resolved
# after AREA04, so every earlier scope keeps its samples and indices.
MID_SCOPES = ((0x16, 0), (6, 0), (0x0F, 0), (0x0F, 1), (3, 0))
MID_IDS = AREA00_IDS
# AREA19 sub 0 and sub 1 (lane J1): the capture-checked bindings
# export_area19_sfx.py / export_area19s1_sfx.py recorded in each sub's
# banks.json over its container (those exporters run first); resolved last,
# so every earlier scope keeps its samples.
AREA19_SCOPES = ((0x13, 0), (0x13, 1))
AREA19_IDS = AREA00_IDS
AREA19_SFX_DIRS = {0: ROOT / 'assets/area19/sub0/sfx', 1: ROOT / 'assets/area19s1/sub1/sfx'}

STATE_AUDIBLE, STATE_ABSENT, STATE_UNSUPPORTED = 1, 2, 3
REASONS = {
    'script': 1,           # a status other than A0 / FF 2F in the script
    'looping sample': 2,   # repeat without loop start / unsettled loop body
    'sweep volume': 3,     # tone +0x0A != 0 (001179E0 sweep form)
    'modulation': 4,       # tone flag 0x20 (00115850 voice +0x14)
    'noise': 5,            # tone flag 0x02 (voice command 0x33)
    'sustained key-off': 6,  # retired in EMSR v2 (key-off runs natively)
    'unbound bank': 7,     # bank slot not registered for this area
    'track defaults': 8,   # channel defaults differ between track slots
    'pitch range': 9,      # pitch outside 1..0x3FFF
    'no voice': 10,        # script produces no voice at all
}


def load_decomp_module(name):
    spec = importlib.util.spec_from_file_location(
        f'decomp_{name}', DECOMP / 'tools' / f'{name}.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


A = load_decomp_module('audio_export')


class Elf(A.ElfImage):
    def __init__(self):
        super().__init__(DECOMP / 'config/SCUS_971.12')
        assert hashlib.sha256(self.data).hexdigest() == ELF_SHA

    def u16(self, address):
        return struct.unpack('<H', self.read(address, 2))[0]

    def u8(self, address):
        return self.read(address, 1)[0]


class Unsupported(Exception):
    def __init__(self, reason, detail=''):
        super().__init__(f'{reason}: {detail}' if detail else reason)
        self.reason, self.detail = reason, detail


def sound_record(elf, sound_id, area, sub):
    """001FB9F0 record selection (int-width pointer walk); None = -1."""
    sound_id &= 0x7FFF
    if sound_id < 0x3E8:
        return 0x25ECA0 + 4 * sound_id, 'global'
    if sound_id < 0x5DC or 0x7D0 <= sound_id < 0x9C4:
        remaps, records, first = ((0x264A70, 0x264B30, 0x3E8)
                                  if sound_id < 0x5DC else
                                  (0x264AD0, 0x264B90, 0x7D0))
        table = elf.u32(remaps + 4 * area)
        if not table:
            return None, 'area'
        table = elf.u32(table + 4 * sub)
        if not table:
            return None, 'area'
        remap = elf.u8(table + sound_id - first)
        if remap == 0xFF:
            return None, 'area'
        table = elf.u32(records + 4 * area)
        if not table:
            return None, 'area'
        table = elf.u32(table + 4 * sub)
        if not table:
            return None, 'area'
        return table + 4 * remap, 'area'
    if sound_id < 0x7D0:
        return 0x261570 + 4 * (sound_id - 0x5DC), 'global'
    return None, 'global'


class Bank:
    def __init__(self, name, data, container, row):
        self.name, self.data, self.row = name, data, row
        bank = container['banks'][row]
        self.hd, self.body, self.type = bank['hd'], bank['body_base'], bank['type']
        self.body_size = bank['body_size']
        following = [b['hd'] for b in container['banks'] if b['hd'] > self.hd]
        self.header_end = min(following + [container['img_off']])

    def u8(self, offset):
        return self.data[offset]

    def u16(self, offset):
        return struct.unpack_from('<H', self.data, offset)[0]

    def u32(self, offset):
        return struct.unpack_from('<I', self.data, offset)[0]

    def script(self, group, index):
        """00119EA0 script-table walk; None when it returns -1."""
        if self.u32(self.hd + 0x20) == 0xFFFFFFFF:
            return None
        table = self.hd + self.u32(self.hd + 0x1C)
        if self.u16(table) < group:
            return None
        x = self.u16(table + 2 * group + 2)
        if x == 0xFFFF:
            return None
        select = x >> 1
        if self.u16(table + 2 * select) < index:
            return None
        return table + self.u16(table + 2 * (index + select) + 2)


def containers_by_type(name, data):
    container = A.parse_container(data)
    if container is None:
        raise ValueError(f'{name}: not an SShd container')
    rows = {}
    for row, bank in enumerate(container['banks']):
        rows.setdefault(bank['type'], []).append(Bank(name, data, container, row))
    return rows


def arrival_binding():
    """{ARRIVAL_SCOPE: binding} from the arrival captures (see ARRIVAL_IDS)."""
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import export_area01_common as C
    import export_area01_sfx as S
    caps = [C.Capture(DECOMP / p) for p in ARRIVAL_CAPTURES if (DECOMP / p / 'eeMemory.bin').exists()]
    if not caps or any(c.area[:2] != ARRIVAL_SCOPE for c in caps):
        raise SystemExit('export_sfx_registry: the AREA01 arrival captures (' + ', '.join(ARRIVAL_CAPTURES) +
                         ') are missing or not in AREA01 sub 0')
    binding = S.area_binding(caps)[0][ARRIVAL_SCOPE]
    binding['binding'] = ('the level exit\'s AREA01 arrival captures (D_00281D50/D_0027C6C0): ' +
                          ', '.join(c.name for c in caps))
    return {ARRIVAL_SCOPE: binding}


def area00_bindings():
    """Both sub bindings from AREA00's capture-checked container helper.
    Restore the shared AREA01 exporter configuration after the scoped call."""
    if not AREA00_SCOPES:
        return {}
    import export_area01_common as C
    import export_area00_common as A0
    names = ('OVERLAY_PATH', 'OVERLAY_SIZE', 'OVERLAY_ID', 'AREA', 'SUB',
             'OUT', 'SCRATCH', 'ROUTE_A01', 'ARRIVAL', 'captures')
    saved = {name: getattr(C, name) for name in names}
    try:
        import export_area00_sfx as S0
        result, container = {}, None
        for area, sub in AREA00_SCOPES:
            assert area == 0
            A0.configure(sub)
            caps = A0.captures(sub)
            binding, rel, data, parsed, _bound, _refused, _report = S0.area_binding(caps, container)
            container = (rel, data, parsed)
            result.update(binding)
        return result
    finally:
        for name, value in saved.items():
            setattr(C, name, value)


def area02_bindings():
    """Both AREA02 sub bindings; restores the shared AREA01 exporter
    configuration afterwards, as area00_bindings does."""
    import export_area01_common as C
    names = ('OVERLAY_PATH', 'OVERLAY_SIZE', 'OVERLAY_ID', 'AREA', 'SUB',
             'OUT', 'SCRATCH', 'ROUTE_A01', 'ARRIVAL', 'captures')
    saved = {name: getattr(C, name) for name in names}
    try:
        import export_area02_sfx as S2
        result = {}
        for area, sub in AREA02_SCOPES:
            result.update(S2.sub_binding(sub)[0])
        return result
    finally:
        for name, value in saved.items():
            setattr(C, name, value)


def area04_bindings():
    """The AREA04 sub-0 binding; restores the shared AREA01 exporter
    configuration afterwards, as area02_bindings does."""
    import export_area01_common as C
    names = ('OVERLAY_PATH', 'OVERLAY_SIZE', 'OVERLAY_ID', 'AREA', 'SUB',
             'OUT', 'SCRATCH', 'ROUTE_A01', 'ARRIVAL', 'captures')
    saved = {name: getattr(C, name) for name in names}
    try:
        import export_area04_sfx as S4
        result = {}
        for area, sub in AREA04_SCOPES:
            result.update(S4.sub_binding(sub)[0])
        return result
    finally:
        for name, value in saved.items():
            setattr(C, name, value)


def mid_bindings():
    """AREA22's and AREA06's sub-0 bindings; restores the shared AREA01
    exporter configuration afterwards, as area02_bindings does."""
    import export_area01_common as C
    names = ('OVERLAY_PATH', 'OVERLAY_SIZE', 'OVERLAY_ID', 'AREA', 'SUB',
             'OUT', 'SCRATCH', 'ROUTE_A01', 'ARRIVAL', 'captures', 'build_load_map')
    saved = {name: getattr(C, name) for name in names}
    try:
        # AREA06's helper first: it needs AREA01's nested load-map builder,
        # which AREA22's configure replaces (export_area06_common's guard)
        import export_area06_common  # noqa: F401
        import export_area15_common as A15   # the same guard (export_area13_common's)
        import export_area03_common as A03
        for name, value in saved.items():    # their imports configure C
            setattr(C, name, value)
        import export_area22_sfx as S22
        result = dict(S22.scope_binding()[0])
        import export_area06_sfx as S6
        result.update(S6.sub_binding(0)[0])
        # AREA15's two subs and AREA03's (export_area13_sfx's target path).
        for sub in (0, 1):
            A15.install(sub)
            result.update(A15.SX.target_binding('area15'))
        A15.uninstall()
        A03.install()
        result.update(A03.SX.target_binding('area03'))
        assert sorted(result) == sorted(MID_SCOPES), sorted(result)
        return result
    finally:
        for name, value in saved.items():
            setattr(C, name, value)


def area19_bindings():
    """AREA19's sub-0 and sub-1 bindings from each sub's banks.json (its
    captures' D_00281D50 / D_0027C6C0 slots, checked at that export) over
    the container and the global rows."""
    gdata = (DECOMP / GLOBAL_CONTAINER).read_bytes()
    global_rows = A.parse_container(gdata)
    result = {}
    for sub, folder in AREA19_SFX_DIRS.items():
        report = json.loads((folder / 'banks.json').read_text())
        data = (folder / 'area19_banks.bin').read_bytes()
        name = f'area19_sub{sub}_banks.bin'
        area_rows = A.parse_container(data)
        if report['target'] != 'area19' or report['sub'] != sub or \
                hashlib.sha256(data).hexdigest() != report['container_sha256']:
            raise ValueError(f'{folder}: banks.json does not describe this container')
        groups = {}
        for key, source in sorted(report['bindings'].items()):
            g, i = map(int, key.split('.'))
            where, row = source.split('#row')
            bank = Bank(GLOBAL_CONTAINER, gdata, global_rows, int(row)) if where == 'global' else \
                Bank(name, data, area_rows, int(row))
            lst = groups.setdefault(g, [])
            if len(lst) != i:
                raise ValueError(f'{folder}: group {g} slot {i} bound without slot {len(lst)}')
            lst.append(bank)
        refused = {tuple(map(int, k.split('.'))): v for k, v in report['refused_why'].items()}
        result[(0x13, sub)] = dict(groups=groups, refused=refused,
                                   binding=f'AREA19 sub {sub} captures (D_00281D50/D_0027C6C0)')
    return result


def area_bindings(elf):
    """(area, sub) -> {group: [Bank]} plus provenance."""
    global_rows = containers_by_type(GLOBAL_CONTAINER,
                                     (DECOMP / GLOBAL_CONTAINER).read_bytes())
    area11 = containers_by_type(AREA11_CONTAINER,
                                (DECOMP / AREA11_CONTAINER).read_bytes())
    bindings = {
        # Both AREA11 captures: D_00281D50 group1 -> handles 0,1,2 (global
        # rows), group2 bank0 -> handle 4 (chunk15), group4 all zero.
        # tools/test_area11_sfx_reference.py re-checks this binding.
        (11, 0): dict(groups={1: global_rows[1], 2: area11[2]},
                      binding='AREA11 captures (D_00281D50/D_0027C6C0)',
                      # Slots no AREA11 bank registers but whose handle
                      # 00119EA0 refuses (-1), so 001FB9F0 plays nothing:
                      # group 3 bank 0 names handle 3 in both captures,
                      # whose D_0027C6C0 header (0x1800030) has no SShd
                      # magic at +0xC. The test re-checks the handle and
                      # the magic and runs 001FB9F0 over both captures.
                      refused={(3, 0): 'group 3 bank 0 = handle 3, header '
                                       'without SShd magic in AREA11'}),
    }
    # Office has no capture: its region comes from the decomp soundmap's
    # script-coverage match (audio_export.match_area_regions).
    _, area_records = A.read_sound_records(elf)
    sources = A.find_containers(DECOMP / 'extract')
    regions = {}
    for name, data in sources:
        types = [b['type'] for b in A.parse_container(data)['banks']]
        if 2 in types and 1 not in types:
            regions[name] = (data, containers_by_type(name, data))
    candidates = {name: {g: [A._Bank(name, data, A.parse_container(data), b.row)
                             for b in banks] for g, banks in rows.items()}
                  for name, (data, rows) in regions.items()}
    bindings.update(arrival_binding())
    match = A.match_area_regions(area_records, candidates)[(2, 1)]
    office = regions[match['region']][1]
    bindings[(2, 1)] = dict(groups={1: global_rows[1], 2: office.get(2, []),
                                    4: office.get(4, [])},
                            binding=f"coverage match {match['region']} "
                                    f"(coverage {match['coverage']}, "
                                    f"exact {match['exact']}); no capture")
    bindings.update(area00_bindings())
    bindings.update(area02_bindings())
    bindings.update(area04_bindings())
    bindings.update(mid_bindings())
    bindings.update(area19_bindings())
    for i, scope in enumerate(ATTACHMENT_SCOPES):
        name = f'extract/chunk{50 + i}/f00_id86.bin'
        rows = containers_by_type(name, (DECOMP / name).read_bytes())
        banks = [bank for row in sorted(rows) for bank in rows[row]]
        assert len(banks) == 1, name
        bindings[scope] = dict(groups={3: banks}, binding=f'module {0x32 + i:#x}, {name}: 001FEF70')
    return bindings


ADPCM_FRAME, ADPCM_SAMPLES = 16, 28
ADPCM_COEFS = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))


def sample_blocks(bank, tone_sample):
    """ADPCM blocks from the tone's start through the first end-flag block.

    Returns (offset, raw, loop_start_block): the SPU2 repeats from the last
    loop-start (flag 0x04) block when the end block also carries the repeat
    flag 0x02; None when the sample ends (flag 0x01 alone)."""
    start = bank.body + (tone_sample << 3)
    end = start
    loop_start = None
    while True:
        if end + ADPCM_FRAME > len(bank.data):
            raise ValueError('sample lacks an end flag')
        flags = bank.data[end + 1]
        if flags & 4:
            loop_start = (end - start) // ADPCM_FRAME
        end += ADPCM_FRAME
        if flags & 1:
            raw = bank.data[start:end]
            if not flags & 2:
                return start, raw, None
            if loop_start is None:
                raise Unsupported('looping sample', 'repeat without loop start')
            return start, raw, loop_start


def decode_blocks(raw, first, count, history):
    """SPU ADPCM blocks [first, first+count) from a (hist1, hist2) state.

    Filters 0..4 and shifts 0..12 only; anything else is refused by the
    caller (no decoder behaviour outside that range is claimed)."""
    hist1, hist2 = history
    out = []
    for block in range(first, first + count):
        at = block * ADPCM_FRAME
        shift, predictor = raw[at] & 0xF, raw[at] >> 4
        c1, c2 = ADPCM_COEFS[predictor]
        for i in range(ADPCM_SAMPLES):
            nibble = raw[at + 2 + (i >> 1)] >> (4 * (i & 1)) & 0xF
            nibble -= 16 if nibble > 7 else 0
            value = (nibble << (12 - shift)) + ((hist1 * c1 + hist2 * c2) >> 6)
            value = max(-32768, min(32767, value))
            hist2, hist1 = hist1, value
            out.append(value)
    return out, (hist1, hist2)


def sample_pcm(raw, loop_start):
    """(pcm, loop_body): the key-on pass decoded from zero history; for a
    looping sample also the body as replayed with the history carried over
    from the loop end. The export requires the third pass to repeat the
    second, so the native mixer may replay one fixed body."""
    blocks = len(raw) // ADPCM_FRAME
    for block in range(blocks):
        if raw[block * ADPCM_FRAME] & 0xF > 12 or raw[block * ADPCM_FRAME] >> 4 > 4:
            raise Unsupported('script', 'ADPCM shift/filter outside 0..12/0..4')
    pcm, history = decode_blocks(raw, 0, blocks, (0, 0))
    if loop_start is None:
        return pcm, None
    body, history = decode_blocks(raw, loop_start, blocks - loop_start, history)
    again, _ = decode_blocks(raw, loop_start, blocks - loop_start, history)
    if again != body:
        raise Unsupported('looping sample', 'loop body does not settle')
    return pcm, body


def script_events(data, position, limit=0x1000):
    """001152D8 SFX-track event loop over one trigger script.

    00117088 fetch with running status (a data byte re-uses the stored
    status and the cursor steps back one), 00118E60 big-endian VLQ delta,
    the track accumulator +0x20 (+= delta<<12, the tail subtracts
    +0x1C = 0x1E0000/60 per tick while the track runs). Dispatch:
      A0 note vel prog   00115850 (vel 0 -> 001176E0 key-off), cursor +4
      B0 41 a2 a3 prog note  00118078 portamento set-up, cursor +6
      B0 07 a2 a3 prog note  001181B0 volume slide set-up (the SFX track's
                         +0x34 == 1 arm: length a2, target a3), cursor +6
      B0 0A a2 a3 prog note  00118418 pan slide set-up (the same arm), cursor +6
      B0 60 lo hi 0      00118790 the track's loop to a script offset
      FF 2F 00           00117C28 end of track (no delta)
    Anything else (other controllers, 0x80/0x90/0xC0/0xE0, tempo) is refused.
    """
    events, running, acc, tick = [], None, 0, 0
    start, end = position, min(position + limit, len(data))
    while position < end:
        at = position
        if data[position] & 0x80:
            running = data[position]
            position += 1
        if running is None:
            raise Unsupported('script', 'no status')
        if running == 0xFF:
            if data[position:position + 2] != b'\x2f\x00':
                raise Unsupported('script', f'unsupported meta 0x{data[position]:02X}')
            events.append(dict(kind='end', tick=tick))
            return events
        if running & 0xF0 == 0xA0:
            note, vel, prog = data[position:position + 3]
            position += 3
            events.append(dict(kind='a0', tick=tick, note=note, vel=vel, prog=prog))
        elif running & 0xF0 == 0xB0 and data[position] == 0x60:
            # 00118790, count 0: the track goes on at the script offset
            # (+0x14), at the next tick (the +0x3C arm clears +0x20). Nothing
            # after it runs: the track ends only by a stop.
            _, low, high, count = data[position:position + 4]
            if count:
                raise Unsupported('script', 'a counted B0 60 loop')
            if acc:
                raise Unsupported('script', 'a B0 60 loop off the tick grid')
            target = start + (high << 8 | low)
            if not any(e['at'] == target for e in events):
                raise Unsupported('script', 'a B0 60 target that is no event')
            events.append(dict(kind='jump', tick=tick, at=at, target=target))
            return events
        elif running & 0xF0 == 0xB0 and data[position] == 0x41:
            _, length, depth, prog, note = data[position:position + 5]
            position += 5
            events.append(dict(kind='portamento', tick=tick, note=note, prog=prog,
                               length=length, depth=depth))
        elif running & 0xF0 == 0xB0 and data[position] == 0x07:
            _, length, target, prog, note = data[position:position + 5]
            position += 5
            events.append(dict(kind='volume', tick=tick, note=note, prog=prog,
                               length=length, depth=target))
        elif running & 0xF0 == 0xB0 and data[position] == 0x0A:
            _, length, target, prog, note = data[position:position + 5]
            position += 5
            events.append(dict(kind='pan', tick=tick, note=note, prog=prog,
                               length=length, depth=target))
        elif running & 0xF0 == 0xB0:
            raise Unsupported('script', f'unsupported controller 0x{data[position]:02X}')
        else:
            raise Unsupported('script', f'unsupported status 0x{running:02X}')
        events[-1]['at'] = at
        delta = 0
        while True:
            byte = data[position]
            position += 1
            delta = delta << 7 | byte & 0x7F
            if not byte & 0x80:
                break
        acc += delta << 12
        while acc > 0:
            acc -= A.SEQ_TICK
            tick += 1
    raise Unsupported('script', 'truncated')


OP_KEY_ON, OP_KEY_OFF, OP_PORTAMENTO, OP_END, OP_VOLUME, OP_PAN, OP_JUMP = 1, 2, 3, 4, 5, 6, 7
OP_MAX = 64
# D_00242630 entries 00116598's pan slide indexes (+0x4C >> 2).
PAN_COUNT = 64
# D_00241D70 entries exported for 00117918: the A0 bend is always 0x40, so
# indices stay within (11|12)*16 + fine(-128..127) + 0xD0 +- 15 (portamento).
LADDER_COUNT = 0x240


def resolve(elf, bindings, sound_id, area, sub, samples):
    record, kind = sound_record(elf, sound_id, area, sub)
    if record is None:
        return dict(id=sound_id, state=STATE_ABSENT, scope=[area, sub],
                    note='001FB9F0 returns -1 (remap FF / null table)')
    group, bank_index, script_group, script_index = struct.unpack(
        '4b', elf.read(record, 4))
    scope = [-1, -1] if kind == 'global' and group == 1 else [area, sub]
    base = dict(id=sound_id, scope=scope, record=[group, bank_index,
                script_group, script_index], record_address=record)
    try:
        binding = bindings[(area, sub)]
        banks = binding['groups'].get(group, [])
        if not 0 <= bank_index < len(banks):
            refused = binding.get('refused', {}).get((group, bank_index))
            if refused:
                return dict(base, state=STATE_ABSENT,
                            note=f'00119EA0 returns -1 ({refused})')
            raise Unsupported('unbound bank', f'group {group} bank {bank_index}')
        bank = banks[bank_index]
        position = bank.script(script_group, script_index)
        if position is None:
            return dict(base, state=STATE_ABSENT,
                        note='00119EA0 returns -1 (no script)')
        events = script_events(bank.data, position)
        hd = bank.hd
        state = hd + bank.u32(hd + 0x20)
        programs = hd + bank.u32(hd + 0x24)
        velocities = hd + bank.u32(hd + 0x14)
        master = bank.u8(state)
        # 00117088 SFX mode: channel record = state + 0x10 + 16 * track.
        # 00115850/001179E0 read bytes 3, 0xC and 0xE (0xA is overwritten
        # with the bend 0x40). All 48 track slots must agree.
        channels = [bank.data[state + 0x10 + 16 * t:state + 0x20 + 16 * t]
                    for t in range(48)]
        if any(c[j] != channels[0][j] for c in channels for j in (3, 0xC, 0xE)):
            raise Unsupported('track defaults')
        channel = channels[0]
        out = []
        loop = events[-1].get('target')
        loop_row = None
        for event in events:
            if event.get('at') == loop:
                loop_row = len(out)
            if event['kind'] == 'jump':
                # EM_SFX_OP_JUMP: `length` = the target's operation index
                out.append(dict(kind='jump', op=OP_JUMP, tick=event['tick'],
                                length=sum('op' in e for e in out[:loop_row])))
                continue
            if event['kind'] == 'end':
                out.append(dict(kind='end', op=OP_END, tick=event['tick']))
                continue
            if event['kind'] == 'portamento':
                out.append(dict(event, op=OP_PORTAMENTO))
                continue
            if event['kind'] == 'volume':
                out.append(dict(event, op=OP_VOLUME))
                continue
            if event['kind'] == 'pan':
                out.append(dict(event, op=OP_PAN))
                continue
            if event['vel'] == 0:
                # 001176E0: keys off this track's sustained (voice +0x0C)
                # voices with the same note and program byte, at run time.
                out.append(dict(kind='key-off', op=OP_KEY_OFF, tick=event['tick'],
                                note=event['note'], prog=event['prog']))
                continue
            # SFX-mode program: offset table at state+0x312 (00117088).
            program = programs + bank.u16(state + 0x312 + 2 * event['prog'])
            assert program == programs + bank.u16(programs + 2 + 2 * event['prog'])
            slot = event['note'] - bank.u8(program + 6)
            if slot < 0:
                out.append(dict(kind='no voice (note below program)',
                                tick=event['tick'], note=event['note']))
                continue
            tone_at = program + 8 + 16 * slot
            tone = bank.data[tone_at:tone_at + 16]
            flags = tone[15]
            if tone[10]:
                raise Unsupported('sweep volume', f'tone +0x0A {tone[10]}')
            if flags & 0x20:
                raise Unsupported('modulation', f'tone flags {flags:#x}')
            if flags & 0x02:
                raise Unsupported('noise', f'tone flags {flags:#x}')
            bend_range = bank.u8(program + 4) if flags & 0x10 else tone[13]
            fine = struct.unpack('b', tone[3:4])[0]
            pitch = A.a0_pitch(elf, tone[2], event['note'], fine, bend_range)
            if not 0 < pitch <= 0x3FFF:
                raise Unsupported('pitch range', str(pitch))
            velocity = bank.u8(velocities + event['vel'] + 2)
            # 00115850's voice +0x28 / +0x34 / +0x2E / +0x2A / +0x30 (the
            # five factors a volume slide keeps) and +0x2C (the velocity
            # level, the factor 001181B0's slide moves); 001179E0 multiplies
            # all six (64-bit) and shifts by 27.
            product = (channel[14] * channel[3] * tone[11] *
                       bank.u8(program + 1) * master)
            scalar = (product * velocity) >> 27
            pan = elf.u16(A.PAN_TABLE + 2 * (tone[12] >> 2))
            tone_sample = struct.unpack_from('<H', tone, 4)[0]
            offset, raw, loop_block = sample_blocks(bank, tone_sample)
            key = (bank.name, offset)
            if key not in samples:
                pcm, body = sample_pcm(raw, loop_block)
                samples[key] = dict(index=len(samples), container=bank.name,
                                    offset=offset, adpcm=raw, pcm=pcm, body=body,
                                    loop_start=None if loop_block is None
                                    else loop_block * ADPCM_SAMPLES)
            left, right = volume_words(scalar, pan)
            out.append(dict(kind='voice', op=OP_KEY_ON, tick=event['tick'],
                            note=event['note'], velocity=event['vel'],
                            prog=event['prog'], program_offset=program,
                            tone_offset=tone_at, center=tone[2], fine=fine,
                            range=tone[13], alloc=tone[0], priority=tone[1],
                            pitch=pitch, scalar=scalar, pan=pan,
                            pan_level=tone[12], level=velocity, product=product,
                            adsr1=struct.unpack_from('<H', tone, 6)[0],
                            adsr2=struct.unpack_from('<H', tone, 8)[0],
                            flags=flags, reverb=bool(flags & 0x80),
                            sustained=bool(flags & 0x01),
                            tone_sample=tone_sample, sample=samples[key]['index'],
                            loops=loop_block is not None,
                            unit_words=[left, right]))
        if not [e for e in out if e.get('op') == OP_KEY_ON]:
            raise Unsupported('no voice')
        # A script with a volume slide: its key-ons carry the level and the
        # product the driver recomputes 001179E0's scalar from.
        slide = any(e.get('op') == OP_VOLUME for e in out)
        # A script with a pan slide (00118418, the SFX arm): its key-ons carry
        # +0x4C, the slide's start.
        pan_slide = any(e.get('op') == OP_PAN for e in out)
        for e in out:
            if e.get('op') == OP_KEY_ON:
                e['slide'] = slide
                e['pan_slide'] = pan_slide
        if len([e for e in out if 'op' in e]) > OP_MAX:
            raise Unsupported('script', f'more than {OP_MAX} operations')
        return dict(base, state=STATE_AUDIBLE, bank=f'{bank.name}#row{bank.row}',
                    bank_header=hd, bank_handle_group=group,
                    script_offset=position, events=out)
    except Unsupported as error:
        return dict(base, state=STATE_UNSUPPORTED, reason=error.reason,
                    detail=error.detail)


def volume_words(scalar, pan, left=0x1000, right=0x1000):
    """001179E0 for the default mono flag D_0027F778 = 0 (both captures)."""
    def word(byte, request):
        value = (scalar * byte * request) >> 19
        value = ((value + 0x8000) & 0xFFFF) - 0x8000     # (short)
        return (value & 0xFFFF) >> 1
    return word(pan >> 8, left), word(pan & 0xFF, right)


def census_ids():
    """The first-level sound census (gen_sfx_registry.FIRST_LEVEL_GROUPS):
    every id the first level can request, in group order, and its scope
    (docs/SFX_REGISTRY_FIRST_LEVEL.md)."""
    registry = load_decomp_module('gen_sfx_registry')
    area, sub = map(int, registry.FIRST_LEVEL_AREA.split('.'))
    return (area, sub), list(registry.first_level_ids())


def scene_ids(census=True):
    """Per-scope ids in resolution order. The census ids come last, after
    the presets, so every preset entry keeps its sample indices and bytes."""
    registry = load_decomp_module('gen_sfx_registry')
    scenes = {}
    for name, scene in registry.SCENES.items():
        area, sub = map(int, scene['area'].split('.'))
        ids = list(scene['ids'])
        door_ids, _, _ = registry.office_door_pairs(scene)
        ids += [i for i in door_ids if i not in ids]
        if (area, sub) == (11, 0):
            ids += [i for i in AREA11_EXTRA_IDS if i not in ids]
        scenes[(area, sub)] = ids
    scenes.setdefault(ARRIVAL_SCOPE, [])
    scenes[ARRIVAL_SCOPE] += [i for i in ARRIVAL_IDS if i not in scenes[ARRIVAL_SCOPE]]
    scenes[ARRIVAL_SCOPE] += [i for i in AREA01_IDS if i not in scenes[ARRIVAL_SCOPE]]
    scenes[ARRIVAL_SCOPE] += [i for i in AREA01_SHARED_IDS if i not in scenes[ARRIVAL_SCOPE]]
    for scope in AREA00_SCOPES:
        scenes.setdefault(scope, [])
        scenes[scope] += [i for i in AREA00_IDS if i not in scenes[scope]]
    for scope in AREA02_SCOPES:
        scenes.setdefault(scope, [])
        scenes[scope] += [i for i in AREA02_IDS if i not in scenes[scope]]
    for scope in AREA04_SCOPES:
        scenes.setdefault(scope, [])
        scenes[scope] += [i for i in AREA04_IDS if i not in scenes[scope]]
    for scope in MID_SCOPES:
        scenes.setdefault(scope, [])
        scenes[scope] += [i for i in MID_IDS if i not in scenes[scope]]
    for scope in AREA19_SCOPES:
        scenes.setdefault(scope, [])
        scenes[scope] += [i for i in AREA19_IDS if i not in scenes[scope]]
    for scope in ATTACHMENT_SCOPES:
        scenes[scope] = list(ATTACHMENT_IDS)
    if census:
        scope, ids = census_ids()
        scenes.setdefault(scope, [])
        scenes[scope] += [i for i in ids if i not in scenes[scope]]
    return scenes


def census_report(entries):
    """Which census ids the presets already carried and which the census
    added, with the state each resolves to under the census scope."""
    scope, ids = census_ids()
    presets = scene_ids(census=False)
    anywhere = {i for scene in presets.values() for i in scene}
    exported = {e['id']: e for e in entries
                if e['scope'] in ([-1, -1], list(scope))}
    # A preset id was already exported for this scope, or globally by
    # another scope's preset (a global entry serves every area).
    preset = {i for i in ids if i in presets.get(scope, []) or
              (i in anywhere and exported[i]['scope'] == [-1, -1])}
    rows = [dict(id=i, preset=i in preset, state=exported[i]['state'],
                 scope=exported[i]['scope'], reason=exported[i].get('reason'))
            for i in ids]
    return dict(scope=list(scope), ids=len(ids),
                added=[r['id'] for r in rows if not r['preset']], rows=rows)


def export(out_dir: Path):
    elf = Elf()
    bindings = area_bindings(elf)
    samples, entries = {}, []
    # Preserve the earlier scope order, then append AREA00 sub 0 and sub 1.
    # Registry entry rows still sort by scope/id after sample allocation.
    def scope_order(kv):
        scope = kv[0]
        return (7 if scope in AREA19_SCOPES else 6 if scope in ATTACHMENT_SCOPES else 5 if scope in MID_SCOPES else 4 if scope in AREA04_SCOPES
                else 3 if scope in AREA02_SCOPES
                else 2 if scope in AREA00_SCOPES else 1 if scope == ARRIVAL_SCOPE else 0,
                MID_SCOPES.index(scope) if scope in MID_SCOPES else scope)
    for (area, sub), ids in sorted(scene_ids().items(), key=scope_order):
        for sound_id in ids:
            entry = resolve(elf, bindings, sound_id, area, sub, samples)
            if entry['scope'] == [-1, -1]:
                if any(e['id'] == sound_id and e['scope'] == [-1, -1]
                       for e in entries):
                    continue
            entries.append(entry)
    entries.sort(key=lambda e: (e['scope'], e['id']))
    ordered = sorted(samples.values(), key=lambda s: s['index'])
    for sample in ordered:
        # Cross-check the loop-aware decoder against the shared decoder.
        packed = struct.pack(f"<{len(sample['pcm'])}h", *sample['pcm'])
        assert packed == A.decode_adpcm(sample['adpcm']), sample['offset']
    ladder = [elf.u16(A.PITCH_LADDER + 2 * i) for i in range(LADDER_COUNT)]
    # D_00242630, only in a registry with a pan slide (its size in the header's
    # last word; the other registries keep their bytes).
    pans = [] if not any(ev.get('op') == OP_PAN for e in entries for ev in e.get('events', ())) else \
        [elf.u16(A.PAN_TABLE + 2 * i) for i in range(PAN_COUNT)]
    blob = bytearray(struct.pack('<4sIIIII', b'EMSR', 2, len(ordered),
                                 len(entries), LADDER_COUNT, len(pans)))
    blob += struct.pack(f'<{LADDER_COUNT}H', *ladder)
    blob += struct.pack(f'<{len(pans)}H', *pans)
    for entry in entries:
        ops = [e for e in entry.get('events', []) if 'op' in e]
        # Voice +0x22 is the D_00281D50 handle; within one scene the
        # (group, bank) pair names it uniquely, which is all 00117428,
        # 001176E0 and 00118078 compare.
        bank = (entry['record'][0] << 8 | entry['record'][1]
                if entry['state'] == STATE_AUDIBLE else 0)
        blob += struct.pack('<IhhBBHHH', entry['id'], *entry['scope'],
                            entry['state'], len(ops),
                            REASONS.get(entry.get('reason'), 0), bank, 0)
        for op in ops:
            # Key-ons of a script with a volume slide (OP_VOLUME): the bytes
            # after `priority` hold the velocity level (voice +0x2C) and the
            # 40-bit product of the other five 001179E0 factors (bits 32..39,
            # then 0..31); every other key-on leaves them 0. OP_VOLUME keeps
            # its length / target where the portamento keeps length / depth.
            tail = 0
            if op['op'] == OP_KEY_ON and op.get('slide'):
                assert 0 < op['product'] < 1 << 40 and (op['product'] * op['level']) >> 27 == op['scalar']
                op = dict(op, length=op['level'], depth=op['product'] >> 32)
                tail = op['product'] & 0xFFFFFFFF
            blob += struct.pack(
                '<HBBBBHHHIHHBbBBBBBBI', op['tick'], op['op'], op.get('note', 0),
                op.get('prog', 0), op.get('flags', 0), op.get('sample', 0),
                op.get('pitch', 0), op.get('pan', 0), op.get('scalar', 0),
                op.get('adsr1', 0), op.get('adsr2', 0), op.get('center', 0),
                op.get('fine', 0), op.get('range', 0), op.get('alloc', 0),
                op.get('priority', 0), op.get('length', 0), op.get('depth', 0),
                op.get('pan_level', 0) if op.get('pan_slide') else 0, tail)
    for sample in ordered:
        loop = sample['loop_start']
        blob += struct.pack('<II', len(sample['pcm']),
                            0xFFFFFFFF if loop is None else loop)
        blob += struct.pack(f"<{len(sample['pcm'])}h", *sample['pcm'])
        if loop is not None:
            assert len(sample['body']) == len(sample['pcm']) - loop
            blob += struct.pack(f"<{len(sample['body'])}h", *sample['body'])
    out_dir.mkdir(parents=True, exist_ok=True)
    registry = out_dir / 'sfx_registry.emsr'
    registry.write_bytes(bytes(blob))
    report = dict(
        elf_sha256=ELF_SHA, format='EMSR v2 (see docs/SFX_PITCH.md)',
        sequencer_tick=f'{A.SEQ_TICK} (0x1E0000/60), 8 delta units per tick',
        bindings={f'{a}.{s}': b['binding'] for (a, s), b in bindings.items()},
        refused_slots={f'{a}.{s}': {f'{g}.{i}': why for (g, i), why
                                    in b.get('refused', {}).items()}
                       for (a, s), b in bindings.items() if b.get('refused')},
        ladder=dict(address=A.PITCH_LADDER, count=LADDER_COUNT),
        samples=[dict(index=s['index'], container=s['container'],
                      offset=s['offset'], adpcm_bytes=len(s['adpcm']),
                      adpcm_sha256=hashlib.sha256(s['adpcm']).hexdigest(),
                      source_frames=len(s['pcm']), loop_start=s['loop_start'])
                 for s in ordered],
        entries=entries,
        census=census_report(entries),
        registry_sha256=hashlib.sha256(bytes(blob)).hexdigest(),
        boundaries=['SPU2 ADSR stepping is a documented-semantics hardware model',
                    'reverb routing (all SFX tracks set the effect mask) is dry',
                    'linear interpolation, not SPU2 Gaussian',
                    'sequencer tick converted at the NTSC field rate 60000/1001'])
    (out_dir / 'sfx_registry.json').write_text(json.dumps(report, indent=1) + '\n')
    counts = {state: sum(e['state'] == state for e in entries)
              for state in (STATE_AUDIBLE, STATE_ABSENT, STATE_UNSUPPORTED)}
    print(f'Exported {len(entries)} registry entries ({counts[1]} audible, '
          f'{counts[2]} originally absent, {counts[3]} unsupported), '
          f'{len(ordered)} samples -> {registry}')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--out', type=Path, default=ROOT / 'assets/sfx')
    export(parser.parse_args().out.resolve())
