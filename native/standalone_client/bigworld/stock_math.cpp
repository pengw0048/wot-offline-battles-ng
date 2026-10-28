#ifndef WOT_STOCK_MATH_PORTABLE
#include "pch.hpp"
#include "stock_math.hpp"
#include "pyscript/script.hpp"
#include "pyscript/script_math.hpp"
#endif

BW_BEGIN_NAMESPACE
int StockMath_token = 1;

#ifndef WOT_STOCK_MATH_PORTABLE
static PyObject * stockVector4BasicNew( PyTypeObject * /*type*/,
    PyObject * /*args*/, PyObject * /*keywords*/ )
{
    // #1513 00694950 exposes the existing constant native provider. Its factory
    // ignores all arguments; every call starts with four zeros.
    return new Vector4Basic();
}

class StockVector4BasicFactory : public PyFactoryMethodLink
{
public:
    StockVector4BasicFactory() :
        PyFactoryMethodLink( "Math", "Vector4Basic", &Vector4Basic::s_type_ ) {}
    void init() override
    {
        // 005a0097 installs tp_new; 005a1710 publishes the native type itself.
        Vector4Basic::s_type_.tp_new = stockVector4BasicNew;
        PyFactoryMethodLink::init();
    }
};
static StockVector4BasicFactory stockVector4BasicFactory;
#endif

Matrix stockSlerp( const Matrix & source, const Matrix & target, float amount )
{
    // #1513 006dea60 reconstructs rotation from the providers' Euler angles.
    // 006deea0 matches the public Matrix yaw/pitch/roll axis normalization.
    // This deliberately returns a pure rotation, without translation or scale.
    D3DXQUATERNION first, last, blended;
    D3DXQuaternionRotationYawPitchRoll( &first, source.yaw(), source.pitch(), source.roll() );
    D3DXQuaternionRotationYawPitchRoll( &last, target.yaw(), target.pitch(), target.roll() );
    D3DXQuaternionSlerp( &blended, &first, &last, amount );
    Matrix result;
    D3DXMatrixRotationQuaternion( &result, &blended );
    return result;
}

static PyObject * py_slerp( PyObject * args )
{
    PyObject * first;
    PyObject * last;
    PyObject * coefficient;
    if (!PyArg_ParseTuple( args, "OOO:slerp", &first, &last, &coefficient )) return NULL;
    Matrix source, target;
    float amount;
    // 00690130 samples MatrixProvider subclasses once. The result owns a new
    // Matrix snapshot rather than retaining either provider or mutating it.
    if (Script::setData( first, source, "() argument 1" ) ||
        Script::setData( last, target, "() argument 2" ) ||
        Script::setData( coefficient, amount, "() argument 3" )) return NULL;
    return Script::getData( stockSlerp( source, target, amount ) );
}
PY_MODULE_FUNCTION( slerp, Math )
BW_END_NAMESPACE
