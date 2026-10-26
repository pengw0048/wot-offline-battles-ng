// Pure WTau reader probe. Original instruction comparison is driven by Python.
#include "stock_space_sounds_data.hpp"
#include "wot/stock_static_model_binary.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>

int main(int argc,char** argv)
{
    wot::StockSpaceSoundData data;
    std::string error;
    const unsigned char empty[] = {24,0,0,0,0,0,0,0};
    assert(data.read(empty,sizeof(empty),error));
    assert(data.definitions.empty()&&data.unusedTail.empty());
    const unsigned char ignored[] = {24,0,0,0,0,0,0,0,255,1,17};
    assert(data.read(ignored,sizeof(ignored),error)&&data.unusedTail.size()==3);
    auto bad=std::vector<unsigned char>(empty,empty+sizeof(empty));bad[0]=23;
    assert(!data.read(bad.data(),bad.size(),error)&&data.unusedTail.size()==3);
    bad[0]=24;bad[4]=1;
    assert(!data.read(bad.data(),bad.size(),error)&&data.unusedTail.size()==3);
    assert(!data.read(NULL,0,error));
    {
        wot::StockStaticModelBinary binary;
        binary.instances.models={0};binary.instances.transforms.resize(1);
        binary.models.info={{2,0}};binary.models.fragile.resize(1);
        binary.models.fragile[0].words[6]=0xffffffffu;
        wot::StockStaticModelBinary::Matrix local,world;
        assert(wot::stockSpaceModelSoundMatrices(binary,0,local,world,error));
        assert(local[0]==1.f&&local[5]==1.f&&local[10]==1.f&&local[15]==1.f);
        binary.models.fragile[0].words[6]=0;
        assert(!wot::stockSpaceModelSoundMatrices(binary,0,local,world,error)&&!error.empty());
        binary.models.info[0].index=1;
        assert(!wot::stockSpaceModelSoundMatrices(binary,0,local,world,error));
        binary.models.info[0].type=0;
        assert(wot::stockSpaceModelSoundMatrices(binary,0,local,world,error));
        assert(!wot::stockSpaceModelSoundMatrices(binary,1,local,world,error));
    }
    if(argc==4&&std::string(argv[1])=="--models")
    {
        std::ifstream in(argv[2],std::ios::binary);
        std::vector<unsigned char> sections[3];
        for(auto& section:sections)
        {
            std::uint32_t size=0;in.read(reinterpret_cast<char*>(&size),4);section.resize(size);
            if(size)in.read(reinterpret_cast<char*>(section.data()),size);
            assert(in.good());
        }
        wot::StockStaticModelBinary binary;
        assert(binary.read(sections[0].data(),sections[0].size(),sections[1].data(),sections[1].size(),
            sections[2].data(),sections[2].size(),error));
        std::ofstream out(argv[3],std::ios::binary);
        for(std::uint32_t i=0;i<binary.instances.models.size();++i)
        {
            const auto model=binary.instances.models[i];
            if(binary.models.sounds[model]==0xffffffffu)continue;
            wot::StockStaticModelBinary::Matrix local,world;
            assert(wot::stockSpaceModelSoundMatrices(binary,i,local,world,error));
            out.write(reinterpret_cast<const char*>(&i),4);
            out.write(reinterpret_cast<const char*>(local.data()),64);
            out.write(reinterpret_cast<const char*>(world.data()),64);
        }
        assert(out.good());return 0;
    }
    if(argc==5&&std::string(argv[1])=="--identities")
    {
        std::ifstream in(argv[2],std::ios::binary),queries(argv[3],std::ios::binary);
        std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(in),{}};
        wot::StockSpaceSoundIdentities identity;
        assert(identity.read(bytes.data(),bytes.size(),error));
        std::ofstream out(argv[4],std::ios::binary);
        std::uint32_t count=0;queries.read(reinterpret_cast<char*>(&count),4);
        while(count--)
        {
            std::uint32_t chunk=0,item=0,object=0xffffffffu;
            queries.read(reinterpret_cast<char*>(&chunk),4);queries.read(reinterpret_cast<char*>(&item),4);
            assert(queries.good());identity.lookup(chunk,item,object);
            out.write(reinterpret_cast<const char*>(&object),4);
        }
        assert(out.good());return 0;
    }
    if(argc==3)
    {
        std::ifstream in(argv[1],std::ios::binary);
        std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(in),{}};
        assert(in.is_open()&&!in.bad()&&data.read(bytes.data(),bytes.size(),error));
        std::ofstream out(argv[2],std::ios::binary);
        for(const auto& record:data.definitions)
        {
            out.write(reinterpret_cast<const char*>(&record.eventName),4);
            out.write(reinterpret_cast<const char*>(record.position.data()),12);
        }
        assert(out.good());
    }
    else assert(argc==1);
    std::cout<<"WTau first-array reader / ignored tail / transactional failure PASS\n";
}
