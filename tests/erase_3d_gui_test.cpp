// Shift+drag on the 3D view erases the selected label inside the rectangle, through the whole
// depth, in the edited mask only, as one undo step; other labels and a locked label stay; a
// Shift+click without a drag still locates the surface point and erases nothing.
#include "ManualSeedSelector.h"
#include "Mask3DView.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QMouseEvent>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>

#include <QVTKOpenGLNativeWidget.h>

#include <cstdio>

#include <itkImage.h>
#include <itkImageFileWriter.h>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-72s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

constexpr unsigned kX = 40, kY = 40, kZ = 16;
using VolumeType = itk::Image<int32_t, 3>;

template <typename ValueAt>
bool writeVolume(const QString &path, ValueAt valueAt)
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
    for (unsigned z = 0; z < kZ; ++z)
        for (unsigned y = 0; y < kY; ++y)
            for (unsigned x = 0; x < kX; ++x)
            {
                VolumeType::IndexType voxel;
                voxel[0] = VolumeType::IndexValueType(x);
                voxel[1] = VolumeType::IndexValueType(y);
                voxel[2] = VolumeType::IndexValueType(z);
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

// Label 1: a slab across the whole width. Label 2: a block on the left, under the rectangle.
int maskAt(unsigned x, unsigned y, unsigned z)
{
    if (z < 4 || z > 11)
        return 0;
    if (x >= 4 && x <= 35 && y >= 10 && y <= 29)
        return 1;
    if (x >= 4 && x <= 12 && y >= 32 && y <= 36)
        return 2;
    return 0;
}

void mouse(QWidget *w, QEvent::Type type, QPoint at, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, QPointF(at), QPointF(w->mapToGlobal(at)), button, buttons, Qt::ShiftModifier);
    QApplication::sendEvent(w, &event);
}

void shiftDrag(QWidget *w, QPoint from, QPoint to)
{
    mouse(w, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 4; ++i)
        mouse(w, QEvent::MouseMove, from + (to - from) * i / 4, Qt::NoButton, Qt::LeftButton);
    mouse(w, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
}

struct Census
{
    int label1 = 0, label2 = 0;
    int erasedRight = 0;  // label-1 voxels gone on the right half (x >= 20)
    int erasedTotal = 0;
};

Census census(const std::vector<int> &labels)
{
    Census c;
    for (unsigned z = 0; z < kZ; ++z)
        for (unsigned y = 0; y < kY; ++y)
            for (unsigned x = 0; x < kX; ++x)
            {
                const int v = labels[(std::size_t(z) * kY + y) * kX + x];
                c.label1 += v == 1;
                c.label2 += v == 2;
                if (maskAt(x, y, z) == 1 && v == 0)
                {
                    ++c.erasedTotal;
                    c.erasedRight += x >= 20;
                }
            }
    return c;
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
    QCoreApplication::setApplicationName("erase_3d_gui_test");

    const QString ct = dir.filePath("slab_0000.nii.gz");
    const QString mask = dir.filePath("slab_mask.nii.gz");
    check(writeVolume(ct, [](unsigned, unsigned, unsigned) { return 100; }) && writeVolume(mask, maskAt),
          "test data written");

    ManualSeedSelector window("");
    window.resize(1400, 900);
    window.show();
    window.addImagesFromPaths({ct});
    auto *view3D = window.findChild<Mask3DView *>();
    auto *vtk = view3D ? view3D->findChild<QVTKOpenGLNativeWidget *>() : nullptr;
    auto *axial = window.findChild<QSlider *>("axialSlider");
    QCheckBox *show3D = nullptr;
    for (QCheckBox *box : window.findChildren<QCheckBox *>())
        if (box->text() == "Show 3D")
            show3D = box;
    const bool found = view3D && vtk && axial && show3D;
    check(found, "controls found");
    if (!found)
        return 1;
    show3D->setChecked(true);
    check(window.applyMaskFromPath(mask.toStdString()), "the mask is edited");
    view3D->waitForSurface();
    for (int i = 0; i < 10; ++i)
        QCoreApplication::processEvents();
    window.setActiveLabel(1);
    const Census before = census(window.activeMaskLabels());
    check(before.label1 > 0 && before.label2 > 0 && vtk->width() > 100 && vtk->height() > 100,
          "both labels drawn in a 3D view of usable size");

    // The camera frames the surface's bounds, centred on x = 19.5: the left half of the view.
    const QPoint from(3, 3);
    const QPoint to(vtk->width() / 2 - 6, vtk->height() - 4);
    shiftDrag(vtk, from, to);
    const Census after = census(window.activeMaskLabels());
    check(after.erasedTotal > 0 && after.label1 == before.label1 - after.erasedTotal,
          "Shift+drag erases label 1 inside the rectangle");
    check(after.erasedRight == 0, "and nothing of it right of the rectangle");
    check(after.label2 == before.label2, "label 2, under the rectangle too, is not the selected label: kept");
    check(window.maskEditHistory().undoCount() == 1, "one undo step");
    window.undoMaskEdit();
    check(census(window.activeMaskLabels()).label1 == before.label1, "undo brings it back");

    window.setLabelLocked(1, true);
    shiftDrag(vtk, from, to);
    check(census(window.activeMaskLabels()).label1 == before.label1, "a locked label is not erased");
    window.setLabelLocked(1, false);

    show3D->setChecked(false);
    shiftDrag(vtk, from, to);
    check(census(window.activeMaskLabels()).label1 == before.label1, "with Show 3D off nothing is erased");
    show3D->setChecked(true);
    view3D->waitForSurface();

    // A click without a drag is still the locate gesture.
    axial->setValue(0);
    const QPoint centre(vtk->width() / 2, vtk->height() / 2);
    mouse(vtk, QEvent::MouseButtonPress, centre, Qt::LeftButton, Qt::LeftButton);
    mouse(vtk, QEvent::MouseButtonRelease, centre, Qt::LeftButton, Qt::NoButton);
    check(census(window.activeMaskLabels()).label1 == before.label1, "Shift+click erases nothing");
    // The ray meets the slab's top face, z = 11.5 before smoothing: slice 11 or 12. VTK's picker
    // needs the render window, which a platform without OpenGL (the CI runners) never creates.
    if (vtk->isValid())
        check(axial->value() >= 10 && axial->value() <= 12, "and moves the slices to the surface point");
    else
        std::printf("%-72s %s\n", "and moves the slices to the surface point", "skipped: no OpenGL context");

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
