#include "pch.hpp"
#include "stock_numeric_format.hpp"
#include "pyscript/script.hpp"

#include <windows.h>
#include <strsafe.h>
#include <time.h>

BW_BEGIN_NAMESPACE
int StockNumericFormat_token = 1;

namespace
{
class StockNumericFormats
{
public:
    StockNumericFormats()
    {
        // 006f2300: the single NLSFormats owner snapshots the user's actual
        // Windows locale. A failed query makes integral/currency calls pass
        // NULL to NLS; it does not select an invented locale or separator.
        currency_.NumDigits = 0;
        currency_.NegativeOrder = 5;
        currency_.PositiveOrder = 1;
        currency_.lpCurrencySymbol = const_cast<wchar_t *>(L"");
        currency_.lpDecimalSep = currencyDecimal_;
        currency_.lpThousandSep = currencyThousands_;
        wchar_t groups[10] = {};
        validCurrency_ = numericLocale(LOCALE_ILZERO, currency_.LeadingZero) &&
            textLocale(LOCALE_SMONDECIMALSEP, currencyDecimal_, 4) &&
            textLocale(LOCALE_SMONTHOUSANDSEP, currencyThousands_, 4) &&
            textLocale(LOCALE_SMONGROUPING, groups, 10);
        if (validCurrency_)
            currency_.Grouping = wot::stock_numeric_format::grouping(groups);

        integral_.NumDigits = 0;
        integral_.lpDecimalSep = const_cast<wchar_t *>(L"");
        decimal_.lpDecimalSep = decimalSeparator_;
        validNumber_ = numericLocale(LOCALE_IDIGITS, decimal_.NumDigits) &&
            textLocale(LOCALE_SDECIMAL, decimalSeparator_, 4) &&
            numericLocale(LOCALE_ILZERO, integral_.LeadingZero);
        decimal_.LeadingZero = integral_.LeadingZero;
        validNumber_ = validNumber_ &&
            numericLocale(LOCALE_INEGNUMBER, integral_.NegativeOrder);
        decimal_.NegativeOrder = integral_.NegativeOrder;
        integral_.lpThousandSep = decimal_.lpThousandSep = thousands_;
        validNumber_ = validNumber_ &&
            textLocale(LOCALE_STHOUSAND, thousands_, 4) &&
            textLocale(LOCALE_SGROUPING, groups, 10);
        if (validNumber_)
            integral_.Grouping = decimal_.Grouping =
                wot::stock_numeric_format::grouping(groups);
    }

    std::wstring integral(std::int64_t value) const
    {
        wchar_t input[512] = {}, output[512] = {};
        // 006f4120 -> 006d5d90 is StringCchPrintfW, not a stream formatter.
        if (SUCCEEDED(StringCchPrintfW(input, 512, L"%I64d", value)) &&
            GetNumberFormatW(LOCALE_USER_DEFAULT, 0, input,
                validNumber_ ? &integral_ : NULL, output, 512))
            return output;
        return std::wstring();
    }

    std::wstring gold(std::int64_t value) const
    {
        wchar_t input[318] = {}, output[512] = {};
        if (SUCCEEDED(StringCchPrintfW(input, 317, L"%I64d", value)) &&
            GetCurrencyFormatW(LOCALE_USER_DEFAULT, 0, input,
                validCurrency_ ? &currency_ : NULL, output, 512))
            return output;
        return std::wstring();
    }

    bool nice(double value, std::wstring &result) const
    {
        double integralPart;
        if (std::modf(value, &integralPart) == 0.0)
        {
            result = integral(wot::stock_numeric_format::signedInteger(integralPart));
            return true;
        }
        // 006f4bd6 dereferences the decimal format after its validity check.
        // A failed Windows locale query is a local error here, rather than
        // recreating that native null dereference or guessing decimal digits.
        if (!validNumber_)
        {
            PyErr_SetString(PyExc_RuntimeError, "Windows numeric locale is unavailable");
            return false;
        }
        // 006f4c3a: the original passes the nice digit count to 006f3ed0.
        result = fractional(value, static_cast<int>(wot::stock_numeric_format::niceDigits(
            value, decimal_.NumDigits)));
        return true;
    }

    // 006f3ed0: precision < 0 keeps LOCALE_IDIGITS and the separator,
    // 0 drops the separator, > 0 trims trailing zeroes. The original mutates
    // and restores its shared format; a local copy has the same effect.
    std::wstring fractional(double value, int precision) const
    {
        wchar_t input[317] = {}, output[512] = {};
        if (FAILED(StringCchPrintfW(input, 317, L"%lf", value)))
            return std::wstring();
        NUMBERFMTW format = decimal_;
        if (precision >= 0)
        {
            format.NumDigits = static_cast<UINT>(precision);
            if (precision == 0)
                format.lpDecimalSep = const_cast<wchar_t *>(L"");
        }
        if (!GetNumberFormatW(LOCALE_USER_DEFAULT, 0, input,
                validNumber_ ? &format : NULL, output, 512))
            return std::wstring();
        std::wstring result = output;
        if (precision > 0)
            wot::stock_numeric_format::trimDecimal(result);
        return result;
    }

    // 006f3d50 / 006f42b0: local time through the CRT, then NLS with the
    // caller's DATE_* or TIME_* flags, LOCALE_USER_DEFAULT and no picture.
    std::wstring date(std::int64_t value, DWORD flags) const
    {
        const SYSTEMTIME time = localSystemTime(value);
        wchar_t output[512] = {};
        if (!GetDateFormatW(LOCALE_USER_DEFAULT, flags, &time, NULL, output, 512))
            return std::wstring();
        return output;
    }
    std::wstring timeOfDay(std::int64_t value, DWORD flags) const
    {
        const SYSTEMTIME time = localSystemTime(value);
        wchar_t output[512] = {};
        if (!GetTimeFormatW(LOCALE_USER_DEFAULT, flags, &time, NULL, output, 512))
            return std::wstring();
        return output;
    }

private:
    static SYSTEMTIME localSystemTime(std::int64_t value)
    {
        // The original ignores _localtime64_s' status. UCRT fills tm with -1
        // and fails quietly for negative times and times past 3001-01-19;
        // the resulting SYSTEMTIME makes NLS fail, so the text is empty.
        tm local;
        const __time64_t stamp = value;
        _localtime64_s(&local, &stamp);
        SYSTEMTIME result;
        result.wYear = static_cast<WORD>(local.tm_year + 1900);
        result.wMonth = static_cast<WORD>(local.tm_mon + 1);
        result.wDayOfWeek = static_cast<WORD>(local.tm_wday);
        result.wDay = static_cast<WORD>(local.tm_mday);
        result.wHour = static_cast<WORD>(local.tm_hour);
        result.wMinute = static_cast<WORD>(local.tm_min);
        result.wSecond = static_cast<WORD>(local.tm_sec);
        result.wMilliseconds = 0;
        return result;
    }
    static bool numericLocale(LCTYPE key, UINT &result)
    {
        return GetLocaleInfoW(LOCALE_USER_DEFAULT, key | LOCALE_RETURN_NUMBER,
            reinterpret_cast<wchar_t *>(&result), 4) != 0;
    }
    static bool textLocale(LCTYPE key, wchar_t *result, int size)
    {
        return GetLocaleInfoW(LOCALE_USER_DEFAULT, key, result, size) != 0;
    }
    CURRENCYFMTW currency_ = {};
    NUMBERFMTW integral_ = {}, decimal_ = {};
    wchar_t currencyDecimal_[4] = {}, currencyThousands_[4] = {};
    wchar_t decimalSeparator_[4] = {}, thousands_[4] = {};
    bool validCurrency_ = false, validNumber_ = false;
};

StockNumericFormats &numericFormats()
{
    static StockNumericFormats formats;
    return formats;
}

bool readInteger(PyObject *value, std::int64_t &result)
{
    // 006903c0 accepts actual int/long/float subtypes, including bool, but
    // never arbitrary objects merely implementing __int__ or __float__.
    if (PyLong_Check(value))
    {
        result = PyLong_AsLongLong(value);
        if (!PyErr_Occurred())
            return true;
    }
    if (PyInt_Check(value))
    {
        result = PyInt_AsLong(value);
        return true;
    }
    if (PyFloat_Check(value))
    {
        result = wot::stock_numeric_format::signedInteger(PyFloat_AsDouble(value));
        return true;
    }
    PyErr_Format(PyExc_TypeError, "%s must be set to a long", "() argument 1");
    return false;
}

bool readDouble(PyObject *value, double &result)
{
    // 0068fc80's long branch is specifically Win32 unsigned long, not
    // PyLong_AsDouble. Negative Python ints and negative Python longs differ.
    if (PyFloat_Check(value))
    {
        result = PyFloat_AsDouble(value);
        return true;
    }
    if (PyInt_Check(value))
    {
        result = static_cast<double>(PyInt_AsLong(value));
        return true;
    }
    if (PyLong_Check(value))
    {
        result = static_cast<double>(PyLong_AsUnsignedLong(value));
        if (!PyErr_Occurred())
            return true;
    }
    PyErr_Format(PyExc_TypeError, "%s must be set to a float", "() argument 1");
    return false;
}

PyObject *unicodeResult(const std::wstring &value)
{
    return PyUnicode_FromWideChar(value.data(), static_cast<Py_ssize_t>(value.size()));
}

PyObject *utf8Result(const std::wstring &value)
{
    // 006f2990/006f2be0/006f33d0: wide -> unicode -> strict UTF-8 byte str.
    PyObject *unicode = PyUnicode_FromWideChar(value.data(),
        static_cast<Py_ssize_t>(value.size()));
    if (!unicode)
        return NULL;
    PyObject *bytes = PyUnicode_AsEncodedString(unicode, "utf_8", "strict");
    Py_DECREF(unicode);
    return bytes;
}
}

PyObject *py_wg_getIntegralFormat(PyObject *args)
{
    if (PyTuple_Size(args) != 1)
        return Script::argCountError("wg_getIntegralFormat", 1, 1, "int64");
    std::int64_t number;
    if (!readInteger(PyTuple_GetItem(args, 0), number))
        return NULL;
    return utf8Result(numericFormats().integral(number));
}
PY_MODULE_FUNCTION(wg_getIntegralFormat, BigWorld)

PyObject *py_wg_getGoldFormat(PyObject *args)
{
    if (PyTuple_Size(args) != 1)
        return Script::argCountError("wg_getGoldFormat", 1, 1, "int64");
    std::int64_t number;
    if (!readInteger(PyTuple_GetItem(args, 0), number))
        return NULL;
    return utf8Result(numericFormats().gold(number));
}
PY_MODULE_FUNCTION(wg_getGoldFormat, BigWorld)

PyObject *py_wg_getNiceNumberFormat(PyObject *args)
{
    if (PyTuple_Size(args) != 1)
        return Script::argCountError("wg_getNiceNumberFormat", 1, 1, "double");
    double number;
    if (!readDouble(PyTuple_GetItem(args, 0), number))
        return NULL;
    std::wstring result;
    if (!numericFormats().nice(number, result))
        return NULL;
    return utf8Result(result);
}
PY_MODULE_FUNCTION(wg_getNiceNumberFormat, BigWorld)

namespace
{
// The U variants (006f2880 ... 006f3c40) return the same text as unicode.
PyObject *textResult(const std::wstring &value, bool unicode)
{
    return unicode ? unicodeResult(value) : utf8Result(value);
}

template <class Format>
PyObject *integerFormat(PyObject *args, const char *name, bool unicode, Format format)
{
    if (PyTuple_Size(args) != 1)
        return Script::argCountError(name, 1, 1, "int64");
    std::int64_t number;
    if (!readInteger(PyTuple_GetItem(args, 0), number))
        return NULL;
    return textResult(format(number), unicode);
}

template <class Format>
PyObject *doubleFormat(PyObject *args, const char *name, bool unicode, Format format)
{
    if (PyTuple_Size(args) != 1)
        return Script::argCountError(name, 1, 1, "double");
    double number;
    if (!readDouble(PyTuple_GetItem(args, 0), number))
        return NULL;
    return textResult(format(number), unicode);
}

// 006f49f0/006f4a90/...: seconds -> 013d83cf time64 (truncated toward zero).
template <class Format>
PyObject *timeFormat(PyObject *args, const char *name, bool unicode, Format format)
{
    return doubleFormat(args, name, unicode, [&format](double seconds)
    {
        return format(wot::stock_numeric_format::signedInteger(seconds));
    });
}

std::wstring fractional(double value) { return numericFormats().fractional(value, -1); }
std::wstring longDate(std::int64_t value) { return numericFormats().date(value, DATE_LONGDATE); }
std::wstring shortDate(std::int64_t value) { return numericFormats().date(value, DATE_SHORTDATE); }
std::wstring yearMonth(std::int64_t value) { return numericFormats().date(value, DATE_YEARMONTH); }
std::wstring longTime(std::int64_t value) { return numericFormats().timeOfDay(value, 0); }
std::wstring shortTime(std::int64_t value) { return numericFormats().timeOfDay(value, TIME_NOSECONDS); }
}

PyObject *py_wg_getFractionalFormat(PyObject *args)
{
    return doubleFormat(args, "wg_getFractionalFormat", false, fractional);
}
PY_MODULE_FUNCTION(wg_getFractionalFormat, BigWorld)

PyObject *py_wg_getFractionalFormatU(PyObject *args)
{
    return doubleFormat(args, "wg_getFractionalFormatU", true, fractional);
}
PY_MODULE_FUNCTION(wg_getFractionalFormatU, BigWorld)

PyObject *py_wg_getGoldFormatU(PyObject *args)
{
    return integerFormat(args, "wg_getGoldFormatU", true,
        [](std::int64_t value) { return numericFormats().gold(value); });
}
PY_MODULE_FUNCTION(wg_getGoldFormatU, BigWorld)

PyObject *py_wg_getIntegralFormatU(PyObject *args)
{
    return integerFormat(args, "wg_getIntegralFormatU", true,
        [](std::int64_t value) { return numericFormats().integral(value); });
}
PY_MODULE_FUNCTION(wg_getIntegralFormatU, BigWorld)

PyObject *py_wg_getNiceNumberFormatU(PyObject *args)
{
    if (PyTuple_Size(args) != 1)
        return Script::argCountError("wg_getNiceNumberFormatU", 1, 1, "double");
    double number;
    if (!readDouble(PyTuple_GetItem(args, 0), number))
        return NULL;
    std::wstring result;
    if (!numericFormats().nice(number, result))
        return NULL;
    return unicodeResult(result);
}
PY_MODULE_FUNCTION(wg_getNiceNumberFormatU, BigWorld)

PyObject *py_wg_getLongDateFormat(PyObject *args)
{
    return timeFormat(args, "wg_getLongDateFormat", false, longDate);
}
PY_MODULE_FUNCTION(wg_getLongDateFormat, BigWorld)

PyObject *py_wg_getLongDateFormatU(PyObject *args)
{
    return timeFormat(args, "wg_getLongDateFormatU", true, longDate);
}
PY_MODULE_FUNCTION(wg_getLongDateFormatU, BigWorld)

PyObject *py_wg_getLongTimeFormat(PyObject *args)
{
    return timeFormat(args, "wg_getLongTimeFormat", false, longTime);
}
PY_MODULE_FUNCTION(wg_getLongTimeFormat, BigWorld)

PyObject *py_wg_getLongTimeFormatU(PyObject *args)
{
    return timeFormat(args, "wg_getLongTimeFormatU", true, longTime);
}
PY_MODULE_FUNCTION(wg_getLongTimeFormatU, BigWorld)

PyObject *py_wg_getShortDateFormat(PyObject *args)
{
    return timeFormat(args, "wg_getShortDateFormat", false, shortDate);
}
PY_MODULE_FUNCTION(wg_getShortDateFormat, BigWorld)

PyObject *py_wg_getShortDateFormatU(PyObject *args)
{
    return timeFormat(args, "wg_getShortDateFormatU", true, shortDate);
}
PY_MODULE_FUNCTION(wg_getShortDateFormatU, BigWorld)

PyObject *py_wg_getShortTimeFormat(PyObject *args)
{
    return timeFormat(args, "wg_getShortTimeFormat", false, shortTime);
}
PY_MODULE_FUNCTION(wg_getShortTimeFormat, BigWorld)

PyObject *py_wg_getShortTimeFormatU(PyObject *args)
{
    return timeFormat(args, "wg_getShortTimeFormatU", true, shortTime);
}
PY_MODULE_FUNCTION(wg_getShortTimeFormatU, BigWorld)

PyObject *py_wg_getYMDateFormat(PyObject *args)
{
    return timeFormat(args, "wg_getYMDateFormat", false, yearMonth);
}
PY_MODULE_FUNCTION(wg_getYMDateFormat, BigWorld)

PyObject *py_wg_getYMDateFormatU(PyObject *args)
{
    return timeFormat(args, "wg_getYMDateFormatU", true, yearMonth);
}
PY_MODULE_FUNCTION(wg_getYMDateFormatU, BigWorld)
BW_END_NAMESPACE
