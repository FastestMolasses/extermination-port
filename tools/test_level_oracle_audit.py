#!/usr/bin/env python3
"""Disc-free checks that stale receipts cannot certify changed translations."""
import json
from pathlib import Path
import tempfile
import unittest

import level_oracle_audit as A


class EvidenceTests(unittest.TestCase):
    def test_dependency_closure_and_stale_receipt(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for directory in ('tools', 'src/game', 'docs'):
                (root / directory).mkdir(parents=True)
            (root / 'tools/test_example_reference.py').write_text(
                "import helper\nSOURCES = ['src/game/em_example.c']\n")
            (root / 'tools/helper.py').write_text('VALUE = 1\n')
            (root / 'src/game/em_example.c').write_text('#include "em_example.h"\n')
            header = root / 'src/game/em_example.h'
            header.write_text('#define VALUE 1\n')
            test = 'tools/test_example_reference.py'
            paths = A.dependencies(root, test)
            self.assertEqual(len(paths), 4)
            row = {'test': test, 'dependencies': paths, 'source_sha256': A.fingerprint(root, paths),
                   'exit_code': 0, 'mode': 'quick'}
            evidence = root / 'docs/LEVEL_ORACLE_EVIDENCE.json'
            evidence.write_text(json.dumps({'schema': 1, 'suites': [row]}))
            self.assertIn(test, A.current_evidence(root)[0])
            header.write_text('#define VALUE 2\n')
            valid, stale = A.current_evidence(root)
            self.assertFalse(valid)
            self.assertEqual(stale, [test])

    def test_failed_or_source_free_suite_never_certifies(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'tools').mkdir()
            (root / 'tools/test_empty_reference.py').write_text('HOOKS = {1: 2}\n')
            with self.assertRaisesRegex(ValueError, 'no native sources'):
                A.dependencies(root, 'tools/test_empty_reference.py')


if __name__ == '__main__':
    unittest.main()
