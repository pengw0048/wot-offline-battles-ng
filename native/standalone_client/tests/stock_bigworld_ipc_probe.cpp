// Win32 kernel probe for the production BigWorldIPC.Mutex core.
// Build: compile with -DWOT_STOCK_BIGWORLD_IPC_CORE_ONLY together with
// bigworld/stock_bigworld_ipc.cpp for x86 Windows; run without arguments.
// Exit code is the number of failed checks. Synchronisation uses events and
// joins only; no sleep is used as evidence.
#include "stock_bigworld_ipc.hpp"

#include <windows.h>

#include <cstdio>
#include <functional>
#include <string>

namespace
{
using wot::StockIpcMutex;
int failures = 0;
const DWORD LONG_WAIT = 10000;

void check( bool condition, const char * name, DWORD detail = 0 )
{
    std::printf( "%s %s %lu\n", condition ? "PASS" : "FAIL", name, static_cast<unsigned long>( detail ) );
    failures += condition ? 0 : 1;
}

std::string unique( const char * label )
{
    char text[128];
    std::snprintf( text, sizeof( text ), "wot_bigworld_ipc_probe_%lu_%lu_%s",
        static_cast<unsigned long>( GetCurrentProcessId() ),
        static_cast<unsigned long>( GetTickCount() ), label );
    return text;
}

struct Thread
{
    std::function<void()> body;
    HANDLE handle = NULL;
    static DWORD WINAPI run( LPVOID self ) { static_cast<Thread *>( self )->body(); return 0; }
    explicit Thread( std::function<void()> work ) : body( work )
    {
        handle = CreateThread( NULL, 0, &Thread::run, this, 0, NULL );
    }
    bool join() { return WaitForSingleObject( handle, LONG_WAIT ) == WAIT_OBJECT_0; }
    ~Thread() { if (handle) CloseHandle( handle ); }
};

StockIpcMutex::WaitResult acquireSeconds( StockIpcMutex & mutex, float seconds )
{
    return mutex.acquire( StockIpcMutex::timeoutMilliseconds( seconds ) );
}

void sameNameAndThreads()
{
    const std::string name = unique( "same" );
    StockIpcMutex first, second, other;
    std::uint32_t error;
    check( first.create( name.c_str(), error ), "create first handle", error );
    check( second.create( name.c_str(), error ), "open same name as second handle", error );
    check( first.handle() != second.handle(), "two distinct handles" );
    check( other.create( unique( "other" ).c_str(), error ), "create distinct name", error );
    check( acquireSeconds( first, 0.f ) == StockIpcMutex::WAIT_RESULT_ACQUIRED, "owner acquires" );
    check( acquireSeconds( first, 0.f ) == StockIpcMutex::WAIT_RESULT_ACQUIRED, "owner recursion" );

    StockIpcMutex::WaitResult timed = StockIpcMutex::WAIT_RESULT_FAILED;
    StockIpcMutex::WaitResult distinct = StockIpcMutex::WAIT_RESULT_FAILED;
    bool foreignRelease = true;
    std::uint32_t foreignError = 0;
    Thread contender( [&]()
    {
        timed = acquireSeconds( second, 0.05f );
        distinct = acquireSeconds( other, 0.f );
        std::uint32_t ignored;
        other.release( ignored );
        foreignRelease = second.release( foreignError );
    } );
    check( contender.join(), "contender finished" );
    check( timed == StockIpcMutex::WAIT_RESULT_TIMEOUT, "other thread times out on same name" );
    check( distinct == StockIpcMutex::WAIT_RESULT_ACQUIRED, "distinct name is independent" );
    check( !foreignRelease && foreignError == ERROR_NOT_OWNER, "non-owner release fails", foreignError );

    HANDLE waiting = CreateEventA( NULL, TRUE, FALSE, NULL );
    HANDLE acquired = CreateEventA( NULL, TRUE, FALSE, NULL );
    StockIpcMutex::WaitResult later = StockIpcMutex::WAIT_RESULT_FAILED;
    Thread waiter( [&]()
    {
        SetEvent( waiting );
        later = acquireSeconds( second, 10.f );
        SetEvent( acquired );
        std::uint32_t ignored;
        second.release( ignored );
    } );
    check( WaitForSingleObject( waiting, LONG_WAIT ) == WAIT_OBJECT_0, "waiter started" );
    check( WaitForSingleObject( acquired, 0 ) == WAIT_TIMEOUT, "waiter blocked while recursively owned" );
    check( first.release( error ), "first release of recursion", error );
    check( WaitForSingleObject( acquired, 0 ) == WAIT_TIMEOUT, "still owned after one release" );
    check( first.release( error ), "second release of recursion", error );
    check( WaitForSingleObject( acquired, LONG_WAIT ) == WAIT_OBJECT_0, "waiter acquires after release" );
    check( waiter.join() && later == StockIpcMutex::WAIT_RESULT_ACQUIRED, "waiter result" );
    check( !first.release( error ) && error == ERROR_NOT_OWNER, "release beyond recursion fails", error );
    CloseHandle( waiting );
    CloseHandle( acquired );
}

void abandonedAndDestroyed()
{
    const std::string name = unique( "abandoned" );
    StockIpcMutex observer;
    std::uint32_t error;
    check( observer.create( name.c_str(), error ), "observer handle", error );
    Thread owner( [&]()
    {
        // Destruction closes this handle without releasing the owned mutex.
        StockIpcMutex local;
        std::uint32_t ignored;
        local.create( name.c_str(), ignored );
        acquireSeconds( local, 0.f );
    } );
    check( owner.join(), "owner thread exited while owning" );
    HANDLE raw = OpenMutexA( SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, name.c_str() );
    check( raw != NULL, "name survives while observer is open" );
    const DWORD rawWait = WaitForSingleObject( raw, 0 );
    check( rawWait == WAIT_ABANDONED, "kernel reports WAIT_ABANDONED", rawWait );
    ReleaseMutex( raw );
    CloseHandle( raw );

    Thread second( [&]()
    {
        StockIpcMutex local;
        std::uint32_t ignored;
        local.create( name.c_str(), ignored );
        acquireSeconds( local, 0.f );
    } );
    check( second.join(), "second owner abandoned" );
    check( acquireSeconds( observer, 0.f ) == StockIpcMutex::WAIT_RESULT_ACQUIRED,
        "acquire maps WAIT_ABANDONED to acquired" );
    check( observer.release( error ), "release after abandoned acquisition", error );
}

void closeAndRecreate()
{
    const std::string first = unique( "first" ), second = unique( "second" );
    StockIpcMutex mutex;
    std::uint32_t error;
    check( mutex.create( first.c_str(), error ), "create before recreate", error );
    check( mutex.create( second.c_str(), error ), "recreate with new name", error );
    HANDLE old = OpenMutexA( SYNCHRONIZE, FALSE, first.c_str() );
    check( old == NULL && GetLastError() == ERROR_FILE_NOT_FOUND, "recreate closed the previous handle" );
    if (old)
        CloseHandle( old );
    check( mutex.close( error ), "explicit close", error );
    check( mutex.handle() == NULL, "close forgets the handle" );
    check( mutex.close( error ), "second close is a no-op", error );
    check( mutex.acquire( 0 ) == StockIpcMutex::WAIT_RESULT_FAILED, "acquire after close fails" );
    check( !mutex.release( error ) && error == ERROR_INVALID_HANDLE, "release after close fails", error );
    std::string message;
    check( wot::stockIpcErrorMessage( ERROR_INVALID_HANDLE, message ) && !message.empty(),
        "system message text" );
    check( !wot::stockIpcErrorMessage( 0, message ), "no message for error zero" );
    StockIpcMutex never;
    check( never.acquire( 0 ) == StockIpcMutex::WAIT_RESULT_FAILED, "acquire before create fails" );
}

int child( const char * name, const char * ready, const char * go )
{
    StockIpcMutex mutex;
    std::uint32_t error;
    if (!mutex.create( name, error ))
        return 10;
    const StockIpcMutex::WaitResult blocked = acquireSeconds( mutex, 0.f );
    HANDLE readyEvent = OpenEventA( EVENT_MODIFY_STATE, FALSE, ready );
    HANDLE goEvent = OpenEventA( SYNCHRONIZE, FALSE, go );
    if (!readyEvent || !goEvent)
        return 11;
    SetEvent( readyEvent );
    if (WaitForSingleObject( goEvent, LONG_WAIT ) != WAIT_OBJECT_0)
        return 12;
    const StockIpcMutex::WaitResult later = acquireSeconds( mutex, 10.f );
    mutex.release( error );
    return (blocked == StockIpcMutex::WAIT_RESULT_TIMEOUT ? 0 : 1) |
        (later == StockIpcMutex::WAIT_RESULT_ACQUIRED ? 0 : 2);
}

void otherProcess( const char * executable )
{
    const std::string name = unique( "process" ), ready = name + "_ready", go = name + "_go";
    HANDLE readyEvent = CreateEventA( NULL, TRUE, FALSE, ready.c_str() );
    HANDLE goEvent = CreateEventA( NULL, TRUE, FALSE, go.c_str() );
    StockIpcMutex mutex;
    std::uint32_t error;
    check( mutex.create( name.c_str(), error ), "parent create", error );
    check( acquireSeconds( mutex, 0.f ) == StockIpcMutex::WAIT_RESULT_ACQUIRED, "parent acquires" );
    std::string command = std::string( "\"" ) + executable + "\" --child " + name + " " + ready + " " + go;
    STARTUPINFOA startup = {};
    startup.cb = sizeof( startup );
    PROCESS_INFORMATION process = {};
    const BOOL started = CreateProcessA( NULL, &command[0], NULL, NULL, FALSE, 0, NULL, NULL,
        &startup, &process );
    check( started != FALSE, "child started", started ? 0 : GetLastError() );
    if (!started)
        return;
    check( WaitForSingleObject( readyEvent, LONG_WAIT ) == WAIT_OBJECT_0, "child observed exclusion" );
    check( mutex.release( error ), "parent releases", error );
    SetEvent( goEvent );
    DWORD code = 0xffffffff;
    check( WaitForSingleObject( process.hProcess, LONG_WAIT ) == WAIT_OBJECT_0, "child exited" );
    GetExitCodeProcess( process.hProcess, &code );
    check( code == 0, "child timed out while held and acquired after release", code );
    CloseHandle( process.hThread );
    CloseHandle( process.hProcess );
    CloseHandle( readyEvent );
    CloseHandle( goEvent );
}

void conversions()
{
    check( StockIpcMutex::timeoutMilliseconds( 0.5f ) == 500, "0.5 s" );
    check( StockIpcMutex::timeoutMilliseconds( -1.f ) == 0xfffffc18u, "-1 s wraps" );
    check( StockIpcMutex::timeoutMilliseconds( 1e30f ) == 0, "out of range is zero" );
}
}

int main( int argc, char ** argv )
{
    if (argc == 5 && std::string( argv[1] ) == "--child")
        return child( argv[2], argv[3], argv[4] );
    sameNameAndThreads();
    abandonedAndDestroyed();
    closeAndRecreate();
    otherProcess( argv[0] );
    conversions();
    std::printf( "%d failure(s)\n", failures );
    return failures;
}
