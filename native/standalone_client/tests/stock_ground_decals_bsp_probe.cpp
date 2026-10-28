#include "stock_ground_decals_bsp.hpp"
#include <cstdio>
struct Node {std::array<float,4> plane;unsigned id;Node* children[2];};
int main(){using namespace wot::GroundDecalMath;std::array<Node,15> nodes;Point center;std::array<Point,3>axes;
while(std::fread(&center,1,12,stdin)==12){if(std::fread(&axes,1,36,stdin)!=36)return 2;for(unsigned i=0;i<nodes.size();++i){if(std::fread(&nodes[i].plane,1,16,stdin)!=16)return 2;nodes[i].id=i;for(unsigned j=0;j<2;++j){unsigned child=i*2+1+j;nodes[i].children[j]=child<nodes.size()?&nodes[child]:nullptr;}}
std::vector<unsigned> visited;visitBox(&nodes[0],center,axes[0],axes[1],axes[2],[](Node*n){return n->plane;},[](Node*n,unsigned i){return n->children[i];},[&](Node*n){visited.push_back(n->id);visited.push_back(n->id?n->id-1:0);});unsigned count=visited.size();std::fwrite(&count,1,4,stdout);std::fwrite(visited.data(),4,count,stdout);}}
