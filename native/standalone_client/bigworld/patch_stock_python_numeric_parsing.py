#!/usr/bin/env python3
"""Preserve the original Win32 parser's nonfinite values under /fp:fast."""
import argparse
from pathlib import Path

PATH = Path('third_party/python/Python/pystrtod.c')
FUNCTION = '''double
_Py_parse_inf_or_nan(const char *p, char **endptr)
{
    double retval;
    const char *s;
    int negate = 0;

    s = p;
    if (*s == '-') {
        negate = 1;
        s++;
    }
    else if (*s == '+') {
        s++;
    }
    if (case_insensitive_match(s, "inf")) {
        s += 3;
        if (case_insensitive_match(s, "inity"))
            s += 5;
        retval = negate ? -Py_HUGE_VAL : Py_HUGE_VAL;
    }
#ifdef Py_NAN
    else if (case_insensitive_match(s, "nan")) {
        s += 3;
        retval = negate ? -Py_NAN : Py_NAN;
    }
#endif
    else {
        s = p;
        retval = -1.0;
    }
    *endptr = (char *)s;
    return retval;
}'''
PREFIX = '''/* The original #1513 parser returns IEEE infinities and signed quiet NaNs.
 * UCRT HUGE_VAL casts 1e300 through float; /fp:fast removes that overflow
 * and folds Py_NAN's multiplication into zero. Preserve this function's
 * original cast semantics without changing the rest of Python's FP model. */
#if defined(_MSC_VER)
#pragma float_control(precise, on, push)
/* #1513 uses negative quiet NaN. Newer MSVC folds Inf*0 to positive NaN. */
static double
_Py_1513_negative_nan(void)
{
    const unsigned int words[2] = {0U, 0xfff80000U};
    double value;
    memcpy(&value, words, sizeof(value));
    return value;
}
#endif
'''
SUFFIX = '''
#if defined(_MSC_VER)
#pragma float_control(pop)
#endif'''


DECIMAL_NAN = '        retval = negate ? -Py_NAN : Py_NAN;'
HEX_NAN = '        x = Py_NAN;'


def nan_branch(statement):
    return ('#if defined(_MSC_VER)\n' +
            statement.replace('Py_NAN', '_Py_1513_negative_nan()') +
            '\n#else\n' + statement + '\n#endif')


def unwrap(source, statement):
    if PREFIX not in source:
        return source
    branch = nan_branch(statement)
    if source.count(PREFIX) != 1 or source.count(SUFFIX) != 1 or source.count(branch) != 1:
        raise ValueError('The Python parser precision boundary is incomplete')
    return source.replace(PREFIX, '', 1).replace(SUFFIX, '', 1).replace(branch, statement, 1)


def wrapped_source(original, function, statement):
    newline = '\r\n' if b'\r\n' in original else '\n'
    source = original.decode().replace('\r\n', '\n')
    plain = unwrap(source, statement)
    if plain.count(function) != 1 or '#pragma float_control' in plain:
        raise ValueError('The pinned Python special-value parser changed')
    if function.count(statement) != 1:
        raise ValueError('The pinned Python NaN branch changed')
    precise = function.replace(statement, nan_branch(statement), 1)
    result = plain.replace(function, PREFIX + precise + SUFFIX, 1)
    return result.replace('\n', newline).encode()


def patch_engine(root):
    root = Path(root)
    decimal = root / PATH
    hexadecimal = root / 'third_party/python/Objects/floatobject.c'
    first = decimal.read_bytes()
    second = hexadecimal.read_bytes()
    text = unwrap(second.decode().replace('\r\n', '\n'), HEX_NAN)
    begin = 'static PyObject *\nfloat_fromhex(PyObject *cls, PyObject *arg)\n{'
    end = '\nPyDoc_STRVAR(float_fromhex_doc,'
    if text.count(begin) != 1 or text.count(end) != 1:
        raise ValueError('The pinned Python hexadecimal parser changed')
    function = text[text.index(begin):text.index(end)].rstrip('\n')
    if (function.count('x = Py_HUGE_VAL;') != 1 or function.count(HEX_NAN) != 1):
        raise ValueError('The pinned hexadecimal special-value branches changed')
    replacements = [(decimal, first, wrapped_source(first, FUNCTION, DECIMAL_NAN)),
                    (hexadecimal, second, wrapped_source(second, function, HEX_NAN))]
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
