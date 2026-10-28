#include "pch.hpp"
#include "stock_space_control_points.hpp"
#include "stock_space_control_points_data.hpp"
#include "stock_control_point_radius.hpp"
#include "stock_space_sounds.hpp"
#include "compiled_space/binary_format.hpp"
#include "compiled_space/compiled_space.hpp"
#include "compiled_space/string_table.hpp"
#include "model/model_animation.hpp"
#include "model/super_model.hpp"
#include "model/super_model_animation.hpp"
#include "moo/complex_effect_material.hpp"
#include "moo/effect_property.hpp"
#include "moo/node.hpp"
#include "moo/render_context.hpp"
#include "moo/texture_manager.hpp"
#include "pyscript/script_math.hpp"
#include "resmgr/xml_section.hpp"
#include "scene/draw_operation.hpp"
#include "scene/scene.hpp"
#include "scene/scene_draw_context.hpp"
#include "scene/spatial_query_operation.hpp"
#include "scene/tick_scene_view.hpp"
#include "space/dynamic_scene_provider.hpp"
#include <map>
#include <mutex>
#include <set>
#include <vector>

BW_BEGIN_NAMESPACE
int StockControlPoints_token = 1;
namespace
{
struct FlagSettings
{
    uint32 colour = 0xffffffff;
    BW::string emblem;
    Vector4 rect = Vector4(0.f, 0.f, 1.f, 1.f);
};
std::mutex flagMutex;
std::map<int, FlagSettings> flagSettings;

FlagSettings settingsFor(int team)
{
    std::lock_guard<std::mutex> guard(flagMutex);
    const auto found = flagSettings.find(team);
    return found == flagSettings.end() ? FlagSettings() : found->second;
}

void wg_setFlagColor(int team, Vector4 colour)
{
    std::lock_guard<std::mutex> guard(flagMutex);
    auto& value = flagSettings[team];
    value.colour = wot::StockControlPointData::colour(&colour.x);
}
PY_AUTO_MODULE_FUNCTION(RETVOID, wg_setFlagColor, ARG(int, ARG(Vector4, END)), BigWorld)

void wg_setFlagEmblem(int team, BW::string resource, Vector4 rect)
{
    std::lock_guard<std::mutex> guard(flagMutex);
    auto& value = flagSettings[team];
    value.emblem = resource;
    value.rect = rect;
}
PY_AUTO_MODULE_FUNCTION(RETVOID, wg_setFlagEmblem,
    ARG(int, ARG(BW::string, ARG(Vector4, END))), BigWorld)

Vector4 colourVector(uint32 argb)
{
    const float scale = .003921568859368563f;
    return Vector4(float((argb >> 16) & 255) * scale,
        float((argb >> 8) & 255) * scale, float(argb & 255) * scale,
        float(argb >> 24) * scale);
}

Moo::ComplexEffectMaterialPtr variant(const Moo::ComplexEffectMaterialPtr& original,
    const char* shader)
{
    DataSectionPtr section = new XMLSection("material");
    original->baseMaterial()->save(section);
    section->deleteSections("fx");
    section->writeString("fx", shader);
    Moo::ComplexEffectMaterialPtr result = new Moo::ComplexEffectMaterial();
    if (!result->load(section)) return Moo::ComplexEffectMaterialPtr();
    // loadProperties inserts ManagedEffect's default EffectPropertyPtr values
    // by reference. Each fashion must own its mutable vector/texture properties.
    auto& properties = result->baseMaterial()->properties();
    for (auto it = properties.begin(); it != properties.end(); ++it)
        if (it->second) it->second = it->second->clone();
    return result;
}

class FlagFashion : public MaterialFashion
{
public:
    struct Binding
    {
        Moo::Visual::PrimitiveGroup* group;
        Moo::ComplexEffectMaterialPtr normal, alpha;
    };
    bool flag = false;
    float alpha = 1.f;
    FlagSettings settings;
    Moo::BaseTexturePtr emblem, background;
    std::vector<Binding> bindings;
    std::vector<std::pair<Moo::Visual::PrimitiveGroup*, Moo::ComplexEffectMaterialPtr>> saved;

    ~FlagFashion()
    {
        // ManagedEffect may retain its last selected Properties binding.
        // Release texture COM references before this mapping's textures retire.
        for (const auto& binding : bindings)
            for (auto material : {binding.normal, binding.alpha})
            {
                if (!flag || !material) continue;
                ID3DXEffect* effect = material->baseMaterial()->properties().existingStockBinding();
                if (effect)
                {
                    effect->SetTexture("flagEmblemMap", NULL);
                    effect->SetTexture("flagBackgroundMap", NULL);
                }
            }
    }

    bool prepare(SuperModel& model, bool isFlag)
    {
        flag = isFlag;
        std::set<Moo::Visual::PrimitiveGroup*> seen;
        for (int part = 0; part != model.nModels(); ++part)
            for (Model* level = model.topModel(part).getObject(); level; level = level->parent())
            {
                BW::vector<Moo::Visual::PrimitiveGroup*> groups;
                level->gatherMaterials("*", groups);
                for (auto* group : groups)
                {
                    if (!group || !group->material_ || !seen.insert(group).second) continue;
                    Binding value;
                    value.group = group;
                    value.normal = flag ? variant(group->material_,
                        "shaders/controlPoint/wg_controlPointFlag.fx") : group->material_;
                    value.alpha = variant(group->material_, flag ?
                        "shaders/controlPoint/wg_controlPointFlagAlpha.fx" :
                        "shaders/controlPoint/wg_controlPointFlagstaff.fx");
                    if (!value.normal || !value.alpha) return false;
                    bindings.push_back(value);
                }
            }
        if (flag)
            background = Moo::TextureManager::instance()->get(
                "content/MilitaryEnvironment/mle000_BaseFlagstaff/mle016_Flagstaff_flag.dds",
                true, true, true, "Art/File Textures/");
        return !bindings.empty();
    }

    void apply(const FlagSettings& current)
    {
        if (settings.emblem != current.emblem)
        {
            const Moo::BaseTexturePtr replacement = current.emblem.empty() ? Moo::BaseTexturePtr() :
                Moo::TextureManager::instance()->get(current.emblem,
                    true, true, true, "Art/File Textures/");
            // 736c06 skips the fashion texture write when 738270's lookup
            // returned NULL; an existing fashion retains its previous texture.
            if (replacement) emblem = replacement;
        }
        settings = current;
    }

    virtual void dress(SuperModel&)
    {
        for (auto& binding : bindings)
        {
            auto selected = alpha == 1.f ? binding.normal : binding.alpha;
            auto material = selected->baseMaterial();
            if (flag)
            {
                float rect[4];
                wot::StockControlPointData::emblemTransform(&settings.rect.x, rect);
                material->setProperty("g_flagColor", colourVector(settings.colour), true);
                material->setProperty("flagEmblemMap", emblem, true);
                material->setProperty("flagBackgroundMap", background, true);
                material->setProperty("g_emblemTC", Vector4(rect[0], rect[1], rect[2], rect[3]), true);
            }
            if (alpha != 1.f) material->setProperty("g_alpha", alpha, true);
            saved.emplace_back(binding.group, binding.group->material_);
            binding.group->material_ = selected;
        }
    }
    virtual void undress(SuperModel&)
    {
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) it->first->material_ = it->second;
        saved.clear();
    }
};
}

namespace CompiledSpace
{
struct StockControlPointSceneLoader::State
{
    struct Instance
    {
        State* owner = NULL;
        int team = 0;
        Matrix staffWorld, flagWorld;
        std::unique_ptr<SuperModel> staff, flag;
        SuperModelAnimationPtr animation;
        SmartPointer<FlagFashion> fashion, staffFashion;
        MaterialFashionVector flagFashions, staffFashions;
        BoundingBox bounds;
        SceneObject object;
        std::shared_ptr<StockControlPointRadius> radius;
        BW::string sound;
        Vector3 soundPosition;
        float dTime = 0.f, flagScale = 1.f;
        DynamicObjectHandle membership;
        bool active = false, published = false, radiusReady = false;

        bool load(const wot::StockControlPointData::Definition& source,
            const char* flagResource, const char* staffResource, const char* radiusResource,
            const char* soundName)
        {
            team = static_cast<int>(source.team);
            for (unsigned row = 0; row != 4; ++row)
                staffWorld.row(row, Vector4(source.transform[row * 4], source.transform[row * 4 + 1],
                    source.transform[row * 4 + 2], source.transform[row * 4 + 3]));
            BW::vector<BW::string> resources(1, staffResource);
            staff.reset(new SuperModel(resources));
            resources[0] = flagResource;
            flag.reset(new SuperModel(resources));
            if (!staff->isOK() || !flag->isOK()) return false;
            flagScale = source.flagScale;
            sound = soundName ? soundName : "";
            fashion = new FlagFashion(); staffFashion = new FlagFashion();
            if (!fashion->prepare(*flag, true) || !staffFashion->prepare(*staff, false)) return false;
            flagFashions.push_back(fashion); staffFashions.push_back(staffFashion);
            animation = flag->getAnimation("flag");
            radius = loadStockControlPointRadius(radiusResource, owner->space, staffWorld.applyToOrigin(),
                source.radius, source.overTerrainHeight, Vector4(source.radiusColour[0],
                    source.radiusColour[1], source.radiusColour[2], source.radiusColour[3]));
            BoundingBox radiusBounds;
            if (!radius || !stockControlPointRadiusBounds(radius, radiusBounds)) return false;
            object.handleAndType(this);
            object.flags().isDynamic(false);
            object.flags().isReflectionVisible(true);
            // 6bbe5d supplies the original common scene flags 0x03.
            object.flags().isCastingStaticShadow(true);
            object.flags().isCastingDynamicShadow(true);
            active = true;
            return true;
        }
        void prepareTransform()
        {
            // Public SuperModel dresses shared catalogue nodes. Do this in the
            // main-thread bind phase, after the background resource load.
            staff->cacheDefaultTransforms(Matrix::identity);
            Vector3 soundPoint(1.f, 18.f, 0.f);
            if (auto node = staff->findNode("HP_FlagSound")) soundPoint = node->worldTransform().applyToOrigin();
            soundPosition = staffWorld.applyToOrigin() + soundPoint;
            Vector3 high(.5f, 17.f, 0.f);
            if (auto node = staff->findNode("HP_UpFlag")) high = node->worldTransform().applyToOrigin();
            // 736500 transforms the attachment point as a normal, adds it
            // to the authored translation, then premultiplies uniform flagScale.
            D3DXVECTOR3 point, attached(high.x, high.y, high.z);
            D3DXVec3TransformNormal(&point, &attached, &staffWorld);
            Matrix placement = staffWorld, scale;
            placement.translation(staffWorld.applyToOrigin() + Vector3(point.x, point.y, point.z));
            scale.setScale(flagScale, flagScale, flagScale);
            D3DXMatrixMultiply(&flagWorld, &scale, &placement);
            staff->localVisibilityBox(bounds);
            bounds.transformBy(staffWorld);
            BoundingBox flagBounds;
            flag->localVisibilityBox(flagBounds); flagBounds.transformBy(flagWorld);
            bounds.addBounds(flagBounds);
            prepareRadius();
        }
        void prepareRadius()
        {
            if (radiusReady || !active || !owner->bound) return;
            BoundingBox radiusBounds;
            if (!prepareStockControlPointRadiusBounds(radius, radiusBounds)) return;
            bounds.addBounds(radiusBounds);
            radiusReady = true;
            // 73b520 prepares the projected height range before submitting the
            // area for visibility selection. Retry from tick, not draw, when
            // terrain was unavailable at bind so the initial box cannot hide it.
            if (published)
                owner->space->dynamicScene().updateObject(membership, staffWorld,
                    BoundingBox::s_insideOut_, bounds);
        }
        void updateAnimations()
        {
            if (!active || !owner->bound) return;
            if (animation)
            {
                const auto* source = animation->pSource(*flag);
                const float duration = source ? source->duration_ : 0.f;
                animation->time += dTime;
                if (duration > 0.f) animation->time -= floorf(animation->time / duration) * duration;
            }
            TmpTransforms transforms;
            if (animation) transforms.push_back(animation.getObject());
            // The SDK immediately draws material fashions and restores shared
            // primitive groups before another team's instance can use them.
            flag->updateAnimations(flagWorld, &transforms, NULL, Model::LOD_AUTO_CALCULATE);
            staff->updateAnimations(staffWorld, NULL, NULL, Model::LOD_AUTO_CALCULATE);
        }
        void draw(const SceneDrawContext& context)
        {
            if (!active || !owner->bound) return;
            fashion->apply(settingsFor(team));
            const Vector3 delta = Moo::rc().invView().applyToOrigin() - staffWorld.applyToOrigin();
            const float distance = delta.lengthSquared() * .0008163265301845968f;
            const float fade = context.drawContext().renderingPassType() == Moo::RENDERING_PASS_COLOR ?
                Math::clamp(.15f, distance * distance, 1.f) : 1.f;
            fashion->alpha = staffFashion->alpha = fade;
            flag->draw(context.drawContext(), flagWorld, &flagFashions);
            staff->draw(context.drawContext(), staffWorld, &staffFashions);
            drawStockControlPointRadius(radius, context);
        }
    };
    struct DrawHandler : IDrawOperationTypeHandler
    {
        virtual void doDraw(const SceneDrawContext& context, const SceneObject* objects, size_t count)
        { for (size_t i = 0; i != count; ++i) objects[i].getAs<Instance>()->draw(context); }
    };
    struct SpatialHandler : ISpatialQueryOperationTypeHandler
    {
        virtual void getWorldTransform(const SceneObject& object, Matrix* output)
        { *output = object.getAs<Instance>()->staffWorld; }
        virtual void getWorldVisibilityBoundingBox(const SceneObject& object, AABB* output)
        { *output = object.getAs<Instance>()->bounds; }
    };
    struct TickHandler : ITickOperationTypeHandler
    {
        virtual void doTick(SceneObject* objects, size_t count, float dTime)
        {
            for (size_t i = 0; i != count; ++i)
            {
                auto* instance = objects[i].getAs<Instance>();
                instance->dTime = dTime;
                instance->prepareRadius();
            }
        }
        virtual void doUpdateAnimations(SceneObject* objects, size_t count, float)
        { for (size_t i = 0; i != count; ++i) objects[i].getAs<Instance>()->updateAnimations(); }
    };
    wot::StockControlPointData binary;
    std::vector<Instance> instances;
    std::vector<DynamicObjectHandle> memberships;
    StockSpaceSounds sounds;
    std::vector<StockSpaceSoundHandle> soundHandles;
    CompiledSpace* space = NULL;
    bool bound = false;
    static bool fail(const char* message) { ASSET_MSG("WTCP2: %s\n", message); return false; }
    bool load(BinaryFormat& file, const StringTable& strings, uint32 mask)
    {
        const size_t section = file.findSection(FourCC("WTCP"));
        if (section == BinaryFormat::NOT_FOUND || file.sectionVersion(section) != 2)
            return fail("Missing or incompatible original control-point section");
        auto* stream = file.openSection(section, true);
        if (!stream) return fail("Cannot map original control-point section");
        const size_t size = stream->size();
        const void* bytes = stream->readRaw(size);
        std::string error;
        const bool parsed = bytes && binary.read(bytes, size, error);
        file.closeSection(stream);
        if (!parsed) return fail(error.empty() ? "Cannot read original control points" : error.c_str());
        instances.resize(binary.definitions.size());
        for (size_t i = 0; i != instances.size(); ++i)
        {
            auto& instance = instances[i]; instance.owner = this;
            const auto& source = binary.definitions[i];
            // 6bc6b0 resolves resources before applying the mapping mask.
            const char* flag = strings.entry(source.flag);
            const char* staff = strings.entry(source.flagstaff);
            const char* radius = strings.entry(source.radiusVisual);
            const char* sound = strings.entry(source.sound);
            if (!(source.visibilityMask & mask)) continue;
            if (!flag || !staff || !radius || !*flag || !*staff || !*radius ||
                !instance.load(source, flag, staff, radius, sound))
                fail("Cannot load original control-point flag models or materials");
        }
        return true;
    }
};

StockControlPointSceneLoader::StockControlPointSceneLoader(uint32 mask) : mappingMask_(mask) {}
StockControlPointSceneLoader::~StockControlPointSceneLoader() { doUnload(); }
void StockControlPointSceneLoader::registerHandlers(Scene& scene)
{
    scene.getObjectOperation<DrawOperation>()->addHandler<State::Instance, State::DrawHandler>();
    scene.getObjectOperation<SpatialQueryOperation>()->addHandler<State::Instance, State::SpatialHandler>();
    scene.getObjectOperation<TickOperation>()->addHandler<State::Instance, State::TickHandler>();
}
bool StockControlPointSceneLoader::doLoadFromSpace(ClientSpace* space, BinaryFormat& file,
    const DataSectionPtr&, const Matrix&, const StringTable& strings)
{
    if (state_ || unloadDepth_) return State::fail("Previous control-point mapping has not unloaded");
    const auto next = std::make_shared<State>();
    next->space = dynamic_cast<CompiledSpace*>(space);
    if (!next->space) return State::fail("Control points require their compiled-space owner");
    if (!next->load(file, strings, mappingMask_)) return false;
    state_ = next; loaded_.store(true, std::memory_order_release); return true;
}
bool StockControlPointSceneLoader::doBind()
{
    const auto live = state_;
    if (!live) return false;
    if (live->bound) return true;
    live->sounds = acquireStockSpaceSounds(live->space);
    live->bound = true;
    for (auto& value : live->instances)
    {
        if (!value.active) continue;
        value.prepareTransform();
        const auto handle = live->space->dynamicScene().addObject(value.object, value.staffWorld,
            BoundingBox::s_insideOut_, value.bounds);
        if (state_ != live) { live->space->dynamicScene().removeObject(handle); return false; }
        value.membership = handle; value.published = true;
        live->memberships.push_back(handle);
        const auto sound = addStockSpaceControlPointSound(live->sounds, value.sound, value.soundPosition);
        if (state_ != live) { removeStockSpaceSound(live->sounds, sound); return false; }
        live->soundHandles.push_back(sound);
    }
    return true;
}
void StockControlPointSceneLoader::doUnload()
{
    loaded_.store(false, std::memory_order_release);
    auto live = state_;
    if (!live) return;
    state_.reset(); live->bound = false;
    for (auto& value : live->instances) retireStockControlPointRadius(value.radius);
    struct Retiring { uint32& depth; explicit Retiring(uint32& v) : depth(v) { ++depth; } ~Retiring() { --depth; } } retiring(unloadDepth_);
    std::vector<DynamicObjectHandle> handles;
    handles.swap(live->memberships);
    for (const auto& handle : handles) live->space->dynamicScene().removeObject(handle);
    std::vector<StockSpaceSoundHandle> sounds;
    sounds.swap(live->soundHandles);
    for (const auto handle : sounds) removeStockSpaceSound(live->sounds, handle);
    live->sounds.reset();
}
float StockControlPointSceneLoader::percentLoaded() const
{ return loaded_.load(std::memory_order_acquire) ? 1.f : 0.f; }
}
BW_END_NAMESPACE
