// Drawing with Show 3D on: a stroke starts no surface build until it ends, the build runs off the
// GUI thread, and neither an edit nor a Show 3D toggle moves the camera the user left.
#include "ManualSeedSelector.h"
#include "Mask3DView.h"
#include "OrthogonalView.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>

#include <itkImage.h>
#include <itkImageFileWriter.h>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-70s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

constexpr unsigned kX = 40, kY = 40, kZ = 12;
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
    image->Allocate();
    image->FillBuffer(100);
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

bool sameCamera(const Mask3DView::CameraState &a, const Mask3DView::CameraState &b)
{
    for (int i = 0; i < 3; ++i)
        if (std::abs(a.position[i] - b.position[i]) > 1e-9 || std::abs(a.focalPoint[i] - b.focalPoint[i]) > 1e-9 ||
            std::abs(a.viewUp[i] - b.viewUp[i]) > 1e-9)
            return false;
    return std::abs(a.viewAngle - b.viewAngle) < 1e-9;
}

void stroke(OrthogonalView *view, int v)
{
    emit view->mousePressed(5, v, Qt::LeftButton);
    for (int u = 8; u <= 30; u += 4)
        emit view->mouseMoved(u, v, Qt::LeftButton);
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
    QCoreApplication::setApplicationName("draw_3d_gui_test");

    const QString ct = dir.filePath("flat_0000.nii.gz");
    check(writeImage(ct), "test image written");
    ManualSeedSelector window("");
    window.addImagesFromPaths({ct});
    auto *axial = window.findChild<OrthogonalView *>("axialView");
    auto *draw = window.findChild<QPushButton *>("maskDrawButton");
    auto *view3D = window.findChild<Mask3DView *>();
    QCheckBox *show3D = nullptr;
    for (QCheckBox *box : window.findChildren<QCheckBox *>())
        if (box->text() == "Show 3D")
            show3D = box;
    const bool found = axial && draw && view3D && show3D;
    check(found, "controls found");
    if (!found)
        return 1;
    show3D->setChecked(true);
    window.setActiveLabel(1);
    window.setBrushRadiusMm(2.0);
    draw->click();

    stroke(axial, 10);
    check(!view3D->surfaceBuildPending() && view3D->surfaceBuildCount() == 0,
          "while the stroke goes on, no surface is built");
    emit axial->mouseReleased(30, 10, Qt::LeftButton);
    check(view3D->surfaceBuildPending(), "its release starts one build, which runs off the GUI thread");
    view3D->waitForSurface();
    check(view3D->hasMaskSurface() && view3D->surfaceBuildCount() == 1, "one surface for the whole stroke");

    // The user zooms in and turns the view.
    Mask3DView::CameraState moved = view3D->captureCameraState();
    moved.position[0] += 7.0;
    moved.position[2] *= 0.5;
    moved.viewAngle = 20.0;
    view3D->restoreCameraState(moved);
    const Mask3DView::CameraState before = view3D->captureCameraState();

    stroke(axial, 20);
    emit axial->mouseReleased(30, 20, Qt::LeftButton);
    view3D->waitForSurface();
    check(view3D->surfaceBuildCount() == 2, "another stroke rebuilds the surface");
    check(sameCamera(view3D->captureCameraState(), before), "and leaves the camera where the user put it");

    show3D->setChecked(false);
    stroke(axial, 30);
    emit axial->mouseReleased(30, 30, Qt::LeftButton);
    check(!view3D->surfaceBuildPending(), "with Show 3D off an edit builds nothing");
    show3D->setChecked(true);
    view3D->waitForSurface();
    check(view3D->surfaceBuildCount() == 3, "turning it back on builds the edited surface");
    check(sameCamera(view3D->captureCameraState(), before), "with the camera unchanged");

    // Edits faster than the builds: the waiting volume is replaced, not queued behind.
    for (int i = 0; i < 4; ++i)
    {
        emit axial->mousePressed(10 + i, 35, Qt::LeftButton);
        emit axial->mouseReleased(10 + i, 35, Qt::LeftButton);
    }
    view3D->waitForSurface();
    check(view3D->surfaceBuildCount() <= 5, "four quick clicks build at most two surfaces");
    check(view3D->hasMaskSurface(), "and the last one is shown");

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
