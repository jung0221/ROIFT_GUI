// The 3D view's surface builder, free of the window: which labels it finds, one closed shell per
// label wound outward, a wall two labels share in both their shells, a label enclosed by another
// with a shell of its own, label values past a byte and past 16 bits, and cancellation.
#include "MaskSurface.h"

#include <vtkCellArray.h>
#include <vtkFeatureEdges.h>
#include <vtkIdList.h>
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

const mask_surface::Shell *shellOf(const mask_surface::Surface &s, int label)
{
    for (const mask_surface::Shell &shell : s.shells)
        if (shell.label == label)
            return &shell;
    return nullptr;
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

// Positive when the triangles are wound so their normals point out.
double signedVolume(vtkPolyData *poly)
{
    double volume = 0.0;
    auto ids = vtkSmartPointer<vtkIdList>::New();
    for (vtkIdType c = 0; c < poly->GetNumberOfCells(); ++c)
    {
        poly->GetPolys()->GetCellAtId(c, ids);
        double a[3], b[3], d[3];
        poly->GetPoint(ids->GetId(0), a);
        poly->GetPoint(ids->GetId(1), b);
        poly->GetPoint(ids->GetId(2), d);
        volume += (a[0] * (b[1] * d[2] - b[2] * d[1]) - a[1] * (b[0] * d[2] - b[2] * d[0]) +
                   a[2] * (b[0] * d[1] - b[1] * d[0])) /
                  6.0;
    }
    return volume;
}

// Closed, wound outward, with normals and a locator.
bool wellFormed(const mask_surface::Shell *shell)
{
    return shell && shell->poly && shell->poly->GetNumberOfCells() > 0 && shell->locator &&
           shell->poly->GetPointData()->GetNormals() && openEdges(shell->poly) == 0 &&
           signedVolume(shell->poly) > 0.0;
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
        const mask_surface::Shell *one = shellOf(s, 1), *two = shellOf(s, 2);
        check(s.shells.size() == 2 && wellFormed(one) && wellFormed(two),
              "one closed shell each, wound outward, with normals and a locator");
        if (one && two)
        {
            double a[6], b[6];
            one->poly->GetBounds(a);
            two->poly->GetBounds(b);
            // Voxels 4..11 in x (0.5 mm), 4..9 in y and 3..6 in z (2 mm): centred on 3.75, 3.25 and
            // 9 mm, walls half a voxel out at 1.75..5.75, 1.5..5 and 5..13 mm. Smoothing rounds a box
            // this small by up to half a voxel at its extremes; the padding collar's voxel would
            // move the centre by a whole one.
            const double centre[3] = {0.5 * (a[0] + b[1]), 0.5 * (a[2] + a[3]), 0.5 * (a[4] + a[5])};
            const bool centred = std::abs(centre[0] - 3.75) < 0.05 && std::abs(centre[1] - 3.25) < 0.05 &&
                                 std::abs(centre[2] - 9.0) < 0.05;
            const bool extent = std::abs(a[0] - 1.75) <= 0.25 && std::abs(b[1] - 5.75) <= 0.25 &&
                                std::abs(a[4] - 5.0) <= 1.0 && std::abs(a[5] - 13.0) <= 1.0;
            check(centred && extent, "the surface lies at index * spacing");
            check(std::abs(a[1] - 3.75) < 0.125 && std::abs(b[0] - 3.75) < 0.125,
                  "the wall they share, at x = 3.75 mm, is in both shells");
        }
    }
    {
        mask_surface::Input input = phantom();
        box(input, 1, 2, 17, 2, 15, 2, 11);
        box(input, 2, 7, 11, 7, 10, 5, 8); // wholly inside label 1
        const mask_surface::Surface s = mask_surface::build(input);
        const mask_surface::Shell *inner = shellOf(s, 2);
        check(wellFormed(inner), "a label enclosed by another has a closed shell of its own");
        check(wellFormed(shellOf(s, 1)), "and the one around it stays closed");
    }
    {
        mask_surface::Input input = phantom();
        box(input, 5, 0, 5, 0, 5, 0, 5); // runs off three faces of the array
        const mask_surface::Surface s = mask_surface::build(input);
        check(wellFormed(shellOf(s, 5)), "a label cut by the array edge is closed");
    }
    {
        mask_surface::Input input = phantom();
        const int count = 40;
        for (int i = 0; i < count; ++i)
        {
            const unsigned x = 1 + 2 * unsigned(i % 10);
            const unsigned z = 1 + 3 * unsigned(i / 10);
            box(input, 100 + i, x, x, 2, 4, z, z + 1); // separate columns
        }
        const mask_surface::Surface s = mask_surface::build(input);
        bool all = s.labels.size() == std::size_t(count) && s.shells.size() == std::size_t(count);
        for (int i = 0; all && i < count; ++i)
            all = wellFormed(shellOf(s, 100 + i));
        check(all, "forty labels, forty closed shells");
    }
    {
        mask_surface::Input input = phantom();
        box(input, 300, 2, 6, 2, 6, 2, 6);
        box(input, 70000, 10, 14, 2, 6, 2, 6);
        const mask_surface::Surface s = mask_surface::build(input);
        check(s.labels == std::vector<int>({300, 70000}), "labels past 255 and past 65535 are kept");
        check(wellFormed(shellOf(s, 300)) && wellFormed(shellOf(s, 70000)), "and each gets its shell");
    }
    {
        const mask_surface::Surface s = mask_surface::build(phantom());
        check(s.labels.empty() && s.shells.empty(), "an empty mask has no surface");
        mask_surface::Input wrong = phantom();
        wrong.labels.pop_back();
        check(mask_surface::build(wrong).shells.empty(), "a buffer that does not fit dims has none");
    }
    {
        mask_surface::Input input = phantom();
        box(input, 1, 4, 7, 4, 9, 3, 6);
        const std::atomic<bool> cancel{true};
        const mask_surface::Surface s = mask_surface::build(input, &cancel);
        check(s.cancelled && s.shells.empty(), "a cancelled build returns no surface");
    }
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
