#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QScreen>
#include <QStringList>
#include <QSurfaceFormat>
#include <QVTKOpenGLNativeWidget.h>
// With VTK's TBB backend (conda-forge) these pull in oneTBB, whose emit() member Qt's emit macro erases.
#pragma push_macro("emit")
#undef emit
#include <vtkSMP.h>
#include <vtkSMPTools.h>
#pragma pop_macro("emit")
#include "CommandLine.h"
#include "ManualSeedSelector.h"
#include "Theme.h"
#include "Version.h"
#include <iostream>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// The Windows build is a GUI-subsystem binary, so a launch from Explorer or the
// Start menu has no console and --help/--version would print into the void.
// Fall back to a dialog only then: a console launch, and every other platform,
// still gets plain stderr.
static void report_cli_message(const QString &title, const QString &text)
{
    std::cerr << text.toStdString() << "\n";
#ifdef Q_OS_WIN
    if (GetConsoleWindow() == nullptr)
        QMessageBox::information(nullptr, title, text);
#else
    Q_UNUSED(title);
#endif
}

static void print_help()
{
    report_cli_message(QStringLiteral("ROIFT GUI"), commandLineUsage());
}

// A conda environment's activation exports __EGL_VENDOR_LIBRARY_DIRS naming its own
// vendor list, which holds Mesa only. This binary loads the system libEGL, so on an
// NVIDIA machine that list leaves Mesa without a driver and it falls back to llvmpipe:
// the 3D view, and the window it is composited into, are then drawn on the CPU. Only
// a list made entirely of conda directories is dropped; one set by hand is kept.
static void ignoreCondaEglVendorDirs()
{
#ifdef Q_OS_LINUX
    const QString dirs = qEnvironmentVariable("__EGL_VENDOR_LIBRARY_DIRS");
    const QStringList entries = dirs.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (entries.isEmpty())
        return;
    for (const QString &entry : entries)
    {
        // <prefix>/share/glvnd/egl_vendor.d, where <prefix> holds conda-meta/
        QDir prefix(entry);
        if (!prefix.cdUp() || !prefix.cdUp() || !prefix.cdUp() ||
            !QFileInfo(prefix.filePath(QStringLiteral("conda-meta"))).isDir())
            return;
    }
    qunsetenv("__EGL_VENDOR_LIBRARY_DIRS");
    std::cerr << "main: ignoring __EGL_VENDOR_LIBRARY_DIRS=" << dirs.toStdString()
              << " from a conda environment; it hides the system GPU driver from EGL\n";
#endif
}

// VTK runs its filters on one thread unless a backend is chosen; the threaded one
// builds the surface of a 704 x 704 x 640 label map in 1.2 s instead of 3.8 s.
// VTK_SMP_BACKEND_IN_USE, when set, still decides.
static void useThreadedVtkFilters()
{
#if VTK_SMP_ENABLE_STDTHREAD && VTK_SMP_DEFAULT_IMPLEMENTATION_SEQUENTIAL
    if (qEnvironmentVariableIsEmpty("VTK_SMP_BACKEND_IN_USE"))
        vtkSMPTools::SetBackend("STDThread");
#endif
}

int main(int argc, char **argv)
{
    // QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    // QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    // Both before QApplication: EGL reads its vendor list once, when Qt first opens it.
    ignoreCondaEglVendorDirs();
    useThreadedVtkFilters();

    // Must precede QApplication: without it Qt on Wayland hands the 3D view an
    // OpenGL ES context, VTK's GLSL 150 shaders fail and the first render segfaults.
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());

    QApplication app(argc, argv);
    // Install the design language on the application, not the main window, so
    // dialogs, menus and message boxes are the same product as the window that
    // opened them.
    app.setStyleSheet(Theme::styleSheet());
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/roift_gui.png")));
    // Identify the app so QSettings has a stable backing store for persisted
    // window geometry, splitter sizes and collapsible-section state.
    QCoreApplication::setOrganizationName("ROIFT");
    QCoreApplication::setApplicationName("roift_gui");
    QCoreApplication::setApplicationVersion(Version::string());
    // arguments(), not argv: on Windows it is decoded from the wide command line, so a
    // non-ASCII path survives; Qt's own options are already removed from both.
    const CommandLineOptions options = parseCommandLine(QCoreApplication::arguments().mid(1));
    switch (options.action)
    {
    case CommandLineOptions::Action::Help:
        print_help();
        return 0;
    case CommandLineOptions::Action::Version:
        report_cli_message(QStringLiteral("ROIFT GUI"),
                           QStringLiteral("roift_gui %1").arg(Version::string()));
        return 0;
    case CommandLineOptions::Action::Error:
        std::cerr << "Error: " << options.error.toStdString() << "\n";
        print_help();
        return 1;
    case CommandLineOptions::Action::Open:
        break;
    }

    if (!options.images.empty())
        std::cerr << "main: opening " << options.images.size() << " path(s) from CLI\n";
    else
        std::cerr << "main: no input path provided via CLI\n";

    ManualSeedSelector w("");
    if (!options.images.empty())
    {
        QStringList initialPaths;
        for (const CommandLineImage &image : options.images)
            initialPaths.push_back(image.path);
        w.addImagesFromPaths(initialPaths);
    }

    // Read inline above, so the first image's files open on it now; the others wait
    // in their image's entry and open the first time it is shown. Each flag opens
    // only its own kind of file, so a seed .txt never reaches the mask reader.
    if (options.withoutImage.hasFiles())
        std::cerr << "Warning: --mask/--seeds ignored, no image was given\n";
    for (const CommandLineImage &image : options.images)
    {
        if (image.hasFiles() && !w.attachFilesToImage(image.path, image.maskPaths, image.seedsPath))
            std::cerr << "Warning: --mask/--seeds for " << image.path.toStdString()
                      << " ignored, the image is not in the list\n";
    }
    // From here on, images chosen in the window are read on a worker thread so
    // the window stays responsive.
    w.setBackgroundImageLoading(true);

    QScreen *screen = QGuiApplication::primaryScreen();
    if (options.fullscreen)
    {
        w.showFullScreen();
    }
    else if (w.geometryWasRestored())
    {
        // A previous session's geometry was restored; respect it.
        w.show();
    }
    else
    {
        // start with a sensible default window size centered on the primary screen
        QRect avail = screen->availableGeometry();
        QSize desired(1200, 800);
        int wdt = std::min(desired.width(), avail.width());
        int hgt = std::min(desired.height(), avail.height());
        w.resize(wdt, hgt);
        w.move(avail.x() + (avail.width() - wdt) / 2, avail.y() + (avail.height() - hgt) / 2);
        w.show();
    }

    return app.exec();
}
