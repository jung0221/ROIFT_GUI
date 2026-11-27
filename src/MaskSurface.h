#pragma once

#include <array>
#include <atomic>
#include <vector>

#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkStaticCellLocator.h>

// The 3D view's mask surfaces, free of the window and of the renderer so that they can be built on
// a worker thread: one closed, smoothed shell per label, each with a locator for picking.
namespace mask_surface
{

struct Input
{
    std::vector<int> labels; // X fastest, then Y, then Z
    std::array<unsigned, 3> dims{};
    std::array<double, 3> spacing{1.0, 1.0, 1.0};
};

// Every wall of one label, against background and against other labels, wound so that its normals
// point out of the label. A wall between two labels is in both their shells, so hiding one label
// leaves the other closed, and a label enclosed by another has a shell of its own.
struct Shell
{
    int label = 0;
    vtkSmartPointer<vtkPolyData> poly;             // triangles with point normals
    vtkSmartPointer<vtkStaticCellLocator> locator; // over poly
};

struct Surface
{
    std::vector<int> labels;   // the positive labels present, ascending
    std::vector<Shell> shells; // one per label, in the same order; empty without a label
    bool cancelled = false;
};

// Vertices are in index * spacing with origin 0, like the slices and the seeds. Labels at or
// below 0 are background. Once *cancel turns true it returns at the next stage, cancelled.
Surface build(const Input &input, const std::atomic<bool> *cancel = nullptr);

} // namespace mask_surface
