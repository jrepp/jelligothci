#!/usr/bin/env python3
"""Regression tests for missing packaged dependencies, using isolated contexts."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('packaging_check', Path(__file__).with_name('check-studio-container.py'))
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class ContainerCheckTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.write('.dockerignore', '*\n!tools/jelli-art/\n!tools/assets/effect_data.py\n!tools/assets/scene_data.py\n!tools/assets/compare.html\n')
        self.write('tools/jelli-art/Containerfile', '''COPY tools/jelli-art/ /app/tools/jelli-art/
COPY tools/assets/effect_data.py tools/assets/scene_data.py tools/assets/compare.html /app/tools/assets/
ENTRYPOINT ["/app/tools/jelli-art/entrypoint.sh"]
HEALTHCHECK CMD ["python", "/app/tools/jelli-art/healthcheck.py"]
''')
        self.write('tools/jelli-art/entrypoint.sh', '#!/bin/sh\n')
        (self.root / 'tools/jelli-art/entrypoint.sh').chmod(0o755)
        self.write('tools/jelli-art/healthcheck.py', 'import json\n')
        self.write('tools/jelli-art/jelli_art.py', 'import effect_data\nPAGE_SCRIPTS = (HERE / "shell.js",)\n')
        self.write('tools/jelli-art/shell.js', '// browser code\n')
        self.write('tools/assets/effect_data.py', 'from scene_data import fields\n')
        self.write('tools/assets/scene_data.py', 'fields = None\n')
        self.write('tools/assets/compare.html', '<html></html>\n')

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def replace(self, name, old, new):
        path = self.root / name
        path.write_text(path.read_text().replace(old, new))

    def test_valid_without_importing_application(self):
        self.assertGreater(checker.check(self.root), 0)

    def test_missing_copy_of_imported_module(self):
        self.replace('tools/jelli-art/Containerfile', 'tools/assets/effect_data.py ', '')
        with self.assertRaisesRegex(checker.PackagingError, "local import 'effect_data' missing"):
            checker.check(self.root)

    def test_missing_allowlist_entry(self):
        self.replace('.dockerignore', '!tools/assets/effect_data.py\n', '')
        with self.assertRaisesRegex(checker.PackagingError, 'COPY source excluded'):
            checker.check(self.root)

    def test_missing_transitive_import(self):
        self.replace('tools/jelli-art/Containerfile', 'tools/assets/scene_data.py ', '')
        with self.assertRaisesRegex(checker.PackagingError, "local import 'scene_data' missing"):
            checker.check(self.root)

    def test_ignored_browser_file(self):
        with (self.root / '.dockerignore').open('a') as out:
            out.write('tools/jelli-art/shell.js\n')
        with self.assertRaisesRegex(checker.PackagingError, 'bundled file missing: shell.js'):
            checker.check(self.root)

    def test_missing_health_script(self):
        (self.root / 'tools/jelli-art/healthcheck.py').unlink()
        with self.assertRaisesRegex(checker.PackagingError, 'Container command file missing'):
            checker.check(self.root)

    def test_missing_health_instruction(self):
        self.replace('tools/jelli-art/Containerfile', 'HEALTHCHECK CMD ["python", "/app/tools/jelli-art/healthcheck.py"]', '')
        with self.assertRaisesRegex(checker.PackagingError, 'Expected exactly one HEALTHCHECK'):
            checker.check(self.root)

    def test_missing_server(self):
        (self.root / 'tools/jelli-art/jelli_art.py').unlink()
        with self.assertRaisesRegex(checker.PackagingError, 'Required studio file missing'):
            checker.check(self.root)

    def test_multistage_build_fails_closed(self):
        with (self.root / 'tools/jelli-art/Containerfile').open('a') as out:
            out.write('FROM base\nFROM another\n')
        with self.assertRaisesRegex(checker.PackagingError, 'Multi-stage builds'):
            checker.check(self.root)

    def test_nonexecutable_entrypoint(self):
        (self.root / 'tools/jelli-art/entrypoint.sh').chmod(0o644)
        with self.assertRaisesRegex(checker.PackagingError, 'Entrypoint is not executable'):
            checker.check(self.root)

    def test_excluded_subtree(self):
        self.write('tools/jelli-art/ui_tests/fixture.py', 'invalid Python!')
        with (self.root / '.dockerignore').open('a') as out:
            out.write('tools/jelli-art/ui_tests/\n')
        checker.check(self.root)

    def test_unsupported_rules_fail_closed(self):
        with (self.root / '.dockerignore').open('a') as out:
            out.write('**/cache\n')
        with self.assertRaisesRegex(checker.PackagingError, 'Unsupported .dockerignore rule'):
            checker.check(self.root)

    def test_unsupported_copy_fails_closed(self):
        self.replace('tools/jelli-art/Containerfile', 'COPY tools/jelli-art/', 'COPY --from=builder tools/jelli-art/')
        with self.assertRaisesRegex(checker.PackagingError, 'Unsupported COPY syntax'):
            checker.check(self.root)


if __name__ == '__main__':
    unittest.main()
