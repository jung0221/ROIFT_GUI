#pragma once

#include "PlanarSlice.h"
#include "Seed.h"

#include <QString>
#include <array>
#include <functional>
#include <vector>

namespace planar
{

struct RunRequest
{
    Geometry geometry;
    std::vector<float> pixels;               // width * height, as extractPlane returns, window applied
    std::array<double, 2> spacing{1.0, 1.0}; // (du, dv)
    std::vector<Seed> seeds;                 // in plane coordinates (u, v, 0); on a repeated pixel the last wins
    bool borderBackground = true;
    double pol = 1.0;
    int niter = 1;
    int percentile = 0;
    int blurPasses = 2;
    QString executablePath;
    QString workDir;                         // caller-owned temporary directory
    std::function<bool()> cancelled;         // polled while oiftrelax runs; empty = never
};

struct RunResult
{
    bool success = false;
    QString message;
    QString commandLine;
    std::vector<int> labels;                 // width * height, values as written by oiftrelax
};

// Writes the plane as a (width, height, 1) int32 NIfTI and the seeds beside it, runs
// oiftrelax with face seeding off, and reads the label plane back. The file names are
// fixed, so one run at a time per workDir. A failure message's first line is a summary.
RunResult segmentPlane(const RunRequest &request);

} // namespace planar
