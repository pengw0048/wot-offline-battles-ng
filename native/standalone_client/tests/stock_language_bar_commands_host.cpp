// Drives the LangBar command owner over a scripted OS layout table. It proves
// the command/answer schema and ordering, not Win32 or SDK dispatch.
#include "stock_language_bar_commands.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace wot::stock_language_bar;

namespace
{
int g_failures = 0;

void expect( bool condition, const char * scenario, const std::string & detail )
{
    if (!condition)
    {
        ++g_failures;
        std::printf( "FAIL %s: %s\n", scenario, detail.c_str() );
    }
}

Layout layout( unsigned long long handle, unsigned language, const char * name, const char * ime )
{
    Layout result;
    result.handle = handle;
    result.language = language;
    result.languageName = name;
    result.ime = ime != NULL;
    result.imeName = ime != NULL ? ime : name;
    return result;
}

struct Host
{
    Host() : active( 0 ), currentWorks( true ), listWorks( true ), switches( true ) {}

    bool current( Layout & result ) const
    {
        if (!currentWorks)
            return false;
        result = layouts[active];
        return true;
    }

    bool installed( std::vector<Layout> & result ) const
    {
        result = listWorks ? layouts : std::vector<Layout>();
        return listWorks;
    }

    bool activate( unsigned long long handle )
    {
        activations.push_back( handle );
        if (!switches)
            return false;
        for (size_t i = 0; i < layouts.size(); ++i)
        {
            if (layouts[i].handle == handle)
                active = i;
        }
        return true;
    }

    void report( const std::string & text ) { reports.push_back( text ); }

    std::vector<Layout> layouts;
    size_t active;
    bool currentWorks;
    bool listWorks;
    bool switches;
    std::vector<unsigned long long> activations;
    std::vector<std::string> reports;
};

const char * const EVENT_NAMES[] = { "SetCurrentInputLanguage", "SetSupportedLanguages",
    "SetSupportedIMENames", "SetIMEName" };

struct Sink
{
    Sink() : host( NULL ), reenter( 0 ) {}

    void post( Event event, const std::string & text )
    {
        posts.push_back( std::string( EVENT_NAMES[event] ) + "=" + text );
        // The authored handler may send another command from inside the answer.
        if (reenter > 0 && host != NULL)
        {
            --reenter;
            handle( *host, *this, ON_INIT, "" );
        }
    }

    std::string joined() const
    {
        std::string result;
        for (size_t i = 0; i < posts.size(); ++i)
            result += (i ? " | " : "") + posts[i];
        return result;
    }

    Host * host;
    int reenter;
    std::vector<std::string> posts;
};

Host desktop()
{
    Host host;
    host.layouts.push_back( layout( 0x04090409, 0x0409, "English (United States)", NULL ) );
    host.layouts.push_back( layout( 0xf0020409, 0x0409, "English (United States)", NULL ) );
    host.layouts.push_back( layout( 0x08040804, 0x0804, "Chinese (Simplified, PRC)", NULL ) );
    host.layouts.push_back( layout( 0xe00e0804, 0x0804, "Chinese (Simplified, PRC)", "QuanPin" ) );
    host.layouts.push_back( layout( 0xe0010804, 0x0804, "Chinese (Simplified, PRC)", "ShuangPin" ) );
    host.layouts.push_back( layout( 0xe0010411, 0x0411, "Japanese (Japan)", "Microsoft IME" ) );
    return host;
}

void run( const char * scenario, Host & host, const char * command, const char * message,
    const std::string & posts, size_t reports )
{
    Sink sink;
    handle( host, sink, command, message );
    expect( sink.joined() == posts, scenario, "posts [" + sink.joined() + "]" );
    expect( host.reports.size() == reports, scenario, "unexpected report count" );
}
} // namespace

int main()
{
    {
        Host host = desktop();
        run( "init", host, ON_INIT, "",
            "SetCurrentInputLanguage=English (United States) | SetIMEName=English (United States)", 0 );
    }
    {
        Host host = desktop();
        host.active = 3;
        run( "init ime", host, ON_INIT, "",
            "SetCurrentInputLanguage=Chinese (Simplified, PRC) | SetIMEName=QuanPin", 0 );
    }
    {
        Host host = desktop();
        host.currentWorks = false;
        run( "init failure", host, ON_INIT, "", "SetCurrentInputLanguage=Unsupported", 1 );
    }
    {
        Host host = desktop();
        run( "installed", host, GET_INSTALLED_LANGUAGES, "",
            "SetSupportedLanguages=English (United States)@@Chinese (Simplified, PRC)@@Japanese (Japan)@@", 0 );
    }
    {
        Host host = desktop();
        host.listWorks = false;
        run( "installed failure", host, GET_INSTALLED_LANGUAGES, "", "", 1 );
        Host empty;
        run( "installed empty", empty, GET_INSTALLED_LANGUAGES, "", "", 1 );
    }
    {
        Host host = desktop();
        run( "select current", host, ON_SELECT_INPUT_LANG, "English (United States)",
            "SetCurrentInputLanguage=English (United States) | SetIMEName=English (United States)", 0 );
        expect( host.activations.empty(), "select current", "activated the active language" );
    }
    {
        Host host = desktop();
        run( "select other", host, ON_SELECT_INPUT_LANG, "Japanese (Japan)",
            "SetCurrentInputLanguage=Japanese (Japan) | SetIMEName=Microsoft IME", 0 );
        expect( host.activations.size() == 1 && host.activations[0] == 0xe0010411, "select other",
            "did not activate the Japanese layout" );
    }
    {
        Host host = desktop();
        run( "select first layout", host, ON_SELECT_INPUT_LANG, "Chinese (Simplified, PRC)",
            "SetCurrentInputLanguage=Chinese (Simplified, PRC) | SetIMEName=Chinese (Simplified, PRC)", 0 );
        expect( host.activations.size() == 1 && host.activations[0] == 0x08040804, "select first layout",
            "did not activate the first Chinese layout" );
    }
    {
        Host host = desktop();
        host.switches = false;
        run( "select refused", host, ON_SELECT_INPUT_LANG, "Japanese (Japan)",
            "SetCurrentInputLanguage=English (United States) | SetIMEName=English (United States)", 1 );
    }
    {
        Host host = desktop();
        run( "select unknown", host, ON_SELECT_INPUT_LANG, "Klingon",
            "SetCurrentInputLanguage=English (United States) | SetIMEName=English (United States)", 1 );
        expect( host.activations.empty(), "select unknown", "activated an unknown language" );
    }
    {
        Host host = desktop();
        host.listWorks = false;
        run( "select unlisted", host, ON_SELECT_INPUT_LANG, "Japanese (Japan)",
            "SetCurrentInputLanguage=English (United States) | SetIMEName=English (United States)", 1 );
    }
    {
        Host host = desktop();
        host.active = 2;
        run( "ime names", host, GET_IME_NAMES, "null", "SetSupportedIMENames=QuanPin@@ShuangPin@@", 0 );
        Host english = desktop();
        run( "ime names none", english, GET_IME_NAMES, "", "", 0 );
    }
    {
        Host host = desktop();
        host.active = 3;
        run( "select ime", host, ON_SELECT_IME, "ShuangPin",
            "SetCurrentInputLanguage=Chinese (Simplified, PRC) | SetIMEName=ShuangPin", 0 );
        expect( host.activations.size() == 1 && host.activations[0] == 0xe0010804, "select ime",
            "did not activate ShuangPin" );
    }
    {
        Host host = desktop();
        host.active = 3;
        run( "select foreign ime", host, ON_SELECT_IME, "Microsoft IME",
            "SetCurrentInputLanguage=Chinese (Simplified, PRC) | SetIMEName=QuanPin", 1 );
        expect( host.activations.empty(), "select foreign ime", "activated another language's IME" );
    }
    {
        Host host = desktop();
        host.active = 3;
        host.switches = false;
        run( "select ime refused", host, ON_SELECT_IME, "ShuangPin",
            "SetCurrentInputLanguage=Chinese (Simplified, PRC) | SetIMEName=QuanPin", 1 );
    }
    {
        Host host = desktop();
        run( "unknown", host, "LangBar_Other", "", "", 1 );
    }
    {
        Host host = desktop();
        Sink sink;
        sink.host = &host;
        sink.reenter = 1;
        handle( host, sink, ON_INIT, "" );
        expect( sink.joined() == "SetCurrentInputLanguage=English (United States) | "
            "SetCurrentInputLanguage=English (United States) | SetIMEName=English (United States) | "
            "SetIMEName=English (United States)", "reentrant init", "posts [" + sink.joined() + "]" );
    }
    std::printf( "%s %d\n", g_failures ? "FAILED" : "PASSED", g_failures );
    return g_failures ? 1 : 0;
}
