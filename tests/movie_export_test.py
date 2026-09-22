#!/usr/bin/env python3
"""Synthetic PSS coverage: no game assets are needed or embedded."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('export_movie', Path(__file__).parents[1] / 'tools/export_movie.py')
movie = importlib.util.module_from_spec(spec)
spec.loader.exec_module(movie)


def pts(value, prefix=2):
    return bytes([(prefix << 4) | ((value >> 29) & 14) | 1,
                  value >> 22 & 255, (value >> 14 & 254) | 1,
                  value >> 7 & 255, (value << 1 & 254) | 1])


def pes(kind, payload, presentation=None, decode=None):
    optional = (pts(presentation, 3 if decode is not None else 2)
                if presentation is not None else b'')
    if decode is not None:
        optional += pts(decode, 1)
    flags = 0xC0 if decode is not None else 0x80 if presentation is not None else 0
    body = bytes([0x80, flags, len(optional)]) + optional + payload
    return b'\0\0\1' + bytes([kind]) + len(body).to_bytes(2, 'big') + body


def picture(reference, kind):
    return b'\0\0\1\0' + ((reference << 6) | kind << 3).to_bytes(2, 'big') + b'\0\0'


def fixture():
    # Three decode-order pictures: I(ref2), B(ref0), B(ref1). These
    # deliberately are only parser fixtures, not decodable MPEG pictures.
    sequence = b'\0\0\1\xb3' + ((32 << 12) | 16).to_bytes(3, 'big') + b'\x14' + b'\0' * 4
    gop = b'\0\0\1\xb8' + b'\0' * 4
    video = sequence + gop + picture(2, 1) + picture(0, 3) + picture(1, 3)
    pcm_block = struct.pack('<4h', 1, -2, 300, -400)
    audio = struct.pack('<4s7I4sI', b'SShd', 24, 1, 48000, 2, 4,
                        0xFFFFFFFF, 0xFFFFFFFF, b'SSbd', len(pcm_block)) + pcm_block
    pss = pes(0xE0, video[:28], 9009, 0) + pes(0xE0, video[28:36], 3003)
    # Omit final picture's PTS to exercise temporal-reference recovery.
    pss += pes(0xE0, video[36:])
    # Split audio in the middle of the header, as real PES packets can.
    pss += pes(0xBD, b'\xff\xa0\0\0' + audio[:13], 3003)
    pss += pes(0xBD, b'\xff\xa0\0\0' + audio[13:]) + b'\0\0\1\xb9'
    return pss, video, audio


class MovieExportTest(unittest.TestCase):
    def test_timestamps(self):
        for value in [0, 3003, 90000, (1 << 33) - 1]:
            self.assertEqual(movie.timestamp(pts(value)), value)

    def test_demux_and_reorder_timing(self):
        pss, expected, audio = fixture()
        video, times, sound, first = movie.demux(pss)
        self.assertEqual(video, expected)
        self.assertEqual(sound, audio)
        self.assertEqual(first, 3003)
        info = movie.video_samples(video, times)
        self.assertEqual((info['width'], info['height']), (32, 16))
        self.assertEqual(info['offsets'], [9009, 0, 0])
        self.assertEqual(info['sync'], [1])
        self.assertEqual(info['duration'], 9009)

    def test_audio_preserves_samples_and_channels(self):
        _, _, audio = fixture()
        pcm, rate, channels = movie.pcm_audio(audio)
        self.assertEqual((rate, channels), (48000, 2))
        self.assertEqual(struct.unpack('<4h', pcm), (1, 300, -2, -400))

    def test_rejects_truncated_packet(self):
        pss, _, _ = fixture()
        with self.assertRaises(ValueError):
            movie.demux(pss[:-8])

    def test_rejects_unknown_audio(self):
        _, _, audio = fixture()
        with self.assertRaises(ValueError):
            movie.pcm_audio(audio[:8] + struct.pack('<I', 0x10) + audio[12:])

    def test_mov_contains_exact_video_and_pcm(self):
        pss, video, audio = fixture()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'test.mov'
            result = movie.remux(pss, path)
            data = path.read_bytes()
            self.assertEqual(result['frames'], 3)
            mdat = data.index(b'mdat') + 4
            self.assertEqual(data[mdat:mdat + len(video)], video)
            pcm, _, _ = movie.pcm_audio(audio)
            self.assertEqual(data[mdat + len(video):mdat + len(video) + len(pcm)], pcm)
            self.assertIn(b'ctts', data)
            self.assertIn(b'sowt', data)


if __name__ == '__main__':
    unittest.main()
