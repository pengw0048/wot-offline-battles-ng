#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace stock_native_bot {
struct BurstDeadline { bool active=false; double remaining=0.; };
struct Slice {
    double elapsed=0.,now=0.,evaluation_time=0.;
    bool refresh=false,publish=false;
    std::int64_t start_us=0,end_us=0;
};
// Callback debt and physical time have separate owners. Burst deadlines are
// reread after each completed slice because that slice can arm or cancel one.
class Clock {
    bool open_=false,refresh_=false;
    double callback_now_=0.;
public:
    double period=.1,bank=0.,refresh_bank=0.,next_publication=0.;
    std::int64_t sample_us=0;
    unsigned callback_steps=0;
    double callback_max_step=0.;
    static double bounded(double,const std::vector<BurstDeadline>&);
    double available(const std::vector<BurstDeadline>&)const;
    bool begin(double elapsed,double now,const std::vector<BurstDeadline>&);
    std::optional<Slice> next(const std::vector<BurstDeadline>&);
    void complete(const Slice&);
    void finish();
};
}
