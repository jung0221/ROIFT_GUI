#include "SliceFill.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace slice_fill
{
namespace
{

constexpr double kFar = 1e20; // squared mm; finite so the parabola intersections stay finite

// Felzenszwalb and Huttenlocher's 1D squared distance transform, samples `step` mm apart:
// d[q] = min over p of ((q - p) step)^2 + f[p], by the lower envelope of parabolas.
void distance1D(const double *f, double *d, int n, double step, std::vector<int> &v, std::vector<double> &z)
{
    v.assign(size_t(n), 0);
    z.assign(size_t(n) + 1, 0.0);
    const auto intersect = [&](int q, int p)
    {
        const double pq = q * step;
        const double pp = p * step;
        return ((f[q] + pq * pq) - (f[p] + pp * pp)) / (2.0 * (pq - pp));
    };
    int k = 0;
    z[0] = -std::numeric_limits<double>::infinity();
    z[1] = std::numeric_limits<double>::infinity();
    for (int q = 1; q < n; ++q)
    {
        double s = intersect(q, v[size_t(k)]);
        while (s <= z[size_t(k)])
        {
            --k;
            s = intersect(q, v[size_t(k)]);
        }
        ++k;
        v[size_t(k)] = q;
        z[size_t(k)] = s;
        z[size_t(k) + 1] = std::numeric_limits<double>::infinity();
    }
    k = 0;
    for (int q = 0; q < n; ++q)
    {
        while (z[size_t(k) + 1] < q * step)
            ++k;
        const double dq = (q - v[size_t(k)]) * step;
        d[q] = dq * dq + f[v[size_t(k)]];
    }
}

// Squared distance in mm^2 from each pixel to the nearest pixel where feature is set.
std::vector<double> squaredDistance(const std::vector<unsigned char> &feature, int w, int h, double su, double sv)
{
    std::vector<double> rows(size_t(w) * h);
    std::vector<double> f(size_t(std::max(w, h)));
    std::vector<double> d(size_t(std::max(w, h)));
    std::vector<int> v;
    std::vector<double> z;
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
            f[size_t(x)] = feature[size_t(x) + size_t(y) * w] ? 0.0 : kFar;
        distance1D(f.data(), d.data(), w, su, v, z);
        std::copy(d.begin(), d.begin() + w, rows.begin() + std::ptrdiff_t(y) * w);
    }
    std::vector<double> out(size_t(w) * h);
    for (int x = 0; x < w; ++x)
    {
        for (int y = 0; y < h; ++y)
            f[size_t(y)] = rows[size_t(x) + size_t(y) * w];
        distance1D(f.data(), d.data(), h, sv, v, z);
        for (int y = 0; y < h; ++y)
            out[size_t(x) + size_t(y) * w] = d[size_t(y)];
    }
    return out;
}

struct Box
{
    int u0 = std::numeric_limits<int>::max();
    int v0 = std::numeric_limits<int>::max();
    int u1 = -1;
    int v1 = -1;
    bool empty() const { return u1 < u0; }
    void add(int u, int v)
    {
        u0 = std::min(u0, u);
        v0 = std::min(v0, v);
        u1 = std::max(u1, u);
        v1 = std::max(v1, v);
    }
};

} // namespace

std::vector<float> signedDistance2D(const std::vector<unsigned char> &inside, int width, int height, double su,
                                    double sv)
{
    std::vector<unsigned char> outside(inside.size());
    for (size_t i = 0; i < inside.size(); ++i)
        outside[i] = inside[i] ? 0 : 1;
    const std::vector<double> toInside = squaredDistance(inside, width, height, su, sv);
    const std::vector<double> toOutside = squaredDistance(outside, width, height, su, sv);
    std::vector<float> sdf(inside.size());
    for (size_t i = 0; i < inside.size(); ++i)
        sdf[i] = inside[i] ? -static_cast<float>(std::sqrt(std::min(toOutside[i], kFar)))
                           : static_cast<float>(std::sqrt(std::min(toInside[i], kFar)));
    return sdf;
}

Result fillBetweenSlices(const std::vector<int> &labels, const std::array<unsigned, 3> &dims,
                         const std::array<double, 3> &spacing, int label, int normalAxis)
{
    Result result;
    if (normalAxis < 0 || normalAxis > 2 || labels.size() != size_t(dims[0]) * dims[1] * dims[2] || labels.empty())
        return result;
    // The slice plane's axes, u fastest, as the slice views draw them.
    const int ua = normalAxis == 0 ? 1 : 0;
    const int va = normalAxis == 2 ? 1 : 2;
    const int nu = int(dims[size_t(ua)]);
    const int nv = int(dims[size_t(va)]);
    const int nn = int(dims[size_t(normalAxis)]);
    const std::array<size_t, 3> stride{1, size_t(dims[0]), size_t(dims[0]) * dims[1]};
    const auto indexOf = [&](int u, int v, int k)
    { return size_t(u) * stride[size_t(ua)] + size_t(v) * stride[size_t(va)] + size_t(k) * stride[size_t(normalAxis)]; };

    std::vector<Box> boxes(static_cast<size_t>(nn));
    for (int k = 0; k < nn; ++k)
        for (int v = 0; v < nv; ++v)
            for (int u = 0; u < nu; ++u)
                if (labels[indexOf(u, v, k)] == label)
                    boxes[size_t(k)].add(u, v);
    std::vector<int> keys;
    for (int k = 0; k < nn; ++k)
        if (!boxes[size_t(k)].empty())
            keys.push_back(k);

    for (size_t i = 0; i + 1 < keys.size(); ++i)
    {
        const int a = keys[i];
        const int b = keys[i + 1];
        if (b - a < 2)
            continue;
        result.gaps.push_back({a, b});
        // Outside both outlines' box, both distances are positive, so nothing beyond the
        // union box (plus a ring of outside pixels for the inside distances) can be filled.
        Box box = boxes[size_t(a)];
        box.add(boxes[size_t(b)].u0, boxes[size_t(b)].v0);
        box.add(boxes[size_t(b)].u1, boxes[size_t(b)].v1);
        const int u0 = std::max(0, box.u0 - 1);
        const int v0 = std::max(0, box.v0 - 1);
        const int u1 = std::min(nu - 1, box.u1 + 1);
        const int v1 = std::min(nv - 1, box.v1 + 1);
        const int w = u1 - u0 + 1;
        const int h = v1 - v0 + 1;
        const auto planeOf = [&](int k)
        {
            std::vector<unsigned char> inside(size_t(w) * h, 0);
            for (int v = 0; v < h; ++v)
                for (int u = 0; u < w; ++u)
                    inside[size_t(u) + size_t(v) * w] = labels[indexOf(u0 + u, v0 + v, k)] == label;
            return signedDistance2D(inside, w, h, spacing[size_t(ua)], spacing[size_t(va)]);
        };
        const std::vector<float> da = planeOf(a);
        const std::vector<float> db = planeOf(b);
        for (int k = a + 1; k < b; ++k)
        {
            const float t = float(k - a) / float(b - a);
            for (int v = 0; v < h; ++v)
                for (int u = 0; u < w; ++u)
                {
                    const size_t p = size_t(u) + size_t(v) * w;
                    if ((1.0f - t) * da[p] + t * db[p] < 0.0f)
                    {
                        const size_t index = indexOf(u0 + u, v0 + v, k);
                        if (labels[index] != label)
                            result.voxels.push_back(index);
                    }
                }
            ++result.filledSlices;
        }
    }
    return result;
}

} // namespace slice_fill
