#pragma once

#include <string>
#include <vector>
#if HAVE_ITK
#include <itkImage.h>
#include <itkImageFileReader.h>

using PixelType = float;
using ImageType = itk::Image<PixelType, 3>;

class NiftiImage {
#else
#include <array>
using PixelType = float;
struct FakeRegion {
    std::array<unsigned int,3> size{{0,0,0}};
    void SetSize(unsigned int i, unsigned int v){ size[i]=v; }
    std::array<unsigned int,3> GetSize() const { return size; }
};

class NiftiImage {
#endif
 public:
    NiftiImage();
    ~NiftiImage();

    bool load(const std::string &path);
    bool save(const std::string &path) const;
    // return voxel value at x,y,z (no bounds checking)
    float getVoxelValue(unsigned int x, unsigned int y, unsigned int z) const;
    // apply threshold: for all voxels with value > threshold, set to newValue
    void applyThreshold(float threshold, float newValue);
    // deep copy the image (returns an independent NiftiImage)
    NiftiImage deepCopy() const;
    std::vector<unsigned char> getAxialSliceAsRGB(unsigned int z, float lo, float hi) const;
    std::vector<unsigned char> getSagittalSliceAsRGB(unsigned int x, float lo, float hi) const;
    std::vector<unsigned char> getCoronalSliceAsRGB(unsigned int y, float lo, float hi) const;

    unsigned int getSizeX() const;
    unsigned int getSizeY() const;
    unsigned int getSizeZ() const;

    float getGlobalMin() const;
    float getGlobalMax() const;

private:
#if HAVE_ITK
    ImageType::Pointer m_image;
    ImageType::RegionType m_region;
#else
    FakeRegion m_region;
#endif
    float m_min = 0.0f;
    float m_max = 1.0f;
};
