#include <QApplication>
#include <QCoreApplication>
#include <QScreen>
#include <QStringList>
#include "ManualSeedSelector.h"
#include <iostream>
#include <string>
#include <vector>

static void print_help()
{
    std::cerr << "roift_gui [--input <nifti_path> [more_paths...]]\n";
    std::cerr << "You can pass multiple NIfTI paths after --input/-i or as positional arguments.\n";
}

int main(int argc, char **argv)
{
    // QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    // QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication app(argc, argv);
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
