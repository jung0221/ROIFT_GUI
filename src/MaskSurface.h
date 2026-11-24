#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <vector>

#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkStaticCellLocator.h>

// The 3D view's mask surface, free of the window and of the renderer so that it can be built on
// a worker thread: contoured, smoothed, coloured per label, with a locator for picking.
namespace mask_surface
{

// Up to this many labels each one is contoured on its own, so touching labels keep their shared
// wall; past it the union is contoured once and each vertex takes the label of the voxel under
// it. Discrete flying edges passes over the volume once per label: 1153 labels took 25 s.
constexpr std::size_t kPerLabelLimit = 32;

struct Input
{
    std::vector<int> labels; // X fastest, then Y, then Z
    std::array<unsigned, 3> dims{};
    std::array<double, 3> spacing{1.0, 1.0, 1.0};
};

struct Surface
{
    std::vector<int> labels;                       // the positive labels present, ascending
    vtkSmartPointer<vtkPolyData> poly;             // null without a label, or when cancelled
    vtkSmartPointer<vtkStaticCellLocator> locator; // over poly
    bool cancelled = false;
};

// Vertices are in index * spacing with origin 0, like the slices and the seeds. Labels at or
// below 0 are background. Once *cancel turns true it returns at the next stage, cancelled.
Surface build(const Input &input, const std::atomic<bool> *cancel = nullptr);

} // namespace mask_surface
