"""Compiler/runtime flags for the portable native test executables and DLLs."""
from functools import lru_cache
from pathlib import Path
import subprocess
import sys
import tempfile

SANITIZERS = ('-fsanitize=address,undefined',)


@lru_cache(maxsize=None)
def _windows_runtime_flags(compiler):
    target = subprocess.run([compiler, '-dumpmachine'], capture_output=True, text=True)
    # Python's Windows DLL loader does not search the compiler's PATH entries.
    # Include winpthreads, which static libstdc++ can still import separately.
    # Only test artifacts link these runtimes statically; production is unchanged.
    if target.returncode == 0 and 'mingw' in target.stdout.lower():
        return ('-static', '-static-libgcc', '-static-libstdc++')
    return ()


@lru_cache(maxsize=None)
def _sanitizers_available(compiler, flags):
    # Probe only an empty program. A real test build is never retried or treated
    # as a capability probe, so source errors cannot disable instrumentation.
    with tempfile.TemporaryDirectory(prefix='native-test-sanitizer-') as folder:
        root = Path(folder)
        source, binary = root / 'probe.cpp', root / 'probe.exe'
        source.write_text('int main() { return 0; }\n', encoding='utf-8')
        command = [compiler, '-std=c++14', *flags, str(source), '-o', str(binary)]
        compiled = subprocess.run(command, capture_output=True, text=True)
        stage, result = 'compile', compiled
        if compiled.returncode == 0:
            stage = 'execute'
            try:
                result = subprocess.run([str(binary)], capture_output=True, text=True)
            except OSError as error:
                print('Native test sanitizer probe unavailable for %s (execute): %s' %
                      (compiler, error), file=sys.stderr)
                return False
        if result.returncode:
            print('Native test sanitizer probe unavailable for %s (%s exit %s):\n%s%s' %
                  (compiler, stage, result.returncode, result.stdout, result.stderr), file=sys.stderr)
            return False
        return True


def native_test_compile_flags(compiler='c++', sanitise=True):
    if sys.platform != 'win32':
        return list(SANITIZERS) if sanitise else []
    runtime = _windows_runtime_flags(compiler)
    flags = (*runtime, *SANITIZERS)
    if sanitise and _sanitizers_available(compiler, flags):
        return list(flags)
    return list(runtime)
