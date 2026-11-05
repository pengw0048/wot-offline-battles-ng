// The existing native gunner SHA1/MT19937 law, shared by room capabilities.
#include "room_strategy_data.hpp"
#include <charconv>
#include <sstream>
namespace wot::room::strategy {
namespace {
std::uint32_t rotl(std::uint32_t x,unsigned n) {return (x<<n)|(x>>(32-n));}
std::uint32_t seed_sha1(const std::string& value) {
    std::vector<std::uint8_t> bytes(value.begin(),value.end());
    const std::uint64_t bits=bytes.size()*8ull;
    bytes.push_back(0x80);while (bytes.size()%64!=56) bytes.push_back(0);
    for (int i=7;i>=0;--i) bytes.push_back(static_cast<std::uint8_t>(bits>>(8*i)));
    std::uint32_t h0=0x67452301u,h1=0xefcdab89u,h2=0x98badcfeu,h3=0x10325476u,h4=0xc3d2e1f0u;
    for (std::size_t offset=0;offset<bytes.size();offset+=64) {
        std::uint32_t w[80]{};
        for (int i=0;i<16;++i) for (int j=0;j<4;++j) w[i]=(w[i]<<8)|bytes[offset+4*i+j];
        for (int i=16;i<80;++i) w[i]=rotl(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
        std::uint32_t a=h0,b=h1,c=h2,d=h3,e=h4;
        for (int i=0;i<80;++i) {
            std::uint32_t f=0,k=0;
            if (i<20) {f=(b&c)|((~b)&d);k=0x5a827999u;}
            else if (i<40) {f=b^c^d;k=0x6ed9eba1u;}
            else if (i<60) {f=(b&c)|(b&d)|(c&d);k=0x8f1bbcdcu;}
            else {f=b^c^d;k=0xca62c1d6u;}
            const auto tmp=rotl(a,5)+f+e+k+w[i];e=d;d=c;c=rotl(b,30);b=a;a=tmp;
        }
        h0+=a;h1+=b;h2+=c;h3+=d;h4+=e;
    }
    return h0&0x7fffffffu;
}
class PythonRandom {
    std::uint32_t state_[624]{};int index_=624;
    std::uint32_t next() {
        if (index_>=624) {
            for (int i=0;i<624;++i) {
                const auto y=(state_[i]&0x80000000u)|(state_[(i+1)%624]&0x7fffffffu);
                state_[i]=state_[(i+397)%624]^(y>>1)^((y&1)?0x9908b0dfu:0u);
            }
            index_=0;
        }
        auto y=state_[index_++];y^=y>>11;y^=(y<<7)&0x9d2c5680u;
        y^=(y<<15)&0xefc60000u;y^=y>>18;return y;
    }
public:
    explicit PythonRandom(std::uint32_t key) {
        state_[0]=19650218u;
        for (int i=1;i<624;++i) state_[i]=1812433253u*(state_[i-1]^(state_[i-1]>>30))+i;
        int i=1;
        for (int k=624;k>0;--k) {
            state_[i]=(state_[i]^((state_[i-1]^(state_[i-1]>>30))*1664525u))+key;
            if (++i>=624) {state_[0]=state_[623];i=1;}
        }
        for (int k=623;k>0;--k) {
            state_[i]=(state_[i]^((state_[i-1]^(state_[i-1]>>30))*1566083941u))-i;
            if (++i>=624) {state_[0]=state_[623];i=1;}
        }
        state_[0]=0x80000000u;
    }
    double random() {const auto a=next()>>5,b=next()>>6;return (a*67108864.0+b)*(1.0/9007199254740992.0);}
};

std::string repr(const Value& raw,bool nested=false) {
    const auto& v=raw.resolved();
    if(null(v))return "None";
    if(v.kind==Value::Kind::Boolean)return v.boolean?"True":"False";
    if(v.kind==Value::Kind::Integer)return std::to_string(v.integer);
    if(v.kind==Value::Kind::Number){char data[64];auto result=std::to_chars(data,data+64,v.number);std::string out(data,result.ptr);if(out.find_first_of(".eE")==std::string::npos)out+=".0";return out;}
    if(v.kind==Value::Kind::Text){if(!nested)return v.text;std::string out="'";for(char c:v.text){if(c=='\\'||c=='\'')out+='\\';out+=c;}return out+"'";}
    if(list(v)){std::string out="(";for(std::size_t i=0;i<v.array.size();++i){if(i)out+=", ";out+=repr(v.array[i],true);}if(v.array.size()==1)out+=",";return out+")";}
    throw std::invalid_argument("Unsupported room capability occasion");
}
}
bool capability(double rating,const std::string& name,const std::vector<Value>& occasion) {
    rating=std::isfinite(rating)?std::clamp(rating,0.,1.):.34;
    const double onset=name=="target_retaliation"?.17:name=="target_human"?.505:0.;
    const double probability=std::clamp((rating-onset)/(1.-onset),0.,1.);
    if(probability<=0.)return false;if(probability>=1.)return true;
    std::string seed="bot-capability-v1|"+name;
    for(const auto& part:occasion)seed+="|"+repr(part);
    return PythonRandom(seed_sha1(seed)).random()<probability;
}
}
namespace wot::room {
bool Planner::capable(const Value& bot,const std::string& name,std::vector<Value> occasion) const {
    if(name=="hull_angling"||name=="flank")occasion.clear();
    occasion.insert(occasion.begin(),integer(strategy::id(bot)));occasion.insert(occasion.begin(),integer(state_.round_id));
    return strategy::capability(strategy::n(bot,"rating",.34),name,occasion);
}
}
