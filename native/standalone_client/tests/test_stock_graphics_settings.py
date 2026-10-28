"""Exercise real registry functions, Python 2 consumers and preference migration."""
import base64
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = ROOT.parents[1]


def load_patch(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'bigworld' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def fresh_engine(root):
    archive = Path(os.environ.get('WOT_PUBLIC_BIGWORLD_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():
        raise unittest.SkipTest('Pinned public BigWorld SDK archive unavailable: ' + str(archive))
    names = ('lib/moo/graphics_settings.cpp', 'lib/moo/graphics_settings.hpp',
             'lib/moo/graphics_settings_picker.hpp', 'lib/romp/py_graphics_setting.cpp',
             'client/bw_winmain.cpp')
    with tarfile.open(archive) as source:
        prefix = source.getmembers()[0].name.split('/')[0] + '/programming/bigworld/'
        for name in names:
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(source.extractfile(prefix + name).read())


def original_profiles(client):
    sys.path.insert(0, str(REPOSITORY))
    from tools.packed_xml import read_packed_xml, PackedElement, TYPE_COMPRESSED_STRING
    with zipfile.ZipFile(client / 'res/packages/misc.pkg') as package:
        resource = read_packed_xml(package.read('system/data/graphics_settings_presets.xml'))

    def element(value):
        return value.value if isinstance(value.value, PackedElement) else value

    def scalar(value):
        if value.value_type == TYPE_COMPRESSED_STRING:
            return base64.b64encode(value.value).decode('ascii')
        return value.value.decode('utf8') if isinstance(value.value, bytes) else value.value

    result = []
    fixture = ['auto root=std::make_shared<DataSection>();']
    for name, value in resource.children:
        if name != b'graphicsPreferences':
            continue
        preset = element(value)
        key = scalar(preset.value)
        index = len(result)
        fixture += ['auto p{0}=std::make_shared<DataSection>();p{0}->text={1};'.format(index, json.dumps(key)),
                    'root->children.emplace_back("graphicsPreferences",p{0});'.format(index)]
        settings = []
        for entry_name, entry_value in preset.children:
            if entry_name != b'entry':
                continue
            fields = {n.decode(): scalar(v) for n, v in element(entry_value).children}
            entry = len(settings)
            settings.append((fields['label'], fields['activeOption']))
            fixture += ['auto e{0}_{1}=std::make_shared<DataSection>();e{0}_{1}->label={2};e{0}_{1}->option={3};'.format(
                index, entry, json.dumps(fields['label']), fields['activeOption']),
                'p{0}->children.emplace_back("entry",e{0}_{1});'.format(index, entry)]
        result.append((key, settings))
    fixture.append('return root;')
    return result, '\n'.join(fixture)


class GraphicsSettingsTests(unittest.TestCase):
    def test_patch_orders_idempotence_and_transactional_failure(self):
        graphics = load_patch('patch_stock_graphics_settings')
        presets = load_patch('patch_stock_graphics_presets')
        for order in ((graphics.patch, presets.apply), (presets.apply, graphics.patch)):
            with self.subTest(order=[p.__module__ for p in order]), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                fresh_engine(root)
                for patch in order:
                    patch(root)
                snapshot = {p.relative_to(root): p.read_bytes() for p in root.rglob('*') if p.is_file()}
                for patch in order:
                    patch(root)
                self.assertEqual(snapshot, {p.relative_to(root): p.read_bytes() for p in root.rglob('*') if p.is_file()})
                cached = root / 'lib/moo/graphics_settings.cpp'
                cached.write_text(cached.read_text().replace('if (setting->stockImportant())',
                                                            'if (setting->label() != "presets")'))
                presets.apply(root)
                self.assertIn('if (setting->stockImportant())', cached.read_text())
                public = (root / 'lib/moo/graphics_settings.cpp').read_text()
                self.assertIn('prepareStockGraphicsPreferences(section, tempSettings, overrideAutoDetectSettingsGroup)', public)
                self.assertIn('writeStockGraphicsPreferencesMetadata(section);', public)
                self.assertIn('if (setting->stockImportant())', public)
                (root / 'lib/romp/py_graphics_setting.cpp').write_text('unsupported source')
                before = {p.relative_to(root): p.read_bytes() for p in root.rglob('*') if p.is_file()}
                with self.assertRaises((ValueError, KeyError)):
                    graphics.patch(root)
                self.assertEqual(before, {p.relative_to(root): p.read_bytes() for p in root.rglob('*') if p.is_file()})

    def test_preference_policy_and_unsigned_shadow_conversion(self):
        if not shutil.which(os.environ.get('CXX', 'c++')):
            raise unittest.SkipTest('C++ compiler unavailable: ' + os.environ.get('CXX', 'c++'))
        source = r'''
#include "wot/stock_graphics_settings.hpp"
#include <cassert>
#include <cstdint>
int main(){
using namespace wot;
auto absent=stockGraphicsPreferencesMigration(false,{});
assert(absent.detect&&absent.result.major==4&&absent.result.minor==2&&absent.result.maintenance==2&&absent.result.status==3);
for(int major:{-1,0,3,4,5})for(int minor:{-1,0,1,2,3})for(int maintenance:{-1,0,1,2,3}){
 auto p=stockGraphicsPreferencesMigration(true,{major,minor,maintenance,23});
 assert(p.result.major==4&&p.result.minor==2&&p.result.maintenance==2);
 assert(p.detect==(major!=4));
 assert(p.result.status==(major!=4?1:minor!=2?2:23));
 assert(p.refreshDetector==(major==4&&(minor<2||(minor==2&&maintenance<2))));
}
assert(stockMigrateShadowOption(0)==1&&stockMigrateShadowOption(1)==2&&stockMigrateShadowOption(2)==3);
assert(stockMigrateShadowOption(3)==3&&stockMigrateShadowOption(UINT32_MAX)==UINT32_MAX);
}
'''.replace('#include <cassert>', '#include <cassert>\n#include <initializer_list>')
        with tempfile.TemporaryDirectory() as folder:
            cpp, binary = Path(folder) / 'probe.cpp', Path(folder) / 'probe'
            cpp.write_text(source)
            command = [os.environ.get('CXX', 'c++'), '-std=c++14', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', '-I', str(ROOT / 'include')]
            if sys.platform != 'win32':
                command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            result = subprocess.run(command + [str(cpp), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_actual_production_registry_and_original_python2_consumers(self):
        if not shutil.which(os.environ.get('CXX', 'clang++')):
            raise unittest.SkipTest('C++ compiler unavailable: ' + os.environ.get('CXX', 'clang++'))
        python = os.environ.get('WOT_PY27', str(Path.home() / '.pyenv/versions/2.7.18/bin/python2.7'))
        client = Path(os.environ.get('WOT_0922_CLIENT', '/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD'))
        if sys.platform != 'darwin' or not Path(python).is_file() or not (client / 'WorldOfTanks.exe').is_file():
            self.skipTest('Pinned CPython 2.7, original client and Darwin extension linker required')
        profiles, fixture = original_profiles(client)
        graphics, presets = load_patch('patch_stock_graphics_settings'), load_patch('patch_stock_graphics_presets')
        with tempfile.TemporaryDirectory(prefix='graphics-settings-production-') as folder:
            root = Path(folder)
            fresh_engine(root)
            graphics.patch(root)
            presets.apply(root)
            public = (root / 'lib/moo/graphics_settings.cpp').read_text()
            registry = '\n'.join(graphics.function(public, marker) for marker in (
                'bool GraphicsSetting::selectOption(', 'bool GraphicsSetting::selectOptionPriv(',
                'void GraphicsSetting::add(', 'GraphicsSetting::SettingsVector GraphicsSetting::stockCurrentSettings()',
                'void GraphicsSetting::captureStockPresetBaseline()', 'void GraphicsSetting::applyStockPreset(',
                'bool GraphicsSetting::needsRestart()'))
            production = '\n'.join(line for name in ('stock_graphics_presets.cpp', 'stock_graphics_settings.cpp')
                                   for line in (ROOT / 'bigworld' / name).read_text().splitlines() if not line.startswith('#include'))
            interface = '\n'.join(line for name in ('stock_graphics_presets.hpp', 'stock_graphics_settings.hpp', 'stock_graphics_detection.hpp')
                                  for line in (ROOT / 'bigworld' / name).read_text().splitlines() if not line.startswith(('#include', '#pragma')))
            detector_source = (ROOT / 'bigworld/stock_graphics_detection.cpp').read_text()
            # Compile the real new preference hooks into the existing registry
            # owner fixture; hardware/profile queries have their own host.
            state = detector_source[detector_source.index('wot::StockGraphicsDetection detection;'):
                                    detector_source.index('bool ensureConfiguration(')]
            production = 'void scheduleStockGraphicsDetection(bool, int) {}\nint measureStockGraphicsDetection() { return 2; }\nvoid stockAdjustDetectedResolution(int) {}\nnamespace {\n' + state + '\n}\n' + '\n'.join(
                graphics.function(detector_source, marker) for marker in
                ('void readStockGraphicsDetectionPreferences(', 'void writeStockGraphicsDetectionPreferences(', 'void finishStockGraphicsDetectionStartup(')) + '\n' + production
            view = (root / 'lib/romp/py_graphics_setting.cpp').read_text()
            production += '\n' + '\n'.join(graphics.function(view, marker) for marker in (
                'static PyObject * graphicsSettings()', 'static bool setGraphicsSetting(', 'static int getGraphicsSetting('))
            methods = re.findall(r'PY_MODULE_FUNCTION\(\s*(\w+),\s*BigWorld\s*\)', production)
            registration = '\n'.join('{"'+name+'",wrap_py_'+name+',METH_VARARGS,NULL},' for name in methods)
            host = (ROOT / 'tests/stock_graphics_settings_host.cpp').read_text()
            for key, value in {'FIXTURE': fixture, 'INTERFACES': interface, 'REGISTRY': registry,
                               'PRODUCTION': production, 'METHODS': registration}.items():
                host = host.replace('@' + key + '@', value)
            (root / 'probe.cpp').write_text(host)
            (root / 'check.py').write_text(RUNNER)
            flags = []
            for flag in shlex.split(subprocess.check_output([python + '-config', '--includes'], text=True)):
                flags += ['-isystem', flag[2:]] if flag.startswith('-I') else [flag]
            result = subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++14', '-dynamiclib', '-undefined', 'dynamic_lookup',
                                     *flags, '-I', str(ROOT / 'include'), str(root / 'probe.cpp'), '-o', str(root / 'graphics_settings_probe.so')],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([python, str(root / 'check.py'), str(root), str(client), json.dumps(profiles)],
                                    capture_output=True, text=True, env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_original_x86_version_status_oracle(self):
        if not shutil.which(os.environ.get('CXX', 'c++')):
            raise unittest.SkipTest('C++ compiler unavailable: ' + os.environ.get('CXX', 'c++'))
        python = Path(os.environ.get('WOT_ORACLE_PYTHON', '/tmp/wot-native-boundaries-venv/bin/python'))
        client = Path(os.environ.get('WOT_0922_CLIENT', '/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD'))
        if not python.is_file() or not (client / 'WorldOfTanks.exe').is_file():
            self.skipTest('Original #1513 PE and Unicorn interpreter required')
        cases = [[major, minor, maintenance, status]
                 for major in (-1, 0, 3, 4, 5) for minor in (-1, 0, 1, 2, 3)
                 for maintenance in (-1, 0, 1, 2, 3) for status in (-1, 0, 3, 23)]
        original = subprocess.run([str(python), str(ROOT / 'tests/stock_graphics_settings_oracle.py'),
                                   str(client / 'WorldOfTanks.exe')], input=json.dumps(cases), capture_output=True, text=True)
        self.assertEqual(original.returncode, 0, original.stderr)
        expected = json.loads(original.stdout)
        source = '''#include "wot/stock_graphics_settings.hpp"
#include <cstdio>
int main(){const int cases[][4]={@CASES@};
for(const auto& c:cases){auto p=wot::stockGraphicsPreferencesMigration(true,{c[0],c[1],c[2],c[3]});
std::printf("%d %d %d %d %d %d\\n",p.result.major,p.result.minor,p.result.maintenance,p.result.status,int(p.detect),int(p.refreshDetector));}}
'''.replace('@CASES@', ','.join('{' + ','.join(map(str, c)) + '}' for c in cases))
        with tempfile.TemporaryDirectory() as folder:
            cpp, binary = Path(folder) / 'probe.cpp', Path(folder) / 'probe'
            cpp.write_text(source)
            result = subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++14', '-Wall', '-Wextra', '-Werror',
                                     '-I', str(ROOT / 'include'), str(cpp), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            actual = [list(map(int, line.split())) for line in result.stdout.splitlines()]
            self.assertEqual(actual, expected)


RUNNER = r'''
from __future__ import print_function
import collections,json,marshal,sys,types,zipfile
sys.path.insert(0,sys.argv[1]);import graphics_settings_probe as p
archive=zipfile.ZipFile(sys.argv[2]+'/res/packages/scripts.pkg')
profiles=json.loads(sys.argv[3]);by_name=dict((name,dict(settings))for name,settings in profiles)
def find(member,name,line=None):
 root=marshal.loads(archive.read(member)[8:]);todo=[root]
 while todo:
  c=todo.pop()
  if c.co_name==name and (line is None or c.co_firstlineno==line):return c
  todo.extend(v for v in c.co_consts if isinstance(v,types.CodeType))
 raise AssertionError((member,name,line))
def klass(member,name,scope,bases=(object,)):
 scope=dict(scope,__name__=member)
 return type(name,bases,eval(find(member,name),scope))
def cell(value):return (lambda:value).func_closure[0]
constants=types.ModuleType('account_helpers.settings_core.settings_constants')
constants.GRAPHICS=klass('scripts/client/account_helpers/settings_core/settings_constants.pyc','GRAPHICS',{})
utilities=types.ModuleType('gui.shared.utils')
container=klass('scripts/client_common/shared_utils/__init__.pyc','CONST_CONTAINER',
 {'ScalarTypes':(types.IntType,types.LongType,types.FloatType,types.BooleanType)+types.StringTypes})
utilities.GRAPHICS_SETTINGS=klass('scripts/client/gui/shared/utils/graphics.pyc','GRAPHICS_SETTINGS',{},(container,))
for name in ('account_helpers','account_helpers.settings_core','gui','gui.shared'):
 module=types.ModuleType(name);module.__path__=[];sys.modules[name]=module
sys.modules[constants.__name__]=constants;sys.modules[utilities.__name__]=utilities
p._seed([('ZZ_LAST',2),('AA_FIRST',1),('HIDDEN',4)])
for name in ('HIDDEN','ZZ_LAST','AA_FIRST'):p._register(name,0,1)
p._flags('HIDDEN',0,0,0);p._flags('AA_FIRST',1,0,1)
p._initialise();p._calls()
names=p.generateGfxSettings()
assert names==('AA_FIRST','HIDDEN','ZZ_LAST','presets')
assert p._calls()==[('ZZ_LAST',2),('AA_FIRST',1),('HIDDEN',4)]
assert constants.GRAPHICS.AA_FIRST=='AA_FIRST' and constants.GRAPHICS.HIDDEN=='HIDDEN'
assert utilities.GRAPHICS_SETTINGS.AA_FIRST=='AA_FIRST' and not hasattr(utilities.GRAPHICS_SETTINGS,'HIDDEN')
# The original SettingsCore qualityOrder reader and CONST_CONTAINER iterator
# must expose control settings only. GraphicSettings.update casts every entry
# to SettingsControlProp; the separately supplied presets value is SettingsDataVo.
qualityOrder=types.FunctionType(find('scripts/client/account_helpers/settings_core/SettingsCore.pyc','<lambda>',260),
 {},closure=(cell(utilities),))
assert set(qualityOrder())=={'AA_FIRST','ZZ_LAST'},qualityOrder()
assert constants.GRAPHICS.presets=='presets' and not hasattr(utilities.GRAPHICS_SETTINGS,'presets')
views=p.graphicsSettings();assert [r[0]for r in views]==['AA_FIRST','ZZ_LAST']
assert p.getGraphicsPresetPropertyNames()==('AA_FIRST','ZZ_LAST')
assert all(len(r)==8 for r in views)
view=p.graphicSetting('AA_FIRST');assert view==('AA_FIRST',1,[['option',i,False,True]for i in range(5)],'description',False,True,False,False)
p._supported('AA_FIRST',3,0)
assert p.graphicSettingIsSupported('AA_FIRST',3) is True
assert p.graphicSettingIsSupported('AA_FIRST',-1) is False
assert p.graphicSettingIsSupported('AA_FIRST',5) is False and p.graphicSettingIsSupported('ABSENT',0) is False
assert p.graphicSetting('ABSENT') is None
assert p.getGraphicsSettingApplyMethod('AA_FIRST',1)=='normal'
assert p.getGraphicsSettingApplyMethod('AA_FIRST',-123)=='restart'
assert p.getGraphicsSettingApplyMethod('ABSENT',999)=='normal'
for index in (-1,5,3):
 try:p.setGraphicsSetting('AA_FIRST',index)
 except ValueError:pass
 else:raise AssertionError(index)
assert p._calls()==[]
p.setGraphicsSetting('AA_FIRST',1);p.setGraphicsSetting('AA_FIRST',1)
assert p._calls()==[('AA_FIRST',1),('AA_FIRST',1)] and p.getGraphicsSetting('AA_FIRST')==1
array=p.graphicSetting('presets');assert array[6] is True and len(array[2])==6
assert array[2][0]=={'index':0,'key':'MAX','settings':by_name['MAX']}
assert array[2][-1]=={'index':5,'key':'CUSTOM','settings':{}}
p.setGraphicPresetByName('UNLISTED')
assert 'UNLISTED' in p.getGraphicsPresetsIndices() and p.graphicSetting('presets')[2]==array[2]
first=p.importantGraphicSettings();second=p.importantGraphicSettings();first['private']=1;assert second=={}
graphic=collections.namedtuple('GraphicSetting','label value options hint advanced needRestart isArray delayed')
get=types.FunctionType(find('scripts/client/gui/shared/utils/graphics.pyc','getGraphicsSetting'),{'BigWorld':p,'GraphicSetting':graphic})
assert get('AA_FIRST').needRestart is True and get('presets').isArray is True and get('ABSENT') is None
original=p.graphicSetting;p.graphicSetting=lambda name:original(name)[:7]
try:get('AA_FIRST')
except TypeError:pass
else:raise AssertionError('The original consumer must reject the old seven-field view')
p.graphicSetting=original
# Run the original SettingsCore mapping closure and GraphicSetting controller
# methods, rather than a replacement implementation of their tuple contract.
class ControllerBase(object):
 def __init__(self,isPreview=False):self.isPreview=isPreview
controller=type('GraphicSetting',(ControllerBase,),{})
controller_scope={'super':super,'GraphicSetting':controller,'graphics':types.ModuleType('graphics'),'BigWorld':p}
controller_scope['graphics'].getGraphicsSetting=get
controller.__init__=types.FunctionType(find('scripts/client/account_helpers/settings_core/options.pyc','__init__',1017),controller_scope,argdefs=(False,))
controller.getApplyMethod=types.FunctionType(find('scripts/client/account_helpers/settings_core/options.pyc','getApplyMethod',1070),controller_scope)
options=types.ModuleType('options');options.GraphicSetting=controller
make=types.FunctionType(find('scripts/client/account_helpers/settings_core/SettingsCore.pyc','<lambda>',116),{},closure=(cell(options),))
constructed=tuple(map(make,names))
assert [name for name,value in constructed]==list(names)
assert all(value.name==name and len(value._currentValue)==8 for name,value in constructed)
assert constructed[0][1].getApplyMethod(0)=='restart'
p._seed([('COLD_NEGATIVE',-1)]);p._register('COLD_NEGATIVE',0,1)
assert p._calls()==[] and p.getGraphicsSetting('COLD_NEGATIVE')==-1
p.generateGfxSettings();assert p._calls()==[('COLD_NEGATIVE',0)]
status_constants=klass('scripts/client/gui/shared/utils/graphics.pyc','GRAPHICS_SETTINGS_STATUS',{})
scope={'BigWorld':p,'GRAPHICS_SETTINGS_STATUS':status_constants,'classmethod':classmethod}
status=klass('scripts/client/gui/shared/utils/graphics.pyc','_GraphicsStatus',scope)
get_status=types.FunctionType(find('scripts/client/gui/shared/utils/graphics.pyc','getStatus'),{'BigWorld':p,'_GraphicsStatus':status})
base={'graphicsSettingsVersion':4,'graphicsSettingsVersionMinor':2,'graphicsSettingsVersionMaintainance':2,'distributionLevel':2,'graphicsSettingsStatus':23}
assert p._prepare(base,2,[('SHADOWS_QUALITY',0),('TEXTURE_QUALITY',3)])=={'SHADOWS_QUALITY':0,'TEXTURE_QUALITY':3}
assert p.graphicsSettingsStatus()==23 and p._metadata()['graphicsSettingsStatus']==23
old=dict(base,graphicsSettingsVersionMinor=1)
assert p._prepare(old,2,[('SHADOWS_QUALITY',2),('TEXTURE_QUALITY',3)])=={'SHADOWS_QUALITY':3,'TEXTURE_QUALITY':3}
assert p.graphicsSettingsStatus()==2 and get_status().isShowWarning() is True
metadata=p._metadata();assert metadata['graphicsSettingsVersion']==4 and metadata['graphicsSettingsVersionMinor']==2 and metadata['graphicsSettingsVersionMaintainance']==2
status.markProcessed();assert p.graphicsSettingsStatus()==0 and get_status().isReset() is False
p._prepare(dict(base,graphicsSettingsVersion=3),0,[]);assert get_status().isReset() is True
assert p._detectorCalls()==(0,1)
# The early desired value must be replaced before a later setting registers.
p._seed([('LATE_GPU_OPTION',1)])
p._deferred('LATE_GPU_OPTION',4)
p._register('LATE_GPU_OPTION',0,0)
assert p.getGraphicsSetting('LATE_GPU_OPTION')==4
p._register('LIVE_GPU_OPTION',0,0)
p._deferred('LIVE_GPU_OPTION',3)
assert p.getGraphicsSetting('LIVE_GPU_OPTION')==3
p._prepare(None,2,[]);assert p.graphicsSettingsStatus()==3 and p._metadata()['distributionLevel']==2
p._detector(0)
try:p._prepare(None,2,[])
except RuntimeError:pass
else:raise AssertionError('Missing detector must not yield plausible settings')
for name,args in [('generateGfxSettings',(1,)),('graphicSetting',(1,)),('graphicSettingIsSupported',('AA_FIRST',)),('graphicsSettingsStatus',(1,)),('resetGraphicsSettingsStatus',(1,)),('importantGraphicSettings',(1,)),('getGraphicsSettingApplyMethod',(1,0))]:
 try:getattr(p,name)(*args)
 except (TypeError,ValueError):pass
 else:raise AssertionError(name)
print('Production registry, original presets, original Python 2 view/status consumers and failure lifecycles passed')
'''


if __name__ == '__main__':
    unittest.main()
