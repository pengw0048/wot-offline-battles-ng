#include "stock_native_player_contacts_python.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_client_state.hpp"
namespace c=stock_native_player_contacts;
namespace p=stock_native_bot_python;
PyObject* convert(PyObject*,PyObject*args){try{
    PyObject*players=nullptr;if(!PyArg_ParseTuple(args,"O",&players))return nullptr;
    stock_native_bot::HumanDescriptor frozen;frozen.mass=25000.;frozen.body_minimum={-1.,0.,-2.};frozen.body_maximum={1.,2.,2.};
    frozen.contact_drive_forward=20.;frozen.contact_drive_reverse=-8.;frozen.contact_rotation_speed=.7;
    frozen.contact_mass=25000.;
    frozen.pivot_available=true;frozen.rotation_around_center=false;frozen.track_center=1.2;
    std::map<c::ID,stock_native_bot::HumanDescriptor> descriptors{{1,frozen}};std::vector<c::Request> requests;std::uint64_t rejected=0;
    if(!c::readPlayerContacts(stock_native_client_state::read(players),descriptors,requests,rejected))return nullptr;
    std::vector<c::Verdict> verdicts;auto validity=p::list();std::uint64_t serial=0;
    for(const auto&r:requests){verdicts.push_back({++serial,r.receipt.player,r.receipt.sequence,true,r.receipt.token});
        p::append(validity.get(),p::boolean(r.descriptor&&r.descriptor->traverse_valid));
        if(r.descriptor&&(r.descriptor->drive_forward!=20.||r.descriptor->track_center!=1.2))throw p::Error("Contact descriptor copy mismatch");}
    auto result=p::dict();p::set(result.get(),"verdicts",p::Ref(stock_native_client_state::write(c::packPlayerVerdicts(verdicts))));
    p::set(result.get(),"rejected",p::integer(rejected));p::set(result.get(),"traverse_valid",validity);return result.release();
}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
static PyMethodDef methods[]={{"convert",convert,METH_VARARGS,nullptr},{nullptr,nullptr,0,nullptr}};
PyMODINIT_FUNC initplayer_contacts_test(){Py_InitModule("player_contacts_test",methods);}
