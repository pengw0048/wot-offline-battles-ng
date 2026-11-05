"""Atomic observation admission and relay parity with pinned BattleState."""
import argparse
import copy
import importlib
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--oracle",type=Path,required=True)
    parser.add_argument("--probe",type=Path,required=True)
    args=parser.parse_args()
    sys.path.insert(0,str(args.oracle/"server"))
    server=importlib.import_module("lan_battle_server")
    now=[100.]
    server.time.monotonic=lambda:now[0]
    players=[dict(id=1,team=1,alive=True,connected=True,participating=True,client_position=True,x=0.,z=0.),
        dict(id=2,team=1,alive=True,connected=True,participating=True,client_position=True,x=10.,z=0.),
        dict(id=3,team=2,alive=True,connected=True,participating=True,client_position=True,x=80.,z=0.)]
    bots=[dict(id=actor,team=1 if actor<3 else 2,alive=True,x=actor*10.,y=0.,z=0.) for actor in range(1,5)]
    manifest=[dict(id=row["id"],team=row["team"],health=100) for row in bots]
    contact=dict(observing_team=1,target_kind="bot",target_id=3,target_team=2,
        visible=True,fresh=True,time_left=10.,x=30.,y=0.,z=0.,health=100,max_health=100,
        class_tag="mediumTank",visible_by_bot_ids=[2,1],visible_by_player_ids=[2,1],
        shootable_by_bot_ids=[2,1],pending_lane_by_bot_ids=[1,2],threatened_bot_ids=[2,1])
    def message(rows=None,**extra):
        return dict(type="bot_observation",round_id=1,contacts=copy.deepcopy([contact] if rows is None else rows),**extra)
    scenarios=[]
    def scenario(messages):
        result=dict(op="observation",players=players,bots=bots,manifest=manifest,messages=messages)
        scenarios.append(result)
    scenario([dict(message=message()),dict(now=101,message=message()),
        dict(message=message([dict(contact,visible=False,fresh=False,time_left=0.,visible_by_bot_ids=[],visible_by_player_ids=[],shootable_by_bot_ids=[])])),
        dict(message=message([]))])
    for field,value in [("round_id",2),("round_id",True),("contacts",{}),("affordances",{}),
            ("player_vision_ranges",{}),("radio_links",{})]:
        bad=message();bad[field]=value
        scenario([dict(message=message()),dict(message=bad)])
    for field,value in [("observing_team",0),("target_id",True),("target_kind","player"),
            ("target_team",1),("time_left",15),("time_left","10"),("fresh",False),
            ("visible",False),("visible_by_bot_ids",[1,1]),("visible_by_bot_ids",[3]),
            ("visible_by_player_ids",[3]),("shootable_by_bot_ids",[3]),
            ("visible_by_player_ids",[1,2,3,4])]:
        bad=dict(contact);bad[field]=value
        # The first valid row must not commit when the second row is invalid.
        scenario([dict(message=message()),dict(now=103,message=message([dict(contact,x=99),bad]))])
    for field,value in [("threatened_bot_ids",[1,True,99,"2",2,2]),
            ("pending_lane_by_bot_ids",[2,True,99,"1"]),("threatened_bot_ids",{}),
            ("pending_lane_by_bot_ids",{})]:
        row=dict(contact);row[field]=value
        scenario([dict(message=message([row]))])
    recipients=[dict(kind="bot",id=1,time_left=8.,sources=[dict(kind="human",id=1,time_left=7.)]),
                dict(kind="human",id=2,time_left=6.,sources=[dict(kind="bot",id=2,time_left=5.)])]
    radio=dict(contact,radio_recipients=recipients)
    scenario([dict(message=message([radio],radio_links=[dict(kind="human",id=1,allies=[dict(kind="bot",id=1)])],
        player_vision_ranges=[dict(id=1,radius=445),dict(id=99,radius=300)])),
        dict(bots={"2":dict(alive=False)},players={"2":dict(alive=False)},message=message([radio]))])
    for mutation in ("duplicate","bad_source","long_source","dead_recipient","zero_lease"):
        row=copy.deepcopy(radio)
        if mutation=="duplicate":row["radio_recipients"].append(copy.deepcopy(recipients[0]))
        elif mutation=="bad_source":row["radio_recipients"][0]["sources"][0]["id"]=3
        elif mutation=="long_source":row["radio_recipients"][0]["sources"][0]["time_left"]=9
        elif mutation=="zero_lease":row["radio_recipients"][0]["time_left"]=0
        scenario([dict(message=message([row]),bots={"1":dict(alive=mutation!="dead_recipient")})])
    scenario([dict(bots={"1":dict(alive=False)},message=message()),
              dict(bots={"3":dict(alive=False)},message=message())])
    hidden=dict(contact,visible=False,fresh=False,time_left=0.,visible_by_bot_ids=[],visible_by_player_ids=[],shootable_by_bot_ids=[])
    scenario([dict(message=message([hidden])),dict(message=message([])),dict(tick=1,message=message())])

    def oracle(request):
        state=server.BattleState(clock=lambda:now[0])
        state.round_id=1;state.phase="battle";state.tick=300
        state.bot_authority_id=state.bot_manifest_authority_id=-1
        state.players={}
        for row in request["players"]:
            actor=server.Player(row["id"],None,("127.0.0.1",0))
            for key,value in row.items():
                if key!="id":setattr(actor,key,value)
            state.players[actor.player_id]=actor
        state.bot_manifest=copy.deepcopy(request["manifest"])
        state.bot_states={row["id"]:copy.deepcopy(row) for row in request["bots"]}
        original=state.bot_planner.report_contacts
        captured=[None]
        def contacts(raw,*positional,**keywords):
            captured[0]=copy.deepcopy(raw)
            return original(raw,*positional,**keywords)
        state.bot_planner.report_contacts=contacts
        canonical=None;results=[]
        for step in request["messages"]:
            now[0]=step.get("now",100.)
            state.tick=step.get("tick",state.tick)
            for kind in ("players","bots"):
                for actor,changes in step.get(kind,{}).items():
                    if kind=="players":
                        for key,value in changes.items():setattr(state.players[int(actor)],key,value)
                    else:state.bot_states[int(actor)].update(changes)
            captured[0]=None
            result=state.update_bot_observation(step.get("sender",-1),step["message"])
            deliveries=[]
            if captured[0] is not None:
                canonical=copy.deepcopy(step["message"])
                canonical["contacts"]=captured[0]
                canonical["player_vision_ranges"]=[dict(id=actor,radius=radius) for actor,radius in state.player_mission_view_ranges.items()]
                if "radio_links" in canonical:canonical["radio_links"]=result["radio_links"]
            if isinstance(result,dict):
                deliveries=[[actor.player_id,result] for actor in state.players.values() if actor.connected and actor.participating]
            memory=[dict(team=team,kind=key[0],id=key[1],contact=value)
                for team,contacts in sorted(state.bot_planner._contacts.items()) for key,value in sorted(contacts.items())]
            direct=lambda source:{str(actor):sorted([list(key) for key in values]) for actor,values in source.items()}
            results.append(json.loads(json.dumps(dict(accepted=bool(result),canonical=canonical,memory=memory,
                bot_spotted=direct(state.bot_spotted),player_spotted=direct(state.player_spotted),deliveries=deliveries))))
        return results
    payload="".join(json.dumps(request)+"\n" for request in scenarios)
    process=subprocess.run([str(args.probe)],input=payload,text=True,capture_output=True,check=True)
    actual=[json.loads(line) for line in process.stdout.splitlines()]
    assert len(actual)==len(scenarios),(len(actual),len(scenarios),process.stderr)
    def mismatch(got,wanted,path=""):
        if isinstance(got,dict) and isinstance(wanted,dict):
            for key in sorted(set(got)|set(wanted)):
                if key not in got:return path+"."+key,"missing",wanted[key]
                if key not in wanted:return path+"."+key,got[key],"extra"
                difference=mismatch(got[key],wanted[key],path+"."+key)
                if difference:return difference
        elif isinstance(got,list) and isinstance(wanted,list):
            if len(got)!=len(wanted):return path+".length",len(got),len(wanted)
            for i,(a,b) in enumerate(zip(got,wanted)):
                difference=mismatch(a,b,path+"[%d]"%i)
                if difference:return difference
        elif got!=wanted:return path,got,wanted
        return None
    for index,(request,got) in enumerate(zip(scenarios,actual)):
        wanted=oracle(request);difference=mismatch(got,wanted)
        if difference:raise AssertionError(dict(case=index,difference=difference))
    print("Pinned observation front-door differential passed: %d scenarios"%len(scenarios))


if __name__=="__main__":main()
