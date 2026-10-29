#pragma once
#include "stock_native_projectile_damage.hpp"
#include <memory>
struct _object;
namespace stock_native_projectile {
// The standard _random.Random.random C entry shares CPython's existing MT
// owner. Gaussian mathematics is native; its spare lives in the same raw
// instance dictionary used by Python random.gauss. No Python function runs.
class Random final:public stock_native_projectile_damage::Random {
    struct State;
    std::unique_ptr<State> state_;
public:
    Random();
    ~Random();
    Random(const Random&)=delete;Random&operator=(const Random&)=delete;
    bool install(_object* instance);
    double uniform()override;
    double gauss(double mean,double sigma)override;
};
}
