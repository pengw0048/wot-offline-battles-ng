#include "pch.hpp"
#include "stock_vehicle_stickers.hpp"
#include "stock_vehicle_stickers_data.hpp"
#include "stock_vehicle_stickers_geometry.hpp"
#include "stock_space_decals_texture.hpp"
#include "stock_effect_adapter.hpp"
#include "stock_dynamic_resolution.hpp"
#include "duplo/py_attachment.hpp"
#include "model/model.hpp"
#include "model/super_model.hpp"
#include "moo/draw_context.hpp"
#include "moo/effect_material.hpp"
#include "moo/effect_visual_context.hpp"
#include "moo/lod_settings.hpp"
#include "moo/moo_utilities.hpp"
#include "moo/render_context.hpp"
#include "moo/renderer.hpp"
#include "moo/sticker_decal.hpp"
#include "moo/sys_mem_texture.hpp"
#include "moo/texture_manager.hpp"
#include "moo/utils.hpp"
#include "moo/vertex_buffer.hpp"
#include "moo/vertex_declaration.hpp"
#include "moo/visual.hpp"
#include "moo/visual_manager.hpp"
#include "physics2/bsp.hpp"
#include "resmgr/binary_block.hpp"
#include <map>
#include <set>
#include <cstdlib>
#include <cstring>
#include <limits>

BW_BEGIN_NAMESPACE
int StockVehicleStickers_token = 1;
namespace
{
typedef StockStickerGeometry::Vertex Vertex;
class StickerRenderer;
std::shared_ptr<StickerRenderer> renderer;

bool failure(const char* message)
{
    ERROR_MSG("WGStickerModel: %s\n", message);
    return false;
}

class PyVehicleStickerGeometry : public PyObjectPlus
{
    Py_Header(PyVehicleStickerGeometry, PyObjectPlus)
public:
    explicit PyVehicleStickerGeometry(const std::shared_ptr<StockVehicleStickerGeometry>& source) :
        PyObjectPlus(&s_type_), source_(source) {}
    const std::shared_ptr<StockVehicleStickerGeometry>& source() const { return source_; }
private:
    std::shared_ptr<StockVehicleStickerGeometry> source_;
};
PY_TYPEOBJECT(PyVehicleStickerGeometry)
PY_BEGIN_METHODS(PyVehicleStickerGeometry)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(PyVehicleStickerGeometry)
PY_END_ATTRIBUTES()

struct StickerImage : public Moo::DeviceCallback
{
    Moo::BaseTexturePtr source;
    ComObjectWrap<DX::Texture> view;
    uint32 width = 0, height = 0;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    unsigned category = 0;
    bool prepare()
    {
        if (view) return true;
        DX::BaseTexture* base = source ? source->pTexture() : NULL;
        if (!base || base->GetType() != D3DRTYPE_TEXTURE)
            return failure("Sticker image is not an actual 2D texture");
        DX::Texture* texture = static_cast<DX::Texture*>(base);
        D3DSURFACE_DESC desc;
        if (FAILED(texture->GetLevelDesc(0, &desc)) || desc.Pool != D3DPOOL_SYSTEMMEM ||
            desc.Format != (category == 2 ? D3DFMT_A8R8G8B8 : D3DFMT_DXT5) ||
            desc.Width < 64 || desc.Height < 64 || desc.Width > 512 || desc.Height > 512 ||
            (desc.Width & (desc.Width - 1)) ||
            (desc.Height & (desc.Height - 1)) || texture->GetLevelCount() < 5)
            return failure("Sticker image violates its original format, power-of-two or mip contract");
        // 957fa0 sets skip=0/anisotropy=1. The common 13f5450 allocator
        // therefore keeps five complete source mips, without adding a border.
        if (FAILED(wot::copyStockSpaceDecalMipTexture(Moo::rc().device(), texture, 0, 5, &view)))
            return failure("Cannot copy the original sticker mip view");
        width = desc.Width; height = desc.Height; format = desc.Format;
        return true;
    }
    Vector4 uv() const
    {
        // 95aa70. Removing atlas placement preserves source texel coordinates
        // and implicit texture LOD gradients, including the half-texel inset.
        const auto value = wot::stockVehicleStickerTextureUV(width, height);
        return Vector4(value[0], value[1], value[2], value[3]);
    }
    virtual void createUnmanagedObjects() {}
    virtual void deleteUnmanagedObjects() { view = NULL; }
    virtual bool recreateForD3DExDevice() const { return true; }
};

struct Sticker : public Moo::DeviceCallback
{
    int id = 0;
    bool active = true, mirrored = false;
    std::shared_ptr<StickerImage> image;
    std::vector<Vertex> vertices;
    Moo::VertexBuffer buffer;
    bool prepare()
    {
        if (buffer.valid()) return true;
        if (vertices.empty()) return false;
        if (vertices.size() > std::numeric_limits<UINT>::max() / sizeof(Vertex))
            return failure("Sticker vertex buffer exceeds the native buffer domain");
        if (FAILED(buffer.create(static_cast<UINT>(vertices.size() * sizeof(Vertex)),
            D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT))) return failure("Cannot create projected sticker vertex buffer");
        Moo::SimpleVertexLock lock(buffer, 0, 0, 0);
        if (!lock) { buffer.release(); return failure("Cannot upload projected sticker vertices"); }
        std::memcpy(lock, vertices.data(), vertices.size() * sizeof(Vertex));
        return true;
    }
    virtual void createUnmanagedObjects() {}
    virtual void deleteUnmanagedObjects() { buffer.release(); }
    virtual bool recreateForD3DExDevice() const { return true; }
};

struct StickerOwner
{
    bool active = true, registered = false;
    int nextID = 1;
    unsigned generation = 0;
    float lodDistance = 1.e10f, alpha = 1.f;
    std::shared_ptr<StockVehicleStickerGeometry> geometry;
    std::shared_ptr<StickerImage> suppliedImage;
    std::vector<std::shared_ptr<Sticker>> stickers;
    void clear()
    {
        ++generation;
        for (const auto& sticker : stickers) sticker->active = false;
        stickers.clear();
    }
    ~StickerOwner() { clear(); }
};

struct QueuedSticker
{
    std::shared_ptr<StickerOwner> owner;
    std::shared_ptr<Sticker> sticker;
    Matrix world;
    float distance, lodDistance, alpha;
};

class StickerRenderer : public Moo::DeviceCallback
{
public:
    wot::StockDamageStickerRegistry styles;
    Moo::EffectMaterialPtr material;
    Moo::VertexDeclaration* declaration = NULL;
    std::array<std::vector<Vertex>, 6> cylinders;
    std::vector<std::weak_ptr<StickerOwner>> owners;
    std::map<std::pair<BW::string, unsigned>, std::weak_ptr<StickerImage>> images;
    std::vector<QueuedSticker> queue;
    bool open = true;
    ~StickerRenderer() { close(); }
    void close()
    {
        if (!open) return;
        open = false;
        for (auto& weak : owners) if (auto owner = weak.lock())
        {
            owner->active = false; owner->clear(); owner->suppliedImage.reset(); owner->geometry.reset();
        }
        queue.clear(); images.clear();
        deleteUnmanagedObjects(); deleteManagedObjects();
    }
    std::shared_ptr<StickerImage> image(const BW::string& name, unsigned category)
    {
        if (!open) return {};
        const auto key = std::make_pair(name, category);
        if (auto present = images[key].lock()) return present;
        auto result = std::make_shared<StickerImage>();
        result->category = category;
        result->source = Moo::TextureManager::instance()->getSystemMemoryTexture(name);
        if (!result->prepare() || !open) return {};
        images[key] = result;
        return result;
    }
    bool initialise();
    bool draw();
    virtual void createManagedObjects()
    {
        Moo::createEffect(material, "shaders/std_effects/wg_forward_decal.fx");
        declaration = Moo::VertexDeclaration::get("xyznuv");
    }
    virtual void deleteManagedObjects() { material = NULL; declaration = NULL; }
    virtual void createUnmanagedObjects() {}
    virtual void deleteUnmanagedObjects()
    {
        queue.clear();
        if (material && material->pEffect())
        {
            ID3DXEffect* binding = material->properties().existingStockBinding();
            if (binding) binding->SetTexture("g_atlasMap", NULL);
            ID3DXEffect* current = material->pEffect()->pEffect();
            if (current && current != binding && !wot::is_stock_effect(current)) current->SetTexture("g_atlasMap", NULL);
        }
    }
    virtual bool recreateForD3DExDevice() const { return true; }
};

bool collect(const std::shared_ptr<StockVehicleStickerGeometry>& source,
    const StockStickerGeometry::Box& bounds, std::vector<WorldTriangle>& output)
{
    PyModelPtr model; Matrix world;
    if (!source || !source->acquire(model, world)) return false;
    SuperModel* superModel = model->pSuperModel();
    if (!superModel) return false;
    superModel->beginRead();
    struct Guard { SuperModel* model; ~Guard() { model->endRead(); } } guard = {superModel};
    class Collector : public CollisionVisitor
    {
    public:
        Vector3 direction;
        std::vector<WorldTriangle>& output;
        Collector(const Vector3& direction, std::vector<WorldTriangle>& output) :
            direction(direction), output(output) {}
        virtual bool visit(const WorldTriangle& triangle, float)
        {
            // Original 955830 checks exactly NOCOLLIDE|WATER, the actual
            // triangle normal, and duplicate vertex triples.
            if (triangle.flags() & 0x50) return false;
            const Vector3 normal = StockStickerGeometry::normalised(triangle.normal());
            if (direction.dotProduct(normal) >= -.035f) return false;
            for (const auto& present : output)
                if (present.v0() == triangle.v0() && present.v1() == triangle.v1() && present.v2() == triangle.v2())
                    return false;
            output.push_back(triangle);
            return false;
        }
    };
    output.clear();
    const Vector3 extent = bounds.world[4] - bounds.world[0];
    Collector collector(StockStickerGeometry::normalised(extent), output);
    bool available = false;
    for (int index = 0; index < superModel->nModels(); ++index)
    {
        ModelPtr part = superModel->topModel(index);
        const BSPTree* bsp = part ? part->decompose() : NULL;
        if (!bsp) continue;
        available = true;
        // 958ec0/1179d70 sweep both actual front-face triangles through the
        // projection depth. No bounding rectangle substitutes for the mesh.
        bsp->intersects(WorldTriangle(bounds.world[0], bounds.world[1], bounds.world[3]), extent, &collector);
        bsp->intersects(WorldTriangle(bounds.world[1], bounds.world[2], bounds.world[3]), extent, &collector);
    }
    return available;
}

bool project(const std::shared_ptr<StockVehicleStickerGeometry>& source,
    Vector3 start, Vector3 end, const Vector2& size, const Vector3& up,
    bool damage, std::vector<Vertex>& output)
{
    auto bounds = StockStickerGeometry::box(start, end, size, up);
    std::vector<WorldTriangle> selected;
    if (!collect(source, bounds, selected)) return false;
    Vector3 normal(0.f, 0.f, 0.f);
    for (const auto& triangle : selected) normal += triangle.normal() * 3.f;
    normal = StockStickerGeometry::normalised(normal);
    if (!selected.empty())
    {
        const float angle = std::acos(StockStickerGeometry::normalised(start - end).dotProduct(normal));
        const float limit = 1.5707964f - PyStickerModel::criticalAngle();
        if (angle > limit)
        {
            StockStickerGeometry::rotate(start, end, normal, angle - limit);
            bounds = StockStickerGeometry::box(start, end, size, up);
            if (!collect(source, bounds, selected)) return false;
        }
    }
    unsigned retries = damage ? 5u : 1u;
    bool reachesEnd = StockStickerGeometry::clip(bounds, selected, output);
    while (!reachesEnd && retries--)
    {
        end += end - start;
        bounds = StockStickerGeometry::box(start, end, size, up);
        if (!collect(source, bounds, selected)) return false;
        reachesEnd = StockStickerGeometry::clip(bounds, selected, output);
    }
    StockStickerGeometry::finish(output);
    return true;
}

bool cylinder(const std::shared_ptr<StockVehicleStickerGeometry>& source,
    const std::vector<Vertex>& authored, const Vector3& position, const Vector3& size,
    bool proportional, std::vector<Vertex>& output)
{
    PyModelPtr model; Matrix world;
    if (!source || !source->acquire(model, world)) return false;
    SuperModel* superModel = model->pSuperModel();
    if (!superModel) return false;
    Matrix transform = Matrix::identity;
    model->worldTransform(world);
    model->updateAnimations(world, 0.f);
    // 954870 captures the inverse transform of the actual G node. Resolve it
    // from this part's current pose, then remove the outer vehicle transform.
    Moo::NodePtr node = superModel->findNode("G");
    if (node)
    {
        Matrix inverseWorld;
        if (!D3DXMatrixInverse(&inverseWorld, NULL, &world)) return false;
        transform.multiply(node->worldTransform(), inverseWorld);
        if (!D3DXMatrixInverse(&transform, NULL, &transform)) return false;
    }
    float frontScale = 1.f, backScale = 1.f, frontOffset = 0.f, backOffset = 0.f;
    if (proportional)
    {
        const float reciprocal = 1.f / size.x;
        frontScale = size.y * 3.1415927f * reciprocal;
        backScale = size.z * 3.1415927f * reciprocal;
        frontOffset = (size.x - size.y * 3.1415927f) * .5f * reciprocal;
        backOffset = (size.x - size.z * 3.1415927f) * .5f * reciprocal;
    }
    output.clear(); output.reserve(authored.size());
    for (auto vertex : authored)
    {
        const float depth = vertex.pos_.z * 100.f;
        const bool front = depth < 0.f;
        const float radius = front ? size.y : size.z;
        Vector3 point(position.x + radius * vertex.pos_.x * 100.f,
            position.y + radius * vertex.pos_.y * 100.f, size.x * depth + position.z);
        vertex.pos_ = StockStickerGeometry::transformed(point, transform);
        D3DXVec3TransformNormal(&vertex.normal_, &vertex.normal_, &transform);
        const float v = vertex.uv_.y * (front ? frontScale : backScale) + (front ? frontOffset : backOffset);
        vertex.uv_.y = v <= 0.f ? 0.f : v;
        output.push_back(vertex);
    }
    return true;
}

class WGStickerModel : public PyAttachment
{
    Py_Header(WGStickerModel, PyAttachment)
public:
    WGStickerModel() : PyAttachment(&s_type_), owner_(std::make_shared<StickerOwner>()) {}
    virtual ~WGStickerModel() { owner_->clear(); }
    virtual bool needsUpdate() const { return owner_->active && PyStickerModel::s_enabledDrawing; }
    virtual bool isLodVisible() const { return needsUpdate(); }
#if ENABLE_TRANSFORM_VALIDATION
    virtual bool isTransformFrameDirty(uint32) const { return false; }
    virtual bool isVisibleTransformFrameDirty(uint32) const { return false; }
#endif
    virtual void tick(float) {}
    virtual void updateAnimations(const Matrix&, float distance) { distance_ = distance; }
    virtual void draw(Moo::DrawContext& context, const Matrix& world)
    {
        if (!renderer || !renderer->open || !owner_->active || !PyStickerModel::s_enabledDrawing ||
            context.renderingPassType() != Moo::RENDERING_PASS_COLOR || Moo::rc().reflectionScene()) return;
        const auto current = renderer;
        const auto geometry = owner_->geometry;
        const unsigned generation = owner_->generation;
        PyModelPtr model; Matrix pose;
        if (!geometry || !geometry->acquire(model, pose) || renderer != current || !current->open ||
            !owner_->active || owner_->geometry != geometry || owner_->generation != generation) return;
        for (const auto& sticker : owner_->stickers)
            if (sticker->active && !sticker->vertices.empty())
                current->queue.push_back({owner_, sticker, world, distance_, owner_->lodDistance, owner_->alpha});
    }
    virtual void localBoundingBox(BoundingBox& bounds, bool skinny = false)
    {
        PyModelPtr model; Matrix world;
        if (!owner_->geometry || !owner_->geometry->acquire(model, world)) return;
        BoundingBox local;
        // 930a20 reads SuperModel bounds. PyModel bounds include this attached
        // sticker and would recursively re-enter this very bounds callback.
        (void)skinny;
        model->pSuperModel()->localBoundingBox(local);
        local.transformBy(toPartRoot_);
        bounds.addBounds(local);
    }
    virtual void localVisibilityBox(BoundingBox& bounds, bool skinny = false) { localBoundingBox(bounds, skinny); }
    virtual void worldBoundingBox(BoundingBox& bounds, const Matrix& world, bool skinny = false)
    {
        BoundingBox local; localBoundingBox(local, skinny);
        if (!local.insideOut()) { local.transformBy(world); bounds.addBounds(local); }
    }
    virtual void worldVisibilityBox(BoundingBox& bounds, const Matrix& world, bool skinny = false)
        { worldBoundingBox(bounds, world, skinny); }

    int addSticker(const BW::string& texture, const BW::string& bumpTexture,
        const Vector3& start, const Vector3& end, const Vector2& size, const Vector3& up, int flags)
    {
        if (!adopt()) return -1;
        const auto current = renderer;
        const auto geometry = owner_->geometry;
        const unsigned generation = owner_->generation;
        // The original ABI retains the bump parameter but this actual effect
        // samples only g_atlasMap; neither native 9301f0 nor 956150 uses it.
        (void)bumpTexture;
        auto sticker = std::make_shared<Sticker>();
        sticker->mirrored = (flags & 4) != 0;
        sticker->image = texture.empty() ? owner_->suppliedImage : current->image(texture, (flags & 1) ? 1 : 0);
        if (!sticker->image) return -1;
        bool projected;
        if (flags & 2)
        {
            const int shape = std::isfinite(up.z) && up.z >= 0.f && up.z < 6.f ? static_cast<int>(up.z) : -1;
            if (shape < 0 || shape >= static_cast<int>(current->cylinders.size()))
            {
                failure("Sticker cylinder index is outside the original six resources");
                return -1;
            }
            projected = cylinder(geometry, current->cylinders[shape], start,
                Vector3(size.x, up.x, up.y), (flags & 8) != 0, sticker->vertices);
        }
        else projected = project(geometry, start, end, size, up, false, sticker->vertices);
        if (!projected) return -1;
        return publish(current, geometry, generation, sticker);
    }
    PY_AUTO_METHOD_DECLARE(RETDATA, addSticker,
        OPTARG(BW::string, "", OPTARG(BW::string, "",
        OPTARG(Vector3, Vector3(0,0,0), OPTARG(Vector3, Vector3(0,0,0),
        OPTARG(Vector2, Vector2(0,0), OPTARG(Vector3, Vector3(0,1,0),
        OPTARG(int, 0, END))))))))

    int addDamageSticker(int styleIndex, const Vector3& start, const Vector3& end)
    {
        if (!adopt()) return -1;
        const auto current = renderer;
        const auto geometry = owner_->geometry;
        const unsigned generation = owner_->generation;
        const auto* style = current->styles.find(styleIndex);
        if (!style || style->variants.empty()) return -1;
        // Copy before texture loading, whose resource work may re-enter owners.
        const auto variant = style->variants[style->variants.size() > 1 ? std::rand() % style->variants.size() : 0];
        Vector2 size(variant.size[0], variant.size[1]);
        if (variant.variation > 0.f)
        {
            const float scale = variant.variation * (float(std::rand()) * 3.051851e-05f) + 1.f;
            size *= scale;
        }
        float yaw = 0.f;
        if (variant.randomYaw) { yaw = float(std::rand()) * 9.587673e-05f; yaw += yaw; }
        const Vector3 direction = StockStickerGeometry::normalised(end - start);
        unsigned dominant = std::fabs(direction.x) < std::fabs(direction.y) ? 1u : 0u;
        if (std::fabs(direction[dominant]) < std::fabs(direction.z)) dominant = 2;
        Vector3 up(0.f, 0.f, 0.f);
        up[(dominant + 1) % 3] = std::sin(yaw);
        up[(dominant + 2) % 3] = std::cos(yaw);
        auto sticker = std::make_shared<Sticker>();
        sticker->image = current->image(BW::string(variant.texture.data(), variant.texture.size()), 0);
        if (!sticker->image || !project(geometry, start, end, size, up, true, sticker->vertices)) return -1;
        return publish(current, geometry, generation, sticker);
    }
    PY_AUTO_METHOD_DECLARE(RETDATA, addDamageSticker,
        ARG(int, OPTARG(Vector3, Vector3(0,0,0), OPTARG(Vector3, Vector3(0,0,0), END))))
    void delSticker(int id)
    {
        const auto found = std::find_if(owner_->stickers.begin(), owner_->stickers.end(),
            [id](const std::shared_ptr<Sticker>& sticker) { return sticker->id == id; });
        if (found != owner_->stickers.end()) { (*found)->active = false; owner_->stickers.erase(found); }
    }
    PY_AUTO_METHOD_DECLARE(RETVOID, delSticker, ARG(int, END))
    void clear() { owner_->clear(); }
    PY_AUTO_METHOD_DECLARE(RETVOID, clear, END)
    void setLODDistance(float distance) { owner_->lodDistance = distance; }
    PY_AUTO_METHOD_DECLARE(RETVOID, setLODDistance, ARG(float, END))
    void setAlpha(float alpha) { owner_->alpha = alpha; }
    PY_AUTO_METHOD_DECLARE(RETVOID, setAlpha, ARG(float, END))
    PyObject* setupSuperModelCall(PyObject* args)
    {
        PyObject* link; PyObject* transform;
        if (!PyArg_ParseTuple(args, "OO:setupSuperModel", &link, &transform)) return NULL;
        if (!PyVehicleStickerGeometry::Check(link))
        {
            PyErr_SetString(PyExc_TypeError, "setupSuperModel requires a real CompoundModel part geometry link");
            return NULL;
        }
        Matrix matrix;
        if (Script::setData(transform, matrix, "setupSuperModel() argument 2") != 0) return NULL;
        ++owner_->generation;
        owner_->geometry = static_cast<PyVehicleStickerGeometry*>(link)->source();
        toPartRoot_ = matrix;
        Py_RETURN_NONE;
    }
    PY_METHOD_DECLARE(py_setupSuperModel)
    PyObject* setTextureDataCall(PyObject* args)
    {
        PyObject* data;
        if (!PyArg_UnpackTuple(args, "setTextureData", 1, 1, &data)) return NULL;
        if (!PyString_Check(data))
        {
            PyErr_SetString(PyExc_RuntimeError, "PyStickerModel::setTextureData - invalid parameter type of data (must be string).");
            return NULL;
        }
        char* bytes = NULL; Py_ssize_t length = 0;
        if (PyString_AsStringAndSize(data, &bytes, &length) || length <= 0)
        {
            PyErr_SetString(PyExc_RuntimeError, "PyStickerModel::setTextureData - failed to get texture contents.");
            return NULL;
        }
        if (!adopt()) { PyErr_SetString(PyExc_RuntimeError, "Sticker renderer is unavailable"); return NULL; }
        const auto current = renderer;
        const unsigned generation = owner_->generation;
        auto image = std::make_shared<StickerImage>();
        image->category = 2;
        image->source = new Moo::SysMemTexture(new BinaryBlock(bytes, static_cast<int>(length), "Sticker image"), D3DFMT_A8R8G8B8);
        if (!image->prepare())
        {
            PyErr_SetString(PyExc_RuntimeError, "PyStickerModel::setTextureData - failed to load texture from input data, texture contents may be corrupt or incomplete.");
            return NULL;
        }
        if (renderer != current || !current->open || !owner_->active || owner_->generation != generation)
        {
            PyErr_SetString(PyExc_RuntimeError, "Sticker owner was retired during texture loading");
            return NULL;
        }
        owner_->suppliedImage = image;
        Py_RETURN_NONE;
    }
    PY_METHOD_DECLARE(py_setTextureData)
    static WGStickerModel* New() { return new WGStickerModel(); }
    PY_AUTO_FACTORY_DECLARE(WGStickerModel, END)
private:
    int publish(const std::shared_ptr<StickerRenderer>& current,
        const std::shared_ptr<StockVehicleStickerGeometry>& geometry,
        unsigned generation, const std::shared_ptr<Sticker>& sticker)
    {
        PyModelPtr model; Matrix world;
        if (!geometry || !geometry->acquire(model, world)) return -1;
        // Resource loading and MatrixProvider callbacks can retire or rebind
        // either owner. Never publish work into a new/cleared attachment.
        if (renderer != current || !current->open || !owner_->active ||
            owner_->generation != generation || owner_->geometry != geometry) return -1;
        sticker->id = owner_->nextID++;
        owner_->stickers.push_back(sticker);
        return sticker->id;
    }
    bool adopt()
    {
        if (!renderer || !renderer->open || !owner_->active) return false;
        if (!owner_->registered) { renderer->owners.push_back(owner_); owner_->registered = true; }
        return true;
    }
    std::shared_ptr<StickerOwner> owner_;
    Matrix toPartRoot_ = Matrix::identity;
    float distance_ = 0.f;
};
PY_TYPEOBJECT(WGStickerModel)
PY_BEGIN_METHODS(WGStickerModel)
    PY_METHOD(addSticker)
    PY_METHOD(addDamageSticker)
    PY_METHOD(delSticker)
    PY_METHOD(clear)
    PY_METHOD(setLODDistance)
    PY_METHOD(setAlpha)
    PY_METHOD(setTextureData)
    PY_METHOD(setupSuperModel)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(WGStickerModel)
PY_END_ATTRIBUTES()
PY_FACTORY_NAMED(WGStickerModel, "WGStickerModel", BigWorld)

PyObject* WGStickerModel::py_setupSuperModel(PyObject* args) { return setupSuperModelCall(args); }
PyObject* WGStickerModel::py_setTextureData(PyObject* args) { return setTextureDataCall(args); }

bool StickerRenderer::initialise()
{
    createManagedObjects();
    if (!material || !material->pEffect() || !declaration ||
        declaration->format().streamStride(0) != sizeof(Vertex))
        return failure("Cannot load the original sticker effect or xyznuv vertex declaration");
    static const char* resources[] = {
        "gui/maps/vehicles/decals/insignia/models/cylinder_8.visual",
        "gui/maps/vehicles/decals/insignia/models/cylinder_10.visual",
        "gui/maps/vehicles/decals/insignia/models/cylinder_12.visual",
        "gui/maps/vehicles/decals/insignia/models/cylinder_16.visual",
        "gui/maps/vehicles/decals/insignia/models/cylinder_20.visual",
        "gui/maps/vehicles/decals/insignia/models/cylinder_24.visual"};
    for (size_t index = 0; index < cylinders.size(); ++index)
    {
        Moo::VisualPtr visual = Moo::VisualManager::instance()->get(resources[index]);
        if (!visual || !visual->isOK()) return failure("A required authored gun-insignia cylinder visual is unavailable");
        Moo::Visual::ReadGuard guard(visual.getObject());
        void* bytes = NULL; Moo::IndicesHolder indices;
        uint32 count = 0, indexCount = 0; Moo::EffectMaterialPtr originalMaterial;
        const bool copied = visual->createCopy(declaration->format(), bytes, indices, count, indexCount, originalMaterial);
        std::unique_ptr<uint8[]> cleanup(static_cast<uint8*>(bytes));
        if (!copied || !bytes || !count || !indexCount || indexCount % 3)
            return failure("Cannot read the actual authored gun-insignia cylinder mesh");
        const Vertex* vertices = static_cast<const Vertex*>(bytes);
        auto& mesh = cylinders[index]; mesh.reserve(indexCount);
        for (uint32 offset = 0; offset < indexCount; ++offset)
        {
            const uint32 vertex = indices[offset];
            if (vertex >= count) return failure("Authored insignia cylinder has an invalid mesh index");
            mesh.push_back(vertices[vertex]);
        }
    }
    return true;
}

class StickerDeviceScope
{
public:
    StickerDeviceScope()
    {
        world_ = Moo::rc().effectVisualContext().worldMatrix();
        saved_ = SUCCEEDED(Moo::rc().device()->CreateStateBlock(D3DSBT_ALL, &state_)) &&
            state_ && SUCCEEDED(state_->Capture());
        targets_ = saved_ && Moo::rc().pushRenderTarget();
    }
    ~StickerDeviceScope()
    {
        if (targets_) Moo::rc().popRenderTarget();
        if (saved_)
        {
            if (FAILED(state_->Apply())) failure("Cannot restore the native device state after sticker rendering");
            Moo::rc().invalidateStateCache();
        }
        Moo::rc().effectVisualContext().worldMatrix(world_);
    }
    bool valid() const { return saved_ && targets_; }
private:
    ComObjectWrap<IDirect3DStateBlock9> state_;
    bool saved_ = false, targets_ = false;
    const Matrix* world_ = NULL;
};

bool StickerRenderer::draw()
{
    std::vector<QueuedSticker> batch; batch.swap(queue);
    if (batch.empty()) return true;
    if (!open || !material || !material->pEffect() || !declaration)
        return failure("The original sticker draw resources are unavailable");
    StickerDeviceScope scope;
    if (!scope.valid()) return failure("Cannot preserve the native device state for sticker drawing");
    const bool deferred = Renderer::instance().pipelineType() == IRendererPipeline::TYPE_DEFERRED_SHADING;
    if (deferred)
    {
        // Original 9589a0 obtains actual deferred surface(2), then restores
        // the complete Canvas target/depth/viewport state after the batch.
        if (FAILED(Moo::rc().setRenderTarget(0, Renderer::instance().pipeline()->gbufferSurface(2))))
            return failure("Cannot bind the original sticker GBuffer surface");
        for (unsigned slot = 1; slot != 4; ++slot)
            if (FAILED(Moo::rc().setRenderTarget(slot, NULL))) return failure("Cannot isolate the sticker colour target");
        DX::Viewport viewport; Moo::rc().getViewport(&viewport); setStockSceneViewport(&viewport, true);
    }
    if (!material->setTechnique(deferred ? "DEFERRED" : "FORWARD"))
        return failure("Cannot select the original sticker render technique");
    auto* device = Moo::rc().device();
    if (FAILED(device->SetStreamSourceFreq(0, 1)) || FAILED(Moo::rc().setVertexDeclaration(declaration->declaration())))
        return failure("Cannot bind the projected sticker vertex stream");
    std::map<const StickerOwner*, bool> validOwners;
    bool success = true;
    beginZBIASDraw(1.0005f);
    struct RestoreProjection { ~RestoreProjection() { endZBIASDraw(); } } restoreProjection;
    for (unsigned category : {1u, 0u, 2u})
    {
        for (const auto& item : batch)
        {
            if (!item.owner->active || !item.sticker->active || !item.sticker->image ||
                item.sticker->image->category != category) continue;
            auto valid = validOwners.find(item.owner.get());
            if (valid == validOwners.end())
            {
                PyModelPtr model; Matrix world;
                const auto geometry = item.owner->geometry;
                const unsigned generation = item.owner->generation;
                const bool acquired = geometry && geometry->acquire(model, world);
                if (!open) return true;
                valid = validOwners.emplace(item.owner.get(), acquired && item.owner->active &&
                    item.owner->generation == generation && item.owner->geometry == geometry).first;
            }
            if (!item.owner->active || !item.sticker->active || !valid->second || !(LodSettings::instance().applyLodBias(item.lodDistance) > item.distance)) continue;
            if (!item.sticker->image->prepare() || !item.sticker->prepare() ||
                FAILED(item.sticker->buffer.set(0, 0, sizeof(Vertex))) ||
                !material->properties().select(*material->pEffect())) { success = false; continue; }
            ID3DXEffect* effect = material->pEffect()->pEffect();
            const auto& image = *item.sticker->image;
            const Vector4 textureUV = image.uv();
            const Vector4 atlasSize(float(image.width), float(image.height), 1.f / image.width, 1.f / image.height);
            if (!effect || FAILED(effect->SetMatrix("worldSticker", &item.world)) ||
                FAILED(effect->SetFloat("alpha", item.alpha)) || FAILED(effect->SetVector("g_uv", &textureUV)) ||
                FAILED(effect->SetVector("g_atlasSize", &atlasSize)) ||
                FAILED(effect->SetBool("g_isMirrored", item.sticker->mirrored)) ||
                FAILED(effect->SetBool("g_noMips", category == 2)) ||
                FAILED(effect->SetTexture("g_atlasMap", image.view.pComObject()))) { success = false; continue; }
            if (!material->begin()) { effect->SetTexture("g_atlasMap", NULL); success = false; continue; }
            bool drawn = material->numPasses() != 0;
            for (uint32 pass = 0; drawn && pass < material->numPasses(); ++pass)
            {
                if (!material->beginPass(pass)) { drawn = false; break; }
                drawn = SUCCEEDED(HRESULT(Moo::rc().drawPrimitive(D3DPT_TRIANGLELIST, 0,
                    static_cast<uint32>(item.sticker->vertices.size() / 3))));
                drawn = material->endPass() && drawn;
            }
            drawn = material->end() && drawn;
            effect->SetTexture("g_atlasMap", NULL);
            success = drawn && success;
        }
    }
    return success;
}

} // namespace

PyObject* newStockVehicleStickerGeometry(const std::shared_ptr<StockVehicleStickerGeometry>& geometry)
{
    if (!geometry) { PyErr_SetString(PyExc_ValueError, "A real vehicle part geometry source is required"); return NULL; }
    return new PyVehicleStickerGeometry(geometry);
}

int registerStockDamageSticker(const BW::string& name, const BW::string& texture,
    const BW::string& bumpTexture, const Vector2& size, float variation, bool randomYaw)
{
    if (!renderer || !renderer->open) return -1;
    wot::StockDamageStickerVariant variant = {
        std::string(texture.data(), texture.size()), std::string(bumpTexture.data(), bumpTexture.size()),
        {{size.x, size.y}}, variation, randomYaw};
    return renderer->styles.add(std::string(name.data(), name.size()), variant);
}

bool initialiseStockVehicleStickers()
{
    if (renderer && renderer->open) return true;
    auto candidate = std::make_shared<StickerRenderer>();
    if (!candidate->initialise()) return false;
    renderer = candidate;
    return true;
}

void finiStockVehicleStickers()
{
    const auto current = renderer;
    renderer.reset();
    if (current) current->close();
}

bool drawStockVehicleStickers()
{
    const auto current = renderer;
    return !current || !current->open ? true : current->draw();
}

BW_END_NAMESPACE
