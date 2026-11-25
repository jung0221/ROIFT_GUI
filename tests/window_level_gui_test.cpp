// Window/level: slider moves that queue up while a redraw runs are drawn together, once per hold
// interval, and the views end on the last value; a lone change is drawn at once.
#include "ManualSeedSelector.h"
#include "OrthogonalView.h"
#include "RangeSlider.h"

#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QSettings>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
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

constexpr unsigned kX = 64, kY = 48, kZ = 8;
constexpr int kStep = 10; // the value of voxel x is x * kStep

bool writeRamp(const QString &path)
{
    using VolumeType = itk::Image<int32_t, 3>;
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
                image->SetPixel(voxel, int32_t(x) * kStep);
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

void flush(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

// The axial slice shows the ramp under the window the spin boxes state, to a grey level.
bool showsWindow(const OrthogonalView *view, const QDoubleSpinBox *level, const QDoubleSpinBox *width)
{
    const QImage &image = view->image();
    if (image.width() != int(kX) || image.height() != int(kY))
        return false;
    const double lo = level->value() - width->value() / 2.0;
    const double hi = level->value() + width->value() / 2.0;
    for (unsigned x = 0; x < kX; ++x)
    {
        const double v = std::clamp(double(x) * kStep, lo, hi);
        const double expected = 255.0 * (v - lo) / (hi - lo);
        if (std::abs(qRed(image.pixel(int(x), int(kY / 2))) - expected) > 1.5)
            return false;
    }
    return true;
}

void mouse(QWidget *w, QEvent::Type type, int x, Qt::MouseButton button, Qt::MouseButtons buttons, bool post)
{
    const QPointF at(x, w->height() / 2);
    auto *event = new QMouseEvent(type, at, QPointF(w->mapToGlobal(at.toPoint())), button, buttons, Qt::NoModifier);
    if (post)
        QCoreApplication::postEvent(w, event); // queued, like input arriving during a redraw
    else
    {
        QApplication::sendEvent(w, event);
        delete event;
    }
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
    QCoreApplication::setApplicationName("window_level_gui_test");

    const QString ct = dir.filePath("ramp_0000.nii.gz");
    check(writeRamp(ct), "test data written");

    ManualSeedSelector window("");
    window.resize(1400, 900);
    window.show();
    window.addImagesFromPaths({ct});
    auto *slider = window.findChild<RangeSlider *>();
    auto *axial = window.findChild<OrthogonalView *>("axialView");
    QDoubleSpinBox *level = nullptr;
    QDoubleSpinBox *width = nullptr;
    for (QDoubleSpinBox *box : window.findChildren<QDoubleSpinBox *>())
    {
        if (box->toolTip() == "Window Level")
            level = box;
        else if (box->toolTip() == "Window Width")
            width = box;
    }
    const bool found = slider && axial && level && width;
    check(found, "controls found");
    if (!found)
        return 1;
    flush(100);
    check(showsWindow(axial, level, width), "the slice is drawn under the starting window");

    // Press on the lower handle, then 100 moves queued at once, as a 1000 Hz mouse delivers them
    // while a redraw runs.
    const int before = window.sliceRedrawCount();
    const int startLower = slider->lowerValue();
    mouse(slider, QEvent::MouseButtonPress, 0, Qt::LeftButton, Qt::LeftButton, false);
    const int moves = 100;
    const int span = slider->width() / 2;
    for (int i = 1; i <= moves; ++i)
        mouse(slider, QEvent::MouseMove, span * i / moves, Qt::NoButton, Qt::LeftButton, true);
    QCoreApplication::processEvents();
    const int drawnDuringBurst = window.sliceRedrawCount() - before;
    std::printf("redraws for %d queued moves: %d\n", moves, drawnDuringBurst);
    check(drawnDuringBurst <= 2, "100 queued moves are not drawn one by one");
    flush(100); // the hold interval ends and draws what it held
    mouse(slider, QEvent::MouseButtonRelease, span, Qt::LeftButton, Qt::NoButton, false);
    check(slider->lowerValue() != startLower, "the drag moved the lower handle");
    check(window.sliceRedrawCount() - before <= 3, "and the whole drag took at most three redraws");
    check(showsWindow(axial, level, width), "the views end on the last value");

    // Still for longer than the hold interval: one change is drawn before the call returns.
    flush(100);
    const int beforeLone = window.sliceRedrawCount();
    width->setValue(width->value() / 2.0);
    check(window.sliceRedrawCount() == beforeLone + 1 && showsWindow(axial, level, width),
          "a lone change of the width is drawn at once");

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
