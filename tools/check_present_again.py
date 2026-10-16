#!/usr/bin/env python3
"""Frames that draw nothing (docs/GS_EXACT.md section 11 "Frames that draw
nothing"): the presenter's log of a level-smoke run against its tick log.

On the PS2 every main-loop iteration kicks its list into the buffer the
display scans next; an iteration whose list draws no picture into it (the
head 001D2300 kicks is the Z-only clear) leaves the picture drawn there two
iterations before, and the display scans that again. The fork's frame store
shows it at the status screen's entries (0x1AE040 state 1 -> 3, the
iteration that calls 0020E060, and the next one): route 01's battery page at
vblanks 193 = 191 and 194 = 192, route 03's panel page at 390 = 388 and
391 = 389, black from the next one.

The run's EM_PRESENT_LOG (src/gfx/metal/em_gfx_metal.m) has one line per
committed frame: "<n> <tag> field <FRAME_1> <XYOFFSET_1> <hash>",
"<n> <tag> again <FBP> <FRAME_1> <XYOFFSET_1> <hash> ov <verts> px <now>
<src>" (the buffer's field shown again), "<n> <tag> again <FBP> gpu ov
<verts> px <now> <src>" (the GPU renderer, or a buffer whose last frame had
no field), "<n> <tag> lost" (nothing drawn over a buffer a movie left:
presented as drawn), "<n> <tag> gpu". The tag is the main-loop counter;
<hash> is the field bytes' hash, <verts> the vertices the frame's own
overlay pass queued (drawn over the kept picture), <now> / <src> the hashes
of the committed picture itself (the whole target, read back from the GPU)
of this frame and of the frame committed two before it. Checked:
- every status-screen entry of the run (a tick log line with 0x1AE040 at
  state 1 before and 3 after: pre/post byte 3) and the iteration after it
  are "again" frames of the buffers the two frames before them showed:
  the same FBP, and for a field the same FRAME_1, XYOFFSET_1 (its half line)
  and field bytes (hash);
- every status-entry "again" frame's presented picture equals, pixel for
  pixel, the picture presented two frames before it (<now> == <src>, both
  read back: a frame that kept the buffer's bookkeeping but presented
  something else, black for example, fails here; the panel entry's
  letterbox bars land on bars already there), and so does every other
  "again" frame whose overlay pass queued nothing (<verts> 0); the others
  (the game-over and load screens' legacy overlay drawing, a port-side
  classification: their own fades and text over the kept picture) are
  counted, not compared;
- every other "again" frame is likewise the frame two tags before it, and
  the frames before and after a pair are not "again" ones (a run that
  ends before the status phase has no entry to check);
- a "lost" frame is only the frame a movie returned to (a tag with more
  than one committed frame).

    python3 tools/check_present_again.py --present <log> --ticks <ticks.jsonl>
"""
import argparse
import json
import sys
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--present', type=Path, required=True)
    ap.add_argument('--ticks', type=Path, required=True)
    a = ap.parse_args()
    frames = {}
    for line in a.present.open():
        f = line.split()
        frames.setdefault(int(f[1]), []).append(f[2:])
    entries = []
    for line in a.ticks.open():
        t = json.loads(line)
        if 'counter' in t and 'pre' in t and bytes.fromhex(t['pre'])[3] == 1 and bytes.fromhex(t['post'])[3] == 3:
            entries.append(t['counter'])
    assert frames, 'the present log is empty'

    def last(tag):
        return frames.get(tag, [[]])[-1]

    def split_px(f):
        """An 'again' line's fields without its ov / px part, the overlay
        vertices and (now, src)."""
        assert len(f) >= 5 and f[-5] == 'ov' and f[-3] == 'px', ('an again line without the overlay count and the '
                                                                 'presented pictures\' hashes', f)
        now, src = f[-2:]
        assert int(now, 16) and int(src, 16), ('a presented picture was not read back', f)
        return f[:-5], int(f[-4]), (now, src)

    overlaid = []

    def shown_again(tag, entry=False):
        """The 'again' frame at `tag` shows what tag - 2 presented."""
        f, p = last(tag), last(tag - 2)
        assert f and f[0] == 'again', (tag, 'not a frame that shows its buffer again', f)
        assert p and p[0] in ('field', 'gpu', 'again'), (tag, 'the frame two before it', p)
        f, ov, (now, src) = split_px(f)
        if entry or not ov:
            assert now == src, (tag, 'the presented picture is not the one presented two frames before it', now, src)
        else:
            overlaid.append(tag)
        if p[0] == 'again':
            p = split_px(p)[0]
        if len(f) == 5:      # a field: FBP, FRAME_1, XYOFFSET_1, hash
            pf = p[1:] if p[0] == 'field' else p[2:]
            assert pf == f[2:], (tag, 'not the field of the frame two before it', f, p)
            assert int(f[2], 16) & 0x1FF == int(f[1], 16), (tag, 'FBP and FRAME_1 disagree', f)
        else:
            assert f[2:] == ['gpu'] and (p[0] == 'gpu' or p[-1] == 'gpu'), (tag, 'kept GPU frame', f, p)

    pairs = 0
    for c in entries:
        shown_again(c, True)
        shown_again(c + 1, True)
        assert last(c - 1)[:1] != ['again'] and last(c + 2)[:1] != ['again'], (c, 'more than the entry pair')
        pairs += 1
    others = [t for t, v in frames.items() if v[-1][0] == 'again' and t not in entries and t - 1 not in entries]
    for t in others:
        shown_again(t)
    lost = [t for t, v in frames.items() if v[-1][0] == 'lost']
    for t in lost:
        assert len(frames[t]) > 1, (t, 'a frame presented as drawn over a lost buffer that no movie preceded')
    print(f'present again: PASS {pairs} status-screen entr{"y" if pairs == 1 else "ies"} (0x1AE040 1 -> 3 and the '
          f'next iteration) show the buffers the two frames before them showed (presented pictures read back and equal); {len(others)} other frame(s) that '
          f'drew nothing show their buffer again ({", ".join(map(str, others[:12])) + (", ..." if len(others) > 12 else "") or "none"}; {len(overlaid)} of them with '
          f'their own overlay pass, not compared by pixels); {len(lost)} drawn over '
          f'a buffer a movie left')


if __name__ == '__main__':
    sys.exit(main())
