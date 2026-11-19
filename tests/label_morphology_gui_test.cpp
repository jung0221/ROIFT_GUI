// Morphology of a label in the window: holes filled around a label inside (a pulp in a tooth),
// growing that stops at what "Over" protects, shrinking, smoothing; each one undo step.
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

constexpr unsigned kDim = 16;
using VolumeType = itk::Image<int32_t, 3>;

template <typename ValueAt>
bool writeVolume(const QString &path, ValueAt valueAt)
{
    VolumeType::Pointer image = VolumeType::New();
    VolumeType::SizeType size;
    size.Fill(kDim);
    VolumeType::IndexType start;
    start.Fill(0);
    image->SetRegions(VolumeType::RegionType(start, size));
    image->Allocate();
    for (unsigned z = 0; z < kDim; ++z)
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

// Label 5: the wall of a 7-voxel cube (x, y, z in 2..8) around a cavity holding a pulp (6) at
// its centre. Label 7: a slab touching the cube's x = 8 face.
int maskAt(unsigned x, unsigned y, unsigned z)
{
    const bool inCube = x >= 2 && x <= 8 && y >= 2 && y <= 8 && z >= 2 && z <= 8;
    if (inCube)
    {
        const bool wall = x == 2 || x == 8 || y == 2 || y == 8 || z == 2 || z == 8;
        if (wall)
            return 5;
        return (x == 5 && y == 5 && z == 5) ? 6 : 0;
    }
    if (x == 9 && y >= 2 && y <= 8 && z >= 2 && z <= 8)
        return 7;
    return 0;
}

long long count(const std::vector<int> &labels, int label)
{
    long long n = 0;
    for (int v : labels)
        n += v == label;
    return n;
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
    QCoreApplication::setApplicationName("label_morphology_gui_test");

    const QString ct = dir.filePath("morph_0000.nii.gz");
    const QString mask = dir.filePath("morph_mask.nii.gz");
    check(writeVolume(ct, [](unsigned x, unsigned y, unsigned z) { return int(x + y + z); }) && writeVolume(mask, maskAt),
          "test data written");
    ManualSeedSelector window("");
    window.addImagesFromPaths({ct});
    check(window.applyMaskFromPath(mask.toStdString()), "the mask is edited");
    const std::vector<int> start = window.activeMaskLabels();
    const long long wall = count(start, 5);

    check(window.fillLabelHoles(5, -1) == 125 - 1, "holes: the 5^3 cavity is filled, less the pulp");
    check(count(window.activeMaskLabels(), 6) == 1 && window.maskEditHistory().undoCount() == 1,
          "the pulp is kept, in one undo step");
    window.undoMaskEdit();

    window.setPaintOver(ManualSeedSelector::PaintOver::Background);
    window.growLabel(5);
    const std::vector<int> grown = window.activeMaskLabels();
    check(count(grown, 7) == count(start, 7) && count(grown, 5) > wall, "growing under Background only keeps the slab");
    window.undoMaskEdit();
    window.setPaintOver(ManualSeedSelector::PaintOver::AnyUnlocked);
    window.growLabel(5);
    check(count(window.activeMaskLabels(), 7) < count(start, 7), "and over any unlocked label takes some of it");
    window.undoMaskEdit();

    check(window.shrinkLabel(5) == wall && count(window.activeMaskLabels(), 5) == 0,
          "shrinking a one-voxel wall clears it");
    window.undoMaskEdit();

    window.smoothLabel(6);
    check(count(window.activeMaskLabels(), 6) == 0, "smoothing a lone voxel removes it");
    window.undoMaskEdit();
    check(window.activeMaskLabels() == start, "every operation undid back to the file");

    window.setLabelLocked(5, true);
    check(window.fillLabelHoles(5, -1) == 0 && window.growLabel(5) == 0 && window.smoothLabel(5) == 0,
          "a locked label is not filled, grown or smoothed");

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
