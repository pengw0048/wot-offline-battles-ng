"""#1513 TextFieldEx registration, native guards, and real DocView autosize layout."""
import itertools
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
import zlib

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
from test_stock_flash_drawable_images import load_patch,sdk,compiler
from test_stock_vector4_basic import block
from stock_flash_scheduler_abc import ABC,Reader

patcher=load_patch('patch_stock_flash_vertical_autosize')
base=load_patch('patch_scaleform_sdk')


def selected_sources(test):
    root=sdk(test)
    sources={name:(root/name).read_text(encoding='utf-8-sig') for name in patcher.FILES}
    # Reconstruct only this family's exact prepared seams when the fixture is a
    # candidate SDK. All unrelated bitmap-fill and selection owner fixes survive.
    if patcher.BODIES in sources[patcher.SOURCE]:
        for name,before,after in reversed(patcher.SEAMS):
            test.assertEqual(sources[name].count(after),1,(name,before))
            sources[name]=sources[name].replace(after,before,1)
    if 'ThunkInfoNum = 10' in sources[patcher.HEADER]:
        def edit(path,before,after,label):
            name=str(path)
            test.assertEqual(sources[name].count(before),1,label)
            sources[name]=sources[name].replace(before,after,1)
        base.text_selection_edits(Path(''),edit)
    return sources


def generated(test,sources):
    root=sdk(test)
    doc_h=(root/'Src/Render/Text/Text_DocView.h').read_text(encoding='utf-8-sig')
    doc_cpp=(root/'Src/Render/Text/Text_DocView.cpp').read_text(encoding='utf-8-sig')
    field=(root/'Src/GFx/GFx_TextField.h').read_text(encoding='utf-8-sig')
    begin=doc_cpp.index('    bool valignApplied = false;')
    end=doc_cpp.index('    // handle auto text fit here.',begin)
    replacements={
        '@METHODS@':patcher.BODIES,
        '@DOC_API@':'\n'.join(block(doc_h,signature) for signature in (
            '    void ClearAutoSizeY()', '    bool IsAutoSizeY() const',
            '    void SetVAlignment(', '    ViewVAlignment GetVAlignment() const')),
        '@SET_Y@':block(doc_cpp,'void DocView::SetAutoSizeY()').replace('void DocView::','void '),
        '@FIELD_API@':'\n'.join(block(field,signature) for signature in (
            '    void                    ClearAutoSizeY()', '    void                    SetAutoSizeY()',
            '    bool                    IsAutoSizeY() const', '    Text::DocView::ViewVAlignment GetVAlignment() const',
            '    void                    SetVAlignment(')),
        '@LAYOUT@':doc_cpp[begin:end],
    }
    source=(HERE/'stock_flash_vertical_autosize_host.cpp').read_text()
    source=source.replace('using ASString=std::string;','using ASString=std::string;\nusing UByte=unsigned char;')
    for token,value in replacements.items():
        test.assertEqual(source.count(token),1,token)
        source=source.replace(token,value)
    test.assertIn(patcher.BODIES,sources[patcher.SOURCE])
    return source


def original_client(test):
    value=os.environ.get('WOT_0922_CLIENT')
    if not value:test.skipTest('Exact #1513 client required')
    root=Path(value)
    for member in ('WorldOfTanks.exe','res/packages/gui.pkg','res/packages/scripts.pkg'):
        test.assertTrue((root/member).is_file(),'Configured #1513 fixture missing: '+member)
    return root


class FlashVerticalAutoSizeTest(unittest.TestCase):
    def test_composes_with_actual_selection_patch_and_is_atomic_idempotent(self):
        original=selected_sources(self);patched=patcher.patch_sources(original)
        self.assertEqual(patcher.patch_sources(patched),patched)
        for name,before,after in patcher.SEAMS:
            bad=dict(original);bad[name]=bad[name].replace(before,'/* changed autosize seam */',1)
            with tempfile.TemporaryDirectory() as directory:
                root=Path(directory)
                for member,source in bad.items():
                    path=root/member;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(source)
                with self.assertRaises(ValueError):patcher.patch(root)
                self.assertEqual({member:(root/member).read_text() for member in bad},bad)
        for name,unused,unused2,unused3 in base.TEXT_SELECTION_SETTERS:
            self.assertEqual(patched[patcher.SOURCE].count('"'+name+'", NULL, Abc::NS_Public, CT_Method, 2, 2}'),1)
        source=patched[patcher.SOURCE]
        self.assertIn('ThunkInfoNum = 16',patched[patcher.HEADER])
        self.assertIn('MemberInfoNum = 11',patched[patcher.HEADER])
        self.assertEqual(source.count('"setVerticalAutoSize", NULL, Abc::NS_Public, CT_Method, 2, 2}'),1)
        self.assertEqual(source.count('"getVerticalAutoSize", NULL, Abc::NS_Public, CT_Method, 1, 1}'),1)

    def test_native_methods_drive_actual_sdk_layout_and_guards(self):
        sources=patcher.patch_sources(selected_sources(self))
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory);cpp=path/'autosize.cpp';exe=path/('autosize.exe' if os.name=='nt' else 'autosize')
            cpp.write_text(generated(self,sources))
            subprocess.run(compiler(self)+['-std=c++14','-O0','-UNDEBUG','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
            for value,y,align,enabled,kind in itertools.product(('none','top','bottom','center','invalid'),(0,1),range(4),(0,1),range(4)):
                result=subprocess.run([str(exe),value,str(y),str(align),str(enabled),str(kind)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                actual=json.loads(result.stdout)
                if kind:self.assertEqual(actual['error'],1507 if kind==1 else 1034)
                else:
                    next_y=bool(y) if not enabled else value!='none'
                    next_align=align if not enabled or value=='invalid' else {'none':0,'top':1,'bottom':2,'center':3}[value]
                    self.assertEqual(actual,{'y':next_y,'align':next_align,'dirty':enabled,
                        'result':{0:'none',1:'top',2:'bottom',3:'center'}[next_align] if next_y else 'none','error':None})

    def test_native_exact_pe_setter_getter_matches_production(self):
        root=original_client(self)
        try:from stock_flash_vertical_autosize_oracle import OriginalVerticalAutoSize
        except ImportError:
            if os.environ.get('WOT_REQUIRE_NATIVE_ORACLE')=='1':raise
            self.skipTest('pefile and Unicorn required')
        oracle=OriginalVerticalAutoSize(root/'WorldOfTanks.exe')
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory);cpp=path/'autosize.cpp';exe=path/('autosize.exe' if os.name=='nt' else 'autosize')
            cpp.write_text(generated(self,patcher.patch_sources(selected_sources(self))))
            subprocess.run(compiler(self)+['-std=c++14','-O0','-UNDEBUG','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
            for value,y,align,enabled in itertools.product(('none','top','bottom','center','invalid'),(0,1),range(4),(0,1)):
                expected=oracle.run(value,bool(y),align,bool(enabled))
                result=subprocess.run([str(exe),value,str(y),str(align),str(enabled),'0'],capture_output=True,text=True,check=True)
                self.assertEqual(json.loads(result.stdout),expected,(value,y,align,enabled))
            self.assertEqual(oracle.run('top',null=True)['error'],1507)

    def test_exact_damage_panel_atlas_and_native_consumers(self):
        root=original_client(self)
        with zipfile.ZipFile(root/'res/packages/gui.pkg') as archive:
            swf=archive.read('gui/flash/battle.swf')
        data=zlib.decompress(swf[8:]) if swf[:3] in (b'CWS',b'CFX') else swf[8:]
        reader=Reader(data);reader.pos=(5+4*(data[0]>>3)+7)//8+4
        abc=None
        while reader.pos<len(data):
            tag=int.from_bytes(reader.raw(2),'little');size=tag&63
            if size==63:size=int.from_bytes(reader.raw(4),'little')
            payload=reader.raw(size)
            if tag>>6==82:
                code=payload[payload.index(b'\0',4)+1:]
                if b'AtlasManager' in code and b'DamagePanel' in code:abc=ABC(code);break
        self.assertIsNotNone(abc)
        def u30(value):
            output=bytearray()
            while value>=128:output.append((value&127)|128);value>>=7
            output.append(value);return bytes(output)
        atlas=abc.bodies[abc.lookup('AtlasManager','getNewBitmapData')]
        self.assertIn(b'\x4a'+u30(abc.names.index('BitmapData'))+b'\x02',atlas)
        self.assertIn(b'\x4f'+u30(abc.names.index('copyPixels'))+b'\x03',atlas)
        config=abc.bodies[abc.lookup('DamagePanel','configUI')]
        self.assertEqual(config.count(b'\x4f'+u30(abc.names.index('setVerticalAutoSize'))+b'\x02'),3)
        py27=os.environ.get('WOT_PY27',shutil.which('python2.7'))
        if not py27:
            fallback=Path('/Users/peng/.pyenv/versions/2.7.18/bin/python2.7')
            if fallback.is_file():py27=str(fallback)
            elif 'WOT_PY27' in os.environ:self.fail('Configured Python 2.7 interpreter missing')
            else:self.skipTest('CPython 2.7 required for exact PYC producer')
        program='''import zipfile,marshal,types,json,sys
z=zipfile.ZipFile(sys.argv[1]);c=marshal.loads(z.read('scripts/client/gui/Scaleform/daapi/view/meta/DamagePanelMeta.pyc')[8:])
def walk(c):
 if c.co_name=='as_setupS': print(json.dumps([c.co_argcount,c.co_firstlineno,list(c.co_names)]))
 for x in c.co_consts:
  if isinstance(x,types.CodeType):walk(x)
walk(c)
'''
        result=subprocess.run([py27,'-c',program,str(root/'res/packages/scripts.pkg')],capture_output=True,text=True,check=True)
        count,line,names=json.loads(result.stdout.strip())
        self.assertEqual((count,line),(8,36));self.assertIn('_isDAAPIInited',names);self.assertIn('as_setup',names)


if __name__=='__main__':unittest.main()
