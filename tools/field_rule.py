"""The code's rule for the field bit D_00810E88 against the buffer index
D_00810E80 (docs/GS_EXACT.md 10.4, em_frame.h em_frame_d810E88), shared by
the level smoke (tools/test_level_smoke.py) and the live movie-field check
(tools/test_movie_field_live.py).

The rule, read from the decomp's C:
- the start-up waits for an odd field (0x1AAEFC), so the first loop top
  holds (D_00810E80, D_00810E88) = (0, 1);
- the vblank handler 0x1AB140 latches the GS field at every vblank and the
  field alternates at every vblank, so at a loop top D_00810E88 is
  (vblanks before it + 1) & 1;
- D_00810E80 flips once per main-loop iteration (step W), so at a loop top
  it is the iteration count & 1;
- a non-movie iteration takes one vblank, so it keeps the relation; a movie
  (00203350, inside one iteration) takes its own vblanks and at host speed
  always ends on a field-1 vblank (its second closing 001009C8 wait), so
  its iteration's step P reads field 1, the next loop top holds field 1
  and the relation afterwards follows the movie iteration's D_00810E80:
  1 keeps D_00810E88 = 1 - D_00810E80, 0 makes them equal.

A relation is 1 for D_00810E88 = 1 - D_00810E80 (step W's field +0x98 =
1 - slot +0x9C) and 0 for D_00810E88 == D_00810E80.
"""
import re

# The start-up rule: the first loop top's (D_00810E80, D_00810E88).
BOOT_LOOP_TOP = (0, 1)
BOOT_RELATION = int(BOOT_LOOP_TOP[1] == 1 - BOOT_LOOP_TOP[0])

# em_frontend.c begin_movie's line and movie_pump's end line.
MOVIE_BEGIN = re.compile(r'^startup: movie (\d+) \((\S+)\)( for the game task)?, D_00810E80 (\d), '
                         r'the first wait\'s field (\d)$', re.M)
MOVIE_END = re.compile(r'^startup: movie (completed|skipped|failed), .*; (\d+) steps \(gate at vblank (\d+), '
                       r'the play loop\'s end at (\d+), P field (\d)\)', re.M)


def field_rule_movies(rows, where):
    """rows: the loop tops [(vs, fi, fld)] of consecutive iterations. Checks
    the code's rule on every pair and returns the movie iterations as
    [(row index, its D_00810E80)]."""
    movies = []
    for i in range(1, len(rows)):
        (v0, i0, f0), (v1, i1, f1) = rows[i - 1], rows[i]
        assert i0 in (0, 1) and f0 in (0, 1) and i1 == 1 - i0, (where, i, 'D_00810E80 does not flip once per '
                                                                'iteration', rows[i - 1], rows[i])
        if v1 - v0 == 1:
            assert f1 == 1 - f0, (where, i, 'the field does not alternate at the vblank', rows[i - 1], rows[i])
        else:
            assert v1 - v0 > 1, (where, i, 'an iteration without a vblank', rows[i - 1], rows[i])
            assert f1 == 1, (where, i, 'the loop top after a movie does not hold field 1 (00203350 ends on a '
                                       'field-1 vblank)', rows[i - 1], rows[i])
            movies.append((i - 1, i0))
    return movies


def run_movies(run):
    """The run log's movies in order: [{'selector', 'name', 'game_task',
    'entry' (D_00810E80 of the movie's iteration), 'first_field', 'outcome',
    'steps', 'p_field'}]. Every movie that began has its end line."""
    begins, ends = list(MOVIE_BEGIN.finditer(run)), list(MOVIE_END.finditer(run))
    assert len(ends) in (len(begins), len(begins) - 1), ('run log: movie begin / end lines', len(begins),
                                                          len(ends))
    out = []
    for k, b in enumerate(begins):
        e = ends[k] if k < len(ends) else None
        assert e is None or b.end() < e.start() and (k + 1 == len(begins) or e.start() < begins[k + 1].start()), (
            'run log: a movie\'s end line out of order', k)
        out.append({'selector': int(b.group(1)), 'name': b.group(2), 'game_task': bool(b.group(3)),
                    'entry': int(b.group(4)), 'first_field': int(b.group(5)),
                    'outcome': e.group(1) if e else None, 'steps': int(e.group(2)) if e else None,
                    'p_field': int(e.group(5)) if e else None})
    return out


def boot_relation(run, where):
    """The relation at the game task's first loop top, derived from the boot:
    the start-up rule's (0, 1), then every frontend movie before the game
    task (the intro, the title's attract movie) sets it to its entry's
    D_00810E80. Each of those movies must have ended with step P reading
    field 1. Returns (relation, the frontend movies)."""
    rel, front = BOOT_RELATION, []
    for m in run_movies(run):
        if m['game_task']:
            break
        assert m['outcome'] in ('completed', 'skipped') and m['p_field'] == 1, (
            where, 'a frontend movie whose iteration\'s P does not read field 1', m)
        rel = m['entry']
        front.append(m)
    return rel, front


def check_boot_clock(rows, where):
    """Loop-top rows [(counter, vs, fi, fld)] against the boot: D_00810E80 is
    the iteration count & 1 and D_00810E88 the field of the vblanks so far
    from the start-up's odd one, (vs + 1) & 1."""
    for counter, vs, fi, fld in rows:
        assert fi == (counter ^ BOOT_LOOP_TOP[0]) & 1 and fld == (vs + BOOT_LOOP_TOP[1]) & 1, (
            where, 'a loop top off the boot\'s clock (first loop top (0, 1), D_00810E80 flipping per '
                   'iteration, the field per vblank)', {'counter': counter, 'vs': vs, 'fld': [fi, fld]})
