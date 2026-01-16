#pragma once

#include <string>
#include <vector>
#include <itkImage.h>
#include <itkImageFileReader.h>

using PixelType = float;
using ImageType = itk::Image<PixelType, 3>;

class NiftiImage
{
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

    bool isMask() const { return m_isMask; }

private:
    ImageType::Pointer m_image;
    ImageType::RegionType m_region;
    float m_min = 0.0f;
    float m_max = 1.0f;
    bool m_isMask = false;
    itk::ImageIOBase::IOComponentType m_component = itk::ImageIOBase::UNKNOWNCOMPONENTTYPE;
};
