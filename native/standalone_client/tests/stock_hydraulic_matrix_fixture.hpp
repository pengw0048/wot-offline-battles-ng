#pragma once
// Full affine float matrix boundary. Rotation order and angle extraction follow
// the public SDK Matrix::setRotate/yaw/pitch/roll, rather than adding Euler angles.
#include <algorithm>
#include <cmath>
namespace BW {
struct Vector3 {
 float x=0.,y=0.,z=0.;Vector3(float a=0.,float b=0.,float c=0.):x(a),y(b),z(c){}
 float&operator[](unsigned i){return (&x)[i];}float operator[](unsigned i)const{return (&x)[i];}
 Vector3 operator-(const Vector3&v)const{return {x-v.x,y-v.y,z-v.z};}
 Vector3 operator-()const{return {-x,-y,-z};}
 float length()const{return std::sqrt(x*x+y*y+z*z);}void normalise(){const float n=length();if(n>0.){x/=n;y/=n;z/=n;}}
};
struct Matrix {
 Vector3 rows[4]{{1,0,0},{0,1,0},{0,0,1},{0,0,0}};
 Vector3 applyVector(const Vector3&v)const{Vector3 out;for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)out[i]+=v[j]*rows[j][i];return out;}
 Vector3 applyPoint(const Vector3&v)const{auto out=applyVector(v);for(unsigned i=0;i<3;++i)out[i]+=rows[3][i];return out;}
 Vector3 applyToOrigin()const{return rows[3];}void translation(Vector3 p){rows[3]=p;}
 void multiply(const Matrix&a,const Matrix&b){Matrix out;for(unsigned i=0;i<3;++i)out.rows[i]=b.applyVector(a.rows[i]);out.rows[3]=b.applyPoint(a.rows[3]);*this=out;}
 void postMultiply(const Matrix&b){Matrix a=*this;multiply(a,b);}void preMultiply(const Matrix&a){Matrix b=*this;multiply(a,b);}
 void setRotateY(float a){*this=Matrix();const float c=std::cos(a),s=std::sin(a);rows[0]={c,0,-s};rows[2]={s,0,c};}
 void preRotateX(float a){Matrix r;const float c=std::cos(a),s=std::sin(a);r.rows[1]={0,c,s};r.rows[2]={0,-s,c};preMultiply(r);}
 void preRotateZ(float a){Matrix r;const float c=std::cos(a),s=std::sin(a);r.rows[0]={c,s,0};r.rows[1]={-s,c,0};preMultiply(r);}
 void setRotate(float yaw,float pitch,float roll){setRotateY(yaw);preRotateX(pitch);preRotateZ(roll);}
 float yaw()const{auto z=rows[2];z.normalise();return std::atan2(z.x,z.z);}
 float pitch()const{auto z=rows[2];z.normalise();return -std::asin(std::clamp(z.y,-1.f,1.f));}
 float roll()const{auto x=rows[0],y=rows[1];x.normalise();y.normalise();return std::atan2(x.y,y.y);}
 bool invert(const Matrix&v){const float a=v.rows[0].x,b=v.rows[0].y,c=v.rows[0].z,d=v.rows[1].x,e=v.rows[1].y,f=v.rows[1].z,g=v.rows[2].x,h=v.rows[2].y,i=v.rows[2].z,det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);if(det==0.)return false;rows[0]={(e*i-f*h)/det,(c*h-b*i)/det,(b*f-c*e)/det};rows[1]={(f*g-d*i)/det,(a*i-c*g)/det,(c*d-a*f)/det};rows[2]={(d*h-e*g)/det,(b*g-a*h)/det,(a*e-b*d)/det};rows[3]=-applyVector(v.rows[3]);return true;}
};
}
