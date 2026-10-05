// Checks that NiftiImage reads 2D raster files as one-slice volumes, indexed the
// way the slice views draw them and planar::extractPlane reads them back.
//
// Colour reads as luminance weighted by alpha / max (ITK alone would weight by the
// raw alpha for a float image) and a multi-page TIFF reads as a volume; both are
// pinned, because a seed placed on the drawn pixel must land on the voxel the tools read.
#include "NiftiImage.h"
#include "PlanarSlice.h"

#include <itkImage.h>
#include <itkImageFileWriter.h>
#include <itkImageRegionIteratorWithIndex.h>
#include <itkRGBAPixel.h>
#include <itkRGBPixel.h>
#include <itkVector.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
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

constexpr unsigned int kWidth = 40;
constexpr unsigned int kHeight = 30;
constexpr unsigned int kStackX = 8;
constexpr unsigned int kStackY = 6;
constexpr unsigned int kStackZ = 3;

using RGB = itk::RGBPixel<unsigned char>;
using RGBA = itk::RGBAPixel<unsigned char>;
using GrayAlpha = itk::Vector<unsigned char, 2>;

// Write an image whose pixels come from valueAt(index).
template <typename ImageT, typename ValueAt>
bool writeImage(const std::string &path, const typename ImageT::SizeType &size, ValueAt valueAt)
{
    auto image = ImageT::New();
    typename ImageT::RegionType region;
    region.SetSize(size);
    image->SetRegions(region);
    image->Allocate();
    itk::ImageRegionIteratorWithIndex<ImageT> it(image, region);
    for (it.GoToBegin(); !it.IsAtEnd(); ++it)
        it.Set(valueAt(it.GetIndex()));
    try
    {
        auto writer = itk::ImageFileWriter<ImageT>::New();
        writer->SetFileName(path);
        writer->SetInput(image);
        writer->Update();
    }
    catch (const itk::ExceptionObject &e)
    {
        std::printf("could not write %s: %s\n", path.c_str(), e.GetDescription());
        return false;
    }
    return true;
}

itk::Size<2> planeSize()
{
    itk::Size<2> size;
    size[0] = kWidth;
    size[1] = kHeight;
    return size;
}

RGB rgb(unsigned char r, unsigned char g, unsigned char b)
{
    RGB p;
    p.Set(r, g, b);
    return p;
}

RGBA rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    RGBA p;
    p.Set(r, g, b, a);
    return p;
}

GrayAlpha grayAlpha(unsigned char value, unsigned char a)
{
    GrayAlpha p;
    p[0] = value;
    p[1] = a;
    return p;
}

bool writeFixtures(const std::filesystem::path &dir)
{
    using Gray = itk::Image<unsigned char, 2>;
    using Gray16 = itk::Image<unsigned short, 2>;
    using Colour = itk::Image<RGB, 2>;
    using ColourAlpha = itk::Image<RGBA, 2>;
    using GrayWithAlpha = itk::Image<GrayAlpha, 2>;
    using Stack = itk::Image<unsigned char, 3>;

    const bool gray = writeImage<Gray>((dir / "gray.png").string(), planeSize(), [](const Gray::IndexType &i)
                                       { return static_cast<unsigned char>(i[0] + i[1]); });
    const bool colour = writeImage<Colour>((dir / "rgb.png").string(), planeSize(), [](const Colour::IndexType &i)
                                           { return (i[0] == 10 && i[1] == 5) ? rgb(200, 100, 50) : rgb(0, 0, 0); });
    const bool alpha = writeImage<ColourAlpha>(
        (dir / "rgba.png").string(), planeSize(), [](const ColourAlpha::IndexType &i)
        {
            if (i[0] == 10 && i[1] == 5)
                return rgba(200, 100, 50, 255);
            if (i[0] == 11 && i[1] == 5)
                return rgba(200, 100, 50, 0);
            return rgba(0, 0, 0, 255);
        });
    const bool grayAlphaWritten = writeImage<GrayWithAlpha>(
        (dir / "graya.png").string(), planeSize(), [](const GrayWithAlpha::IndexType &i)
        {
            if (i[0] == 10 && i[1] == 5)
                return grayAlpha(100, 255);
            if (i[0] == 11 && i[1] == 5)
                return grayAlpha(100, 0);
            return grayAlpha(0, 255);
        });
    const bool deep = writeImage<Gray16>((dir / "gray16.tif").string(), planeSize(), [](const Gray16::IndexType &i)
                                         { return static_cast<unsigned short>((i[0] == 3 && i[1] == 4) ? 1000 : 0); });
    Stack::SizeType stackSize;
    stackSize[0] = kStackX;
    stackSize[1] = kStackY;
    stackSize[2] = kStackZ;
    const bool stack = writeImage<Stack>((dir / "stack.tif").string(), stackSize, [](const Stack::IndexType &i)
                                         { return static_cast<unsigned char>(i[0] + kStackX * i[1] + kStackX * kStackY * i[2]); });
    return gray && colour && alpha && grayAlphaWritten && deep && stack;
}

bool hasSize(const NiftiImage &image, unsigned int x, unsigned int y, unsigned int z)
{
    return image.getSizeX() == x && image.getSizeY() == y && image.getSizeZ() == z;
}

// The R byte of every drawn pixel against the same plane cut from buffer().
void checkPlaneMatchesView(const NiftiImage &stack, planar::Plane plane, int index, const char *what)
{
    planar::Geometry g;
    const std::array<int, 3> dims{static_cast<int>(kStackX), static_cast<int>(kStackY), static_cast<int>(kStackZ)};
    if (!planar::makeGeometry(plane, index, dims, &g))
    {
        check(false, what);
        return;
    }
    const std::vector<float> values = planar::extractPlane(g, stack.buffer(), kStackX * kStackY * kStackZ);

    // lo = 0, hi = 255 makes the view's window 255 * (v - lo) / (hi - lo), exact for these integers.
    std::vector<unsigned char> drawn;
    const unsigned int u = static_cast<unsigned int>(index);
    if (plane == planar::Plane::Axial)
        drawn = stack.getAxialSliceAsRGB(u, 0.0f, 255.0f);
    else if (plane == planar::Plane::Sagittal)
        drawn = stack.getSagittalSliceAsRGB(u, 0.0f, 255.0f);
    else
        drawn = stack.getCoronalSliceAsRGB(u, 0.0f, 255.0f);

    const std::size_t pixels = static_cast<std::size_t>(g.width) * static_cast<std::size_t>(g.height);
    bool same = values.size() == pixels && drawn.size() == 3 * pixels;
    for (std::size_t i = 0; same && i < pixels; ++i)
        same = drawn[3 * i] == static_cast<unsigned char>(std::lround(values[i]));
    check(same, what);
}

} // namespace

int main()
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("roift_raster_test_" + std::to_string(std::random_device{}()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const bool wrote = !ec && writeFixtures(dir);
    check(wrote, "fixtures written");
    if (!wrote)
        return 1;

    NiftiImage gray, colour, alpha, grayWithAlpha, deep, stack;
    check(gray.load((dir / "gray.png").string()), "gray.png loads");
    check(colour.load((dir / "rgb.png").string()), "rgb.png loads");
    check(alpha.load((dir / "rgba.png").string()), "rgba.png loads");
    check(grayWithAlpha.load((dir / "graya.png").string()), "graya.png loads");
    check(deep.load((dir / "gray16.tif").string()), "gray16.tif loads");
    check(stack.load((dir / "stack.tif").string()), "stack.tif loads");

    check(hasSize(gray, kWidth, kHeight, 1), "gray.png is 40 x 30 x 1");
    check(hasSize(colour, kWidth, kHeight, 1), "rgb.png is 40 x 30 x 1");
    check(hasSize(alpha, kWidth, kHeight, 1), "rgba.png is 40 x 30 x 1");
    check(hasSize(deep, kWidth, kHeight, 1), "gray16.tif is 40 x 30 x 1");

    check(gray.getVoxelValue(7, 2, 0) == 9.0f, "gray.png (7,2) is x + y = 9");
    check(gray.getVoxelValue(0, 0, 0) == 0.0f && gray.getVoxelValue(39, 29, 0) == 68.0f,
          "gray.png first file row is y = 0 (no flip)");

    const float luminance = 0.2125f * 200.0f + 0.7154f * 100.0f + 0.0721f * 50.0f;
    const float rgbValue = colour.getVoxelValue(10, 5, 0);
    std::printf("  rgb.png (10,5) = %.4f, expected luminance %.4f\n", rgbValue, luminance);
    check(std::abs(rgbValue - luminance) <= 0.5f, "rgb.png (10,5) is the luminance of (200,100,50)");
    std::printf("  rgba.png (10,5) = %.4f, (11,5) = %.4f\n", alpha.getVoxelValue(10, 5, 0), alpha.getVoxelValue(11, 5, 0));
    check(std::abs(alpha.getVoxelValue(10, 5, 0) - rgbValue) <= 0.5f, "rgba.png opaque pixel matches rgb.png");
    check(alpha.getVoxelValue(11, 5, 0) == 0.0f, "rgba.png alpha 0 reads as 0 (alpha weights luminance)");
    std::printf("  graya.png (10,5) = %.4f, (11,5) = %.4f\n", grayWithAlpha.getVoxelValue(10, 5, 0),
                grayWithAlpha.getVoxelValue(11, 5, 0));
    check(std::abs(grayWithAlpha.getVoxelValue(10, 5, 0) - 100.0f) <= 0.5f &&
              grayWithAlpha.getVoxelValue(11, 5, 0) == 0.0f,
          "graya.png opaque 100 reads 100, alpha 0 reads 0");

    check(deep.getVoxelValue(3, 4, 0) == 1000.0f, "gray16.tif (3,4) keeps 16-bit value 1000");

    check(hasSize(stack, kStackX, kStackY, kStackZ), "stack.tif is 8 x 6 x 3 (pages are z)");
    bool ordered = hasSize(stack, kStackX, kStackY, kStackZ);
    for (unsigned int z = 0; ordered && z < kStackZ; ++z)
        for (unsigned int y = 0; ordered && y < kStackY; ++y)
            for (unsigned int x = 0; ordered && x < kStackX; ++x)
                ordered = stack.getVoxelValue(x, y, z) == static_cast<float>(x + kStackX * y + kStackX * kStackY * z);
    check(ordered, "stack.tif voxel (x,y,z) is x + 8y + 48z");

    const char *rasterNames[] = {"a.png", "a.JPG", "a.Jpeg", "a.BMP", "a.tif", "a.TiFf"};
    bool allRaster = true;
    for (const char *name : rasterNames)
        allRaster = allRaster && NiftiImage::isRasterPath(name);
    check(allRaster, "isRasterPath: png jpg jpeg bmp tif tiff, any case");
    check(!NiftiImage::isRasterPath("a.nii.gz") && !NiftiImage::isRasterPath("a.npz"),
          "isRasterPath: false for .nii.gz and .npz");

    const float *voxels = stack.buffer();
    check(voxels != nullptr, "buffer() is non-null after load");
    check(NiftiImage().buffer() == nullptr, "buffer() is null before load");
    bool contiguous = voxels != nullptr && hasSize(stack, kStackX, kStackY, kStackZ);
    const unsigned int probes[][3] = {{0, 0, 0}, {7, 5, 2}, {3, 2, 1}, {5, 1, 2}, {1, 4, 0}};
    for (const auto &p : probes)
        contiguous = contiguous &&
                     voxels[p[0] + kStackX * (p[1] + kStackY * p[2])] == stack.getVoxelValue(p[0], p[1], p[2]);
    check(contiguous, "buffer() is x-fastest: [x + 8(y + 6z)] == getVoxelValue");

    check(!stack.isMask(), "stack.tif is windowed, not drawn as a mask");
    if (voxels != nullptr && hasSize(stack, kStackX, kStackY, kStackZ))
    {
        checkPlaneMatchesView(stack, planar::Plane::Axial, 1, "axial z=1: extractPlane matches getAxialSliceAsRGB");
        checkPlaneMatchesView(stack, planar::Plane::Sagittal, 5, "sagittal x=5: extractPlane matches getSagittalSliceAsRGB");
        checkPlaneMatchesView(stack, planar::Plane::Coronal, 2, "coronal y=2: extractPlane matches getCoronalSliceAsRGB");
    }
    else
    {
        check(false, "plane mapping checks need stack.tif loaded");
    }

    fs::remove_all(dir, ec);
    std::printf("%s\n", failures ? "FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
