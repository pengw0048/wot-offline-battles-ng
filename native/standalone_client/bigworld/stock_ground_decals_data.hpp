#pragma once
#include <array>
#include <cmath>
#include <vector>
#include <limits>
#include <cstring>

namespace wot { namespace GroundDecalMath
{
using Point=std::array<float,3>;
using Triangle=std::array<Point,3>;
struct Contact {Point point,normal;};
inline float add(float a,float b){volatile float r=a+b;return r;}
inline float sub(float a,float b){volatile float r=a-b;return r;}
inline float mul(float a,float b){volatile float r=a*b;return r;}
inline float div(float a,float b){volatile float r=a/b;return r;}
inline Point minus(const Point&a,const Point&b){return {{sub(a[0],b[0]),sub(a[1],b[1]),sub(a[2],b[2])}};}
inline Point plus(const Point&a,const Point&b){return {{add(a[0],b[0]),add(a[1],b[1]),add(a[2],b[2])}};}
inline Point scale(const Point&a,float b){return {{mul(a[0],b),mul(a[1],b),mul(a[2],b)}};}
inline float dot(const Point&a,const Point&b){return add(add(mul(a[0],b[0]),mul(a[1],b[1])),mul(a[2],b[2]));}
inline Point cross(const Point&a,const Point&b){return {{sub(mul(a[1],b[2]),mul(a[2],b[1])),sub(mul(a[2],b[0]),mul(a[0],b[2])),sub(mul(a[0],b[1]),mul(a[1],b[0]))}};}
inline Point unit(const Point&a)
{
    const float squared=dot(a,a);
    if(squared==0.f)
    {
        // Original x86 SSE invalid 0*infinity produces its negative quiet NaN.
        // ARM hosts choose the opposite sign for this otherwise identical case.
        const unsigned bits=0xffc00000u;float value;std::memcpy(&value,&bits,4);
        return {{value,value,value}};
    }
    return scale(a,div(1.f,std::sqrt(squared)));
}
inline std::vector<Point> clipPlane(const std::vector<Point>& polygon,const Point& point,const Point& normal)
{
    std::vector<Point> result;
    if(polygon.empty()) return result;
    const float plane=dot(normal,point);
    Point previous=polygon.back();float previousDot=dot(normal,previous),previousDistance=sub(previousDot,plane);
    for(const auto& value:polygon)
    {
        const float currentDot=dot(normal,value),distance=sub(currentDot,plane);
        if(mul(previousDistance,distance)<0.f)
        {
            const float difference=sub(currentDot,previousDot);
            if(mul(difference,difference)>1.e-8f)
                result.push_back(plus(previous,scale(minus(value,previous),div(sub(plane,previousDot),difference))));
        }
        if(distance>-.001f) result.push_back(value);
        previous=value;previousDot=currentDot;previousDistance=distance;
    }
    return result;
}
inline std::vector<Point> clipFace(const std::vector<Point>& face,const Point& direction,std::vector<Point> polygon)
{
    Point axis=direction;
    if(dot(cross(minus(face[0],face.back()),minus(face[1],face[0])),axis)>0.f)axis=scale(axis,-1.f);
    for(unsigned index=0;index<face.size() && !polygon.empty();++index)
    {
        const unsigned previous=index?index-1:static_cast<unsigned>(face.size()-1);
        polygon=clipPlane(polygon,face[index],cross(minus(face[index],face[previous]),axis));
    }
    return polygon;
}
struct Face {std::vector<Point> points;Point normal;};
inline Face boxFace(const Point& center,float radius,unsigned axis,float sign)
{
    Face result;result.normal={{0,0,0}};result.normal[axis]=sign;
    const unsigned first=(axis+1)%3,second=(axis+2)%3;
    for(const auto& signs:std::array<std::array<float,2>,4>{{{{-1,-1}},{{1,-1}},{{1,1}},{{-1,1}}}})
    {
        Point value=center;value[axis]=add(value[axis],mul(radius,sign));
        value[first]=add(value[first],mul(radius,signs[0]));
        value[second]=add(value[second],mul(radius,signs[1]));
        result.points.push_back(value);
    }
    return result;
}
inline std::vector<Contact> contacts(const Triangle& triangle,const Point& center,float radius)
{
    const Point normal=cross(minus(triangle[2],triangle[0]),minus(triangle[2],triangle[1]));
    const Point normalized=unit(normal);
    struct Axis {Point value;float length;unsigned box,edge,kind;};
    std::vector<Axis> axes;
    for(unsigned i=0;i<3;++i){Point axis={{0,0,0}};axis[i]=1.f;axes.push_back({axis,1.f,i,0,0});}
    axes.push_back({normal,std::sqrt(dot(normal,normal)),0,0,1});
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)
    {
        const Point axis=cross(axes[i].value,minus(triangle[(j+1)%3],triangle[j]));
        const float squared=dot(axis,axis);
        if(squared>=.00001f)axes.push_back({axis,std::sqrt(squared),i,j,2});
    }
    unsigned selected=0;float sign=-1.f,best=std::numeric_limits<float>::infinity();
    for(unsigned i=0;i<axes.size();++i)
    {
        const Point& axis=axes[i].value;
        const float projectedCenter=dot(center,axis);
        const float extent=mul(radius,add(add(std::abs(axis[0]),std::abs(axis[1])),std::abs(axis[2])));
        const float minimum=sub(projectedCenter,extent),maximum=add(projectedCenter,extent);
        float low=dot(axis,triangle[0]),high=dot(axis,triangle[1]);if(high<low)std::swap(low,high);
        const float third=dot(axis,triangle[2]);if(third>high)high=third;else if(third<low)low=third;
        if(minimum>high || low>maximum)return {};
        const float left=sub(maximum,low),right=sub(high,minimum);
        const float depth=div(left<right?left:right,axes[i].length);
        if(best>depth){selected=i;sign=right>left?1.f:-1.f;}
        // MINSS selects its second operand on unordered input too.
        best=best<depth?best:depth;
    }
    const Axis& axis=axes[selected];const Point direction=scale(axis.value,div(sign,axis.length));
    std::vector<Contact> output;
    unsigned kind=axis.kind,faceAxis=axis.box;float faceSign=sign;
    if(kind==2)
    {
        faceAxis=0;for(unsigned i=1;i<3;++i)
            if(mul(direction[i],direction[i])>mul(direction[faceAxis],direction[faceAxis]))faceAxis=i;
        const float boxAlignment=mul(direction[faceAxis],direction[faceAxis]);
        const float alignment=dot(direction,normalized),triangleAlignment=mul(alignment,alignment);
        if((boxAlignment>triangleAlignment?boxAlignment:triangleAlignment)>=.99f)
            kind=boxAlignment>triangleAlignment?0:1;
        faceSign=direction[faceAxis]>0.f?1.f:-1.f;
    }
    if(kind==0)
    {
        const Face face=boxFace(center,radius,faceAxis,faceSign);
        const std::vector<Point> polygon=clipFace(face.points,direction,{triangle.begin(),triangle.end()});
        const float plane=dot(face.points[0],face.normal),denominator=dot(direction,face.normal);
        for(const auto& value:polygon)
            if(mul(sub(plane,dot(value,face.normal)),denominator)>0.f)output.push_back({value,normalized});
    }
    else if(kind==1)
    {
        unsigned faceAxis=0;for(unsigned i=1;i<3;++i)if(std::abs(direction[i])>std::abs(direction[faceAxis]))faceAxis=i;
        const Face face=boxFace(center,radius,faceAxis,direction[faceAxis]>0.f?1.f:-1.f);
        const std::vector<Point> polygon=clipFace({triangle.begin(),triangle.end()},direction,face.points);
        const float plane=dot(triangle[0],normalized),denominator=dot(direction,normalized);
        for(const auto& value:polygon)
        {
            const float distance=sub(plane,dot(value,normalized));
            if(mul(distance,denominator)<0.f)
                output.push_back({plus(value,scale(direction,div(distance,denominator))),normalized});
        }
    }
    else
    {
        unsigned faceAxis=0;for(unsigned i=1;i<3;++i)
            if(mul(direction[i],direction[i])>mul(direction[faceAxis],direction[faceAxis]))faceAxis=i;
        const Face face=boxFace(center,radius,faceAxis,direction[faceAxis]>0.f?1.f:-1.f);
        auto support=[](const std::vector<Point>& vertices,const Point& n)
        {
            unsigned first=0,second=1;float best=-std::numeric_limits<float>::max(),next=best;
            for(unsigned i=0;i<vertices.size();++i)
            {
                const float value=dot(vertices[i],n);
                if(value>best){next=best;second=first;best=value;first=i;}
                else if(value>next){next=value;second=i;}
            }
            return std::array<Point,2>{{vertices[first],vertices[second]}};
        };
        const auto boxEdge=support(face.points,direction);
        const auto triangleEdge=support({triangle.begin(),triangle.end()},scale(direction,-1.f));
        const Point u=minus(boxEdge[1],boxEdge[0]),v=minus(triangleEdge[1],triangleEdge[0]);
        const Point w=minus(boxEdge[0],triangleEdge[0]);
        const float A=dot(u,u),B=dot(u,v),C=dot(v,v),D=dot(u,w),E=dot(v,w);
        const float determinant=sub(mul(-C,A),mul(-B,B));
        if(std::abs(determinant)>=.00001f)
        {
            const float inverse=div(1.f,determinant);
            const float t=mul(sub(mul(-E,A),mul(B,-D)),inverse);
            output.push_back({plus(triangleEdge[0],scale(v,t)),normalized});
        }
    }
    return output;
}
}}

namespace wot { namespace GroundDecalMath
{
struct SurfaceContact {Point point,normal;unsigned flags;};
struct Projection {Point axis;float distance,depth;bool object;};
template<class Normalize>
inline bool project(const std::vector<SurfaceContact>& contacts,const Point& start,
    const Point& direction,Projection& result,Normalize normalize)
{
    if(contacts.empty())return false;
    Point normals={{0,0,0}},mean={{0,0,0}};
    float minimum=100000000.f,maximum=-1.f;bool object=false;
    const float denominator=dot(direction,direction);
    for(const auto& contact:contacts)
    {
        normals=plus(normals,contact.normal);
        const Point offset=scale(direction,div(dot(direction,minus(contact.point,start)),denominator));
        mean=plus(mean,offset);
        const float squared=dot(offset,offset);
        minimum=minimum<squared?minimum:squared;
        maximum=maximum>squared?maximum:squared;
        object|=(contact.flags&~8u)!=0;
    }
    mean=scale(mean,div(1.f,static_cast<float>(contacts.size())));
    result.axis=scale(normalize(normals),-1.f);
    result.distance=std::sqrt(dot(mean,mean));
    const float far=sub(std::sqrt(maximum),result.distance);
    const float near=sub(result.distance,std::sqrt(minimum));
    const float range=far>near?far:near;
    const float candidate=add(mul(std::abs(dot(result.axis,direction)),range),.4f);
    result.depth=candidate>=.4f?candidate:.4f;
    result.object=object;
    return true;
}
}}
