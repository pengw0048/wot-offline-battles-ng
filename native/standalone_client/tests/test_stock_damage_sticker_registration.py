"""Real CPython 2.7, pinned Script converters and original vehicles producer.

The math-object carrier and resource section are bounded fixtures. The wrapper,
converter function bodies, registry implementation and PYC producer are real.
"""
from pathlib import Path
import base64
import importlib.util
import json
import os
import shlex
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def body(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


class DamageStickerBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        python = Path(os.environ.get('WOT_TEST_PY27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not config.exists() or not archive.exists():
            raise unittest.SkipTest('CPython 2.7 development runtime and pristine public-engine archive are required')
        cls.temporary = tempfile.TemporaryDirectory(prefix='stock-damage-registration-')
        cls.directory = Path(cls.temporary.name)
        with tarfile.open(archive) as source:
            name = next(name for name in source.getnames() if name.endswith('/programming/bigworld/lib/pyscript/script.cpp'))
            script = source.extractfile(name).read().decode('utf-8-sig')
        functions = []
        for type_name in ('bool & rBool', 'double & rDouble', 'float & rFloat', 'Vector2 & rVector', 'BW::string & rString'):
            functions.append(body(script, 'int Script::setData( PyObject * pObject, ' + type_name))
        host = (ROOT / 'tests/stock_damage_sticker_registration_host.cpp').read_text()
        host = host.replace('@CONVERTERS@', '\n'.join(functions)).replace('@PRODUCTION_FILE@',
            str(ROOT / 'bigworld/stock_damage_sticker_registration.cpp'))
        (cls.directory / 'host.cpp').write_text(host)
        destination = os.environ.get('WOT_DAMAGE_STICKER_FIXTURE_OUT')
        if destination:
            destination = Path(destination); destination.mkdir(parents=True, exist_ok=True)
            (destination / 'host.cpp').write_text(host.replace(str(ROOT / 'bigworld/stock_damage_sticker_registration.cpp'),
                'stock_damage_sticker_registration.cpp'))
            for name in ('stock_damage_sticker_registration.cpp', 'stock_vehicle_stickers_data.hpp'):
                (destination / name).write_bytes((ROOT / 'bigworld' / name).read_bytes())
            (destination / 'native-case.py').write_text(r'''
import sys, BigWorld as B
assert sys.maxint==0x7fffffff
B._owner(1)
assert B.wg_registerDamageSticker('impact','diffuse','normal',(1.25,2.5),0xffffffffL,True)==1
assert B._inspect()[0][1][0][4]==4294967296.0
assert B.wg_registerDamageSticker('impact','other','',B._vector(3,4),-.25,'false')==0
assert B.wg_registerDamageSticker(u'\u4f24\u75d5\0suffix',u'\u7eb9\u7406.dds','',(1,2),0,42)==2
assert B._inspect()[1][0]==''.join(map(chr,[0xe4,0xbc,0xa4,0xe7,0x97,0x95,0]))+'suffix'
before=B._inspect();calls=B._calls()
for index,values in ((0,(None,1)),(1,(None,1)),(2,(None,1)),(3,([1,2],(1,),(1,2,3))),
                     (4,(-1L,1L<<32,1L<<128,'1')),(5,(1L,1.0,u'true','yes'))):
    for value in values:
        arguments=['impact','d','b',(1,2),.3,True];arguments[index]=value
        try:B.wg_registerDamageSticker(*arguments)
        except TypeError:pass
        else:raise AssertionError((index,value))
        assert B._inspect()==before and B._calls()==calls
for count in range(9):
    if count==6:continue
    try:B.wg_registerDamageSticker(*(['x']*count))
    except TypeError:pass
    else:raise AssertionError(count)
B._owner(0)
assert B.wg_registerDamageSticker('x','d','b',(1,2),0,False)==-1
print('Actual Win32 CPython '+sys.version.split()[0]+': uint32-long boundary, six args and nonmutating failures PASS')
''')
        cls.probe = cls.directory / 'probe'
        flags = [flag for flag in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True)) if flag != '-DNDEBUG']
        libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
        subprocess.run(['c++', '-std=c++14', '-Wno-deprecated-register', '-UNDEBUG', '-I'+str(ROOT / 'bigworld'),
                        str(cls.directory / 'host.cpp'), *flags, *libs, '-o', str(cls.probe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def execute(self, code, extra=None):
        path = self.directory / 'case.py'
        path.write_text('# coding: utf-8'+chr(10)+code)
        environment = dict(os.environ)
        environment.update(extra or {})
        subprocess.run([str(self.probe), str(path)], env=environment, check=True)

    def test_exact_six_arguments_conversion_atomicity_and_unmodified_native_ids(self):
        self.execute(r'''
import BigWorld as B, math, struct, sys
def f32(x):return struct.unpack('<f',struct.pack('<f',x))[0]
args=('impact','diffuse.dds','normal.dds',(1.25,2.5),.3,True)
assert type(B.wg_registerDamageSticker(*args)) is int
assert B.wg_registerDamageSticker(*args)==-1 and B._inspect()==[]
B._owner(1)
assert B.wg_registerDamageSticker(*args)==1
assert B.wg_registerDamageSticker(*args)==0
assert B._inspect()==[('impact',[('diffuse.dds','normal.dds',1.25,2.5,f32(.3),True)]*2)]
assert B.wg_registerDamageSticker('other','x','',(3,4),-.2,'FALSE')==2
assert B.wg_registerDamageSticker('other','y','',(5,6),-.4,'TrUe')==1
assert B._inspect()[1][1][-1][-1] is True
assert B.wg_registerDamageSticker(u'伤痕\x00suffix',u'纹理.dds','',(1,2),0,42)==3
assert B._inspect()[2][0]==u'伤痕\x00suffix'.encode('utf-8')
assert B._inspect()[2][1][0][0]==u'纹理.dds'.encode('utf-8')
assert B.wg_registerDamageSticker('math','a','b',B._vector(3,4),True,False)==4
class Number(object):
    def __float__(self):return 1.5
assert B.wg_registerDamageSticker('numeric','a','b',(Number(),Number()),1,0)==5
before=B._inspect();calls=B._calls()
def rejected(call):
    try:call()
    except TypeError:pass
    else:raise AssertionError('Invalid native argument accepted')
    assert B._inspect()==before and B._calls()==calls
for count in range(9):
    if count!=6:rejected(lambda count=count:B.wg_registerDamageSticker(*(args*2)[:count]))
rejected(lambda:B.wg_registerDamageSticker(name='a',texture='b',bump='c',size=(1,2),variation=0,randomYaw=True))
for slot,values in ((0,(None,1,[])),(1,(None,1,[])),(2,(None,1,[])),
                    (3,(None,[],[1,2],(1,),(1,2,3),('x',2))),
                    (4,(None,'1',[],Number(),-1L,1L<<128)),(5,(None,[],1.0,1L,'yes',u'true'))):
    for value in values:
        values=list(args);values[slot]=value
        rejected(lambda values=values:B.wg_registerDamageSticker(*values))
B._owner(0)
assert B.wg_registerDamageSticker(*args)==-1
calls=B._calls();before=[]
rejected(lambda:B.wg_registerDamageSticker(None,*args[1:]))
B._owner(1)
assert B.wg_registerDamageSticker('fresh','x','',(1,2),0,False)==1
assert len(B._inspect())==1
print('CPython '+sys.version.split()[0]+': six-argument guards, UTF-8/NUL, native bool/vector/float rules and registry identity passed')
''')

    def test_original_pyc_producer_and_all_original_style_variants(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client: self.skipTest('Exact #1513 client is required')
        sys.path.insert(0, str(ROOT.parents[1] / 'tools'))
        import packed_xml
        with zipfile.ZipFile(Path(client) / 'res/packages/scripts.pkg') as archive:
            pyc = archive.read('scripts/common/items/vehicles.pyc')
            xml = archive.read('scripts/item_defs/vehicles/common/damage_stickers.xml')
        def value(raw):
            if raw.value_type == packed_xml.TYPE_ELEMENT: return element(raw.value)
            if raw.value_type == packed_xml.TYPE_VECTOR:
                return list(struct.unpack('<%df' % (len(raw.value)//4), raw.value))
            if raw.value_type == packed_xml.TYPE_COMPRESSED_STRING: return base64.b64encode(raw.value).decode('ascii')
            if isinstance(raw.value, bytes): return raw.value.decode('utf-8')
            return raw.value
        def element(node):
            return {'value': value(node.value), 'children': [[key.decode('utf-8'), value(child)] for key, child in node.children]}
        tree = self.directory / 'damage-tree.json'
        tree.write_text(json.dumps(element(packed_xml.read_packed_xml(xml))))
        source = self.directory / 'vehicles.pyc'; source.write_bytes(pyc)
        self.execute(r'''
import BigWorld as B, os, json, marshal, types, struct
def raw(value):return value.encode('utf-8') if isinstance(value,unicode) else value
class Section(object):
    def __init__(self,value,name=''):
        self.name=raw(name)
        if isinstance(value,dict):self.value=value['value'];self.children=[(raw(k),Section(v,k)) for k,v in value['children']]
        else:self.value=value;self.children=[]
    def items(self):return self.children[:]
    def has_key(self,key):return any(k==key for k,v in self.children)
    def __getitem__(self,key):return next((v for k,v in self.children if k==key),None)
    def readFloat(self,key,default=0):
        node=self[key]
        if node is None:return default
        return float(node.value[0] if isinstance(node.value,list) else node.value)
    @property
    def asString(self):return raw(self.value)
    @property
    def asBool(self):return self.value if isinstance(self.value,bool) else self.value.lower()=='true'
root=Section(json.load(open(os.environ['DAMAGE_TREE'])))
xmlPath='scripts/item_defs/vehicles/common/damage_stickers.xml'
class Resources(object):
    purged=False
    def openSection(self,path):assert path==xmlPath;return root
    def purge(self,path,recurse):assert path==xmlPath and recurse is True;self.purged=True
class XML(object):
    def raiseWrongXml(self,*args):raise ValueError(args)
    def readInt(self,ctx,section,key,default):return int(section[key].value) if section.has_key(key) else default
    def readNonEmptyString(self,ctx,section,key):
        value=section[key].asString
        if not value:raise ValueError('Empty original resource string')
        return value
    def readPositiveVector2(self,ctx,section,key):
        values=section[key].value
        if not isinstance(values,list):values=map(float,values.split())
        assert len(values)==2 and min(values)>0
        return B._vector(*values)
resources=Resources()
namespace={'ResMgr':resources,'_xml':XML(),'IS_CLIENT':True,'BigWorld':B,'__builtins__':__builtins__}
code=marshal.loads(open(os.environ['VEHICLES_PYC'],'rb').read()[8:])
def find(code,name):
    if code.co_name==name:return code
    for item in code.co_consts:
        if isinstance(item,types.CodeType):
            found=find(item,name)
            if found is not None:return found
for name in ('_readAndRegisterDamageStickerTextureParams','_readDamageStickers'):
    original=find(code,name);assert original is not None
    namespace[name]=types.FunctionType(original,namespace)
B._owner(1)
result=namespace['_readDamageStickers'](xmlPath)
styles=B._inspect();assert len(styles)==11 and sum(len(v) for n,v in styles)==42
assert [item['id'] for item in result['descrs']]==range(11)
assert result['ids']==dict((name,index) for index,(name,variants) in enumerate(styles))
for name,variants in styles:
    node=root[name];assert node is not None and not node.has_key('texName')
    for index,actual in enumerate(variants,1):
        variant=node['variant%d'%index];assert variant is not None
        size=XML().readPositiveVector2(None,variant,'modelSizes').tuple()
        variation=struct.unpack('<f',struct.pack('<f',variant.readFloat('variation',0)))[0]
        assert actual==(variant['texName'].asString,variant['bumpTexName'].asString if variant.has_key('bumpTexName') else '',
                        size[0],size[1],variation,variant['randomYaw'].asBool if variant.has_key('randomYaw') else True)
assert resources.purged
print('Original vehicles.pyc producer: 11 styles, 42 variants, final IDs 0..10 and every authored field passed')
''', {'DAMAGE_TREE': str(tree), 'VEHICLES_PYC': str(source)})


class DamageStickerModulePatchTests(unittest.TestCase):
    def test_pristine_token_link_is_idempotent_and_failure_is_transactional(self):
        path = ROOT / 'bigworld/patch_stock_damage_sticker_registration.py'
        spec = importlib.util.spec_from_file_location('damage_registration_patch', path)
        patch = importlib.util.module_from_spec(spec); spec.loader.exec_module(patch)
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.exists(): self.skipTest('Pristine public-engine archive is required')
        with tarfile.open(archive) as source:
            name = next(name for name in source.getnames() if name.endswith('/programming/bigworld/client/script_bigworld.cpp'))
            original = source.extractfile(name).read()
        with tempfile.TemporaryDirectory(prefix='damage-registration-patch-') as directory:
            root = Path(directory); (root/'client').mkdir(); (root/'lib').mkdir()
            target=root/'client/script_bigworld.cpp'; target.write_bytes(original)
            patch.patch(root); first=target.read_bytes(); patch.patch(root)
            self.assertEqual(target.read_bytes(),first)
            text=first.decode(); self.assertEqual(text.count('StockDamageStickerRegistration_token |'),1)
            self.assertEqual(text.count('#include "stock_damage_sticker_registration.hpp"'),1)
            target.write_text(original.decode('utf-8-sig').replace('\tMath_token |','\tunknown_token |'))
            before=target.read_bytes(); header=(root/'lib/stock_damage_sticker_registration.hpp').read_bytes()
            with self.assertRaises(ValueError):patch.patch(root)
            self.assertEqual(target.read_bytes(),before)
            self.assertEqual((root/'lib/stock_damage_sticker_registration.hpp').read_bytes(),header)


if __name__ == '__main__':
    unittest.main()
