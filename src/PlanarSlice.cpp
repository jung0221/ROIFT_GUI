#include "PlanarSlice.h"

namespace planar
{

bool makeGeometry(Plane plane, int index, const std::array<int, 3> &dims, Geometry *out)
{
    Geometry g;
    g.plane = plane;
    g.index = index;
    g.dims = dims;
    switch (plane)
    {
    case Plane::Axial:    g.uAxis = 0; g.vAxis = 1; g.normalAxis = 2; break;
    case Plane::Sagittal: g.uAxis = 1; g.vAxis = 2; g.normalAxis = 0; break;
    case Plane::Coronal:  g.uAxis = 0; g.vAxis = 2; g.normalAxis = 1; break;
    }
    if (dims[0] <= 0 || dims[1] <= 0 || dims[2] <= 0 || index < 0 || index >= dims[g.normalAxis])
        return false;
    g.width = dims[g.uAxis];
    g.height = dims[g.vAxis];
    *out = g;
    return true;
}

std::size_t volumeIndex(const Geometry &g, int u, int v)
{
    std::array<int, 3> p{};
    p[g.uAxis] = u;
    p[g.vAxis] = v;
    p[g.normalAxis] = g.index;
    return std::size_t(p[0]) + std::size_t(g.dims[0]) * (std::size_t(p[1]) + std::size_t(g.dims[1]) * std::size_t(p[2]));
}

bool toPlane(const Geometry &g, const std::array<int, 3> &voxel, int *u, int *v)
{
    if (voxel[g.normalAxis] != g.index)
        return false;
    const int pu = voxel[g.uAxis];
    const int pv = voxel[g.vAxis];
    if (pu < 0 || pu >= g.width || pv < 0 || pv >= g.height)
        return false;
    *u = pu;
    *v = pv;
    return true;
}

std::array<double, 2> planeSpacing(const Geometry &g, const std::array<double, 3> &spacing)
{
    return {spacing[g.uAxis], spacing[g.vAxis]};
}

static std::size_t voxelCount(const Geometry &g)
{
    return std::size_t(g.dims[0]) * std::size_t(g.dims[1]) * std::size_t(g.dims[2]);
}

std::vector<float> extractPlane(const Geometry &g, const float *volume, std::size_t volumeSize)
{
    if (volume == nullptr || volumeSize != voxelCount(g))
        return {};
    std::vector<float> plane(std::size_t(g.width) * std::size_t(g.height));
    for (int v = 0; v < g.height; ++v)
        for (int u = 0; u < g.width; ++u)
            plane[std::size_t(v) * g.width + u] = volume[volumeIndex(g, u, v)];
    return plane;
}

std::vector<std::pair<int, int>> borderPixels(const Geometry &g)
{
    std::vector<std::pair<int, int>> pixels;
    if (g.width <= 0 || g.height <= 0)
        return pixels;
    for (int u = 0; u < g.width; ++u)
        pixels.emplace_back(u, 0);
    for (int v = 1; v < g.height; ++v)
    {
        pixels.emplace_back(0, v);
        if (g.width > 1)
            pixels.emplace_back(g.width - 1, v);
    }
    if (g.height > 1)
        for (int u = 1; u < g.width - 1; ++u)
            pixels.emplace_back(u, g.height - 1);
    return pixels;
}

bool pastePlaneLabels(const Geometry &g, const std::vector<int> &planeLabels,
                      const std::set<int> &runLabels, std::vector<int> &volumeLabels)
{
    if (planeLabels.size() != std::size_t(g.width) * std::size_t(g.height) ||
        volumeLabels.size() != voxelCount(g))
        return false;
    for (int v = 0; v < g.height; ++v)
        for (int u = 0; u < g.width; ++u)
        {
            const int result = planeLabels[std::size_t(v) * g.width + u];
            int &current = volumeLabels[volumeIndex(g, u, v)];
            if (result > 0)
                current = result;
            else if (runLabels.count(current))
                current = 0;
        }
    return true;
}

} // namespace planar
