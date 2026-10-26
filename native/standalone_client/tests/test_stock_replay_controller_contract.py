"""Read the exact #1513 PE/PYC boundary; this is not native playback acceptance."""
from __future__ import print_function
import marshal
import os
import re
import struct
import sys
import types
import unittest
import zipfile

METHODS = (
    'startRecording', 'startPlayback', 'beginTimeWarp', 'getTimeMark', 'stop',
    'onClientReady', 'onCommonSfwUnloaded', 'onAmmoButtonPressed',
    'registerWotReplayFileExtension', 'getAutoStartFileName', 'setResultingFileName',
    'getGunReloadAmountLeft', 'setGunReloadTime', 'getArcadeGunMarkerSize',
    'setArcadeGunMarkerSize', 'getSPGGunMarkerParams', 'setSPGGunMarkerParams',
    'getAimClipPosition', 'setAimClipPosition', 'getConsumableSlotCooldownAmount',
    'setActiveConsumableSlot', 'setArenaInfoStr', 'getArenaInfoStr',
    'setArenaStatisticsStr', 'onBattleChatMessage', 'onLockTarget', 'onSniperMode',
    'onSetCruiseMode', 'isPlaying', 'isEffectNeedToPlay', 'onServerAim',
    'onSetEquipmentID', 'resetArenaPeriod', 'confirmDlgAccepted', 'setDataCallback',
    'serializeCallbackData', 'getCallbackHandler', 'isFileCompressed',
    'setupStreamExcludeFilter', 'setupAvatarMethodExcludeFilter')
CALLBACKS = ('replayFinishedCallback', 'controlModeChangedCallback',
             'ammoButtonPressedCallback', 'playerVehicleIDChangedCallback',
             'clientVersionDiffersCallback', 'battleChatMessageCallback',
             'lockTargetCallback', 'cruiseModeCallback', 'equipmentIdCallback',
             'warpFinishedCallback', 'sniperModeCallback')


class PE:
    def __init__(self, path):
        with open(path, 'rb') as source:
            self.data = source.read()
        pe, = struct.unpack_from('<I', self.data, 0x3c)
        machine, count = struct.unpack_from('<HH', self.data, pe + 4)
        if machine != 0x14c:
            raise ValueError('The replay contract requires the pinned x86 client')
        optional = pe + 24
        optional_size, = struct.unpack_from('<H', self.data, pe + 20)
        self.base, = struct.unpack_from('<I', self.data, optional + 28)
        self.sections = [struct.unpack_from('<8sIIIIIIHHI', self.data, optional + optional_size + i * 40) for i in range(count)]

    def read(self, address, size):
        rva = address - self.base
        for section in self.sections:
            if section[2] <= rva < section[2] + section[3]:
                offset = section[4] + rva - section[2]
                return self.data[offset:offset + size]
        raise ValueError('Replay contract address is not file-backed')


@unittest.skipUnless(os.environ.get('WOT_0922_CLIENT'), 'Exact client data requires WOT_0922_CLIENT')
class ReplayContract(unittest.TestCase):
    def test_exact_method_table_and_wrapper_surface(self):
        image = PE(os.path.join(os.environ['WOT_0922_CLIENT'], 'WorldOfTanks.exe'))
        basicsize, = struct.unpack('<I', image.read(0x1a0f128, 4))
        self.assertEqual(basicsize, 0x220)
        methods = []
        for index in range(41):
            name, function, flags, doc = struct.unpack('<4I', image.read(0x1a0f200 + index * 16, 16))
            if not name:
                break
            methods.append(image.read(name, 90).split(b'\0', 1)[0].decode('ascii'))
            self.assertEqual(flags, 3)
            self.assertTrue(0x6fe000 <= function < 0x701000)
        self.assertEqual(tuple(methods), METHODS)
        source = os.path.join(os.path.dirname(__file__), '..', 'bigworld', 'stock_replay_controller.cpp')
        with open(source, 'r') as stream:
            names = re.findall(r'REPLAY_METHOD\("([^"]+)"', stream.read())
        self.assertEqual(tuple(names), METHODS)

    @unittest.skipUnless(sys.version_info[:2] == (2, 7), 'Exact PYC contract uses CPython 2.7')
    def test_startup_and_cleanup_consumers(self):
        with zipfile.ZipFile(os.path.join(os.environ['WOT_0922_CLIENT'], 'res', 'packages', 'scripts.pkg')) as source:
            code = marshal.loads(source.read('scripts/client/BattleReplay.pyc')[8:])
        cls = next(value for value in code.co_consts if isinstance(value, types.CodeType) and value.co_name == 'BattleReplay')
        functions = dict((value.co_name, value) for value in cls.co_consts if isinstance(value, types.CodeType))
        init = functions['__init__']
        self.assertIn('WGReplayController', init.co_names)
        self.assertIn('enableAutoRecordingBattles', init.co_names)
        self.assertIn('clientVersion', init.co_names)
        self.assertTrue(set(CALLBACKS).issubset(set(init.co_names)))
        # The original cleanup clears every callback except sniperModeCallback;
        # native GC/fini must still retire that last reference safely.
        self.assertTrue((set(CALLBACKS) - {'sniperModeCallback'}).issubset(set(functions['destroy'].co_names)))
        for name, width in (('__init__', 1), ('record', 2), ('play', 2), ('stop', 4), ('onClientReady', 1)):
            self.assertEqual(functions[name].co_argcount, width)
        self.assertIn('playerVehicleID', functions['onClientReady'].co_names)
        self.assertIn('onClientReady', functions['onClientReady'].co_names)


if __name__ == '__main__':
    unittest.main()
