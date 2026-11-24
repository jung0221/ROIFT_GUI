// The slice keys (W/S, A/D, Q/E) reach the slices whatever widget has the focus, as long as the
// pointer is over a view, a view has the focus, or the focused widget does not take text; a text
// field keeps its letters, and Ctrl+letter is never taken.
#include "ManualSeedSelector.h"
#include "Mask3DView.h"
#include "OrthogonalView.h"
#include "UiUtils.h"

#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDir>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
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

constexpr unsigned kDim = 20;
using VolumeType = itk::Image<int32_t, 3>;

bool writeImage(const QString &path)
{
    VolumeType::Pointer image = VolumeType::New();
    VolumeType::SizeType size;
    size.Fill(kDim);
    VolumeType::IndexType start;
    start.Fill(0);
    image->SetRegions(VolumeType::RegionType(start, size));
    image->Allocate();
    image->FillBuffer(0);
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

void settle()
{
    for (int i = 0; i < 10; ++i)
        QCoreApplication::processEvents();
}

// Typed into whatever has the focus, the way a key press is delivered.
void type(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QWidget *target = QApplication::focusWidget();
    if (!target)
        return;
    const QString text = (modifiers & Qt::ControlModifier) ? QString() : QKeySequence(key).toString().toLower();
    QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, modifiers, text);
    QApplication::sendEvent(target, &release);
}

// Focuses w and types key; how far the slider moved.
int stepWith(QWidget *w, QSlider *slider, Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    w->setFocus(Qt::MouseFocusReason);
    settle();
    slider->setValue(int(kDim / 2));
    const int before = slider->value();
    type(key, modifiers);
    settle();
    return slider->value() - before;
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
    QCoreApplication::setApplicationName("slice_keys_gui_test");

    // What counts as typing text.
    {
        QLineEdit edit;
        QSpinBox spin;
        QComboBox plain;
        QComboBox editable;
        editable.setEditable(true);
        QPlainTextEdit log;
        log.setReadOnly(true);
        check(UiUtils::focusTakesText(&edit), "a line edit takes text");
        check(!UiUtils::focusTakesText(&spin) && !UiUtils::focusTakesText(spin.findChild<QLineEdit *>()),
              "a spin box (and its inner line edit) does not");
        check(!UiUtils::focusTakesText(&plain) && UiUtils::focusTakesText(&editable),
              "a combo box only when editable");
        check(!UiUtils::focusTakesText(&log), "a read-only log does not");
        check(!UiUtils::focusTakesText(nullptr), "no focus does not");
    }

    const QString ct = dir.filePath("cube_0000.nii.gz");
    check(writeImage(ct), "test image written");
    ManualSeedSelector window("");
    window.resize(1400, 900);
    window.show();
    window.activateWindow();
    settle();
    window.addImagesFromPaths({ct});
    settle();
    auto *axialView = window.findChild<OrthogonalView *>("axialView");
    auto *axial = window.findChild<QSlider *>("axialSlider");
    auto *sagittal = window.findChild<QSlider *>("sagittalSlider");
    auto *coronal = window.findChild<QSlider *>("coronalSlider");
    auto *labelSpin = window.findChild<QSpinBox *>("labelSelector");
    auto *maskList = window.findChild<QListWidget *>("maskList");
    auto *view3D = window.findChild<Mask3DView *>();
    auto *vtkWidget = view3D ? view3D->findChild<QVTKOpenGLNativeWidget *>() : nullptr;
    auto *draw = window.findChild<QPushButton *>("maskDrawButton");
    if (draw)
        draw->click(); // the drawing controls: label number, brush radius
    settle();
    QDoubleSpinBox *numberField = nullptr;
    for (QDoubleSpinBox *box : window.findChildren<QDoubleSpinBox *>())
        if (!numberField && box->isVisible())
            numberField = box;
    const bool found = axialView && axial && sagittal && coronal && labelSpin && maskList && vtkWidget &&
                       numberField && labelSpin->isVisible() && maskList->isVisible() && vtkWidget->isVisible();
    check(found, "controls found and shown");
    if (!found)
        return 1;

    const QPoint overAxial = axialView->mapToGlobal(axialView->rect().center());
    QCursor::setPos(overAxial);
    settle();
    check(stepWith(axialView, axial, Qt::Key_W) == 1, "W on the focused axial view: next axial slice");
    check(stepWith(axialView, axial, Qt::Key_S) == -1, "S: previous");
    check(stepWith(axialView, sagittal, Qt::Key_D) == 1 && stepWith(axialView, sagittal, Qt::Key_A) == -1,
          "D and A step the sagittal slice");
    check(stepWith(axialView, coronal, Qt::Key_E) == 1 && stepWith(axialView, coronal, Qt::Key_Q) == -1,
          "E and Q step the coronal slice");
    check(stepWith(labelSpin, axial, Qt::Key_W) == 1, "with the label number focused and the pointer on a view");
    check(stepWith(numberField, axial, Qt::Key_W) == 1, "with a number field focused");
    check(stepWith(maskList, axial, Qt::Key_W) == 1, "with the mask list focused");
    check(stepWith(vtkWidget, axial, Qt::Key_W) == 1, "with the 3D view focused, so VTK never reads W");
    check(stepWith(axialView, axial, Qt::Key_W, Qt::ControlModifier) == 0, "Ctrl+W is not a slice key");

    QCursor::setPos(window.mapToGlobal(QPoint(2, 2)));
    settle();
    check(stepWith(labelSpin, axial, Qt::Key_W) == 0, "pointer off the views: the focused field keeps W");
    check(stepWith(vtkWidget, axial, Qt::Key_W) == 1, "but a focused 3D view still passes it to the slices");

    QLineEdit *text = new QLineEdit(&window);
    text->setGeometry(axialView->mapTo(&window, axialView->rect().topLeft()).x(), 0, 80, 20);
    text->show();
    QCursor::setPos(overAxial);
    settle();
    const int moved = stepWith(text, axial, Qt::Key_W);
    check(moved == 0 && text->text() == "w", "a text field keeps its letters even over a view");

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
