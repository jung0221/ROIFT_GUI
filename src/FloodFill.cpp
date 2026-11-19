#include "FloodFill.h"

namespace flood_fill
{

Outcome region(const std::vector<int> &labels, const float *image, const std::array<unsigned, 3> &dims,
               std::size_t seed, double lo, double hi, int planeAxis, std::size_t maxVoxels,
               std::vector<std::size_t> &voxels)
{
    voxels.clear();
    const std::size_t total = std::size_t(dims[0]) * dims[1] * dims[2];
    if (!image || labels.size() != total || seed >= total)
        return Outcome::Invalid;
    const int label = labels[seed];
    const auto accepts = [&](std::size_t i)
    { return labels[i] == label && image[i] >= lo && image[i] <= hi; };
    if (!accepts(seed))
        return Outcome::Filled; // the seed itself is outside the window: an empty region

    const std::array<long long, 3> n{dims[0], dims[1], dims[2]};
    const std::array<long long, 3> stride{1, n[0], n[0] * n[1]};
    std::vector<bool> seen(total, false);
    std::vector<std::size_t> stack{seed};
    seen[seed] = true;
    while (!stack.empty())
    {
        const std::size_t i = stack.back();
        stack.pop_back();
        voxels.push_back(i);
        if (voxels.size() > maxVoxels)
        {
            voxels.clear();
            return Outcome::TooLarge;
        }
        const std::array<long long, 3> p{static_cast<long long>(i) % n[0], (static_cast<long long>(i) / n[0]) % n[1],
                                         static_cast<long long>(i) / (n[0] * n[1])};
        for (int axis = 0; axis < 3; ++axis)
        {
            if (axis == planeAxis)
                continue;
            for (const int step : {-1, 1})
            {
                const long long q = p[size_t(axis)] + step;
                if (q < 0 || q >= n[size_t(axis)])
                    continue;
                const std::size_t j = static_cast<std::size_t>(static_cast<long long>(i) + step * stride[size_t(axis)]);
                if (!seen[j] && accepts(j))
                {
                    seen[j] = true;
                    stack.push_back(j);
                }
            }
        }
    }
    return Outcome::Filled;
}

} // namespace flood_fill
