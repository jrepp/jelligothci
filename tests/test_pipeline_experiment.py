"""Reject stale and inconsistent pipeline measurements."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/debug'))
import pipeline_experiment as pipeline


def result():
    return dict(ok=True, run=2, boot=7, active=False, expired=False, stage='copy', count=150, window=128,
                p50_us=20, p95_us=30, max_us=40, total_us=3000, bytes=4096)


class PipelineTests(unittest.TestCase):
    def test_network_resource_failure_rejected(self):
        good = dict(ok=True, available=True, pending=False, result=0)
        pipeline.validate_network(good)
        for key, value in (('result', 257), ('result', False), ('available', False),
                           ('pending', True), ('ok', False)):
            with self.subTest(key=key), self.assertRaises(ValueError):
                pipeline.validate_network(dict(good, **{key: value}))

    def test_missing_stage_instrumentation(self):
        replies = {stage: dict(count=10, total_us=0) for stage in pipeline.STAGES}
        pipeline.validate_stages(replies)
        for missing in ('engine', 'render', 'transfer', 'lvgl', 'lvgl_wait', 'lvgl_flush', 'lvgl_work', 'lvgl_image', 'lvgl_tasks', 'lvgl_events'):
            broken = dict(replies, **{missing: dict(count=0, total_us=0)})
            with self.subTest(missing=missing), self.assertRaises(ValueError):
                pipeline.validate_stages(broken)
        # A static scene need not submit display updates during the capture.
        pipeline.validate_stages(dict(replies, stage=dict(count=0, total_us=0),
                                      transfer=dict(count=0, total_us=0), lvgl=dict(count=0, total_us=0), lvgl_work=dict(count=0, total_us=0)))

    def test_reject_unaccounted_lvgl_time(self):
        replies = {stage: dict(count=10, total_us=0) for stage in pipeline.STAGES}
        replies['lvgl']['total_us'] = 20
        replies['lvgl_work']['total_us'] = 10
        replies['lvgl_flush']['total_us'] = 7
        replies['lvgl_wait']['total_us'] = 3
        pipeline.validate_stages(replies)
        replies['lvgl_wait']['total_us'] = 4
        with self.assertRaises(ValueError):
            pipeline.validate_stages(replies)

    def test_reject_overlapping_dispatch_totals(self):
        replies = {stage: dict(count=10, total_us=0) for stage in pipeline.STAGES}
        replies['lvgl']['total_us'] = replies['lvgl_work']['total_us'] = 20
        replies['lvgl_tasks']['total_us'] = 15
        replies['lvgl_dispatch']['total_us'] = 4
        replies['lvgl_draw_wait']['total_us'] = 1
        pipeline.validate_stages(replies)
        replies['lvgl_events']['total_us'] = 21
        with self.assertRaises(ValueError):
            pipeline.validate_stages(replies)
        replies['lvgl_events']['total_us'] = 0
        replies['lvgl_dispatch']['total_us'] = 5
        with self.assertRaises(ValueError):
            pipeline.validate_stages(replies)

    def test_invalid_distribution_and_stale_runs(self):
        pipeline.validate(result(), 2, 'copy', 7)
        for key, value in [('expired', True), ('boot', 8), ('run', 1), ('active', True), ('stage', 'swap'), ('ok', False),
                           ('count', -1), ('window', 129), ('window', 100),
                           ('p50_us', 31), ('p95_us', 41), ('total_us', 39), ('bytes', True)]:
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                pipeline.validate(dict(result(), **{key: value}), 2, 'copy', 7)

    def test_cleanup_attempts_all_restorations_after_timeout(self):
        calls = []
        def request(command):
            calls.append(command)
            if command.endswith('stop'):
                raise TimeoutError('Lost stop reply')
            return dict(ok=True, overlays='baseline', memcpy='builtin')
        with self.assertRaisesRegex(ValueError, 'Lost stop reply'):
            pipeline.restore_modes(request)
        self.assertEqual(calls, ['display profile stop', 'display profile overlays baseline',
                                 'display profile memcpy builtin'])

    def test_cleanup_rejects_unsuccessful_restore(self):
        calls = []
        def request(command):
            calls.append(command)
            return dict(ok=True, active=False, overlays='skip', memcpy='builtin')
        with self.assertRaises(ValueError):
            pipeline.restore_modes(request)
        self.assertEqual(len(calls), 3)

    def test_empty_and_short_windows(self):
        pipeline.validate(dict(result(), count=0, window=0, p50_us=0, p95_us=0,
                               max_us=0, total_us=0, bytes=0), 2, 'copy', 7)
        pipeline.validate(dict(result(), count=10, window=10), 2, 'copy', 7)


if __name__ == '__main__':
    unittest.main()
