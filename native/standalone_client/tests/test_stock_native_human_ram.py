"""Exact worker human-ram ingress/status laws in actual CPython 2.7."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''
import ast,copy,imp,math,os,random,sys,types
import human_ram_test as native
base=os.path.join(sys.argv[1],'src/res/scripts/client/gui/mods/offline_lan_0922')
for name in ('gui','gui.mods','gui.mods.offline_lan_0922'):
    module=types.ModuleType(name);module.__path__=[];sys.modules[name]=module
math_native=types.ModuleType('gui.mods.offline_lan_0922.native_math');math_native.call=lambda *a:None
sys.modules[math_native.__name__]=math_native;sys.modules['gui.mods.offline_lan_0922'].native_math=math_native
collision=imp.load_source('human_ram_collision',os.path.join(base,'tank_collision.py'))
tracks=imp.load_source('gui.mods.offline_lan_0922.ram_tracks',os.path.join(base,'ram_tracks.py'))
class Box(object):
    def __init__(self,**kw):self.__dict__.update(kw)
protocol=dict(integer_types=(int,long))
tree=ast.parse(open(os.path.join(base,'lan_client.py')).read())
exec compile(ast.Module([n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='_exact_int']),'exact-int','exec') in protocol
env=dict(math=math,tank_collision=collision,lan_protocol=Box(**protocol),_STRING_TYPES=(str,unicode),_number=lambda v,d=0.:float(v or d))
tree=ast.parse(open(os.path.join(base,'battle_runtime.py')).read())
names=('_validated_ram_contact_normal','_validated_human_ram_probe_body','_human_ram_armor_results','_ram_contact_armor_status')
owner=next(n for n in tree.body if isinstance(n,ast.ClassDef) and any(isinstance(m,ast.FunctionDef) and m.name in names for m in n.body))
owner.name='Oracle';owner.bases=[ast.Name(id='object',ctx=ast.Load())];owner.decorator_list=[];owner.body=[n for n in owner.body if isinstance(n,ast.FunctionDef) and n.name in names]
exec compile(ast.fix_missing_locations(ast.Module([owner])),'human-ram','exec') in env
Oracle=env['Oracle'];Oracle._ram_obb_overlap_point=staticmethod(collision.obb_overlap_point);Oracle._ram_contact_xz_samples=staticmethod(lambda *args:[])
Oracle._ram_pose_matrix=lambda s,*a:Box()
Oracle._projectile_vehicle_matrices=lambda s,*a,**kw:(Box(),Box())
Oracle._native_ram_vehicle_armor=lambda *args,**kw:None
def pair(self,*unused):
    if self.mode==2:return None,None,None
    first=dict(armor=40.);second=dict(armor=80.)
    if self.mode!=4:first['contact_tracks']=['leftTrackHealth']
    return (first,second,0.,0.,0.),None,None
Oracle._ram_plate_pair_from_probe=pair
def body(id,x):return dict(id=id,vehicle='ussr:test',x=x,y=0.,z=0.,yaw=0.,pitch=0.,roll=0.,shape=[1.,2.,-.5,1.5])
first=body(1,-.6);second=body(2,.6)
record=lambda id:dict(kind='player',network_id=id,engine_id=id,ready=True,tombstone=False,state={'vehicle':'ussr:test'})
entity=Box(isStarted=True,typeDescriptor=object())
def battle(requests):return Box(_worker_mode=True,_last_snapshot={'human_ram_probes':requests},_records={'player:1':record(1),'player:2':record(2)},_remote_factory=Box(_vehicles={1:entity,2:entity}))
def request(seq=1):return dict(seq=seq,contact_normal=[-1.,0.],first=copy.deepcopy(first),second=copy.deepcopy(second))
def compare(b,mode=0):
    oracle=Oracle();oracle.__dict__.update(b.__dict__);oracle.mode=mode
    oracle._server_entity=lambda id:None if mode==1 else b._remote_factory._vehicles.get(id)
    try:expected=oracle._human_ram_armor_results();bad=False
    except (RuntimeError,ValueError,TypeError,OverflowError):expected=[];bad=True
    actual,errors,queries=native.run(b,mode)
    assert actual==expected,(actual,expected)
    assert bool(errors)==bad,(errors,bad)
    return actual,errors,queries
assert native.adapter()==(40.,80.)
for mode in (0,1,2,4):compare(battle([request()]),mode)
for seq in (1,1.,'1',u'1',' 1 ','+001'):compare(battle([dict(request(),seq=seq)]))
for value in (True,None,1.5,'1.0',0,-1,2147483648,float('nan')):compare(battle([dict(request(),seq=value)]))
for edit in (lambda r:r.update(extra=1),lambda r:r['first'].update(extra=1),lambda r:r['first'].update(id=True),lambda r:r['first'].update(x=5001.),lambda r:r['first'].update(y=20.),lambda r:r['first'].update(shape=[.1,2.,-.5,1.5]),lambda r:r.update(contact_normal=[0.,0.]),lambda r:r.update(contact_normal=[1.,0.]),lambda r:r['second'].update(x=10.),lambda r:r['first'].update(vehicle='other'),lambda r:r['first'].update(id=2)):
    raw=request();edit(raw);compare(battle([raw]))
for field,value in (('ready',False),('tombstone',True)):
    b=battle([request()]);b._records['player:1'][field]=value;compare(b)
b=battle([request()]);b._records['player:1']['kind']='bot';compare(b)
b=battle([request()]);b._remote_factory._vehicles={};compare(b)
b=battle([request()]);b._worker_mode=False;compare(b)
rng=random.Random(1513)
for i in range(200):
    raw=request(i+1)
    for part in ('first','second'):
        raw[part].update(y=rng.uniform(-1.,1.),yaw=rng.uniform(-.6,.6),pitch=rng.uniform(-.4,.4),roll=rng.uniform(-.4,.4),shape=[rng.uniform(.5,2.),rng.uniform(.75,4.),-.5,rng.uniform(.6,2.)])
    raw['contact_normal']=[rng.uniform(-4.,-.01),rng.uniform(-.3,.3),99.]
    if i%5==0:raw['first']['x']=str(raw['first']['x']);raw['first']['shape'][0]=str(raw['first']['shape'][0])
    compare(battle([raw]),i%3)
# Invalid rows and a native geometry failure have local terminal diagnostics;
# the next valid request in the same batch remains observable.
bad=request(1);bad['first']['x']=float('nan')
b=battle([bad,request(2)]);rows,errors,queries=native.run(b,0)
assert [r['seq'] for r in rows]==[2] and len(errors)==1 and queries==1
rows,errors,queries=native.run(battle([request()]),3);assert rows==[] and errors and queries==1
# The exact boundaries invoke native entity ownership, never these helpers.
b=battle([request()]);b._server_entity=lambda *a:(_ for _ in ()).throw(AssertionError('Python callback'))
assert native.run(b,0)[0][0]['available'] is True
print('human ram ingress passed')
'''
class HumanRam(unittest.TestCase):
    def test_exact_ingress_status_and_local_failures(self):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:self.skipTest('set WOT_FORK_0101_SOURCE')
        py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        with tempfile.TemporaryDirectory(prefix='human-ram-') as directory:
            folder=Path(directory);script=folder/'fixture.py';script.write_text(FIXTURE)
            sources=[ROOT/'bigworld/stock_native_human_ram.cpp',ROOT/'bigworld/stock_native_bot_armor_geometry.cpp',ROOT/'bigworld/stock_native_bot_resource_lifecycle.cpp',ROOT/'tests/stock_native_human_ram_host.cpp']
            command=['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-DWOT_RESOURCE_LIFECYCLE_HOST_BUILD','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(ROOT/'bigworld'),'-I'+str(Path(source)/'native'),*[str(p) for p in sources],'-o',str(folder/'human_ram_test.so')]
            result=subprocess.run(command,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([py27,'-B',str(script),source],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr);self.assertIn('human ram ingress passed',result.stdout)
if __name__=='__main__':unittest.main()
