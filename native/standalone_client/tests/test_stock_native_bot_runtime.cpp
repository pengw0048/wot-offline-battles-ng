#include "stock_native_bot_runtime.hpp"
#include <cassert>
#include <iostream>
using namespace stock_native_bot;
namespace w=stock_native_world;
Key key(ID id){return {offline_simulation::ActorKind::Bot,id};}
struct Scene:w::Scene {
    unsigned queries=0;
    std::vector<w::Destruction> effects;
    w::QueryStatus segment(const Point&a,const Point&b,std::uint16_t,const w::Filter&,w::Hit&hit)override{
        ++queries;if(a[0]==b[0]&&a[2]==b[2]&&a[1]>=0.&&b[1]<=0.){hit.point={a[0],0.,a[2]};hit.normal={0.,1.,0.};return w::QueryStatus::Found;}return w::QueryStatus::Miss;}
    w::QueryStatus material(const Point&,const Point&,const Point&,w::MaterialHit&)override{return w::QueryStatus::Miss;}
    w::QueryStatus water(const Point&,const Point&,double&)override{return w::QueryStatus::Miss;}
    w::Admission refresh(const w::Key&,w::Registry&)override{return w::Admission::Pending;}
    bool unresolved(const w::Key&,std::vector<w::Box>&)override{return false;}
    bool destroy(const w::Destruction& value)override{effects.push_back(value);return true;}
    bool treeChunks(const std::vector<w::Box>&,std::vector<std::uint32_t>&)override{return true;}
    bool turretSweep(const w::TurretSweep&)override{return true;}
};
struct World:step::WeaponRuntimeWorld,observation::World,step::ImpactWorld {
    std::map<ID,step::AimPose> poses;
    void bind(Key,const std::optional<step::WeaponTarget>&,const stock_native_bot_armor::Shell*)override{}
    std::optional<step::WeaponTarget> target(const std::string&,ID)override{return {};}
    std::optional<Point> barrel_point(Key,const step::AimPose&p)override{return p.position;}
    bool source_available(Key)override{return true;}
    std::vector<step::LaneBody> lane_bodies(Key)override{return {};}
    bool turret_clear(const step::AimPose&,const step::AimPose&)override{return true;}
    bool barrel_under_water(const Point&)override{return false;}
    bool vehicle_segment(const step::LaneBody&,const Point&,const Point&)override{return false;}
    bool debris_segment(const Point&,const Point&)override{return false;}
    bool static_segment_blocked(const Point&,const Point&)override{return false;}
    bool wreck_segment_blocked(const Point&,const Point&)override{return false;}
    std::optional<double> nominal_damage(const Point&,const Point&)override{return 10.;}
    step::ArcQuery segment(const Point&,const Point&,Point&)override{return step::ArcQuery::clear;}
    bool outgoingLane(const observation::Actor&,const observation::Actor&,double)override{return true;}
    bool staticClear(const Point&,const Point&)override{return true;}
    std::optional<double> ground(double,double,double)override{return 0.;}
    double waterDepth(const Point&)override{return -1.;}
    std::vector<Point> ground_witnesses()override{return {};}
    std::vector<step::ImpactHit> material_segment(const Point&,const Point&)override{return {};}
    std::vector<step::ImpactHit> interior_segment(const Point&,const Point&)override{return {};}
};
struct Service:Services {
    World world;stock_native_bot_turret::Obstacles obstacles;unsigned callbacks=0,changes=0,scans=0,modes=0;
    std::vector<ID> warmed;
    step::WeaponRuntimeWorld&weaponWorld()override{return world;}observation::World&tacticalWorld()override{return world;}
    stock_native_bot_turret::Obstacles&turrets()override{return obstacles;}step::ImpactWorld&impactWorld(ID)override{return world;}
    std::optional<contacts::Armor> armor(const contacts::Body&,const contacts::Body&,const std::array<double,3>&)override{return {};}
    void actorChanged(const Actor&a,const step::AimPose&p)override{world.poses[a.motion.id]=p;++changes;}
    void modeChanged(ID,const Mode&)override{++modes;}
    void beginCallback(double,double)override{++callbacks;}
    void scanDestructibles(const motion::State&)override{++scans;}
    bool prewarmDestructibles(const Actor&a,double)override{warmed.push_back(a.motion.id);return true;}
    bool prewarmPlayerDestructibles(const Human&h,double)override{warmed.push_back(h.contacts.network_id);return true;}
};
Snapshot fixture(const motion::Descriptor&d) {
    Snapshot s;s.identity={1,1,0,0};s.clock.period=.05;auto&p=s.planning;
    p.map.width=p.map.height=41;p.map.cell_size=5.;p.map.origin_x=p.map.origin_z=-100.;p.map.has_bounds=true;
    p.map.bounds={-100.,-100.,100.,100.};p.map.heights.assign(1681,0.);p.map.hazards.assign(1681,0);p.map.links.assign(1681,255);
    p.grade_up=p.grade_down=1.;p.bake={2.15,3.,true};p.directions={{{0,1},{1,1},{1,0},{1,-1},{0,-1},{-1,-1},{-1,0},{-1,1}}};
    for(ID id:{8,1}) {
        ActorSnapshot a;a.motion.state.id=id;a.motion.state.position={double(id)*5.,0.,0.};a.motion.state.grounded_once=true;
        a.motion.team=1;a.motion.slot=id==8?0:1;a.motion.descriptor=d;a.motion.suspension.half_length=d.half_length;a.motion.suspension.half_width=d.half_width;
        auto&c=a.combat;c.state.health=c.state.maximum_health=c.state.display_health=100;c.ledger.initialize(c.state,0,0,0);
        c.gun_config.fully_aimed_dispersion=.2;c.gun_config.aiming_time=1.;c.gun_config.reload_full=1.;
        c.gun_config.clip_size=c.gun_config.shell_count=c.gun_config.burst_count=1;c.ammo.categories={0};c.ammo.remaining={5};
        c.aim_config.fixed_pitch=true;c.aim_config.fixed_limits={-.5,.5};c.aim_config.turret_speed=c.aim_config.gun_speed=1.;
        a.weapon.round_id=1;a.weapon.team=1;a.weapon.class_tag="mediumTank";a.metadata.team=1;
        for(auto&m:a.modes){m.motion.descriptor=d;m.motion.suspension=a.motion.suspension;
            m.combat.gun_config=c.gun_config;m.combat.aim_config=c.aim_config;m.combat.water_sensor=Point{};m.combat.valid_pitch=true;
            m.combat.minimum_yaw=-3.141592653589793;m.combat.maximum_yaw=3.141592653589793;
            m.navigation.candidate={true,d.half_width};m.navigation.hull_minimum=d.hull_minimum;m.navigation.hull_maximum=d.hull_maximum;
            m.navigation.mass=d.drive.mass;m.navigation.speed_forward=d.drive.forward;m.navigation.speed_reverse=d.drive.reverse;m.navigation.valid=true;
            m.perception.config.key=key(id);m.perception.sample.key=key(id);m.perception.config.team=1;m.perception.identity=id;m.perception.target_available=true;
            m.physics.geometry.turret.chassis.bounds={d.body_minimum,d.body_maximum};m.physics.geometry.turret.hull=m.physics.geometry.turret.chassis;
        }
        nav::NavigationActor n;n.id=id;n.team=1;n.half_width=n.collision_half_width=d.half_width;n.half_length=n.collision_half_length=d.half_length;
        p.actors[id]=n;p.descriptors[id]=a.modes[0].navigation;stock_native_bot_strategy::Agent agent;agent.id=id;agent.team=1;agent.profile.class_tag="mediumTank";p.director.agents[id]=agent;
        planning::StrategicOrder order;order.drive.mode=order.navigation.mode="route";order.drive.move.set({double(id)*5.,0.,60.});p.orders[id]=order;p.order_tokens[id]=1;s.actors.push_back(a);
    }
    return s;
}
int main() {
    motion::Descriptor d;for(auto&v:d.drive.t)std::cin>>v;assert(std::cin);
    d.drive.mass=25000.;d.drive.power=280000.;d.drive.ratio=1.;d.drive.forward=14.;d.drive.reverse=6.;d.drive.friction=.07;d.drive.brake=4.;d.drive.rotation=.7;
    d.drive.terrain[0]=1.;d.drive.terrain[1]=1.2;d.drive.terrain[2]=1.8;d.half_width=1.7;d.half_length=3.5;d.track_center=1.3;
    d.collision_shape={1.7,3.5,0.,2.};d.body_minimum=d.hull_minimum={-1.7,0.,-3.5};d.body_maximum=d.hull_maximum={1.7,2.,3.5};
    {
        Scene external_scene;Service external_service;w::Registry external_registry;
        Runtime external(fixture(d),external_registry,external_scene,external_service);
        const auto before=external.snapshotExternalContacts();const auto updates=external.statistics().callbacks;
        motion::ContactResponse response;response.delta_velocity={3.,2.};
        assert(external.externalContact(1,response,.1,.5));
        const auto& touched=external.actors().at(1).motion;
        assert(touched.position[0]==before.actors.at(1)[0]&&touched.position[2]==before.actors.at(1)[1]);
        assert(touched.speed==before.actors.at(1)[2]+2.&&touched.push_x==before.actors.at(1)[3]+3.);
        assert(external.statistics().callbacks==updates&&external.clock().sample_us==0);
        assert(external_service.world.poses.at(1).position==touched.position);
        external.restoreExternalContacts(before,.5);
        const auto restored=external.snapshotExternalContacts();assert(restored.actors==before.actors);
        assert(!external.externalContact(999,response,.1,.5));
    }
    Scene scene;Service service;w::Registry registry;Runtime runtime(fixture(d),registry,scene,service);
    assert(runtime.update(.02,.02,20.).empty());assert(runtime.clock().sample_us==0);
    auto first=runtime.update(.03,.05,50.);assert(first.size()==1);assert(first[0].state.sample_us==50000);assert(first[0].state.rows.size()==2);
    assert(first[0].state.rows[0][0]==8&&first[0].state.rows[1][0]==1);assert(runtime.statistics().refreshes==1);
    auto late=runtime.update(.65,.70,700.);assert(runtime.statistics().slices==5);assert(runtime.statistics().refreshes==2);
    assert(runtime.clock().sample_us==700000);assert(runtime.clock().callback_steps==4);assert(late.size()==2);
    assert(late.front().state.sample_us==250000&&late.back().state.sample_us==700000);
    assert(runtime.actors().at(1).motion.position[2]>0.);assert(runtime.statistics().python_business_callbacks==0);
    assert(runtime.statistics().refresh_slices==2&&runtime.statistics().reuse_slices==3);
    assert(runtime.statistics().refresh_ns>0&&runtime.statistics().reuse_ns>0);
    assert(runtime.statistics().refresh_actors==4&&runtime.statistics().reuse_actors==6);
    assert(std::abs(runtime.equipmentTime()-.7)<1.e-9);assert(runtime.contactActor(1)&&!runtime.contactActor(99));
    // Opt-in stage accounting partitions work without altering a slice or
    // leaving clocks active after the diagnostic ends.
    {
        Service timed_service,plain_service;w::Registry timed_registry,plain_registry;
        Runtime timed(fixture(d),timed_registry,scene,timed_service);
        Runtime plain(fixture(d),plain_registry,scene,plain_service);
        timed.stageTiming(true);
        auto measured=timed.update(.1,.1,100.);auto expected=plain.update(.1,.1,100.);
        assert(measured.size()==expected.size());
        for(std::size_t i=0;i<measured.size();++i)assert(measured[i].state.rows==expected[i].state.rows);
        std::uint64_t accounted=0;for(auto value:timed.statistics().stage_ns)accounted+=value;
        assert(accounted>0&&accounted<=timed.statistics().refresh_ns+timed.statistics().reuse_ns);
        assert(timed.statistics().detailed_slices==timed.statistics().slices);
        assert(plain.statistics().detailed_slices==0);
        const auto before=timed.statistics().stage_ns;timed.stageTiming(false);
        timed.update(.05,.15,150.);assert(timed.statistics().stage_ns==before);
    }
    // Equal orders preserve leases; changed routes invalidate only their actor.
    auto orders=runtime.planning().decisions().orders;
    const auto token=runtime.planning().decisions().tokens.at(1);
    assert(runtime.acceptOrders(orders,1));assert(runtime.planning().decisions().tokens.at(1)==token);
    assert(!runtime.acceptOrders({},0));assert(runtime.planning().decisions().orders.size()==2);
    orders.at(1).navigation.route_id="changed";
    assert(runtime.acceptOrders(orders,2));assert(runtime.planning().decisions().tokens.at(1)==token+1);
    assert(runtime.planning().decisions().tokens.at(8)==1);assert(!runtime.planning().decisions().cache.count(1));
    auto invalid=ServerSnapshot{};invalid.combat.record=step::combat_record(runtime.actors().at(1).combat);invalid.fire_sequence=2;
    bool rejected=false;try{runtime.serverSnapshot(1,invalid);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);assert(runtime.weapons().at(key(1)).fire_seq==0);
    invalid.has_reload=true;invalid.reload_time=.8;invalid.reload_duration=1.;invalid.clip=std::array<int,2>{0,1};
    invalid.ammunition=runtime.weapons().at(key(1)).ammo;invalid.ammunition->remaining[0]=3;assert(runtime.serverSnapshot(1,invalid));
    assert(runtime.weapons().at(key(1)).fire_seq==2&&runtime.weapons().at(key(1)).ammo.remaining[0]==3);
    auto killed=ServerSnapshot{};killed.combat.record=step::combat_record(runtime.actors().at(8).combat);killed.combat.record.alive=false;killed.combat.record.health=0;
    killed.combat.revision=killed.combat.base_revision=1;assert(runtime.serverSnapshot(8,killed));assert(!runtime.actors().at(8).motion.alive);
    runtime.update(.05,.75,750.);assert(runtime.actors().at(1).motion.alive);assert(!runtime.actors().at(8).motion.alive);
    // A full human roster participates in the same native contact dictionary.
    Human human;human.contacts.network_id=3;auto&body=human.contacts.body;
    body.id=1000003;body.team=1;body.x=-50.;body.mass=25000.;body.shape=d.collision_shape;
    body.flags=offline_contact::Alive|offline_contact::HasY|offline_contact::PositionFixed;
    human.contacts.checkpoint_field_present=true;human.perception.config.key={offline_simulation::ActorKind::Human,3};
    human.perception.sample.key=human.perception.config.key;human.perception.config.team=1;
    human.perception.sample.pose.position={-50.,0.,0.};human.perception.identity=3;human.perception.target_available=true;
    human.observation.key=human.perception.config.key;human.observation.pose.position={-50.,0.,0.};
    human.observation.target.team=1;human.observation.target.identity={"human",3};
    human.neighbour.id=1000003;human.neighbour.position={-50.,0.,0.};human.neighbour.team=1;human.neighbour.alive=true;
    human.neighbour.half_length=d.half_length;human.neighbour.half_width=d.half_width;human.neighbour.lower_y=0.;human.neighbour.upper_y=2.;
    human.navigation.position={-50.,0.,0.};human.navigation.collision_half_width=d.half_width;human.navigation.collision_half_length=d.half_length;
    runtime.humans({human});auto with_human=runtime.update(.05,.80,800.);assert(with_human.size()==1);
    assert(with_human[0].state.rows.size()==2);runtime.humans({});
    // An armed burst deadline consumes a short physical tail without thinking.
    auto burst_snapshot=fixture(d);auto&armed=burst_snapshot.actors[0].combat;
    armed.gun_config.reload_intra=.1;armed.gun_config.clip_size=2;armed.gun_config.burst_count=2;armed.gun_config.burst_interval=.02;
    armed.gun.emplace(armed.gun_config);armed.gun->clip=1;armed.gun->burst_remaining=1;
    armed.fire_sequence=1;armed.burst.active=true;armed.burst.group_seq=1;armed.burst.count=2;
    armed.burst.next_index=1;armed.burst.interval=armed.burst.time_left=.02;
    Service burst_service;w::Registry burst_registry;Runtime burst_runtime(burst_snapshot,burst_registry,scene,burst_service);
    auto burst=burst_runtime.update(.03,.03,30.);assert(burst.size()==1);assert(burst_runtime.clock().sample_us==20000);
    assert(burst_runtime.statistics().refreshes==0);assert(!burst_runtime.weapons().at(key(8)).burst.active);
    assert(burst_runtime.weapons().at(key(8)).fire_seq==1);assert(burst_runtime.clock().bank>.009999);
    // Completing a siege transition immobilizes its complete starting slice.
    auto siege_snapshot=fixture(d);auto&switching=siege_snapshot.actors[0];switching.has_siege=true;
    switching.combat.siege.mode=1;switching.combat.siege.remaining=.05;switching.combat.siege.total=.1;
    switching.combat.siege_config={true,.1,.1,1.};switching.motion.state.speed=4.;
    Service siege_service;w::Registry siege_registry;Runtime siege_runtime(siege_snapshot,siege_registry,scene,siege_service);
    siege_runtime.update(.1,.1,100.);assert(siege_runtime.actors().at(8).mode==1);assert(siege_service.modes==1);
    assert(siege_runtime.actors().at(8).motion.position==switching.motion.state.position);assert(siege_runtime.actors().at(8).motion.speed==0.);
    // A nearer camera promotes an existing far lease before visibility work.
    auto far_snapshot=fixture(d);far_snapshot.camera=Point{0.,0.,1000.};
    Service far_service;w::Registry far_registry;Runtime far_runtime(far_snapshot,far_registry,scene,far_service);
    far_runtime.update(.05,.05,50.);far_runtime.update(.3,.35,350.);
    assert(far_runtime.planning().decisions().cache.at(1).interval==.6);
    far_runtime.camera(Point{0.,0.,0.});far_runtime.update(.05,.4,400.);
    assert(far_runtime.planning().decisions().cache.at(1).interval==.15);
    assert(std::abs(far_runtime.planning().decisions().cache.at(1).at-.4)<1.e-9);
    // Countdown prewarm advances without any simulation callback. Empty
    // placement reads retry, while a proved crushed overlap seals once.
    auto warm_snapshot=fixture(d);auto extra=warm_snapshot.actors[0];
    extra.motion.state.id=17;extra.motion.state.position={85.,0.,0.};extra.motion.slot=2;
    warm_snapshot.actors.push_back(extra);
    for(auto&a:warm_snapshot.actors){a.spawn_anchor=a.motion.state.position;for(auto&m:a.modes)m.spawn_drive_cap=14.;}
    warm_snapshot.actors[1].spawn_anchor=Point{0.,0.,0.};
    Service warm_service;w::Registry warm_registry;Scene warm_scene;
    warm_registry.catalog_available=true;warm_registry.unit_vehicle_mass=1000.;
    Runtime warm_runtime(warm_snapshot,warm_registry,warm_scene,warm_service);
    assert(warm_runtime.prewarm(0.)==2);assert(warm_service.warmed==std::vector<ID>({8,1}));
    const auto add_prop=[&](unsigned id,double x,w::Kind kind){w::Instance instance;
        instance.kind=kind;instance.admission=w::Admission::Admitted;instance.filename="exact/prop";
        w::Box box;box.center={x,1.,0.};box.axes={{{.2,0.,0.}},{{0.,1.,0.}},{{0.,0.,.2}}};instance.boxes={box};
        instance.origin={x,0.,0.};instance.scaled_health[-1]=1.;instance.reference_health[-1]=1.;
        instance.item_scale=1.;instance.descriptor_valid=instance.native_name_exact=true;warm_registry.put({1,id},instance);};
    add_prop(1,40.,w::Kind::Fragile);add_prop(2,40.,w::Kind::Structure);add_prop(3,5.,w::Kind::Fragile);
    warm_runtime.prewarm(.5);assert(warm_scene.effects.empty());
    warm_runtime.prewarm(1.);warm_runtime.prewarm(1.1);
    assert(warm_service.warmed==std::vector<ID>({8,1,17,8,1,17,8,1}));
    assert(warm_scene.effects.size()==1&&warm_scene.effects[0].key.second==1&&warm_scene.effects[0].speed==0.);
    add_prop(4,40.,w::Kind::Fragile);warm_runtime.prewarm(3.);warm_runtime.prewarm(3.1);
    assert(warm_scene.effects.size()==1&&warm_runtime.clock().sample_us==0&&warm_service.callbacks==0);
    human.body_minimum=d.body_minimum;human.body_maximum=d.body_maximum;
    human.spawn_anchor=Point{-50.,0.,0.};human.spawn_drive_cap=14.;human.contact_mass=25000.;warm_runtime.humans({human});
    add_prop(5,-50.,w::Kind::Fragile);
    assert(warm_runtime.prewarmPlayers(3.2,true)==1&&warm_scene.effects.size()==2);
    assert(warm_scene.effects.back().key.second==5&&warm_scene.effects.back().speed==0.);
    assert(warm_runtime.prewarmPlayers(3.3,true)==0);
    assert(warm_runtime.prewarmPlayers(3.4,false)==1&&warm_scene.effects.size()==2);
    std::cout<<"Native runtime integration passed\n";
}
