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

    // Morphology on a hollow cube: a 7 x 7 x 7 shell of label 5 around a 5 x 5 x 5 cavity whose
    // centre holds label 6 (a pulp in a tooth).
    std::vector<int> shell(labels.size(), 0);
    for (unsigned z = 1; z <= 7; ++z)
        for (unsigned y = 1; y <= 7; ++y)
            for (unsigned x = 1; x <= 7; ++x)
            {
                const bool wall = x == 1 || x == 7 || y == 1 || y == 7 || z == 1 || z == 7;
                shell[at(x, y, z)] = wall ? 5 : (x == 4 && y == 4 && z == 4 ? 6 : 0);
            }
    check(label_ops::holes(shell, dims, 5, -1).size() == 125 - 1, "3D holes: the cavity, without the pulp in it");
    std::vector<int> open = shell;
    open[at(4, 4, 1)] = 0; // a tunnel through the bottom wall
    check(label_ops::holes(open, dims, 5, -1).empty(), "a cavity open to the outside is no 3D hole");
    // Slice by slice: the five inner slices (125 voxels less the pulp), and the tunnel voxel,
    // which its own slice of the bottom wall encloses.
    check(label_ops::holes(open, dims, 5, 2).size() == 125 - 1 + 1, "slice by slice the open cavity is a hole on every slice");

    std::vector<int> dot(labels.size(), 0);
    dot[at(5, 5, 5)] = 8;
    check(label_ops::dilation(dot, dims, 8).size() == 6, "growing one voxel adds its six face neighbours");
    std::vector<int> cube(labels.size(), 0);
    for (unsigned z = 2; z < 5; ++z)
        for (unsigned y = 2; y < 5; ++y)
            for (unsigned x = 2; x < 5; ++x)
                cube[at(x, y, z)] = 8;
    check(label_ops::erosion(cube, dims, 8).size() == 26, "shrinking a 3 x 3 x 3 cube leaves its centre");
    std::vector<int> edge(labels.size(), 0);
    edge[at(0, 0, 0)] = 8;
    check(label_ops::erosion(edge, dims, 8).size() == 1, "outside the volume counts as not the label");

    std::vector<int> block(labels.size(), 0);
    for (unsigned z = 2; z < 8; ++z)
        for (unsigned y = 2; y < 8; ++y)
            for (unsigned x = 2; x < 8; ++x)
                block[at(x, y, z)] = 8;
    block[at(8, 5, 5)] = 8;   // a one-voxel spike
    block[at(5, 5, 7)] = 0;   // a one-voxel notch in the top face
    const label_ops::Smoothing smooth = label_ops::majority(block, dims, 8);
    const bool spikeGone = std::find(smooth.remove.begin(), smooth.remove.end(), at(8, 5, 5)) != smooth.remove.end();
    const bool notchFilled = std::find(smooth.add.begin(), smooth.add.end(), at(5, 5, 7)) != smooth.add.end();
    check(spikeGone && notchFilled, "the majority vote removes a spike and fills a notch");

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
