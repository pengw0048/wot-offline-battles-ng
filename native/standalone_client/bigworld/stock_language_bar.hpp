#pragma once

#include <windows.h>
#include <imm.h>

#include <string>
#include <vector>

#include "GFx/IME/GFx_IMEManager.h"
#include "GFx/GFx_PlayerImpl.h"
#include "GFx/GFx_ASMovieRootBase.h"
#include "stock_language_bar_commands.hpp"

#ifdef _MSC_VER
#pragma comment( lib, "imm32.lib" )
#endif

namespace wot
{
namespace stock_language_bar
{
inline bool utf8( const std::wstring & text, std::string & result )
{
    result.clear();
    if (text.empty())
        return true;
    const int bytes = WideCharToMultiByte( CP_UTF8, 0, text.c_str(), static_cast<int>( text.size() ),
        NULL, 0, NULL, NULL );
    if (bytes <= 0)
        return false;
    result.resize( static_cast<size_t>( bytes ) );
    return WideCharToMultiByte( CP_UTF8, 0, text.c_str(), static_cast<int>( text.size() ),
        &result[0], bytes, NULL, NULL ) == bytes;
}

// 00cd3b29: GetLocaleInfoW( LOWORD(hkl), LOCALE_SLANGUAGE ).
inline bool languageName( LANGID language, std::string & result )
{
    const LCID locale = MAKELCID( language, SORT_DEFAULT );
    const int size = GetLocaleInfoW( locale, LOCALE_SLANGUAGE, NULL, 0 );
    if (size <= 1)
        return false;
    std::wstring name( static_cast<size_t>( size ), L'\0' );
    if (GetLocaleInfoW( locale, LOCALE_SLANGUAGE, &name[0], size ) != size)
        return false;
    name.resize( static_cast<size_t>( size - 1 ) );
    return utf8( name, result ) && !result.empty();
}

// 00cd3b9f: an HKL with any of the top three bits set is an IMM IME layout.
inline bool imeDescription( HKL layout, std::string & result )
{
    if ((reinterpret_cast<ULONG_PTR>( layout ) & 0xe0000000) == 0)
        return false;
    const UINT size = ImmGetDescriptionW( layout, NULL, 0 );
    if (size == 0)
        return false;
    std::wstring name( size + 1, L'\0' );
    const UINT written = ImmGetDescriptionW( layout, &name[0], size + 1 );
    if (written == 0 || written > size)
        return false;
    name.resize( written );
    return utf8( name, result ) && !result.empty();
}

inline bool describe( HKL handle, Layout & result )
{
    if (handle == NULL)
        return false;
    result.handle = reinterpret_cast<ULONG_PTR>( handle );
    result.language = LOWORD( reinterpret_cast<ULONG_PTR>( handle ) );
    if (!languageName( static_cast<LANGID>( result.language ), result.languageName ))
        return false;
    result.ime = imeDescription( handle, result.imeName );
    if (!result.ime)
        result.imeName = result.languageName;
    return true;
}

// The calling thread's keyboard layouts, read live on every command.
class Win32Layouts
{
public:
    explicit Win32Layouts( Scaleform::Log * log ) : log_( log ) {}

    bool current( Layout & result ) const
    {
        return describe( GetKeyboardLayout( 0 ), result );
    }

    // A layout whose language has no name is not offered.
    bool installed( std::vector<Layout> & result ) const
    {
        result.clear();
        const int count = GetKeyboardLayoutList( 0, NULL );
        if (count <= 0)
            return false;
        std::vector<HKL> handles( static_cast<size_t>( count ) );
        const int listed = GetKeyboardLayoutList( count, &handles[0] );
        if (listed <= 0)
            return false;
        for (int i = 0; i < listed && i < count; ++i)
        {
            Layout layout;
            if (describe( handles[i], layout ))
                result.push_back( layout );
        }
        return true;
    }

    // 00cd36ce activates without checking; success needs the thread to report it.
    bool activate( unsigned long long handle ) const
    {
        HKL layout = reinterpret_cast<HKL>( static_cast<ULONG_PTR>( handle ) );
        ActivateKeyboardLayout( layout, 0 );
        return GetKeyboardLayout( 0 ) == layout;
    }

    void report( const std::string & text ) const
    {
        if (log_ != NULL)
            log_->LogError( "LangBar: %s\n", text.c_str() );
    }

private:
    Scaleform::Log * log_;
};

// Dispatches through the SDK's AS3 IME owner bound to one command's movie and LangBar.
class MovieSink
{
public:
    explicit MovieSink( Scaleform::GFx::ASIMEManager & target ) : target_( target ) {}

    void post( Event event, const std::string & text )
    {
        switch (event)
        {
        case CURRENT_INPUT_LANGUAGE:
            target_.BroadcastSetCurrentInputLanguage( text.c_str() );
            break;
        case SUPPORTED_LANGUAGES:
            target_.BroadcastSetSupportedLanguages( text.c_str() );
            break;
        case SUPPORTED_IMES:
            target_.BroadcastSetSupportedIMEs( text.c_str() );
            break;
        case IME_NAME:
            target_.BroadcastSetIMEName( text.c_str() );
            break;
        }
    }

private:
    Scaleform::GFx::ASIMEManager & target_;
};

/**
 *	#1513 installs IMEManagerWin32 on the loader; its LangBar commands answer the
 *	sending LangBar with the calling thread's keyboard layouts. This owner keeps
 *	only that family: composition, candidate lists and WM_IME messages remain
 *	with the OS.
 */
class Owner : public Scaleform::GFx::IMEManagerBase
{
public:
    virtual void SendLangBarMessage( Scaleform::GFx::DisplayObject * sender,
        const Scaleform::GFx::ASString & command, const Scaleform::GFx::ASString & message )
    {
        namespace GFx = Scaleform::GFx;
        if (sender == NULL)
            return;
        // Answers run script, which may remove the sender or release its movie.
        Scaleform::Ptr<GFx::DisplayObject> keepSender = sender;
        Scaleform::Ptr<GFx::MovieImpl> movie = sender->GetMovieImpl();
        if (!movie || !movie->pASMovieRoot)
            return;
        GFx::ASSupport * support = movie->pASMovieRoot->GetASSupport();
        Scaleform::Ptr<GFx::ASIMEManager> target;
        if (support != NULL)
            target = support->CreateASIMEManager();
        if (!target)
            return;
        // Not the focus-owned active movie: the answer goes to the sender's own movie.
        target->SetIMEManager( this );
        target->SetActiveMovie( movie );
        target->pLangContext2 = sender;
        Scaleform::Ptr<Scaleform::Log> log = movie->GetLog();
        Win32Layouts host( log.GetPtr() );
        MovieSink sink( *target );
        handle( host, sink, command.ToCStr(), message.ToCStr() );
        target->pLangContext2 = NULL;
        target->ShutDown();
    }
};

// #1513 DeviceApp: the IME owner is installed before any movie exists.
inline void install( Scaleform::GFx::Loader & loader )
{
    Scaleform::Ptr<Owner> owner = *new Owner();
    loader.SetIMEManager( owner );
}
} // namespace stock_language_bar
} // namespace wot
