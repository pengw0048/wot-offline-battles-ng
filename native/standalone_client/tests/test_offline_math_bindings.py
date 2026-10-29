"""Transactional source adaptation of the fixed numerical extension input."""
import importlib.util
import difflib
import os
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('offline_math_bindings', ROOT/'bigworld/patch_offline_math_bindings.py')
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)


class OfflineMathBindingTests(unittest.TestCase):
    def setUp(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('Fixed v0.10.1 source input is required')
        self.temporary = tempfile.TemporaryDirectory(prefix='offline-math-bindings-')
        self.root = Path(self.temporary.name)
        shutil.copytree(Path(source)/'native', self.root/'native')

    def tearDown(self):
        if hasattr(self, 'temporary'):
            self.temporary.cleanup()

    def contents(self):
        return {p.name:p.read_bytes() for p in (self.root/'native').iterdir() if p.is_file()}

    def test_only_binding_and_platform_seams_change(self):
        before = self.contents()
        adapter.patch(self.root)
        after = self.contents()
        changed = {name for name in before if before[name] != after[name]}
        self.assertEqual(changed, {'offline_math_batch_native.cpp',
            'offline_destructible_prepare_python.inc', 'offline_navigation_query_python.inc',
            'offline_simulation_motion_python.inc'})
        self.assertEqual(len(adapter.METHODS), 103)
        self.assertIn('sim_control_contacts_materialized', adapter.METHODS)
        self.assertIn('sim_control_source_still', adapter.METHODS)
        self.assertEqual(adapter.expanded_methods(self.root/'native', after['offline_math_batch_native.cpp'].decode()), adapter.METHODS)
        for old, new in [(b'#ifdef WOT_HOST_PYTHON',b'#ifdef WOT_PYTHON_C_API')]:
            self.assertEqual(before['offline_destructible_prepare_python.inc'].replace(old,new),
                             after['offline_destructible_prepare_python.inc'])
        self.assertEqual(before['offline_navigation_query_python.inc'].replace(
            b'#ifdef WOT_HOST_PYTHON',b'#if !defined(_WIN32)').replace(
            b'#ifndef WOT_HOST_PYTHON',b'#if defined(_WIN32)'),
            after['offline_navigation_query_python.inc'])
        self.assertEqual(before['offline_simulation_motion_python.inc'].replace(
            b'return hits_[handle-1].get();', b'return hits_[static_cast<unsigned>(handle-1)].get();'),
            after['offline_simulation_motion_python.inc'])
        cpp = after['offline_math_batch_native.cpp'].decode()
        self.assertIn('#ifdef WOT_PYTHON_C_API\n#include <Python.h>', cpp)
        self.assertIn('#if defined(_WIN32)\nextern "C" __declspec(dllexport)', cpp)
        self.assertIn('#if !defined(_WIN32)\n#include <time.h>', cpp)
        # No numerical statement or registered method row in this TU changes.
        for line in difflib.ndiff(before['offline_math_batch_native.cpp'].decode().splitlines(),cpp.splitlines()):
            if line[:2] not in ('+ ','- '):
                continue
            text = line[2:].strip()
            self.assertTrue(not text or text.startswith(('#','//','extern "C"')),text)

    def test_repeat_is_idempotent(self):
        adapter.patch(self.root)
        before = self.contents()
        adapter.patch(self.root)
        self.assertEqual(before, self.contents())

    def test_rotation_repeat_keeps_independent_activation(self):
        adapter.patch(self.root, rotation_query=True)
        before = self.contents()
        adapter.patch(self.root, rotation_query=True)
        self.assertEqual(before, self.contents())
        cpp = before['offline_math_batch_native.cpp'].decode()
        self.assertEqual(adapter.expanded_methods(self.root/'native', cpp), adapter.METHODS)
        self.assertIn('#ifdef WOT_NATIVE_ROTATION_QUERY', cpp)
        self.assertNotIn('#define WOT_NATIVE_ENGINE_QUERY', cpp)
        motion = before['offline_simulation_motion_python.inc'].decode()
        self.assertIn('#ifdef WOT_NATIVE_ENGINE_QUERY\n            if(!snapshot.supplied_departing)', motion)
        self.assertIn('if(rotation_world && !native_)', motion)

    def test_rotation_partial_initialization_is_rejected_without_mutation(self):
        adapter.patch(self.root, rotation_query=True)
        path = self.root/'native/offline_math_batch_native.cpp'
        complete = path.read_text()
        for seam in ('    if (!stock_rotation_binding::initialize()) return;\n',
                     '#include "stock_motion_rotation_python.inc"\n'):
            path.write_text(complete.replace(seam, '', 1))
            before = self.contents()
            with self.assertRaises(ValueError):
                adapter.patch(self.root, rotation_query=True)
            self.assertEqual(before, self.contents())

    def test_changed_method_table_is_rejected_without_mutation(self):
        path = self.root/'native/offline_math_batch_native.cpp'
        path.write_text(path.read_text().replace('{"driver_aim",', '{"changed_driver_aim",', 1))
        before = self.contents()
        with self.assertRaises(ValueError):
            adapter.patch(self.root)
        self.assertEqual(before, self.contents())

    def test_late_platform_failure_is_transactional(self):
        path = self.root/'native/offline_navigation_query_python.inc'
        path.write_text(path.read_text().replace('#ifndef WOT_HOST_PYTHON', '#if defined(CHANGED)', 1))
        before = self.contents()
        with self.assertRaises(ValueError):
            adapter.patch(self.root)
        self.assertEqual(before, self.contents())

    def test_nested_control_registration_is_not_silently_lost(self):
        path = self.root/'native/offline_simulation_control_python.inc'
        path.write_text(path.read_text().replace('"sim_control_radio"','"changed_control_radio"',1))
        before = self.contents()
        with self.assertRaises(ValueError):
            adapter.patch(self.root)
        self.assertEqual(before,self.contents())

    def test_final_checked_index_seam_failure_is_transactional(self):
        path = self.root/'native/offline_simulation_motion_python.inc'
        path.write_text(path.read_text().replace('return hits_[handle-1].get();','return changed_index;',1))
        before = self.contents()
        with self.assertRaises(ValueError):
            adapter.patch(self.root)
        self.assertEqual(before,self.contents())


if __name__ == '__main__':
    unittest.main()
