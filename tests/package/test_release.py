# SPDX-License-Identifier: Apache-2.0
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('wirelink_release', Path(__file__).with_name('release.py'))
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class InventoryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        manylinux = self.root / 'generated-sdks-manylinux_2_28-x86_64'
        manylinux.mkdir()
        for package in release.PACKAGES:
            for abi in release.ABIS:
                for platform in ('manylinux_2_28_x86_64', 'macosx_13_0_x86_64', 'macosx_13_0_arm64', 'win_amd64'):
                    (manylinux / f'{package}-0.13.0-{abi}-{abi}-{platform}.whl').touch()
            (manylinux / f'{package}-0.13.0.tar.gz').touch()

    def test_complete_supported_inventory(self):
        self.assertEqual(len(release.inventory(self.root, '0.13.0')), 42)

    def test_missing_and_duplicate_platforms_fail_even_at_forty_wheels(self):
        wheel = next(self.root.rglob('*.whl'))
        wheel.unlink()
        with self.assertRaises(ValueError):
            release.inventory(self.root, '0.13.0')
        remaining = next(self.root.rglob('*.whl'))
        (self.root / remaining.name).touch()
        with self.assertRaises(ValueError):
            release.inventory(self.root, '0.13.0')

    def test_abi_version_and_platform_contracts(self):
        for name in ('wirelink_calculator_sdk-0.12.0-cp310-cp310-win_amd64.whl',
                     'wirelink_calculator_sdk-0.13.0-cp314-cp314t-win_amd64.whl',
                     'wirelink_calculator_sdk-0.13.0-cp314-cp314-linux_x86_64.whl',
                     'wirelink_calculator_sdk-0.13.0-cp314-cp314-macosx_15_0_arm64.whl'):
            with self.assertRaises(ValueError):
                release.wheel_key(name, '0.13.0')

    def test_both_tested_source_distributions_are_required(self):
        next(self.root.rglob('*.tar.gz')).unlink()
        with self.assertRaises(ValueError):
            release.inventory(self.root, '0.13.0')

    def test_current_pair_and_release_notes_are_consistent(self):
        value = release.validate_source(release.ROOT, 'v0.13.0')
        self.assertEqual(value['wlc']['codegen_contract'], '0.13')
        with self.assertRaises(AssertionError):
            release.validate_source(release.ROOT, 'v0.8.0')


if __name__ == '__main__':
    unittest.main()
