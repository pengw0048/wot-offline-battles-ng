"""The launcher freezes existing overlay wire replies before native startup."""
import base64
import importlib.util
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock
import core
import vehicle_overlays as overlays


class RoomOverlayPayload(unittest.TestCase):
    MEMBER = 'scripts/item_defs/vehicles/ussr/R11_MS-1.xml'

    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(prefix='room-overlay-')
        self.addCleanup(self.folder.cleanup)
        self.game = Path(self.folder.name) / 'game'
        self.game.mkdir()

    def install(self, data=b'packed-xml-data', member=None):
        member = member or self.MEMBER
        manifest = overlays._empty_manifest()
        manifest['activeProfile'] = '测试车辆方案'
        manifest['members'] = [dict(
            sourceMember=member, sourcePackage=overlays.SOURCE_PACKAGE,
            overlayRelativePath=member, overlaySha256=overlays._sha256(data),
            edits=[dict(fieldPath='speedLimits/forward', originalPackedType='integer',
                        originalValue=32, replacementValue=40)])]
        root = self.game / overlays.OVERLAY_ROOT
        installed = root / member
        installed.parent.mkdir(parents=True, exist_ok=True)
        installed.write_bytes(data)
        (root / overlays.MANIFEST_NAME).write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
        return manifest, installed

    def test_absence_and_empty_owned_manifest_keep_original_query_shape(self):
        payload = overlays.room_overlay_payload(str(self.game))
        self.assertEqual(dict(schema=1, members={}, manifest=dict(
            type='vehicle_overlay_manifest', present=False, digest='', profile='',
            manifest=None, members=[])), payload)
        root = self.game / overlays.OVERLAY_ROOT
        root.mkdir(parents=True)
        manifest = overlays._empty_manifest()
        (root / overlays.MANIFEST_NAME).write_text(json.dumps(manifest), encoding='utf-8')
        payload = overlays.room_overlay_payload(str(self.game))
        self.assertTrue(payload['manifest']['present'])
        self.assertEqual(manifest, payload['manifest']['manifest'])
        self.assertEqual({}, payload['members'])

    def test_export_has_exact_member_payload_and_atomic_failure_keeps_old_snapshot(self):
        manifest, installed = self.install()
        target = Path(self.folder.name) / 'worker' / 'room-overlay.json'
        overlays.export_room_overlay(str(self.game), str(target))
        payload = json.loads(target.read_text(encoding='utf-8'))
        self.assertEqual(manifest, payload['manifest']['manifest'])
        self.assertEqual('测试车辆方案', payload['manifest']['profile'])
        member = payload['members'][self.MEMBER]
        self.assertEqual('vehicle_overlay_member_data', member['type'])
        self.assertEqual(installed.read_bytes(), base64.b64decode(member['data_b64']))
        self.assertEqual(len(installed.read_bytes()), member['size'])
        old = target.read_bytes()
        installed.write_bytes(b'changed')
        with self.assertRaisesRegex(overlays.VehicleOverlayError, 'changed'):
            overlays.export_room_overlay(str(self.game), str(target))
        self.assertEqual(old, target.read_bytes())
        self.assertEqual(['room-overlay.json'], [p.name for p in target.parent.iterdir()])

    def test_original_store_differential_keeps_manifest_bytes_identity(self):
        source = os.environ.get('WOT_FORK_SOURCE')
        if not source:
            self.skipTest('Pinned source is required for the original overlay store oracle')
        path = Path(source) / 'server' / 'vehicle_overlay_store.py'
        spec = importlib.util.spec_from_file_location('pinned_vehicle_overlay_store', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.install(b'\x00\x01\xffexact-packed-xml')
        original = module.VehicleOverlayStore(str(self.game))
        actual = overlays.room_overlay_payload(str(self.game))
        self.assertEqual(original.manifest_payload(), actual['manifest'])
        self.assertEqual(original.member_payload(self.MEMBER), actual['members'][self.MEMBER])
        self.assertIsNone(original.member_payload('unknown'))
        self.assertNotIn('unknown', actual['members'])

    def test_existing_member_and_manifest_bounds_fail_before_publication(self):
        unused_manifest, installed = self.install(b'four')
        with mock.patch.object(overlays, 'MAX_SHARED_OVERLAY_MEMBER_BYTES', 3):
            with self.assertRaisesRegex(overlays.VehicleOverlayError, 'size is invalid'):
                overlays.room_overlay_payload(str(self.game))
        with mock.patch.object(overlays, 'MAX_SHARED_OVERLAY_TOTAL_BYTES', 3):
            with self.assertRaisesRegex(overlays.VehicleOverlayError, 'too large'):
                overlays.room_overlay_payload(str(self.game))
        with mock.patch.object(overlays, 'MAX_OVERLAY_MANIFEST_BYTES', 1):
            with self.assertRaisesRegex(overlays.VehicleOverlayError, 'larger than'):
                overlays.room_overlay_payload(str(self.game))
        self.install(b'')
        with self.assertRaisesRegex(overlays.VehicleOverlayError, 'size is invalid'):
            overlays.room_overlay_payload(str(self.game))
        installed.unlink()
        with self.assertRaisesRegex(overlays.VehicleOverlayError, 'missing'):
            overlays.room_overlay_payload(str(self.game))

    def test_foreign_duplicate_checksum_and_symlink_rejections_reuse_launcher_validation(self):
        manifest, installed = self.install()
        path = self.game / overlays.OVERLAY_ROOT / overlays.MANIFEST_NAME
        for changed in [dict(manifest, schema=999),
                        dict(manifest, members=manifest['members'] * 2),
                        dict(manifest, members=[dict(manifest['members'][0], overlaySha256='invalid')])]:
            path.write_text(json.dumps(changed), encoding='utf-8')
            with self.assertRaises(overlays.VehicleOverlayError):
                overlays.room_overlay_payload(str(self.game))
        path.write_text(json.dumps(manifest))
        outside = Path(self.folder.name) / 'outside.xml'
        outside.write_bytes(installed.read_bytes())
        installed.unlink()
        try:
            installed.symlink_to(outside)
        except OSError:
            self.skipTest('Creating symlinks requires platform support')
        with self.assertRaises(overlays.VehicleOverlayError):
            overlays.room_overlay_payload(str(self.game))

    def test_host_config_only_publishes_after_overlay_is_ready(self):
        self.install()
        config_path = core.write_room_host_config(str(self.game))
        value = json.loads(Path(config_path).read_text())
        self.assertEqual(core.room_host_overlay_path(str(self.game)), value['vehicle_overlay_path'])
        overlay = json.loads(Path(value['vehicle_overlay_path']).read_text())
        self.assertTrue(overlay['manifest']['present'])
        self.assertNotIn('data_b64', value)
        old = Path(config_path).read_bytes()
        with mock.patch.object(overlays, 'export_room_overlay', side_effect=overlays.VehicleOverlayError('refused')):
            with self.assertRaisesRegex(overlays.VehicleOverlayError, 'refused'):
                core.write_room_host_config(str(self.game), team1_size=1)
        self.assertEqual(old, Path(config_path).read_bytes())


if __name__ == '__main__':
    unittest.main()
