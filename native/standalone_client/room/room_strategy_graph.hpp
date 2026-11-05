#pragma once
#include "room_strategy_data.hpp"
#include <queue>

namespace wot::room::strategy {
// The already-validated _Graph view, over one cold resource. Edges remain
// directed; nearest-free sampling would cross walls and is intentionally absent.
struct GraphData {
    int width=0,height=0;
    double cell=0.;
    std::array<double,2> origin{};
    std::array<double,4> bounds{};
    std::vector<std::int32_t> heights_mm;
    std::vector<std::uint8_t> links,hazards;
    std::array<std::array<int,2>,8> directions{};
    static constexpr std::int32_t missing=std::numeric_limits<std::int32_t>::min();
    static std::shared_ptr<const GraphData> fromValue(const Value& raw) {
        auto out=std::make_shared<GraphData>();out->width=int(intField(raw,"width"));out->height=int(intField(raw,"height"));out->cell=n(raw,"cell_size");
        const auto size=std::int64_t(out->width)*out->height;
        if(s(raw,"game_version")!="0.9.22.0.1-cn-1513"||out->width<=0||out->height<=0||out->width>4096||out->height>4096||size>1000000||out->cell<=0.||out->cell>32.||rows(raw.get("origin")).size()!=2||rows(raw.get("bounds")).size()!=4||rows(raw.get("heights_mm")).size()!=std::size_t(size)||rows(raw.get("links")).size()!=std::size_t(size)||rows(raw.get("hazards")).size()!=std::size_t(size)||rows(raw.get("directions")).size()!=8)
            throw std::invalid_argument("Invalid cold strategy navigation graph");
        for(int i=0;i<2;++i)out->origin[i]=rows(raw.get("origin"))[i].numeric();for(int i=0;i<4;++i)out->bounds[i]=rows(raw.get("bounds"))[i].numeric();
        const auto count=static_cast<std::size_t>(size); // Validated positive and <= 1,000,000 above, including on x86.
        out->heights_mm.reserve(count);out->links.reserve(count);out->hazards.reserve(count);
        for(std::size_t i=0;i<count;++i){const auto& h=rows(raw.get("heights_mm"))[i];out->heights_mm.push_back(null(h)?missing:std::int32_t(h.numeric()));out->links.push_back(std::uint8_t(rows(raw.get("links"))[i].numeric()));out->hazards.push_back(std::uint8_t(rows(raw.get("hazards"))[i].numeric()));}
        std::set<std::array<int,2>> unique;
        for(int i=0;i<8;++i){const auto& d=rows(rows(raw.get("directions"))[i]);if(d.size()!=2)throw std::invalid_argument("Invalid cold strategy graph direction");out->directions[i]={int(d[0].numeric()),int(d[1].numeric())};const auto& v=out->directions[i];if((v[0]==0&&v[1]==0)||std::max(std::abs(v[0]),std::abs(v[1]))!=1||!unique.insert(v).second)throw std::invalid_argument("Invalid cold strategy graph direction");}
        return out;
    }
};
class Graph {
    std::shared_ptr<const GraphData> owner_;
    const GraphData& data_;
    int width_,height_;
public:
    explicit Graph(std::shared_ptr<const GraphData> data):owner_(std::move(data)),data_(*owner_),width_(data_.width),height_(data_.height) {}
    explicit Graph(const Value& raw):Graph(GraphData::fromValue(raw)) {}
    Value location(int index) const {
        return record({{"x",number(data_.origin[0]+(index%width_)*data_.cell)},
            {"y",number(data_.heights_mm[index]/1000.)},{"z",number(data_.origin[1]+(index/width_)*data_.cell)}});
    }
    bool usable(int index) const {
        if(index<0||index>=width_*height_||data_.heights_mm[index]==GraphData::missing||(data_.hazards[index]&15))return false;
        const auto p=location(index);return n(p,"x")>=data_.bounds[0]&&n(p,"x")<=data_.bounds[2]&&n(p,"z")>=data_.bounds[1]&&n(p,"z")<=data_.bounds[3];
    }
    std::optional<int> closest(const Value& p) const {
        if(n(p,"x")<data_.bounds[0]||n(p,"x")>data_.bounds[2]||n(p,"z")<data_.bounds[1]||n(p,"z")>data_.bounds[3])return {};
        const int col=int(std::nearbyint((n(p,"x")-data_.origin[0])/data_.cell)),row=int(std::nearbyint((n(p,"z")-data_.origin[1])/data_.cell));
        if(col<0||col>=width_||row<0||row>=height_)return {};const int index=row*width_+col;return usable(index)?std::optional<int>(index):std::nullopt;
    }
    std::optional<int> reachableJoin(const Value& pose,const Value& points) const {
        auto start=closest(pose);if(!start)return {};
        std::map<int,std::vector<int>> goals;int index=0;for(const auto& p:rows(points)){auto c=closest(point(p));if(c)goals[*c].push_back(index);++index;}
        if(goals.empty())return {};
        auto heuristic=[&](int cell){double best=std::numeric_limits<double>::infinity();const auto p=location(cell);for(const auto& g:goals)best=std::min(best,distance(p,location(g.first)));return best;};
        using Item=std::tuple<double,double,int>;std::priority_queue<Item,std::vector<Item>,std::greater<Item>> todo;
        todo.emplace(heuristic(*start),0.,*start);std::map<int,double> costs{{*start,0.}};
        while(!todo.empty()) {
            const auto [unused,cost,cell]=todo.top();todo.pop();if(cost!=costs[cell])continue;
            auto goal=goals.find(cell);if(goal!=goals.end())return *std::max_element(goal->second.begin(),goal->second.end());
            const auto a=location(cell);unsigned bit=0;
            for(const auto& d:data_.directions) {
                const bool connected=(static_cast<unsigned>(data_.links[cell])&(1u<<bit++))!=0u;if(!connected)continue;
                const int x=cell%width_+d[0],z=cell/width_+d[1];
                if(x<0||x>=width_||z<0||z>=height_)continue;const int next=z*width_+x;if(!usable(next))continue;
                const auto b=location(next);const double horizontal=distance(a,b),dy=n(a,"y")-n(b,"y"),value=cost+std::sqrt(horizontal*horizontal+dy*dy);
                auto prior=costs.find(next);if(prior==costs.end()||value<prior->second){costs[next]=value;todo.emplace(value+heuristic(next),value,next);}
            }
        }
        return {};
    }
};
}
