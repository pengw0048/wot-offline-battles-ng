"""Run actual public macro registration and patched Scaleform vararg bodies."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).parents[1]


def load_patch(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'bigworld' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class StockStartupPermutationTests(unittest.TestCase):
    def setUp(self):
        self.archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE',
                            '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        self.compiler = shutil.which('c++')
        if not self.archive.is_file() or not self.compiler:
            self.skipTest('Pinned public engine source and host C++ compiler required')
        self.temporary = tempfile.TemporaryDirectory(prefix='wot-startup-body-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def extract(self, relative):
        with tarfile.open(self.archive) as archive:
            name = next(name for name in archive.getnames()
                        if name.endswith('/programming/bigworld/' + relative))
            text = archive.extractfile(name).read().decode('utf-8')
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def compile(self, source):
        path = self.root / 'probe.cpp'
        path.write_text(source)
        output = self.root / 'probe'
        subprocess.run([self.compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror',
                        str(path), '-o', str(output)], check=True)
        return output

    def test_original_macro_order_and_supported_options_select_packaged_variants(self):
        renderer = self.extract('lib/moo/renderer.cpp')
        macro_header = self.extract('lib/moo/effect_macro_setting.hpp').read_text()
        macro_source = self.extract('lib/moo/effect_macro_setting.cpp').read_text()
        patch = load_patch('patch_stock_effect_owners')
        patch.patch_macro_settings(self.root)
        generated = renderer.read_text()
        patch.patch_macro_settings(self.root)
        self.assertEqual(renderer.read_text(), generated)
        owner = generated[generated.index('namespace\n{'):generated.index('//-- define singleton.')]
        actual = '\n'.join(line for line in (macro_header + '\n' + macro_source).splitlines()
                           if not line.startswith('#include'))
        source = r'''
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#define BW_GUARD
namespace BW {using string=std::string; using stringstream=std::stringstream;
 template<class T> using vector=std::vector<T>;}
template<class T> using SmartPointer=T*;
template<class T> void bw_safe_delete(T*& value){delete value;value=nullptr;}
namespace Moo {
class GraphicsSetting {
 int active_;
 std::vector<bool> supported_;
public:
 GraphicsSetting(const std::string&,const std::string&,int active,bool,bool,bool):active_(active){}
 virtual ~GraphicsSetting()=default;
 virtual void onOptionSelected(int)=0;
 int activeOption() const{return active_;}
 void addOption(const std::string&,const std::string&,bool supported){supported_.push_back(supported);}
 static void add(GraphicsSetting* setting){
  if(setting->active_<0){
   for(std::size_t i=0;i<setting->supported_.size();++i)if(setting->supported_[i]){
    setting->active_=static_cast<int>(i);setting->onOptionSelected(setting->active_);return;
   }
   assert(false);
  }
 }
};
struct EffectCompiler {
 static std::map<std::string,std::string> definitions;
 static std::string infix;
 static void setMacroDefinition(const std::string& key,const std::string& value){definitions[key]=value;}
 static void fxoInfix(const std::string& value){infix=value;}
};
std::map<std::string,std::string> EffectCompiler::definitions;
std::string EffectCompiler::infix;
}
''' + actual + '\n' + owner + r'''
int main(int argc,char** argv){
 assert(argc==3);
 const bool deferred=std::atoi(argv[1])!=0;
 g_pipelineMacroSetting->addOption("DEFERRED","Deferred shading",deferred,"1");
 g_pipelineMacroSetting->addOption("FORWARD","Forward shading",true,"0");
 Moo::GraphicsSetting::add(g_pipelineMacroSetting);
 if(std::atoi(argv[2]))std::reverse(static_settings().begin(),static_settings().end());
 Moo::EffectMacroSetting::setupSettings();
 Moo::EffectMacroSetting::updateManagerInfix();
 assert(static_settings().size()==2);
 assert(Moo::EffectCompiler::infix==(deferred?"10":"11"));
 const auto& definitions=Moo::EffectCompiler::definitions;
 assert(definitions.at("SHADER_DEBUG")=="0");
 assert(definitions.at("WG_DEFERRED_SHADING")== (deferred?"1":"0"));
 assert(definitions.at("BW_DEFERRED_SHADING")== (deferred?"1":"0"));
}
'''
        output = self.compile(source)
        for deferred in ('0', '1'):
            for reversed_order in ('0', '1'):
                subprocess.run([str(output), deferred, reversed_order], check=True)

    def test_scaleform_formats_variadic_values_and_preserves_severity(self):
        log = self.extract('lib/scaleform/log.cpp')
        formatter = self.extract('lib/cstdmf/log_msg.cpp').read_text()
        patch = load_patch('patch_scaleform_sdk')

        def edit(path, before, after, label):
            path.write_text(patch.replace_once(path.read_text(), before, after, label))

        patch.log_edits(self.root, edit)
        generated = log.read_text()
        patch.log_edits(self.root, edit)
        self.assertEqual(log.read_text(), generated)
        body = generated[generated.index('void Log::LogMessageVarg'):generated.index('} // namespace ScaleformBW')]
        begin = formatter.index('/*static*/ void LogMsg::formatMessage(')
        formatter = formatter[begin:formatter.index('\n\n\n/**', begin)]
        source = r'''
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#define BW_GUARD
#define IF_NOT_MF_ASSERT_DEV(value) if(!(value))
#define bw_vsnprintf std::vsnprintf
namespace BW {
struct LogMsg {static void formatMessage(char*,std::size_t,const char*,va_list);};
''' + formatter + r'''
}
using LogMessageId=int;
const int Log_Warning=1,Log_Message=2;
std::string output;
int severity=0;
void capture(int level,const char* format,...){
 assert(std::string(format)=="%s");
 char buffer[16384];va_list arguments;va_start(arguments,format);
 std::vsnprintf(buffer,sizeof(buffer),format,arguments);va_end(arguments);
 output=buffer;severity=level;
}
#define WARNING_MSG(...) capture(1,__VA_ARGS__)
#define INFO_MSG(...) capture(2,__VA_ARGS__)
#define ERROR_MSG(...) capture(3,__VA_ARGS__)
struct Log {void LogMessageVarg(LogMessageId,const char*,va_list);};
''' + body + r'''
void emit(int level,const char* format,...){
 va_list arguments;va_start(arguments,format);Log log;
 log.LogMessageVarg(level,format,arguments);va_end(arguments);
}
int main(){
 emit(Log_Warning,"movie=%s line=%d amount=%.2f %%", "login.swf",42,1.25);
 assert(output=="movie=login.swf line=42 amount=1.25 %"&&severity==1);
 emit(Log_Message,"%s\n","ready");assert(output=="ready"&&severity==2);
 emit(0,"%s","");assert(output.empty()&&severity==3);
 const std::string longMessage(20000,'x');
 emit(Log_Warning,"%s",longMessage.c_str());assert(output==std::string(16383,'x'));
}
'''
        subprocess.run([str(self.compile(source))], check=True)


if __name__ == '__main__':
    unittest.main()
