// Checks that NiftiImage reads 2D raster files as one-slice volumes, indexed the
// way the slice views draw them and planar::extractPlane reads them back, and that
// readMaskVolume reads raster label images as labels.
//
// Colour reads as luminance with alpha ignored, grey+alpha as its grey, spacing as
// one pixel whatever the DPI, and a multi-page TIFF as a volume. A seed placed on
// the drawn pixel must land on the voxel the tools read.
#include "MaskLayers.h"
#include "NiftiImage.h"
#include "PlanarSlice.h"

#include <itkImage.h>
#include <itkImageFileReader.h>
#include <itkImageFileWriter.h>
#include <itkImageRegionIteratorWithIndex.h>
#include <itkPNGImageIO.h>
#include <itkRGBAPixel.h>
#include <itkRGBPixel.h>
#include <itkVector.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

// 0.2125 R + 0.7154 G + 0.0721 B of (200, 100, 50).
constexpr float kLuminance = 117.645f;

using RGB = itk::RGBPixel<unsigned char>;
using RGBA = itk::RGBAPixel<unsigned char>;
using GrayAlpha = itk::Vector<unsigned char, 2>;

// Write an image whose pixels come from valueAt(index), through io when given.
template <typename ImageT, typename ValueAt>
bool writeImage(const std::string &path, const typename ImageT::SizeType &size, ValueAt valueAt,
                itk::ImageIOBase *io = nullptr, double spacing = 1.0)
{
    auto image = ImageT::New();
    typename ImageT::RegionType region;
    region.SetSize(size);
    image->SetRegions(region);
    typename ImageT::SpacingType pixelSpacing;
    pixelSpacing.Fill(spacing);
    image->SetSpacing(pixelSpacing);
    image->Allocate();
    itk::ImageRegionIteratorWithIndex<ImageT> it(image, region);
    for (it.GoToBegin(); !it.IsAtEnd(); ++it)
        it.Set(valueAt(it.GetIndex()));
    try
    {
        auto writer = itk::ImageFileWriter<ImageT>::New();
        writer->SetFileName(path);
        if (io)
            writer->SetImageIO(io);
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

void putLE(std::ofstream &out, std::uint32_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        out.put(static_cast<char>((value >> (8 * i)) & 0xFF));
}

// A 32-bpp BMP of one colour whose fourth byte is 0. ITK cannot write one.
bool writeBmp32(const std::string &path, unsigned int width, unsigned int height, RGB colour)
{
    std::ofstream out(path, std::ios::binary);
    const std::uint32_t pixelBytes = width * height * 4;
    out.put('B');
    out.put('M');
    putLE(out, 54 + pixelBytes, 4);
    putLE(out, 0, 4);
    putLE(out, 54, 4);
    putLE(out, 40, 4);
    putLE(out, width, 4);
    putLE(out, height, 4);
    putLE(out, 1, 2);
    putLE(out, 32, 2);
    putLE(out, 0, 4); // BI_RGB
    putLE(out, pixelBytes, 4);
    putLE(out, 2835, 4);
    putLE(out, 2835, 4);
    putLE(out, 0, 4);
    putLE(out, 0, 4);
    for (unsigned int i = 0; i < width * height; ++i)
    {
        out.put(static_cast<char>(colour.GetBlue()));
        out.put(static_cast<char>(colour.GetGreen()));
        out.put(static_cast<char>(colour.GetRed()));
        out.put(0);
    }
    return static_cast<bool>(out);
}

// An uncompressed single-strip TIFF of unsigned 32-bit samples. ITK cannot write one.
bool writeUint32Tiff(const std::string &path, unsigned int width, unsigned int height)
{
    struct Entry
    {
        std::uint16_t tag, type;
        std::uint32_t value;
    };
    constexpr std::uint16_t kShort = 3, kLong = 4;
    constexpr std::uint32_t kEntries = 10;
    constexpr std::uint32_t kDataOffset = 8 + 2 + kEntries * 12 + 4;
    const Entry entries[kEntries] = {
        {256, kLong, width},          {257, kLong, height},  {258, kShort, 32},
        {259, kShort, 1},             {262, kShort, 1},      {273, kLong, kDataOffset},
        {277, kShort, 1},             {278, kLong, height},  {279, kLong, width * height * 4},
        {339, kShort, 1}, // SampleFormat: unsigned integer
    };
    std::ofstream out(path, std::ios::binary);
    out.put('I');
    out.put('I');
    putLE(out, 42, 2);
    putLE(out, 8, 4);
    putLE(out, kEntries, 2);
    for (const Entry &e : entries)
    {
        putLE(out, e.tag, 2);
        putLE(out, e.type, 2);
        putLE(out, 1, 4);
        putLE(out, e.value, 4);
    }
    putLE(out, 0, 4);
    for (unsigned int i = 0; i < width * height; ++i)
        putLE(out, 70000 + i, 4);
    return static_cast<bool>(out);
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

unsigned char jpegValue(unsigned int x, unsigned int y)
{
    return static_cast<unsigned char>(3 * x + 2 * y);
}

int paletteLabel(unsigned int x)
{
    return x < 10 ? 0 : (x < 20 ? 1 : 2);
}

int greyLabel(unsigned int x, unsigned int y)
{
    return (x >= 30 && y >= 20) ? 5 : paletteLabel(x);
}

bool writeFixtures(const std::filesystem::path &dir)
{
    using Gray = itk::Image<unsigned char, 2>;
    using Gray16 = itk::Image<unsigned short, 2>;
    using Colour = itk::Image<RGB, 2>;
    using ColourAlpha = itk::Image<RGBA, 2>;
    using GrayWithAlpha = itk::Image<GrayAlpha, 2>;
    using Stack = itk::Image<unsigned char, 3>;

    const auto colourAt = [](const Colour::IndexType &i)
    { return (i[0] == 10 && i[1] == 5) ? rgb(200, 100, 50) : rgb(0, 0, 0); };
    bool ok = writeImage<Gray>((dir / "gray.png").string(), planeSize(), [](const Gray::IndexType &i)
                               { return static_cast<unsigned char>(i[0] + i[1]); });
    ok = writeImage<Colour>((dir / "rgb.png").string(), planeSize(), colourAt) && ok;
    ok = writeImage<Colour>((dir / "rgb.bmp").string(), planeSize(), colourAt) && ok;
    ok = writeImage<ColourAlpha>((dir / "rgba.png").string(), planeSize(), [](const ColourAlpha::IndexType &i)
                                 {
                                     if (i[0] == 10 && i[1] == 5)
                                         return rgba(200, 100, 50, 255);
                                     if (i[0] == 11 && i[1] == 5)
                                         return rgba(200, 100, 50, 0);
                                     return rgba(0, 0, 0, 255);
                                 }) && ok;
    ok = writeImage<GrayWithAlpha>((dir / "graya.png").string(), planeSize(), [](const GrayWithAlpha::IndexType &i)
                                   {
                                       if (i[0] == 10 && i[1] == 5)
                                           return grayAlpha(100, 255);
                                       if (i[0] == 11 && i[1] == 5)
                                           return grayAlpha(100, 0);
                                       return grayAlpha(0, 255);
                                   }) && ok;
    ok = writeImage<Gray16>((dir / "gray16.tif").string(), planeSize(), [](const Gray16::IndexType &i)
                            { return static_cast<unsigned short>((i[0] == 3 && i[1] == 4) ? 1000 : 0); }) && ok;
    ok = writeImage<Gray>((dir / "gray.jpg").string(), planeSize(), [](const Gray::IndexType &i)
                          { return jpegValue(i[0], i[1]); }) && ok;
    ok = writeImage<Gray>((dir / "spaced.tif").string(), planeSize(), [](const Gray::IndexType &i)
                          { return static_cast<unsigned char>(paletteLabel(i[0])); }, nullptr, 0.5) && ok;
    ok = writeImage<Gray>((dir / "labels.png").string(), planeSize(), [](const Gray::IndexType &i)
                          { return static_cast<unsigned char>(greyLabel(i[0], i[1])); }) && ok;

    auto paletteIO = itk::PNGImageIO::New();
    paletteIO->SetWritePalette(true);
    paletteIO->SetColorPalette({rgb(0, 0, 0), rgb(128, 0, 0), rgb(0, 128, 0)});
    ok = writeImage<Gray>((dir / "palette.png").string(), planeSize(), [](const Gray::IndexType &i)
                          { return static_cast<unsigned char>(paletteLabel(i[0])); }, paletteIO.GetPointer()) && ok;

    Stack::SizeType stackSize;
    stackSize[0] = kStackX;
    stackSize[1] = kStackY;
    stackSize[2] = kStackZ;
    ok = writeImage<Stack>((dir / "stack.tif").string(), stackSize, [](const Stack::IndexType &i)
                           { return static_cast<unsigned char>(i[0] + kStackX * i[1] + kStackX * kStackY * i[2]); }) && ok;

    ok = writeBmp32((dir / "rgb32.bmp").string(), kWidth, kHeight, rgb(200, 100, 50)) && ok;
    ok = writeUint32Tiff((dir / "uint32.tif").string(), kWidth, kHeight) && ok;
    return ok;
}

bool hasSize(const NiftiImage &image, unsigned int x, unsigned int y, unsigned int z)
{
    return image.getSizeX() == x && image.getSizeY() == y && image.getSizeZ() == z;
}

bool hasUnitSpacing(const NiftiImage &image)
{
    return image.getSpacingX() == 1.0 && image.getSpacingY() == 1.0 && image.getSpacingZ() == 1.0;
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

template <typename LabelAt>
bool maskHasLabels(const MaskVolume &mask, LabelAt labelAt)
{
    if (mask.dimX != kWidth || mask.dimY != kHeight || mask.dimZ != 1 || !mask.isValid())
        return false;
    for (unsigned int y = 0; y < kHeight; ++y)
        for (unsigned int x = 0; x < kWidth; ++x)
            if (mask.data[x + kWidth * y] != labelAt(x, y))
                return false;
    return true;
}

void checkImages(const std::filesystem::path &dir)
{
    NiftiImage gray, colour, colourBmp, colour32, alpha, grayWithAlpha, deep, jpeg, spaced, stack;
    check(gray.load((dir / "gray.png").string()), "gray.png loads");
    check(colour.load((dir / "rgb.png").string()), "rgb.png loads");
    check(colourBmp.load((dir / "rgb.bmp").string()), "rgb.bmp (24-bit) loads");
    check(colour32.load((dir / "rgb32.bmp").string()), "rgb32.bmp (32-bit, fourth byte 0) loads");
    check(alpha.load((dir / "rgba.png").string()), "rgba.png loads");
    check(grayWithAlpha.load((dir / "graya.png").string()), "graya.png loads");
    check(deep.load((dir / "gray16.tif").string()), "gray16.tif loads");
    check(jpeg.load((dir / "gray.jpg").string()), "gray.jpg loads");
    check(spaced.load((dir / "spaced.tif").string()), "spaced.tif loads");
    check(stack.load((dir / "stack.tif").string()), "stack.tif loads");

    check(hasSize(gray, kWidth, kHeight, 1) && hasSize(colour, kWidth, kHeight, 1) &&
              hasSize(colourBmp, kWidth, kHeight, 1) && hasSize(colour32, kWidth, kHeight, 1) &&
              hasSize(alpha, kWidth, kHeight, 1) && hasSize(grayWithAlpha, kWidth, kHeight, 1) &&
              hasSize(deep, kWidth, kHeight, 1) && hasSize(jpeg, kWidth, kHeight, 1),
          "every 2D file is 40 x 30 x 1");

    check(gray.getVoxelValue(7, 2, 0) == 9.0f, "gray.png (7,2) is x + y = 9");
    check(gray.getVoxelValue(0, 0, 0) == 0.0f && gray.getVoxelValue(39, 29, 0) == 68.0f,
          "gray.png first file row is y = 0 (no flip)");

    std::printf("  rgb.png (10,5) = %.4f, rgb.bmp = %.4f, rgb32.bmp = %.4f\n", colour.getVoxelValue(10, 5, 0),
                colourBmp.getVoxelValue(10, 5, 0), colour32.getVoxelValue(10, 5, 0));
    check(std::abs(colour.getVoxelValue(10, 5, 0) - kLuminance) <= 1e-3f, "rgb.png (10,5) is the luminance of (200,100,50)");
    check(colour.getVoxelValue(11, 5, 0) == 0.0f, "rgb.png black pixel reads 0");
    check(std::abs(colourBmp.getVoxelValue(10, 5, 0) - kLuminance) <= 1e-3f && colourBmp.getVoxelValue(0, 0, 0) == 0.0f,
          "rgb.bmp (10,5) is the same luminance");
    check(std::abs(colour32.getVoxelValue(10, 5, 0) - kLuminance) <= 1e-3f,
          "rgb32.bmp reads its luminance, not 0 (fourth byte ignored)");

    std::printf("  rgba.png (10,5) = %.4f, (11,5) = %.4f\n", alpha.getVoxelValue(10, 5, 0), alpha.getVoxelValue(11, 5, 0));
    check(std::abs(alpha.getVoxelValue(10, 5, 0) - kLuminance) <= 1e-3f, "rgba.png opaque pixel is the luminance");
    check(std::abs(alpha.getVoxelValue(11, 5, 0) - kLuminance) <= 1e-3f, "rgba.png alpha-0 pixel is the luminance too (alpha ignored)");
    check(grayWithAlpha.getVoxelValue(10, 5, 0) == 100.0f && grayWithAlpha.getVoxelValue(11, 5, 0) == 100.0f,
          "graya.png reads its grey, 100, at alpha 255 and alpha 0");
    check(grayWithAlpha.getVoxelValue(0, 0, 0) == 0.0f, "graya.png grey 0 reads 0");

    check(deep.getVoxelValue(3, 4, 0) == 1000.0f, "gray16.tif (3,4) keeps 16-bit value 1000");

    float jpegError = 0.0f;
    for (unsigned int y = 0; y < kHeight; ++y)
        for (unsigned int x = 0; x < kWidth; ++x)
            jpegError = std::max(jpegError, std::abs(jpeg.getVoxelValue(x, y, 0) - static_cast<float>(jpegValue(x, y))));
    std::printf("  gray.jpg largest error = %.1f levels\n", jpegError);
    check(jpegError <= 4.0f, "gray.jpg is within 4 levels of the written gradient");

    double itkSpacing = 0.0;
    try
    {
        auto reader = itk::ImageFileReader<itk::Image<float, 2>>::New();
        reader->SetFileName((dir / "spaced.tif").string());
        reader->UpdateOutputInformation();
        itkSpacing = reader->GetOutput()->GetSpacing()[0];
    }
    catch (const itk::ExceptionObject &)
    {
    }
    std::printf("  spaced.tif spacing as ITK reads it = %.4f\n", itkSpacing);
    check(std::abs(itkSpacing - 0.5) < 1e-6, "spaced.tif carries 0.5 mm in its resolution tags");
    check(hasUnitSpacing(spaced), "spaced.tif reads with spacing 1 (pixels, not DPI)");
    check(hasUnitSpacing(colour) && hasUnitSpacing(jpeg) && hasUnitSpacing(stack), "every raster reads with spacing 1");

    NiftiImage refused;
    check(!refused.load((dir / "uint32.tif").string()), "uint32.tif (32-bit integer samples) is refused");
    std::printf("  uint32.tif lastError: %s\n", refused.lastError().c_str());
    check(refused.lastError().find("32-bit") != std::string::npos, "the refusal sets lastError, naming the 32-bit samples");
    check(refused.load((dir / "gray.png").string()) && refused.lastError().empty(), "a successful load clears lastError");
    NiftiImage kept;
    const bool keptLoaded = kept.load((dir / "gray16.tif").string());
    const bool keptAfterRefusal = keptLoaded && !kept.load((dir / "uint32.tif").string()) &&
                                  hasSize(kept, kWidth, kHeight, 1) && kept.getVoxelValue(3, 4, 0) == 1000.0f;
    check(keptAfterRefusal, "a refused load leaves the loaded image as it was");

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
}

void checkMasks(const std::filesystem::path &dir)
{
    const NpzImportOptions noNumpy;
    MaskVolume mask;
    QString error;

    const bool palette = readMaskVolume((dir / "palette.png").string(), noNumpy, mask, &error);
    check(palette && maskHasLabels(mask, [](unsigned int x, unsigned int) { return paletteLabel(x); }),
          "palette.png mask reads its indices {0,1,2}, not colours");
    check(palette && mask.distinctLabels() == std::vector<int>({1, 2}), "palette.png mask labels are 1 and 2");

    const bool grey = readMaskVolume((dir / "labels.png").string(), noNumpy, mask, &error);
    check(grey && maskHasLabels(mask, greyLabel), "labels.png (8-bit grey) mask reads exact labels {0,1,2,5}");

    const bool spaced = readMaskVolume((dir / "spaced.tif").string(), noNumpy, mask, &error);
    check(spaced && mask.spacingX == 1.0 && mask.spacingY == 1.0 && mask.spacingZ == 1.0,
          "spaced.tif mask reads with spacing 1, as the image does");

    error.clear();
    const bool greyAlpha = readMaskVolume((dir / "graya.png").string(), noNumpy, mask, &error);
    std::printf("  graya.png mask error: %s\n", qPrintable(error));
    check(!greyAlpha && !error.isEmpty() && !mask.isValid(), "graya.png (grey+alpha) mask is refused with a message");
    error.clear();
    const bool colour = readMaskVolume((dir / "rgb.png").string(), noNumpy, mask, &error);
    check(!colour && !error.isEmpty(), "rgb.png (colour) mask is refused with a message");
    error.clear();
    const bool wide = readMaskVolume((dir / "uint32.tif").string(), noNumpy, mask, &error);
    std::printf("  uint32.tif mask error: %s\n", qPrintable(error));
    check(!wide && !error.isEmpty(), "uint32.tif mask is refused with a message");
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

    checkImages(dir);
    checkMasks(dir);

    fs::remove_all(dir, ec);
    std::printf("%s\n", failures ? "FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
