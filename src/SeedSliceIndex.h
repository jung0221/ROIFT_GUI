#pragma once

#include "Seed.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// The seeds on each slice of each plane, as indices into the seed list, so a slice view visits
// the seeds on its own slice rather than the whole list on every paint and pointer move.
class SeedSliceIndex
{
public:
    // The coordinate a plane holds fixed: sagittal x, coronal y, axial z.
    enum Axis
    {
        AxisX = 0,
        AxisY = 1,
        AxisZ = 2
    };

    // Brings the index up to @p seeds. Seeds appended since the last call are added; any other
    // edit of the list (removal, clear, reload) needs invalidate() first. True if the index changed.
    bool sync(const std::vector<Seed> &seeds);
    void invalidate() { m_valid = false; }
    // Indices of the seeds whose @p axis coordinate is @p slice, ascending; valid after sync().
    const std::vector<std::uint32_t> &onSlice(Axis axis, int slice) const;
    // Moves whenever sync() changes the index, so a caller can cache what it derived from the list.
    std::uint64_t revision() const { return m_revision; }

private:
    void rebuild(const std::vector<Seed> &seeds);
    void add(const Seed &seed, std::uint32_t index);

    std::array<std::vector<std::vector<std::uint32_t>>, 3> m_slices;
    std::size_t m_indexed = 0;
    bool m_valid = false;
    std::uint64_t m_revision = 0;
};
