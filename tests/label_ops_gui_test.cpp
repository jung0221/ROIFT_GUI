// Label operations on the edited mask: pieces, removing small pieces, keeping the largest,
// giving one piece another label, swapping and merging labels. Each is one undo step, and
// none changes a locked label.
#include "ManualSeedSelector.h"

#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>
#include <map>

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

constexpr unsigned kDim = 20;
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
    VolumeType::SpacingType spacing;
    spacing.Fill(0.5); // 0.125 mm^3 a voxel
    image->SetSpacing(spacing);
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

// Label 21 (a tooth): a 4 x 4 x 4 block and two loose voxels; label 11 a 2 x 2 x 2 block (its
// mirror tooth, 8 voxels); label 3 a 3-voxel bar.
int maskAt(unsigned x, unsigned y, unsigned z)
{
    if (x >= 2 && x < 6 && y >= 2 && y < 6 && z >= 2 && z < 6)
        return 21;
    if ((x == 15 && y == 15 && z == 15) || (x == 18 && y == 2 && z == 2))
        return 21;
    if (x >= 10 && x < 12 && y >= 10 && y < 12 && z >= 2 && z < 4)
        return 11;
    if (y == 18 && z == 18 && x >= 2 && x < 5)
        return 3;
    return 0;
}

std::map<int, long long> histogram(const std::vector<int> &labels)
{
    std::map<int, long long> counts;
    for (int v : labels)
        if (v != 0)
            ++counts[v];
    return counts;
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
    QCoreApplication::setApplicationName("label_ops_gui_test");

    const QString ct = dir.filePath("ops_0000.nii.gz");
    const QString mask = dir.filePath("ops_mask.nii.gz");
    check(writeVolume(ct, [](unsigned x, unsigned y, unsigned z) { return int(x + y + z); }) && writeVolume(mask, maskAt),
          "test data written");
    ManualSeedSelector window("");
    window.addImagesFromPaths({ct});
    check(window.applyMaskFromPath(mask.toStdString()), "the mask is edited");
    const std::vector<int> start = window.activeMaskLabels();
    const MaskEditHistory &history = window.maskEditHistory();

    const auto pieces = window.labelPieces(21);
    check(pieces.size() == 3 && pieces[0].voxels == 64, "label 21: a 64-voxel piece and two loose voxels");

    // 0.125 mm^3 a voxel: a 1 mm^3 limit takes the loose voxels and leaves the block.
    check(window.removeLabelIslands(21, 1.0) == 2, "removing pieces up to 1 mm^3 clears the two loose voxels");
    check(window.labelPieces(21).size() == 1 && history.undoCount() == 1, "one piece left, one undo step");
    window.undoMaskEdit();
    check(window.activeMaskLabels() == start, "undo brings them back");

    check(window.keepLargestPiece(21) == 2 && window.labelPieces(21).size() == 1, "keep largest clears the rest");
    window.undoMaskEdit();

    check(window.relabelPieceAt(10, 10, 2, 21) == 8, "a mirror-numbered tooth takes label 21 in one step");
    auto counts = histogram(window.activeMaskLabels());
    check(!counts.count(11) && counts[21] == 64 + 2 + 8, "label 11 is gone, label 21 grew by its 8 voxels");
    window.undoMaskEdit();

    check(window.swapLabels(21, 3) == 66 + 3, "swapping 21 and 3 changes all their voxels");
    counts = histogram(window.activeMaskLabels());
    check(counts[3] == 66 && counts[21] == 3, "and exchanges their counts");
    window.undoMaskEdit();

    check(window.mergeLabelInto(3, 11) == 3, "merging 3 into 11");
    counts = histogram(window.activeMaskLabels());
    check(!counts.count(3) && counts[11] == 11, "leaves no 3 and 11 more voxels of 11");
    window.undoMaskEdit();
    check(window.activeMaskLabels() == start, "every operation undid back to the file");

    window.setLabelLocked(3, true);
    check(window.swapLabels(21, 3) == 0 && window.activeMaskLabels() == start, "a swap with a locked label is refused");
    check(window.mergeLabelInto(11, 3) == 0 && window.activeMaskLabels() == start, "and so is merging into one");
    check(window.relabelPieceAt(2, 18, 18, 21) == 0, "and giving a locked piece another label");
    window.setLabelLocked(3, false);

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
