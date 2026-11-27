// Checks which image each --mask and --seeds of the command line belongs to.
#include "CommandLine.h"

#include <cstdio>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-66s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

CommandLineOptions parse(const QString &line)
{
    return parseCommandLine(line.split(QLatin1Char(' '), Qt::SkipEmptyParts));
}

bool opens(const CommandLineOptions &options, size_t count)
{
    return options.action == CommandLineOptions::Action::Open && options.images.size() == count;
}

bool failsWith(const CommandLineOptions &options, const char *start)
{
    return options.action == CommandLineOptions::Action::Error && options.error.startsWith(QLatin1String(start));
}

} // namespace

int main()
{
    {
        const CommandLineOptions o = parse("-i a.nii.gz -m a_lungs.nii.gz -m a_heart.nii.gz -s a.txt "
                                           "-i b.nii.gz -m b_lungs.nii.gz");
        check(opens(o, 2) && o.images[0].path == "a.nii.gz" && o.images[1].path == "b.nii.gz",
              "two images, in command-line order");
        check(o.images[0].maskPaths == QStringList({"a_lungs.nii.gz", "a_heart.nii.gz"}) &&
                  o.images[0].seedsPath == "a.txt",
              "the first image keeps the masks and seeds that follow it");
        check(o.images[1].maskPaths == QStringList({"b_lungs.nii.gz"}) && o.images[1].seedsPath.isEmpty(),
              "the second image gets only its own mask");
    }
    {
        const CommandLineOptions o = parse("a.nii.gz -m a_mask.nii.gz --mask b_mask.nii.gz b.nii.gz");
        check(opens(o, 2) && o.images[0].maskPaths.size() == 2 && o.images[1].maskPaths.isEmpty(),
              "positional images pair the same way");
    }
    {
        const CommandLineOptions o = parse("-i a.nii.gz b.nii.gz c.nii.gz -m c_mask.nii.gz");
        check(opens(o, 3) && o.images[0].maskPaths.isEmpty() && o.images[2].maskPaths == QStringList({"c_mask.nii.gz"}),
              "after a list of images, a mask goes to the last one");
    }
    {
        const CommandLineOptions o = parse("-m lungs.nii.gz -s seeds.txt -i ct.nii.gz -m artery.nii.gz");
        check(opens(o, 1) && o.images[0].maskPaths == QStringList({"lungs.nii.gz", "artery.nii.gz"}) &&
                  o.images[0].seedsPath == "seeds.txt" && !o.withoutImage.hasFiles(),
              "files before any image go to the first, ahead of its own");
    }
    {
        const CommandLineOptions o = parse("-m lungs.nii.gz -s seeds.txt");
        check(opens(o, 0) && o.withoutImage.maskPaths == QStringList({"lungs.nii.gz"}) &&
                  o.withoutImage.seedsPath == "seeds.txt",
              "with no image, the files are kept apart to be reported");
    }

    check(failsWith(parse("-i a.nii.gz -s one.txt -s two.txt"), "--seeds given twice for a.nii.gz"),
          "a second seed file for one image is refused");
    check(failsWith(parse("-s one.txt -i a.nii.gz -s two.txt"), "--seeds given twice for a.nii.gz"),
          "including one named before the image");
    check(opens(parse("-i a.nii.gz -s a.txt -i b.nii.gz -s b.txt"), 2), "one seed file per image is fine");
    check(failsWith(parse("-i"), "--input requires") && failsWith(parse("-i -m x"), "--input requires"),
          "--input without a path is refused");
    check(failsWith(parse("-i a.nii.gz -m"), "--mask requires"), "--mask without a path is refused");
    check(failsWith(parse("-i a.nii.gz -s"), "--seeds requires"), "--seeds without a path is refused");
    check(failsWith(parse("--bogus"), "unknown option '--bogus'"), "an unknown option is refused");
    check(failsWith(parse("-i a.nii.gz --mask-required"), "--mask-required"),
          "--mask-required with no mask is refused");
    check(opens(parse("--mask-required -i a.nii.gz -m m.nii.gz"), 1), "--mask-required counts masks named after it");

    check(parse("-i a.nii.gz --help").action == CommandLineOptions::Action::Help, "--help");
    check(parse("--version").action == CommandLineOptions::Action::Version, "--version");
    check(parse("-f a.nii.gz").fullscreen && opens(parse("-f a.nii.gz"), 1), "--fullscreen");

    return failures ? 1 : 0;
}
