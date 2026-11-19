#pragma once

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

// Fill between slices, free of the window: a label drawn on a few slices along one axis is
// carried through the empty slices between them by blending the two outlines' signed
// distance maps (shape-based interpolation), in millimetres so anisotropic voxels interpolate
// the patient, not the grid.
namespace slice_fill
{

// Signed distance in mm from each pixel centre of a w x h plane, u fastest: negative inside
// (to the nearest outside pixel), positive outside (to the nearest inside pixel). A plane with
// no outside pixel is a large negative constant inside, and one with no inside pixel large and
// positive.
std::vector<float> signedDistance2D(const std::vector<unsigned char> &inside, int width, int height, double su,
                                    double sv);

struct Gap
{
    int from = 0; // a slice holding the label
    int to = 0;   // the next one along the axis; every slice strictly between is empty of it
};

struct Result
{
    std::vector<Gap> gaps;              // only gaps of at least one empty slice
    std::vector<std::size_t> voxels;    // voxels to give the label, none holding it already
    int filledSlices = 0;
};

// For each run of empty slices of `label` between two slices that hold it, along `normalAxis`
// (0 = x, 1 = y, 2 = z) of an x-fastest volume of `dims` with `spacing` in mm.
Result fillBetweenSlices(const std::vector<int> &labels, const std::array<unsigned, 3> &dims,
                         const std::array<double, 3> &spacing, int label, int normalAxis);

} // namespace slice_fill
