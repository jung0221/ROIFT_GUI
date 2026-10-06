// Checks NiftiImage on NIfTI volumes: a .nii.gz is read in place and leaves no
// decompressed copy in the temp directory; samples, spacing and range come back
// exactly; the slice getters window the buffer with the historical arithmetic
// and give a black slice for an index past the volume; a failed read leaves the
// previous image and its spacing intact; non-finite voxels stay out of the range.
#include "NiftiImage.h"

#include <itkImage.h>
#include <itkImageFileWriter.h>
#include <itkImageRegionIteratorWithIndex.h>
#include <itkNiftiImageIO.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-66s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

constexpr unsigned int kX = 7;
constexpr unsigned int kY = 5;
constexpr unsigned int kZ = 4;
const double kSpacing[3] = {0.5, 0.75, 2.0};

using Index = itk::Image<float, 3>::IndexType;

// Write a volume of Pixel samples whose voxels come from valueAt(index), through the NIfTI IO.
template <typename Pixel, typename ValueAt>
bool writeVolume(const std::string &path, ValueAt valueAt)
{
    using ImageT = itk::Image<Pixel, 3>;
    auto image = ImageT::New();
    typename ImageT::SizeType size;
    size[0] = kX;
    size[1] = kY;
    size[2] = kZ;
    typename ImageT::RegionType region;
    region.SetSize(size);
    image->SetRegions(region);
    typename ImageT::SpacingType spacing;
    for (unsigned int i = 0; i < 3; ++i)
        spacing[i] = kSpacing[i];
    image->SetSpacing(spacing);
    image->Allocate();
    itk::ImageRegionIteratorWithIndex<ImageT> it(image, region);
    for (it.GoToBegin(); !it.IsAtEnd(); ++it)
    {
        Index index;
        for (unsigned int i = 0; i < 3; ++i)
            index[i] = it.GetIndex()[i];
        it.Set(static_cast<Pixel>(valueAt(index)));
    }
    try
    {
        auto writer = itk::ImageFileWriter<ImageT>::New();
        writer->SetImageIO(itk::NiftiImageIO::New());
        writer->SetFileName(path);
        writer->SetInput(image);
        writer->Update();
    }
    catch (const std::exception &e)
    {
        std::printf("failed to write %s: %s\n", path.c_str(), e.what());
        return false;
    }
    return true;
}

// The CT-like phantom: distinct values, negative at the origin.
float ctValue(unsigned int x, unsigned int y, unsigned int z)
{
    return static_cast<float>(static_cast<int>(x) + 10 * static_cast<int>(y) + 100 * static_cast<int>(z) - 50);
}

// The historical windowing of one voxel.
unsigned char windowed(float v, float lo, float hi)
{
    const float denom = (hi - lo != 0.0f) ? (hi - lo) : 1.0f;
    if (v < lo)
        v = lo;
    if (v > hi)
        v = hi;
    return static_cast<unsigned char>(255.0f * (v - lo) / denom);
}

bool sliceMatches(const std::vector<unsigned char> &rgb, unsigned int w, unsigned int h, float lo, float hi,
                  const std::function<float(unsigned int, unsigned int)> &voxel)
{
    if (rgb.size() != static_cast<std::size_t>(w) * h * 3)
        return false;
    for (unsigned int v = 0; v < h; ++v)
        for (unsigned int u = 0; u < w; ++u)
        {
            const unsigned char expected = windowed(voxel(u, v), lo, hi);
            const std::size_t pix = (static_cast<std::size_t>(v) * w + u) * 3;
            if (rgb[pix] != expected || rgb[pix + 1] != expected || rgb[pix + 2] != expected)
                return false;
        }
    return true;
}

bool allZero(const std::vector<unsigned char> &rgb, std::size_t expectedSize)
{
    if (rgb.size() != expectedSize)
        return false;
    for (unsigned char c : rgb)
        if (c != 0)
            return false;
    return true;
}

bool spacingIs(const NiftiImage &image, const double spacing[3])
{
    return std::abs(image.getSpacingX() - spacing[0]) < 1e-4 && std::abs(image.getSpacingY() - spacing[1]) < 1e-4 &&
           std::abs(image.getSpacingZ() - spacing[2]) < 1e-4;
}

std::filesystem::path makeTempDir()
{
    std::random_device device;
    std::ostringstream name;
    name << "nifti_volume_test_" << std::hex << device() << device();
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name.str();
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace

int main()
{
    const std::filesystem::path dir = makeTempDir();
    const std::string gzPath = (dir / "vol.nii.gz").string();
    const std::string niiPath = (dir / "vol.nii").string();
    const std::string maskPath = (dir / "mask.nii.gz").string();
    const std::string nanPath = (dir / "nan.nii.gz").string();
    const std::string garbagePath = (dir / "garbage.nii").string();

    auto ct = [](const Index &i) { return ctValue(i[0], i[1], i[2]); };
    bool wrote = writeVolume<int16_t>(gzPath, ct) && writeVolume<int16_t>(niiPath, ct) &&
                 writeVolume<uint8_t>(maskPath, [](const Index &i) { return i[0] < 3 ? 1.0f : 0.0f; }) &&
                 writeVolume<float>(nanPath, [](const Index &i)
                                    {
                                        if (i[0] == 0 && i[1] == 0 && i[2] == 0)
                                            return std::numeric_limits<float>::quiet_NaN();
                                        if (i[0] == 1 && i[1] == 0 && i[2] == 0)
                                            return std::numeric_limits<float>::infinity();
                                        return static_cast<float>(i[0]); });
    {
        std::ofstream garbage(garbagePath, std::ios::binary);
        garbage << "this is not a NIfTI header, just sixty-four bytes of plain text";
        wrote = wrote && garbage.good();
    }
    check(wrote, "fixtures written");
    if (!wrote)
        return 1;

    // --- a gzipped volume ---------------------------------------------------
    NiftiImage image;
    check(image.load(gzPath), "vol.nii.gz loads");
    check(image.lastError().empty(), "a successful load leaves lastError empty");
    check(image.getSizeX() == kX && image.getSizeY() == kY && image.getSizeZ() == kZ, "size is 7 x 5 x 4");
    check(spacingIs(image, kSpacing), "spacing is read from the header");
    check(image.getGlobalMin() == ctValue(0, 0, 0) && image.getGlobalMax() == ctValue(kX - 1, kY - 1, kZ - 1),
          "range is the smallest and largest sample");
    check(!image.isMask(), "a CT-like range is not a mask");

    bool buffered = (image.buffer() != nullptr);
    for (unsigned int z = 0; z < kZ && buffered; ++z)
        for (unsigned int y = 0; y < kY && buffered; ++y)
            for (unsigned int x = 0; x < kX; ++x)
            {
                const float expected = ctValue(x, y, z);
                if (image.buffer()[x + y * kX + z * kX * kY] != expected || image.getVoxelValue(x, y, z) != expected)
                {
                    buffered = false;
                    break;
                }
            }
    check(buffered, "buffer() is X fastest and getVoxelValue agrees with it");
    check(image.getVoxelValue(kX, 0, 0) == 0.0f && image.getVoxelValue(0, kY, 0) == 0.0f &&
              image.getVoxelValue(0, 0, kZ) == 0.0f,
          "getVoxelValue outside the volume is 0");

    // The previous loader inflated to <stem>_decompressed.nii in the temp directory.
    const std::filesystem::path leftover = std::filesystem::temp_directory_path() / "vol.nii_decompressed.nii";
    check(!std::filesystem::exists(leftover), "no decompressed copy is left in the temp directory");

    NiftiImage plain;
    check(plain.load(niiPath) && plain.getSizeZ() == kZ && plain.getVoxelValue(3, 2, 1) == ctValue(3, 2, 1) &&
              spacingIs(plain, kSpacing),
          "vol.nii loads the same");

    // --- slices ---------------------------------------------------------------
    const float lo = -20.0f;
    const float hi = 150.0f;
    check(sliceMatches(image.getAxialSliceAsRGB(2, lo, hi), kX, kY, lo, hi,
                       [](unsigned int u, unsigned int v) { return ctValue(u, v, 2); }),
          "axial slice is the windowed plane z = 2, (x, y)");
    check(sliceMatches(image.getSagittalSliceAsRGB(3, lo, hi), kY, kZ, lo, hi,
                       [](unsigned int u, unsigned int v) { return ctValue(3, u, v); }),
          "sagittal slice is the plane x = 3 as (y, z)");
    check(sliceMatches(image.getCoronalSliceAsRGB(1, lo, hi), kX, kZ, lo, hi,
                       [](unsigned int u, unsigned int v) { return ctValue(u, 1, v); }),
          "coronal slice is the plane y = 1 as (x, z)");
    const std::vector<unsigned char> full =
        image.getAxialSliceAsRGB(kZ - 1, image.getGlobalMin(), image.getGlobalMax());
    check(!full.empty() && full.back() == 255 && image.getAxialSliceAsRGB(0, image.getGlobalMin(), image.getGlobalMax())[0] == 0,
          "the full range windows the extreme voxels to 0 and 255");
    check(allZero(image.getAxialSliceAsRGB(kZ, lo, hi), std::size_t(kX) * kY * 3) &&
              allZero(image.getSagittalSliceAsRGB(kX, lo, hi), std::size_t(kY) * kZ * 3) &&
              allZero(image.getCoronalSliceAsRGB(kY, lo, hi), std::size_t(kX) * kZ * 3),
          "an index past the volume gives a black slice of the right size");
    check(allZero(NiftiImage().getAxialSliceAsRGB(0, lo, hi), 0), "an empty image gives an empty slice");

    // --- a failed read ----------------------------------------------------------
    check(!image.load(garbagePath), "a .nii that is not NIfTI is refused");
    check(!image.lastError().empty(), "and the refusal carries a reason");
    check(image.getSizeX() == kX && spacingIs(image, kSpacing) && image.getVoxelValue(3, 2, 1) == ctValue(3, 2, 1),
          "the previous image and its spacing survive a failed read");
    check(!image.load((dir / "missing.nii.gz").string()) && !image.lastError().empty(),
          "a missing file is refused with a reason");
    check(image.load(gzPath) && image.lastError().empty(), "a later successful load clears lastError");

    // --- a mask ---------------------------------------------------------------------
    NiftiImage mask;
    check(mask.load(maskPath) && mask.isMask(), "a {0, 1} uint8 volume is classified as a mask");
    check(mask.getGlobalMin() == 0.0f && mask.getGlobalMax() == 1.0f, "a mask's range is 0 to 1");
    const std::vector<unsigned char> maskSlice = mask.getAxialSliceAsRGB(0, 0.0f, 1.0f);
    check(maskSlice.size() == std::size_t(kX) * kY * 3 && maskSlice[0] == 255 && maskSlice[3 * 3] == 0,
          "a mask slice is white where the label is set and black elsewhere");

    // --- non-finite voxels -----------------------------------------------------------
    NiftiImage withNan;
    check(withNan.load(nanPath), "a float volume with NaN and infinity loads");
    check(withNan.getGlobalMin() == 0.0f && withNan.getGlobalMax() == static_cast<float>(kX - 1),
          "NaN and infinity are left out of the range");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    std::printf("%s\n", failures == 0 ? "all checks passed" : "some checks FAILED");
    return failures == 0 ? 0 : 1;
}
