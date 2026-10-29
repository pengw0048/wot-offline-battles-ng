from __future__ import print_function
import imp
import json
import os
import sys
import types
base=os.path.join(sys.argv[1],'src/res/scripts/client/gui/mods/offline_lan_0922')
for name in ('gui','gui.mods','gui.mods.offline_lan_0922'):
    module=types.ModuleType(name);module.__path__=[];sys.modules[name]=module
native=types.ModuleType('gui.mods.offline_lan_0922.native_math')
native.call=lambda *unused:None
sys.modules[native.__name__]=native
sys.modules['gui.mods.offline_lan_0922'].native_math=native
collision=imp.load_source('contact_oracle',os.path.join(base,'tank_collision.py'))
def body(values):
    out=dict(zip(('id','x','y','z','yaw','pitch','roll'),values[:7]))
    out['shape']=values[7:];return out
for line in sys.stdin:
    value=json.loads(line);op=value[0]
    if op==0:result=collision.ram_damage(*value[1:])
    elif op==1:
        a,b=body(value[1]),body(value[2]);p=value[3]
        result=[collision.obb_contact(a['x'],a['z'],a['yaw'],a['shape'],b['x'],b['z'],b['yaw'],b['shape']),
            collision.vertical_overlap(a['y'],a['shape'],b['y'],b['shape'],pitch_a=a['pitch'],roll_a=a['roll'],pitch_b=b['pitch'],roll_b=b['roll']),
            collision.body_contains_point(a,p,.75)]
    else:
        # Exercise the embedded client's x86 integer hashing for negative ids.
        # Current runtime IDs are positive 31-bit integers; the generated cases
        # stay in that domain so the host 64-bit build has the same integer hash.
        data={}
        for key in value[1:]:data[key]=None
        result=list(data)
    print(json.dumps(result))
