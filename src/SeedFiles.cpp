#include "SeedFiles.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <unordered_set>

std::vector<Seed> dedupeSeedsKeepingLatest(const std::vector<Seed> &seeds)
{
    std::vector<Seed> filtered;
    filtered.reserve(seeds.size());
    std::unordered_set<std::string> seen;
    for (int i = static_cast<int>(seeds.size()) - 1; i >= 0; --i)
    {
        const Seed &s = seeds[static_cast<size_t>(i)];
        const std::string key = std::to_string(s.x) + ":" + std::to_string(s.y) + ":" + std::to_string(s.z);
        if (seen.find(key) != seen.end())
            continue;
        seen.insert(key);
        filtered.push_back(s);
    }
    std::reverse(filtered.begin(), filtered.end());
    return filtered;
}

bool writeMultilabelSeedFile(const QString &seedFilePath, const std::vector<Seed> &filteredSeeds)
{
    std::ofstream ofs(seedFilePath.toStdString());
    if (!ofs)
        return false;

    ofs << filteredSeeds.size() << "\n";
    for (const auto &seed : filteredSeeds)
    {
        const int labelId = std::max(0, seed.label);
        ofs << seed.x << " " << seed.y << " " << seed.z << " " << labelId << " " << labelId << "\n";
    }
    return true;
}

bool writeLegacySeedFile(const QString &seedFilePath, const std::vector<Seed> &filteredSeeds, int internalLabel)
{
    std::ofstream ofs(seedFilePath.toStdString());
    if (!ofs)
        return false;

    ofs << filteredSeeds.size() << "\n";
    for (const auto &seed : filteredSeeds)
    {
        const int internalFlag = (seed.label == internalLabel) ? 1 : 0;
        ofs << seed.x << " " << seed.y << " " << seed.z << " " << seed.label << " " << internalFlag << "\n";
    }
    return true;
}
