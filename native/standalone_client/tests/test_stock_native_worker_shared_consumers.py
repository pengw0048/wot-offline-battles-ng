"""Execute the worker consumers' real mutation boundaries with immutable room views."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORLD = ROOT / 'bigworld'


def function(source, signature):
    begin = source.index(signature)
    cursor = source.index('{', begin)
    depth = 1
    while depth:
        cursor += 1
        depth += (source[cursor] == '{') - (source[cursor] == '}')
    return source[begin:cursor + 1]


class WorkerSharedConsumers(unittest.TestCase):
    def test_batch_dispatch_stops_at_reentrant_lifecycle_edges(self):
        if not shutil.which('c++'):
            self.skipTest('A C++17 compiler is required')
        source = (WORLD / 'stock_native_battle_link.cpp').read_text()
        helpers = '\n'.join(function(source, sig) for sig in (
            'const Value& at(', 'bool has(', 'Value integer(', 'Value number(',
            'Value text(', 'Value object(', 'Value array(', 'std::optional<std::int64_t> exact(',
            'std::optional<std::int64_t> ranged(', 'std::string safeText(',
            'std::optional<std::array<Value, 4>> lineage(', 'Value mergeOrders(',
            'void notify(const std::string& kind, std::shared_ptr<const Value> message)'))
        code = BATCH_PREAMBLE + helpers + BATCH_DISPATCH + function(source, 'void dispatchBatch(') + BATCH_FIXTURE
        with tempfile.TemporaryDirectory(prefix='worker-shared-dispatch-') as directory:
            folder = Path(directory)
            (folder / 'probe.cpp').write_text(code)
            compiled = subprocess.run(['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
                                       '-I' + str(ROOT), str(folder / 'probe.cpp'),
                                       '-o', str(folder / 'probe')], capture_output=True, text=True)
            self.assertEqual(0, compiled.returncode, compiled.stdout + compiled.stderr)
            run = subprocess.run([str(folder / 'probe')], capture_output=True, text=True)
            self.assertEqual(0, run.returncode, run.stdout + run.stderr)
            self.assertIn('Shared worker batch lifecycle passed', run.stdout)

    def test_real_worker_session_accepts_owned_and_shared_message_classes(self):
        fork = os.environ.get('WOT_FORK_SOURCE')
        if not fork or sys.platform != 'darwin' or not shutil.which('c++'):
            self.skipTest('The pinned native headers and macOS CPython 2.7 extension compiler are required')
        import test_stock_native_worker_session as session
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                      env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        host = (ROOT / 'tests/stock_native_worker_session_host.cpp').read_text()
        host = host.replace('#include "../bigworld/', '#include "' + str(WORLD) + '/')
        host = '#include "' + str(ROOT / 'room/room_value.hpp') + '"\n' + host
        original = 'worker::onEvent(kind, std::make_shared<const Value>(cs::read(raw)));'
        self.assertIn(original, host)
        host = host.replace(original, r'''
            Value message=cs::read(raw);
            const char* mode=std::getenv("WOT_WORKER_SHARED_VIEW");
            if(mode && mode[0]=='1') message=wot::room::objectView(wot::room::freeze(std::move(message)));
            if(mode && mode[0]=='2') {
                for(auto& field:message.object) {
                    Value shared;shared.kind=Value::Kind::Shared;
                    shared.reference=wot::room::freeze(std::move(field.second));
                    field.second=std::move(shared);
                }
            }
            worker::onEvent(kind, wot::room::freeze(std::move(message)));
        ''')
        # This fake startup records the round; its scalar read must model the
        # real startup::roundId consumer rather than reject the shared fixture.
        host = host.replace('b->round_id = message.get("round_id").integer;',
                            'b->round_id = message.get("round_id").resolved().integer;')
        with tempfile.TemporaryDirectory(prefix='worker-shared-session-') as directory:
            folder = Path(directory)
            source = (WORLD / 'stock_native_battle_startup.cpp').read_text()
            (folder / 'worker_session_startup.inc').write_text('\n'.join(function(source, sig) for sig in (
                'void stop(Battle& b, bool restore_account)', 'void teardownAfterFailure(Battle& b)',
                'void reportFailure(Battle& b, bool lobby_restored)',
                'void scheduleLobbyRestore(Battle& b, bool report_failure, bool)')))
            (folder / 'host.cpp').write_text(host)
            command = ['c++', '-std=c++17', '-O1', '-DWOT_NATIVE_BATTLE_HOST_BUILD',
                       '-DWOT_CLIENT_REPLICA_HOST_BUILD', '-Wno-register', '-bundle', '-undefined',
                       'dynamic_lookup', '-isystem', str(Path(prefix) / 'include/python2.7'),
                       '-I' + str(folder), '-I' + str(WORLD), '-I' + str(ROOT.parent),
                       '-I' + str(Path(fork) / 'native'), str(folder / 'host.cpp'),
                       str(WORLD / 'stock_native_client_state.cpp'),
                       '-o', str(folder / 'worker_session_test.so')]
            compiled = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(0, compiled.returncode, compiled.stdout + compiled.stderr)
            (folder / 'fixture.py').write_text(session.FIXTURE + SESSION_MESSAGE_CLASSES)
            for mode in ('0', '1', '2'):
                with self.subTest(view=mode):
                    run = subprocess.run([py27, '-B', str(folder / 'fixture.py'), str(folder)], cwd=folder,
                                         env=dict(os.environ, WOT_WORKER_SHARED_VIEW=mode),
                                         capture_output=True, text=True)
                    self.assertEqual(0, run.returncode, run.stdout + run.stderr)
                    self.assertIn('Consecutive production worker rounds', run.stdout)

    def test_mutable_consumers_and_runtime_rows_preserve_shared_sections(self):
        if not shutil.which('c++'):
            self.skipTest('A C++17 compiler is required')
        link = (WORLD / 'stock_native_battle_link.cpp').read_text()
        sources = sorted(WORLD.glob('stock_native_battle*.cpp'))
        bodies, calls = [], []
        for source in sources:
            text = source.read_text()
            signature = next((sig for sig in ('Value& writable(', 'V& writable(') if sig in text), None)
            if not signature:
                continue
            index = len(calls)
            dependency = function(text, 'Value owned(') if 'Value owned(' in text else ''
            bodies.append('namespace mutation%d { using V=Value; using Kind=Value::Kind; %s %s }' %
                          (index, dependency, function(text, signature)))
            calls.append('checkMutation(mutation%d::writable);' % index)
        self.assertGreaterEqual(len(calls), 11)
        helpers = '\n'.join(function(link, sig) for sig in (
            'const Value& at(', 'bool has(', 'Value integer(', 'Value number(',
            'Value text(', 'Value object(', 'Value array(', 'Value shared(',
            'Value& writable(', 'std::optional<std::int64_t> exact(',
            'std::optional<double> real(', 'double finite(', 'std::string safeText(', 'bool loadTiming(',
            'void restoreEquipment(',
            'Value canonicalRow(', 'std::pair<Value, long> canonicalRows('))
        critical = (WORLD / 'stock_native_battle_vehicles_critical.cpp').read_text()
        canonical = '\nnamespace critical_payload {\n' + function(critical, 'Value& writable(')
        canonical += '\n' + function(critical, 'std::optional<Value> canonicalOf(') + '\n}\n'
        combat = (WORLD / 'stock_native_battle_events_combat.cpp').read_text()
        statistics = STATISTICS_PREAMBLE + '\n'.join(function(combat, sig) for sig in (
            'const Value& field(', 'bool has(', 'std::int64_t integer(', 'Value& writable(',
            'Actor* presented(', 'void applyStatisticsEvent(')) + '\n}\n'
        code = PREAMBLE + '\n'.join(bodies) + '\n' + helpers + canonical + statistics + FIXTURE
        code = code.replace('/* MUTATION_CALLS */', '\n'.join(calls))
        with tempfile.TemporaryDirectory(prefix='worker-shared-consumers-') as directory:
            folder = Path(directory)
            (folder / 'probe.cpp').write_text(code)
            compiled = subprocess.run(['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
                                       '-I' + str(ROOT), str(folder / 'probe.cpp'),
                                       '-o', str(folder / 'probe')], capture_output=True, text=True)
            self.assertEqual(0, compiled.returncode, compiled.stdout + compiled.stderr)
            run = subprocess.run([str(folder / 'probe')], capture_output=True, text=True)
            self.assertEqual(0, run.returncode, run.stdout + run.stderr)
            self.assertIn('Shared worker mutation and runtime admission passed', run.stdout)


PREAMBLE = r'''
#include "room/room_value.hpp"
#include <algorithm>
#include <cassert>
#include <ctime>
#include <iostream>
#include <set>
#include <stdexcept>
using Value=stock_native_client_state::Value;
using K=Value::Kind;
namespace p { using Error=std::runtime_error; }
const Value none;
struct Link {
    std::shared_ptr<const Value> last_snapshot;
    struct { std::optional<std::int64_t> round_id=7; } s;
    std::optional<std::int64_t> timing_round;
    std::int64_t timing_tick=-1;
    std::optional<double> rtt_ms,combat_deadline,combat_end_deadline,combat_duration;
    std::string combat_phase;
} storage;
Link* link=&storage;
double roundSix(double v) { return std::round(v*1000000.)/1000000.; }
'''

SESSION_MESSAGE_CLASSES = r'''
world = fresh()
for kind in ('welcome','roster'):
    native.event(kind, dict(round_id=1, phase='loading'))
    assert native.details()['active'] == 1
for index,kind in enumerate(('snapshot','events','battle_live','bot_observation','player_destructible_contact')):
    native.event(kind,dict(round_id=0, events=[], contacts=[], timing=dict(phase='loading')))
    assert native.details()['received'] == index
    native.event(kind,dict(round_id=1, events=[], contacts=[], timing=dict(phase='loading')))
    assert native.details()['received'] == index + 1
native.event('roster', dict(round_id=1, phase='waiting'))
assert native.details()['active'] is None and native.details()['restore_pending']
world.restore()
start(2)
assert native.details()['active'] == 2
native.shutdown()
'''

BATCH_PREAMBLE = r'''
#include "room/room_value.hpp"
#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
using Value=stock_native_client_state::Value;
using K=Value::Kind;
using Handler=std::function<void(const std::string&,std::shared_ptr<const Value>)>;
struct Link { bool running=true;std::uint64_t generation=1;Handler handler; };
std::unique_ptr<Link> link;
constexpr std::int64_t MAX_MOTION=9007199254740991LL;
'''

STATISTICS_PREAMBLE = r'''
namespace statistics {
struct Actor { int engine_id=1;struct { Value state; } presented; };
struct Battle { Actor actor;Actor* vehicle(const std::string& key) { return key=="player:2"?&actor:nullptr; } };
namespace vehicles { void applyStatistics(Battle&,Actor&,const Value&) {} }
'''

BATCH_DISPATCH = r'''
void dispatch(Value message,double) {
    notify(safeText(at(message,"type")),wot::room::freeze(std::move(message)));
}
'''

BATCH_FIXTURE = r'''
Value message(const std::string& kind,int tick) {
    auto out=object();out.object["type"]=text(kind);out.object["round_id"]=integer(7);
    out.object["server_tick"]=integer(tick);out.object["authority_epoch"]=integer(2);
    out.object["bot_authority_id"]=integer(-1);out.object["map"]=text("33_fjord");
    return wot::room::objectView(wot::room::freeze(std::move(out)));
}
int main() {
    std::vector<std::vector<Value>> batches={
        {message("snapshot",1),message("events",1),message("snapshot",2)},
        {message("snapshot",1),message("battle_live",1)},
        {message("error",1),message("snapshot",1)}};
    auto full=message("snapshot",1);full.object["bot_manifest"]=array();
    batches.push_back({full,message("snapshot",2)});
    for(const auto& batch:batches) for(int mode=0;mode<3;++mode) {
        auto frames=batch;int count=0;
        link=std::make_unique<Link>();
        auto marker=std::make_shared<int>(42);std::weak_ptr<int> alive=marker;
        link->handler=[&,marker](const std::string&,std::shared_ptr<const Value>) {
            ++count;
            if(mode==0) { link->running=false;link->handler=nullptr; }
            else if(mode==1) link.reset();
            else { link=std::make_unique<Link>();link->generation=2;
                   link->handler=[&](const std::string&,std::shared_ptr<const Value>){++count;}; }
            // notify owns its callback until the synchronous body returns.
            assert(!alive.expired() && *marker==42);
        };
        marker.reset();dispatchBatch(frames,0.);assert(count==1);
        assert(alive.expired());
    }
    link=std::make_unique<Link>();std::vector<std::string> order;
    link->handler=[&](const std::string& type,std::shared_ptr<const Value>){order.push_back(type);};
    auto normal=batches[0];dispatchBatch(normal,0.);
    assert((order==std::vector<std::string>{"events","snapshot","snapshot"}));
    // Coalescing preserves an older order section when the new snapshot is lean.
    auto first=message("snapshot",1),last=message("snapshot",2);
    first.object["bot_orders"]=array();first.object["bot_order_revision"]=integer(3);
    std::vector<Value> lean={first,last};int deliveries=0;
    link->handler=[&](const std::string& type,std::shared_ptr<const Value> row){
        assert(type=="snapshot" && row->get("server_tick").numeric()==2.);
        assert(has(*row,"bot_orders") && row->get("bot_order_revision").numeric()==3.);++deliveries;
    };
    dispatchBatch(lean,0.);assert(deliveries==1);
    link.reset();dispatchBatch(lean,0.);
    std::cout<<"Shared worker batch lifecycle passed\n";
}
'''

FIXTURE = r'''
Value wrap(Value value,int depth) {
    for(int i=0;i<depth;++i) value=shared(std::make_shared<const Value>(std::move(value)));
    return value;
}
void checkMutation(Value& (*mutate)(Value&)) {
    for(int depth=0;depth<5;++depth) {
        Value source=object(); source.object["health"]=integer(10);
        source.object["effective_params"]=wrap(object(),2);
        Value frozen=wrap(source,depth), copy=frozen;
        const auto static_owner=copy.get("effective_params").reference;
        mutate(copy).object["health"]=integer(7);
        assert(!copy.reference && copy.get("health").numeric()==7.);
        assert(frozen.get("health").numeric()==10.);
        assert(copy.get("effective_params").reference==static_owner);
    }
}
Value actor(int depth) {
    Value row=object();
    row.object["id"]=wrap(integer(16),depth);
    row.object["team"]=integer(1); row.object["slot"]=integer(0);
    row.object["vehicle"]=text("ussr:R11_MS-1"); row.object["x"]=number(4.);
    Value item=object(); item.object["equipment"]=wrap(integer(0),depth);
    item.object["usesLeft"]=integer(1);
    Value items=array(); items.array.push_back(wrap(item,depth));
    row.object["equipment_states"]=wrap(items,depth);
    return wrap(row,depth);
}
int main() {
    /* MUTATION_CALLS */
    for(int depth=0;depth<5;++depth) {
        storage=Link();storage.rtt_ms=100.;
        auto timing=object();timing.object["phase"]=wrap(text("prebattle"),depth);
        timing.object["start_in_ms"]=wrap(integer(10000),depth);
        timing.object["remaining_ms"]=wrap(integer(900000),depth);
        timing.object["duration_ms"]=wrap(integer(900000),depth);
        auto message=object();message.object["round_id"]=wrap(integer(7),depth);
        message.object["server_tick"]=wrap(integer(1),depth);
        message.object["_client_received_time"]=wrap(number(100.),depth);
        message.object["timing"]=wrap(timing,depth);
        assert(loadTiming(wrap(message,depth)) && storage.combat_phase=="prebattle");
        assert(std::abs(*storage.combat_deadline-109.95)<1e-9 && *storage.combat_duration==900.);
        timing.object["phase"]=wrap(text("unknown"),depth);
        message.object["timing"]=wrap(timing,depth);
        assert(!loadTiming(wrap(message,depth)) && storage.combat_phase=="prebattle");
    }
    for(int depth=0;depth<5;++depth) {
        auto payload=object();auto edges=array();edges.array.push_back(integer(9));
        payload.object["events"]=wrap(edges,depth);payload.object["fire"]=wrap(wot::room::boolean(true),depth);
        auto canonical=critical_payload::canonicalOf(wrap(payload,depth));
        assert(canonical && canonical->get("events").resolved().kind==K::Array);
        assert(canonical->get("events").resolved().array.empty());
        assert(canonical->get("fire").truth() && payload.get("events").resolved().array.size()==1);
    }
    assert(!critical_payload::canonicalOf(wrap(integer(1),3)));
    for(int depth=0;depth<5;++depth) {
        auto previous=object();previous.object["frags"]=wrap(integer(1),depth);
        previous.object["team_killer"]=wrap(wot::room::boolean(false),depth);
        statistics::Battle battle;battle.actor.presented.state=wrap(previous,depth);
        auto event=object();event.object["actor_kind"]=wrap(text("player"),depth);
        event.object["actor_id"]=wrap(integer(2),depth);
        event.object["frags"]=wrap(integer(3),depth);event.object["team_killer"]=wrap(wot::room::boolean(true),depth);
        statistics::applyStatisticsEvent(battle,wrap(event,depth));
        assert(battle.actor.presented.state.get("frags").numeric()==3.);
        assert(battle.actor.presented.state.get("team_killer").truth());
        assert(previous.get("frags").numeric()==1. && !previous.get("team_killer").truth());
        event.object["actor_id"]=text("invalid");
        statistics::applyStatisticsEvent(battle,wrap(event,depth));
        assert(battle.actor.presented.state.get("frags").numeric()==3.);
    }
    Value contract=object();contract.object["kind"]=text("medkit");
    Value contracts=array();contracts.array.push_back(wrap(contract,3));
    for(int depth=0;depth<5;++depth) {
        Value message=object();message.object["round_id"]=integer(7);
        message.object["authority_epoch"]=integer(2);
        message.object["bot_equipment_contracts"]=wrap(contracts,depth);
        Value rows=array();rows.array.push_back(actor(depth));
        message.object["bots"]=wrap(rows,depth);
        auto owner=std::make_shared<const Value>(std::move(message));
        Value view=wot::room::objectView(owner);
        storage.last_snapshot.reset();
        auto admitted=canonicalRows(view.get("bots"),"bots",&view);
        assert(admitted.first.array.size()==1 && admitted.second==0);
        const Value& row=admitted.first.array[0];
        assert(row.get("id").numeric()==16 && row.get("x").numeric()==4.);
        const Value& equipment=row.get("equipment_states").resolved().array[0];
        assert(equipment.get("equipment").get("kind").resolved().text=="medkit");
        // Admission never writes its immutable source or replaces the compact index in it.
        const Value& original=owner->get("bots").resolved().array[0];
        assert(original.get("equipment_states").resolved().array[0].get("equipment").numeric(-1.)==0.);
        Value previous=view;previous.object["bots"]=admitted.first;
        storage.last_snapshot=std::make_shared<const Value>(std::move(previous));
        // Invalid same-round pose retains the canonical old row, including its resolved contract.
        Value invalid=actor(depth).resolved();invalid.object["x"]=number(3000.);
        Value bad=array();bad.array.push_back(wrap(invalid,depth));
        auto retained=canonicalRows(wrap(bad,depth),"bots",&view);
        assert(retained.second==1 && retained.first==admitted.first);
        // An epoch change cannot reuse a previous equipment contract.
        Value next=view;next.object["authority_epoch"]=integer(3);
        next.object.erase("bot_equipment_contracts");
        Value compact=actor(depth).resolved();
        Value item=compact.get("equipment_states").resolved().array[0].resolved();
        item.object["equipment"]=wrap(Value(),depth);
        Value items=array();items.array.push_back(wrap(item,depth));
        compact.object["equipment_states"]=wrap(items,depth);
        Value lean=array();lean.array.push_back(wrap(compact,depth));
        auto refused=canonicalRows(wrap(lean,depth),"bots",&next);
        assert(refused.second==1 && refused.first==admitted.first);
        Value new_round=next;new_round.object["round_id"]=integer(8);
        auto missing=canonicalRows(wrap(lean,depth),"bots",&new_round);
        assert(missing.second==0 && missing.first.array.empty());
    }
    std::cout<<"Shared worker mutation and runtime admission passed\n";
}
'''

if __name__ == '__main__':
    unittest.main()
