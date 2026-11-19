// Fill between slices in the window: a label drawn on two axial slices is carried through the
// slices between as one undo step, and the fill writes like a brush stroke, so a locked label
// and "Over: Background only" are respected.
#include "ManualSeedSelector.h"

#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>

#include <itkImage.h>
#include <itkImageFileWriter.h>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-66s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

constexpr unsigned kDim = 30;
constexpr unsigned kDimZ = 12;
using VolumeType = itk::Image<int32_t, 3>;

template <typename ValueAt>
bool writeVolume(const QString &path, ValueAt valueAt)
{
    VolumeType::Pointer image = VolumeType::New();
    VolumeType::SizeType size;
    size[0] = kDim;
    size[1] = kDim;
    size[2] = kDimZ;
    VolumeType::IndexType start;
    start.Fill(0);
    image->SetRegions(VolumeType::RegionType(start, size));
    image->Allocate();
    for (unsigned z = 0; z < kDimZ; ++z)
        for (unsigned y = 0; y < kDim; ++y)
            for (unsigned x = 0; x < kDim; ++x)
            {
                VolumeType::IndexType voxel;
                voxel[0] = static_cast<VolumeType::IndexValueType>(x);
                voxel[1] = static_cast<VolumeType::IndexValueType>(y);
                voxel[2] = static_cast<VolumeType::IndexValueType>(z);
                image->SetPixel(voxel, valueAt(x, y, z));
            }
    try
    {
        auto writer = itk::ImageFileWriter<VolumeType>::New();
        writer->SetFileName(path.toStdString());
        writer->SetInput(image);
        writer->Update();
    }
    catch (...)
    {
        return false;
    }
    return true;
}

bool inDisc(unsigned x, unsigned y)
{
    const int dx = int(x) - 15;
    const int dy = int(y) - 15;
    return dx * dx + dy * dy <= 36;
}

// Label 5: a whole disc on slices 2 and 9. Label 1: a bar crossing the disc on every other
// slice, which the fill has to paint over or leave.
int maskAt(unsigned x, unsigned y, unsigned z)
{
    if ((z == 2 || z == 9) && inDisc(x, y))
        return 5;
    if (y == 15 && x >= 12 && x <= 18)
        return 1;
    return 0;
}

size_t at(unsigned x, unsigned y, unsigned z)
{
    return size_t(x) + size_t(y) * kDim + size_t(z) * kDim * kDim;
}

// True when every disc voxel of slices 3 to 8 holds 5, except the bar's, which hold `bar`.
bool filledAround(const std::vector<int> &labels, int bar)
{
    for (unsigned z = 3; z <= 8; ++z)
        for (unsigned y = 0; y < kDim; ++y)
            for (unsigned x = 0; x < kDim; ++x)
            {
                const int expected = (y == 15 && x >= 12 && x <= 18) ? bar : (inDisc(x, y) ? 5 : 0);
                if (labels[at(x, y, z)] != expected)
                    return false;
            }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QTemporaryDir workDir;
    if (!workDir.isValid())
        return 1;
    const QDir dir(workDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.filePath("settings"));
    qputenv("ROIFT_AUTOSAVE_DIR", dir.filePath("autosave").toLocal8Bit());
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("roift-test");
    QCoreApplication::setApplicationName("slice_fill_gui_test");

    const QString ct = dir.filePath("fill_0000.nii.gz");
    const QString mask = dir.filePath("fill_mask.nii.gz");
    check(writeVolume(ct, [](unsigned x, unsigned y, unsigned z) { return int(x + 2 * y + z); }) && writeVolume(mask, maskAt),
          "test data written");
    ManualSeedSelector window("");
    window.addImagesFromPaths({ct});
    check(window.applyMaskFromPath(mask.toStdString()), "the mask is edited");
    const std::vector<int> start = window.activeMaskLabels();
    const MaskEditHistory &history = window.maskEditHistory();

    const long long changed = window.fillBetweenSlices(5, 2);
    check(changed > 0 && filledAround(window.activeMaskLabels(), 5), "the disc fills slices 3 to 8, over the bar");
    check(history.undoCount() == 1, "in one undo step");
    window.undoMaskEdit();
    check(window.activeMaskLabels() == start, "which undoes back to the two drawn slices");

    window.setPaintOver(ManualSeedSelector::PaintOver::Background);
    window.fillBetweenSlices(5, 2);
    check(filledAround(window.activeMaskLabels(), 1), "Over: Background only leaves the bar");
    window.undoMaskEdit();
    window.setPaintOver(ManualSeedSelector::PaintOver::AnyUnlocked);

    window.setLabelLocked(1, true);
    window.fillBetweenSlices(5, 2);
    check(filledAround(window.activeMaskLabels(), 1), "a locked bar is left as it is");
    window.undoMaskEdit();
    window.setLabelLocked(1, false);

    check(window.fillBetweenSlices(5, 0) == 0, "along x the disc has no gap to fill");
    check(window.fillBetweenSlices(7, 2) == 0 && window.activeMaskLabels() == start, "an absent label fills nothing");

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
