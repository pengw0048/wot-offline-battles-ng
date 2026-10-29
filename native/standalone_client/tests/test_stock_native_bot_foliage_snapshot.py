"""CPython 2.7 cold foliage input and native fallen-tree lifecycle parity."""
import os,subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''
import ast,copy,imp,math,os,random,sys,types
import foliage_snapshot_test as native
base=sys.argv[1]+'/src/res/scripts/client/gui/mods/offline_lan_0922'
for name in ('gui','gui.mods','gui.mods.offline_lan_0922'):
    m=types.ModuleType(name);m.__path__=[base] if name.endswith('0922') else [];sys.modules[name]=m
foliage=imp.load_source('foliage_oracle',base+'/foliage.py')
class Box(object):
    def __init__(self,**kw):self.__dict__.update(kw)
def initial():
    f=foliage.FoliageMap(dict(cell_size=32.,instances=[(0.,0.,0.,10.,1.,0.,0.,1.,.15,3.)],cells={'0,0':[0]},fallen_trees=[(1,7,-1.,0.,-2.,1.,10.,2.,0)]))
    return Box(_foliage=f,_disabled_optional_features=set(),_fallen_tree_foliage_seen_bodies=set(),_fallen_tree_foliage_stable={},_next_fallen_tree_foliage_refresh=0.)
def snapshot(f):return (f.instances,f.cells,sorted(f.inactive_instances))
battle=initial();assert native.inspect(battle)==snapshot(battle._foliage)
f=battle._foliage;f.activate_fallen_tree(1,7);f.update_fallen_tree_pose(1,7,(4.,5.,6.),((1.,0.,0.),(0.,2.,0.),(0.,0.,3.)))
assert native.inspect(battle)==snapshot(f)
assert native.update(battle)==(1,len(f.native_dirty_cells),1)
saved=f.instances[0];f.instances[0]=(float('nan'),)*10
try:native.inspect(battle)
except ValueError:pass
else:raise AssertionError('nonfinite foliage accepted')
f.instances[0]=saved
battle._disabled_optional_features.add('foliage camouflage');assert native.inspect(battle)==([],{},[])
text=open(base+'/battle_runtime.py').read();tree=ast.parse(text)
method=next(m for c in tree.body if isinstance(c,ast.ClassDef) for m in c.body if isinstance(m,ast.FunctionDef) and m.name=='_refresh_fallen_tree_foliage');method.decorator_list=[]
env=dict(FALLEN_TREE_FOLIAGE_REFRESH_SECONDS=.1,FALLEN_TREE_FOLIAGE_STABLE_READS=3);exec(compile(ast.Module(body=[method]),'fixed-tree-refresh','exec'),env)
def reference(cases):
    b=initial();f=b._foliage;f.activate_fallen_tree(1,7);b._runtime=Box(area_destructibles=Box(g_destructiblesManager=Box(forceNoAnimation=False,_DestructiblesManager__destructiblesWaitDestroy={},_DestructiblesManager__loadedChunkIDs=set([1]))));out=[]
    for now,center,axes,body,waiting,loaded in cases:
        manager=b._runtime.area_destructibles.g_destructiblesManager;manager._DestructiblesManager__destructiblesWaitDestroy={1:[]} if waiting else {};manager._DestructiblesManager__loadedChunkIDs=set([1]) if loaded else set()
        b._fallen_tree_body_identities=lambda:set([(1,7)]) if body else set()
        b._native_fallen_tree_pose=lambda *unused:(center,axes)
        changed=env['_refresh_fallen_tree_foliage'](b,now)
        stable=b._fallen_tree_foliage_stable.get((1,7));out.append((int(changed),copy.deepcopy(snapshot(f)),len(f.refreshing_fallen_trees),None if stable is None else stable[0],b._next_fallen_tree_foliage_refresh))
    return out
rng=random.Random(1513)
for mode in range(5):
    cases=[]
    for i in range(35):
        now=i*.051;center=(.015625 if mode==4 else i*.25,5.,6.)
        axes=((1.,0.,0.),(0.,2.,0.),(0.,0.,3.));body=(i<20) if mode==0 else False;waiting=mode==1 and i<3;loaded=not(mode==2 and i<4)
        if mode==3:center=(rng.uniform(-70.,70.),rng.uniform(0.,15.),rng.uniform(-70.,70.))
        cases.append((now,center,axes,body,waiting,loaded))
    actual=native.trace(initial(),cases);expected=reference(cases)
    assert actual==expected,(mode,[(i,a,b) for i,(a,b) in enumerate(zip(actual,expected)) if a!=b][:1])
print('foliage snapshot and native lifecycle passed')
'''
class FoliageSnapshotTests(unittest.TestCase):
    def test_cold_rows_dirty_updates_and_native_tree_lifecycle(self):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:self.skipTest('fixed gameplay source is required')
        py27=subprocess.check_output(['pyenv','which','python2.7'],text=True,env=dict(os.environ,PYENV_VERSION='2.7.18')).strip();prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        with tempfile.TemporaryDirectory(prefix='foliage-snapshot-') as directory:
            folder=Path(directory);script=folder/'fixture.py';script.write_text(FIXTURE)
            command=['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(Path(source)/'native'),'-I'+str(ROOT/'bigworld'),str(ROOT/'bigworld/stock_native_bot_foliage_snapshot.cpp'),str(ROOT/'tests/stock_native_bot_foliage_snapshot_host.cpp'),'-o',str(folder/'foliage_snapshot_test.so')]
            build=subprocess.run(command,text=True,capture_output=True);self.assertEqual(build.returncode,0,build.stderr)
            run=subprocess.run([py27,'-B',str(script),source],text=True,capture_output=True);self.assertEqual(run.returncode,0,run.stderr);self.assertIn('foliage snapshot and native lifecycle passed',run.stdout)
if __name__=='__main__':unittest.main()
