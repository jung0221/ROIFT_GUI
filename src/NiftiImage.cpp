#include "NiftiImage.h"
#include <itkImageFileReader.h>
#include <itkMinimumMaximumImageCalculator.h>
#include <itkImageFileWriter.h>
#include <itkNiftiImageIO.h>
#include <itkImageRegionIterator.h>
#include <itkImageDuplicator.h>
#include <algorithm>
#include <filesystem>
#include <unordered_set>
#include <cmath>
#include <zlib.h>
#include <cstdio>
#include <itkImageIOFactory.h>
#include <itkImageIOBase.h>

NiftiImage::NiftiImage() {}
NiftiImage::~NiftiImage() {}

bool NiftiImage::load(const std::string &path)
{
    auto has_suffix_ci = [](const std::string &p, const std::string &suf)
    {
        if (p.size() < suf.size())
            return false;
        return std::equal(suf.rbegin(), suf.rend(), p.rbegin(), p.rend(), [](char a, char b)
                          { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    };

    // If .nii.gz, decompress to a temporary .nii to avoid any plugin quirks.
    std::string actualPath = path;
    std::string tempPath;

    auto cleanupTemp = [&]() {
        if (!tempPath.empty())
        {
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
        }
    };

    auto decompressGzip = [&](const std::string &src, const std::string &dst) -> bool
    {
        gzFile in = gzopen(src.c_str(), "rb");
        if (!in)
        {
            std::cerr << "NiftiImage::load: failed to open gzip source: " << src << "\n";
            return false;
        }
        FILE *out = std::fopen(dst.c_str(), "wb");
        if (!out)
        {
            std::cerr << "NiftiImage::load: failed to open temp output: " << dst << "\n";
            gzclose(in);
            return false;
        }
        constexpr size_t CHUNK = 1 << 15;
        std::vector<unsigned char> buf(CHUNK);
        int readBytes = 0;
        while ((readBytes = gzread(in, buf.data(), static_cast<unsigned int>(buf.size()))) > 0)
        {
            if (std::fwrite(buf.data(), 1, static_cast<size_t>(readBytes), out) != static_cast<size_t>(readBytes))
            {
                std::cerr << "NiftiImage::load: write error while decompressing " << src << "\n";
                gzclose(in);
                std::fclose(out);
                return false;
            }
        }
        gzclose(in);
        std::fclose(out);
        return true;
    };

    if (has_suffix_ci(path, ".nii.gz"))
    {
        try
        {
            auto tmpdir = std::filesystem::temp_directory_path();
            auto stem = std::filesystem::path(path).stem().string(); // stem of .nii.gz -> .nii
            tempPath = (tmpdir / (stem + "_decompressed.nii")).string();
            if (!decompressGzip(path, tempPath))
            {
                cleanupTemp();
                return false;
            }
            actualPath = tempPath;
        }
        catch (const std::exception &e)
        {
            std::cerr << "NiftiImage::load: failed to create temp for gzip: " << e.what() << "\n";
            cleanupTemp();
            return false;
        }
    }

    // Defensive diagnostics: ensure the file exists before trying to read
    try
    {
        if (!std::filesystem::exists(actualPath))
        {
            std::cerr << "NiftiImage::load: file does not exist: " << actualPath << std::endl;
            return false;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "NiftiImage::load: filesystem check error: " << e.what() << "\n";
    }

    // Read directly into float using NIfTI IO (ITK handles conversion from
    // integer types, preserving signedness and scl_slope/scl_inter).
    try
    {
        itk::NiftiImageIO::Pointer nio = itk::NiftiImageIO::New();
        nio->SetFileName(actualPath);
        nio->ReadImageInformation();
        m_component = nio->GetComponentType();

        using ReaderType = itk::ImageFileReader<ImageType>;
        ReaderType::Pointer reader = ReaderType::New();
        reader->SetImageIO(nio);
        reader->SetFileName(actualPath);
        reader->Update();
        m_image = reader->GetOutput();
        if (!m_image)
        {
            std::cerr << "NiftiImage::load: reader produced null output for '" << path << "'" << std::endl;
            return false;
        }
        m_region = m_image->GetLargestPossibleRegion();
    }
    catch (itk::ExceptionObject &e)
    {
        std::cerr << "NiftiImage::load: ITK exception while reading '" << path << "': " << e << std::endl;
        return false;
    }
    catch (const std::exception &e)
    {
        std::cerr << "NiftiImage::load: std::exception while reading '" << path << "': " << e.what() << std::endl;
        return false;
    }
    catch (...)
    {
        std::cerr << "NiftiImage::load: unknown exception while reading '" << path << "'\n";
        return false;
    }

    if (m_region.GetSize()[0] == 0 || m_region.GetSize()[1] == 0 || m_region.GetSize()[2] == 0)
    {
        std::cerr << "NiftiImage::load: image has zero size in one or more dimensions for '" << path << "' size=(" << m_region.GetSize()[0] << "," << m_region.GetSize()[1] << "," << m_region.GetSize()[2] << ")" << std::endl;
        return false;
    }
    if (m_region.GetSize()[0] == 0 || m_region.GetSize()[1] == 0 || m_region.GetSize()[2] == 0)
    {
        std::cerr << "NiftiImage::load: image has zero size in one or more dimensions for '" << path << "' size=(" << m_region.GetSize()[0] << "," << m_region.GetSize()[1] << "," << m_region.GetSize()[2] << ")" << std::endl;
        return false;
    }

    using MinMaxCalculatorType = itk::MinimumMaximumImageCalculator<ImageType>;
    MinMaxCalculatorType::Pointer calc = MinMaxCalculatorType::New();
    calc->SetImage(m_image);
    calc->Compute();
    m_min = static_cast<float>(calc->GetMinimum());
    m_max = static_cast<float>(calc->GetMaximum());
    if (m_max == m_min)
        m_max = m_min + 1.0f;

    const bool isInteger = (m_component == itk::ImageIOBase::UCHAR || m_component == itk::ImageIOBase::CHAR ||
                            m_component == itk::ImageIOBase::USHORT || m_component == itk::ImageIOBase::SHORT ||
                            m_component == itk::ImageIOBase::UINT || m_component == itk::ImageIOBase::INT ||
                            m_component == itk::ImageIOBase::ULONG || m_component == itk::ImageIOBase::LONG);

    // Sample voxels to decide if this is a mask: small integer range or few unique values.
    size_t uniqueLimit = 16;
    std::unordered_set<int> uniques;
    size_t sampleLimit = 200000; // enough to classify without costing too much
    size_t sampled = 0;
    if (isInteger)
    {
        itk::ImageRegionConstIterator<ImageType> it(m_image, m_region);
        for (it.GoToBegin(); !it.IsAtEnd() && sampled < sampleLimit; ++it, ++sampled)
        {
            int v = static_cast<int>(std::lrint(it.Value()));
            uniques.insert(v);
            if (uniques.size() > uniqueLimit)
                break;
        }
    }

    m_isMask = false;
    if (isInteger)
    {
        const bool smallRange = (m_max - m_min) <= 1.5f;
        const bool fewValues = uniques.size() > 0 && uniques.size() <= 8;
        if (smallRange || fewValues)
            m_isMask = true;
    }

    // For masks, normalize min/max to [0,1] to avoid windowing artifacts.
    if (m_isMask)
    {
        m_min = 0.0f;
        m_max = 1.0f;
    }

    // Log loaded image properties for debugging
    std::cerr << "NiftiImage::load: loaded '" << path << "' (actual='" << actualPath << "') size=(" << m_region.GetSize()[0] << "," << m_region.GetSize()[1] << "," << m_region.GetSize()[2] << ") min=" << m_min << " max=" << m_max << " comp=" << m_component << " isMask=" << (m_isMask ? "yes" : "no") << " uniq=" << uniques.size() << " sampled=" << sampled << "\n";
    cleanupTemp();
    return true;
}

unsigned int NiftiImage::getSizeX() const { return m_region.GetSize()[0]; }
unsigned int NiftiImage::getSizeY() const { return m_region.GetSize()[1]; }
unsigned int NiftiImage::getSizeZ() const { return m_region.GetSize()[2]; }

float NiftiImage::getGlobalMin() const { return m_min; }
float NiftiImage::getGlobalMax() const { return m_max; }

bool NiftiImage::save(const std::string &path) const
{
    try
    {
        using WriterType = itk::ImageFileWriter<ImageType>;
        WriterType::Pointer writer = WriterType::New();
        std::string outpath = path;
        auto has_suffix = [](const std::string &p, const std::string &suf)
        {
            if (p.size() < suf.size())
                return false;
            return p.compare(p.size() - suf.size(), suf.size(), suf) == 0;
        };
        if (!has_suffix(outpath, ".nii") && !has_suffix(outpath, ".nii.gz"))
            outpath += ".nii.gz";
        itk::NiftiImageIO::Pointer nio = itk::NiftiImageIO::New();
        writer->SetImageIO(nio);
        writer->SetFileName(outpath);
        writer->SetInput(m_image);
        writer->Update();
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "NiftiImage::save: exception: " << e.what() << std::endl;
        return false;
    }
}

float NiftiImage::getVoxelValue(unsigned int x, unsigned int y, unsigned int z) const
{
    if (!m_image)
    {
        return 0.0f;
    }
    ImageType::IndexType idx;
    idx[0] = x;
    idx[1] = y;
    idx[2] = z;
    ImageType::RegionType region = m_image->GetLargestPossibleRegion();
    if (!region.IsInside(idx))
    {
        return 0.0f;
    }
    return static_cast<float>(m_image->GetPixel(idx));
}

void NiftiImage::applyThreshold(float threshold, float newValue)
{
    if (!m_image)
        return;
    using IteratorType = itk::ImageRegionIterator<ImageType>;
    ImageType::RegionType region = m_image->GetLargestPossibleRegion();
    IteratorType it(m_image, region);
    for (it.GoToBegin(); !it.IsAtEnd(); ++it)
    {
        auto v = it.Get();
        if (v > threshold)
            it.Set(static_cast<PixelType>(newValue));
    }
}

NiftiImage NiftiImage::deepCopy() const
{
    NiftiImage out;
    if (!m_image)
        return out;
    using DuplicatorType = itk::ImageDuplicator<ImageType>;
    DuplicatorType::Pointer dup = DuplicatorType::New();
    dup->SetInputImage(m_image);
    dup->Update();
    out.m_image = dup->GetOutput();
    out.m_region = out.m_image->GetLargestPossibleRegion();
    out.m_min = m_min;
    out.m_max = m_max;
    return out;
}

static void fillRGBFromSlice(const std::vector<PixelType> &slice, std::vector<unsigned char> &out, float lo, float hi, unsigned int w, unsigned int h, bool isMask)
{
    out.resize(w * h * 3);
    if (isMask)
    {
        for (unsigned int i = 0; i < w * h; ++i)
        {
            float v = slice[i];
            unsigned char c = (std::abs(v) > 0.5f) ? 255u : 0u; // any non-zero -> 255
            out[i * 3 + 0] = c;
            out[i * 3 + 1] = c;
            out[i * 3 + 2] = c;
        }
    }
    else
    {
        const float denom = (hi - lo != 0.0f) ? (hi - lo) : 1.0f;
        for (unsigned int i = 0; i < w * h; ++i)
        {
            float v = slice[i];
            if (v < lo)
                v = lo;
            if (v > hi)
                v = hi;
            unsigned char c = static_cast<unsigned char>(255.0f * (v - lo) / denom);
            out[i * 3 + 0] = c;
            out[i * 3 + 1] = c;
            out[i * 3 + 2] = c;
        }
    }
}

std::vector<unsigned char> NiftiImage::getAxialSliceAsRGB(unsigned int z, float lo, float hi) const
{
    unsigned int w = getSizeX();
    unsigned int h = getSizeY();
    std::vector<PixelType> slice(w * h);
    ImageType::IndexType idx;
    if (!m_image)
    {
        // Defensive: if image pointer is null, return a black image buffer
        std::fill(slice.begin(), slice.end(), PixelType(0));
    }
    else
    {
        for (unsigned int y = 0; y < h; ++y)
        {
            for (unsigned int x = 0; x < w; ++x)
            {
                idx[0] = x;
                idx[1] = y;
                idx[2] = z;
                slice[y * w + x] = m_image->GetPixel(idx);
            }
        }
    }
    std::vector<unsigned char> out;
    fillRGBFromSlice(slice, out, lo, hi, w, h, m_isMask);
    return out;
}

std::vector<unsigned char> NiftiImage::getSagittalSliceAsRGB(unsigned int x, float lo, float hi) const
{
    unsigned int w = getSizeY();
    unsigned int h = getSizeZ();
    std::vector<PixelType> slice(w * h);
    ImageType::IndexType idx;
    if (!m_image)
    {
        std::fill(slice.begin(), slice.end(), PixelType(0));
    }
    else
    {
        for (unsigned int z = 0; z < h; ++z)
        {
            for (unsigned int y = 0; y < w; ++y)
            {
                idx[0] = x;
                idx[1] = y;
                idx[2] = z;
                slice[z * w + y] = m_image->GetPixel(idx);
            }
        }
    }
    std::vector<unsigned char> out;
    fillRGBFromSlice(slice, out, lo, hi, w, h, m_isMask);
    return out;
}

std::vector<unsigned char> NiftiImage::getCoronalSliceAsRGB(unsigned int yidx, float lo, float hi) const
{
    unsigned int w = getSizeX();
    unsigned int h = getSizeZ();
    std::vector<PixelType> slice(w * h);
    ImageType::IndexType idx;
    if (!m_image)
    {
        std::fill(slice.begin(), slice.end(), PixelType(0));
    }
    else
    {
        for (unsigned int z = 0; z < h; ++z)
        {
            for (unsigned int x = 0; x < w; ++x)
            {
                idx[0] = x;
                idx[1] = yidx;
                idx[2] = z;
                slice[z * w + x] = m_image->GetPixel(idx);
            }
        }
    }
    std::vector<unsigned char> out;
    fillRGBFromSlice(slice, out, lo, hi, w, h, m_isMask);
    return out;
}