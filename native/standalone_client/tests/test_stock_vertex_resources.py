"""Exercise the production #1513 vertex format lookup and Vertices registration text."""
import base64
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile

from native.standalone_client.tests.test_stock_space_assets import load_patch, without_includes

ROOT = Path(__file__).resolve().parents[1]
cache_patch = load_patch('patch_stock_vertex_format_cache')
registration_patch = load_patch('patch_stock_vertices_registration')
ENGINE = 'BigWorld-Engine-14.4.1-3d3cc16ae60bb1d71cb014e9b0539e54e6d8c4fb/programming/bigworld/'

# D3D9 element sizes for the storages in original #1513 formats (storage table
# 0x01a1afb8, declaration table 0x01a1b0a0 and patch_processed_resources).
STORAGE_BYTES = {
    'FLOAT1': 4, 'FLOAT2': 8, 'FLOAT3': 12, 'FLOAT4': 16, 'COLOR': 4, 'FLOAT16_2': 4,
    'FLOAT16_4': 8, 'UBYTE2': 2, 'UBYTE3': 3, 'UBYTE4': 4, 'UBYTE4N': 4,
    'UBYTE4_NORMAL_8_8_8': 4, 'SHORT2': 4, 'SHORT4': 8, 'SC_FLOAT1_MULT3_I': 4,
    'SC_UBYTE3_MULT3_III': 3, 'SC_UBYTE4_MULT3_REVERSE_PADDED_IIIP': 4,
    'SC_UBYTE4_REVERSE_PADDED_PWWP': 4,
    # Public engine-res formats (public moo/unit_test/test_vertex_format.cpp sizes).
    'UBYTE1': 1, 'SHORT1': 2, 'SHORT3': 6, 'SHORT2N': 4, 'SHORT4N': 8, 'USHORT2N': 4,
    'USHORT4N': 8, 'UDEC3': 4, 'DEC3N': 4, 'SC_DIV3_III': 3, 'SC_REVERSE_III': 3,
    'SC_REVERSE_PADDED_III_': 4, 'SC_REVERSE_PADDED__WW_': 4, 'SC_REVERSE_PADDED_WW_': 3,
}
SEMANTICS = {'POSITION', 'BLENDWEIGHT', 'BLENDINDICES', 'NORMAL', 'PSIZE', 'TEXCOORD', 'TANGENT',
             'BINORMAL', 'TESSFACTOR', 'POSITIONT', 'COLOR', 'FOG', 'DEPTH', 'SAMPLE'}


def packed_tree(element):
    """Convert packed XML into (tag, text, children) as PackedSection::asString reads it."""
    from packed_xml import PackedElement, TYPE_COMPRESSED_STRING, TYPE_ELEMENT

    def text(value):
        if value.value_type == TYPE_COMPRESSED_STRING:
            # BW packs base64-decodable strings as blobs and re-encodes on read.
            return base64.b64encode(value.value).decode()
        if isinstance(value.value, bytes):
            return value.value.decode()
        return str(value.value)

    def convert(name, value):
        if value.value_type == TYPE_ELEMENT:
            nested = value.value
            return (name, text(nested.value), [convert(n.decode(), v) for n, v in nested.children])
        return (name, text(value), [])
    assert isinstance(element, PackedElement)
    return ('root', text(element.value), [convert(n.decode(), v) for n, v in element.children])


def xml_tree(node):
    return (node.tag, (node.text or '').strip(), [xml_tree(child) for child in node])


def definition(tree):
    """Mirror public VertexFormat::load: stream strides and D3D9 target names."""
    def child(node, name):
        return next((c for c in node[2] if c[0] == name), None)

    def read(node, name, default):
        found = child(node, name)
        return found[1].strip() if found is not None and found[1].strip() else default
    strides, targets = {}, {}
    for section in tree[2]:
        if section[0] == 'elements':
            for element in section[2]:
                if element[0] not in SEMANTICS:
                    continue
                last = max(strides) if strides else 0
                stream = int(read(element, 'stream', str(last)))
                strides[stream] = strides.get(stream, 0) + STORAGE_BYTES[read(element, 'type', 'FLOAT3')]
        elif section[0] == 'targets':
            for target in section[2]:
                if read(target, 'format', ''):
                    targets[read(target, 'name', '')] = read(target, 'format', '')
    return strides, targets


def format_resources(client=None, engine_res=None):
    """Return original and optional public engine-res formats in runtime path order."""
    sys.path.insert(0, str(ROOT.parents[1] / 'tools'))
    from packed_xml import read_packed_xml
    resources = {}
    if client:
        with zipfile.ZipFile(Path(client) / 'res/packages/shaders.pkg') as archive:
            for name in archive.namelist():
                if name.startswith('shaders/formats/') and name.endswith('.xml'):
                    resources[name] = definition(packed_tree(read_packed_xml(archive.read(name))))
    if engine_res:
        base = Path(engine_res)
        for path in sorted((base / 'shaders/formats').rglob('*.xml')):
            name = path.relative_to(base).as_posix()
            # engine-res follows the original packages in the runtime paths.xml.
            resources.setdefault(name, definition(xml_tree(ET.parse(path).getroot())))
    return resources


def synthetic_resources():
    """The original #1513 layout for the static geometry formats when no client is present."""
    target = {'D3D9': 'xyznuvtbpc'}
    return {'shaders/formats/xyznuvtb.xml': ({0: 32}, target),
            'shaders/formats/set3/xyznuvtbpc.xml': ({0: 32}, {}),
            'shaders/formats/uv2.xml': ({10: 8}, {'D3D9': 'uv2pc'}),
            'shaders/formats/set3/uv2pc.xml': ({10: 8}, {}),
            'shaders/formats/colour.xml': ({11: 4}, {}),
            'shaders/formats/instancing.xml': ({9: 4}, {})}


def resource_bytes(resources):
    def text(value):
        value = value.encode()
        return struct.pack('<I', len(value)) + value
    result = bytearray(struct.pack('<I', len(resources)))
    for name, (strides, targets) in sorted(resources.items()):
        result += text(name) + struct.pack('<I', len(strides))
        for stream, stride in sorted(strides.items()):
            result += struct.pack('<II', stream, stride)
        result += struct.pack('<I', len(targets))
        for key, value in sorted(targets.items()):
            result += text(key) + text(value)
    return bytes(result)


def engine_sources(names):
    archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():
        raise unittest.SkipTest('Pinned public engine source archive is required')
    with tarfile.open(archive) as source:
        return {name: source.extractfile(ENGINE + name).read().decode() for name in names}


# Boundary shims for the production vertex_format_cache.cpp text. Resources and
# DataSection parsing are replaced by exact per-format stream strides/targets.
CACHE_SHIMS = r'''
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_GUARD
#define MF_ASSERT(x) assert(x)
#define DECLARE_DEBUG_COMPONENT2(a, b)
namespace BW {
using uint32=std::uint32_t;
using string=std::string;
struct StringRef {
    const char* data_;size_t length_;
    static const size_t npos=size_t(-1);
    StringRef():data_(""),length_(0){}
    StringRef(const char* p):data_(p),length_(std::strlen(p)){}
    StringRef(const char* p,size_t n):data_(p),length_(n){}
    StringRef(const std::string& p):data_(p.data()),length_(p.size()){}
    bool empty()const{return !length_;}
    std::string to_string()const{return std::string(data_,length_);}
    size_t find(const StringRef& value)const{size_t at=to_string().find(value.to_string());return at==std::string::npos?npos:at;}
};
inline std::string operator+(const StringRef& a,const StringRef& b){return a.to_string()+b.to_string();}
inline std::string operator+(const std::string& a,const StringRef& b){return a+b.to_string();}
struct SimpleMutex{std::mutex value;};
struct SimpleMutexHolder{std::lock_guard<std::mutex> lock;explicit SimpleMutexHolder(SimpleMutex& m):lock(m.value){}};
template<class T> struct StringRefMap {
    typedef std::map<std::string,T> Map;typedef typename Map::iterator iterator;typedef typename Map::const_iterator const_iterator;
    typedef std::pair<StringRef,T> value_type;Map map_;
    iterator find(const StringRef& key){return map_.find(key.to_string());}
    iterator end(){return map_.end();}
    std::pair<iterator,bool> insert(const value_type& value){return map_.insert(std::make_pair(value.first.to_string(),value.second));}
};
template<class P=void> struct StringRefSet {
    std::set<std::string> set_;
    size_t count(const StringRef& key)const{return set_.count(key.to_string());}
    void insert(const StringRef& key){set_.insert(key.to_string());}
};
struct FormatResource{std::map<uint32,uint32> strides;std::map<std::string,std::string> targets;};
std::map<std::string,FormatResource> resources;
std::map<std::string,unsigned> opens;
std::mutex opensLock;
std::string raceBarrier;unsigned raceArrivals=0;std::condition_variable raceReady;
typedef const FormatResource* DataSectionPtr;
struct BWResource {
    static BWResource& instance(){static BWResource value;return value;}
    DataSectionPtr openSection(const std::string& path){
        std::unique_lock<std::mutex> lock(opensLock);
        ++opens[path];
        // Holds two lookups at the same resource so both load before inserting.
        if(path==raceBarrier){++raceArrivals;raceReady.notify_all();raceReady.wait(lock,[]{return raceArrivals>=2;});}
        auto found=resources.find(path);return found==resources.end()?nullptr:&found->second;
    }
};
namespace Moo {
struct VertexFormat {
    struct ConversionContext{};
    std::string name_;std::map<uint32,uint32> strides_;std::map<std::string,std::string> targets_;
    uint32 streamCount()const{return strides_.empty()?0:strides_.rbegin()->first+1;}
    uint32 streamStride(uint32 stream)const{auto found=strides_.find(stream);return found==strides_.end()?0:found->second;}
    uint32 countElements()const{return uint32(strides_.size());}
    StringRef name()const{return StringRef(name_.data(),name_.size());}
    StringRef getTargetFormatName(const StringRef& key)const{
        auto found=targets_.find(key.to_string());
        return found==targets_.end()?StringRef():StringRef(found->second.data(),found->second.size());}
    static bool load(VertexFormat& format,DataSectionPtr section,const StringRef& name){
        format.name_=name.to_string();format.strides_=section->strides;format.targets_=section->targets;
        return !format.strides_.empty();}
};
class VertexFormatCache {
public:
    static const VertexFormat* get(const StringRef& formatName);
    static const VertexFormat* find(const StringRef& formatName);
    static const VertexFormat* add(const StringRef& formatName,const VertexFormat& format);
    static string getResourcePath(const StringRef& formatName);
    static const VertexFormat* getTarget(const VertexFormat& source,const StringRef& key,bool fallback=true);
    template<class T> static StringRef getTargetName(const StringRef& key,bool fallback=true){
        const VertexFormat* source=get(T::name());const VertexFormat* target=source?getTarget(*source,key,fallback):nullptr;
        return target?target->name():StringRef();}
};
}}
'''

LOOKUP_MAIN = r'''
#include <fstream>
#include <thread>
int main(int argc,char** argv){
    assert(argc==3);std::ifstream input(argv[1],std::ios::binary);std::ofstream output(argv[2]);
    auto u32=[&](){std::uint32_t value=0;input.read(reinterpret_cast<char*>(&value),4);return value;};
    auto text=[&](){std::string value(u32(),'\0');input.read(&value[0],value.size());return value;};
    for(std::uint32_t n=u32();n;--n){
        std::string name=text();BW::FormatResource& resource=BW::resources[name];
        for(std::uint32_t s=u32();s;--s){std::uint32_t stream=u32();resource.strides[stream]=u32();}
        for(std::uint32_t t=u32();t;--t){std::string key=text();resource.targets[key]=text();}
    }
    using BW::Moo::VertexFormatCache;
    for(std::uint32_t n=u32();n;--n){
        const std::string command=text(),name=text();
        if(command=="race"){
            BW::raceBarrier="shaders/formats/"+name+".xml";const BW::Moo::VertexFormat* found[2]={};
            std::thread first([&]{found[0]=VertexFormatCache::get(name);});
            std::thread second([&]{found[1]=VertexFormatCache::get(name);});
            first.join();second.join();BW::raceBarrier.clear();
            output<<"race "<<name<<' '<<(found[0]&&found[0]==found[1])<<'\n';continue;
        }
        const BW::Moo::VertexFormat* source=VertexFormatCache::get(name);
        const BW::Moo::VertexFormat* target=source?VertexFormatCache::getTarget(*source,"D3D9",true):nullptr;
        output<<"get "<<name<<' '<<(source?source->name_:"NULL")<<' '<<(target?target->name_:"NULL")
              <<' '<<(target?target->streamStride(target->strides_.begin()->first):0)<<'\n';
    }
    for(const auto& open:BW::opens)output<<"open "<<open.first<<' '<<open.second<<'\n';
    return input.peek()==std::char_traits<char>::eof()?0:2;
}
'''

# Boundary shims for the production VerticesManager and Vertices::destroy text.
REGISTRATION_SHIMS = r'''
#include <cassert>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_GUARD
#define PROFILER_SCOPED(x)
#define MF_ASSERT(x) assert(x)
#define MF_ASSERT_DEV(x) assert(x)
#define IF_NOT_MF_ASSERT_DEV(x) if(!(x))
#define SINGLETON_MANAGER_WRAPPER(x)
#define REGISTER_SINGLETON(x)
#define DECLARE_DEBUG_COMPONENT2(a, b)
#define ENABLE_RELOAD_MODEL 1
unsigned errors=0,assets=0;
#define ERROR_MSG(...) (++errors)
#define ASSET_MSG(...) (++assets)
namespace BW {
template<class K,class V> using map=std::map<K,V>;
using string=std::string;
template<class T> void bw_safe_delete(T*& value){delete value;value=nullptr;}
struct SimpleMutex{std::recursive_mutex value;};
struct SimpleMutexHolder{std::lock_guard<std::recursive_mutex> lock;explicit SimpleMutexHolder(SimpleMutex& m):lock(m.value){}};
namespace Moo {
class Vertices;
template<class T> class SmartPointer {
    T* pointer_=nullptr;
    void retain(){if(pointer_)pointer_->incRef();}
    void release(){if(pointer_)pointer_->decRef();}
public:
    SmartPointer(T* value=nullptr):pointer_(value){retain();}
    SmartPointer(const SmartPointer& value):pointer_(value.pointer_){retain();}
    SmartPointer& operator=(const SmartPointer& value){if(this!=&value){release();pointer_=value.pointer_;retain();}return *this;}
    ~SmartPointer(){release();}
    T* operator->()const{return pointer_;}T& operator*()const{return *pointer_;}
    explicit operator bool()const{return pointer_!=nullptr;}
};
typedef SmartPointer<Vertices> VerticesPtr;
struct DeviceCallback{virtual ~DeviceCallback(){}virtual void deleteManagedObjects(){}virtual void createManagedObjects(){}};
unsigned liveVertices=0;
class Vertices {
    mutable int references_=0;std::string resourceID_;
public:
    explicit Vertices(const std::string& resourceID,int=0):resourceID_(resourceID)
#if ENABLE_RELOAD_MODEL
        ,isInVerticesManger_(@DEFAULT@)
#endif
    {++liveVertices;}
    ~Vertices(){--liveVertices;}
    void incRef()const{++references_;}
    void decRef()const{if(!--references_)destroy();}
    int refCount()const{return references_;}
    const std::string& resourceID()const{return resourceID_;}
    void load(){} void release(){}
    void destroy()const;
#if ENABLE_RELOAD_MODEL
    bool isInVerticesManger_;
    void isInVerticeManger(bool value){isInVerticesManger_=value;}
#endif
};
}}
@MANAGER_HEADER@
@MANAGER_SOURCE@
namespace BW { namespace Moo {
@DESTROY@
}}
int main(){
    using namespace BW::Moo;
    VerticesManager::init();VerticesManager* manager=VerticesManager::instance();
    VerticesPtr found[8];size_t count=0;
    auto registered=[&](const char* id,const Vertices* owner){
        manager->find(id,found,count);bool result=count==1&&&*found[0]==owner;
        for(auto& value:found)value=VerticesPtr();return result;};
    {
        // A live owner keeps its entry while a rejected duplicate is destroyed.
        VerticesPtr owner(new Vertices("static.primitives/vertices"));
        manager->populateResource("static.primitives/vertices",owner);
        {
            VerticesPtr duplicate(new Vertices("static.primitives/vertices"));
            manager->populateResource("static.primitives/vertices",duplicate);
        }
        std::printf("duplicateKeptOwner=%d\n",int(registered("static.primitives/vertices",&*owner)));
    }
    std::printf("ownerRemoved=%d\n",int(!registered("static.primitives/vertices",nullptr)));
    {
        // The loader's own registration path follows the same insert result.
        VerticesPtr loaded=manager->get("model.primitives/vertices");
        std::printf("getRegistered=%d\n",int(registered("model.primitives/vertices",&*loaded)));
    }
    std::printf("errors=%u assets=%u live=%u\n",errors,assets,liveVertices);
    VerticesManager::fini();return 0;
}
'''


def compile_cpp(directory, name, text):
    compiler = shutil.which(os.environ.get('CXX', 'c++'))
    if not compiler or os.name == 'nt':
        raise unittest.SkipTest('A Unix C++ compiler is needed for the production Moo text')
    source, binary = directory / (name + '.cpp'), directory / name
    source.write_text(text)
    subprocess.run([compiler, '-std=c++14', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-pthread',
                    str(source), '-o', str(binary)], check=True)
    return binary


class VertexResourcePatchTests(unittest.TestCase):
    def test_patchers_are_repeatable_and_fail_closed_on_unknown_seams(self):
        names = ['lib/moo/vertex_format_cache.cpp', 'lib/moo/vertices.hpp',
                 'lib/moo/vertices.cpp', 'lib/moo/vertices_manager.cpp']
        sources = engine_sources(names)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name, text in sources.items():
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                (root / name).write_text(text)
            cache_patch.patch(root)
            registration_patch.patch(root)
            once = {name: (root / name).read_bytes() for name in names}
            cache_patch.patch(root)
            registration_patch.patch(root)
            self.assertEqual(once, {name: (root / name).read_bytes() for name in names})
            self.assertNotIn(b'ENABLE_RELOAD_MODEL\n  isInVerticesManger_', once['lib/moo/vertices.cpp'])
            for patcher, name, seam in ((cache_patch, 'lib/moo/vertex_format_cache.cpp', 'formatsLock_;'),
                                        (registration_patch, 'lib/moo/vertices_manager.cpp', 'vertices_.insert(')):
                (root / name).write_text(sources[name].replace(seam, seam.replace('_', '__', 1)))
                saved = {other: (root / other).read_bytes() for other in names}
                with self.assertRaisesRegex(ValueError, 'Unrecognized pinned'):
                    patcher.patch(root)
                self.assertEqual(saved, {other: (root / other).read_bytes() for other in names})
                (root / name).write_text(sources[name])


class VertexFormatLookupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = engine_sources(['lib/moo/vertex_format_cache.cpp'])['lib/moo/vertex_format_cache.cpp']
        cls.temporary = tempfile.TemporaryDirectory(prefix='vertex-format-cache-')
        root = Path(cls.temporary.name)
        (root / 'lib/moo').mkdir(parents=True)
        (root / 'lib/moo/vertex_format_cache.cpp').write_text(source)
        cache_patch.patch(root)
        patched = (root / 'lib/moo/vertex_format_cache.cpp').read_text()
        cls.binaries = {variant: compile_cpp(root, 'lookup-' + variant,
                                             CACHE_SHIMS + without_includes(text) + LOOKUP_MAIN)
                        for variant, text in (('pinned', source), ('patched', patched))}
        cls.root = root

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def lookup(self, variant, resources, commands):
        def text(value):
            value = value.encode()
            return struct.pack('<I', len(value)) + value
        data = resource_bytes(resources) + struct.pack('<I', len(commands))
        data += b''.join(text(command) + text(name) for command, name in commands)
        source, output = self.root / 'lookup.bin', self.root / 'lookup.txt'
        source.write_bytes(data)
        subprocess.run([str(self.binaries[variant]), str(source), str(output)], check=True)
        lines = output.read_text().splitlines()
        results = [line.split()[1:] for line in lines if not line.startswith('open ')]
        opens = {line.split()[1]: int(line.split()[2]) for line in lines if line.startswith('open ')}
        return results, opens

    def test_candidate_order_missing_cache_and_concurrent_insert(self):
        resources = synthetic_resources()
        resources['shaders/formats/set3/prefixed.xml'] = ({0: 12}, {})
        resources['shaders/formats/plain.xml'] = ({0: 16}, {'D3D9': 'absent'})
        commands = [('get', 'xyznuvtb'), ('get', 'xyznuvtb'), ('get', 'set3/prefixed'),
                    ('get', 'plain'), ('get', 'missing'), ('get', 'missing'), ('race', 'colour')]
        results, opens = self.lookup('patched', resources, commands)
        self.assertEqual(results[:6], [
            ['xyznuvtb', 'xyznuvtb', 'set3/xyznuvtbpc', '32'],
            ['xyznuvtb', 'xyznuvtb', 'set3/xyznuvtbpc', '32'],
            ['set3/prefixed', 'set3/prefixed', 'set3/prefixed', '12'],
            # A declared target that cannot load never falls back to its source.
            ['plain', 'plain', 'NULL', '0'],
            ['missing', 'NULL', 'NULL', '0'], ['missing', 'NULL', 'NULL', '0']])
        self.assertEqual(results[6], ['colour', '1'])
        # Each absent or rejected candidate is opened once; a prefixed name has no second candidate.
        for path in ('set3/xyznuvtb', 'set3/missing', 'missing', 'set3/absent', 'absent', 'set3/plain'):
            self.assertEqual(opens['shaders/formats/%s.xml' % path], 1, path)
        self.assertNotIn('shaders/formats/set3/set3/prefixed.xml', opens)
        results, unused = self.lookup('pinned', resources, [('get', 'xyznuvtb'), ('race', 'colour')])
        self.assertEqual(results, [['xyznuvtb', 'xyznuvtb', 'NULL', '0'], ['colour', '0']])

    def test_original_static_geometry_formats(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for original vertex formats')
        layers = [('original', format_resources(client))]
        engine_res = os.environ.get('WOT_ENGINE_RES')
        if engine_res:
            layers.append(('engine-res', format_resources(client, engine_res)))
        names = ['xyznuv', 'xyznuvtb', 'xyznuviiiwwtb', 'uv2', 'colour', 'instancing']
        expected = {'xyznuv': ['set3/xyznuvpc', '24'], 'xyznuvtb': ['set3/xyznuvtbpc', '32'],
                    'xyznuviiiwwtb': ['set3/xyznuviiiwwtbpc', '40'], 'uv2': ['set3/uv2pc', '8'],
                    'colour': ['colour', '4'], 'instancing': ['instancing', '4']}
        for layer, resources in layers:
            results, unused = self.lookup('patched', resources, [('get', name) for name in names])
            self.assertEqual({row[0]: row[2:] for row in results}, expected, layer)
            pinned, unused = self.lookup('pinned', resources, [('get', name) for name in names])
            # Without set3/ the original layer has no target and engine-res supplies public set 1.
            self.assertTrue(all(row[2:] != expected[row[0]] for row in pinned if row[0] in names[:4]), layer)


class VerticesRegistrationTests(unittest.TestCase):
    def run_registration(self, patched):
        names = ['lib/moo/vertices_manager.hpp', 'lib/moo/vertices_manager.cpp',
                 'lib/moo/vertices.hpp', 'lib/moo/vertices.cpp']
        sources = engine_sources(names)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name, text in sources.items():
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                (root / name).write_text(text)
            if patched:
                registration_patch.patch(root)
            vertices = (root / 'lib/moo/vertices.cpp').read_text()
            # The production initializer and destroy() decide registration.
            default = 'false' if 'isInVerticesManger_( false )' in vertices else 'true'
            start = vertices.index('void Vertices::destroy() const')
            destroy = vertices[start:vertices.index('\n}\n', start) + 3]
            # The shim keeps the Hybrid flag; the pinned text decides its default.
            text = REGISTRATION_SHIMS.replace('@DEFAULT@', default).replace('@DESTROY@', destroy)
            text = text.replace('@MANAGER_HEADER@', without_includes((root / names[0]).read_text()))
            text = text.replace('@MANAGER_SOURCE@', without_includes((root / names[1]).read_text()))
            binary = compile_cpp(root, 'registration', text)
            return subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout

    def test_rejected_duplicate_does_not_deregister_the_owner(self):
        self.assertEqual(self.run_registration(True),
                         'duplicateKeptOwner=1\nownerRemoved=1\ngetRegistered=1\nerrors=0 assets=1 live=0\n')
        # Pinned Hybrid text: the duplicate's destroy erases the live owner's entry.
        pinned = self.run_registration(False)
        self.assertIn('duplicateKeptOwner=0', pinned)
        self.assertIn('errors=1', pinned)


if __name__ == '__main__':
    unittest.main()
