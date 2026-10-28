#include "stock_destructibles_data.hpp"
#include "wot/stock_static_model_binary.hpp"
#include <cstdio>
int main()
{
    using Binary=wot::StockStaticModelBinary;
    unsigned count,index,models,fragiles,auxiliary;
    while(std::fread(&count,4,1,stdin)==1)
    {
        if(std::fread(&index,4,1,stdin)!=1||std::fread(&models,4,1,stdin)!=1||std::fread(&fragiles,4,1,stdin)!=1||std::fread(&auxiliary,4,1,stdin)!=1)return 2;
        Binary data;data.instances.models.resize(count);data.instances.flags.resize(count);data.instances.triples.resize(count);data.instances.auxiliary.resize(auxiliary);
        data.models.info.resize(models);data.models.fragile.resize(fragiles);
        for(auto& value:data.instances.flags)std::fread(&value.activity,4,1,stdin);
        std::fread(data.instances.models.data(),4,count,stdin);std::fread(data.models.info.data(),8,models,stdin);
        std::fread(data.models.fragile.data(),36,fragiles,stdin);std::fread(data.instances.triples.data(),12,count,stdin);std::fread(data.instances.auxiliary.data(),4,auxiliary,stdin);
        wot::StockDestructibleGeometryChanges changes;std::string error;
        if(!wot::destroyStockCompiledDestructible(data,index,false,changes,error))return 3;
        for(const auto& value:data.instances.flags)std::fwrite(&value.activity,4,1,stdout);
        std::fwrite(data.instances.models.data(),4,count,stdout);
        for(const auto* values:{&changes.added,&changes.removed,&changes.updated})
        {unsigned size=values->size();std::fwrite(&size,4,1,stdout);std::fwrite(values->data(),4,size,stdout);}
    }
}
