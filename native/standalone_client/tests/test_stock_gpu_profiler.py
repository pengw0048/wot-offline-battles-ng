"""Exercise actual GPU query producers and the CPU hierarchy with native dump data."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('gpu_profiler_patch', ROOT/'bigworld/patch_stock_gpu_profiler.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


def sources(test):
    archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():
        if 'WOT_PUBLIC_ENGINE_ARCHIVE' in os.environ:
            test.fail('Configured pinned SDK archive is missing')
        test.skipTest('Pinned public SDK archive required')
    wanted = (*patcher.FILES, 'lib/cstdmf/profiler.cpp', 'lib/cstdmf/profiler.hpp')
    result = {}
    with tarfile.open(archive) as tar:
        for member in tar:
            for name in wanted:
                if member.name.endswith('/programming/bigworld/' + name):
                    result[name] = tar.extractfile(member).read().decode('utf-8-sig')
    test.assertEqual(set(result), set(wanted))
    return result


def host(source):
    cpp,hpp,ipp=(source[n] for n in patcher.FILES)
    block=patcher.function
    declarations = block(hpp, 'class GPUProfiler :').replace('private:', 'public:') + ';'
    scope = block(hpp, 'class AutoGPUProfilerScope') + ';'
    methods = []
    for name in ('GPUProfiler::GPUProfiler()', 'GPUProfiler::~GPUProfiler()', 'void GPUProfiler::init()',
                 'void GPUProfiler::fini()', 'void GPUProfiler::createUnmanagedObjects()',
                 'void GPUProfiler::createUnmanagedObjectsInternal()', 'void GPUProfiler::deleteUnmanagedObjects()',
                 'void GPUProfiler::beginFrame()', 'void GPUProfiler::endFrame()',
                 ('uint64' if 'uint64 GPUProfiler::beginScope' in cpp else 'int')+' GPUProfiler::beginScope(',
                 'void GPUProfiler::endScope('):
        methods.append(block(cpp,name))
    if 'static bool readGPUProfileQuery' in cpp:
        methods.insert(0, block(cpp,'static bool readGPUProfileQuery'))
    for name in ('AutoGPUProfilerScope::AutoGPUProfilerScope(', 'AutoGPUProfilerScope::~AutoGPUProfilerScope()',
                 'GPUProfiler& GPUProfiler::instance()', 'GPUProfiler::GPUProfileEvent::set('):
        method=block(ipp,name)
        if name.startswith('GPUProfiler::GPUProfileEvent::set'):
            method='void '+method
        methods.append(method)
    defines='\n'.join(line for line in cpp.splitlines() if line.startswith('#define RELEASE_QUERY'))
    start=cpp.index('#define CREATE_QUERY');end=cpp.index('#define RELEASE_QUERY',start)
    defines=cpp[start:end]+defines
    methods.insert(0, defines+'\nGPUProfiler GPUProfiler::s_instance_;\n'
                   'uint64 GPUProfiler::GPUProfileEvent::startOfGPUFrameTimeStamp_=0;\n'
                   'uint64 GPUProfiler::GPUProfileEvent::gpuFrequency_=0;\n'
                   'uint64 GPUProfiler::GPUProfileEvent::stampsPerSecond_=0;\n'
                   'static const char*s_primCountName="Prim Count";\n'
                   'static const char*s_drawPrimCallsName="draw Prim Calls";\n')
    return ((ROOT/'tests/stock_gpu_profiler_host.cpp').read_text()
            .replace('@WATCHER_ENABLED@', block(source['lib/cstdmf/profiler.hpp'],'bool watcherEnabled() const'))
            .replace('@PROCESS_ENTRIES@', block(source['lib/cstdmf/profiler.cpp'],'void Profiler::processEntries()'))
            .replace('@ADD_GPU_ENTRY@', block(source['lib/cstdmf/profiler.cpp'],'void Profiler::addGPUEntry('))
            .replace('@GPU_CLASS@',declarations).replace('@GPU_SCOPE_CLASS@',scope)
            .replace('@GPU_METHODS@','\n'.join(methods)))


class GPUProfilerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler=os.environ.get('CXX','c++')
        if not shutil.which(cls.compiler):
            raise unittest.SkipTest('C++ compiler required')

    def test_atomic_idempotent_patch(self):
        source=sources(self);changed=patcher.patch_sources(source)
        self.assertEqual(patcher.patch_sources(changed),changed)
        self.assertEqual(source['lib/cstdmf/profiler.cpp'],changed['lib/cstdmf/profiler.cpp'])
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            for name in patcher.FILES:
                p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(source[name])
            bad=root/patcher.FILES[1];bad.write_text(bad.read_text().replace('int  beginScope','int changedBeginScope'))
            before={name:(root/name).read_bytes() for name in patcher.FILES}
            with self.assertRaises(ValueError):patcher.patch(root)
            self.assertEqual(before,{name:(root/name).read_bytes()for name in patcher.FILES})

    def test_actual_gpu_producers_cpu_hierarchy_and_native_failure(self):
        source=sources(self);changed=patcher.patch_sources(source)
        with tempfile.TemporaryDirectory(prefix='gpu-profiler-host-') as temp:
            root=Path(temp)
            for old,values in ((True,source),(False,changed)):
                cpp=root/('old.cpp' if old else 'fixed.cpp');binary=cpp.with_suffix('')
                cpp.write_text(host(values))
                flags=['-DOLD_CODE'] if old else []
                result=subprocess.run([self.compiler,'-std=c++17','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                                       *flags,str(cpp),'-o',str(binary)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                cases=('dump','off') if old else ('off','dump','transitions','empty','failures','ownership','device','long')
                for case in cases:
                    result=subprocess.run([str(binary),case],capture_output=True,text=True)
                    expected=1 if old and case=='off' else 0
                    self.assertEqual(result.returncode,expected,case+'\n'+result.stdout+result.stderr)
                    if old and case=='off':self.assertIn('Disabled GPU timing still issues queries',result.stderr)


if __name__=='__main__':unittest.main()
