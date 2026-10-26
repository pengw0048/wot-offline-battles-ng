#include <windows.h>
#include <d3dx9.h>
#include <vector>
#include <fstream>
#include <cstring>
namespace {
std::ofstream boundaries;
void record(unsigned kind, const void* first, unsigned firstSize, const void* second,
    unsigned secondSize, const void* result, unsigned outputSize)
{
    if (!boundaries.is_open()) return;
    const unsigned inputSize = firstSize + secondSize;
    boundaries.write(reinterpret_cast<const char*>(&kind), 4);
    boundaries.write(reinterpret_cast<const char*>(&inputSize), 4);
    boundaries.write(reinterpret_cast<const char*>(&outputSize), 4);
    boundaries.write(static_cast<const char*>(first), firstSize);
    if (secondSize) boundaries.write(static_cast<const char*>(second), secondSize);
    boundaries.write(static_cast<const char*>(result), outputSize);
}
D3DXVECTOR3* probeNormalise(D3DXVECTOR3* output, const D3DXVECTOR3* input)
{
    const D3DXVECTOR3 source = *input;
    auto* result = D3DXVec3Normalize(output, input);
    record(0, &source, 12, NULL, 0, output, 12);
    return result;
}
D3DXMATRIX* probeInverse(D3DXMATRIX* output, FLOAT* determinant, const D3DXMATRIX* input)
{
    const D3DXMATRIX source = *input;
    auto* result = D3DXMatrixInverse(output, determinant, input);
    record(1, &source, 64, NULL, 0, output, 64);
    return result;
}
D3DXVECTOR4* probeTransform(D3DXVECTOR4* output, const D3DXVECTOR3* input, const D3DXMATRIX* matrix)
{
    auto* result = D3DXVec3Transform(output, input, matrix);
    record(2, input, 12, matrix, 64, output, 16);
    return result;
}
}
#define D3DXVec3Normalize probeNormalise
#define D3DXMatrixInverse probeInverse
#define D3DXVec3Transform probeTransform
#pragma warning(push)
#pragma warning(disable:4458)
#include "stock_vehicle_stickers_geometry.hpp"
#pragma warning(pop)
#include <stdexcept>
#define REQUIRE(expression) do { if (!(expression)) throw std::runtime_error(#expression); } while (false)
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc == 4 && std::strcmp(argv[3], "intersect") == 0)
    {
        std::ifstream input(argv[1], std::ios::binary);
        float row[15];
        while (input.read(reinterpret_cast<char*>(row), sizeof(row)))
        {
            BW::Vector3 point(0,0,0);
            const bool hit = BW::StockStickerGeometry::intersect(
                BW::Vector3(row[0],row[1],row[2]), BW::Vector3(row[3],row[4],row[5]),
                BW::Vector3(row[6],row[7],row[8]), BW::Vector3(row[9],row[10],row[11]),
                BW::Vector3(row[12],row[13],row[14]), point);
            std::uint32_t words[3]; std::memcpy(words, &point, 12);
            std::cout << unsigned(hit) << ' ' << words[0] << ' ' << words[1] << ' ' << words[2] << '\n';
        }
        return input.eof() ? 0 : 2;
    }
    if (argc != 3) return 2;
    const auto volume = BW::StockStickerGeometry::box(BW::Vector3(0,0,1), BW::Vector3(0,0,-1),
        BW::Vector2(1,1), BW::Vector3(0,1,0));
    std::vector<BW::StockStickerGeometry::Vertex> projected;
    BW::WorldTriangle surface(BW::Vector3(-1,-1,0), BW::Vector3(1,-1,0), BW::Vector3(0,1,0));
    BW::StockStickerGeometry::clip(volume, {surface}, projected);
    REQUIRE(!projected.empty() && projected.size()%3 == 0);
    for (const auto& vertex : projected)
    {
        REQUIRE(vertex.pos_.x >= -.5001f && vertex.pos_.x <= .5001f);
        REQUIRE(vertex.pos_.y >= -.5001f && vertex.pos_.y <= .5001f);
        REQUIRE(std::fabs(vertex.pos_.z) < .0001f);
        REQUIRE(vertex.uv_.x >= 0.f && vertex.uv_.x <= 1.f && vertex.uv_.y >= 0.f && vertex.uv_.y <= 1.f);
        REQUIRE(vertex.normal_.z > .99f);
    }
    BW::StockStickerGeometry::finish(projected);
    for (const auto& vertex : projected) REQUIRE(std::fabs(vertex.pos_.z-.0025f) < .000001f);
    BW::StockStickerGeometry::clip(volume, {BW::WorldTriangle(BW::Vector3(10,10,0),
        BW::Vector3(11,10,0), BW::Vector3(10,11,0))}, projected);
    REQUIRE(projected.empty());
    std::ifstream input(argv[1], std::ios::binary);
    boundaries.open(std::string(argv[2]) + ".boundaries", std::ios::binary);
    float row[11];
    while (input.read(reinterpret_cast<char*>(row), sizeof(row)))
    {
        const auto bounds = BW::StockStickerGeometry::box(BW::Vector3(row[0], row[1], row[2]),
            BW::Vector3(row[3], row[4], row[5]), BW::Vector2(row[6], row[7]), BW::Vector3(row[8], row[9], row[10]));
        std::uint32_t words[80];
        std::memcpy(words, bounds.world, 96); std::memcpy(words + 24, bounds.local, 96);
        std::memcpy(words + 48, &bounds.toWorld, 64); std::memcpy(words + 64, &bounds.toLocal, 64);
        for (auto word : words) std::cout << word << ' ';
        std::cout << '\n';
    }
    const auto dll = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"D3DX9_43.dll"));
    if (!dll) return 3;
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(dll);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(dll + dos->e_lfanew);
    std::ofstream table(argv[2], std::ios::binary);
    for (std::uintptr_t offset = 0x1ca000; offset < 0x1ca240; offset += 4)
    {
        auto value = *reinterpret_cast<const std::uintptr_t*>(dll + offset);
        if (value >= dll && value < dll + nt->OptionalHeader.SizeOfImage) value = value - dll + 0x10000000;
        table.write(reinterpret_cast<const char*>(&value), 4);
    }
}
