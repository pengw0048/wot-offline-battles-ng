// The runner inserts the pinned SDK archive-stream and mutex owners.
// Only the platform calls and archive metadata index are supplied by the host.

#include <cstdio>
#include <mutex>
using int32=int;
#define PRIzu "zu"
#define MF_ASSERT_NOALLOC(value) assert(value)
#define MF_VERIFY(value) assert(value)
#define ERROR_MSG(...) std::fprintf(stderr,__VA_ARGS__)
#define CRITICAL_MSG(...) assert(false)
using CRITICAL_SECTION=std::recursive_mutex;
static void InitializeCriticalSection(CRITICAL_SECTION*){}
static void DeleteCriticalSection(CRITICAL_SECTION*){}
static void EnterCriticalSection(CRITICAL_SECTION* value){value->lock();}
static void LeaveCriticalSection(CRITICAL_SECTION* value){value->unlock();}
static bool TryEnterCriticalSection(CRITICAL_SECTION* value){return value->try_lock();}

@MUTEX@

struct IFileStreamer {
    virtual ~IFileStreamer()=default;
    virtual size_t read(size_t,void*)=0;
    virtual bool skip(int)=0;
    virtual bool setOffset(size_t)=0;
    virtual size_t getOffset()const=0;
    virtual void* memoryMap(size_t,size_t,bool)=0;
    virtual void memoryUnmap(void*)=0;
};
struct FileStreamerPtr:std::shared_ptr<IFileStreamer> {
    using std::shared_ptr<IFileStreamer>::shared_ptr;
    FileStreamerPtr()=default;
    bool exists()const{return bool(*this);}
    FileStreamerPtr& operator=(IFileStreamer* value){reset(value);return *this;}
};
struct IFileSystem {virtual ~IFileSystem()=default;virtual FileStreamerPtr streamFile(const BW::StringRef&)=0;};
struct ZipFileSystem:IFileSystem {
    enum {METHOD_STORE=0};
    struct Entry {unsigned compressionMethod=0;size_t uncompressedSize=0;};
    struct LocalFile {Entry entry_;size_t offset=0;size_t dataOffset()const{return offset;}};
    using FileMap=std::map<std::string,std::pair<size_t,int>>;
    IFileSystem* parentSystem_=nullptr;SimpleMutex mutex_;
    FILE* pFile_=std::tmpfile();std::string path_="owned-test.pkg";
    FileMap fileMap_,dirMap_;std::vector<LocalFile> centralDirectory_;
    ZipFileSystem(){assert(pFile_);}
    ~ZipFileSystem(){std::fclose(pFile_);}
    void resolveDuplicate(std::string&){}
    FileMap::iterator fileMapLookup(FileMap& map,const std::string& path){return map.find(path);}
    bool openZip(){return pFile_!=nullptr;}
    void beginReadLocalFile(LocalFile&){}
    FileStreamerPtr streamFile(const BW::StringRef&)override;
    void add(const std::string& name,const std::vector<unsigned char>& bytes){
        std::fseek(pFile_,0,SEEK_END);LocalFile local;local.offset=std::ftell(pFile_);local.entry_.uncompressedSize=bytes.size();
        std::fwrite(bytes.data(),1,bytes.size(),pFile_);std::fflush(pFile_);
        fileMap_[name]={centralDirectory_.size(),0};centralDirectory_.push_back(local);
    }
};
using ZipFileSystemPtr=ZipFileSystem*;
struct ReadWriteLock {struct ReadGuard {explicit ReadGuard(int&) {}};};
struct MultiFileSystem:IFileSystem {
    using FileSystemVector=std::vector<IFileSystem*>;
    FileSystemVector baseFileSystems_;int baseFileSystemsLock_=0;
    FileStreamerPtr streamFile(const BW::StringRef&)override;
};

@STREAMER@

@ZIP_STREAM@

@MULTI_STREAM@

struct FileSystem:MultiFileSystem {
    ZipFileSystem archive;
    FileSystem(){baseFileSystems_.push_back(&archive);}
    void add(const std::string& name,const std::vector<unsigned char>& bytes){archive.add(name,bytes);}
};
