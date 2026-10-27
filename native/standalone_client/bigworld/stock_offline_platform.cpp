// Platform-only port of colorfulmeans/wot-0.9.22-offline-battles v0.10.1,
// commit 144619f6eb2925f8da4b13637bf65b8550fc898f. Numerical/gameplay owners
// and the original executable-address patches are not part of this module.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdint.h>
#include <tlhelp32.h>
#include "stock_offline_platform.hpp"
#include "stock_worker_runtime.hpp"

namespace {
static_assert(sizeof(void *) == 4, "The supported platform is Win32 x86");
#define MAX_HIDDEN_WINDOWS 16U

#define EXCEPTION_TRAIL_PATH_ENV L"WOT_OFFLINE_EXCEPTION_TRAIL_PATH"
#define TRAIL_MAX_RECORDS 512L
#define TRAIL_MAX_FRAMES 24U
#define TRAIL_MAX_MODULES 256U
#define TRAIL_NAME_CHARS 40U
#define TRAIL_TEXT_CHARS 96U
#define TRAIL_BUFFER_BYTES 4096U

#define TRAIL_STATUS_NOT_CONFIGURED 301L
#define TRAIL_STATUS_PATH_INVALID 302L
#define TRAIL_STATUS_MODULES_UNAVAILABLE 303L
#define TRAIL_STATUS_TLS_UNAVAILABLE 304L
#define TRAIL_STATUS_HANDLER_REFUSED 305L


typedef struct HiddenWindow {
	HWND handle;
	WINDOWPLACEMENT placement;
	HANDLE generation;
} HiddenWindow;

typedef struct HideContext {
	DWORD process_id;
	DWORD error_code;
	unsigned int first_new_index;
} HideContext;

typedef struct WorkerWindowContext {
	DWORD process_id;
	unsigned int marked;
	unsigned int visible;
} WorkerWindowContext;

typedef struct TrailModule {
	uintptr_t base;
	uintptr_t end;
	int local;
	char name[TRAIL_NAME_CHARS];
} TrailModule;


static unsigned char *g_image_base = 0;
static HiddenWindow g_hidden_windows[MAX_HIDDEN_WINDOWS];
static unsigned int g_hidden_window_count = 0;
static LONG g_window_busy = 0;
static LONG g_window_generation = 0;
static const WCHAR WINDOW_GENERATION[] = L"WoTIndependent.OfflinePlatform.Generation";
static TrailModule g_trail_modules[TRAIL_MAX_MODULES];
static unsigned int g_trail_module_count = 0;
static HANDLE g_trail_file = INVALID_HANDLE_VALUE;
static DWORD g_trail_slot = TLS_OUT_OF_INDEXES;
static PVOID g_trail_handler = 0;
static volatile LONG g_trail_enabled = 0;
static volatile LONG g_trail_readers = 0;
static LONG g_closed = 0;
static volatile LONG g_trail_sequence = 0;
static volatile LONG g_trail_writing = 0;
static unsigned long g_trail_repeats = 0;
static DWORD g_trail_last_code = 0;
static uintptr_t g_trail_last_address = 0;
static LARGE_INTEGER g_trail_record_start;
static char g_trail_buffer[TRAIL_BUFFER_BYTES];
static int readable_region(const void *address, SIZE_T bytes)
{
	MEMORY_BASIC_INFORMATION info;
	uintptr_t cursor = (uintptr_t)address;
	uintptr_t end;
	uintptr_t previous;
	DWORD protection;
	if (address == 0 || bytes == 0 ||
			bytes > (SIZE_T)((uintptr_t)-1 - cursor)) {
		return 0;
	}
	end = cursor + bytes;
	while (cursor < end) {
		if (VirtualQuery((const void *)cursor, &info, sizeof(info)) !=
				sizeof(info) || info.State != MEM_COMMIT) {
			return 0;
		}
		if ((info.Protect & PAGE_GUARD) != 0) {
			return 0;
		}
		protection = info.Protect & 0xffU;
		if (protection != PAGE_READONLY &&
				protection != PAGE_READWRITE &&
				protection != PAGE_WRITECOPY &&
				protection != PAGE_EXECUTE_READ &&
				protection != PAGE_EXECUTE_READWRITE &&
				protection != PAGE_EXECUTE_WRITECOPY) {
			return 0;
		}
		if (info.RegionSize > (SIZE_T)((uintptr_t)-1 -
				(uintptr_t)info.BaseAddress)) {
			return 0;
		}
		previous = cursor;
		cursor = (uintptr_t)info.BaseAddress + info.RegionSize;
		if (cursor <= previous) {
			return 0;
		}
	}
	return 1;
}


static int trail_module_is_local(const WCHAR *name)
{
	return lstrcmpiW(name, L"msvcp140.dll") == 0 ||
		lstrcmpiW(name, L"vcruntime140.dll") == 0;
}


static void trail_copy_name(char *destination, const WCHAR *source)
{
	unsigned int index = 0;
	while (index + 1U < TRAIL_NAME_CHARS && source[index] != L'\0') {
		WCHAR value = source[index];
		destination[index] = (value >= 0x20 && value < 0x7f) ?
			(char)value : '?';
		++index;
	}
	destination[index] = '\0';
}


/* Snapshot the module table once, while the caller is a normal Python call.
 * Resolving names inside the handler would take the loader lock, which the
 * faulting thread may already own.
 */
static int capture_trail_modules(void)
{
	MODULEENTRY32W entry;
	HANDLE snapshot = INVALID_HANDLE_VALUE;
	unsigned int attempt;
	unsigned int count = 0;

	for (attempt = 0; attempt < 4U; ++attempt) {
		snapshot = CreateToolhelp32Snapshot(
			TH32CS_SNAPMODULE, GetCurrentProcessId());
		if (snapshot != INVALID_HANDLE_VALUE) {
			break;
		}
		if (GetLastError() != ERROR_BAD_LENGTH) {
			return 0;
		}
	}
	if (snapshot == INVALID_HANDLE_VALUE) {
		return 0;
	}
	ZeroMemory(&entry, sizeof(entry));
	entry.dwSize = (DWORD)sizeof(entry);
	if (Module32FirstW(snapshot, &entry)) {
		do {
			TrailModule *record = &g_trail_modules[count];
			record->base = (uintptr_t)entry.modBaseAddr;
			record->end = record->base + (uintptr_t)entry.modBaseSize;
			record->local = (entry.modBaseAddr == g_image_base) ||
				trail_module_is_local(entry.szModule);
			trail_copy_name(record->name, entry.szModule);
			++count;
			entry.dwSize = (DWORD)sizeof(entry);
		} while (count < TRAIL_MAX_MODULES &&
			Module32NextW(snapshot, &entry));
	}
	CloseHandle(snapshot);
	g_trail_module_count = count;
	return count != 0;
}


static const TrailModule *trail_module_for(uintptr_t address)
{
	unsigned int index;
	for (index = 0; index < g_trail_module_count; ++index) {
		const TrailModule *record = &g_trail_modules[index];
		if (address >= record->base && address < record->end) {
			return record;
		}
	}
	return 0;
}


static unsigned int trail_put(unsigned int used, const char *text)
{
	while (*text != '\0' && used + 1U < TRAIL_BUFFER_BYTES) {
		g_trail_buffer[used++] = *text++;
	}
	return used;
}


static unsigned int trail_put_hex32(unsigned int used, unsigned long value)
{
	static const char DIGITS[] = "0123456789ABCDEF";
	unsigned int shift = 32U;
	while (shift > 0U && used + 1U < TRAIL_BUFFER_BYTES) {
		shift -= 4U;
		g_trail_buffer[used++] = DIGITS[(value >> shift) & 0xfU];
	}
	return used;
}


static unsigned int trail_put_uint(unsigned int used, unsigned long value,
		unsigned int width)
{
	char scratch[12];
	unsigned int count = 0;
	do {
		scratch[count++] = (char)('0' + (int)(value % 10UL));
		value /= 10UL;
	} while (value != 0UL && count < sizeof(scratch));
	while (width > count && used + 1U < TRAIL_BUFFER_BYTES) {
		g_trail_buffer[used++] = '0';
		--width;
	}
	while (count > 0U && used + 1U < TRAIL_BUFFER_BYTES) {
		g_trail_buffer[used++] = scratch[--count];
	}
	return used;
}


static unsigned int trail_put_address(unsigned int used, uintptr_t address)
{
	const TrailModule *module = trail_module_for(address);
	used = trail_put(used, "0x");
	used = trail_put_hex32(used, (unsigned long)address);
	if (module != 0) {
		used = trail_put(used, " (");
		used = trail_put(used, module->name);
		used = trail_put(used, "+0x");
		used = trail_put_hex32(used,
			(unsigned long)(address - module->base));
		used = trail_put(used, ")");
	}
	return used;
}


/* A C++ throw carries its ThrowInfo in the third parameter. Only exceptions
 * thrown by the exact client image or its C++ runtime are recorded; the
 * Chinese IME, the display driver and the WGC client all throw and catch
 * their own C++ exceptions during normal play.
 */
static int trail_cxx_is_local(const EXCEPTION_RECORD *record)
{
	const TrailModule *module;
	uintptr_t throw_info;
	if (record->NumberParameters < 3U ||
			(uintptr_t)record->ExceptionInformation[0] != 0x19930520U) {
		return 0;
	}
	throw_info = (uintptr_t)record->ExceptionInformation[2];
	if (throw_info == 0) {
		return 0;
	}
	module = trail_module_for(throw_info);
	return module != 0 && module->local;
}


static int trail_is_recorded(const EXCEPTION_RECORD *record)
{
	switch (record->ExceptionCode) {
	case 0xc0000005UL: /* access violation */
	case 0xc0000006UL: /* in-page error */
	case 0xc000001dUL: /* illegal instruction */
	case 0xc0000094UL: /* integer divide by zero */
	case 0xc0000096UL: /* privileged instruction */
	case 0xc00000fdUL: /* stack overflow */
	case 0xc0000374UL: /* heap corruption */
	case 0xc0000409UL: /* security check failure */
		return 1;
	case 0xe06d7363UL:
		return trail_cxx_is_local(record);
	default:
		return 0;
	}
}


/* Best effort only: a std::exception keeps its message pointer at +4. The
 * text is emitted only when every byte up to its terminator is printable, so
 * an object of another shape produces no field instead of an invented one.
 */
static int trail_exception_text(uintptr_t object, char *destination)
{
	const char *text;
	unsigned int span = TRAIL_TEXT_CHARS;
	unsigned int index;
	if (!readable_region((const void *)object, 8U)) {
		return 0;
	}
	if (!ReadProcessMemory(GetCurrentProcess(), (const void *)(object + 4U),
			&text, sizeof(text), 0)) {
		return 0;
	}
	if (text == 0) {
		return 0;
	}
	while (span > 8U && !readable_region(text, span)) {
		span /= 2U;
	}
	if (span <= 8U && !readable_region(text, span)) {
		return 0;
	}
	if (!ReadProcessMemory(GetCurrentProcess(), text, destination, span, 0)) {
		return 0;
	}
	for (index = 0; index + 1U < span; ++index) {
		char value = destination[index];
		if (value == '\0') {
			destination[index] = '\0';
			return index != 0U;
		}
		if (value < 0x20 || value >= 0x7f) {
			return 0;
		}
		destination[index] = value;
	}
	return 0;
}


/* The destination is opened once, while this is still a normal Python call.
 * Opening it from the handler would allocate on the process heap, which the
 * faulting thread may already hold locked - a heap corruption report is one
 * of the faults worth recording.
 */
static void trail_flush(unsigned int used)
{
	DWORD written = 0;
	if (used == 0U || g_trail_file == INVALID_HANDLE_VALUE) {
		return;
	}
	WriteFile(g_trail_file, g_trail_buffer, (DWORD)used, &written, 0);
}


static unsigned int trail_put_time(unsigned int used)
{
	SYSTEMTIME now;
	GetSystemTime(&now);
	used = trail_put_uint(used, now.wYear, 4U);
	used = trail_put(used, "-");
	used = trail_put_uint(used, now.wMonth, 2U);
	used = trail_put(used, "-");
	used = trail_put_uint(used, now.wDay, 2U);
	used = trail_put(used, "T");
	used = trail_put_uint(used, now.wHour, 2U);
	used = trail_put(used, ":");
	used = trail_put_uint(used, now.wMinute, 2U);
	used = trail_put(used, ":");
	used = trail_put_uint(used, now.wSecond, 2U);
	used = trail_put(used, ".");
	used = trail_put_uint(used, now.wMilliseconds, 3U);
	return trail_put(used, "Z");
}


static LONG record_exception(PEXCEPTION_POINTERS pointers)
{
	DWORD saved_error = GetLastError();
	const EXCEPTION_RECORD *record;
	const CONTEXT *context;
	char text[TRAIL_TEXT_CHARS];
	unsigned int used = 0;
	unsigned int frame;
	uintptr_t cursor;
	LONG sequence;
	int replace_record;
	LARGE_INTEGER zero;

	if (pointers == 0 || g_trail_slot == TLS_OUT_OF_INDEXES) {
		SetLastError(saved_error);
		return EXCEPTION_CONTINUE_SEARCH;
	}
	record = pointers->ExceptionRecord;
	context = pointers->ContextRecord;
	if (record == 0 || context == 0 || !trail_is_recorded(record) ||
			TlsGetValue(g_trail_slot) != 0) {
		SetLastError(saved_error);
		return EXCEPTION_CONTINUE_SEARCH;
	}
	/* One writer at a time, and never recursively: the shared buffer keeps
	 * this handler off the faulting stack, which a stack overflow has
	 * already exhausted, and serialises the repeat and sequence state.
	 */
	if (InterlockedCompareExchange(&g_trail_writing, 1, 0) != 0) {
		SetLastError(saved_error);
		return EXCEPTION_CONTINUE_SEARCH;
	}
	TlsSetValue(g_trail_slot, (LPVOID)(uintptr_t)1);

	/* RaiseException is shared by different C++ throws. Even a repeated
	 * code/address must refresh the final record's message and context.
	 * Keep the first 511 slots and overwrite the last slot after the budget
	 * fills, so bounded storage never disables the recorder before a crash.
	 */
	replace_record = g_trail_sequence != 0 &&
		record->ExceptionCode == g_trail_last_code &&
		(uintptr_t)record->ExceptionAddress == g_trail_last_address;
	if (replace_record) {
		++g_trail_repeats;
	} else {
		g_trail_repeats = 0;
		if (g_trail_sequence < TRAIL_MAX_RECORDS) {
			++g_trail_sequence;
		} else {
			replace_record = 1;
		}
	}
	zero.QuadPart = 0;
	if (!(replace_record ?
			SetFilePointerEx(g_trail_file, g_trail_record_start, 0, FILE_BEGIN) :
			SetFilePointerEx(g_trail_file, zero, &g_trail_record_start,
				FILE_CURRENT))) {
		TlsSetValue(g_trail_slot, 0);
		InterlockedExchange(&g_trail_writing, 0);
		SetLastError(saved_error);
		return EXCEPTION_CONTINUE_SEARCH;
	}
	sequence = g_trail_sequence;
	g_trail_last_code = record->ExceptionCode;
	g_trail_last_address = (uintptr_t)record->ExceptionAddress;

	used = trail_put(used, "EXC seq=");
	used = trail_put_uint(used, (unsigned long)sequence, 0U);
	used = trail_put(used, " time=");
	used = trail_put_time(used);
	used = trail_put(used, " tid=");
	used = trail_put_uint(used, GetCurrentThreadId(), 0U);
	used = trail_put(used, " code=0x");
	used = trail_put_hex32(used, record->ExceptionCode);
	used = trail_put(used, " flags=0x");
	used = trail_put_hex32(used, record->ExceptionFlags);
	used = trail_put(used, " at=");
	used = trail_put_address(used, (uintptr_t)record->ExceptionAddress);
	if (g_trail_repeats != 0UL) {
		used = trail_put(used, " repeats=");
		used = trail_put_uint(used, g_trail_repeats, 0U);
	}
	used = trail_put(used, "\r\n");

	if (record->ExceptionCode == 0xc0000005UL &&
			record->NumberParameters >= 2U) {
		used = trail_put(used, "EXC access kind=");
		used = trail_put_uint(used,
			(unsigned long)record->ExceptionInformation[0], 0U);
		used = trail_put(used, " address=0x");
		used = trail_put_hex32(used,
			(unsigned long)record->ExceptionInformation[1]);
		used = trail_put(used, "\r\n");
	} else if (record->ExceptionCode == 0xe06d7363UL) {
		uintptr_t object = (uintptr_t)record->ExceptionInformation[1];
		used = trail_put(used, "EXC cxx throwinfo=");
		used = trail_put_address(used,
			(uintptr_t)record->ExceptionInformation[2]);
		used = trail_put(used, " object=0x");
		used = trail_put_hex32(used, (unsigned long)object);
		if (trail_exception_text(object, text)) {
			used = trail_put(used, " what=\"");
			used = trail_put(used, text);
			used = trail_put(used, "\"");
		}
		used = trail_put(used, "\r\n");
	}

	used = trail_put(used, "EXC reg eip=0x");
	used = trail_put_hex32(used, context->Eip);
	used = trail_put(used, " esp=0x");
	used = trail_put_hex32(used, context->Esp);
	used = trail_put(used, " ebp=0x");
	used = trail_put_hex32(used, context->Ebp);
	used = trail_put(used, " eax=0x");
	used = trail_put_hex32(used, context->Eax);
	used = trail_put(used, " ebx=0x");
	used = trail_put_hex32(used, context->Ebx);
	used = trail_put(used, " ecx=0x");
	used = trail_put_hex32(used, context->Ecx);
	used = trail_put(used, " edx=0x");
	used = trail_put_hex32(used, context->Edx);
	used = trail_put(used, " esi=0x");
	used = trail_put_hex32(used, context->Esi);
	used = trail_put(used, " edi=0x");
	used = trail_put_hex32(used, context->Edi);
	used = trail_put(used, " efl=0x");
	used = trail_put_hex32(used, context->EFlags);
	used = trail_put(used, "\r\n");

	cursor = (uintptr_t)context->Ebp;
	for (frame = 0; frame < TRAIL_MAX_FRAMES; ++frame) {
		uintptr_t next;
		uintptr_t caller;
		uintptr_t pair[2];
		if ((cursor & 3U) != 0U ||
				!readable_region((const void *)cursor, 8U)) {
			break;
		}
		if (!ReadProcessMemory(GetCurrentProcess(), (const void *)cursor,
				pair, sizeof(pair), 0)) {
			break;
		}
		next = pair[0];
		caller = pair[1];
		if (caller == 0) {
			break;
		}
		used = trail_put(used, "EXC frame ");
		used = trail_put_uint(used, frame, 2U);
		used = trail_put(used, " ");
		used = trail_put_address(used, caller);
		used = trail_put(used, "\r\n");
		if (next <= cursor) {
			break;
		}
		cursor = next;
	}
	used = trail_put(used, "EXC end seq=");
	used = trail_put_uint(used, (unsigned long)sequence, 0U);
	used = trail_put(used, "\r\n");
	trail_flush(used);
	if (replace_record) {
		SetEndOfFile(g_trail_file);
	}

	TlsSetValue(g_trail_slot, 0);
	InterlockedExchange(&g_trail_writing, 0);
	SetLastError(saved_error);
	return EXCEPTION_CONTINUE_SEARCH;
}


static LONG CALLBACK exception_trail_handler(PEXCEPTION_POINTERS pointers)
{
    DWORD saved = GetLastError();
    InterlockedIncrement(&g_trail_readers);
    LONG result = EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedCompareExchange(&g_trail_enabled, 0, 0))
        result = record_exception(pointers);
    InterlockedDecrement(&g_trail_readers);
    SetLastError(saved);
    return result;
}


static void write_trail_header(void)
{
	unsigned int used = 0;
	used = trail_put(used, "EXC session time=");
	used = trail_put_time(used);
	used = trail_put(used, " pid=");
	used = trail_put_uint(used, GetCurrentProcessId(), 0U);
	used = trail_put(used, " image=0x");
	used = trail_put_hex32(used, (unsigned long)(uintptr_t)g_image_base);
	used = trail_put(used, " modules=");
	used = trail_put_uint(used, g_trail_module_count, 0U);
	used = trail_put(used, " limit=");
	used = trail_put_uint(used, (unsigned long)TRAIL_MAX_RECORDS, 0U);
	used = trail_put(used, "\r\n");
	trail_flush(used);
}


static void close_trail_file(void)
{
	if (g_trail_file != INVALID_HANDLE_VALUE) {
		CloseHandle(g_trail_file);
		g_trail_file = INVALID_HANDLE_VALUE;
	}
}


static long install_exception_trail_internal(void)
{
	WCHAR path[MAX_PATH];
	DWORD length;
	LARGE_INTEGER zero;

	if (g_closed) {
		return TRAIL_STATUS_HANDLER_REFUSED;
	}
	if (g_trail_handler != 0) {
		return 0;
	}
	length = GetEnvironmentVariableW(
		EXCEPTION_TRAIL_PATH_ENV, path, MAX_PATH);
	if (length == 0) {
		return TRAIL_STATUS_NOT_CONFIGURED;
	}
	if (length >= MAX_PATH) {
		return TRAIL_STATUS_PATH_INVALID;
	}
	g_image_base = (unsigned char *)GetModuleHandleW(0);
	if (!capture_trail_modules()) {
		return TRAIL_STATUS_MODULES_UNAVAILABLE;
	}
	if (g_trail_slot == TLS_OUT_OF_INDEXES) {
		g_trail_slot = TlsAlloc();
		if (g_trail_slot == TLS_OUT_OF_INDEXES) {
			return TRAIL_STATUS_TLS_UNAVAILABLE;
		}
	}
	/* Sharing the file keeps it readable while the process still runs, and
	 * a worker that restarts inside one launcher session appends instead of
	 * discarding the records that explain why it restarted.
	 */
	g_trail_file = CreateFileW(path, GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
		OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (g_trail_file == INVALID_HANDLE_VALUE) {
		return TRAIL_STATUS_PATH_INVALID;
	}
	zero.QuadPart = 0;
	if (!SetFilePointerEx(g_trail_file, zero, 0, FILE_END)) {
		close_trail_file();
		return TRAIL_STATUS_PATH_INVALID;
	}
	/* Finish using the shared buffer before another thread can enter. */
	write_trail_header();
	InterlockedExchange(&g_trail_enabled, 1);
	g_trail_handler =
		AddVectoredExceptionHandler(1UL, exception_trail_handler);
	if (g_trail_handler == 0) {
		InterlockedExchange(&g_trail_enabled, 0);
		close_trail_file();
		return TRAIL_STATUS_HANDLER_REFUSED;
	}
	return 0;
}


static PyObject *install_exception_trail(PyObject *unused_self,
		PyObject *unused_args)
{
	(void)unused_self;
	(void)unused_args;
	return PyInt_FromLong(install_exception_trail_internal());
}



// ShowWindow/SetWindowPlacement may synchronously dispatch application code.
// Reject nested operations while the published record set is being changed.
struct WindowOperation {
    bool owned;
    WindowOperation() : owned(InterlockedCompareExchange(&g_window_busy, 1, 0) == 0) {}
    ~WindowOperation() { if (owned) InterlockedExchange(&g_window_busy, 0); }
};

static bool owns_window(const HiddenWindow &record)
{
    DWORD pid = 0;
    return IsWindow(record.handle) &&
        GetWindowThreadProcessId(record.handle, &pid) != 0 &&
        pid == GetCurrentProcessId() &&
        GetPropW(record.handle, WINDOW_GENERATION) == record.generation;
}

static int hidden_window_index(HWND handle)
{
    for (unsigned int i = 0; i < g_hidden_window_count; ++i)
        if (g_hidden_windows[i].handle == handle && owns_window(g_hidden_windows[i]))
            return (int)i;
    return -1;
}

static DWORD window_error()
{
    DWORD error = GetLastError();
    return error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error;
}

static bool restore_window(HiddenWindow &record, DWORD &error)
{
    if (!owns_window(record)) return true;
    if (wot::stockSimulationWorker()) {
        // A simulation worker's windows stay hidden for the whole process.
        RemovePropW(record.handle, WINDOW_GENERATION);
        return true;
    }
    SetLastError(ERROR_SUCCESS);
    if (!SetWindowPlacement(record.handle, &record.placement)) {
        error = window_error();
        return false;
    }
    if (owns_window(record)) {
        // Retire our identity before dispatching synchronous window messages.
        RemovePropW(record.handle, WINDOW_GENERATION);
        ShowWindow(record.handle, record.placement.showCmd);
    }
    return true;
}

static void restore_hidden_range(unsigned int first)
{
    while (g_hidden_window_count > first) {
        HiddenWindow record = g_hidden_windows[--g_hidden_window_count];
        DWORD ignored = ERROR_SUCCESS;
        if (!restore_window(record, ignored)) {
            // Rollback failure must remain recoverable by a later show call.
            g_hidden_windows[g_hidden_window_count++] = record;
            break;
        }
    }
}

static BOOL CALLBACK hide_window_callback(HWND handle, LPARAM parameter)
{
    HideContext *context = (HideContext *)parameter;
    DWORD pid = 0;
    GetWindowThreadProcessId(handle, &pid);
    if (pid != context->process_id || !IsWindowVisible(handle) || hidden_window_index(handle) >= 0)
        return TRUE;
    if (g_hidden_window_count >= MAX_HIDDEN_WINDOWS) {
        context->error_code = ERROR_INSUFFICIENT_BUFFER;
        return FALSE;
    }
    HiddenWindow record = {};
    record.handle = handle;
    record.placement.length = sizeof(record.placement);
    if (!GetWindowPlacement(handle, &record.placement)) {
        context->error_code = window_error();
        return FALSE;
    }
    if (GetPropW(handle, WINDOW_GENERATION)) {
        context->error_code = ERROR_ALREADY_EXISTS;
        return FALSE;
    }
    do { record.generation = (HANDLE)(uintptr_t)(DWORD)InterlockedIncrement(&g_window_generation); }
    while (!record.generation);
    if (!SetPropW(handle, WINDOW_GENERATION, record.generation)) {
        context->error_code = window_error();
        return FALSE;
    }
    g_hidden_windows[g_hidden_window_count++] = record;
    ShowWindow(handle, SW_HIDE);
    return TRUE;
}

static PyObject *hide_process_windows(PyObject *, PyObject *)
{
    WindowOperation operation;
    if (!operation.owned) return PyInt_FromLong(-ERROR_BUSY);
    if (g_closed) return PyInt_FromLong(-ERROR_OPERATION_ABORTED);
    // Drop dead/replaced identities before admitting newly visible windows.
    unsigned int count = 0;
    for (unsigned int i = 0; i < g_hidden_window_count; ++i)
        if (owns_window(g_hidden_windows[i])) g_hidden_windows[count++] = g_hidden_windows[i];
    g_hidden_window_count = count;
    HideContext context = { GetCurrentProcessId(), ERROR_SUCCESS, count };
    SetLastError(ERROR_SUCCESS);
    if (!EnumWindows(hide_window_callback, (LPARAM)&context)) {
        if (!context.error_code) context.error_code = window_error();
        restore_hidden_range(count);
        return PyInt_FromLong(-(long)context.error_code);
    }
    return PyInt_FromLong((long)(g_hidden_window_count - count));
}

static PyObject *show_process_windows(PyObject *, PyObject *)
{
    WindowOperation operation;
    if (!operation.owned) return PyInt_FromLong(-ERROR_BUSY);
    HiddenWindow pending[MAX_HIDDEN_WINDOWS];
    unsigned int count = g_hidden_window_count;
    for (unsigned int i = 0; i < count; ++i) pending[i] = g_hidden_windows[i];
    g_hidden_window_count = 0;
    unsigned int restored = 0;
    DWORD first_error = ERROR_SUCCESS;
    for (unsigned int i = 0; i < count; ++i) {
        const bool live = owns_window(pending[i]);
        DWORD error = ERROR_SUCCESS;
        if (!restore_window(pending[i], error)) {
            g_hidden_windows[g_hidden_window_count++] = pending[i];
            if (!first_error) first_error = error;
        } else if (live && !wot::stockSimulationWorker()) ++restored;
    }
    return PyInt_FromLong(first_error ? -(long)first_error : (long)restored);
}

static BOOL CALLBACK worker_window_callback(HWND handle, LPARAM parameter)
{
    WorkerWindowContext *context = (WorkerWindowContext *)parameter;
    DWORD pid = 0;
    GetWindowThreadProcessId(handle, &pid);
    if (pid != context->process_id) return TRUE;
    if (IsWindowVisible(handle)) ++context->visible;
    if (GetPropW(handle, wot::stockWorkerWindowProperty()) == (HANDLE)handle) ++context->marked;
    return TRUE;
}

// Proves, without hiding anything, that this simulation worker's own window
// exists and that no window of this process is visible.
static PyObject *confirm_hidden_worker_window(PyObject *, PyObject *)
{
    WindowOperation operation;
    if (!operation.owned) return PyInt_FromLong(-ERROR_BUSY);
    if (g_closed) return PyInt_FromLong(-ERROR_OPERATION_ABORTED);
    if (!wot::stockSimulationWorker()) return PyInt_FromLong(-ERROR_NOT_SUPPORTED);
    WorkerWindowContext context = { GetCurrentProcessId(), 0, 0 };
    SetLastError(ERROR_SUCCESS);
    if (!EnumWindows(worker_window_callback, (LPARAM)&context))
        return PyInt_FromLong(-(long)window_error());
    if (context.marked != 1) return PyInt_FromLong(-ERROR_INVALID_WINDOW_HANDLE);
    if (context.visible) return PyInt_FromLong(-ERROR_INVALID_STATE);
    return PyInt_FromLong(1);
}

static PyMethodDef methods[] = {
    { "install_exception_trail", install_exception_trail, METH_NOARGS,
      "Record first-chance faults without changing their disposition." },
    { "hide_process_windows", hide_process_windows, METH_NOARGS,
      "Hide visible top-level windows owned by this process." },
    { "show_process_windows", show_process_windows, METH_NOARGS,
      "Restore surviving windows previously hidden by this module." },
    { "confirm_hidden_worker_window", confirm_hidden_worker_window, METH_NOARGS,
      "Confirm that this simulation worker's own window was never shown." },
    { NULL, NULL, 0, NULL }
};
} // namespace

const PyMethodDef *stockOfflinePlatformMethods() { return methods; }

void finiStockOfflinePlatform()
{
    g_closed = 1;
    InterlockedExchange(&g_trail_enabled, 0);
    if (g_trail_handler) {
        RemoveVectoredExceptionHandler(g_trail_handler);
        g_trail_handler = NULL;
    }
    // Already admitted callbacks finish before resources retire. Late callbacks
    // only inspect process-lived atomics and return CONTINUE_SEARCH.
    while (InterlockedCompareExchange(&g_trail_readers, 0, 0)) Sleep(0);
    close_trail_file();
    if (g_trail_slot != TLS_OUT_OF_INDEXES) {
        TlsFree(g_trail_slot);
        g_trail_slot = TLS_OUT_OF_INDEXES;
    }
    WindowOperation operation;
    if (operation.owned) {
        // The application is exiting. Retire only our markers; showing a
        // hidden worker here would flash a window during normal teardown.
        while (g_hidden_window_count) {
            const HiddenWindow &record = g_hidden_windows[--g_hidden_window_count];
            if (owns_window(record)) RemovePropW(record.handle, WINDOW_GENERATION);
        }
    }
}
