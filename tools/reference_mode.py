#!/usr/bin/env python3
"""Quick/full case selection for the original-instruction reference tests.

CLAUDE.md "Tests": a default run finishes in about 10 s. It keeps every case
class, every boundary and every captured-state comparison, plus a fixed-seed
sample of the bulk sweep. EM_TEST_FULL=1 runs the exhaustive sweep exactly as
before. Both modes compare the same fields; quick mode only runs fewer inputs.
"""
import re
import os
import random

FULL = os.environ.get('EM_TEST_FULL', '') not in ('', '0')
MODE = 'full' if FULL else 'quick'


def cover(items, *axes):
    """Indices of a deterministic subset in which every distinct value of
    every axis (a function of an item) occurs at least once."""
    seen, chosen = set(), []
    for index, item in enumerate(items):
        keys = {(a, axis(item)) for a, axis in enumerate(axes)}
        if not keys <= seen:
            seen |= keys
            chosen.append(index)
    return chosen


def select(items, count, seed, axes=(), keep=None):
    """Every item in full mode. In quick mode: the items keep(index, item)
    marks, a covering set of every value on each axis, and a fixed-seed
    sample of the rest up to `count` items in total. Order is preserved."""
    items = list(items)
    if FULL:
        return items
    chosen = set(cover(items, *axes)) if axes else set()
    if keep:
        chosen.update(i for i, item in enumerate(items) if keep(i, item))
    rest = [i for i in range(len(items)) if i not in chosen]
    chosen.update(random.Random(seed).sample(rest, max(0, min(len(rest), count - len(chosen)))))
    return [items[i] for i in sorted(chosen)]


def pick(full, quick):
    """A per-mode constant, e.g. a random-sweep length."""
    return full if FULL else quick


def parallel_map(fn, items, cost=None):
    """[fn(x) for x in items], computed in forked worker processes.

    For independent, CPU-bound oracle cases: each call builds its own oracle
    and any native state it needs (a forked worker inherits loaded ctypes
    libraries). fn must be a module-level function; whatever it reads from
    module globals must be set before the call (fork inherits it).
    Results come back in input order, so the caller compares exactly as a
    serial loop would. cost(item), if given, starts the dearest items first.
    EM_TEST_JOBS=1 (or no fork) runs serially."""
    import multiprocessing
    items = list(items)
    jobs = int(os.environ.get('EM_TEST_JOBS', '0') or 0) or min(8, max(1, (os.cpu_count() or 2) - 2))
    jobs = min(jobs, len(items))
    if jobs < 2 or 'fork' not in multiprocessing.get_all_start_methods():
        return [fn(item) for item in items]
    order = sorted(range(len(items)), key=lambda i: -cost(items[i])) if cost else list(range(len(items)))
    with multiprocessing.get_context('fork').Pool(jobs) as pool:
        results = pool.map(fn, [items[i] for i in order], chunksize=1)
    out = [None] * len(items)
    for i, result in zip(order, results):
        out[i] = result
    return out


def part(selected, total, label):
    return f'{total:,} {label}' if FULL else f'{selected:,} of {total:,} {label}'


def banner(*parts):
    """One honest line naming the mode and the case counts that ran."""
    line = f'mode {MODE}: ' + ', '.join(parts)
    if not FULL:
        line += ' (EM_TEST_FULL=1 runs the exhaustive sweep)'
    print(line, flush=True)
    return line


def in_scope_beat(name):
    """True for the first level's route beats 00..14 (New Game to Roger;
    docs/FIRST_LEVEL_ROUTE.md). Beat 15 (15_level_exit) is an opt-in capture
    that leaves AREA11 (docs/FIRST_LEVEL_EXIT.md) and is not part of the
    first-level route the reference tests compare against."""
    m = re.match(r'(\d{2})_', name)
    return bool(m) and int(m.group(1)) <= 14
