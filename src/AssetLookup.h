#pragma once

#include <string>
#include <cstdint>

struct AssetLookup {
    std::string path;
    std::string type;
    std::string symbol;
    uint32_t offset = 0;
    uint32_t count = 0;
};
