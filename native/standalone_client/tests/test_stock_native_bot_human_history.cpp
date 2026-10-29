#include "../bigworld/stock_native_bot_human_history.hpp"
#include <Python.h>
#include <cassert>
int main(){Py_DontWriteBytecodeFlag=1;Py_Initialize();const char* setup=R"PY(
class O: pass
battle=O(); bots=O(); bots.states={7: {'mass': 42, 'collision_shape': [1,2,3,4], 'ram_profile': {}, 'team': 1}}
a={'id': 7,'x': 0.,'y': 1.,'z': 2.,'yaw': 3.,'mass': 42,'collision_shape': [1,2,3,4], 'ram_profile': {}, 'ram_motion': [[9,1,100]+[0.]*18]}
b=dict(a); b.update(x=10.,yaw=-3.,ram_motion=[[9,2,300]+[1.]*18])
battle._ram_bot_history={10:{7:a},20:{7:b}}
battle._ram_bot_history_index={7:[(100,10),(300,20)]}
raw=[{'id':1,'participating':True,'world_pose':True,'ram_contacts':[{'bot_id':7,'bot_state_revision':15,'presentation_time_us':200},{'bot_id':7,'bot_state_revision':15,'presentation_time_us':200,'ram_bot_state':{'id':7,'x':99}}, {'bot_id':7,'bot_state_revision':10,'presentation_time_us':100}, {'bot_id':7,'bot_state_revision':9,'presentation_time_us':50}]}, {'id':4,'participating':True,'world_pose':True}, {'id':2,'participating':True,'world_pose':False},{'id':-1,'participating':True,'world_pose':True}]
)PY";assert(PyRun_SimpleString(setup)==0);auto main=PyImport_AddModule("__main__");auto globals=PyModule_GetDict(main);auto result=stock_native_bot::prepareHumanInput(PyDict_GetItemString(globals,"raw"),PyDict_GetItemString(globals,"battle"),PyDict_GetItemString(globals,"bots"));if(!result)PyErr_Print();assert(result);PyDict_SetItemString(globals,"result",result);Py_DECREF(result);assert(PyRun_SimpleString("assert len(result)==2 and result[1]['ram_contacts']==[]\nr=result[0]['ram_contacts']\na=r[0]['_ram_contact_bot_state']\nassert a['x']==5. and abs(a['yaw']-3.141592653589793)<1e-12\nassert a['ram_vx']==50000. and a['ram_motion'][0][1]==1\np=r[1]['_ram_contact_bot_state']\nassert p['x']==99 and p['mass']==42 and p['team']==1\nassert r[2]['_ram_contact_bot_state']['ram_vx']==50000.\nassert '_ram_contact_bot_state' not in r[3]\n") == 0);Py_Finalize();}
