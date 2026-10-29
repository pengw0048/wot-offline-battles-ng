#include "../bigworld/stock_native_bot_internal_geometry.hpp"
#include "../bigworld/stock_native_projectile_cone.hpp"
#include <Python.h>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
using namespace wot::offline_bot_step;
int main(){Py_DontWriteBytecodeFlag=1;Py_Initialize();const char* source=std::getenv("WOT_GAMEPLAY_SOURCE");assert(source);
 auto path=PySys_GetObject(const_cast<char*>("path"));std::string scripts=std::string(source)+"/src/res/scripts/client";
 PyObject* p=PyString_FromString(scripts.c_str());assert(PyList_Insert(path,0,p)==0);Py_DECREF(p);
 const char* script=R"PY(
import sys, types
gui=types.ModuleType('gui');gui.__path__=[sys.path[0]+'/gui'];sys.modules['gui']=gui
mods=types.ModuleType('gui.mods');mods.__path__=[sys.path[0]+'/gui/mods'];sys.modules['gui.mods']=mods
from gui.mods.offline_lan_0922 import critical_damage,internal_geometry,internal_mesh,internal_hit_layouts
vertices=[(-1.,-1.,-1.),(1.,-1.,-1.),(1.,1.,-1.),(-1.,1.,-1.),(-1.,-1.,1.),(1.,-1.,1.),(1.,1.,1.),(-1.,1.,1.)]
triangles=[(0,2,1),(0,3,2),(4,5,6),(4,6,7),(0,1,5),(0,5,4),(1,2,6),(1,6,5),(2,3,7),(2,7,6),(3,0,4),(3,4,7)]
mesh=internal_mesh.prepare(vertices,triangles)[0]
rows=[{'shape':'aabb','minimum':(-1.,-1.,-1.),'maximum':(1.,1.,1.)},
 {'shape':'aabb','center':(0.,0.,0.),'half_extents':(1.,2.,1.),'rotation_yaw_degrees':30.},
 {'shape':'sphere','center':(0.,0.,0.),'radius':1.},
 {'shape':'ellipsoid','center':(0.,0.,0.),'radii':(1.,2.,.5),'rotation_yaw_degrees':25.},
 {'shape':'capsule','center':(0.,0.,0.),'radius':.5,'half_length':1.,'axis':'x','rotation_yaw_degrees':15.},mesh]
layout={'valid':True,'targets':[{'parent':'hull','entity':'engine','primitives':rows}]}
cone_layout={'valid':True,'targets':[{'parent':'hull','entity':'device%d'%i,'zone_id':'zone%d'%i,'primitives':[row]} for i,row in enumerate(rows)]}
cone_cases=[(-3.,.2,.1),(-3.,2.1,0.),(-3.,3.,3.),(0.,0.,0.),(-3.,-1.,1.)]
cone_expected=[[(r['entity']+'Health',r['cone_entry_axial_m']) for r in internal_hit_layouts.resolve_explosion(cone_layout,{'hull':{'point':point,'direction':(1.,0.,0.)}},5.*2**.5,mode='cone',cone_cos=2**-.5,cone_depth_m=5.) if r['damage_eligible']] for point in cone_cases]
critical_damage._offh_internal_layout=lambda descriptor:layout
descriptor=object();segments=[((-3.,.2,.1),(3.,.2,.1)),((3.,.2,.1),(-3.,.2,.1)),
    ((0.,0.,0.),(.2,.1,0.)),((3.,3.,3.),(4.,3.,3.)),((0.,0.,0.),(0.,0.,0.))]
expected=[[internal_geometry.primitive_interval(a,b,p) for a,b in segments] for p in rows]
)PY";
 if(PyRun_SimpleString(script)){PyErr_Print();return 1;}
 auto main=PyModule_GetDict(PyImport_AddModule("__main__"));InternalLayout layout;
 if(!load_internal_layout(PyDict_GetItemString(main,"descriptor"),layout)){PyErr_Print();return 2;}
 assert(layout.valid && layout.targets.size()==1 && layout.targets[0].primitives.size()==6);
 auto expected=PyDict_GetItemString(main,"expected");const Point start{-3.,.2,.1},end{3.,.2,.1};
 auto raw_segments=PyDict_GetItemString(main,"segments");
 for(int i=0;i<6;++i)for(int j=0;j<5;++j){auto raw=PyList_GetItem(raw_segments,j);auto a=PyTuple_GetItem(raw,0),b=PyTuple_GetItem(raw,1);Point from{},to{};
  for(int k=0;k<3;++k){from[k]=PyFloat_AsDouble(PyTuple_GetItem(a,k));to[k]=PyFloat_AsDouble(PyTuple_GetItem(b,k));}
  auto ranges=internal_intervals(layout.targets[0].primitives[i],from,to);auto want=PyList_GetItem(PyList_GetItem(expected,i),j);
  if(want==Py_None){assert(ranges.empty());continue;}assert(!ranges.empty());
  assert(std::abs(ranges[0].first-PyFloat_AsDouble(PyTuple_GetItem(want,0)))<1.e-9);
  assert(std::abs(ranges[0].second-PyFloat_AsDouble(PyTuple_GetItem(want,1)))<1.e-9);}
 std::array<InternalSegment,4> segments{};segments[1]={start,end,true};const auto hits=internal_hits(layout,segments,6.);
 assert(hits.size()==1 && hits[0].name=="engineHealth" && hits[0].distance>0.);
 assert(PyRun_SimpleString("critical_damage._offh_internal_layout=lambda descriptor:cone_layout")==0);InternalLayout cone_layout;if(!load_internal_layout(PyDict_GetItemString(main,"descriptor"),cone_layout)){PyErr_Print();return 3;}
 auto cases=PyDict_GetItemString(main,"cone_cases"),expected_cones=PyDict_GetItemString(main,"cone_expected");for(int case_index=0;case_index<5;++case_index){auto raw=PyList_GetItem(cases,case_index);Point a{};for(int k=0;k<3;++k)a[k]=PyFloat_AsDouble(PyTuple_GetItem(raw,k));auto b=a;b[0]+=5.;std::array<InternalSegment,4> cone_segments{};cone_segments[1]={a,b,true};auto cone=stock_native_projectile_damage::internalCone(cone_layout,cone_segments,5.,{});auto want_cone=PyList_GetItem(expected_cones,case_index);assert(cone.size()==std::size_t(PyList_Size(want_cone)));for(std::size_t i=0;i<cone.size();++i){auto row=PyList_GetItem(want_cone,Py_ssize_t(i));assert(cone[i].name==PyString_AsString(PyTuple_GetItem(row,0)));if(std::abs(cone[i].distance-PyFloat_AsDouble(PyTuple_GetItem(row,1)))>=1.e-8)std::cerr<<"case="<<case_index<<" device="<<cone[i].name<<" actual="<<cone[i].distance<<" expected="<<PyFloat_AsDouble(PyTuple_GetItem(row,1))<<"\n";assert(std::abs(cone[i].distance-PyFloat_AsDouble(PyTuple_GetItem(row,1)))<1.e-8);}}

}
