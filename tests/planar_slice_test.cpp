// Checks the plane mapping the slice runner relies on. It must match what the slice
// views draw (NiftiImage::get*SliceAsRGB), or seeds and results land on other pixels.
#include "PlanarSlice.h"

#include <cstddef>
#include <cstdio>
#include <numeric>
#include <set>
#include <vector>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-62s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

} // namespace

int main()
{
    using namespace planar;
    const std::array<int, 3> dims{5, 7, 9};
    std::vector<float> volume(5 * 7 * 9);
    std::iota(volume.begin(), volume.end(), 0.0f); // value == linear index, x fastest

    Geometry axial, coronal, sagittal, unused;
    check(makeGeometry(Plane::Axial, 3, dims, &axial), "axial z=3 accepted");
    check(makeGeometry(Plane::Coronal, 6, dims, &coronal), "coronal y=6 accepted");
    check(makeGeometry(Plane::Sagittal, 4, dims, &sagittal), "sagittal x=4 accepted");
    check(!makeGeometry(Plane::Axial, 9, dims, &unused), "axial z=9 refused (sizeZ=9)");
    check(!makeGeometry(Plane::Sagittal, -1, dims, &unused), "negative index refused");

    check(axial.width == 5 && axial.height == 7, "axial is sizeX x sizeY");
    check(coronal.width == 5 && coronal.height == 9, "coronal is sizeX x sizeZ");
    check(sagittal.width == 7 && sagittal.height == 9, "sagittal is sizeY x sizeZ");

    const auto a = extractPlane(axial, volume.data(), volume.size());
    const auto c = extractPlane(coronal, volume.data(), volume.size());
    const auto s = extractPlane(sagittal, volume.data(), volume.size());
    check(a[4 * 5 + 2] == 2 + 5 * (4 + 7 * 3), "axial (u=2,v=4) is voxel (2,4,3)");
    check(c[8 * 5 + 1] == 1 + 5 * (6 + 7 * 8), "coronal (u=1,v=8) is voxel (1,6,8)");
    check(s[0 * 7 + 6] == 4 + 5 * (6 + 7 * 0), "sagittal (u=6,v=0) is voxel (4,6,0)");

    int u = -1, v = -1;
    check(toPlane(sagittal, {4, 6, 0}, &u, &v) && u == 6 && v == 0, "voxel (4,6,0) -> sagittal (6,0)");
    check(!toPlane(sagittal, {3, 6, 0}, &u, &v), "voxel off the plane is refused");
    check(toPlane(coronal, {1, 6, 8}, &u, &v) && u == 1 && v == 8, "voxel (1,6,8) -> coronal (1,8)");

    const auto border = borderPixels(axial);
    std::set<std::pair<int, int>> unique(border.begin(), border.end());
    bool onEdge = true;
    for (const auto &p : border)
        onEdge = onEdge && (p.first == 0 || p.first == 4 || p.second == 0 || p.second == 6);
    check(border.size() == 2 * 5 + 2 * 7 - 4 && unique.size() == border.size(),
          "border: every edge pixel once (20 on 5 x 7)");
    check(onEdge, "border: nothing inside");

    // Paste policy: the run owns the labels it seeded.
    std::vector<int> labels(volume.size(), 0);
    const std::size_t onA = volumeIndex(axial, 0, 0), onB = volumeIndex(axial, 1, 0),
                      onC = volumeIndex(axial, 2, 0), onD = volumeIndex(axial, 3, 0),
                      off = 0 + 5 * (0 + 7 * 4);
    labels[onB] = 1; labels[onC] = 3; labels[onD] = 3; labels[off] = 1;
    std::vector<int> result(std::size_t(axial.width) * axial.height, 0);
    result[0] = 1; // pixel A
    result[3] = 1; // pixel D: the run claims a pixel holding another label
    check(pastePlaneLabels(axial, result, std::set<int>{1}, labels), "paste accepted for matching buffers");
    check(labels[onA] == 1, "result label written");
    check(labels[onB] == 0, "stale run label cleared");
    check(labels[onC] == 3, "other label kept where the result is 0");
    check(labels[onD] == 1, "other label overwritten where the result claims it");
    check(labels[off] == 1, "other slice untouched");

    // Refusals and edge cases.
    const std::array<double, 3> spacing{0.7, 0.8, 2.5};
    check(planeSpacing(axial, spacing) == std::array<double, 2>{0.7, 0.8}, "axial spacing (0.7, 0.8)");
    check(planeSpacing(coronal, spacing) == std::array<double, 2>{0.7, 2.5}, "coronal spacing (0.7, 2.5)");
    check(planeSpacing(sagittal, spacing) == std::array<double, 2>{0.8, 2.5}, "sagittal spacing (0.8, 2.5)");
    check(!makeGeometry(Plane::Axial, 0, {5, 0, 9}, &unused), "dims with a zero refused");

    u = -1;
    v = -1;
    check(!toPlane(sagittal, {4, 7, 0}, &u, &v) && u == -1 && v == -1,
          "voxel out of bounds on the plane refused, u/v untouched");

    Geometry thin;
    check(makeGeometry(Plane::Coronal, 1, {5, 3, 1}, &thin) && thin.width == 5 && thin.height == 1,
          "coronal y=1 of 5 x 3 x 1 is 5 x 1");
    const auto thinBorder = borderPixels(thin);
    std::set<std::pair<int, int>> thinUnique(thinBorder.begin(), thinBorder.end());
    check(thinBorder.size() == 5 && thinUnique.size() == 5, "border of a 1-pixel-tall plane: 5 pixels once");

    std::vector<int> before(volume.size(), 2);
    std::vector<int> after = before;
    const std::vector<int> goodPlane(std::size_t(axial.width) * axial.height, 1);
    check(!pastePlaneLabels(axial, std::vector<int>(3, 1), std::set<int>{1}, after) && after == before,
          "paste refused for a wrong plane size, volume unchanged");
    std::vector<int> shortVolume(volume.size() - 1, 2);
    const std::vector<int> shortBefore = shortVolume;
    check(!pastePlaneLabels(axial, goodPlane, std::set<int>{1}, shortVolume) && shortVolume == shortBefore,
          "paste refused for a wrong volume size, volume unchanged");

    check(extractPlane(axial, volume.data(), volume.size() - 1).empty(), "extract refused for a wrong volume size");
    check(extractPlane(axial, nullptr, volume.size()).empty(), "extract refused for a null volume");

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
