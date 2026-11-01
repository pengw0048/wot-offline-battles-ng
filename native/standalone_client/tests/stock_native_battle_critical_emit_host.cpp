#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace p {
struct Ref { explicit Ref(void* = nullptr) {} void* get() const { return nullptr; } };
Ref integer(std::int64_t) { return Ref{}; }
}
void* Py_None = nullptr;
void* Py_True = reinterpret_cast<void*>(1);
void* Py_False = reinterpret_cast<void*>(2);
void* Py_BuildValue(const char*, double) { return nullptr; }
namespace py {
p::Ref optional(void*, const char*) { return p::Ref{}; }
bool callable(const p::Ref&) { return false; }
template<class... Args> void invoke(Args...) {}
template<class... Args> void call(Args...) {}
}
struct Battle {
    struct { p::Ref avatar; } engine;
    bool serviceable = false, accepted = false, throws = false;
    int calls = 0, warnings = 0;
    template<class... Args> void warn(Args...) { ++warnings; }
};
namespace vehicles {
bool damageInfoServiceable(Battle& b) { return b.serviceable; }
bool showDamageInfo(Battle& b, std::int64_t, int, int, std::int64_t) {
    ++b.calls;
    if (b.throws) throw std::runtime_error("stock repair callback unavailable");
    return b.accepted;
}
}
struct LocalCriticalOwner {
    struct Actor { std::int64_t engine_id = 17; } storage;
    Actor* actor = &storage;
    p::Ref entity;
    struct Edge { std::string kind; bool left = false; int status = 0, code = 0, index = 0, extra = 0; double seconds = 0.; };
    std::map<std::string, Edge> pending;
    std::vector<std::string> order;
    bool retired = false;
};
bool active(Battle&, LocalCriticalOwner& o) { return !o.retired; }
std::string describe(const std::exception& e) { return e.what(); }

@EMIT@

void check(bool condition) { if (!condition) throw std::runtime_error("repair edge was consumed before stock accepted it"); }
void queue(LocalCriticalOwner& o) {
    o.pending["left"] = {"repaired", true, 0, 0, 5, 9};
    o.order.push_back("left");
}
int main() {
    try {
    Battle b;
    LocalCriticalOwner o;
    queue(o);
    emit(b, o);
    check(o.pending.size() == 1 && o.order.size() == 1 && b.calls == 0);
    b.serviceable = true;
    emit(b, o);
    check(o.pending.size() == 1 && o.order.size() == 1 && b.calls == 1);
    b.throws = true;
    emit(b, o);
    check(o.pending.size() == 1 && o.order.size() == 1 && b.warnings == 1);
    b.throws = false;
    b.accepted = true;
    emit(b, o);
    check(o.pending.empty() && o.order.empty() && b.calls == 3);
    emit(b, o);
    check(b.calls == 3);
    queue(o);
    o.retired = true;
    emit(b, o);
    check(o.pending.size() == 1 && b.calls == 3);
    return 0;
    } catch (const std::exception&) {
        return 1;
    }
}
