#include "stock_native_battle_params.hpp"
#include "stock_native_bot_combat.hpp"
namespace native_battle {void log(const std::string&){}std::string describe(const std::exception&e){return e.what();}}
static PyObject* inspect(PyObject*,PyObject*descriptor){try {
    auto factors=native_battle::params::botCrewFactors(descriptor,1);
    if(factors->roster!=std::vector<std::string>({"commander","loader1"})||factors->rows.size()!=8)throw std::runtime_error("native fitting roster mismatch");
    if(factors->rows[1].damaged_gun_factor!=1.)throw std::runtime_error("native fire projection mismatch");
    const auto&base=factors->rows[0];const auto&knocked=factors->rows[4];
    if(base.adrenaline_reload_factor!=.8||base.engine_fire_factor!=.6||base.damaged_gun_factor!=.9||base.ammo_bay_health!=1.3)throw std::runtime_error("native skill producer mismatch");
    if(knocked.adrenaline_reload_factor!=1.||knocked.engine_fire_factor!=1.||knocked.ratios[0]<=1.||knocked.repair>=base.repair)throw std::runtime_error("native casualty projection mismatch");
    wot::offline_bot_step::CriticalState state;state.fitting=factors;state.health=5;state.maximum_health=100;
    using namespace wot::offline_bot_step;
    if(std::abs(critical_factor(state,CriticalStat::reload)-.8)>1.e-9)throw std::runtime_error("native adrenaline projection mismatch");
    state.crew_ko={"loader1"};if(std::abs(critical_factor(state,CriticalStat::reload)-knocked.ratios[0])>1.e-9)throw std::runtime_error("native crew lookup mismatch");
    state.crew_ko.clear();state.devices["gunHealth"].critical=true;state.devices["gunHealth"].hp=10.;
    if(std::abs(critical_factor(state,CriticalStat::dispersion)-1.8)>1.e-9)throw std::runtime_error("native damaged gun projection mismatch");
    if(base.base_moving!=.25||base.base_still!=.5||base.invisibility_still[0]!=.2)throw std::runtime_error("native seasonal camouflage projection mismatch");
    Py_RETURN_TRUE;
}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_AssertionError,e.what());return nullptr;}}
static PyMethodDef methods[]={{"inspect",inspect,METH_O,nullptr},{nullptr,nullptr,0,nullptr}};
extern "C" void initfitting_test(){Py_InitModule("fitting_test",methods);}
