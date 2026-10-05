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

    // The standard CPU oiftrelax, resolved as a volume run resolves it, or an empty
    // string when only a GPU binary is found or ROIFT_EXECUTABLE names one.
    QString resolveCpuRoiftExecutable();
}
