// Constructing the main window must open no other window.
//
// A parentless widget made visible during construction is a top-level window
// until a layout adopts it. On Wayland, Qt requests an activation token for each
// window it shows; a window closed before its token is used leaves GNOME drawing
// a busy cursor over every window for 15 s.
#include "ManualSeedSelector.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QWidget>

#include <cstdio>
#include <string>
#include <vector>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-58s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

class WindowShowRecorder : public QObject
{
public:
    std::vector<std::string> shown;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Show)
        {
            auto *widget = qobject_cast<QWidget *>(watched);
            if (widget && widget->isWindow())
                shown.push_back(std::string(widget->metaObject()->className()) + " \"" +
                                widget->objectName().toStdString() + "\" " +
                                std::to_string(widget->width()) + "x" + std::to_string(widget->height()));
        }
        return false;
    }
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    WindowShowRecorder recorder;
    app.installEventFilter(&recorder);
    {
        ManualSeedSelector window("");
        app.removeEventFilter(&recorder);

        for (const std::string &name : recorder.shown)
            std::printf("  shown as a window: %s\n", name.c_str());
        check(recorder.shown.empty(), "constructing the window shows no other window");

        // The initial method is Standard OIFT, which takes a smoothing level and
        // neither alpha nor sigma.
        const auto *smoothing = window.findChild<QComboBox *>("segmentationSmoothing");
        const auto *alpha = window.findChild<QDoubleSpinBox *>("segmentationAlpha");
        const auto *sigma = window.findChild<QDoubleSpinBox *>("segmentationSigma");
        check(smoothing && !smoothing->isHidden(), "smoothing is shown with the window");
        check(alpha && alpha->isHidden(), "alpha is hidden");
        check(sigma && sigma->isHidden(), "sigma is hidden");
    }

    if (failures)
        std::printf("%d check(s) failed\n", failures);
    else
        std::printf("all checks passed\n");
    return failures ? 1 : 0;
}
