#pragma once

#include "Seed.h"

#include <QString>
#include <vector>

// Seed files for oiftrelax: a count line, then `x y z <col4> <col5>`; the binary reads
// column 5 as the label (ctsegmentation/docs/en/reference/seed-files.md).
std::vector<Seed> dedupeSeedsKeepingLatest(const std::vector<Seed> &seeds);
bool writeMultilabelSeedFile(const QString &seedFilePath, const std::vector<Seed> &filteredSeeds);
bool writeLegacySeedFile(const QString &seedFilePath, const std::vector<Seed> &filteredSeeds, int internalLabel);
