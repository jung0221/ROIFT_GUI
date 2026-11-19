// Pins label_ops: 26-connected pieces of one label, largest first, and the voxels of the piece
// holding a seed.
#include "LabelOps.h"

#include <algorithm>
#include <cstdio>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-62s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

constexpr unsigned kN = 10;

std::size_t at(unsigned x, unsigned y, unsigned z)
{
    return std::size_t(x) + std::size_t(y) * kN + std::size_t(z) * kN * kN;
}

} // namespace

int main()
{
    const std::array<unsigned, 3> dims{kN, kN, kN};
    std::vector<int> labels(std::size_t(kN) * kN * kN, 0);
    for (unsigned z = 0; z < 3; ++z)
        for (unsigned y = 0; y < 3; ++y)
            for (unsigned x = 0; x < 3; ++x)
                labels[at(x, y, z)] = 1; // a 27-voxel block
    labels[at(5, 5, 5)] = 1;
    labels[at(6, 6, 6)] = 1; // touches (5,5,5) at a corner only
    labels[at(9, 9, 9)] = 1; // alone
    labels[at(9, 0, 0)] = 2;

    const std::vector<label_ops::Piece> pieces = label_ops::pieces(labels, dims, 1);
    check(pieces.size() == 3, "three pieces of label 1");
    check(pieces.size() == 3 && pieces[0].voxels == 27 && pieces[1].voxels == 2 && pieces[2].voxels == 1,
          "largest first: 27, 2, 1");
    check(pieces.size() == 3 && pieces[1].seed == at(5, 5, 5), "a piece's seed is its first voxel");

    std::vector<std::size_t> corner = label_ops::pieceAt(labels, dims, at(6, 6, 6));
    std::sort(corner.begin(), corner.end());
    check(corner == std::vector<std::size_t>{at(5, 5, 5), at(6, 6, 6)}, "voxels touching at a corner are one piece");
    check(label_ops::pieceAt(labels, dims, at(1, 1, 1)).size() == 27, "the piece of a seed in the block is the block");
    check(label_ops::pieces(labels, dims, 2).size() == 1 && label_ops::pieces(labels, dims, 7).empty(),
          "other labels are separate; an absent label has no pieces");
    check(label_ops::pieces(std::vector<int>(5, 1), dims, 1).empty(), "a buffer of the wrong size has no pieces");

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
