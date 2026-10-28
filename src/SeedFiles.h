#pragma once

#include "Seed.h"

#include <QString>
#include <string>
#include <vector>

// Seed files for oiftrelax: a count line, then `x y z <col4> <col5>`; the binary reads column 5.
// multilabel: col4 = col5 = max(0, label); legacy: col4 = label, col5 = 1 for internalLabel, else 0.
std::vector<Seed> dedupeSeedsKeepingLatest(const std::vector<Seed> &seeds);
// One line `x y z <col4> <col5> [# note]`. The note is for the viewer only: oiftrelax's fscanf
// cannot skip it, so the runners always write a fresh file without notes.
bool parseSeedLine(const std::string &line, Seed &seed);
bool writeMultilabelSeedFile(const QString &seedFilePath, const std::vector<Seed> &filteredSeeds);
bool writeLegacySeedFile(const QString &seedFilePath, const std::vector<Seed> &filteredSeeds, int internalLabel);
