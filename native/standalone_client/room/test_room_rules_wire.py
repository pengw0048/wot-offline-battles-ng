"""Canonical projectile transactions must survive the actual Core wire boundary."""
import argparse
import copy
import json
from pathlib import Path
import subprocess
import sys

ROOM = Path(__file__).resolve().parent
sys.path[:0] = [str(ROOM), str(ROOM.parent / 'wire')]
from room_rules_test import actor, launch, send
from schema import SCHEMA, EVENT_KINDS


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', required=True)
    parser.add_argument('--resources', required=True)
    args = parser.parse_args()
    seen = set()
    count = 0

    cases = []
    def run(name, players, bots, operations, statuses=None):
        cases.append((name,copy.deepcopy(dict(players=players,bots=bots,operations=operations)),statuses))

    def check(name,operations,statuses,rows,baseline):
        nonlocal count
        assert baseline==rows, name+' owning/borrowed worker output differs'
        if statuses is None:
            statuses = [0] * len(operations)
        assert [row['status'] for row in rows] == statuses, (name, [(row['status'],row['code'],row['detail']) for row in rows])
        for step, row in enumerate(rows):
            messages = [delivery['message'] for delivery in row['deliveries']]
            messages.append(row['snapshot'])
            for message in messages:
                message = copy.deepcopy(message)
                message.pop('_room_sections', None)
                try:
                    encoded = SCHEMA.encode(message)
                    decoded = SCHEMA.decode(encoded)
                except Exception as error:
                    raise AssertionError('%s step=%d type=%s: %s' %
                                         (name, step, message['type'], error)) from error
                assert decoded == message, (name, message['type'], decoded, message)
                seen.update(event['kind'] for event in message.get('events', []))
                count += 1
        return rows

    def terminal(pid, outcome='impact', direct=None, **changes):
        value = dict(type='projectile_resolve', round_id=1, authority_epoch=1,
                     projectile_id=pid, base_checked_ms=0, outcome=outcome,
                     resolved_time_ms=100, checked_distance=10., piercing_loss=0.,
                     penetration_factor=1., impact=[0., 1., 10.] if outcome == 'impact' else None,
                     direct=direct, splash=[], destructibles=[])
        value.update(changes)
        return value

    def effect(kind, identifier, damage=100, **changes):
        value = dict(target_kind=kind, target_id=identifier, damage=damage,
                     shot_result=2, x=0., y=1., z=10.)
        value.update(changes)
        return value

    for bot in (False, True):
        for victim_bot in (False, True):
            shooter = actor(1, bot, 1)
            shooter['fire_seq'] = int(bot)
            victim = actor(2, victim_bot, 2)
            players = [row for row, flag in ((shooter, bot), (victim, victim_bot)) if not flag]
            bots = [row for row, flag in ((shooter, bot), (victim, victim_bot)) if flag]
            pid = '1:%s:1:1' % ('b' if bot else 'p')
            shot = launch(bot=bot)
            shot['source_shot']['shell']['caliber']=100.123456789 if bot else 100.
            proof = dict((key,copy.deepcopy(value)) for key,value in shot.items()
                         if key not in ('type','round_id','authority_epoch','launch_time_us','launch_pose'))
            proof.update(projectile_id=pid,source_vehicle=shooter['vehicle'],
                         burst_group_seq=1,burst_index=0,burst_count=1)
            def dispatch(message):
                message = copy.deepcopy(message)
                if not bot and message['type'] != 'projectile_launch':
                    for item in message['cursors'] if message['type']=='projectile_progress' else [message]:
                        item['launch_proof'] = copy.deepcopy(proof)
                return send(message,bot,1)
            target_kind = 'bot' if victim_bot else 'player'
            prefix = '%s-to-%s' % ('bot' if bot else 'human', target_kind)
            progress = dict(type='projectile_progress', round_id=1, authority_epoch=1,
                cursors=[dict(projectile_id=pid, base_checked_ms=0, checked_through_ms=50,
                    checked_distance=5., piercing_loss=0., penetration_factor=1., destructibles=[])])
            run(prefix+'-progress-miss', players, bots, [dispatch(shot), dispatch(progress),
                dispatch(terminal(pid, 'miss', base_checked_ms=50)),
                dispatch(terminal(pid, 'miss', base_checked_ms=50))])
            for outcome in ('expired', 'impact'):
                run(prefix+'-'+outcome, players, bots, [dispatch(shot), dispatch(terminal(pid, outcome))])
            for damage in (0, 1, 1000):
                hit = effect(target_kind, 2, damage, potential_damage=1000,
                             structural_armor_hit=True, damage_sticker=2**64-1)
                run(prefix+'-damage-'+str(damage), players, bots,
                    [dispatch(shot), dispatch(terminal(pid, direct=hit)), dict(op='finish', winner=1)])
            bounce = dict(type='projectile_ricochet', round_id=1, authority_epoch=1,
                projectile_id=pid, base_checked_ms=0, resolved_time_ms=100, checked_distance=10.,
                piercing_loss=0., penetration_factor=1., impact=[0.,1.,10.],
                segment_origin=[0.,1.,10.], segment_velocity=[100.,0.,0.],
                base_penetration_multiplier=.75,
                direct=effect(target_kind,2,0,shot_result=0), destructibles=[])
            run(prefix+'-ricochet', players, bots, [dispatch(shot), dispatch(bounce),
                dispatch(terminal(pid,'miss',base_checked_ms=100,resolved_time_ms=200,
                                  checked_distance=20.))])
            critical = dict(devices=[dict(name='engineHealth',hp=0.,max_hp=100.,state='destroyed')],
                destroyed=['engineHealth'], crew_ko=[], crew_roster=['commander'],
                fire=False, ammo_rack_death=False, events=[])
            fitted = dict(devices=[dict(name='engineHealth',max_hp=100.)],crew_roster=['commander'])
            victim['effective_params'] = dict(critical=fitted)
            victim['critical_profile'] = fitted
            hit = effect(target_kind,2,50,critical=critical,critical_target_base_revision=0,
                critical_target_ack_seq=0,hull_damage=50,
                critical_delta=dict(devices=[dict(name='engineHealth',hp_loss=100.)],crew_ko=[],ignite=False))
            run(prefix+'-critical', players, bots, [dispatch(shot), dispatch(terminal(pid,direct=hit))])
            bad = effect(target_kind,2,100,damage_sticker=-1)
            run(prefix+'-invalid-sticker',players,bots,[dispatch(shot),dispatch(terminal(pid,direct=bad))], [0,1])
    for bot in (False, True):
        shooter=actor(1,bot,1);shooter['fire_seq']=int(bot)
        human_target=actor(2,False,2);bot_target=actor(3,True,2)
        players=([shooter] if not bot else [])+[human_target]
        bots=([shooter] if bot else [])+[bot_target]
        shot=launch(bot=bot);shot.update(is_he=True,splash_radius=5.)
        shot['source_shot']['shell'].update(kind='HIGH_EXPLOSIVE',explosionRadius=5.)
        pid='1:%s:1:1'%('b' if bot else 'p')
        proof=dict((key,copy.deepcopy(value)) for key,value in shot.items()
                   if key not in ('type','round_id','authority_epoch','launch_time_us','launch_pose'))
        proof.update(projectile_id=pid,source_vehicle=shooter['vehicle'],burst_group_seq=1,burst_index=0,burst_count=1)
        factors=dict((name,1.2 if name in ('reload','aim_time','bloom_move','bloom_rotation','bloom_turret','dispersion') else .8)
                     for name in ('mobility','traverse','turret_speed','vision','speed','reload','aim_time','bloom_move','bloom_rotation','bloom_turret','dispersion'))
        hit=effect('player',2,30,high_explosive=True,stun_end_server_time_ms=11000,stun_factors=factors)
        splash=effect('bot',3,40,high_explosive=True,target_x=0.,target_y=0.,target_z=0.,
                      stun_end_server_time_ms=11000,stun_factors=factors)
        resolved=terminal(pid,direct=hit,splash=[splash])
        if not bot:resolved['launch_proof']=proof
        run('HE-stun-splash-'+str(bot),players,bots,[send(shot,bot,1),send(resolved,bot,1),
            dict(op='advance',tick=360,time=12000,dt=1.)])
        wreck=copy.deepcopy(bot_target);wreck.update(alive=False,health=0)
        resolved=terminal(pid,wreck_hit=dict(target_kind='bot',target_id=3),hit_vehicle=True)
        if not bot:resolved['launch_proof']=proof
        run('wreck-'+str(bot),[shooter] if not bot else [],([shooter] if bot else [])+[wreck],
            [send(shot,bot,1),send(resolved,bot,1)])
        tree=dict(destructible_kind='tree',chunk_id=7,item_index=3,x=0.,y=0.,z=10.,fall_yaw=0.,speed=0.,is_shot=True)
        resolved=terminal(pid,'miss',destructibles=[tree])
        if not bot:resolved['launch_proof']=proof
        run('destructible-'+str(bot),[shooter] if not bot else [],[shooter] if bot else [],
            [send(shot,bot,1),send(resolved,bot,1)])
    human = actor()
    run('human-expiry',[human],[],[send(launch(bot=False),False,1),dict(op='advance',tick=1500,time=50000)])
    dead = actor(bot=True)
    run('deferred-bot-death',[],[dead],[send(launch()),dict(op='actor',kind='bot',id=1,changes=dict(alive=False))],[2,0])
    names=('engineHealth','ammoBayHealth','fuelTankHealth','radioHealth','leftTrackHealth',
           'rightTrackHealth','gunHealth','turretRotatorHealth','surveyingDeviceHealth')
    drowning=dict(devices=[dict(name=name,hp=0.,max_hp=100.,state='destroyed') for name in names],
        destroyed=list(names),crew_roster=['commander'],crew_ko=['commander'],
        fire=False,ammo_rack_death=False,events=[])
    environment=dict(type='player_environment',round_id=1,authority_epoch=1,sample_seq=1,
        observations=[dict(player_id=2,input_seq=1,level=2,drowning_critical=drowning)])
    for pushed in (False,True):
        operations=[send(environment)]
        if pushed:operations.append(dict(op='push',event=dict(attacker_kind='player',attacker_id=1,
                                              victim_kind='player',victim_id=2)))
        operations.append(dict(op='advance',tick=301,dt=10.001))
        run('drowning-'+str(pushed),[actor(1),actor(2,team=2)],[],operations)
    shot=launch(bot=False)
    proof=dict((key,copy.deepcopy(value)) for key,value in shot.items()
               if key not in ('type','round_id','authority_epoch'))
    proof.update(projectile_id='1:p:1:1',source_vehicle='ussr:R11_MS-1',
                 burst_group_seq=1,burst_index=0,burst_count=1)
    observation=dict(contacts=[dict(observing_team=1,target_kind='human',target_id=2,
        visible=True,fresh=True,time_left=1.,visible_by_player_ids=[3],visible_by_bot_ids=[],
        radio_recipients=[dict(kind='human',id=1,sources=[dict(kind='human',id=3,time_left=1.)])])])
    resolved=terminal('1:p:1:1',direct=effect('player',2,100),launch_proof=proof)
    run('radio-assist',[actor(1),actor(2,team=2),actor(3)],[],
        [dict(op='observe',message=observation),send(shot,False,1),send(resolved,False,1)])
    requests=[]
    for name,request,statuses in cases:
        requests.extend((request,dict(request,owned_worker=True)))
    result=subprocess.run([args.probe,args.resources],
        input=''.join(json.dumps(request)+'\n' for request in requests),
        capture_output=True,text=True,check=True)
    publications=[json.loads(line) for line in result.stdout.splitlines()]
    assert len(publications)==2*len(cases)
    for index,(name,request,statuses) in enumerate(cases):
        check(name,request['operations'],statuses,publications[2*index],publications[2*index+1])
    required = {'shot','bot_shot','projectile_impact','projectile_ricochet','hit','bot_hit',
                'bot_human_hit','bot_bot_hit','vehicle_statistics','battle_result','projectile_launch_failed',
                'destructible','stun','health','environment_critical','detection','assist'}
    assert required == set(EVENT_KINDS)-{'authority','bot_manifest'}, 'Rules event coverage must track the complete schema'
    assert required <= seen, required-seen
    print('Rules/Core wire lifecycle passed: %d encoded and decoded publications; %d event variants' % (count,len(seen)))


if __name__ == '__main__':
    main()
