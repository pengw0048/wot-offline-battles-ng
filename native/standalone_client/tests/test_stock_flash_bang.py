"""Connect exact EffectsList flash effects to native provider and draw owners."""
from pathlib import Path
import importlib.util
import io
import json
import os
import random
import shlex
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'bigworld'))
import patch_stock_flash_bang as patcher


def body(source,signature):
    start=source.index(signature);end=source.index('{',start);depth=1
    while depth:
        end+=1;depth+=(source[end]=='{')-(source[end]=='}')
    return source[start:end+1]


def sources(test):
    archive=Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE','/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.exists():test.skipTest('Pinned public engine archive required')
    needed={'lib/moo/renderer.hpp','lib/moo/deferred_pipeline.hpp','lib/moo/deferred_pipeline.cpp',
        'lib/moo/forward_pipeline.hpp','lib/moo/forward_pipeline.cpp','client/script_app.cpp',
        'lib/pyscript/script_math.cpp'}
    output={}
    with tarfile.open(archive) as original:
        for member in original:
            for relative in needed:
                if member.name.endswith('/programming/bigworld/'+relative):
                    output[relative]=original.extractfile(member).read().decode()
            if len(output)==len(needed):break
    test.assertEqual(set(output),needed)
    return output


class FlashBangTests(unittest.TestCase):
    def test_original_geometrics_flash_passes(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('WOT_0922_CLIENT required')
        sys.path.insert(0,str(ROOT.parents[1]/'tools'))
        from prepare_native_client import OriginalResources
        resources=OriginalResources(client)
        with tempfile.TemporaryDirectory(prefix='flash-shader-') as directory:
            directory=Path(directory);binary=directory/'probe'
            result=subprocess.run(['c++','-std=c++17','-I',str(ROOT/'include'),
                str(ROOT/'tests/stock_flash_bang_effect.cpp'),str(ROOT/'src/stock_shader_binary.cpp'),
                str(ROOT/'src/stock_effect_reflection.cpp'),str(ROOT/'src/stock_effect_binding.cpp'),
                '-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            for model in (10,11):
                with zipfile.ZipFile(io.BytesIO(resources.read('system/materials/geometrics.%d.dx9.fxo'%model))) as archive:
                    effect=directory/('geometrics%d.effect'%model);effect.write_bytes(archive.read('effect'))
                result=subprocess.run([str(binary),str(effect)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_native_draw_history_provider_and_original_python_consumer(self):
        python=Path(os.environ.get('WOT_PYTHON27','/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config=python.with_name('python2.7-config')
        if not config.exists():self.skipTest('CPython 2.7 development files required')
        original=sources(self)
        generated=(ROOT/'tests/stock_flash_bang_host.cpp').read_text()
        header=(ROOT/'bigworld/stock_flash_bang.hpp').read_text()
        header='\n'.join(line for line in header.splitlines() if not line.startswith(('#include','#pragma')))
        generated=generated.replace('@HEADER@',header).replace('@ANIMATION@','\n'.join(
            body(original['lib/pyscript/script_math.cpp'],signature) for signature in (
                'void Vector4Animation::tick(', 'void Vector4Animation::output(')))
        with tempfile.TemporaryDirectory(prefix='stock-flash-bang-') as directory:
            directory=Path(directory);source=directory/'host.cpp';source.write_text(generated);binary=directory/'host'
            flags=shlex.split(subprocess.check_output([str(config),'--cflags'],text=True))
            libs=shlex.split(subprocess.check_output([str(config),'--ldflags'],text=True))
            result=subprocess.run(['c++','-std=c++14','-Wno-deprecated-register','-Wno-writable-strings',
                '-I',str(ROOT/'bigworld'),*flags,'-UNDEBUG',str(source),*libs,'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            environment=dict(os.environ)
            if environment.get('WOT_0922_CLIENT'):
                sys.path.insert(0,str(ROOT.parents[1]/'tools'))
                from prepare_native_client import OriginalResources
                from packed_xml import PackedElement,read_packed_xml
                resources=OriginalResources(environment['WOT_0922_CLIENT'])
                root=read_packed_xml(resources.read('scripts/item_defs/vehicles/common/shot_effects.xml'))
                def element(node):
                    return node if isinstance(node,PackedElement) else (node.value if node.value_type==0 else None)
                def children(node):
                    node=element(node)
                    return node.children if node else []
                def scalar(node):
                    node=element(node).value if element(node) else node
                    return node.value.decode() if isinstance(node.value,bytes) else node.value
                descriptors=[]
                def walk(node,path):
                    for name,child in children(node):
                        if name==b'flashBang':
                            stages=next(value for key,value in children(child) if key==b'stages')
                            rows=[]
                            for key,stage in children(stages):
                                colour=next(value for key,value in children(stage) if key==b'color')
                                rows.append([float(scalar(stage)),[float(v) for v in scalar(colour).split()]])
                            descriptors.append([path+'/'+name.decode(),rows])
                        walk(child,path+'/'+name.decode())
                walk(root,'shot_effects')
                self.assertGreaterEqual(len(descriptors),40)
                values=directory/'authored_stages.json';values.write_text(json.dumps(descriptors))
                environment['WOT_FLASH_BANG_STAGES']=str(values)
            result=subprocess.run([str(binary),str(ROOT/'tests/stock_flash_bang_consumer.py')],env=environment,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('FlashBang actual provider, shader draw, history/reset and CPython contracts passed',result.stdout)
            if os.environ.get('WOT_0922_CLIENT'):
                self.assertIn('Original EffectsList flash-bang producer/lifecycle passed',result.stdout)
                self.assertIn('Original authored flash-bang descriptors passed',result.stdout)

    def test_original_wrapper_ownership_tick_and_render_dispatch(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('WOT_0922_CLIENT required')
        if not all(importlib.util.find_spec(name) for name in ('pefile','unicorn')):
            self.skipTest('pefile/unicorn required')
        from stock_flash_bang_oracle import OriginalFlashBang,OWNER
        oracle=OriginalFlashBang(Path(client)/'WorldOfTanks.exe')
        first=oracle.provider_at(0,(.2,.4,.6,.8));second=oracle.provider_at(1,(.6,.8,.2,.4))
        for pipeline in ('deferred','forward'):
            for argc in (0,2,3):
                result,calls=oracle.wrapper(provider=first,argc=argc,pipeline=pipeline)
                self.assertEqual(result,'NULL');self.assertEqual(calls[0][0],'argCountError')
                self.assertEqual(calls[0][2:], [1,1,'Vector4ProviderPtr'])
            self.assertEqual(oracle.wrapper(provider=first,fail=True,pipeline=pipeline)[0],'NULL')
            self.assertEqual(oracle.wrapper(provider=first,pipeline=pipeline,present=False)[0],'None')
            self.assertEqual(oracle.entries(),[])
            for provider in (first,first,second):
                self.assertEqual(oracle.wrapper(provider=provider,pipeline=pipeline)[0],'None')
            self.assertEqual(oracle.entries(),[first,first,second])
            self.assertEqual(oracle.tick(),tuple(struct.unpack('<4f',struct.pack('<4f',.6,.8,.6,.8))))
            oracle.wrapper(remove=True,provider=first,pipeline=pipeline)
            self.assertEqual(oracle.entries(),[first,second])
            oracle.wrapper(remove=True,provider=first,pipeline=pipeline)
            oracle.wrapper(remove=True,provider=first,pipeline=pipeline)
            oracle.wrapper(remove=True,provider=second,pipeline=pipeline)
            self.assertEqual(oracle.entries(),[])
            for pointer in (first,second):self.assertEqual(struct.unpack('<I',oracle.u.mem_read(pointer+4,4))[0],100)
        for previous in (False,True):
            events,valid=oracle.render_prefix((.2,.4,.6,.8),previous)
            self.assertEqual(events[0][0],'textured2D' if previous else 'standard2D')
            self.assertEqual(events[1][0],'quad');self.assertEqual(events[1][1],(-2,-2))
            self.assertEqual(events[1][2],(1284,724));self.assertEqual(events[2][0],'draw')
        for alpha in (0,-1):
            events,valid=oracle.render_prefix((1,1,1,alpha),True)
            self.assertEqual(events,[]);self.assertEqual(valid,0)

    def test_patcher_anchors_and_pipeline_lifecycle(self):
        original=sources(self)
        with tempfile.TemporaryDirectory(prefix='stock-flash-patch-') as directory:
            directory=Path(directory)
            for relative,text in original.items():
                path=directory/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text)
            patcher.patch(directory)
            first={relative:(directory/relative).read_text() for relative in original}
            patcher.patch(directory)
            self.assertEqual(first,{relative:(directory/relative).read_text() for relative in original})
            for name in ('deferred','forward'):
                source=first['lib/moo/%s_pipeline.cpp'%name]
                self.assertEqual(source.count('m_stockFlashBang.reset(new StockFlashBangSupport())'),1)
                self.assertEqual(source.count('flashBang->tick()'),1)
                self.assertEqual(source.count('flashBang->draw()'),1)
            script=first['client/script_app.cpp']
            self.assertLess(script.index('finiStockFlashBangAnimations()'),script.index('BigWorldClientScript::fini()'))
            self.assertLess(script.index('initStockFlashBangAnimations()'),script.index('BigWorldClientScript::init('))
            path=directory/'lib/moo/forward_pipeline.cpp'
            path.write_text(original['lib/moo/forward_pipeline.cpp'].replace('void ForwardPipeline::tick(', 'void ForwardPipeline::changed('))
            with self.assertRaises(RuntimeError):patcher.patch(directory)
            # No partial writes on a failed anchor in a later input file.
            self.assertEqual((directory/'lib/moo/deferred_pipeline.cpp').read_text(),first['lib/moo/deferred_pipeline.cpp'])
