// Appended to the actual production FileOpener definitions by the isolated
// Windows SDK probe. Only BW resource IO is supplied by the fixture.
#include <fstream>
#include <iostream>
#include <thread>
#include <future>
std::atomic<int> BW::BinaryBlock::alive{0};
BW::ResourceFixture BW::BWResource::fixture;
int main()
{
    ::Scaleform::GFx::System system;
    using namespace BW;
    auto& resources=BWResource::fixture;
    resources.files["gui/flash/../maps/test.bin"]="exact";
    resources.sections["gui/maps/test.bin"]="canonical";
    ::Scaleform::Ptr<ScaleformBW::FileOpener> opener = *new ScaleformBW::FileOpener();
    auto* file=opener->OpenFile("GUI\\FLASH\\..\\MAPS/TEST.bin",0,0);
    assert(file && file->IsValid());
    unsigned char bytes[16]={};
    assert(file->Read(bytes,5)==5 && std::string(reinterpret_cast<char*>(bytes),5)=="exact");file->Release();
    resources.files.clear();resources.reads.clear();
    assert(opener->stockPrecache("gui/flash/../maps/test.bin",false));
    assert((resources.reads==std::vector<string>{"fs:gui/flash/../maps/test.bin","ds:gui/maps/test.bin"}));
    file=opener->OpenFile("gui/flash/../maps/test.bin",0,0);
    auto* second=opener->OpenFile("gui/flash/../maps/test.bin",0,0);
    assert(file && second && file->Read(bytes,3)==3 && second->Read(bytes,9)==9);
    assert(std::string(file->GetFilePath())=="gui/flash/../maps/test.bin");
    resources.sections["gui/maps/test.bin"]="reloaded";
    assert(opener->stockPrecache("gui/flash/../maps/test.bin",true));
    auto* newer=opener->OpenFile("gui/flash/../maps/test.bin",0,0);
    assert(newer->Read(bytes,8)==8 && std::string(reinterpret_cast<char*>(bytes),8)=="reloaded");newer->Release();
    resources.sections.clear();assert(!opener->stockPrecache("gui/flash/../maps/test.bin",true));
    newer=opener->OpenFile("gui/flash/../maps/test.bin",0,0);assert(newer);newer->Release();
    opener->stockEraseCached("");assert(!opener->OpenFile("gui/flash/../maps/test.bin",0,0));
    assert(file->Seek(0)==0 && file->Read(bytes,9)==9 && std::string(reinterpret_cast<char*>(bytes),9)=="canonical");
    second->Release();file->Release();assert(BinaryBlock::alive==0);

    std::ofstream("stream.bin",std::ios::binary)<<"physical-stream";
    resources.absolute["video"]="stream.bin";
    opener->stockAddStreamingFile("video");opener->stockEnableStreaming();
    file=opener->OpenFile("video",0,0);
    assert(file && dynamic_cast<::Scaleform::SysFile*>(file) && file->IsValid());
    assert(file->Read(bytes,8)==8 && std::string(reinterpret_cast<char*>(bytes),8)=="physical");
    resources.sections["pak-only"]="packed";opener->stockAddStreamingFile("pak-only");
    auto count=resources.reads.size();assert(!opener->OpenFile("pak-only",0,0));assert(count==resources.reads.size());
    opener->stockAddStreamingFile("C:/missing-scaleform-inventory-file.bin");
    auto* absent=opener->OpenFile("C:/missing-scaleform-inventory-file.bin",0,0);
    assert(absent && !absent->IsValid());absent->Release();
    opener->stockDisableStreaming();second=opener->OpenFile("pak-only",0,0);assert(second);second->Release();
    // Parallel GFx reads have independent file objects and resource leases.
    assert(opener->stockPrecache("pak-only",false));
    std::atomic<bool> good{true};std::vector<std::thread> readers;
    for(int n=0;n<8;++n)readers.emplace_back([&]{
        for(int i=0;i<300;++i){auto*f=opener->OpenFile("pak-only",0,0);unsigned char b[6];if(!f||f->Read(b,6)!=6||std::memcmp(b,"packed",6))good=false;if(f)f->Release();}
    });
    for(auto& thread:readers)thread.join();assert(good);
    second=opener->OpenFile("pak-only",0,0);
    std::promise<void> allowPendingRead;auto permission=allowPendingRead.get_future();
    std::thread pending([retained=opener,&permission]{
        permission.wait();auto* f=retained->OpenFile("pak-only",0,0);unsigned char b[6];
        assert(f&&f->Read(b,6)==6&&std::memcmp(b,"packed",6)==0);f->Release();
    });
    opener.Clear();allowPendingRead.set_value();pending.join();
    assert(second->Read(bytes,6)==6 && std::string(reinterpret_cast<char*>(bytes),6)=="packed");second->Release();
    assert(file->Read(bytes,7)==7 && std::string(reinterpret_cast<char*>(bytes),7)=="-stream");file->Release();
    assert(BinaryBlock::alive==0);
    std::cout<<"Actual Scaleform SDK: lookup priority, cache/force/erase, per-file leases, physical streaming, 2400 parallel opens and opener teardown PASS\n";
}
