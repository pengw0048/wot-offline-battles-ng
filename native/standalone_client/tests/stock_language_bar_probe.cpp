// Hidden real-SDK probe of the original gui/flash/LangBar.swf: without and with
// the stock LangBar owner, a real SDK Stage mouseDown and later Stage listener.
// Console only: no window, renderer, client, worker or server. Usage:
//   stock_language_bar_probe.exe LangBar.swf baseline|owner|focus-before-activation
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244 4100 4189 4458 4005)
#endif
#include <GFx.h>
#include <GFx/GFx_AS3Support.h>
#include <GFx/AMP/Amp_Server.h>
#include <GFx/GFx_PlayerImpl.h>
#include <GFx/AS3/AS3_MovieRoot.h>
#include <GFx/AS3/AS3_AvmDisplayObjContainer.h>
#include <GFx/AS3/Obj/AS3_Obj_Global.h>
#include <Kernel/HeapPT/HeapPT_SysAllocWinAPI.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "stock_language_bar.hpp"

using namespace Scaleform;
using namespace Scaleform::GFx;

namespace
{
int g_failures = 0;

void check( const char * name, bool passed, const std::string & detail )
{
    if (!passed)
        ++g_failures;
    std::printf( "CHECK %s %s %s\n", name, passed ? "PASS" : "FAIL", detail.c_str() );
    std::fflush( stdout );
}

// Forwards SDK output and counts the reported LangBar #1009.
class CountingLog : public Log
{
public:
    CountingLog() : nullReferences( 0 ) {}

    void LogMessageVarg( LogMessageId, const char * format, va_list arguments )
    {
        char text[2048];
        std::vsnprintf( text, sizeof( text ), format, arguments );
        if (std::strstr( text, "#1009" ) != NULL)
            ++nullReferences;
        std::fputs( text, stdout );
        std::fflush( stdout );
    }

    unsigned nullReferences;
};

// A native Stage listener registered after LangBar's own.
class Counter : public FunctionHandler
{
public:
    Counter() : calls( 0 ) {}
    void Call( const Params & ) { ++calls; }
    unsigned calls;
};

std::string utf8( const std::wstring & text )
{
    std::string result;
    return wot::stock_language_bar::utf8( text, result ) ? result : std::string( "<utf8 failure>" );
}

// The OS answer, read directly rather than through the owner.
std::string osLanguage( HKL layout )
{
    wchar_t name[260] = { 0 };
    if (GetLocaleInfoW( MAKELCID( LOWORD( reinterpret_cast<ULONG_PTR>( layout ) ), SORT_DEFAULT ),
            LOCALE_SLANGUAGE, name, 260 ) <= 0)
        return "<GetLocaleInfoW failed>";
    return utf8( name );
}

std::vector<std::string> osLanguages( std::vector<HKL> & layouts )
{
    std::vector<std::string> names;
    const int count = GetKeyboardLayoutList( 0, NULL );
    layouts.assign( count > 0 ? count : 0, NULL );
    if (count > 0)
        layouts.resize( GetKeyboardLayoutList( count, &layouts[0] ) );
    for (size_t i = 0; i < layouts.size(); ++i)
        wot::stock_language_bar::addUnique( names, osLanguage( layouts[i] ) );
    return names;
}

struct Instance
{
    Ptr<Movie> movie;
    AS3::MovieRoot * root;
    AS3::ASVM * vm;
    AS3::Value stage;
    AS3::Value main;

    AS3::Value Str( const char * text ) { return AS3::Value( vm->GetStringManager().CreateString( text ) ); }

    std::string Exception()
    {
        if (!vm->IsException())
            return "none";
        AS3::Value e = vm->GetExceptionValue();
        vm->IgnoreException();
        ASString s = vm->GetStringManager().CreateEmptyString();
        if (!e.Convert2String( s ))
            vm->IgnoreException();
        return s.ToCStr();
    }

    AS3::Value Get( const AS3::Value & object, const char * name )
    {
        AS3::Value result;
        if (!object.IsObject() || !object.GetObject())
            return result;
        const AS3::Multiname mn( vm->GetPublicNamespace(), Str( name ) );
        if (!object.GetObject()->GetProperty( mn, result ))
            Exception();
        return result;
    }

    AS3::Value Call( const AS3::Value & object, const char * method, unsigned argc = 0,
        const AS3::Value * argv = NULL )
    {
        AS3::Value result;
        if (object.IsObject() && object.GetObject())
            object.GetObject()->ExecutePropertyUnsafe( vm->GetStringManager().CreateString( method ),
                result, argc, argv );
        return result;
    }

    bool IsNull( const AS3::Value & v ) const { return !v.IsObject() || v.GetObject() == NULL; }

    unsigned Count( const AS3::Value & v ) const
    {
        if (v.IsNumber())
            return unsigned( v.AsNumber() );
        return v.GetKind() == AS3::Value::kInt ? unsigned( v.AsInt() ) :
            v.GetKind() == AS3::Value::kUInt ? unsigned( v.AsUInt() ) : 0u;
    }

    std::string Text( const AS3::Value & tab )
    {
        AS3::Value text = Get( Get( tab, "tf" ), "text" );
        return text.IsString() ? text.AsString().ToCStr() : "<no text>";
    }

    // The VM holds the handler reference; a Counter is heap-owned.
    void Listen( Counter & counter, const AS3::Value & target, const char * type )
    {
        GFx::Value function;
        movie->CreateFunction( &function, &counter );
        AS3::Value listener;
        root->GFxValue2ASValue( function, &listener );
        AS3::Value args[] = { Str( type ), listener };
        Call( target, "addEventListener", 2, args );
    }

    void Click( float x, float y )
    {
        movie->NotifyMouseState( x, y, 0 );
        movie->Advance( 0.02f );
        movie->NotifyMouseState( x, y, 1 );
        movie->Advance( 0.02f );
        movie->NotifyMouseState( x, y, 0 );
        movie->Advance( 0.02f );
    }

    // AS3 values must not outlive the root that owns them.
    void Unbind()
    {
        stage.SetUndefined();
        main.SetUndefined();
    }

    void Release()
    {
        Unbind();
        movie = NULL;
    }

    void Bind()
    {
        root = static_cast<AS3::MovieRoot *>( movie->pASMovieRoot.GetPtr() );
        vm = root->GetAVM();
        stage = AS3::Value( AS3::ToAvmDisplayObjContainer( root->GetStage() )->GetAS3Obj() );
        AS3::Value zero( SInt32( 0 ) );
        main = Call( stage, "getChildAt", 1, &zero );
    }
};

// LangBar frame1, and its LangBar_OnInit, run before any SetFocusEvent.
bool create( MovieDef & def, Instance & instance )
{
    instance.movie = *def.CreateInstance( true );
    if (!instance.movie)
        return false;
    instance.movie->SetViewport( GFx::Viewport( 1024, 768, 0, 0, 1024, 768 ) );
    instance.movie->SetMouseCursorCount( 1 );
    // frame1 ends in stop(), so this Advance cannot reach frame2 or rerun frame1.
    instance.movie->Advance( 0.0f );
    instance.Bind();
    return !instance.IsNull( instance.main );
}

// The receivers MouseDownHandler reads first, and one outside Stage click.
void sampleClick( const char * label, Instance & a, CountingLog & log, bool expectInitialized,
    const std::string & expected )
{
    AS3::Value ime = a.Get( a.main, "IMENameTab_mc" );
    AS3::Value input = a.Get( a.main, "InputLangTab_mc" );
    std::printf( "%s IMENameTab_mc=%s InputLangTab_mc=%s\n", label, a.IsNull( ime ) ? "null" : "Bar_mc",
        a.IsNull( input ) ? "null" : "Bar_mc" );
    Ptr<Counter> later = *new Counter;
    a.Listen( *later, a.stage, "mouseDown" );
    const unsigned before = log.nullReferences;
    a.Click( 900.0f, 600.0f );
    const unsigned thrown = log.nullReferences - before;
    char detail[256];
    std::snprintf( detail, sizeof( detail ), "#1009=%u laterListener=%u", thrown, later->calls );
    std::string name = std::string( label ) + "-click";
    if (expectInitialized)
    {
        check( (name + "-receivers").c_str(), !a.IsNull( ime ) && !a.IsNull( input ), "" );
        check( (name + "-label").c_str(), a.Text( input ) == expected,
            "shown=\"" + a.Text( input ) + "\" os=\"" + expected + "\"" );
        check( name.c_str(), thrown == 0 && later->calls == 1, detail );
    }
    else
    {
        check( (name + "-reproduces-failure").c_str(), a.IsNull( ime ) && thrown == 1 && later->calls == 0,
            detail );
    }
}

void selection( Instance & a )
{
    std::vector<HKL> layouts;
    const std::vector<std::string> names = osLanguages( layouts );
    AS3::Value tab = a.Str( "InputLangTab_mc" );
    a.Call( a.main, "SelectItem", 1, &tab );
    const unsigned count = a.Count( a.Get( a.main, "NumRows" ) );
    std::string shown;
    AS3::Value clips = a.Get( a.main, "Clips" );
    for (unsigned i = 0; i < count; ++i)
    {
        AS3::Value row;
        const AS3::Multiname mn( a.vm->GetPublicNamespace(), AS3::Value( SInt32( i ) ) );
        if (!a.IsNull( clips ))
            clips.GetObject()->GetProperty( mn, row );
        shown += std::string( i ? "|" : "" ) + a.Text( row );
    }
    std::string os;
    for (size_t i = 0; i < names.size(); ++i)
        os += std::string( i ? "|" : "" ) + names[i];
    check( "installed-languages", shown == os && count == names.size(), "shown=" + shown + " os=" + os );

    const HKL original = GetKeyboardLayout( 0 );
    for (size_t i = 0; i < names.size(); ++i)
    {
        AS3::Value item = a.Str( ("InputLangNames_" + names[i]).c_str() );
        a.Call( a.main, "SelectItem", 1, &item );
        const std::string active = osLanguage( GetKeyboardLayout( 0 ) );
        check( "select-input-language", active == names[i] && a.Text( a.Get( a.main, "InputLangTab_mc" ) ) == active,
            "requested=\"" + names[i] + "\" os=\"" + active + "\" shown=\"" +
            a.Text( a.Get( a.main, "InputLangTab_mc" ) ) + "\"" );
    }
    ActivateKeyboardLayout( original, 0 );

    AS3::Value imeTab = a.Str( "IMENameTab_mc" );
    a.Call( a.main, "SelectItem", 1, &imeTab );
    std::printf( "ime-list rows=%u (IMM IME layouts of the current language only)\n",
        a.Count( a.Get( a.main, "NumRows" ) ) );
    a.Click( 900.0f, 600.0f );
}

// The authored frame1 call: IMEEx.SendLangBarMessage( this, "LangBar_OnInit", "" ).
bool initAgain( Instance & a )
{
    AS3::Class * imeEx = a.vm->GetClass( StringDataPtr( "scaleform.gfx.IMEEx" ), a.vm->GetFrameAppDomain() );
    if (imeEx == NULL)
        return false;
    AS3::Value args[] = { a.main, a.Str( "LangBar_OnInit" ), a.Str( "" ) };
    a.Call( AS3::Value( imeEx ), "SendLangBarMessage", 3, args );
    return true;
}

// Ordinary text focus and typing still go through the existing focus owner.
void typing( Instance & a )
{
    AS3::Value field;
    a.vm->CreateObject( "flash.text.TextField", a.vm->GetFrameAppDomain(), field, 0, NULL );
    if (a.IsNull( field ))
    {
        check( "typing", false, "cannot create a TextField " + a.Exception() );
        return;
    }
    const AS3::Multiname type( a.vm->GetPublicNamespace(), a.Str( "type" ) );
    field.GetObject()->SetProperty( type, a.Str( "input" ) );
    a.Call( a.stage, "addChild", 1, &field );
    const AS3::Multiname focus( a.vm->GetPublicNamespace(), a.Str( "focus" ) );
    a.stage.GetObject()->SetProperty( focus, field );
    a.movie->HandleEvent( KeyEvent( GFx::Event::KeyDown, Key::A, 'a', 'a' ) );
    a.movie->HandleEvent( CharEvent( 'a' ) );
    a.movie->HandleEvent( KeyEvent( GFx::Event::KeyUp, Key::A, 'a', 'a' ) );
    a.movie->Advance( 0.02f );
    // Whether KeyEvent and CharEvent each insert is SDK input policy; only typing is checked.
    AS3::Value text = a.Get( field, "text" );
    const std::string typed = text.IsString() ? text.AsString().ToCStr() : "";
    check( "typing", !typed.empty() && typed.find_first_not_of( 'a' ) == std::string::npos,
        "text=\"" + typed + "\" exception=" + a.Exception() );
}
} // namespace

int main( int argc, char ** argv )
{
    SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX );
    if (argc != 3)
        return 2;
    const std::string mode = argv[2];
    const bool owner = mode != "baseline";
    SysAllocWinAPI allocator;
    GFx::System system( &allocator );
    {
        Ptr<AMP::ServerState> s = *new AMP::ServerState;
        s->StateFlags = AMP::Amp_Disabled;
        AmpServer::GetInstance().UpdateState( s );
    }
    const std::string expected = osLanguage( GetKeyboardLayout( 0 ) );
    std::printf( "os layout=%08lx language=\"%s\" mode=%s\n",
        static_cast<unsigned long>( reinterpret_cast<ULONG_PTR>( GetKeyboardLayout( 0 ) ) ), expected.c_str(),
        mode.c_str() );
    {
        Ptr<CountingLog> log = *new CountingLog;
        Loader loader;
        loader.SetLog( log );
        loader.SetFileOpener( Ptr<FileOpener>( *new FileOpener ) );
        loader.SetAS3Support( Ptr<ASSupport>( *new AS3Support ) );
        if (owner)
            wot::stock_language_bar::install( loader );
        check( "owner-installed-before-movie", bool( loader.GetIMEManager() ) == owner, mode );
        Ptr<MovieDef> def = *loader.CreateMovie( argv[1], Loader::LoadAll | Loader::LoadWaitCompletion );
        if (!def)
            return 3;

        // A partially started instance never runs frame1 and is released untouched.
        {
            Ptr<Movie> partial = *def->CreateInstance( false );
        }

        Instance a;
        if (!create( *def, a ))
            return 4;
        if (mode == "focus-before-activation")
        {
            // No SetFocusEvent yet, so the owner has no active movie. A click on the
            // tab moves mouse focus through MovieImpl::QueueSetFocusTo and HandleFocus.
            std::printf( "focus before activation: begin\n" );
            std::fflush( stdout );
            a.Click( 5.0f, 5.0f );
            check( "focus-before-activation", true, "returned exception=" + a.Exception() );
            a.Release();
            return g_failures;
        }
        sampleClick( "first-frame-before-focus", a, *log, owner, expected );
        if (!owner)
        {
            a.Release();
            return g_failures;
        }

        // Repeated init keeps the authored tabs and their OS label.
        {
            AS3::Value input = a.Get( a.main, "InputLangTab_mc" );
            const bool sent = initAgain( a );
            check( "repeated-init", sent && a.Get( a.main, "InputLangTab_mc" ).GetObject() == input.GetObject() &&
                a.Text( input ) == expected, "exception=" + a.Exception() );
        }

        // B is the focus-owned active movie when C's first frame asks.
        a.movie->HandleEvent( SetFocusEvent() );
        Instance b;
        create( *def, b );
        b.movie->HandleEvent( SetFocusEvent() );
        Ptr<Counter> bLanguage = *new Counter, bIme = *new Counter;
        b.Listen( *bLanguage, b.main, "SetCurrentInputLanguage" );
        b.Listen( *bIme, b.main, "SetIMEName" );
        Instance c;
        create( *def, c );
        sampleClick( "inactive-movie", c, *log, true, expected );
        check( "active-movie-untouched", bLanguage->calls == 0 && bIme->calls == 0, "" );

        // Restart reloads frame1 in a fresh root of the same movie.
        a.Unbind();
        a.movie->Restart();
        a.Bind();
        sampleClick( "restarted", a, *log, true, expected );

        selection( c );
        b.movie->HandleEvent( SetFocusEvent() );
        typing( b );

        // C goes first; a later command from A must not reach it.
        c.Release();
        const bool sent = initAgain( a );
        check( "after-release", sent && a.Text( a.Get( a.main, "InputLangTab_mc" ) ) == expected,
            "exception=" + a.Exception() );
        b.Release();
        a.Release();
    }
    std::printf( "%s failures=%d\n", g_failures ? "FAILED" : "PASSED", g_failures );
    return g_failures;
}
