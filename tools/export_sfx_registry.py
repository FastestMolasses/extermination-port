#!/usr/bin/env python3
"""Export the native SFX registry through the original A0 trigger path.

Every id the port registry carries (tools/gen_sfx_registry.py presets in the
decomp repo, plus the AREA11 panel pair) is resolved per scene area exactly
as the original does it:

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
                      binding='AREA11 captures (D_00281D50/D_0027C6C0)'),
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
    match = A.match_area_regions(area_records, candidates)[(2, 1)]
    office = regions[match['region']][1]
    bindings[(2, 1)] = dict(groups={1: global_rows[1], 2: office.get(2, []),
                                    4: office.get(4, [])},
                            binding=f"coverage match {match['region']} "
                                    f"(coverage {match['coverage']}, "
                                    f"exact {match['exact']}); no capture")
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
      FF 2F 00           00117C28 end of track (no delta)
    Anything else (other controllers, 0x80/0x90/0xC0/0xE0, tempo) is refused.
    """
    events, running, acc, tick = [], None, 0, 0
    end = min(position + limit, len(data))
    while position < end:
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
        elif running & 0xF0 == 0xB0 and data[position] == 0x41:
            _, length, depth, prog, note = data[position:position + 5]
            position += 5
            events.append(dict(kind='portamento', tick=tick, note=note, prog=prog,
                               length=length, depth=depth))
        elif running & 0xF0 == 0xB0:
            raise Unsupported('script', f'unsupported controller 0x{data[position]:02X}')
        else:
            raise Unsupported('script', f'unsupported status 0x{running:02X}')
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


OP_KEY_ON, OP_KEY_OFF, OP_PORTAMENTO, OP_END = 1, 2, 3, 4
OP_MAX = 32
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
        banks = bindings[(area, sub)]['groups'].get(group, [])
        if not 0 <= bank_index < len(banks):
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
        for event in events:
            if event['kind'] == 'end':
                out.append(dict(kind='end', op=OP_END, tick=event['tick']))
                continue
            if event['kind'] == 'portamento':
                out.append(dict(event, op=OP_PORTAMENTO))
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
            scalar = (channel[14] * channel[3] * tone[11] * velocity *
                      bank.u8(program + 1) * master) >> 27
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
                            adsr1=struct.unpack_from('<H', tone, 6)[0],
                            adsr2=struct.unpack_from('<H', tone, 8)[0],
                            flags=flags, reverb=bool(flags & 0x80),
                            sustained=bool(flags & 0x01),
                            tone_sample=tone_sample, sample=samples[key]['index'],
                            loops=loop_block is not None,
                            unit_words=[left, right]))
        if not [e for e in out if e.get('op') == OP_KEY_ON]:
            raise Unsupported('no voice')
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


def scene_ids():
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
    return scenes


def export(out_dir: Path):
    elf = Elf()
    bindings = area_bindings(elf)
    samples, entries = {}, []
    for (area, sub), ids in sorted(scene_ids().items()):
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
    blob = bytearray(struct.pack('<4sIIIII', b'EMSR', 2, len(ordered),
                                 len(entries), LADDER_COUNT, 0))
    blob += struct.pack(f'<{LADDER_COUNT}H', *ladder)
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
            blob += struct.pack(
                '<HBBBBHHHIHHBbBBBBBBI', op['tick'], op['op'], op.get('note', 0),
                op.get('prog', 0), op.get('flags', 0), op.get('sample', 0),
                op.get('pitch', 0), op.get('pan', 0), op.get('scalar', 0),
                op.get('adsr1', 0), op.get('adsr2', 0), op.get('center', 0),
                op.get('fine', 0), op.get('range', 0), op.get('alloc', 0),
                op.get('priority', 0), op.get('length', 0), op.get('depth', 0),
                0, 0)
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
        ladder=dict(address=A.PITCH_LADDER, count=LADDER_COUNT),
        samples=[dict(index=s['index'], container=s['container'],
                      offset=s['offset'], adpcm_bytes=len(s['adpcm']),
                      adpcm_sha256=hashlib.sha256(s['adpcm']).hexdigest(),
                      source_frames=len(s['pcm']), loop_start=s['loop_start'])
                 for s in ordered],
        entries=entries,
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
