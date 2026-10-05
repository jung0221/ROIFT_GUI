// Drives the real window with raster images, the input the plane mode exists for.
//
// oiftrelax and the Python helpers read files, not the volume in memory, and none
// of them reads PNG or TIFF; nativeImagePath() must hand them a NIfTI holding the
// voxels of the image now selected, in a directory that goes away with the window.
// A one-slice image has no sagittal, coronal or 3D picture, so it fills the view area.
// A slice run writes oiftrelax's labels into one plane of the edited mask and nowhere else.
#include "ManualSeedSelector.h"
#include "SegmentationRunner.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <itkImage.h>
#include <itkImageFileReader.h>
#include <itkImageFileWriter.h>
#include <itkImageRegionIteratorWithIndex.h>
#include <itkNiftiImageIO.h>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-62s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

using ValueAt = std::function<float(unsigned int, unsigned int, unsigned int)>;

// The grid of the slice-run volume.
constexpr unsigned int kSliceX = 40, kSliceY = 48, kSliceZ = 32;
constexpr std::size_t kSliceVoxels = std::size_t(kSliceX) * kSliceY * kSliceZ;

std::size_t sliceIndex(unsigned int x, unsigned int y, unsigned int z)
{
    return x + std::size_t(kSliceX) * (y + std::size_t(kSliceY) * z);
}

// Dice of label 1 against the disc of radius 10 around (cu, cv) on a width x height plane,
// whose pixel (u, v) is labels[voxelAt(u, v)].
double discDice(const std::vector<int> &labels, unsigned int width, unsigned int height, int cu, int cv,
                const char *name, const std::function<std::size_t(unsigned int, unsigned int)> &voxelAt)
{
    if (labels.size() != kSliceVoxels)
        return 0.0;
    int both = 0, labelled = 0, inDisc = 0;
    for (unsigned int v = 0; v < height; ++v)
        for (unsigned int u = 0; u < width; ++u)
        {
            const int du = int(u) - cu, dv = int(v) - cv;
            const bool disc = du * du + dv * dv <= 100;
            const bool one = labels[voxelAt(u, v)] == 1;
            both += disc && one;
            labelled += one;
            inDisc += disc;
        }
    const double dice = labelled + inDisc > 0 ? 2.0 * both / (labelled + inDisc) : 0.0;
    std::printf("  %s: Dice %.3f (%d label-1 pixels, %d in the disc)\n", name, dice, labelled, inDisc);
    return dice;
}

// Write an image of the given size, 8-bit unless told otherwise; 2D when sizeZ is 1.
template <unsigned int Dimension, typename PixelT = unsigned char>
bool writeImage(const QString &path, const unsigned int (&size)[3], const ValueAt &valueAt)
{
    using ImageT = itk::Image<PixelT, Dimension>;
    auto image = ImageT::New();
    typename ImageT::SizeType imageSize;
    for (unsigned int i = 0; i < Dimension; ++i)
        imageSize[i] = size[i];
    typename ImageT::RegionType region;
    region.SetSize(imageSize);
    image->SetRegions(region);
    image->Allocate();
    itk::ImageRegionIteratorWithIndex<ImageT> it(image, region);
    for (it.GoToBegin(); !it.IsAtEnd(); ++it)
    {
        const typename ImageT::IndexType i = it.GetIndex();
        const unsigned int z = Dimension > 2 ? static_cast<unsigned int>(i[Dimension - 1]) : 0;
        it.Set(static_cast<PixelT>(valueAt(i[0], i[1], z)));
    }
    try
    {
        auto writer = itk::ImageFileWriter<ImageT>::New();
        writer->SetFileName(path.toStdString());
        writer->SetInput(image);
        writer->Update();
    }
    catch (const itk::ExceptionObject &e)
    {
        std::printf("could not write %s: %s\n", qPrintable(path), e.GetDescription());
        return false;
    }
    return true;
}

// True when path is a NIfTI of this size whose voxels are valueAt.
bool exportHolds(const std::string &path, const unsigned int (&size)[3], const ValueAt &valueAt)
{
    using FloatVolume = itk::Image<float, 3>;
    FloatVolume::Pointer volume;
    try
    {
        auto reader = itk::ImageFileReader<FloatVolume>::New();
        reader->SetImageIO(itk::NiftiImageIO::New());
        reader->SetFileName(path);
        reader->Update();
        volume = reader->GetOutput();
    }
    catch (const itk::ExceptionObject &e)
    {
        std::printf("could not read %s as NIfTI: %s\n", path.c_str(), e.GetDescription());
        return false;
    }
    const FloatVolume::SizeType actual = volume->GetLargestPossibleRegion().GetSize();
    if (actual[0] != size[0] || actual[1] != size[1] || actual[2] != size[2])
        return false;
    for (unsigned int z = 0; z < size[2]; ++z)
        for (unsigned int y = 0; y < size[1]; ++y)
            for (unsigned int x = 0; x < size[0]; ++x)
            {
                FloatVolume::IndexType index;
                index[0] = x;
                index[1] = y;
                index[2] = z;
                if (volume->GetPixel(index) != valueAt(x, y, z))
                    return false;
            }
    return true;
}

int rowForPath(QListWidget *list, const QString &path)
{
    const QString wanted = QFileInfo(path).absoluteFilePath();
    for (int row = 0; row < list->count(); ++row)
        if (list->item(row)->data(Qt::UserRole).toString() == wanted)
            return row;
    return -1;
}

// Select the list row of path, then ask for the file the tools would be handed.
std::string selectAndExport(ManualSeedSelector &window, QListWidget *list, const QString &path)
{
    const int row = rowForPath(list, path);
    if (row < 0)
        return std::string();
    list->setCurrentRow(row);
    return window.nativeImagePath();
}

// Let posted layout requests run so widget geometry reflects the current layout.
void settle()
{
    for (int i = 0; i < 5; ++i)
        QCoreApplication::processEvents();
}

double widthShare(const QWidget *panel, const QWidget *container)
{
    return container->width() > 0 ? double(panel->width()) / container->width() : 0.0;
}

double heightShare(const QWidget *panel, const QWidget *container)
{
    return container->height() > 0 ? double(panel->height()) / container->height() : 0.0;
}


} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QTemporaryDir workDir;
    if (!workDir.isValid())
    {
        std::printf("could not create a temporary directory\n");
        return 1;
    }
    const QDir dir(workDir.path());
    const unsigned int stackSize[3] = {8, 6, 3};
    const unsigned int planeSize[3] = {40, 30, 1};
    const ValueAt stackValue = [](unsigned int x, unsigned int y, unsigned int z)
    { return static_cast<float>(x + 8 * y + 48 * z); };
    const ValueAt planeValue = [](unsigned int x, unsigned int y, unsigned int) { return static_cast<float>(x + y); };
    const ValueAt firstValue = [](unsigned int x, unsigned int, unsigned int) { return static_cast<float>(x); };
    const ValueAt secondValue = [](unsigned int x, unsigned int y, unsigned int) { return static_cast<float>(200 - x - y); };

    const QString tifPath = dir.filePath("stack.tif");
    const QString pngPath = dir.filePath("plane.png");
    dir.mkdir("a");
    dir.mkdir("b");
    const QString firstPath = dir.filePath("a/image.png");
    const QString secondPath = dir.filePath("b/image.png");
    const QString badPath = dir.filePath("bad.png");
    QFile bad(badPath);
    const bool wroteBad = bad.open(QIODevice::WriteOnly) && bad.write("not a picture") > 0;
    bad.close();
    const bool wrote = writeImage<3>(tifPath, stackSize, stackValue) && writeImage<2>(pngPath, planeSize, planeValue) &&
                       writeImage<2>(firstPath, planeSize, firstValue) &&
                       writeImage<2>(secondPath, planeSize, secondValue) && wroteBad;
    check(wrote, "fixtures written");
    if (!wrote)
        return 1;

    auto window = std::make_unique<ManualSeedSelector>("");
    window->addImagesFromPaths({tifPath, pngPath, firstPath, secondPath, badPath});
    check(window->hasImage(), "stack.tif opens as the image");
    QListWidget *images = window->findChild<QListWidget *>("imageList");
    QLabel *status = window->findChild<QLabel *>("statusLabel");
    check(images != nullptr && images->count() == 5 && status != nullptr, "image list holds the five files");
    if (!images || images->count() != 5 || !status)
        return 1;

    const std::string native = window->nativeImagePath();
    const QString nativePath = QString::fromStdString(native);
    check(QFileInfo(nativePath).absoluteFilePath() != QFileInfo(tifPath).absoluteFilePath(),
          "stack.tif: nativeImagePath is not the .tif");
    check(nativePath.endsWith(".nii.gz") && QFileInfo::exists(nativePath), "stack.tif: export is an existing .nii.gz");
    check(exportHolds(native, stackSize, stackValue), "stack.tif: export is 8 x 6 x 3 with the .tif voxels");

    // <temp>/<window directory>/<n>/roift_src_<base>.nii.gz: a fresh <n> per export.
    const QString exportDir = QFileInfo(nativePath).absolutePath();
    const QString windowDir = QFileInfo(exportDir).absolutePath();
    const QString tempRoot = QDir::cleanPath(QDir::temp().absolutePath());
    check(QDir::cleanPath(windowDir) != tempRoot && QDir::cleanPath(QFileInfo(windowDir).absolutePath()) == tempRoot,
          "export lives in a per-window directory under temp, not in temp");
    check(QFileInfo(nativePath).fileName() == "roift_src_stack.nii.gz", "export keeps the name roift_src_<base>.nii.gz");

    const std::string planeExport = selectAndExport(*window, images, pngPath);
    check(QString::fromStdString(planeExport).endsWith(".nii.gz") && exportHolds(planeExport, planeSize, planeValue),
          "plane.png: export is 40 x 30 x 1 with the .png values");

    // A queued run may still be reading an earlier export, so none is ever overwritten.
    const std::string first = selectAndExport(*window, images, firstPath);
    check(exportHolds(first, planeSize, firstValue), "a/image.png: export holds a/image.png");
    const std::string second = selectAndExport(*window, images, secondPath);
    check(exportHolds(second, planeSize, secondValue), "b/image.png (same name): export holds b/image.png");
    check(first != second, "a/image.png and b/image.png export to different paths");
    check(exportHolds(first, planeSize, firstValue), "the export of a/image.png survives exporting b/image.png");
    const std::string firstAgain = selectAndExport(*window, images, firstPath);
    check(exportHolds(firstAgain, planeSize, firstValue), "a/image.png reselected: export holds a/image.png again");

    const ValueAt changedValue = [](unsigned int, unsigned int y, unsigned int) { return static_cast<float>(7 * y); };
    const bool rewrote = writeImage<2>(firstPath, planeSize, changedValue);
    images->setCurrentRow(rowForPath(images, pngPath));
    const std::string changed = selectAndExport(*window, images, firstPath);
    check(rewrote && exportHolds(changed, planeSize, changedValue),
          "a/image.png rewritten: reselected export holds the new file");
    check(changed != firstAgain && exportHolds(first, planeSize, firstValue) &&
              exportHolds(firstAgain, planeSize, firstValue),
          "re-exporting a/image.png leaves its earlier exports unchanged");

    images->setCurrentRow(rowForPath(images, badPath));
    std::printf("  status: %s\n", qPrintable(status->text()));
    check(status->text().startsWith("Could not read bad.png: ") && !status->text().endsWith(": "),
          "an unreadable file shows its reason in the status bar");
    check(!status->text().contains("Tried to create"), "the reason is ITK's first line, not its list of readers");

    window.reset();
    check(!QFileInfo::exists(nativePath) && !QFileInfo::exists(windowDir),
          "closing the window removes its export directory");

    // Panel geometry needs a shown window; the window above is never shown.
    {
        const QString grayPath = dir.filePath("gray.png");
        const QString volumePath = dir.filePath("volume.nii.gz");
        const unsigned int volumeSize[3] = {12, 10, 8};
        const bool wroteLayout = writeImage<2>(grayPath, planeSize, planeValue) &&
                                 writeImage<3, std::int16_t>(volumePath, volumeSize, stackValue);
        check(wroteLayout, "layout fixtures written");

        ManualSeedSelector shown("");
        shown.resize(1200, 900);
        shown.show();
        settle();

        QListWidget *list = shown.findChild<QListWidget *>("imageList");
        QWidget *container = shown.findChild<QWidget *>("viewContainer");
        QWidget *axialPanel = shown.findChild<QWidget *>("axialPanel");
        QWidget *sagittalPanel = shown.findChild<QWidget *>("sagittalPanel");
        QWidget *coronalPanel = shown.findChild<QWidget *>("coronalPanel");
        QWidget *renderPanel = shown.findChild<QWidget *>("renderPanel");
        OrthogonalView *axialView = shown.findChild<OrthogonalView *>("axialView");
        QLabel *axialTitle = shown.findChild<QLabel *>("axialTitle");
        QSlider *axialSlider = shown.findChild<QSlider *>("axialSlider");
        const bool found = wroteLayout && list && container && axialPanel && sagittalPanel && coronalPanel &&
                           renderPanel && axialView && axialTitle && axialSlider;
        check(found, "view container, panels, axial view, title and slider are named");
        if (!found)
            return 1;

        shown.addImagesFromPaths({grayPath, volumePath});
        list->setCurrentRow(rowForPath(list, grayPath));
        settle();
        std::printf("  gray.png: axial panel %dx%d in container %dx%d, title \"%s\"\n", axialPanel->width(),
                    axialPanel->height(), container->width(), container->height(), qPrintable(axialTitle->text()));
        check(shown.hasImage(), "gray.png opens as the image");
        check(sagittalPanel->isHidden() && coronalPanel->isHidden() && renderPanel->isHidden(),
              "gray.png: sagittal, coronal and 3D panels are hidden");
        check(!axialPanel->isHidden() && axialPanel->isVisible(), "gray.png: axial panel is shown");
        check(widthShare(axialPanel, container) >= 0.9 && heightShare(axialPanel, container) >= 0.9,
              "gray.png: axial panel fills the view area");
        check(axialView->image().size() == QSize(40, 30), "gray.png: axial view shows the 40 x 30 image");
        check(axialTitle->text() == "Image", "gray.png: axial panel is titled Image");
        check(!axialSlider->isVisible(), "gray.png: the axial slider row is hidden");

        list->setCurrentRow(rowForPath(list, volumePath));
        settle();
        std::printf("  volume: axial panel %dx%d in container %dx%d, title \"%s\"\n", axialPanel->width(),
                    axialPanel->height(), container->width(), container->height(), qPrintable(axialTitle->text()));
        check(axialPanel->isVisible() && sagittalPanel->isVisible() && coronalPanel->isVisible() &&
                  renderPanel->isVisible(),
              "12 x 10 x 8 volume: all four panels are shown again");
        const double share = widthShare(axialPanel, container);
        check(share >= 0.35 && share <= 0.65, "12 x 10 x 8 volume: axial panel takes half the width");
        check(axialTitle->text().startsWith("Axial: "), "12 x 10 x 8 volume: axial panel is titled Axial");
        check(axialSlider->isVisible(), "12 x 10 x 8 volume: the axial slider row is shown");
    }

    // A one-slice image is segmented as a slice; a volume as a whole or one slice at a time,
    // and a slice run has no batch, sweep, GPU, method or legacy mode.
    {
        const QString grayPath = dir.filePath("gray.png");
        const QString volumePath = dir.filePath("volume.nii.gz");
        ManualSeedSelector scoped("");
        scoped.addImagesFromPaths({grayPath, volumePath});
        QListWidget *list = scoped.findChild<QListWidget *>("imageList");
        QComboBox *scope = scoped.findChild<QComboBox *>("segmentationScope");
        QComboBox *plane = scoped.findChild<QComboBox *>("segmentationPlane");
        QCheckBox *border = scoped.findChild<QCheckBox *>("planeBorderBackground");
        QComboBox *mode = scoped.findChild<QComboBox *>("segmentationMode");
        const QList<QWidget *> volumeOnly{scoped.findChild<QWidget *>("segmentAll"),
                                          scoped.findChild<QWidget *>("polaritySweep"),
                                          scoped.findChild<QWidget *>("useGpu"),
                                          scoped.findChild<QWidget *>("segmentationMethod"), mode};
        const bool found = list && scope && plane && border && !volumeOnly.contains(nullptr);
        check(found, "scope, plane, border and the five volume controls are named");
        if (!found)
            return 1;
        const auto allEnabled = [&volumeOnly](bool enabled)
        {
            return std::all_of(volumeOnly.begin(), volumeOnly.end(),
                               [enabled](const QWidget *w) { return w->isEnabled() == enabled; });
        };

        list->setCurrentRow(rowForPath(list, grayPath));
        check(scoped.hasImage() && scope->currentText() == "Current slice" && !scope->isEnabled(),
              "gray.png: scope reads Current slice and is disabled");
        check(plane->currentText() == "Axial" && !plane->isEnabled(), "gray.png: plane reads Axial and is disabled");

        list->setCurrentRow(rowForPath(list, volumePath));
        check(scope->currentText() == "Volume" && scope->isEnabled(),
              "volume after gray.png: scope reads Volume and is enabled");
        mode->setCurrentIndex(mode->findText("Legacy binary"));
        scope->setCurrentIndex(scope->findText("Current slice"));
        check(plane->isEnabled() && border->isEnabled(), "volume, Current slice: plane and border enabled");
        check(allEnabled(false), "volume, Current slice: batch, sweep, GPU, method and mode disabled");
        check(mode->currentText() == "Multi-label", "volume, Current slice: mode reads Multi-label");

        scope->setCurrentIndex(scope->findText("Volume"));
        check(allEnabled(true), "back to Volume: batch, sweep, GPU, method and mode enabled");
        check(mode->currentText() == "Legacy binary", "back to Volume: the mode chosen before is restored");
        check(!plane->isEnabled() && !border->isEnabled(), "back to Volume: plane and border disabled");

        // A slice run is Standard OIFT whatever Method shows, so its Smoothing row is the one shown.
        QComboBox *method = scoped.findChild<QComboBox *>("segmentationMethod");
        QWidget *alpha = scoped.findChild<QWidget *>("segmentationAlpha");
        QWidget *sigma = scoped.findChild<QWidget *>("segmentationSigma");
        QWidget *smoothing = scoped.findChild<QWidget *>("segmentationSmoothing");
        const bool rowsFound = method && alpha && sigma && smoothing;
        check(rowsFound, "method, alpha, sigma and smoothing are named");
        if (!rowsFound)
            return 1;
        method->setCurrentIndex(method->findText("Gradient Weight (1A)"));
        check(!alpha->isHidden() && sigma->isHidden() && smoothing->isHidden(),
              "Volume, Gradient Weight: alpha shown, sigma and smoothing hidden");
        scope->setCurrentIndex(scope->findText("Current slice"));
        check(!smoothing->isHidden() && smoothing->isEnabled() && alpha->isHidden() && sigma->isHidden(),
              "Current slice, Gradient Weight kept: smoothing shown and enabled, alpha and sigma hidden");
        scope->setCurrentIndex(scope->findText("Volume"));
        check(!alpha->isHidden() && sigma->isHidden() && smoothing->isHidden(),
              "back to Volume: Gradient Weight's rows again");
    }

    // Slice mode runs only the standard CPU binary, whatever ROIFT_EXECUTABLE names.
    {
        const QByteArray savedExecutable = qgetenv("ROIFT_EXECUTABLE");
        const QByteArray savedPath = qgetenv("PATH");
#if defined(Q_OS_WIN)
        const QString suffix = ".exe";
#else
        const QString suffix;
#endif
        auto touch = [](const QString &path)
        {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write("#") > 0;
        };
        const QString gpuPath = dir.filePath("oiftrelax_gpu" + suffix);
        const QString parallelPath = dir.filePath("oiftrelax_parallel" + suffix);
        const QString cpuPath = dir.filePath("oiftrelax" + suffix);
        const bool made = touch(gpuPath) && touch(parallelPath) && touch(cpuPath);
        qputenv("ROIFT_EXECUTABLE", QFile::encodeName(gpuPath));
        QString whyNot;
        check(made && SegmentationRunner::resolveCpuRoiftExecutable(&whyNot).isEmpty(),
              "CPU resolver: ROIFT_EXECUTABLE naming oiftrelax_gpu yields none");
        std::printf("  CPU resolver, oiftrelax_gpu: %s\n", qPrintable(whyNot));
        check(whyNot.startsWith("ROIFT_EXECUTABLE names oiftrelax_gpu"),
              "CPU resolver: the reason names the binary ROIFT_EXECUTABLE names");
        qputenv("ROIFT_EXECUTABLE", QFile::encodeName(parallelPath));
        check(SegmentationRunner::resolveCpuRoiftExecutable().isEmpty(),
              "CPU resolver: ROIFT_EXECUTABLE naming oiftrelax_parallel yields none");
        qputenv("ROIFT_EXECUTABLE", QFile::encodeName(cpuPath));
        check(SegmentationRunner::resolveCpuRoiftExecutable() == QFileInfo(cpuPath).absoluteFilePath(),
              "CPU resolver: ROIFT_EXECUTABLE naming oiftrelax is used");

        // CTest names the built oiftrelax only when the tree has one; roift/gpu/oiftrelax_gpu
        // sits ahead of roift/oiftrelax in the folder search.
        if (!savedExecutable.isEmpty())
        {
            QTemporaryDir emptyPathDir;
            qunsetenv("ROIFT_EXECUTABLE");
            qputenv("PATH", QFile::encodeName(emptyPathDir.path()));
            const QString found = SegmentationRunner::resolveCpuRoiftExecutable();
            std::printf("  CPU resolver, folder search: \"%s\"\n", qPrintable(found));
            check(emptyPathDir.isValid() && QFileInfo(found).fileName() == "oiftrelax" + suffix,
                  "CPU resolver: folder search finds oiftrelax, not oiftrelax_gpu");
        }

        if (savedExecutable.isEmpty())
            qunsetenv("ROIFT_EXECUTABLE");
        else
            qputenv("ROIFT_EXECUTABLE", savedExecutable);
        qputenv("PATH", savedPath);
    }

    // A slice run segments coronal plane y = 24 with oiftrelax and writes only that plane of
    // the edited mask; a result whose image or mask was swapped during the run is dropped.
    if (qEnvironmentVariableIsEmpty("ROIFT_EXECUTABLE"))
    {
        std::printf("slice run checks skipped: CTest names no built oiftrelax\n");
    }
    else
    {
        const unsigned int size[3] = {kSliceX, kSliceY, kSliceZ};
        std::mt19937 rng(20261005);
        std::uniform_int_distribution<int> noise(-20, 20);
        std::vector<float> ballVoxels(kSliceVoxels);
        std::vector<float> otherVoxels(kSliceVoxels);
        for (unsigned int z = 0; z < kSliceZ; ++z)
            for (unsigned int y = 0; y < kSliceY; ++y)
                for (unsigned int x = 0; x < kSliceX; ++x)
                {
                    const int dx = int(x) - 20, dy = int(y) - 24, dz = int(z) - 16;
                    const bool inBall = dx * dx + dy * dy + dz * dz <= 100;
                    ballVoxels[sliceIndex(x, y, z)] = float((inBall ? 1000 : 0) + noise(rng));
                    otherVoxels[sliceIndex(x, y, z)] = float(noise(rng));
                }
        const ValueAt ballValue = [&ballVoxels](unsigned int x, unsigned int y, unsigned int z)
        { return ballVoxels[sliceIndex(x, y, z)]; };
        const ValueAt otherValue = [&otherVoxels](unsigned int x, unsigned int y, unsigned int z)
        { return otherVoxels[sliceIndex(x, y, z)]; };
        // On edge pixels of the plane, which take border background seeds: label 3, not a run
        // label, and label 1, a run label. Label 1 also off the plane.
        const ValueAt maskAValue = [](unsigned int x, unsigned int y, unsigned int z)
        {
            if (x == 0 && y == 24 && z == 0)
                return 3.0f;
            if ((x == 39 && y == 24 && z == 0) || (x == 20 && y == 10 && z == 16))
                return 1.0f;
            return 0.0f;
        };
        const ValueAt maskBValue = [](unsigned int x, unsigned int y, unsigned int z)
        { return (x == 20 && y == 24 && z == 16) ? 7.0f : 0.0f; };
        const ValueAt otherMaskValue = [](unsigned int x, unsigned int y, unsigned int z)
        { return (y == 24 && x >= 10 && x < 16 && z >= 10 && z < 16) ? 5.0f : 0.0f; };

        const QString ballPath = dir.filePath("ball.nii.gz");
        const QString otherPath = dir.filePath("other.nii.gz");
        const QString maskAPath = dir.filePath("mask_a.nii.gz");
        const QString maskBPath = dir.filePath("mask_b.nii.gz");
        const QString otherMaskPath = dir.filePath("other_mask.nii.gz");
        const QString thinMaskPath = dir.filePath("thin_mask.nii.gz");
        const unsigned int thinSize[3] = {kSliceX, kSliceY, kSliceZ / 2};
        const QString seedPath = dir.filePath("ball_seeds.txt");
        QFile seedFile(seedPath);
        const bool wroteSeeds = seedFile.open(QIODevice::WriteOnly | QIODevice::Text) &&
                                seedFile.write("3\n20 24 16 1 1\n20 30 16 1 1\n5 24 5 2 2\n") > 0;
        seedFile.close();
        const bool wroteSlice = wroteSeeds && writeImage<3, std::int16_t>(ballPath, size, ballValue) &&
                                writeImage<3, std::int16_t>(otherPath, size, otherValue) &&
                                writeImage<3, std::int16_t>(maskAPath, size, maskAValue) &&
                                writeImage<3, std::int16_t>(maskBPath, size, maskBValue) &&
                                writeImage<3, std::int16_t>(otherMaskPath, size, otherMaskValue) &&
                                writeImage<3, std::int16_t>(thinMaskPath, thinSize, maskBValue);
        check(wroteSlice, "slice run fixtures written");

        ManualSeedSelector slice("");
        slice.addImagesFromPaths({ballPath, otherPath});
        QListWidget *list = slice.findChild<QListWidget *>("imageList");
        QListWidget *masks = slice.findChild<QListWidget *>("maskList");
        QComboBox *scope = slice.findChild<QComboBox *>("segmentationScope");
        QComboBox *plane = slice.findChild<QComboBox *>("segmentationPlane");
        QSlider *coronal = slice.findChild<QSlider *>("coronalSlider");
        QPlainTextEdit *log = slice.findChild<QPlainTextEdit *>("logConsole");
        QPushButton *run = nullptr;
        for (QPushButton *button : slice.findChildren<QPushButton *>())
            if (button->accessibleName() == "Run segmentation")
                run = button;
        const bool found = wroteSlice && list && masks && scope && plane && coronal && log && run;
        check(found, "image and mask lists, scope, plane, slider, log and Run are found");
        if (!found)
            return 1;

        // Selects the slice on the ball image, with mask_a edited and the seeds loaded.
        const auto prepare = [&]()
        {
            list->setCurrentRow(rowForPath(list, ballPath));
            slice.applyMaskFromPath(maskAPath.toStdString());
            slice.loadSeedsFromFile(seedPath.toStdString());
            scope->setCurrentIndex(scope->findText("Current slice"));
            plane->setCurrentIndex(plane->findText("Coronal"));
            coronal->setValue(24);
        };
        struct Outcome
        {
            bool arrived = false;
            bool success = false;
            QString message;
        };
        // Clicks Run, calls meanwhile before any event is processed, then waits for the result.
        const auto runSlice = [&](const std::function<void()> &meanwhile)
        {
            Outcome outcome;
            QEventLoop loop;
            QObject::connect(&slice, &ManualSeedSelector::planeSegmentationFinished, &loop,
                             [&outcome, &loop](bool success, const QString &message)
                             {
                                 outcome = {true, success, message};
                                 loop.quit();
                             });
            run->click();
            if (meanwhile)
                meanwhile();
            if (!outcome.arrived)
            {
                QTimer::singleShot(30000, &loop, &QEventLoop::quit);
                loop.exec();
            }
            std::printf("  slice run: %s\n", qPrintable(outcome.message));
            return outcome;
        };

        prepare();
        const std::vector<int> before = slice.activeMaskLabels();
        check(before.size() == kSliceVoxels && slice.activeMaskDims() == std::array<unsigned, 3>{kSliceX, kSliceY, kSliceZ},
              "mask_a.nii.gz is the edited 40 x 48 x 32 mask");

        const Outcome done = runSlice(nullptr);
        check(done.arrived && done.success, "coronal 24: the run succeeds");
        const std::vector<int> &after = slice.activeMaskLabels();
        const double dice = discDice(after, kSliceX, kSliceZ, 20, 16, "coronal 24",
                                     [](unsigned int u, unsigned int v) { return sliceIndex(u, 24, v); });
        check(dice >= 0.9, "coronal 24: label 1 matches the ball's section, Dice >= 0.9");
        const bool sized = after.size() == kSliceVoxels;
        check(sized && after[sliceIndex(5, 24, 5)] == 2, "coronal 24: the label-2 seed's pixel holds 2");
        check(sized && after[sliceIndex(0, 24, 0)] == 3, "coronal 24: label 3 under a background result is kept");
        check(sized && before[sliceIndex(39, 24, 0)] == 1 && after[sliceIndex(39, 24, 0)] == 0,
              "coronal 24: label 1 under a background result is cleared");
        bool offPlaneKept = sized;
        for (unsigned int z = 0; z < kSliceZ && offPlaneKept; ++z)
            for (unsigned int y = 0; y < kSliceY && offPlaneKept; ++y)
                for (unsigned int x = 0; x < kSliceX && offPlaneKept; ++x)
                    if (y != 24 && after[sliceIndex(x, y, z)] != before[sliceIndex(x, y, z)])
                        offPlaneKept = false;
        check(offPlaneKept && after[sliceIndex(20, 10, 16)] == 1, "every voxel off plane y = 24 is unchanged");
        check(log->toPlainText().contains("2 seed(s) used, 1 on other slices ignored"),
              "the log counts 2 seeds used and 1 ignored");

        // Plane y = 10 holds no seed; the refusal comes before any task.
        const std::vector<int> beforeRefusal = slice.activeMaskLabels();
        coronal->setValue(10);
        const Outcome refused = runSlice(nullptr);
        check(refused.arrived && !refused.success && refused.message.contains("No object seed") &&
                  !slice.isSegmentationTaskRunning(),
              "coronal 10, no seed on it: refused at once");
        check(slice.activeMaskLabels() == beforeRefusal, "coronal 10: the refusal changes nothing");
        coronal->setValue(24);

        const auto voxelsOf = [](const ValueAt &valueAt)
        {
            std::vector<int> voxels(kSliceVoxels);
            for (unsigned int z = 0; z < kSliceZ; ++z)
                for (unsigned int y = 0; y < kSliceY; ++y)
                    for (unsigned int x = 0; x < kSliceX; ++x)
                        voxels[sliceIndex(x, y, z)] = int(valueAt(x, y, z));
            return voxels;
        };

        const Outcome imageChanged = runSlice([&]()
                                              {
            list->setCurrentRow(rowForPath(list, otherPath));
            slice.applyMaskFromPath(otherMaskPath.toStdString()); });
        check(imageChanged.arrived && !imageChanged.success && imageChanged.message.contains("image changed"),
              "image switched during the run: result discarded, image named");
        check(slice.activeMaskLabels() == voxelsOf(otherMaskValue), "image switched: the other image's mask is untouched");

        prepare();
        slice.applyMaskFromPath(maskBPath.toStdString());
        slice.applyMaskFromPath(maskAPath.toStdString());
        const int maskBRow = rowForPath(masks, maskBPath);
        check(maskBRow >= 0 && QFileInfo(slice.activeMaskPath()) == QFileInfo(maskAPath),
              "mask_a edited, mask_b listed beside it");
        const Outcome maskChanged = runSlice([&]()
                                             {
            if (maskBRow >= 0)
                emit masks->itemClicked(masks->item(maskBRow)); });
        check(maskChanged.arrived && !maskChanged.success && maskChanged.message.contains("edited mask changed"),
              "mask switched during the run: result discarded, mask named");
        check(QFileInfo(slice.activeMaskPath()) == QFileInfo(maskBPath) &&
                  slice.activeMaskLabels() == voxelsOf(maskBValue),
              "mask switched: mask_b, now edited, is untouched");

        // Axial slice z = 16 is the axial slider's value; u = x, v = y.
        prepare();
        QSlider *axial = slice.findChild<QSlider *>("axialSlider");
        plane->setCurrentIndex(plane->findText("Axial"));
        if (axial)
            axial->setValue(16);
        const Outcome axialDone = runSlice(nullptr);
        check(axial && axialDone.arrived && axialDone.success, "axial 16: the run succeeds");
        const double axialDice = discDice(slice.activeMaskLabels(), kSliceX, kSliceY, 20, 24, "axial 16",
                                          [](unsigned int u, unsigned int v) { return sliceIndex(u, v, 16); });
        check(axialDice >= 0.9, "axial 16: label 1 matches the ball's section, Dice >= 0.9");

        // A mask whose depth is only mapped onto the image's is refused before the run.
        slice.applyMaskFromPath(thinMaskPath.toStdString());
        const Outcome thin = runSlice(nullptr);
        check(slice.activeMaskDims() == std::array<unsigned, 3>{kSliceX, kSliceY, kSliceZ / 2} && thin.arrived &&
                  !thin.success && thin.message.contains("40 x 48 x 16") && !slice.isSegmentationTaskRunning(),
              "a 40 x 48 x 16 edited mask: refused at once");
    }

    std::printf("%s\n", failures == 0 ? "all checks passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
