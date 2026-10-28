// D3DX and ClientSpace are declared boundaries. The actual SDK visit dispatch,
// production collision filter and complete production query owner run here.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>
#include "stock_destructible_falling_query_data.hpp"
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
namespace BW {
using uint32=std::uint32_t;
struct Vector3 {float x=0,y=0,z=0;Vector3(){}Vector3(float a,float b,float c):x(a),y(b),z(c){}};
struct Matrix {float v[4][4];};
void D3DXMatrixRotationYawPitchRoll(Matrix* out,float yaw,float pitch,float roll){
 const auto a=wot::stockFallAdd,m=wot::stockFallMultiply,s=wot::stockFallSubtract;
 const float sy=std::sin(double(yaw)),cy=std::cos(double(yaw)),sp=std::sin(double(pitch)),cp=std::cos(double(pitch)),sr=std::sin(double(roll)),cr=std::cos(double(roll));
 *out={{{a(m(cr,cy),m(m(sr,sp),sy)),m(sr,cp),s(m(m(sr,sp),cy),m(cr,sy)),0},
 {s(m(m(cr,sp),sy),m(sr,cy)),m(cr,cp),a(m(sr,sy),m(m(cr,sp),cy)),0},
 {m(cp,sy),-sp,m(cp,cy),0},{0,0,0,1}}};
}
void D3DXVec3TransformCoord(Vector3* out,const Vector3* input,const Matrix* matrix){
 const auto a=wot::stockFallAdd,m=wot::stockFallMultiply;Vector3 result(0,0,0);
 for(unsigned c=0;c<3;++c) {
  (&result.x)[c]=a(a(a(m(input->x,matrix->v[0][c]),m(input->y,matrix->v[1][c])),m(input->z,matrix->v[2][c])),matrix->v[3][c]);
 }
 *out=result;
}
struct CollisionObstacle {};
const unsigned TRIANGLE_WATER=0x40;
struct WorldTriangle {unsigned value;unsigned flags()const{return value;}};
@COLLISION_CONSTANTS@
@COLLISION_CLASS@
class ClientSpace {
public:
 unsigned flags=0,count=0;float distance=.5f;
 std::vector<std::array<float,6>> segments;
 float collide(const Vector3& start,const Vector3& end,CollisionCallback& callback){
  assert(!callback.useHighestAvailableLod());++count;segments.push_back({{start.x,start.y,start.z,end.x,end.y,end.z}});
  const int result=callback.visit(CollisionObstacle{},WorldTriangle{flags},distance);
  return result==COLLIDE_ALL?-1.f:distance;
 }
};
}
@PRODUCTION@
int main(){using namespace BW;unsigned flags;float distance;
 while(std::scanf("%u %f",&flags,&distance)==2){StockFallCollision callback;callback.closest=100;callback.flags=0xabababab;
  const int response=callback.visit(CollisionObstacle{},WorldTriangle{flags},distance);
  std::fwrite(&response,4,1,stdout);std::fwrite(&callback.closest,4,1,stdout);std::fwrite(&callback.flags,4,1,stdout);
 }
 ClientSpace native;const Vector3 origin(23,9,-17);
 float pitch=queryStockDestructibleFallPitch(native,origin,10,0);assert(pitch==wot::stockFallSubtract(1.5707963705062866f,1.4835299253463745f)&&native.count==1);
 assert(native.segments[0][0]==23&&native.segments[0][1]==18.5f&&native.segments[0][2]==-17);
 native=ClientSpace();native.flags=0x1000|0x40;
 pitch=queryStockDestructibleFallPitch(native,origin,10,0);assert(pitch==wot::stockFallSubtract(1.5707963705062866f,-1.4835299253463745f)&&native.count==2);
 native=ClientSpace();native.flags=71<<8;
 pitch=queryStockDestructibleFallPitch(native,origin,10,0);assert(pitch>3.f&&native.count==2);
 native=ClientSpace();queryStockDestructibleFallPitch(native,origin,0,0);
 assert(native.segments[0][1]==wot::stockFallAdd(9.f,wot::stockFallMultiply(.2f,.95f)));
 native=ClientSpace();queryStockDestructibleFallPitch(native,origin,std::numeric_limits<float>::quiet_NaN(),0);
 assert(native.segments[0][1]==wot::stockFallAdd(9.f,wot::stockFallMultiply(.2f,.95f))); // Original MAXSS selects its second operand for NaN.
}
