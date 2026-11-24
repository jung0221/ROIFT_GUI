// The 3D view's surface builder, free of the window: which labels it finds, where the surface
// lies, that a label cut by the array edge is still closed, the per-vertex labels of the union
// path past kPerLabelLimit, label values past a byte and past 16 bits, and cancellation.
#include "MaskSurface.h"

#include <vtkDataArray.h>
#include <vtkFeatureEdges.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <set>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-70s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

constexpr unsigned kX = 24, kY = 20, kZ = 16;

mask_surface::Input phantom()
{
    mask_surface::Input input;
    input.dims = {kX, kY, kZ};
    input.spacing = {0.5, 0.5, 2.0};
    input.labels.assign(std::size_t(kX) * kY * kZ, 0);
    return input;
}

void box(mask_surface::Input &input, int label, unsigned x0, unsigned x1, unsigned y0, unsigned y1, unsigned z0,
         unsigned z1)
{
    for (unsigned z = z0; z <= z1; ++z)
        for (unsigned y = y0; y <= y1; ++y)
            for (unsigned x = x0; x <= x1; ++x)
                input.labels[(std::size_t(z) * kY + y) * kX + x] = label;
}

std::set<int> vertexLabels(vtkPolyData *poly)
{
    std::set<int> found;
    vtkDataArray *scalars = poly ? poly->GetPointData()->GetScalars() : nullptr;
    if (!scalars)
        return found;
    for (vtkIdType i = 0; i < scalars->GetNumberOfTuples(); ++i)
        found.insert(static_cast<int>(scalars->GetTuple1(i)));
    return found;
}

vtkIdType openEdges(vtkPolyData *poly)
{
    auto edges = vtkSmartPointer<vtkFeatureEdges>::New();
    edges->SetInputData(poly);
    edges->BoundaryEdgesOn();
    edges->NonManifoldEdgesOff();
    edges->FeatureEdgesOff();
    edges->ManifoldEdgesOff();
    edges->Update();
    return edges->GetOutput()->GetNumberOfCells();
}

} // namespace

int main()
{
    {
        mask_surface::Input input = phantom();
        box(input, 1, 4, 7, 4, 9, 3, 6);
        box(input, 2, 8, 11, 4, 9, 3, 6); // touches label 1
        input.labels[0] = -3;             // negative is background
        const mask_surface::Surface s = mask_surface::build(input);
        check(s.labels == std::vector<int>({1, 2}), "two touching boxes: labels 1 and 2");
        check(s.poly && s.poly->GetNumberOfCells() > 0 && s.locator, "a surface and its locator");
        check(vertexLabels(s.poly) == std::set<int>({1, 2}), "each contour carries its own label");
        double b[6];
        s.poly->GetBounds(b);
        // Voxels 4..11 in x (0.5 mm) and 3..6 in z (2 mm): the walls lie half a voxel out, at
        // 1.75..5.75 and 5..13 mm; smoothing moves them under a tenth of a voxel. A quarter
        // voxel of slack still catches a surface shifted by the padding collar's voxel.
        const bool inX = std::abs(b[0] - 1.75) < 0.125 && std::abs(b[1] - 5.75) < 0.125;
        const bool inZ = std::abs(b[4] - 5.0) < 0.5 && std::abs(b[5] - 13.0) < 0.5;
        check(inX && inZ, "the surface lies at index * spacing");
    }
    {
        mask_surface::Input input = phantom();
        box(input, 5, 0, 5, 0, 5, 0, 5); // runs off three faces of the array
        const mask_surface::Surface s = mask_surface::build(input);
        check(s.poly && openEdges(s.poly) == 0, "a label cut by the array edge is closed");
    }
    {
        mask_surface::Input input = phantom();
        const int count = int(mask_surface::kPerLabelLimit) + 8;
        for (int i = 0; i < count; ++i)
        {
            const unsigned x = 1 + 2 * unsigned(i % 10);
            const unsigned z = 1 + 3 * unsigned(i / 10);
            box(input, 100 + i, x, x, 2, 4, z, z + 1); // separate columns
        }
        const mask_surface::Surface s = mask_surface::build(input);
        std::set<int> expected;
        for (int i = 0; i < count; ++i)
            expected.insert(100 + i);
        check(s.labels.size() == std::size_t(count), "past the per-label limit every label is found");
        check(vertexLabels(s.poly) == expected, "and every vertex of the union takes its label");
    }
    {
        mask_surface::Input input = phantom();
        box(input, 300, 2, 6, 2, 6, 2, 6);
        box(input, 70000, 10, 14, 2, 6, 2, 6);
        const mask_surface::Surface s = mask_surface::build(input);
        check(s.labels == std::vector<int>({300, 70000}), "labels past 255 and past 65535 are kept");
        check(vertexLabels(s.poly) == std::set<int>({300, 70000}), "and contoured at their own values");
    }
    {
        const mask_surface::Surface s = mask_surface::build(phantom());
        check(s.labels.empty() && !s.poly, "an empty mask has no surface");
        mask_surface::Input wrong = phantom();
        wrong.labels.pop_back();
        check(!mask_surface::build(wrong).poly, "a buffer that does not fit dims has none");
    }
    {
        mask_surface::Input input = phantom();
        box(input, 1, 4, 7, 4, 9, 3, 6);
        const std::atomic<bool> cancel{true};
        const mask_surface::Surface s = mask_surface::build(input, &cancel);
        check(s.cancelled && !s.poly, "a cancelled build returns no surface");
    }
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
