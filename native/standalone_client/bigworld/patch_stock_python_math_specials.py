#!/usr/bin/env python3
"""Preserve the original Win32 math/cmath special values under /fp:fast."""
import argparse
from pathlib import Path
import re

BLOCK = '''/* #1513 returns IEEE infinity and negative quiet NaN; /fp:fast folds UCRT
 * HUGE_VAL to 1e300 and Py_NAN to 0.0, so use the original words. */
#if defined(_MSC_VER)
static double
_Py_1513_special_value(unsigned int high)
{
    const unsigned int words[2] = {0U, high};
    double value;
    memcpy(&value, words, sizeof(value));
    return value;
}
#define _Py_1513_HUGE_VAL _Py_1513_special_value(0x7ff00000U)
#define _Py_1513_NAN _Py_1513_special_value(0xfff80000U)
#else
#define _Py_1513_HUGE_VAL Py_HUGE_VAL
#define _Py_1513_NAN Py_NAN
#endif
'''
TOKENS = (('Py_HUGE_VAL', '_Py_1513_HUGE_VAL'), ('Py_NAN', '_Py_1513_NAN'))
# Path, include anchor and every special-value expansion: (start, HUGE_VAL, NAN).
FILES = {
    'mathmodule.c': ('third_party/python/Modules/mathmodule.c', '#include "_math.h"\n', [
        ('static double\nm_tgamma(double x)\n{', 1, 2),
        ('static double\nm_lgamma(double x)\n{', 2, 0),
        ('static double\nm_atan2(double y, double x)\n{', 0, 1),
        ('static double\nm_log(double x)\n{', 1, 2),
        ('static double\nm_log10(double x)\n{', 1, 2),
        ('static PyObject *\nmath_ldexp(PyObject *self, PyObject *args)\n{', 1, 0),
    ]),
    'cmathmodule.c': ('third_party/python/Modules/cmathmodule.c', '#include "_math.h"\n', [
        ('#define P34 0.75*Py_MATH_PI\n#define INF Py_HUGE_VAL\n#define N Py_NAN\n', 1, 1),
        ('static double\nc_atan2(Py_complex z)\n{', 0, 1),
    ]),
    'complexobject.c': ('third_party/python/Objects/complexobject.c',
                        '#include "structmember.h"\n', [
        ('double\nc_abs(Py_complex z)\n{', 0, 1),
    ]),
}
# #1513 initcmath words differ from the public tables only in these zero signs.
TABLE_NOTE = '    /* Zero signs below follow the #1513 initcmath table words. */\n'
TABLE_ANCHOR = '    INIT_SPECIAL_VALUES(acos_special_values, {\n'
ZERO_SIGNS = [
    ('acos', 2, 3, 'P12,-0.', 'P12,0.'), ('acos', 3, 3, 'P12,-0.', 'P12,0.'),
    ('acosh', 5, 3, 'INF,0.', 'INF,-0.'), ('acosh', 5, 4, 'INF,0.', 'INF,-0.'),
    ('asinh', 0, 3, '-INF,0.', '-INF,-0.'), ('asinh', 0, 4, '-INF,0.', '-INF,-0.'),
    ('asinh', 2, 3, '-0.,0.', '-0.,-0.'), ('asinh', 3, 3, '0.,0.', '0.,-0.'),
    ('asinh', 5, 3, 'INF,0.', 'INF,-0.'), ('asinh', 5, 4, 'INF,0.', 'INF,-0.'),
    ('atanh', 2, 3, '-0.,0.', '-0.,-0.'), ('atanh', 3, 3, '0.,0.', '0.,-0.'),
    ('cosh', 0, 3, 'INF,-0.', 'INF,0.'), ('cosh', 2, 3, '1.,-0.', '1.,0.'),
    ('cosh', 3, 3, '1.,0.', '1.,-0.'), ('cosh', 5, 3, 'INF,0.', 'INF,-0.'),
    ('exp', 0, 5, '0.,0.', '0.,-0.'), ('exp', 2, 3, '1.,0.', '1.,-0.'),
    ('exp', 3, 3, '1.,0.', '1.,-0.'), ('exp', 5, 3, 'INF,0.', 'INF,-0.'),
    ('log', 3, 3, '-INF,0.', '-INF,-0.'), ('log', 5, 3, 'INF,0.', 'INF,-0.'),
    ('log', 5, 4, 'INF,0.', 'INF,-0.'), ('sinh', 0, 3, '-INF,0.', '-INF,-0.'),
    ('sinh', 2, 3, '-0.,0.', '-0.,-0.'), ('sinh', 3, 3, '0.,0.', '0.,-0.'),
    ('sinh', 5, 3, 'INF,0.', 'INF,-0.'), ('sqrt', 2, 3, '0.,0.', '0.,-0.'),
    ('sqrt', 3, 3, '0.,0.', '0.,-0.'), ('sqrt', 5, 3, 'INF,0.', 'INF,-0.'),
    ('sqrt', 5, 4, 'INF,0.', 'INF,-0.'), ('tanh', 0, 5, '-1.,0.', '-1.,-0.'),
    ('tanh', 2, 3, '-0.,0.', '-0.,-0.'), ('tanh', 3, 3, '0.,0.', '0.,-0.'),
    ('tanh', 5, 5, '1.,0.', '1.,-0.'), ('rect', 0, 3, '-INF,-0.', '-INF,0.'),
    ('rect', 2, 3, '-0.,-0.', '0.,0.'), ('rect', 2, 5, '0.,0.', '-0.,0.'),
    ('rect', 3, 5, '0.,0.', '0.,-0.'), ('rect', 5, 3, 'INF,0.', 'INF,-0.'),
]
ENTRY = re.compile(r'C\(([^,()]+),([^,()]+)\)')


def count(text, token):
    return len(re.findall(r'\b%s\b' % token, text))


def table_entries(text, name):
    begin = '    INIT_SPECIAL_VALUES(%s_special_values, {\n' % name
    if text.count(begin) != 1:
        raise ValueError('The pinned cmath %s table changed' % name)
    start = text.index(begin) + len(begin)
    entries = list(ENTRY.finditer(text, start, text.index('\n    })\n', start)))
    if len(entries) != 49:
        raise ValueError('The pinned cmath %s table changed' % name)
    return entries


def table_signs(text, forward):
    """Move the listed table entries between public and #1513 zero signs."""
    for name, row, column, public, retail in ZERO_SIGNS:
        entry = table_entries(text, name)[row*7 + column]
        old, new = (public, retail) if forward else (retail, public)
        if ','.join(part.strip() for part in entry.groups()) != old:
            raise ValueError('The pinned cmath %s table entry changed' % name)
        text = text[:entry.start()] + 'C(%s)' % new + text[entry.end():]
    return text


def unwrap(source):
    if BLOCK not in source:
        return source
    if source.count(BLOCK) != 1:
        raise ValueError('The Python math special-value boundary is incomplete')
    source = source.replace(BLOCK, '', 1)
    for original, replacement in TOKENS:
        source = re.sub(r'\b%s\b' % replacement, original, source)
    if TABLE_NOTE in source:
        if source.count(TABLE_NOTE + TABLE_ANCHOR) != 1:
            raise ValueError('The cmath table sign boundary is incomplete')
        source = table_signs(source.replace(TABLE_NOTE, '', 1), False)
    return source


def patched_source(original, name):
    include, sites = FILES[name][1:]
    newline = '\r\n' if b'\r\n' in original else '\n'
    plain = unwrap(original.decode().replace('\r\n', '\n'))
    if plain.count(include) != 1 or '_Py_1513_' in plain or TABLE_NOTE in plain:
        raise ValueError('The pinned Python %s includes changed' % name)
    expected = [0, 0]
    for start, huge, nan in sites:
        if plain.count(start) != 1:
            raise ValueError('The pinned Python %s special-value site changed' % name)
        body = plain[plain.index(start):]
        body = body[:body.index('\n}\n')] if start.endswith('{') else start
        if (count(body, 'Py_HUGE_VAL'), count(body, 'Py_NAN')) != (huge, nan):
            raise ValueError('The pinned Python %s special values changed' % name)
        expected = [expected[0] + huge, expected[1] + nan]
    if [count(plain, 'Py_HUGE_VAL'), count(plain, 'Py_NAN')] != expected:
        raise ValueError('The pinned Python %s has another special-value producer' % name)
    result = plain.replace(include, include + BLOCK, 1)
    head, separator, tail = result.partition(BLOCK)
    if count(head, 'Py_HUGE_VAL') or count(head, 'Py_NAN'):
        raise ValueError('The pinned Python %s special values moved' % name)
    for original, replacement in TOKENS:
        tail = re.sub(r'\b%s\b' % original, replacement, tail)
    if name == 'cmathmodule.c':
        if tail.count(TABLE_ANCHOR) != 1:
            raise ValueError('The pinned cmath tables changed')
        tail = table_signs(tail.replace(TABLE_ANCHOR, TABLE_NOTE + TABLE_ANCHOR, 1), True)
    return (head + separator + tail).replace('\n', newline).encode()


def patch_engine(root):
    root = Path(root)
    replacements = []
    for name, (relative, unused_include, unused_sites) in FILES.items():
        path = root / relative
        old = path.read_bytes()
        replacements.append((path, old, patched_source(old, name)))
    written = []
    try:
        for path, old, new in replacements:
            if old != new:
                written.append((path, old))
                path.write_bytes(new)
    except OSError:
        # Restore earlier files so a late write failure publishes nothing.
        for path, old in written:
            path.write_bytes(old)
        raise
    return bool(written)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch_engine(parser.parse_args().engine_root)
