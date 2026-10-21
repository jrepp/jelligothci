# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""MOD editor persistence, bounded imports, and four-channel preview coverage."""
import base64
import copy
from concurrent.futures import ThreadPoolExecutor
import io
from pathlib import Path
import tempfile
import unittest
import wave
from unittest.mock import patch
import jelli_art as studio
import music_data

ROOT = Path(__file__).resolve().parents[2]


def encoded(data):
    return base64.b64encode(data).decode('ascii')


def wav_data(pcm, channels=1, width=1):
    output = io.BytesIO()
    with wave.open(output, 'wb') as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(width)
        wav.setframerate(22050)
        wav.writeframes(pcm)
    return output.getvalue()


class MusicTests(unittest.TestCase):
    def setUp(self):
        self.raw = (ROOT/'content/music/morning-tea.mod').read_bytes()
        self.doc = music_data.music_mod.decode(self.raw)

    def test_atomic_save_stale_readonly_concurrent_writers(self):
        with tempfile.TemporaryDirectory() as temp, \
                patch.object(studio, 'CONTENT', Path(temp)), \
                patch.object(studio, 'CONTENT_WRITABLE', True), \
                patch.object(studio, 'record', return_value={}):
            path = Path(temp)/'music/morning-tea.mod'
            path.parent.mkdir()
            path.write_bytes(self.raw)
            initial = studio.music_document()
            before = path.read_bytes()
            for body in ({}, {'song':'morning-tea'}, {'song':True, 'data':self.doc}):
                with self.assertRaises(studio.request_body.RequestError):
                    studio.save_music(body)
            with self.assertRaises(studio.StudioError):
                studio.save_music({'song':'morning-tea','data':self.doc})
            with self.assertRaises(studio.StaleError):
                studio.save_music({'song':'morning-tea','data':self.doc,'base':None})
            with patch.object(studio, 'CONTENT_WRITABLE', False):
                self.assertFalse(studio.music_document()['editable'])
                with self.assertRaises(studio.StudioError):
                    studio.save_music({'song':'morning-tea','data':self.doc,'base':initial['base']})
            self.assertEqual(before, path.read_bytes())
            altered = copy.deepcopy(self.doc)
            altered['title'] = 'Concurrent edit'
            body = {'song':'morning-tea','data':altered,'base':initial['base']}

            def save(_):
                try:
                    return studio.save_music(body)['ok']
                except studio.StaleError:
                    return False

            with ThreadPoolExecutor(max_workers=2) as pool:
                self.assertEqual(sorted(pool.map(save, range(2))), [False, True])
            self.assertEqual(music_data.music_mod.decode(path.read_bytes()), altered)
            current = path.read_bytes()
            bad = copy.deepcopy(altered)
            bad['patterns'][0][0][0].update(effect=1, param=1)
            with self.assertRaises(ValueError):
                studio.save_music({'song':'morning-tea','data':bad,'base':studio.music_document()['base']})
            self.assertEqual(path.read_bytes(), current)

    def test_missing_score_starter_create_stale_null_and_record_identity(self):
        with tempfile.TemporaryDirectory() as temp, \
                patch.object(studio, 'CONTENT', Path(temp)), \
                patch.object(studio, 'CONTENT_WRITABLE', True), \
                patch.object(studio, 'record', return_value={}):
            loaded = studio.music_document('garden-waltz')
            self.assertIsNone(loaded['base'])
            self.assertEqual(loaded['data'], music_data.initial())
            path = Path(temp)/'music/garden-waltz.mod'
            self.assertFalse(path.exists())
            body = {'song':'garden-waltz','data':loaded['data'],'base':None}
            self.assertTrue(studio.save_music(body)['ok'])
            before = path.read_bytes()
            with self.assertRaises(studio.StaleError):
                studio.save_music(body)
            self.assertEqual(before, path.read_bytes())
            self.assertIsNone(studio.music_document('morning-tea')['base'])
            for bad in ('../music', '', 'unknown', None, [], True):
                with self.assertRaises(ValueError):
                    studio.music_document(bad)

    def test_mod_import_export_and_unsupported_effects(self):
        self.assertEqual(music_data.import_module(encoded(self.raw))['data'], self.doc)
        self.assertEqual(base64.b64decode(music_data.export_module(self.doc)['file']), self.raw)
        raw = bytearray(self.raw)
        raw[1086] = (raw[1086] & 240) | 1
        for data in (bytes(raw), self.raw[:1084], b'not a module'):
            with self.assertRaises(ValueError):
                music_data.import_module(encoded(data))
        doc = copy.deepcopy(self.doc)
        doc['patterns'][0][0][0].update(effect=0,param=1)
        with self.assertRaises(ValueError):
            music_data.export_module(doc)
        with patch.object(music_data.build_music, 'render_stems') as renderer:
            with self.assertRaises(ValueError):
                music_data.preview(doc)
            renderer.assert_not_called()

    def test_base64_and_sample_import_bounds(self):
        for bad in (None, True, [], '!', '====', 'abc', encoded(bytes(131073))):
            with self.subTest(value=type(bad)), self.assertRaises(ValueError):
                music_data.import_module(bad)
        pcm = music_data.import_sample(encoded(bytes([128,255,0,127])), 'pcm')
        self.assertEqual(pcm, {'pcm':[-128,-1,0,127], 'rate':None})
        self.assertEqual(music_data.import_sample(encoded(b'\x7f'), 'pcm')['pcm'], [127,0])
        for data, kind in ((b'', 'pcm'), (bytes(8193), 'pcm'), (b'x', 'unknown'),
                           (b'garbage', 'wav'), (wav_data(bytes(20))[:-10], 'wav'),
                           (wav_data(bytes(8193)), 'wav'),
                           (wav_data(b'\0\0', channels=2), 'wav'),
                           (wav_data(b'\0\0', width=2), 'wav')):
            with self.subTest(kind=kind, length=len(data)), self.assertRaises(ValueError):
                music_data.import_sample(encoded(data), kind)
        self.assertEqual(len(music_data.import_sample(encoded(bytes(8192)), 'pcm')['pcm']),8192)

    def test_unsigned_wav_becomes_signed_pcm_with_even_padding(self):
        imported = music_data.import_sample(encoded(wav_data(bytes([0,128,255]))), 'wav')
        self.assertEqual(imported, {'pcm':[-128,0,127,0], 'rate':22050})

    def test_preview_four_nonzero_stems_is_unsaved(self):
        before = (ROOT/'content/music/morning-tea.mod').read_bytes()
        result = music_data.preview(self.doc)
        self.assertEqual(result['seconds'], 30)
        self.assertEqual(len(result['stems']), 4)
        for stem in result['stems']:
            with wave.open(io.BytesIO(base64.b64decode(stem)), 'rb') as wav:
                self.assertEqual((wav.getnchannels(),wav.getsampwidth(),wav.getframerate(),wav.getnframes()),
                                 (1,2,22050,661500))
                self.assertTrue(any(wav.readframes(661500)))
        self.assertEqual(before, (ROOT/'content/music/morning-tea.mod').read_bytes())


if __name__ == '__main__':
    unittest.main()
