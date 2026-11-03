"""Keep SDK preset overlays from replacing the exact client's quality catalogue."""
import base64
import importlib.util
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = ROOT.parents[1]
RESOURCE = 'system/data/graphics_settings_presets.xml'
sys.path.insert(0, str(REPOSITORY / 'tools'))
from packed_xml import read_packed_xml, PackedElement, TYPE_COMPRESSED_STRING
from prepare_native_client import OriginalResources


def packed_profiles(data):
    """Use the existing packed reader and PackedSection string conversion."""
    def element(value):
        return value.value if isinstance(value.value, PackedElement) else value

    def scalar(value):
        if value.value_type == TYPE_COMPRESSED_STRING:
            return base64.b64encode(value.value).decode('ascii')
        return value.value.decode('utf8') if isinstance(value.value, bytes) else value.value

    profiles = {}
    for name, value in read_packed_xml(data).children:
        if name != b'graphicsPreferences':
            continue
        preset = element(value)
        settings = {}
        for name, value in preset.children:
            if name != b'entry':
                continue
            fields = {name.decode('ascii'): scalar(value) for name, value in element(value).children}
            settings[fields['label']] = fields['activeOption']
        profiles[scalar(preset.value)] = settings
    return profiles


class ShadowQualityResourceTests(unittest.TestCase):
    def test_sdk_overlay_yields_to_the_original_complete_catalogue(self):
        client = os.environ.get('WOT_0922_CLIENT')
        resources = Path(os.environ.get('WOT_PUBLIC_RESOURCES',
            '/Users/peng/.cache/wot-independent-client/builds/c4009598/client/engine-res'))
        if not client or not (resources / RESOURCE).is_file():
            self.skipTest('Exact #1513 client and pristine public SDK resources are required')
        spec = importlib.util.spec_from_file_location(
            'shadow_quality_patch', ROOT / 'bigworld/patch_stock_deferred_buffers.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        with OriginalResources(Path(client)) as original, tempfile.TemporaryDirectory(
                prefix='shadow-quality-resource-') as temporary:
            root = Path(temporary)
            shutil.copytree(resources / 'shaders', root / 'shaders')
            overlay = root / RESOURCE
            overlay.parent.mkdir(parents=True)
            shutil.copyfile(resources / RESOURCE, overlay)
            original_bytes = original.read(RESOURCE)
            unused_priority, package, unused_member = original.index[RESOURCE.lower()]
            self.assertEqual(package.name, 'misc.pkg')
            original_stat = package.stat()

            def lookup():
                # The engine-res mount precedes the original ordered mounts.
                return overlay.read_bytes() if overlay.is_file() else original.read(RESOURCE)

            public_bytes = lookup()
            self.assertNotEqual(public_bytes, original_bytes)
            # The public XML comment itself contains "--", which BigWorld
            # accepts but a strict XML parser rejects. Its data is ordinary XML.
            public = ET.fromstring(re.sub(rb'<!--.*?-->', b'', public_bytes, flags=re.S))
            public_profiles = {
                preset.text.strip(): {entry.findtext('label').strip(): int(entry.findtext('activeOption'))
                                      for entry in preset.findall('entry')}
                for preset in public.findall('graphicsPreferences')}
            self.assertEqual(set(public_profiles), {'MAX', 'HIGH', 'MEDIUM', 'LOW', 'MIN'})
            self.assertTrue(all(len(values) == 12 for values in public_profiles.values()))
            self.assertEqual(public_profiles['HIGH']['SHADOWS_QUALITY'], 1)

            expected_high = {
                'RENDER_PIPELINE': 0, 'TEXTURE_QUALITY': 1, 'SHADOWS_QUALITY': 2,
                'DECALS_QUALITY': 1, 'LIGHTING_QUALITY': 1, 'TERRAIN_QUALITY': 1,
                'WATER_QUALITY': 1, 'FAR_PLANE': 1, 'FLORA_QUALITY': 1,
                'OBJECT_LOD': 1, 'SPEEDTREE_QUALITY': 1, 'POST_PROCESSING_QUALITY': 1,
                'MOTION_BLUR_QUALITY': 3, 'SNIPER_MODE_EFFECTS_QUALITY': 1,
                'EFFECTS_QUALITY': 1, 'SNIPER_MODE_GRASS_ENABLED': 1,
                'VEHICLE_DUST_ENABLED': 1, 'VEHICLE_TRACES_ENABLED': 1,
                'SEMITRANSPARENT_LEAVES_ENABLED': 1}
            missing = set(expected_high) - set(public_profiles['HIGH'])
            self.assertEqual(missing, {
                'MOTION_BLUR_QUALITY', 'SNIPER_MODE_EFFECTS_QUALITY', 'EFFECTS_QUALITY',
                'SNIPER_MODE_GRASS_ENABLED', 'VEHICLE_DUST_ENABLED',
                'VEHICLE_TRACES_ENABLED', 'SEMITRANSPARENT_LEAVES_ENABLED'})
            patch.patch_resources(root)
            self.assertFalse(overlay.exists())
            self.assertEqual(lookup(), original_bytes)
            profiles = packed_profiles(lookup())
            self.assertEqual(set(profiles), set(public_profiles))
            self.assertTrue(all(len(values) == 19 and set(values) == set(expected_high)
                                for values in profiles.values()))
            self.assertEqual(profiles['HIGH'], expected_high)
            self.assertEqual({name: values['SHADOWS_QUALITY'] for name, values in profiles.items()},
                             {'MAX': 1, 'HIGH': 2, 'MEDIUM': 3, 'LOW': 4, 'MIN': 4})
            before = {path.relative_to(root): path.read_bytes()
                      for path in root.rglob('*') if path.is_file()}
            patch.patch_resources(root)
            self.assertEqual(before, {path.relative_to(root): path.read_bytes()
                                      for path in root.rglob('*') if path.is_file()})
            self.assertEqual(lookup(), original_bytes)
            self.assertEqual(original.read(RESOURCE), original_bytes)
            self.assertEqual(package.stat().st_size, original_stat.st_size)
            self.assertEqual(package.stat().st_mtime_ns, original_stat.st_mtime_ns)


if __name__ == '__main__':
    unittest.main()
