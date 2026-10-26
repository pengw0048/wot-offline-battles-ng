#include "pch.hpp"
#include "stock_math_providers.hpp"
#include "stock_replay_controller.hpp"
#include "moo/render_context.hpp"
#include "pyscript/script.hpp"
#include <cmath>

BW_BEGIN_NAMESPACE

class WGYawOnlyTransform : public MatrixProvider
{
    Py_Header( WGYawOnlyTransform, MatrixProvider )
public:
    WGYawOnlyTransform() : MatrixProvider( false, &s_type_ ) {}
    void matrix( Matrix & result ) const override
    {
        const MatrixProviderPtr source = source_;
        if (!source) { result = Matrix::identity; return; }
        Matrix sampled;
        sampled.setZero();
        source->matrix( sampled );
        Vector3 forward( sampled( 2, 0 ), sampled( 2, 1 ), sampled( 2, 2 ) );
        D3DXVec3Normalize( &forward, &forward );
        // 6dd860 normalises the third row before its x87 atan2(x,z).
        const float yaw = static_cast<float>( std::atan2(
            static_cast<double>( forward.x ), static_cast<double>( forward.z ) ) );
        D3DXMatrixRotationY( &result, yaw );
    }
    PY_RW_ATTRIBUTE_DECLARE( source_, source )
    static WGYawOnlyTransform * New() { return new WGYawOnlyTransform(); }
    PY_AUTO_FACTORY_DECLARE( WGYawOnlyTransform, END )
private:
    MatrixProviderPtr source_;
};
PY_TYPEOBJECT( WGYawOnlyTransform )
PY_BEGIN_METHODS( WGYawOnlyTransform )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( WGYawOnlyTransform )
    PY_ATTRIBUTE( source )
PY_END_ATTRIBUTES()
PY_FACTORY( WGYawOnlyTransform, Math )

class WGInverseTransform : public MatrixProvider
{
    Py_Header( WGInverseTransform, MatrixProvider )
public:
    WGInverseTransform() : MatrixProvider( false, &s_type_ ) {}
    void matrix( Matrix & result ) const override
    {
        const MatrixProviderPtr source = source_;
        if (!source) { result = Matrix::identity; return; }
        source->matrix( result );
        // 6912c0 leaves the sampled matrix intact when D3DX cannot invert it.
        D3DXMatrixInverse( &result, NULL, &result );
    }
    PY_RW_ATTRIBUTE_DECLARE( source_, source )
    static WGInverseTransform * New() { return new WGInverseTransform(); }
    PY_AUTO_FACTORY_DECLARE( WGInverseTransform, END )
private:
    MatrixProviderPtr source_;
};
PY_TYPEOBJECT( WGInverseTransform )
PY_BEGIN_METHODS( WGInverseTransform )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( WGInverseTransform )
    PY_ATTRIBUTE( source )
PY_END_ATTRIBUTES()
PY_FACTORY( WGInverseTransform, Math )

class WGBoundMatrixProvider : public MatrixProvider
{
    Py_Header( WGBoundMatrixProvider, MatrixProvider )
public:
    WGBoundMatrixProvider() : MatrixProvider( false, &s_type_ ),
        minBound_( 0.f, 0.f, 0.f ), maxBound_( 0.f, 0.f, 0.f ) {}
    void matrix( Matrix & result ) const override
    {
        const MatrixProviderPtr source = source_;
        if (!source) { result = Matrix::identity; return; }
        source->matrix( result );
        for (unsigned axis = 0; axis != 3; ++axis)
        {
            const float value = result( 3, axis );
            // Preserve 6df150's ordered comparisons, including NaNs and
            // reversed bounds, rather than assuming a sorted finite interval.
            if (value < minBound_[axis]) result( 3, axis ) = minBound_[axis];
            else if (maxBound_[axis] < value) result( 3, axis ) = maxBound_[axis];
        }
        result( 3, 3 ) = 1.f;
    }
    PY_RW_ATTRIBUTE_DECLARE( source_, source )
    PY_RW_ATTRIBUTE_DECLARE( minBound_, minBound )
    PY_RW_ATTRIBUTE_DECLARE( maxBound_, maxBound )
    static WGBoundMatrixProvider * New() { return new WGBoundMatrixProvider(); }
    PY_AUTO_FACTORY_DECLARE( WGBoundMatrixProvider, END )
private:
    MatrixProviderPtr source_;
    Vector3 minBound_, maxBound_;
};
PY_TYPEOBJECT( WGBoundMatrixProvider )
PY_BEGIN_METHODS( WGBoundMatrixProvider )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( WGBoundMatrixProvider )
    PY_ATTRIBUTE( source )
    PY_ATTRIBUTE( minBound )
    PY_ATTRIBUTE( maxBound )
PY_END_ATTRIBUTES()
PY_FACTORY( WGBoundMatrixProvider, Math )

class WGCombinedMP : public MatrixProvider
{
    Py_Header( WGCombinedMP, MatrixProvider )
public:
    WGCombinedMP() : MatrixProvider( false, &s_type_ ) {}
    void matrix( Matrix & result ) const override
    {
        // 6df230 requires both sources and samples rotation first.
        const MatrixProviderPtr rotation = rotationSrc_;
        const MatrixProviderPtr translationSource = translationSrc_;
        if (!rotation || !translationSource) { result = Matrix::identity; return; }
        Matrix translation;
        translation.setZero();
        rotation->matrix( result );
        translationSource->matrix( translation );
        for (unsigned axis = 0; axis != 3; ++axis)
            result( 3, axis ) = translation( 3, axis );
        result( 3, 3 ) = 1.f;
    }
    PY_RW_ATTRIBUTE_DECLARE( rotationSrc_, rotationSrc )
    PY_RW_ATTRIBUTE_DECLARE( translationSrc_, translationSrc )
    static WGCombinedMP * New() { return new WGCombinedMP(); }
    PY_AUTO_FACTORY_DECLARE( WGCombinedMP, END )
private:
    MatrixProviderPtr rotationSrc_, translationSrc_;
};
PY_TYPEOBJECT( WGCombinedMP )
PY_BEGIN_METHODS( WGCombinedMP )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( WGCombinedMP )
    PY_ATTRIBUTE( rotationSrc )
    PY_ATTRIBUTE( translationSrc )
PY_END_ATTRIBUTES()
PY_FACTORY( WGCombinedMP, Math )

class WGTranslationOnlyMP : public MatrixProvider
{
    Py_Header( WGTranslationOnlyMP, MatrixProvider )
public:
    WGTranslationOnlyMP() : MatrixProvider( false, &s_type_ ) {}
    void matrix( Matrix & result ) const override
    {
        const MatrixProviderPtr source = source_;
        if (!source) { result = Matrix::identity; return; }
        source->matrix( result );
        D3DXMatrixTranslation( &result, result( 3, 0 ), result( 3, 1 ), result( 3, 2 ) );
    }
    PY_RW_ATTRIBUTE_DECLARE( source_, source )
    static WGTranslationOnlyMP * New() { return new WGTranslationOnlyMP(); }
    PY_AUTO_FACTORY_DECLARE( WGTranslationOnlyMP, END )
private:
    MatrixProviderPtr source_;
};
PY_TYPEOBJECT( WGTranslationOnlyMP )
PY_BEGIN_METHODS( WGTranslationOnlyMP )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( WGTranslationOnlyMP )
    PY_ATTRIBUTE( source )
PY_END_ATTRIBUTES()
PY_FACTORY( WGTranslationOnlyMP, Math )

class WGStrategicAreaViewMP : public MatrixProvider
{
    Py_Header( WGStrategicAreaViewMP, MatrixProvider )
public:
    WGStrategicAreaViewMP() : MatrixProvider( false, &s_type_ ),
        baseScale_( 1.f, 1.f ), baseHeight_( 100.f ) {}
    void matrix( Matrix & result ) const override
    {
        const MatrixProviderPtr source = source_;
        if (!source) { result = Matrix::identity; return; }
        source->matrix( result );
        const float height = result( 3, 1 );
        Matrix scale;
        D3DXMatrixScaling( &scale, baseScale_.x * height / baseHeight_, 1.f,
            baseScale_.y * height / (Moo::rc().camera().aspectRatio() * baseHeight_) );
        D3DXMatrixMultiply( &result, &scale, &result );
    }
    void setBaseHeight( float value ) { baseHeight_ = value; }
    PY_AUTO_METHOD_DECLARE( RETVOID, setBaseHeight, ARG( float, END ) )
    PY_RW_ATTRIBUTE_DECLARE( source_, source )
    PY_RW_ATTRIBUTE_DECLARE( baseScale_, baseScale )
    static WGStrategicAreaViewMP * New() { return new WGStrategicAreaViewMP(); }
    PY_AUTO_FACTORY_DECLARE( WGStrategicAreaViewMP, END )
private:
    MatrixProviderPtr source_;
    Vector2 baseScale_;
    float baseHeight_;
};
PY_TYPEOBJECT( WGStrategicAreaViewMP )
PY_BEGIN_METHODS( WGStrategicAreaViewMP )
    PY_METHOD( setBaseHeight )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( WGStrategicAreaViewMP )
    PY_ATTRIBUTE( source )
    PY_ATTRIBUTE( baseScale )
PY_END_ATTRIBUTES()
PY_FACTORY( WGStrategicAreaViewMP, Math )

Vector3 stockSmoothTranslation( const Vector3 & current, const Vector3 & target )
{
    const float dx = target.x - current.x;
    const float dy = target.y - current.y;
    const float dz = target.z - current.z;
    const float distance = std::sqrt( (dy * dy + dx * dx) + dz * dz );
    // COMISS/JBE also snaps an unordered distance to the sampled target.
    if (!(distance > 0.01f)) return target;
    const float candidate = 0.5f / distance;
    float weight = 0.03f;
    if (0.03f <= candidate)
        weight = candidate < 0.5f ? candidate : 0.5f;
    const float previous = 1.f - weight;
    return Vector3( current.x * previous + target.x * weight,
        target.y * weight + current.y * previous,
        target.z * weight + current.z * previous );
}

class WGSmoothTranslationOnlyMP : public MatrixProvider
{
    Py_Header( WGSmoothTranslationOnlyMP, MatrixProvider )
public:
    explicit WGSmoothTranslationOnlyMP( PyTypeObject * type = &s_type_ ) :
        MatrixProvider( true, type ), current_( 0.f, 0.f, 0.f ), target_( 0.f, 0.f, 0.f ) {}
    void matrix( Matrix & result ) const override
    {
        D3DXMatrixTranslation( &result, current_.x, current_.y, current_.z );
    }
    void tick( float /*dTime*/ ) override
    {
        const MatrixProviderPtr source = source_;
        if (!source) return;
        Matrix sampled;
        sampled.setZero();
        source->matrix( sampled );
        if (source_ != source) return;
        target_.set( sampled( 3, 0 ), sampled( 3, 1 ), sampled( 3, 2 ) );
        current_ = stockSmoothTranslation( current_, target_ );
    }
    MatrixProviderPtr source() const { return source_; }
    void source( MatrixProviderPtr value )
    {
        // A provider may synchronously replace this binding while sampled.
        // Retain both owners and only publish for the binding still selected.
        const MatrixProviderPtr previous = source_;
        source_ = value;
        if (!value) return;
        Matrix sampled;
        sampled.setZero();
        value->matrix( sampled );
        if (source_ != value) return;
        current_.set( sampled( 3, 0 ), sampled( 3, 1 ), sampled( 3, 2 ) );
    }
    PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( MatrixProviderPtr, source, source )
    static WGSmoothTranslationOnlyMP * New() { return new WGSmoothTranslationOnlyMP(); }
    PY_AUTO_FACTORY_DECLARE( WGSmoothTranslationOnlyMP, END )
private:
    MatrixProviderPtr source_;
    Vector3 current_, target_;
};
PY_TYPEOBJECT( WGSmoothTranslationOnlyMP )
PY_BEGIN_METHODS( WGSmoothTranslationOnlyMP )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( WGSmoothTranslationOnlyMP )
    PY_ATTRIBUTE( source )
PY_END_ATTRIBUTES()
PY_FACTORY( WGSmoothTranslationOnlyMP, Math )

class WGReplayAwaredSmoothTranslationOnlyMP : public WGSmoothTranslationOnlyMP
{
    Py_Header( WGReplayAwaredSmoothTranslationOnlyMP, WGSmoothTranslationOnlyMP )
public:
    WGReplayAwaredSmoothTranslationOnlyMP() : WGSmoothTranslationOnlyMP( &s_type_ ) {}
    void tick( float dTime ) override
    {
        if (!stockReplayPlaybackPaused()) WGSmoothTranslationOnlyMP::tick( dTime );
    }
    static WGReplayAwaredSmoothTranslationOnlyMP * New()
        { return new WGReplayAwaredSmoothTranslationOnlyMP(); }
    PY_AUTO_FACTORY_DECLARE( WGReplayAwaredSmoothTranslationOnlyMP, END )
};
PY_TYPEOBJECT( WGReplayAwaredSmoothTranslationOnlyMP )
PY_BEGIN_METHODS( WGReplayAwaredSmoothTranslationOnlyMP )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( WGReplayAwaredSmoothTranslationOnlyMP )
PY_END_ATTRIBUTES()
PY_FACTORY( WGReplayAwaredSmoothTranslationOnlyMP, Math )

BW_END_NAMESPACE
