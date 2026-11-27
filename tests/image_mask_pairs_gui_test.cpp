// Drives attachFilesToImage() the way main() does for `-i a -m ... -i b -m ... -s ...`:
// the image on screen opens its masks at once, another image opens its own the first
// time it is shown (here through a background read, as in the running app), and a
// later visit does not open them again.
#include "ManualSeedSelector.h"
#include "UiUtils.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QListWidget>
#include <QTemporaryDir>

#include <itkImage.h>
#include <itkImageFileWriter.h>
#include <itkImageRegionIteratorWithIndex.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-70s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

using VolumeType = itk::Image<int16_t, 3>;

// An 8 x 6 x depth volume; voxels on the left half take @p value.
bool writeVolume(const QString &path, unsigned int depth, int16_t value)
{
    auto image = VolumeType::New();
    VolumeType::SizeType size;
    size[0] = 8;
    size[1] = 6;
    size[2] = depth;
    VolumeType::RegionType region;
    region.SetSize(size);
    image->SetRegions(region);
    image->Allocate();
    itk::ImageRegionIteratorWithIndex<VolumeType> it(image, region);
    for (it.GoToBegin(); !it.IsAtEnd(); ++it)
        it.Set(it.GetIndex()[0] < 4 ? value : 0);
    try
    {
        auto writer = itk::ImageFileWriter<VolumeType>::New();
        writer->SetFileName(path.toStdString());
        writer->SetInput(image);
        writer->Update();
    }
    catch (const std::exception &e)
    {
        std::printf("failed to write %s: %s\n", qPrintable(path), e.what());
        return false;
    }
    return true;
}

QString key(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

int rowForPath(QListWidget *list, const QString &path)
{
    for (int row = 0; row < list->count(); ++row)
        if (key(list->item(row)->data(UiUtils::kPathRole).toString()) == key(path))
            return row;
    return -1;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // Images and masks in separate folders: the folder scan would otherwise list every
    // NIfTI beside an image as one of its masks, and hide what the pairing did.
    QTemporaryDir dir;
    if (!dir.isValid() || !QDir(dir.path()).mkpath("a") || !QDir(dir.path()).mkpath("b") ||
        !QDir(dir.path()).mkpath("masks"))
    {
        std::printf("no temporary directory\n");
        return 1;
    }
    const QString a = dir.filePath("a/case_a.nii.gz");
    const QString b = dir.filePath("b/case_b.nii.gz");
    const QString aFirst = dir.filePath("masks/case_a_first.nii.gz");
    const QString aSecond = dir.filePath("masks/case_a_second.nii.gz");
    const QString bOnly = dir.filePath("masks/case_b_only.nii.gz");
    const QString bSeeds = dir.filePath("masks/case_b_seeds.txt");
    bool wrote = writeVolume(a, 12, 100) && writeVolume(b, 3, 100) && writeVolume(aFirst, 12, 1) &&
                 writeVolume(aSecond, 12, 2) && writeVolume(bOnly, 3, 1);
    {
        std::ofstream seeds(bSeeds.toStdString());
        seeds << "2\n1 1 1 1 1\n6 4 2 0 0\n";
        wrote = wrote && seeds.good();
    }
    check(wrote, "fixtures written");
    if (!wrote)
        return 1;

    ManualSeedSelector window("");
    QListWidget *images = window.findChild<QListWidget *>("imageList");
    QListWidget *masks = window.findChild<QListWidget *>("maskList");
    check(images && masks, "image and mask lists found");
    if (!images || !masks)
        return 1;

    // main(): the images first, read inline, then each image's files.
    window.addImagesFromPaths({a, b});
    check(window.hasImage() && window.getImagePath() == key(a).toStdString(), "the first image is on screen");
    check(window.attachFilesToImage(a, {aFirst, aSecond}, {}), "the first image takes its two masks");
    check(window.attachFilesToImage(b, {bOnly}, bSeeds), "the second takes its mask and seed file");
    check(!window.attachFilesToImage(dir.filePath("a/absent.nii.gz"), {aFirst}, {}),
          "an image that is not in the list is refused");

    check(window.activeMaskPath() == key(aSecond), "the image on screen opens its masks at once, the last edited");
    check(rowForPath(masks, aFirst) >= 0 && rowForPath(masks, aSecond) >= 0, "both are listed under Masks");
    check(rowForPath(masks, bOnly) < 0, "the other image's mask is not");
    check(window.getSeeds().empty(), "nor are its seeds loaded");

    // As in the running app, a selection now reads on a worker thread.
    window.setBackgroundImageLoading(true);
    images->setCurrentRow(rowForPath(images, b));
    window.waitForImageLoad();
    check(window.getImagePath() == key(b).toStdString(), "the second image is on screen");
    check(window.activeMaskPath() == key(bOnly), "it opens its own mask when shown");
    check(rowForPath(masks, bOnly) >= 0 && rowForPath(masks, aFirst) < 0, "and lists only that one");
    check(window.getSeeds().size() == 2, "it loads its seed file");

    images->setCurrentRow(rowForPath(images, a));
    window.waitForImageLoad();
    check(window.getImagePath() == key(a).toStdString(), "back on the first image");
    check(rowForPath(masks, aFirst) >= 0 && rowForPath(masks, aSecond) >= 0, "its masks are still listed");
    check(window.activeMaskPath().isEmpty(), "but a second visit does not open them again");

    std::printf("%s\n", failures == 0 ? "all checks passed" : "some checks FAILED");
    return failures == 0 ? 0 : 1;
}
