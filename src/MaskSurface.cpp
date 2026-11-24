#include "MaskSurface.h"

#include <vtkCellData.h>
#include <vtkDiscreteFlyingEdges3D.h>
#include <vtkImageData.h>
#include <vtkIntArray.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSMPThreadLocal.h>
#include <vtkSMPTools.h>
#include <vtkStaticCellLocator.h>
#include <vtkWindowedSincPolyDataFilter.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>

namespace mask_surface
{
namespace
{

// Labels below this are recorded in a flat table per thread, larger ones in a set.
constexpr int kFlatLabels = 1 << 16;

bool cancelled(const std::atomic<bool> *cancel)
{
    return cancel && cancel->load(std::memory_order_relaxed);
}

std::vector<int> presentLabels(const Input &input)
{
    struct Seen
    {
        std::vector<unsigned char> flat;
        std::set<int> large;
    };
    vtkSMPThreadLocal<Seen> local;
    const std::size_t plane = std::size_t(input.dims[0]) * input.dims[1];
    vtkSMPTools::For(0, vtkIdType(input.dims[2]),
                     [&](vtkIdType z0, vtkIdType z1)
                     {
                         Seen &seen = local.Local();
                         if (seen.flat.empty())
                             seen.flat.assign(kFlatLabels, 0);
                         const int *p = input.labels.data() + std::size_t(z0) * plane;
                         const int *end = input.labels.data() + std::size_t(z1) * plane;
                         for (; p != end; ++p)
                         {
                             const int v = *p;
                             if (v <= 0)
                                 continue;
                             if (v < kFlatLabels)
                                 seen.flat[std::size_t(v)] = 1;
                             else
                                 seen.large.insert(v);
                         }
                     });
    std::vector<unsigned char> flat(kFlatLabels, 0);
    std::set<int> large;
    for (Seen &seen : local)
    {
        for (std::size_t v = 0; v < seen.flat.size(); ++v)
            flat[v] |= seen.flat[v];
        large.insert(seen.large.begin(), seen.large.end());
    }
    std::vector<int> labels;
    for (int v = 1; v < kFlatLabels; ++v)
        if (flat[std::size_t(v)])
            labels.push_back(v);
    labels.insert(labels.end(), large.begin(), large.end());
    return labels;
}

// The labels with a one-voxel background collar on every face: flying edges emits a face only
// where a label meets background inside the array, so a label cut by the field of view would
// otherwise be left open. Binary maps every label to 1.
template <typename T>
void fillPadded(vtkImageData *image, const Input &input, bool binary)
{
    T *dst = static_cast<T *>(image->GetScalarPointer());
    const std::size_t sx = input.dims[0], sy = input.dims[1], sz = input.dims[2];
    const std::size_t px = sx + 2, py = sy + 2, pz = sz + 2;
    vtkSMPTools::For(0, vtkIdType(pz),
                     [&](vtkIdType z0, vtkIdType z1)
                     {
                         for (std::size_t z = std::size_t(z0); z < std::size_t(z1); ++z)
                         {
                             T *slice = dst + z * px * py;
                             if (z == 0 || z == pz - 1)
                             {
                                 std::fill(slice, slice + px * py, T(0));
                                 continue;
                             }
                             std::fill(slice, slice + px, T(0));
                             std::fill(slice + (py - 1) * px, slice + py * px, T(0));
                             for (std::size_t y = 1; y + 1 < py; ++y)
                             {
                                 T *row = slice + y * px;
                                 const int *src = input.labels.data() + ((z - 1) * sy + (y - 1)) * sx;
                                 row[0] = T(0);
                                 row[px - 1] = T(0);
                                 for (std::size_t x = 0; x < sx; ++x)
                                 {
                                     const int v = src[x];
                                     row[x + 1] = v <= 0 ? T(0) : binary ? T(1) : T(v);
                                 }
                             }
                         }
                     });
}

// A vertex of the union's surface lies between a labelled voxel and background: it takes the
// voxel it rounds to, else the nearest labelled one around it.
void paintLabels(vtkPolyData *poly, const Input &input)
{
    vtkPoints *points = poly->GetPoints();
    const vtkIdType n = points ? points->GetNumberOfPoints() : 0;
    auto out = vtkSmartPointer<vtkIntArray>::New();
    out->SetName("Scalars");
    out->SetNumberOfTuples(n);
    int *values = out->GetPointer(0);
    const int sx = int(input.dims[0]), sy = int(input.dims[1]), sz = int(input.dims[2]);
    const auto at = [&](int x, int y, int z) -> int
    {
        if (x < 0 || y < 0 || z < 0 || x >= sx || y >= sy || z >= sz)
            return 0;
        return input.labels[(std::size_t(z) * sy + std::size_t(y)) * sx + std::size_t(x)];
    };
    vtkSMPTools::For(0, n,
                     [&](vtkIdType begin, vtkIdType end)
                     {
                         for (vtkIdType i = begin; i < end; ++i)
                         {
                             double p[3];
                             points->GetPoint(i, p);
                             const double v[3] = {p[0] / input.spacing[0], p[1] / input.spacing[1],
                                                  p[2] / input.spacing[2]};
                             const int c[3] = {int(std::lround(v[0])), int(std::lround(v[1])), int(std::lround(v[2]))};
                             int label = at(c[0], c[1], c[2]);
                             if (label <= 0)
                             {
                                 label = 0;
                                 double best = std::numeric_limits<double>::max();
                                 for (int dz = -1; dz <= 1; ++dz)
                                     for (int dy = -1; dy <= 1; ++dy)
                                         for (int dx = -1; dx <= 1; ++dx)
                                         {
                                             const int value = at(c[0] + dx, c[1] + dy, c[2] + dz);
                                             if (value <= 0)
                                                 continue;
                                             const double ex = c[0] + dx - v[0];
                                             const double ey = c[1] + dy - v[1];
                                             const double ez = c[2] + dz - v[2];
                                             const double d = ex * ex + ey * ey + ez * ez;
                                             if (d < best)
                                             {
                                                 best = d;
                                                 label = value;
                                             }
                                         }
                             }
                             values[i] = label;
                         }
                     });
    poly->GetPointData()->SetScalars(out);
    if (poly->GetCellData())
        poly->GetCellData()->SetScalars(nullptr);
}

} // namespace

Surface build(const Input &input, const std::atomic<bool> *cancel)
{
    Surface surface;
    const std::size_t count = std::size_t(input.dims[0]) * input.dims[1] * input.dims[2];
    if (count == 0 || input.labels.size() != count)
        return surface;

    surface.labels = presentLabels(input);
    if (surface.labels.empty())
        return surface;
    if (cancelled(cancel))
    {
        surface.cancelled = true;
        return surface;
    }

    const bool binary = surface.labels.size() > kPerLabelLimit;
    const int largest = surface.labels.back();
    // The smallest type that holds every label: a 101 M-voxel CBCT is 101 MB as bytes, 404 MB as int.
    const int scalarType = (binary || largest <= 255) ? VTK_UNSIGNED_CHAR
                           : largest <= 65535         ? VTK_UNSIGNED_SHORT
                                                      : VTK_INT;
    auto image = vtkSmartPointer<vtkImageData>::New();
    image->SetDimensions(int(input.dims[0]) + 2, int(input.dims[1]) + 2, int(input.dims[2]) + 2);
    image->SetSpacing(input.spacing[0], input.spacing[1], input.spacing[2]);
    // One voxel back, so the original voxels keep index * spacing.
    image->SetOrigin(-input.spacing[0], -input.spacing[1], -input.spacing[2]);
    image->AllocateScalars(scalarType, 1);
    if (scalarType == VTK_UNSIGNED_CHAR)
        fillPadded<std::uint8_t>(image, input, binary);
    else if (scalarType == VTK_UNSIGNED_SHORT)
        fillPadded<std::uint16_t>(image, input, binary);
    else
        fillPadded<int>(image, input, binary);
    if (cancelled(cancel))
    {
        surface.cancelled = true;
        return surface;
    }

    auto contour = vtkSmartPointer<vtkDiscreteFlyingEdges3D>::New();
    contour->SetComputeNormals(true);
    contour->SetComputeScalars(true);
    contour->SetComputeGradients(false);
    contour->InterpolateAttributesOff();
    contour->SetInputData(image);
    if (binary)
    {
        contour->SetNumberOfContours(1);
        contour->SetValue(0, 1);
    }
    else
    {
        contour->SetNumberOfContours(int(surface.labels.size()));
        for (std::size_t i = 0; i < surface.labels.size(); ++i)
            contour->SetValue(int(i), surface.labels[i]);
    }
    contour->Update();
    if (cancelled(cancel))
    {
        surface.cancelled = true;
        return surface;
    }

    auto smoother = vtkSmartPointer<vtkWindowedSincPolyDataFilter>::New();
    smoother->SetNumberOfIterations(15);
    smoother->BoundarySmoothingOn();
    smoother->FeatureEdgeSmoothingOff();
    smoother->SetFeatureAngle(120.0);
    smoother->SetPassBand(0.1);
    smoother->NonManifoldSmoothingOn();
    smoother->NormalizeCoordinatesOn();
    smoother->SetInputConnection(contour->GetOutputPort());
    smoother->Update();
    if (cancelled(cancel))
    {
        surface.cancelled = true;
        return surface;
    }

    // A copy detached from the filters, which die with this thread's stack.
    surface.poly = vtkSmartPointer<vtkPolyData>::New();
    surface.poly->ShallowCopy(smoother->GetOutput());
    if (binary)
        paintLabels(surface.poly, input);
    if (surface.poly->GetNumberOfCells() > 0)
    {
        surface.locator = vtkSmartPointer<vtkStaticCellLocator>::New();
        surface.locator->SetDataSet(surface.poly);
        surface.locator->BuildLocator();
    }
    return surface;
}

} // namespace mask_surface
