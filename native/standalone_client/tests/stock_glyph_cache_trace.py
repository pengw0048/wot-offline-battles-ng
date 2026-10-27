# Python 2.7 driver for stock_glyph_cache_host.cpp: run each oracle step through the
# production GUI.wg_setGlyphCacheParams on a fresh manager and print the stored bytes.
import binascii
import json
import sys

import GUI
import _host


def value(kind, raw):
    if kind == 'long':
        return long(raw)
    if kind == 'float':
        return float(raw)
    return raw


out = []
for step in json.load(open(sys.argv[1])):
    _host.createManager()
    result, error = 'None', None
    try:
        GUI.wg_setGlyphCacheParams(*[value(kind, raw) for kind, raw in step])
    except Exception as exception:
        result, error = 'NULL', '%s: %s' % (type(exception).__name__, exception)
    flag, stored = _host.pending()
    out.append([result, error, flag if flag else None, binascii.hexlify(stored) if flag else None])
    _host.destroyManager()
    _host.calls()
print(json.dumps(out))
