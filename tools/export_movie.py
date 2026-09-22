#!/usr/bin/env python3
"""Losslessly remux a user's local PS2 PSS movie for native OS playback.

Only Python's standard library is required. MPEG-2 access units are copied
verbatim; Sony private-stream PCM16 channel blocks are interleaved without
resampling. The QuickTime file carries both tracks and original presentation
timestamps. No copyrighted media or tables are embedded in this tool.
"""
from __future__ import annotations

import argparse
import bisect
import hashlib
import json
from pathlib import Path
import re
import struct


def be(*values: int) -> bytes:
    return struct.pack('>' + 'I' * len(values), *values)


def atom(kind: bytes, payload: bytes) -> bytes:
    return be(8 + len(payload)) + kind + payload


def full(kind: bytes, payload: bytes, flags: int = 0) -> bytes:
    return atom(kind, be(flags) + payload)


def timestamp(data: bytes) -> int:
    if len(data) != 5 or any(not data[i] & 1 for i in (0, 2, 4)):
        raise ValueError('invalid PES timestamp')
    return ((data[0] >> 1 & 7) << 30 | data[1] << 22 |
            (data[2] >> 1) << 15 | data[3] << 7 | data[4] >> 1)


def demux(data: bytes) -> tuple[bytes, list[tuple[int, int, int | None]], bytes, int]:
    """Return elementary video, PES timestamps, Sony audio, first audio PTS."""
    video, audio = bytearray(), bytearray()
    times = []
    audio_pts = None
    pos = 0
    while pos < len(data):
        if len(data) - pos < 4:
            raise ValueError('truncated program-stream start code')
        if data[pos:pos + 3] != b'\0\0\1':
            raise ValueError(f'expected program-stream start code at {pos:#x}')
        sid = data[pos + 3]
        if sid == 0xB9:
            break
        if sid == 0xBA:
            if len(data) < pos + 14 or data[pos + 4] & 0xC0 != 0x40:
                raise ValueError('only MPEG-2 program-stream pack headers are supported')
            pos += 14 + (data[pos + 13] & 7)
            continue
        if len(data) < pos + 6:
            raise ValueError('truncated PES header')
        size = int.from_bytes(data[pos + 4:pos + 6], 'big')
        end = pos + 6 + size
        if end > len(data):
            raise ValueError('truncated PES packet')
        if sid in (0xE0, 0xBD):
            if size < 3 or data[pos + 6] & 0xC0 != 0x80:
                raise ValueError('invalid MPEG-2 PES header')
            body = pos + 9 + data[pos + 8]
            if body > end:
                raise ValueError('PES optional header exceeds packet')
            flags = data[pos + 7] >> 6
            pts = timestamp(data[pos + 9:pos + 14]) if flags & 2 else None
            dts = timestamp(data[pos + 14:pos + 19]) if flags == 3 else None
            if sid == 0xE0:
                if pts is not None:
                    times.append((len(video), pts, dts))
                video.extend(data[body:end])
            else:
                # SCUS-97112's private stream carries ff/a0/00/00 before
                # EVERY payload, including continuations of channel blocks.
                if data[body:body + 4] != b'\xff\xa0\0\0':
                    raise ValueError('unsupported PSS private audio substream')
                if audio_pts is None and pts is not None:
                    audio_pts = pts
                audio.extend(data[body + 4:end])
        pos = end
    if not video or not times or not audio or audio_pts is None:
        raise ValueError('movie must contain timestamped MPEG-2 video and PSS PCM audio')
    return bytes(video), times, bytes(audio), audio_pts


def pcm_audio(audio: bytes) -> tuple[bytes, int, int]:
    """Reinterleave little-endian PCM16, preserving every source sample."""
    if len(audio) < 40:
        raise ValueError('truncated Sony audio header')
    magic, hlen, codec, rate, channels, block, _, _, body, size = struct.unpack_from('<4s7I4sI', audio)
    if (magic, hlen, codec, channels, body) != (b'SShd', 24, 1, 2, b'SSbd'):
        raise ValueError('unsupported Sony audio format (requires stereo PCM16)')
    if not rate or not block or block % 2 or size != len(audio) - 40 or size % (2 * block):
        raise ValueError('invalid Sony PCM size or channel-block alignment')
    raw = memoryview(audio)[40:]
    out = bytearray(size)
    # Channel blocks are in BYTES; each pair contains block/2 stereo frames.
    for start in range(0, size, 2 * block):
        for byte in (0, 1):
            out[start + byte:start + 2 * block:4] = raw[start + byte:start + block:2]
            out[start + 2 + byte:start + 2 * block:4] = raw[start + block + byte:start + 2 * block:2]
    return bytes(out), rate, channels


def video_samples(video: bytes, times: list[tuple[int, int, int | None]]) -> dict:
    pictures, starts, refs, groups, sync = [], [], [], [], []
    prefix, group = None, -1
    seq = None
    for match in re.finditer(b'\x00\x00\x01(.)', video, re.DOTALL):
        pos, kind = match.start(), match[1][0]
        if kind in (0xB3, 0xB8):
            if prefix is None:
                prefix = pos
            if kind == 0xB3 and seq is None:
                seq = pos
            if kind == 0xB8:
                group += 1
        elif kind == 0:
            if pos + 6 > len(video):
                raise ValueError('truncated MPEG picture header')
            header = int.from_bytes(video[pos + 4:pos + 6], 'big')
            pictures.append(pos)
            starts.append(prefix if prefix is not None else pos)
            refs.append(header >> 6)
            groups.append(group)
            if header >> 3 & 7 == 1:
                sync.append(len(pictures))
            prefix = None
    if seq is None or not pictures or starts[0] != 0:
        raise ValueError('video must start with an MPEG sequence header')
    dims = int.from_bytes(video[seq + 4:seq + 7], 'big')
    width, height = dims >> 12, dims & 0xFFF
    rate_code = video[seq + 7] & 15
    rates = {1: (24000, 1001), 2: (24, 1), 3: (25, 1), 4: (30000, 1001),
             5: (30, 1), 6: (50, 1), 7: (60000, 1001), 8: (60, 1)}
    if rate_code not in rates:
        raise ValueError('unknown MPEG frame rate')
    numerator, denominator = rates[rate_code]
    if 90000 * denominator % numerator:
        raise ValueError('frame period is not an integer in the PSS 90-kHz clock')
    tick = 90000 * denominator // numerator
    pts, dts = {}, {}
    bases = {}
    for offset, presentation, decode in times:
        index = bisect.bisect_left(pictures, offset)
        if index >= len(pictures) or index in pts:
            raise ValueError('ambiguous PES timestamp-to-picture association')
        pts[index] = presentation
        if decode is not None:
            dts[index] = decode
        base = presentation - refs[index] * tick
        if groups[index] in bases and bases[groups[index]] != base:
            raise ValueError('variable MPEG timing requires explicit handling')
        bases[groups[index]] = base
    # One E900 B picture shares an un-timestamped PES. Its GOP temporal
    # reference supplies PTS; verify the same relation for every known PTS.
    for index in range(len(pictures)):
        if index not in pts:
            if groups[index] not in bases:
                raise ValueError('GOP has no timestamp anchor')
            pts[index] = bases[groups[index]] + refs[index] * tick
    if not dts:
        raise ValueError('MPEG stream has no decode timestamp anchor')
    anchor = min(dts)
    first_dts = dts[anchor] - anchor * tick
    if any(value != first_dts + index * tick for index, value in dts.items()):
        raise ValueError('nonuniform decode timing')
    offsets = [pts[i] - first_dts - i * tick for i in range(len(pictures))]
    if min(offsets) < 0:
        raise ValueError('negative composition offsets unsupported')
    config_end = next((m.start() for m in re.finditer(b'\x00\x00\x01[\x00\xb8]', video)
                       if m.start() > seq), pictures[0])
    return dict(width=width, height=height, tick=tick, starts=starts,
                sizes=[b - a for a, b in zip(starts, starts[1:] + [len(video)])],
                offsets=offsets, sync=sync, first_pts=min(pts.values()),
                first_dts=first_dts, duration=max(pts.values()) + tick - min(pts.values()),
                # Sequence+extensions preceding the first GOP/picture.
                config=video[seq:config_end])


MATRIX = be(0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000)


def runs(values: list[int]) -> bytes:
    pairs = []
    for value in values:
        if pairs and pairs[-1][1] == value:
            pairs[-1][0] += 1
        else:
            pairs.append([1, value])
    return be(len(pairs)) + b''.join(be(*pair) for pair in pairs)


def track(track_id: int, timescale: int, duration: int, movie_duration: int,
          media_start: int, entry: bytes, sizes: list[int] | tuple[int, int],
          tick: int, offset: int, width: int = 0, height: int = 0,
          composition: list[int] | None = None, sync: list[int] | None = None) -> bytes:
    is_video = width != 0
    kind = b'vide' if is_video else b'soun'
    count = len(sizes) if isinstance(sizes, list) else sizes[1]
    tkhd = full(b'tkhd', be(0, 0, track_id, 0, movie_duration) + b'\0' * 8 +
                struct.pack('>4H', 0, 0, 0 if is_video else 0x100, 0) + MATRIX +
                be(width << 16, height << 16), 3)
    elst = atom(b'edts', full(b'elst', be(1, movie_duration, media_start) + struct.pack('>hh', 1, 0)))
    mdhd = full(b'mdhd', be(0, 0, timescale, duration) + struct.pack('>HH', 0, 0))
    hdlr = full(b'hdlr', be(0) + kind + b'\0' * 12 + (b'Video\0' if is_video else b'Audio\0'))
    media_header = full(b'vmhd', b'\0' * 8, 1) if is_video else full(b'smhd', b'\0' * 4)
    dinf = atom(b'dinf', full(b'dref', be(1) + full(b'url ', b'', 1)))
    stsz = (be(0, count) + b''.join(be(n) for n in sizes) if isinstance(sizes, list)
            else be(*sizes))
    tables = (full(b'stsd', be(1) + entry) + full(b'stts', be(1, count, tick)) +
              full(b'stsc', be(1, 1, count, 1)) + full(b'stsz', stsz) + full(b'stco', be(1, offset)))
    if composition is not None:
        tables += full(b'ctts', runs(composition))
    if sync is not None:
        tables += full(b'stss', be(len(sync)) + b''.join(be(n) for n in sync))
    minf = atom(b'minf', media_header + dinf + atom(b'stbl', tables))
    return atom(b'trak', tkhd + elst + atom(b'mdia', mdhd + hdlr + minf))


def remux(data: bytes, output: Path) -> dict:
    video, times, audio, audio_pts = demux(data)
    info = video_samples(video, times)
    pcm, rate, channels = pcm_audio(audio)
    if audio_pts != info['first_pts']:
        raise ValueError('audio/video start mismatch requires a separate edit offset')
    w, h = info['width'], info['height']
    # Standard QuickTime visual sample entry. glbl retains the MPEG-2
    # sequence headers; the same bytes also remain in the first sample.
    visual = (b'\0' * 6 + struct.pack('>H', 1) + b'\0' * 16 +
              struct.pack('>HH', w, h) + be(0x480000, 0x480000, 0) +
              struct.pack('>H', 1) + b'\0' * 32 + struct.pack('>Hh', 24, -1))
    ventry = atom(b'm2v1', visual + atom(b'glbl', info['config']))
    aentry = atom(b'sowt', b'\0' * 6 + struct.pack('>H', 1) + b'\0' * 8 +
                  struct.pack('>4H', channels, 16, 0, 0) + be(rate << 16))
    ftyp = atom(b'ftyp', b'qt  ' + be(0) + b'qt  ')
    video_offset = len(ftyp) + 8
    audio_offset = video_offset + len(video)
    frames = len(info['sizes'])
    # Preserve all PCM, including authored trailing padding. The playback
    # backend ends at the video track's end, matching the MPEG drain gate.
    audio_duration = len(pcm) // (channels * 2)
    audio_movie_duration = (audio_duration * 90000 + rate - 1) // rate
    duration = max(info['duration'], audio_movie_duration)
    mvhd = full(b'mvhd', be(0, 0, 90000, duration, 0x10000) + struct.pack('>H', 0x100) +
                b'\0' * 10 + MATRIX + b'\0' * 24 + be(3))
    video_track = track(1, 90000, frames * info['tick'], info['duration'],
                        info['first_pts'] - info['first_dts'], ventry, info['sizes'],
                        info['tick'], video_offset, w, h, info['offsets'], info['sync'])
    audio_track = track(2, rate, audio_duration, audio_movie_duration, 0,
                        aentry, (channels * 2, audio_duration), 1, audio_offset)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('wb') as file:
        file.write(ftyp)
        file.write(be(8 + len(video) + len(pcm)) + b'mdat')
        file.write(video)
        file.write(pcm)
        file.write(atom(b'moov', mvhd + video_track + audio_track))
    return dict(width=w, height=h, frames=frames, video_seconds=info['duration'] / 90000,
                audio_seconds=audio_duration / rate, audio_rate=rate, channels=channels,
                video_sha256=hashlib.sha256(video).hexdigest(),
                pcm_sha256=hashlib.sha256(pcm).hexdigest(),
                source_sha256=hashlib.sha256(data).hexdigest())


def iso_file(path: Path, name: str) -> bytes:
    """Read one ordinary ISO9660 file without mounting or modifying the disc."""
    with path.open('rb') as file:
        file.seek(16 * 2048)
        pvd = file.read(2048)
        if pvd[:7] != b'\x01CD001\x01':
            raise ValueError('missing ISO9660 primary volume descriptor')
        record = pvd[156:]
        extent, length = struct.unpack_from('<I', record, 2)[0], struct.unpack_from('<I', record, 10)[0]
        parts = [p.upper() for p in name.strip('/').split('/')]
        for component in parts:
            file.seek(extent * 2048)
            directory = file.read(length)
            pos, found = 0, None
            while pos < len(directory):
                size = directory[pos]
                if not size:
                    pos = (pos // 2048 + 1) * 2048
                    continue
                item = directory[pos:pos + size]
                if len(item) < 34:
                    raise ValueError('truncated ISO directory record')
                identifier = item[33:33 + item[32]].decode('ascii')
                if identifier.split(';')[0] == component.split(';')[0]:
                    found = item
                    break
                pos += size
            if found is None:
                raise ValueError(f'{component} not found in ISO')
            extent, length = struct.unpack_from('<I', found, 2)[0], struct.unpack_from('<I', found, 10)[0]
        file.seek(extent * 2048)
        data = file.read(length)
        if len(data) != length:
            raise ValueError('truncated ISO file extent')
        return data


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--pss', type=Path, help='user-extracted PSS movie')
    source.add_argument('--iso', type=Path, help='user-supplied ISO9660 disc image')
    parser.add_argument('--disc-path', default='/MOVIE/E900.PSS', help='movie path inside ISO')
    parser.add_argument('--out', required=True, type=Path, help='local ignored .mov asset')
    args = parser.parse_args()
    try:
        data = args.pss.read_bytes() if args.pss else iso_file(args.iso, args.disc_path)
        print(json.dumps(remux(data, args.out), indent=2))
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f'error: {error}\n')


if __name__ == '__main__':
    main()
