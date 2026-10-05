// Runs oiftrelax on synthetic planes through planar::segmentPlane: the int32 export, the
// border seeds that stand in for face seeding, and the refusals that start no process.
//
// usage: plane_segmentation_test <path-to-oiftrelax>
#include "PlaneSegmentation.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>

#include <itkImage.h>
#include <itkImageFileReader.h>
#include <itkImageRegionConstIterator.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <set>
#include <utility>
#include <vector>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-62s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

constexpr int kWidth = 64; // unequal sides catch a swapped u/v
constexpr int kHeight = 48;
constexpr int kCentreU = 30;
constexpr int kCentreV = 20;
constexpr int kRadius = 12;
constexpr double kMinDice = 0.9;

using PlaneImage = itk::Image<int32_t, 3>;

bool inDisc(int u, int v)
{
    const int du = u - kCentreU, dv = v - kCentreV;
    return du * du + dv * dv <= kRadius * kRadius;
}

// u-fastest, uniform integer noise in [-10, 10] from a fixed generator.
std::vector<float> discPlane(float background, float disc, float offset)
{
    std::mt19937 rng(0);
    std::uniform_int_distribution<int> noise(-10, 10);
    std::vector<float> pixels(std::size_t(kWidth) * kHeight);
    for (int v = 0; v < kHeight; ++v)
        for (int u = 0; u < kWidth; ++u)
            pixels[std::size_t(v) * kWidth + u] = (inDisc(u, v) ? disc : background) + float(noise(rng)) + offset;
    return pixels;
}

double discDice(const std::vector<int> &labels, int label)
{
    if (labels.size() != std::size_t(kWidth) * kHeight)
        return 0.0;
    long both = 0, segmented = 0, truth = 0;
    for (int v = 0; v < kHeight; ++v)
        for (int u = 0; u < kWidth; ++u)
        {
            const bool s = labels[std::size_t(v) * kWidth + u] == label;
            const bool t = inDisc(u, v);
            both += (s && t);
            segmented += s;
            truth += t;
        }
    return segmented + truth ? 2.0 * double(both) / double(segmented + truth) : 0.0;
}

bool labelsWithin(const std::vector<int> &labels, const std::set<int> &allowed)
{
    return !labels.empty() && std::all_of(labels.begin(), labels.end(),
                                          [&](int l) { return allowed.count(l) > 0; });
}

planar::RunRequest discRequest(const QString &exe, const QString &workDir)
{
    planar::RunRequest r;
    planar::makeGeometry(planar::Plane::Axial, 0, {kWidth, kHeight, 1}, &r.geometry);
    r.pixels = discPlane(50.0f, 200.0f, 0.0f);
    r.seeds = {Seed{kCentreU, kCentreV, 0, 2, 2}};
    r.borderBackground = true;
    r.pol = 1.0;
    r.niter = 0;
    r.percentile = 0;
    r.blurPasses = 2;
    r.executablePath = exe;
    r.workDir = workDir;
    return r;
}

struct SeedRow
{
    int x, y, z, marker, label;
};

// Rows of a seed file; false unless the count line matches the rows read.
bool readSeedFile(const QString &path, std::vector<SeedRow> *rows)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QStringList lines = QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
    if (lines.isEmpty())
        return false;
    const int count = lines.front().trimmed().toInt();
    rows->clear();
    for (int i = 1; i < lines.size(); ++i)
    {
        const QStringList f = lines[i].split(' ', Qt::SkipEmptyParts);
        if (f.size() != 5)
            return false;
        rows->push_back({f[0].toInt(), f[1].toInt(), f[2].toInt(), f[3].toInt(), f[4].toInt()});
    }
    return count == int(rows->size());
}

// Smallest and largest value of the int32 plane file.
bool readPlaneRange(const QString &path, int32_t *lo, int32_t *hi)
{
    auto reader = itk::ImageFileReader<PlaneImage>::New();
    reader->SetFileName(path.toStdString());
    try
    {
        reader->Update();
    }
    catch (const itk::ExceptionObject &e)
    {
        std::printf("    %s\n", e.GetDescription());
        return false;
    }
    *lo = INT32_MAX;
    *hi = INT32_MIN;
    itk::ImageRegionConstIterator<PlaneImage> it(reader->GetOutput(), reader->GetOutput()->GetLargestPossibleRegion());
    for (it.GoToBegin(); !it.IsAtEnd(); ++it)
    {
        *lo = std::min(*lo, it.Get());
        *hi = std::max(*hi, it.Get());
    }
    return true;
}

void printFailure(const planar::RunResult &result)
{
    if (!result.success)
        std::printf("    message: %s\n", qPrintable(result.message));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2)
    {
        std::printf("usage: plane_segmentation_test <path-to-oiftrelax>\n");
        return 2;
    }
    const QString exe = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();

    // 1. A bright disc, one object seed, background from the border.
    {
        QTemporaryDir work;
        check(work.isValid(), "bright disc: temporary directory created");
        const planar::RunResult result = planar::segmentPlane(discRequest(exe, work.path()));
        printFailure(result);
        const double dice = discDice(result.labels, 2);
        std::printf("    bright disc: Dice %.4f\n", dice);
        check(result.success, "bright disc: run succeeds");
        check(labelsWithin(result.labels, {0, 2}), "bright disc: labels within {0, 2}");
        check(dice >= kMinDice, "bright disc: Dice >= 0.9");
        check(!result.commandLine.isEmpty(), "bright disc: command line reported");

        using ReaderT = itk::ImageFileReader<PlaneImage>;
        auto reader = ReaderT::New();
        reader->SetFileName(QDir(work.path()).filePath("plane.nii.gz").toStdString());
        bool asIs = false, int32File = false;
        try
        {
            reader->Update();
            int32File = reader->GetImageIO()->GetComponentType() == itk::IOComponentEnum::INT;
            const std::vector<float> pixels = discPlane(50.0f, 200.0f, 0.0f);
            const PlaneImage *plane = reader->GetOutput();
            asIs = true;
            for (int v = 0; v < kHeight; ++v)
                for (int u = 0; u < kWidth; ++u)
                    asIs = asIs && plane->GetPixel({{u, v, 0}}) == std::lround(pixels[std::size_t(v) * kWidth + u]);
        }
        catch (const itk::ExceptionObject &e)
        {
            std::printf("    %s\n", e.GetDescription());
        }
        check(int32File, "bright disc: plane written as int32");
        check(asIs, "bright disc: integral values written as they are");
    }

    // 2. Anisotropic pixels: the file carries (du, dv, max(du, dv)).
    {
        QTemporaryDir work;
        check(work.isValid(), "anisotropic: temporary directory created");
        planar::RunRequest request = discRequest(exe, work.path());
        request.spacing = {0.7, 2.0};
        const planar::RunResult result = planar::segmentPlane(request);
        printFailure(result);
        const double dice = discDice(result.labels, 2);
        std::printf("    anisotropic disc: Dice %.4f\n", dice);
        check(result.success, "anisotropic: run succeeds");
        check(dice >= kMinDice, "anisotropic: Dice >= 0.9");

        auto reader = itk::ImageFileReader<PlaneImage>::New();
        reader->SetFileName(QDir(work.path()).filePath("plane.nii.gz").toStdString());
        bool spacingOk = false, sizeOk = false;
        try
        {
            reader->Update();
            const auto spacing = reader->GetOutput()->GetSpacing();
            const auto size = reader->GetOutput()->GetLargestPossibleRegion().GetSize();
            spacingOk = std::fabs(spacing[0] - 0.7) < 1e-5 && std::fabs(spacing[1] - 2.0) < 1e-5 &&
                        std::fabs(spacing[2] - 2.0) < 1e-5;
            sizeOk = size[0] == std::size_t(kWidth) && size[1] == std::size_t(kHeight) && size[2] == 1;
        }
        catch (const itk::ExceptionObject &e)
        {
            std::printf("    %s\n", e.GetDescription());
        }
        check(spacingOk, "anisotropic: plane spacing (0.7, 2.0, 2.0)");
        check(sizeOk, "anisotropic: plane size (64, 48, 1)");
    }

    // 3. Non-integral, all negative: the affine int32 export, not gft's float rescale.
    {
        QTemporaryDir work;
        check(work.isValid(), "negative non-integral: temporary directory created");
        planar::RunRequest request = discRequest(exe, work.path());
        request.pixels = discPlane(-1000.0f, -200.0f, 0.25f);
        const planar::RunResult result = planar::segmentPlane(request);
        printFailure(result);
        const double dice = discDice(result.labels, 2);
        std::printf("    negative non-integral disc: Dice %.4f\n", dice);
        check(result.success, "negative non-integral: run succeeds");
        check(dice >= kMinDice, "negative non-integral: Dice >= 0.9");

        int32_t lo = 0, hi = 0;
        const bool mapped = readPlaneRange(QDir(work.path()).filePath("plane.nii.gz"), &lo, &hi) && lo == 0 &&
                            hi == 10000;
        check(mapped, "negative non-integral: plane mapped onto [0, 10000]");
    }

    // 4. No border seeds and no background seed: the object takes the plane.
    {
        QTemporaryDir work;
        check(work.isValid(), "border off: temporary directory created");
        planar::RunRequest request = discRequest(exe, work.path());
        request.borderBackground = false;
        const planar::RunResult result = planar::segmentPlane(request);
        printFailure(result);
        check(result.success, "border off: run succeeds");
        check(labelsWithin(result.labels, {2}), "border off: every pixel labelled 2");
    }

    // 5. A user seed on the border keeps its label; no background row is added there.
    {
        QTemporaryDir work;
        check(work.isValid(), "border user seed: temporary directory created");
        planar::RunRequest request = discRequest(exe, work.path());
        request.seeds.push_back(Seed{0, kCentreV, 0, 2, 2});
        const planar::RunResult result = planar::segmentPlane(request);
        printFailure(result);
        check(result.success, "border user seed: run succeeds");

        std::vector<SeedRow> rows;
        const bool parsed = readSeedFile(QDir(work.path()).filePath("seeds.txt"), &rows);
        check(parsed, "border user seed: seed file count matches its rows");
        bool backgroundThere = false, userThere = false;
        std::set<std::pair<int, int>> background;
        for (const SeedRow &r : rows)
        {
            if (r.x == 0 && r.y == kCentreV && r.label == 0)
                backgroundThere = true;
            if (r.x == 0 && r.y == kCentreV && r.label == 2 && r.marker == 2)
                userThere = true;
            if (r.label == 0)
                background.insert({r.x, r.y});
        }
        const std::size_t edge = std::size_t(2 * kWidth + 2 * kHeight - 4);
        check(userThere && !backgroundThere, "border user seed: no label-0 row at (0, 20)");
        check(background.size() == edge - 1 && rows.size() == edge + 1,
              "border user seed: every other edge pixel seeded once");
        check(!rows.empty() && rows.front().x == kCentreU && rows.front().y == kCentreV,
              "border user seed: request seeds written first");
        check(result.labels.size() == std::size_t(kWidth) * kHeight &&
                  result.labels[std::size_t(kCentreV) * kWidth + 0] == 2,
              "border user seed: pixel (0, 20) labelled 2");
    }

    // 6. A missing executable is named, and nothing runs.
    {
        QTemporaryDir work;
        check(work.isValid(), "missing executable: temporary directory created");
        const QString missing = "/nonexistent/oiftrelax";
        const planar::RunResult result = planar::segmentPlane(discRequest(missing, work.path()));
        check(!result.success, "missing executable: refused");
        check(result.message.contains(missing), "missing executable: message names the path");
        check(QDir(work.path()).isEmpty(), "missing executable: nothing written");
    }

    // 7. No object seed.
    {
        QTemporaryDir work;
        check(work.isValid(), "no object seed: temporary directory created");
        planar::RunRequest request = discRequest(exe, work.path());
        request.seeds = {Seed{5, 5, 0, 0, 0}};
        const planar::RunResult result = planar::segmentPlane(request);
        check(!result.success, "no object seed: refused");
        check(!QFileInfo::exists(QDir(work.path()).filePath("seeds.txt")), "no object seed: no seeds.txt written");
    }

    // 8. Planes the border would cover entirely, and a pixel buffer of the wrong size.
    {
        QTemporaryDir work;
        check(work.isValid(), "64 x 2 plane: temporary directory created");
        planar::RunRequest request = discRequest(exe, work.path());
        planar::makeGeometry(planar::Plane::Axial, 0, {kWidth, 2, 1}, &request.geometry);
        request.pixels.assign(std::size_t(kWidth) * 2, 100.0f);
        request.seeds = {Seed{30, 1, 0, 2, 2}};
        const planar::RunResult thin = planar::segmentPlane(request);
        check(!thin.success, "64 x 2 plane: refused");
        check(QDir(work.path()).isEmpty(), "64 x 2 plane: nothing written");

        planar::RunRequest wrongSize = discRequest(exe, work.path());
        wrongSize.pixels.pop_back();
        check(!planar::segmentPlane(wrongSize).success, "pixel buffer of the wrong size: refused");
        check(QDir(work.path()).isEmpty(), "pixel buffer of the wrong size: nothing written");
    }

    // 9. Two seeds on one pixel: the later one alone reaches the file.
    {
        QTemporaryDir work;
        check(work.isValid(), "repeated pixel: temporary directory created");
        planar::RunRequest request = discRequest(exe, work.path());
        request.seeds = {Seed{kCentreU, kCentreV, 0, 1, 1}, Seed{kCentreU, kCentreV, 0, 2, 2}};
        const planar::RunResult result = planar::segmentPlane(request);
        printFailure(result);
        check(result.success, "repeated pixel: run succeeds");
        std::vector<SeedRow> rows;
        int atCentre = 0, labelAtCentre = -1;
        if (readSeedFile(QDir(work.path()).filePath("seeds.txt"), &rows))
            for (const SeedRow &r : rows)
                if (r.x == kCentreU && r.y == kCentreV)
                {
                    ++atCentre;
                    labelAtCentre = r.label;
                }
        check(atCentre == 1 && labelAtCentre == 2, "repeated pixel: one row, label 2");
    }

    // 10. An integral plane too wide for gft's buckets is mapped, not written as is.
    {
        QTemporaryDir work;
        check(work.isValid(), "wide integral range: temporary directory created");
        planar::RunRequest request = discRequest(exe, work.path());
        for (int v = 0; v < kHeight; ++v)
            for (int u = 0; u < kWidth; ++u)
                request.pixels[std::size_t(v) * kWidth + u] = inDisc(u, v) ? 1000000.0f : 0.0f;
        const planar::RunResult result = planar::segmentPlane(request);
        printFailure(result);
        check(result.success, "wide integral range: run succeeds");
        int32_t lo = -1, hi = -1;
        check(readPlaneRange(QDir(work.path()).filePath("plane.nii.gz"), &lo, &hi) && lo >= 0 && hi <= 10000,
              "wide integral range: plane within [0, 10000]");
    }

    // 11. Cancelled before the first poll: the process is killed and nothing is read.
    {
        QTemporaryDir work;
        check(work.isValid(), "cancelled: temporary directory created");
        planar::RunRequest request = discRequest(exe, work.path());
        request.cancelled = [] { return true; };
        QElapsedTimer timer;
        timer.start();
        const planar::RunResult result = planar::segmentPlane(request);
        const qint64 elapsed = timer.elapsed();
        check(!result.success && result.message == "Cancelled.", "cancelled: refused with \"Cancelled.\"");
        check(elapsed < 5000, "cancelled: returns within 5 s");
        check(result.labels.empty() && !QFileInfo::exists(QDir(work.path()).filePath("label.nii.gz")),
              "cancelled: no label plane");
    }

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
