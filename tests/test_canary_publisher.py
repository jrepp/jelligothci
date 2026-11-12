"""Bound the untrusted CI artifact before publishing it as static data."""
import importlib.util
import io
from pathlib import Path
import struct
import unittest
import zipfile
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('publisher', Path(__file__).resolve().parents[1] / 'scripts/publish-engine-canary.py')
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)


def archive(name='canary.png', width=1398, height=1398):
    stream = io.BytesIO()
    png = b'\x89PNG\r\n\x1a\n' + struct.pack('>I', 13) + b'IHDR' + struct.pack('>II', width, height) + bytes(17)
    with zipfile.ZipFile(stream, 'w') as result:
        result.writestr(name, png)
    return stream.getvalue()


class PublisherTest(unittest.TestCase):
    def test_only_bounded_root_png_is_accepted(self):
        self.assertTrue(publisher.extract_png(archive()).startswith(b'\x89PNG'))
        for invalid in (archive('../canary.png'), archive(width=4097), archive(height=0), b'bad'):
            with self.assertRaises((ValueError, zipfile.BadZipFile)):
                publisher.extract_png(invalid)
        with self.assertRaises(ValueError):
            publisher.extract_png(bytes(publisher.MAX_ZIP + 1))

    def test_pr_must_be_open_current_and_same_repository(self):
        good = {'state': 'open', 'head': {'sha': 'abc', 'repo': {'full_name': 'x/y'}}}
        with patch.object(publisher, 'api', return_value=good):
            self.assertTrue(publisher.current_pr('x/y', 1, 'abc'))
            self.assertFalse(publisher.current_pr('x/y', 1, 'old'))
            self.assertFalse(publisher.current_pr('x/z', 1, 'abc'))
        good['state'] = 'closed'
        with patch.object(publisher, 'api', return_value=good):
            self.assertFalse(publisher.current_pr('x/y', 1, 'abc'))


if __name__ == '__main__':
    unittest.main()
