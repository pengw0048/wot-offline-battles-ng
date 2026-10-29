#pragma once
#include "stock_native_client_state.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <stdexcept>

namespace stock_native_client_weapon_runtime {
// The contract and quantity/trigger edges retain the stock cold parser. Only
// relative clocks vary in an otherwise identical accepted equipment ledger.
struct EquipmentClock {
    double ready_at=0.;
    std::optional<double> automatic,ai;
};
struct EquipmentFrame {
    stock_native_client_state::Value signature;
    std::vector<EquipmentClock> clocks;
};
inline double equipmentNumber(const stock_native_client_state::Value&raw) {
    using V=stock_native_client_state::Value;const auto&v=raw.resolved();double n=-1.;
    if(v.kind==V::Kind::Integer||v.kind==V::Kind::Number)n=v.numeric(-1.);
    else if(v.kind==V::Kind::Text){const char*begin=v.text.c_str();const char*decimal=begin;while(*decimal&&std::isspace(static_cast<unsigned char>(*decimal)))++decimal;if(*decimal=='+'||*decimal=='-')++decimal;if(decimal[0]=='0'&&(decimal[1]=='x'||decimal[1]=='X'))return -1.;char*end=nullptr;n=std::strtod(begin,&end);if(end==begin)n=-1.;else {while(*end&&std::isspace(static_cast<unsigned char>(*end)))++end;if(*end)n=-1.;}}
    return std::isfinite(n)?n:-1.;
}
inline EquipmentFrame equipmentFrame(const stock_native_client_state::Value&raw,double now) {
    using V=stock_native_client_state::Value;const auto&rows=raw.resolved();
    if(rows.kind!=V::Kind::Array)throw std::invalid_argument("canonical player equipment snapshot is invalid");
    EquipmentFrame out;out.signature=rows;now=std::max(0.,std::isfinite(now)?now:0.);
    for(std::size_t i=0;i<rows.array.size();++i){const auto&row=rows.array[i].resolved();
        if(row.kind!=V::Kind::Object||row.object.size()!=6)throw std::invalid_argument("equipment wire snapshot is incomplete");
        for(const char*n:{"equipment","usesLeft","cooldownTimeLeft","active","autoPendingElapsed","aiPendingElapsed"})if(!row.object.count(n))throw std::invalid_argument("equipment wire snapshot is incomplete");
        const auto&contract=row.get("equipment");const double cooldown=equipmentNumber(row.get("cooldownTimeLeft"));
        if(cooldown<0.||cooldown>std::max(0.,equipmentNumber(contract.get("cooldownSeconds")))+.000001)throw std::invalid_argument("equipment wire cooldown is invalid");
        EquipmentClock clock;clock.ready_at=now+cooldown;unsigned index=0;
        for(const char*n:{"autoPendingElapsed","aiPendingElapsed"}){const auto&value=row.get(n).resolved();std::optional<double> elapsed;
            if(value.kind!=V::Kind::Null){const double amount=equipmentNumber(value);if(amount<0.||amount>3600.)throw std::invalid_argument("equipment wire pending clock is invalid");elapsed=amount;}
            if(index++==0){if(elapsed&&!contract.get("autoactivate").truth())throw std::invalid_argument("equipment wire auto clock is invalid");clock.automatic=elapsed?std::optional<double>(now-*elapsed):std::nullopt;}
            else {const auto&kind=contract.get("kind").resolved().text;if(elapsed&&kind!="repairkit"&&kind!="medkit")throw std::invalid_argument("equipment wire AI clock is invalid");clock.ai=elapsed?std::optional<double>(now-*elapsed):std::nullopt;}
            if(elapsed&&equipmentNumber(row.get("usesLeft"))==0.)throw std::invalid_argument("exhausted equipment has a pending activation");
        }
        out.clocks.push_back(clock);auto&semantic=out.signature.array[i];semantic=row;
        for(const char*n:{"cooldownTimeLeft","autoPendingElapsed","aiPendingElapsed"})semantic.object.erase(n);
    }
    return out;
}
struct EquipmentGate {
    bool valid=false;std::int64_t revision=0;
    stock_native_client_state::Value signature,result;
    bool matches(const EquipmentFrame&f,std::int64_t r,const stock_native_client_state::Value&v)const{return valid&&revision==r&&signature==f.signature&&result==v;}
    void commit(const EquipmentFrame&f,std::int64_t r,stock_native_client_state::Value v){signature=f.signature;revision=r;result=std::move(v);valid=true;}
};
}
