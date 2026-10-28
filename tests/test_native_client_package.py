"""Reject binaries that cannot address the supported client's resource load."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    'package_native_client', ROOT / 'tools/package_native_client.py')
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


def pe_header(machine=0x014c, characteristics=0x0122, magic=0x010b):
    data = bytearray(256)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 60, 128)
    data[128:132] = b'PE\0\0'
    struct.pack_into('<H', data, 132, machine)
    struct.pack_into('<HHH', data, 148, 224, characteristics, magic)
    return data


class NativeClientPackageTests(unittest.TestCase):
    def test_accepts_large_address_aware_x86_executable(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'bwclient_h.exe'
            executable.write_bytes(pe_header())
            PACKAGE.validate_native_executable(executable)

    def test_rejects_two_gib_binary_before_packaging_mutates_output(self):
        with tempfile.TemporaryDirectory() as directory:
            client = Path(directory)
            (client / 'bwclient_h.exe').write_bytes(
                pe_header(characteristics=0x0102))
            with self.assertRaisesRegex(ValueError, 'LARGEADDRESSAWARE'):
                PACKAGE.package(client)
            self.assertEqual([p.name for p in client.iterdir()], ['bwclient_h.exe'])

    def test_rejects_wrong_architecture_and_nonexecutable_images(self):
        for data in (pe_header(machine=0x8664, magic=0x020b),
                     pe_header(magic=0x020b),
                     pe_header(characteristics=0x0120)):
            with self.subTest(data=data[128:154]), tempfile.TemporaryDirectory() as directory:
                executable = Path(directory) / 'bwclient_h.exe'
                executable.write_bytes(data)
                with self.assertRaisesRegex(ValueError, '32-bit x86'):
                    PACKAGE.validate_native_executable(executable)

    def test_rejects_truncated_headers(self):
        data = pe_header()
        for length in (0, 63, 128, 153):
            with self.subTest(length=length), tempfile.TemporaryDirectory() as directory:
                executable = Path(directory) / 'bwclient_h.exe'
                executable.write_bytes(data[:length])
                with self.assertRaisesRegex(ValueError, 'header'):
                    PACKAGE.validate_native_executable(executable)


if __name__ == '__main__':
    unittest.main()
