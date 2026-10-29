#include "stock_native_bot_internal_geometry.hpp"
#include <Python.h>
#include <cmath>
#include <stdexcept>
namespace wot::offline_bot_step {
namespace {
struct Error:std::runtime_error {using std::runtime_error::runtime_error;};
struct Ref {PyObject* p;explicit Ref(PyObject* q=nullptr):p(q){}~Ref(){Py_XDECREF(p);}Ref(const Ref&)=delete;Ref& operator=(const Ref&)=delete;PyObject* get()const{return p;}};
PyObject* field(PyObject* d,const char* key){if(!PyDict_Check(d))throw Error("internal descriptor is not a dict");return PyDict_GetItemString(d,key);}
Py_ssize_t count(PyObject* p){if(!p || !PySequence_Check(p))throw Error("internal sequence is invalid");return PySequence_Size(p);}
Ref item(PyObject* p,Py_ssize_t i){auto value=PySequence_GetItem(p,i);if(!value)throw Error("internal sequence index invalid");return Ref(value);}
double number(PyObject* p){if(!p || p==Py_None)throw Error("internal number missing");const double v=PyFloat_AsDouble(p);if(PyErr_Occurred() || !std::isfinite(v))throw Error("internal number invalid");return v;}
double field_number(PyObject* d,const char* key,double fallback=0.){auto p=field(d,key);return !p || p==Py_None?fallback:number(p);}
std::string str(PyObject* p){if(!p || p==Py_None)return {};
#if PY_MAJOR_VERSION >= 3
 const char* v=PyUnicode_AsUTF8(p);if(!v)throw Error("internal string invalid");return v;
#else
 if(PyString_Check(p))return std::string(PyString_AS_STRING(p),PyString_GET_SIZE(p));
 Ref encoded(PyUnicode_AsUTF8String(p));if(!encoded.get())throw Error("internal string invalid");return std::string(PyString_AS_STRING(encoded.get()),PyString_GET_SIZE(encoded.get()));
#endif
}
Point point(PyObject* p){if(count(p)!=3)throw Error("internal point invalid");auto x=item(p,0),y=item(p,1),z=item(p,2);return {number(x.get()),number(y.get()),number(z.get())};}
Point field_point(PyObject* d,const char* key,Point fallback={}){auto p=field(d,key);return !p || p==Py_None?fallback:point(p);}
int node(PyObject* raw,InternalMesh& mesh){if(count(raw)!=5)throw Error("internal BVH invalid");
 const int index=static_cast<int>(mesh.nodes.size());mesh.nodes.emplace_back();auto low=item(raw,0),high=item(raw,1),left=item(raw,2),right=item(raw,3),faces=item(raw,4);
 mesh.nodes[index].minimum=point(low.get());mesh.nodes[index].maximum=point(high.get());
 if(left.get()!=Py_None){const int l=node(left.get(),mesh),r=node(right.get(),mesh);mesh.nodes[index].left=l;mesh.nodes[index].right=r;}
 else if(faces.get()!=Py_None)for(Py_ssize_t i=0;i<count(faces.get());++i){auto v=item(faces.get(),i);const double n=number(v.get());if(n<0.||n>=mesh.triangles.size()||std::floor(n)!=n)throw Error("internal BVH face invalid");mesh.nodes[index].triangles.push_back(static_cast<int>(n));}
 return index;
}
InternalPrimitive primitive(PyObject* raw){InternalPrimitive out;const auto shape=str(field(raw,"shape"));
 out.shape=shape=="mesh"?InternalShape::mesh:shape=="sphere"?InternalShape::sphere:shape=="ellipsoid"?InternalShape::ellipsoid:shape=="capsule"?InternalShape::capsule:InternalShape::box;
 out.id=str(field(raw,"primitive_id"));out.has_center=field(raw,"center")&&field(raw,"center")!=Py_None;out.has_half_extents=field(raw,"half_extents")&&field(raw,"half_extents")!=Py_None;out.center=field_point(raw,"center");out.minimum=field_point(raw,"minimum");out.maximum=field_point(raw,"maximum");
 out.half_extents=field_point(raw,"half_extents",Point{.1,.1,.1});out.radii=field_point(raw,"radii",out.half_extents);
 out.radius=field_number(raw,"radius",.1);out.half_length=field_number(raw,"half_length");out.yaw_degrees=field_number(raw,"rotation_yaw_degrees");
 const auto axis=str(field(raw,"axis"));out.axis=axis=="x"?0:axis=="z"?2:1;
 if(out.shape==InternalShape::mesh){auto vertices=field(raw,"vertices"),triangles=field(raw,"triangles"),tree=field(raw,"bvh");
  for(Py_ssize_t i=0;i<count(vertices);++i){auto v=item(vertices,i);out.mesh.vertices.push_back(point(v.get()));}
  for(Py_ssize_t i=0;i<count(triangles);++i){auto face=item(triangles,i);if(count(face.get())!=3)throw Error("internal face invalid");std::array<int,3> ids{};
   for(int j=0;j<3;++j){auto v=item(face.get(),j);const double n=number(v.get());if(n<0.||n>=out.mesh.vertices.size()||std::floor(n)!=n)throw Error("internal vertex index invalid");ids[j]=static_cast<int>(n);}out.mesh.triangles.push_back(ids);}
  auto closed=field(raw,"closed");const int c=closed?PyObject_IsTrue(closed):0;if(c<0)throw Error("internal closed invalid");out.mesh.closed=c!=0;
  auto orientations=field(raw,"orientations");if(orientations && orientations!=Py_None){if(count(orientations)!=static_cast<Py_ssize_t>(out.mesh.triangles.size()))throw Error("internal orientations invalid");
   for(Py_ssize_t i=0;i<count(orientations);++i){auto v=item(orientations,i);const int n=static_cast<int>(number(v.get()));if(n!=1 && n!=-1)throw Error("internal orientation invalid");out.mesh.orientations.push_back(n);}}
  if(!tree || tree==Py_None)throw Error("internal BVH missing");node(tree,out.mesh);
 }
 return out;
}
}
bool load_internal_layout(PyObject* descriptor,InternalLayout& output){
 try {Ref module(PyImport_ImportModule("gui.mods.offline_lan_0922.critical_damage"));if(!module.get())throw Error("critical import failed");
  Ref getter(PyObject_GetAttrString(module.get(),"_offh_internal_layout"));if(!getter.get())throw Error("internal layout getter missing");
  Ref raw(PyObject_CallFunctionObjArgs(getter.get(),descriptor,nullptr));if(!raw.get())throw Error("internal layout getter failed");
  InternalLayout result;if(raw.get()==Py_None){output=std::move(result);return true;}
  auto valid=field(raw.get(),"valid");const int flag=valid?PyObject_IsTrue(valid):0;if(flag<0)throw Error("internal valid invalid");result.valid=flag!=0;
  auto targets=field(raw.get(),"targets");if(result.valid && targets && targets!=Py_None)for(Py_ssize_t i=0;i<count(targets);++i){auto row=item(targets,i);InternalTarget target;
   target.parent=str(field(row.get(),"parent"));target.entity=str(field(row.get(),"entity"));target.zone_id=str(field(row.get(),"zone_id"));auto primitives=field(row.get(),"primitives");
   if(primitives && primitives!=Py_None)for(Py_ssize_t j=0;j<count(primitives);++j){auto p=item(primitives,j);target.primitives.push_back(primitive(p.get()));}
   result.targets.push_back(std::move(target));}
  output=std::move(result);return true;
 }catch(const std::exception& e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}
}
