"""Original UI consumers and native desktop operation ownership/failure tests."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DesktopUtilitiesTest(unittest.TestCase):
    def test_real_win32_operation_bodies_contain_failures_and_transfer_ownership(self):
        host = r'''
#include <cassert>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#define WOT_DESKTOP_UTILITIES_HOST
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define PY_AUTO_MODULE_FUNCTION(...)
namespace BW { using wstring=std::wstring; }
using HGLOBAL=void*;
using LPWSTR=wchar_t*;
constexpr unsigned GMEM_MOVEABLE=2,GMEM_DDESHARE=0x2000,CF_UNICODETEXT=13;
constexpr unsigned LOCALE_USER_DEFAULT=0x400,LOCALE_IFIRSTDAYOFWEEK=0x100c,LOCALE_RETURN_NUMBER=0x20000000;
bool openOK=true,emptyOK=true,allocOK=true,lockOK=true,setOK=true;
unsigned opened=0,closed=0,freed=0,unlocked=0;
std::map<void*,size_t> allocations;
std::wstring clipboard;
void* transferred=nullptr;
bool OpenClipboard(void* owner) { assert(!owner);++opened;return openOK; }
bool EmptyClipboard() { return emptyOK; }
HGLOBAL GlobalAlloc(unsigned flags,size_t bytes) {
    assert(flags==(GMEM_MOVEABLE|GMEM_DDESHARE));if(!allocOK)return nullptr;
    void* data=malloc(bytes);allocations[data]=bytes;return data;
}
void* GlobalLock(HGLOBAL allocation) { return lockOK?allocation:nullptr; }
bool GlobalUnlock(HGLOBAL) { ++unlocked;return true; }
HGLOBAL SetClipboardData(unsigned format,HGLOBAL allocation) {
    assert(format==CF_UNICODETEXT);
    if(!setOK)return nullptr;
    clipboard=static_cast<wchar_t*>(allocation);transferred=allocation;return allocation;
}
HGLOBAL GlobalFree(HGLOBAL allocation) {
    assert(allocation!=transferred && allocations.erase(allocation)==1);
    free(allocation);++freed;return nullptr;
}
bool CloseClipboard() { ++closed;return true; }
int localeResult=2,localeDay=0;
int GetLocaleInfoW(unsigned locale,unsigned flags,LPWSTR data,int count) {
    assert(locale==LOCALE_USER_DEFAULT && flags==(LOCALE_IFIRSTDAYOFWEEK|LOCALE_RETURN_NUMBER) && count==2);
    *reinterpret_cast<int*>(data)=localeDay;return localeResult;
}
#include "stock_desktop_utilities.cpp"
int main() {
    for(const std::wstring& text:{std::wstring(),std::wstring(L"Player"),std::wstring(L"玩家の名前"),std::wstring(200,L'界')}) {
        auto closeBefore=closed;
        assert(BW::wg_copyToClipboard(text));
        assert(clipboard==text && closed==closeBefore+1 && transferred);
        assert(allocations[transferred]==(text.size()+1)*sizeof(wchar_t));
        assert(allocations.erase(transferred)==1);free(transferred);transferred=nullptr;
    }
    auto closeBefore=closed;openOK=false;
    assert(!BW::wg_copyToClipboard(L"no open") && closed==closeBefore);
    openOK=true;emptyOK=false;assert(!BW::wg_copyToClipboard(L"no empty"));emptyOK=true;
    allocOK=false;assert(!BW::wg_copyToClipboard(L"no allocation"));allocOK=true;
    auto freeBefore=freed;lockOK=false;
    assert(!BW::wg_copyToClipboard(L"no lock") && freed==freeBefore+1);lockOK=true;
    freeBefore=freed;setOK=false;
    assert(!BW::wg_copyToClipboard(L"no transfer") && freed==freeBefore+1);setOK=true;
    assert(allocations.empty() && closed==opened-1);
    for(int day=0;day!=7;++day) { localeDay=day;assert(BW::wg_firstDayOfWeek()==day); }
    localeDay=99;assert(BW::wg_firstDayOfWeek()==99); // no guessed clamp
    for(int count:{0,1,3}) { localeResult=count;assert(BW::wg_firstDayOfWeek()==-1); }
}
'''
        with tempfile.TemporaryDirectory(prefix='stock-desktop-') as tmp:
            path = Path(tmp)
            source, binary = path / 'host.cpp', path / 'host'
            source.write_text(host, encoding='utf-8')
            command = ['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'bigworld'), str(source), '-o', str(binary)]
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True)

    def test_original_native_clipboard_and_locale_instructions(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Set WOT_0922_CLIENT for original native instructions')
        from stock_desktop_utilities_oracle import OriginalDesktopUtilities
        original = OriginalDesktopUtilities(Path(client) / 'WorldOfTanks.exe')
        for text in ('', 'Player', '玩家の名前', '😀' * 20):
            success, data = original.copy(text)
            self.assertTrue(success)
            self.assertEqual(data, text.encode('utf-16-le') + b'\0\0')
            self.assertEqual([name for name, args in original.operations],
                ['open', 'empty', 'allocate', 'lock', 'copy', 'unlock', 'set', 'close'])
        self.assertEqual(original.copy('locked elsewhere', opened=False), (False, None))
        self.assertEqual([name for name, args in original.operations], ['open'])
        for day in range(7):
            self.assertEqual(original.first_day(day), day)
        self.assertEqual(original.first_day(99), 99)
        for count in (0, 1, 3):
            self.assertEqual(original.first_day(5, count=count), -1)
        self.assertIsNone(original.first_day(0, arity=1))
        self.assertEqual([name for name, args in original.operations], ['arity-error'])

    def test_exact_original_pyc_clipboard_and_calendar_consumers(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Set WOT_0922_CLIENT for exact original consumers')
        python = os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7')
        result = subprocess.run([python, '-B', str(ROOT / 'tests/stock_desktop_utilities_consumer.py'),
            str(Path(client) / 'res/packages/scripts.pkg')], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('Exact clipboard and calendar consumer contracts passed', result.stdout)
