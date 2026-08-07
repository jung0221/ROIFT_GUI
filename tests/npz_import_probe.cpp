// Command-line probe over the numpy importer, used by scripts/npz_import_selftest.py.
// Prints the resolved import as key=value lines and dumps the volume as raw
// float32 (X fastest) so the caller can compare it against numpy directly.
#include "NiftiImage.h"

#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <iostream>
#include <string>

namespace
{

void printUsage()
{
    std::cerr << "npz_import_probe <array.npz> <dump.raw> [--array NAME] [--channel N]\n"
              << "                 [--order auto|xyz|xzy|yxz|yzx|zxy|zyx] [--flip xyz]\n"
              << "                 [--spacing SX SY SZ] [--reference PATH]\n";
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        printUsage();
        return 2;
    }

    const std::string path = argv[1];
    const std::string dumpPath = argv[2];
    NpzImportOptions options;

    for (int i = 3; i < argc; ++i)
    {
        const std::string flag = argv[i];
        auto next = [&](const char *what) -> std::string
        {
            if (i + 1 >= argc)
            {
                std::cerr << "missing value for " << what << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (flag == "--array")
            options.arrayName = next("--array");
        else if (flag == "--channel")
            options.channel = std::atoi(next("--channel").c_str());
        else if (flag == "--reference")
            options.referencePath = next("--reference");
        else if (flag == "--order")
        {
            std::string value = next("--order");
            for (char &c : value)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            static const NpzImportOptions::AxisOrder orders[] = {
                NpzImportOptions::AxisOrder::XYZ, NpzImportOptions::AxisOrder::XZY,
                NpzImportOptions::AxisOrder::YXZ, NpzImportOptions::AxisOrder::YZX,
                NpzImportOptions::AxisOrder::ZXY, NpzImportOptions::AxisOrder::ZYX};
            options.axisOrder = NpzImportOptions::AxisOrder::Auto;
            for (NpzImportOptions::AxisOrder candidate : orders)
                if (value == npzAxisOrderName(candidate))
                    options.axisOrder = candidate;
            if (value != "AUTO" && options.axisOrder == NpzImportOptions::AxisOrder::Auto)
            {
                std::cerr << "unknown --order '" << value << "'\n";
                return 2;
            }
        }
        else if (flag == "--flip")
        {
            const std::string value = next("--flip"); // any of "x", "y", "z"
            for (char c : value)
            {
                if (c == 'x' || c == 'X') options.flip[0] = true;
                else if (c == 'y' || c == 'Y') options.flip[1] = true;
                else if (c == 'z' || c == 'Z') options.flip[2] = true;
            }
        }
        else if (flag == "--spacing")
        {
            for (int a = 0; a < 3; ++a)
                options.spacing[a] = std::atof(next("--spacing").c_str());
        }
        else
        {
            printUsage();
            return 2;
        }
    }

    NiftiImage image;
    NpzImportReport report;
    std::string error;
    if (!image.loadNumpy(path, options, &report, &error))
    {
        std::cout << "status=error\nmessage=" << error << "\n";
        return 1;
    }

    std::cout << "status=ok\n"
              << "array=" << report.arrayName << "\n"
              << "channel=" << report.channel << "\n"
              << "channel_count=" << report.channelCount << "\n"
              << "size=" << image.getSizeX() << "," << image.getSizeY() << "," << image.getSizeZ() << "\n"
              << "spacing=" << image.getSpacingX() << "," << image.getSpacingY() << "," << image.getSpacingZ() << "\n"
              << "axis_order=" << npzAxisOrderName(report.axisOrder) << "\n"
              << "flip=" << (report.flip[0] ? "X" : "-") << (report.flip[1] ? "Y" : "-")
              << (report.flip[2] ? "Z" : "-") << "\n"
              << "geometry_source=" << report.geometrySource << "\n"
              << "geometry_resolved=" << (report.geometryResolved ? 1 : 0) << "\n"
              << "is_mask=" << (image.isMask() ? 1 : 0) << "\n";

    FILE *dump = std::fopen(dumpPath.c_str(), "wb");
    if (!dump)
    {
        std::cerr << "could not open dump file " << dumpPath << "\n";
        return 1;
    }
    for (unsigned int z = 0; z < image.getSizeZ(); ++z)
        for (unsigned int y = 0; y < image.getSizeY(); ++y)
            for (unsigned int x = 0; x < image.getSizeX(); ++x)
            {
                const float value = image.getVoxelValue(x, y, z);
                std::fwrite(&value, sizeof(value), 1, dump);
            }
    std::fclose(dump);
    return 0;
}
