"""Real Mosaic owner, SDK viewport bodies, Python conversion and #1513 producer."""
import importlib.util
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ('lib/ashes/simple_gui_component.hpp','lib/ashes/simple_gui_component.cpp',
         'lib/moo/render_context.hpp','lib/moo/render_context.cpp',
         'lib/scaleform/util.hpp','lib/scaleform/util.cpp',
         'lib/scaleform/flash_gui_component.hpp','lib/scaleform/flash_gui_component.cpp',
         'lib/scaleform/flash_text_gui_component.cpp')


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT/'bigworld'/(name+'.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def body(text, signature):
    start = text.index(signature)
    end = text.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (text[end] == '{') - (text[end] == '}')
    return text[start:end+1]


class RestrictedViewportTests(unittest.TestCase):
    def fresh(self, root, extra=()):
        archive = Path(os.environ.get('WOT_PUBLIC_BIGWORLD_ARCHIVE','/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():self.skipTest('Pinned public SDK archive required')
        with tarfile.open(archive) as source:
            names = {member.name.split('/programming/bigworld/',1)[1]:member
                     for member in source if '/programming/bigworld/' in member.name}
            for name in set(FILES).union(extra):
                if name not in names:continue
                destination=root/name;destination.parent.mkdir(parents=True,exist_ok=True)
                destination.write_bytes(source.extractfile(names[name]).read())

    def build(self, root):
        compiler=shutil.which(os.environ.get('CXX','c++'))
        if not compiler:self.skipTest('C++ compiler required')
        self.fresh(root)
        load('patch_stock_flash_restricted_viewport').patch(root)
        util=(root/'lib/scaleform/util.cpp').read_text()
        methods='\n'.join(body(util, signature) for signature in (
            'void fullScreenViewport( GFx::Viewport &outGv )',
            'void fullScreenViewport( GFx::Viewport &outGv, bool restricted )',
            'void rectFromClipBounds(', 'void runtimeGUIViewport('))
        include=root/'include';(include/'moo').mkdir(parents=True)
        (include/'windows.h').write_text('#pragma once\n#ifndef _MSC_VER\n#define __cdecl\n#endif\n'
            'using HMODULE=void*;void* LoadLibraryW(const wchar_t*);void FreeLibrary(void*);'
            'void* GetProcAddress(void*,const char*);\n')
        (include/'moo/device_callback.hpp').write_text('#pragma once\n')
        source=(ROOT/'tests/stock_flash_restricted_viewport_host.cpp').read_text().replace('@VIEWPORT_METHODS@',methods)
        (root/'probe.cpp').write_text(source)
        binary=root/('probe.exe' if os.name=='nt' else 'probe')
        subprocess.run([compiler,'-std=c++14','-Wall','-Wextra','-Werror',
            '-I',str(include),'-I',str(ROOT/'bigworld'),str(root/'probe.cpp'),'-o',str(binary)],check=True)
        return binary

    def test_real_owner_absence_failures_layout_reset_and_teardown(self):
        with tempfile.TemporaryDirectory(prefix='flash-mosaic-owner-')as directory:
            binary=self.build(Path(directory));subprocess.run([str(binary)],check=True)

    def test_patch_composition_idempotence_and_transaction(self):
        restricted=load('patch_stock_flash_restricted_viewport')
        input_mode=load('patch_stock_flash_input_mode')
        dynamic=load('patch_stock_dynamic_resolution')
        extra=set(re.findall(r"['\"]((?:lib|client)/[^'\"]+\.(?:cpp|hpp|ipp))['\"]",
                             (ROOT/'bigworld/patch_stock_dynamic_resolution.py').read_text()))
        for order in ((restricted.patch,input_mode.patch),(input_mode.patch,restricted.patch)):
            with tempfile.TemporaryDirectory(prefix='flash-restricted-patch-')as directory:
                root=Path(directory);self.fresh(root,extra);dynamic.patch(root)
                for patch in order:patch(root)
                before={p.relative_to(root):p.read_bytes()for p in root.rglob('*')if p.is_file()}
                for patch in reversed(order):patch(root)
                dynamic.patch(root)
                self.assertEqual(before,{p.relative_to(root):p.read_bytes()for p in root.rglob('*')if p.is_file()})
        with tempfile.TemporaryDirectory()as directory:
            root=Path(directory);self.fresh(root)
            path=root/'lib/scaleform/util.cpp';path.write_text(path.read_text().replace('fullScreenViewport( gv );','changedViewport( gv );'))
            before={p:p.read_bytes()for p in root.rglob('*')if p.is_file()}
            with self.assertRaises(ValueError):restricted.patch(root)
            self.assertEqual(before,{p:p.read_bytes()for p in root.rglob('*')if p.is_file()})

    def test_native_viewport_oracle_and_two_level_clipping(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('Exact #1513 client required')
        try:from stock_flash_restricted_viewport_oracle import OriginalViewport
        except ImportError:self.skipTest('Unicorn and pefile required')
        original=OriginalViewport(Path(client)/'WorldOfTanks.exe')
        self.assertEqual([original.flag(value)for value in (False,True)],[0,1])
        # aa8950's Flash factory passes true to the original base constructor.
        self.assertEqual(bytes(original.u.mem_read(0xaa8a1a,2)),b'\x6a\x01')
        cases=[]
        for restricted in (False,True):
            for active in (False,True):
                for region in ((1280,0,1280,720),(960,12,1920,696)):
                    for clip in ((-1,1,1,-1),(-.5,.75,.75,-.5),(-.9,.4,.4,-.8)):
                        corners=((-1,1),(1,1),(-1,-1),(1,-1))
                        cases.append((restricted,active,region,clip,corners))
        data='\n'.join(' '.join(str(v)for v in (int(r),int(a),*region,*clip,
            *(v for c in corners for v in c)))for r,a,region,clip,corners in cases)+'\n'
        with tempfile.TemporaryDirectory(prefix='flash-viewport-oracle-')as directory:
            binary=self.build(Path(directory))
            result=subprocess.run([str(binary),'geometry'],input=data,capture_output=True,text=True,check=True)
        lines=result.stdout.splitlines();self.assertEqual(len(lines),len(cases))
        for case,line in zip(cases,lines):
            actual=[float(v)for v in line.split()];expected=original.viewport(*case)
            self.assertEqual(actual[:6],expected[:6],case)
            for a,e in zip(actual[6:],expected[6:]):self.assertAlmostEqual(a,e,places=4,msg=str(case))

    def test_actual_bool_macros_and_original_markers_constructor(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('Exact #1513 client required')
        import test_stock_flash_input_mode as flash
        sources=flash.pinned_sources(self)
        with tempfile.TemporaryDirectory()as directory:
            tree=Path(directory);self.fresh(tree)
            load('patch_stock_flash_restricted_viewport').patch(tree)
            header=(tree/'lib/ashes/simple_gui_component.hpp').read_text()
            base=(tree/'lib/ashes/simple_gui_component.cpp').read_text()
            declaration=re.search(r'PY_RW_ATTRIBUTE_DECLARE\( drawWithRestrictedViewPort_, drawWithRestrictedViewPort \)',header).group()
            default=re.search(r'drawWithRestrictedViewPort_\( (true|false) \)',base).group(1)
        text=flash.generated_source(sources,flash.load_patch().patch_sources(flash.flash_sources(sources)))
        converter=body(sources['lib/pyscript/script.cpp'],'int Script::setData( PyObject * pObject, bool & rBool,')
        converter=converter.replace('int Script::setData(','inline int setData(')
        old=body(text,'inline int setData(PyObject * o, bool & value, const char * name)')
        text=text.replace(old,converter)
        text=text.replace('PyObject * getData(const uint data);','PyObject * getData(const uint data);\n'
            'inline PyObject* getData(const bool value){return PyBool_FromLong(value);}')
        text='#include <strings.h>\n#define _stricmp strcasecmp\n'+text if os.name!='nt' else text
        control=text
        text=text.replace('bool visible_, focus_, moveFocus_;','bool drawWithRestrictedViewPort_='+default+';\n'+declaration+'\n'
            'bool visible_, focus_, moveFocus_;')
        text=text.replace('    HOST_BOOL(visible, visible_),','''    {(char*)"drawWithRestrictedViewPort",
        [](PyObject*s,void*){return getSelf(s)->pyGet_drawWithRestrictedViewPort();},
        [](PyObject*s,PyObject*v,void*){return getSelf(s)->pySet_drawWithRestrictedViewPort(v);},NULL,NULL},
    HOST_BOOL(visible, visible_),''')
        python=Path(os.environ.get('WOT_PYTHON27','/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config=python.with_name('python2.7-config')
        if not config.exists():self.skipTest('CPython 2.7 development files required')
        compiler=shutil.which(os.environ.get('CXX','c++'))
        if not compiler:self.skipTest('C++ compiler required')
        flags=[flag for flag in shlex.split(subprocess.check_output([str(config),'--cflags'],text=True))
               if flag!='-DNDEBUG']
        libs=shlex.split(subprocess.check_output([str(config),'--ldflags'],text=True))
        with tempfile.TemporaryDirectory(prefix='flash-restricted-python-')as directory:
            root=Path(directory)
            binary=root/'host'
            for mode,source in (('pristine',control),('patched',text)):
                (root/'host.cpp').write_text(source)
                subprocess.run([compiler,'-std=c++14','-UNDEBUG','-Wno-deprecated-register',
                    '-Wno-writable-strings',str(root/'host.cpp'),*flags,*libs,'-o',str(binary)],check=True)
                result=subprocess.run([str(binary),str(ROOT/'tests/stock_flash_restricted_viewport_consumer.py'),
                    mode,str(Path(client)/'res/packages/scripts.pkg')],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)


if __name__=='__main__':unittest.main()
