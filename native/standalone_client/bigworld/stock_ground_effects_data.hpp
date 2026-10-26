#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

namespace wot
{
struct StockGroundEffectMath
{
    using Vector=std::array<float,3>;
    struct Impact { Vector position;float delay;int kind; };
    template<class Random> static std::vector<Impact> airstrike(const Vector& origin,const Vector& direction,
        float speed,float width,float length,int across,int along,float variation,float stepOffset,Random random)
    {
        if (across<=0||along<=0||!std::isfinite(speed)||speed<=0.f||
            !std::isfinite(width)||!std::isfinite(length)||!std::isfinite(variation)||!std::isfinite(stepOffset))
            throw std::invalid_argument("Invalid airstrike geometry");
        for (float value:origin) if (!std::isfinite(value)) throw std::invalid_argument("Non-finite airstrike origin");
        for (float value:direction) if (!std::isfinite(value)) throw std::invalid_argument("Non-finite airstrike direction");
        const std::uint64_t count=std::uint64_t(across)*std::uint64_t(along);
        if (count>0x15555555u) throw std::length_error("Original airstrike point vector exceeds its native extent");
        const float dx=across>1?width/float(across-1):width;
        const float dz=along>1?length/float(along-1):length;
        std::vector<Vector> points,batch;
        points.reserve(static_cast<std::size_t>(count));
        for (int row=0;row<along;++row)
        {
            const float rowX=origin[0]+float(row)*(direction[0]*dz);
            const float rowY=direction[1]*dz*float(row)+origin[1];
            const float rowZ=direction[2]*dz*float(row)+origin[2];
            for (int column=0;column<across;++column)
            {
                const float offset=float(column)-float(across-1)*.5f;
                const float x=offset*(dx*direction[2])+rowX;
                const float z=rowZ-direction[0]*dx*offset;
                const float extentX=stepOffset*dx,minimumX=-extentX;
                const float px=float(random())*(extentX-minimumX)*3.0518509447574615e-05f+minimumX+x;
                const float extentZ=stepOffset*dz,minimumZ=-extentZ;
                const float pz=float(random())*(extentZ-minimumZ)*3.0518509447574615e-05f+minimumZ+z;
                batch.push_back({{px,rowY,pz}});
            }
            if ((row&1)||row==along-1)
            {
                // 8b18f0 creates a new MT19937(5489) for each pair of rows.
                // MSVC's ascending shuffle uses this exact bounded rejection;
                // std::shuffle would use a different plan on other hosts.
                std::mt19937 generator(5489u);
                for (std::uint32_t n=2;n<=batch.size();++n)
                {
                    const std::uint32_t maximum=0xffffffffu;
                    std::uint32_t value;
                    do { value=generator(); }
                    while (maximum%n!=n-1 && maximum/n<=value/n);
                    const std::uint32_t selected=value%n;
                    if (selected!=n-1) std::swap(batch[n-1],batch[selected]);
                }
                points.insert(points.end(),batch.begin(),batch.end());batch.clear();
            }
        }
        const float duration=length/speed;
        const float interval=duration/float(points.size());
        float delay=0.f;
        std::vector<Impact> result;
        result.reserve(points.size()+1);
        for (const auto& point:points)
        {
            const int kind=float(random())*3.0518509447574615e-05f<=variation?1:0;
            result.push_back({point,delay,kind});delay=delay+interval;
        }
        const Vector centre={{origin[0]+length*direction[0]*.5f,origin[1]+direction[1]*length*.5f,
            origin[2]+direction[2]*length*.5f}};
        result.push_back({centre,duration*.5f,2});
        return result;
    }
};
}
