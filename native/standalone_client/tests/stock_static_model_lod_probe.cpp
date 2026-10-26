#include "wot/stock_static_model_binary.hpp"
#include <fstream>

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    std::ifstream input(argv[1], std::ios::binary);
    std::ofstream output(argv[2], std::ios::binary);
    std::uint32_t mode;
    using Binary = wot::StockStaticModelBinary;
    while (input.read(reinterpret_cast<char*>(&mode), 4))
    {
        Binary::Bounds bounds;
        input.read(reinterpret_cast<char*>(&bounds), sizeof(bounds));
        float result;
        if (mode == 0)
        {
            float squared;
            input.read(reinterpret_cast<char*>(&squared), 4);
            result = Binary::lodThreshold(bounds, squared);
        }
        else if (mode == 1)
        {
            Binary::Matrix world, view;
            float inputs[3];
            input.read(reinterpret_cast<char*>(world.data()), sizeof(world));
            input.read(reinterpret_cast<char*>(view.data()), sizeof(view));
            input.read(reinterpret_cast<char*>(inputs), sizeof(inputs));
            result = Binary::lodMetric(bounds, world, view, inputs[0], inputs[1], inputs[2]);
        }
        else return 2;
        if (!input) return 2;
        output.write(reinterpret_cast<const char*>(&result), 4);
    }
    return input.eof() && output ? 0 : 2;
}
