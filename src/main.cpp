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
#include <vtkSMP.h>
#include <vtkSMPTools.h>
#include "ManualSeedSelector.h"
#include "Theme.h"
#include "Version.h"
#include <iostream>
#include <string>
#include <vector>

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
    report_cli_message(
        QStringLiteral("ROIFT GUI"),
        QStringLiteral(
            "roift_gui [--version] [--input <image_path> [more_paths...]]\n"
            "You can pass multiple paths after --input/-i or as positional arguments.\n"
            "Accepted: .nii, .nii.gz, DICOM (file or directory), .npz, .npy, and the raster\n"
            "formats .png, .jpg, .jpeg, .bmp, .tif and .tiff (measured in pixels).\n"
            "A .npz/.npy carries no spacing: it is taken from a matching .nii.gz next to it,\n"
            "or from a <name>.json sidecar, otherwise 1 mm isotropic is assumed."));
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
    std::vector<std::string> inputPaths;
    std::string seedsPath;
    bool startFullscreen = false;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--help" || a == "-h")
        {
            print_help();
            return 0;
        }
        if (a == "--version")
        {
            report_cli_message(QStringLiteral("ROIFT GUI"),
                               QStringLiteral("roift_gui %1").arg(Version::string()));
            return 0;
        }
        if (a == "--input" || a == "-i")
        {
            if (i + 1 < argc && std::string(argv[i + 1]).rfind("-", 0) != 0)
            {
                int j = i + 1;
                while (j < argc)
                {
                    std::string candidate = argv[j];
                    if (candidate.rfind("-", 0) == 0)
                        break;
                    inputPaths.push_back(candidate);
                    ++j;
                }
                i = j - 1;
            }
            else
            {
                std::cerr << "Error: --input requires at least one path\n";
                print_help();
                return 1;
            }
        }
        else if (a == "--mask" || a == "-m")
        {
            if (i + 1 < argc)
            {
                seedsPath = argv[i + 1];
                ++i;
            }
            else
            {
                std::cerr << "Error: --mask requires a path\n";
                print_help();
                return 1;
            }
        }
        else if (a == "--mask-required")
        {
            // signal that a mask must be provided when input is given
            if (seedsPath.empty())
            {
                std::cerr << "Error: --mask-required specified but no --mask provided\n";
                print_help();
                return 1;
            }
        }
        else if (a == "--seeds" || a == "-s")
        {
            if (i + 1 < argc)
            {
                seedsPath = argv[i + 1];
                ++i;
            }
            else
            {
                std::cerr << "Error: --seeds requires a path\n";
                print_help();
                return 1;
            }
        }
        else if (a == "--fullscreen" || a == "-f")
        {
            startFullscreen = true;
        }
        else if (!a.empty() && a[0] == '-')
        {
            std::cerr << "Error: unknown option '" << a << "'\n";
            print_help();
            return 1;
        }
        else
        {
            inputPaths.push_back(a);
        }
    }

    if (!inputPaths.empty())
    {
        std::cerr << "main: opening " << inputPaths.size() << " path(s) from CLI\n";
    }
    else
    {
        std::cerr << "main: no input path provided via CLI\n";
    }
    ManualSeedSelector w("");
    if (!inputPaths.empty())
    {
        QStringList initialPaths;
        for (const std::string &p : inputPaths)
            initialPaths.push_back(QString::fromStdString(p));
        w.addImagesFromPaths(initialPaths);
    }

    // if an image was provided and successfully loaded, optionally load seeds
    if (!seedsPath.empty() && w.hasImage())
    {
        // 'seedsPath' variable is used for --seeds previously; support --mask as well
        // try to apply as mask first, then if fails try as seeds file for backwards compatibility
        bool okMask = w.applyMaskFromPath(seedsPath);
        if (!okMask)
        {
            bool okSeeds = w.loadSeedsFromFile(seedsPath);
            if (!okSeeds)
                std::cerr << "Warning: failed to load mask or seeds from " << seedsPath << "\n";
        }
    }
    // From here on, images chosen in the window are read on a worker thread so
    // the window stays responsive; the command-line ones above were read inline
    // so that --mask/--seeds had an image to apply to.
    w.setBackgroundImageLoading(true);

    QScreen *screen = QGuiApplication::primaryScreen();
    if (startFullscreen)
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
