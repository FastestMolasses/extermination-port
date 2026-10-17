#!/usr/bin/env python3
"""The field after a movie, live (make test-movie-field; about 3 s).

The blocking movie 00203350 takes its own number of vblanks inside one
main-loop iteration and at host speed always ends on a field-1 vblank
(em_movie_field.h; docs/GS_EXACT.md 10.4), so the relation between the
field bit D_00810E88 and the buffer index D_00810E80 after a movie follows
the D_00810E80 the movie was entered on (tools/field_rule.py). The level
smoke's fixtures enter both of their movies on 1, where the code's rule and
the old fixed relation give the same rows; this check plays the New Game
movie from both entries:

  entry 0  EM_STARTUP_TEST=newgame, EM_STARTUP_MENU_WAIT=1 (the CROSS one
           title tick after the fixtures' tick 15): afterwards D_00810E88 ==
           D_00810E80 at every loop top and step W's field +0x98 equals the
           slot +0x9C (the fork's route 15 after the departure movie);
  entry 1  the fixtures' press (menu tick 15, the canonical fork run's
           entry, probes_1016/newgame/d0): D_00810E88 = 1 - D_00810E80.

Each run stops at the AREA11 opening's first frame (EM_NEW_GAME_STATE_TEST)
and logs every game-task tick (EM_AREA_CHANGE_LOG, the loop top's fld and
vs). Asserted per run:
  - the boot's clock at every logged loop top (first loop top (0, 1),
    D_00810E80 the iteration count & 1, the field (vs + 1) & 1);
  - the starting relation derived from the boot (the start-up rule, then
    the intro movie's entry from the run log), not read from the log;
  - exactly one movie in the log, the New Game movie, entered on the
    expected D_00810E80, taking the run log's steps in vblanks, its P
    reading field 1 (the run log's line and the next loop top's field);
  - before it the boot's relation, after it the relation its entry gives,
    at every loop top and in step W's stored field against the slot.
"""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import field_rule as FR  # noqa: E402

BIN = os.environ.get('EM_BIN', 'build/extermination')
RCTX_FIELD98 = 13  # em_scene_bindings.c log_tick_end's "rctx" order (test_level_smoke.RCTX_FIELDS)


def play(out, menu_wait):
    env = {k: v for k, v in os.environ.items()
           if not k.startswith('EM_') or k in ('EM_BIN',)}
    env.update(EM_HEADLESS='1', EM_UNCAPPED='1', EM_STARTUP_TEST='newgame',
               EM_STARTUP_MENU_WAIT=str(menu_wait), EM_NEW_GAME_STATE_TEST=str(out / 'state.img'),
               EM_AREA_CHANGE_LOG=str(out / 'ticks.jsonl'))
    p = subprocess.run([BIN], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120)
    (out / 'run.log').write_text(p.stdout)
    assert p.returncode == 0, (f'menu wait {menu_wait}: exit {p.returncode}', p.stdout[-2000:])
    return p.stdout, [json.loads(line) for line in (out / 'ticks.jsonl').open() if '"tick"' in line]


def check(name, run, ticks, entry):
    where = (name,)
    rel, front = FR.boot_relation(run, where)
    assert front and front[0]['name'] == 'E900.PSS', (where, 'the intro movie before the title', front)
    game = [m for m in FR.run_movies(run) if m['game_task']]
    assert len(game) == 1 and game[0]['name'] == 'E900.PSS' and game[0]['entry'] == entry, (
        where, f'one New Game movie entered with D_00810E80 = {entry}', game)
    assert game[0]['outcome'] == 'skipped' and game[0]['p_field'] == 1, (where, 'the movie\'s P field', game[0])
    rows = [(t['counter'], t['vs'], t['fld'][0], t['fld'][1]) for t in ticks]
    assert len(rows) >= 20 and all(b[0] == a[0] + 1 for a, b in zip(rows, rows[1:])), (
        where, 'the game task\'s ticks are not consecutive iterations')
    FR.check_boot_clock(rows, where)
    first = int(rows[0][3] == 1 - rows[0][2])
    assert first == rel, (where, 'the first logged loop top\'s relation is not the one the boot and the intro '
                                 'give', rows[0], rel)
    movies = FR.field_rule_movies([(v, i, f) for _, v, i, f in rows], where)
    assert len(movies) == 1 and movies[0][1] == entry, (where, 'the movie iterations in the tick log', movies)
    m = movies[0][0]
    assert rows[m + 1][1] - rows[m][1] == game[0]['steps'], (where, 'the movie iteration\'s vblanks against its '
                                                                    'steps', rows[m], rows[m + 1], game[0])
    assert rows[m + 1][3] == 1, (where, 'the loop top after the movie: field 1 (its P\'s)', rows[m + 1])
    before = [int(f == 1 - i) for _, _, i, f in rows[:m + 1]]
    after = [int(f == 1 - i) for _, _, i, f in rows[m + 1:]]
    assert set(before) == {rel}, (where, 'the relation before the movie', before)
    assert set(after) == {entry}, (where, f'the relation after a movie entered on {entry}: D_00810E88 '
                                          f'{"= 1 -" if entry else "=="} D_00810E80 at every loop top', after)
    fields = 0
    for k, t in enumerate(ticks):
        r = t.get('rctx')
        if not r:
            continue
        field, slot = struct.unpack('<II', bytes.fromhex(r[RCTX_FIELD98]))
        want = 1 - slot if (rel if k <= m else entry) else slot
        assert slot in (0, 1) and field == want, (where, 'step W field +0x98 against slot +0x9C', t['tick'],
                                                  field, slot)
        fields += k > m
    assert fields >= 2, (where, 'step W\'s field too little exercised after the movie', fields)
    return (f'{name}: the New Game movie entered on {entry} at counter {rows[m][0]} took {game[0]["steps"]} '
            f'vblanks, P field 1; {m + 1} loop tops before it in the boot\'s relation, {len(rows) - m - 1} after '
            f'it {"opposite" if entry else "equal"}, {fields} step W fields after it by that relation')


def main():
    with tempfile.TemporaryDirectory(prefix='movie_field_live_', dir='build') as tmp:
        lines = []
        for name, wait, entry in (('entry 0', 1, 0), ('entry 1', 0, 1)):
            out = Path(tmp) / f'wait{wait}'
            out.mkdir()
            run, ticks = play(out, wait)
            lines.append(check(name, run, ticks, entry))
    for line in lines:
        print('movie field live:', line)
    print('movie field live: PASS')


if __name__ == '__main__':
    main()
