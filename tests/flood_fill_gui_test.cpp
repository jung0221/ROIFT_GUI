// Fill in the window: a click in Fill mode gives the active label to the region of similar
// intensity under it, on the slice or in 3D with Ball, in one undo step; a locked label is
// refused.
#include "ManualSeedSelector.h"
#include "OrthogonalView.h"

#include <QApplication>
#include <QDir>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
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
constexpr unsigned kDimZ = 10;
using VolumeType = itk::Image<int32_t, 3>;

bool inCube(unsigned x, unsigned y, unsigned z)
{
    return x >= 6 && x < 14 && y >= 6 && y < 14 && z >= 2 && z < 8;
}

bool writeImage(const QString &path)
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
                image->SetPixel(voxel, inCube(x, y, z) ? 1000 : 0); // bone in air
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

int count(const std::vector<int> &labels, int label)
{
    int n = 0;
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
    QCoreApplication::setApplicationName("flood_fill_gui_test");

    const QString ct = dir.filePath("cube_0000.nii.gz");
    check(writeImage(ct), "test image written");
    ManualSeedSelector window("");
    window.addImagesFromPaths({ct});
    auto *axial = window.findChild<OrthogonalView *>("axialView");
    auto *slider = window.findChild<QSlider *>("axialSlider");
    auto *fill = window.findChild<QPushButton *>("maskFillButton");
    const bool found = axial && slider && fill;
    check(found, "controls found");
    if (!found)
        return 1;
    slider->setValue(4);
    window.setActiveLabel(3);
    window.setFillTolerance(100);
    fill->click();

    emit axial->mousePressed(9, 9, Qt::LeftButton);
    emit axial->mouseMoved(2, 2, Qt::LeftButton); // moves of the same press do not fill again
    emit axial->mouseReleased(2, 2, Qt::LeftButton);
    check(count(window.activeMaskLabels(), 3) == 8 * 8, "a click fills the cube's 8 x 8 section on this slice");
    check(window.maskEditHistory().undoCount() == 1, "in one undo step");
    window.undoMaskEdit();

    window.setBrushSphere(true);
    emit axial->mousePressed(9, 9, Qt::LeftButton);
    emit axial->mouseReleased(9, 9, Qt::LeftButton);
    check(count(window.activeMaskLabels(), 3) == 8 * 8 * 6, "with Ball it fills the whole cube");
    window.undoMaskEdit();
    window.setBrushSphere(false);

    emit axial->mousePressed(1, 1, Qt::LeftButton);
    emit axial->mouseReleased(1, 1, Qt::LeftButton);
    check(count(window.activeMaskLabels(), 3) == int(kDim * kDim) - 8 * 8, "a click in the air fills the air around it");
    window.undoMaskEdit();

    window.setLabelLocked(3, true);
    check(window.floodFillAt(9, 9, 4, 2) == 0 && count(window.activeMaskLabels(), 3) == 0,
          "filling with a locked label is refused");

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
