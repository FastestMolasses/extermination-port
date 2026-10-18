"""Private-output guards and profile dependency ordering, without game data."""
from pathlib import Path
import tempfile
import unittest

import level_asset_audit as A


class AssetAuditTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix='levels-asset-guard-')).resolve()
        self.created = []

    def tearDown(self):
        for path in reversed(self.created):
            if path.is_symlink() or path.is_file():
                path.unlink()
            elif path.exists():
                path.rmdir()
        self.root.rmdir()

    def test_fresh_private_tree(self):
        self.assertEqual(A.private_assets(self.root), self.root/'assets')
        assets = self.root/'assets'
        assets.mkdir()
        self.created.append(assets)
        self.assertEqual(A.private_assets(self.root), assets)

    def test_symlink_root_or_descendant_refused(self):
        assets = self.root/'assets'
        assets.mkdir()
        self.created.append(assets)
        child = assets/'redirect'
        child.symlink_to(self.root, target_is_directory=True)
        self.created.append(child)
        with self.assertRaises(ValueError):
            A.private_assets(self.root)
        link = self.root/'worktree-link'
        link.symlink_to(self.root, target_is_directory=True)
        self.created.append(link)
        with self.assertRaises(ValueError):
            A.private_assets(link)

    def test_asset_root_symlink_refused(self):
        assets = self.root/'assets'
        assets.symlink_to(self.root, target_is_directory=True)
        self.created.append(assets)
        with self.assertRaises(ValueError):
            A.private_assets(self.root)

    def test_profile_order(self):
        for profile in A.PROFILES:
            jobs = A.commands(profile)
            self.assertEqual(jobs[-1], ('verify', f'tools/test_area{profile}_assets_reference.py'))
            self.assertEqual(len(jobs), 5 if profile in A.SPLIT else 4)
            if profile in A.REQUIRES:
                self.assertLess(A.PROFILES.index(A.REQUIRES[profile]), A.PROFILES.index(profile))
        with self.assertRaises(ValueError):
            A.commands('03')


if __name__ == '__main__':
    unittest.main()
