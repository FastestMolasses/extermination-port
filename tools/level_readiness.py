#!/usr/bin/env python3
"""Inventory locally retained route evidence, without exporting game data.

Native arm64 macOS (standard Python only). This is deliberately read-only except
for the two requested report destinations. It never imports a capture harness,
starts an emulator, reads instruction/data images, or treats a hook as an oracle
entry. See docs/LEVEL_READINESS.md for the limits of each evidence category.
"""
from __future__ import annotations

import argparse
import ast
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path

from level_oracle_audit import current_evidence
import re

ROOT = Path(__file__).resolve().parents[1]
ORDINALS = ('SECOND THIRD FOURTH FIFTH SIXTH SEVENTH EIGHTH NINTH TENTH '
            'ELEVENTH TWELFTH THIRTEENTH FOURTEENTH FIFTEENTH').split()
GROUPS = {
    2: ('a01',), 3: ('a00',), 4: ('a01r', 'a02'), 5: ('a04',),
    6: ('a22',), 7: ('a01u', 'a06'), 8: ('a06b', 'a01v', 'a22b', 'a04b'),
    9: ('a13',), 10: ('a19', 'a13b'), 11: ('a13c',),
    12: ('a13d', 'a19b'), 13: ('a19c',), 14: ('a19d', 'a15'),
    15: ('a15b', 'a19e', 'a03'),
}
# a02 includes the revisit. Level 15 repeats its boot union in three files,
# but each file retains a different overlay: all three are required. Never
# assign all rows of a combined report to the group in its filename.
CENSUSES = {
    2: ('a01_delta',), 3: ('a00_delta',), 4: ('a02_delta',),
    5: ('a04_delta',), 6: ('a22_delta',), 7: ('a01u_delta', 'a06_delta'),
    8: ('a06b_delta', 'a01v_delta', 'a22b_delta', 'a04b_delta'),
    9: ('a13_delta',), 10: ('a19_delta', 'a13b_delta'), 11: ('a13c_delta',),
    12: ('a13d_delta', 'a19b_swap_delta', 'a19b_area13_delta'),
    13: ('a19c_delta',), 14: ('a19d_delta', 'a15_delta'),
    15: ('a15b_delta', 'a19e_delta', 'a03_delta'),
}
AREAS = ('00', '01', '02', '03', '04', '06', '13', '15', '19', '22')
ASSET_PROFILES = ('00', '01', '02', '03', '04', '06', '13', '13x', '15', '19', '19x', '19s1', '22')
STATUS = ('live', 'verified', 'unverified', 'missing')
DEFINITION = re.compile(r'\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{', re.S)
ADDRESS = re.compile(r'(?<![\w])(?:0x)?(00[0-9A-Fa-f]{6})(?![\w])')
# Non-address names and intentional cross-overlay reuse, read at their actual
# definitions. These establish ownership only; they do not confer verification.
ALIASES = {
    'boot:001FC520': ('src/game/em_sfx_bank.c', 'em_sfx_service_release'),
    'boot:0017A800': ('src/game/em_aim_fire_control.c', 'pitch_to_target'),
    'boot:0017AF70': ('src/game/em_aim_fire_control.c', 'track_target'),
    'boot:001A56A0': ('src/game/em_actor_collision.c', 'column_round'),
    'overlay:AREA19:008298D0': ('src/game/em_area01_overlay.c', 'em_area01_ovl_00828850'),
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(path):
    if not path.is_file():
        raise ValueError(f'missing required evidence: {path}')
    return json.loads(path.read_text())


def function_key(row):
    address = int(row['addr'], 16)
    region = row.get('region') or ('overlay:' + row['overlay'] if row.get('overlay') else 'boot')
    if address >= 0x823500 and not region.startswith('overlay:'):
        raise ValueError(f'overlay address without resident identity: {region}:{address:08X}')
    return f'{region}:{address:08X}'


def source_definitions(root):
    """Definitions only; mere calls, declarations and address comments do not count."""
    by_symbol = defaultdict(list)
    by_key = defaultdict(list)
    for path in sorted((root / 'src').rglob('*.c')):
        text = path.read_text()
        # Preserve offsets while excluding examples in comments and literals.
        code = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',
                      lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), text, flags=re.S)
        for m in DEFINITION.finditer(code):
            symbol = m[1]
            item = {'file': str(path.relative_to(root)), 'symbol': symbol,
                    'line': text.count('\n', 0, m.start()) + 1}
            by_symbol[symbol].append(item)
            a = re.search(r'(?:_|^e)([0-9A-Fa-f]{6,8})$', symbol)
            if not a:
                continue
            addr = int(a[1], 16)
            if addr < 0x823500:
                region = 'boot'
            else:
                # Multi-overlay translation files identify the resident in
                # their filename. Do not merge different overlays by address.
                area = re.search(r'area(\d\d)', path.name)
                if not area:
                    continue
                region = 'overlay:AREA' + area[1]
            by_key[f'{region}:{addr:08X}'].append(item)
    return by_symbol, by_key


def census_ledgers(root):
    """Keep existing evidence and its exact location; no new route promotion."""
    result = defaultdict(list)
    names = ('FIRST_LEVEL_CENSUS', 'SECOND_LEVEL_CENSUS', 'THIRD_LEVEL_CENSUS')
    for level, name in enumerate(names, 1):
        path = root / 'docs' / (name + '.md')
        for line_no, line in enumerate(path.read_text().splitlines(), 1):
            cells = [x.strip() for x in line.strip().strip('|').split('|')]
            if not line.startswith('|') or len(cells) < 6:
                continue
            a = ADDRESS.fullmatch(cells[0].strip('`'))
            if not a:
                continue
            addr = int(a[1], 16)
            if level == 1:
                region = 'overlay:AREA11' if addr >= 0x823500 else 'boot'
                status, evidence = cells[3], cells[4] + '; ' + cells[5]
            else:
                region = 'boot' if cells[1] == 'boot' else 'overlay:' + cells[1]
                status, evidence = cells[4], cells[5] + '; evidence key ' + cells[6]
            if status not in ('live', 'verified-unbound', 'unverified', 'missing', 'boundary', 'stand-in'):
                continue
            result[f'{region}:{addr:08X}'].append({
                'level': level, 'status': status, 'evidence': evidence,
                'document': str(path.relative_to(root)), 'line': line_no,
            })
    return result


def oracle_entries(root, definitions, owners):
    """Index explicit entry tables, never CALLEES/HOOKS/RUN tables.

    This establishes a test's existence, not a successful execution. Dynamic
    tables and implicit nested entries remain an explicit audit gap.
    """
    result = defaultdict(list)
    for path in sorted((root / 'tools').glob('test_*_reference.py')):
        tree = ast.parse(path.read_text())
        assignments = {t.id: node.value for node in tree.body if isinstance(node, ast.Assign)
                       for t in node.targets if isinstance(t, ast.Name)}
        table = 'FUNCS' if isinstance(assignments.get('FUNCS'), ast.Dict) else 'ENTRIES'
        funcs = assignments.get(table)
        if not isinstance(funcs, ast.Dict):
            continue
        native_files = set(re.findall(r'\bem_\w+\.c\b', path.read_text()))
        addresses = [k.value for k in funcs.keys if isinstance(k, ast.Constant) and isinstance(k.value, int)]
        overlay_map = {}
        if 'OVERLAY_OF' in assignments:
            overlay_map = static_expression(assignments['OVERLAY_OF'], {'FUNCS': addresses, 'ARENA': 0x823500})
        id_to_area = {}
        files = assignments.get('OVERLAY_FILES')
        if isinstance(files, ast.Dict):
            for k, v in zip(files.keys, files.values):
                if not isinstance(k, ast.Constant):
                    continue
                for part in ast.walk(v):
                    if isinstance(part, ast.Constant) and isinstance(part.value, str):
                        area = re.search(r'AREA(\d\d)\.BIN', part.value)
                        if area:
                            id_to_area[k.value] = area[1]
        for node in tree.body:
            if not isinstance(node, ast.Assign) or not any(
                    isinstance(t, ast.Name) and t.id == table for t in node.targets):
                continue
            if not isinstance(node.value, ast.Dict):
                continue
            for key, value in zip(node.value.keys, node.value.values):
                if not isinstance(key, ast.Constant) or not isinstance(key.value, int):
                    continue
                addr = key.value
                symbol = None
                if isinstance(value, (ast.Tuple, ast.List)) and value.elts:
                    v = value.elts[0]
                    if isinstance(v, ast.Constant) and isinstance(v.value, str) and v.value.startswith('em_'):
                        symbol = v.value
                candidates = list(definitions.get(symbol, [])) if symbol else [
                    c for k, cs in owners.items() if k.endswith(f':{addr:08X}')
                    for c in cs if Path(c['file']).name in native_files]
                # Some public entry points are generated by C macros. Their
                # real internal definitions still identify the resident area.
                if symbol and not candidates:
                    stem = symbol.rsplit('_', 1)[0]
                    candidates = [c for k, cs in owners.items() if k.endswith(f':{addr:08X}')
                                  for c in cs if Path(c['file']).stem.startswith(stem)]
                if addr < 0x823500:
                    regions = ['boot']
                else:
                    regions = (['overlay:AREA' + id_to_area[overlay_map[addr]]]
                               if addr in overlay_map and overlay_map[addr] in id_to_area else [])
                    if not regions:
                        regions = sorted({'overlay:AREA' + a[1] for c in candidates
                                      if (a := re.search(r'area(\d\d)', c['file']))})
                    if not regions:
                        area = re.match(r'test_area(\d\d)_', path.name)
                        regions = ['overlay:AREA' + area[1]] if area else []
                for region in regions:
                    result[f'{region}:{addr:08X}'].append({
                        'test': str(path.relative_to(root)), 'line': key.lineno,
                        'symbol': symbol, 'entry_table': table, 'definitions': candidates,
                    })
    return result


def static_expression(node, names):
    """Small non-executing reader for the harnesses' overlay identity tables."""
    if isinstance(node, ast.Constant):
        return node.value
    if isinstance(node, ast.Name):
        return names[node.id]
    if isinstance(node, (ast.Tuple, ast.List)):
        return [static_expression(x, names) for x in node.elts]
    if isinstance(node, ast.Dict):
        return {static_expression(k, names): static_expression(v, names)
                for k, v in zip(node.keys, node.values)}
    if isinstance(node, ast.IfExp):
        return static_expression(node.body if static_expression(node.test, names) else node.orelse, names)
    if isinstance(node, ast.Compare) and len(node.ops) == 1:
        left, right = static_expression(node.left, names), static_expression(node.comparators[0], names)
        op = node.ops[0]
        if isinstance(op, ast.In):
            return left in right
        if isinstance(op, ast.Eq):
            return left == right
        if isinstance(op, ast.GtE):
            return left >= right
    if isinstance(node, ast.DictComp) and len(node.generators) == 1:
        gen = node.generators[0]
        if isinstance(gen.target, ast.Name) and not gen.is_async:
            result = {}
            for item in static_expression(gen.iter, names):
                env = dict(names, **{gen.target.id: item})
                if all(static_expression(test, env) for test in gen.ifs):
                    result[static_expression(node.key, env)] = static_expression(node.value, env)
            return result
    raise ValueError(f'unsupported static overlay identity expression: {type(node).__name__}')


def scene_of(row):
    value = row.get('area4', row.get('area'))
    if not isinstance(value, str) or not re.fullmatch(r'[0-9a-fA-F]{4,8}', value):
        raise ValueError('route row has no recognized area/subarea observation')
    return f'AREA{int(value[:2], 16):02d}/sub{int(value[2:4], 16)}'


def capture_inventory(decomp, groups):
    output = []
    for group in groups:
        legacy = decomp / 'build/s87' / ('route_' + group)
        fork = decomp / 'build/fork_refs/s87' / ('route_' + group)
        paths = sorted(legacy.glob('*/trace.json'))
        if not paths:
            raise ValueError(f'no retained route traces for {group}: {legacy}')
        for p in paths:
            if not p.parent.name.startswith(group + '_'):
                continue
            d = read_json(p)
            rows = d['rows']
            if not rows:
                raise ValueError(f'empty route: {p}')
            scenes = sorted({scene_of(r) for r in rows})
            fpath = fork / p.parent.name / 'trace.json'
            fdata = read_json(fpath) if fpath.is_file() else None
            frows = fdata['rows'] if fdata else []
            # Presence is not parity. Capture snapshots are checked for presence
            # only; instruction/data images are never read by this tool.
            snapshot_files = ('eeMemory.bin', 'scratchpad.bin', 'gs.bin', 'state.p2s')
            output.append({
                'beat': p.parent.name, 'scenes': scenes,
                'legacy_rows': len(rows), 'fork_rows': len(frows),
                'fork_scenes': sorted({scene_of(r) for r in frows}),
                'source': d.get('source'),
                'legacy_trace': str(p.relative_to(decomp)),
                'legacy_trace_sha256': digest(p),
                'fork_trace': str(fpath.relative_to(decomp)) if fdata else None,
                'fork_trace_sha256': digest(fpath) if fdata else None,
                'legacy_missing_snapshot_files': [n for n in snapshot_files if not (p.parent / n).is_file()],
                'fork_missing_snapshot_files': [n for n in snapshot_files if not (fpath.parent / n).is_file()] if fdata else list(snapshot_files),
                'first_counter': rows[0].get('counter'), 'last_counter': rows[-1].get('counter'),
            })
    return output


def asset_inventory(root):
    output = []
    for profile in ASSET_PROFILES:
        doc = root / 'docs' / ('AREA' + profile.upper() + '_ASSETS.md')
        lines = doc.read_text().splitlines()
        outputs = []
        in_outputs = False
        for no, line in enumerate(lines, 1):
            if line.startswith('## '):
                in_outputs = line.startswith('## Outputs')
                continue
            # AREA01's output table precedes its first section heading.
            if line.startswith('| Output'):
                in_outputs = True
            if in_outputs:
                outputs.append({'line': no, 'row': line})
        exporters = [str(p.relative_to(root)) for p in sorted((root / 'tools').glob(f'export_area{profile}_*.py'))]
        checker = f'tools/test_area{profile}_assets_reference.py'
        output.append({'profile': profile, 'document': str(doc.relative_to(root)),
                       'exporters': exporters, 'checker': checker if (root / checker).is_file() else None,
                       'outputs': outputs, 'verification': 'full private rerun recorded in LEVEL_ASSET_DELIVERY.md; coverage remains profile-scoped'})
    return output


def classify(key, owners, ledgers, tests):
    rows = ledgers.get(key, [])
    live = [x for x in rows if x['status'] == 'live']
    if live:
        return 'live', live
    verified = [x for x in rows if x['status'] == 'verified-unbound']
    if verified:
        return 'verified', verified
    executed = [t for t in tests.get(key, []) if t.get('execution_evidence') and t.get('definitions')]
    if owners and executed:
        return 'verified', rows
    # A test file, function name, or DECODED comment is not execution evidence.
    if owners or any(x['status'] in ('unverified', 'stand-in') for x in rows):
        return 'unverified', rows
    return 'missing', rows


def build_report(root, decomp):
    definitions, owners = source_definitions(root)
    for key, (file, symbol) in ALIASES.items():
        owners[key] += [d for d in definitions.get(symbol, []) if d['file'] == file]
    ledgers = census_ledgers(root)
    tests = oracle_entries(root, definitions, owners)
    execution, stale = current_evidence(root)
    for entries in tests.values():
        for entry in entries:
            if entry['test'] in execution:
                row = execution[entry['test']]
                if not any(d['file'] in row['dependencies'] for d in entry['definitions']):
                    continue
                entry['execution_evidence'] = {k: row[k] for k in
                    ('mode', 'source_sha256', 'finished_utc', 'private_log')}
    # The explicit test resident table overrides a filename hint in files that
    # contain more than one overlay (AREA06's file also contains AREA01 code).
    for key, entries in tests.items():
        for entry in entries:
            for owner in entry['definitions']:
                if key.startswith('overlay:'):
                    for other in list(owners):
                        if other.endswith(key[-9:]) and other != key:
                            owners[other] = [o for o in owners[other] if o['file'] != owner['file']]
                if owner not in owners[key]:
                    owners[key].append(owner)
    functions = {}
    levels = []
    sources = {}
    # Boundary descriptions are retained as annotations, never proof of a
    # translated original. Later census ledger rows take precedence.
    boundary_data = read_json(decomp / 'build/s87/census/classified.json')
    boundaries = {function_key(r): r for r in boundary_data['functions']
                  if r['port_status'] == 'boundary'}
    for level, groups in GROUPS.items():
        captures = capture_inventory(decomp, groups)
        scene_by_beat = {c['beat']: c['scenes'] for c in captures}
        keys = set()
        issues = []
        for name in CENSUSES[level]:
            path = decomp / 'build/s87/census' / (name + '.json')
            data = read_json(path)
            relative = str(path.relative_to(decomp))
            sources[relative] = digest(path)
            for k, v in data['summary'].items():
                if ('missing' in k or 'incomplete' in k or k == 'unattributed_hits') and v:
                    issues.append({'source': relative, 'kind': k, 'value': v})
            for row in data['functions']:
                beats = [b for b in row['beats'] if b.split('_')[0] in groups]
                # AREA00's census deliberately includes its arrival in a01_07.
                if level == 3 and 'a01_07_level_exit' in row['beats']:
                    beats.append('a01_07_level_exit')
                if not beats:
                    continue
                key = function_key(row)
                keys.add(key)
                if key not in functions:
                    observed = [function_key(dict(row, addr=a)) for a in row.get('hit_points', [])]
                    # Grouped census entries sometimes start at an unexecuted
                    # overlay pad; retain the group key and name the actual hit.
                    mapped_owners = list(owners.get(key, []))
                    mapped_tests = list(tests.get(key, []))
                    for hit in observed:
                        if hit != key:
                            mapped_owners += [o for o in owners.get(hit, []) if o not in mapped_owners]
                            mapped_tests += [t for t in tests.get(hit, []) if t not in mapped_tests]
                    status, evidence = classify(key, mapped_owners, ledgers, {key: mapped_tests})
                    functions[key] = {
                        'key': key, 'status': status, 'owners': mapped_owners,
                        'oracle_entries': mapped_tests, 'ledger_evidence': evidence,
                        'observed_entry_points': observed,
                        'native_boundary': boundaries[key].get('boundary_kind') if key in boundaries and not evidence and status == 'missing' else None,
                        'boundary_substitute': boundaries[key].get('substitute') if key in boundaries and not evidence and status == 'missing' else None,
                        'uses': [],
                    }
                functions[key]['uses'].append({'level': level, 'beats': sorted(set(beats)),
                                              'census': relative, 'bytes': row['size'],
                                              'scene_candidates': sorted({s for b in beats for s in scene_by_beat.get(b, [])}),
                                              'scene_attribution': 'beat observations, not per-function hits',
                                              'first_beat': row.get('first_beat'),
                                              'first_frame': row.get('first_frame')})
        if not keys:
            raise ValueError(f'empty function inventory for level {level}')
        area_index = {}
        for scene in sorted({s for c in captures for s in c['scenes']}):
            area_index[scene] = sorted(k for k in keys if any(
                u['level'] == level and scene in u['scene_candidates'] for u in functions[k]['uses']))
        levels.append({'level': level, 'document': 'docs/' + ORDINALS[level - 2] + '_LEVEL_ROUTE.md',
                       'groups': list(groups), 'functions': sorted(keys), 'captures': captures,
                       'area_function_candidates': area_index,
                       'census_issues': issues})
    return {'schema': 1, 'sources': sources, 'levels': levels,
            'oracle_audit': {'current_suites': len(execution), 'stale_tests': stale},
            'functions': sorted(functions.values(), key=lambda x: x['key']),
            'assets': asset_inventory(root)}


def link(path, label=None):
    return f'[{label or Path(path).name}](../{path})'


def markdown(report):
    functions = {f['key']: f for f in report['functions']}
    lines = [
        '# Level readiness: levels 2–15', '',
        'Generated by `python3 tools/level_readiness.py --write`. Native arm64 macOS; '
        'only local metadata is read. No game images, instruction listings, captures or assets are embedded.', '',
        '## Evidence rules', '',
        '- **L — already live:** the current first/second-level census cites native route evidence. '
        'Reuse still needs a comparison in the new area; L does not certify that new route.',
        '- **V — translated and oracle-verified:** an existing census ledger cites executed original-instruction '
        'comparisons, or a passing audit receipt matches the current native sources and local test dependencies. '
        'Receipt-backed results are linked in the function table; older ledger evidence remains historical.',
        '- **U — translated but unverified here:** a definition exists, but this map has not accepted execution '
        'evidence for it. An explicit oracle entry is listed separately. Some have historical full sweeps in '
        'their module docs; step 2 must audit/rerun them before promotion.',
        '- **M — missing:** no definition or reviewed owner mapping was found. This is a conservative audit '
        'queue, not proof that no implementation exists. Dynamic entry tables and macro/alias owners need review.',
        '- **Native boundary:** retained SDK/driver classification, excluded from the game-translation backlog. '
        'Its substitute is not an original translation; a boundary never gains V from a hook or a range label.', '',
        'Function keys include the resident overlay. All census `functions` rows are used, not just '
        '`new_functions`. Combined census reports are filtered by actual beat membership; repeated rows '
        'are unioned. Census frames are not fork recording frames. Boot hits in a transition beat cannot '
        'be assigned to one subarea without per-hit residency: each such beat retains all observed scenes, '
        'rather than inventing a function/subarea attribution. The JSON report retains every beat and source.', '',
        '## Ranked work', '',
        '1. Audit the U/M queue and reuse the existing original-instruction harnesses. The level 8–14 '
        'harnesses already contain entry tables, original-image identity checks, ordered call/store comparisons '
        'and coverage assertions. Their presence alone does not promote a row. Keep additions opt-in and unwired.',
        '2. Recorded-area exports now pass: sixteen texture/shadow profile-subareas, fifteen original module '
        'deliveries, and thirteen area asset profiles including AREA03 arrival. Zero-key runtime materials '
        'and unrecorded events/reloads remain explicit limits. See LEVEL_ASSET_DELIVERY.md.',
        '3. The largest fixed texture catalog has 609 keys; AREA00 now has 580 after page discovery. '
        'The allowed 1024-slot hash registry passes all required default/GPU regressions. SFX scope union '
        'requires 14274 entries against 4096 and exposes two inferred-office conflicts; main owns that binding decision.',
        '4. Restore the missing fork route chain from a04b_00, then record later groups. Existing legacy '
        'captures remain original evidence; fork file presence alone is not parity or successful replay. '
        'Record displayed fields and missing boundary counters where needed; use the shared run lock.',
        '5. Continue AREA00 arrival at `D_00248290` / `0015AB00` after capacity proof, then neutral frames, '
        'movement, camera, drawing, interactions and both returns. Preserve the strict AREA01 prerequisite '
        'voice discrepancy and every first divergent row. See ' + link('docs/LEVEL3_BINDING.md') + '.', '',
        '## Route coverage and function counts', '',
        'Counts are unique `(region, address)` per level; reused rows occur in several levels. '
        'The native-boundary column is a subset of M, not additional missing game logic.', '',
        '| Level | Route groups | Functions | L | V | U | M | Native boundary | Legacy beats | Fork traces present |',
        '|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|',
    ]
    for level in report['levels']:
        fs = [functions[k] for k in level['functions']]
        counts = Counter(f['status'] for f in fs)
        caps = level['captures']
        lines.append(f"| {level['level']} | {', '.join(level['groups'])} | {len(fs)} | " +
                     ' | '.join(str(counts[s]) for s in STATUS) +
                     f" | {sum(bool(f['native_boundary']) for f in fs)} | {len(caps)} | {sum(bool(c['fork_rows']) for c in caps)} |")
    for level in report['levels']:
        n = level['level']
        lines += ['', f'### Level {n}', '', link(level['document']), '',
                  '| Beat | Observed area/subarea (legacy) | Legacy rows | Fork rows | Fork snapshot files missing |',
                  '|---|---|---:|---:|---|']
        for c in level['captures']:
            lines.append(f"| `{c['beat']}` | {', '.join(c['scenes'])} | {c['legacy_rows']} | {c['fork_rows']} | " +
                         (', '.join(c['fork_missing_snapshot_files']) if c['fork_rows'] else 'not recorded') + ' |')
        lines += ['', '| Area/subarea | Functions associated with its beats |', '|---|---:|']
        for scene, keys in level['area_function_candidates'].items():
            lines.append(f'| {scene} | {len(keys)} |')
        lines += ['', 'These are conservative beat associations. On transition beats the census does '
                  'not establish the subarea of each individual hit. Query the exact candidate list with '
                  f'`--level {n} --area AREAxx/subN`; the JSON retains that limitation per use.']
        for issue in level['census_issues']:
            lines += ['', f"Census limit: `{issue['kind']}` in `{issue['source']}`: `{json.dumps(issue['value'], sort_keys=True)}`."]
    lines += ['', '## Asset exporters and loader outputs', '',
              'The current private export and reference-test receipts are recorded in '
              '[LEVEL_ASSET_DELIVERY.md](LEVEL_ASSET_DELIVERY.md). The rows below preserve each '
              'asset document’s output contract; old refusal notes must be reconciled with newer bindings. '
              'Descriptive JSON/scene/material sidecars are not byte-for-byte runtime proof.', '',
              '| Area | World/object texture catalog | Shadow receivers | Module delivery | Remaining common categories |',
              '|---|---|---|---|---|']
    for area in AREAS:
        texture = f'tools/export_area{area}_world_textures.py'
        shadow = f'tools/export_area{area}_shadow_receivers.py'
        delivery = 'complete sector delivery and original-loader proof: LEVEL_ASSET_DELIVERY.md'
        lines.append(f'| AREA{area} | ' + (link(texture) if (ROOT / texture).exists() else link('tools/export_level_world_textures.py')) +
                     ' | ' + (link(shadow) if (ROOT / shadow).exists() else link('tools/export_level_shadow_receivers.py')) +
                     f' | {delivery} | collision, tables, doors, SFX, models, messages, scripts: profiles below |')
    for a in report['assets']:
        lines += ['', f"### AREA{a['profile'].upper()} export profile", '', link(a['document']), '',
                  'Exporters: ' + ', '.join(link(p) for p in a['exporters']) + '.', '',
                  'Reference: ' + (link(a['checker']) if a['checker'] else 'missing') + '. ' + a['verification'] + '.', '']
        lines += [o['row'] for o in a['outputs']]
    lines += ['', '## Capacity limits', '',
              '| Resource | Current capacity | Measured requirement | Work remaining |',
              '|---|---:|---|---|',
              '| Object textures | 1024 (`src/em_gfx.h`), hash lookup | Fixed maximum 609; AREA00 580; unresolved zero keys retained separately | All required default/GPU regressions pass; runtime materials need main |',
              '| SFX entries / samples | 4096 / 1024 (`em_sfx_bank.c`) | Available catalog union 14274 / 348; two conflicting office entries | Complete per-area selection or larger combined registry and corrected office binding require main |',
              '| Actor records | 256 (`em_actor_pool.h`) | Snapshot maximum 134; original free counters agree | Whole-route peaks remain unmeasured |',
              '| Effect entities / selected receivers | 128 / 512 | Effect snapshots contain zero live entries; all sixteen full receiver banks parse | Snapshot absence is not a route peak; selected receiver peak remains unmeasured |', '',
              '## Capture limits still requiring work', '',
              '- The retained legacy censuses are route-shape coverage. Some census replays differ from their '
              'recordings, and overlay swaps can leave blind spots. No full fork census is inferred from a route trace.',
              '- a04b_00 is not a completed fork beat. Later groups have legacy traces but no fork chain. '
              'The earlier fork chain diverges from the legacy path at a02_02 after disc timing differences.',
              '- Level 10 has unrecorded between-beat counters; levels 11 and 12 also record boundary gaps. '
              'The level-12 overlay blind spot is supplemented by a19b_swap_delta and a19b_area13_delta '
              '(level 13’s documented remeasurement).',
              '- AREA03 has arrival and lift coverage only; its exit and subareas 1/2 are outside the retained '
              'level-15 route. Unvisited subareas of other areas and unplayed branches remain unverified.',
              '- Per-row game state, endpoint RAM/GS, displayed fields, audio and census coverage are different '
              'proofs. A saved GS image alone does not prove the displayed framebuffer. Later live pixels '
              'and SPU residency remain separate capture work.', '',
              '## Requests for main', '',
              'The explicit texture capacity/index exception passes its regression proof. The AREA04 zero-key original-draw '
              'counterexample and required runtime texture handling are recorded in LEVEL_ASSET_DELIVERY.md. '
              'They remain outside the allowed texture-capacity exception. '
              'Known prerequisites: AREA01’s strict voice timing discrepancy; any SFX/pool capacity '
              'change proven necessary by the complete-area measurements.', '',
              '## Readiness checkpoint (2026-10-18)', '',
              'Thirteen disc-free attribution/receipt tests pass, including overlay identity, hook exclusion, '
              'combined-report membership, missing evidence, and snapshot completeness. The report '
              'regenerates deterministically from the retained metadata. The staged-index native build '
              'passes with zero compiler warnings (`build/levels/readiness-index-build.log`). '
              'No native gameplay or pixel pass is claimed by this tooling-only checkpoint. '
              f"The current standalone audit has {report['oracle_audit']['current_suites']} source-matching passing receipts; "
              f"{len(report['oracle_audit']['stale_tests'])} receipts are stale. "
              'See [LEVEL_ORACLE_AUDIT.md](LEVEL_ORACLE_AUDIT.md) for the new translations and remaining '
              'step-2 gaps. [LEVEL_ASSET_DELIVERY.md](LEVEL_ASSET_DELIVERY.md) records the stage-3 '
              'private exports and remaining capacity work; new captures and AREA00 continuation are still pending.', '',
              '## Complete function index', '',
              'Every retained function has one row below; the level column is its membership set. '
              'For exact beats, first-hit metadata, census hashes, owner definitions and test entry locations, '
              'generate the ignored JSON report. The full per-level sets are the rows containing that level. '
              'An entry-table link alone means a test exists. A current-pass annotation additionally requires '
              'a passing receipt whose source fingerprint still matches. Ledger links retain '
              'historical evidence and limits. `—` means the automated index found none.', '',
              '| Original region/address | Levels | Status | Definition or existing ledger | Explicit oracle entry |',
              '|---|---|---|---|---|']
    letters = dict(zip(STATUS, ('L', 'V', 'U', 'M')))
    for f in report['functions']:
        levels = ','.join(map(str, sorted({u['level'] for u in f['uses']})))
        evidence = ', '.join(link(e['document'], f"L{e['level']} census:{e['line']}") for e in f['ledger_evidence'])
        owner = ', '.join(link(o['file'], o['symbol']) for o in f['owners'])
        if f['native_boundary']:
            evidence = 'native boundary: ' + f['native_boundary']
        lines.append(f"| `{f['key']}` | {levels} | {letters[f['status']]} | {evidence or owner or '—'} | " +
                     (', '.join(link(t['test']) + (' (current ' + t['execution_evidence']['mode'] + ' pass)' if t.get('execution_evidence') else '') for t in f['oracle_entries']) or '—') + ' |')
    lines += ['', '## Reproduce', '', '```sh',
              'python3 tools/level_readiness.py --write --json build/levels/readiness.json',
              'python3 tools/level_readiness.py --check',
              'python3 tools/level_readiness.py --level 15 --area AREA03/sub0',
              'python3 tools/test_level_readiness.py', '```', '',
              'No emulator or exporter is run by these commands. Missing required metadata fails closed; '
              'a missing fork recording is reported explicitly. Generated assets and capture receipts '
              'belong in ignored private directories and must never be staged.', '']
    return '\n'.join(lines)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--decomp', type=Path, default=ROOT.parent / 'Extermination')
    p.add_argument('--json', type=Path, help='optional local metadata report (use ignored build/)')
    p.add_argument('--level', type=int, choices=GROUPS, help='list the original functions associated with this level')
    p.add_argument('--area', help='restrict the level query to an observed AREAxx/subN (beat candidates)')
    mode = p.add_mutually_exclusive_group()
    mode.add_argument('--write', action='store_true', help='regenerate docs/LEVEL_READINESS.md')
    mode.add_argument('--check', action='store_true', help='fail if the checked-in report is stale')
    args = p.parse_args()
    if args.area and args.level is None:
        p.error('--area requires --level')
    report = build_report(ROOT, args.decomp)
    rendered = markdown(report)
    target = ROOT / 'docs/LEVEL_READINESS.md'
    if args.write:
        target.write_text(rendered)
    if args.check and (not target.exists() or target.read_text() != rendered):
        raise SystemExit('readiness report is stale; run with --write')
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    counts = Counter(f['status'] for f in report['functions'])
    print(f"readiness: {len(report['levels'])} levels, {len(report['functions'])} unique census entries; " +
          ', '.join(f'{s}={counts[s]}' for s in STATUS))
    if args.level is not None:
        level = next(l for l in report['levels'] if l['level'] == args.level)
        if args.area and args.area not in level['area_function_candidates']:
            p.error('area/subarea was not observed on this level; available: ' + ', '.join(level['area_function_candidates']))
        keys = set(level['area_function_candidates'][args.area] if args.area else level['functions'])
        for f in report['functions']:
            if f['key'] in keys:
                print(f"{f['key']} {f['status']} " + ', '.join(o['file'] + ':' + o['symbol'] for o in f['owners']))


if __name__ == '__main__':
    main()
