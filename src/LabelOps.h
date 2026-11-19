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

} // namespace label_ops
