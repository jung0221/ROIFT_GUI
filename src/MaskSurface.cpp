#include "MaskSurface.h"

#include <vtkCellArray.h>
#include <vtkFloatArray.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkImageData.h>
#include <vtkObjectFactory.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSMPThreadLocal.h>
#include <vtkSMPThreadLocalObject.h>
#include <vtkSMPTools.h>
#include <vtkStaticCellLocator.h>
#include <vtkSurfaceNets3D.h>
#include <vtkTriangleMeshPointNormals.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
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

// The labels with a one-voxel background collar on every face: surface nets emits a face only
// where a label meets another value inside the array, so a label cut by the field of view would
// otherwise be left open.
template <typename T>
void fillPadded(vtkImageData *image, const Input &input)
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
                                     row[x + 1] = v <= 0 ? T(0) : T(v);
                                 }
                             }
                         }
                     });
}

// Surface nets winds a face by the labels on its two sides only from VTK 9.6 on; before, the
// winding is fixed per axis. The unsmoothed net it caches shows, on every version, which voxels a
// face lies between, and smoothing moves its points without touching its faces or their order.
class OrientedSurfaceNets : public vtkSurfaceNets3D
{
public:
    static OrientedSurfaceNets *New();
    vtkTypeMacro(OrientedSurfaceNets, vtkSurfaceNets3D);
    vtkPolyData *unsmoothed() const { return this->GeometryCache; }

protected:
    OrientedSurfaceNets() = default;
    ~OrientedSurfaceNets() override = default;
};
vtkStandardNewMacro(OrientedSurfaceNets);

// Position of a label value in the ascending label list, -1 for background.
class ShellIndex
{
public:
    explicit ShellIndex(const std::vector<int> &labels) : m_labels(labels)
    {
        m_flat.assign(std::size_t(std::min(labels.back() + 1, kFlatLabels)), -1);
        for (std::size_t i = 0; i < labels.size() && labels[i] < int(m_flat.size()); ++i)
            m_flat[std::size_t(labels[i])] = int(i);
    }

    int operator()(int label) const
    {
        if (label <= 0)
            return -1;
        if (label < int(m_flat.size()))
            return m_flat[std::size_t(label)];
        const auto it = std::lower_bound(m_labels.begin(), m_labels.end(), label);
        return (it != m_labels.end() && *it == label) ? int(it - m_labels.begin()) : -1;
    }

private:
    const std::vector<int> &m_labels;
    std::vector<int> m_flat;
};

// The corners of a quad's two triangles, split along 0-2 or along 1-3.
constexpr int kSplit02[6] = {0, 1, 2, 0, 2, 3};
constexpr int kSplit13[6] = {0, 1, 3, 1, 2, 3};

// A shell holding at least 1 / kLargeShellShare of all triangles is finished alone, on every thread.
constexpr vtkIdType kLargeShellShare = 8;

// The net's quads, as arrays so the loops over them stay fast in a Debug build: the four points of
// each, the shell its normal leaves (`inner`) and the one it enters (`outer`), -1 for background,
// and the diagonal it is split along once smoothed.
struct Quads
{
    std::vector<vtkIdType> points;       // four per quad
    std::vector<int> inner;
    std::vector<int> outer;
    std::vector<unsigned char> split02; // triangles 0-1-2 and 0-2-3, else 0-1-3 and 1-2-3
};

// An unsmoothed quad is axis-aligned and centred on the midpoint of the two voxel centres it
// separates, half a voxel either side of it along its normal.
Quads classifyQuads(vtkPolyData *unsmoothed, vtkPolyData *smoothed, const Input &input, const ShellIndex &shellOf)
{
    vtkPoints *raw = unsmoothed->GetPoints();
    vtkPoints *moved = smoothed->GetPoints();
    vtkCellArray *cells = unsmoothed->GetPolys();
    const vtkIdType count = cells->GetNumberOfCells();
    Quads quads;
    quads.points.assign(static_cast<std::size_t>(count) * 4, 0);
    quads.inner.assign(static_cast<std::size_t>(count), -1);
    quads.outer.assign(static_cast<std::size_t>(count), -1);
    quads.split02.assign(static_cast<std::size_t>(count), 1);
    const long long dims[3] = {input.dims[0], input.dims[1], input.dims[2]};
    const int *labels = input.labels.data();
    const auto labelAt = [&](const long long v[3]) -> int
    {
        for (int a = 0; a < 3; ++a)
            if (v[a] < 0 || v[a] >= dims[a])
                return 0;
        return labels[(v[2] * dims[1] + v[1]) * dims[0] + v[0]];
    };
    vtkSMPThreadLocalObject<vtkIdList> lists;
    vtkSMPTools::For(0, count,
                     [&](vtkIdType begin, vtkIdType end)
                     {
                         vtkIdList *list = lists.Local();
                         vtkIdType *quadPoints = quads.points.data();
                         for (vtkIdType c = begin; c < end; ++c)
                         {
                             vtkIdType n = 0;
                             const vtkIdType *ids = nullptr;
                             cells->GetCellAtId(c, n, ids, list);
                             if (n != 4)
                                 continue; // not a quad: no shell gets it
                             double p[4][3];
                             for (int k = 0; k < 4; ++k)
                             {
                                 quadPoints[4 * c + k] = ids[k];
                                 raw->GetPoint(ids[k], p[k]);
                             }
                             double centre[3], d02[3], d13[3];
                             for (int a = 0; a < 3; ++a)
                             {
                                 centre[a] = 0.25 * (p[0][a] + p[1][a] + p[2][a] + p[3][a]) / input.spacing[a];
                                 d02[a] = p[2][a] - p[0][a];
                                 d13[a] = p[3][a] - p[1][a];
                             }
                             const double normal[3] = {d02[1] * d13[2] - d02[2] * d13[1],
                                                       d02[2] * d13[0] - d02[0] * d13[2],
                                                       d02[0] * d13[1] - d02[1] * d13[0]};
                             int axis = 0;
                             for (int a = 1; a < 3; ++a)
                                 if (std::abs(normal[a]) > std::abs(normal[axis]))
                                     axis = a;
                             const double half = normal[axis] > 0.0 ? 0.5 : -0.5;
                             long long behind[3], ahead[3];
                             for (int a = 0; a < 3; ++a)
                                 behind[a] = ahead[a] = std::llround(centre[a]);
                             behind[axis] = std::llround(centre[axis] - half);
                             ahead[axis] = std::llround(centre[axis] + half);
                             quads.inner[std::size_t(c)] = shellOf(labelAt(behind));
                             quads.outer[std::size_t(c)] = shellOf(labelAt(ahead));

                             for (int k = 0; k < 4; ++k)
                                 moved->GetPoint(ids[k], p[k]);
                             double l02 = 0.0, l13 = 0.0;
                             for (int a = 0; a < 3; ++a)
                             {
                                 l02 += (p[2][a] - p[0][a]) * (p[2][a] - p[0][a]);
                                 l13 += (p[3][a] - p[1][a]) * (p[3][a] - p[1][a]);
                             }
                             quads.split02[std::size_t(c)] = l02 <= l13 ? 1 : 0;
                         }
                     });
    return quads;
}

// Each shell's triangles, with its own copies of the points so each label gets the normals of its
// own side of a shared wall. One pass over the faces, shell by shell, through one point map.
std::vector<vtkSmartPointer<vtkPolyData>> assembleShells(vtkPoints *points, const Quads &quads, std::size_t shellCount)
{
    const std::size_t quadCount = quads.inner.size();
    const int *inner = quads.inner.data();
    const int *outer = quads.outer.data();
    std::vector<vtkIdType> start(shellCount + 1, 0);
    for (std::size_t c = 0; c < quadCount; ++c)
    {
        if (inner[c] >= 0)
            ++start[std::size_t(inner[c]) + 1];
        if (outer[c] >= 0)
            ++start[std::size_t(outer[c]) + 1];
    }
    std::partial_sum(start.begin(), start.end(), start.begin());
    // quad * 2, plus 1 where the shell sees the quad from its front and winds it the other way
    std::vector<vtkIdType> faces(static_cast<std::size_t>(start.back()));
    {
        std::vector<vtkIdType> next(start.begin(), start.end() - 1);
        vtkIdType *slot = next.data();
        vtkIdType *out = faces.data();
        for (std::size_t c = 0; c < quadCount; ++c)
        {
            if (inner[c] >= 0)
                out[slot[inner[c]]++] = vtkIdType(c) * 2;
            if (outer[c] >= 0)
                out[slot[outer[c]]++] = vtkIdType(c) * 2 + 1;
        }
    }

    const vtkIdType pointCount = points->GetNumberOfPoints();
    vtkFloatArray *sourceFloats = vtkFloatArray::SafeDownCast(points->GetData());
    const float *source = sourceFloats ? sourceFloats->GetPointer(0) : nullptr;
    const vtkIdType *quadPoints = quads.points.data();
    const unsigned char *split02 = quads.split02.data();
    std::vector<vtkIdType> localIds(static_cast<std::size_t>(pointCount), -1);
    vtkIdType *local = localIds.data();
    std::vector<vtkIdType> usedIds(static_cast<std::size_t>(pointCount));
    vtkIdType *used = usedIds.data();

    std::vector<vtkSmartPointer<vtkPolyData>> shells(shellCount);
    for (std::size_t s = 0; s < shellCount; ++s)
    {
        const vtkIdType *face = faces.data() + start[s];
        const vtkIdType count = start[s + 1] - start[s];
        auto connectivity = vtkSmartPointer<vtkIdTypeArray>::New();
        connectivity->SetNumberOfValues(count * 6);
        vtkIdType *out = connectivity->GetPointer(0);
        vtkIdType usedCount = 0;
        for (vtkIdType f = 0; f < count; ++f)
        {
            const vtkIdType quad = face[f] / 2;
            const bool reversed = (face[f] % 2) != 0;
            const vtkIdType *ids = quadPoints + 4 * quad;
            vtkIdType v[4];
            for (int k = 0; k < 4; ++k)
            {
                vtkIdType &id = local[ids[k]];
                if (id < 0)
                {
                    id = usedCount;
                    used[usedCount++] = ids[k];
                }
                v[k] = id;
            }
            // Two triangles; reversing a shell's copy swaps the last two corners of each.
            const int *corner = split02[quad] ? kSplit02 : kSplit13;
            for (int t = 0; t < 2; ++t, out += 3)
            {
                out[0] = v[corner[3 * t]];
                out[1] = v[corner[3 * t + (reversed ? 2 : 1)]];
                out[2] = v[corner[3 * t + (reversed ? 1 : 2)]];
            }
        }

        auto shellPoints = vtkSmartPointer<vtkPoints>::New();
        if (source)
        {
            shellPoints->SetDataTypeToFloat();
            shellPoints->SetNumberOfPoints(usedCount);
            float *dst = static_cast<vtkFloatArray *>(shellPoints->GetData())->GetPointer(0);
            for (vtkIdType i = 0; i < usedCount; ++i, dst += 3)
            {
                const float *p = source + 3 * used[i];
                dst[0] = p[0];
                dst[1] = p[1];
                dst[2] = p[2];
                local[used[i]] = -1;
            }
        }
        else
        {
            shellPoints->SetDataType(points->GetDataType());
            shellPoints->SetNumberOfPoints(usedCount);
            for (vtkIdType i = 0; i < usedCount; ++i)
            {
                double p[3];
                points->GetPoint(used[i], p);
                shellPoints->SetPoint(i, p);
                local[used[i]] = -1;
            }
        }
        auto offsets = vtkSmartPointer<vtkIdTypeArray>::New();
        offsets->SetNumberOfValues(count * 2 + 1);
        vtkIdType *o = offsets->GetPointer(0);
        for (vtkIdType i = 0; i <= count * 2; ++i)
            o[i] = i * 3;
        auto triangles = vtkSmartPointer<vtkCellArray>::New();
        triangles->SetData(offsets, connectivity);
        shells[s] = vtkSmartPointer<vtkPolyData>::New();
        shells[s]->SetPoints(shellPoints);
        shells[s]->SetPolys(triangles);
    }
    return shells;
}

// Point normals and a locator for one shell.
Shell finishShell(int label, vtkPolyData *triangles)
{
    Shell shell;
    shell.label = label;
    auto normals = vtkSmartPointer<vtkTriangleMeshPointNormals>::New();
    normals->SetInputData(triangles);
    normals->Update();
    shell.poly = vtkSmartPointer<vtkPolyData>::New();
    shell.poly->ShallowCopy(normals->GetOutput());
    if (shell.poly->GetNumberOfCells() > 0)
    {
        shell.locator = vtkSmartPointer<vtkStaticCellLocator>::New();
        shell.locator->SetDataSet(shell.poly);
        shell.locator->BuildLocator();
    }
    return shell;
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
    const auto stop = [&]()
    {
        surface.cancelled = true;
        surface.shells.clear();
        return surface;
    };
    if (cancelled(cancel))
        return stop();

    const int largest = surface.labels.back();
    // The smallest type that holds every label: a 101 M-voxel CBCT is 101 MB as bytes, 404 MB as int.
    const int scalarType = largest <= 255 ? VTK_UNSIGNED_CHAR : largest <= 65535 ? VTK_UNSIGNED_SHORT : VTK_INT;
    auto image = vtkSmartPointer<vtkImageData>::New();
    image->SetDimensions(int(input.dims[0]) + 2, int(input.dims[1]) + 2, int(input.dims[2]) + 2);
    image->SetSpacing(input.spacing[0], input.spacing[1], input.spacing[2]);
    // One voxel back, so the original voxels keep index * spacing.
    image->SetOrigin(-input.spacing[0], -input.spacing[1], -input.spacing[2]);
    image->AllocateScalars(scalarType, 1);
    if (scalarType == VTK_UNSIGNED_CHAR)
        fillPadded<std::uint8_t>(image, input);
    else if (scalarType == VTK_UNSIGNED_SHORT)
        fillPadded<std::uint16_t>(image, input);
    else
        fillPadded<int>(image, input);
    if (cancelled(cancel))
        return stop();

    // One pass for every label, where discrete flying edges passes over the volume once per label.
    // Quads, so that the smoothed faces are those of the unsmoothed net, in the same order.
    auto nets = vtkSmartPointer<OrientedSurfaceNets>::New();
    nets->SetInputData(image);
    nets->SetNumberOfLabels(int(surface.labels.size()));
    for (std::size_t i = 0; i < surface.labels.size(); ++i)
        nets->SetValue(int(i), surface.labels[i]);
    nets->SetOutputMeshTypeToQuads();
    nets->DataCachingOn();
    nets->Update();
    vtkPolyData *smoothed = nets->GetOutput();
    vtkPolyData *unsmoothed = nets->unsmoothed();
    if (!unsmoothed || unsmoothed->GetNumberOfCells() != smoothed->GetNumberOfCells())
        unsmoothed = smoothed; // not expected: the faces are then classified where smoothing left them
    image = nullptr;
    if (cancelled(cancel))
        return stop();

    const Quads quads = classifyQuads(unsmoothed, smoothed, input, ShellIndex(surface.labels));
    if (cancelled(cancel))
        return stop();
    const std::vector<vtkSmartPointer<vtkPolyData>> polys =
        assembleShells(smoothed->GetPoints(), quads, surface.labels.size());
    nets = nullptr;
    if (cancelled(cancel))
        return stop();

    // The locator build is threaded inside, but not when nested in another parallel loop: the
    // largest shells (a jaw can be half the triangles) go one at a time, the rest side by side.
    vtkIdType triangles = 0;
    for (const auto &poly : polys)
        triangles += poly->GetNumberOfCells();
    std::vector<std::size_t> large, small;
    for (std::size_t s = 0; s < polys.size(); ++s)
        (polys[s]->GetNumberOfCells() * kLargeShellShare >= triangles ? large : small).push_back(s);
    std::sort(small.begin(), small.end(), [&](std::size_t a, std::size_t b)
              { return polys[a]->GetNumberOfCells() > polys[b]->GetNumberOfCells(); });
    surface.shells.resize(polys.size());
    for (std::size_t s : large)
    {
        if (cancelled(cancel))
            return stop();
        surface.shells[s] = finishShell(surface.labels[s], polys[s]);
    }
    vtkSMPTools::For(0, vtkIdType(small.size()), 1,
                     [&](vtkIdType begin, vtkIdType end)
                     {
                         for (vtkIdType i = begin; i < end && !cancelled(cancel); ++i)
                         {
                             const std::size_t s = small[std::size_t(i)];
                             surface.shells[s] = finishShell(surface.labels[s], polys[s]);
                         }
                     });
    if (cancelled(cancel))
        return stop();
    return surface;
}

} // namespace mask_surface
