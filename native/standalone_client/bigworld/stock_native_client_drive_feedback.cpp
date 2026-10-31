#if !defined(WOT_CLIENT_FEEDBACK_HOST_BUILD)
#include "pch.hpp"
#include "connection_control.hpp"
#include "entity.hpp"
#include "py_entity.hpp"
#include "pyscript/script_math.hpp"
#endif
#include "stock_native_client_drive_feedback.hpp"
#include "stock_native_client_drive_roster.hpp"
#include "stock_native_client_state.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_battle_local.hpp"
#include <algorithm>
#include <map>
namespace stock_native_client_drive {
namespace {
namespace p=stock_native_bot_python;
struct View {PyObject_HEAD PyObject*original;PyObject*replacement;};
void dispose(View*self){Py_XDECREF(self->original);Py_XDECREF(self->replacement);Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));}
PyObject*forward(PyObject*self,PyObject*name){return PyObject_GetAttr(reinterpret_cast<View*>(self)->original,name);}
PyObject*replacement(View*self,void*){Py_INCREF(self->replacement);return self->replacement;}
PyTypeObject initialType(){PyTypeObject out{};PyObject_Init(reinterpret_cast<PyObject*>(&out),&PyType_Type);return out;}
PyTypeObject filterType=initialType();
PyTypeObject vehicleType=initialType();
PyGetSetDef filterFields[]={{const_cast<char*>("velocity"),reinterpret_cast<getter>(replacement),nullptr,nullptr,nullptr},{nullptr,nullptr,nullptr,nullptr,nullptr}};
PyGetSetDef vehicleFields[]={{const_cast<char*>("filter"),reinterpret_cast<getter>(replacement),nullptr,nullptr,nullptr},{nullptr,nullptr,nullptr,nullptr,nullptr}};
void ready(PyTypeObject&type,const char*name,PyGetSetDef*fields){if(type.tp_name)return;type.tp_name=name;type.tp_basicsize=sizeof(View);type.tp_flags=Py_TPFLAGS_DEFAULT;type.tp_dealloc=reinterpret_cast<destructor>(dispose);type.tp_getattro=[](PyObject*self,PyObject*name)->PyObject*{auto*out=p::directAttribute(self,name);if(out||!PyErr_ExceptionMatches(PyExc_AttributeError))return out;PyErr_Clear();return forward(self,name);};type.tp_getset=fields;if(PyType_Ready(&type)<0)throw p::Error("Native collision feedback view initialization failed");}
p::Ref view(PyTypeObject&type,PyObject*original,PyObject*value){auto*self=reinterpret_cast<View*>(type.tp_alloc(&type,0));if(!self)throw p::Error("Native collision feedback view allocation failed");Py_INCREF(original);Py_INCREF(value);self->original=original;self->replacement=value;return p::Ref(reinterpret_cast<PyObject*>(self));}
#if defined(WOT_CLIENT_FEEDBACK_HOST_BUILD)
extern p::Ref feedbackEntity(ID);
extern p::Ref feedbackVector(const Point&);
#else
p::Ref feedbackEntity(ID id){auto*c=BW::ConnectionControl::pInstance();auto*e=c?c->findEntity(static_cast<BW::EntityID>(id)):nullptr;if(!e||e->isDestroyed()||!e->isInWorld())return p::retain(Py_None);return p::Ref(e->pPyEntity().newRef());}
p::Ref feedbackVector(const Point&v){return p::Ref(BW::Script::getData(BW::Vector3(float(v[0]),float(v[1]),float(v[2]))));}
#endif
p::Ref entityView(PyObject*entity,const Point&velocity){auto filter=p::value(entity,"filter");auto v=feedbackVector(velocity);auto filter_view=view(filterType,filter.get(),v.get());return view(vehicleType,entity,filter_view.get());}
}
struct PlayerFeedback::Impl {
    std::shared_ptr<stock_native_client_state::State>state;std::uint64_t epoch;
    p::Ref avatar,callback;std::map<std::pair<ID,ID>,double>last;
    // The stock Avatar contact callback the native hook replaced receives the presented contact.
    explicit Impl(const native_battle::Battle&b):state(b.vehicles),epoch(state->epoch),avatar(p::retain(b.engine.avatar?b.engine.avatar.get():Py_None)),callback(p::retain(b.local&&b.local->ram_contact_hook?b.local->ram_contact_hook.get():Py_None)) {ready(filterType,"offline_client_drive.FilterView",filterFields);ready(vehicleType,"offline_client_drive.VehicleView",vehicleFields);}
};
PlayerFeedback::PlayerFeedback(const native_battle::Battle&b):impl_(new Impl(b)){}
PlayerFeedback::~PlayerFeedback()=default;
void PlayerFeedback::observed(ID first,ID second,const Point&a,const Point&b,double now){auto&o=*impl_;if(o.state->retired||o.state->epoch!=o.epoch)return;const float x=float(a[0])-float(b[0]),y=float(a[1])-float(b[1]),z=float(a[2])-float(b[2]);const float length=std::sqrt(float(float(x*x+y*y)+z*z));if(length<=0.f)return;o.last[{std::min(first,second),std::max(first,second)}]=now;}
void PlayerFeedback::present(PyObject*local,const offline_contact::Body&own,const ContactPeer&peer,double now){auto&o=*impl_;if(o.state->retired||o.state->epoch!=o.epoch||o.avatar.get()==Py_None||!PyCallable_Check(o.callback.get()))return;auto other=feedbackEntity(peer.engine_id);if(other.get()==Py_None||!p::truth(local,"isStarted")||!p::truth(other.get(),"isStarted"))return;
    const ID first=p::integral(local,"id"),second=p::integral(other.get(),"id");const auto key=std::make_pair(std::min(first,second),std::max(first,second));const auto previous=o.last.find(key);if(previous!=o.last.end()&&now-previous->second<.2)return;if(own.vx==peer.body.vx&&own.vy==peer.body.vy&&own.vz==peer.body.vz)return;const auto overlap=stock_native_client_contact_roster::overlapPoint(own,peer.body);if(!overlap)return;
    const double low=std::max(own.y+own.shape[2],peer.body.y+peer.body.shape[2]),high=std::min(own.y+own.shape[3],peer.body.y+peer.body.shape[3]);auto first_view=entityView(local,{own.vx,own.vy,own.vz}),second_view=entityView(other.get(),{peer.body.vx,peer.body.vy,peer.body.vz}),point=feedbackVector({(*overlap)[0],(low+high)*.5,(*overlap)[1]});auto time=p::number(now);p::Ref args(PyTuple_Pack(5,o.avatar.get(),first_view.get(),second_view.get(),point.get(),time.get()));o.last[key]=now;p::Ref result(PyObject_CallObject(o.callback.get(),args.get()));
}
}
