// Real SDK ByteArray regression. Use HeapPT, matching BigWorld's SysAlloc owner;
// the SDK default HeapMH can accidentally tolerate stack-local LH containers.
#ifdef _MSC_VER
#pragma warning(push)
// SDK headers carry warnings unrelated to the probe assertions.
#pragma warning(disable: 4244 4100 4189 4456 4458 4005)
#endif
#include <GFx/GFx_Loader.h>
#include <GFx/GFx_AS3Support.h>
#include <GFx/GFx_PlayerImpl.h>
#include <GFx/AMP/Amp_Server.h>
#include <GFx/AS3/AS3_MovieRoot.h>
#include <GFx/AS3/Obj/Utils/AS3_Obj_Utils_ByteArray.h>
#include <Kernel/HeapPT/HeapPT_SysAllocWinAPI.h>
#include <AS3/AS3_Global.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif
using namespace Scaleform;
using namespace Scaleform::GFx;

// FWS v9, minimal RECT/frame header, AS3 FileAttributes, ShowFrame and End.
// The immutable buffer outlives every MemoryFile lease; no disk fixture is needed.
class ProbeFileOpener : public FileOpener
{
public:
    File* OpenFile(const char* path, int, int) override
    {
        static const UByte swf[] = {
            0x46, 0x57, 0x53, 0x09, 0x18, 0x00, 0x00, 0x00,
            0x08, 0x00, 0x00, 0x0c, 0x01, 0x00, 0x44, 0x11,
            0x08, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00
        };
        if (std::strcmp(path, "amf3-empty.swf") != 0) return NULL;
        return new MemoryFile(String(path), swf, sizeof(swf));
    }
    SInt64 GetFileModifyTime(const char*) override { return 0; }
};

static void check(bool condition, const char* label)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(2); }
}

struct Probe
{
    AS3::ASVM& vm;
    AS3::Value create(const char* name)
    {
        AS3::Value value;
        check(vm.CreateObject(name, vm.GetFrameAppDomain(), value, 0, NULL), name);
        check(!vm.IsException(), "create exception");
        return value;
    }
    AS3::Value text(const char* s) { return AS3::Value(vm.GetStringManager().CreateString(s)); }
    void set(const AS3::Value& o, const char* key, const AS3::Value& value)
    {
        const AS3::Multiname name(vm.GetPublicNamespace(), text(key));
        check(o.GetObject()->SetProperty(name, value), "set property");
        check(!vm.IsException(), "set exception");
    }
    AS3::Value get(const AS3::Value& o, const char* key)
    {
        AS3::Value value;
        const AS3::Multiname name(vm.GetPublicNamespace(), text(key));
        check(o.GetObject()->GetProperty(name, value), "get property");
        check(!vm.IsException(), "get exception");
        return value;
    }
    AS3::Value call(const AS3::Value& o, const char* name, unsigned argc = 0,
                    const AS3::Value* argv = NULL)
    {
        AS3::Value value;
        o.GetObject()->ExecutePropertyUnsafe(vm.GetStringManager().CreateString(name),
                                             value, argc, argv);
        if (vm.IsException())
        {
            AS3::Value error = vm.GetExceptionValue();
            vm.IgnoreException();
            ASString message = vm.GetStringManager().CreateEmptyString();
            error.Convert2String(message);
            std::fprintf(stderr, "AS3 %s: %s\n", name, message.ToCStr());
            check(false, "ByteArray thunk exception");
        }
        return value;
    }
    static AS3::Instances::fl_utils::ByteArray& bytes(const AS3::Value& value)
    {
        return *static_cast<AS3::Instances::fl_utils::ByteArray*>(value.GetObject());
    }
    void graph()
    {
        AS3::Value root = create("Object"), child = create("Object");
        AS3::Value point = create("flash.geom.Point");
        vm.RegisterClassAlias(vm.GetStringManager().CreateString("probe.Point"),
                              point.GetObject()->GetTraits().GetConstructor());
        set(point, "x", AS3::Value(SInt32(3)));
        set(point, "y", AS3::Value(SInt32(7)));
        set(root, "self", root);
        set(root, "left", child);
        set(root, "right", child);
        set(root, "point", point);
        set(child, "owner", root);
        set(root, "text", text("repeated"));
        set(child, "text", text("repeated"));
        AS3::Value buffer = create("flash.utils.ByteArray");
        call(buffer, "writeObject", 1, &root);
        const UInt32 first = bytes(buffer).GetLength();
        check(first > 1, "nonempty object encoding");
        call(buffer, "writeObject", 1, &root);
        check(bytes(buffer).GetLength() == first * 2, "fresh writer reference scope");
        check(std::memcmp(bytes(buffer).GetDataPtr(), static_cast<const UInt8*>(bytes(buffer).GetDataPtr()) + first,
                          first) == 0, "repeated calls encode complete objects");
        bytes(buffer).positionSet(0);
        AS3::Value a = call(buffer, "readObject"), b = call(buffer, "readObject");
        check(a.GetObject() != b.GetObject(), "fresh reader reference scope");
        check(a.GetObject() != root.GetObject(), "decoded independent graph");
        check(get(a, "self").GetObject() == a.GetObject(), "root cycle");
        const AS3::Value left = get(a, "left");
        check(left.GetObject() == get(a, "right").GetObject(), "shared object reference");
        check(get(left, "owner").GetObject() == a.GetObject(), "mutual cycle");
        check(get(left, "text").AsString() == get(a, "text").AsString(), "string reference");
        check(get(get(a, "point"), "x").AsNumber() == 3, "sealed member table");
        check(bytes(buffer).positionGet() == bytes(buffer).GetLength(), "complete read");
    }
    void selfWrite()
    {
        AS3::Value value = create("flash.utils.ByteArray");
        const UInt8 initial[] = {0x61, 0x62};
        bytes(value).Set(initial, 2);
        bytes(value).positionSet(2);
        call(value, "writeObject", 1, &value);
        const UInt8 expected[] = {0x61, 0x62, 0x0c, 0x07, 0x61, 0x62, 0x0c};
        check(bytes(value).GetLength() == sizeof(expected), "self-write length");
        check(std::memcmp(bytes(value).GetDataPtr(), expected, sizeof(expected)) == 0,
              "self-write source snapshot");
        bytes(value).positionSet(2);
        const AS3::Value decoded = call(value, "readObject");
        check(bytes(decoded).GetLength() == 3, "self-write decoded length");
    }
};

int main()
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    for (unsigned lifetime = 0; lifetime != 3; ++lifetime)
    {
        {
            SysAllocWinAPI allocator;
            Scaleform::GFx::System system(&allocator);
            Ptr<AMP::ServerState> state = *new AMP::ServerState;
            state->StateFlags = AMP::Amp_Disabled;
            AmpServer::GetInstance().UpdateState(state);
            state.Clear();
            Loader loader;
            loader.SetFileOpener(Ptr<FileOpenerBase>(*new ProbeFileOpener));
            loader.SetAS3Support(Ptr<ASSupport>(*new AS3Support));
            Ptr<MovieDef> definition = *loader.CreateMovie("amf3-empty.swf", Loader::LoadAll | Loader::LoadWaitCompletion);
            check(definition.GetPtr() != NULL, "AS3 fixture load");
            Ptr<Movie> movie = *definition->CreateInstance(true);
            check(movie.GetPtr() != NULL, "AS3 instance");
            AS3::ASVM* vm = static_cast<AS3::MovieRoot*>(movie->pASMovieRoot.GetPtr())->GetAVM();
            check(vm != NULL, "AS3 VM");
            Probe probe = {*vm};
            std::printf("HeapPT lifetime %u: ByteArray scopes\n", lifetime);
            std::fflush(stdout);
            for (unsigned pass = 0; pass != 64; ++pass)
            {
                probe.graph();
                probe.selfWrite();
                vm->GetGC().ForceCollect(NULL);
            }
            std::printf("PASS HeapPT lifetime %u: 64 graph/shared/cycle/read/write/self-write scopes\n", lifetime);
            std::fflush(stdout);
        }
        check(Memory::GetGlobalHeap() == NULL, "global heap shutdown");
        check(!Scaleform::System::HasMemoryLeaks, "SDK heap leak check");
    }
    std::puts("PASS real SDK ByteArray scope cleanup and full SDK reinitialization");
}
