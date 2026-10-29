"""Compile the production NLS owner and (locally) its real CPython 2 boundary.

NLS receipts below are controlled OS responses, not Windows locale acceptance.
The tests verify the exact format structures, call order and failure paths.
"""
from pathlib import Path
import os
import shlex
import struct
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'bigworld/stock_numeric_format.cpp'
HEADER = ROOT / 'bigworld/stock_numeric_format.hpp'

NLS_HOST = r'''
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <vector>
#include <utility>
using UINT=unsigned;using LCTYPE=unsigned;using HRESULT=int;using DWORD=unsigned;using WORD=unsigned short;
static bool formatFailure=false;
struct NUMBERFMTW {UINT NumDigits,LeadingZero,Grouping;wchar_t *lpDecimalSep,*lpThousandSep;UINT NegativeOrder;};
struct CURRENCYFMTW {UINT NumDigits,LeadingZero,Grouping;wchar_t *lpDecimalSep,*lpThousandSep;UINT NegativeOrder,PositiveOrder;wchar_t *lpCurrencySymbol;};
#define LOCALE_USER_DEFAULT 0x400
#define LOCALE_RETURN_NUMBER 0x20000000
#define LOCALE_ILZERO 0x12
#define LOCALE_IDIGITS 0x11
#define LOCALE_INEGNUMBER 0x1010
#define LOCALE_SMONDECIMALSEP 0x16
#define LOCALE_SMONTHOUSANDSEP 0x17
#define LOCALE_SMONGROUPING 0x18
#define LOCALE_SDECIMAL 0xe
#define LOCALE_STHOUSAND 0xf
#define LOCALE_SGROUPING 0x10
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
#define DATE_SHORTDATE 1
#define DATE_LONGDATE 2
#define DATE_YEARMONTH 8
#define TIME_NOSECONDS 2
#define __cdecl
struct SYSTEMTIME {WORD wYear,wMonth,wDayOfWeek,wDay,wHour,wMinute,wSecond,wMilliseconds;};
typedef long long __time64_t;
// Controlled UCRT model of common_localtime_s (10.0.26100 localtime.cpp):
// tm is filled with -1, then negative times and times past max_time_t fail
// quietly. Valid conversions return the fixture below.
static unsigned localCalls=0;static long long lastTime=0;
static const long long maxLocalTime=0x793582affLL;
static tm localFixture={};
static int _localtime64_s(tm* out,const __time64_t* time) {
    ++localCalls;lastTime=*time;std::memset(out,0xff,sizeof(*out));
    if(*time<0 || *time>maxLocalTime)return 22;
    *out=localFixture;return 0;
}
static SYSTEMTIME lastSystemTime={};static DWORD lastFlags=0;static unsigned dateCalls=0,timeCalls=0;
static std::wstring dateOutput=L"Tuesday, March 5, 2024",timeOutput=L"8:07:09 PM";
// NLS validates SYSTEMTIME; this fixture rejects the same out-of-range fields.
static bool validTime(const SYSTEMTIME* t) {
    return t->wMonth>=1 && t->wMonth<=12 && t->wDay>=1 && t->wDay<=31 &&
        t->wHour<24 && t->wMinute<60 && t->wSecond<60;
}
static int nlsTime(const std::wstring& text,unsigned& calls,UINT locale,DWORD flags,
        const SYSTEMTIME* t,const wchar_t* picture,wchar_t* out,int size) {
    assert(locale==0x400 && !picture && size==512);++calls;
    lastSystemTime=*t;lastFlags=flags;
    if(formatFailure || !validTime(t))return 0;
    std::copy(text.c_str(),text.c_str()+text.size()+1,out);
    return static_cast<int>(text.size()+1);
}
static int GetDateFormatW(UINT l,DWORD f,const SYSTEMTIME* t,const wchar_t* p,wchar_t* o,int s)
{return nlsTime(dateOutput,dateCalls,l,f,t,p,o,s);}
static int GetTimeFormatW(UINT l,DWORD f,const SYSTEMTIME* t,const wchar_t* p,wchar_t* o,int s)
{return nlsTime(timeOutput,timeCalls,l,f,t,p,o,s);}
static UINT failKey=0,decimalDigits=2;
static bool lastNull=false;
static unsigned numberCalls=0,currencyCalls=0;
static std::wstring lastInput,output=L"1,234.50",numberGroups=L"3;0",moneyGroups=L"3;2;0",decimalText=L",";
static NUMBERFMTW lastNumber={};static CURRENCYFMTW lastCurrency={};
static std::vector<UINT> localeQueries;
static int GetLocaleInfoW(UINT locale,LCTYPE key,wchar_t* out,int size) {
    assert(locale==0x400);localeQueries.push_back(key);
    if(key==failKey)return 0;
    if(key&LOCALE_RETURN_NUMBER) {
        assert(size==4);
        *reinterpret_cast<UINT*>(out)=key==(LOCALE_IDIGITS|LOCALE_RETURN_NUMBER)?decimalDigits:1;
        return 2;
    }
    const std::wstring text=key==LOCALE_SGROUPING?numberGroups:
        key==LOCALE_SMONGROUPING?moneyGroups:
        key==LOCALE_SDECIMAL?decimalText:key==LOCALE_STHOUSAND?L"\u202f":
        key==LOCALE_SMONDECIMALSEP?L".":L"'";
    assert(static_cast<int>(text.size()+1)<=size);
    std::copy(text.c_str(),text.c_str()+text.size()+1,out);
    return static_cast<int>(text.size()+1);
}
static int GetNumberFormatW(UINT locale,UINT flags,const wchar_t* input,
        const NUMBERFMTW* fmt,wchar_t* out,int size) {
    assert(locale==0x400 && flags==0 && size==512);++numberCalls;
    lastInput=input;lastNull=!fmt;if(fmt)lastNumber=*fmt;
    if(formatFailure)return 0;
    assert(static_cast<int>(output.size()+1)<=size);
    std::copy(output.c_str(),output.c_str()+output.size()+1,out);
    return static_cast<int>(output.size()+1);
}
static int GetCurrencyFormatW(UINT locale,UINT flags,const wchar_t* input,
        const CURRENCYFMTW* fmt,wchar_t* out,int size) {
    assert(locale==0x400 && flags==0 && size==512);++currencyCalls;
    lastInput=input;lastNull=!fmt;if(fmt)lastCurrency=*fmt;
    if(formatFailure)return 0;
    assert(static_cast<int>(output.size()+1)<=size);
    std::copy(output.c_str(),output.c_str()+output.size()+1,out);
    return static_cast<int>(output.size()+1);
}
template<class T> static HRESULT StringCchPrintfW(wchar_t* out,unsigned size,
        const wchar_t* fmt,T value) {
    assert(std::wstring(fmt)==L"%I64d" || std::wstring(fmt)==L"%lf");
    // This fixture supplies only the CRT call; production uses StringCchPrintfW.
    int n=std::wstring(fmt)==L"%I64d"?
        std::swprintf(out,size,L"%lld",static_cast<long long>(value)):
        std::swprintf(out,size,L"%lf",static_cast<double>(value));
    return n<0 || static_cast<unsigned>(n)>=size?-1:0;
}
'''

NLS_CASES = r'''
int main() {
    using namespace wot::stock_numeric_format;
    assert(grouping(L"3;0")==3 && grouping(L"3;2;0")==32);
    assert(grouping(L"3")==30 && grouping(L"3;2")==320);
    assert(grouping(L"0")==0 && grouping(L"3;0;")==30 && grouping(L"abc")==0);
    assert(grouping(L"9;8;7;0")==987 && grouping(L"1;9")==190);
    assert(signedInteger(1.9)==1 && signedInteger(-1.9)==-1);
    const auto low=(std::numeric_limits<std::int64_t>::min)();
    assert(signedInteger(9223372036854775808.0)==low);
    assert(signedInteger(-9223372036854775808.0)==low);
    assert(signedInteger(std::numeric_limits<double>::infinity())==low);
    assert(signedInteger(std::numeric_limits<double>::quiet_NaN())==low);
    assert(niceDigits(.1,4)==1 && niceDigits(.125,4)==3 && niceDigits(.12345,2)==2);
    assert(niceDigits(.125,0)==0 && niceDigits(.1,1)==1);
    for(auto pair: {std::make_pair(L"1.00",L"1"),{L"1,20",L"1,2"},
            {L"0.00",L"0"},{L"1\u066b00",L"1\u066b"},{L"1.00-",L"1.00-"},
            {L"(1.00)",L"(1.00)"},{L"0",L"0"}}) {
        std::wstring value=pair.first;trimDecimal(value);assert(value==pair.second);
    }
    StockNumericFormats formats;
    assert((localeQueries==std::vector<UINT>{
        LOCALE_RETURN_NUMBER|LOCALE_ILZERO,LOCALE_SMONDECIMALSEP,
        LOCALE_SMONTHOUSANDSEP,LOCALE_SMONGROUPING,
        LOCALE_RETURN_NUMBER|LOCALE_IDIGITS,LOCALE_SDECIMAL,
        LOCALE_RETURN_NUMBER|LOCALE_ILZERO,LOCALE_RETURN_NUMBER|LOCALE_INEGNUMBER,
        LOCALE_STHOUSAND,LOCALE_SGROUPING}));
    assert(formats.integral(-1234567)==output);
    assert(lastInput==L"-1234567" && !lastNull);
    assert(lastNumber.NumDigits==0 && lastNumber.LeadingZero==1 && lastNumber.Grouping==3);
    assert(std::wstring(lastNumber.lpDecimalSep).empty());
    assert(std::wstring(lastNumber.lpThousandSep)==L"\u202f" && lastNumber.NegativeOrder==1);
    assert(formats.gold(-1234)==output && lastInput==L"-1234");
    assert(lastCurrency.NumDigits==0 && lastCurrency.LeadingZero==1 && lastCurrency.Grouping==32);
    assert(lastCurrency.NegativeOrder==5 && lastCurrency.PositiveOrder==1);
    assert(std::wstring(lastCurrency.lpCurrencySymbol).empty());
    assert(std::wstring(lastCurrency.lpDecimalSep)==L".");
    assert(std::wstring(lastCurrency.lpThousandSep)==L"'");
    std::wstring result;
    output=L"12,30";assert(formats.nice(12.3,result) && result==L"12,3");
    assert(lastInput==L"12.300000" && lastNumber.NumDigits==1);
    output=L"12,35";assert(formats.nice(12.3456789,result) && result==L"12,35");
    assert(lastInput==L"12.345679" && lastNumber.NumDigits==2);
    assert(formats.nice(-0.0,result) && lastInput==L"0" && lastNumber.NumDigits==0);
    assert(formats.nice(std::numeric_limits<double>::infinity(),result));
    assert(lastInput==L"-9223372036854775808");
    formatFailure=true;
    assert(formats.integral(1).empty() && formats.gold(1).empty());
    assert(formats.nice(.3,result) && result.empty());formatFailure=false;
    // Currency and number initialization are independent and short-circuit
    // their failed query chains. OS defaults are passed through as NULL.
    localeQueries.clear();failKey=LOCALE_SMONDECIMALSEP;
    StockNumericFormats badCurrency;
    assert(localeQueries.size()==8);
    badCurrency.gold(1);assert(lastNull);
    badCurrency.integral(1);assert(!lastNull);
    failKey=LOCALE_SDECIMAL;StockNumericFormats badNumber;
    badNumber.integral(1);assert(lastNull);
    assert(!badNumber.nice(.1,result) && errors==1);
    assert(badNumber.nice(1.0,result) && lastNull);
    failKey=0;decimalDigits=0;StockNumericFormats noDecimals;
    output=L"1";assert(noDecimals.nice(1.5,result));
    assert(lastNumber.NumDigits==0 && std::wstring(lastNumber.lpDecimalSep).empty());
    decimalDigits=4;numberGroups=L"3;2";StockNumericFormats fourDecimals;
    output=L"0,1250";assert(fourDecimals.nice(.125,result) && result==L"0,125");
    assert(lastNumber.NumDigits==3 && lastNumber.Grouping==320);

    // 006f3ed0: -1 keeps IDIGITS and the separator with no trim; 0 drops the
    // separator; > 0 trims. The shared format is never left modified.
    decimalDigits=3;StockNumericFormats fractions;
    output=L"1,20";assert(fractions.fractional(1.2,-1)==L"1,20");
    assert(lastInput==L"1.200000" && lastNumber.NumDigits==3 && !lastNull);
    assert(std::wstring(lastNumber.lpDecimalSep)==L",");
    output=L"1";assert(fractions.fractional(1.2,0)==L"1");
    assert(lastNumber.NumDigits==0 && std::wstring(lastNumber.lpDecimalSep).empty());
    output=L"1,250";assert(fractions.fractional(1.25,3)==L"1,25" && lastNumber.NumDigits==3);
    output=L"2,000";fractions.fractional(2.0,-1);
    assert(lastNumber.NumDigits==3 && std::wstring(lastNumber.lpDecimalSep)==L",");
    assert(fractions.integral(5)==output && std::wstring(lastNumber.lpDecimalSep).empty());
    formatFailure=true;assert(fractions.fractional(1.5,-1).empty());formatFailure=false;
    output=L"0.333333";badNumber.fractional(1.0/3.0,-1);assert(lastNull && lastInput==L"0.333333");

    // 006f3d50/006f42b0: tm -> SYSTEMTIME with zero milliseconds, flags as given.
    localFixture.tm_year=124;localFixture.tm_mon=2;localFixture.tm_mday=5;localFixture.tm_wday=2;
    localFixture.tm_hour=20;localFixture.tm_min=7;localFixture.tm_sec=9;localFixture.tm_isdst=0;
    for(DWORD flags: {DATE_LONGDATE,DATE_SHORTDATE,DATE_YEARMONTH}) {
        assert(fractions.date(1709687229,flags)==dateOutput && lastFlags==flags && lastTime==1709687229);
        assert(lastSystemTime.wYear==2024 && lastSystemTime.wMonth==3 && lastSystemTime.wDay==5);
        assert(lastSystemTime.wDayOfWeek==2 && lastSystemTime.wHour==20 && lastSystemTime.wMinute==7);
        assert(lastSystemTime.wSecond==9 && lastSystemTime.wMilliseconds==0);
    }
    for(DWORD flags: {0u,static_cast<DWORD>(TIME_NOSECONDS)}) {
        assert(fractions.timeOfDay(0,flags)==timeOutput && lastFlags==flags && lastTime==0);
    }
    // A rejected time leaves tm all -1 and NLS rejects the SYSTEMTIME.
    assert(fractions.date(-1,DATE_LONGDATE).empty() && lastSystemTime.wMonth==0);
    assert(lastSystemTime.wYear==1899 && lastSystemTime.wDay==0xffff);
    assert(fractions.timeOfDay(-9223372036854775807LL-1,0).empty() && lastSystemTime.wHour==0xffff);
    assert(fractions.date(maxLocalTime+1,DATE_SHORTDATE).empty() && lastSystemTime.wMonth==0);
    assert(fractions.timeOfDay(maxLocalTime+1,0).empty() && lastTime==maxLocalTime+1);
    assert(fractions.date(maxLocalTime,DATE_SHORTDATE)==dateOutput);
    formatFailure=true;
    assert(fractions.date(1,DATE_LONGDATE).empty() && fractions.timeOfDay(1,0).empty());
    formatFailure=false;
}
'''

CAPI_REGISTER = r'''
static PyObject* setOutput(PyObject*,PyObject* args) {
    PyObject* value;if(!PyArg_ParseTuple(args,"U",&value))return NULL;
    wchar_t text[512];Py_ssize_t n=PyUnicode_AsWideChar((PyUnicodeObject*)value,text,511);
    if(n<0)return NULL;output.assign(text,n);Py_RETURN_NONE;
}
static PyObject* inputReceipt(PyObject*,PyObject*) {
    return PyUnicode_FromWideChar(lastInput.data(),lastInput.size());
}
static PyObject* setTimeOutputs(PyObject*,PyObject* args) {
    PyObject *date,*time;if(!PyArg_ParseTuple(args,"UU",&date,&time))return NULL;
    wchar_t text[512];
    Py_ssize_t n=PyUnicode_AsWideChar((PyUnicodeObject*)date,text,511);dateOutput.assign(text,n);
    n=PyUnicode_AsWideChar((PyUnicodeObject*)time,text,511);timeOutput.assign(text,n);
    localFixture.tm_year=124;localFixture.tm_mon=2;localFixture.tm_mday=5;localFixture.tm_hour=20;
    Py_RETURN_NONE;
}
static PyObject* timeReceipt(PyObject*,PyObject*) {
    return Py_BuildValue("(LIII)",lastTime,lastFlags,dateCalls,timeCalls);
}
#define WG(name) {#name,[](PyObject*,PyObject* a)->PyObject*{return py_##name(a);},METH_VARARGS,NULL}
static PyMethodDef methods[]={
    {"wg_getIntegralFormat",[](PyObject*,PyObject* a)->PyObject*{return py_wg_getIntegralFormat(a);},METH_VARARGS,NULL},
    {"wg_getGoldFormat",[](PyObject*,PyObject* a)->PyObject*{return py_wg_getGoldFormat(a);},METH_VARARGS,NULL},
    {"wg_getNiceNumberFormat",[](PyObject*,PyObject* a)->PyObject*{return py_wg_getNiceNumberFormat(a);},METH_VARARGS,NULL},
    WG(wg_getFractionalFormat),WG(wg_getFractionalFormatU),WG(wg_getGoldFormatU),
    WG(wg_getIntegralFormatU),WG(wg_getNiceNumberFormatU),WG(wg_getLongDateFormat),
    WG(wg_getLongDateFormatU),WG(wg_getLongTimeFormat),WG(wg_getLongTimeFormatU),
    WG(wg_getShortDateFormat),WG(wg_getShortDateFormatU),WG(wg_getShortTimeFormat),
    WG(wg_getShortTimeFormatU),WG(wg_getYMDateFormat),WG(wg_getYMDateFormatU),
    {"setOutput",setOutput,METH_VARARGS,NULL},{"inputReceipt",inputReceipt,METH_NOARGS,NULL},
    {"setTimeOutputs",setTimeOutputs,METH_VARARGS,NULL},{"timeReceipt",timeReceipt,METH_NOARGS,NULL},
    {NULL,NULL,0,NULL}};
PyMODINIT_FUNC initnumeric_probe() {Py_InitModule("numeric_probe",methods);}
'''
CAPI_CHECK = r'''
import numeric_probe as n
class Integer(int):pass
class Long(long):pass
class Float(float):pass
class Convertible(object):
    def __float__(self):return 1.5
    def __int__(self):return 1
n.setOutput(u'\u8d1f1\u202f234,50')
for f in (n.wg_getIntegralFormat,n.wg_getGoldFormat,n.wg_getNiceNumberFormat):
    value=f(1234)
    assert type(value) is str and value==u'\u8d1f1\u202f234,50'.encode('utf-8')
    for args in [(),(1,2),(None,),('1',),(u'1',),(Convertible(),)]:
        try:f(*args)
        except TypeError:pass
        else:raise AssertionError('invalid argument accepted')
    try:f(value=1)
    except TypeError:pass
    else:raise AssertionError('keyword accepted')
for f in (n.wg_getIntegralFormat,n.wg_getGoldFormat):
    for value in [True,False,Integer(-4),Long(-4),Float(-1.9),2**63-1,-2**63]:
        f(value);assert n.inputReceipt()==unicode(long(value))
    for value in [2**63,-2**63-1]:
        try:f(value)
        except TypeError:pass
        else:raise AssertionError('overflow accepted')
    for value in [float('nan'),float('inf'),-float('inf')]:
        f(value);assert n.inputReceipt()==u'-9223372036854775808'
# The real Win32 converter rejects a negative long and long > ULONG_MAX.
# The host Python API uses 64-bit unsigned long, so test its shared valid
# domain here; the full Win32 boundaries are verified by the x86 audit.
for value in [Integer(-4),Long(4),Long(2**32-1),Float(1.125),True]:
    n.wg_getNiceNumberFormat(value)
for value in [Long(-1),Long(2**64)]:
    try:n.wg_getNiceNumberFormat(value)
    except TypeError:pass
    else:raise AssertionError('invalid unsigned long accepted')
n.wg_getNiceNumberFormat(12.3456789)
assert n.inputReceipt()==u'12.345679'
# Unicode peers return the same text as unicode; byte peers return UTF-8 str.
text=u'\u8d1f1\u202f234,50'
n.setOutput(text)
for name,kind in (('Fractional','double'),('Gold','int64'),('Integral','int64'),('NiceNumber','double')):
    byte,uni=getattr(n,'wg_get%sFormat'%name),getattr(n,'wg_get%sFormatU'%name)
    for value in (1234,Integer(5),Float(1.5),True):
        a,b=byte(value),uni(value)
        assert type(a) is str and type(b) is unicode and a.decode('utf-8')==b
        assert value!=1234 or b==text
    for f,label in ((byte,'wg_get%sFormat'%name),(uni,'wg_get%sFormatU'%name)):
        for args in [(),(1,2)]:
            try:f(*args)
            except TypeError as e:assert str(e)=='%s() expects 1 argument of type %s'%(label,kind),str(e)
            else:raise AssertionError('arity accepted')
        for args in [(None,),('1',),(u'1',),(Convertible(),)]:
            try:f(*args)
            except TypeError:pass
            else:raise AssertionError('invalid argument accepted')
# Fractional uses the configured digits and "%lf" six-decimal input.
n.wg_getFractionalFormat(2.5);assert n.inputReceipt()==u'2.500000'
n.wg_getFractionalFormatU(-1e-7);assert n.inputReceipt()==u'-0.000000'
n.wg_getFractionalFormat(1e20);assert n.inputReceipt()==u'100000000000000000000.000000'
# Dates/times: seconds truncated toward zero to time64, flags per wrapper.
n.setTimeOutputs(u'2024\u5e743\u67085\u65e5',u'20:07')
flags={'LongDate':2,'ShortDate':1,'YMDate':8,'LongTime':0,'ShortTime':2}
for name,flag in flags.items():
    for suffix in ('','U'):
        f=getattr(n,'wg_get%sFormat%s'%(name,suffix))
        expected=u'2024\u5e743\u67085\u65e5' if 'Date' in name else u'20:07'
        for value,seconds in ((1709687229,1709687229),(Long(7),7),(1.9,1),(Float(-0.5),0),
                              (True,1),(Integer(3),3),(2**31,2**31)):
            result=f(value)
            assert n.timeReceipt()[:2]==(seconds,flag),(name,value,n.timeReceipt())
            if suffix:assert type(result) is unicode and result==expected
            else:assert type(result) is str and result==expected.encode('utf-8')
        for value in (-1,-1.5,float('nan'),float('inf'),-float('inf'),-2**31):
            assert f(value)==('' if not suffix else u'') and n.timeReceipt()[0]<0
        assert f(32536800000.0)==('' if not suffix else u'') and n.timeReceipt()[0]==32536800000
        for args in [(),(1,2)]:
            try:f(*args)
            except TypeError as e:assert str(e)=='wg_get%sFormat%s() expects 1 argument of type double'%(name,suffix)
            else:raise AssertionError('arity accepted')
        for args in [(None,),('1',),(Convertible(),),(Long(-1),)]:
            try:f(*args)
            except TypeError:pass
            else:raise AssertionError('invalid argument accepted')
# Run the original direct consumers when the reviewed local package is supplied.
import sys
if len(sys.argv)>1:
    import zipfile,marshal,types
    sys.modules['BigWorld']=n
    z=zipfile.ZipFile(sys.argv[1])
    numbers={'__name__':'audit_numbers'}
    exec marshal.loads(z.read('scripts/client/gui/shared/formatters/numbers.pyc')[8:]) in numbers
    assert numbers['formatInt'](0,'-')=='-'
    assert numbers['formatInt'](-1,'-')=='-'
    assert type(numbers['formatInt'](1)) is str
    assert numbers['makeStringWithThousandSymbol'](10000).endswith('K')
    assert n.inputReceipt()==u'10'
    for name in ('gui','gui.shared','gui.shared.formatters','gui.shared.money','debug_utils'):
        sys.modules[name]=types.ModuleType(name)
    styles=types.ModuleType('text_styles')
    styles.credits=styles.gold=styles.crystal=lambda x:x
    sys.modules['gui.shared.formatters'].text_styles=styles
    class Currency(object):
        CREDITS='credits';GOLD='gold';CRYSTAL='crystal'
    sys.modules['gui.shared.money'].Currency=Currency
    sys.modules['debug_utils'].LOG_WARNING=lambda *a:None
    currency={'__name__':'audit_currency'}
    exec marshal.loads(z.read('scripts/client/gui/shared/formatters/currency.pyc')[8:]) in currency
    assert currency['getBWFormatter']('credits') is n.wg_getIntegralFormat
    assert currency['getBWFormatter']('gold') is n.wg_getGoldFormat
    assert currency['getBWFormatter']('crystal') is n.wg_getIntegralFormat
    assert currency['getBWFormatter']('unknown') is n.wg_getIntegralFormat
    def code(member,name):
        root=marshal.loads(z.read('scripts/client/'+member+'.pyc')[8:])
        stack=[root]
        while stack:
            c=stack.pop()
            if c.co_name==name:return c
            stack.extend(k for k in c.co_consts if isinstance(k,types.CodeType))
    getDateTimeFormat=types.FunctionType(code('helpers/time_utils','getDateTimeFormat'),{'BigWorld':n})
    value=getDateTimeFormat(1709687229)
    assert value==u'2024\u5e743\u67085\u65e5 20:07'.encode('utf-8'),value
    header=types.FunctionType(code('gui/Scaleform/daapi/view/lobby/components/CalendarComponent','formatYMHeader'),{'BigWorld':n})
    assert header(None,1709687229.9)==u'2024\u5e743\u67085\u65e5'.encode('utf-8') and n.timeReceipt()[:2]==(1709687229,8)
    fraction=types.FunctionType(code('gui/battle_results/components/style','getFractionalFormatIfNoEmpty'),{'BigWorld':n,'markValueAsEmpty':lambda v:'empty'})
    n.setOutput(u'0,5');assert fraction(0.5)=='0,5' and fraction(0)=='empty'
print('Actual CPython 2 UTF-8 str, subtype, arity and converter contracts passed')
'''


def body():
    source = SOURCE.read_text()
    return '\n'.join(line.replace('BW_NAMESPACE ', '') for line in source.splitlines()
                     if not line.startswith('#include') and
                     not line.startswith('BW_') and
                     not line.startswith('PY_MODULE_FUNCTION('))


def oracle_driver(time_cases, fractional_cases):
    """Production owner driven with the oracle's fixtures; prints receipts."""
    def wide(text):
        return 'L"' + ''.join('\\x%04x' % ord(c) for c in text) + '"'
    lines = ['int main() {', 'StockNumericFormats* f=nullptr;']
    for case in time_cases:
        lines.append('{localFixture={%s};dateOutput=timeOutput=std::wstring(%s);formatFailure=%s;'
                     'lastTime=0x7fffffffffffffffLL;double s;std::uint64_t b=%dULL;std::memcpy(&s,&b,8);'
                     'const auto t=wot::stock_numeric_format::signedInteger(s);if(!f)f=new StockNumericFormats();'
                     % (','.join(str(v) for v in case['tm']), wide(case['output']),
                        'true' if case['fail'] else 'false',
                        struct.unpack('<Q', struct.pack('<d', case['seconds']))[0]))
        lines.append('std::wstring r=%s;' % case['call'])
        lines.append('std::printf("T %lld %u %u %u %u %u %u %u %u %u ",lastTime,lastFlags,lastSystemTime.wYear,'
                     'lastSystemTime.wMonth,lastSystemTime.wDayOfWeek,lastSystemTime.wDay,lastSystemTime.wHour,'
                     'lastSystemTime.wMinute,lastSystemTime.wSecond,lastSystemTime.wMilliseconds);'
                     'for(wchar_t c:r)std::printf("%04x",unsigned(c));std::printf("\\n");}')
    for case in fractional_cases:
        lines.append('{decimalDigits=%d;decimalText=%s;failKey=%s;StockNumericFormats g;output=%s;formatFailure=%s;'
                     'std::wstring r=g.fractional(%r,%d);' % (
                         case['digits'], wide(case['separator']), '0' if case['valid'] else 'LOCALE_SDECIMAL',
                         wide(case['output']), 'true' if case['fail'] else 'false', case['value'], case['precision']))
        lines.append('std::printf("F %d %u ",lastNull?1:0,lastNumber.NumDigits);'
                     'for(const wchar_t* p=lastNull?L"":lastNumber.lpDecimalSep;*p;++p)std::printf("%04x",unsigned(*p));'
                     'std::printf(" ");for(wchar_t c:r)std::printf("%04x",unsigned(c));std::printf("\\n");}')
    lines.append('}')
    return '\n'.join(lines)


class StockNumericFormatTest(unittest.TestCase):
    def test_nls_owner_and_pure_boundaries(self):
        source = body()
        source = source[:source.index('\nStockNumericFormats &numericFormats()')] + '\n}\n'
        program = ('#include "' + str(HEADER) + '"\n' + NLS_HOST +
                   'static unsigned errors=0;static void* PyExc_RuntimeError=nullptr;\n'
                   'static void PyErr_SetString(void*,const char*) {++errors;}\n' +
                   source + NLS_CASES)
        with tempfile.TemporaryDirectory(prefix='stock-numeric-') as directory:
            path = Path(directory)
            (path / 'probe.cpp').write_text(program)
            subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17',
                            '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                            str(path / 'probe.cpp'), '-o', str(path / 'probe')], check=True)
            subprocess.run([str(path / 'probe')], check=True)

    def test_original_x86_nls_helpers(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        sys.path.insert(0, str(ROOT / 'tests'))
        try:
            import stock_numeric_format_nls_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        original = oracle.Original(Path(client) / 'WorldOfTanks.exe')
        times = []
        for index, case in enumerate(oracle.time_cases()):
            helper = sorted(oracle.HELPERS)[index % len(oracle.HELPERS)]
            flags = oracle.HELPERS[helper][1]
            call = ('f->date(t,%d)' if 'Date' in helper else 'f->timeOfDay(t,%d)') % flags
            times.append(dict(case, helper=helper, call=call))
        fractions = oracle.fractional_cases()
        source = body()
        source = source[:source.index('\nStockNumericFormats &numericFormats()')] + '\n}\n'
        program = ('#include "' + str(HEADER) + '"\n' + NLS_HOST +
                   '#include <cstdio>\nstatic unsigned errors=0;static void* PyExc_RuntimeError=nullptr;\n'
                   'static void PyErr_SetString(void*,const char*) {++errors;}\n' + source +
                   oracle_driver(times, fractions))
        with tempfile.TemporaryDirectory(prefix='stock-numeric-oracle-') as directory:
            path = Path(directory)
            (path / 'probe.cpp').write_text(program)
            subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-w', '-ffp-contract=off',
                            str(path / 'probe.cpp'), '-o', str(path / 'probe')], check=True)
            produced = subprocess.run([str(path / 'probe')], check=True, capture_output=True,
                                      text=True).stdout.splitlines()
        hexed = lambda text: ''.join('%04x' % ord(c) for c in text)
        compared = {'time': 0, 'empty': 0, 'fractional': 0, 'null': 0}
        for case, line in zip(times, produced[:len(times)]):
            fields = line.split(' ')
            trace = original.time(case['helper'], case)
            self.assertEqual(int(fields[1]), trace['time'], case)
            self.assertEqual(int(fields[2]), trace['flags'], case)
            self.assertEqual([int(v) for v in fields[3:11]], trace['system'], case)
            self.assertEqual(fields[11], hexed(trace['result'] or ''), case)
            compared['time'] += 1
            compared['empty'] += not trace['result']
        for case, line in zip(fractions, produced[len(times):]):
            null, digits, separator, result = (line.split(' ') + [''])[1:5]
            trace = original.fractional(case)
            self.assertTrue(trace['restored'], case)
            self.assertEqual(null == '1', trace['format'] is None, case)
            if trace['format'] is not None:
                self.assertEqual(int(digits), trace['format'][0], case)
                self.assertEqual(separator, hexed(trace['format'][3]), case)
            else:
                compared['null'] += 1
            self.assertEqual(result, hexed(trace['result']), case)
            compared['fractional'] += 1
        self.assertGreater(compared['empty'], 0)
        print('Original NLS helpers matched production:', compared)

    def test_actual_python2_capi(self):
        python = os.environ.get('WOT_PY27') or shutil.which('python2.7')
        if not python:
            candidate = Path.home() / '.pyenv/versions/2.7.18/bin/python2.7'
            if candidate.is_file():
                python = str(candidate)
        if sys.platform != 'darwin' or not python:
            self.skipTest('Requires the local CPython 2.7 audit interpreter')
        includes = []
        for flag in shlex.split(subprocess.check_output([python + '-config', '--includes'], text=True)):
            includes.extend(['-isystem', flag[2:]] if flag.startswith('-I') else [flag])
        program = ('#define PY_SSIZE_T_CLEAN\n#include <Python.h>\n#include "' +
                   str(HEADER) + '"\n' + NLS_HOST + r'''
namespace Script {
PyObject* argCountError(const char* name,int first,int last,const char* type) {
    assert(first==1 && last==1);
    PyErr_Format(PyExc_TypeError,"%s() expects 1 argument of type %s",name,type);
    return NULL;
}
}
''' + body() + CAPI_REGISTER)
        with tempfile.TemporaryDirectory(prefix='stock-numeric-capi-') as directory:
            path = Path(directory)
            (path / 'probe.cpp').write_text(program)
            (path / 'check.py').write_text(CAPI_CHECK)
            subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++14', '-Wall',
                            '-Wextra', '-Werror', '-Wno-missing-field-initializers',
                            '-dynamiclib', '-undefined', 'dynamic_lookup', *includes,
                            str(path / 'probe.cpp'), '-o', str(path / 'numeric_probe.so')], check=True)
            package_args = []
            if os.environ.get('WOT_0922_CLIENT'):
                package_args = [str(Path(os.environ['WOT_0922_CLIENT']) / 'res/packages/scripts.pkg')]
            subprocess.run([python, str(path / 'check.py'), *package_args],
                           env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'), check=True)


if __name__ == '__main__':
    unittest.main()
