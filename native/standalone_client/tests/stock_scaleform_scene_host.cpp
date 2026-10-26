// Runs the SDK 4.2.22 MovieClip scene bodies (pristine or patched) over a
// guarded AS3 fixture. A null Scene dereference is reported, not executed.
#include <cassert>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

typedef std::size_t UPInt;
#define SF_ASSERT(x) assert(x)
#define SF_UNUSED1(x) (void)(x)

struct NullDereference {};
template <class T> class SPtr
{
public:
    SPtr() : p_(NULL) {}
    SPtr(T * p) : p_(p) {}
    SPtr & operator=(T * p) { p_ = p; return *this; }
    T * operator->() const { if (!p_) throw NullDereference(); return p_; }
    operator T *() const { return p_; }
private:
    T * p_;
};

struct CharacterDef
{
    enum Type { Sprite = 1, MovieData = 2 };
    Type type;
    Type GetType() const { return type; }
};
struct MovieDataDef
{
    struct SceneInfo
    {
        std::string Name;
        unsigned Offset, NumFrames;
        std::vector<std::pair<unsigned, std::string> > Labels;
    };
    std::vector<SceneInfo> scenes;
    // LoadTaskData::GetScenes: no DefineSceneAndFrameLabelData yields NULL/0.
    const SceneInfo * GetScenes(UPInt * count) const
    {
        *count = scenes.size();
        return scenes.empty() ? NULL : &scenes[0];
    }
};
struct MovieDefImpl
{
    MovieDataDef * data;
    MovieDataDef * GetDataDef() const { return data; }
};
namespace GFx
{
struct Sprite
{
    CharacterDef def;
    MovieDefImpl resource;
    unsigned frame;
    std::vector<std::vector<std::string> > frameLabels;
    const CharacterDef * GetDef() const { return &def; }
    unsigned GetCurrentFrame() const { return frame; }
    MovieDefImpl * GetResourceMovieDef() { return &resource; }
};
}
struct DisplayObject
{
    GFx::Sprite * sprite;
    bool IsSprite() const { return sprite != NULL; }
    GFx::Sprite * CharToSprite_Unsafe() const { return sprite; }
};
struct Class { const char * name; };
struct Value;

namespace Instances
{
namespace fl_display { struct Scene; }
namespace fl
{
struct Array
{
    std::vector<std::pair<unsigned, std::string> > labels;
    std::vector<fl_display::Scene *> scenes;
    void PushBack(const Value & value);
};
}
namespace fl_display
{
// SDK Scene::labelsGet: a scene record lists its labels; a sprite scene lists
// every frame label of the sprite with one-based frame numbers.
struct Scene
{
    const MovieDataDef::SceneInfo * SceneInfo = NULL;
    GFx::Sprite * SpriteObj = NULL;
    void Set(const MovieDataDef::SceneInfo * info) { SceneInfo = info; }
    void Set(GFx::Sprite * sprite) { SpriteObj = sprite; }
    void labelsGet(SPtr<fl::Array> & result)
    {
        result = new fl::Array();
        if (SceneInfo)
        {
            result->labels = SceneInfo->Labels;
            return;
        }
        SF_ASSERT(SpriteObj);
        for (unsigned i = 0; i < SpriteObj->frameLabels.size(); ++i)
            for (const std::string & label : SpriteObj->frameLabels[i])
                result->labels.push_back(std::make_pair(i + 1, label));
    }
};
}
}
struct Value
{
    Value(Instances::fl_display::Scene * s) : scene(s) {}
    Instances::fl_display::Scene * scene;
};
void Instances::fl::Array::PushBack(const Value & value) { scenes.push_back(value.scene); }

struct VM
{
    Class sceneClass = { "flash.display.Scene" };
    int constructed = 0;
    bool failConstruct = false;
    int GetCurrentAppDomain() { return 7; }
    Class * GetClass(const char * name, int domain)
    {
        assert(std::string(name) == sceneClass.name && domain == 7);
        return &sceneClass;
    }
    Instances::fl::Array * MakeArray() { return new Instances::fl::Array(); }
};
struct ASVM : VM
{
    // A failed construction leaves the result empty, as a pending VM exception does.
    void ConstructInstance(SPtr<Instances::fl_display::Scene> & result, Class * cls, unsigned argc)
    {
        assert(cls == &sceneClass && argc == 0);
        if (failConstruct) { result = NULL; return; }
        ++constructed;
        result = new Instances::fl_display::Scene();
    }
};

namespace Instances
{
namespace fl_display
{
class MovieClip
{
public:
    DisplayObject * pDispObj;
    ASVM * vm;
    VM & GetVM() { return *vm; }
    void currentLabelsGet(SPtr<Instances::fl::Array> & result);
    void currentSceneGet(SPtr<Instances::fl_display::Scene> & result);
    void scenesGet(SPtr<Instances::fl::Array> & result);
    SPtr<Instances::fl_display::Scene> CreateFakeScene();
};
#include "@BODIES@"
}
}

namespace
{
using Instances::fl::Array;
using Instances::fl_display::MovieClip;
using Instances::fl_display::Scene;

std::string describe(Scene * scene, GFx::Sprite & sprite)
{
    if (!scene) return "null";
    if (scene->SceneInfo) return "scene:" + scene->SceneInfo->Name;
    return scene->SpriteObj == &sprite ? "sprite" : "foreign-sprite";
}

void report(const char * name, GFx::Sprite & sprite, ASVM & vm)
{
    DisplayObject object = { &sprite };
    MovieClip clip = { &object, &vm };
    std::string scene, labels, scenes;
    try
    {
        SPtr<Scene> current = NULL;
        clip.currentSceneGet(current);
        scene = describe(current, sprite);
    }
    catch (NullDereference &) { scene = "NULL_DEREFERENCE"; }
    try
    {
        SPtr<Array> result = new Array();
        clip.currentLabelsGet(result);
        if (!result) labels = "null";
        for (UPInt i = 0; result && i < result->labels.size(); ++i)
            labels += (i ? "," : "") + std::to_string(result->labels[i].first) + ":" +
                result->labels[i].second;
    }
    catch (NullDereference &) { labels = "NULL_DEREFERENCE"; }
    try
    {
        SPtr<Array> result = NULL;
        clip.scenesGet(result);
        for (UPInt i = 0; i < result->scenes.size(); ++i)
            scenes += (i ? "," : "") + describe(result->scenes[i], sprite);
    }
    catch (NullDereference &) { scenes = "NULL_DEREFERENCE"; }
    std::printf("%s scene=%s labels=[%s] scenes=[%s]\n", name, scene.c_str(), labels.c_str(),
        scenes.c_str());
}
}

int main()
{
    ASVM vm;
    // gui/flash/lobby.swf: one frame, label tag 43 on frame 0, no tag 86.
    MovieDataDef lobbyData;
    GFx::Sprite lobby = { { CharacterDef::MovieData }, { &lobbyData }, 0,
        { { "net_wg_app_impl_LobbyApp" } } };
    report("sceneless-root", lobby, vm);
    report("sceneless-root-repeat", lobby, vm);

    MovieDataDef authored;
    authored.scenes.push_back({ "Intro", 0, 2, { { 1, "start" } } });
    authored.scenes.push_back({ "Loop", 2, 3, { { 3, "loop" }, { 5, "end" } } });
    GFx::Sprite timeline = { { CharacterDef::MovieData }, { &authored }, 0,
        { { "start" }, {}, { "loop" }, {}, { "end" }, { "tail" } } };
    for (unsigned frame : { 0u, 1u, 2u, 4u, 5u })
    {
        timeline.frame = frame;
        const std::string name = "scene-root-frame-" + std::to_string(frame);
        report(name.c_str(), timeline, vm);
    }

    MovieDataDef unused;
    GFx::Sprite nested = { { CharacterDef::Sprite }, { &unused }, 0, { {}, { "open" } } };
    report("nested-sprite", nested, vm);

    vm.failConstruct = true;
    report("sceneless-root-construct-failure", lobby, vm);
    timeline.frame = 2;
    report("scene-root-construct-failure", timeline, vm);
    return 0;
}
