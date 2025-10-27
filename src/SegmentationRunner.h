#pragma once

#include <string>

class ManualSeedSelector;

namespace SegmentationRunner {
    // Show the segmentation dialog and run ROIFT (single or per-label batch).
    // The function uses public APIs on ManualSeedSelector to read seeds, image path
    // and to apply/load generated masks.
    void showSegmentationDialog(ManualSeedSelector *parent);
}
