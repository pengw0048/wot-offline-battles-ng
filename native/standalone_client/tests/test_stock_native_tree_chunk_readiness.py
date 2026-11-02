"""Exercise production WGDE census and tree warming without a native scene."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags

ROOT = Path(__file__).resolve().parents[1]


def method(filename, start, end):
    source = (ROOT / 'bigworld' / filename).read_text()
    return source[source.index(start):source.index(end, source.index(start))]


class TreeChunkReadinessTest(unittest.TestCase):
    def compile_run(self, source):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        with tempfile.TemporaryDirectory(prefix='native-tree-readiness-') as folder:
            path = Path(folder) / 'probe.cpp'
            binary = Path(folder) / 'probe'
            path.write_text(source)
            result = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                     *native_test_compile_flags(compiler), str(path), '-o', str(binary)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)

    def test_sparse_cells_and_missing_expected_objects_have_distinct_readiness(self):
        body = method('stock_native_bot_world_adapter.cpp',
                      'bool NativeScene::warmTreeChunks(', 'bool NativeScene::turretSweep(')
        self.compile_run(r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cmath>
#include <map>
#include <set>
#include <utility>
#include <vector>
namespace BW {
using uint32=std::uint32_t;
bool complete=true;
std::vector<uint32> census;
bool stockDestructibleChunks(int,std::vector<uint32>& output){output=census;return complete;}
}
namespace offline_destructible {
struct Item {int index=0;std::array<double,3> position{};bool tree=false,named=false,catalog=false;};
struct Chunk {std::map<int,Item> items;std::map<std::pair<int,int>,std::vector<int>> origins;};
}
using Key=std::pair<std::uint32_t,std::uint32_t>;
enum class Kind {Tree,Falling,Structure};
enum class Admission {Pending,Admitted};
struct Instance {Kind kind=Kind::Tree;std::vector<int> boxes;double tree_health=10.;
    std::array<double,3> origin{};bool native_name_exact=true;};
struct Frozen {Instance instance;Admission admission=Admission::Admitted;};
struct Registry {
    struct Geometry {std::map<std::uint32_t,offline_destructible::Chunk> rows;unsigned writes=0;
        void drop_chunk(std::uint32_t id){rows.erase(id);}
        void chunk(std::uint32_t id,offline_destructible::Chunk row){rows[id]=std::move(row);++writes;}} geometry;
    std::set<std::uint32_t> ready_tree_chunks;std::map<Key,Instance> instances;
};
struct Space {int id()const{return 1;}};
struct NativeScene {
    Registry registry_;Space storage,*space_=&storage;
    std::map<Key,Frozen> identities_;std::map<std::uint32_t,std::vector<Key>> by_chunk_;
    std::uint64_t stream_revision_=0,revision=1;bool active=true;
    bool current()const{return active;}std::uint64_t streamRevision()const{return revision;}
    Admission refresh(Key key,Registry& registry){const auto& row=identities_.at(key);
        if(row.admission==Admission::Admitted){registry.instances[key]=row.instance;}
        return row.admission;}
    bool warmTreeChunks(const std::vector<std::uint32_t>&);
    void put(std::uint32_t chunk,Kind kind,bool boxes=false,Admission admission=Admission::Admitted){
        Key key{chunk,7};Frozen value;value.instance.kind=kind;value.admission=admission;
        if(boxes){value.instance.boxes.push_back(1);}
        identities_[key]=value;by_chunk_[chunk]={key};}
};
@METHOD@
int main(){
    NativeScene scene;
    // A fully bound sparse census proves the omitted neighboring cells empty.
    BW::census={10};assert(scene.warmTreeChunks({10,11,12}));
    for(auto id:{10u,11u,12u}){assert(scene.registry_.ready_tree_chunks.count(id));assert(scene.registry_.geometry.rows.at(id).items.empty());}
    const auto writes=scene.registry_.geometry.writes;
    assert(scene.warmTreeChunks({10,11,12})&&scene.registry_.geometry.writes==writes);
    // Missing expected trunks and bodyless falling objects remain pending.
    scene.put(13,Kind::Tree);scene.put(14,Kind::Falling);
    scene.put(15,Kind::Falling,true);scene.put(16,Kind::Structure);
    assert(scene.warmTreeChunks({13,14,15,16}));
    assert(!scene.registry_.ready_tree_chunks.count(13)&&!scene.registry_.ready_tree_chunks.count(14));
    assert(scene.registry_.ready_tree_chunks.count(15)&&scene.registry_.ready_tree_chunks.count(16));
    // A present chunk still waits for its exact live tree identity.
    BW::census={10,13};scene.identities_.at({13,7}).admission=Admission::Pending;
    assert(scene.warmTreeChunks({13})&&!scene.registry_.ready_tree_chunks.count(13));
    scene.identities_.at({13,7}).admission=Admission::Admitted;
    assert(scene.warmTreeChunks({13})&&scene.registry_.ready_tree_chunks.count(13));
    assert(scene.registry_.geometry.rows.at(13).items.at(7).tree);
    // Mapping replacement invalidates both populated and empty proofs.
    ++scene.revision;BW::census={14};assert(scene.warmTreeChunks({11,13,14}));
    assert(!scene.registry_.ready_tree_chunks.count(10)&&!scene.registry_.ready_tree_chunks.count(12));
    assert(scene.registry_.ready_tree_chunks.count(11)&&!scene.registry_.ready_tree_chunks.count(13));
    assert(scene.registry_.ready_tree_chunks.count(14));
    BW::complete=false;assert(!scene.warmTreeChunks({20}));assert(!scene.registry_.ready_tree_chunks.count(20));
    BW::complete=true;scene.active=false;assert(!scene.warmTreeChunks({20}));
}
'''.replace('@METHOD@', body))

    def test_census_requires_bound_metadata_and_retains_sparse_records(self):
        body = method('stock_destructibles.cpp', 'bool stockDestructibleChunks(',
                      '\nvoid ',)
        self.compile_run(r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>
using uint32=std::uint32_t;using SpaceID=int;
struct ClientSpace {bool clear=false;bool cleared()const{return clear;}};
using ClientSpacePtr=std::shared_ptr<ClientSpace>;
struct SpaceManager {ClientSpacePtr value=std::make_shared<ClientSpace>();
    static SpaceManager& instance(){static SpaceManager manager;return manager;}
    ClientSpacePtr space(int){return value;}};
struct Data {struct Row{uint32 id;};std::vector<Row> chunks;};
struct Owner {struct Mapping{bool bound=false;std::shared_ptr<Data> data;std::weak_ptr<int> objects[2]={};};
    std::vector<Mapping> mappings;bool valid=true;bool current()const{return valid;}};
using StockDestructibles=std::shared_ptr<Owner>;
std::map<ClientSpace*,std::weak_ptr<Owner>> owners;
@METHOD@
int main(){auto& manager=SpaceManager::instance();std::vector<uint32> chunks{99};
    assert(!stockDestructibleChunks(1,chunks)&&chunks.empty());
    auto owner=std::make_shared<Owner>();owners[manager.value.get()]=owner;
    assert(!stockDestructibleChunks(1,chunks));
    owner->mappings.push_back({false,std::make_shared<Data>()});
    assert(!stockDestructibleChunks(1,chunks));
    owner->mappings[0].bound=true;assert(stockDestructibleChunks(1,chunks)&&chunks.empty());
    owner->mappings[0].data->chunks={{20},{10},{20}};
    assert(stockDestructibleChunks(1,chunks)&&(chunks==std::vector<uint32>{10,20}));
    owner->mappings.push_back({false,std::make_shared<Data>()});
    // Retired mapping slots do not invalidate another fully bound mapping.
    assert(stockDestructibleChunks(1,chunks)&&(chunks==std::vector<uint32>{10,20}));
    auto pending=std::make_shared<int>(1);owner->mappings[1].objects[0]=pending;
    assert(!stockDestructibleChunks(1,chunks)&&chunks.empty());
    owner->mappings[1].bound=true;owner->mappings[1].data->chunks={{30}};
    assert(stockDestructibleChunks(1,chunks)&&(chunks==std::vector<uint32>{10,20,30}));
    owner->mappings[1].data.reset();assert(!stockDestructibleChunks(1,chunks)&&chunks.empty());
    owner->valid=false;assert(!stockDestructibleChunks(1,chunks));
    manager.value->clear=true;assert(!stockDestructibleChunks(1,chunks));
}
'''.replace('@METHOD@', body))


if __name__ == '__main__':
    unittest.main()
