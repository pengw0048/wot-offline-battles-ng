"""Exercise the production BWSG/BSGD reader at explicit resource/GPU boundaries."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

from native.standalone_client.tests.test_stock_space_assets import (
    HARNESS as ASSET_HARNESS, load_patch, without_includes, table_bytes)
from native.standalone_client.tests.test_stock_vertex_resources import (
    cache_patch, format_resources, resource_bytes, synthetic_resources)

ROOT = Path(__file__).resolve().parents[1]
patch = load_patch('patch_stock_space_geometry')
strings_patch = load_patch('patch_stock_space_strings')

HARNESS = ASSET_HARNESS.split('struct Request')[0] + r'''
using string=std::string;
using D3DPOOL=int;
const D3DPOOL D3DPOOL_MANAGED=1;
#define FAILED(value) ((value)<0)
#define BW_GUARD
#define DECLARE_DEBUG_COMPONENT2(a, b)
unsigned fault=0,liveVertices=0;
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
// Resource boundary: exact original (and optional engine-res) stream strides and targets.
struct FormatResource{std::map<uint32,uint32> strides;std::map<std::string,std::string> targets;};
std::map<std::string,FormatResource> resources;
typedef const FormatResource* DataSectionPtr;
struct BWResource {
    static BWResource& instance(){static BWResource value;return value;}
    DataSectionPtr openSection(const std::string& path){
        if(fault==4)return nullptr;
        auto found=resources.find(path);return found==resources.end()?nullptr:&found->second;}
};
namespace Moo {
std::vector<std::shared_ptr<std::vector<unsigned char>>> buffers;
struct VertexBuffer {
    std::shared_ptr<std::vector<unsigned char>> bytes;
    int create(uint32 size,int,int,D3DPOOL,const char*) {
        if(fault==1)return -1;
        bytes=std::make_shared<std::vector<unsigned char>>(size);buffers.push_back(bytes);return 0;
    }
    int lock(uint32,uint32,void** pointer,int){
        if(fault==2)return -1;*pointer=fault==8?nullptr:bytes->data();return 0;
    }
    int unlock(){return fault==3?-1:0;}
    void addToPreloadList(){}
};
struct RenderContext {static D3DPOOL patchD3DPool(D3DPOOL p){return p;}};
struct VertexFormat {
    struct ConversionContext{};
    std::string name_;std::map<uint32,uint32> strides_;std::map<std::string,std::string> targets_;
    unsigned streamCount()const{return strides_.empty()?0:strides_.rbegin()->first+1;}
    uint32 streamStride(uint32 stream)const{auto found=strides_.find(stream);return found==strides_.end()?0:found->second;}
    uint32 countElements()const{return uint32(strides_.size());}
    StringRef name()const{return StringRef(name_.data(),name_.size());}
    StringRef getTargetFormatName(const StringRef& key)const{
        auto found=targets_.find(key.to_string());
        return found==targets_.end()?StringRef():StringRef(found->second.data(),found->second.size());}
    // Element parsing stays outside this harness; the fixture holds its strides.
    static bool load(VertexFormat& format,DataSectionPtr section,const StringRef& name){
        format.name_=name.to_string();format.strides_=section->strides;format.targets_=section->targets;
        return !format.strides_.empty();}
};
struct InstancingStream{static StringRef name(){return StringRef("instancing",10);}};
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
// Public 14.4.1 VertexDeclaration::get opens the named definition directly;
// combine/merge rejects a stream that both declarations define.
struct VertexDeclaration {
    std::string name_;VertexFormat format_;
    const VertexFormat& format()const{return format_;}
    static const char* getTargetDevice(){return "D3D9";}
    static std::map<std::string,std::unique_ptr<VertexDeclaration>>& cache(){static std::map<std::string,std::unique_ptr<VertexDeclaration>> value;return value;}
    static VertexDeclaration* get(const StringRef& name){
        if(fault==5)return nullptr;
        auto& values=cache();auto found=values.find(name.to_string());
        if(found!=values.end())return found->second.get();
        DataSectionPtr section=BWResource::instance().openSection(VertexFormatCache::getResourcePath(name));
        std::unique_ptr<VertexDeclaration> value(new VertexDeclaration);value->name_=name.to_string();
        if(!section||!VertexFormat::load(value->format_,section,name))return nullptr;
        return (values[value->name_]=std::move(value)).get();
    }
    static VertexDeclaration* combine(VertexDeclaration* a,VertexDeclaration* b){
        if(fault==7||!a||!b)return nullptr;
        auto& values=cache();const std::string key=a->name_+"_"+b->name_;auto found=values.find(key);
        if(found!=values.end())return found->second.get();
        std::unique_ptr<VertexDeclaration> value(new VertexDeclaration(*a));value->name_=key;
        for(const auto& stream:b->format_.strides_)
            if(!value->format_.strides_.insert(stream).second)return nullptr;
        return (values[key]=std::move(value)).get();
    }
};
struct VertexStreamDesc {
    uint32 streamIndex_;const char* formatName_;
    using StreamDescs=std::vector<VertexStreamDesc>;
    const VertexFormat* defaultVertexFormat()const{return fault==6?nullptr:VertexFormatCache::get(formatName_);}
    static const StreamDescs& getKnownStreams(){static StreamDescs value{{10,"uv2"},{11,"colour"}};return value;}
};
class Vertices:public ReferenceCount {
public:
    std::string resource;uint32 count=0;
    struct Stream {uint32 index,offset,stride,size;VertexBuffer buffer;};
    std::vector<Stream> streams;
    Vertices(const std::string& value,int):resource(value){++liveVertices;}
    ~Vertices(){--liveVertices;}
    const std::string& resourceID()const{return resource;}
    void initFromValues(uint32 size,const char* format){assert(format&&*format);count=size;}
    void addStream(uint32 i,const VertexBuffer& b,uint32 o,uint32 stride,uint32 size){
        assert(b.bytes&&o+size<=b.bytes->size());streams.push_back({i,o,stride,size,b});}
};
using VerticesPtr=SmartPointer<Vertices>;
struct VerticesManager {
    std::vector<VerticesPtr> published;
    static VerticesManager* instance(){static VerticesManager value;return &value;}
    void populateResource(const std::string& resource,const VerticesPtr& value){assert(value.get()->resource==resource);published.push_back(value);}
};
}
namespace CompiledSpace {
class BinaryFormat {
public:
    class Stream {
        std::vector<unsigned char>& bytes_;size_t cursor_=0;
    public:
        explicit Stream(std::vector<unsigned char>& bytes):bytes_(bytes){}
        size_t tell()const{return cursor_;}size_t size()const{return bytes_.size();}
        bool eof()const{return cursor_==bytes_.size();}
        void* readRaw(size_t n){if(cursor_>bytes_.size()||n>bytes_.size()-cursor_)return nullptr;
            void* p=bytes_.data()+cursor_;cursor_+=n;return p;}
        bool read(uint32& value){void* p=readRaw(4);if(!p)return false;std::memcpy(&value,p,4);return true;}
    };
    Stream metadata,data;unsigned version,dataVersion,metaOpen=0,dataOpen=0,metaClose=0,dataClose=0;
    BinaryFormat(std::vector<unsigned char>& m,std::vector<unsigned char>& d,unsigned a,unsigned b):metadata(m),data(d),version(a),dataVersion(b){}
    Stream* findAndOpenSection(FourCC magic,uint32 requested,const char*) {
        assert(requested==2);
        if(magic.value=="BWSG"){if(version!=2)return nullptr;++metaOpen;return &metadata;}
        assert(magic.value=="BSGD");if(dataVersion!=2)return nullptr;++dataOpen;return &data;
    }
    void closeSection(Stream* stream){if(stream==&metadata)++metaClose;else{assert(stream==&data);++dataClose;}}
    const char* resourceID()const{return "fixture";}
};
}}
@CACHE_SOURCE@
@STRING_TYPES@
@GEOMETRY_TYPES@
@STRING_HEADER@
@GEOMETRY_HEADER@
@STRING_SOURCE@
@GEOMETRY_SOURCE@
int main(int argc,char** argv){
    assert(argc==3);std::ifstream input(argv[1],std::ios::binary);std::ofstream output(argv[2],std::ios::binary);
    if(!input||!output)return 2;
    auto read=[&](std::uint32_t& value){return bool(input.read(reinterpret_cast<char*>(&value),4));};
    auto u32=[&](){std::uint32_t value=0;read(value);return value;};
    auto text=[&](){std::string value(u32(),'\0');input.read(&value[0],value.size());return value;};
    for(std::uint32_t n=u32();n;--n){
        std::string name=text();BW::FormatResource& resource=BW::resources[name];
        for(std::uint32_t s=u32();s;--s){std::uint32_t stream=u32();resource.strides[stream]=u32();}
        for(std::uint32_t t=u32();t;--t){std::string key=text();resource.targets[key]=text();}
    }
    auto write=[&](std::uint32_t value){output.write(reinterpret_cast<char*>(&value),4);};
    auto blob=[&](std::vector<unsigned char>& data){std::uint32_t bytes;if(!read(bytes)||bytes>512*1024*1024)return false;
        data.resize(bytes);return bool(input.read(reinterpret_cast<char*>(data.data()),bytes));};
    std::uint32_t cases;if(!read(cases))return 2;
    for(std::uint32_t n=0;n<cases;++n){
        std::uint32_t version,dataVersion;if(!read(version)||!read(dataVersion)||!read(BW::fault))return 2;
        std::vector<unsigned char> metadata,data;if(!blob(metadata)||!blob(data))return 2;
        assert(!BW::liveVertices);BW::Moo::buffers.clear();
        BW::CompiledSpace::BinaryFormat reader(metadata,data,version,dataVersion);
        BW::CompiledSpace::StaticGeometry owner;bool valid=owner.read(reader);write(valid);
        auto& published=BW::Moo::VerticesManager::instance()->published;
        if(valid){
            assert(owner.isValid()&&owner.percentLoaded()==1.f);
            write(static_cast<std::uint32_t>(published.size()));
            size_t offset=0;
            for(const auto& buffer:BW::Moo::buffers){
                assert(offset+buffer->size()<=data.size());
                assert(std::memcmp(data.data()+offset,buffer->data(),buffer->size())==0);offset+=buffer->size();}
            assert(offset==data.size());
            for(const auto& value:published){
                const auto& resource=value.get()->resource;
                write(static_cast<std::uint32_t>(resource.size()));output.write(resource.data(),resource.size());
                write(value.get()->count);write(static_cast<std::uint32_t>(value.get()->streams.size()));
                for(const auto& stream:value.get()->streams){write(stream.index);write(stream.offset);write(stream.stride);write(stream.size);}
            }
            // A second load cannot republish while existing native leases exist.
            if(!published.empty())assert(!owner.loadGeometry());
        }else{assert(published.empty());assert(!BW::liveVertices);}
        owner.close();owner.close();assert(!owner.isValid()&&owner.percentLoaded()==0.f);
        assert(reader.metaOpen==reader.metaClose&&reader.dataOpen==reader.dataClose);
        // Other scene owners can retain the real resources after mapping teardown.
        for(const auto& value:published)assert(!value.get()->streams.empty());
        published.clear();assert(!BW::liveVertices);BW::Moo::buffers.clear();
    }
    return input.peek()==std::char_traits<char>::eof()&&output?0:2;
}
'''
# The production StringRef accepts C strings and BW strings implicitly.
HARNESS = HARNESS.replace('#include <map>', '#include <map>\n#include <memory>\n#include <mutex>\n#include <set>')
HARNESS = HARNESS.replace('T* get()const{return pointer_;}',
    'T* get()const{return pointer_;} T* operator->()const{return pointer_;}')
HARNESS = HARNESS.replace('const char* data;size_t length;',
    'const char* data;size_t length;\n    StringRef(const char* p):data(p),length(std::strlen(p)){}\n'
    '    StringRef(const std::string& p):data(p.data()),length(p.size()){}\n'
    '    static const size_t npos=size_t(-1);StringRef():data(""),length(0){}\n'
    '    bool empty()const{return !length;}\n'
    '    size_t find(const StringRef& v)const{size_t at=to_string().find(v.to_string());return at==std::string::npos?npos:at;}')


def array(rows, fmt):
    stride = struct.calcsize(fmt)
    return struct.pack('<II', stride, len(rows)) + b''.join(struct.pack(fmt, *row) for row in rows)


def fixture():
    strings = [(7, b'objects/test.primitives/vertices'), (8, b'xyznuvtb')]
    data = bytes(range(80))
    entries = [(7, 0, 2, 2, 8)]
    streams = [(0, 32, 64, 0, 0), (10, 8, 16, 0, 64)]
    metadata = table_bytes(strings) + array(entries, '<5I') + array(streams, '<5I') + array([(80,)], '<I')
    return metadata, data


def describe(metadata):
    stride, count = struct.unpack_from('<II', metadata)
    offset = 8 + stride * count
    length, = struct.unpack_from('<I', metadata, offset)
    pool = offset + 4
    strings = {key: metadata[pool + start:pool + start + size]
               for key, start, size in struct.iter_unpack('<III', metadata[8:offset])}
    offset = pool + length
    arrays = []
    for fmt in ('<5I', '<5I', '<I'):
        stride, count = struct.unpack_from('<II', metadata, offset)
        offset += 8
        arrays.append(list(struct.iter_unpack(fmt, metadata[offset:offset + stride * count])))
        offset += stride * count
    assert offset == len(metadata)
    entries, streams, buffers = arrays
    result = bytearray(struct.pack('<II', 1, len(entries)))
    for key, begin, end, vertices, unused_format in entries:
        resource = strings[key]
        result += struct.pack('<I', len(resource)) + resource + struct.pack('<II', vertices, end - begin)
        for index, stride, size, unused_buffer, position in streams[begin:end]:
            result += struct.pack('<4I', index, position, stride, size)
    return result, len(entries), len(streams), len(buffers)


class StaticGeometryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            raise unittest.SkipTest('Pinned public engine source archive is required')
        cls.temporary = tempfile.TemporaryDirectory(prefix='stock-space-geometry-')
        cls.root = Path(cls.temporary.name)
        directory = cls.root / 'lib/compiled_space'
        directory.mkdir(parents=True)
        names = ('string_table_types.hpp', 'static_geometry_types.hpp', 'string_table.hpp',
                 'static_geometry.hpp', 'string_table.cpp', 'static_geometry.cpp')
        cache = cls.root / 'lib/moo/vertex_format_cache.cpp'
        cache.parent.mkdir(parents=True)
        with tarfile.open(archive) as source:
            members = source.getnames()
            for name in names:
                member = next(n for n in members if n.endswith('/programming/bigworld/lib/compiled_space/' + name))
                (directory / name).write_bytes(source.extractfile(member).read())
            member = next(n for n in members if n.endswith('/programming/bigworld/lib/moo/vertex_format_cache.cpp'))
            cache.write_bytes(source.extractfile(member).read())
        pinned_cache = cache.read_text()
        cache_patch.patch(cls.root)
        strings_patch.patch(cls.root)
        patch.patch(cls.root)
        before = {name: (directory / name).read_bytes() for name in names}
        patch.patch(cls.root)
        assert before == {name: (directory / name).read_bytes() for name in names}
        markers = ('STRING_TYPES', 'GEOMETRY_TYPES', 'STRING_HEADER', 'GEOMETRY_HEADER', 'STRING_SOURCE', 'GEOMETRY_SOURCE')
        cls.binaries = {}
        # The pinned lookup reproduces the runtime failure; the patched one is #1513 0x009a8000.
        for variant, lookup in (('patched', cache.read_text()), ('pinned', pinned_cache)):
            text = HARNESS.replace('@CACHE_SOURCE@', without_includes(lookup))
            for marker, name in zip(markers, names):
                text = text.replace('@' + marker + '@', without_includes((directory / name).read_text()))
            source = cls.root / (variant + '.cpp')
            source.write_text(text)
            cls.binaries[variant] = cls.root / variant
            subprocess.run(['c++', '-std=c++14', '-O1', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-const-variable', '-Wno-unused-local-typedefs',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                            str(source), '-o', str(cls.binaries[variant])], check=True)
        client = os.environ.get('WOT_0922_CLIENT')
        cls.resources = format_resources(client) if client else synthetic_resources()

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_cases(self, cases, variant='patched', resources=None):
        resources = self.resources if resources is None else resources
        inputs = bytearray(resource_bytes(resources) + struct.pack('<I', len(cases)))
        expected = bytearray()
        for version, data_version, fault, metadata, data, valid in cases:
            inputs += struct.pack('<4I', version, data_version, fault, len(metadata)) + metadata
            inputs += struct.pack('<I', len(data)) + data
            expected += describe(metadata)[0] if valid else struct.pack('<I', 0)
        source, output = self.root / 'inputs.bin', self.root / 'outputs.bin'
        source.write_bytes(inputs)
        subprocess.run([str(self.binaries[variant]), str(source), str(output)], check=True)
        self.assertEqual(output.read_bytes(), expected)

    def test_gpu_native_resource_ownership_and_failure_transactions(self):
        metadata, data = fixture()
        self.run_cases([(2, 2, 0, metadata, data, True)])
        # Lookup and declaration caches are process-wide, so each fault starts fresh.
        for fault in range(1, 9):
            self.run_cases([(2, 2, fault, metadata, data, False)])

    def test_invalid_section_versions_arrays_and_stream_ranges(self):
        metadata, data = fixture()
        cases = [(1, 2, 0, metadata, data, False), (2, 1, 0, metadata, data, False)]
        cases += [(2, 2, 0, metadata[:length], data, False) for length in range(len(metadata))]
        cases += [(2, 2, 0, metadata, data[:-1], False), (2, 2, 0, metadata, data + b'x', False),
                  (2, 2, 0, metadata + b'x', data, False)]
        table_size = len(table_bytes([(7, b'objects/test.primitives/vertices'), (8, b'xyznuvtb')]))
        for position, value in ((table_size, 4), (table_size + 4, 0xffffffff),
                (table_size + 8, 99), (table_size + 12, 10), (table_size + 16, 10),
                (table_size + 20, 0xffffffff), (table_size + 24, 99),
                (table_size + 28, 4), (table_size + 36 + 4, 0),
                (table_size + 36 + 12, 99), (table_size + 36 + 16, 80)):
            altered = bytearray(metadata)
            struct.pack_into('<I', altered, position, value)
            cases.append((2, 2, 0, altered, data, False))
        self.run_cases(cases)

    def test_all_original_vertex_buffers_and_resource_streams(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for original compiled geometry')
        sys.path.insert(0, str(ROOT.parents[1] / 'tools'))
        from space_bin_0922 import CompiledSpace
        # The runtime also mounts public engine-res, whose set-1 targets shadow nothing in #1513.
        layers = [self.resources]
        if os.environ.get('WOT_ENGINE_RES'):
            layers.append(format_resources(client, os.environ['WOT_ENGINE_RES']))
        total = [0, 0, 0, 0]
        for package in (Path(client) / 'res/packages').glob('*.pkg'):
            with zipfile.ZipFile(package) as archive:
                for name in archive.namelist():
                    if not name.endswith('/space.bin'):
                        continue
                    space = CompiledSpace(archive.read(name))
                    metadata, data = space.section_data('BWSG'), space.section_data('BSGD')
                    for resources in layers:
                        self.run_cases([(2, 2, 0, metadata, data, True)], resources=resources)
                        self.run_cases([(2, 2, 0, metadata, data, False)], 'pinned', resources)
                    counts = describe(metadata)[1:]
                    total[0] += 1
                    for i, value in enumerate(counts, 1):
                        total[i] += value
        self.assertGreater(total[0], 0)
        print('Production static geometry consumed %d original maps, %d resources, %d streams, %d exact vertex buffers'
              ' over %d resource layers; the pinned lookup rejected every map' % (tuple(total) + (len(layers),)))


if __name__ == '__main__':
    unittest.main()
