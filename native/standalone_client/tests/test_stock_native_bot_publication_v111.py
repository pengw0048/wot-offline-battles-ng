"""Compare the native compact publication with the v1.1.1 codec."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = os.environ.get('WOT_FORK_SOURCE')
PROBE = r'''
#include "stock_native_bot_publication.hpp"
#include <iostream>
using namespace stock_native_bot_publication;
int main() {
    Input input; input.scalars[0]=7;input.scalars[37]=4.;
    input.flags=LocalCombatManoeuvre|AmmoMagazineStarted|AimFeedback|AimFeedbackAligned;
    input.aim_feedback_kind=1;input.aim_feedback_id=9;input.aim_feedback_error=.25;
    auto row=encode(input,0.);
    for (auto value:row) std::cout<<value<<' ';
}
'''

@unittest.skipUnless(SOURCE, 'set WOT_FORK_SOURCE to v1.1.1 source')
class PublicationV111Test(unittest.TestCase):
    def test_compact_row_matches_original(self):
        source = Path(SOURCE)
        sys.path.insert(0, str(source / 'src/res/scripts/client'))
        try:
            from gui.mods.offline_lan_0922 import bot_state_codec
            expected = bot_state_codec.encode_row(dict(
                id=7, alive=False, world_pose=False, turret_yaw=4.,
                local_combat_manoeuvre=True, ammo_magazine_started=True,
                aim_feedback_kind='human', aim_feedback_id=9,
                aim_feedback_error=.25, aim_feedback_aligned=True))
        finally:
            sys.path.pop(0)
        with tempfile.TemporaryDirectory(prefix='bot-pub-v111-') as folder:
            folder = Path(folder)
            law = (source/'native/offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0]
            (folder/'offline_release_drive_law.hpp').write_text('#pragma once\n#include <algorithm>\n#include <cmath>\n'+law)
            (folder/'probe.cpp').write_text(PROBE)
            subprocess.run(['c++', '-std=c++17', '-I'+str(folder), '-I'+str(ROOT/'bigworld'),
                            '-I'+str(source/'native'), str(ROOT/'bigworld/stock_native_bot_publication.cpp'),
                            str(folder/'probe.cpp'), '-o', str(folder/'probe')], check=True)
            actual = [int(x) for x in subprocess.check_output([str(folder/'probe')], text=True).split()]
        self.assertEqual(actual, expected)

if __name__ == '__main__':
    unittest.main()
