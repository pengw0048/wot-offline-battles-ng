"""Compare native strategy data laws with the pinned v1.1.1 Python source.

The source tree and compiled probe are explicit inputs. This never imports a
Python planner in the room process, starts a VM or touches a gameplay session.
"""
import argparse
import copy
import importlib
import json
import random
import subprocess
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--oracle", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.oracle / "server"))
    planner = importlib.import_module("server_bot_ai")
    cover = importlib.import_module("gui.mods.offline_lan_0922.ai.cover")
    capture = importlib.import_module("gui.mods.offline_lan_0922.capture_orders")
    empty = importlib.import_module("gui.mods.offline_lan_0922.empty_ammo")
    tactics = importlib.import_module("gui.mods.offline_lan_0922.bot_tactics")
    spg = importlib.import_module("gui.mods.offline_lan_0922.spg_positions")
    network = importlib.import_module("gui.mods.offline_lan_0922.route_network")
    gunnery = importlib.import_module("gui.mods.offline_lan_0922.bot_gunnery")
    ammunition = importlib.import_module("gui.mods.offline_lan_0922.ammo_policy")
    randomizer = random.Random(1513)
    requests, expected = [], []

    def check(request, value):
        requests.append(request)
        expected.append(value)

    for inventory in (None, [], [0], [0, 0], [False], [True], [0, 1], [-1], [0.0], ["0"], [0, 0, 0]):
        for shells in ([], [{"index": 0}], [{"index": 0}, {"index": 1}]):
            state, profile = {"ammo_remaining": inventory}, {"shells": shells}
            check(dict(op="empty", state=state, profile=profile), empty.confirmed(state, profile))
    for actor in (1, 2, 29, 8191, 40000001):
        check(dict(op="personality", id=actor), planner.BotPlanner._personality(actor))
    for name in gunnery.CAPABILITIES:
        for rating in (0, .17, .34, .505, .67, .999, 1):
            for occasion in ((7, 29), (7, 29, None), (7, 29, 31.5), (7, 29, ("bot", 4), 9)):
                check(dict(op="capability", rating=rating, name=name, occasion=occasion),
                      gunnery.capability_allowed(rating, name, *occasion))
    for tag in ("SPG", "mediumTank", "AT-SPG"):
        for kinds in (("ARMOR_PIERCING", "HOLLOW_CHARGE", "HIGH_EXPLOSIVE"),
                      ("HIGH_EXPLOSIVE", "HIGH_EXPLOSIVE", "ARMOR_PIERCING")):
            for ratios in (None, [1, 0, 2], [0, 1, 0]):
                profile = dict(class_tag=tag, vehicle_name="ussr:fixture", gun_name="fixture",
                    shells=[dict(index=i, kind=kind, name="shell%d" % i) for i, kind in enumerate(kinds)])
                if ratios is not None:
                    profile["ammo_ratios"] = ratios
                for remaining in ([0, 0, 0], [2, 0, 1], [0, 3, 2]):
                    for selected in (None, 0, 1, 2):
                        state = dict(ammo_remaining=remaining, ammo_policy_shell_index=selected)
                        check(dict(op="shell", bot=dict(profile=profile, state=state)),
                              planner.BotPlanner._shell_index(profile, {}, {}, state))
    for _ in range(100):
        candidates = []
        for i in range(4):
            candidates.append(dict(id="candidate-%d" % i, position={"x": i, "y": 0, "z": 1},
                peek_position=[i+1, 0, 2], travel_distance=randomizer.uniform(-5, 220),
                slope=randomizer.uniform(0, 35), route_alignment=randomizer.uniform(-.2, 1.2),
                enemy_occlusion=randomizer.random(), exposure=randomizer.random(),
                water=randomizer.random(), ally_congestion=randomizer.random(),
                peek_feasible=randomizer.choice([True, False, 1, 0, "yes"]),
                escape_feasible=randomizer.choice([True, False, 1, 0, "true"])))
        check(dict(op="cover", candidates=candidates), cover.score_candidates(candidates))
    for identity in (19,-1,True,False,0,0.5,"候補_ascii_1",None):
        candidate=dict(id=identity,position=[1,0,2],escape_feasible=True)
        check(dict(op="cover",candidates=[candidate]),cover.score_candidates([candidate]))
    area = dict(point=dict(x=0, y=0, z=0), radius=50)
    for pose_x in (-60, -50, 0, 49, 51):
        for destination_x in (0, 49, 51, 200):
            for mode in ("route", "engage", "flank", "base_capture"):
                for throttle in (None, 0, 1):
                    pose = dict(x=pose_x, y=0, z=0)
                    order = dict(capture_area=area, move_position=dict(x=destination_x,y=0,z=0),
                                 combat_mode=mode, throttle_override=throttle, target_id=5)
                    check(dict(op="capture", pose=pose, order=order), capture.constrain(order, pose))
    for pose in (dict(x=2500.12345,y=0,z=2500.67891),[2500.12345,0,2500.67891]):
        order=dict(capture_area=dict(point=dict(x=2500,y=0,z=2500),radius=50),
            move_position=dict(x=2500,y=0,z=2500),combat_mode="route",throttle_override=None)
        check(dict(op="capture",pose=pose,order=order),capture.constrain(order,pose))
    for _ in range(300):
        coords = [tuple(randomizer.uniform(-20, 20) for _ in range(2)) for _ in range(4)]
        pose, target, following, previous = coords
        for onward, before in ((None, None), (following, None), (None, previous), (following, previous)):
            check(dict(op="disk", pose=pose, target=target, following=onward, previous=before),
                  tactics.route_disk_reached(pose, target, onward, before))
    for _ in range(40):
        directions = [(1,0),(-1,0),(0,1),(0,-1),(1,1),(1,-1),(-1,1),(-1,-1)]
        graph = dict(game_version=spg.CATALOG["game_version"], width=5, height=5,
            cell_size=4., origin=[0,0], bounds=[0,0,16,16], directions=directions,
            heights_mm=[randomizer.randint(-1000, 1000) for _ in range(25)],
            hazards=[randomizer.choice([0,0,0,1,8]) for _ in range(25)],
            links=[randomizer.randint(0,255) for _ in range(25)])
        pose = dict(x=8,y=0,z=8)
        points = [dict(x=randomizer.randint(0,4)*4,y=0,z=randomizer.randint(0,4)*4) for _ in range(3)]
        expected_join = network.reachable_join(spg._Graph(graph,graph["bounds"]),(pose["x"],0,pose["z"]),points)
        check(dict(op="join", graph=graph, pose=pose, points=points), expected_join)
    def frozen(value):
        return json.loads(json.dumps(value))

    def plan_oracle(scenario):
        owner = planner.BotPlanner()
        owner.reset(scenario["round_id"])
        owner.tactics_map = scenario["map"]
        owner.tactics = scenario.get("resources", {}).get("strategy", {}).get("tactics", owner.tactics)
        if "graph" in scenario:
            owner._travel_graph_cache[scenario["map"]] = spg._Graph(scenario["graph"], scenario["graph"]["bounds"])
        bots, humans, commands = [], [], []
        context = scenario["rules"]["capture_context"]
        result = []
        for step in scenario["steps"]:
            bots = copy.deepcopy(step.get("bots", bots))
            humans = copy.deepcopy(step.get("players", humans))
            context = step.get("rules", {}).get("capture_context", context)
            commands = step.get("commands", commands)
            owner.report_contacts(step.get("contacts", []), owner.known_targets(bots, humans), step["now"])
            owner.report_affordances(step.get("affordances", []), owner.known_bots(scenario["manifest"], bots),
                                     owner.known_targets(bots, humans), step["now"])
            for hit in step.get("damage", []):
                owner.report_damage(hit["victim"], hit["kind"], hit["attacker"], hit["damage"], step["now"])
            result.append(frozen(owner.build_orders(scenario["manifest"], bots, humans, step["now"], context, commands)))
        return result

    for variant in range(32):
        manifest, bots = [], []
        tag = ("mediumTank", "heavyTank", "AT-SPG", "SPG")[variant % 4]
        for actor in range(1, 7):
            side = 1 if actor <= 3 else 2
            sign = 1 if side == 1 else -1
            profile = dict(class_tag=tag, dominant_role="support", speed=12, armor=80,
                desired_range=80 if variant % 3 == 0 else 180, fire_range=520,
                roles=dict(support=.8, flanker=.9 if variant % 5 == 0 else .2),
                shells=[dict(index=0, kind="ARMOR_PIERCING", name="fixture", damage=120)])
            route = dict(id="main", waypoints=[dict(x=0, y=0, z=60*sign),
                dict(x=0, y=0, z=0), dict(x=0, y=0, z=-60*sign)])
            manifest.append(dict(id=actor, team=side, slot=actor, skill_rating=(0,.34,.67,1)[variant%4],
                                 profile=profile, route=route))
            bots.append(dict(id=actor, team=side, alive=True, world_pose=True, x=(actor-3)*8,
                y=0, z=40*sign, yaw=0, speed=0, health=500, max_health=500,
                shell_index=0, ammo_remaining=[10], clip=2, clip_size=3, reload_time=0, fire_seq=0))
        context = dict(bases={"1":[], "2":[]}, states={"1":dict(invaders=0), "2":dict(invaders=0)},
            contributors={"1":[], "2":[]}, capture_bases={"1":[dict(id="1:0",x=0,y=0,z=60,radius=20)],
            "2":[dict(id="2:0",x=0,y=0,z=-60,radius=20)]}, arena_bounds=[-500,-500,500,500])
        graph = dict(game_version=spg.CATALOG["game_version"], width=9, height=9, cell_size=20,
            origin=[-80,-80], bounds=[-500,-500,500,500], heights_mm=[0]*81,
            links=[255]*81, hazards=[0]*81,
            directions=[(1,0),(-1,0),(0,1),(0,-1),(1,1),(1,-1),(-1,1),(-1,-1)])
        scenario = dict(op="plan", round_id=variant+1, map="01_karelia", manifest=manifest,
                        rules=dict(capture_context=context), graph=graph, steps=[])
        for tick in range(16):
            poses = copy.deepcopy(bots)
            if variant & 1 and tick >= 2:
                poses[0].update(clip=0, fire_seq=1, reload_time=max(0,15-tick))
            if variant & 2 and tick >= 4:
                poses[1]["ammo_remaining"] = [0]
            if variant & 4 and tick >= 6:
                poses[0].update(x=0,z=-50)
            if variant & 8 and tick >= 8:
                poses[2]["critical"] = dict(destroyed=["engineHealth"])
            contact = dict(observing_team=1, target_kind="bot", target_id=4, target_team=2,
                visible=True, x=8,y=0,z=-40, health=500,max_health=500,class_tag="mediumTank",armor=80,
                shootable_by_bot_ids=[1,2,3] if tick%5<3 else [],
                pending_lane_by_bot_ids=[1,2,3] if tick%5==3 else [], threatened_bot_ids=[1,2,3])
            step = dict(now=tick*2, bots=poses, players=[], contacts=[contact] if variant%3 else [])
            if variant & 16 and tick >= 3:
                context_tick=copy.deepcopy(context)
                context_tick["bases"]["1"]=[dict(id="1:0",x=0,y=0,z=60,radius=20)]
                context_tick["states"]["1"].update(invaders=1,time_left=50)
                context_tick["contributors"]["1"]=[dict(kind="bot",id=4,points=tick)]
                step["rules"]=dict(capture_context=context_tick)
            scenario["steps"].append(step)
        check(scenario, plan_oracle(scenario))
    # Reuse the last complete roster, with deterministic maximum competence,
    # to exercise the geometry-reported cover state machine and authored waits.
    baseline=copy.deepcopy(scenario)
    for row in baseline["manifest"]:
        row["skill_rating"]=1
        row["profile"]["class_tag"]="mediumTank"
        row["profile"]["roles"]["flanker"]=.1
    baseline["rules"]["capture_context"]=copy.deepcopy(context)
    baseline["steps"]=[]
    for tick in range(15):
        poses=copy.deepcopy(bots)
        for row in poses:
            row.update(ammo_remaining=[10],clip=2,reload_time=0,fire_seq=0)
        if tick>=1:poses[0].update(x=-30,z=20)
        if tick>=4:poses[0].update(x=-20,z=20)
        if tick>=6:poses[0]["fire_seq"]=1
        if tick>=8:poses[0].update(x=-30,z=20,clip=0,reload_time=4,fire_seq=2)
        candidate=dict(id="cover-a",position=dict(x=-30,y=0,z=20),peek_position=dict(x=-20,y=0,z=20),
            travel_distance=20,route_alignment=.8,enemy_occlusion=.9,exposure=.1,slope=0,water=0,
            ally_congestion=0,peek_feasible=True,escape_feasible=True)
        contact=dict(observing_team=1,target_kind="bot",target_id=4,target_team=2,visible=True,
            x=8,y=0,z=-40,health=500,max_health=500,class_tag="mediumTank",armor=80,
            shootable_by_bot_ids=[1,2,3],threatened_bot_ids=[1,2,3])
        baseline["steps"].append(dict(now=tick,bots=poses,players=[],contacts=[contact],
            affordances=[dict(bot_id=1,target_kind="bot",target_id=4,candidates=[candidate])]))
    check(baseline,plan_oracle(baseline))
    waiting=copy.deepcopy(baseline)
    instructions=[[0,40,1,3,[[0,40,3,90]]],[0,0,0],[0,-60,0]]
    authored=dict(id="route",team=1,classes=["mediumTank"],slots=[],policy="fixed",points=instructions)
    waiting["resources"]=dict(strategy=dict(tactics=dict(maps={"01_karelia":dict(routes=[authored])})))
    waiting["manifest"]=waiting["manifest"][:1]
    waiting["manifest"][0]["route"]=dict(id="user_route",waypoints=[dict(x=p[0],y=0,z=p[1]) for p in instructions])
    waiting["steps"]=[]
    for tick in range(14):
        pose=copy.deepcopy(bots[0]);pose.update(x=0,y=0,z=40 if tick<5 else 0 if tick<9 else -60)
        waiting["steps"].append(dict(now=tick,bots=[pose],players=[]))
    check(waiting,plan_oracle(waiting))
    recovery=copy.deepcopy(waiting)
    recovery.pop("resources")
    recovery["manifest"][0]["route"]=dict(id="lane",waypoints=[dict(x=0,y=0,z=z) for z in [60,20,-20,-60]])
    recovery["steps"]=[]
    for tick in range(12):
        pose=copy.deepcopy(bots[0]);pose.update(x=0,z=40,route_wreck_blocked=True)
        if tick>=4:pose.update(z=60)
        recovery["steps"].append(dict(now=tick*20,bots=[pose],players=[]))
    check(recovery,plan_oracle(recovery))
    pressure=copy.deepcopy(baseline)
    pressure["steps"]=[]
    for row in pressure["manifest"]:
        side=row["team"];lane="left" if row["id"] in (1,2) else "right"
        x=-50 if lane=="left" else 50;sign=1 if side==1 else -1
        row["route"]=dict(id=lane,waypoints=[dict(x=x,y=0,z=z*sign) for z in [60,0,-60]])
    for tick in [0,1,20,40,60,179,180,181,200]:
        poses=copy.deepcopy(bots)
        for row in poses:
            row.update(ammo_remaining=[10],clip=2,reload_time=0,fire_seq=0,x=-50 if row["id"] in (1,2) else 50)
        contacts=[dict(observing_team=1,target_kind="bot",target_id=actor,target_team=2,visible=True,
            x=50,y=0,z=-20,health=500,max_health=500,class_tag="mediumTank",armor=80,
            shootable_by_bot_ids=[]) for actor in (4,5,6)]
        pressure["steps"].append(dict(now=tick,bots=poses,players=[],contacts=contacts))
    check(pressure,plan_oracle(pressure))
    for variant in range(48):
        profile=dict(class_tag="SPG" if variant%3==0 else "mediumTank",clip_size=(1,3)[variant%2],
            vehicle_name="ussr:fixture",gun_name="fixture",
            shells=[dict(index=i,kind=kind,name="shell%d"%i) for i,kind in enumerate(
                ("ARMOR_PIERCING","HOLLOW_CHARGE","HIGH_EXPLOSIVE") if variant%3 else
                ("HIGH_EXPLOSIVE","HIGH_EXPLOSIVE","ARMOR_PIERCING"))])
        if variant%5==0:profile["ammo_ratios"]=[1,0,2]
        operations=[dict(operation="choose",state=dict(ammo_remaining=[20,20,20]))]
        for shot in range(1,13):
            group=(shot-1)//3+1 if variant%4==0 else shot
            operations.append(dict(operation="launch",seq=shot,shell=shot//5%3,group=group,
                count=3 if variant%4==0 else 1,position=dict(x=0 if shot<7 else 5,z=0),
                target=["bot",8 if shot<9 else 9]))
            operations.append(dict(operation="choose",state=dict(ammo_remaining=[20 if shot<10 else 0,20,20],
                burst_active=shot%3!=0,burst_group_seq=group,burst_next_index=shot%3)))
            if shot%2==0:
                for sequence in (shot,shot-1):
                    hit=variant%3!=0 or sequence%4!=0
                    operations.append(dict(operation="resolve",seq=sequence,hit=hit,
                        damage=100 if (variant+sequence)%5==0 else 0,penetrated=(variant+sequence)%4==0,
                        known=not(variant%7==0 and sequence%3==0)))
            if shot==6:operations.append(dict(operation="context",position=dict(x=4,z=0),target=None))
        owner=ammunition.ShotPolicy(profile);reference=[]
        for operation in operations:
            kind=operation["operation"];selected=None
            if kind=="choose":selected=owner.choose(operation["state"])
            elif kind=="context":owner.context(operation["position"],tuple(operation["target"]) if operation["target"] else None)
            elif kind=="launch":owner.launch(operation["seq"],operation["shell"],operation["group"],operation["count"],operation["position"],tuple(operation["target"]))
            elif kind=="resolve":owner.resolve(operation["seq"],operation["hit"],operation["damage"],operation["penetrated"],operation["known"])
            snapshot={key:getattr(owner,key) for key in ("phase","desired","epoch","magazine","used","phase_fired","phase_id")}
            snapshot.update(pending=len(owner.pending),selected=selected);reference.append(snapshot)
        check(dict(op="ammo",profile=profile,operations=operations),reference)
    for map_name,entry in list(spg.CATALOG["maps"].items())[:8]:
        zone=entry["zones"][0];cell=zone["cells"][0]
        left,bottom,right,top=spg.cell_bounds(cell,entry["bounds"])
        point=dict(x=(left+right)/2,y=0,z=(bottom+top)/2)
        side=next(int(team) for team,name in entry["spawn_sides"].items() if name==zone["side"])
        plan=dict(schema=1,catalog=spg.CATALOG["revision"],map=map_name,mode="regular",
            side=zone["side"],zone=zone["id"],cell=cell,source=zone["source"],vehicle="ussr:fixture",
            point=point,face=point,radius=2,clearance=4,geometry="baked_checked",fire_validation="runtime_required")
        resources=dict(spg_catalog=dict(revision=spg.CATALOG["revision"],maps={map_name:entry}))
        mutations=[{},dict(schema=True),dict(schema="1"),dict(catalog="wrong"),dict(cell="Z0"),
            dict(clearance=1),dict(radius=3),dict(point=dict(x=point["x"],y=True,z=point["z"])),
            dict(point=dict(x=right,y=0,z=top)),dict(vehicle="other:vehicle")]
        for patch in mutations:
            raw=dict(plan);raw.update(patch)
            check(dict(op="spg",plan=raw,resources=resources,map=map_name,vehicle="ussr:fixture",team=side),
                spg.canonical_plan(raw,map_name,"ussr:fixture",team=side))
    map_name="01_karelia"
    tactics_profile=tactics.empty()
    tactics_profile["maps"][map_name]=dict(positions=[dict(id="manual_a",team=1,point=[0,0],radius=30)])
    manual=dict(schema=1,catalog=tactics.digest(tactics_profile),map=map_name,mode="regular",side="team1",
        zone="manual_a",cell="manual",source="launcher_manual_v1",vehicle="ussr:fixture",
        point=dict(x=0,y=0,z=0),face=dict(x=0,y=0,z=10),radius=2,clearance=4,
        geometry="baked_checked",fire_validation="runtime_required")
    resources=dict(tactics=tactics_profile,tactics_digest=manual["catalog"],arena_bounds=tactics.MAPS[map_name]["bounds"])
    for patch in ({},dict(side="team2"),dict(cell="A1"),dict(catalog="wrong"),dict(clearance=True),
                  dict(point=dict(x=29,y=0,z=0)),dict(point=dict(x="0",y=0,z=0)),dict(schema=1.0)):
        raw=dict(manual);raw.update(patch)
        check(dict(op="spg",plan=raw,resources=resources,map=map_name,vehicle="ussr:fixture",team=1),
              spg.canonical_plan(raw,map_name,"ussr:fixture",team=1,tactics=tactics_profile))
    server=importlib.import_module("lan_battle_server")
    server._server_log=lambda line:None
    for variant in range(8):
        profile=dict(class_tag="SPG" if variant%2 else "mediumTank",clip_size=3 if variant&2 else 1,
            shells=[dict(index=0,kind="HIGH_EXPLOSIVE" if variant%2 else "ARMOR_PIERCING",name="a"),
                    dict(index=1,kind="HIGH_EXPLOSIVE" if variant%2 else "HOLLOW_CHARGE",name="b")])
        manifest=[dict(id=1,team=1,profile=profile)]
        poses=[dict(id=1,team=1,x=0,y=0,z=0,ammo_remaining=[20,20],shell_index=0)]
        orders=[dict(id=1,target_kind="bot",target_id=7,shell_index=0)]
        operations=[dict(type="refresh")]
        for sequence in range(1,9):
            operations.append(dict(type="launch",bot_id=1,shot_seq=sequence,shell_index=sequence//3%2,
                burst_group_seq=(sequence-1)//2+1 if variant&4 else sequence,
                burst_count=2 if variant&4 else 1,position=dict(x=4 if sequence>5 else 0,z=0)))
            operations.append(dict(type="terminal",bot_id=1,shot_seq=sequence,hit=not(variant%2),
                damage=0,penetrated=False,known=True))
        state=server.BattleState()
        state.bot_manifest=copy.deepcopy(manifest);state.bot_states={1:copy.deepcopy(poses[0])}
        state.bot_orders=dict(orders=copy.deepcopy(orders));reference=[]
        for operation in operations:
            if operation["type"]=="refresh":state._refresh_bot_ammo_orders()
            elif operation["type"]=="launch":
                policy=state._bot_ammo_policy(1);policy.choose(state.bot_states[1]);order=state.bot_orders["orders"][0]
                policy.launch(operation["shot_seq"],operation["shell_index"],operation["burst_group_seq"],
                    operation["burst_count"],operation["position"],(order["target_kind"],order["target_id"]))
                state._refresh_bot_ammo_orders()
            else:
                state._bot_ammo_terminal(dict(shooter_kind="bot",shooter_id=1,shot_seq=operation["shot_seq"],
                    projectile_id=operation["shot_seq"],_ammo_hit=operation["hit"],_ammo_damage=operation["damage"],
                    _ammo_penetrated=operation["penetrated"]),known=operation["known"])
            policies={}
            for actor,policy in state.bot_ammo_policies.items():
                snapshot={key:getattr(policy,key) for key in ("phase","desired","epoch","magazine","used","phase_fired","phase_id")}
                snapshot.update(pending=len(policy.pending));policies[str(actor)]=snapshot
            reference.append(frozen(dict(bots=list(state.bot_states.values()),orders=state.bot_orders["orders"],policies=policies)))
        check(dict(op="ammo_room",manifest=manifest,bots=poses,orders=orders,operations=operations),reference)
    payload = "".join(json.dumps(request)+"\n" for request in requests)
    process = subprocess.run([str(args.probe)], input=payload, text=True, capture_output=True, check=True)
    actual = [json.loads(line) for line in process.stdout.splitlines()]
    assert len(actual) == len(expected), (len(actual), len(expected), process.stderr)
    def behavior(value):
        if isinstance(value, dict):
            # The original retention loop writes these two observations into
            # the last Bot's variable. Native state correctly attributes them
            # to the actor whose lease was evaluated; behavior is unchanged.
            return {key: behavior(item) for key,item in value.items()
                    if key not in ("aim_retention", "tracking_reason")}
        if isinstance(value, list):
            return [behavior(item) for item in value]
        return value
    def difference(got,wanted,path="",tolerant=False):
        if isinstance(got,dict) and isinstance(wanted,dict):
            for key in sorted(set(got)|set(wanted)):
                if key not in got:return path+"."+key,"missing",wanted[key]
                if key not in wanted:return path+"."+key,got[key],"extra"
                mismatch=difference(got[key],wanted[key],path+"."+key,tolerant)
                if mismatch:return mismatch
        elif isinstance(got,list) and isinstance(wanted,list):
            if len(got)!=len(wanted):return path+".length",len(got),len(wanted)
            for item,(a,b) in enumerate(zip(got,wanted)):
                mismatch=difference(a,b,path+"[%d]"%item,tolerant)
                if mismatch:return mismatch
        elif got!=wanted:
            if tolerant and isinstance(got,(int,float)) and isinstance(wanted,(int,float)) and abs(got-wanted)<1.e-12:return None
            return path,got,wanted
        return None
    for index, (got, wanted) in enumerate(zip(actual, expected)):
        if requests[index]["op"] == "plan":
            got,wanted=behavior(got),behavior(wanted)
        mismatch=difference(got,wanted,tolerant=requests[index]["op"]=="plan")
        if mismatch:
            Path("/tmp/wot-room-strategy-failing-case.json").write_text(json.dumps(requests[index]))
            raise AssertionError(dict(case=index, difference=mismatch))
    print("Pinned strategy data differential passed: %d cases" % len(expected))


if __name__ == "__main__":
    main()
