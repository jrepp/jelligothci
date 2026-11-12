# /// script
# requires-python = ">=3.15,<3.16"
# dependencies = ["Pillow==13.0.0"]
# ///
"""Bound the untrusted CI artifact before publishing it as static data."""
import importlib.util
import io
from pathlib import Path
import unittest
import zipfile
from unittest.mock import patch

from PIL import Image

spec = importlib.util.spec_from_file_location('publisher', Path(__file__).resolve().parents[1] / 'scripts/publish-engine-canary.py')
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)


def archive(name='canary.png', width=1398, height=1398):
    stream = io.BytesIO()
    image = Image.new('RGB', (width, height), (31, 47, 63))
    png_stream = io.BytesIO()
    image.save(png_stream, format='PNG')
    png = png_stream.getvalue()
    with zipfile.ZipFile(stream, 'w') as result:
        result.writestr(name, png)
    return stream.getvalue()


class PublisherTest(unittest.TestCase):
    def test_only_bounded_root_png_is_accepted(self):
        self.assertTrue(publisher.extract_png(archive(height=1584)).startswith(b'\x89PNG'))
        for invalid in (archive('../canary.png'), archive(width=4097), archive(height=1583), b'bad'):
            with self.assertRaises((ValueError, zipfile.BadZipFile)):
                publisher.extract_png(invalid)
        with self.assertRaises(ValueError):
            publisher.extract_png(bytes(publisher.MAX_ZIP + 1))

        many = io.BytesIO()
        with zipfile.ZipFile(many, 'w') as result:
            result.writestr('canary.png', publisher.extract_png(archive(height=1584)))
            for index in range(publisher.MAX_ENTRIES):
                result.writestr(f'extra-{index}', b'x')
        with self.assertRaisesRegex(ValueError, 'too many files'):
            publisher.extract_png(many.getvalue())

    def test_truncated_png_is_rejected(self):
        png = publisher.extract_png(archive(height=1584))[:-20]
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, 'w') as result:
            result.writestr('canary.png', png)
        with self.assertRaisesRegex(ValueError, 'invalid or truncated PNG'):
            publisher.extract_png(stream.getvalue())

    def test_pr_must_be_open_current_and_same_repository(self):
        good = {'state': 'open', 'head': {'sha': 'abc', 'repo': {'full_name': 'x/y'}}}
        with patch.object(publisher, 'api', return_value=good):
            self.assertTrue(publisher.current_pr('x/y', 1, 'abc'))
            self.assertFalse(publisher.current_pr('x/y', 1, 'old'))
            self.assertFalse(publisher.current_pr('x/z', 1, 'abc'))
        good['state'] = 'closed'
        with patch.object(publisher, 'api', return_value=good):
            self.assertFalse(publisher.current_pr('x/y', 1, 'abc'))

    def test_old_run_cannot_replace_any_newer_bot_comment(self):
        def comment(run):
            return {'user': {'login': 'github-actions[bot]'}, 'body':
                    f'{publisher.MARKER}<!-- jelli-engine-canary-run:{run} -->', 'id': run}
        existing, newer = publisher.find_existing_comment([comment(20), comment(30)], 25)
        self.assertIsNone(existing)
        self.assertTrue(newer)
        existing, newer = publisher.find_existing_comment([comment(20), comment(30)], 31)
        self.assertEqual(existing['id'], 30)
        self.assertFalse(newer)


if __name__ == '__main__':
    unittest.main()
