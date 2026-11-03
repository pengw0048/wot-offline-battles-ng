"""Freeze exact stock factor producers for all physical crew/fire states."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''
import sys,types
class O(object):
 def __init__(self,**values):self.__dict__.update(values)
def module(name,**values):
 result=types.ModuleType(name);result.__dict__.update(values);sys.modules[name]=result;return result
module('items',__path__=[])
module('constants',VEHICLE_TTC_ASPECTS=O(DEFAULT=0,WHEN_STILL=1))
module('items.tankmen',MAX_SKILL_LEVEL=100)
module('items.qualifiers',QUALIFIER_TYPE=O(MAIN_SKILL=0))
module('items.vehicles')
module('VehicleQualifiersApplier',VehicleQualifiersApplier=lambda mapping,descriptor:{0:{}})
def rejected(*args):raise AssertionError('a fitted Bot must never generate a replacement crew')
module('items.utils',generateDefaultCrew=rejected,makeDefaultVehicleAttributeFactors=lambda:{})
class Crew(object):
 def __init__(self,descriptor,compact,bonuses,activityFlags,isFire):
  assert list(compact)==['commander-native','loader-native']
  self.active=activityFlags;self.fire=isFire;self._skillProcessors={};self.camouflageFactor=.1*sum(activityFlags)
 def onCollectFactors(self,factors):
  efficiency=.5+.2*sum(self.active)-.05*int(self.fire)
  factors.update({'gun/reloadTime':1./efficiency,'gun/aimingTime':1./efficiency,'turret/rotationSpeed':efficiency,
   'gun/rotationSpeed':efficiency,'chassis/terrainResistance':[1./efficiency]*3,'circularVisionRadius':efficiency,
   'radio/distance':efficiency,'repairSpeed':efficiency,'invisibility':(.2 if factors['aspect'] else .1,1.),'additiveShotDispersionFactor':.95})
  config=O(vehicleHealthFraction=.1,gunReloadTimeFactor=.8,fireStartingChanceFactor=.6,shotDispersionFactorPerLevel=.001,ammoBayHealthFactor=1.3)
  for name in ('loader_desperado','driver_tidyPerson','gunner_gunsmith','loader_pedant'):
   self._skillProcessors[name](self,1,100.,0.,self.active[1],self.fire,config)
 def onCollectShotDispersionFactors(self,output):output[0]=1./(.5+.2*sum(self.active)-.05*int(self.fire))
 def boostSkillBy(self,directive):pass
module('items.VehicleDescrCrew',VehicleDescrCrew=Crew)
class Directive(object):
 name='ventilationBattleBooster';crewLevelIncrease=0.;tags=()
 def updateVehicleAttrFactors(self,descriptor,factors,aspect):factors['aspect']=aspect
class Descriptor(object):
 type=O(crewRoles=[('commander',),('loader',)])
 optionalDevices=();miscAttrs={}
 def computeBaseInvisibility(self,factor,camouflage):
  assert camouflage==42
  return (factor+.05,(factor+.05)*2.)
descriptor=Descriptor()
context=dict(crew=('commander-native','loader-native'),directives=(Directive(),),crew_skills=((),('radioman_retransmitter',)),camouflages={'1':42})
module('gui',__path__=[]);module('gui.mods',__path__=[]);module('gui.mods.offline_lan_0922',__path__=[])
module('gui.mods.offline_lan_0922.bot_fittings',context=lambda value:context)
import fitting_test
assert fitting_test.inspect(descriptor)
print('native Bot fitting factors passed')
'''
class FittingFactorsTest(unittest.TestCase):
    def test_native_crew_fire_and_skill_projection(self):
        source=os.environ.get('WOT_FORK_SOURCE')
        if not source:self.skipTest('fixed gameplay source required')
        py27=subprocess.check_output(['pyenv','which','python2.7'],text=True,env=dict(os.environ,PYENV_VERSION='2.7.18')).strip()
        prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        with tempfile.TemporaryDirectory(prefix='bot-fitting-') as directory:
            folder=Path(directory);(folder/'fixture.py').write_text(FIXTURE)
            command=['c++','-std=c++17','-Wno-register','-DWOT_NATIVE_BATTLE_HOST_BUILD','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(ROOT/'bigworld'),'-I'+str(Path(source)/'native')]
            command += [str(ROOT/name) for name in ('tests/stock_native_bot_fitting_host.cpp','tests/stock_native_client_state_host.cpp','bigworld/stock_native_battle_params_loadout.cpp','bigworld/stock_native_bot_descriptor.cpp','bigworld/stock_native_bot_combat.cpp')]
            command += ['-o',str(folder/'fitting_test.so')]
            built=subprocess.run(command,capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stderr)
            run=subprocess.run([py27,'-B',str(folder/'fixture.py')],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stderr)
if __name__=='__main__':unittest.main()
