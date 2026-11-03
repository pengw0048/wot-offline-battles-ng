// Resource classification from the pinned v1.1.1 #1513 scenery policy.
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
namespace stock_native_scenery_policy {
inline std::string normalized(std::string name) {
    std::replace(name.begin(),name.end(),'\\','/');
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
    const auto first=name.find_first_not_of(" \t\r\n"),last=name.find_last_not_of(" \t\r\n");
    return first==std::string::npos?std::string():name.substr(first,last-first+1);
}
inline bool railwayVehicle(const std::string& name) {
    const std::string path="/"+normalized(name)+"/";
    return path.find("/rw004_carriage/")!=std::string::npos||path.find("/rw005_locomotive/")!=std::string::npos;
}
inline bool softVegetation(const std::string& filename,double health) {
    if(!std::isfinite(health)||!(health>0.&&health<=5.))return false;
    const auto name=normalized(filename);if(name.empty())return false;
    auto stem=name.substr(name.find_last_of('/')==std::string::npos?0:name.find_last_of('/')+1);
    const auto dot=stem.find_last_of('.');if(dot!=std::string::npos)stem.resize(dot);
    static const char* const stems[]={"aloevera_02","azaliapatch_ld","broomsankeweed_alone","broomsnakeweed","cacti","canereeds","fern","grapes_1","grapes_2","horseweed","ivy","knapweed","marigoldpatch","scrub","sedge_1","waterlily"};
    static const char* const tokens[]={"amurmaple","azalea","bamboocluster","broomsnakeweed_2","bush","canereeds1","cattailcluster","cedar","creepingjuniper","juniper_grass","mulga","mulga_1","northernratacluster","redrosebush","redyucca_rt","rododendron_1","rododendronpink_1","rododendronpink_2","shrub","shrub2","shrub3","shrub_tree","shrubsouth","thicket"};
    for(const auto* candidate:stems)if(stem==candidate)return true;
    for(const auto* token:tokens)if(stem.find(token)!=std::string::npos)return true;
    return false;
}
}
