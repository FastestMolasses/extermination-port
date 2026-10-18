#!/usr/bin/env python3
"""Fast, disc-free checks for readiness evidence attribution (opt-in)."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import level_readiness as R


class ReadinessTests(unittest.TestCase):
    def supplemental_fixture(self, root):
        name = 'a04b_00_synthetic'
        folder = root / 'build/fork_refs/synthetic/s87/route_a04b' / name
        folder.mkdir(parents=True)
        trace = folder / 'trace.json'
        trace.write_text(json.dumps({'beat': name, 'emulator': 'fork', 'teleports': [],
                                    'rows': [{'area4': '04000000', 'counter': 1}]}))
        status = root / 'build/fork_refs/synthetic/progress.json'
        status.write_text(json.dumps({'results': [{'name': name, 'result': 'passed'}]}))
        driver = status.parent / 'driver.py'
        driver.write_text('# synthetic provenance fixture, never executed\n')
        data = {'schema': 1, 'beats': {name: {
            'trace': str(trace.relative_to(root)), 'trace_sha256': R.digest(trace),
            'run_status': str(status.relative_to(root)),
            'driver': str(driver.relative_to(root)), 'driver_sha256': R.digest(driver),
        }}}
        (root / 'docs').mkdir()
        index = root / 'docs/LEVEL_ROUTE_EVIDENCE.json'
        index.write_text(json.dumps(data))
        return name, trace, status, index, data

    def test_supplemental_capture_requires_matching_trace_and_resume_result(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            name, trace, status, _, _ = self.supplemental_fixture(root)
            self.assertEqual(R.supplemental_captures(root, root), {name: trace.resolve()})
            status.write_text(json.dumps({'results': [{'name': name, 'result': 'failed'}]}))
            with self.assertRaisesRegex(ValueError, 'resume check'):
                R.supplemental_captures(root, root)

    def test_supplemental_capture_rejects_changed_trace(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _, trace, _, _, _ = self.supplemental_fixture(root)
            trace.write_text(trace.read_text() + '\n')
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                R.supplemental_captures(root, root)

    def test_supplemental_capture_rejects_changed_driver(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            name, _, _, _, data = self.supplemental_fixture(root)
            (root / data['beats'][name]['driver']).write_text('# changed synthetic driver\n')
            with self.assertRaisesRegex(ValueError, 'capture-driver hash mismatch'):
                R.supplemental_captures(root, root)

    def test_supplemental_capture_cannot_escape_private_capture_root(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            name, _, _, index, data = self.supplemental_fixture(root)
            data['beats'][name]['trace'] = 'build/fork_refs/../../outside/trace.json'
            index.write_text(json.dumps(data))
            with self.assertRaisesRegex(ValueError, 'escapes fork_refs'):
                R.supplemental_captures(root, root)

    def boundary_fixture(self, root):
        name, trace, _, index, data = self.supplemental_fixture(root)
        source = trace.parent.parent / 'synthetic_source'
        source.mkdir()
        state = source / 'state.p2s'
        state.write_bytes(b'synthetic state, not a save state')
        previous = source / 'trace.json'
        previous.write_text(json.dumps({'rows': [{'counter': -2}]}))
        target = json.loads(trace.read_text())
        target['source_state'] = str(state)
        trace.write_text(json.dumps(target))
        entry = data['beats'][name]
        entry['trace_sha256'] = R.digest(trace)
        folder = root / 'build/fork_refs/boundaries' / name
        folder.mkdir(parents=True)
        boundary = folder / 'trace.json'
        record = {'name': name, 'target_trace': entry['trace'], 'target_trace_sha256': entry['trace_sha256'],
                  'source_trace': str(previous.relative_to(root)), 'source_trace_sha256': R.digest(previous),
                  'source_state_sha256': R.digest(state), 'gap': 3, 'missing_counters': 2, 'differing_keys': [],
                  'rows': [dict(target['rows'][0], counter=-2 + i, boundary_frame=i) for i in range(4)]}
        boundary.write_text(json.dumps(record))
        status = folder.parent / 'results.json'
        status.write_text(json.dumps({'results': [dict(record, result='passed')]}))
        entry['boundary'] = {'trace': str(boundary.relative_to(root)), 'trace_sha256': R.digest(boundary),
                             'run_status': str(status.relative_to(root))}
        index.write_text(json.dumps(data))
        return name, entry, target, boundary, record, state

    def test_boundary_matches_all_endpoint_fields_and_source(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            name, entry, target, _, _, state = self.boundary_fixture(root)
            result = R.verified_boundary(root, name, entry, target)
            self.assertEqual(result['missing_counters_recorded'], 2)
            state.write_bytes(b'changed synthetic state')
            with self.assertRaisesRegex(ValueError, 'source hash mismatch'):
                R.verified_boundary(root, name, entry, target)

    def test_boundary_cannot_hide_changed_endpoint_field(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            name, entry, target, path, record, _ = self.boundary_fixture(root)
            record['rows'][-1]['area4'] = '05000000'
            path.write_text(json.dumps(record))
            entry['boundary']['trace_sha256'] = R.digest(path)
            with self.assertRaisesRegex(ValueError, 'endpoint differs'):
                R.verified_boundary(root, name, entry, target)

    def test_boundary_missing_counter_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            name, entry, target, path, record, _ = self.boundary_fixture(root)
            record['rows'][1]['counter'] += 1
            path.write_text(json.dumps(record))
            entry['boundary']['trace_sha256'] = R.digest(path)
            with self.assertRaisesRegex(ValueError, 'nonconsecutive'):
                R.verified_boundary(root, name, entry, target)

    def test_overlay_identity_required(self):
        a = {'addr': '0x823580', 'region': 'overlay:AREA00'}
        b = dict(a, region='overlay:AREA01')
        self.assertNotEqual(R.function_key(a), R.function_key(b))
        with self.assertRaises(ValueError):
            R.function_key(dict(a, region='boot'))

    def test_hook_is_not_an_entry_or_proof(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'tools').mkdir()
            (root / 'tools/test_fake_reference.py').write_text(
                "FUNCS = {0x123456: ('em_fake_00123456', 4, [], None)}\n"
                "HOOKS = {0x123460: ('em_fake_00123460', 4, [], None)}\n"
                "CALLEES = {0x123464: 'run'}\n")
            tests = R.oracle_entries(root, {}, {})
            self.assertEqual(list(tests), ['boot:00123456'])
            status, _ = R.classify('boot:00123456', [{'symbol': 'em_fake'}], {}, tests)
            self.assertEqual(status, 'unverified')

    def test_resident_table_overrides_source_filename(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'tools').mkdir()
            (root / 'tools/test_area06_port_reference.py').write_text(
                "FUNCS = {0x826950: ('em_area06_port_00826950', 4, [], None)}\n"
                "OVERLAY_FILES = {2: DECOMP / 'extract/OVERLAY/AREA01.BIN'}\n"
                "OVERLAY_OF = {entry: 2 for entry in FUNCS if entry >= ARENA}\n")
            owner = {'file': 'src/game/em_area06_port_overlay.c', 'symbol': 'em_area06_port_00826950'}
            entries = R.oracle_entries(root, {owner['symbol']: [owner]}, {})
            self.assertEqual(list(entries), ['overlay:AREA01:00826950'])

    def test_static_identity_table_cannot_execute_code(self):
        import ast
        with self.assertRaises(ValueError):
            R.static_expression(ast.parse('start_emulator()', mode='eval').body, {})

    def test_native_entry_table_is_indexed_but_size_table_alone_is_not(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'tools').mkdir()
            (root / 'tools/test_area00_fx_reference.py').write_text(
                "SIZES = {0x123456: 16, 0x123460: 32}\n"
                "ENTRIES = {0x123456: ('rr', None)}\n"
                "SOURCES = ['src/game/em_area00_fx_gs.c']\n")
            owner = {'file': 'src/game/em_area00_fx_gs.c', 'symbol': 'e00123456'}
            entries = R.oracle_entries(root, {}, {'boot:00123456': [owner]})
            self.assertEqual(list(entries), ['boot:00123456'])
            self.assertEqual(entries['boot:00123456'][0]['definitions'], [owner])
            self.assertEqual(entries['boot:00123456'][0]['entry_table'], 'ENTRIES')

    def test_passing_receipt_requires_an_identified_native_definition(self):
        key = 'boot:00123456'
        test = {'execution_evidence': {'mode': 'quick'}, 'definitions': []}
        self.assertEqual(R.classify(key, [{}], {}, {key: [test]})[0], 'unverified')
        test['definitions'] = [{'file': 'owned.c'}]
        self.assertEqual(R.classify(key, [{}], {}, {key: [test]})[0], 'verified')

    def test_definitions_not_calls_comments_or_declarations(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'src').mkdir()
            (root / 'src/em_level11_port_area13.c').write_text(
                '/* int em_fake_00123450(void) {} */\n'
                'int em_fake_00123454(void);\n'
                'int l11_00823580(void) { return 0; }\n'
                'int em_real_00123458(void) { em_call_0012345C(); return 0; }\n'
                'static int e00123460(void) { return 0; }\n')
            _, owners = R.source_definitions(root)
            self.assertEqual(set(owners), {'overlay:AREA13:00823580', 'boot:00123458', 'boot:00123460'})

    def test_missing_trace_fails_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(ValueError, 'no retained route traces'):
                R.capture_inventory(Path(tmp), ('a03',))

    def test_capture_presence_not_parity_or_snapshot_completeness(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for prefix in ('build/s87', 'build/fork_refs/s87'):
                folder = root / prefix / 'route_a00/a00_00_example'
                folder.mkdir(parents=True)
                (folder / 'trace.json').write_text(json.dumps({'rows': [
                    {'area4': '00000000', 'counter': 1},
                    {'area4': '00010000', 'counter': 2},
                ]}))
            row, = R.capture_inventory(root, ('a00',))
            self.assertEqual(row['scenes'], ['AREA00/sub0', 'AREA00/sub1'])
            self.assertEqual(row['fork_rows'], 2)
            self.assertEqual(len(row['fork_missing_snapshot_files']), 4)
            self.assertNotIn('parity', row)

    def test_combined_census_filtered_by_beats_and_unioned(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            folder = root / 'build/s87/census'
            folder.mkdir(parents=True)
            (folder / 'classified.json').write_text('{"functions": []}')
            rows = [
                {'addr': '0x123456', 'region': 'boot', 'beats': ['a15b_00_event'], 'size': 16},
                {'addr': '0x123460', 'region': 'boot', 'beats': ['a03_00_elevator'], 'size': 32},
                {'addr': '0x123464', 'region': 'boot', 'beats': ['a19_00_old'], 'size': 48},
            ]
            for name in ('combined', 'supplement'):
                (folder / (name + '.json')).write_text(json.dumps({'summary': {}, 'functions': rows}))
            with patch.object(R, 'GROUPS', {15: ('a15b', 'a03')}), \
                    patch.object(R, 'CENSUSES', {15: ('combined', 'supplement')}), \
                    patch.object(R, 'census_ledgers', return_value={}), \
                    patch.object(R, 'capture_inventory', return_value=[]), \
                    patch.object(R, 'asset_inventory', return_value=[]):
                report = R.build_report(root, root)
            self.assertEqual(report['levels'][0]['functions'], ['boot:00123456', 'boot:00123460'])
            self.assertEqual(len(report['functions']), 2)
            self.assertEqual(len(report['functions'][0]['uses']), 2)

    def test_ledger_retains_provenance_without_new_route_promotion(self):
        evidence = {'level': 1, 'status': 'live', 'document': 'old-ledger', 'line': 10}
        status, retained = R.classify('boot:00123456', [], {'boot:00123456': [evidence]}, {})
        self.assertEqual(status, 'live')
        self.assertEqual(retained, [evidence])
        self.assertEqual(retained[0]['level'], 1)


if __name__ == '__main__':
    unittest.main()
