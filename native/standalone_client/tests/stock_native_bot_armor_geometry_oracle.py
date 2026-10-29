from __future__ import print_function
import ast,imp,json,math,os,sys,types,struct
base=os.path.join(sys.argv[1],'src/res/scripts/client/gui/mods/offline_lan_0922')
for name in ('gui','gui.mods','gui.mods.offline_lan_0922'):
    module=types.ModuleType(name);module.__path__=[];sys.modules[name]=module
native=types.ModuleType('gui.mods.offline_lan_0922.native_math');native.call=lambda *unused:None
sys.modules[native.__name__]=native;sys.modules['gui.mods.offline_lan_0922'].native_math=native
collision=imp.load_source('armor_collision_oracle',os.path.join(base,'tank_collision.py'))
class Box(object):
    def __init__(self,**values):self.__dict__.update(values)
class Vector(tuple):
    def __new__(cls,p):return tuple.__new__(cls,p)
    x=property(lambda s:s[0]);y=property(lambda s:s[1]);z=property(lambda s:s[2])
env=dict(math=math,tank_collision=collision,_xyz=lambda p:tuple(p))
source=ast.parse(open(os.path.join(base,'battle_runtime.py')).read());names=set(('_native_ram_vehicle_armor','_ram_plate_pair_from_probe_impl','_ram_corner_probe_direction','_validated_ram_contact_normal','_ram_contact_xz_samples'))
owner=next(n for n in source.body if isinstance(n,ast.ClassDef) and any(isinstance(m,ast.FunctionDef) and m.name in names for m in n.body))
methods=[n for n in owner.body if isinstance(n,ast.FunctionDef) and n.name in names]
owner.name='Oracle';owner.bases=[ast.Name(id='object',ctx=ast.Load())];owner.decorator_list=[];owner.body=methods
exec(compile(ast.fix_missing_locations(ast.Module(body=[owner])),'fixed-armor-methods','exec'),env)
Oracle=env['Oracle'];Oracle._vector=staticmethod(Vector);Oracle._collision_shape=staticmethod(lambda d:d.shape)
Oracle._ram_obb_overlap_polygon=staticmethod(collision.obb_overlap_polygon)
for line in sys.stdin:
    data=json.loads(line);mode,a,b,normal=data;law=mode%7
    def body(row):
        result=dict(zip(('id','x','y','z','yaw','pitch','roll'),row[:7]));result['shape']=row[7:];return result
    Oracle._vector=staticmethod(lambda p:Vector(tuple(struct.unpack('f',struct.pack('f',v))[0] for v in p)) if mode>=14 else Vector(p))
    a,b=body(a),body(b);polygon=collision.obb_overlap_polygon(a,b) or [];queries=[]
    def vehicle(body):
        hull=Box(hitTester=Box(bbox=((-1.,0.,-2.),(1.,.8,2.)))) if law%2==0 else Box()
        shape=list(body['shape']);position=(body['x'],body['y'],body['z']);pitch=body['pitch'];roll=body['roll']
        if mode>=7:
            shape[0]*=1.1;shape[1]*=.9;position=(position[0]+.15,position[1]+.2,position[2]-.1);pitch+=.17;roll-=.09
        descriptor=Box(shape=shape,chassis=Box(hullPosition=(0.,.3,0.)),hull=hull)
        return Box(id=body['id'],typeDescriptor=descriptor),Box(translation=position,pitch=pitch,roll=roll)
    va,ma=vehicle(a);vb,mb=vehicle(b);oracle=Oracle();oracle._runtime=Box(math=None);oracle._ram_contact_ray_trace=None
    def segment(vehicle,matrix,start,end,unused,**kw):
        queries.append([vehicle.id,list(start),list(end)]);shape=vehicle.typeDescriptor.shape;reach=math.sqrt(shape[0]*shape[0]+shape[1]*shape[1])+1.
        track=Box(extra=Box(name='leftTrackHealth'),armor=20.,vehicleDamageFactor=0.)
        plate=Box(armor=40.,vehicleDamageFactor=1.);bad=Box(armor=float('nan'),vehicleDamageFactor=1.)
        y=start[1];solid=True
        if law==1:solid=.65<=y<=.85
        if law==2:solid=abs(end[2]-start[2])>.1
        if law==3:solid=y<.7 if vehicle.id==1 else y>1.3
        if law==5:return ()
        if not solid:values=[(reach-.05,track)]
        elif law==4:values=[(reach,plate),(reach+.1,track)]
        else:values=[(reach-(.25 if law==6 else .05),track),(reach+.01,bad),(reach+.1,plate)]
        return [Box(dist=d,matInfo=m) for d,m in values]
    env['collide_vehicle_at_matrix']=segment
    n=oracle._validated_ram_contact_normal(normal);result=None
    low=max(a['y']+a['shape'][2],b['y']+b['shape'][2]);high=min(a['y']+a['shape'][3],b['y']+b['shape'][3])
    if polygon and n is not None and high>low:
        center=(sum(p[0] for p in polygon)/len(polygon),sum(p[1] for p in polygon)/len(polygon))
        proof=dict(contact_normal=n,hit_point=(center[0],(low+high)*.5,center[1]),contact_y_span=(low,high),
            contact_xz_candidates=oracle._ram_contact_xz_samples(a,b,n),local_vehicle=va,bot_vehicle=vb,local_matrix=ma,bot_matrix=mb)
        oracle._ram_surface_context=dict(tracks={},normals={id(va):n,id(vb):(-n[0],-n[1])})
        matched,unused_a,unused_b=oracle._ram_plate_pair_from_probe_impl(proof,oracle._native_ram_vehicle_armor)
        if matched:
            tracks=oracle._ram_surface_context['tracks'];result=[[matched[0]['armor'],matched[1]['armor']],[sorted(tracks.get(id(va),())),sorted(tracks.get(id(vb),()))]]
    print(json.dumps([result,queries,polygon]))
