"""Connect #1513 taskbar reset and resource retirement to the real App owner."""
from pathlib import Path
import argparse


def replace_once(text, old, new, label):
    if new in text:
        return text
    if text.count(old) != 1:
        raise RuntimeError('Unexpected Windows notifier ' + label)
    return text.replace(old, new, 1)


def patch(root):
    path = Path(root) / 'client/app.cpp'
    text = path.read_text()
    text = replace_once(text, '#include "app.hpp"',
                        '#include "app.hpp"\n#include "stock_windows_notifier.hpp"', 'App include')
    text = replace_once(text, '\tcurrentState_ = STATE_FINALISED;',
                        '\tcurrentState_ = STATE_FINALISED;\n\tfinaliseStockWindowsNotifier();', 'native retirement')
    text = replace_once(text, '\tMouseCursor::staticSetFocus( state );',
                        '\tMouseCursor::staticSetFocus( state );\n'
                        '\tif (state) resetStockWindowsNotifier();', 'focus-gain reset')
    path.write_text(text)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bigworld_root', type=Path)
    patch(parser.parse_args().bigworld_root)
