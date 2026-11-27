#include "CommandLine.h"

namespace
{

// A path cannot start with '-': it would be read as an option.
bool isOption(const QString &argument)
{
    return argument.startsWith(QLatin1Char('-'));
}

CommandLineOptions failure(const QString &message)
{
    CommandLineOptions options;
    options.action = CommandLineOptions::Action::Error;
    options.error = message;
    return options;
}

QString seedsTwice(const QString &imagePath)
{
    return QStringLiteral("--seeds given twice for %1; an image takes one seed file")
        .arg(imagePath.isEmpty() ? QStringLiteral("the first image") : imagePath);
}

} // namespace

CommandLineOptions parseCommandLine(const QStringList &arguments)
{
    CommandLineOptions options;
    // Files named before the first image wait here until that image is known.
    CommandLineImage leading;
    bool maskRequired = false;
    auto currentImage = [&]() -> CommandLineImage &
    { return options.images.empty() ? leading : options.images.back(); };

    for (int i = 0; i < arguments.size(); ++i)
    {
        const QString &a = arguments[i];
        const bool hasValue = i + 1 < arguments.size();
        if (a == "--help" || a == "-h")
        {
            options.action = CommandLineOptions::Action::Help;
            return options;
        }
        if (a == "--version")
        {
            options.action = CommandLineOptions::Action::Version;
            return options;
        }
        if (a == "--input" || a == "-i")
        {
            if (!hasValue || isOption(arguments[i + 1]))
                return failure(QStringLiteral("--input requires at least one path"));
            while (i + 1 < arguments.size() && !isOption(arguments[i + 1]))
                options.images.push_back({arguments[++i], {}, {}});
        }
        else if (a == "--mask" || a == "-m")
        {
            if (!hasValue)
                return failure(QStringLiteral("--mask requires a path"));
            currentImage().maskPaths.push_back(arguments[++i]);
        }
        else if (a == "--seeds" || a == "-s")
        {
            if (!hasValue)
                return failure(QStringLiteral("--seeds requires a path"));
            CommandLineImage &image = currentImage();
            if (!image.seedsPath.isEmpty())
                return failure(seedsTwice(image.path));
            image.seedsPath = arguments[++i];
        }
        else if (a == "--mask-required")
        {
            maskRequired = true;
        }
        else if (a == "--fullscreen" || a == "-f")
        {
            options.fullscreen = true;
        }
        else if (isOption(a))
        {
            return failure(QStringLiteral("unknown option '%1'").arg(a));
        }
        else
        {
            options.images.push_back({a, {}, {}});
        }
    }

    if (leading.hasFiles())
    {
        if (options.images.empty())
        {
            options.withoutImage = leading;
        }
        else
        {
            CommandLineImage &first = options.images.front();
            if (!leading.seedsPath.isEmpty() && !first.seedsPath.isEmpty())
                return failure(seedsTwice(first.path));
            first.maskPaths = leading.maskPaths + first.maskPaths;
            if (first.seedsPath.isEmpty())
                first.seedsPath = leading.seedsPath;
        }
    }

    if (maskRequired)
    {
        bool anyMask = !options.withoutImage.maskPaths.isEmpty();
        for (const CommandLineImage &image : options.images)
            anyMask = anyMask || !image.maskPaths.isEmpty();
        if (!anyMask)
            return failure(QStringLiteral("--mask-required specified but no --mask provided"));
    }
    return options;
}

QString commandLineUsage()
{
    return QStringLiteral(
        "roift_gui [--version] [--fullscreen] [[--input] <image>... [--mask <mask>]... [--seeds <seeds.txt>]]...\n"
        "\n"
        "Each --mask/-m and --seeds/-s belongs to the image named last before it, so\n"
        "  roift_gui -i a.nii.gz -m a_lungs.nii.gz -m a_heart.nii.gz -i b.nii.gz -m b_lungs.nii.gz\n"
        "opens a.nii.gz with its two masks and lists b.nii.gz, whose mask opens when b is selected.\n"
        "--input/-i takes one or more paths; an image may also be named without it.\n"
        "--mask may be repeated, one layer per file; --seeds is given once per image.\n"
        "Masks and seeds named before any image belong to the first one.\n"
        "\n"
        "Accepted: .nii, .nii.gz, DICOM (file or directory), .npz, .npy, and the raster\n"
        "formats .png, .jpg, .jpeg, .bmp, .tif and .tiff (measured in pixels).\n"
        "A .npz/.npy carries no spacing: it is taken from a matching .nii.gz next to it,\n"
        "or from a <name>.json sidecar, otherwise 1 mm isotropic is assumed.");
}
