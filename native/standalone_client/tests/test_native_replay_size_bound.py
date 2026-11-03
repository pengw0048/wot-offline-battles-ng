"""Both native replay threads enforce the cumulative decompressed bound."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_stock_wire_codec import python27_audit_runtime

ROOT = Path(__file__).resolve().parents[1]
BIGWORLD = ROOT / 'bigworld'

FIXTURE = r'''
import gzip, os, struct, sys
sys.path.insert(0, sys.argv[1])
import offline_client_wire_native as wire
frame = wire.encode(dict(type='team_chat', text='x' * 512))
path = os.path.join(sys.argv[1], 'bounded.wotlanreplay')
writer = wire.replay_writer(path)
count = 0
while wire.record_frame(writer, frame, 1.):
    count += 1
    assert count < 20
assert count > 0
ok, error = wire.replay_finish(writer)
assert not ok and 'size bound' in error, (ok, error)

# A highly compressible file is bounded by logical bytes, including its magic
# and record headers. It must also fail without a launcher preflight.
path = os.path.join(sys.argv[1], 'over-limit.wotlanreplay')
record = struct.pack('<BdI', 4, 1., len(frame)) + frame
with gzip.open(path, 'wb') as output:
    output.write('WOTLANR3' + record * 20)
reader = wire.replay_reader(path)
read = 0
try:
    while wire.replay_read(reader) is not None:
        read += 1
except ValueError:
    pass
else:
    raise AssertionError('over-limit decompressed replay accepted')
assert read == count, (read, count)

# The previous raw-wire schema must fail at the container boundary, before decoding.
legacy = os.path.join(sys.argv[1], 'previous-schema.wotlanreplay')
with gzip.open(legacy, 'wb') as output:
    output.write('WOTLANR2' + record)
with open(legacy, 'rb') as stream:
    before = stream.read()
try:
    wire.replay_reader(legacy)
except ValueError as error:
    assert 'requires WOTLANR3' in str(error), error
else:
    raise AssertionError('Previous raw-wire replay schema was admitted')
with open(legacy, 'rb') as stream:
    assert stream.read() == before

# A bounded stream still reaches a clean EOF.
path = os.path.join(sys.argv[1], 'within-limit.wotlanreplay')
with gzip.open(path, 'wb') as output:
    output.write('WOTLANR3' + record * count)
reader = wire.replay_reader(path)
for unused in range(count):
    assert wire.replay_read(reader)[2]['text'] == 'x' * 512
assert wire.replay_read(reader) is None
# The exact logical boundary is valid, including a clean EOF probe.
text = 'x' * (8192 - 8 - 13 - len(frame) + 512)
exact = wire.encode(dict(type='team_chat', text=text))
assert len(exact) + 13 + 8 == 8192
path = os.path.join(sys.argv[1], 'exact-limit.wotlanreplay')
writer = wire.replay_writer(path)
assert wire.record_frame(writer, exact, 1.)
assert wire.replay_finish(writer)[0]
reader = wire.replay_reader(path)
assert wire.replay_read(reader)[2]['text'] == text
assert wire.replay_read(reader) is None
print('Native receive writer and streaming reader enforce the same logical bound')
'''


class ReplaySizeBoundTests(unittest.TestCase):
    def test_background_writer_and_reader_refuse_the_same_overflow(self):
        py27 = python27_audit_runtime()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='replay-size-bound-') as directory:
            folder = Path(directory)
            source = (BIGWORLD / 'stock_wire_replay.cpp').read_text()
            bound = 'const unsigned long long max_total = 8ULL * 1024ULL * 1024ULL * 1024ULL;'
            self.assertEqual(1, source.count(bound))
            # Exercise the production boundary at a small size instead of
            # allocating eight GiB. No production configuration is added.
            copied = folder / 'stock_wire_replay.cpp'
            copied.write_text(source.replace(bound, 'const unsigned long long max_total = 8192ULL;'))
            link = ['-bundle', '-undefined', 'dynamic_lookup'] if sys.platform == 'darwin' else ['-shared', '-fPIC']
            command = ['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-parameter', '-Wno-missing-field-initializers', '-DWOT_WIRE_HOST_BUILD']
            command += link + ['-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(BIGWORLD)]
            command += [str(BIGWORLD / 'stock_wire_codec.cpp'), str(copied),
                        str(BIGWORLD / 'stock_wire_replica_store.cpp'),
                        str(ROOT / 'tests/stock_native_client_state_host.cpp'),
                        str(ROOT / 'tests/stock_replay_preflight_host.cpp'),
                        '-lz', '-o', str(folder / 'offline_client_wire_native.so')]
            built = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(0, built.returncode, built.stdout + built.stderr)
            (folder / 'fixture.py').write_text(FIXTURE)
            result = subprocess.run([py27, '-B', str(folder / 'fixture.py'), str(folder)],
                                    text=True, capture_output=True, timeout=30)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
