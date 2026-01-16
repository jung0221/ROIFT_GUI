#include <QApplication>
#include <QCoreApplication>
#include <QScreen>
#include "ManualSeedSelector.h"
#include <iostream>
#include <string>

static void print_help()
{
    std::cerr << "roift_gui [--input <nifti_path>]\n";
    std::cerr << "If no --input is provided, you may pass the nifti path as the first positional argument.\n";
}

int main(int argc, char **argv)
{
    // QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    // QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication app(argc, argv);
    std::string path;
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
        if (a == "--input" || a == "-i")
        {
            if (i + 1 < argc)
            {
                path = argv[i + 1];
                ++i;
            }
            else
            {
                std::cerr << "Error: --input requires a path\n";
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
        else if (path.empty())
        {
            // first positional argument
            path = a;
        }
    }

    if (!path.empty())
    {
        std::cerr << "main: opening path from CLI: '" << path << "'\n";
    }
    else
    {
        std::cerr << "main: no input path provided via CLI\n";
    }
    ManualSeedSelector w(path);
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
    QScreen *screen = QGuiApplication::primaryScreen();
    if (startFullscreen)
    {
        w.showFullScreen();
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
