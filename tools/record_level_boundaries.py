#!/usr/bin/env python3
"""Capture untraced neutral ticks between indexed fork route beats.

Uses the decomp's hidden owned-session API. A boundary passes only if its
last sampled row equals every field of the next beat's first row. Captures,
failure details and logs remain private under build/fork_refs. This records
the indexed fork playthrough; it does not reconstruct missing legacy ticks.
--route-set selects main-line beats of a fork re-recording set (its
manifest.json, e.g. build/fork_refs/s87/route_a00) that the supplemental
index does not list; the same replay and endpoint check apply.
"""
from __future__ import annotations

import argparse
import ast
import contextlib
import inspect
import json
import os
from pathlib import Path
import signal
import sys
import textwrap
import traceback

from level_readiness import ROOT, digest, supplemental_captures
from record_level_routes import apply_owner_binding, private_root, route_beats, validate_owner_binding


def install_sampler(rc, fn, route):
    """Resolve the beat's explicit sampler call without executing its driving.

    The retained level beats name a sampler at function scope. Only
    the Route argument and named sampler constants are accepted; expressions
    are deliberately rejected rather than evaluated.
    """
    function = ast.parse(textwrap.dedent(inspect.getsource(fn))).body[0]
    for statement in function.body:
        if not isinstance(statement, ast.Expr) or not isinstance(statement.value, ast.Call):
            continue
        call = statement.value
        if not isinstance(call.func, ast.Name):
            continue
        name = call.func.id
        if not (name.startswith('use_') and name.endswith('_sampler')):
            continue
        if call.keywords or not call.args or not all(isinstance(a, ast.Name) for a in call.args):
            raise ValueError('Unsupported sampler signature: ' + fn.__name__)
        if call.args[0].id != 'r' or any(not a.id.isupper() for a in call.args[1:]):
            raise ValueError('Unsupported sampler argument: ' + fn.__name__)
        getattr(rc, name)(route, *(getattr(rc, a.id) for a in call.args[1:]))
        return
    raise ValueError('No explicit sampler: ' + fn.__name__)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--decomp', type=Path, default=ROOT.parent / 'Extermination')
    ap.add_argument('--out-root', type=Path, required=True)
    ap.add_argument('--beats', help='optional comma-separated indexed beat names')
    ap.add_argument('--route-set', type=Path,
                    help='decomp-relative fork set with manifest.json (main-line beats; requires --beats)')
    args = ap.parse_args()
    decomp = args.decomp.resolve()
    if args.route_set:
        if not args.beats or args.route_set.is_absolute():
            ap.error('--route-set takes a decomp-relative path and requires --beats')
        route_set = private_root(decomp, decomp / args.route_set)
        manifest = json.loads((route_set / 'manifest.json').read_text())
        indexed = {}
        for name, entry in manifest['beats'].items():
            folder = (decomp / entry['folder']).resolve()
            if folder != route_set / name:
                raise ValueError('Route-set manifest folder mismatch: ' + name)
            indexed[name] = folder / 'trace.json'
        source_label = str(args.route_set) + '/manifest.json'
    else:
        indexed = supplemental_captures(ROOT, decomp)
        source_label = 'LEVEL_ROUTE_EVIDENCE.json'
    names = args.beats.split(',') if args.beats else list(indexed)
    if any(name not in indexed for name in names):
        ap.error('every requested beat must be in ' + source_label)
    out = private_root(decomp, args.out_root)
    out.mkdir(parents=True, exist_ok=False)
    sys.path.insert(0, str(decomp / 'tools'))
    import route_capture as rc
    import route_lanes as lanes
    import pcsx2_session as ps

    rc.use_fork('phase', True)
    ps.FORK_MTVU, ps.FORK_LANE = False, None
    os.environ.pop('EXTERMINATION_FORK_LOCK_HELD', None)
    os.environ['PCSX2_FORK_OWNER_LABEL'] = 'levels-boundary-capture'
    signal.signal(signal.SIGTERM, rc._term_to_interrupt)
    beats = route_beats(rc, lanes, names)
    results = []

    def save():
        (out / 'results.json').write_text(json.dumps({'results': results}, indent=2) + '\n')

    for name in names:
        trace = indexed[name]
        target = json.loads(trace.read_text())
        for binding in target.get('levels_capture_policy', {}).get('owner_bindings', []):
            # Revalidate the original anchor identities instead of trusting
            # saved read addresses. Applying the same binding is idempotent.
            private_root(decomp, Path(binding['actual_ram']))
            validate_owner_binding(rc, binding)
            apply_owner_binding(rc, binding)
        source = Path(target['source_state']).resolve()
        if (decomp / 'build/fork_refs').resolve() not in source.parents:
            raise ValueError('Boundary source is outside fork_refs: ' + name)
        previous_trace = source.parent / 'trace.json'
        previous = json.loads(previous_trace.read_text())
        gap = target['rows'][0]['counter'] - previous['rows'][-1]['counter']
        if not 0 <= gap <= 120:
            raise ValueError('Unexpected boundary counter distance: ' + name)
        if gap <= 1:
            results.append({'name': name, 'result': 'no_missing_counter', 'gap': gap})
            save()
            continue
        folder = out / name
        folder.mkdir()
        row = {'name': name, 'gap': gap, 'missing_counters': gap - 1,
               'target_trace': str(trace.relative_to(decomp)), 'target_trace_sha256': digest(trace),
               'source_trace': str(previous_trace.relative_to(decomp)),
               'source_trace_sha256': digest(previous_trace), 'source_state_sha256': digest(source)}
        with (folder / 'run.log').open('x') as log, contextlib.redirect_stdout(log), contextlib.redirect_stderr(log):
            try:
                with rc.open_session(source, log_dir=folder / 'session') as session:
                    if session.visible:
                        raise RuntimeError('Boundary session must be hidden')
                    route = rc.Route(session)
                    route.begin()
                    install_sampler(rc, beats[name][1], route)
                    if route.rows[0]['counter'] != previous['rows'][-1]['counter']:
                        raise ValueError('Loaded source does not match its final trace counter')
                    # run_beat's lead-in uses session.step with the pad left
                    # cleared by the session. Route.step would set a neutral
                    # stick packet, which is a different input history.
                    rows = [dict(route.now(), boundary_frame=0)]
                    for i in range(gap):
                        session.step(1)
                        rows.append(dict(route.now(), boundary_frame=i + 1))
                    actual = {k: v for k, v in rows[-1].items() if k != 'boundary_frame'}
                    expected = {k: v for k, v in target['rows'][0].items() if k != 'f'}
                    changed = sorted(k for k in actual.keys() | expected.keys()
                                     if actual.get(k) != expected.get(k))
                    detail = dict(row, rows=rows, differing_keys=changed)
                    (folder / 'trace.json').write_text(json.dumps(detail, separators=(',', ':')) + '\n')
                    if changed:
                        raise ValueError('Replayed boundary does not match the recorded next row')
                row['result'] = 'passed'
            except BaseException as exc:
                traceback.print_exc()
                row.update(result='failed', error_type=type(exc).__name__)
                results.append(row)
                save()
                return 1
        results.append(row)
        save()
        print(name + ': boundary rows captured and matched', flush=True)
    save()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
