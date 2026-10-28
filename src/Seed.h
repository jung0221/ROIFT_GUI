#pragma once

#include <string>

struct Seed
{
    int x, y, z, label, internal;
    bool fromFile = false;
    std::string note; // text after the numbers on its seed file line, shown on hover
};
