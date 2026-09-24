#!/usr/bin/env python3
"""Export the player's full animation bank, chained clips included.

docs/PLAYER_CLIPS.md. Source: the player's clip bank on the user's own disc
(../Extermination/extract/chunk28/f01_id3c.bin, generated locally by the
decomp's extractor). Nothing here embeds original data.

Outputs (ignored, never committed):
  assets/player_clips_full.bank   the raw bank bytes, exactly as the original
                                  holds them at the player's +40 (EE 0xD689C0);
                                  the region em_pose_host_workers maps.
  assets/player_clips_full.empx   decoded keys for every exported clip, with the
                                  header's +4 follow-on link, its +6 halfword and
                                  the +14 event table (em_pose_chain.c loads it).
  build/player_clips_full/export.json   hashes, per-clip header facts, the
                                  first-level clip list and every check made.

Verification, before anything is written:
  * the bank file equals the RAM at the player's +40 byte for byte in every
    captured AREA11 image that has the player record (the playable image, the
    panel/elevator/status captures and route beats 00..15 when present);
  * each exported clip's source extent (header, parents, the three section
    directories and every key stream, the event table) lies inside the bank,
    so the byte-for-byte bank check covers it;
  * every clip id the route traces put in +20C is in FIRST_LEVEL (a new id
    fails the export until PLAYER_CLIPS.md records its evidence), and
    FIRST_LEVEL is closed under the bank's follow-on links;
  * the 14 clips shared with assets/player_channels.empc (when present)
    decode to identical key payloads.

--first-level writes only FIRST_LEVEL (plus its chain closure) instead of the
whole bank. The default is the whole bank (459 clips, about 4 MB decoded):
reachability of every clip a translated state can request is not proven for
the first level, and a missing clip is a fail-stop fault at run time.

EMPX v1 layout (little endian):
  'EMPX', u32 version 1, u32 bone_count, u32 clip_count, u32 bank_clip_count,
  i32 parents[bone_count];
  per clip: u16 id, u16 frames (+2), i16 next (+4), i16 half6 (+6),
            u32 event_count, event_count x (i16 frame, u16 flags),
            then per bone, per channel (rotation, translation, scale):
            u32 key_count, key_count x (u16 time, u16 hold, f32 value[4]).
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
BANK_ADDRESS = 0xD689C0
PLAYER = 0x8102B0

# The clips the first level plays or can chain into (docs/PLAYER_CLIPS.md has
# the evidence for each id). Route: +20C over the route beats. Chain: the
# bank's +4 link of a route clip. Prior: the opening/pickup/door/fidget clips
# of assets/player_channels.empc (PLAYER_POSE.md, 00161020's fidget request).
ROUTE_CLIPS = (0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x42, 0x45, 0x47, 0x15C,
               0x5E, 0x60, 0x61, 0x65, 0x69, 0x6B, 0x6E, 0x70, 0x73, 0x77, 0x78, 0x79,
               0x8C, 0xE3, 0xE6, 0xE8, 0xEA, 0xF0, 0x156, 0x164, 0x165, 0x166)
EXIT_CLIPS = (0x4B, 0x4D, 0x4E)                     # route beat 15 (level exit, opt-in)
CHAIN_CLIPS = (0x5F, 0x72)                          # +4 links of 0x5E and 0x73
PRIOR_CLIPS = (0x40, 0x41, 0x43, 0x15D)
FIRST_LEVEL = tuple(sorted(set(ROUTE_CLIPS + EXIT_CLIPS + CHAIN_CLIPS + PRIOR_CLIPS)))


def u16(data, at):
    return struct.unpack_from('<H', data, at)[0]


def u32(data, at):
    return struct.unpack_from('<I', data, at)[0]


def header_of(bank, clip):
    """001C6120's rule: the directory word at 4 + 4 * (clip & 0x7FFF), rounded
    down to a word, is the header's offset in the bank."""
    return (struct.unpack_from('<i', bank, 4 + 4 * (clip & 0x7FFF))[0] >> 2) << 2


def clip_facts(bank, clip):
    at = header_of(bank, clip)
    bones, frames, nxt, half6 = struct.unpack_from('<HHhh', bank, at)
    sections = struct.unpack_from('<III', bank, at + 8)
    events = u32(bank, at + 0x14)
    pairs = []
    if events:
        count = struct.unpack_from('<h', bank, at + events)[0]
        pairs = [struct.unpack_from('<hH', bank, at + events + 4 + 4 * i) for i in range(max(count, 0))]
    return dict(header=at, bones=bones, frames=frames, next=nxt, half6=half6,
                sections=sections, events=events, event_pairs=pairs)


def clip_extent(bank, clip, facts):
    """Every byte range the original reads for this clip (the key walks read
    12-byte records until the 0xFFFF sentinel)."""
    at, ranges = facts['header'], []
    ranges.append((at, at + 0x20 + 4 * facts['bones']))
    for section in facts['sections']:
        table = at + section
        ranges.append((table, table + 4 * facts['bones']))
        for bone in range(facts['bones']):
            record = table + u32(bank, table + 4 * bone)
            while True:
                if record + 12 > len(bank):
                    raise ValueError(f'clip {clip:#x}: key stream leaves the bank')
                ranges.append((record, record + 12))
                if u16(bank, record + 10) == 0xFFFF:
                    break
                record += 12
    if facts['events']:
        ranges.append((at + facts['events'], at + facts['events'] + 4 + 4 * len(facts['event_pairs'])))
    for lo, hi in ranges:
        if lo < 0 or hi > len(bank):
            raise ValueError(f'clip {clip:#x}: extent outside the bank')
    return ranges


def captures(decomp):
    """Captured AREA11 RAM images whose player record points at the bank."""
    ref = decomp / 'build/startup-reference'
    paths = [ref / 'playable_ee.bin', ref / 'handoff_ee.bin', ref / 'panel/eeMemory.bin',
             ref / 'panel/animation_ee.bin', ref / 'elevator/clip47_ee.bin',
             ref / 'elevator/completed_ee.bin', ref / 'status-hub/eeMemory.bin']
    route = decomp / 'build/s87/route'
    if route.exists():
        paths += sorted(p / 'eeMemory.bin' for p in route.iterdir() if p.name[:2].isdigit())
    return [p for p in paths if p.exists()]


def route_clip_ids(decomp):
    """+20C over every route trace row (and each beat's final +2C)."""
    route = decomp / 'build/s87/route'
    seen = {}
    if not route.exists():
        return seen
    for beat in sorted(p for p in route.iterdir() if p.name[:2].isdigit()):
        trace = beat / 'trace.json'
        if not trace.exists():
            continue
        for row in json.loads(trace.read_text())['rows']:
            seen.setdefault(row['clip'] & 0xFFFF, set()).add(beat.name)
        ram = beat / 'eeMemory.bin'
        if ram.exists():
            with open(ram, 'rb') as f:
                f.seek(PLAYER + 0x2C)
                seen.setdefault(u16(f.read(2), 0) & 0x7FFF, set()).add(beat.name + ':+2C')
    return seen


def decoded_payload(bank, clip, facts, parents_out, OpeningClip):
    decoded = OpeningClip(bank, facts['header'])
    if parents_out[0] is None:
        parents_out[0] = decoded.parents
    if decoded.parents != parents_out[0]:
        raise ValueError(f'clip {clip:#x}: hierarchy differs from the bank\'s first clip')
    out = bytearray(struct.pack('<HHhhI', clip, facts['frames'], facts['next'], facts['half6'],
                                len(facts['event_pairs'])))
    for frame, flags in facts['event_pairs']:
        out += struct.pack('<hH', frame, flags)
    tracks = bytearray()
    keys = 0
    for bone in range(facts['bones']):
        for channel in (decoded.rotation[bone], decoded.translation[bone], decoded.scale[bone]):
            ks = channel.keys
            if ks[0][0] != 0 or ks[-1][0] != 0xFFFF or len(ks) < 2 or len(ks) > 4096:
                raise ValueError(f'clip {clip:#x}: key stream shape')
            tracks += struct.pack('<I', len(ks))
            for time, values, hold in ks:
                values = tuple(values) + (0.0,) * (4 - len(values))
                tracks += struct.pack('<HH4f', time, int(hold), *values)
            keys += len(ks)
    return bytes(out), bytes(tracks), keys


def build_empx(bank, clips, OpeningClip, events_override=None):
    """The EMPX bytes for `clips` (ids); events_override {clip: [(frame, flags)]}
    replaces a clip's event list (the chain oracle's synthetic event case)."""
    parents = [None]
    body, report = bytearray(), []
    for clip in clips:
        facts = clip_facts(bank, clip)
        if facts['bones'] != 21:
            raise ValueError(f'clip {clip:#x}: {facts["bones"]} nodes')
        if events_override and clip in events_override:
            facts = dict(facts, event_pairs=list(events_override[clip]))
        head, tracks, keys = decoded_payload(bank, clip, facts, parents, OpeningClip)
        body += head + tracks
        report.append(dict(clip=clip, frames=facts['frames'], next=facts['next'], half6=facts['half6'],
                           events=len(facts['event_pairs']), keys=keys, header=facts['header'],
                           tracks_sha256=hashlib.sha256(tracks).hexdigest()))
    head = struct.pack('<4sIIII', b'EMPX', 1, 21, len(clips), u32(bank, 0))
    return head + struct.pack('<21i', *parents[0]) + bytes(body), report


def empc_payloads(path):
    """clip id -> key payload of the existing EMPC (for the cross-check)."""
    data = path.read_bytes()
    magic, version, bones, count = struct.unpack_from('<4sIII', data, 0)
    assert magic == b'EMPC' and version == 1
    at, out = 16 + 4 * bones, {}
    for _ in range(count):
        cid = u16(data, at)
        start = at + 8
        at = start
        for _ in range(bones * 3):
            n = u32(data, at)
            at += 4 + 20 * n
        out[cid] = data[start:at]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--decomp', type=Path, default=ROOT.parent / 'Extermination')
    ap.add_argument('--output', type=Path, default=ROOT / 'assets/player_clips_full.empx')
    ap.add_argument('--raw-output', type=Path, default=ROOT / 'assets/player_clips_full.bank')
    ap.add_argument('--first-level', action='store_true', help='export only the first-level clips')
    ap.add_argument('--no-captures', action='store_true',
                    help='skip the captured-RAM checks (refused unless no capture exists)')
    args = ap.parse_args()
    sys.path.insert(0, str(args.decomp / 'tools'))
    from export_opening_actors import OpeningClip

    bank = (args.decomp / 'extract/chunk28/f01_id3c.bin').read_bytes()
    count = u32(bank, 0)
    checks = {}

    # 1. The bank is the RAM image at the player's +40, byte for byte.
    images = captures(args.decomp)
    if not images and not args.no_captures:
        sys.exit('no captured RAM image found; pass --no-captures to export unverified')
    verified = []
    for path in images:
        with open(path, 'rb') as f:
            f.seek(PLAYER + 0x40)
            address = u32(f.read(4), 0)
            if address != BANK_ADDRESS:
                continue
            f.seek(address)
            ram = f.read(len(bank))
        if ram != bank:
            first = next(i for i in range(len(bank)) if ram[i] != bank[i])
            sys.exit(f'{path}: RAM differs from the disc bank at +{first:#x}')
        verified.append(str(path.relative_to(args.decomp)))
    if images and not verified:
        sys.exit('no capture has the player bank at 0xD689C0')
    checks['bank_equals_ram'] = verified

    # 2. Every exported clip's extent lies in the verified bytes; links resolve.
    facts = {c: clip_facts(bank, c) for c in range(count)}
    for clip, f in facts.items():
        clip_extent(bank, clip, f)
        if f['next'] not in (-1, -2) and not 0 <= f['next'] < count:
            sys.exit(f'clip {clip:#x}: follow-on {f["next"]} outside the bank')
    chained = {c: f['next'] for c, f in facts.items() if f['next'] >= 0}
    checks['chained'] = {f'{c:#x}': f'{n:#x}' for c, n in sorted(chained.items())}
    checks['event_tables'] = [f'{c:#x}' for c, f in facts.items() if f['events']]

    # 3. The first-level list covers the route and is chain-closed.
    seen = route_clip_ids(args.decomp)
    missing = sorted(c for c in seen if c not in FIRST_LEVEL)
    if missing:
        sys.exit('route clips without evidence in PLAYER_CLIPS.md: ' +
                 ', '.join(f'{c:#x} ({sorted(seen[c])})' for c in missing))
    open_links = sorted(c for c in FIRST_LEVEL if c in chained and chained[c] not in FIRST_LEVEL)
    if open_links:
        sys.exit(f'first-level list not chain-closed: {[hex(c) for c in open_links]}')
    checks['route_clips'] = {f'{c:#x}': sorted(b) for c, b in sorted(seen.items())}
    checks['first_level'] = [f'{c:#x}' for c in FIRST_LEVEL]

    clips = list(FIRST_LEVEL) if args.first_level else list(range(count))
    payload, report = build_empx(bank, clips, OpeningClip)

    # 4. The shared clips decode exactly as the existing EMPC export.
    empc = ROOT / 'assets/player_channels.empc'
    if empc.exists():
        old = empc_payloads(empc)
        by_id = {r['clip']: r for r in report}
        same = []
        for cid, data in old.items():
            if cid in by_id:
                if hashlib.sha256(data).hexdigest() != by_id[cid]['tracks_sha256']:
                    sys.exit(f'clip {cid:#x} decodes differently from assets/player_channels.empc')
                same.append(f'{cid:#x}')
        checks['same_as_player_channels_empc'] = same

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(payload)
    args.raw_output.write_bytes(bank)
    out = ROOT / 'build/player_clips_full'
    out.mkdir(parents=True, exist_ok=True)
    (out / 'export.json').write_text(json.dumps({
        'source': 'extract/chunk28/f01_id3c.bin', 'source_sha256': hashlib.sha256(bank).hexdigest(),
        'bank_address': f'{BANK_ADDRESS:#x}', 'bank_clip_count': count,
        'empx_sha256': hashlib.sha256(payload).hexdigest(), 'empx_bytes': len(payload),
        'exported_clips': len(clips), 'checks': checks,
        'clips': [dict(r, clip=f'{r["clip"]:#x}', header=f'{r["header"]:#x}') for r in report]},
        indent=1) + '\n')
    print(f'player clips: {len(clips)} clips ({len(payload):,} bytes) -> {args.output}; '
          f'raw bank {len(bank):,} bytes -> {args.raw_output}; '
          f'bank == RAM in {len(verified)} captures; chained {len(chained)}; '
          f'route ids {len(seen)} all in the first-level list of {len(FIRST_LEVEL)}')


if __name__ == '__main__':
    main()
