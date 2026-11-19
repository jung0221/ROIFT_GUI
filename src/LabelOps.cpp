#include "LabelOps.h"

#include <algorithm>
#include <cstdint>

namespace label_ops
{
namespace
{

// Flood from seed over voxels holding `label`, marking them in visited; calls visit(index)
// once per voxel of the piece.
template <typename Visit>
void flood(const std::vector<int> &labels, const std::array<unsigned, 3> &dims, std::size_t seed, int label,
           std::vector<bool> &visited, Visit visit)
{
    const long long nx = dims[0];
    const long long ny = dims[1];
    const long long nz = dims[2];
    const long long plane = nx * ny;
    std::vector<std::size_t> stack{seed};
    visited[seed] = true;
    while (!stack.empty())
    {
        const std::size_t index = stack.back();
        stack.pop_back();
        visit(index);
        const long long x = static_cast<long long>(index) % nx;
        const long long y = (static_cast<long long>(index) / nx) % ny;
        const long long z = static_cast<long long>(index) / plane;
        for (long long dz = -1; dz <= 1; ++dz)
        {
            const long long zz = z + dz;
            if (zz < 0 || zz >= nz)
                continue;
            for (long long dy = -1; dy <= 1; ++dy)
            {
                const long long yy = y + dy;
                if (yy < 0 || yy >= ny)
                    continue;
                for (long long dx = -1; dx <= 1; ++dx)
                {
                    const long long xx = x + dx;
                    if (xx < 0 || xx >= nx)
                        continue;
                    const std::size_t next = static_cast<std::size_t>(xx + yy * nx + zz * plane);
                    if (!visited[next] && labels[next] == label)
                    {
                        visited[next] = true;
                        stack.push_back(next);
                    }
                }
            }
        }
    }
}

bool fits(const std::vector<int> &labels, const std::array<unsigned, 3> &dims)
{
    return labels.size() == std::size_t(dims[0]) * dims[1] * dims[2] && !labels.empty();
}

} // namespace

std::vector<Piece> pieces(const std::vector<int> &labels, const std::array<unsigned, 3> &dims, int label)
{
    std::vector<Piece> found;
    if (!fits(labels, dims))
        return found;
    std::vector<bool> visited(labels.size(), false);
    for (std::size_t i = 0; i < labels.size(); ++i)
    {
        if (labels[i] != label || visited[i])
            continue;
        Piece piece;
        piece.seed = i;
        flood(labels, dims, i, label, visited, [&piece](std::size_t)
              { ++piece.voxels; });
        found.push_back(piece);
    }
    std::sort(found.begin(), found.end(), [](const Piece &a, const Piece &b)
              { return a.voxels != b.voxels ? a.voxels > b.voxels : a.seed < b.seed; });
    return found;
}

namespace
{

// The label's bounding box grown by one voxel, clamped to the volume; empty when it is absent.
struct Box
{
    std::array<long long, 3> lo{0, 0, 0};
    std::array<long long, 3> hi{-1, -1, -1}; // inclusive
    bool empty() const { return hi[0] < lo[0]; }
    long long size(int a) const { return hi[size_t(a)] - lo[size_t(a)] + 1; }
};

Box labelBox(const std::vector<int> &labels, const std::array<unsigned, 3> &dims, int label)
{
    Box box;
    std::array<long long, 3> lo{dims[0], dims[1], dims[2]};
    std::array<long long, 3> hi{-1, -1, -1};
    const long long nx = dims[0], ny = dims[1];
    for (std::size_t i = 0; i < labels.size(); ++i)
    {
        if (labels[i] != label)
            continue;
        const long long p[3] = {static_cast<long long>(i) % nx, (static_cast<long long>(i) / nx) % ny,
                                static_cast<long long>(i) / (nx * ny)};
        for (int a = 0; a < 3; ++a)
        {
            lo[size_t(a)] = std::min(lo[size_t(a)], p[a]);
            hi[size_t(a)] = std::max(hi[size_t(a)], p[a]);
        }
    }
    if (hi[0] < 0)
        return box;
    for (int a = 0; a < 3; ++a)
    {
        box.lo[size_t(a)] = std::max(0LL, lo[size_t(a)] - 1);
        box.hi[size_t(a)] = std::min(static_cast<long long>(dims[size_t(a)]) - 1, hi[size_t(a)] + 1);
    }
    return box;
}

// Calls f(index, x, y, z) for every voxel of the box.
template <typename F>
void forBox(const Box &box, const std::array<unsigned, 3> &dims, F f)
{
    const long long nx = dims[0], ny = dims[1];
    for (long long z = box.lo[2]; z <= box.hi[2]; ++z)
        for (long long y = box.lo[1]; y <= box.hi[1]; ++y)
            for (long long x = box.lo[0]; x <= box.hi[0]; ++x)
                f(static_cast<std::size_t>(x + y * nx + z * nx * ny), x, y, z);
}

const long long kFace[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};

} // namespace

std::vector<std::size_t> pieceAt(const std::vector<int> &labels, const std::array<unsigned, 3> &dims,
                                 std::size_t seed)
{
    std::vector<std::size_t> voxels;
    if (!fits(labels, dims) || seed >= labels.size())
        return voxels;
    std::vector<bool> visited(labels.size(), false);
    flood(labels, dims, seed, labels[seed], visited, [&voxels](std::size_t index)
          { voxels.push_back(index); });
    return voxels;
}

std::vector<std::size_t> holes(const std::vector<int> &labels, const std::array<unsigned, 3> &dims, int label,
                               int planeAxis)
{
    std::vector<std::size_t> found;
    if (!fits(labels, dims))
        return found;
    const Box box = labelBox(labels, dims, label);
    if (box.empty())
        return found;
    // Within the box grown by one voxel the outer shell is outside the label wherever the box
    // did not reach the volume's edge, so flooding from the box's border reaches every voxel
    // that the outside reaches.
    const long long w = box.size(0), h = box.size(1), d = box.size(2);
    const long long nx = dims[0], ny = dims[1];
    std::vector<std::uint8_t> reached(size_t(w * h * d), 0);
    const auto local = [&](long long x, long long y, long long z)
    { return size_t((x - box.lo[0]) + (y - box.lo[1]) * w + (z - box.lo[2]) * w * h); };
    const auto global = [&](long long x, long long y, long long z)
    { return size_t(x + y * nx + z * nx * ny); };
    std::vector<std::array<long long, 3>> stack;
    const auto onBorder = [&](long long x, long long y, long long z)
    {
        const long long p[3] = {x, y, z};
        for (int a = 0; a < 3; ++a)
        {
            if (a == planeAxis)
                continue;
            if (p[a] == box.lo[size_t(a)] || p[a] == box.hi[size_t(a)])
                return true;
        }
        return false;
    };
    forBox(box, dims, [&](std::size_t index, long long x, long long y, long long z)
           {
        if (labels[index] != label && onBorder(x, y, z))
        {
            reached[local(x, y, z)] = 1;
            stack.push_back({x, y, z});
        } });
    while (!stack.empty())
    {
        const std::array<long long, 3> p = stack.back();
        stack.pop_back();
        for (const auto &step : kFace)
        {
            if (planeAxis >= 0 && step[planeAxis] != 0)
                continue;
            const long long q[3] = {p[0] + step[0], p[1] + step[1], p[2] + step[2]};
            bool inside = true;
            for (int a = 0; a < 3; ++a)
                inside = inside && q[a] >= box.lo[size_t(a)] && q[a] <= box.hi[size_t(a)];
            if (!inside)
                continue;
            const size_t l = local(q[0], q[1], q[2]);
            if (reached[l] || labels[global(q[0], q[1], q[2])] == label)
                continue;
            reached[l] = 1;
            stack.push_back({q[0], q[1], q[2]});
        }
    }
    forBox(box, dims, [&](std::size_t index, long long x, long long y, long long z)
           {
        if (labels[index] == 0 && !reached[local(x, y, z)])
            found.push_back(index); });
    return found;
}

namespace
{

// Voxels of the box (sel = true: holding the label; false: not) with a face neighbour on the
// other side.
std::vector<std::size_t> boundary(const std::vector<int> &labels, const std::array<unsigned, 3> &dims, int label,
                                  bool ofLabel)
{
    std::vector<std::size_t> found;
    if (!fits(labels, dims))
        return found;
    const Box box = labelBox(labels, dims, label);
    if (box.empty())
        return found;
    const long long n[3] = {dims[0], dims[1], dims[2]};
    forBox(box, dims, [&](std::size_t index, long long x, long long y, long long z)
           {
        if ((labels[index] == label) != ofLabel)
            return;
        const long long p[3] = {x, y, z};
        for (const auto &step : kFace)
        {
            const long long q[3] = {p[0] + step[0], p[1] + step[1], p[2] + step[2]};
            bool inside = true;
            for (int a = 0; a < 3; ++a)
                inside = inside && q[a] >= 0 && q[a] < n[a];
            const bool other = inside ? (labels[size_t(q[0] + q[1] * n[0] + q[2] * n[0] * n[1])] == label) != ofLabel
                                      : ofLabel; // outside the volume is not the label
            if (other)
            {
                found.push_back(index);
                return;
            }
        } });
    return found;
}

} // namespace

std::vector<std::size_t> dilation(const std::vector<int> &labels, const std::array<unsigned, 3> &dims, int label)
{
    return boundary(labels, dims, label, false);
}

std::vector<std::size_t> erosion(const std::vector<int> &labels, const std::array<unsigned, 3> &dims, int label)
{
    return boundary(labels, dims, label, true);
}

Smoothing majority(const std::vector<int> &labels, const std::array<unsigned, 3> &dims, int label)
{
    Smoothing result;
    if (!fits(labels, dims))
        return result;
    const Box box = labelBox(labels, dims, label);
    if (box.empty())
        return result;
    // Counts of the label in each 3 x 3 x 3 neighbourhood by three separable 3-tap sums over
    // the box; voxels beyond the box hold no label, so the sums stay exact.
    const long long w = box.size(0), h = box.size(1), d = box.size(2);
    std::vector<std::uint8_t> a(size_t(w * h * d), 0), b(a.size(), 0);
    const auto local = [&](long long x, long long y, long long z) { return size_t(x + y * w + z * w * h); };
    forBox(box, dims, [&](std::size_t index, long long x, long long y, long long z)
           { a[local(x - box.lo[0], y - box.lo[1], z - box.lo[2])] = labels[index] == label; });
    const long long size[3] = {w, h, d};
    const long long step[3] = {1, w, w * h};
    std::vector<std::uint8_t> *from = &a, *to = &b;
    for (int axis = 0; axis < 3; ++axis)
    {
        for (long long z = 0; z < d; ++z)
            for (long long y = 0; y < h; ++y)
                for (long long x = 0; x < w; ++x)
                {
                    const long long p[3] = {x, y, z};
                    const size_t i = local(x, y, z);
                    int sum = (*from)[i];
                    if (p[axis] > 0)
                        sum += (*from)[i - size_t(step[axis])];
                    if (p[axis] + 1 < size[axis])
                        sum += (*from)[i + size_t(step[axis])];
                    (*to)[i] = static_cast<std::uint8_t>(sum);
                }
        std::swap(from, to);
    }
    constexpr int kMajority = 14; // of 27
    forBox(box, dims, [&](std::size_t index, long long x, long long y, long long z)
           {
        const int count = (*from)[local(x - box.lo[0], y - box.lo[1], z - box.lo[2])];
        const bool is = labels[index] == label;
        if (!is && count >= kMajority)
            result.add.push_back(index);
        else if (is && count < kMajority)
            result.remove.push_back(index); });
    return result;
}

} // namespace label_ops
