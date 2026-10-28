#include "pch.hpp"
#include "vehicle_appearance.hpp"
#include "stock_vehicle_stickers.hpp"
#include "stock_vehicle_fashions.hpp"
#include "stock_local_lighting.hpp"
#include "stock_effect_adapter.hpp"

#include "pyscript/script_math.hpp"
#include "cstdmf/bgtask_manager.hpp"
#include "duplo/py_attachment.hpp"
#include "duplo/pymodel.hpp"
#include "duplo/py_transform_fashion.hpp"
#include "model/fashion.hpp"
#include "model/model.hpp"
#include "model/super_model.hpp"
#include "model/super_model_node_tree.hpp"
#include "moo/node.hpp"
#include "moo/draw_context.hpp"
#include "moo/complex_effect_material.hpp"
#include "moo/effect_material.hpp"
#include "moo/effect_visual_context.hpp"
#include "moo/texture_manager.hpp"
#include "moo/render_context.hpp"
#include "resmgr/auto_config.hpp"
#include "romp/py_resource_refs.hpp"
#include "romp/resource_ref.hpp"
#include "romp/water_scene_renderer.hpp"

#include <map>
#include <memory>
#include <vector>

BW_BEGIN_NAMESPACE

int StockVehicleAppearance_token = 1;

// #1513's appearance owns these providers for its entire lifetime. Model
// refresh rebinds their targets rather than replacing providers held by nodes.
class WGAdaptiveMatrixProvider : public MatrixProvider
{
    Py_Header( WGAdaptiveMatrixProvider, MatrixProvider )

public:
    WGAdaptiveMatrixProvider() :
        MatrixProvider( false, &s_type_ ),
        staticTransform_( Matrix::identity )
    {
    }

    virtual void matrix( Matrix & result ) const
    {
        // Exact #1513 0x006dde30: delegate to the live target, or return the
        // explicit static transform. There is no implicit interpolation here.
        if (target_)
            target_->matrix( result );
        else
            result = staticTransform_;
    }

    void setStaticTransform( MatrixProviderPtr value )
    {
        // Exact #1513 0x006de930 samples the supplied provider once. It does
        // not retain the provider or change the live target.
        value->matrix( staticTransform_ );
    }

    PY_RW_ATTRIBUTE_DECLARE( target_, target )
    PY_AUTO_METHOD_DECLARE( RETVOID, setStaticTransform,
        NZARG( MatrixProviderPtr, END ) )

    static WGAdaptiveMatrixProvider * New()
    {
        return new WGAdaptiveMatrixProvider();
    }
    PY_AUTO_FACTORY_DECLARE( WGAdaptiveMatrixProvider, END )

private:
    MatrixProviderPtr target_;
    Matrix staticTransform_;
};

PY_TYPEOBJECT( WGAdaptiveMatrixProvider )
PY_BEGIN_METHODS( WGAdaptiveMatrixProvider )
    PY_METHOD( setStaticTransform )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( WGAdaptiveMatrixProvider )
    PY_ATTRIBUTE( target )
PY_END_ATTRIBUTES()
PY_FACTORY( WGAdaptiveMatrixProvider, Math )

namespace
{
AutoConfigString compoundStippleMap( "system/stippleMap" );

class CompoundStippleConstant : public Moo::EffectConstantValue
{
public:
    explicit CompoundStippleConstant( Moo::BaseTexturePtr texture ) : texture_( texture ) {}
    virtual bool operator()( ID3DXEffect * effect, D3DXHANDLE handle )
    {
        return SUCCEEDED( effect->SetTexture( handle, texture_ ? texture_->pTexture() : NULL ) );
    }
private:
    Moo::BaseTexturePtr texture_;
};

void clearCompoundStippleBinding( ID3DXEffect * effect )
{
    if (!effect) return;
    const D3DXHANDLE handle = effect->GetParameterBySemantic( NULL, "StippleMap" );
    if (handle) effect->SetTexture( handle, NULL );
}

void clearCompoundStippleMaterial( Moo::EffectMaterialPtr material )
{
    if (!material || !material->pEffect()) return;
    ID3DXEffect * binding = material->properties().existingStockBinding();
    clearCompoundStippleBinding( binding );
    ID3DXEffect * current = material->pEffect()->pEffect();
    if (current != binding && current && !wot::is_stock_effect( current ))
        clearCompoundStippleBinding( current );
}

struct CompoundRenderingResources
{
    bool open = false;
    bool registered = false;
    Moo::BaseTexturePtr texture;
    Moo::EffectConstantValuePtr constant;
    std::shared_ptr<Moo::DrawContext::OverrideBlock> stipple;

    void close()
    {
        if (!open && !registered) return;
        open = false;
        Moo::rc().effectVisualContext().unregisterAutoConstant(
            Moo::EffectVisualContext::AUTO_CONSTANT_TYPE_PER_OBJECT, "StippleMap" );
        Moo::EffectConstantValuePtr * mapping =
            Moo::rc().effectVisualContext().getMapping( "StippleMap", false );
        if (mapping && mapping->get() == constant.get()) *mapping = NULL;
        registered = false;
        for (unsigned index = 0; stipple && index < 2; ++index)
        {
            Moo::ComplexEffectMaterial * complex = stipple->stockMaterial( index != 0 );
            if (!complex) continue;
            clearCompoundStippleMaterial( complex->baseMaterial() );
            for (unsigned pass = 0; pass < Moo::RENDERING_PASS_COUNT; ++pass)
                for (unsigned instanced = 0; instanced < 2; ++instanced)
                    clearCompoundStippleMaterial( complex->pass(
                        static_cast<Moo::ERenderingPassType>( pass ), instanced != 0 ) );
        }
        IDirect3DDevice9 * device = Moo::rc().device();
        if (device && texture)
            for (unsigned stage = 0; stage < 20; ++stage)
            {
                const DWORD slot = stage < 16 ? stage : D3DVERTEXTEXTURESAMPLER0 + stage - 16;
                ComObjectWrap<IDirect3DBaseTexture9> bound;
                if (SUCCEEDED( device->GetTexture( slot, &bound ) ) &&
                    bound.pComObject() == texture->pTexture()) device->SetTexture( slot, NULL );
            }
        Moo::rc().invalidateStateCache();
        constant = NULL;
        texture = NULL;
        stipple.reset();
    }
};
std::shared_ptr<CompoundRenderingResources> compoundRenderingResources;

class CompoundStippleScope
{
public:
    CompoundStippleScope( Moo::DrawContext & context,
        const std::shared_ptr<Moo::DrawContext::OverrideBlock> & block ) :
        context_( context ), block_( block ), previous_( NULL )
    {
        if (block_) previous_ = context_.stockExchangeOverrideBlock( block_.get() );
    }
    ~CompoundStippleScope()
    {
        if (block_) context_.stockExchangeOverrideBlock( previous_ );
    }
private:
    Moo::DrawContext & context_;
    std::shared_ptr<Moo::DrawContext::OverrideBlock> block_;
    Moo::DrawContext::OverrideBlock * previous_;
    CompoundStippleScope( const CompoundStippleScope & ) = delete;
    CompoundStippleScope & operator=( const CompoundStippleScope & ) = delete;
};

class CompoundReflectionScope
{
public:
    CompoundReflectionScope( Moo::DrawContext & context, const PyModel * model ) :
        context_( context ), previous_( context.stockExchangeApprovedReflectionModel( model ) ) {}
    ~CompoundReflectionScope() { context_.stockExchangeApprovedReflectionModel( previous_ ); }
private:
    Moo::DrawContext & context_;
    const PyModel * previous_;
    CompoundReflectionScope( const CompoundReflectionScope & ) = delete;
    CompoundReflectionScope & operator=( const CompoundReflectionScope & ) = delete;
};

enum AssemblyOperationKind { ROOT_PART, ADD_PART, EMPLACE_PART, ADD_NODE, NODE_ALIAS };

struct AssemblyOperation
{
    AssemblyOperationKind kind;
    BW::string model;
    BW::string name;
    BW::string parent;
    BW::string prefix;
    Matrix local;
};

struct CompoundNode
{
    BW::string name;
    BW::string parentName;
    int parent;
    Matrix authoredLocal;
    Matrix local;
    Matrix world;
    MatrixProviderPtr source;
};

struct CompoundPart
{
    PyModelPtr model;
    PyObjectPtr fashion;
    BW::string mountName;
    size_t mount;
    // Original model node identifier -> compound node. Scene Root is omitted
    // by #1513 0x008e21e0; the model mounts on its declared compound parent.
    std::vector<std::pair<BW::string, size_t> > bones;
};

struct CompoundAttachment;
typedef std::shared_ptr<CompoundAttachment> CompoundAttachmentPtr;

struct CompoundState
{
    std::vector<CompoundNode> nodes;
    std::vector<CompoundPart> parts;
    std::map<BW::string, size_t> names;
    MatrixProviderPtr matrix;
    std::vector<CompoundAttachmentPtr> attachments;
    bool active;
    uint32 frame;
    uint32 spaceID;

    CompoundState() : active( true ), frame( 0 ), spaceID( 0 ) {}

    bool evaluate( size_t index, std::vector<unsigned char> & visiting )
    {
        if (visiting[index] == 2)
            return true;
        if (visiting[index] == 1)
        {
            PyErr_SetString( PyExc_ValueError, "Compound node hierarchy contains a cycle" );
            return false;
        }
        visiting[index] = 1;
        CompoundNode & node = nodes[index];
        // #1513 0x008e3f00 writes providers directly into local transforms.
        // Applying a provider as an extra transform doubles authored offsets.
        MatrixProviderPtr provider = index == 0 && matrix ? matrix : node.source;
        if (provider)
        {
            Matrix local = node.local;
            provider->matrix( local );
            if (!active) return false;
            node.local = local;
        }
        if (node.parent >= 0)
        {
            if (!evaluate( static_cast<size_t>( node.parent ), visiting ))
                return false;
            node.world.multiply( node.local, nodes[node.parent].world );
        }
        else
            node.world = node.local;
        visiting[index] = 2;
        return true;
    }

    bool evaluate()
    {
        std::vector<unsigned char> visiting( nodes.size(), 0 );
        for (size_t index = 0; index < nodes.size(); ++index)
            if (!evaluate( index, visiting ))
                return false;
        return true;
    }
};

typedef std::shared_ptr<CompoundState> CompoundStatePtr;

// #1513 AttachmentList::NodeProvider (008db160/008fd4a0) is a real
// MatrixLiaison. Its setter acknowledges the attachment's initial transform
// without replacing the authored local offset or the compound node pose.
struct CompoundAttachment : MatrixLiaison
{
    PyAttachmentPtr object;
    size_t node;
    Matrix local;
    Matrix world;
    bool castsLocalShadow;
    bool active;

    CompoundAttachment( PyAttachmentPtr value, size_t index,
        const Matrix & offset, const Matrix & parent, bool flags ) :
        object( value ), node( index ), local( offset ), castsLocalShadow( flags ), active( true )
    {
        world.multiply( local, parent );
    }

    virtual const Matrix & getMatrix() const { return world; }
    virtual bool setMatrix( const Matrix & ) { return true; }

    bool ownsObject()
    {
        return object->isAttached() && object->matrixLiaison() == this;
    }

    void retire()
    {
        // Membership is removed before calling arbitrary attachment code.
        // Reentrant removal and compound reset therefore see the retired item.
        active = false;
        if (!this->ownsObject()) return;
        if (object->isInWorld()) object->leaveWorld();
        if (this->ownsObject()) object->detach();
    }

    ~CompoundAttachment() { this->retire(); }
};

CompoundAttachmentPtr findCompoundAttachment( const CompoundStatePtr & state, PyObject * object )
{
    for (size_t index = 0; index < state->attachments.size(); ++index)
    {
        CompoundAttachmentPtr item = state->attachments[index];
        if (item->active && item->object.get() == object) return item;
    }
    return CompoundAttachmentPtr();
}

void removeCompoundAttachment( const CompoundStatePtr & state, PyObject * object )
{
    for (size_t index = 0; index < state->attachments.size(); ++index)
    {
        CompoundAttachmentPtr item = state->attachments[index];
        if (item->object.get() != object) continue;
        state->attachments.erase( state->attachments.begin() + index );
        item->retire();
        return;
    }
}

template<class Callback>
void visitCompoundAttachments( const CompoundStatePtr & state, Callback callback )
{
    const std::vector<CompoundAttachmentPtr> snapshot = state->attachments;
    for (size_t index = 0; index < snapshot.size() && state->active; ++index)
    {
        const CompoundAttachmentPtr & item = snapshot[index];
        if (!item->active || !item->ownsObject()) continue;
        item->world.multiply( item->local, state->nodes[item->node].world );
        callback( *item );
    }
}

class CompoundStickerGeometry : public StockVehicleStickerGeometry
{
public:
    CompoundStickerGeometry(const CompoundStatePtr& state, size_t part) :
        state_(state), part_(part), identity_(state->parts[part].model.get()) {}

    virtual bool acquire(PyModelPtr& model, Matrix& world)
    {
        CompoundStatePtr state = state_.lock();
        if (!state || !state->active || part_ >= state->parts.size() ||
            state->parts[part_].model.get() != identity_ || !state->evaluate())
            return false;
        // Matrix providers can synchronously reset the compound while evaluate
        // calls Python. Revalidate the live part before dereferencing it again.
        if (!state->active || part_ >= state->parts.size() ||
            state->parts[part_].model.get() != identity_ ||
            state->parts[part_].mount >= state->nodes.size()) return false;
        world = state->nodes[state->parts[part_].mount].world;
        model = state->parts[part_].model;
        return model->pSuperModel() != NULL;
    }

private:
    std::weak_ptr<CompoundState> state_;
    size_t part_;
    // Identity comparison only, after locking the live part owner. Retaining
    // the PyModel here would cycle through its node's sticker attachment.
    PyObject* identity_;
};

// A real pre-transform fashion writes the original skeleton's local pose
// before Model::dress computes the skin palette. PyModelNode.local only
// transforms attachments and cannot implement the stock Gun bone contract.
class CompoundPose : public TransformFashion
{
public:
    CompoundPose( const CompoundStatePtr & state, size_t part ) :
        state_( state ), part_( part ) {}

    virtual void dress( SuperModel & model, Matrix & /* world */ )
    {
        CompoundStatePtr state = state_.lock();
        if (!state || !state->active)
            return;
        const CompoundPart & part = state->parts[part_];
        for (size_t index = 0; index < part.bones.size(); ++index)
        {
            const std::pair<BW::string, size_t> & bone = part.bones[index];
            Moo::NodePtr node = model.findNode( bone.first );
            if (node)
                node->blendClobber( Model::blendCookie(), state->nodes[bone.second].local );
        }
    }
    virtual bool needsRedress() const { return false; }

private:
    std::weak_ptr<CompoundState> state_;
    size_t part_;
};
}

class PyCompoundPose : public PyTransformFashion
{
    Py_Header( PyCompoundPose, PyTransformFashion )
public:
    explicit PyCompoundPose( TransformFashionPtr fashion ) :
        PyTransformFashion( &s_type_ ), fashion_( fashion ) {}
    virtual const TransformFashionPtr transformFashion() const { return fashion_; }
    virtual TransformFashionPtr transformFashion() { return fashion_; }
private:
    TransformFashionPtr fashion_;
};
PY_TYPEOBJECT( PyCompoundPose )
PY_BEGIN_METHODS( PyCompoundPose )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( PyCompoundPose )
PY_END_ATTRIBUTES()

class PyCompoundNode : public MatrixProvider
{
    Py_Header( PyCompoundNode, MatrixProvider )
public:
    PyCompoundNode( const CompoundStatePtr & state, size_t node ) :
        MatrixProvider( false, &s_type_ ), state_( state ), node_( node ) {}
    virtual void matrix( Matrix & result ) const
    {
        result = state_->active ? state_->nodes[node_].world : Matrix::identity;
    }
    PY_RO_ATTRIBUTE_DECLARE( state_->nodes[node_].name, name )
    bool isDangling() const { return !state_->active; }
    PY_RO_ATTRIBUTE_DECLARE( this->isDangling(), isDangling )
    MatrixProviderPtr local() const
    {
        if (!state_->active) return MatrixProviderPtr();
        return node_ == 0 ? state_->matrix : state_->nodes[node_].source;
    }
    void local( MatrixProviderPtr value )
    {
        if (!state_->active) return;
        // Keep the old provider alive until the new binding is visible: its
        // Python destructor may reset the compound or rebind this same node.
        MatrixProviderPtr previous = this->local();
        if (node_ == 0) state_->matrix = value;
        else state_->nodes[node_].source = value;
    }
    PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( MatrixProviderPtr, local, local )
    Vector3 position() const
    {
        return state_->active ? state_->nodes[node_].world.applyToOrigin() : Vector3::zero();
    }
    PY_RO_ATTRIBUTE_DECLARE( this->position(), position )
    PyObject * pyGet_localMatrix()
    {
        // 008dcd90 returns a Math.Matrix value, not a provider or a writable
        // node alias. A reset compound returns the original identity value.
        return Script::getData( state_->active ? state_->nodes[node_].local : Matrix::identity );
    }
    PY_RO_ATTRIBUTE_SET( localMatrix )
    PyObject * attach( PyObjectPtr object, const Matrix & offset, bool flags )
    {
        if (!state_->active) Py_RETURN_NONE;
        if (!PyAttachment::Check( object.get() ))
        {
            PyErr_SetString( PyExc_TypeError, "attach requires a native PyAttachment" );
            return NULL;
        }
        PyAttachmentPtr attachment( static_cast<PyAttachment *>( object.get() ) );
        if (attachment->isAttached())
        {
            PyErr_SetString( PyExc_TypeError, "Attachment cannot be attached elsewhere" );
            return NULL;
        }
        CompoundAttachmentPtr item( new CompoundAttachment( attachment, node_, offset,
            state_->nodes[node_].world, flags ) );
        // Publish the identity before virtual attach/enterWorld may call Python.
        state_->attachments.push_back( item );
        if (!attachment->attach( item.get() ))
        {
            removeCompoundAttachment( state_, object.get() );
            if (!PyErr_Occurred()) PyErr_SetString( PyExc_TypeError, "Attachment rejected its node" );
            return NULL;
        }
        if (!state_->active || !item->active || !item->ownsObject())
        {
            item->retire();
            Py_RETURN_NONE;
        }
        // 008da620/008da7b0 enter the attachment immediately, even when the
        // compound has not yet been inserted into a ClientSpace.
        if (!attachment->isInWorld()) attachment->enterWorld();
        if (!state_->active || !item->active) item->retire();
        Py_RETURN_NONE;
    }
    PY_AUTO_METHOD_DECLARE( RETOWN, attach, NZARG( PyObjectPtr,
        OPTARG( Matrix, Matrix::identity, OPTARG( bool, false, END ) ) ) )
    void detach( PyObjectPtr object )
    {
        // 008e05a0 compares the owning compound, not the adapter's node index.
        if (state_->active) removeCompoundAttachment( state_, object.get() );
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, detach, NZARG( PyObjectPtr, END ) )
private:
    CompoundStatePtr state_;
    size_t node_;
};
PY_TYPEOBJECT( PyCompoundNode )
PY_BEGIN_METHODS( PyCompoundNode )
    PY_METHOD( attach )
    PY_METHOD( detach )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( PyCompoundNode )
    PY_ATTRIBUTE( name )
    PY_ATTRIBUTE( isDangling )
    PY_ATTRIBUTE( local )
    PY_ATTRIBUTE( localMatrix )
    PY_ATTRIBUTE( position )
PY_END_ATTRIBUTES()

class PyCompoundPartBounds : public MatrixProvider
{
    Py_Header( PyCompoundPartBounds, MatrixProvider )
public:
    PyCompoundPartBounds( const CompoundStatePtr & state, size_t part ) :
        MatrixProvider( false, &s_type_ ), state_( state ), part_( part ),
        last_( Matrix::identity )
    {
        this->matrix( last_ );
    }
    virtual void matrix( Matrix & result ) const
    {
        if (state_->active)
        {
            // #1513 008dd280 transforms this part's authored minimum by its
            // mount node and scales each basis row by the matching extent.
            PyModelPtr model = state_->parts[part_].model;
            const size_t mount = state_->parts[part_].mount;
            BoundingBox bounds;
            model->localBoundingBox( bounds );
            if (!state_->active || part_ >= state_->parts.size() ||
                state_->parts[part_].model.get() != model.get())
            {
                result = last_;
                return;
            }
            const Matrix & world = state_->nodes[mount].world;
            const Vector3 extent = bounds.maxBounds() - bounds.minBounds();
            last_ = world;
            last_.translation( world.applyPoint( bounds.minBounds() ) );
            last_[0] *= extent.x;
            last_[1] *= extent.y;
            last_[2] *= extent.z;
        }
        // As with retained node adapters, teardown keeps the last committed
        // snapshot rather than dereferencing the reset compound's resources.
        result = last_;
    }
private:
    CompoundStatePtr state_;
    size_t part_;
    mutable Matrix last_;
};
PY_TYPEOBJECT( PyCompoundPartBounds )
PY_BEGIN_METHODS( PyCompoundPartBounds )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( PyCompoundPartBounds )
PY_END_ATTRIBUTES()

class PyCompoundModel : public PyAttachment
{
    Py_Header( PyCompoundModel, PyAttachment )
public:
    explicit PyCompoundModel( const CompoundStatePtr & state ) :
        PyAttachment( &s_type_ ), state_( state ) {}

    virtual ~PyCompoundModel()
    {
        this->reset();
    }

    virtual void tick( float elapsed )
    {
        if (!state_->active || !state_->evaluate()) return;
        std::vector<CompoundPart> parts = state_->parts;
        for (size_t index = 0; index < parts.size() && state_->active; ++index)
            parts[index].model->tick( elapsed );
        visitCompoundAttachments( state_, [elapsed]( CompoundAttachment & item )
            { item.object->tick( elapsed ); } );
        // Parts draw from the compound's evaluated root. Publish that same
        // root through the attachment liaison before the scene's post-tick
        // spatial update, including while the compound is outside the view.
        // Entity only installs an ActionMatcher for PyModel, not compounds.
        if (state_->active)
            this->worldTransform( state_->nodes[0].world );
    }
    virtual bool needsUpdate() const { return state_->active; }
    virtual bool isLodVisible() const { return this->visible(); }
#if ENABLE_TRANSFORM_VALIDATION
    virtual bool isTransformFrameDirty( uint32 frame ) const
    {
        return state_->active && state_->frame != frame;
    }
    virtual bool isVisibleTransformFrameDirty( uint32 frame ) const
    {
        return this->isTransformFrameDirty( frame );
    }
#endif
    virtual void updateAnimations( const Matrix & /* world */, float lod )
    {
        if (!state_->active || !state_->evaluate())
            return;
        std::vector<CompoundPart> parts = state_->parts;
        for (size_t index = 0; index < parts.size() && state_->active; ++index)
        {
            CompoundPart & part = parts[index];
            const Matrix pose = state_->nodes[part.mount].world;
            part.model->worldTransform( pose );
            if (!state_->active) return;
            part.model->updateAnimations( pose, lod );
        }
        visitCompoundAttachments( state_, [lod]( CompoundAttachment & item )
            { item.object->updateAnimations( item.world, lod ); } );
        state_->frame = Moo::rc().frameTimestamp();
    }
    virtual void draw( Moo::DrawContext & context, const Matrix & /* world */ )
    {
        // 008de410/008de2c0 suppress both parts and their independent
        // attachments outside pass 2 when the compound skips its colour pass.
        if (skipColorPass_ && context.renderingPassType() != Moo::RENDERING_PASS_SHADOWS)
            return;
        if (this->visible())
        {
            std::vector<CompoundPart> parts = state_->parts;
            // 008de410 approves the complete compound at its root once, then
            // charges all parts. It never consumes a separate budget slot per
            // part or lets a budget boundary reflect only half the vehicle.
            PyModelPtr reflectionModel = parts[0].model;
            const bool reflection = Moo::rc().reflectionScene();
            const bool drawParts = !reflection || (WaterSceneRenderer::currentScene() &&
                WaterSceneRenderer::currentScene()->shouldReflect(
                    state_->nodes[0].world.applyToOrigin(), reflectionModel.get() ));
            const auto resources = compoundRenderingResources;
            const auto override = stipple_ && resources && resources->open ? resources->stipple :
                std::shared_ptr<Moo::DrawContext::OverrideBlock>();
            if (drawParts && stipple_ && !override)
            {
                ERROR_MSG( "CompoundModel: stipple rendering resources are unavailable\n" );
                return;
            }
            if (drawParts)
            {
                if (reflection)
                    for (size_t index = 0; index < parts.size(); ++index)
                        WaterSceneRenderer::incReflectionCount();
                // Original 008de410 scopes the override only around its own
                // model parts; independently registered attachments retain the
                // caller's previous override when their draw is dispatched.
                CompoundStippleScope scope( context, override );
                for (size_t index = 0; index < parts.size() && state_->active; ++index)
                {
                    CompoundPart & part = parts[index];
                    CompoundReflectionScope approved( context, reflection ? part.model.get() : NULL );
                    part.model->draw( context, state_->nodes[part.mount].world );
                }
            }
            visitCompoundAttachments( state_, [&context, reflection, &reflectionModel]( CompoundAttachment & item )
                {
                    // 008de1d0 publishes flags 01/15 -> ab8060 masks
                    // 203/337. Native point/spot shadow queries a68f90 and
                    // a69610 require mask 100, present only for the true case.
                    // The same public pass also draws sunlight. Its original
                    // mask is independent of this local-light participation.
                    if (!stockLocalLightingShadowContext( context ) || item.castsLocalShadow)
                    {
                        // 008de2c0 independently tests each attachment at its
                        // own world position with the parent compound's priority.
                        if (reflection)
                        {
                            if (!WaterSceneRenderer::currentScene() ||
                                !WaterSceneRenderer::currentScene()->shouldReflect(
                                    item.world.applyToOrigin(), reflectionModel.get() )) return;
                            WaterSceneRenderer::incReflectionCount();
                        }
                        CompoundReflectionScope approved( context,
                            reflection ? item.object->getPyModel() : NULL );
                        item.object->draw( context, item.world );
                    }
                } );
        }
    }
    virtual void enterWorld()
    {
        PyAttachment::enterWorld();
        std::vector<CompoundPart> parts = state_->parts;
        for (size_t index = 0; index < parts.size() && state_->active; ++index)
            if (!parts[index].model->isInWorld()) parts[index].model->enterWorld();
        visitCompoundAttachments( state_, []( CompoundAttachment & item )
            { if (!item.object->isInWorld()) item.object->enterWorld(); } );
    }
    virtual void leaveWorld()
    {
        if (this->isInWorld())
            PyAttachment::leaveWorld();
        std::vector<CompoundPart> parts = state_->parts;
        for (size_t index = 0; index < parts.size() && state_->active; ++index)
            if (parts[index].model->isInWorld()) parts[index].model->leaveWorld();
        visitCompoundAttachments( state_, []( CompoundAttachment & item )
            { if (item.object->isInWorld()) item.object->leaveWorld(); } );
    }
    virtual void tossed( bool outside )
    {
        std::vector<CompoundPart> parts = state_->parts;
        for (size_t index = 0; index < parts.size() && state_->active; ++index)
            parts[index].model->tossed( outside );
        visitCompoundAttachments( state_, [outside]( CompoundAttachment & item )
            { item.object->tossed( outside ); } );
    }
    virtual void localBoundingBox( BoundingBox & bounds, bool skinny = false )
    {
        Matrix inverse;
        if (!state_->active || !inverse.invert( state_->nodes[0].world ))
            return;
        std::vector<CompoundPart> parts = state_->parts;
        for (size_t index = 0; index < parts.size() && state_->active; ++index)
        {
            CompoundPart & part = parts[index];
            BoundingBox local;
            part.model->localBoundingBox( local, skinny );
            if (!state_->active) return;
            if (local.insideOut()) continue;
            Matrix relative;
            relative.multiply( state_->nodes[part.mount].world, inverse );
            local.transformBy( relative );
            bounds.addBounds( local );
        }
        const CompoundStatePtr state = state_;
        visitCompoundAttachments( state, [&bounds, &inverse, skinny, state]( CompoundAttachment & item )
            {
                BoundingBox local;
                item.object->localBoundingBox( local, skinny );
                // Disabled/empty particles and retired attachments may contribute
                // no bounds. The SDK's inside-out sentinel cannot be transformed.
                if (!state->active || !item.active || !item.ownsObject() || local.insideOut()) return;
                Matrix relative;
                relative.multiply( item.world, inverse );
                local.transformBy( relative );
                bounds.addBounds( local );
            } );
    }
    virtual void localVisibilityBox( BoundingBox & bounds, bool skinny = false )
    {
        this->localBoundingBox( bounds, skinny );
    }
    virtual void worldBoundingBox( BoundingBox & bounds, const Matrix &, bool skinny = false )
    {
        if (state_->active)
        {
            std::vector<CompoundPart> parts = state_->parts;
            for (size_t index = 0; index < parts.size() && state_->active; ++index)
            {
                CompoundPart & part = parts[index];
                part.model->worldBoundingBox( bounds, state_->nodes[part.mount].world, skinny );
            }
            visitCompoundAttachments( state_, [&bounds, skinny]( CompoundAttachment & item )
                { item.object->worldBoundingBox( bounds, item.world, skinny ); } );
        }
    }
    virtual void worldVisibilityBox( BoundingBox & bounds, const Matrix & world, bool skinny = false )
    {
        this->worldBoundingBox( bounds, world, skinny );
    }

    PyObject * node( const BW::string & name, MatrixProviderPtr source )
    {
        if (!state_->active)
        {
            PyErr_SetString( PyExc_ValueError, "CompoundModel has been reset" );
            return NULL;
        }
        std::map<BW::string, size_t>::const_iterator found = state_->names.find( name );
        const size_t index = name.empty() ? 0 :
            (found == state_->names.end() ? state_->nodes.size() : found->second);
        if (index >= state_->nodes.size())
        {
            // #1513 008dd57c returns a null adapter; Script::getData at
            // 0068ed50 converts it to None for optional authored nodes.
            Py_RETURN_NONE;
        }
        if (source)
        {
            if (index == 0)
                state_->matrix = source;
            else
                state_->nodes[index].source = source;
        }
        // The stock node adapter is cached by node index. Its identity is
        // independent of which local source was most recently installed.
        PyObjectPtr & handle = handles_[index];
        if (!handle)
            handle = PyObjectPtr( new PyCompoundNode( state_, index ), PyObjectPtr::STEAL_REFERENCE );
        Py_INCREF( handle.get() );
        return handle.get();
    }
    PY_AUTO_METHOD_DECLARE( RETOWN, node,
        ARG( BW::string, OPTARG( MatrixProviderPtr, NULL, END ) ) )
    // Exact #1513 008dd020 delegates to node("", NULL); 008dd500 resolves
    // that name to index zero and returns the same cached node adapter.
    PyObject * pyGet_root() { return this->node( "", MatrixProviderPtr() ); }
    PY_RO_ATTRIBUTE_SET( root )

    bool context( uint32 & spaceID, BoundingBox & worldBounds )
    {
        if (!state_->active || !state_->evaluate())
            return false;
        spaceID = state_->spaceID;
        this->worldBoundingBox( worldBounds, Matrix::identity );
        return true;
    }
    bool readNode( const BW::string & name, Matrix & authoredLocal,
        Matrix & parentWorld, Matrix & world )
    {
        if (!state_->active || !state_->evaluate())
            return false;
        const std::map<BW::string, size_t>::const_iterator found = state_->names.find( name );
        const size_t index = name.empty() ? 0 :
            (found == state_->names.end() ? state_->nodes.size() : found->second);
        if (index >= state_->nodes.size())
            return false;
        authoredLocal = state_->nodes[index].authoredLocal;
        const int parent = state_->nodes[index].parent;
        parentWorld = parent < 0 ? Matrix::identity : state_->nodes[parent].world;
        world = state_->nodes[index].world;
        return true;
    }
    // Svarog managers read the stored node-local array without evaluating.
    bool readNodeLocal( const BW::string & name, Matrix & local ) const
    {
        if (!state_->active)
            return false;
        const std::map<BW::string, size_t>::const_iterator found = state_->names.find( name );
        const size_t index = name.empty() ? 0 :
            (found == state_->names.end() ? state_->nodes.size() : found->second);
        if (index >= state_->nodes.size())
            return false;
        local = state_->nodes[index].local;
        return true;
    }
    // Node-array identity: the node index and its resolved parent index.
    bool readNodeIdentity( const BW::string & name, int & index, int & parent ) const
    {
        if (!state_->active)
            return false;
        const std::map<BW::string, size_t>::const_iterator found = state_->names.find( name );
        const size_t node = name.empty() ? 0 :
            (found == state_->names.end() ? state_->nodes.size() : found->second);
        if (node >= state_->nodes.size())
            return false;
        index = static_cast<int>( node );
        parent = state_->nodes[node].parent;
        return true;
    }
    bool writeNodeLocal( const BW::string & name, const Matrix & local )
    {
        if (!state_->active)
            return false;
        const std::map<BW::string, size_t>::const_iterator found = state_->names.find( name );
        const size_t index = name.empty() ? 0 :
            (found == state_->names.end() ? state_->nodes.size() : found->second);
        if (index >= state_->nodes.size())
            return false;
        state_->nodes[index].local = local;
        return true;
    }

    PyObject * getBoundsForPart( int index )
    {
        if (!state_->active || index < 0 || static_cast<size_t>( index ) >= state_->parts.size())
        {
            PyErr_SetString( PyExc_ValueError, "CompoundModel has no such active part" );
            return NULL;
        }
        return new PyCompoundPartBounds( state_, static_cast<size_t>( index ) );
    }
    PY_AUTO_METHOD_DECLARE( RETOWN, getBoundsForPart, ARG( int, END ) )

    PyObject * getBoundsForRoot() { return this->getBoundsForPart( 0 ); }
    PY_AUTO_METHOD_DECLARE( RETOWN, getBoundsForRoot, END )

    bool containsAttachment( PyObjectPtr object )
    {
        return state_->active && !!findCompoundAttachment( state_, object.get() );
    }
    PY_AUTO_METHOD_DECLARE( RETDATA, containsAttachment, ARG( PyObjectPtr, END ) )

    // Both #1513 wrappers (008dc6f0/008dc920) validate one BW::string and
    // return None. Their complete native bodies perform no sound operation.
    void getSound( const BW::string & ) {}
    void playSound( const BW::string & ) {}
    PY_AUTO_METHOD_DECLARE( RETVOID, getSound, ARG( BW::string, END ) )
    PY_AUTO_METHOD_DECLARE( RETVOID, playSound, ARG( BW::string, END ) )

    PyObject* getPartGeometryLink(int index)
    {
        if (!state_->active || index < 0 || static_cast<size_t>(index) >= state_->parts.size())
        {
            PyErr_SetString(PyExc_ValueError, "CompoundModel has no such active part");
            return NULL;
        }
        return newStockVehicleStickerGeometry(
            std::make_shared<CompoundStickerGeometry>(state_, static_cast<size_t>(index)));
    }
    PY_AUTO_METHOD_DECLARE( RETOWN, getPartGeometryLink, ARG( int, END ) )

    bool isHighPriorityReflection() const { return state_->active && highPriorityReflection_; }
    void isHighPriorityReflection( bool value )
    {
        highPriorityReflection_ = value;
        std::vector<CompoundPart> parts = state_->parts;
        for (size_t index = 0; index < parts.size() && state_->active; ++index)
            parts[index].model->highPriorityReflection( value );
    }
    PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( bool, isHighPriorityReflection, isHighPriorityReflection )

    MatrixProviderPtr matrix() const { return state_->matrix; }
    void matrix( MatrixProviderPtr value )
    {
        if (!state_->active) return;
        MatrixProviderPtr previous = state_->matrix;
        state_->matrix = value;
    }
    PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( MatrixProviderPtr, matrix, matrix )
    bool visible() const
    {
        // 008df790 tests all per-part visibility bytes, not a separate
        // compound-level cached flag. 008dfd30 writes every byte on assignment.
        if (state_->active)
            for (size_t index = 0; index < state_->parts.size(); ++index)
                if (state_->parts[index].model->visible()) return true;
        return false;
    }
    void visible( bool value )
    {
        std::vector<CompoundPart> parts = state_->parts;
        for (size_t index = 0; index < parts.size() && state_->active; ++index)
            parts[index].model->visible( value );
    }
    PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( bool, visible, visible )
    bool inWorld() const { return state_->active && PyAttachment::isInWorld(); }
    PY_RO_ATTRIBUTE_DECLARE( this->inWorld(), isInWorld )
    Vector3 position() const
    {
        return state_->active ? state_->nodes[0].world.applyToOrigin() : Vector3::zero();
    }
    void position( const Vector3 & value )
    {
        if (!state_->active) return;
        // 008ddb70 installs a new static root provider containing a pure
        // translation. It discards the old root rotation/scale/provider.
        Matrix translation;
        translation.setTranslate( value );
        PyMatrix * provider = new PyMatrix();
        provider->set( translation );
        this->matrix( MatrixProviderPtr( provider, true ) );
    }
    PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( Vector3, position, position )
    PY_RW_ATTRIBUTE_DECLARE( skipColorPass_, skipColorPass )
    PY_RW_ATTRIBUTE_DECLARE( stipple_, stipple )

    PY_METHOD_DECLARE( py_setupFashions )

    void reset()
    {
        if (!state_->active)
            return;
        state_->active = false;
        // Retire all identities before a virtual leaveWorld/detach or a
        // Python finalizer can reenter reset, query nodes, or detach an item.
        std::vector<CompoundAttachmentPtr> attachments;
        attachments.swap( state_->attachments );
        std::vector<CompoundPart> parts;
        parts.swap( state_->parts );
        if (this->isInWorld()) PyAttachment::leaveWorld();
        for (size_t index = 0; index < attachments.size(); ++index)
            attachments[index]->retire();
        for (size_t index = 0; index < parts.size(); ++index)
            if (parts[index].model->isInWorld()) parts[index].model->leaveWorld();
        state_->matrix = NULL;
        for (size_t index = 0; index < state_->nodes.size(); ++index)
            state_->nodes[index].source = NULL;
        handles_.clear();
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, reset, END )

private:
    CompoundStatePtr state_;
    std::map<size_t, PyObjectPtr> handles_;
    bool highPriorityReflection_ = false;
    bool skipColorPass_ = false;
    bool stipple_ = false;
};
PyObject * PyCompoundModel::py_setupFashions( PyObject * args )
{
    PyObject * object;
    if (!PyArg_ParseTuple( args, "O:setupFashions", &object ))
        return NULL;
    if (!state_->active)
    {
        PyErr_SetString( PyExc_ValueError, "CompoundModel has been reset" );
        return NULL;
    }
    PyObjectPtr values( PySequence_Fast( object, "setupFashions requires a fashion sequence" ),
        PyObjectPtr::STEAL_REFERENCE );
    if (!values)
        return NULL;
    const Py_ssize_t count = PySequence_Fast_GET_SIZE( values.get() );
    if (static_cast<size_t>( count ) > state_->parts.size())
    {
        PyErr_SetString( PyExc_ValueError, "There are more fashions than compound model parts" );
        return NULL;
    }
    // 008dca60 converts the entire vector before touching a part. A
    // shorter vector updates only its supplied slots, including None.
    for (Py_ssize_t index = 0; index < count; ++index)
        if (!isStockVehicleFashion( PySequence_Fast_GET_ITEM( values.get(), index ) ))
        {
            PyErr_SetString( PyExc_TypeError, "setupFashions requires native vehicle fashions or None" );
            return NULL;
        }
    for (Py_ssize_t index = 0; index < count; ++index)
    {
        CompoundPart & part = state_->parts[index];
        PyObject * fashion = PySequence_Fast_GET_ITEM( values.get(), index );
        if ((part.fashion ? part.fashion.get() : Py_None) == fashion)
            continue;
        // 008dfd80 detaches an old fashion before attaching its new
        // replacement. Equal pointers preserve the existing binding.
        if (!setStockVehiclePartFashion( part.model.get(), fashion ))
            return NULL;
        part.fashion = fashion == Py_None ? NULL : fashion;
    }
    Py_RETURN_NONE;
}
PY_TYPEOBJECT( PyCompoundModel )
PY_BEGIN_METHODS( PyCompoundModel )
    PY_METHOD( node )
    PY_METHOD( reset )
    PY_METHOD( getBoundsForPart )
    PY_METHOD( getBoundsForRoot )
    PY_METHOD( containsAttachment )
    PY_METHOD( getSound )
    PY_METHOD( playSound )
    PY_METHOD( getPartGeometryLink )
    PY_METHOD( setupFashions )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( PyCompoundModel )
    PY_ATTRIBUTE( root )
    PY_ATTRIBUTE( matrix )
    PY_ATTRIBUTE( visible )
    PY_ATTRIBUTE( isHighPriorityReflection )
    PY_ATTRIBUTE( isInWorld )
    PY_ATTRIBUTE( position )
    PY_ATTRIBUTE( skipColorPass )
    PY_ATTRIBUTE( stipple )
PY_END_ATTRIBUTES()

namespace
{
PyModelPtr loadPartModel( const BW::string & resource )
{
    PyObjectPtr args( Py_BuildValue( "(s)", resource.c_str() ),
        PyObjectPtr::STEAL_REFERENCE );
    if (!args)
        return PyModelPtr();
    PyObject * object = PyModel::pyNew( args.get() );
    if (!object)
        return PyModelPtr();
    return PyModelPtr( static_cast<PyModel *>( object ), ScriptObject::FROM_NEW_REFERENCE );
}

size_t appendNode( CompoundState & state, const BW::string & name,
    const BW::string & parent, const Matrix & local )
{
    CompoundNode node;
    node.name = name;
    node.parentName = parent;
    node.parent = -2;
    node.authoredLocal = local;
    node.local = local;
    node.world = local;
    const size_t index = state.nodes.size();
    state.nodes.push_back( node );
    state.names[name] = index;
    return index;
}

PyObject * buildCompound( const std::vector<AssemblyOperation> & operations, uint32 spaceID )
{
    CompoundStatePtr state( new CompoundState() );
    state->spaceID = spaceID;
    std::vector<std::pair<BW::string, BW::string> > aliases;
    bool haveRoot = false;
    for (size_t operationIndex = 0; operationIndex < operations.size(); ++operationIndex)
    {
        const AssemblyOperation & operation = operations[operationIndex];
        if (operation.kind == NODE_ALIAS)
        {
            aliases.push_back( std::make_pair( operation.parent, operation.name ) );
            continue;
        }
        if (operation.kind == ROOT_PART)
        {
            if (haveRoot)
            {
                PyErr_SetString( PyExc_ValueError,
                    "A vehicle compound requires exactly one root part" );
                return NULL;
            }
            haveRoot = true;
        }
        else if (!haveRoot)
        {
            PyErr_SetString( PyExc_ValueError, "Compound operation precedes its root part" );
            return NULL;
        }
        if (operation.kind != EMPLACE_PART)
        {
            const size_t node = appendNode( *state, operation.name,
                operation.parent, operation.local );
            if (operation.kind == ROOT_PART)
                state->nodes[node].parent = -1;
        }
        else if (!operation.name.empty())
            aliases.push_back( std::make_pair( operation.parent, operation.name ) );
        if (operation.kind == ADD_NODE)
            continue;

        CompoundPart part;
        part.mountName = operation.kind == EMPLACE_PART ? operation.parent : operation.name;
        part.model = loadPartModel( operation.model );
        if (!part.model)
            return NULL;
        SuperModel * superModel = part.model->pSuperModel();
        if (!superModel || (!superModel->getInstanceNodeTree() &&
                !superModel->regenerateInstanceNodeTree()))
        {
            PyErr_Format( PyExc_ValueError, "Compound part has no skeleton: %s",
                operation.model.c_str() );
            return NULL;
        }
        const SuperModelNodeTree * tree = superModel->getInstanceNodeTree();
        const int count = tree->getNumNodes();
        std::vector<size_t> indices( count, 0 );
        // #1513 0x008e21e0 discards Scene Root and grafts its children onto
        // the named mount, retaining their actual parent and local transforms.
        for (int index = 1; index < count; ++index)
        {
            const SuperModelNode & bone = tree->getNode( index );
            const size_t node = appendNode( *state,
                operation.prefix + bone.getIdentifier(), part.mountName,
                superModel->getNodeRelativeTransform( index ) );
            indices[index] = node;
            if (bone.getParentIndex() > 0)
                state->nodes[node].parent = static_cast<int>( indices[bone.getParentIndex()] );
            part.bones.push_back( std::make_pair( bone.getIdentifier(), node ) );
        }
        state->parts.push_back( part );
    }
    if (!haveRoot)
    {
        PyErr_SetString( PyExc_ValueError, "Compound has no root part" );
        return NULL;
    }
    // Aliases are resolved after model trees have been loaded, matching the
    // second pass in #1513 0x008e2620. Hull aliases must retain the V node.
    for (size_t index = 0; index < aliases.size(); ++index)
    {
        std::map<BW::string, size_t>::const_iterator source = state->names.find( aliases[index].first );
        if (source == state->names.end())
        {
            PyErr_Format( PyExc_ValueError, "Compound alias refers to missing node %s",
                aliases[index].first.c_str() );
            return NULL;
        }
        state->names[aliases[index].second] = source->second;
    }
    for (size_t index = 0; index < state->nodes.size(); ++index)
    {
        CompoundNode & node = state->nodes[index];
        if (node.parent != -2)
            continue;
        std::map<BW::string, size_t>::const_iterator parent = state->names.find( node.parentName );
        if (node.parentName.empty())
            node.parent = 0;
        else if (parent != state->names.end())
            node.parent = static_cast<int>( parent->second );
        else
        {
            PyErr_Format( PyExc_ValueError, "Compound refers to missing parent %s",
                node.parentName.c_str() );
            return NULL;
        }
    }
    PyMatrix * root = new PyMatrix();
    root->set( state->nodes[0].local );
    state->matrix = MatrixProviderPtr( root, true );
    for (size_t index = 0; index < state->parts.size(); ++index)
    {
        CompoundPart & part = state->parts[index];
        std::map<BW::string, size_t>::const_iterator mount = state->names.find( part.mountName );
        if (!part.mountName.empty() && mount == state->names.end())
        {
            PyErr_Format( PyExc_ValueError, "Compound part refers to missing mount %s",
                part.mountName.c_str() );
            return NULL;
        }
        part.mount = part.mountName.empty() ? 0 : mount->second;
        PyObjectPtr fashion( new PyCompoundPose( new CompoundPose( state, index ) ),
            PyObjectPtr::STEAL_REFERENCE );
        if (PyObject_SetAttrString( part.model.get(), "_stockCompoundPose", fashion.get() ) < 0)
            return NULL;
    }
    if (!state->evaluate())
        return NULL;
    return new PyCompoundModel( state );
}
}

class PyCompoundAssembler : public PyObjectPlus
{
    Py_Header( PyCompoundAssembler, PyObjectPlus )
public:
    PyCompoundAssembler() : PyObjectPlus( &s_type_ ), spaceID_( 0 ) {}

    void addRootPart( const BW::string & model, const BW::string & name,
        const Matrix & local, const BW::string & prefix )
    {
        this->append( ROOT_PART, model, name, "", local, prefix );
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, addRootPart,
        ARG( BW::string, OPTARG( BW::string, "",
        OPTARG( Matrix, Matrix::identity, OPTARG( BW::string, "", END ) ) ) ) )

    void addPart( const BW::string & model, const BW::string & parent,
        const BW::string & name, const Matrix & local, const BW::string & prefix )
    {
        this->append( ADD_PART, model, name, parent, local, prefix );
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, addPart,
        ARG( BW::string, OPTARG( BW::string, "", OPTARG( BW::string, "",
        OPTARG( Matrix, Matrix::identity, OPTARG( BW::string, "", END ) ) ) ) ) )

    void emplacePart( const BW::string & model, const BW::string & parent,
        const BW::string & alias, const BW::string & prefix )
    {
        this->append( EMPLACE_PART, model, alias, parent, Matrix::identity, prefix );
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, emplacePart,
        ARG( BW::string, OPTARG( BW::string, "", OPTARG( BW::string, "",
        OPTARG( BW::string, "", END ) ) ) ) )

    void addNode( const BW::string & name, const BW::string & parent, const Matrix & local )
    {
        this->append( ADD_NODE, "", name, parent, local, "" );
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, addNode,
        ARG( BW::string, OPTARG( BW::string, "", OPTARG( Matrix, Matrix::identity, END ) ) ) )

    void addNodeAlias( const BW::string & source, const BW::string & alias )
    {
        this->append( NODE_ALIAS, "", alias, source, Matrix::identity, "" );
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, addNodeAlias, ARG( BW::string, ARG( BW::string, END ) ) )

    PY_RW_ATTRIBUTE_DECLARE( assemblerName_, assemblerName )
    PY_RW_ATTRIBUTE_DECLARE( spaceID_, spaceID )

    const std::vector<AssemblyOperation> & operations() const { return operations_; }
    const BW::string & resourceID() const { return assemblerName_; }
    PyObject * product( const std::vector<AssemblyOperation> & operations )
    {
        // #1513 0x008d8630 caches the compound product on its assembler.
        if (!product_)
            product_ = PyObjectPtr( buildCompound( operations, spaceID_ ), PyObjectPtr::STEAL_REFERENCE );
        if (!product_)
            return NULL;
        Py_INCREF( product_.get() );
        return product_.get();
    }
    static PyCompoundAssembler * New() { return new PyCompoundAssembler(); }
    PY_AUTO_FACTORY_DECLARE( PyCompoundAssembler, END )

private:
    void append( AssemblyOperationKind kind, const BW::string & model,
        const BW::string & name, const BW::string & parent, const Matrix & local,
        const BW::string & prefix )
    {
        AssemblyOperation operation;
        operation.kind = kind;
        operation.model = model;
        operation.name = name;
        operation.parent = parent;
        operation.local = local;
        operation.prefix = prefix;
        operations_.push_back( operation );
    }
    std::vector<AssemblyOperation> operations_;
    BW::string assemblerName_;
    uint32 spaceID_;
    PyObjectPtr product_;
};
PY_TYPEOBJECT( PyCompoundAssembler )
PY_BEGIN_METHODS( PyCompoundAssembler )
    PY_METHOD( addRootPart )
    PY_METHOD( addPart )
    PY_METHOD( emplacePart )
    PY_METHOD( addNode )
    PY_METHOD( addNodeAlias )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( PyCompoundAssembler )
    PY_ATTRIBUTE( assemblerName )
    PY_ATTRIBUTE( spaceID )
PY_END_ATTRIBUTES()
PY_FACTORY_NAMED( PyCompoundAssembler, "CompoundAssembler", BigWorld )

namespace
{
void compoundModRef( void * data, int delta )
{
    PyObject * object = static_cast<PyObject *>( data );
    if (delta > 0)
        Py_INCREF( object );
    else
        Py_DECREF( object );
}

PyObject * compoundPyInstance( ResourceRef & resource )
{
    PyObject * object = static_cast<PyObject *>( resource.data() );
    Py_INCREF( object );
    return object;
}

struct CompoundLoadRequest
{
    BW::string id;
    PyObjectPtr assembler;
    std::vector<AssemblyOperation> operations;
    ResourceRefs retainedModels;
    bool loaded;
};

class StockResourceRefs : public PyResourceRefs
{
public:
    StockResourceRefs() : PyResourceRefs( ResourceRefs() ) {}
    const ResourceRefs & loadedResources() const { return resourceRefs_; }
    const BW::vector<BW::string> & failedResources() const { return pFailedIDs_; }
};

class CompoundResourceLoader : public BackgroundTask
{
public:
    CompoundResourceLoader( const std::vector<CompoundLoadRequest> & requests,
        PyObject * callback, int priority ) :
        BackgroundTask( "StockCompoundResourceLoader" ), requests_( requests ),
        callback_( callback ), result_( new StockResourceRefs(),
            PyObjectPtr::STEAL_REFERENCE )
    {
        FileIOTaskManager::instance().addBackgroundTask( this, priority );
    }

    virtual void doBackgroundTask( TaskManager & manager )
    {
        // Only resource objects are touched in this thread. Python model
        // wrappers, node providers and the callback are created on the main
        // thread, as in the public engine's ResourceRefs loader.
        for (size_t index = 0; index < requests_.size(); ++index)
        {
            CompoundLoadRequest & request = requests_[index];
            request.loaded = true;
            if (!request.assembler)
            {
                ResourceRef resource = ResourceRef::getOrLoad( request.id );
                request.loaded = static_cast<bool>( resource );
                request.retainedModels.push_back( resource );
            }
            else
            {
                for (size_t operation = 0; operation < request.operations.size(); ++operation)
                {
                    const BW::string & model = request.operations[operation].model;
                    if (model.empty())
                        continue;
                    ResourceRef resource = ResourceRef::getOrLoad( model );
                    if (!resource)
                        request.loaded = false;
                    request.retainedModels.push_back( resource );
                }
            }
        }
        manager.addMainThreadTask( this );
    }

    virtual void doMainThreadTask( TaskManager & /* manager */ )
    {
        PyResourceRefs * result = static_cast<PyResourceRefs *>( result_.get() );
        for (size_t index = 0; index < requests_.size(); ++index)
        {
            CompoundLoadRequest & request = requests_[index];
            if (!request.loaded)
            {
                result->addFailedResourceID( request.id );
                continue;
            }
            if (!request.assembler)
            {
                result->addLoadedResourceRef( request.retainedModels[0] );
                continue;
            }
            PyCompoundAssembler * assembler =
                static_cast<PyCompoundAssembler *>( request.assembler.get() );
            PyObjectPtr product( assembler->product( request.operations ),
                PyObjectPtr::STEAL_REFERENCE );
            if (!product)
            {
                PyErr_Print();
                result->addFailedResourceID( request.id );
                continue;
            }
            ResourceRef resource( product.get(), compoundModRef, request.id, compoundPyInstance );
            result->addLoadedResourceRef( resource );
        }
        PyObjectPtr returned( PyObject_CallFunctionObjArgs( callback_.get(), result_.get(), NULL ),
            PyObjectPtr::STEAL_REFERENCE );
        if (!returned)
            PyErr_Print();
    }

private:
    std::vector<CompoundLoadRequest> requests_;
    PyObjectPtr callback_;
    PyObjectPtr result_;
};
}

static bool isStockVehicleResource( ScriptObject object )
{
    return PyCompoundAssembler::Check( object );
}
PY_AUTO_MODULE_FUNCTION( RETDATA, isStockVehicleResource, ARG( ScriptObject, END ), BigWorld )

PyObject * py_loadStockVehicleResourcesBG( PyObject * args )
{
    PyObject * resources;
    PyObject * callback;
    int priority = BgTaskManager::DEFAULT;
    if (!PyArg_ParseTuple( args, "OO|i:loadResourceListBG", &resources, &callback, &priority ))
        return NULL;
    if (!PyCallable_Check( callback ))
    {
        PyErr_SetString( PyExc_TypeError, "loadResourceListBG requires a callable callback" );
        return NULL;
    }
    PyObjectPtr sequence( PySequence_Fast( resources,
        "loadResourceListBG requires a resource sequence" ), PyObjectPtr::STEAL_REFERENCE );
    if (!sequence)
        return NULL;
    std::vector<CompoundLoadRequest> requests;
    const Py_ssize_t count = PySequence_Fast_GET_SIZE( sequence.get() );
    for (Py_ssize_t index = 0; index < count; ++index)
    {
        PyObject * object = PySequence_Fast_GET_ITEM( sequence.get(), index );
        CompoundLoadRequest request;
        request.loaded = false;
        if (PyCompoundAssembler::Check( object ))
        {
            PyCompoundAssembler * assembler = static_cast<PyCompoundAssembler *>( object );
            request.assembler = object;
            request.id = assembler->resourceID();
            request.operations = assembler->operations();
        }
        else if (PyString_Check( object ) && PyString_GET_SIZE( object ) > 0)
        {
            if (Script::setData( object, request.id ) != 0)
                return NULL;
        }
        else
        {
            // #1513 00a14090 silently skips None, empty strings and other
            // objects. The Hangar appends an absent segment2 model as None.
            continue;
        }
        requests.push_back( request );
    }
    new CompoundResourceLoader( requests, callback, priority );
    Py_RETURN_NONE;
}
PY_MODULE_FUNCTION( loadStockVehicleResourcesBG, BigWorld )

class StockPrerequisitesRequest : public PyObjectPlus
{
    Py_Header( StockPrerequisitesRequest, PyObjectPlus )
public:
    StockPrerequisitesRequest() : PyObjectPlus( &s_type_ ), cancelled_( false ), ready_( false ) {}
    PY_METHOD_DECLARE( pyCall )
    void cancel() { cancelled_ = true; result_ = NULL; failures_.clear(); }
    void fail( PyObject * resources )
    {
        const Py_ssize_t count = PySequence_Size( resources );
        for (Py_ssize_t index = 0; index < count; ++index)
        {
            PyObjectPtr item( PySequence_GetItem( resources, index ), PyObjectPtr::STEAL_REFERENCE );
            BW::string id;
            if (item && PyCompoundAssembler::Check( item.get() ))
                id = static_cast<PyCompoundAssembler *>( item.get() )->resourceID();
            else if (item && PyString_Check( item.get() ))
                id = PyString_AsString( item.get() );
            else
            {
                PyErr_Clear();
                continue;
            }
            failures_.push_back( ResourceRef( NULL, NULL, id, NULL ) );
        }
        ready_ = true;
    }
    bool collect( ResourceRefs & resources ) const
    {
        if (!ready_)
            return false;
        if (!result_)
        {
            resources = failures_;
            return true;
        }
        const StockResourceRefs * result = static_cast<StockResourceRefs *>( result_.get() );
        resources = result->loadedResources();
        const BW::vector<BW::string> & failures = result->failedResources();
        for (size_t index = 0; index < failures.size(); ++index)
            resources.push_back( ResourceRef( NULL, NULL, failures[index], NULL ) );
        return true;
    }
private:
    bool cancelled_;
    bool ready_;
    PyObjectPtr result_;
    ResourceRefs failures_;
};

PyObject * StockPrerequisitesRequest::pyCall( PyObject * args )
{
    PyObject * result;
    if (!PyArg_ParseTuple( args, "O:prerequisites", &result ))
        return NULL;
    if (!PyResourceRefs::Check( result ))
    {
        PyErr_SetString( PyExc_TypeError, "Expected native prerequisite resource refs" );
        return NULL;
    }
    if (!cancelled_ && !ready_)
    {
        result_ = result;
        ready_ = true;
    }
    Py_RETURN_NONE;
}
PY_TYPEOBJECT_WITH_CALL( StockPrerequisitesRequest )
PY_BEGIN_METHODS( StockPrerequisitesRequest )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( StockPrerequisitesRequest )
PY_END_ATTRIBUTES()

bool beginStockVehiclePrerequisites( PyObject * resources, PyObjectPtr & request )
{
    if (!resources || !PySequence_Check( resources ))
        return false;
    const Py_ssize_t count = PySequence_Size( resources );
    bool containsAssembler = false;
    for (Py_ssize_t index = 0; index < count; ++index)
    {
        PyObjectPtr value( PySequence_GetItem( resources, index ), PyObjectPtr::STEAL_REFERENCE );
        if (!value)
            return false;
        if (PyCompoundAssembler::Check( value.get() ))
            containsAssembler = true;
    }
    if (!containsAssembler)
        return false;
    // Publish the request before scheduling: the callback owns only this
    // request and never captures an Entity or an unguarded native pointer.
    request = PyObjectPtr( new StockPrerequisitesRequest(), PyObjectPtr::STEAL_REFERENCE );
    PyObjectPtr args( Py_BuildValue( "(OO)", resources, request.get() ),
        PyObjectPtr::STEAL_REFERENCE );
    if (!args)
    {
        PyErr_Print();
        static_cast<StockPrerequisitesRequest *>( request.get() )->fail( resources );
        return true;
    }
    PyObjectPtr result( py_loadStockVehicleResourcesBG( args.get() ), PyObjectPtr::STEAL_REFERENCE );
    if (!result)
    {
        // An identified assembler request must not fall through the public
        // string-only path and become a successful empty prerequisite list.
        PyErr_Print();
        static_cast<StockPrerequisitesRequest *>( request.get() )->fail( resources );
        return true;
    }
    return true;
}

bool pollStockVehiclePrerequisites( PyObject * request, ResourceRefs & resources )
{
    return static_cast<StockPrerequisitesRequest *>( request )->collect( resources );
}

void cancelStockVehiclePrerequisites( PyObject * request )
{
    if (request)
        static_cast<StockPrerequisitesRequest *>( request )->cancel();
}

bool stockVehicleModelBounds( PyObject * model, BoundingBox & bounds )
{
    if (PyCompoundModel::Check( model ))
        static_cast<PyCompoundModel *>( model )->localBoundingBox( bounds );
    else if (PyModel::Check( model ))
        static_cast<PyModel *>( model )->localBoundingBox( bounds );
    else
        return false;
    return true;
}

bool stockVehicleCompoundContext( PyObject * compound, uint32 & spaceID,
    BoundingBox & worldBounds )
{
    return compound && PyCompoundModel::Check( compound ) &&
        static_cast<PyCompoundModel *>( compound )->context( spaceID, worldBounds );
}

bool stockVehicleCompoundNode( PyObject * compound, const BW::string & name,
    Matrix & authoredLocal, Matrix & parentWorld, Matrix & world )
{
    return compound && PyCompoundModel::Check( compound ) &&
        static_cast<PyCompoundModel *>( compound )->readNode( name, authoredLocal, parentWorld, world );
}

bool stockVehicleCompoundNodeLocal( PyObject * compound, const BW::string & name, Matrix & local )
{
    return compound && PyCompoundModel::Check( compound ) &&
        static_cast<PyCompoundModel *>( compound )->readNodeLocal( name, local );
}

bool stockVehicleCompoundNodeIdentity( PyObject * compound, const BW::string & name,
    int & index, int & parent )
{
    return compound && PyCompoundModel::Check( compound ) &&
        static_cast<PyCompoundModel *>( compound )->readNodeIdentity( name, index, parent );
}

bool writeStockVehicleCompoundNodeLocal( PyObject * compound,
    const BW::string & name, const Matrix & local )
{
    return compound && PyCompoundModel::Check( compound ) &&
        static_cast<PyCompoundModel *>( compound )->writeNodeLocal( name, local );
}

bool initialiseStockVehicleAppearanceResources()
{
    if (compoundRenderingResources && compoundRenderingResources->open) return true;
    const BW::string & textureName = compoundStippleMap.value();
    if (textureName.empty())
    {
        ERROR_MSG( "CompoundModel: resources.xml system/stippleMap is not configured\n" );
        return false;
    }
    auto resources = std::make_shared<CompoundRenderingResources>();
    resources->texture = Moo::TextureManager::instance()->get(
        textureName, true, true, true, "texture/compound stipple" );
    if (!resources->texture || !resources->texture->pTexture())
    {
        ERROR_MSG( "CompoundModel: cannot load stipple texture %s\n", textureName.c_str() );
        return false;
    }
    // ManagedEffect classifies per-object semantics when an effect is loaded.
    // Register the genuine producer before constructing either override effect.
    resources->constant = new CompoundStippleConstant( resources->texture );
    Moo::rc().effectVisualContext().registerAutoConstant(
        Moo::EffectVisualContext::AUTO_CONSTANT_TYPE_PER_OBJECT, "StippleMap", resources->constant );
    *Moo::rc().effectVisualContext().getMapping( "StippleMap" ) = resources->constant;
    resources->registered = true;
    resources->stipple = std::make_shared<Moo::DrawContext::OverrideBlock>(
        BW::StringRef( "shaders/std_effects/stipple/stipple" ), false );
    for (unsigned index = 0; index < 2; ++index)
    {
        Moo::ComplexEffectMaterial * material = resources->stipple->stockMaterial( index != 0 );
        if (!material || !material->baseMaterial() || !material->baseMaterial()->pEffect())
        {
            ERROR_MSG( "CompoundModel: cannot load original %s stipple effect\n",
                index ? "skinned" : "rigid" );
            resources->close();
            return false;
        }
    }
    resources->open = true;
    compoundRenderingResources = resources;
    return true;
}

void finiStockVehicleAppearanceResources()
{
    auto resources = compoundRenderingResources;
    compoundRenderingResources.reset();
    if (resources) resources->close();
}

BW_END_NAMESPACE
