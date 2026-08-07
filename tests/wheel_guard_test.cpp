// Checks that the mouse wheel scrolls the tool sidebar instead of editing the
// control under the cursor.
//
// The guard has two halves and each is easy to lose:
//
//   * it must leave the value alone — otherwise the control still edits;
//   * it must hand the event to the enclosing scroll area — otherwise the
//     panel behind the control simply stops scrolling, which is worse than
//     the problem being fixed.
//
// The second half is why the guard forwards the event by hand instead of
// leaving it unaccepted for Qt to propagate: measured against the real window,
// that propagation does not reach the scroll area.
#include "Theme.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cstdio>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-58s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

/// Roll the wheel one notch downward over @p target and report whether the
/// target itself accepted it.
bool rollWheelOver(QWidget *target)
{
    const QPointF local(target->width() / 2.0, target->height() / 2.0);
    QWheelEvent wheel(local,
                      target->mapToGlobal(local.toPoint()),
                      QPoint(0, -40),   // pixelDelta
                      QPoint(0, -120),  // angleDelta: one notch down
                      Qt::NoButton,
                      Qt::NoModifier,
                      Qt::NoScrollPhase,
                      false);
    wheel.ignore();
    QApplication::sendEvent(target, &wheel);
    return wheel.isAccepted();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // A tool sidebar in miniature: a scroll area whose content is taller than
    // the viewport, holding the three control types the guard covers.
    QScrollArea area;
    area.setWidgetResizable(true);
    area.setFixedSize(200, 100);

    QWidget *content = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(content);

    QSpinBox *spin = new QSpinBox();
    spin->setRange(0, 100);
    spin->setValue(50);
    layout->addWidget(spin);

    QDoubleSpinBox *doubleSpin = new QDoubleSpinBox();
    doubleSpin->setRange(0.0, 100.0);
    doubleSpin->setValue(50.0);
    layout->addWidget(doubleSpin);

    QSlider *slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 100);
    slider->setValue(50);
    layout->addWidget(slider);

    QComboBox *combo = new QComboBox();
    combo->addItems({"one", "two", "three"});
    combo->setCurrentIndex(1);
    layout->addWidget(combo);

    // Force the content past the viewport height so there is something to
    // scroll; without this the propagation half of the test proves nothing.
    QWidget *filler = new QWidget();
    filler->setMinimumHeight(600);
    layout->addWidget(filler);

    area.setWidget(content);
    area.show();
    QApplication::processEvents();

    check(area.verticalScrollBar()->maximum() > 0, "fixture: the panel actually has room to scroll");

    // Without the guard a spin box both edits and swallows the wheel. Establish
    // that first, so the assertions afterwards are known to be testing the
    // guard and not some accident of the fixture.
    check(rollWheelOver(spin), "unguarded: a spin box accepts the wheel");
    check(spin->value() != 50, "unguarded: a spin box edits on the wheel");
    spin->setValue(50);

    Theme::guardWheel(&area);
    QScrollBar *bar = area.verticalScrollBar();

    // --- the wheel must not edit, and must scroll the panel instead ---
    const struct
    {
        QWidget *widget;
        const char *name;
    } guarded[] = {
        {spin, "spin box"},
        {doubleSpin, "double spin box"},
        {slider, "slider"},
        {combo, "combo box"},
    };

    for (const auto &target : guarded)
    {
        bar->setValue(0);
        rollWheelOver(target.widget);
        check(bar->value() > 0,
              QStringLiteral("wheel over a %1 scrolls the panel behind it").arg(target.name).toLocal8Bit().constData());
    }

    check(spin->value() == 50, "wheel over a spin box leaves the value alone");
    check(qFuzzyCompare(doubleSpin->value(), 50.0), "wheel over a double spin box leaves the value alone");
    check(slider->value() == 50, "wheel over a slider leaves the value alone");
    check(combo->currentIndex() == 1, "wheel over a combo box leaves the selection alone");

    // --- the scroll bar itself keeps its wheel ---
    bar->setValue(0);
    rollWheelOver(bar);
    check(bar->value() > 0, "wheel over the scroll bar still scrolls it");

    std::printf("\n%s\n", failures ? "FAILURES" : "all wheel-guard checks passed");
    return failures ? 1 : 0;
}
