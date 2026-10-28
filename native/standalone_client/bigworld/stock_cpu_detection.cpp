#include "pch.hpp"
#include "stock_cpu_detection.hpp"
#include "wot/stock_gpu_performancer.hpp"
#include <intrin.h>
#include <windows.h>

BW_BEGIN_NAMESPACE
namespace
{
unsigned cpuVendor()
{
    // 92c5e0(force=false) only caches the immutable vendor, never the frequency.
    static unsigned char cached=4;
    if(cached==4)
    {
        int registers[4];
        __cpuid(registers,0);
        const auto ebx=static_cast<unsigned>(registers[1]);
        const auto ecx=static_cast<unsigned>(registers[2]);
        const auto edx=static_cast<unsigned>(registers[3]);
        if(ebx==0x756e6547u&&ecx==0x6c65746eu&&edx==0x49656e69u)cached=1;
        else if(ebx==0x68747541u&&ecx==0x444d4163u&&edx==0x69746e65u)cached=2;
        else cached=3;
    }
    return cached;
}
}

int64 stockMeasureCpuFrequency()
{
    // 934c10 is deliberately raw RDTSC: no CPUID/RDTSCP fence or affinity change.
    const uint64 start=__rdtsc();
    Sleep(500);
    const uint64 elapsed=__rdtsc()-start;
    SYSTEM_INFO information;
    GetSystemInfo(&information);
    const unsigned processors=information.dwNumberOfProcessors;
    const unsigned vendor=processors<2?3:cpuVendor();
    return wot::stockCpuFrequencyScore(elapsed,processors,vendor);
}
BW_END_NAMESPACE
