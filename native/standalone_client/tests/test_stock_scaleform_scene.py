"""MovieClip.currentScene/currentLabels: the #1513 4.6.34 contract on SDK 4.2.22.

The host runs the pinned SDK bodies over a guarded AS3 fixture. The real SDK
and the exact gui/flash/lobby.swf are exercised only by the Win32 probe.
"""
from pathlib import Path
import importlib.util
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
REVISION = '7d92be560bf1b2e4ecae9ac69e340d4cae9a557e'
MOVIE_CLIP = 'Src/GFx/AS3/Obj/Display/AS3_Obj_Display_MovieClip.cpp'
SDK_FILES = ('Src/GFx/AMP/Amp_Server.h', '3rdParty/libpng-1.5.13/png.h', 'Src/Kernel/SF_SIMD_SSE.h',
             'Src/GFx/AS3/Obj/Utils/AS3_Obj_Utils_ByteArray.cpp', 'Src/GFx/AS3/Obj/AS3_Obj_Date.cpp',
             MOVIE_CLIP, 'Src/GFx/AS3/Obj/AS3_Obj_Global.h', 'Src/GFx/AS3/Obj/AS3_Obj_Global.cpp',
             'Src/GFx/AS3/AS3_VM.h', 'Src/GFx/AS3/AS3_VM.cpp',
             'Src/GFx/AS3/Obj/Utils/AS3_Obj_Utils_ByteArray.h', 'Src/GFx/AS3/Obj/AS3_Obj_Date.h',
             'Src/GFx/AS3/Obj/Utils/AS3_Obj_Utils_Dictionary.h',
             'Src/GFx/AS3/Obj/Gfx/AS3_Obj_Gfx_TextFieldEx.h', 'Src/GFx/AS3/Obj/Gfx/AS3_Obj_Gfx_TextFieldEx.cpp',
             'Src/GFx/AS3/Obj/Filters/AS3_Obj_Filters_DropShadowFilter.cpp',
             'Src/GFx/GFx_Loader.cpp', 'Src/GFx/GFx_LoaderImpl.cpp')
BODIES = (('    void MovieClip::currentLabelsGet(', '    void MovieClip::enabledGet('),
          ('    void MovieClip::scenesGet(', '    void MovieClip::totalFramesGet('),
          ('    SPtr<Instances::fl_display::Scene> MovieClip::CreateFakeScene()',
           '    const MovieDataDef::SceneInfo* MovieClip::GetSceneInfo('))

PRISTINE = '''sceneless-root scene=null labels=[NULL_DEREFERENCE] scenes=[]
sceneless-root-repeat scene=null labels=[NULL_DEREFERENCE] scenes=[]
scene-root-frame-0 scene=scene:Intro labels=[1:start] scenes=[scene:Intro,scene:Loop]
scene-root-frame-1 scene=scene:Intro labels=[1:start] scenes=[scene:Intro,scene:Loop]
scene-root-frame-2 scene=scene:Loop labels=[3:loop,5:end] scenes=[scene:Intro,scene:Loop]
scene-root-frame-4 scene=scene:Loop labels=[3:loop,5:end] scenes=[scene:Intro,scene:Loop]
scene-root-frame-5 scene=null labels=[NULL_DEREFERENCE] scenes=[scene:Intro,scene:Loop]
nested-sprite scene=sprite labels=[2:open] scenes=[sprite]
sceneless-root-construct-failure scene=null labels=[NULL_DEREFERENCE] scenes=[]
scene-root-construct-failure scene=NULL_DEREFERENCE labels=[NULL_DEREFERENCE] scenes=[NULL_DEREFERENCE]
'''
# 00dbb750 falls back to the sprite scene for any unmatched timeline; 00dbb6d0
# guards the scene. A failed Scene construction still dereferences, as in 4.6.34.
PATCHED = '''sceneless-root scene=sprite labels=[1:net_wg_app_impl_LobbyApp] scenes=[]
sceneless-root-repeat scene=sprite labels=[1:net_wg_app_impl_LobbyApp] scenes=[]
scene-root-frame-0 scene=scene:Intro labels=[1:start] scenes=[scene:Intro,scene:Loop]
scene-root-frame-1 scene=scene:Intro labels=[1:start] scenes=[scene:Intro,scene:Loop]
scene-root-frame-2 scene=scene:Loop labels=[3:loop,5:end] scenes=[scene:Intro,scene:Loop]
scene-root-frame-4 scene=scene:Loop labels=[3:loop,5:end] scenes=[scene:Intro,scene:Loop]
scene-root-frame-5 scene=sprite labels=[1:start,3:loop,5:end,6:tail] scenes=[scene:Intro,scene:Loop]
nested-sprite scene=sprite labels=[2:open] scenes=[sprite]
sceneless-root-construct-failure scene=NULL_DEREFERENCE labels=[NULL_DEREFERENCE] scenes=[]
scene-root-construct-failure scene=NULL_DEREFERENCE labels=[NULL_DEREFERENCE] scenes=[NULL_DEREFERENCE]
'''


def pinned_sdk(test):
    checkout = Path(os.environ.get('WOT_SCALEFORM_SDK_GIT',
                                   '/Users/peng/.cache/wot-independent-client/scaleform-sdk-4.2'))
    try:
        return {name: subprocess.run(['git', '-C', str(checkout), 'show', REVISION + ':' + name],
                                     check=True, capture_output=True).stdout for name in SDK_FILES}
    except (OSError, subprocess.CalledProcessError):
        test.skipTest('Pinned Scaleform SDK checkout unavailable')


def load_patch():
    spec = importlib.util.spec_from_file_location('patch_scaleform_sdk', ROOT / 'bigworld/patch_scaleform_sdk.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def bodies(source):
    text = source.replace('\r\n', '\n')
    parts = []
    for begin, end in BODIES:
        start = text.index(begin)
        parts.append(text[start:text.index(end, start)])
    return '\n'.join(parts)


class ScaleformSceneTest(unittest.TestCase):
    def test_patch_is_idempotent_and_fails_closed(self):
        files = pinned_sdk(self)
        patch = load_patch()
        with tempfile.TemporaryDirectory(prefix='scaleform-scene-patch-') as directory:
            root = Path(directory)
            for name, data in files.items():
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                (root / name).write_bytes(data)
            patch.patch_sdk(root)
            first = {name: (root / name).read_bytes() for name in SDK_FILES}
            patch.patch_sdk(root)
            self.assertEqual(first, {name: (root / name).read_bytes() for name in SDK_FILES})
            for name, data in files.items():
                (root / name).write_bytes(data)
            (root / MOVIE_CLIP).write_bytes(files[MOVIE_CLIP].replace(
                b'scene->labelsGet(result);', b'scene->labelsGet( result );'))
            before = {name: (root / name).read_bytes() for name in SDK_FILES}
            with self.assertRaises(ValueError):
                patch.patch_sdk(root)
            self.assertEqual(before, {name: (root / name).read_bytes() for name in SDK_FILES})

    def test_pristine_body_fails_and_patched_body_matches_original_runtime(self):
        files = pinned_sdk(self)
        patch = load_patch()
        with tempfile.TemporaryDirectory(prefix='scaleform-scene-host-') as directory:
            root = Path(directory)
            for name, data in files.items():
                (root / 'sdk' / name).parent.mkdir(parents=True, exist_ok=True)
                (root / 'sdk' / name).write_bytes(data)
            pristine = files[MOVIE_CLIP].decode('utf-8-sig')
            patch.patch_sdk(root / 'sdk')
            patched = (root / 'sdk' / MOVIE_CLIP).read_text(encoding='utf-8-sig')
            outputs = {}
            for label, source in (('pristine', pristine), ('patched', patched)):
                (root / (label + '.inc')).write_text(bodies(source))
                host = root / (label + '.cpp')
                host.write_text((ROOT / 'tests/stock_scaleform_scene_host.cpp').read_text().replace(
                    '@BODIES@', str(root / (label + '.inc'))))
                binary = root / label
                subprocess.run(['c++', '-std=c++14', '-UNDEBUG', '-Wall', '-Werror', str(host), '-o', str(binary)],
                               check=True)
                outputs[label] = subprocess.run([str(binary)], check=True, capture_output=True,
                                                text=True).stdout
            self.assertEqual(outputs['pristine'], PRISTINE)
            self.assertEqual(outputs['patched'], PATCHED)


if __name__ == '__main__':
    unittest.main()
