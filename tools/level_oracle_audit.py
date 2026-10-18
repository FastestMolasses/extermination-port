#!/usr/bin/env python3
"""Opt-in standalone oracle audit; writes evidence metadata, never game data.

Native Python/clang on macOS. Run --run into a NEW ignored directory; --check
validates the retained source fingerprints without running or trusting labels.
The committed receipt contains only test names, mode, source fingerprints,
success, elapsed time and the local log path. Test logs remain private.
"""
from __future__ import annotations

import argparse
import ast
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / 'docs/LEVEL_ORACLE_EVIDENCE.json'
SUITES = tuple('area01_overlay area01_math area01_render area01_render_existing area01_sys '
               'area01_side area01_ui area01_exita area01_exitb area01_room area01_revisit '
               'area00_overlay area00_low area00_world area00_hud area00_fx area00_fx_existing '
               'area02_overlay area02_math area02_misc area02_ui area04_port area22_port '
               'area06_port level8_port level9_port level10_port level11_port level12_port '
               'level13_port level14_port area00_return area13_route area15_route'.split())


def dependencies(root, test, cache=None):
    """Conservative local dependency closure; no test or original data executed.

    Literal C filenames anywhere in each test (including comments) are included,
    as are every local Python import and every transitive local C include. The
    explicit inventory below is an audit boundary: a suite with no C source is
    rejected instead of being granted a source-free certificate.
    """
    root = root.resolve()
    cache = {} if cache is None else cache
    pending, found = [root / test], set()
    while pending:
        path = pending.pop()
        if path in found:
            continue
        found.add(path)
        stamp = (path.stat().st_mtime_ns, path.stat().st_size)
        if path in cache and cache[path][0] == stamp:
            pending.extend(cache[path][1])
            continue
        previous = len(pending)
        text = path.read_text()
        if path.suffix == '.py':
            tree = ast.parse(text)
            modules = []
            for node in ast.walk(tree):
                if isinstance(node, ast.Import):
                    modules += [a.name for a in node.names]
                elif isinstance(node, ast.ImportFrom) and node.module:
                    modules.append(node.module)
            for module in modules:
                p = root / 'tools' / (module.replace('.', '/') + '.py')
                if p.is_file():
                    pending.append(p)
            for name in re.findall(r'\bem_\w+\.c\b', text):
                for parent in (root / 'src/game', root / 'src'):
                    if (parent / name).is_file():
                        pending.append(parent / name)
        else:
            for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"', text, re.M):
                for parent in (path.parent, root / 'src', root / 'src/game'):
                    if (parent / name).is_file():
                        pending.append((parent / name).resolve())
                        break
        cache[path] = (stamp, pending[previous:])
    if not any(p.suffix == '.c' for p in found):
        raise ValueError(f'no native sources identified for {test}')
    return sorted(str(p.relative_to(root)) for p in found)


def fingerprint(root, paths):
    h = hashlib.sha256()
    for relative in sorted(paths):
        h.update(relative.encode() + b'\0')
        h.update(hashlib.sha256((root / relative).read_bytes()).digest())
    return h.hexdigest()


def current_evidence(root, path=None):
    path = path or root / 'docs/LEVEL_ORACLE_EVIDENCE.json'
    if not path.is_file():
        return {}, []
    data = json.loads(path.read_text())
    if data.get('schema') != 1:
        raise ValueError('unsupported oracle evidence schema')
    valid, stale, cache = {}, [], {}
    for row in data['suites']:
        test = row['test']
        paths = dependencies(root, test, cache)
        if (row['exit_code'] != 0 or row['mode'] not in ('quick', 'full') or
                paths != row['dependencies'] or fingerprint(root, paths) != row['source_sha256']):
            stale.append(test)
        else:
            valid[test] = row
    return valid, stale


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', type=Path, help='new private directory under build/')
    parser.add_argument('--full', action='store_true')
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--suite', choices=SUITES, action='append')
    parser.add_argument('--evidence', type=Path, default=EVIDENCE)
    args = parser.parse_args()
    if not args.run:
        good, stale = current_evidence(ROOT, args.evidence)
        missing = {f'tools/test_{s}_reference.py' for s in (args.suite or SUITES)} - set(good)
        print(f'{len(good)} current oracle receipts; {len(stale)} stale; {len(missing)} missing')
        return 1 if missing or stale else 0
    folder = args.run.resolve()
    if not folder.is_relative_to(ROOT / 'build'):
        parser.error('--run must be inside this worktree\'s ignored build/')
    folder.mkdir(parents=True, exist_ok=False)
    env = {k: v for k, v in os.environ.items() if not k.startswith('EM_')}
    env.update(EM_HEADLESS='1', EM_REFERENCE='fork', EM_TEST_JOBS='2', PYTHONUNBUFFERED='1')
    if args.full:
        env['EM_TEST_FULL'] = '1'
    current_evidence(ROOT, args.evidence)  # validate retained rows, including stale ones
    old = json.loads(args.evidence.read_text())['suites'] if args.evidence.is_file() else []
    rows = {row['test']: row for row in old}  # retain stale history; --check still rejects it
    for suite in args.suite or SUITES:
        test = f'tools/test_{suite}_reference.py'
        paths = dependencies(ROOT, test)
        before = fingerprint(ROOT, paths)
        start = time.monotonic()
        log = folder / (suite + '.log')
        print('START', suite, flush=True)
        with log.open('x') as out:
            proc = subprocess.Popen([sys.executable, test], cwd=ROOT, env=env, stdout=out,
                                    stderr=subprocess.STDOUT, start_new_session=True)
            try:
                rc = proc.wait()
            finally:
                if proc.poll() is None:
                    os.killpg(proc.pid, signal.SIGTERM)
                    proc.wait(timeout=10)
        if fingerprint(ROOT, paths) != before:
            raise RuntimeError(f'sources changed while {suite} ran; no receipt written')
        row = {'test': test, 'mode': 'full' if args.full else 'quick', 'exit_code': rc,
               'finished_utc': datetime.now(timezone.utc).isoformat(),
               'seconds': round(time.monotonic() - start, 3), 'dependencies': paths,
               'source_sha256': before, 'private_log': str(log.relative_to(ROOT))}
        rows[test] = row
        payload = {'schema': 1, 'scope': 'standalone instruction oracles; no live binding certification',
                   'suites': [rows[k] for k in sorted(rows)]}
        # Always retain an interrupted audit's results privately; publish the
        # requested metadata receipt only after every requested suite passed.
        (folder / 'results.json').write_text(json.dumps(payload, indent=2) + '\n')
        print('PASS' if rc == 0 else 'FAIL', suite, row['seconds'], flush=True)
        if rc:
            return rc
    args.evidence.write_text(json.dumps(payload, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
