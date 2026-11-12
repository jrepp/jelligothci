"""The browser shard plan discovers every test file exactly once."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("ui_shards", ROOT / "tools/jelli-art/ui_tests/shards.py")
shards = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(shards)


class UiShardTests(unittest.TestCase):
    def test_current_suite_has_complete_disjoint_stable_shards(self):
        first = shards.partition(shards.TESTS)
        self.assertEqual(first, shards.partition(shards.TESTS))
        self.assertEqual(len(first), 3)
        files = [file for group in first for file in group]
        self.assertEqual(len(files), len(set(files)))
        self.assertEqual(set(files), {p for p in shards.TESTS.rglob("*.py") if p.name.startswith("test_") or p.name.endswith("_test.py")})
        self.assertTrue(all(first))

    def test_new_file_is_included_and_invalid_counts_fail(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            for index in range(4):
                (directory / f"test_{index}.py").write_text("x" * (index + 1))
            (directory / "helper.py").write_text("helper")
            nested = directory / "nested"
            nested.mkdir()
            (nested / "example_test.py").write_text("test")
            before = shards.partition(directory)
            added = directory / "test_added.py"
            added.write_text("new")
            after = shards.partition(directory)
            self.assertNotIn(added, [p for group in before for p in group])
            self.assertEqual([p for group in after for p in group].count(added), 1)
            with self.assertRaises(ValueError):
                shards.partition(directory, 0)
            with self.assertRaises(ValueError):
                shards.partition(directory, 7)


if __name__ == "__main__":
    unittest.main()
