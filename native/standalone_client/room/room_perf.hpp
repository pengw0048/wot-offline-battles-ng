#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <time.h>
#endif

namespace wot::room {
// Fixed storage, owned by the measured thread. Sampling never takes a lock.
struct Timing {
    using Clock = std::chrono::steady_clock;
    std::array<double, 1024> samples{};
    std::uint64_t count = 0;
    double total = 0., maximum = 0.;
    void add(double milliseconds) {
        samples[static_cast<std::size_t>(count++ % samples.size())] = milliseconds;
        total += milliseconds; maximum = std::max(maximum, milliseconds);
    }
    void elapsed(Clock::time_point start) {
        add(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    }
    std::string report(const char* name) const {
        auto sorted = samples; const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(count, samples.size()));
        std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(n));
        std::ostringstream out; out << std::fixed << std::setprecision(3) << name << "_n=" << count;
        if (n) out << " " << name << "_mean_ms=" << total / double(count)
                   << " " << name << "_p95_ms=" << sorted[(n - 1) * 95 / 100]
                   << " " << name << "_p99_ms=" << sorted[(n - 1) * 99 / 100]
                   << " " << name << "_max_ms=" << maximum;
        return out.str();
    }
};
inline std::string threadIdentity() {
#if defined(_WIN32)
    return std::to_string(GetCurrentThreadId());
#else
    std::ostringstream out; out << std::this_thread::get_id(); return out.str();
#endif
}
inline double threadCpuMilliseconds() {
#if defined(_WIN32)
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return 0.;
    const auto ticks = [](FILETIME value) { return (std::uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime; };
    return double(ticks(kernel) + ticks(user)) / 10000.;
#else
    timespec value{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value)) return 0.;
    return double(value.tv_sec) * 1000. + double(value.tv_nsec) / 1000000.;
#endif
}
}
