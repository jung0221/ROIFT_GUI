// Outlines only: the slice views draw just the edge of each label, so the image stays visible
// inside it; off, the label is filled as before. Reads the composed axial slice back.
#include "ManualSeedSelector.h"
#include "OrthogonalView.h"

#include <QApplication>
#include <QCheckBox>
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

constexpr unsigned kDim = 24;
constexpr unsigned kDimZ = 4;
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

bool grey(const QColor &c)
{
    return c.red() == c.green() && c.green() == c.blue();
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
    QCoreApplication::setApplicationName("mask_outline_gui_test");

    const QString ct = dir.filePath("outline_0000.nii.gz");
    const QString mask = dir.filePath("outline_mask.nii.gz");
    const bool wrote = writeVolume(ct, [](unsigned x, unsigned y, unsigned) { return int(x * 3 + y); }) &&
                       writeVolume(mask, [](unsigned x, unsigned y, unsigned)
                                   { return (x >= 6 && x < 16 && y >= 6 && y < 16) ? 1 : 0; });
    check(wrote, "test data written");
    ManualSeedSelector window("");
    window.addImagesFromPaths({ct});
    check(window.applyMaskFromPath(mask.toStdString()), "a 10 x 10 square is the edited, drawn mask");
    auto *axial = window.findChild<OrthogonalView *>("axialView");
    auto *box = window.findChild<QCheckBox *>("maskOutlineOnly");
    check(axial && box, "controls found");
    if (!axial || !box)
        return 1;

    QImage slice = axial->image();
    check(!grey(slice.pixelColor(10, 10)) && !grey(slice.pixelColor(6, 10)), "filled: inside and edge are tinted");
    check(grey(slice.pixelColor(3, 3)), "and background is not");

    box->click();
    check(window.maskOutlineOnly(), "the box turns outlines on");
    slice = axial->image();
    check(grey(slice.pixelColor(10, 10)), "outlines: the inside shows the image");
    check(!grey(slice.pixelColor(6, 10)) && !grey(slice.pixelColor(15, 10)) && !grey(slice.pixelColor(10, 6)) &&
              !grey(slice.pixelColor(10, 15)),
          "and all four edges are drawn");
    check(grey(slice.pixelColor(7, 10)), "one voxel in from the edge is not");

    box->click();
    check(!grey(axial->image().pixelColor(10, 10)), "off again, the inside is tinted");

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
