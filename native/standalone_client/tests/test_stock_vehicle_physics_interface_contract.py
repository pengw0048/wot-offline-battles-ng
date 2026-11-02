"""Stock-client and configured-simulation physics surfaces are distinct.

This checks the registered NG surface, not names in WorldOfTanks.exe. The
recorded stock references can also be re-derived from the pinned bytecode.
It does not prove that a registered method has the correct native behavior.
"""
import json
import os
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]

# Immediate LOAD_FAST physics -> LOAD_ATTR/STORE_ATTR in the exact #1513 code
# objects. Calls and properties both require Python type registration.
STOCK_REFERENCES = {
    'physics_shared.configurePhysics': [
        'centerOfMass', 'configure', 'freezeAccelEpsilon',
        'freezeAngAccelEpsilon', 'freezeAngVelEpsilon', 'freezeVelEpsilon',
        'hullCOMZ', 'isFrozen', 'movementSignals', 'simulationYBound'],
    'physics_shared.initVehiclePhysicsClient': [
        'addDamperSpring', 'centerOfMass', 'removeAllDamperSprings'],
    'Vehicle.__startWGPhysics': [
        'movementSignals', 'owner', 'setArenaBounds', 'staticMode',
        'visibilityMask'],
    'SuspensionController.onSiegeStateChanged': ['setDamperSpringsLength'],
}

# These members are called, rather than merely read, by the consumers above.
# A property bearing the same name is not evidence of a callable method.
REQUIRED_METHODS = frozenset((
    'configure', 'removeAllDamperSprings', 'addDamperSpring',
    'setArenaBounds', 'setDamperSpringsLength', 'setHullAimingAnglesDelta'))

BYTECODE_REFERENCES = r'''
import dis, json, marshal, sys, types, zipfile
archive = zipfile.ZipFile(sys.argv[1] + '/res/packages/scripts.pkg')
targets = {
 'scripts/common/physics_shared.pyc': {
  'configurePhysics': 'physics_shared.configurePhysics',
  'initVehiclePhysicsClient': 'physics_shared.initVehiclePhysicsClient'},
 'scripts/client/Vehicle.pyc': {'__startWGPhysics': 'Vehicle.__startWGPhysics'},
 'scripts/client/vehicle_systems/components/suspension_controller.pyc': {
  'onSiegeStateChanged': 'SuspensionController.onSiegeStateChanged'}}
result = {}
def walk(code, selected):
 if code.co_name in selected:
  instructions = []; offset = 0
  while offset < len(code.co_code):
   opcode = ord(code.co_code[offset]); offset += 1; argument = None
   if opcode >= dis.HAVE_ARGUMENT:
    argument = ord(code.co_code[offset]) + 256 * ord(code.co_code[offset + 1]); offset += 2
   instructions.append((dis.opname[opcode], argument))
  names = set()
  for index, (opcode, argument) in enumerate(instructions[:-1]):
   if opcode == 'LOAD_FAST' and code.co_varnames[argument] == 'physics':
    following, member = instructions[index + 1]
    if following in ('LOAD_ATTR', 'STORE_ATTR'): names.add(code.co_names[member])
  result[selected[code.co_name]] = sorted(names)
 for child in code.co_consts:
  if isinstance(child, types.CodeType): walk(child, selected)
for member, selected in targets.items():
 walk(marshal.loads(archive.read(member)[8:]), selected)
print(json.dumps(result, sort_keys=True))
'''


def without_comments(source):
    return re.sub(r'//[^\n]*|/\*.*?\*/', '', source, flags=re.S)


def registered_surface(source, type_name):
    source = without_comments(source)
    result = {}
    for section, macro in (('METHODS', 'PY_METHOD'),
                           ('ATTRIBUTES', 'PY_ATTRIBUTE')):
        match = re.search(r'PY_BEGIN_' + section + r'\(\s*' + type_name +
                          r'\s*\)(.*?)PY_END_' + section + r'\(\)',
                          source, re.S)
        if not match:
            raise AssertionError('Missing NG registration block: ' + type_name)
        result[section] = set(re.findall(macro + r'\(\s*(\w+)\s*\)', match.group(1)))
    return result


def native_physics_references(source):
    # Detect simulation-only calls accidentally restored on client-only physics.
    source = without_comments(source)
    return set(re.findall(
        r'py::optional\(\s*physics\.get\(\)\s*,\s*"(\w+)"\s*\)', source))


def require_members(registered, consumers):
    for consumer, references in consumers.items():
        missing = sorted(member for member in references
                         if member not in registered['METHODS' if member in REQUIRED_METHODS else 'ATTRIBUTES'])
        if missing:
            raise AssertionError('%s requires missing NG WGVehiclePhysics members: %s' %
                                 (consumer, ', '.join(missing)))


class StockVehiclePhysicsInterfaceContractTests(unittest.TestCase):
    def setUp(self):
        self.binding = (ROOT / 'bigworld/vehicle_physics_binding.cpp').read_text()
        self.remote = (ROOT / 'bigworld/stock_native_battle_remote_vehicle.cpp').read_text()
        self.consumers = dict(STOCK_REFERENCES)
        self.consumers['configured simulation aiming contract'] = ['setHullAimingAnglesDelta']

    def test_required_consumers_have_registered_ng_members(self):
        self.assertNotIn('setHullAimingAnglesDelta', native_physics_references(self.remote))
        require_members(registered_surface(self.binding, 'PyWGVehiclePhysics'), self.consumers)
        self.assertRegex(without_comments(self.binding),
                         r'PY_FACTORY_NAMED\(\s*PyWGVehiclePhysics\s*,\s*"WGVehiclePhysics"\s*,\s*BigWorld\s*\)')
        # Stock client startup installs ground-placement springs only. It does
        # not configure the simulation Tank the separate aiming API requires.
        filter_source = (ROOT / 'bigworld/stock_vehicle_filter.cpp').read_text()
        self.assertIn('getVehiclePhysics', registered_surface(filter_source, 'PyWGVehicleFilter')['METHODS'])

    def test_removed_required_registration_is_detected(self):
        required = set().union(*(set(values) for values in self.consumers.values()))
        for member in sorted(required):
            with self.subTest(member=member):
                changed, count = re.subn(
                    r'PY_(?:METHOD|ATTRIBUTE)\(\s*' + member + r'\s*\)', '', self.binding)
                self.assertEqual(count, 1, member)
                with self.assertRaisesRegex(AssertionError, 'missing NG WGVehiclePhysics members: ' + member):
                    require_members(registered_surface(changed, 'PyWGVehiclePhysics'), self.consumers)

    def test_declarations_comments_and_properties_do_not_replace_a_method(self):
        changed = re.sub(r'PY_METHOD\(\s*setHullAimingAnglesDelta\s*\)', '', self.binding)
        changed = changed.replace('PY_BEGIN_ATTRIBUTES( PyWGVehiclePhysics )',
                                  'PY_BEGIN_ATTRIBUTES( PyWGVehiclePhysics )\n'
                                  'PY_ATTRIBUTE( setHullAimingAnglesDelta )\n'
                                  '// PY_METHOD( setHullAimingAnglesDelta )')
        # A declaration or property does not register the configured API call.
        with self.assertRaisesRegex(AssertionError, 'missing NG WGVehiclePhysics members: setHullAimingAnglesDelta'):
            require_members(registered_surface(changed, 'PyWGVehiclePhysics'), self.consumers)

    @unittest.skipUnless(os.environ.get('WOT_0922_CLIENT'), 'Exact #1513 bytecode is required')
    def test_recorded_stock_references_match_exact_bytecode(self):
        python = os.environ.get('WOT_PYTHON27')
        if not python:
            python = subprocess.check_output(
                ['pyenv', 'which', 'python2.7'], text=True,
                env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        result = subprocess.run(
            [python, '-B', '-c', BYTECODE_REFERENCES, os.environ['WOT_0922_CLIENT']],
            text=True, capture_output=True, env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout), STOCK_REFERENCES)


if __name__ == '__main__':
    unittest.main()
