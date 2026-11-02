"""The live equipment publisher follows the battle's actual client-ready edge."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def method(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


HOST = r'''
#include "stock_native_client_weapon_runtime.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <memory>
#include <stdexcept>
namespace native_battle {
struct Startup {bool client_ready_received=false;};
// Retain the old disconnected flag in the fixture so the old publisher
// compiles, but fails to deliver when the real startup becomes ready.
struct LocalGun {bool client_ready=false;};
struct Battle {std::unique_ptr<Startup> startup;std::unique_ptr<LocalGun> gun=std::make_unique<LocalGun>();};
}
namespace stock_native_client_weapon_runtime {
namespace p {using Error=std::runtime_error;}
struct Owner {std::unique_ptr<Runtime> runtime;bool advancing=false;Edges pending;};
std::shared_ptr<Owner> owner;
std::vector<EquipmentEdge> delivered;
std::shared_ptr<Owner> ensure(native_battle::Battle&){return owner;}
void refreshOwner(native_battle::Battle&,Owner&,double){}
Factors factors(const Owner&){return {};}
void queue(native_battle::Battle&,Owner&o,const Edges&edges,double){o.pending=edges;}
void emitSafe(native_battle::Battle&,Owner&o){delivered.insert(delivered.end(),o.pending.equipment.begin(),o.pending.equipment.end());}
struct Guard {Owner&o;explicit Guard(Owner&v):o(v){o.advancing=true;}~Guard(){o.advancing=false;}};
@RUNTIME@
@PUBLISH@
}
int main(){
    namespace w=stock_native_client_weapon_runtime;
    native_battle::Battle b;
    w::Gun gun;gun.ammunition={40};gun.config.clip_size=1;gun.config.aim_time=1.;
    w::Metadata metadata;metadata.shells={18490};metadata.ready_stage=7;metadata.cooldown_stage=8;metadata.exhausted_stage=9;
    auto install=[&]{w::owner=std::make_shared<w::Owner>();w::owner->runtime=std::make_unique<w::Runtime>(gun,metadata);};
    auto publish=[&](const char*action,double now=10.){return w::publish(b,action,now,false,0.,0.);};
    std::vector<w::Equipment> rows;
    for(int compact:{507,1019,1531}){w::Equipment row;row.compact=compact;row.uses_left=-1;rows.push_back(row);}
    install();w::owner->runtime->equipment(rows);
    assert(!publish("equipment")&&w::delivered.empty());
    b.startup=std::make_unique<native_battle::Startup>();
    assert(publish("ammo")&&w::delivered.empty());
    assert(!publish("equipment")&&w::delivered.empty());
    b.startup->client_ready_received=true;
    // Unchanged shell ammo cannot mask the first consumable publication.
    assert(!publish("ammo"));
    assert(publish("equipment")&&w::delivered.size()==3);
    for(std::size_t i=0;i<rows.size();++i){assert(w::delivered[i].compact==rows[i].compact);assert(w::delivered[i].quantity==1&&w::delivered[i].stage==7);}
    assert(!publish("equipment")&&w::delivered.size()==3);
    rows[1].ready_at=100.;w::owner->runtime->equipment(rows);
    assert(publish("equipment")&&w::delivered.back().compact==1019&&w::delivered.back().stage==8&&w::delivered.back().remaining==90);
    assert(!publish("equipment",11.));
    assert(publish("equipment",100.)&&w::delivered.back().stage==7);
    rows[2].uses_left=0;w::owner->runtime->equipment(rows);
    assert(publish("equipment",101.)&&w::delivered.back().compact==1531&&w::delivered.back().quantity==0&&w::delivered.back().stage==9);
    // A replacement native owner inherits current readiness, without another
    // client-ready callback or any Python battle-state mirror.
    install();w::owner->runtime->equipment(rows);w::delivered.clear();
    assert(publish("equipment",102.)&&w::delivered.size()==3);
    // A new round starts unready and does not inherit the old owner's gate.
    b.startup=std::make_unique<native_battle::Startup>();install();
    w::owner->runtime->equipment(rows);w::delivered.clear();
    assert(!publish("equipment")&&w::delivered.empty());
    b.startup->client_ready_received=true;
    assert(publish("ammo")&&w::delivered.size()==3);
    // An empty mounted loadout stays empty after readiness as well.
    install();w::owner->runtime->equipment({});w::delivered.clear();
    assert(!publish("equipment")&&w::delivered.empty());
    // If the server ledger arrives after readiness, it is still published.
    w::owner->runtime->equipment(rows);
    assert(publish("equipment")&&w::delivered.size()==3);
}
'''


class EquipmentReadinessTests(unittest.TestCase):
    def test_live_publisher_uses_startup_readiness(self):
        bridge = (ROOT / 'bigworld/stock_native_client_weapon_runtime_python.cpp').read_text()
        runtime = (ROOT / 'bigworld/stock_native_client_weapon_runtime.cpp').read_text()
        runtime = runtime[runtime.index('Runtime::Runtime('):runtime.rfind('\n}')]
        publish = method(bridge, 'bool publish(Battle&b,')
        host = HOST.replace('@RUNTIME@', runtime).replace('@PUBLISH@', publish.replace('Battle&b', 'native_battle::Battle&b'))
        with tempfile.TemporaryDirectory(prefix='equipment-ready-') as directory:
            folder = Path(directory)
            (folder / 'host.cpp').write_text(host)
            result = subprocess.run(['c++', '-std=c++17', '-O1', '-I' + str(ROOT / 'bigworld'), str(folder / 'host.cpp'),
                                     str(ROOT / 'bigworld/stock_native_client_weapon.cpp'), '-o', str(folder / 'test')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(folder / 'test')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        # Snapshot, advance and explicit publication must share this same gate.
        for signature in ('Gun*advanceWithMotion(', 'bool equipmentSnapshot(Battle&b,', 'bool publish(Battle&b,'):
            body = method(bridge, signature)
            self.assertIn('b.startup->client_ready_received', body)
            self.assertNotIn('b.gun->client_ready', body)
        header = (ROOT / 'bigworld/stock_native_battle_projectiles.hpp').read_text()
        self.assertNotIn('bool client_ready', header)


if __name__ == '__main__':
    unittest.main()
