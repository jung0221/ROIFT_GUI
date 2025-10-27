#include "NiftiImage.h"
#if HAVE_ITK
#include <itkImageFileReader.h>
#include <itkMinimumMaximumImageCalculator.h>
#include <itkImageFileWriter.h>
#include <itkNiftiImageIO.h>
#include <itkImageRegionIterator.h>
#include <itkImageDuplicator.h>
#include <algorithm>
#include <filesystem>

NiftiImage::NiftiImage() {}
NiftiImage::~NiftiImage() {}

bool NiftiImage::load(const std::string &path) {
    using ReaderType = itk::ImageFileReader<ImageType>;
    ReaderType::Pointer reader = ReaderType::New();
    reader->SetFileName(path);
    // Defensive diagnostics: ensure the file exists before trying to read
    try {
        if (!std::filesystem::exists(path)) {
            std::cerr << "NiftiImage::load: file does not exist: " << path << std::endl;
            return false;
        }
    } catch (const std::exception &e) {
        // If filesystem check fails for some reason, continue and let ITK report errors
        std::cerr << "NiftiImage::load: filesystem check error: " << e.what() << "\n";
    }

    try {
        reader->Update();
    } catch (itk::ExceptionObject &e) {
        std::cerr << "NiftiImage::load: ITK exception while reading '" << path << "': " << e << std::endl;
        return false;
    } catch (const std::exception &e) {
        std::cerr << "NiftiImage::load: std::exception while reading '" << path << "': " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "NiftiImage::load: unknown exception while reading '" << path << "'\n";
        return false;
    }
    m_image = reader->GetOutput();
    m_region = m_image->GetLargestPossibleRegion();

    using MinMaxCalculatorType = itk::MinimumMaximumImageCalculator<ImageType>;
    MinMaxCalculatorType::Pointer calc = MinMaxCalculatorType::New();
    calc->SetImage(m_image);
    calc->Compute();
    m_min = static_cast<float>(calc->GetMinimum());
    m_max = static_cast<float>(calc->GetMaximum());
    if (m_max == m_min) m_max = m_min + 1.0f;
    // Log loaded image properties for debugging
    std::cerr << "NiftiImage::load: loaded '" << path << "' size=(" << m_region.GetSize()[0] << "," << m_region.GetSize()[1] << "," << m_region.GetSize()[2] << ") min=" << m_min << " max=" << m_max << "\n";
    return true;
}

unsigned int NiftiImage::getSizeX() const { return m_region.GetSize()[0]; }
unsigned int NiftiImage::getSizeY() const { return m_region.GetSize()[1]; }
unsigned int NiftiImage::getSizeZ() const { return m_region.GetSize()[2]; }

float NiftiImage::getGlobalMin() const { return m_min; }
float NiftiImage::getGlobalMax() const { return m_max; }

bool NiftiImage::save(const std::string &path) const {
#if HAVE_ITK
    try {
        using WriterType = itk::ImageFileWriter<ImageType>;
        WriterType::Pointer writer = WriterType::New();
        std::string outpath = path;
        auto has_suffix = [](const std::string &p, const std::string &suf) {
            if (p.size() < suf.size()) return false;
            return p.compare(p.size() - suf.size(), suf.size(), suf) == 0;
        };
        if (!has_suffix(outpath, ".nii") && !has_suffix(outpath, ".nii.gz")) outpath += ".nii.gz";
        itk::NiftiImageIO::Pointer nio = itk::NiftiImageIO::New();
        writer->SetImageIO(nio);
        writer->SetFileName(outpath);
        writer->SetInput(m_image);
        writer->Update();
        return true;
    } catch (const std::exception &e) {
        std::cerr << "NiftiImage::save: exception: " << e.what() << std::endl;
        return false;
    }
#else
    (void)path;
    std::cerr << "NiftiImage::save: not supported without ITK" << std::endl;
    return false;
#endif
}

float NiftiImage::getVoxelValue(unsigned int x, unsigned int y, unsigned int z) const {
#if HAVE_ITK
    ImageType::IndexType idx;
    idx[0] = x; idx[1] = y; idx[2] = z;
    return static_cast<float>(m_image->GetPixel(idx));
#else
    (void)x; (void)y; (void)z;
    return 0.0f;
#endif
}

void NiftiImage::applyThreshold(float threshold, float newValue) {
#if HAVE_ITK
    using IteratorType = itk::ImageRegionIterator<ImageType>;
    ImageType::RegionType region = m_image->GetLargestPossibleRegion();
    IteratorType it(m_image, region);
    for (it.GoToBegin(); !it.IsAtEnd(); ++it) {
        auto v = it.Get();
        if (v > threshold) it.Set(static_cast<PixelType>(newValue));
    }
#else
    (void)threshold; (void)newValue;
#endif
}

#if HAVE_ITK
NiftiImage NiftiImage::deepCopy() const {
    NiftiImage out;
    if (!m_image) return out;
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
#else
NiftiImage NiftiImage::deepCopy() const {
    return NiftiImage();
}
#endif

static void fillRGBFromSlice(const std::vector<PixelType> &slice, std::vector<unsigned char> &out, float lo, float hi, unsigned int w, unsigned int h) {
    out.resize(w * h * 3);
    for (unsigned int i = 0; i < w * h; ++i) {
        float v = slice[i];
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        unsigned char c = static_cast<unsigned char>(255.0f * (v - lo) / (hi - lo));
        out[i*3 + 0] = c;
        out[i*3 + 1] = c;
        out[i*3 + 2] = c;
    }
}

std::vector<unsigned char> NiftiImage::getAxialSliceAsRGB(unsigned int z, float lo, float hi) const {
    unsigned int w = getSizeX();
    unsigned int h = getSizeY();
    std::vector<PixelType> slice(w * h);
    ImageType::IndexType idx;
    for (unsigned int y = 0; y < h; ++y) {
        for (unsigned int x = 0; x < w; ++x) {
            idx[0] = x; idx[1] = y; idx[2] = z;
            slice[y*w + x] = m_image->GetPixel(idx);
        }
    }
    std::vector<unsigned char> out;
    fillRGBFromSlice(slice, out, lo, hi, w, h);
    return out;
}

std::vector<unsigned char> NiftiImage::getSagittalSliceAsRGB(unsigned int x, float lo, float hi) const {
    unsigned int w = getSizeY();
    unsigned int h = getSizeZ();
    std::vector<PixelType> slice(w * h);
    ImageType::IndexType idx;
    for (unsigned int z = 0; z < h; ++z) {
        for (unsigned int y = 0; y < w; ++y) {
            idx[0] = x; idx[1] = y; idx[2] = z;
            slice[z*w + y] = m_image->GetPixel(idx);
        }
    }
    std::vector<unsigned char> out;
    fillRGBFromSlice(slice, out, lo, hi, w, h);
    return out;
}

std::vector<unsigned char> NiftiImage::getCoronalSliceAsRGB(unsigned int yidx, float lo, float hi) const {
    unsigned int w = getSizeX();
    unsigned int h = getSizeZ();
    std::vector<PixelType> slice(w * h);
    ImageType::IndexType idx;
    for (unsigned int z = 0; z < h; ++z) {
        for (unsigned int x = 0; x < w; ++x) {
            idx[0] = x; idx[1] = yidx; idx[2] = z;
            slice[z*w + x] = m_image->GetPixel(idx);
        }
    }
    std::vector<unsigned char> out;
    fillRGBFromSlice(slice, out, lo, hi, w, h);
    return out;
}
#else
#include <cmath>
#include <iostream>

NiftiImage::NiftiImage() {
    // create a small synthetic volume
    m_region.SetSize(0, 128);
    m_region.SetSize(1, 128);
    m_region.SetSize(2, 64);
    m_min = 0.0f; m_max = 1.0f;
}
NiftiImage::~NiftiImage() {}

bool NiftiImage::load(const std::string &path) {
    (void)path;
    // pretend we loaded an image; keep synthetic dims
    return true;
}

unsigned int NiftiImage::getSizeX() const { return m_region.GetSize()[0]; }
unsigned int NiftiImage::getSizeY() const { return m_region.GetSize()[1]; }
unsigned int NiftiImage::getSizeZ() const { return m_region.GetSize()[2]; }

float NiftiImage::getGlobalMin() const { return m_min; }
float NiftiImage::getGlobalMax() const { return m_max; }

static void fillRGBFromSliceSynthetic(std::vector<unsigned char> &out, unsigned int w, unsigned int h) {
    out.resize(w*h*3);
    for (unsigned int y=0;y<h;++y){
        for (unsigned int x=0;x<w;++x){
            float v = float(x)/float(w-1);
            unsigned char c = static_cast<unsigned char>(v*255.0f);
            out[(y*w+x)*3+0] = c;
            out[(y*w+x)*3+1] = c;
            out[(y*w+x)*3+2] = c;
        }
    }
}

std::vector<unsigned char> NiftiImage::getAxialSliceAsRGB(unsigned int z, float lo, float hi) const {
    (void)z; (void)lo; (void)hi;
    std::vector<unsigned char> out; fillRGBFromSliceSynthetic(out, getSizeX(), getSizeY()); return out;
}
std::vector<unsigned char> NiftiImage::getSagittalSliceAsRGB(unsigned int x, float lo, float hi) const {
    (void)x; (void)lo; (void)hi;
    std::vector<unsigned char> out; fillRGBFromSliceSynthetic(out, getSizeY(), getSizeZ()); return out;
}
std::vector<unsigned char> NiftiImage::getCoronalSliceAsRGB(unsigned int yidx, float lo, float hi) const {
    (void)yidx; (void)lo; (void)hi;
    std::vector<unsigned char> out; fillRGBFromSliceSynthetic(out, getSizeX(), getSizeZ()); return out;
}

#endif
