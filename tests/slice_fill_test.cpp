// Pins slice_fill: the signed distance against a brute-force one, and fill between slices on
// discs: the same disc carried through, a growing disc passing the mean radius halfway, a
// moving disc moving linearly while its outlines overlap, nothing between outlines that do not, gaps found between every pair of drawn slices, and voxels
// already holding the label left out.
#include "SliceFill.h"

#include <cmath>
#include <cstdio>
#include <random>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-66s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

constexpr unsigned kN = 40;
constexpr unsigned kSlices = 21;

size_t at(unsigned x, unsigned y, unsigned z)
{
    return size_t(x) + size_t(y) * kN + size_t(z) * kN * kN;
}

void disc(std::vector<int> &labels, unsigned z, double cx, double cy, double r, int label)
{
    for (unsigned y = 0; y < kN; ++y)
        for (unsigned x = 0; x < kN; ++x)
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r)
                labels[at(x, y, z)] = label;
}

// Pixels of the slice that fill would give the label, by (x, y).
std::vector<unsigned char> filledOn(const slice_fill::Result &result, unsigned z)
{
    std::vector<unsigned char> plane(size_t(kN) * kN, 0);
    for (size_t index : result.voxels)
        if (index / (size_t(kN) * kN) == z)
            plane[index % (size_t(kN) * kN)] = 1;
    return plane;
}

double area(const std::vector<unsigned char> &plane)
{
    double n = 0;
    for (unsigned char v : plane)
        n += v;
    return n;
}

} // namespace

int main()
{
    // Signed distance against brute force, on random shapes with anisotropic pixels.
    std::mt19937 rng(3);
    bool exact = true;
    for (int trial = 0; trial < 20 && exact; ++trial)
    {
        const int w = 17, h = 13;
        const double su = 0.4, sv = 1.1;
        std::vector<unsigned char> inside(size_t(w) * h);
        std::bernoulli_distribution coin(0.3);
        for (auto &v : inside)
            v = coin(rng);
        const std::vector<float> sdf = slice_fill::signedDistance2D(inside, w, h, su, sv);
        for (int y = 0; y < h && exact; ++y)
            for (int x = 0; x < w && exact; ++x)
            {
                const bool in = inside[size_t(x) + size_t(y) * w];
                double best = 1e30;
                for (int yy = 0; yy < h; ++yy)
                    for (int xx = 0; xx < w; ++xx)
                        if (bool(inside[size_t(xx) + size_t(yy) * w]) != in)
                            best = std::min(best, std::hypot((x - xx) * su, (y - yy) * sv));
                const double expected = best >= 1e29 ? (in ? -1e10 : 1e10) : (in ? -best : best);
                exact = std::abs(sdf[size_t(x) + size_t(y) * w] - expected) < 1e-3 * std::max(1.0, std::abs(expected));
            }
    }
    check(exact, "signed distance equals brute force on anisotropic random shapes");

    const std::array<unsigned, 3> dims{kN, kN, kSlices};
    const std::array<double, 3> spacing{0.5, 0.5, 1.0};

    std::vector<int> same(size_t(kN) * kN * kSlices, 0);
    disc(same, 0, 20, 20, 6, 4);
    disc(same, 20, 20, 20, 6, 4);
    slice_fill::Result result = slice_fill::fillBetweenSlices(same, dims, spacing, 4, 2);
    std::vector<unsigned char> reference(size_t(kN) * kN, 0);
    for (unsigned y = 0; y < kN; ++y)
        for (unsigned x = 0; x < kN; ++x)
            reference[size_t(x) + size_t(y) * kN] = same[at(x, y, 0)] == 4;
    bool carried = result.gaps.size() == 1 && result.filledSlices == 19;
    for (unsigned z = 1; z < 20 && carried; ++z)
        carried = filledOn(result, z) == reference;
    check(carried, "the same disc on slices 0 and 20 is carried through 1 to 19 exactly");

    std::vector<int> growing(size_t(kN) * kN * kSlices, 0);
    disc(growing, 0, 20, 20, 4, 4);
    disc(growing, 20, 20, 20, 12, 4);
    result = slice_fill::fillBetweenSlices(growing, dims, spacing, 4, 2);
    const double meanArea = M_PI * 8 * 8;
    check(std::abs(area(filledOn(result, 10)) - meanArea) / meanArea < 0.08, "a 4-to-12 voxel disc is 8 voxels halfway");

    // Overlapping outlines: a disc moving from x 15 to x 25 is at x 20 halfway.
    std::vector<int> moving(size_t(kN) * kN * kSlices, 0);
    disc(moving, 0, 15, 20, 8, 4);
    disc(moving, 20, 25, 20, 8, 4);
    result = slice_fill::fillBetweenSlices(moving, dims, spacing, 4, 2);
    const std::vector<unsigned char> half = filledOn(result, 10);
    double sx = 0, n = 0;
    for (unsigned y = 0; y < kN; ++y)
        for (unsigned x = 0; x < kN; ++x)
            if (half[size_t(x) + size_t(y) * kN])
            {
                sx += x;
                n += 1;
            }
    check(n > 0 && std::abs(sx / n - 20.0) < 0.5, "a disc moving from x 15 to x 25 is at x 20 halfway");

    // The documented limit: outlines that do not overlap blend to nothing in between.
    std::vector<int> apart(size_t(kN) * kN * kSlices, 0);
    disc(apart, 0, 10, 20, 5, 4);
    disc(apart, 20, 30, 20, 5, 4);
    result = slice_fill::fillBetweenSlices(apart, dims, spacing, 4, 2);
    check(area(filledOn(result, 10)) == 0, "outlines that do not overlap leave the middle slice empty");

    std::vector<int> three(size_t(kN) * kN * kSlices, 0);
    disc(three, 0, 20, 20, 5, 4);
    disc(three, 5, 20, 20, 5, 4);
    disc(three, 6, 20, 20, 5, 4);
    disc(three, 15, 20, 20, 5, 4);
    disc(three, 10, 5, 5, 2, 9); // another label, not a key slice of 4
    result = slice_fill::fillBetweenSlices(three, dims, spacing, 4, 2);
    check(result.gaps.size() == 2 && result.gaps[0].from == 0 && result.gaps[0].to == 5 && result.gaps[1].from == 6 &&
              result.gaps[1].to == 15 && result.filledSlices == 4 + 8,
          "gaps between every pair of drawn slices; adjacent ones are not gaps");
    bool fresh = true;
    for (size_t index : result.voxels)
        fresh = fresh && three[index] != 4;
    check(fresh, "no voxel already holding the label is listed");

    std::vector<int> sagittal(size_t(kN) * kN * kSlices, 0);
    for (unsigned z = 5; z < 15; ++z)
        for (unsigned y = 15; y < 25; ++y)
        {
            sagittal[at(4, y, z)] = 4;
            sagittal[at(30, y, z)] = 4;
        }
    result = slice_fill::fillBetweenSlices(sagittal, dims, spacing, 4, 0);
    check(result.gaps.size() == 1 && result.filledSlices == 25 && result.voxels.size() == 25u * 10 * 10,
          "along x, a 10 x 10 square on x 4 and x 30 fills the 25 slices between");

    check(slice_fill::fillBetweenSlices(std::vector<int>(5, 4), dims, spacing, 4, 2).voxels.empty(),
          "a buffer of the wrong size fills nothing");

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
