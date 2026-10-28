#include "SeedSliceIndex.h"

namespace
{

// Indexed by SeedSliceIndex::Axis.
constexpr int Seed::*kCoordinate[3] = {&Seed::x, &Seed::y, &Seed::z};

} // namespace

bool SeedSliceIndex::sync(const std::vector<Seed> &seeds)
{
    // A shorter list cannot be an append, and an index kept for it would point past its end.
    if (!m_valid || seeds.size() < m_indexed)
    {
        rebuild(seeds);
        return true;
    }
    if (seeds.size() == m_indexed)
        return false;
    for (std::size_t i = m_indexed; i < seeds.size(); ++i)
        add(seeds[i], static_cast<std::uint32_t>(i));
    m_indexed = seeds.size();
    ++m_revision;
    return true;
}

const std::vector<std::uint32_t> &SeedSliceIndex::onSlice(Axis axis, int slice) const
{
    static const std::vector<std::uint32_t> none;
    const auto &slices = m_slices[axis];
    if (slice < 0 || static_cast<std::size_t>(slice) >= slices.size())
        return none;
    return slices[static_cast<std::size_t>(slice)];
}

void SeedSliceIndex::rebuild(const std::vector<Seed> &seeds)
{
    // Counted, then filled through raw pointers: a debug build makes every operator[] and
    // push_back a call, and filling with push_back took 65 ms for 1.2M seeds.
    const Seed *const begin = seeds.data();
    const Seed *const end = begin + seeds.size();

    std::vector<std::uint32_t> counts[3];
    std::uint32_t *count[3] = {nullptr, nullptr, nullptr};
    int extent[3] = {0, 0, 0};
    for (const Seed *s = begin; s != end; ++s)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            const int c = s->*kCoordinate[axis];
            if (c < 0)
                continue;
            if (c >= extent[axis])
            {
                extent[axis] = c + 1;
                counts[axis].resize(static_cast<std::size_t>(extent[axis]), 0);
                count[axis] = counts[axis].data();
            }
            ++count[axis][c];
        }
    }

    std::vector<std::uint32_t *> cursors[3];
    std::uint32_t **cursor[3] = {nullptr, nullptr, nullptr};
    for (int axis = 0; axis < 3; ++axis)
    {
        auto &slices = m_slices[axis];
        slices.clear();
        slices.resize(static_cast<std::size_t>(extent[axis]));
        cursors[axis].resize(slices.size());
        for (std::size_t c = 0; c < slices.size(); ++c)
        {
            slices[c].resize(count[axis][c]);
            cursors[axis][c] = slices[c].data();
        }
        cursor[axis] = cursors[axis].data();
    }
    std::uint32_t index = 0;
    for (const Seed *s = begin; s != end; ++s, ++index)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            const int c = s->*kCoordinate[axis];
            if (c >= 0)
                *cursor[axis][c]++ = index;
        }
    }
    m_indexed = seeds.size();
    m_valid = true;
    ++m_revision;
}

void SeedSliceIndex::add(const Seed &seed, std::uint32_t index)
{
    for (int axis = 0; axis < 3; ++axis)
    {
        // A negative coordinate is on no slice a view can show.
        const int c = seed.*kCoordinate[axis];
        if (c < 0)
            continue;
        auto &slices = m_slices[axis];
        if (static_cast<std::size_t>(c) >= slices.size())
            slices.resize(static_cast<std::size_t>(c) + 1);
        slices[static_cast<std::size_t>(c)].push_back(index);
    }
}
