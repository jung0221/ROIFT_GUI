// Checks SeedSliceIndex against a scan of the whole list: the seeds each slice view draws,
// in the order it draws them, after a build, an append, and an edit that invalidates.
#include "SeedSliceIndex.h"

#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-62s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

int coordinate(const Seed &s, SeedSliceIndex::Axis axis)
{
    return axis == SeedSliceIndex::AxisX ? s.x : axis == SeedSliceIndex::AxisY ? s.y : s.z;
}

// Every slice of every axis, up to @p extent, holds exactly the scan's indices in list order.
bool matchesScan(const SeedSliceIndex &index, const std::vector<Seed> &seeds, int extent)
{
    for (SeedSliceIndex::Axis axis : {SeedSliceIndex::AxisX, SeedSliceIndex::AxisY, SeedSliceIndex::AxisZ})
    {
        for (int slice = -1; slice <= extent; ++slice)
        {
            std::vector<std::uint32_t> scan;
            for (std::size_t i = 0; i < seeds.size(); ++i)
                if (coordinate(seeds[i], axis) == slice && slice >= 0)
                    scan.push_back(static_cast<std::uint32_t>(i));
            if (index.onSlice(axis, slice) != scan)
                return false;
        }
    }
    return true;
}

Seed seedAt(int x, int y, int z, int internal)
{
    Seed s;
    s.x = x;
    s.y = y;
    s.z = z;
    s.label = internal ? 1 : 0;
    s.internal = internal;
    return s;
}

} // namespace

int main()
{
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> coord(0, 15);
    std::vector<Seed> seeds;
    for (int i = 0; i < 2000; ++i)
        seeds.push_back(seedAt(coord(rng), coord(rng), coord(rng), i % 9 == 0));

    SeedSliceIndex index;
    check(index.sync(seeds) && matchesScan(index, seeds, 17), "first sync matches a scan on every slice");
    const std::uint64_t built = index.revision();
    check(!index.sync(seeds) && index.revision() == built, "unchanged list: no work, revision kept");

    seeds.push_back(seedAt(3, 4, 40, 1)); // past every slice built so far
    seeds.push_back(seedAt(3, 4, 5, 1));
    check(index.sync(seeds) && index.revision() != built && matchesScan(index, seeds, 41),
          "appended seeds are added, beyond the old extent too");

    seeds.erase(seeds.begin() + 10, seeds.begin() + 500);
    index.invalidate();
    check(index.sync(seeds) && matchesScan(index, seeds, 41), "removal after invalidate() rebuilds");

    // A shorter list is rebuilt even without invalidate(): the old indices would run past its end.
    seeds.resize(100);
    check(index.sync(seeds) && matchesScan(index, seeds, 41), "shrunk list rebuilds on its own");

    seeds.clear();
    index.invalidate();
    check(index.sync(seeds) && index.onSlice(SeedSliceIndex::AxisZ, 5).empty(), "cleared list leaves every slice empty");

    seeds = {seedAt(-1, 2, 3, 1), seedAt(1, 2, 3, 0)};
    index.invalidate();
    index.sync(seeds);
    check(index.onSlice(SeedSliceIndex::AxisX, -1).empty() &&
              index.onSlice(SeedSliceIndex::AxisZ, 3) == std::vector<std::uint32_t>{0, 1} &&
              index.onSlice(SeedSliceIndex::AxisZ, 1000).empty(),
          "negative coordinate on no slice of its axis; out of range empty");

    return failures ? 1 : 0;
}
