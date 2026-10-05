#pragma once

#include <array>
#include <cstddef>
#include <set>
#include <utility>
#include <vector>

namespace planar
{

// One plane of an x-fastest volume, oriented as the slice views draw it.
enum class Plane
{
    Axial,    // z = index; u = x, v = y
    Sagittal, // x = index; u = y, v = z
    Coronal   // y = index; u = x, v = z
};

struct Geometry
{
    Plane plane = Plane::Axial;
    int index = 0;
    std::array<int, 3> dims{0, 0, 0};
    int uAxis = 0, vAxis = 1, normalAxis = 2;
    int width = 0, height = 0;
};

bool makeGeometry(Plane plane, int index, const std::array<int, 3> &dims, Geometry *out);
std::size_t volumeIndex(const Geometry &g, int u, int v);
bool toPlane(const Geometry &g, const std::array<int, 3> &voxel, int *u, int *v);
std::array<double, 2> planeSpacing(const Geometry &g, const std::array<double, 3> &spacing);
// u-fastest: pixel (u, v) at v * width + u, which is also x-fastest in a (U, V, 1) file.
// Empty unless volume is non-null and volumeSize is the voxel count of g.dims.
std::vector<float> extractPlane(const Geometry &g, const float *volume, std::size_t volumeSize);
// Every pixel of the four edges, once.
std::vector<std::pair<int, int>> borderPixels(const Geometry &g);
// A pixel takes the result where it is positive; elsewhere, a pixel holding one of
// runLabels is cleared and any other label is kept. Returns false, writing nothing, when
// either buffer does not match g.
bool pastePlaneLabels(const Geometry &g, const std::vector<int> &planeLabels,
                      const std::set<int> &runLabels, std::vector<int> &volumeLabels);

} // namespace planar
