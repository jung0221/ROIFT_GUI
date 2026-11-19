#pragma once

#include <array>
#include <cstddef>
#include <vector>

// Flood fill, free of the window: the voxels reachable from a seed through face neighbours
// that hold the seed's label and whose image intensity lies in [lo, hi].
namespace flood_fill
{

enum class Outcome
{
    Filled,   // `voxels` is the region
    TooLarge, // the region passed maxVoxels; `voxels` is empty
    Invalid   // the buffers do not fit dims, or the seed is outside
};

// planeAxis < 0 spreads in 3D; 0, 1 or 2 keeps the region on the seed's slice across that axis.
Outcome region(const std::vector<int> &labels, const float *image, const std::array<unsigned, 3> &dims,
               std::size_t seed, double lo, double hi, int planeAxis, std::size_t maxVoxels,
               std::vector<std::size_t> &voxels);

} // namespace flood_fill
