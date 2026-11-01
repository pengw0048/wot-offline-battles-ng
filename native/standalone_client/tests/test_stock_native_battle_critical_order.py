"""A local repair checkpoint cannot consume an admitted destruction HUD edge."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_stock_native_battle_combat_event import FIXTURE as COMBAT_FIXTURE

ROOT = Path(__file__).resolve().parents[1]
SOURCE = os.environ.get('WOT_FORK_0101_SOURCE')


def method(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def host_source():
    local = (ROOT / 'bigworld/stock_native_battle_critical_local.cpp').read_text()
    host = (ROOT / 'tests/stock_native_battle_combat_event_host.cpp').read_text()
    host = '#include "stock_native_client_critical.hpp"\n' + host
    owner = local[local.index('struct LocalCriticalOwner {'):local.index('\nCritical::Critical()')]
    host = host.replace('struct LocalCriticalOwner {};',
                        'namespace core = stock_native_client_critical;\n'
                        'namespace drive = stock_native_client_drive;\n' + owner)
    begin = local.index('using V = Value;')
    helpers = local[begin:local.index('// The stock descriptor extra index', begin)]
    functions = '\n'.join(method(local, signature) for signature in (
        'void mirror(Battle& b, LocalCriticalOwner& o)',
        'void mirrorTracks(Battle& b, LocalCriticalOwner& o)',
        'void payload(Battle& b, LocalCriticalOwner& o, const core::Effects& effects)',
        'bool reconcileNative(Battle& b, Actor& vehicle, const V& source, V* canonical)'))
    support = ('Critical::Critical() = default;\nnamespace critical {\n' + helpers +
               'LocalCriticalOwner& ensure(Battle& b) { return *b.critical->local; }\n'
               'void metadata(Battle&, LocalCriticalOwner&) {}\n' + functions + '\n}\n')
    host = host.replace('namespace vehicles {', 'double Battle::clock() const { return 1.; }\n' + support + 'namespace vehicles {', 1)
    host = host.replace('bool reconcileCriticalAuthority(Battle&, Actor&, const Value&, Value*) { return true; }',
                        'bool reconcileCriticalAuthority(Battle& b, Actor& a, const Value& v, Value* c) {'
                        ' if(!b.critical) return true; bool accepted=critical::reconcileNative(b,a,v,c);'
                        ' if(accepted && p::flag(py::optional(b.engine.modules.get(),"require_equal").get()) && !(*c==a.presented.critical))'
                        ' throw p::Error("fixture did not reach the equal durable-cache boundary"); return accepted; }')
    host = host.replace('bool presentCritical(Battle&, Actor&, const Value&, std::int64_t) { return true; }',
                        'bool presentCritical(Battle& b, Actor&, const Value& events, std::int64_t) {'
                        ' py::call(b.engine.modules.get(), "critical", p::Ref(cs::write(events)).get()); return true; }')
    host = host.replace('Value applyPayload(Battle&, PyObject*, const Value&) { throw p::Error("critical payload rejected"); }',
                        'Value applyPayload(Battle&, PyObject*, const Value& v) {'
                        ' if(v.get("invalid").truth()) throw p::Error("critical payload rejected");'
                        ' return v.get("events"); }')
    host = host.replace('i < 2;', 'i < 3;').replace(
        'actor->key = i ? "bot:2" : "player:1";',
        'actor->key = i == 2 ? "player:2" : i ? "bot:2" : "player:1";')
    setup = r'''
        if (b::p::flag(b::py::optional(modules,"pre_repair").get())) {
            battle.critical = std::make_unique<b::Critical>();
            auto actor = battle.vehicles->actors.at("player:1");
            auto& owner = battle.critical->local;
            owner = std::make_unique<b::LocalCriticalOwner>();
            owner->actor = actor;
            owner->state = battle.vehicles;
            auto first = b::cs::read(b::p::item(events,0).get());
            actor->state = first;
            auto initial = b::py::optional(modules,"owner_initial");
            if (initial.get()!=Py_None) actor->state.object["critical"] = b::cs::read(initial.get());
            actor->presented.critical_revision = b::p::numeric(modules,"presented_revision",0.);
            b::drive::StatusDescriptor descriptor;
            for(const char* name : {"leftTrackHealth","rightTrackHealth","engineHealth","ammoBayHealth","gunHealth"}) {
                descriptor.maxima[name] = 130.;
                descriptor.regeneration[name] = 100.;
            }
            owner->status = b::drive::installStatus(actor,descriptor);
            owner->status->update(1,0.);
            b::core::Metadata metadata;
            metadata.maxima = descriptor.maxima;
            metadata.regeneration = descriptor.regeneration;
            metadata.extra_indexes["rightTrackHealth"] = 82;
            owner->maxima = metadata.maxima;
            owner->model = std::make_shared<b::core::Owner>(metadata,owner->status->critical());
            auto current = owner->status->critical();
            owner->model->accept(1,1,0,current);
            owner->status->trackOverlay(owner->model);
            auto effects = owner->model->advance(current,b::p::numeric(modules,"repair_dt",.03),1.,true,.57,owner->status->casualties());
            owner->status->localTracks(owner->model->tracks());
            b::critical::mirror(battle,*owner);
            b::critical::payload(battle,*owner,effects);
            if (!battle.critical->owned || !effects.payload) throw b::p::Error("repair setup did not take ownership");
        }
'''
    host = host.replace('b::compat::stock().runtime = b::borrow(modules);', setup + '\n        b::compat::stock().runtime = b::borrow(modules);')
    host += '\nnamespace stock_native_client_drive { void synchronizeStatus(const native_battle::Battle&,double) {} }\n'
    return host


FIXTURE = COMBAT_FIXTURE + r'''
failing=None
critical_rows=[]
modules.critical=lambda events:critical_rows.extend(events)
modules.pre_repair=True
entities[93]=entities[92]
for track in ('leftTrackHealth','rightTrackHealth'):
 for attacker in ('human','bot'):
  for scenario in ('broken','functional','stale','new_base','no_edge'):
   critical_rows[:]=[]
   modules.owner_initial=None
   modules.require_equal=scenario!='new_base'
   modules.repair_dt=20. if scenario=='functional' else .03
   modules.presented_revision=2 if scenario=='stale' else 0
   destroyed=dict(devices=[dict(name=track,hp=0.,max_hp=130.,state='destroyed')],destroyed=[track],crew_ko=[],fire=False,ammo_rack_death=False,
    events=[] if scenario=='no_edge' else [dict(kind='device',name=track,state='destroyed',old_state='normal',cause='shot')])
   first=dict(source='shot',kind='bot_human_hit' if attacker=='bot' else 'hit',target=1,damage=0,health=457,dead=False,death_reason=0,attack_reason=0,shot_result=1,shell_index=0,
    critical=destroyed,critical_revision=2 if scenario=='new_base' else 1,critical_base_revision=2 if scenario=='new_base' else 1,critical_ack_seq=0)
   first['attacker_bot' if attacker=='bot' else 'attacker']=2
   result=native.exercise(avatar,modules,O(entity=lambda identity:entities[identity]),VehicleMarkerPlugin(provider),[first],1)
   assert not result['errors'],(track,attacker,scenario,result)
   expected=destroyed['events'] if scenario in ('broken','new_base') else []
   assert critical_rows==expected,(track,attacker,scenario,critical_rows)
for attacker in ('human','bot'):
 for kind,name,state,old,cause in (
   ('device','engineHealth','destroyed','normal','shot'),
   ('device','engineHealth','critical','normal','shot'),
   ('device','ammoBayHealth','destroyed','normal','shot'),
   ('device','ammoBayHealth','critical','destroyed','repair'),
   ('device','gunHealth','normal','critical','repair'),
   ('crew','driver','destroyed','normal','shot'),
   ('crew','driver','normal','destroyed','repair')):
  critical_rows[:]=[]
  modules.owner_initial=None;modules.repair_dt=.03;modules.presented_revision=0;modules.require_equal=True
  devices=[dict(name='rightTrackHealth',hp=0.,max_hp=130.,state='destroyed')]
  if kind=='device':devices.append(dict(name=name,hp={'destroyed':0.,'critical':40.,'normal':130.}[state],max_hp=130.,state=state))
  devices.sort(key=lambda row:row['name'])
  payload=dict(devices=devices,destroyed=sorted([row['name'] for row in devices if row['state']=='destroyed']),
   crew_ko=['driver'] if kind=='crew' and state=='destroyed' else [],fire=False,ammo_rack_death=False,
   events=[dict(kind=kind,name=name,state=state,old_state=old,cause=cause)])
  first=dict(source='shot',kind='bot_human_hit' if attacker=='bot' else 'hit',target=1,damage=0,health=457,dead=False,death_reason=0,attack_reason=0,shot_result=1,shell_index=0,
   critical=payload,critical_revision=1,critical_base_revision=1,critical_ack_seq=0)
  first['attacker_bot' if attacker=='bot' else 'attacker']=2
  result=native.exercise(avatar,modules,O(entity=lambda identity:entities[identity]),VehicleMarkerPlugin(provider),[first],1)
  assert not result['errors'],(attacker,kind,name,state,result)
  assert critical_rows==payload['events'],(attacker,kind,name,state,critical_rows)
# An owner that completed the repair already delivered its repair edge. A
# matching server checkpoint must not play that edge a second time.
modules.require_equal=True;modules.repair_dt=20.;critical_rows[:]=[]
modules.owner_initial=dict(devices=[dict(name='rightTrackHealth',hp=0.,max_hp=130.,state='destroyed')],destroyed=['rightTrackHealth'],crew_ko=[],fire=False,ammo_rack_death=False,events=[])
payload=dict(devices=[dict(name='rightTrackHealth',hp=100.,max_hp=130.,state='critical')],destroyed=[],crew_ko=[],fire=False,ammo_rack_death=False,
 events=[dict(kind='device',name='rightTrackHealth',state='critical',old_state='destroyed',cause='repair')])
first=dict(first,critical=payload)
result=native.exercise(avatar,modules,O(entity=lambda identity:entities[identity]),VehicleMarkerPlugin(provider),[first],1)
assert not result['errors'] and not critical_rows,(result,critical_rows)
print('Real native repair ownership preserves the first admitted destruction HUD edge')
'''


@unittest.skipUnless(SOURCE, 'fixed gameplay source is required')
class CriticalOrderTests(unittest.TestCase):
    def test_checkpoint_before_event_preserves_destroyed_edge(self):
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                      env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='critical-order-') as directory:
            folder = Path(directory)
            cpp = folder / 'host.cpp'
            cpp.write_text(host_source())
            command = ['c++', '-std=c++17', '-O1', '-DWOT_NATIVE_BATTLE_HOST_BUILD', '-DWOT_CLIENT_REPLICA_HOST_BUILD',
                       '-Wno-register', '-bundle', '-undefined', 'dynamic_lookup', '-Wl,-dead_strip',
                       '-Wl,-exported_symbol,_initbattle_combat_event_test', '-isystem', str(Path(prefix) / 'include/python2.7'),
                       '-I' + str(ROOT / 'bigworld'), '-I' + str(Path(SOURCE) / 'native')]
            cpp.write_text(cpp.read_text().replace('"../bigworld/', '"'))
            command += [str(cpp)] + [str(ROOT / 'bigworld' / name) for name in (
                'stock_native_battle_events_combat.cpp', 'stock_native_battle_events.cpp', 'stock_native_battle_feedback.cpp',
                'stock_native_battle_compat.cpp', 'stock_native_client_state.cpp', 'stock_native_client_critical.cpp',
                'stock_native_client_drive_status.cpp')]
            command += ['-o', str(folder / 'battle_combat_event_test.so')]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([py27, '-B', '-c', FIXTURE], cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
