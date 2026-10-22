# /// script
# requires-python = ">=3.15,<3.16"
# dependencies = ["Pillow==13.0.0"]
# ///
"""Timeline schema, persistence and compiler regression coverage."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/assets'))
import effect_data
import jelli_art as studio

class EffectsTests(unittest.TestCase):
    def setUp(self):
        self.manifest=json.loads((ROOT/'assets/slice/assets.json').read_text())
        self.doc=json.loads((ROOT/'content/effects.json').read_text())

    def test_compile_and_defaults(self):
        effect_data.validate(effect_data.initial(self.manifest),self.manifest)
        forms=json.loads((ROOT/'content/pets.json').read_text())['forms']
        emitted=effect_data.emit(self.doc,self.manifest,forms)
        self.assertIn('jelli_effect_point',emitted)
        self.assertIn('.anchor=2',emitted)

    def test_reject_invalid_references_bounds_and_overlapping_cues(self):
        for mutate in [
            lambda d:d['effects'][0]['tracks'][0].update(anchor='missing'),
            lambda d:d['effects'][0]['tracks'][0].update(anchor=[]),
            lambda d:d['effects'][0]['tracks'][0].update(duration_ms=30001),
            lambda d:d['effects'][0]['tracks'][0].update(x=True),
            lambda d:d['effects'][0]['tracks'][0]['frames'][0].update(asset='backgrounds.home'),
            lambda d:d['effects'][0]['cues'][1].update(start_ms=0),
            lambda d:d['effects'][0]['cues'][0].update(start_ms='bad'),
            lambda d:d['hardpoints'][0]['positions'].pop('baby'),
            lambda d:d['effects'][0].update(tracks=d['effects'][0]['tracks']*5),
        ]:
            with self.subTest(mutate=mutate):
                doc=copy.deepcopy(self.doc);mutate(doc)
                with self.assertRaises(ValueError):effect_data.validate(doc,self.manifest)

    def test_trigger_bindings(self):
        forms=json.loads((ROOT/'content/pets.json').read_text())['forms']
        old=copy.deepcopy(self.doc);old.pop('triggers',None)
        self.assertIn('COUNT]={0,0,0}',effect_data.emit(old,self.manifest,forms))
        self.doc['triggers']={'evolution':'celebrate','present-caught':None}
        self.assertIn('COUNT]={1,0,0}',effect_data.emit(self.doc,self.manifest,forms))
        for bad in [{'evolution':'missing'}, {'other':'celebrate'}, {'evolution':True}, []]:
            self.doc['triggers']=bad
            with self.assertRaises(ValueError):effect_data.validate(self.doc,self.manifest)

    def test_atomic_save_stale_and_readonly(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(studio,'CONTENT',Path(temp)),patch.object(studio,'read_manifest',return_value=self.manifest),patch.object(studio,'record',return_value={}):
            r=studio.save_effects({'data':self.doc,'base':None});path=Path(temp)/'effects.json';before=path.read_bytes()
            with self.assertRaises(studio.StaleError):studio.save_effects({'data':self.doc,'base':None})
            with self.assertRaises(studio.StudioError):studio.save_effects({'data':self.doc})
            bad=copy.deepcopy(self.doc);bad['effects'][0]['duration_ms']=1
            with self.assertRaises(ValueError):studio.save_effects({'data':bad,'base':r['base']})
            with patch.object(studio,'CONTENT_WRITABLE',False):
                with self.assertRaises(studio.StudioError):studio.save_effects({'data':self.doc,'base':r['base']})
            self.assertEqual(before,path.read_bytes())

    @unittest.skipUnless(shutil.which("node"), "Node is optional outside studio CI")
    def test_browser_sampler_boundaries(self):
        script="""const M=require(process.argv[1]);const e=JSON.parse(require('fs').readFileSync(0,'utf8')).effects[0];
const a=M.sample(e,500),b=M.sample(e,3000);
if(a.cue.pose!=='happy'||a.sprites.length!==3||b.cue||b.sprites.length)process.exit(1);
if(M.frameAt([{asset:'a',duration_ms:100},{asset:'b',duration_ms:200}],300,true).asset!=='a')process.exit(2);"""
        subprocess.run(['node','-e',script,str(ROOT/'tools/jelli-art/effect_model.js')],input=json.dumps(self.doc),text=True,check=True)

if __name__=='__main__':unittest.main()
