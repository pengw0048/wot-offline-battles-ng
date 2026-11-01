#include "../bigworld/stock_native_player_environment_python.hpp"
#include "../bigworld/stock_native_battle.hpp"
#include "../bigworld/stock_native_battle_remote.hpp"
#include <Python.h>
#include <cassert>
using namespace stock_native_player_environment;
namespace native_battle {
Battle* current(){return nullptr;}
namespace remote {
p::Ref get(const Battle&,std::int64_t){Py_INCREF(Py_None);return p::Ref(Py_None);}
Overlay* overlay(PyObject*,bool){return nullptr;}
}
}
namespace {
using stock_native_client_state::Value;
Value number(double n){Value v;v.kind=Value::Kind::Number;v.number=n;return v;}
Value row(double x,double y,double z,double yaw){Value v;v.kind=Value::Kind::Object;
 v.object["x"]=number(x);v.object["y"]=number(y);v.object["z"]=number(z);v.object["yaw"]=number(yaw);v.object["pitch"]=number(0.);v.object["roll"]=number(0.);return v;}
}
int main(){Py_DontWriteBytecodeFlag=1;Py_Initialize();
 if(PyRun_SimpleString("class O(object):pass\nentity=O();entity.siegeState=0;entity.devices_hp=None;entity._destroyed_devices=set();entity._critical_devices=set();entity._crew_ko=set();entity.is_on_fire=False;entity._ammo_rack_death=False\n"))return 1;
 auto* entity=PyDict_GetItemString(PyModule_GetDict(PyImport_AddModule("__main__")),"entity");
 // The worker seeds a human's collision pose at spawn; the authority row moves on.
 stock_native_client_state::Actor record;record.kind="player";record.id=3;
 record.presented.collision_pose=row(0.,0.,-35.,0.);
 record.state=row(382.,56.31,38.,1.25);record.state.object["alive"].kind=Value::Kind::Boolean;record.state.object["alive"].boolean=true;
 record.state.object["health"]=number(100.);record.state.object["input_seq"]=number(64.);
 Metadata metadata;Player player;
 if(!readPlayer(record,entity,metadata,player)){PyErr_Print();return 2;}
 assert(player.id==3&&player.input_sequence==64);
 assert(player.pose.position[0]==382.&&player.pose.position[1]==56.31&&player.pose.position[2]==38.&&player.pose.yaw==1.25);
 Py_Finalize();}
