"""Real CPython 2.7 checks of the production BigWorldIPC.Mutex binding.

Executed by stock_bigworld_ipc_host.cpp. Win32 is recorded, not executed.
"""
import gc
import sys

import BigWorldIPC
import Host

ERROR_ALREADY_EXISTS, ERROR_INVALID_HANDLE, ERROR_NOT_OWNER = 183, 6, 288
WAIT_TIMEOUT = 0x102


def expect(kind, function, message=None):
    try:
        function()
    except kind as error:
        if message is not None:
            assert str(error) == message, (str(error), message)
        return error
    raise AssertionError('%s was not raised' % kind.__name__)


def results(created=0x100, wait=0, last=0, released=1, closed=1, formatted=1):
    Host.setResults(created, wait, last, released, closed, formatted)


def calls():
    return [(c[0],) + tuple(int(v) for v in c[1]) + tuple(c[2:]) for c in Host.calls()]


# Module registration 005a2b40: the PyMutex type itself is BigWorldIPC.Mutex.
assert [name for name in dir(BigWorldIPC) if not name.startswith('__')] == ['Mutex']
expect(TypeError, lambda: BigWorldIPC.Mutex(1), 'PyMutex() takes no arguments.')
mutex = BigWorldIPC.Mutex(ignored=True)
assert type(mutex) is BigWorldIPC.Mutex
assert (type(mutex).__name__, type(mutex).__module__) == ('Mutex', 'BigWorldIPC')
assert mutex.__methods__[:4] == ['create', 'close', 'acquire', 'release']
assert mutex.__members__ == []
assert calls() == []

# Argument contracts (exact 0068e100 texts); keywords are ignored like the original.
expect(TypeError, lambda: mutex.create(), 'create() expects 1 argument of type BW::string')
expect(TypeError, lambda: mutex.create('a', 'b'), 'create() expects 1 argument of type BW::string')
expect(TypeError, lambda: mutex.create(1), '() argument 1 must be set to a string.')
expect(TypeError, lambda: mutex.acquire(), 'acquire() expects 1 argument of type float')
expect(TypeError, lambda: mutex.acquire('1'))
expect(TypeError, lambda: mutex.release(1), 'release() takes no arguments.')
expect(TypeError, lambda: mutex.close(None), 'close() takes no arguments.')
assert calls() == []

# create: close the previous handle, then CreateMutexA(NULL, FALSE, name).
results(created=0x100)
assert mutex.create('wot_ipc_test', unused=1) is None
assert calls() == [('CreateMutexA', 0, 0, 'wot_ipc_test')]
results(created=0x104, last=ERROR_ALREADY_EXISTS)
assert mutex.create(u'\u6c34') is None
assert calls() == [('CloseHandle', 0x100), ('CreateMutexA', 0, 0, '\xe6\xb0\xb4')]
results(created=0, last=5)
expect(SystemError, lambda: mutex.create('denied'), 'PyMutex.create: system message 5\r\n')
assert calls() == [('CloseHandle', 0x104), ('CreateMutexA', 0, 0, 'denied'), ('GetLastError',),
                   ('FormatMessageA', 0x1000, 5, 0, 0x400)]
# No last error, or no system text: the original reports nothing.
results(created=0, last=0)
assert mutex.create('quiet') is None
assert calls() == [('CreateMutexA', 0, 0, 'quiet'), ('GetLastError',)]
results(created=0, last=5, formatted=0)
assert mutex.create('quiet') is None
assert [c[0] for c in calls()] == ['CreateMutexA', 'GetLastError', 'FormatMessageA']

# acquire: float seconds -> low 32 bits of trunc(double(f) * 1000).
results(created=0x200)
mutex.create('wot_ipc_test')
calls()
for value, milliseconds in ((0, 0), (0.5, 500), (1.0005, 1000), (-1., 0xfffffc18),
                            (4294967.5, 204), (float('inf'), 0),
                            (float('nan'), 0), (1e30, 0), (-0.0009, 0)):
    assert Host.milliseconds(value) == milliseconds, (value, Host.milliseconds(value), milliseconds)
for wait, outcome in ((0, True), (0x80, True), (WAIT_TIMEOUT, False), (0xc0, False)):
    results(created=0x200, wait=wait)
    assert mutex.acquire(0.25) is outcome
    assert calls() == [('WaitForSingleObject', 0x200, 250)]
results(created=0x200, wait=0xffffffff)
expect(SystemError, lambda: mutex.acquire(1), "PyMutex.acquire: Can't acquire mutex")
assert calls() == [('WaitForSingleObject', 0x200, 1000)]

# release / close: errors carry the system text; a closed handle is NULL.
results(created=0x200)
assert mutex.release() is None and calls() == [('ReleaseMutex', 0x200)]
results(created=0x200, released=0, last=ERROR_NOT_OWNER)
expect(SystemError, lambda: mutex.release(), 'PyMutex.release: system message 288\r\n')
assert calls() == [('ReleaseMutex', 0x200), ('GetLastError',), ('FormatMessageA', 0x1000, 288, 0, 0x400)]
results(created=0x200)
assert mutex.close() is None and calls() == [('CloseHandle', 0x200)]
assert mutex.close() is None and calls() == []
results(wait=0xffffffff, released=0, last=ERROR_INVALID_HANDLE)
expect(SystemError, lambda: mutex.acquire(0), "PyMutex.acquire: Can't acquire mutex")
expect(SystemError, lambda: mutex.release(), 'PyMutex.release: system message 6\r\n')
assert calls()[:2] == [('WaitForSingleObject', 0, 0), ('ReleaseMutex', 0)]
results(created=0x300, closed=0, last=ERROR_INVALID_HANDLE)
mutex.create('again')
calls()
expect(SystemError, lambda: mutex.close(), 'PyMutex.close: system message 6\r\n')
assert [c[0] for c in calls()] == ['CloseHandle', 'GetLastError', 'FormatMessageA']
assert mutex.close() is None and calls() == []
# A failed CloseHandle during create is raised, but the new handle is kept.
results(created=0x304)
mutex.create('kept')
results(created=0x308, closed=0, last=ERROR_INVALID_HANDLE)
expect(SystemError, lambda: mutex.create('next'), 'PyMutex.close: system message 6\r\n')
results(created=0x308, wait=0)
calls()
assert mutex.acquire(0) is True and calls() == [('WaitForSingleObject', 0x308, 0)]

# Destruction closes the handle without releasing it and leaves no error.
results(created=0x400)
owned = BigWorldIPC.Mutex()
owned.create('owned')
owned.acquire(1)
calls()
del owned
gc.collect()
assert calls() == [('CloseHandle', 0x400)]
results(created=0x500, closed=0, last=ERROR_INVALID_HANDLE)
broken = BigWorldIPC.Mutex()
broken.create('broken')
calls()
del broken
assert sys.exc_info() == (None, None, None)
assert [c[0] for c in calls()] == ['CloseHandle', 'GetLastError']
print('BigWorldIPC.Mutex consumer checks passed')
