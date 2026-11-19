#include "LabelOps.h"

#include <algorithm>

namespace label_ops
{
namespace
{

// Flood from seed over voxels holding `label`, marking them in visited; calls visit(index)
// once per voxel of the piece.
template <typename Visit>
void flood(const std::vector<int> &labels, const std::array<unsigned, 3> &dims, std::size_t seed, int label,
           std::vector<bool> &visited, Visit visit)
{
    const long long nx = dims[0];
    const long long ny = dims[1];
    const long long nz = dims[2];
    const long long plane = nx * ny;
    std::vector<std::size_t> stack{seed};
    visited[seed] = true;
    while (!stack.empty())
    {
        const std::size_t index = stack.back();
        stack.pop_back();
        visit(index);
        const long long x = static_cast<long long>(index) % nx;
        const long long y = (static_cast<long long>(index) / nx) % ny;
        const long long z = static_cast<long long>(index) / plane;
        for (long long dz = -1; dz <= 1; ++dz)
        {
            const long long zz = z + dz;
            if (zz < 0 || zz >= nz)
                continue;
            for (long long dy = -1; dy <= 1; ++dy)
            {
                const long long yy = y + dy;
                if (yy < 0 || yy >= ny)
                    continue;
                for (long long dx = -1; dx <= 1; ++dx)
                {
                    const long long xx = x + dx;
                    if (xx < 0 || xx >= nx)
                        continue;
                    const std::size_t next = static_cast<std::size_t>(xx + yy * nx + zz * plane);
                    if (!visited[next] && labels[next] == label)
                    {
                        visited[next] = true;
                        stack.push_back(next);
                    }
                }
            }
        }
    }
}

bool fits(const std::vector<int> &labels, const std::array<unsigned, 3> &dims)
{
    return labels.size() == std::size_t(dims[0]) * dims[1] * dims[2] && !labels.empty();
}

} // namespace

std::vector<Piece> pieces(const std::vector<int> &labels, const std::array<unsigned, 3> &dims, int label)
{
    std::vector<Piece> found;
    if (!fits(labels, dims))
        return found;
    std::vector<bool> visited(labels.size(), false);
    for (std::size_t i = 0; i < labels.size(); ++i)
    {
        if (labels[i] != label || visited[i])
            continue;
        Piece piece;
        piece.seed = i;
        flood(labels, dims, i, label, visited, [&piece](std::size_t)
              { ++piece.voxels; });
        found.push_back(piece);
    }
    std::sort(found.begin(), found.end(), [](const Piece &a, const Piece &b)
              { return a.voxels != b.voxels ? a.voxels > b.voxels : a.seed < b.seed; });
    return found;
}

std::vector<std::size_t> pieceAt(const std::vector<int> &labels, const std::array<unsigned, 3> &dims,
                                 std::size_t seed)
{
    std::vector<std::size_t> voxels;
    if (!fits(labels, dims) || seed >= labels.size())
        return voxels;
    std::vector<bool> visited(labels.size(), false);
    flood(labels, dims, seed, labels[seed], visited, [&voxels](std::size_t index)
          { voxels.push_back(index); });
    return voxels;
}

} // namespace label_ops
