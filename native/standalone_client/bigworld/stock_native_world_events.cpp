#include "stock_native_world_events.hpp"
#include "stock_native_bot_python.hpp"
#include <algorithm>
#include <cmath>

namespace stock_native_world {
namespace {
namespace p=stock_native_bot_python;
const char* kind(Kind value){switch(value){case Kind::Tree:return "tree";case Kind::Falling:return "column";case Kind::Fragile:return "fragile";case Kind::Structure:return "module";default:throw p::Error("Native destructible kind is unsupported");}}
MaterialKey identity(const Destruction&e){return {e.key.first,e.key.second,e.kind==Kind::Structure?e.material:-1};}
p::Ref key(const Key&k){return p::Ref(Py_BuildValue("(II)",unsigned(k.first),unsigned(k.second)));}
p::Ref key(const Destruction&e){auto material=e.kind==Kind::Structure?p::integer(e.material):p::retain(Py_None);return p::Ref(Py_BuildValue("(IIN)",unsigned(e.key.first),unsigned(e.key.second),material.release()));}
void add(PyObject*set,const p::Ref&value){if(!PySet_Check(set)||PySet_Add(set,value.get())<0)throw p::Error("Native destructible mirror set is invalid");}
void discard(PyObject*set,PyObject*value){if(set&&PySet_Check(set)&&PySet_Discard(set,value)<0)throw p::Error("Native destructible mirror removal failed");}
void remove(PyObject*dict,const char*name){if(PyDict_GetItemString(dict,name)&&PyDict_DelItemString(dict,name)<0)throw p::Error("Native destructible mirror reset failed");}
void remove(PyObject*dict,PyObject*key){if(PyDict_Check(dict)&&PyDict_GetItem(dict,key)&&PyDict_DelItem(dict,key)<0)throw p::Error("Native destructible mirror item reset failed");}
p::Ref dictionary(PyObject*dict,const char*name){auto*raw=PyDict_GetItemString(dict,name);if(raw){if(!PyDict_Check(raw))throw p::Error("Native destructible mirror mapping is invalid");return p::retain(raw);}auto out=p::dict();p::set(dict,name,out);return out;}
p::Ref set(PyObject*dict,const char*name){auto*raw=PyDict_GetItemString(dict,name);if(raw){if(!PySet_Check(raw))throw p::Error("Native destructible mirror collection is invalid");return p::retain(raw);}p::Ref out(PySet_New(nullptr));p::set(dict,name,out);return out;}
p::Ref objectDict(PyObject*object){if(PyDict_Check(object))return p::retain(object);auto out=p::value(object,"__dict__");if(!PyDict_Check(out.get()))throw p::Error("Native destructible mirror has no raw dictionary");return out;}
p::Ref module(const char*name){p::Ref object(PyImport_ImportModule(name));if(!PyModule_Check(object.get()))throw p::Error("Native destructible module is invalid");return p::retain(PyModule_GetDict(object.get()));}
p::Ref event(const Destruction&e){auto out=p::dict();p::set(out.get(),"destructible_kind",p::text(kind(e.kind)));p::set(out.get(),"chunk_id",p::integer(e.key.first));p::set(out.get(),"item_index",p::integer(e.key.second));p::set(out.get(),"x",p::number(e.position[0]));p::set(out.get(),"y",p::number(e.position[1]));p::set(out.get(),"z",p::number(e.position[2]));p::set(out.get(),"fall_yaw",p::number(e.yaw));p::set(out.get(),"speed",p::number(e.speed));p::set(out.get(),"is_shot",p::boolean(e.shot));if(e.kind==Kind::Structure)p::set(out.get(),"mat_kind",p::integer(e.material));return out;}
Destruction parse(PyObject*raw,double now){
    if(!PyDict_Check(raw)||!std::isfinite(now))throw p::Error("Canonical destructible event is invalid");Destruction out;out.now=now;
    auto type=p::value(raw,"destructible_kind");const auto name=p::string(type.get());if(name=="tree")out.kind=Kind::Tree;else if(name=="column")out.kind=Kind::Falling;else if(name=="fragile")out.kind=Kind::Fragile;else if(name=="module")out.kind=Kind::Structure;else throw p::Error("Canonical destructible kind is invalid");
    auto chunk=p::value(raw,"chunk_id"),item=p::value(raw,"item_index"),shot=p::value(raw,"is_shot");const auto c=p::wide(chunk.get()),i=p::wide(item.get());if(c<0||std::uint64_t(c)>0xffffffffULL||i<0||i>0xffff||!PyBool_Check(shot.get()))throw p::Error("Canonical destructible identity is invalid");out.key={std::uint32_t(c),std::uint32_t(i)};out.shot=shot.get()==Py_True;
    auto x=p::value(raw,"x"),y=p::value(raw,"y"),z=p::value(raw,"z");out.position={{p::scalar(x.get()),p::scalar(y.get()),p::scalar(z.get())}};out.yaw=p::numeric(raw,"fall_yaw");out.speed=p::numeric(raw,"speed");
    if(out.kind==Kind::Structure){auto material=p::value(raw,"mat_kind");out.material=p::wide(material.get());if(out.material<73||out.material>=86)throw p::Error("Canonical destructible module is invalid");}return out;
}
bool equal(const std::vector<Box>&a,const std::vector<Box>&b){if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i)if(a[i].center!=b[i].center||a[i].axes!=b[i].axes||a[i].material!=b[i].material)return false;return true;}
}
WorldEvents::WorldEvents(Registry&r,stock_native_destructible::Runtime&native,Scene&scene,std::uint32_t space):registry_(r),native_(native),scene_(scene),space_(space),epoch_(r.epoch){}
WorldEvents::~WorldEvents(){if(Py_IsInitialized()){Py_XDECREF(battle_);Py_XDECREF(battle_dict_);Py_XDECREF(sensor_);Py_XDECREF(authority_);Py_XDECREF(area_);Py_XDECREF(geometry_);}}
bool WorldEvents::install(PyObject*battle){
    try {
        if(battle_)throw p::Error("Native world event owner is already installed");auto bd=objectDict(battle),sensor=module("gui.mods.offline_lan_0922.destructibles_sensor"),authority=module("gui.mods.offline_lan_0922.destructibles_authority"),area=module("AreaDestructibles"),geometry=module("gui.mods.offline_lan_0922.native_destructibles");
        // Never leave an old per-body Python animator running beside native
        // authority. The bootstrap owner must be created before its first fall.
        auto animator=p::value(area.get(),"g_destructiblesAnimator");if(animator.get()!=Py_None){auto bodies=p::value(animator.get(),"_DestructiblesAnimator__bodies");if(bodies.get()!=Py_None)for(Py_ssize_t i=0;i<p::size(bodies.get());++i){auto body=p::item(bodies.get(),i);if(PyDict_Check(body.get())&&p::integral(body.get(),"spaceID",-1)==space_)throw p::Error("Native world installation encountered active stock destructible bodies; install before legacy authority advances");}}
        auto state=dictionary(authority.get(),"_state"),sid=p::value(state.get(),"spaceID");const bool other=sid.get()!=Py_None&&p::wide(sid.get())!=space_;
        if(other){p::set(state.get(),"chunks",p::dict());p::set(state.get(),"entities",p::dict());registry_.skin.destroyed.clear();registry_.skin.speculative.clear();registry_.pending.clear();}
        p::set(state.get(),"spaceID",p::integer(space_));dictionary(state.get(),"chunks");dictionary(state.get(),"entities");
        auto sensor_space=p::value(sensor.get(),"g_offh_destr_runtime_space");if(sensor_space.get()!=Py_None&&p::wide(sensor_space.get())!=space_){
            const char*names[]={"g_offh_destr_seen","g_offh_destr_nodesc","g_offh_tree_state","g_offh_destr_ordered","g_offh_destr_chunks","g_offh_destr_instances","g_offh_destr_contact_bins","g_offh_destr_pending","g_offh_destr_speculative","g_offh_destr_native_replacement_bsp_active","g_offh_destr_catalog_published","g_offh_destr_catalog_publish_pending","g_offh_destr_falling_active","g_offh_destr_ground_skips","g_offh_destr_unresolved_obstacles","g_offh_destr_unresolved_logs","g_offh_destr_broken_cache","g_offh_destr_item_names","g_offh_destr_proved_layouts","g_offh_destr_catalog_tree_names","g_offh_destr_catalog_model_names","g_offh_destr_isolated_name_types","g_offh_destr_native_name_lists","g_offh_destr_item_name_budget","g_offh_destr_contact_name_budget","g_offh_destr_native_fragile_replacements","g_offh_destr_item_name_cache_serial","g_offh_destr_isolated_chunks","g_offh_destr_isolated_slots","g_offh_destr_name_unresolved_slots","g_offh_destr_isolation_logs","g_offh_destr_isolation_log_capped","g_offh_destr_diagnostics","g_offh_destr_diag_last_static","g_offh_destr_contact_diagnostic_times","g_offh_destr_spatial_revision","g_offh_destr_spatial_published_revision","g_offh_destr_spatial_global_generation","g_offh_destr_spatial_cell_generations","g_offh_destr_spatial_chunk_generations","g_offh_destr_empty_contact_receipts","g_offh_destr_empty_proximity_receipts","g_offh_destr_receipt_stats","g_offh_destr_spatial_mutation_batch"};for(const auto*name:names)remove(sensor.get(),name);
            auto old=p::value(geometry.get(),"_owner");if(old.get()!=Py_None){auto raw=objectDict(old.get()),handle=p::value(raw.get(),"handle");if(handle.get()!=Py_None)offline_destructible::close(p::wide(handle.get()));p::set(raw.get(),"handle",p::retain(Py_None));}p::set(geometry.get(),"_owner",p::retain(Py_None));p::set(geometry.get(),"_disabled",p::boolean(false));
        }
        p::set(sensor.get(),"g_offh_destr_runtime_space",p::integer(space_));
        auto tree=dictionary(sensor.get(),"g_offh_tree_state");p::set(tree.get(),"spaceID",p::integer(space_));dictionary(tree.get(),"chunks");auto committed=set(tree.get(),"native_committed");p::set(tree.get(),"felled",committed);set(tree.get(),"canonical_published");dictionary(tree.get(),"publish_pending");
        battle_=p::retain(battle).release();battle_dict_=bd.release();sensor_=sensor.release();authority_=authority.release();area_=area.release();geometry_=geometry.release();return true;
    }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}
void WorldEvents::published(const Destruction&e){
    if(e.kind==Kind::Tree){trusted_published_.insert(e.key);auto tree=dictionary(sensor_,"g_offh_tree_state"),values=set(tree.get(),"canonical_published");add(values.get(),key(e.key));auto pending=dictionary(tree.get(),"publish_pending");remove(pending.get(),key(e.key).get());}
    else{auto values=set(sensor_,"g_offh_destr_catalog_published");add(values.get(),key(e));auto pending=dictionary(sensor_,"g_offh_destr_catalog_publish_pending");remove(pending.get(),key(e).get());}
}
void WorldEvents::mirror(const Destruction&e,bool logical,bool canonical){
    auto state=dictionary(authority_,"_state"),chunks=dictionary(state.get(),"chunks"),chunkkey=p::integer(e.key.first);auto*existing=PyDict_GetItem(chunks.get(),chunkkey.get());auto chunk=existing?p::retain(existing):p::dict();
    if(!PyDict_Check(chunk.get()))throw p::Error("Native authority chunk mirror is invalid");if(!existing&&PyDict_SetItem(chunks.get(),chunkkey.get(),chunk.get())<0)throw p::Error("Native authority chunk allocation failed");
    for(const char*name:{"fallenTrees","fallenColumns","destroyedFragiles","destroyedModules"})if(!PyDict_GetItemString(chunk.get(),name))p::set(chunk.get(),name,p::list());
    auto keys=set(chunk.get(),"keys"),presentations=dictionary(chunk.get(),"treePresentations");auto material=e.kind==Kind::Structure?p::integer(e.material):p::retain(Py_None);p::Ref dedup(Py_BuildValue("(IN)",unsigned(e.key.second),material.release()));add(keys.get(),dedup);
    // Stock replay collections belong to its entity owner. Writing native
    // orders there would play them a second time on a later legacy controller
    // creation. Keep the encoded native ledger separate; Runtime rebinds it.
    auto records=dictionary(chunk.get(),"native_records");const auto encoded=native_.acceptedData(e);auto data=encoded?p::integer(std::int64_t(*encoded)):p::retain(Py_None);if(PyDict_SetItem(records.get(),dedup.get(),data.get())<0)throw p::Error("Native authority record allocation failed");
    if((e.kind==Kind::Fragile||e.kind==Kind::Structure)&&!e.shot&&!logical&&native_.applied(e)){auto ready=set(chunk.get(),"contactCollisionReady");add(ready.get(),dedup);}
    auto speculative=p::value(sensor_,"g_offh_destr_speculative");discard(speculative.get(),key(e).get());registry_.skin.speculative.erase(identity(e));
    if(e.kind==Kind::Tree){
        auto tree=dictionary(sensor_,"g_offh_tree_state"),committed=set(tree.get(),"native_committed");add(committed.get(),key(e.key));p::set(tree.get(),"felled",committed);activated_trees_.insert(e.key);
        if(!logical&&encoded){auto entry=p::dict();p::set(entry.get(),"data",data);p::set(entry.get(),"fallPitch",p::number((double(*encoded&0xffffu)/65536.)*(2.*3.14159265358979323846)-3.14159265358979323846));p::set(entry.get(),"pos",p::tupleNumbers(e.position));p::set(entry.get(),"initialMatrixSignature",p::retain(Py_None));p::set(entry.get(),"status",p::text("presented"));auto item=p::integer(e.key.second);if(PyDict_SetItem(presentations.get(),item.get(),entry.get())<0)throw p::Error("Native tree presentation mirror failed");}
    }else if(e.kind==Kind::Falling){auto active=dictionary(sensor_,"g_offh_destr_falling_active");remove(active.get(),key(e.key).get());}
    else {const auto row=registry_.instances.find(e.key);if(e.kind==Kind::Structure||row==registry_.instances.end()||row->second.retained_collision)p::set(sensor_,"g_offh_destr_native_replacement_bsp_active",p::boolean(true));if(e.kind==Kind::Fragile){auto replacements=set(sensor_,"g_offh_destr_native_fragile_replacements");p::Ref value(Py_BuildValue("(III)",unsigned(space_),unsigned(e.key.first),unsigned(e.key.second)));add(replacements.get(),value);}}
    if(canonical)published(e);
}
void WorldEvents::mirrorFallings(){
    auto instances=dictionary(sensor_,"g_offh_destr_instances"),bins=dictionary(sensor_,"g_offh_destr_contact_bins");std::set<offline_destructible::Cell> changed;std::set<std::uint32_t> chunks;
    for(const auto&e:native_.acceptedOrders())if(e.kind==Kind::Falling){
        const auto native=registry_.instances.find(e.key);if(native==registry_.instances.end()||registry_.isolated(e.key)||native->second.boxes.empty())continue;const auto&boxes=native->second.boxes;const auto old=falling_mirrored_.find(e.key);if(old!=falling_mirrored_.end()&&equal(old->second,boxes))continue;
        auto id=key(e.key);auto*raw=PyDict_GetItem(instances.get(),id.get());if(!raw||!PyDict_Check(raw))continue;std::set<offline_destructible::Cell> cells;
        for(const auto&box:boxes){double x=0.,z=0.;for(const auto&a:box.axes){x+=std::abs(a[0]);z+=std::abs(a[2]);}for(int ix=int(std::floor((box.center[0]-x)/8.));ix<=int(std::floor((box.center[0]+x)/8.));++ix)for(int iz=int(std::floor((box.center[2]-z)/8.));iz<=int(std::floor((box.center[2]+z)/8.));++iz)cells.insert({ix,iz});}
        Py_ssize_t cursor=0;PyObject*cellkey,*members;std::vector<p::Ref> empty;
        while(PyDict_Next(bins.get(),&cursor,&cellkey,&members))if(PySet_Check(members)&&PySet_Contains(members,id.get())==1){auto parts=p::numbers<2>(cellkey);changed.insert({int(parts[0]),int(parts[1])});discard(members,id.get());if(PySet_Size(members)==0)empty.push_back(p::retain(cellkey));}
        for(const auto&c:empty)remove(bins.get(),c.get());p::Ref bin_keys(PyTuple_New(Py_ssize_t(cells.size())));Py_ssize_t n=0;
        for(const auto&c:cells){p::Ref k(Py_BuildValue("(ii)",c.first,c.second));auto*existing=PyDict_GetItem(bins.get(),k.get());auto values=existing?p::retain(existing):p::Ref(PySet_New(nullptr));add(values.get(),id);if(!existing&&PyDict_SetItem(bins.get(),k.get(),values.get())<0)throw p::Error("Native falling bin mirror failed");PyTuple_SET_ITEM(bin_keys.get(),n++,p::retain(k.get()).release());changed.insert(c);}
        p::Ref rows(PyTuple_New(Py_ssize_t(boxes.size())));n=0;for(const auto&box:boxes){p::Ref axes(PyTuple_New(Py_ssize_t(box.axes.size())));for(std::size_t i=0;i<box.axes.size();++i)PyTuple_SET_ITEM(axes.get(),Py_ssize_t(i),p::tupleNumbers(box.axes[i]).release());auto mat=box.material<0?p::retain(Py_None):p::integer(box.material);PyTuple_SET_ITEM(rows.get(),n++,Py_BuildValue("(NNN)",p::tupleNumbers(box.center).release(),axes.release(),mat.release()));}p::set(raw,"boxes",rows);p::set(raw,"bin_keys",bin_keys);chunks.insert(e.key.first);falling_mirrored_[e.key]=boxes;
        auto owner=p::value(geometry_,"_owner");if(owner.get()!=Py_None){auto handle=p::value(owner.get(),"handle");if(handle.get()!=Py_None){auto store=offline_destructible::find(p::wide(handle.get()));if(store){offline_destructible::Instance value;value.key={e.key.first,int(e.key.second)};value.kind=2;value.boxes=boxes;value.bins=cells;store->put(std::move(value));}}}
    }
    if(changed.empty()&&chunks.empty())return;const auto revision=p::integral(sensor_,"g_offh_destr_spatial_revision");if(p::integral(sensor_,"g_offh_destr_spatial_published_revision")!=revision)p::set(sensor_,"g_offh_destr_spatial_global_generation",p::integer(p::integral(sensor_,"g_offh_destr_spatial_global_generation")+1));
    auto generations=dictionary(sensor_,"g_offh_destr_spatial_cell_generations");for(const auto&c:changed){p::Ref k(Py_BuildValue("(ii)",c.first,c.second));auto*old=PyDict_GetItem(generations.get(),k.get());auto next=p::integer((old?p::wide(old):0)+1);if(PyDict_SetItem(generations.get(),k.get(),next.get())<0)throw p::Error("Native falling cell generation failed");}
    auto cg=dictionary(sensor_,"g_offh_destr_spatial_chunk_generations");for(auto c:chunks){auto k=p::integer(c);auto*old=PyDict_GetItem(cg.get(),k.get());auto next=p::integer((old?p::wide(old):0)+1);if(PyDict_SetItem(cg.get(),k.get(),next.get())<0)throw p::Error("Native falling chunk generation failed");}p::set(sensor_,"g_offh_destr_spatial_revision",p::integer(revision+1));p::set(sensor_,"g_offh_destr_spatial_published_revision",p::integer(revision+1));
}
PyObject* WorldEvents::pack(PyObject*battle){
    try{if(battle!=battle_||epoch_!=registry_.epoch)throw p::Error("Native world publication owner is stale");std::vector<Event> events=registry_.journal;for(const auto&row:trusted_)events.push_back(row.second);std::sort(events.begin(),events.end(),[](const Event&a,const Event&b){return a.sequence<b.sequence;});auto out=p::list();for(const auto&e:events){const bool trusted=trusted_.count(e.destruction.key)&&trusted_.at(e.destruction.key).sequence==e.sequence;if(!trusted&&!mirrored_.count(e.sequence)){mirror(e.destruction,false,false);mirrored_.insert(e.sequence);}p::Ref row(Py_BuildValue("(LN)",static_cast<PY_LONG_LONG>(e.sequence),event(e.destruction).release()));p::append(out.get(),row);packed_=std::max(packed_,e.sequence);}mirrorFallings();return out.release();}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}
}
bool WorldEvents::ack(std::uint64_t sequence){
    try{if(!battle_||epoch_!=registry_.epoch||sequence>packed_)return false;for(const auto&e:registry_.journal)if(e.sequence<=sequence)published(e.destruction);for(auto i=trusted_.begin();i!=trusted_.end();)if(i->second.sequence<=sequence){published(i->second.destruction);trusted_published_.insert(i->first);i=trusted_.erase(i);}else ++i;registry_.acknowledge(sequence);return true;}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}
bool WorldEvents::ingress(PyObject*raw,double now){
    try{if(!battle_||epoch_!=registry_.epoch)throw p::Error("Native canonical destructible owner is stale");const auto e=parse(raw,now);const auto id=identity(e);if(registry_.isolated(e.key))return false;if(registry_.skin.destroyed.count(id)||registry_.skin.destroyed.count(MaterialKey{e.key.first,e.key.second,-1})){mirror(e,!native_.acceptedData(e).has_value(),true);return true;}
        const auto live=registry_.instances.find(e.key),baked=registry_.baked_instances.find(e.key);const Instance*proof=live!=registry_.instances.end()?&live->second:baked!=registry_.baked_instances.end()?&baked->second:nullptr;if(proof&&proof->kind!=e.kind)return false;if(!proof&&e.kind!=Kind::Tree)return false;
        const auto admitted=scene_.refresh(e.key,registry_);if(admitted==Admission::Isolated)return false;const bool logical=e.kind==Kind::Tree&&admitted!=Admission::Admitted;
        if(!logical&&!native_.destroy(e))return false;registry_.skin.destroyed.insert(id);registry_.pending[id]=now;++registry_.generation;if(e.kind==Kind::Tree)registry_.skin.tree_instances.insert(e.key);if(!logical&&admitted!=Admission::Admitted)replay_pending_[id]=e;mirror(e,logical,true);return true;
    }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}
bool WorldEvents::mirrorOnly(const Destruction& event){
    try{if(!battle_||epoch_!=registry_.epoch)throw p::Error("Native projectile mirror owner is stale");mirror(event,false,false);return true;}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}
bool WorldEvents::mappingChanged(double now){
    try{if(epoch_!=registry_.epoch)return false;const auto revision=scene_.streamRevision();if(revision==stream_revision_)return true;for(const auto&e:native_.acceptedOrders())scene_.refresh(e.key,registry_);native_.mappingChanged(now);stream_revision_=revision;for(auto i=replay_pending_.begin();i!=replay_pending_.end();)if(native_.applied(i->second)){mirror(i->second,false,true);i=replay_pending_.erase(i);}else ++i;return true;}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}
TrustedTreeStatus WorldEvents::trustedTreeStatus(const Key&key){if(trusted_.count(key)||trusted_published_.count(key))return TrustedTreeStatus::Tree;for(const auto&e:registry_.journal)if(e.destruction.kind==Kind::Tree&&e.destruction.key==key)return TrustedTreeStatus::Tree;return scene_.trustedTree(key);}
ContactResult WorldEvents::commitTrustedTrees(const std::set<MaterialKey>&token,const Point&start,const Point&end,double end_yaw,double speed,double now){
    ContactResult out;bool pending=false,hard=false;const Point a{{float(start[0]),float(start[1]),float(start[2])}},b{{float(end[0]),float(end[1]),float(end[2])}};const double dx=b[0]-a[0],dz=b[2]-a[2],yaw=dx*dx+dz*dz>1.e-16?std::atan2(dx,dz):end_yaw+(speed<0.?3.14159265358979323846:0.);
    if(token.empty()||token.size()>64||!std::isfinite(speed)||!std::isfinite(now)||!std::isfinite(yaw)){out.status=ContactStatus::Hard;return out;}
    for(const auto&id:token){if(std::get<2>(id)!=-1){hard=true;continue;}const Key k{std::get<0>(id),std::get<1>(id)};if(trusted_published_.count(k))continue;bool frozen=false;for(const auto&e:registry_.journal)if(e.destruction.kind==Kind::Tree&&e.destruction.key==k)frozen=true;if(frozen){pending=true;continue;}if(!trusted_.count(k)){const auto status=trustedTreeStatus(k);if(status==TrustedTreeStatus::Pending){pending=true;continue;}if(status!=TrustedTreeStatus::Tree){hard=true;continue;}Destruction e;e.kind=Kind::Tree;e.key=k;e.position=b;e.yaw=yaw;e.speed=speed;e.now=now;scene_.refresh(k,registry_);const auto value=registry_.instances.find(k);if(value!=registry_.instances.end())for(unsigned i=0;i<3;++i)e.position[i]=float(value->second.origin[i]);trusted_[k]={registry_.next_sequence++,e};}pending=true;}
    out.status=hard?ContactStatus::Hard:pending?ContactStatus::Pending:ContactStatus::Crushed;out.pending=pending;out.contacts=token;return out;
}
}
