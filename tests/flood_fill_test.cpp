// Pins flood_fill: the face-connected region of the seed's label within an intensity window,
// in 3D or on one slice, stopped by another label, and refused past the size limit.
#include "FloodFill.h"

#include <cstdio>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-62s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

constexpr unsigned kX = 20, kY = 20, kZ = 12;

std::size_t at(unsigned x, unsigned y, unsigned z)
{
    return std::size_t(x) + std::size_t(y) * kX + std::size_t(z) * kX * kY;
}

} // namespace

int main()
{
    const std::array<unsigned, 3> dims{kX, kY, kZ};
    std::vector<float> image(std::size_t(kX) * kY * kZ, 0.0f);
    for (unsigned z = 5; z < 10; ++z)
        for (unsigned y = 5; y < 10; ++y)
            for (unsigned x = 5; x < 10; ++x)
                image[at(x, y, z)] = 100.0f; // a bright 5 x 5 x 5 cube
    image[at(10, 7, 7)] = 104.0f;             // touching it, inside a tolerance of 5
    std::vector<int> labels(image.size(), 0);
    std::vector<std::size_t> region;

    check(flood_fill::region(labels, image.data(), dims, at(7, 7, 7), 95, 105, -1, 1000, region) ==
                  flood_fill::Outcome::Filled &&
              region.size() == 126,
          "3D: the cube and the touching voxel within the window");
    check(flood_fill::region(labels, image.data(), dims, at(7, 7, 7), 98, 102, -1, 1000, region) ==
                  flood_fill::Outcome::Filled &&
              region.size() == 125,
          "a narrower window leaves the 104 voxel out");
    check(flood_fill::region(labels, image.data(), dims, at(7, 7, 7), 95, 105, 2, 1000, region) ==
                  flood_fill::Outcome::Filled &&
              region.size() == 26,
          "on the axial slice: 25 of the cube and the touching voxel");

    for (unsigned z = 0; z < kZ; ++z)
        for (unsigned y = 0; y < kY; ++y)
            labels[at(7, y, z)] = 1; // a wall of another label through the cube
    flood_fill::region(labels, image.data(), dims, at(6, 7, 7), 95, 105, -1, 1000, region);
    check(region.size() == 50, "another label stops it: only x 5 and 6 of the cube");
    flood_fill::region(labels, image.data(), dims, at(7, 7, 7), 95, 105, -1, 1000, region);
    check(region.size() == 25, "seeded on the wall, it fills the wall's part in the window");

    check(flood_fill::region(labels, image.data(), dims, at(6, 7, 7), 95, 105, -1, 10, region) ==
                  flood_fill::Outcome::TooLarge &&
              region.empty(),
          "past the limit it stops and returns nothing");
    check(flood_fill::region(labels, image.data(), dims, image.size(), 0, 1, -1, 10, region) ==
              flood_fill::Outcome::Invalid,
          "a seed outside the volume is refused");

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
