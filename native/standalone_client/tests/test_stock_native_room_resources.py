"""Current-map cold resources and exact authored route admission, without Python headers."""
import copy
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1];FORK=os.environ.get('WOT_FORK_SOURCE')
DIRECTIONS=[[-1,-1],[0,-1],[1,-1],[-1,0],[1,0],[-1,1],[0,1],[1,1]]
CLASSES=['lightTank','mediumTank','heavyTank','AT-SPG','SPG']
ORACLE=r'''
import copy,json,sys
sys.path.insert(0,sys.argv[1])
from gui.mods.offline_lan_0922 import bot_tactics as c,bot_tactics_runtime as r
output=[]
for request in json.load(sys.stdin):
 graph=request['graph'];grid=r.graph_view(graph['map'],graph);value={'errors':[r.validate_route(grid,route) for route in request.get('routes',[])]}
 if 'tactics' in request:
  settings=c.map_settings(request['tactics'],graph['map']);configs={'1':{},'2':{}};proof={'1':{},'2':{}};valid={}
  for route in settings.get('routes',[]):configs[str(route['team'])]['user_'+route['id']]=route
  for route in settings.get('default_routes',[]):
   team=str(route['team']);configs[team][c.default_route_id(route)]=c.route_config(request['tactics'],graph['map'],c.default_route_id(route),route['team'])
   if not route.get('disabled') and r.validate_route(grid,route) is None:valid.setdefault((team,route['id']),[]).append(route)
  for (team,family),candidates in valid.items():
   rows=[]
   for tag in c.CLASSES:
    eligible=[row for row in candidates if row.get('class_tag','all') in (tag,'all')]
    if not eligible:continue
    row=min(eligible,key=lambda row:row.get('class_tag','all')!=tag)
    route=dict(id=c.default_route_id(row),capacity=15,class_weights={tag:1.},waypoints=[dict(x=p[0],y=0.,z=p[1]) for p in row['points']])
    rows.append(dict(class_tag=tag,route=route))
   proof[team][family]=rows
  value['route_configs']=configs;value['class_routes']=proof
 output.append(value)
print(json.dumps(output))
'''
@unittest.skipUnless(FORK,'Pinned source is required for current-map resource contracts')
class RoomResources(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.folder=tempfile.TemporaryDirectory(prefix='room-resources-');cls.addClassCleanup(cls.folder.cleanup);cls.binary=Path(cls.folder.name)/'resources'
  prebuilt=os.environ.get('WOT_ROOM_TEST_BIN')
  if prebuilt:
      cls.binary=Path(prebuilt)/'resources'
  else:
      subprocess.run(['c++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-I'+str(Path(FORK)/'native'),str(ROOT/'tests/room_resources_probe.cpp'),str(ROOT/'bigworld/stock_native_room_resources.cpp'),str(ROOT/'room/room_json.cpp'),'-o',str(cls.binary)],check=True,capture_output=True,text=True)
 def graph(self,width=5,height=5):
  links=[]
  for z in range(height):
   for x in range(width):links.append(sum(1<<bit for bit,(dx,dz) in enumerate(DIRECTIONS) if 0<=x+dx<width and 0<=z+dz<height))
  return dict(format='offline-lan-0922-navgraph',version=2,game_version='0.9.22.0.1-cn-1513',map='04_himmelsdorf',origin=[-8.,-8.],cell_size=4.,width=width,height=height,heights_mm=[0]*(width*height),links=links,hazards=[0]*(width*height),directions=DIRECTIONS,bounds=[-300.,-300.,400.,400.],objective_bases=[[-8.,-8.],[8.,8.]],objective_base_radii=[5.,5.])
 def compare(self,requests):
  expected=json.loads(subprocess.run([sys.executable,'-I','-B','-c',ORACLE,str(Path(FORK)/'src/res/scripts/client')],input=json.dumps(requests),capture_output=True,text=True,check=True).stdout.splitlines()[-1])
  native=subprocess.run([str(self.binary)],input='\n'.join(json.dumps(r) for r in requests)+'\n',capture_output=True,text=True,check=True)
  actual=[json.loads(line) for line in native.stdout.splitlines()]
  self.assertEqual(len(actual),len(expected))
  for index,(a,b) in enumerate(zip(expected,actual)):
   with self.subTest(index=index):
    self.assertNotIn('error',b);self.assertEqual(a['errors'],b['errors']);self.assertTrue(b['cache_move']);self.assertTrue(b['cache_retained']);self.assertTrue(b['cache_empty'])
    if 'tactics' in requests[index]:
     strategy=b['resources']['strategy'];self.assertEqual(a['route_configs'],strategy['route_configs']);self.assertEqual(a['class_routes'],strategy['class_routes']);self.assertEqual(requests[index]['tactics'],strategy['tactics']);self.assertEqual(requests[index]['actual_routes'],strategy['routes']);self.assertEqual(requests[index]['graph']['heights_mm'],b['graph']['heights_mm']);self.assertEqual(len(requests[index]['graph']['heights_mm'])*6,b['graph']['bytes'])
  return actual
 def route(self,points,**options):
  row=dict(id='lane',team=1,class_tag='all',points=points,disabled=False);row.update(options);return row
 def test_coarse_directed_membership_capture_circle_and_wait_admission(self):
  graph=self.graph();graph['links'][0]=0;graph['heights_mm'][24]=None;graph['hazards'][12]=1
  routes=[self.route([[-8.,-8.,0],[-4.,-8.,0]]),self.route([[-4.,-8.,0],[-8.,-8.,0]]),self.route([[0.,0.,0]]),self.route([[8.,8.,0]]),self.route([[8.,8.,0]],team=2),self.route([[0.,-4.,0,0,[[0.,0.]]]]),self.route([[0.,-4.,0,0,[[100.,100.]]]]),self.route([[8.,8.,0]],purpose='spg_attack',team=2)]
  self.compare([dict(graph=graph,routes=routes)])
 def test_additive_mesh_cells_directed_seams_wait_exits_and_missing_ground(self):
  graph=self.graph();graph['heights_mm'][12]=None
  patch=dict(origin=[-4.,0.],cell_size=2.,width=5,height=1,heights_mm=[0,0,0,0,0],links=[16,16,16,16,0],hazards=[0]*5)
  graph['local_meshes']=[patch]
  routes=[self.route([[-4.,0.,0],[4.,0.,0]]),self.route([[4.,0.,0],[-4.,0.,0]]),self.route([[-4.,0.,0,0,[[0.,0.]]],[4.,0.,0]]),self.route([[4.,0.,0,0,[[0.,0.]]],[-4.,0.,0]]),self.route([[0.,0.,0]]),self.route([[0.,2.,0]])]
  self.compare([dict(graph=graph,routes=routes)])
 def test_random_coarse_and_additive_legal_graphs_match_actual_validator(self):
  rng=random.Random(151303);requests=[]
  for index in range(90):
   graph=self.graph();size=25
   graph['heights_mm']=[None if rng.random()<.12 else rng.randint(-500,500) for _ in range(size)]
   graph['hazards']=[rng.choice([0,0,0,1,4]) for _ in range(size)]
   for i in range(size):graph['links'][i]&=rng.randint(0,255)
   if index%2:
    patch=dict(origin=[-2.,-2.],cell_size=2.,width=3,height=3,heights_mm=[0]*9,links=[],hazards=[rng.choice([0,0,1]) for _ in range(9)])
    for z in range(3):
     for x in range(3):patch['links'].append(sum(1<<bit for bit,(dx,dz) in enumerate(DIRECTIONS) if 0<=x+dx<3 and 0<=z+dz<3 and rng.random()<.7))
    graph['local_meshes']=[patch]
   routes=[]
   for unused in range(22):
    points=[]
    for unused in range(rng.randint(1,5)):
     p=[rng.choice([-8.,-6.,-4.,-2.,0.,2.,4.,6.,8.]),rng.choice([-8.,-6.,-4.,-2.,0.,2.,4.,6.,8.]),0]
     if rng.random()<.4:p += [0,[[rng.choice([-4.,0.,4.]),rng.choice([-4.,0.,4.])]]]
     points.append(p)
    routes.append(self.route(points,team=rng.choice([1,2]),purpose=rng.choice(['advance','spg_attack'])))
   requests.append(dict(graph=graph,routes=routes))
  self.compare(requests)
 def test_full_cold_metadata_exact_class_variants_and_empty_profile_scope(self):
  graph=self.graph();base=dict(schema=1,client='wot-0.9.22.0.1-cn-1513',name='Empty',behavior=[],maps={})
  def request(tactics):
   archive=dict(maps={'04_himmelsdorf':dict(tactics=tactics,tactics_digest='reviewed-existing-token',tactical_map=dict(bounds=graph['bounds'],routes={}))},spg_catalog=dict(schema=1))
   return dict(graph=graph,routes=[],tactics=tactics,archive=archive,actual_routes={'1':[dict(id='baked',waypoints=[[-8.,-8.,False],[8.,8.,False]])],'2':[]})
  authored=copy.deepcopy(base);authored['maps']['04_himmelsdorf']=dict(routes=[dict(id='manual',team=1,classes=['SPG'],slots=[],policy='preferred',points=[[-4.,-4.,0],[0.,-4.,0]])],default_routes=[self.route([[-8.,-8.,0],[8.,8.,0]]),self.route([[-4.,-4.,0],[0.,-4.,0]],class_tag='heavyTank'),self.route([[200.,200.,0]],class_tag='SPG'),self.route([[-8.,-8.,0]],id='disabled',disabled=True)])
  actual=self.compare([request(base),request(authored)])
  self.assertEqual(actual[0]['resources']['strategy']['class_routes'],{'1':{},'2':{}})
  variants=actual[1]['resources']['strategy']['class_routes']['1']['lane'];self.assertEqual([v['class_tag'] for v in variants],CLASSES)
 def test_factory_and_cache_never_publish_dense_value_columns_or_retain_python(self):
  source=(ROOT/'bigworld/stock_native_room_resources.cpp').read_text();header=(ROOT/'bigworld/stock_native_room_resources.hpp').read_text();round_source=(ROOT/'bigworld/stock_native_battle_bots_round.cpp').read_text();planning=(ROOT/'bigworld/stock_native_bot_planning_snapshot.cpp').read_text()
  self.assertNotIn('Python.h',source+header);self.assertNotIn('PyObject',source+header)
  self.assertIn('takeMapInput(seed.navigation_input)',planning);self.assertIn('readSharedMapInput(*seed, navigation)',round_source)
  self.assertLess(round_source.index('link::configureRoomResources('),round_source.index('outgoing.object["type"] = text("bot_manifest")'))
 @unittest.skipUnless(os.environ.get('WOT_ROOM_NAVGRAPHS'),'Real current-map navgraphs are optional cold evidence')
 def test_real_current_map_empty_and_reviewed_default_profiles(self):
  script="import json,sys;sys.path.insert(0,sys.argv[1]);from gui.mods.offline_lan_0922 import bot_tactics as c,prebaked_navigation as p;raw=json.load(sys.stdin);print(json.dumps([p.with_shore_hazards(raw),c.for_round(c.empty(),raw['map']),c.for_round(c.default_profile(),raw['map'])]))"
  requests=[]
  for name in ('04_himmelsdorf','05_prohorovka'):
   raw=json.loads((Path(os.environ['WOT_ROOM_NAVGRAPHS'])/(name+'.json')).read_text())
   graph,empty,reviewed=json.loads(subprocess.run([sys.executable,'-I','-B','-c',script,str(Path(FORK)/'src/res/scripts/client')],input=json.dumps(raw),capture_output=True,text=True,check=True).stdout.splitlines()[-1])
   for tactics in (empty,reviewed):
    archive=dict(maps={name:dict(tactics=tactics,tactics_digest='reviewed-existing-token',tactical_map=dict(bounds=graph['bounds'],routes={}))},spg_catalog=dict(schema=1))
    requests.append(dict(graph=graph,routes=[],tactics=tactics,archive=archive,actual_routes=graph['routes']))
  results=self.compare(requests)
  for request,result in zip(requests,results):
   self.assertNotIn('graph',result['resources'])
   self.assertLessEqual(result['graph']['bytes'],6*1000000)
   print('Cold current-map factory %s authored=%s cells=%d bytes=%d elapsed_us=%d'%(request['graph']['map'],bool(request['tactics']['maps']),len(request['graph']['heights_mm']),result['graph']['bytes'],result['prepare_us']))
 def test_cold_identity_and_compact_column_mismatches_are_rejected(self):
  graph=self.graph();tactics=dict(schema=1,client='wot-0.9.22.0.1-cn-1513',name='Empty',behavior=[],maps={})
  archive=dict(maps={'04_himmelsdorf':dict(tactics=tactics,tactics_digest='reviewed-existing-token',tactical_map=dict(bounds=graph['bounds']))})
  base=dict(graph=graph,tactics=tactics,archive=archive,actual_routes={},routes=[]);cases=[]
  wrong=copy.deepcopy(base);wrong['graph']['game_version']='foreign';cases.append(wrong)
  wrong=copy.deepcopy(base);wrong['graph']['links'][0]=256;cases.append(wrong)
  wrong=copy.deepcopy(base);wrong['graph']['heights_mm'].pop();cases.append(wrong)
  wrong=copy.deepcopy(base);wrong['graph']['directions'][0]=[1.e20,0];cases.append(wrong)
  wrong=copy.deepcopy(base);wrong['archive']['maps']['04_himmelsdorf'].pop('tactics_digest');cases.append(wrong)
  wrong=copy.deepcopy(base);wrong['tactics']=dict(wrong['tactics'],name='unreviewed');cases.append(wrong)
  result=subprocess.run([str(self.binary)],input='\n'.join(json.dumps(c) for c in cases)+'\n',capture_output=True,text=True,check=True)
  for value in result.stdout.splitlines():self.assertIn('error',json.loads(value))
if __name__=='__main__':unittest.main()
