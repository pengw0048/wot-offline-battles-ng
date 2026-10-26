#!/usr/bin/env python3
"""Attach original preference events to their real DeviceApp read/write owner."""
import argparse
from pathlib import Path


def patch(root):
    root = Path(root)
    path = root/'client/device_app.cpp'
    source = path.read_text(encoding='utf-8-sig')
    edits = (
        ('#include "device_app.hpp"', '#include "device_app.hpp"\n#include "stock_preferences_callbacks.hpp"'),
        ('    if (preferences)\n    {\n        DataSectionPtr appPreferences',
         '    stockRecordPreferencesLoaded(preferences);\n\n'
         '    if (preferences)\n    {\n        DataSectionPtr appPreferences'),
        ('        // save it\n        if (dataRes.save() == DataHandle::DHE_NoError)',
         '        // Subscribers see all native fields before this exact tree is saved.\n'
         '        stockNotifySavePreferences(root);\n\n'
         '        // save it\n        if (dataRes.save() == DataHandle::DHE_NoError)'),
    )
    for before, after in edits:
        if after in source:
            continue
        if source.count(before) != 1:
            raise ValueError('Pinned DeviceApp preferences event seam changed: '+repr(before))
        source=source.replace(before,after,1)
    helper=Path(__file__).with_name('stock_preferences_callbacks.hpp').read_bytes()
    path.write_text(source,encoding='utf-8')
    (root/'lib/stock_preferences_callbacks.hpp').write_bytes(helper)


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root',type=Path)
    patch(parser.parse_args().engine_root)
