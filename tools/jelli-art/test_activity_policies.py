# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Activity policy round trips use the same generator as saved content."""
import copy
import json
from pathlib import Path
import unittest
import activities
from behaviors import BehaviorError

REPO = Path(__file__).resolve().parents[2]


class ActivityPoliciesTest(unittest.TestCase):
    def setUp(self):
        self.doc = json.loads((REPO / 'content/activities.json').read_text())
        self.manifest = json.loads((REPO / 'assets/slice/assets.json').read_text())

    def validate(self, doc):
        activities.validate(doc, REPO, REPO / 'content', self.manifest)

    def test_visible_policies_survive_serialization_and_generator(self):
        recipe = copy.deepcopy(self.doc['moments'][-1])
        recipe.update(id=len(self.doc['moments']), name='NEW HUG', daily_reward=False,
                      pose='happy', prize='teddy-bear')
        self.doc['moments'].append(recipe)
        reloaded = json.loads(json.dumps(self.doc))
        self.validate(reloaded)
        self.assertTrue(reloaded['moments'][-2]['daily_reward'])
        self.assertFalse(reloaded['moments'][-1]['daily_reward'])
        self.assertEqual(reloaded['moments'][-1]['prize'], 'teddy-bear')

    def test_invalid_policies_and_wrong_kind_assets_fail(self):
        for key, value in [('daily_reward', 'yes'), ('pose', 'missing'),
                           ('prize', 'missing'), ('prop', 10001), ('icon', 10001)]:
            with self.subTest(key=key):
                doc = copy.deepcopy(self.doc)
                doc['moments'][-1][key] = value
                with self.assertRaises(BehaviorError):
                    self.validate(doc)


if __name__ == '__main__':
    unittest.main()
