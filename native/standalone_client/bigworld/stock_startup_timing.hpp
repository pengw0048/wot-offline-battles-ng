#pragma once

// Startup happens before App::initLoggers. Buffer bounded stage summaries and
// publish them once that logger exists; the steady-state path records nothing.
#include "cstdmf/cstdmf_windows.hpp"
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace wot_startup {
struct Record {
    std::string stage;
    unsigned calls = 0;
    double wall_ms = 0, cpu_ms = 0, worst_ms = 0;
};
struct State {
    std::atomic<bool> enabled{true};
    std::mutex mutex;
    std::vector<Record> records;
};
inline State& state() { static State value; return value; }
inline double file_time_ms(const FILETIME& value) {
    ULARGE_INTEGER number;
    number.LowPart = value.dwLowDateTime; number.HighPart = value.dwHighDateTime;
    return double(number.QuadPart) / 10000.;
}
inline double process_cpu_ms() {
    FILETIME creation, exit, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) return 0;
    // Process CPU includes background work during this inclusive stage.
    return file_time_ms(kernel) + file_time_ms(user);
}
inline std::string& task_path() { static thread_local std::string value; return value; }
class Scope {
public:
    explicit Scope(const char* label, bool aggregate = false)
        : active_(state().enabled.load(std::memory_order_relaxed)), aggregate_(aggregate) {
        if (!active_) return;
        if (aggregate_) label_ = label;
        else {
            previous_ = task_path();
            label_ = previous_.empty() ? label : previous_ + "/" + label;
            task_path() = label_;
        }
        cpu_ = process_cpu_ms();
        start_ = std::chrono::steady_clock::now();
    }
    ~Scope() {
        if (!active_) return;
        const double wall = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start_).count();
        const double cpu = process_cpu_ms() - cpu_;
        if (!aggregate_) task_path() = previous_;
        auto& owner = state();
        std::lock_guard<std::mutex> lock(owner.mutex);
        if (!owner.enabled.load(std::memory_order_relaxed)) return;
        for (auto& record : owner.records) if (record.stage == label_) {
            ++record.calls; record.wall_ms += wall; record.cpu_ms += cpu;
            if (wall > record.worst_ms) record.worst_ms = wall;
            return;
        }
        owner.records.push_back({label_, 1, wall, cpu, wall});
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    bool active_, aggregate_;
    std::string label_, previous_;
    double cpu_ = 0;
    std::chrono::steady_clock::time_point start_;
};
inline std::vector<Record> finish() {
    auto& owner = state();
    std::lock_guard<std::mutex> lock(owner.mutex);
    owner.enabled.store(false, std::memory_order_relaxed);
    std::vector<Record> result;
    result.swap(owner.records);
    if (result.empty()) return result;
    FILETIME creation, exit, kernel, user, now;
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
        GetSystemTimeAsFileTime(&now);
        const double wall = file_time_ms(now) - file_time_ms(creation);
        result.push_back({"process.logger_ready", 1, wall,
            file_time_ms(kernel) + file_time_ms(user), wall});
    }
    return result;
}
} // namespace wot_startup
