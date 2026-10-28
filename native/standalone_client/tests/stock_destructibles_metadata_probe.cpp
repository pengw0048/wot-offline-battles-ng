#include "stock_destructibles_data.hpp"
#include "wot/stock_static_model_binary.hpp"
#include <cstdio>
int main(){
 using Binary=wot::StockStaticModelBinary;
 unsigned kind,entry,words[11],length;
 while(std::fread(&kind,4,1,stdin)==1){
  if(std::fread(&entry,4,1,stdin)!=1||std::fread(words,4,11,stdin)!=11||std::fread(&length,4,1,stdin)!=1||length>128)return 2;
  std::string effect(length,' ');if(std::fread(&effect[0],1,length,stdin)!=length)return 2;
  Binary data;data.instances.models={0};data.models.info.push_back({kind,0});
  if(kind==1){data.models.falling.resize(1);std::memcpy(data.models.falling[0].words,words,44);}
  else {data.models.fragile.resize(1);std::memcpy(data.models.fragile[0].words,words,36);data.models.fragile[0].words[8]=entry;}
  unsigned category=unsigned(wot::stockCompiledDestructibleCategory(data,0)),key=wot::stockCompiledDestructibleEffectKey(data,0,effect);
  float scale=wot::stockCompiledDestructibleEffectScale(data,0);float physics[16]={};if(kind==1)std::memcpy(physics,words+4,28);
  std::fwrite(&category,4,1,stdout);std::fwrite(&key,4,1,stdout);std::fwrite(&scale,4,1,stdout);std::fwrite(physics,4,16,stdout);
 }
}
