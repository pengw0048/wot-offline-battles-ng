"""Compile the actual identity readers for chunk and compiled collision owners."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def method(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


HOST = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>
#include <typeinfo>
using uint32 = std::uint32_t;
namespace BW { using string = std::string; }
struct Vector3 {
    float x, y, z;
    Vector3(float a, float b, float c) : x(a), y(b), z(c) {}
    float length() const { return std::sqrt(x*x+y*y+z*z); }
};
struct Matrix {
    Vector3 basis{1, 0, 0};
    Vector3 applyVector(const Vector3& value) const {
        assert(value.x == 1 && value.y == 0 && value.z == 0); return basis;
    }
};
struct SceneObject {
    const std::type_info* type = &typeid(void);
    void* object = nullptr;
    template<class T> explicit SceneObject(T* value) : type(&typeid(T)), object(value) {}
    SceneObject() = default;
    template<class T> bool isType() const { return *type == typeid(T); }
    template<class T> T* getAs() const { return static_cast<T*>(object); }
};
struct CollisionObstacle {
    SceneObject object;
    Matrix world;
    const SceneObject& sceneObject() const { return object; }
    const Matrix& transform() const { return world; }
};
struct StockObstacleIdentity {
    uint32 chunkID = 0, itemIndex = 0;
    BW::string filename;
    float scale = 0;
};
struct ChunkItem { virtual ~ChunkItem() {} };
struct StockChunkModel : ChunkItem {
    bool known = true;
    StockObstacleIdentity value{4, 9, "objects/wall.model", 99};
    bool identity(StockObstacleIdentity& output) const {
        if (!known) return false;
        output = value;
        return true;
    }
};
namespace CompiledSpace {
struct StockStaticModelSceneLoader {
    struct State {
        struct Instance {
            struct Item { bool known = true; uint32 chunkID = 0x80000000u, itemIndex = 12; } item;
        };
    };
    static bool itemIdentity(const SceneObject&, uint32&, uint32&);
    static bool modelFilename(const SceneObject&, BW::string&);
};
@COMPILED_READERS@
}
@ITEM_READERS@
@FULL_READER@
int main() {
    StockChunkModel chunk;
    CollisionObstacle obstacle{SceneObject(static_cast<ChunkItem*>(&chunk)), {{3, 4, 0}}};
    StockObstacleIdentity output;
    assert(readStockObstacleIdentity(obstacle, output));
    assert(output.chunkID == 4 && output.itemIndex == 9 && output.filename == "objects/wall.model");
    assert(output.scale == 5); // Collision world transform, never cached authored scale 99.
    CompiledSpace::StockStaticModelSceneLoader::State::Instance compiled;
    obstacle.object = SceneObject(&compiled);
    assert(readStockObstacleIdentity(obstacle, output));
    assert(output.chunkID == 0x80000000u && output.itemIndex == 12 && output.filename.empty());
    assert(output.scale == 5);
    obstacle.world.basis = {0, 0, 2};
    assert(readStockObstacleIdentity(obstacle, output) && output.scale == 2);
    auto fails = [&] {
        output = {11, 13, "sentinel", 17};
        assert(!readStockObstacleIdentity(obstacle, output));
        assert(output.chunkID == 11 && output.itemIndex == 13 && output.filename == "sentinel" && output.scale == 17);
    };
    for (float invalid : {0.f, INFINITY, NAN}) { obstacle.world.basis = {invalid, 0, 0}; fails(); }
    obstacle.world.basis = {1, 0, 0};
    compiled.item.known = false; fails();
    obstacle.object = SceneObject(); fails();
    obstacle.object = SceneObject(static_cast<CompiledSpace::StockStaticModelSceneLoader::State::Instance*>(nullptr)); fails();
    ChunkItem other;
    obstacle.object = SceneObject(&other); fails();
    chunk.known = false;
    obstacle.object = SceneObject(static_cast<ChunkItem*>(&chunk)); fails();
}
'''


class StockSceneIdentityTests(unittest.TestCase):
    def test_actual_compiled_and_chunk_readers_keep_known_empty_name_and_collision_scale(self):
        source = (ROOT / 'bigworld/stock_scene_identity.cpp').read_text()
        compiled = (ROOT / 'bigworld/stock_static_model_scene.cpp').read_text()
        code = (HOST.replace('@COMPILED_READERS@', '\n'.join(method(compiled, signature) for signature in (
            'bool StockStaticModelSceneLoader::itemIdentity(',
            'bool StockStaticModelSceneLoader::modelFilename(')))
            .replace('@ITEM_READERS@', '\n'.join(method(source, signature) for signature in (
                'bool readStockObstacleItem(', 'bool readStockObstacleFilename(')))
            .replace('@FULL_READER@', method(source, 'bool readStockObstacleIdentity(')))
        with tempfile.TemporaryDirectory(prefix='stock-scene-identity-') as directory:
            root = Path(directory)
            (root / 'host.cpp').write_text(code)
            built = subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra',
                                    '-Werror', '-UNDEBUG', str(root / 'host.cpp'), '-o', str(root / 'host')],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(root / 'host')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
