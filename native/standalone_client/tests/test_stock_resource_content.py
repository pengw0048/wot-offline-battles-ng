"""Original application-content producer, snapshot and physical lookup tests."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


def body(source,signature):
    start=source.index(signature);brace=source.index('{',start);depth=1;end=brace+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]


def compile_run(test,source,data=None):
    with tempfile.TemporaryDirectory(prefix='resource-content-owner-') as directory:
        cpp,binary=Path(directory)/'probe.cpp',Path(directory)/'probe';cpp.write_text(source)
        command=['c++','-std=c++14','-Wall','-Wextra','-Werror','-UNDEBUG']
        if sys.platform!='win32':command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        result=subprocess.run(command+[str(cpp),'-o',str(binary)],capture_output=True,text=True)
        test.assertEqual(result.returncode,0,result.stderr)
        result=subprocess.run([str(binary)],input=data,capture_output=True)
        test.assertEqual(result.returncode,0,result.stderr.decode())
        return result.stdout


PREFIX=r'''
#include <cassert>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace BW{using string=std::string;struct StringRef{std::string value;StringRef(const char* v):value(v){}const char* data()const{return value.data();}size_t size()const{return value.size();}};}
struct Binary{std::string name;bool parse=true,child=true;};
using BinaryPtr=std::shared_ptr<Binary>;
struct Section;using DataSectionPtr=std::shared_ptr<Section>;
struct Section{BinaryPtr bytes;bool leaf=false;DataSectionPtr openSection(const char* name){assert(std::string(name)=="app_type");if(!bytes->child)return nullptr;auto p=std::make_shared<Section>();p->bytes=bytes;p->leaf=true;return p;}std::string asString(){assert(leaf);return bytes->name;}};
namespace XMLSection{DataSectionPtr createFromBinary(const char* root,const BinaryPtr& b){assert(std::string(root)=="protocol");if(!b->parse)return nullptr;auto p=std::make_shared<Section>();p->bytes=b;return p;}}
struct IFileSystem{enum FileType{FT_NOT_FOUND,FT_FILE};std::map<std::string,BinaryPtr> entries;std::vector<std::string> queries,reads;FileType getFileType(const std::string& p){queries.push_back(p);return entries.count(p)?FT_FILE:FT_NOT_FOUND;}BinaryPtr readFile(const std::string& p){reads.push_back(p);return entries.at(p);}};
'''


class ResourceContentTests(unittest.TestCase):
    def test_actual_physical_reader_and_lifetime(self):
        source=(ROOT/'bigworld/stock_resource_content.cpp').read_text()
        production=body(source,'int loadStockResourceContentType(')
        probe=PREFIX+production+r'''
int main(){
IFileSystem files;files.entries["app/app_type.xml"]=std::make_shared<Binary>(Binary{"hd",true,true});
files.entries["cwd/app_type.xml"]=std::make_shared<Binary>(Binary{"sandbox",true,true});
files.entries["mount/app_type.xml"]=std::make_shared<Binary>(Binary{"tutorial",true,true});
const int snapshot=loadStockResourceContentType(files,"app/");assert(snapshot==2);
assert((files.queries==std::vector<std::string>{"app/app_type.xml"}));assert(files.reads==files.queries);
files.entries["app/app_type.xml"]->name="sd";assert(snapshot==2);assert(files.reads.size()==1);
assert(loadStockResourceContentType(files,"app/")==1);
assert(loadStockResourceContentType(files,"missing/")==0&&files.reads.size()==2);
files.entries["app/app_type.xml"].reset();assert(loadStockResourceContentType(files,"app/")==0);
files.entries["app/app_type.xml"]=std::make_shared<Binary>(Binary{"hd",false,true});assert(loadStockResourceContentType(files,"app/")==0);
files.entries["app/app_type.xml"]->parse=true;files.entries["app/app_type.xml"]->child=false;assert(loadStockResourceContentType(files,"app/")==0);
files.entries["app/app_type.xml"]->child=true;
for(const char* name:{"HD"," sd","hd ","unknown",""}){files.entries["app/app_type.xml"]->name=name;assert(loadStockResourceContentType(files,"app/")==0);}
}
'''
        compile_run(self,probe)

    def test_original_exact_name_selection(self):
        client=Path(os.environ.get('WOT_0922_CLIENT','/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD'))
        python=Path(os.environ.get('WOT_ORACLE_PYTHON','/tmp/wot-native-boundaries-venv/bin/python'))
        if not (client/'WorldOfTanks.exe').is_file() or not python.is_file():self.skipTest('Pinned original PE and Unicorn required')
        names=['full','sd','hd','incomplete','tutorial','sandbox','','FULL','HD','SD','Sandbox',' hd','hd ','unknown','incomplete-extra','abcdefghijklmnop','hd\x00other']
        script='import json,sys;sys.path.insert(0,sys.argv[1]);from stock_resource_content_oracle import content_types;print(json.dumps(content_types(sys.argv[2],json.load(sys.stdin))))'
        result=subprocess.run([str(python),'-c',script,str(ROOT/'tests'),str(client/'WorldOfTanks.exe')],input=json.dumps(names).encode(),capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr.decode())
        expected=json.loads(result.stdout)
        source=(ROOT/'bigworld/stock_resource_content.cpp').read_text()
        main=['int main(){IFileSystem files;auto b=std::make_shared<Binary>();files.entries["app_type.xml"]=b;']
        for name in names:
            literal=json.dumps(name).replace('\\u0000','\\0')
            main.append('b->name=std::string(%s,%d);printf("%%d\\n",loadStockResourceContentType(files,""));'%(literal,len(name.encode())))
        main+=['}']
        output=compile_run(self,PREFIX+body(source,'int loadStockResourceContentType(')+'\n'+'\n'.join(main))
        self.assertEqual([int(v) for v in output.splitlines()],expected)
        self.assertEqual(expected[:6],list(range(6)))

    def test_pristine_patch_owner_and_idempotence(self):
        spec=importlib.util.spec_from_file_location('resource_content_patch',ROOT/'bigworld/patch_stock_resource_content.py')
        patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(patch)
        archive=Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE','/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():self.skipTest('Pinned public archive required')
        with tempfile.TemporaryDirectory(prefix='resource-content-patch-') as directory,tarfile.open(archive) as original:
            root=Path(directory)
            for member in original.getmembers():
                for relative in ('lib/resmgr/bwresource.cpp','lib/resmgr/bwresource.hpp'):
                    if member.name.endswith('/programming/bigworld/'+relative):
                        path=root/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(original.extractfile(member).read())
            patch.patch(root);once={p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()}
            patch.patch(root);self.assertEqual(once,{p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()})
            source=(root/'lib/resmgr/bwresource.cpp').read_text()
            self.assertEqual(source.count('loadStockResourceContentType(*nativeFileSystem_, getAppDirectory())'),1)
            self.assertLess(source.index('nativeFileSystem_ = NativeFileSystem::create'),source.index('loadStockResourceContentType(*nativeFileSystem_'))
            self.assertIn('return pimpl_->stockContentType_;',source)
            path=root/'lib/resmgr/bwresource.hpp';path.write_text('changed header');before={p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()}
            with self.assertRaises(ValueError):patch.patch(root)
            self.assertEqual(before,{p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()})


if __name__=='__main__':unittest.main()
