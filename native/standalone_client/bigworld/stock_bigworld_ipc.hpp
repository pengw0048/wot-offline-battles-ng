#pragma once

#include <cstdint>
#include <string>

namespace wot
{
// #1513 PyMutex (type 01a0cb40) owns at most one Win32 mutex handle.
class StockIpcMutex
{
public:
    enum WaitResult { WAIT_RESULT_ACQUIRED, WAIT_RESULT_TIMEOUT, WAIT_RESULT_FAILED };

    StockIpcMutex() : handle_( nullptr ) {}
    // 006ae740 closes the handle; it never releases an owned mutex.
    ~StockIpcMutex();
    // 006ae7b0: CloseHandle, then forget the handle even when CloseHandle fails.
    bool close( std::uint32_t & error );
    // 006ae480 after closing: CreateMutexA( NULL, FALSE, name ), which also opens an existing name.
    bool create( const char * name, std::uint32_t & error );
    // 006ae3b6: WAIT_OBJECT_0 and WAIT_ABANDONED both acquire.
    WaitResult acquire( std::uint32_t milliseconds );
    bool release( std::uint32_t & error );
    void * handle() const { return handle_; }

    // 006ae39d: float seconds widened, * 1000.0, CRT 013d83b1, low 32 bits.
    static std::uint32_t timeoutMilliseconds( float seconds );

private:
    StockIpcMutex( const StockIpcMutex & );
    StockIpcMutex & operator=( const StockIpcMutex & );
    void * handle_;
};

// 00686c00: FormatMessageA text; false for error zero or a format failure.
bool stockIpcErrorMessage( std::uint32_t error, std::string & message );
}
