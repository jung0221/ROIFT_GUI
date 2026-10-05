#include "PlaneSegmentation.h"
#include "SeedFiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>

#include <itkImage.h>
#include <itkImageFileReader.h>
#include <itkImageFileWriter.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <utility>

namespace planar
{

namespace
{

using PlaneImage = itk::Image<int32_t, 3>;

// The border seeds would cover a plane thinner than this, leaving nothing to segment.
constexpr int kMinPlaneSide = 3;
// Non-integral planes are mapped onto [0, kIntegerRange] before the int32 export.
constexpr double kIntegerRange = 10000.0;
constexpr double kIntegralTolerance = 1e-6;
constexpr int kFailureTailLines = 3;

QString validate(const RunRequest &r)
{
    const Geometry &g = r.geometry;
    if (g.width < kMinPlaneSide || g.height < kMinPlaneSide)
        return QString("The plane is %1 x %2 pixels; segmenting one needs at least %3 x %3.")
            .arg(g.width).arg(g.height).arg(kMinPlaneSide);
    if (r.pixels.size() != std::size_t(g.width) * std::size_t(g.height))
        return QString("The plane holds %1 pixels, not %2 x %3.").arg(r.pixels.size()).arg(g.width).arg(g.height);
    if (!std::all_of(r.pixels.begin(), r.pixels.end(), [](float v) { return std::isfinite(v); }))
        return "The plane holds a pixel that is not a finite number.";
    if (!(r.spacing[0] > 0.0 && r.spacing[1] > 0.0 && std::isfinite(r.spacing[0]) && std::isfinite(r.spacing[1])))
        return QString("The plane spacing (%1, %2) is not positive and finite.").arg(r.spacing[0]).arg(r.spacing[1]);
    // oiftrelax would skip a seed outside the plane silently.
    for (const Seed &s : r.seeds)
        if (s.x < 0 || s.x >= g.width || s.y < 0 || s.y >= g.height || s.z != 0)
            return QString("Seed (%1, %2, %3) lies outside the %4 x %5 plane.")
                .arg(s.x).arg(s.y).arg(s.z).arg(g.width).arg(g.height);
    if (std::none_of(r.seeds.begin(), r.seeds.end(), [](const Seed &s) { return s.label > 0; }))
        return "The plane has no object seed (label > 0).";
    if (r.executablePath.isEmpty() || !QFileInfo(r.executablePath).isFile())
        return QString("oiftrelax not found: \"%1\".").arg(r.executablePath);
    if (r.workDir.isEmpty() || !QDir(r.workDir).exists())
        return QString("The working directory \"%1\" does not exist.").arg(r.workDir);
    return {};
}

// gft reads int32 as is but rescales float32 by its maximum from 0, which fails for a
// plane whose maximum is <= 0; so integral planes go as they are and others onto [0, 10000].
std::vector<int32_t> toInt32(const std::vector<float> &pixels)
{
    const auto [lo, hi] = std::minmax_element(pixels.begin(), pixels.end());
    const double minimum = *lo, maximum = *hi;
    const bool fitsInt32 = minimum >= double(std::numeric_limits<int32_t>::min()) &&
                           maximum <= double(std::numeric_limits<int32_t>::max());
    const bool integral = fitsInt32 && std::all_of(pixels.begin(), pixels.end(), [](float v)
                                                   { return std::fabs(double(v) - std::round(double(v))) < kIntegralTolerance; });

    std::vector<int32_t> out(pixels.size(), 0);
    if (integral)
    {
        std::transform(pixels.begin(), pixels.end(), out.begin(), [](float v) { return int32_t(std::lround(v)); });
        return out;
    }
    const double range = maximum - minimum;
    if (range > 0.0)
        std::transform(pixels.begin(), pixels.end(), out.begin(), [&](float v)
                       { return int32_t(std::lround((double(v) - minimum) * kIntegerRange / range)); });
    return out;
}

QString writePlane(const RunRequest &r, const QString &path)
{
    const Geometry &g = r.geometry;
    auto image = PlaneImage::New();
    PlaneImage::SizeType size;
    size[0] = itk::SizeValueType(g.width);
    size[1] = itk::SizeValueType(g.height);
    size[2] = 1;
    PlaneImage::RegionType region;
    region.SetSize(size);
    image->SetRegions(region);
    // gft blurs with min(dx, dy, dz), so dz must not undercut the in-plane spacing.
    PlaneImage::SpacingType spacing;
    spacing[0] = r.spacing[0];
    spacing[1] = r.spacing[1];
    spacing[2] = std::max(r.spacing[0], r.spacing[1]);
    image->SetSpacing(spacing);
    image->Allocate();
    const std::vector<int32_t> values = toInt32(r.pixels);
    std::copy(values.begin(), values.end(), image->GetBufferPointer()); // u-fastest is x-fastest

    auto writer = itk::ImageFileWriter<PlaneImage>::New();
    writer->SetFileName(path.toStdString());
    writer->SetInput(image);
    try
    {
        writer->Update();
    }
    catch (const itk::ExceptionObject &e)
    {
        return QString("Could not write the plane to %1: %2").arg(path, e.GetDescription());
    }
    return {};
}

// Request seeds first, then background on every border pixel no request seed holds.
std::vector<Seed> runSeeds(const RunRequest &r)
{
    std::vector<Seed> seeds = r.seeds;
    if (!r.borderBackground)
        return seeds;
    std::set<std::pair<int, int>> taken;
    for (const Seed &s : r.seeds)
        taken.insert({s.x, s.y});
    for (const auto &[u, v] : borderPixels(r.geometry))
        if (!taken.count({u, v}))
            seeds.push_back(Seed{u, v, 0, 0, 0});
    return seeds;
}

QString quoteCommand(const QString &exe, const QStringList &args)
{
    QStringList quoted{'"' + exe + '"'};
    for (const QString &arg : args)
        quoted << '"' + arg + '"';
    return quoted.join(' ');
}

// Appends the last non-empty lines of the process output to message.
QString withOutputTail(const QString &message, const QString &output)
{
    QStringList lines;
    for (const QString &line : QString(output).replace('\r', '\n').split('\n'))
        if (!line.trimmed().isEmpty())
            lines << line.trimmed();
    lines = lines.mid(std::max<qsizetype>(0, lines.size() - kFailureTailLines));
    return lines.isEmpty() ? message : message + '\n' + lines.join('\n');
}

QString readLabels(const QString &path, const Geometry &g, std::vector<int> *labels)
{
    auto reader = itk::ImageFileReader<PlaneImage>::New();
    reader->SetFileName(path.toStdString());
    try
    {
        reader->Update();
    }
    catch (const itk::ExceptionObject &e)
    {
        return QString("Could not read the label plane %1: %2").arg(path, e.GetDescription());
    }
    const PlaneImage *image = reader->GetOutput();
    const auto size = image->GetLargestPossibleRegion().GetSize();
    if (size[0] != std::size_t(g.width) || size[1] != std::size_t(g.height) || size[2] != 1)
        return QString("The label plane is %1 x %2 x %3, not %4 x %5 x 1.")
            .arg(size[0]).arg(size[1]).arg(size[2]).arg(g.width).arg(g.height);
    const int32_t *buffer = image->GetBufferPointer();
    labels->assign(buffer, buffer + std::size_t(g.width) * std::size_t(g.height));
    return {};
}

} // namespace

RunResult segmentPlane(const RunRequest &request)
{
    RunResult result;
    result.message = validate(request);
    if (!result.message.isEmpty())
        return result;

    const QDir work(request.workDir);
    const QString planePath = work.absoluteFilePath("plane.nii.gz");
    const QString seedPath = work.absoluteFilePath("seeds.txt");
    const QString labelPath = work.absoluteFilePath("label.nii.gz");

    result.message = writePlane(request, planePath);
    if (!result.message.isEmpty())
        return result;
    if (!writeMultilabelSeedFile(seedPath, runSeeds(request)))
    {
        result.message = QString("Could not write the seed file %1.").arg(seedPath);
        return result;
    }
    // A label file left by an earlier run must not pass for this one's.
    if (QFileInfo::exists(labelPath) && !QFile::remove(labelPath))
    {
        result.message = QString("Could not remove the stale label plane %1.").arg(labelPath);
        return result;
    }

    // Stride 0 turns face seeding off and the empty slot 8 is skipped, leaving --blur.
    const QStringList args{planePath,
                           seedPath,
                           QString::number(request.pol),
                           QString::number(request.niter),
                           QString::number(request.percentile),
                           labelPath,
                           "0",
                           "",
                           "--blur",
                           QString::number(request.blurPasses)};
    result.commandLine = quoteCommand(request.executablePath, args);

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setWorkingDirectory(work.absolutePath());
    process.start(request.executablePath, args);
    if (!process.waitForStarted(-1))
    {
        result.message = QString("Could not start %1: %2").arg(request.executablePath, process.errorString());
        return result;
    }
    process.waitForFinished(-1);
    const QString output = QString::fromLocal8Bit(process.readAll());

    if (process.exitStatus() == QProcess::CrashExit)
    {
        result.message = withOutputTail(QString("oiftrelax crashed (exit code %1).").arg(process.exitCode()), output);
        return result;
    }
    if (process.exitCode() != 0)
    {
        result.message = withOutputTail(QString("oiftrelax exited with code %1.").arg(process.exitCode()), output);
        return result;
    }
    if (!QFileInfo::exists(labelPath))
    {
        result.message = withOutputTail(QString("oiftrelax finished but wrote no label plane %1.").arg(labelPath), output);
        return result;
    }

    result.message = readLabels(labelPath, request.geometry, &result.labels);
    if (!result.message.isEmpty())
    {
        result.labels.clear();
        return result;
    }
    result.success = true;
    result.message = QString("Segmented the %1 x %2 plane.").arg(request.geometry.width).arg(request.geometry.height);
    return result;
}

} // namespace planar
