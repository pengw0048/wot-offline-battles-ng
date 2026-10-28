#include "stock_destructible_falling_query_data.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace wot;
StockFallPoint rotate(float yaw,float pitch,const StockFallPoint& point){
 const float sy=std::sin(double(yaw)),cy=std::cos(double(yaw)),sp=std::sin(double(pitch)),cp=std::cos(double(pitch)),sr=0,cr=1;
 const auto a=stockFallAdd,m=stockFallMultiply,s=stockFallSubtract;
 float matrix[4][4]={{a(m(cr,cy),m(m(sr,sp),sy)),m(sr,cp),s(m(m(sr,sp),cy),m(cr,sy)),0},
  {s(m(m(cr,sp),sy),m(sr,cy)),m(cr,cp),a(m(sr,sy),m(m(cr,sp),cy)),0},
  {m(cp,sy),-sp,m(cp,cy),0},{0,0,0,1}};
 StockFallPoint result;for(unsigned c=0;c<3;++c)result[c]=a(a(a(m(point[0],matrix[0][c]),m(point[1],matrix[1][c])),m(point[2],matrix[2][c])),matrix[3][c]);return result;
}
int main(){StockFallPoint origin;float height,yaw,distances[32];
 while(std::fread(origin.data(),4,3,stdin)==3){if(std::fread(&height,4,1,stdin)!=1||std::fread(&yaw,4,1,stdin)!=1||std::fread(distances,4,32,stdin)!=32)return 2;
  unsigned next=0;std::vector<StockFallPoint> segments;
  auto collide=[&](const StockFallPoint& a,const StockFallPoint& b){segments.push_back(a);segments.push_back(b);if(next>=32)std::abort();return distances[next++];};
  float value=stockDestructibleFallConstraint(origin,height,yaw,rotate,collide);unsigned kind=0;
  std::fwrite(&value,4,1,stdout);std::fwrite(&kind,4,1,stdout);std::fwrite(&next,4,1,stdout);
  for(const auto& point:segments)std::fwrite(point.data(),4,3,stdout);
 }
}
