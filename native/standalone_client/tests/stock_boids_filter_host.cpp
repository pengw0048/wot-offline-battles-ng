#include "stock_boids_filter.hpp"
#include <cassert>
#include <fstream>
#include <algorithm>
#include <iostream>
void normalize(wot::StockBoidVector& v){float n=std::sqrt((v.x*v.x+v.y*v.y)+v.z*v.z);if(n){v.x/=n;v.y/=n;v.z/=n;}else v={0,0,0};}
unsigned testCase=0;const char* stage="";void check(float a,float b){if(std::fabs(a-b)>2e-6f*(std::max)(1.f,std::fabs(b)))std::cerr<<stage<<" case="<<testCase<<" actual="<<a<<" expected="<<b<<"\n";assert(std::isnan(a)==std::isnan(b));if(!std::isnan(a))assert(std::fabs(a-b)<=2e-6f*(std::max)(1.f,std::fabs(b)));}
void read(std::ifstream& file,float* value,size_t n){file.read(reinterpret_cast<char*>(value),n*sizeof(float));assert(file);}
int main(int argc,char** argv){assert(argc==3);std::ifstream avoid(argv[1],std::ios::binary),angles(argv[2],std::ios::binary);unsigned count;avoid.read(reinterpret_cast<char*>(&count),4);for(unsigned i=0;i<count;++i){stage="avoid";testCase=i;float r[16];read(avoid,r,16);wot::StockBoidVector delta={r[0],r[1],r[2]};wot::stockBoidAvoidDeadZone(delta,{r[3],r[4],r[5]},{r[6],r[7],r[8]},{r[9],r[10],r[11]},r[12],normalize);check(delta.x,r[13]);check(delta.y,r[14]);check(delta.z,r[15]);}unsigned avoids=count;angles.read(reinterpret_cast<char*>(&count),4);for(unsigned i=0;i<count;++i){stage="angles";testCase=i;float r[23];read(angles,r,23);wot::StockBoidAngles value={r[0],r[1],r[2],r[3],r[4]};wot::stockBoidSteer(value,{r[5],r[6],r[7]},{r[8],r[9],r[10]},{r[11],r[12],r[13]},r[14],r[15],r[16],r[17]!=0,normalize);check(value.yaw,r[18]);check(value.pitch,r[19]);check(value.roll,r[20]);check(value.yawDelta,r[21]);check(value.speed,r[22]);}std::cout<<avoids<<" avoidance and "<<count<<" angle cases match original x86\n";}
