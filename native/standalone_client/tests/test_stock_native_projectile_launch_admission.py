"""Exercise the actual Bot launch methods with controlled runtime boundaries."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from native_test_compile_flags import native_test_compile_flags

ROOT = Path(__file__).resolve().parents[1]


def method(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class NativeProjectileLaunchAdmission(unittest.TestCase):
    def test_actual_owner_methods_admit_once_and_preserve_canonical_retirement(self):
        source = (ROOT / 'bigworld/stock_native_projectile_python.cpp').read_text()
        host = (ROOT / 'tests/stock_native_projectile_launch_admission_host.cpp').read_text()
        for marker, signature in (
                ('PACKED', 'Value packedLaunch('),
                ('LAUNCHES', 'std::vector<LaunchMessage> launches('),
                ('RESULT', 'bool launchResult('),
                ('CONFIRM_SEQUENCE', 'bool confirmSequence('),
                ('CONFIRMED', 'void confirmed('),
                ('MATCHING_ROUND', 'bool matchingRound('),
                ('EVENT', 'bool event(')):
            body = method(source, signature).replace(')override{', '){')
            host = host.replace('@' + marker + '@', body)
        with tempfile.TemporaryDirectory(prefix='native-launch-admission-') as temporary:
            directory = Path(temporary)
            path = directory / 'host.cpp'
            path.write_text(host)
            binary = directory / 'admission'
            result = subprocess.run(['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
                # The production bridge uses compact single-line guards.
                '-Wno-misleading-indentation', *native_test_compile_flags('c++'), str(path), '-o', str(binary)],
                text=True, capture_output=True)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], text=True, capture_output=True, timeout=20)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertIn('canonical retirement passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
