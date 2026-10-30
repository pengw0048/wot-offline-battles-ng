"""Run the actual Python-2 recorder against the unchanged Python-3 writer."""
from __future__ import print_function
import cPickle
import copy
import os
import sys
import time

sys.path.insert(0, sys.argv[1])
sys.path.insert(0, sys.argv[5])
from effective_params_fixture import effective_params
from gui.mods.offline_lan_0922 import stock_snapshot_static as static
from gui.mods.offline_lan_0922 import stock_replay_codec as codec
from gui.mods.offline_lan_0922 import replay_process, _original_replay_process
from gui.mods.offline_lan_0922 import offline_replay, replay_reader


def wait(recorder):
    deadline = time.time() + 15.
    while not recorder.done and time.time() < deadline:
        time.sleep(.01)
    assert recorder.done, recorder.stats()
    recorder._thread.join(1.)


params = static.freeze(effective_params())
outfits = static.freeze({'1': 'YWJj'})
static_list = static.freeze([{'value': [1, 2, 3]}])
def normalizer(value):
    return value
params.normalized(normalizer)  # Canonical memo must never enter IPC.
snapshot = dict(type='snapshot', round_id=1,
                players=[dict(id=1, effective_params=params, outfits=outfits,
                              pose=dict(x=1., y=2., z=3.))],
                bots=[dict(id=n, effective_params=params, pose=dict(x=float(n)))
                      for n in range(2, 31)])
header = dict(welcome=dict(type='welcome'), start=dict(type='battle_start',
              bot_manifest=snapshot['bots']), snapshot=snapshot, config={})
command = lambda: [sys.argv[3], '-u', sys.argv[2]]

# Reproduce the real asynchronous failure with the unadapted producer.
bad = _original_replay_process.ProcessRecorder(
    os.path.join(sys.argv[4], 'negative'), 2, header, command_factory=command)
wait(bad)
assert bad.failed and 'IPC global objects are forbidden' in bad.failure, bad.stats()
assert not os.path.exists(bad.path)
raw = cPickle.dumps(dict(op='start', header=header), 2)
try:
    replay_reader._decode(raw)
except ValueError:
    pass
else:
    raise AssertionError('original restricted playback decoder admitted globals')

good = replay_process.ProcessRecorder(
    os.path.join(sys.argv[4], 'positive'), 2, header, command_factory=command)
good.append('wire', snapshot, moment=.1)
snapshot['players'][0]['pose']['x'] = 999.  # Bytes were frozen before queuing.
assert snapshot['players'][0]['effective_params'] is params

# An immutable value version is detached once, not once per snapshot. Dynamic
# state remains unprojected until the synchronous production cPickle freeze.
original_detach = static.detach
original_deepcopy = copy.deepcopy
def forbidden(*unused, **unused_kw):
    raise AssertionError('recording traversed a cached static version again')
static.detach = forbidden
copy.deepcopy = forbidden
try:
    for n in range(3):
        good.append('wire', snapshot, moment=.2 + n * .1)
finally:
    static.detach = original_detach
    copy.deepcopy = original_deepcopy

# New versions and explicit empty/null replacements remain observable.
new = dict(snapshot)
new['players'] = [dict(id=1, effective_params=static.freeze({}), outfits=None)]
good.append('wire', new, moment=.5)
listed = dict(snapshot)
listed['players'] = [dict(id=1, outfits=static_list)]
good.append('wire', listed, moment=.6)
hinted = offline_replay.capture_message(snapshot, (True, {1: ('effective_params', 'outfits')}))
good.append('wire', hinted, moment=.7)
good.close('test')
wait(good)
assert not good.failed and good._saved, good.stats()
reader = offline_replay.Reader(good.path)
assert len(reader.header['snapshot']['bots']) == 29
first = reader.next()
assert first['data']['players'][0]['pose']['x'] == 1.
assert first['data']['players'][0]['effective_params'] == static.detach(params)
rows = [reader.next() for n in range(6)]
assert rows[3]['data']['players'][0]['effective_params'] == {}
assert rows[3]['data']['players'][0]['outfits'] is None
assert rows[4]['data']['players'][0]['outfits'] == [{'value': [1, 2, 3]}]
assert 'effective_params' not in rows[5]['data']['players'][0]
assert reader.next()['type'] == 'end' and reader.ended
reader.close()

value = dict(op='record', kind='wire', data=listed)
assert replay_reader._decode(codec.dumps(value, 2))['data']['players'][0]['outfits'] == [{'value': [1, 2, 3]}]
with open(os.path.join(sys.argv[4], 'plain.pickle'), 'wb') as f:
    f.write(codec.dumps(value, 2))
with open(os.path.join(sys.argv[4], 'forbidden.pickle'), 'wb') as f:
    # An undeclared dynamic location is not a license to admit any subclass.
    f.write(codec.dumps(dict(op='record', kind='wire', data=dict(type='snapshot', opaque=params)), 2))
print('original_writer_rejected=True fixed_writer_saved=True bots=29 records=7 static_rewalks=0')
