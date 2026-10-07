"""Behavior checks for the C size gate; fixtures never enter the source tree."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "check_c_size", Path(__file__).resolve().parents[1] / "scripts/check-c-size.py"
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SizeGateTests(unittest.TestCase):
    limits = {
        "max_function_nloc": 80,
        "max_file_lines": 400,
        "max_cyclomatic_complexity": 20,
    }

    def check(self, source):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.c"
            path.write_text(source)
            return module.check_file(path, self.limits)

    def test_small_function_passes(self):
        self.assertEqual(self.check("int increment(int x) { return x + 1; }\n"), [])

    def test_large_file_fails_even_with_blank_lines(self):
        self.assertTrue(any("file limit" in issue for issue in self.check("\n" * 401)))

    def test_large_function_fails(self):
        source = "int long_function(int x) {\n" + "    x += 1;\n" * 80 + "return x;\n}\n"
        self.assertTrue(any("nloc=" in issue for issue in self.check(source)))

    def test_complex_function_fails(self):
        branches = "".join(f"if (x == {n}) return {n};\n" for n in range(21))
        issues = self.check("int branches(int x) {\n" + branches + "return -1;\n}\n")
        self.assertTrue(any("cyclomatic_complexity=" in issue for issue in issues))

    def test_comment_text_does_not_inflate_function_nloc(self):
        source = "int small(void) {\n" + "/* explanation */\n" * 85 + "return 0;\n}\n"
        self.assertEqual(self.check(source), [])


if __name__ == "__main__":
    unittest.main()
