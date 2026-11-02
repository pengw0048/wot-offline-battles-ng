"""Applied Human pose stays with its replica cursor, observation and gun owner."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags
from test_stock_vehicle_physics_owner import block

ROOT = Path(__file__).resolve().parents[1]
FORK = os.environ.get('WOT_FORK_0101_SOURCE')

REPLICA = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
using Pose=std::array<double,8>;
using State=std::optional<Pose>;
namespace cs {State hydraulicBody(const State&s){return s;}}
struct Sample {double time=0;std::optional<std::int64_t>revision;Pose value{};State state;};
using Bracket=std::optional<std::array<std::int64_t,4>>;
// Protocol parsing is a separate tested boundary. This passive fixture only
// supplies the admitted state consumed by the production interpolation body.
struct Actor {
 bool dead=false,settled=false,timed=false,teleport=false,resume=false;
 bool has_resume_sample=false,has_observed=false,warmup_active=false,has_ideal=false,has_delay=false;
 unsigned warmup_intervals=0;
 Pose current{},target{},velocity{};State hydraulic_body,target_hydraulic_body,state;
 std::vector<Sample>samples;std::string kind="player";
 double sample_time=0,render_delta=0,anchor_time=0,anchor_local=0,delay=0,ideal=0,observed=0,resume_sample=0,target_time=0;
 std::optional<double>cursor;std::optional<std::int64_t>sample_revision;
 std::uint64_t probe_frames=0,probe_held=0,probe_moving_held=0;double probe_max_gap_ms=0;
};
double wrap(double n){const double pi=std::acos(-1.);while(n>pi)n-=2*pi;while(n<-pi)n+=2*pi;return n;}
double clamp(double n,double lo,double hi){return std::max(lo,std::min(hi,n));}
@BLEND@
@BODY@
@BRACKET@
@INTERPOLATE@
int main(){
 Pose first{0.,1.,2.,3.1,.02,.03,3.1,.1},last{10.,3.,4.,-3.1,.04,.05,-3.1,.2};
 Pose bodyA{.5,1.2,2.4,3.1,.2,.3,3.1,-.1},bodyB{11.,4.,5.,-3.1,.6,.7,-3.1,.3};
 Actor a;a.timed=true;a.current=first;a.target=last;a.samples={{100.,1,first,bodyA},{200.,2,last,bodyB}};
 a.anchor_time=150.;a.cursor=150.;a.target_hydraulic_body=bodyB;a.sample_time=200.;a.sample_revision=2;
 Bracket bracket;bool snap=false;
 assert(interpolate(a,0.,0.,.2,bracket,snap));
 assert(!snap&&a.current==blend(first,last,.5));
 assert(a.hydraulic_body==blend(bodyA,bodyB,.5));
 assert(bracket&&(*bracket)[0]==1&&(*bracket)[2]==2);
 // Missing endpoints cannot borrow the latest target body, even while the
 // baseline interpolation is perfectly valid. The exact next endpoint recovers.
 a.samples[0].state.reset();a.cursor=150.;a.anchor_time=150.;a.delay=0.;
 interpolate(a,0.,0.,.2,bracket,snap);assert(!a.hydraulic_body);
 a.cursor=200.;a.anchor_time=200.;a.delay=0.;
 interpolate(a,0.,0.,.2,bracket,snap);assert(a.hydraulic_body==bodyB&&a.current==blend(first,last,1.));
 // An untimed first receipt snaps the complete unpredicted sample; following
 // frames predict and blend both positions by the same amount.
 Actor u;u.target=last;u.target_hydraulic_body=bodyB;u.velocity[0]=2.;
 interpolate(u,.05,.02,.5,bracket,snap);assert(snap&&u.current==last&&u.hydraulic_body==bodyB);
 interpolate(u,.05,.02,.5,bracket,snap);assert(!snap);
 assert(std::abs(u.current[0]-10.05)<1.e-12&&std::abs((*u.hydraulic_body)[0]-11.05)<1.e-12);
 u.target_hydraulic_body.reset();interpolate(u,.05,.02,.5,bracket,snap);assert(!u.hydraulic_body);
 u.target_hydraulic_body=bodyA;interpolate(u,.05,.02,.5,bracket,snap);assert(snap&&u.hydraulic_body==bodyA);
 // Ordinary vehicles and Bots remain baseline-only.
 Actor ordinary;ordinary.target=last;interpolate(ordinary,0.,0.,.5,bracket,snap);assert(!ordinary.hydraulic_body&&ordinary.current==blend(Pose{},last,.5));
}
'''

AIM = r'''
#include "stock_native_bot_observation_runtime.hpp"
#include <cassert>
#include <cmath>
#include <stdexcept>
using namespace wot::offline_bot_step;
namespace perception=stock_native_bot_perception;
using Actor=stock_native_bot_observation::Actor;
namespace wot::offline_bot_step {
@POINT@
}
@FROZEN@
@OVERLAY@
int main(){
 AimPose baseline;baseline.position={1.,2.,3.};baseline.yaw=.12;baseline.pitch=.03;baseline.roll=-.1;
 baseline.turret_yaw=.2;baseline.gun_pitch=.3;baseline.require_hydraulic_body=true;
 baseline.hydraulic_body=std::array<double,8>{11.,12.,13.,.5,.6,-.2,.7,-.15};
 AimPose actual;actual.position={11.,12.,13.};actual.yaw=.5;actual.pitch=.6;actual.roll=-.2;actual.turret_yaw=.7;actual.gun_pitch=-.15;
 GunMount mount{{0.,1.,.2},{.2,2.,.3},{.1,.3,2.}};
 assert(shot_origin(baseline,mount)==shot_origin(actual,mount));
 assert(shot_direction(baseline)==shot_direction(actual));
 assert(world_barrel_yaw(baseline)==world_barrel_yaw(actual));
 AimSample point;point.point={1.,2.,3.};point.mount={.2,.3,.4};point.turret=true;point.static_yaw=-.9;
 auto actual_point=point;actual_point.static_yaw.reset();
 assert(vehicle_aim_point(point,baseline)==vehicle_aim_point(actual_point,actual));
 Actor a;a.pose=baseline;const auto frozen=frozenPose(a);
 (*a.pose.hydraulic_body)[4]=-.8;a.pose.pitch=.9;
 overlayPose(a,frozen);assert(a.pose.hydraulic_body==baseline.hydraulic_body&&a.pose.pitch==baseline.pitch);
 auto missing=frozen;missing.hydraulic_body.reset();overlayPose(a,missing);
 assert(!a.pose.hydraulic_body&&a.pose.require_hydraulic_body);
 bool failed=false;try{shot_origin(a.pose,mount);}catch(const std::runtime_error&){failed=true;}assert(failed);
 failed=false;try{vehicle_aim_point(point,a.pose);}catch(const std::runtime_error&){failed=true;}assert(failed);
 a.pose.require_hydraulic_body=false;assert(shot_origin(a.pose,mount)==shot_origin(AimPose{a.pose},mount));
}
'''

MEMORY = r'''
#include "stock_native_bot_perception.hpp"
#include <cassert>
using namespace stock_native_bot_perception;
namespace c=offline_simulation::control;
struct ControlProbe {
 std::vector<c::Observation> events;std::vector<c::Contact> found;
 std::vector<c::Observation>take_observations(){auto out=events;events.clear();return out;}
 template<class F>std::vector<c::Contact>contacts(const Key&,F){return found;}
};
struct MemoryProbe {
 ControlProbe control_;std::map<Key,Actor>actors_;
 std::map<TeamKey,Remembered>remembered_;std::map<TeamKey,Observation>observations_;
 std::map<std::pair<Key,double>,Pose>sampled_poses_;
 c::SightReply sight(const c::SightRequest&){return {};}
 void events(bool);std::vector<Contact>contacts(const Key&);
};
@SAMPLE@
@EVENTS@
@CONTACTS@
int main(){
 const Key observer{offline_simulation::ActorKind::Bot,1},human{offline_simulation::ActorKind::Human,2};
 MemoryProbe owner;owner.actors_[observer].config.team=1;
 auto&target=owner.actors_[human];target.require_hydraulic_body=true;
 const std::array<double,8> first{1.,2.,3.,.1,.2,.3,.4,.5},second{1.,2.,3.,.1,.7,.3,.4,.5};
 target.hydraulic_body=first;
 c::Observation event;event.observer=observer;event.target=human;event.sampled_at=1.;event.duration=10.;
 owner.control_.events={event};c::Contact contact;contact.target=human;contact.has_pose=true;contact.sampled_at=1.;
 owner.control_.found={contact};auto rows=owner.contacts(observer);assert(rows[0].pose.hydraulic_body==first);
 // Same baseline, later actual hydraulic application: the old contact must
 // retain its observed body instead of attaching the latest actor receipt.
 target.hydraulic_body=second;rows=owner.contacts(observer);assert(rows[0].pose.hydraulic_body==first);
 event.sampled_at=2.;owner.control_.events={event};owner.control_.found[0].sampled_at=2.;
 rows=owner.contacts(observer);assert(rows[0].pose.hydraulic_body==second);
 owner.control_.found[0].sampled_at=.5;rows=owner.contacts(observer);
 assert(!rows[0].pose.hydraulic_body&&rows[0].pose.require_hydraulic_body);
 target.hydraulic_body.reset();event.sampled_at=3.;owner.control_.events={event};owner.control_.found[0].sampled_at=3.;
 rows=owner.contacts(observer);assert(!rows[0].pose.hydraulic_body&&rows[0].pose.require_hydraulic_body);
}
'''

ADMISSION = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <optional>
#include <vector>
using Point=std::array<double,3>;
namespace geometry {
struct Vehicle{bool has_siege=true,ready=true;};
struct Pose{bool require_hydraulic_body=true;std::optional<std::array<double,8>>hydraulic_body;};
}
namespace stock_native_bot_armor {struct Body{std::int64_t id=1;};}
namespace step {using Point=::Point;struct LaneBody{std::int64_t id=1;};}
struct GeometryRow{bool available=true;int key=1;geometry::Pose pose;};
struct Vehicles{geometry::Vehicle value;const geometry::Vehicle*find(int)const{return &value;}};
struct NativeWeaponWorld {
 struct Context{Vehicles vehicles;}context_;GeometryRow actor;int queries=0;
 const GeometryRow*row(std::int64_t)const{return &actor;}
 std::vector<int>intersections(const GeometryRow&,const geometry::Pose&,const Point&,const Point&){++queries;return {};}
 bool ready(const stock_native_bot_armor::Body&);
 bool vehicle_segment(const step::LaneBody&,const Point&,const Point&);
};
@KNOWN@
@READY@
@LANE@
int main(){
 NativeWeaponWorld world;
 assert(!world.ready({}));assert(world.vehicle_segment({}, {}, {}));assert(world.queries==0);
 world.actor.pose.hydraulic_body=std::array<double,8>{};
 assert(world.ready({}));assert(!world.vehicle_segment({}, {}, {}));assert(world.queries==1);
 world.actor.pose.hydraulic_body.reset();world.context_.vehicles.value.has_siege=false;
 assert(world.ready({}));assert(!world.vehicle_segment({}, {}, {}));
 world.context_.vehicles.value.has_siege=true;world.actor.pose.require_hydraulic_body=false;
 assert(world.ready({}));assert(!world.vehicle_segment({}, {}, {}));
}
'''


class HumanHydraulicConsumer(unittest.TestCase):
    def compile_run(self, source, extra=(), include=()):
        with tempfile.TemporaryDirectory(prefix='human-hydraulic-consumer-') as folder:
            path = Path(folder)
            (path / 'probe.cpp').write_text(source)
            command = ['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                       *native_test_compile_flags(), '-I' + str(ROOT / 'bigworld'),
                       *include, str(path / 'probe.cpp'), *extra, '-o', str(path / 'probe')]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            subprocess.run([str(path / 'probe')], check=True)

    def test_same_sample_replica_interpolation_absence_and_recovery(self):
        source = (ROOT / 'bigworld/stock_native_battle_remote_replica.cpp').read_text()
        fixture = REPLICA
        for token, signature in (('BLEND', 'Pose blend('), ('BODY', 'std::optional<Pose> blendBody('),
                                 ('BRACKET', 'Bracket bracket('), ('INTERPOLATE', 'bool interpolate(')):
            fixture = fixture.replace('@' + token + '@', block(source, signature))
        self.compile_run(fixture)

    @unittest.skipUnless(FORK, 'Pinned control core headers are required')
    def test_applied_gun_and_observation_memory_do_not_borrow_newer_body(self):
        weapon = (ROOT / 'bigworld/stock_native_bot_weapon.cpp').read_text()
        observation = (ROOT / 'bigworld/stock_native_bot_observation_runtime.cpp').read_text()
        fixture = AIM.replace('@POINT@', block(weapon, 'Point vehicle_aim_point('))
        fixture = fixture.replace('@FROZEN@', block(observation, 'perception::Pose frozenPose('))
        fixture = fixture.replace('@OVERLAY@', block(observation, 'void overlayPose('))
        self.compile_run(fixture, [str(ROOT / 'bigworld/stock_native_bot_step_state.cpp')],
                         ['-I' + str(Path(FORK) / 'native')])

    @unittest.skipUnless(FORK, 'Pinned control core headers are required')
    def test_control_observation_identity_keeps_the_original_applied_body(self):
        source = (ROOT / 'bigworld/stock_native_bot_perception.cpp').read_text()
        fixture = MEMORY.replace('@SAMPLE@', block(source, 'Pose appliedSample('))
        for token, signature in (('EVENTS', 'void Perception::events('),
                                 ('CONTACTS', 'std::vector<Contact> Perception::contacts(')):
            fixture = fixture.replace('@' + token + '@', block(source, signature).replace('Perception::', 'MemoryProbe::'))
        self.compile_run(fixture, include=['-I' + str(Path(FORK) / 'native')])

    def test_missing_receipt_holds_all_visual_writers_before_their_cache_commit(self):
        replica = block((ROOT / 'bigworld/stock_native_battle_remote_replica.cpp').read_text(), 'void presentReplicaPose(')
        edges = block((ROOT / 'bigworld/stock_native_battle_vehicles_edges.cpp').read_text(), 'void applyPose(')
        remote = (ROOT / 'bigworld/stock_native_battle_remote_vehicle.cpp').read_text()
        for function, write in ((replica, 'if (!e.has_written'), (edges, 'const double now = b.clock()')):
            self.assertLess(function.index('hydraulicBodyPending('), function.index(write))
        pending = block(remote, 'bool hydraulicBodyPending(')
        self.assertIn('actor->kind == "player"', pending)
        self.assertIn('hasSiege(*rv) && !rv->hydraulic_body', pending)
        for signature, write in (('bool setPose(', 'const Point ypr'), ('bool setAim(', 'double turret_yaw')):
            body = block(remote, signature)
            self.assertLess(body.index('hydraulicBodyPending('), body.index(write))
        self.assertNotIn('setHullAimingAnglesDelta', remote)

    def test_unknown_human_body_holds_only_its_ram_and_launch_admission(self):
        source = (ROOT / 'bigworld/stock_native_bot_weapon_world.cpp').read_text()
        fixture = ADMISSION
        for token, signature in (('KNOWN', 'bool bodyKnown('), ('READY', 'bool NativeWeaponWorld::ready('),
                                 ('LANE', 'bool NativeWeaponWorld::vehicle_segment(')):
            fixture = fixture.replace('@' + token + '@', block(source, signature))
        self.compile_run(fixture)


if __name__ == '__main__':
    unittest.main()
