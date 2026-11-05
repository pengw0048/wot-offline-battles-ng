"""Portable canonical rule lifecycles. These tests do not prove Windows gameplay."""
import copy
import json
import contextlib
import importlib
import io
import random
import sys
import types
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOM = Path(__file__).resolve().parent


def actor(identifier=1, bot=False, team=1):
    return dict(id=identifier, player_id=identifier, team=team, vehicle='ussr:R11_MS-1',
                name='Tester', account_key='account', connected=True, participating=True,
                client_position=True, world_pose=True, x=0., y=0., z=0., health=1000,
                max_health=1000, alive=True, fire_seq=0, input_seq=1, input_processed_seq=1,
                combat_revision=0, combat_base_revision=0, combat_ack_seq=0,
                combat_fire_elapsed=0., combat_fire_timer=0., critical={},
                vehicle_tier=1, vehicle_class='lightTank')


def launch(sequence=1, bot=True, shooter=1):
    out = dict(type='projectile_launch', round_id=1, authority_epoch=1,
               shooter_kind='bot' if bot else 'player', shooter_id=shooter,
               shot_seq=sequence, shell_index=0, origin=[0., 1., 0.],
               velocity=[0., 0., 100.], gravity=9.81, max_distance=700.,
               max_time_ms=20000, is_he=False, splash_radius=0., penetration_factor=1.,
               source_shot=dict(speed=100., gravity=9.81, maxDistance=700.,
                                piercingPower=[100., 100.], deadeye=False,
                                shell=dict(kind='ARMOR_PIERCING', caliber=100.,
                                           damage=[100., 50.], explosionRadius=0.)))
    if bot:
        out.update(launch_time_us=200000, launch_pose=[0., 0., 0., 0., 0., 0.])
    else:
        out.update(fire_intent_seq=sequence, fire_input_seq=1,
                   launch_server_time_ms=10000, range_origin=[0., 0., 0.])
    return out


def send(message, worker=True, sender=-1):
    return dict(op='dispatch', message=message, worker=worker, sender=sender)


class RoomRules(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        pin = os.environ.get('WOT_FORK_SOURCE')
        if not pin:
            raise unittest.SkipTest('WOT_FORK_SOURCE supplies the existing native contact law')
        cls.folder = tempfile.TemporaryDirectory(prefix='room-rules-')
        cls.binary = Path(cls.folder.name) / 'rules'
        sources = [p for p in ROOM.glob('room_rules*.cpp') if not p.name.endswith(('_test_host.cpp','_probe.cpp'))]
        prebuilt=os.environ.get('WOT_ROOM_TEST_BIN')
        if prebuilt:
            cls.binary=Path(prebuilt)/'rules'
        else:
            subprocess.run(['c++', '-std=c++17', '-O0', '-ffp-contract=off', '-Wall', '-Wextra',
                            '-Werror', '-fsanitize=address,undefined', '-I'+str(Path(pin)/'native'), str(ROOM/'room_rules_test_host.cpp'),
                            str(ROOM/'room_json.cpp'), str(ROOM/'room_lobby_policy.cpp'), *(str(p) for p in sources),
                            str(ROOM.parent/'bigworld/stock_native_bot_combat.cpp'),
                            str(Path(pin)/'native/offline_contact_roster.cpp'),
                            '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def run_rules(self, **scenario):
        p = subprocess.run([str(self.binary)], input=json.dumps(scenario)+'\n',
                           capture_output=True, text=True, check=True)
        result = json.loads(p.stdout)
        self.assertNotIn('error', result)
        # Validate every exercised canonical producer at the external boundary.
        sys.path.insert(0,str(ROOM.parent/'wire'))
        from schema import SCHEMA
        events=[]
        for event in copy.deepcopy(result['events']):
            if event.get('kind')=='environment_push' or event.get('type')=='player_destructible_contact':
                continue
            for key in list(event):
                if key.startswith('_room_'):event.pop(key)
            event['event_id']='1:300:%d'%len(events)
            events.append(event)
        for offset in range(0,len(events),256):
            SCHEMA.encode(dict(type='events',round_id=1,server_tick=300,authority_epoch=1,
                server_time_ms=10000,worker_status='connected',worker_failure_reason='',
                events=events[offset:offset+256]))
        for outcome in result['outcomes']:
            if isinstance(outcome,dict):
                for reply in outcome.get('replies',[]):SCHEMA.encode(reply)
        for receipt in result['receipts']:SCHEMA.encode(receipt)
        return result

    def test_pending_launch_keeps_frozen_payload_and_waits_for_edge(self):
        bot = actor(bot=True)
        original = launch()
        changed = dict(original, velocity=[99., 0., 0.])
        result = self.run_rules(bots=[bot], operations=[send(original), send(changed),
            dict(op='actor', kind='bot', id=1,
                 changes=dict(fire_seq=1, burst_group_seq=1, burst_count=1,
                              burst_next_index=1, burst_shell_index=0))])
        self.assertEqual([2, 1], [o['status'] for o in result['outcomes']])
        self.assertEqual(1, len(result['projectiles']))
        self.assertEqual([0., 0., 100.], result['projectiles'][0]['velocity'])
        self.assertEqual(1, result['statistics']['bot:1']['shots_fired'])

    def test_admitted_fire_edge_survives_death_but_new_edge_does_not(self):
        bot = actor(bot=True)
        ops = [dict(op='actor', kind='bot', id=1,
                    changes=dict(fire_seq=1, burst_group_seq=1, burst_count=1,
                                 burst_next_index=1, burst_shell_index=0)),
               dict(op='actor', kind='bot', id=1, changes=dict(alive=False)),
               send(launch()), send(launch()), send(launch(2)), send(launch(2))]
        result = self.run_rules(bots=[bot], operations=ops)
        self.assertEqual(1, len(result['projectiles']))
        failures = [e for e in result['events'] if e['kind']=='projectile_launch_failed']
        self.assertEqual(1, len(failures))
        self.assertEqual(2, failures[0]['shot_seq'])

    def test_progress_and_terminal_seal_identity(self):
        bot = actor(bot=True); bot['fire_seq'] = 1
        message = launch()
        terminal = dict(type='projectile_resolve', round_id=1, authority_epoch=1,
                        projectile_id='1:b:1:1', base_checked_ms=0, outcome='miss',
                        resolved_time_ms=500, checked_distance=50., piercing_loss=0.,
                        penetration_factor=1., impact=None, direct=None, splash=[], destructibles=[])
        result = self.run_rules(bots=[bot], operations=[send(message), send(terminal),
            send(terminal), send(dict(terminal, resolved_time_ms=600))])
        self.assertEqual([0, 0, 0, 1], [o['status'] for o in result['outcomes']])
        self.assertFalse(result['projectiles'])
        self.assertEqual(1, sum(e['kind']=='projectile_impact' for e in result['events']))

    def test_bad_terminal_is_local_and_releases_capacity(self):
        bot = actor(bot=True); bot['fire_seq'] = 1
        bad = dict(type='projectile_resolve', round_id=1, authority_epoch=1,
                   projectile_id='1:b:1:1', base_checked_ms=1, outcome='miss',
                   resolved_time_ms=500, checked_distance=50., piercing_loss=0.,
                   penetration_factor=1., impact=None, direct=None, splash=[], destructibles=[])
        result = self.run_rules(bots=[bot], operations=[send(launch()), send(bad)])
        self.assertEqual(1, result['outcomes'][-1]['status'])
        self.assertFalse(result['projectiles'])
        self.assertEqual('expired', result['events'][-1]['outcome'])

    def test_capture_round_robin_and_owner_in_circle(self):
        players = [actor(i, team=2) for i in range(1, 6)]
        players.append(actor(6, team=1))
        config = dict(capture_bases={'1': {'x': 0., 'z': 0., 'radius': 50.}})
        result = self.run_rules(players=players, config=config, tick=300,
            operations=[dict(op='advance', tick=tick, dt=1.) for tick in (330, 360)])
        self.assertEqual(6, result['rules']['bases']['1']['points'])
        self.assertFalse(result['rules']['bases']['1']['stopped'])
        self.assertEqual([2, 1, 1, 1, 1],
                         [result['statistics']['player:%d'%i]['capture_points'] for i in range(1, 6)])

    def test_equipment_invalid_payload_is_terminal_and_next_sequence_proceeds(self):
        player = actor()
        result = self.run_rules(players=[player], operations=[
            send(dict(type='equipment_intent', round_id=1, intent_seq=1), False, 1),
            send(dict(type='equipment_intent', round_id=1, intent_seq=2), False, 1)])
        self.assertEqual([0, 0], [o['status'] for o in result['outcomes']])
        self.assertEqual(2, result['players'][0]['equipment_intent_result']['intent_seq'])
        self.assertFalse(result['players'][0]['equipment_intent_result']['accepted'])

    def test_contact_verdict_waits_for_publication_and_keeps_selective_ack(self):
        player = actor()
        player.update(destructible_contact_seq=2, destructible_contact_resolved_seq=0,
                      destructible_contacts=[dict(seq=1,token=[[7,3,None]],x=0.,y=0.,z=0.,yaw=0.),
                                             dict(seq=2,token=[[7,4,None]],x=0.,y=0.,z=0.,yaw=0.)])
        verdict = dict(type='player_destructible_contact_result',round_id=1,
                       player_id=1,contact_seq=2,accepted=True,token=[[7,4,None]])
        publication = dict(type='destructible',round_id=1,destructible_kind='tree',
                           chunk_id=7,item_index=4,x=0.,y=0.,z=0.,fall_yaw=0.,speed=0.,is_shot=True)
        result = self.run_rules(players=[player],operations=[send(verdict),send(publication)])
        self.assertEqual([2,0],[row['status'] for row in result['outcomes']])
        self.assertEqual(0,result['players'][0]['destructible_contact_resolved_seq'])
        self.assertEqual([2],result['players'][0]['destructible_contact_resolved_seqs'])
        self.assertEqual([1],[r['seq'] for r in result['players'][0]['destructible_contacts']])

    def test_human_ram_freezes_probe_and_applies_both_halves_once(self):
        players = [actor(1,team=1),actor(2,team=2)]
        profiles = [dict(id=p['id'],vehicle=p['vehicle'],mass=5730.,shape=[1.,2.,-1.,1.],
                         ram_profile=dict(spall_coefficient=1.,ramming_bonus=0.)) for p in players]
        operations=[]
        for stamp in (1000000,1100000):
            for identifier,z,speed in ((1,0.,10.),(2,3.,0.)):
                operations.append(dict(op='actor',kind='player',id=identifier,motion_time_us=stamp,
                    changes=dict(z=z,yaw=0.,speed=speed),publication=dict(pose_time_us=stamp)))
        operations.append(dict(op='advance',tick=302,dt=1./30.))
        pending=self.run_rules(players=players,profiles=profiles,operations=operations)
        self.assertEqual(1,len(pending['human_ram_probes']))
        proof=dict(seq=1,first_id=1,second_id=2,available=True,armor_first=10.,armor_second=10.)
        operations.extend([dict(op='checkpoint',message=dict(round_id=1,bots=[],human_ram_armors=[proof])),
                           dict(op='advance',tick=303,dt=1./30.),
                           dict(op='checkpoint',message=dict(round_id=1,bots=[],human_ram_armors=[proof])),
                           dict(op='advance',tick=304,dt=1./30.)])
        result=self.run_rules(players=players,profiles=profiles,operations=operations)
        self.assertEqual([967,967],[p['health'] for p in result['players']])
        self.assertEqual(2,sum(e.get('source')=='ram' for e in result['events']))
        self.assertFalse(result['human_ram_probes'])

    def test_finish_uses_round_terminal_for_active_flights(self):
        result=self.run_rules(players=[actor()],operations=[send(launch(bot=False),False,1),
            dict(op='finish',winner=1,reason='team_eliminated')])
        self.assertEqual([],result['projectiles'])
        self.assertEqual(['shot','battle_result'],[e['kind'] for e in result['events']])
        sys.path.insert(0,str(ROOM.parent/'wire'))
        from schema import SCHEMA
        message=dict(type='events',round_id=1,server_tick=300,authority_epoch=1,server_time_ms=10000,worker_status='connected',worker_failure_reason='',events=copy.deepcopy(result['events']))
        for index,event in enumerate(message['events']):event['event_id']=str(index+1)
        SCHEMA.encode(message)

    def test_receipt_finish_and_ack_commit_only_after_durable_store(self):
        result=self.run_rules(players=[actor()],operations=[
            dict(op='store',okay=False),dict(op='finish',winner=1,reason='team_eliminated'),
            dict(op='store',okay=True),dict(op='finish',winner=1,reason='team_eliminated'),
            dict(op='finish',winner=1,reason='team_eliminated'),
            dict(op='store',okay=False),send(dict(type='battle_receipt_ack',receipt_id='test-room:1:1'),False,1),
            dict(op='store',okay=True),send(dict(type='battle_receipt_ack',receipt_id='test-room:1:1'),False,1)])
        self.assertEqual([False,True,False],[r for r in result['outcomes'] if isinstance(r,bool)])
        self.assertEqual([1,1,0,0],[len(r) for r in result['persistence']])
        self.assertEqual([],result['receipts'])
        self.assertEqual(1,len([e for e in result['events'] if e['kind']=='battle_result']))
        self.assertEqual([1,0],[r['status'] for r in result['outcomes'] if isinstance(r,dict)])

    def test_worker_failure_finishes_operations_without_minting_receipts(self):
        bot=actor(bot=True)
        result=self.run_rules(players=[actor()],bots=[bot],operations=[send(launch()),
            dict(op='finish',winner=0,reason='worker_disconnected',record_receipts=False)])
        self.assertEqual([],result['persistence'])
        self.assertEqual([],result['receipts'])
        self.assertEqual(1,len([e for e in result['events'] if e['kind']=='projectile_launch_failed']))
        self.assertEqual('worker_disconnected',result['battle_result']['reason'])

    def test_receipt_finish_is_once_and_outlives_reset(self):
        player = actor()
        result = self.run_rules(players=[player], operations=[
            dict(op='finish', winner=1, reason='battle_timeout'),
            dict(op='finish', winner=1, reason='battle_timeout')])
        self.assertEqual([True, False], result['outcomes'])
        self.assertEqual(1, len(result['receipts']))
        self.assertEqual('test-room:1:1', result['receipts'][0]['receipt_id'])
        self.assertEqual(1, len(result['receipts'][0]['public_results']))


class PinnedRuleOracle(RoomRules):
    @classmethod
    def setUpClass(cls):
        pin = os.environ.get('WOT_FORK_SOURCE')
        if not pin:
            raise unittest.SkipTest('WOT_FORK_SOURCE is required for the pinned oracle')
        super().setUpClass()
        cls.pin = Path(pin)
        staged = Path(cls.folder.name)/'server'
        subprocess.run([sys.executable, str(ROOM.parent/'wire/stage_server.py'),
                        str(cls.pin/'server'), str(staged)], check=True)
        sys.path[:0] = [str(staged), str(cls.pin/'tests'),
                       str(cls.pin/'src/res/scripts/client')]
        cls.server = importlib.import_module('lan_battle_server')
        cls.fixture = importlib.import_module('test_port_0922_server_projectiles')

    def native_players(self, state):
        result = []
        for player in state.players.values():
            row = state._public_player(player)
            row.update(connected=player.connected, client_position=player.client_position,
                       input_processed_seq=player.input_processed_seq,
                       effective_params=copy.deepcopy(player.effective_params),
                       account_key='account')
            result.append(row)
        return result

    def test_capture_matches_randomized_pinned_circles_and_liveness(self):
        rng = random.Random(1513)
        for case in range(25):
            state = self.server.BattleState(map_name='04_himmelsdorf')
            state.client_build = self.server.CLIENT_BUILD_0922
            state.phase = 'battle'; state.tick = 300
            state.capture_bases = {1: [dict(x=0., z=0., radius=30.)],
                                   2: [dict(x=500., z=0., radius=50.)]}
            for identifier in range(1, 9):
                player = self.fixture._player(identifier, team=rng.choice((1,2)),
                                               x=rng.choice((0., 29.99, 30.01, 450., 500.)))
                player.z = rng.choice((0., 5., 50.)); player.alive = rng.choice((True,True,False))
                player.client_position = rng.choice((True,True,False))
                state.players[identifier] = player
            players = self.native_players(state)
            ticks = [330, 360, 390, 420]
            with contextlib.redirect_stdout(io.StringIO()):
                for tick in ticks:
                    state.tick = tick; state._update_capture()
            result = self.run_rules(players=players,
                resources=dict(capture_bases=state.capture_bases),
                operations=[dict(op='advance', tick=tick, dt=1.) for tick in ticks])
            self.assertEqual(state.rules_state['bases'], result['rules']['bases'], case)
            for player in players:
                identifier = player['id']
                expected = state._statistics_row('player', identifier)['capture_points']
                actual = result['statistics']['player:%s'%identifier].get('capture_points', 0)
                self.assertEqual(expected, actual, (case, identifier))

    def test_player_launch_and_progress_match_pinned_wire(self):
        for origin in ([0.,1.,0.], [0.0000005, 1.0000015, -0.0000005],
                       [100.1234567, 10.9999995, -400.125]):
            state = self.fixture._state()
            message = self.fixture._launch(origin=origin)
            self.assertTrue(self.fixture._launch_authority(state, message))
            # The helper admitted input before launch; restore the pre-shot owner frontier.
            players = self.native_players(state); players[0]['fire_seq'] = 0
            progress = self.fixture._progress(state, '1:p:1:1', 100, 10.)
            self.assertTrue(state.progress_projectiles(1, progress))
            result = self.run_rules(players=players, tick=300,
                operations=[send(message, False, 1), send(progress, False, 1)])
            self.assertEqual([0,0], [o['status'] for o in result['outcomes']])
            self.assertEqual(state._projectile_snapshot(), result['projectiles'])

    def test_hull_terminal_and_duplicate_match_pinned_canonical_damage(self):
        for damage in (0, 1, 100, 999, 1000, 5000):
            state = self.fixture._state()
            message = self.fixture._launch()
            self.assertTrue(self.fixture._launch_authority(state, message))
            players = self.native_players(state); players[0]['fire_seq'] = 0
            terminal = self.fixture._resolve(state, '1:p:1:1',
                                              direct=self.fixture._effect(damage=damage))
            self.assertTrue(state.resolve_projectile(1, terminal))
            self.assertTrue(state.resolve_projectile(1, terminal))
            result = self.run_rules(players=players, tick=300,
                operations=[send(message,False,1),send(terminal,False,1),send(terminal,False,1)])
            self.assertEqual([0,0,0], [o['status'] for o in result['outcomes']], damage)
            self.assertEqual(state.players[2].health, result['players'][1]['health'])
            self.assertEqual(state.players[2].alive, result['players'][1]['alive'])
            for kind,identifier in (('player',1),('player',2)):
                expected = state._statistics_row(kind,identifier)
                actual = result['statistics']['%s:%s'%(kind,identifier)]
                for name in ('shots_fired','shots_hit','shots_penetrated','damage_dealt',
                             'damage_received','kills','damage_blocked'):
                    self.assertEqual(expected[name], actual.get(name,0), (damage,name))

    def test_critical_delta_rebases_repairs_and_preserves_crew_hull_health(self):
        for hp,status,loss,crew_hit in ((80.,'normal',60.,False),(25.,'destroyed',60.,False),
                                      (50.,'critical',10.,False),(100.,'normal',0.00001,False),
                                      (100.,'normal',20.,True)):
            state=self.fixture._state(); target=state.players[2]
            target.critical=dict(devices=[dict(name='engineHealth',hp=hp,max_hp=100.,state=status)],
                destroyed=['engineHealth'] if status=='destroyed' else [],crew_ko=[],
                crew_roster=['commander'],fire=False,ammo_rack_death=False,events=[])
            target.critical_revision=4; target.critical_report_base_revision=3;target.critical_ack_seq=2
            message=self.fixture._launch();self.assertTrue(self.fixture._launch_authority(state,message))
            players=self.native_players(state);players[0]['fire_seq']=0
            proposal=dict(devices=[dict(name='engineHealth',hp=0.,max_hp=100.,state='destroyed')],
                destroyed=['engineHealth'],crew_ko=['commander'] if crew_hit else [],
                crew_roster=['commander'],fire=False,ammo_rack_death=False,events=[])
            effect=self.fixture._effect(damage=50,hull_damage=50,critical=proposal,
                critical_target_base_revision=1,critical_target_ack_seq=0,
                critical_delta=dict(devices=[dict(name='engineHealth',hp_loss=loss)],
                    crew_ko=['commander'] if crew_hit else [],ignite=False))
            terminal=self.fixture._resolve(state,'1:p:1:1',direct=effect)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertTrue(state.resolve_projectile(1,terminal))
            result=self.run_rules(players=players,tick=300,operations=[send(message,False,1),send(terminal,False,1)])
            self.assertEqual(0,result['outcomes'][-1]['status'],result['outcomes'])
            actual=result['players'][1]
            self.assertEqual(target.health,actual['health'])
            self.assertEqual(target.alive,actual['alive'])
            self.assertEqual(target.critical,actual['critical'])
            self.assertEqual(target.critical_revision,actual['critical_revision'])
            self.assertEqual(target.critical_report_base_revision,actual['critical_base_revision'])
            self.assertEqual(target.critical_ack_seq,actual['critical_ack_seq'])

    def test_governor_activation_encoding_and_bad_intent_frontier_match(self):
        equipment=importlib.import_module('gui.mods.offline_lan_0922.equipment_mechanics')
        descriptor=types.SimpleNamespace(name='removedRpmLimiter',id=(0,12),compactDescr=3083,
            tags=('trigger',),reuseCount=0,cooldownSeconds=0,enginePowerFactor=1.1,
            engineHpLossPerSecond=1.5)
        contract=equipment.project_equipment(descriptor)
        for extra,active,selected in ((1,True,None),(0,False,None),(0,True,None),
                                      (2,True,None),(1,True,'unknown_extra')):
            state=self.fixture._state(players=1);player=state.players[1]
            player.effective_params['equipment']=[contract]
            self.assertTrue(state._install_player_equipments(player))
            players=self.native_players(state)
            intent=dict(type='equipment_intent',round_id=1,intent_seq=1,equipment_id=12,
                activation_code=(extra<<16)+12,selected=selected,requested_active=active)
            self.assertTrue(state.submit_equipment_intent(1,intent))
            result=self.run_rules(players=players,initialize=True,tick=300,
                operations=[send(intent,False,1),send(intent,False,1)])
            actual=result['players'][0]
            self.assertEqual(player.equipment_intent_result,actual['equipment_intent_result'])
            self.assertEqual(player.equipment_states[0].active,actual['equipment_states'][0]['active'])
            self.assertEqual(player.equipment_states[0].uses_left,actual['equipment_states'][0]['usesLeft'])

    def test_active_consumables_preserve_fitted_crew_and_critical_progress(self):
        equipment=importlib.import_module('gui.mods.offline_lan_0922.equipment_mechanics')
        for name,kind in [('largeRepairkit','repairkit'),('largeMedkit','medkit'),('handExtinguishers','extinguisher')]:
            state=self.fixture._state(players=1);player=state.players[1]
            descriptor=types.SimpleNamespace(name=name,id=(0,12),compactDescr=3083,
                tags=(kind,),reuseCount=0,cooldownSeconds=90.,repairAll=kind!='extinguisher')
            contract=equipment.project_equipment(descriptor)
            self.assertEqual(kind,contract['kind'])
            player.effective_params['equipment']=[contract]
            roster=player.effective_params['critical']['crew_roster']
            player.critical=dict(devices=[dict(name='engineHealth',hp=0.,max_hp=100.,state='destroyed')],
                destroyed=['engineHealth'],crew_ko=['commander'],crew_roster=roster,
                fire=kind=='extinguisher',ammo_rack_death=False,events=[])
            self.assertTrue(state._install_player_equipments(player));players=self.native_players(state)
            intent=dict(type='equipment_intent',round_id=1,intent_seq=1,equipment_id=12,
                activation_code=65548,selected=None,requested_active=None)
            with contextlib.redirect_stdout(io.StringIO()):self.assertTrue(state.submit_equipment_intent(1,intent))
            actual=self.run_rules(players=players,initialize=True,tick=300,operations=[send(intent,False,1)])['players'][0]
            self.assertEqual(player.equipment_intent_result,actual['equipment_intent_result'],name)
            self.assertEqual(player.critical,actual['critical'],name)
            self.assertEqual(player.critical_revision,actual['critical_revision'],name)
            self.assertEqual(player.equipment_states[0].uses_left,actual['equipment_states'][0]['usesLeft'],name)

    def test_radio_assisted_crew_knockout_records_kill_without_hull_damage(self):
        state=self.fixture._state(players=3);target=state.players[2]
        roster=target.effective_params['critical']['crew_roster']
        target.critical=dict(devices=[],destroyed=[],crew_ko=[],crew_roster=roster,
            fire=False,ammo_rack_death=False,events=[])
        message=self.fixture._launch();self.assertTrue(self.fixture._launch_authority(state,message))
        players=self.native_players(state);players[0]['fire_seq']=0
        state.player_spotted={3:{('player',2)}}
        state.radio_assist_sources={(('player',1),('player',2)):{('player',3):self.server.time.monotonic()+1.}}
        state._commit_detections()
        proposal=dict(target.critical,crew_ko=roster)
        effect=self.fixture._effect(damage=0,hull_damage=0,critical=proposal,
            critical_target_base_revision=0,critical_target_ack_seq=0,
            critical_delta=dict(devices=[],crew_ko=roster,ignite=False))
        terminal=self.fixture._resolve(state,'1:p:1:1',direct=effect)
        with contextlib.redirect_stdout(io.StringIO()):self.assertTrue(state.resolve_projectile(1,terminal))
        observation=dict(contacts=[dict(observing_team=1,target_kind='human',target_id=2,
            visible=True,fresh=True,time_left=1.,visible_by_player_ids=[3],visible_by_bot_ids=[],
            radio_recipients=[dict(kind='human',id=1,sources=[dict(kind='human',id=3,time_left=1.)])])])
        result=self.run_rules(players=players,tick=300,operations=[dict(op='observe',message=observation),
            send(message,False,1),send(terminal,False,1),dict(op='finish',winner=1,reason='team_eliminated')])
        receipt=next(r for r in result['receipts'] if r['player_id']==3)
        interaction=next(r for r in receipt['interactions'] if r['target_id']==2)
        self.assertEqual(state._statistics_interaction(('player',3),('player',2))['kills_assisted_radio'],interaction['kills_assisted_radio'])
        self.assertEqual(1,interaction['kills_assisted_radio'])
        self.assertEqual(0,interaction['assist_radio'])
        self.assertEqual(target.health,result['players'][1]['health'])

    def test_equipment_negative_terminal_reasons_match_original_order(self):
        changes=[dict(equipment_id=True),dict(equipment_id=1.5),dict(equipment_id=-1),
            dict(equipment_id=65536),dict(activation_code=None),dict(activation_code=1.5),
            dict(selected='not_a_module'),dict(selected=17),dict(requested_active=1)]
        for change in changes:
            state=self.fixture._state(players=1);player=state.players[1]
            self.assertTrue(state._install_player_equipments(player));players=self.native_players(state)
            intent=dict(type='equipment_intent',round_id=1,intent_seq=1,equipment_id=12,
                activation_code=65548,selected=None,requested_active=True)
            intent.update(change)
            self.assertTrue(state.submit_equipment_intent(1,intent))
            actual=self.run_rules(players=players,initialize=True,tick=300,
                operations=[send(intent,False,1)])
            self.assertEqual(player.equipment_intent_result,
                actual['players'][0]['equipment_intent_result'],change)

    def test_native_receipt_passes_the_existing_client_and_account_validators(self):
        state=self.fixture._state()
        result=self.run_rules(players=self.native_players(state),tick=600,
            operations=[dict(op='finish',winner=1,reason='team_eliminated')])
        self.assertTrue(result['outcomes'][0])
        client=importlib.import_module('gui.mods.offline_lan_0922.lan_client')
        store=importlib.import_module('gui.mods.offline_lan_0922.account_rpc.postbattle_store')
        for receipt in result['receipts']:
            self.assertTrue(client._valid_battle_receipt(receipt),receipt)
            canonical=store._receipt(receipt)
            self.assertEqual(receipt['receipt_id'],canonical['receipt_id'])

    def test_governor_wear_and_repair_use_the_shared_pinned_mechanics(self):
        equipment=importlib.import_module('gui.mods.offline_lan_0922.equipment_mechanics')
        descriptor=types.SimpleNamespace(name='removedRpmLimiter',id=(0,12),compactDescr=3083,
            tags=('trigger',),reuseCount=0,cooldownSeconds=0,enginePowerFactor=1.1,
            engineHpLossPerSecond=15.)
        state=self.fixture._state(players=1);player=state.players[1]
        player.effective_params['equipment']=[equipment.project_equipment(descriptor)]
        player.critical=dict(devices=[dict(name='engineHealth',hp=100.,max_hp=100.,state='normal')],
            destroyed=[],crew_ko=[],crew_roster=['commander'],fire=False,ammo_rack_death=False,events=[])
        self.assertTrue(state._install_player_equipments(player))
        players=self.native_players(state)
        intent=dict(type='equipment_intent',round_id=1,intent_seq=1,equipment_id=12,
            activation_code=65548,selected=None,requested_active=True)
        self.assertTrue(state.submit_equipment_intent(1,intent))
        operations=[send(intent,False,1)]
        with contextlib.redirect_stdout(io.StringIO()):
            for tick in range(330,931,30):
                state.tick=tick;state._tick_player_critical(1.)
                operations.append(dict(op='advance',tick=tick,dt=1.))
        result=self.run_rules(players=players,initialize=True,tick=300,operations=operations)
        self.assertEqual(player.critical,result['players'][0]['critical'])
        self.assertEqual(player.rpm_damage_elapsed,result['players'][0]['rpm_damage_elapsed'])

    def test_stack_cumulative_receipts_do_not_spend_player_hp_twice(self):
        state=self.fixture._state();players=self.native_players(state)
        first=dict(upper=['player',1],lower=['player',2],upper_total=50,lower_total=80,
                   lower_environment_total=10,upper_impact_total=0)
        second=dict(first,upper_total=60,lower_total=100,lower_environment_total=15)
        operations=[]
        with contextlib.redirect_stdout(io.StringIO()):
            for row in (first,first,second,second):
                progress=dict(stacks=[row])
                state._consume_stack_contacts(progress)
                operations.append(dict(op='checkpoint',message=dict(round_id=1,bots=[],push_contacts=progress)))
        result=self.run_rules(players=players,tick=300,operations=operations)
        self.assertEqual([p.health for p in state.players.values()],
                         [p['health'] for p in result['players']])
        for identifier in (1,2):
            expected=state._statistics_row('player',identifier)
            actual=result['statistics']['player:%d'%identifier]
            for name in ('damage_received','damage_dealt','kills'):
                self.assertEqual(expected[name],actual.get(name,0),(identifier,name))

    def test_bot_inventory_and_burst_laws_match_original_positive_and_negative_cases(self):
        module=importlib.import_module('test_port_0922_bot_ammo')
        calls=[];original=self.server.BattleState._validate_bot_ammo_transition
        def capture(previous,current,unavailable_checkpoint=False):
            calls.append(dict(op='inventory',previous=copy.deepcopy(previous),
                current=copy.deepcopy(current),unavailable=unavailable_checkpoint))
            return original(previous,current,unavailable_checkpoint)
        self.server.BattleState._validate_bot_ammo_transition=staticmethod(capture)
        try:
            suite=unittest.TestSuite(module.BotAmmunitionTests(name) for name in
                unittest.defaultTestLoader.getTestCaseNames(module.BotAmmunitionTests)
                if name.startswith('test_server_') and 'policy' not in name)
            result=unittest.TestResult()
            with contextlib.redirect_stdout(io.StringIO()):suite.run(result)
            self.assertEqual([],result.errors);self.assertEqual([],result.failures)
        finally:self.server.BattleState._validate_bot_ammo_transition=staticmethod(original)
        self.assertGreater(len(calls),15)
        rng=random.Random(925)
        previous=dict(fire_seq=0,shell_index=0,burst_active=False,burst_group_seq=0,
            burst_count=0,burst_next_index=0,burst_interval=0.,burst_time_left=0.,burst_shell_index=0)
        for n in range(150):
            current=dict(previous,fire_seq=rng.randrange(0,5),burst_active=bool(rng.randrange(2)),
                burst_group_seq=rng.randrange(0,4),burst_count=rng.randrange(0,5),
                burst_next_index=rng.randrange(0,5),burst_interval=rng.choice((0.,0.1)))
            calls.append(dict(op='burst_edges',previous=previous,current=current))
            if n%4==0:previous=current
        expected=[]
        for call in calls:
            try:
                if call['op']=='inventory':original(call['previous'],call['current'],call['unavailable']);row=dict(valid=True)
                else:row=dict(valid=True,edges=list(self.server.BattleState._bot_burst_transition(call['previous'],call['current'])))
            except (ValueError,TypeError,IndexError):row=dict(valid=False)
            expected.append(row)
        actual=self.run_rules(operations=calls)['outcomes']
        self.assertEqual(expected,actual)

    def test_receipt_restore_matches_original_deep_validator(self):
        generated=self.run_rules(players=self.native_players(self.fixture._state()),tick=600,
            operations=[dict(op='finish',winner=1,reason='team_eliminated')])['receipts']
        cases=[copy.deepcopy(generated[0])]
        legacy=copy.deepcopy(cases[0]);legacy.pop('shells_fired',None);legacy.pop('equipment_used',None)
        legacy['rewards'].pop('crystal',None);legacy['stats'].pop('max_piercing_series',None)
        for row in legacy['public_results']:row['stats'].pop('max_piercing_series',None)
        cases.append(legacy)
        unsigned=copy.deepcopy(cases[0]);unsigned['arena_unique_id']=2**64-1;cases.append(unsigned)
        for field,value in [('protocol',4),('team',True),('player_name',''),
                            ('account_key','bad!'),('shells_fired',{'11':1}),
                            ('equipment_used',[0]),('public_results',[])]:
            row=copy.deepcopy(cases[0]);row[field]=value;cases.append(row)
        for path,value in [(('stats','damage'),True),(('rewards','repair_cost'),1),
                           (('public_results',0,'killer_id'),2),
                           (('public_results',0,'achievements'),['unknown'])]:
            row=copy.deepcopy(cases[0]);target=row
            for name in path[:-1]:target=target[name]
            target[path[-1]]=value;cases.append(row)
        for index,row in enumerate(cases):
            try:expected=self.server._persisted_result_receipt(copy.deepcopy(row));valid=True
            except (TypeError,ValueError,KeyError):valid=False
            process=subprocess.run([str(self.binary)],input=json.dumps(dict(restore_receipts=[row]))+'\n',
                capture_output=True,text=True,check=True)
            result=json.loads(process.stdout)
            self.assertEqual(valid,'error' not in result,index)
            if valid:self.assertEqual([expected],result['receipts'],index)

    def test_landing_rounding_normalized_identity_and_receipt_frontier_match(self):
        for speed in (0.,10.,10.0000005,11.5,13.3333335,30.):
            state=self.fixture._state(); player=state.players[1]
            state._install_player_equipments(player)
            player.input_seq=1
            profile=player.effective_params.get('critical')
            if profile:
                player.critical=dict(devices=[dict(name=r['name'],hp=r['max_hp'],max_hp=r['max_hp'],state='normal') for r in profile['devices']],destroyed=[],crew_ko=[],crew_roster=profile['crew_roster'],fire=False,ammo_rack_death=False,events=[])
            players=self.native_players(state)
            message=dict(type='landing_observation',round_id=1,authority_epoch=1,
                observation_seq=1,input_seq=1,impact_speed=speed,module_contacts=[
                    dict(name='engineHealth',weight=0.1),dict(name='engineHealth',weight=0.3)])
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertTrue(state.submit_landing_observation(1,message))
            expected=dict(player.landing_observation_results[1])
            replay=dict(message,module_contacts=[dict(name='engineHealth',weight=0.3)])
            result=self.run_rules(players=players,tick=300,operations=[
                send(message,False,1),send(replay,False,1)])
            for outcome in result['outcomes']:
                self.assertEqual([expected],outcome['replies'],(speed,outcome))
            self.assertEqual(player.health,result['players'][0]['health'],speed)
            self.assertEqual(player.critical,result['players'][0]['critical'],speed)

    def test_friendly_environment_damage_marks_attacker_immediately(self):
        state=self.fixture._state();state.players[2].team=1
        players=self.native_players(state)
        row=dict(upper=['player',1],lower=['player',2],upper_total=0,lower_total=80,
                 lower_environment_total=0,upper_impact_total=0)
        with contextlib.redirect_stdout(io.StringIO()):
            state._consume_stack_contacts(dict(stacks=[row]))
        result=self.run_rules(players=players,tick=300,operations=[dict(op='checkpoint',
            message=dict(round_id=1,bots=[],push_contacts=dict(stacks=[row])))])
        self.assertTrue(state.players[1].team_killer)
        self.assertTrue(result['players'][0]['team_killer'])
        self.assertEqual(state.players[1].frags,result['players'][0].get('frags',0))
        self.assertEqual(state._statistics_row('player',1)['team_damage'],
                         result['statistics']['player:1']['team_damage'])

    def test_pusher_drowning_keeps_display_hp_and_crew_only_critical_credit(self):
        state=self.fixture._state();players=self.native_players(state)
        critical=self.fixture._terminal_critical()
        observation=dict(type='player_environment',round_id=1,authority_epoch=1,sample_seq=1,
            observations=[dict(player_id=2,input_seq=0,level=2,drowning_critical=critical)])
        self.assertTrue(state.update_player_environment(-1,observation))
        state._remember_environment_push(('player',1),('player',2),sequence=1)
        with contextlib.redirect_stdout(io.StringIO()):
            state._tick_player_drowning(10.001)
        result=self.run_rules(players=players,tick=300,operations=[send(observation),
            dict(op='push',event=dict(attacker_kind='player',attacker_id=1,victim_kind='player',victim_id=2)),
            dict(op='advance',tick=301,dt=10.001)])
        self.assertEqual(state.players[2].health,result['players'][1]['health'])
        self.assertEqual(state.players[2].display_health,result['players'][1]['display_health'])
        self.assertEqual(state.players[2].critical,result['players'][1]['critical'])
        for name in ('damage_dealt','kills','critical_hits'):
            self.assertEqual(state._statistics_row('player',1)[name],
                             result['statistics']['player:1'].get(name,0),name)


if __name__ == '__main__':
    unittest.main()
