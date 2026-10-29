"""Native advisory material traversal against the pinned CPython 2.7 laws."""
import json
import math
import os
from pathlib import Path
import random
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SOURCE=os.environ.get('WOT_FORK_0101_SOURCE')

@unittest.skipUnless(SOURCE,'set WOT_FORK_0101_SOURCE to the fixed gameplay checkout')
class ArmorLaw(unittest.TestCase):
    def test_multilayer_nominal_scores(self):
        rng=random.Random(1513);cases=[]
        kinds=['ARMOR_PIERCING','ARMOR_PIERCING_CR','ARMOR_PIERCING_HE','HIGH_EXPLOSIVE','HOLLOW_CHARGE']
        for index in range(1400):
            layers=[]
            for layer in range(index%8):
                layers.append([rng.randrange(8)!=0,layer*.2+rng.random()*.1,rng.choice([0.,1.,math.cos(math.radians(70)),math.cos(math.radians(85)),rng.uniform(-1,1)]),
                    rng.randrange(4),rng.randrange(4),rng.choice([-1.,0.,rng.uniform(1,500)]),rng.choice([0.,0.,1.])]+[rng.choice([0,1]) for _ in range(5)])
            rng.shuffle(layers)
            cases.append([kinds[index%5],rng.uniform(20,240),rng.uniform(0,400),rng.uniform(0,400),rng.choice([0.,100.,500.,720.]),rng.uniform(0,1800),rng.uniform(.1,.8),rng.uniform(.8,1.8),rng.choice([0.,100.,500.,720.,rng.uniform(0,1000)]),layers])
        with tempfile.TemporaryDirectory(prefix='native-armor-') as folder:
            binary=Path(folder)/'host';oracle=Path(folder)/'oracle.py'
            subprocess.run(['c++','-std=c++17','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(ROOT/'bigworld'),str(ROOT/'bigworld/stock_native_bot_armor_law.cpp'),str(ROOT/'tests/stock_native_bot_armor_host.cpp'),'-o',str(binary)],check=True)
            oracle.write_text('''from __future__ import print_function
import imp,json,sys,types,os
for name in ('gui','gui.mods','gui.mods.offline_lan_0922'):
    m=types.ModuleType(name);m.__path__=[];sys.modules[name]=m
stub=types.ModuleType('gui.mods.offline_lan_0922.stun_mechanics');sys.modules[stub.__name__]=stub
sys.modules['gui.mods.offline_lan_0922'].stun_mechanics=stub
c=imp.load_source('armor_reference',os.path.join(sys.argv[1],'src/res/scripts/client/gui/mods/offline_lan_0922/combat_rules.py'))
class Material(object):pass
for row in json.load(sys.stdin):
    kind,caliber,p100,p500,maximum,damage,factor,absorb,distance,raw=row
    shot=dict(shell=dict(kind=kind,caliber=caliber,damage=damage,explosionDamageFactor=factor,explosionDamageAbsorptionFactor=absorb),piercingPower=(p100,p500),maxDistance=maximum)
    hits=[]
    for present,d,cosine,component,material,armor,vdf,angle,norm,ricochet,cal,once in raw:
        m=None
        if present:
            m=Material();m.kind=material;m.armor=armor;m.vehicleDamageFactor=vdf;m.useHitAngle=angle;m.checkCaliberForHitAngleNorm=norm;m.mayRicochet=ricochet;m.checkCaliberForRichet=cal;m.collideOnceOnly=once
        hits.append((d,cosine,m,component))
    contact=c._offh_resolve_armor_contact(shot,distance,hits,penetration_factor=1.)
    result=None;score=None
    if contact:
        result=[contact['result'],contact['effective_armor'],contact['piercing'],contact['accumulated_loss'],contact['layer']=='structural']
        if contact['layer']!='structural' and kind!='HIGH_EXPLOSIVE':score=0.
        else:score=c.damage(shot,contact['result'],c._offh_he_nominal_armor(hits),rolled_damage=c.shell_nominal_damage(shot))
    print(json.dumps([result,score]))
''')
            lines=[]
            for row in cases:
                flat=row[:-1]+[len(row[-1])]+[v for layer in row[-1] for v in layer]
                lines.append(' '.join(str(int(v)) if isinstance(v,bool) else str(v) for v in flat))
            actual=[json.loads(line) for line in subprocess.check_output([str(binary)],input='\n'.join(lines)+'\n',text=True).splitlines()]
            py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
            expected=[json.loads(line) for line in subprocess.check_output([py27,'-B',str(oracle),SOURCE],input=json.dumps(cases),text=True).splitlines()]
            def equal(a,b,where):
                if isinstance(b,list):
                    self.assertEqual(len(a),len(b),where)
                    for i,(x,y) in enumerate(zip(a,b)):equal(x,y,where+'/'+str(i))
                elif b is None:self.assertIsNone(a,where)
                else:self.assertTrue(math.isclose(a,b,rel_tol=1.e-12,abs_tol=1.e-9),(where,a,b))
            self.assertEqual(len(actual),len(expected))
            for index,(a,b) in enumerate(zip(actual,expected)):equal(a,b,str(index))

if __name__=='__main__':unittest.main()
