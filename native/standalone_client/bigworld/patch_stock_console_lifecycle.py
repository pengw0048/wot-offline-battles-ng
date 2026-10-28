"""Detach the Python console's output hook before the script runtime retires."""
from pathlib import Path
import sys


def replace_once(text, old, new, label):
    if new in text:
        return text
    if text.count(old) != 1:
        raise RuntimeError('Unexpected %s source' % label)
    return text.replace(old, new, 1)


def patch(root):
    root = Path(root)
    changes = {
        'lib/romp/console.hpp': (
            '\tPythonConsole();\n\t~PythonConsole();',
            '\tPythonConsole();\n\t~PythonConsole();\n\tvoid finiScriptOutput();'),
        'lib/romp/console.cpp': (
            'PythonConsole::~PythonConsole()\n{\n\tBW_GUARD;\n\tif (pHookedWriter_ != NULL)\n\t{\n'
            '\t\tScriptOutputHook::unhookScriptOutput( pHookedWriter_, this );\n\t}\n}',
            'PythonConsole::~PythonConsole()\n{\n\tBW_GUARD;\n\tthis->finiScriptOutput();\n}\n\n'
            'void PythonConsole::finiScriptOutput()\n{\n\tBW_GUARD;\n'
            '\t// Canvas owns this subscription and retires before Script. The writer\n'
            '\t// can outlive Py_Finalize when another owner retains an output stream.\n'
            '\tScriptOutputWriter * writer = pHookedWriter_;\n'
            '\tpHookedWriter_ = NULL;\n'
            '\tif (writer != NULL)\n\t{\n'
            '\t\tScriptOutputHook::unhookScriptOutput( writer, this );\n\t}\n}'),
        'client/canvas_app.cpp': (
            'void CanvasApp::fini()\n{\n\tBW_GUARD;',
            'void CanvasApp::fini()\n{\n\tBW_GUARD;\n'
            '#if ENABLE_CONSOLES\n'
            '\tPythonConsole * console = static_cast< PythonConsole * >(\n'
            '\t\tConsoleManager::instance().find( "Python" ) );\n'
            '\tif (console != NULL)\n\t{\n\t\tconsole->finiScriptOutput();\n\t}\n'
            '#endif // ENABLE_CONSOLES'),
    }
    updated = {}
    for name, (before, after) in changes.items():
        path = root / name
        original = path.read_text()
        updated[path] = replace_once(original, before, after, name)
    for path, text in updated.items():
        path.write_text(text)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('Usage: patch_stock_console_lifecycle.py BIGWORLD_ROOT')
    patch(sys.argv[1])
