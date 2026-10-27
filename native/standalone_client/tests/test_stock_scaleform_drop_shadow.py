"""Run the pinned DropShadowFilter constructor and flag setters on the host.

The exact #1513 constructor and original carousel bytecode define the argument
contract. This test proves parameter/flag handling, not Windows pixels.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from stock_battle_gui_support import client_executable, compiler
from test_stock_scaleform_scene import REVISION, SDK_FILES, load_patch, pinned_sdk

SOURCE = 'Src/GFx/AS3/Obj/Filters/AS3_Obj_Filters_DropShadowFilter.cpp'


def function(source, signature):
    start = source.index(signature)
    begin = source.index('{', start)
    depth = 1
    end = begin + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


HOST = r'''
#include <cmath>
#include <cstdio>
#include <cstdlib>
using UInt32 = unsigned;
using SInt32 = int;
#define SF_UNUSED(x) (void)(x)
struct Value {
    using Number = double;
    Number number;
    bool valid;
    Value(Number n = 0, bool ok = true) : number(n), valid(ok) {}
    bool Convert2Number(Number& out) const { out = number; return valid; }
    bool Convert2UInt32(UInt32& out) const { out = (UInt32)number; return valid; }
    bool Convert2Int32(SInt32& out) const { out = (SInt32)number; return valid; }
    bool Convert2Boolean() const { return number != 0 && !std::isnan(number); }
};
namespace Render { struct BlurFilterParams {
    enum { Mode_Inner = 0x20, Mode_Knockout = 0x10, Mode_HideObject = 0x40 };
    unsigned Mode = 0;
}; }
struct Shadow {
    Render::BlurFilterParams params;
    Render::BlurFilterParams& GetParams() { return params; }
};
struct DropShadowFilter {
    Shadow shadow;
    double fields[8] = {};
    unsigned writes = 0;
    Shadow* GetShadowFilterData() { return &shadow; }
    void AS3Constructor(unsigned argc, const Value* argv);
    void innerSet(const Value&, bool);
    void knockoutSet(const Value&, bool);
    void hideObjectSet(const Value&, bool);
#define FIELD(name, index, type) void name(const Value&, type value) { fields[index] = value; ++writes; }
    FIELD(distanceSet, 0, double)
    FIELD(angleSet, 1, double)
    FIELD(colorSet, 2, UInt32)
    FIELD(alphaSet, 3, double)
    FIELD(blurXSet, 4, double)
    FIELD(blurYSet, 5, double)
    FIELD(strengthSet, 6, double)
    FIELD(qualitySet, 7, SInt32)
#undef FIELD
};
@BODIES@
int main(int argc, char** argv) {
    Value args[11];
    for (int i = 1; i < argc; ++i) {
        char* end = nullptr;
        double n = std::strtod(argv[i], &end);
        args[i - 1] = Value(n, end != argv[i] && *end == '\0');
    }
    DropShadowFilter filter;
    filter.AS3Constructor(argc - 1, args);
    for (double value : filter.fields) std::printf("%.8g ", value);
    std::printf("%u %u\n", filter.shadow.params.Mode, filter.writes);
}
'''


def write(root, files):
    for name, data in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def snapshot(root):
    return {name: (root / name).read_bytes() for name in SDK_FILES}


class ScaleformDropShadowTest(unittest.TestCase):
    def test_constructor_production_body_defaults_and_all_optional_flags(self):
        files = pinned_sdk(self)
        cxx = compiler(self)
        defaults = [4, 45, 0, 1, 4, 4, 1, 1]
        normal = [0, 90, 13224374, 0.2, 8, 8, 4, 2]
        premium = [0, 90, 16723968, 0.7, 12, 12, 3, 2]
        with tempfile.TemporaryDirectory(prefix='scaleform-shadow-host-') as directory:
            root = Path(directory)
            write(root / 'sdk', files)
            load_patch().patch_sdk(root / 'sdk')
            variants = {'pristine': files[SOURCE].decode().replace('\r\n', '\n'),
                        'patched': (root / 'sdk' / SOURCE).read_text()}
            binaries = {}
            for name, source in variants.items():
                signatures = ['void DropShadowFilter::AS3Constructor('] + [
                    'void DropShadowFilter::%sSet(' % field for field in ('inner', 'knockout', 'hideObject')]
                body = '\n'.join(function(source, signature) for signature in signatures)
                host = root / (name + '.cpp')
                host.write_text(HOST.replace('@BODIES@', body))
                binaries[name] = root / name
                subprocess.run([cxx, '-std=c++14', '-Wall', '-Wextra', '-Werror',
                                str(host), '-o', str(binaries[name])], check=True)

            def run(name, args):
                result = subprocess.run([str(binaries[name])] + [str(x) for x in args],
                                        check=True, capture_output=True, text=True)
                return [float(x) for x in result.stdout.split()]

            # Both real eight-argument BaseTankIcon filters formerly set knockout.
            for args in (normal, premium):
                self.assertEqual(run('pristine', args)[8], 0x30)
                self.assertEqual(run('patched', args), args + [0, 8])
            # Every legal argument count, retaining each omitted default.
            args = normal + [1, 1, 1]
            for count in range(12):
                expected = args[:min(count, 8)] + defaults[min(count, 8):]
                mode = (0x20 if count >= 9 else 0) | (0x10 if count >= 10 else 0) | (0x40 if count >= 11 else 0)
                self.assertEqual(run('patched', args[:count]), expected + [mode, 8], count)
            # Each explicit flag is independent of strength/quality and the others.
            for mask in range(8):
                flags = [int(bool(mask & (1 << bit))) for bit in range(3)]
                mode = sum(flag * bit for flag, bit in zip(flags, (0x20, 0x10, 0x40)))
                self.assertEqual(run('patched', premium + flags), premium + [mode, 8])
            for tail in ([0, 0], [0, 2], [4, 0]):
                values = normal[:6] + tail
                self.assertEqual(run('patched', values), values + [0, 8])
            self.assertEqual(run('patched', normal + [0, 'nan', -1]), normal + [0x40, 8])
            # The existing failed numeric coercion remains terminal before setters.
            for index in range(8):
                values = list(normal)
                values[index] = 'invalid'
                self.assertEqual(run('patched', values), [0] * 10, index)

    def test_patch_is_atomic_idempotent_and_rejects_unknown_constructor(self):
        files = pinned_sdk(self)
        patch = load_patch()
        with tempfile.TemporaryDirectory(prefix='scaleform-shadow-patch-') as directory:
            root = Path(directory)
            write(root, files)
            patch.patch_sdk(root)
            final = snapshot(root)
            patch.patch_sdk(root)
            self.assertEqual(snapshot(root), final)
            for original, changed in ((b'argc >= 7 ) inner', b'argc >= 6 ) inner'),
                                      (b'argv[7].Convert2Boolean()', b'argv[9].Convert2Boolean()')):
                write(root, files)
                (root / SOURCE).write_bytes(files[SOURCE].replace(original, changed))
                before = snapshot(root)
                with self.assertRaises(ValueError):
                    patch.patch_sdk(root)
                self.assertEqual(snapshot(root), before)

    def test_exact_1513_constructor_reads_flags_after_strength_and_quality(self):
        exe = client_executable(self)
        self.assertEqual(exe.cstr(0x15d565c), 'flash.filters::DropShadowFilter()')
        # 00de4030: argc >= 9/10/11; Value stride 16, argv[8/9/10].
        for address, expected in (
                (0xde41d6, '83fe09720e8d8f80000000'),
                (0xde41e9, '83fe0a720e8d8f90000000'),
                (0xde41fc, '83fe0b720e8d8fa0000000')):
            self.assertEqual(exe.code(address, len(bytes.fromhex(expected))), bytes.fromhex(expected))
        # The three defaults are false; the native flags are inner/knockout/hide.
        self.assertEqual(exe.code(0xde4081, 12), bytes.fromhex('c645ff00c645fe00c645fd00'))
        for address, shift in ((0xde437b, 5), (0xde4390, 4), (0xde43a5, 6)):
            self.assertEqual(exe.code(address, 3), bytes([0xc1, 0xe1, shift]))

    def test_patched_real_sdk_translation_unit_compiles(self):
        cxx = shutil.which('i686-w64-mingw32-g++')
        if not cxx:
            self.skipTest('i686 MinGW compiler required for the SDK translation-unit check')
        checkout = Path(os.environ.get('WOT_SCALEFORM_SDK_GIT',
                        '/Users/peng/.cache/wot-independent-client/scaleform-sdk-4.2'))
        with tempfile.TemporaryDirectory(prefix='scaleform-shadow-sdk-') as directory:
            root = Path(directory)
            archive = subprocess.run(['git', '-C', str(checkout), 'archive', REVISION,
                                      'Src', 'Include', '3rdParty/libpng-1.5.13/png.h'],
                                     check=True, capture_output=True).stdout
            subprocess.run(['tar', '-x', '-C', str(root)], input=archive, check=True)
            load_patch().patch_sdk(root)
            result = subprocess.run([cxx, '-std=c++14', '-w', '-c', '-DSF_BUILD_STATICLIB',
                                     '-DSF_BUILD_SHIPPING', '-ISrc', '-IInclude', '-ISrc/GFx',
                                     SOURCE, '-o', 'shadow.o'], cwd=root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr[-4000:])


if __name__ == '__main__':
    unittest.main()
