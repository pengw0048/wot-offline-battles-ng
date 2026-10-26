// Compare the original x86 instruction block with the production classifier.
// The sole original import is resolved to the actual installed UCRT SSE cosf.
#include "stock_space_decals_data.hpp"
#include <windows.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <random>
#include <vector>

#if !defined(_MSC_VER) || !defined(_M_IX86)
#error This oracle requires the supported MSVC Win32 target.
#endif

__declspec(naked) unsigned __cdecl originalDecalCamera(void*, const void*, void*)
{
    __asm
    {
        push ebp
        push ebx
        push esi
        push edi
        mov eax, [esp + 20]
        mov edi, [esp + 24]
        mov ebp, [esp + 28]
        mov esi, 1
        call eax
        mov eax, esi
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}

int main(int argc, char** argv)
{
    static_assert(sizeof(void*) == 4, "The original instruction oracle is Win32");
    assert(argc == 2);
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<unsigned char> block((std::istreambuf_iterator<char>(input)), {});
    assert(block.size() == 0xa9123c - 0xa91199);
    assert(block[0x76] == 0xe8);
    const unsigned originalHalf = 0x014ae8e4;
    assert(std::memcmp(block.data() + 0x5f, &originalHalf, 4) == 0);
    HMODULE crt = LoadLibraryW(L"ucrtbase.dll");
    assert(crt);
    FARPROC cosine = GetProcAddress(crt, "__libm_sse2_cosf");
    assert(cosine);
    auto* code = static_cast<unsigned char*>(VirtualAlloc(NULL, block.size() + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    assert(code);
    std::memcpy(code, block.data(), block.size());
    code[block.size()] = 0xc3;
    static const float half = .5f;
    const void* halfAddress = &half;
    std::memcpy(code + 0x5f, &halfAddress, 4);
    const unsigned relative = static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(cosine) -
        reinterpret_cast<std::uintptr_t>(code + 0x7b));
    std::memcpy(code + 0x77, &relative, 4);
    DWORD protection;
    assert(VirtualProtect(code, block.size() + 1, PAGE_EXECUTE_READ, &protection));
    assert(FlushInstructionCache(GetCurrentProcess(), code, block.size() + 1));
    unsigned cases = 0;
    auto compare = [&](const float (&scale)[3], const float (&position)[3], const float (&camera)[3], float nearPlane, float fov)
    {
        unsigned char record[124] = {};
        alignas(16) unsigned char stack[256] = {};
        void* frame = stack + 128;
        float nativeCamera[] = {nearPlane, 1000.f, fov, 1.7777778f, 100.f, 0.f};
        const void* cameraAddress = nativeCamera;
        std::memcpy(record + 48, position, 12); std::memcpy(record + 60, scale, 12);
        std::memcpy(stack + 64, camera, 12); std::memcpy(stack + 136, &cameraAddress, 4);
        const unsigned original = originalDecalCamera(code, record, frame);
        const bool actual = wot::stockSpaceDecalCameraOutside(scale, position, camera, nearPlane, fov);
        if (actual != (original == 0))
        {
            std::fprintf(stderr, "Camera classification mismatch at case %u: original %u, actual %u, near %.9g, FOV %.9g\n",
                cases, original, unsigned(actual), nearPlane, fov);
            std::abort();
        }
        ++cases;
    };
    std::mt19937 random(1513);
    std::uniform_real_distribution<float> coordinate(-500.f, 500.f), size(0.f, 100.f), angle(.1f, 3.f);
    for (unsigned index = 0; index < 4096; ++index)
    {
        const float scale[] = {size(random), size(random), size(random)};
        const float position[] = {coordinate(random), coordinate(random), coordinate(random)};
        const float camera[] = {coordinate(random), coordinate(random), coordinate(random)};
        compare(scale, position, camera, 1.f + size(random) * .01f, angle(random));
    }
    for (float fov : {.1f, .5f, .7853982f, 1.0471976f, 1.5f, 1.57f, 1.6f, 2.f, 2.8f})
    for (float nearPlane : {.01f, .1f, 1.f, 5.f})
    for (float sizeValue : {0.f, .1f, 1.f, 10.f, 100.f})
    {
        const float scale[] = {sizeValue, sizeValue, sizeValue};
        const float camera[] = {0.f, 0.f, 0.f};
        const float edge = std::sqrt(3.f * sizeValue * sizeValue) * .5f + nearPlane / std::cos(fov);
        float position[] = {edge, 0.f, 0.f};
        compare(scale, position, camera, nearPlane, fov);
        for (unsigned direction = 0; direction != 2; ++direction)
        {
            position[0] = edge;
            for (unsigned step = 0; step != 3; ++step)
            {
                position[0] = std::nextafter(position[0], direction ? std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::infinity());
                compare(scale, position, camera, nearPlane, fov);
            }
        }
    }
    const float invalid[] = {std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f};
    const float zero[] = {0.f, 0.f, 0.f};
    compare(invalid, zero, zero, .1f, 1.f); compare(zero, invalid, zero, .1f, 1.f);
    assert(VirtualFree(code, 0, MEM_RELEASE));
    FreeLibrary(crt);
    std::printf("Actual original a91199..a91239 + installed UCRT SSE cosf: %u camera/edge/NaN classifications EXACT MATCH\n", cases);
    return 0;
}
