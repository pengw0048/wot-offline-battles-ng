#include "pch.hpp"
#include "pyscript/pyobject_plus.hpp"
#include "pyscript/script.hpp"

BW_BEGIN_NAMESPACE

/**
 *	#1513 BigWorld.Screener (factory 00fb1400): a PyWatermarkersWrapper over the
 *	inert EmptyWatermarker. Waiting.show/close call setEnabled; nothing reads it.
 */
class PyWatermarkersWrapper : public PyObjectPlus
{
	Py_Header( PyWatermarkersWrapper, PyObjectPlus )

public:
	PyWatermarkersWrapper() : PyObjectPlus( &s_type_ ) {}

	// 00fb1150 stores the flag in EmptyWatermarker+0x10, which no code reads.
	void setEnabled( bool ) {}
	// 00fb12f0 converts the ID and discards it.
	void setUserId( long long ) {}
	// 00fb11d0 calls the empty EmptyWatermarker slot 0.
	void setSettings( const BW::string &, float ) {}

	PY_AUTO_METHOD_DECLARE( RETVOID, setEnabled, ARG( bool, END ) )
	PY_AUTO_METHOD_DECLARE( RETVOID, setUserId, ARG( long long, END ) )
	PY_AUTO_METHOD_DECLARE( RETVOID, setSettings,
		ARG( BW::string, ARG( float, END ) ) )
};

PY_TYPEOBJECT( PyWatermarkersWrapper )

PY_BEGIN_METHODS( PyWatermarkersWrapper )
	PY_METHOD( setUserId )
	PY_METHOD( setSettings )
	PY_METHOD( setEnabled )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( PyWatermarkersWrapper )
PY_END_ATTRIBUTES()

PY_MODULE_ATTRIBUTE( BigWorld, Screener, new PyWatermarkersWrapper() )

BW_END_NAMESPACE
