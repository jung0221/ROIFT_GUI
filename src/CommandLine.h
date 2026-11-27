#pragma once

#include <QString>
#include <QStringList>

#include <vector>

// One image named on the command line, with the masks and seed file that follow it.
struct CommandLineImage
{
    QString path;
    QStringList maskPaths;
    QString seedsPath;

    bool hasFiles() const { return !maskPaths.isEmpty() || !seedsPath.isEmpty(); }
};

struct CommandLineOptions
{
    enum class Action
    {
        Open,
        Help,
        Version,
        Error
    };
    Action action = Action::Open;
    // Set when action is Error.
    QString error;
    // In command-line order. Masks and seeds named before any image belong to the first.
    std::vector<CommandLineImage> images;
    // Masks and seeds given while no image was named at all; path is empty.
    CommandLineImage withoutImage;
    bool fullscreen = false;
};

// Parse the arguments after the program name. Each --mask and --seeds belongs to the
// image named last before it, so `-i a -m a_mask -i b -m b_mask` pairs them.
CommandLineOptions parseCommandLine(const QStringList &arguments);

QString commandLineUsage();
