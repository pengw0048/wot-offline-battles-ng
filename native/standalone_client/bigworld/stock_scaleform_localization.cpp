#include "pch.hpp"
#include "stock_scaleform_localization.hpp"
#include "stock_numeric_format.hpp"
#include "scaleform/fx_game_delegate.hpp"
#include "pyscript/script.hpp"
#include "script/init_time_job.hpp"
#include "cstdmf/string_utils.hpp"

BW_BEGIN_NAMESPACE
namespace ScaleformBW
{
namespace
{
// helpers.i18n.makeString (#1513 WGGFxStringTranslator+0x10) and
// makeStringJSON (01d59494), imported on first use.
PyObject * s_makeString = NULL;
PyObject * s_makeStringJSON = NULL;
bool s_closing = false;

// Drops the cached helpers while Python is still alive.
class LocalizationFini : public Script::FiniTimeJob
{
public:
    void fini()
    {
        // Later fini jobs may still update Flash while Script is not finalised.
        // Close before DECREF, which can also re-enter Python.
        s_closing = true;
        Py_CLEAR( s_makeString );
        Py_CLEAR( s_makeStringJSON );
    }
};
LocalizationFini s_localizationFini;

bool pythonAvailable()
{
    return !s_closing && Py_IsInitialized() && !Script::isFinalised();
}

// 006f5286/00ab38de. A failed import or lookup is reported and retried on the
// next call; #1513 dereferences the NULL helper instead.
PyObject * helper( PyObject *& cache, const char * name )
{
    if (cache == NULL)
    {
        PyObject * module = PyImport_ImportModule( "helpers.i18n" );
        if (module != NULL)
        {
            cache = PyObject_GetAttrString( module, name );
            Py_DECREF( module );
        }
        if (cache == NULL)
            PyErr_Print();
    }
    return cache;
}

// Script::ask consumes the helper reference and the arguments and reports a
// Python exception; a NULL argument from a failed conversion is reported here.
PyObject * call( PyObject * function, PyObject * args )
{
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE( args ); ++i)
    {
        if (PyTuple_GET_ITEM( args, i ) == NULL)
        {
            Py_DECREF( args );
            PyErr_Print();
            return NULL;
        }
    }
    Py_INCREF( function );
    return Script::ask( function, args, "", false, true );
}

/**
 *	#1513 WGGFxStringTranslator (vtable 014bf270): every automatic text
 *	translation goes through helpers.i18n.makeString; only a str result
 *	replaces the text, always as HTML.
 */
class StockStringTranslator : public GFx::Translator
{
public:
    // 00621aec: WWT_Default.
    StockStringTranslator() : GFx::Translator( 0 ) {}

    // 009c0050.
    unsigned GetCaps() const { return Cap_ReceiveHtml; }

    // 006f5250.
    void Translate( TranslateInfo * pInfo )
    {
        if (!pythonAvailable())
            return;
        PyObject * function = helper( s_makeString, "makeString" );
        if (function == NULL)
            return;
        const wchar_t * key = pInfo->GetKey();
        PyObject * args = PyTuple_New( 1 );
        PyTuple_SET_ITEM( args, 0, Script::getData( BW::wstring( key ? key : L"" ) ) );
        PyObject * result = call( function, args );
        if (result == NULL)
            return;
        if (PyString_Check( result ))
            pInfo->SetResultHtml( PyString_AsString( result ) );
        Py_DECREF( result );
    }
};

/**
 *	#1513 WGExtensions (vtable 0150c004). LocaleBase.envokeC converts the
 *	ExternalInterface result to String, so an unregistered callback displays
 *	"null" even when the supplied experience/damage/currency value is valid.
 */
class StockLocalizationHandler : public FxDelegateHandler
{
public:
    void Accept( CallbackProcessor * pProcessor )
    {
        pProcessor->Process( "WG.getGoldFormat", &getNumericFormat<wot::stock_numeric_format::GOLD> );
        pProcessor->Process( "WG.getIntegralFormat", &getNumericFormat<wot::stock_numeric_format::INTEGRAL> );
        pProcessor->Process( "WG.getFractionalFormat", &getNumericFormat<wot::stock_numeric_format::FRACTIONAL> );
        pProcessor->Process( "WG.getNiceNumberFormat", &getNumericFormat<wot::stock_numeric_format::NICE_NUMBER> );
        pProcessor->Process( "WG.getShortTimeFormat", &getNumericFormat<wot::stock_numeric_format::SHORT_TIME> );
        pProcessor->Process( "WG.getLongTimeFormat", &getNumericFormat<wot::stock_numeric_format::LONG_TIME> );
        pProcessor->Process( "WG.getShortDateFormat", &getNumericFormat<wot::stock_numeric_format::SHORT_DATE> );
        pProcessor->Process( "WG.getLongDateFormat", &getNumericFormat<wot::stock_numeric_format::LONG_DATE> );
        pProcessor->Process( "WG.getYMDateFormat", &getNumericFormat<wot::stock_numeric_format::YEAR_MONTH> );
        pProcessor->Process( "WG.getFormattedLocalizedString", &getFormattedLocalizedString );
    }

    template <wot::stock_numeric_format::Format Kind>
    static void getNumericFormat( const FxDelegateArgs & params )
    {
        // The original directly reads the first Number. Contain malformed
        // calls without inventing a number or handing a WG name to DAAPI.
        if (params.GetArgCount() < 1 || !params[0].IsNumber())
        {
            ERROR_MSG( "WG numeric format: expected a Number argument\n" );
            return;
        }
        if (!pythonAvailable())
            return;
        std::wstring result;
        if (!wot::stock_numeric_format::format( Kind, params[0].GetNumber(), result ))
        {
            PyErr_Print();
            return;
        }
        params.GetMovie()->SetExternalInterfaceRetVal( GFx::Value( result.c_str() ) );
    }

    // 00ab37c0: (key, argsJSON) -> makeStringJSON; a str result is returned
    // as a wide string, anything else leaves the return value unset.
    static void getFormattedLocalizedString( const FxDelegateArgs & params )
    {
        // #1513 reads the second argument unconditionally.
        if (params.GetArgCount() < 2)
        {
            ERROR_MSG( "WG.getFormattedLocalizedString: expected a key and JSON arguments\n" );
            return;
        }
        const Scaleform::String key = params[0].ToString();
        const Scaleform::String json = params[1].ToString();
        if (!pythonAvailable())
            return;
        PyObject * function = helper( s_makeStringJSON, "makeStringJSON" );
        if (function == NULL)
            return;
        PyObject * args = PyTuple_New( 2 );
        PyTuple_SET_ITEM( args, 0, Script::getData( BW::string( key.ToCStr() ) ) );
        PyTuple_SET_ITEM( args, 1, Script::getData( BW::string( json.ToCStr() ) ) );
        PyObject * result = call( function, args );
        if (result == NULL)
            return;
        if (PyString_Check( result ))
        {
            BW::wstring text;
            bw_utf8tow( PyString_AsString( result ), text );
            params.GetMovie()->SetExternalInterfaceRetVal( GFx::Value( text.c_str() ) );
        }
        Py_DECREF( result );
    }
};
}

void installStockTranslator( GFx::Loader & loader )
{
    Ptr<GFx::Translator> translator = *new StockStringTranslator();
    loader.SetTranslator( translator );
}

void registerStockLocalization( FxDelegate & delegate )
{
    Ptr<FxDelegateHandler> handler = *new StockLocalizationHandler();
    delegate.RegisterHandler( handler );
}
} // namespace ScaleformBW
BW_END_NAMESPACE
