#!/usr/bin/env python3
"""Preserve the original Win32 math/cmath special values under /fp:fast."""
import argparse
from pathlib import Path
import re

ROOT = Path('third_party/python/Modules')
INCLUDE = '#include "_math.h"\n'
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
# Every special-value expansion in each file: (function start, HUGE_VAL, NAN).
SITES = {
    'mathmodule.c': [
        ('static double\nm_tgamma(double x)\n{', 1, 2),
        ('static double\nm_lgamma(double x)\n{', 2, 0),
        ('static double\nm_atan2(double y, double x)\n{', 0, 1),
        ('static double\nm_log(double x)\n{', 1, 2),
        ('static double\nm_log10(double x)\n{', 1, 2),
        ('static PyObject *\nmath_ldexp(PyObject *self, PyObject *args)\n{', 1, 0),
    ],
    'cmathmodule.c': [
        ('#define P34 0.75*Py_MATH_PI\n#define INF Py_HUGE_VAL\n#define N Py_NAN\n', 1, 1),
        ('static double\nc_atan2(Py_complex z)\n{', 0, 1),
    ],
}


def count(text, token):
    return len(re.findall(r'\b%s\b' % token, text))


def unwrap(source):
    if BLOCK not in source:
        return source
    if source.count(BLOCK) != 1:
        raise ValueError('The Python math special-value boundary is incomplete')
    source = source.replace(BLOCK, '', 1)
    for original, replacement in TOKENS:
        source = re.sub(r'\b%s\b' % replacement, original, source)
    return source


def patched_source(original, name):
    newline = '\r\n' if b'\r\n' in original else '\n'
    plain = unwrap(original.decode().replace('\r\n', '\n'))
    if plain.count(INCLUDE) != 1 or '_Py_1513_' in plain:
        raise ValueError('The pinned Python %s includes changed' % name)
    expected = [0, 0]
    for start, huge, nan in SITES[name]:
        if plain.count(start) != 1:
            raise ValueError('The pinned Python %s special-value site changed' % name)
        body = plain[plain.index(start):]
        body = body[:body.index('\n}\n')] if start.endswith('{') else start
        if (count(body, 'Py_HUGE_VAL'), count(body, 'Py_NAN')) != (huge, nan):
            raise ValueError('The pinned Python %s special values changed' % name)
        expected = [expected[0] + huge, expected[1] + nan]
    if [count(plain, 'Py_HUGE_VAL'), count(plain, 'Py_NAN')] != expected:
        raise ValueError('The pinned Python %s has another special-value producer' % name)
    result = plain.replace(INCLUDE, INCLUDE + BLOCK, 1)
    head, separator, tail = result.partition(BLOCK)
    if count(head, 'Py_HUGE_VAL') or count(head, 'Py_NAN'):
        raise ValueError('The pinned Python %s special values moved' % name)
    for original, replacement in TOKENS:
        tail = re.sub(r'\b%s\b' % original, replacement, tail)
    return (head + separator + tail).replace('\n', newline).encode()


def patch_engine(root):
    root = Path(root)
    replacements = []
    for name in SITES:
        path = root / ROOT / name
        old = path.read_bytes()
        replacements.append((path, old, patched_source(old, name)))
    changed = False
    for path, old, new in replacements:
        if old != new:
            path.write_bytes(new)
            changed = True
    return changed


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch_engine(parser.parse_args().engine_root)
