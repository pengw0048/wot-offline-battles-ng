#pragma once

#include <string>
#include <vector>

namespace wot
{
namespace stock_language_bar
{
// #1513 IMEManagerWin32::SendLangBarMessage (00ccb210) commands; the authored
// gui/flash/LangBar.swf sends no others.
const char * const ON_INIT = "LangBar_OnInit";
const char * const GET_INSTALLED_LANGUAGES = "LangBar_GetInstalledLanguages";
const char * const ON_SELECT_INPUT_LANG = "LangBar_OnSelectInputLang";
const char * const GET_IME_NAMES = "LangBar_GetIMENames";
const char * const ON_SELECT_IME = "LangBar_OnSelectIME";

// 00cd3b49: the label #1513 broadcasts when the input language has no name.
const char * const UNSUPPORTED = "Unsupported";

// The scaleform.gfx.IMEEventEx types LangBar listens for.
enum Event
{
    CURRENT_INPUT_LANGUAGE,
    SUPPORTED_LANGUAGES,
    SUPPORTED_IMES,
    IME_NAME
};

// One installed keyboard layout as the OS names it, in UTF-8.
struct Layout
{
    Layout() : handle( 0 ), language( 0 ), ime( false ) {}

    unsigned long long handle;
    unsigned language;
    bool ime;
    std::string languageName;
    // The IME description, or languageName for any other layout (00cd3c2c).
    std::string imeName;
};

// 00ccb3e0: every entry is followed by "@@"; LangBar.CreateList splits on it.
inline std::string joinList( const std::vector<std::string> & names )
{
    std::string list;
    for (size_t i = 0; i < names.size(); ++i)
        list += names[i] + "@@";
    return list;
}

inline void addUnique( std::vector<std::string> & names, const std::string & name )
{
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (names[i] == name)
            return;
    }
    names.push_back( name );
}

// Posts the calling thread's layout: the language first, since LangBar creates
// the tabs that the IME name then fills.
template <class Host, class Sink>
void postCurrent( Host & host, Sink & sink )
{
    Layout current;
    if (!host.current( current ))
    {
        host.report( "cannot name the current input language" );
        sink.post( CURRENT_INPUT_LANGUAGE, UNSUPPORTED );
        return;
    }
    sink.post( CURRENT_INPUT_LANGUAGE, current.languageName );
    sink.post( IME_NAME, current.imeName );
}

// LangBar already shows the requested name; activate it and post the layout
// the OS actually reports afterwards.
template <class Host, class Sink>
void select( Host & host, Sink & sink, const std::string & name, bool ime )
{
    Layout current;
    std::vector<Layout> layouts;
    if (!host.current( current ) || !host.installed( layouts ))
    {
        host.report( "cannot read the keyboard layouts to select " + name );
    }
    else if ((ime ? current.imeName : current.languageName) != name)
    {
        const Layout * target = NULL;
        for (size_t i = 0; i < layouts.size() && target == NULL; ++i)
        {
            const Layout & layout = layouts[i];
            if (ime ? layout.ime && layout.language == current.language && layout.imeName == name :
                    layout.languageName == name)
                target = &layout;
        }
        if (target == NULL)
            host.report( "no installed keyboard layout is named " + name );
        else if (!host.activate( target->handle ))
            host.report( "the keyboard layout for " + name + " did not become active" );
    }
    postCurrent( host, sink );
}

template <class Host, class Sink>
void handle( Host & host, Sink & sink, const std::string & command, const std::string & message )
{
    if (command == ON_INIT)
    {
        postCurrent( host, sink );
    }
    else if (command == GET_INSTALLED_LANGUAGES)
    {
        std::vector<Layout> layouts;
        std::vector<std::string> names;
        if (host.installed( layouts ))
        {
            for (size_t i = 0; i < layouts.size(); ++i)
                addUnique( names, layouts[i].languageName );
        }
        if (names.empty())
            host.report( "no installed input language can be named" );
        else
            sink.post( SUPPORTED_LANGUAGES, joinList( names ) );
    }
    else if (command == ON_SELECT_INPUT_LANG)
    {
        select( host, sink, message, false );
    }
    else if (command == GET_IME_NAMES)
    {
        // 00ccb3a2 lists the current language's IMEs; the message is unused.
        Layout current;
        std::vector<Layout> layouts;
        std::vector<std::string> names;
        if (host.current( current ) && host.installed( layouts ))
        {
            for (size_t i = 0; i < layouts.size(); ++i)
            {
                if (layouts[i].ime && layouts[i].language == current.language)
                    addUnique( names, layouts[i].imeName );
            }
        }
        // A language without an IME opens no list.
        if (!names.empty())
            sink.post( SUPPORTED_IMES, joinList( names ) );
    }
    else if (command == ON_SELECT_IME)
    {
        select( host, sink, message, true );
    }
    else
    {
        host.report( "unknown LangBar command " + command );
    }
}
} // namespace stock_language_bar
} // namespace wot
