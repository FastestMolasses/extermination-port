#!/usr/bin/env python3
"""The comparison tool's offline audio carries the movie's PCM
(make test-movie-field; about 4 s).

The decomp's tools/video_compare/ records the port with EM_INPUT_RECORD,
plays the recording back with EM_REPLAY_AUDIO (no audio device: em_replay
pulls the shared mixer em_bgm_render_offline on the game thread, 800.8
frames a step, into a WAV) and puts the WAV beside the original's audio.
Since the movies play on the game's field clock, their sound is one more
producer of that mixer (em_movie_sound.h): field n's PCM frames are pushed
in step n's movie vblank hook and pulled in the same step. This check:

  1. records a short run: EM_NEW_GAME=1 (the comparison video's switch,
     which holds START on the New Game movie as the video's recording
     does), stopped at the AREA11 opening's first frame
     (EM_NEW_GAME_STATE_TEST);
  2. plays it back with EM_REPLAY_AUDIO;
  3. asserts the run log's movie sound line: the movie's ring took every
     frame pushed (0 overruns, every pull at 48 kHz);
  4. asserts the WAV over the movie's sounding fields (from the first
     sounding field's step on, floor(n * 800.8) per field) holds the
     movie's PCM16 frames exactly as the decoder reads them
     (build/movie_test --pcm), nothing added, dropped or scaled.
"""
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

BIN = os.environ.get('EM_BIN', 'build/extermination')
MOVIE_TEST = 'build/movie_test'
E900 = 'assets/startup/intro.mov'


def run(env, out, name):
    e = {k: v for k, v in os.environ.items() if not k.startswith('EM_')}
    e.update(EM_HEADLESS='1', EM_UNCAPPED='1', EM_NEW_GAME='1', EM_NEW_GAME_STATE_TEST=str(out / f'{name}.img'))
    e.update(env)
    p = subprocess.run([BIN], env=e, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=300)
    (out / f'{name}.log').write_text(p.stdout)
    assert p.returncode == 0, (name, p.returncode, p.stdout[-2000:])
    return p.stdout


def main():
    assert Path(MOVIE_TEST).exists(), 'build/movie_test is missing (make test-movie-field builds it)'
    with tempfile.TemporaryDirectory(prefix='replay_movie_audio_', dir='build') as tmp:
        out = Path(tmp)
        rec, wav, log = out / 'run.rec', out / 'run.wav', out / 'play.rec'
        run({'EM_INPUT_RECORD': str(rec)}, out, 'record')
        text = run({'EM_INPUT_PLAY': str(rec), 'EM_REPLAY_AUDIO': str(wav), 'EM_INPUT_RECORD': str(log)}, out,
                   'play')
        begin = re.findall(r'^startup: movie 0 \(E900\.PSS\) for the game task', text, re.M)
        sound = re.findall(r'^startup: movie sound: first sounding field at step (-?\d+), (\d+) frames pushed, '
                           r'(\d+) overruns, (\d+) pulls at another rate$', text, re.M)
        end = re.findall(r'^startup: movie (\w+), .*, (\d+) sounding fields \((\d+) PCM frames\)$', text, re.M)
        assert len(begin) == 1 and len(sound) == 1 and len(end) == 1, ('one New Game movie in the playback',
                                                                       begin, sound, end)
        first, pushed, overruns, bad_rate = (int(x) for x in sound[0])
        outcome, fields, pcm_frames = end[0][0], int(end[0][1]), int(end[0][2])
        assert outcome == 'skipped', ('the switch skips the New Game movie', outcome)
        assert overruns == 0 and bad_rate == 0, ('the movie sound ring dropped frames', overruns, bad_rate)
        assert first >= 0 and fields >= 10, ('the movie sounded too little to check', first, fields)
        lo, hi = first * 4004 // 5, (first + fields) * 4004 // 5
        assert hi - lo == pushed, ('the frames pushed are not the sounding fields\' 800.8 each', pushed, hi - lo)
        ref = out / 'e900.raw'
        subprocess.run([MOVIE_TEST, '--pcm', E900, str(pcm_frames), str(ref)], check=True)
        want = ref.read_bytes()
        assert len(want) == 4 * pcm_frames, ('the decoder\'s PCM', len(want), pcm_frames)
        data = wav.read_bytes()
        assert data[:4] == b'RIFF' and data[36:40] == b'data', 'the WAV header'
        size = struct.unpack_from('<I', data, 40)[0]
        pcm = data[44:44 + size]
        assert len(pcm) >= 4 * hi, ('the WAV ends before the movie\'s last sounding field', len(pcm) // 4, hi)
        got = pcm[4 * lo:4 * lo + len(want)]
        if got != want:
            k = next(i for i in range(0, len(want), 4) if got[i:i + 4] != want[i:i + 4]) // 4
            raise AssertionError(('the WAV does not hold the movie\'s PCM unchanged', 'first frame off', k,
                                  struct.unpack_from('<2h', got, 4 * k), struct.unpack_from('<2h', want, 4 * k)))
        # past the PCM (none here: the movie's PCM outlasts its skip) the SPU2 plays silence
        rest = pcm[4 * lo + len(want):4 * hi]
        assert rest == bytes(len(rest)), 'the sounding fields past the PCM are not silent'
        nonzero = sum(1 for i in range(0, len(want), 2) if want[i:i + 2] != b'\0\0')
        print(f'replay movie audio: the New Game movie {outcome} after {fields} sounding fields (steps {first}..'
              f'{first + fields - 1}); {pushed} frames pushed, 0 overruns, 0 pulls at another rate; the WAV\'s '
              f'frames {lo}..{hi - 1} hold the movie\'s {pcm_frames} PCM frames bit for bit ({nonzero} non-zero '
              f'samples)')
    print('replay movie audio: PASS')


if __name__ == '__main__':
    main()
