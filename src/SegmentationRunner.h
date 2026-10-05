#pragma once

#include <QString>
#include <string>

class ManualSeedSelector;

namespace SegmentationRunner
{
    // Show the segmentation dialog and run ROIFT (single or per-label batch).
    // The function uses public APIs on ManualSeedSelector to read seeds, image path
    // and to apply/load generated masks.
    void showSegmentationDialog(ManualSeedSelector *parent);

    // Run segmentation using parameters from the main window UI
    void runSegmentation(ManualSeedSelector *parent);

    // The standard CPU oiftrelax, searched for where a volume run searches (ROIFT_EXECUTABLE,
    // PATH, the build and install folders), or an empty string when ROIFT_EXECUTABLE names
    // another binary or none is found; whyNot then says which, in one line.
    QString resolveCpuRoiftExecutable(QString *whyNot = nullptr);
}
