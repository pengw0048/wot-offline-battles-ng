"""Exercise the patched public picker body at its native/Python boundary."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SIGNATURE = 'bool EntityPicker::skeletonCheck('


def load_patch():
    spec = importlib.util.spec_from_file_location(
        'entity_picker_patch', ROOT / 'bigworld/patch_stock_entity_picker.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def function(source):
    start = source.index(SIGNATURE)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


PREFIX = r'''
#include <cassert>
#include <cstring>
#include <memory>
#define BW_GUARD do {} while (false)
static int errors = 0;
#define ERROR_MSG(...) (++errors)
struct Vector3 { int value; };
struct Matrix {
    Vector3 applyToOrigin() const { return Vector3{17}; }
    Vector3 applyToUnitAxisVector(int axis) const { assert(axis == 2); return Vector3{29}; }
};
struct PyObject { int marker = 23; };
struct PyEntity : PyObject {
    bool destroyed = false;
    bool isDestroyed() const { return destroyed; }
};
using PyEntityPtr = std::shared_ptr<PyEntity>;
struct Entity {
    virtual ~Entity() {}
    PyEntityPtr wrapper;
    PyEntityPtr pPyEntity() const { return wrapper; }
};
struct PyObjectPtr {
    enum Reference { STEAL_REFERENCE };
    PyObject* object;
    PyObjectPtr(PyObject* p, Reference) : object(p) {}
    explicit operator bool() const { return object != nullptr; }
    PyObject* get() const { return object; }
};
static PyObject* expected = nullptr;
static PyObject* attribute = nullptr;
static PyObject* colliderObject = nullptr;
static Entity* activeEntity = nullptr;
static bool wrongLayout = false, retireDuringLookup = false;
static int lookups = 0, cleared = 0, collided = 0;
static bool hit = false;
static std::weak_ptr<PyEntity> observedWrapper;
PyObject* PyObject_GetAttrString(PyObject* object, const char* name) {
    ++lookups;
    assert(std::strcmp(name, "skeletonCollider") == 0);
    if (object != expected) { wrongLayout = true; return nullptr; }
    assert(!observedWrapper.expired());
    if (retireDuringLookup) {
        activeEntity->wrapper->destroyed = true;
        activeEntity->wrapper.reset();
        // The production body must pin the wrapper across Python re-entry.
        assert(!observedWrapper.expired());
    }
    return attribute;
}
void PyErr_Clear() { ++cleared; }
struct SkeletonCollider : PyObject {
    static bool Check(PyObject* object) { return object == colliderObject; }
    bool doCollide(Vector3 start, Vector3 direction) {
        assert(start.value == 17 && direction.value == 29);
        ++collided;
        return hit;
    }
};
struct EntityPicker {
    bool skeletonCheck(const Entity&, const Matrix&) const;
};
static void setup(Entity& entity) {
    entity.wrapper = std::make_shared<PyEntity>();
    observedWrapper = entity.wrapper;
    expected = entity.wrapper.get();
    activeEntity = &entity;
    assert(static_cast<void*>(&entity) != static_cast<void*>(expected));
    wrongLayout = retireDuringLookup = false;
    lookups = cleared = collided = errors = 0;
    attribute = nullptr;
    colliderObject = nullptr;
}
'''


class EntityPickerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(os.environ.get('WOT_PUBLIC_BIGWORLD_ROOT',
                                   '/tmp/wot-bigworld-engine-14.4.1/programming/bigworld'))
        path = root / 'client/entity_picker.cpp'
        if path.is_file():
            cls.original = path.read_text(encoding='utf-8-sig')
            return
        archive = Path(os.environ.get('WOT_PUBLIC_BIGWORLD_ARCHIVE',
                                      '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            raise unittest.SkipTest('Pinned public BigWorld source or archive required')
        with tarfile.open(archive) as source:
            for member in source:
                if member.name.endswith('/programming/bigworld/client/entity_picker.cpp'):
                    cls.original = source.extractfile(member).read().decode('utf-8-sig')
                    return
        raise AssertionError('Pinned archive lacks the entity picker')

    def patched(self, root):
        path = root / 'client/entity_picker.cpp'
        path.parent.mkdir(parents=True)
        path.write_text(self.original)
        load_patch().patch(root)
        return path

    def compile_run(self, directory, name, body, main):
        compiler = shutil.which('c++')
        if compiler is None:
            self.skipTest('Host C++ compiler required')
        cpp, executable = directory / (name + '.cpp'), directory / name
        cpp.write_text(PREFIX + body + main)
        args = [compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-UNDEBUG']
        if os.name != 'nt':
            args += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        result = subprocess.run(args + [str(cpp), '-o', str(executable)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return subprocess.run([str(executable)], capture_output=True, text=True)

    def test_real_body_and_wrong_layout_negative_control(self):
        main = r'''
int main() {
    Entity entity; setup(entity);
    SkeletonCollider collider; colliderObject = attribute = &collider;
    hit = true;
    const bool selected = EntityPicker().skeletonCheck(entity, Matrix());
    if (wrongLayout) return 73;
    assert(selected && lookups == 1 && collided == 1 && cleared == 0);
}
'''
        with tempfile.TemporaryDirectory(prefix='entity-picker-body-') as directory:
            root = Path(directory)
            path = self.patched(root)
            original = self.compile_run(root, 'original', function(self.original), main)
            self.assertEqual(original.returncode, 73, original.stderr)
            fixed = self.compile_run(root, 'fixed', function(path.read_text()), main)
            self.assertEqual(fixed.returncode, 0, fixed.stderr)

    def test_real_body_preserves_collider_and_lifecycle_semantics(self):
        main = r'''
int main() {
    Entity entity; EntityPicker picker; Matrix look;
    // Missing and already-destroyed wrappers never enter CPython.
    assert(!picker.skeletonCheck(entity, look) && lookups == 0);
    setup(entity); entity.wrapper->destroyed = true;
    assert(!picker.skeletonCheck(entity, look) && lookups == 0);
    // Missing attributes retain the public optional-collider behavior.
    setup(entity);
    assert(picker.skeletonCheck(entity, look));
    assert(lookups == 1 && cleared == 1 && collided == 0 && !wrongLayout);
    // Non-colliders retain the public diagnostic and permissive behavior.
    setup(entity); PyObject other; attribute = &other;
    assert(picker.skeletonCheck(entity, look));
    assert(errors == 1 && collided == 0 && cleared == 0);
    // A real collider still accepts and rejects using its native result.
    setup(entity); SkeletonCollider collider; colliderObject = attribute = &collider;
    hit = false; assert(!picker.skeletonCheck(entity, look));
    hit = true; assert(picker.skeletonCheck(entity, look));
    assert(collided == 2 && lookups == 2 && cleared == 0 && errors == 0);
    // Attribute lookup may destroy the entity and drop its wrapper owner.
    setup(entity); retireDuringLookup = true;
    assert(!picker.skeletonCheck(entity, look));
    assert(cleared == 1 && collided == 0 && observedWrapper.expired());
    setup(entity); colliderObject = attribute = &collider; retireDuringLookup = true;
    assert(!picker.skeletonCheck(entity, look));
    assert(cleared == 0 && collided == 0 && observedWrapper.expired());
}
'''
        with tempfile.TemporaryDirectory(prefix='entity-picker-lifetime-') as directory:
            root = Path(directory)
            path = self.patched(root)
            result = self.compile_run(root, 'lifetime', function(path.read_text()), main)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_patch_idempotence_and_rejects_drift_without_writes(self):
        with tempfile.TemporaryDirectory(prefix='entity-picker-patch-') as directory:
            root = Path(directory)
            path = self.patched(root)
            patch = load_patch()
            original_bytes = path.read_bytes()
            before = path.stat().st_mtime_ns
            patch.patch(root)
            self.assertEqual(path.read_bytes(), original_bytes)
            self.assertEqual(path.stat().st_mtime_ns, before)
            self.assertNotIn('(PyObject*)(&entity)', path.read_text())
            for source in (self.original.replace('skeletonCollider', 'changedCollider'),
                           self.original.replace(patch.ORIGINAL, patch.REPLACEMENT, 1),
                           self.original + patch.ORIGINAL):
                path.write_text(source)
                before_bytes = path.read_bytes()
                with self.assertRaises(ValueError):
                    patch.patch(root)
                self.assertEqual(path.read_bytes(), before_bytes)


if __name__ == '__main__':
    unittest.main()
