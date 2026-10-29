#!/usr/bin/env python3
"""Avoid dynamic profiler metadata and string locking while recording is off."""
import argparse
from pathlib import Path

SCRIPT = 'lib/pyscript/script.cpp'
PROFILER = 'lib/cstdmf/profiler.hpp'
QUERY = '''\tbool recordingActive() const
\t{
\t\t// Use the same volatile recording state as addEntry, excluding pending activation.
\t\treturn isActive_;
\t}

'''
SCRIPT_BEFORE = '''#if ENABLE_PROFILER
\t\t\tchar profilerString[1024];
\t\t\tresolvePythonModuleAndFunctionNames( pFunction, profilerString,
\t\t\t\t\t\t\t\t\t\t\tARRAY_SIZE( profilerString ) );
\t\t\tPROFILER_SCOPED_DYNAMIC_STRING_CATEGORY( profilerString,
\t\t\t\tProfiler::CATEGORY_PYTHON );
#endif // ENABLE_PROFILER

\t\t\tpResult = PyObject_CallObject( pFunction, pArgs );'''
SCRIPT_AFTER = '''#if ENABLE_PROFILER
\t\t\tif (g_profiler.recordingActive())
\t\t\t{
\t\t\t\tchar profilerString[1024];
\t\t\t\tresolvePythonModuleAndFunctionNames( pFunction, profilerString,
\t\t\t\t\tARRAY_SIZE( profilerString ) );
\t\t\t\tPROFILER_SCOPED_DYNAMIC_STRING_CATEGORY( profilerString,
\t\t\t\t\tProfiler::CATEGORY_PYTHON );
\t\t\t\tpResult = PyObject_CallObject( pFunction, pArgs );
\t\t\t}
\t\t\telse
#endif // ENABLE_PROFILER
\t\t\t{
\t\t\t\tpResult = PyObject_CallObject( pFunction, pArgs );
\t\t\t}'''
EDITS = {
    SCRIPT: ((SCRIPT_BEFORE, SCRIPT_AFTER),),
    PROFILER: (
        ('\tbool watcherEnabled() const\n', QUERY + '\tbool watcherEnabled() const\n'),
        ('''\tinline ScopedDynamicStringProfiler( const char* dynamicText,
\t\tProfiler::EventCategory category = Profiler::CATEGORY_CPP )
\t{
\t\t// This code could be called from multiple threads''',
         '''\tinline ScopedDynamicStringProfiler( const char* dynamicText,
\t\tProfiler::EventCategory category = Profiler::CATEGORY_CPP ) :
\t\ttext_( NULL ), category_( category )
\t{
\t\tif (!g_profiler.recordingActive()) return;
\t\t// This code could be called from multiple threads'''),
        ('''\tinline ~ScopedDynamicStringProfiler()
\t{
\t\tg_profiler.addEntry( text_, Profiler::EVENT_END, 0, category_ );
\t}''',
         '''\tinline ~ScopedDynamicStringProfiler()
\t{
\t\t// A scope skipped before activation must never emit an unmatched END.
\t\tif (text_) g_profiler.addEntry( text_, Profiler::EVENT_END, 0, category_ );
\t}'''),
    ),
}


def patch_source(source, relative):
    edits = EDITS[relative]
    if all(source.count(after) == 1 for before, after in edits):
        if any(source.count(before) != sum(after.count(before) for _, after in edits)
               for before, _ in edits):
            raise ValueError('Duplicated original inactive profiler seam: ' + relative)
        return source
    if any(source.count(before) != 1 or after in source for before, after in edits):
        raise ValueError('Inactive profiler seam is partial, duplicated, or changed: ' + relative)
    for before, after in edits:
        source = source.replace(before, after, 1)
    return source


def patch(root):
    prepared = []
    for relative in EDITS:
        path = Path(root) / relative
        data = path.read_bytes()
        bom = b'\xef\xbb\xbf' if data.startswith(b'\xef\xbb\xbf') else b''
        source = data[len(bom):].decode('utf-8')
        crlf = source.count('\r\n')
        if (crlf and (crlf != source.count('\n') or crlf != source.count('\r'))) or (not crlf and '\r' in source):
            raise ValueError('Unsupported line endings: ' + relative)
        newline = '\r\n' if crlf else '\n'
        updated = patch_source(source.replace('\r\n', '\n'), relative)
        replacement = bom + updated.replace('\n', newline).encode('utf-8')
        prepared.append((path, data, replacement))
    # Validate both files before mutating either SDK seam.
    for path, original, replacement in prepared:
        if replacement != original:
            path.write_bytes(replacement)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
