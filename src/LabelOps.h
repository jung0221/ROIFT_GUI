#pragma once

#include <array>
#include <cstddef>
#include <vector>

// Connected pieces of one label of an x-fastest label volume, free of the window. Pieces are
// 26-connected: voxels touching at an edge or a corner belong together, so a thin oblique
// structure (a canal, a root apex) is one piece and not a chain of islands.
namespace label_ops
{

struct Piece
{
    std::size_t seed = 0; // the piece's first voxel in x-fastest order
    long long voxels = 0;
};

// The pieces of label, largest first (ties by seed). Uses a bit per voxel of scratch memory.
std::vector<Piece> pieces(const std::vector<int> &labels, const std::array<unsigned, 3> &dims, int label);

// Every voxel of the piece containing seed, which must hold the label of that piece.
std::vector<std::size_t> pieceAt(const std::vector<int> &labels, const std::array<unsigned, 3> &dims,
                                 std::size_t seed);

// Morphology of one label. Each works inside the label's bounding box plus one voxel, so its
// cost follows the structure, not the volume. Neighbours are the six face neighbours unless
// said otherwise; outside the volume counts as not the label.

// Background voxels the label encloses: not reachable from outside without crossing the
// label. planeAxis < 0 in 3D; 0, 1 or 2 slice by slice across that axis. Voxels of other labels
// inside (a pulp in a tooth) are never listed.
std::vector<std::size_t> holes(const std::vector<int> &labels, const std::array<unsigned, 3> &dims, int label,
                               int planeAxis);
// Voxels not holding the label with a face neighbour that does.
std::vector<std::size_t> dilation(const std::vector<int> &labels, const std::array<unsigned, 3> &dims, int label);
// Voxels of the label with a face neighbour that does not hold it.
std::vector<std::size_t> erosion(const std::vector<int> &labels, const std::array<unsigned, 3> &dims, int label);
// A 3 x 3 x 3 majority vote on the label: add are voxels outside it with at least 14 of their
// 27 holding it, remove voxels of it with at most 13.
struct Smoothing
{
    std::vector<std::size_t> add;
    std::vector<std::size_t> remove;
};
Smoothing majority(const std::vector<int> &labels, const std::array<unsigned, 3> &dims, int label);

} // namespace label_ops
