// A mask saved from the window must land on its image's grid: same origin, spacing and
// direction as the image, and the same labels as the buffer. Before this was pinned, the
// writer left all three at ITK's defaults, so a mask on a 0.3 mm image was saved with 1 mm
// voxels at the origin and no longer overlaid its image in any other program.
#include "ManualSeedSelector.h"

#include <QApplication>
#include <QDir>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>

#include <itkImage.h>
#include <itkImageFileReader.h>
#include <itkImageFileWriter.h>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-62s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

constexpr unsigned kX = 20, kY = 16, kZ = 8;
using VolumeType = itk::Image<int32_t, 3>;

VolumeType::Pointer makeVolume(int (*valueAt)(unsigned, unsigned, unsigned))
{
    VolumeType::Pointer image = VolumeType::New();
    VolumeType::SizeType size;
    size[0] = kX;
    size[1] = kY;
    size[2] = kZ;
    VolumeType::IndexType start;
    start.Fill(0);
    image->SetRegions(VolumeType::RegionType(start, size));
    image->Allocate();

    // A grid nothing would reach by default: anisotropic, off origin, axes flipped and swapped.
    VolumeType::SpacingType spacing;
    spacing[0] = 0.3;
    spacing[1] = 0.4;
    spacing[2] = 1.25;
    image->SetSpacing(spacing);
    VolumeType::PointType origin;
    origin[0] = 12.5;
    origin[1] = -40.0;
    origin[2] = 7.75;
    image->SetOrigin(origin);
    VolumeType::DirectionType direction;
    direction.Fill(0.0);
    direction[0][1] = -1.0;
    direction[1][0] = 1.0;
    direction[2][2] = -1.0;
    image->SetDirection(direction);

    for (unsigned z = 0; z < kZ; ++z)
        for (unsigned y = 0; y < kY; ++y)
            for (unsigned x = 0; x < kX; ++x)
            {
                VolumeType::IndexType voxel;
                voxel[0] = static_cast<VolumeType::IndexValueType>(x);
                voxel[1] = static_cast<VolumeType::IndexValueType>(y);
                voxel[2] = static_cast<VolumeType::IndexValueType>(z);
                image->SetPixel(voxel, valueAt(x, y, z));
            }
    return image;
}

bool write(VolumeType *image, const QString &path)
{
    try
    {
        auto writer = itk::ImageFileWriter<VolumeType>::New();
        writer->SetFileName(path.toStdString());
        writer->SetInput(image);
        writer->Update();
    }
    catch (...)
    {
        return false;
    }
    return true;
}

VolumeType::Pointer read(const QString &path)
{
    try
    {
        auto reader = itk::ImageFileReader<VolumeType>::New();
        reader->SetFileName(path.toStdString());
        reader->Update();
        return reader->GetOutput();
    }
    catch (...)
    {
        return nullptr;
    }
}

bool sameGeometry(const VolumeType *a, const VolumeType *b)
{
    constexpr double kTolerance = 1e-4; // NIfTI stores the affine as float
    for (unsigned i = 0; i < 3; ++i)
    {
        if (std::abs(a->GetSpacing()[i] - b->GetSpacing()[i]) > kTolerance ||
            std::abs(a->GetOrigin()[i] - b->GetOrigin()[i]) > kTolerance)
            return false;
        for (unsigned j = 0; j < 3; ++j)
            if (std::abs(a->GetDirection()[i][j] - b->GetDirection()[i][j]) > kTolerance)
                return false;
    }
    return a->GetLargestPossibleRegion() == b->GetLargestPossibleRegion();
}

int labelAt(unsigned x, unsigned y, unsigned z)
{
    return static_cast<int>((x / 5 + y / 4 + z) % 4);
}

int intensityAt(unsigned x, unsigned y, unsigned z)
{
    return static_cast<int>(x * 7 + y * 3 + z);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir workDir;
    if (!workDir.isValid())
        return 1;
    const QDir dir(workDir.path());
    const QString ctPath = dir.filePath("grid_0000.nii.gz");
    const QString maskPath = dir.filePath("grid_mask.nii.gz");
    const QString savedPath = dir.filePath("grid_saved.nii.gz");

    VolumeType::Pointer ct = makeVolume(intensityAt);
    VolumeType::Pointer mask = makeVolume(labelAt);
    check(write(ct, ctPath) && write(mask, maskPath), "image and mask written");
    VolumeType::Pointer ctOnDisk = read(ctPath);
    check(ctOnDisk.IsNotNull(), "image read back");
    if (ctOnDisk.IsNull())
        return 1;

    ManualSeedSelector window("");
    window.addImagesFromPaths({ctPath});
    check(window.hasImage(), "image loaded");
    check(window.applyMaskFromPath(maskPath.toStdString()), "mask loaded into the edited buffer");

    QString error;
    check(window.saveActiveMaskTo(savedPath.toStdString(), &error), "the mask saves");
    VolumeType::Pointer saved = read(savedPath);
    check(saved.IsNotNull(), "the saved mask reads back");
    if (saved.IsNull())
        return 1;

    check(sameGeometry(saved, ctOnDisk), "the saved mask has the image's origin, spacing and direction");

    bool labelsKept = true;
    for (unsigned z = 0; z < kZ && labelsKept; ++z)
        for (unsigned y = 0; y < kY && labelsKept; ++y)
            for (unsigned x = 0; x < kX && labelsKept; ++x)
            {
                VolumeType::IndexType voxel;
                voxel[0] = static_cast<VolumeType::IndexValueType>(x);
                voxel[1] = static_cast<VolumeType::IndexValueType>(y);
                voxel[2] = static_cast<VolumeType::IndexValueType>(z);
                labelsKept = saved->GetPixel(voxel) == labelAt(x, y, z);
            }
    check(labelsKept, "every label is kept at its voxel");

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
