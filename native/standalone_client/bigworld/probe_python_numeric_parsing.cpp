// Actual embedded CPython parser regression; no replacement Python objects.
#include <Python.h>
#include <cstdio>
#include <cstring>
#include <stdint.h>

static int failed;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); ++failed; } } while(0)
struct Case { const char *text; uint64_t bits; unsigned int length; };
static Case cases[] = {
    {"inf", 0x7ff0000000000000ULL, 3}, {"-inf", 0xfff0000000000000ULL, 4},
    {"+INF", 0x7ff0000000000000ULL, 4}, {"infinity", 0x7ff0000000000000ULL, 8},
    {"-INFINITY", 0xfff0000000000000ULL, 9},
    {"nan", 0xfff8000000000000ULL, 3}, {"-nan", 0x7ff8000000000000ULL, 4},
    {"+NaN", 0xfff8000000000000ULL, 4},
};
static uint64_t bits(double value) { uint64_t result; std::memcpy(&result,&value,8); return result; }

int main(int argc,char **argv)
{
    if(argc!=3)return 2;
    const bool baseline=std::strcmp(argv[2],"baseline")==0;
    Py_NoSiteFlag=1;Py_DontWriteBytecodeFlag=1;Py_SetPythonHome(argv[1]);Py_Initialize();
    CHECK(std::strncmp(Py_GetVersion(),"2.7.7",5)==0);
    unsigned int differences=0;
    for(unsigned int i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        const Case &c=cases[i];char *end=NULL;
        const double raw=_Py_parse_inf_or_nan(c.text,&end);
        CHECK(end==c.text+c.length);
        if(bits(raw)!=c.bits)++differences;
        if(!baseline)CHECK(bits(raw)==c.bits);
        PyObject *value=PyObject_CallFunction((PyObject *)&PyFloat_Type,
            const_cast<char *>("s"),c.text);
        CHECK(value && PyFloat_Check(value));
        uint64_t converted=value?bits(PyFloat_AsDouble(value)):0;
        if(!baseline)CHECK(converted==c.bits);
        std::printf("%s raw=%016llx builtin=%016llx expected=%016llx\n",c.text,
            (unsigned long long)bits(raw),(unsigned long long)converted,(unsigned long long)c.bits);
        Py_XDECREF(value);
    }
    const char *suffix="nAnSuffix"; char *suffixEnd=NULL;
    const uint64_t suffixBits=bits(_Py_parse_inf_or_nan(suffix,&suffixEnd));
    CHECK(suffixEnd==suffix+3);
    if(!baseline)CHECK(suffixBits==0xfff8000000000000ULL);
    const char *invalid="not-a-number"; char *invalidEnd=NULL;
    CHECK(_Py_parse_inf_or_nan(invalid,&invalidEnd)==-1.0 && invalidEnd==invalid);
    const char *rejected[]={"nanSuffix","infinit","infinitySuffix","0x10","","--inf","n a n","1,5"};
    for(unsigned int i=0;i<sizeof(rejected)/sizeof(rejected[0]);++i) {
        PyObject *value=PyObject_CallFunction((PyObject *)&PyFloat_Type,
            const_cast<char *>("s"),rejected[i]);
        CHECK(!value && PyErr_ExceptionMatches(PyExc_ValueError));Py_XDECREF(value);PyErr_Clear();
    }
    struct Finite { const char *text; uint64_t expected; } finite[]={
        {"1.5",0x3ff8000000000000ULL},{"-0",0x8000000000000000ULL},
        {"2.2250738585072014e-308",0x0010000000000000ULL},
        {"4.9406564584124654e-324",1ULL},{"1e5000",0x7ff0000000000000ULL},
        {"-1e5000",0xfff0000000000000ULL},{"1e-5000",0ULL},
        {"1.7976931348623157e308",0x7fefffffffffffffULL}};
    for(unsigned int i=0;i<sizeof(finite)/sizeof(finite[0]);++i) {
        PyObject *value=PyObject_CallFunction((PyObject *)&PyFloat_Type,
            const_cast<char *>("s"),finite[i].text);
        CHECK(value && bits(PyFloat_AsDouble(value))==finite[i].expected);Py_XDECREF(value);
    }
    char *overflowEnd=NULL;
    const double overflow=PyOS_string_to_double("1e5000",&overflowEnd,PyExc_OverflowError);
    CHECK(overflow==-1.0 && PyErr_ExceptionMatches(PyExc_OverflowError));PyErr_Clear();
    CHECK(overflowEnd && *overflowEnd=='\0');
    // The float constructor also accepts Unicode and surrounding whitespace.
    PyObject *unicode=PyUnicode_FromUnicode(L" -NaN \t",7);
    PyObject *value=PyObject_CallFunctionObjArgs((PyObject *)&PyFloat_Type,unicode,NULL);
    CHECK(value && PyFloat_Check(value));
    if(!baseline)CHECK(bits(PyFloat_AsDouble(value))==0x7ff8000000000000ULL);
    Py_XDECREF(value);Py_DECREF(unicode);
    unsigned int hexDifferences=0;
    for(unsigned int i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        // Original fromhex multiplies its signed scale by x. Unlike the
        // decimal parser's unary minus, this preserves its negative qNaN.
        const uint64_t expected=(i>=5)?0xfff8000000000000ULL:cases[i].bits;
        value=PyObject_CallMethod((PyObject *)&PyFloat_Type,const_cast<char *>("fromhex"),
            const_cast<char *>("s"),cases[i].text);
        CHECK(value && PyFloat_Check(value));
        const uint64_t actual=value?bits(PyFloat_AsDouble(value)):0;
        if(actual!=expected)++hexDifferences;
        if(!baseline)CHECK(actual==expected);
        std::printf("fromhex %s=%016llx expected=%016llx\n",cases[i].text,
            (unsigned long long)actual,(unsigned long long)expected);
        Py_XDECREF(value);
    }
    Finite hex[]={ {"0x1.8p0",0x3ff8000000000000ULL}, {"-0x0p0",0x8000000000000000ULL},
        {"0x1p-1074",1ULL},{"0x1p-1022",0x0010000000000000ULL},
        {"0x1.fffffffffffffp1023",0x7fefffffffffffffULL},
        {"0x1.00000000000008p0",0x3ff0000000000000ULL},
        {"0x1.00000000000009p0",0x3ff0000000000001ULL} };
    for(unsigned int i=0;i<sizeof(hex)/sizeof(hex[0]);++i) {
        value=PyObject_CallMethod((PyObject *)&PyFloat_Type,const_cast<char *>("fromhex"),
            const_cast<char *>("s"),hex[i].text);
        CHECK(value && bits(PyFloat_AsDouble(value))==hex[i].expected);Py_XDECREF(value);
    }
    value=PyObject_CallMethod((PyObject *)&PyFloat_Type,const_cast<char *>("fromhex"),
        const_cast<char *>("s"),"0x1p1024");
    CHECK(!value && PyErr_ExceptionMatches(PyExc_OverflowError));Py_XDECREF(value);PyErr_Clear();
    if(baseline)CHECK(hexDifferences==8);else CHECK(hexDifferences==0);
    if(baseline)CHECK(differences==8);else CHECK(differences==0);
    // Record other users of the same macro without claiming their original
    // numerical contract is audited or changing their compilation settings.
    uint64_t infinityBits=0x7ff0000000000000ULL; double infinity;
    std::memcpy(&infinity,&infinityBits,8);
    PyObject *mathModule=PyImport_ImportModule("math");
    if(mathModule) {
        value=PyObject_CallMethod(mathModule,const_cast<char *>("lgamma"),
            const_cast<char *>("d"),infinity);
        if(value) { std::printf("UNAUDITED math.lgamma(realIEEEInf)=%016llx\n",
            (unsigned long long)bits(PyFloat_AsDouble(value))); Py_DECREF(value); }
        else PyErr_Clear();
        Py_DECREF(mathModule);
    } else PyErr_Clear();
    Py_Finalize();
    std::printf("Actual embedded 2.7.7 %s parser probe: %d failures; original-PE differences %u\n",
        baseline?"baseline":"patched",failed,differences);
    return failed?1:0;
}
