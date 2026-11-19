// The brush in millimetres: on anisotropic voxels a stamp covers exactly the voxels whose
// centres lie within the radius in mm (an ellipse in voxel units), as a disc in the view's
// plane or as a ball; a fast stroke leaves no gap; Ctrl+wheel resizes the brush while the
// plain wheel still zooms.
#include "ManualSeedSelector.h"
#include "OrthogonalView.h"

#include <QApplication>
#include <QDir>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QWheelEvent>

#include <cmath>
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

constexpr unsigned kX = 40, kY = 40, kZ = 10;
constexpr double kSpacing[3] = {0.5, 0.5, 2.0};
using VolumeType = itk::Image<int32_t, 3>;

bool writeImage(const QString &path)
{
    VolumeType::Pointer image = VolumeType::New();
    VolumeType::SizeType size;
    size[0] = kX;
    size[1] = kY;
    size[2] = kZ;
    VolumeType::IndexType start;
    start.Fill(0);
    image->SetRegions(VolumeType::RegionType(start, size));
    VolumeType::SpacingType spacing;
    for (unsigned i = 0; i < 3; ++i)
        spacing[i] = kSpacing[i];
    image->SetSpacing(spacing);
    image->Allocate();
    for (unsigned z = 0; z < kZ; ++z)
        for (unsigned y = 0; y < kY; ++y)
            for (unsigned x = 0; x < kX; ++x)
            {
                VolumeType::IndexType voxel;
                voxel[0] = static_cast<VolumeType::IndexValueType>(x);
                voxel[1] = static_cast<VolumeType::IndexValueType>(y);
                voxel[2] = static_cast<VolumeType::IndexValueType>(z);
                image->SetPixel(voxel, int32_t(x * 7 + y * 3 + z));
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

// Voxels whose centre is within radiusMm of the centre voxel's, counted directly; `axes`
// says which offsets are free (a disc frees two, a ball three).
int expectedStamp(double radiusMm, const bool axes[3])
{
    int count = 0;
    for (int dz = -20; dz <= 20; ++dz)
        for (int dy = -20; dy <= 20; ++dy)
            for (int dx = -20; dx <= 20; ++dx)
            {
                const int d[3] = {dx, dy, dz};
                bool allowed = true;
                double mm2 = 0.0;
                for (int a = 0; a < 3; ++a)
                {
                    allowed = allowed && (axes[a] || d[a] == 0);
                    mm2 += (d[a] * kSpacing[a]) * (d[a] * kSpacing[a]);
                }
                if (allowed && mm2 <= radiusMm * radiusMm)
                    ++count;
            }
    return count;
}

int paintedCount(ManualSeedSelector &window)
{
    int n = 0;
    for (int v : window.activeMaskLabels())
        n += v != 0;
    return n;
}

void tap(OrthogonalView *view, int u, int v)
{
    emit view->mousePressed(u, v, Qt::LeftButton);
    emit view->mouseReleased(u, v, Qt::LeftButton);
}

void wheel(OrthogonalView *view, Qt::KeyboardModifiers modifiers)
{
    const QPointF at(view->width() / 2.0, view->height() / 2.0);
    QWheelEvent event(at, view->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton, modifiers,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(view, &event);
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
    QCoreApplication::setApplicationName("brush_mm_gui_test");

    const QString ct = dir.filePath("aniso_0000.nii.gz");
    check(writeImage(ct), "a 0.5 x 0.5 x 2 mm image written");

    ManualSeedSelector window("");
    window.addImagesFromPaths({ct});
    auto *axial = window.findChild<OrthogonalView *>("axialView");
    auto *sagittal = window.findChild<OrthogonalView *>("sagittalView");
    auto *draw = window.findChild<QPushButton *>("maskDrawButton");
    const bool found = axial && sagittal && draw;
    check(found, "controls found");
    if (!found)
        return 1;
    window.setActiveLabel(1);
    draw->click();
    const bool disc[3] = {true, true, false};
    const bool ball[3] = {true, true, true};
    const bool sagittalDisc[3] = {false, true, true};

    window.setBrushRadiusMm(2.0);
    tap(axial, 20, 20);
    check(paintedCount(window) == expectedStamp(2.0, disc), "a 2 mm disc covers the voxels within 2 mm (49 here)");
    window.undoMaskEdit();

    window.setBrushSphere(true);
    tap(axial, 20, 20);
    check(paintedCount(window) == expectedStamp(2.0, ball), "a 2 mm ball reaches one 2 mm slice each way");
    window.undoMaskEdit();
    window.setBrushSphere(false);

    tap(sagittal, 20, 5);
    check(paintedCount(window) == expectedStamp(2.0, sagittalDisc), "on the sagittal view the disc is (y, z), in mm");
    window.undoMaskEdit();

    window.setBrushRadiusMm(0.1);
    tap(axial, 20, 20);
    check(paintedCount(window) == 1, "a radius under a voxel still paints the centre voxel");
    window.undoMaskEdit();

    // One move 30 voxels long: without walking the segment there would be two dots.
    window.setBrushRadiusMm(1.0);
    emit axial->mousePressed(5, 30, Qt::LeftButton);
    emit axial->mouseMoved(35, 30, Qt::LeftButton);
    emit axial->mouseReleased(35, 30, Qt::LeftButton);
    auto *slider = window.findChild<QSlider *>("axialSlider");
    const unsigned z = slider ? unsigned(slider->value()) : 0;
    const std::vector<int> &labels = window.activeMaskLabels();
    bool continuous = labels.size() == std::size_t(kX) * kY * kZ;
    for (unsigned x = 5; continuous && x <= 35; ++x)
        continuous = labels[x + 30 * kX + std::size_t(z) * kX * kY] == 1;
    check(continuous, "a fast stroke paints every voxel along its path");
    check(window.maskEditHistory().undoCount() == 1, "and is still one undo step");

    const double before = window.brushRadiusMm();
    wheel(axial, Qt::ControlModifier);
    check(std::abs(window.brushRadiusMm() - before * 1.1) < 0.051, "Ctrl+wheel grows the brush by a tenth");
    const double afterCtrl = window.brushRadiusMm();
    wheel(axial, Qt::NoModifier);
    check(window.brushRadiusMm() == afterCtrl, "the plain wheel leaves it (and zooms)");

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
