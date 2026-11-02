"""Native lifecycle routes replace retired bodies without retaining closures."""
import ast
import importlib.util
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    'lifecycle_bindings', ROOT / 'bigworld/stock_native_client_lifecycle_bindings.py')
BINDINGS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BINDINGS)


class LifecycleBindingsTests(unittest.TestCase):
    def execute(self, source):
        native = types.ModuleType('offline_client_lifecycle_native')
        calls = []

        def route(name):
            def invoke(*args):
                calls.append((name, args))
                return calls[-1]
            return invoke

        for name in ('TransportPerformance',) + tuple(
                call.split('(')[0] for call in BINDINGS.RECORDING_PORTS) + tuple(
                call.split('(')[0] for unused, call in BINDINGS.LAN_POLL_PORTS):
            setattr(native, name, route(name))
        namespace = {}
        with patch.dict(sys.modules, {native.__name__: native}):
            exec(compile(source, '<staged lifecycle>', 'exec'), namespace)
        return namespace, calls

    def assert_routes_only(self, source, names):
        functions = [node for node in ast.walk(ast.parse(source))
                     if isinstance(node, ast.FunctionDef) and node.name in names]
        self.assertEqual(set(names), {node.name for node in functions})
        for node in functions:
            self.assertEqual(1, len(node.body), node.name)
            self.assertIsInstance(node.body[0], ast.Return, node.name)
        self.assertNotIn('retired_body', source)

    def test_recording_routes_replace_whole_bodies(self):
        source = 'from __future__ import print_function\n\n'
        for definition in BINDINGS.RECORDING_PORTS:
            source += ('def %s:\n    """retired_body"""\n'
                       '    def retired_body():\n        raise RuntimeError()\n'
                       '    return retired_body()\n\n') % definition
        source += 'def retained_helper():\n    return 19\n'
        staged = BINDINGS.offline_replay(source)
        names = [call.split('(')[0] for call in BINDINGS.RECORDING_PORTS]
        self.assert_routes_only(staged, names)
        namespace, calls = self.execute(staged)
        marker = object()
        for name in names:
            self.assertEqual((name, (marker, namespace)), namespace[name](marker))
        self.assertEqual(len(names), len(calls))
        self.assertEqual(19, namespace['retained_helper']())

    def lan_source(self):
        source = ('from __future__ import print_function\n'
                  '_TRANSPORT_PROFILE_CLOCK = object()\n'
                  'class _TransportPerformance(object):\n'
                  '    retired_body = True\n'
                  'class LANClient(object):\n')
        for name, call in BINDINGS.LAN_POLL_PORTS:
            # The port's final argument is the module namespace, not a Python parameter.
            arguments = call[call.index('(') + 1:-1].rsplit(', globals()', 1)[0]
            if name in ('_dispatch_message', '_notify'):
                arguments += '=None'
            source += ('    def %s(%s):\n        """retired_body"""\n'
                       '        return retired_body\n') % (name, arguments)
        return source + '    def retained_helper(self):\n        return 23\n'

    def test_lan_routes_remove_shadowed_performance_class_and_keep_arguments(self):
        staged = BINDINGS.lan_client(self.lan_source())
        self.assert_routes_only(staged, ['_TransportPerformance'] + [name for name, unused in BINDINGS.LAN_POLL_PORTS])
        self.assertFalse(any(isinstance(node, ast.ClassDef) and node.name == '_TransportPerformance'
                             for node in ast.walk(ast.parse(staged))))
        namespace, calls = self.execute(staged)
        self.assertIn('_TRANSPORT_PROFILE_CLOCK', namespace)
        self.assertEqual(('TransportPerformance', (namespace, 7)), namespace['_TransportPerformance'](7))
        client = namespace['LANClient']()
        for name, call in BINDINGS.LAN_POLL_PORTS:
            args = [object() for unused in call[call.index('(') + 1:-1].split(', ')[1:-1]]
            expected = (call.split('(')[0], tuple([client] + args + [namespace]))
            self.assertEqual(expected, getattr(client, name)(*args))
        self.assertEqual(23, client.retained_helper())
        client._notify('snapshot', {})
        self.assertIsNone(calls[-1][1][-2])

    def test_changed_or_duplicate_recording_seams_are_rejected(self):
        source = 'from __future__ import print_function\n'
        source += ''.join('def %s:\n    pass\n' % call for call in BINDINGS.RECORDING_PORTS)
        for changed in (source.replace('def local_state(runtime):', 'def renamed(runtime):'),
                        source + 'def local_state(runtime):\n    pass\n'):
            with self.assertRaises(ValueError):
                BINDINGS.offline_replay(changed)

    def test_changed_or_duplicate_lan_seams_are_rejected(self):
        source = self.lan_source()
        for changed in (source.replace('def _poll(self):', 'def _poll(other):'),
                        source + '    def _poll(self):\n        pass\n',
                        source.replace('class _TransportPerformance(object):', 'class Renamed(object):')):
            with self.assertRaises(ValueError):
                BINDINGS.lan_client(changed)


if __name__ == '__main__':
    unittest.main()
