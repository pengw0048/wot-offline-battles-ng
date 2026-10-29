#include "stock_native_bot_publication_python.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_combat_snapshot.hpp"
#include <algorithm>
#include <vector>

namespace stock_native_bot {
namespace p=stock_native_bot_python;
PyObject* packNativeActor(const Actor&a,const offline_simulation::weapons::ActorState&w,
    const contacts::Actor* contact,PyObject* previous,double equipment_time){try{
    p::Ref out(step::combat_snapshot_value(a.combat,a.siege,w,{}));auto*d=out.get();const auto&m=a.motion;
    p::set(d,"id",p::integer(m.id));p::set(d,"team",p::integer(a.team));p::set(d,"slot",p::integer(a.slot));
    const char*names[]={"x","y","z","yaw","pitch","roll","speed","aim_yaw","push_x","push_z","push_yaw","vertical_speed",
        "air_lateral_x","air_lateral_z","suspension_pitch_velocity","suspension_roll_velocity","terrain_pitch","suspension_pitch",
        "slide_speed","last_drive_pitch","_contact_forward_speed","_direction_command","_native_rotation_refused_until",
        "_support_repair_elapsed","_suspension_support_vertical_speed","_stun_until_equipment_time"};
    const double values[]={m.position[0],m.position[1],m.position[2],m.yaw,m.pitch,m.roll,m.speed,a.weapon.aim_yaw,m.push_x,m.push_z,m.push_yaw,m.vertical_speed,
        m.air_lateral_x,m.air_lateral_z,m.pitch_speed,m.roll_speed,m.terrain_pitch,m.hydraulic_pitch,m.slide_speed,m.last_drive_pitch,
        m.contact_forward,m.direction_command,m.rotation_refused_until,m.support_repair_elapsed,m.support_vertical_speed,a.combat.stun_until_equipment_time};
    for(unsigned i=0;i<sizeof(values)/sizeof(*values);++i)p::set(d,names[i],p::number(values[i]));
    p::set(d,"movement_dir",p::integer(m.movement_direction));p::set(d,"rotation_dir",p::integer(m.rotation_direction));
    p::set(d,"airborne",p::boolean(m.airborne));p::set(d,"grounded_once",p::boolean(m.grounded_once));
    p::set(d,"left_flying",p::boolean(m.left_flying));p::set(d,"right_flying",p::boolean(m.right_flying));
    p::set(d,"hull_aiming",p::boolean(a.weapon.hull_aiming));p::set(d,"_contact_dynamics",p::boolean(m.contact_dynamics));
    p::set(d,"_rotation_contact_blocked",p::boolean(m.rotation_contact_blocked));p::set(d,"_basic_continuous_support",p::boolean(m.basic_continuous_support));
    p::set(d,"world_pose",p::boolean(a.world_pose));p::set(d,"service_brake",p::boolean(m.service_brake));p::set(d,"max_health",p::integer(a.combat.maximum_health));
    p::set(d,"combat_revision",p::integer(a.ledger.revision));p::set(d,"combat_base_revision",p::integer(a.ledger.base_revision));
    p::set(d,"combat_ack_seq",p::integer(a.ledger.acknowledged_sequence));p::set(d,"combat_seq",p::integer(a.ledger.next_sequence));
    if(a.selected_target){p::set(d,"target_kind",p::text(a.selected_target->kind));p::set(d,"target_id",p::integer(a.selected_target->id));}
    else {p::set(d,"target_kind",p::retain(Py_None));p::set(d,"target_id",p::retain(Py_None));}
    std::vector<const char*> remove;
    const auto optionalNumber=[&](const char* key,const std::optional<double>& v){if(v)p::set(d,key,p::number(*v));else remove.push_back(key);};
    optionalNumber("_basic_fall_origin_y",m.basic_fall_origin);optionalNumber("_legacy_straddle_reference_y",m.straddle_reference);
    optionalNumber("_basic_slope_basis_yaw",m.slope_basis_yaw);optionalNumber("ground_height",m.ground_height);
    optionalNumber("destructible_contact_speed",m.destructible_contact_speed);
    if(m.contact_motor_turn)p::set(d,"_contact_motor_turn",p::integer(*m.contact_motor_turn));else remove.push_back("_contact_motor_turn");
    if(m.slope_target)p::set(d,"_slope_pose_target",p::tupleNumbers(*m.slope_target));else remove.push_back("_slope_pose_target");
    if(m.support_gradient)p::set(d,"_suspension_support_gradient",p::tupleNumbers(*m.support_gradient));else remove.push_back("_suspension_support_gradient");
    if(m.ground_plane){const auto& plane=*m.ground_plane;auto row=p::dict();p::set(row.get(),"gradient_x",p::number(plane.gradient_x));p::set(row.get(),"gradient_z",p::number(plane.gradient_z));
        p::set(row.get(),"center_x",p::number(plane.center_x));p::set(row.get(),"center_y",p::number(plane.center_y));p::set(row.get(),"center_z",p::number(plane.center_z));
        p::set(row.get(),"normal",p::tupleNumbers(plane.normal));p::set(d,"_suspension_ground_plane",row);}else remove.push_back("_suspension_ground_plane");
    if(a.shot_angles){p::set(d,"shot_yaw",p::number((*a.shot_angles)[0]));p::set(d,"shot_pitch",p::number((*a.shot_angles)[1]));}
    else {remove.push_back("shot_yaw");remove.push_back("shot_pitch");}
    if(contact){auto acknowledgements=p::list();for(const auto& item:contact->push_acks){auto row=p::list();p::append(row.get(),p::integer(item.first));
        p::append(row.get(),p::integer(item.second.sequence));for(double v:item.second.totals)p::append(row.get(),p::number(v));p::append(acknowledgements.get(),row);}
        p::set(d,"contact_push_acks",acknowledgements);auto motions=p::list();
        for(const auto& item:contact->ram_motion){const auto& v=item.second;auto row=p::list();p::append(row.get(),p::integer(v.player));
            p::append(row.get(),p::integer(v.sequence));p::append(row.get(),p::integer(v.sample_us));
            for(const auto& pnt:{v.velocity,v.position,v.angles,v.player_position,v.player_angles,v.player_velocity})for(double x:pnt)p::append(row.get(),p::number(x));
            p::append(motions.get(),row);}p::set(d,"ram_motion",motions);p::set(d,"_ram_motion_seq",p::integer(contact->ram_motion_sequence));}
    if(previous&&PyDict_Check(previous)){
        auto existing=PyDict_GetItemString(previous,"equipment_states");
        if(existing && (PyList_Check(existing)||PyTuple_Check(existing)) && p::size(existing)==Py_ssize_t(a.equipment.size())){
            auto rows=p::list();for(std::size_t i=0;i<a.equipment.size();++i){auto old=p::item(existing,Py_ssize_t(i));
                if(!PyDict_Check(old.get()))throw p::Error("Native equipment mirror shape invalid");
                p::Ref row(PyDict_Copy(old.get()));const auto& e=a.equipment[i];p::set(row.get(),"usesLeft",p::integer(e.uses_left));
                p::set(row.get(),"cooldownTimeLeft",p::number(std::max(0.,e.ready_at-equipment_time)));p::set(row.get(),"active",p::boolean(e.active));
                p::set(row.get(),"autoPendingElapsed",e.auto_pending_since?p::number(std::max(0.,equipment_time-*e.auto_pending_since)):p::retain(Py_None));
                p::set(row.get(),"aiPendingElapsed",e.ai_pending_since?p::number(std::max(0.,equipment_time-*e.ai_pending_since)):p::retain(Py_None));
                p::append(rows.get(),row);}p::set(d,"equipment_states",rows);
        }else if(a.equipment.empty())p::set(d,"equipment_states",p::list());
        for(const char* key:remove)if(PyDict_DelItemString(previous,key)<0)PyErr_Clear();
    }
    return out.release();
}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
}
