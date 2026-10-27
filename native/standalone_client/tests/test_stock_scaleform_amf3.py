"""ByteArray AMF3: the #1513 4.6.34 codec seams on SDK 4.2.22.

The real SDK, the exact gui/flash/lobby.swf DataUtils.cloneObject consumer and
the encoded bytes are exercised only by the Win32 probe.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_stock_scaleform_scene import REVISION, SDK_FILES, load_patch
from test_stock_scaleform_class_alias import pinned_sdk, write

BYTE_ARRAY_H = 'Src/GFx/AS3/Obj/Utils/AS3_Obj_Utils_ByteArray.h'
BYTE_ARRAY_CPP = 'Src/GFx/AS3/Obj/Utils/AS3_Obj_Utils_ByteArray.cpp'
VM_CPP = 'Src/GFx/AS3/AS3_VM.cpp'


def patched(test):
    files = pinned_sdk(test)
    with tempfile.TemporaryDirectory(prefix='scaleform-amf3-') as directory:
        root = Path(directory)
        write(root, files)
        load_patch().patch_sdk(root)
        return files, {name: (root / name).read_text(encoding='latin-1') for name in SDK_FILES}


class ScaleformAMF3Test(unittest.TestCase):
    def test_object_methods_run_the_codec(self):
        files, text = patched(self)
        self.assertIn(b'NOT_IMPLEMENTED("ByteArray::readObject()")', files[BYTE_ARRAY_CPP])
        self.assertIn(b'NOT_IMPLEMENTED("ByteArray::writeObject()")', files[BYTE_ARRAY_CPP])
        source = text[BYTE_ARRAY_CPP]
        self.assertNotIn('NOT_IMPLEMENTED("ByteArray::readObject()")', source)
        self.assertNotIn('NOT_IMPLEMENTED("ByteArray::writeObject()")', source)
        self.assertIn('AMF3ReadValue(scope, result);', source)
        self.assertIn('AMF3WriteValue(scope, object);', source)
        # The codec precedes its first use in readObject.
        self.assertLess(source.index('struct ByteArray::AMF3ReadScope'), source.index('void ByteArray::readObject('))

    def test_every_declared_codec_member_is_defined_once(self):
        unused_files, text = patched(self)
        declared = set(re.findall(r'^\s+\w[\w:]*\s+\*?(AMF3\w+)\(', text[BYTE_ARRAY_H], re.M))
        defined = re.findall(r'ByteArray::(AMF3\w+)\(', text[BYTE_ARRAY_CPP])
        self.assertTrue(declared)
        self.assertEqual(sorted(declared), sorted(set(defined)))
        self.assertEqual(len(defined), len(set(defined)))

    def test_externalizable_error_is_registered_in_order(self):
        unused_files, text = patched(self)
        source = text[VM_CPP]
        mapping = [int(n) for n in re.findall(r'^\s+(\d{4}),$', source.split('errorMappingTable[] =')[1]
                                              .split('};')[0], re.M)]
        self.assertEqual(mapping, sorted(mapping))
        messages = re.findall(r'/\* (\d{4}),(\d+)\*/', source)
        self.assertEqual([int(code) for code, unused in messages], mapping)
        self.assertEqual(messages[-1], ('2173', str(len(mapping) - 1)))
        self.assertIn('eReadExternalNotImplementedError                = 2173,', text['Src/GFx/AS3/AS3_VM.h'])

    def test_codec_compiles_against_the_patched_sdk(self):
        checkout = Path(os.environ.get('WOT_SCALEFORM_SDK_GIT',
                                       '/Users/peng/.cache/wot-independent-client/scaleform-sdk-4.2'))
        with tempfile.TemporaryDirectory(prefix='scaleform-amf3-compile-') as directory:
            root = Path(directory)
            try:
                archive = subprocess.run(['git', '-C', str(checkout), 'archive', REVISION, 'Src', 'Include',
                                          '3rdParty/libpng-1.5.13/png.h'],
                                         check=True, capture_output=True).stdout
            except (OSError, subprocess.CalledProcessError):
                self.skipTest('Pinned Scaleform SDK checkout unavailable')
            subprocess.run(['tar', '-x', '-C', str(root)], input=archive, check=True)
            load_patch().patch_sdk(root)
            command = ['c++', '-std=c++14', '-fsyntax-only', '-w', '-DSF_BUILD_STATICLIB', '-I', 'Src',
                       '-I', 'Include', '-I', 'Src/GFx', BYTE_ARRAY_CPP]
            if sys.platform == 'darwin':
                command[1:1] = ['-arch', 'x86_64']
            result = subprocess.run(command, cwd=root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr[-4000:])


if __name__ == '__main__':
    unittest.main()
