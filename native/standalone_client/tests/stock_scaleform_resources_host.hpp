#pragma once
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_GUARD
#ifdef WOT_REAL_SDK
#include "GFx/GFx_Loader.h"
#include "Kernel/SF_SysFile.h"
#else
namespace Scaleform {
using UByte=unsigned char;
class FileConstants { public: enum { Open_Read=1,Open_Buffered=32,Mode_Read=0444,Mode_ReadWrite=0666 }; };
class String { std::string text_;public:String(const char* p):text_(p){} const char* ToCStr()const{return text_.c_str();} };
class File { int refs_=1;public:virtual~File(){}void AddRef(){++refs_;}void Release(){if(!--refs_)delete this;}virtual const char*GetFilePath()=0;virtual int Read(UByte*,int)=0;virtual bool IsValid()=0;virtual int Seek(int,int=0)=0; };
class MemoryFile:public File {std::string name_;const UByte* bytes_;int size_,index_=0;public:
MemoryFile(const char* n,const UByte* b,int size):name_(n),bytes_(b),size_(size){}const char*GetFilePath()override{return name_.c_str();}
bool IsValid()override{return size_>0;}int Read(UByte* b,int n)override{n=std::max(0,std::min(n,size_-index_));std::memcpy(b,bytes_+index_,n);index_+=n;return n;}int Seek(int n,int=0)override{index_=n;return n;}};
class SysFile:public File {public:static int created;std::string path;SysFile(String p,int,int):path(p.ToCStr()){++created;}const char*GetFilePath()override{return path.c_str();}bool IsValid()override{return true;}int Read(UByte*,int)override{return 0;}int Seek(int n,int=0)override{return n;}};
template<class T>class Ptr {T*p_=nullptr;public:Ptr(){}Ptr(T*p):p_(p){if(p_)p_->AddRef();}Ptr(const Ptr&o):Ptr(o.p_){}~Ptr(){if(p_)p_->Release();}T*GetPtr()const{return p_;}T*operator->()const{return p_;}explicit operator bool()const{return p_!=nullptr;}};
namespace GFx {class FileOpenerBase {int refs_=1;public:virtual~FileOpenerBase(){}void AddRef(){++refs_;}void Release(){if(!--refs_)delete this;}};class FileOpener:public FileOpenerBase {public:virtual File*OpenFile(const char*,int,int)=0;};}
}
#endif
namespace BW {
using string=std::string;
namespace BWUtil { inline bool isAbsolutePath(const string&p){return !p.empty()&&(p[0]=='/'||p[0]=='\\'||(p.size()>1&&p[1]==':'));} }
using GFile=Scaleform::File;using SInt=int;
using Scaleform::MemoryFile;using Scaleform::FileConstants;using Scaleform::UByte;namespace GFx=Scaleform::GFx;
struct BinaryBlock {static std::atomic<int> alive;std::string data;explicit BinaryBlock(const std::string&d):data(d){++alive;}~BinaryBlock(){--alive;}const char*cdata()const{return data.data();}int len()const{return static_cast<int>(data.size());}};
using BinaryPtr=std::shared_ptr<BinaryBlock>;
struct IFileSystem {enum FileType{FT_NOT_FOUND,FT_DIRECTORY,FT_FILE};};
struct ResourceFixture {
 std::mutex mutex;std::map<string,string> files,sections,absolute;std::vector<string> reads,purged;
 BinaryPtr readFile(const char*p){std::lock_guard<std::mutex>lock(mutex);reads.push_back("fs:"+string(p));auto f=files.find(p);return f==files.end()?BinaryPtr():std::make_shared<BinaryBlock>(f->second);}
 BinaryPtr readBinary(const char*p){std::lock_guard<std::mutex>lock(mutex);reads.push_back("ds:"+string(p));auto f=sections.find(p);return f==sections.end()?BinaryPtr():std::make_shared<BinaryBlock>(f->second);}
};
class BWResource {public:static ResourceFixture fixture;static BWResource&instance(){static BWResource s;return s;}ResourceFixture*fileSystem(){return &fixture;}ResourceFixture*rootSection(){return &fixture;}
 static IFileSystem::FileType resolveToAbsolutePath(string&p){auto i=fixture.absolute.find(p);if(i==fixture.absolute.end())return IFileSystem::FT_NOT_FOUND;p=i->second;return IFileSystem::FT_FILE;}
 void purge(const string&p,bool){fixture.purged.push_back(p);}
};
}
