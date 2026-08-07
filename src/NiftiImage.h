#pragma once

#include <string>
#include <vector>
#include <itkImage.h>
#include <itkImageFileReader.h>

#include "NpzVolume.h"

using PixelType = float;
using ImageType = itk::Image<PixelType, 3>;

// How to turn a numpy array into a 3D medical volume. A .npz/.npy stores raw
// samples only: no spacing, no origin, no orientation and no axis convention,
// so all of that has to be supplied or recovered before the volume is usable.
struct NpzImportOptions
{
    // Which numpy axes map to the image axes. The letters say what the array
    // axes ARE, in order, so ZYX means the array is indexed [z][y][x]. All six
    // permutations are offered because a numpy file records no convention and
    // the common producers disagree: SimpleITK gives ZYX, nibabel XYZ, and
    // stacked DICOM slices give YXZ (rows, columns, slices).
    enum class AxisOrder
    {
        Auto,
        XYZ,
        XZY,
        YXZ,
        YZX,
        ZXY,
        ZYX
    };

    std::string arrayName;               // empty: pick a sensible array automatically
    int channel = -1;                    // channel of a 4D array; -1: first channel
    AxisOrder axisOrder = AxisOrder::Auto;
    bool flip[3] = {false, false, false}; // mirror the image X / Y / Z axis after mapping
    double spacing[3] = {0.0, 0.0, 0.0}; // all > 0 overrides the resolved spacing
    std::string referencePath;           // volume to copy geometry from; empty: look for a sibling
};

// What the importer did, or would do. Filled by both previewNumpy() and
// loadNumpy() so the UI can show the outcome before any voxel is read.
struct NpzImportReport
{
    std::string arrayName;
    int channel = 0;
    int channelCount = 1;
    unsigned int size[3] = {0, 0, 0};
    double spacing[3] = {1.0, 1.0, 1.0};
    NpzImportOptions::AxisOrder axisOrder = NpzImportOptions::AxisOrder::ZYX;
    bool flip[3] = {false, false, false};
    // True when more than one axis order fits, so the chosen one is a guess.
    bool axisOrderAmbiguous = false;
    std::string geometrySource;    // where spacing/origin/direction came from
    bool geometryResolved = false; // false: fell back to 1 mm isotropic
};

/// "ZYX", "YXZ", ... for an axis order; "auto" for the unresolved value.
const char *npzAxisOrderName(NpzImportOptions::AxisOrder order);

// Which numpy axis carries which image axis, for one shape and axis order.
struct NpzAxisMapping
{
    bool hasChannel = false;
    size_t channelAxis = 0;
    size_t channelCount = 1;
    size_t axisForX = 0, axisForY = 0, axisForZ = 0;
    size_t sizeX = 0, sizeY = 0, sizeZ = 0;
};

/// Resolve `order` against `shape`. Fails for Auto and for non-3D/4D shapes.
bool npzBuildAxisMapping(const std::vector<size_t> &shape, NpzImportOptions::AxisOrder order,
                         NpzAxisMapping &mapping);

class NiftiImage
{
public:
    NiftiImage();
    ~NiftiImage();

    bool load(const std::string &path);
    bool save(const std::string &path) const;

    // True for paths this class routes through the numpy importer.
    static bool isNumpyPath(const std::string &path);
    // List the arrays in a .npz/.npy without loading any of them.
    static bool inspectNumpy(const std::string &path, std::vector<npz::ArrayInfo> &arrays, std::string *error);
    // What loadNumpy() would produce for these options, without reading voxels.
    static bool previewNumpy(const std::string &path, const NpzImportOptions &options,
                             NpzImportReport &report, std::string *error = nullptr);
    // Import one numpy array as a volume. Called by load() with default options.
    bool loadNumpy(const std::string &path, const NpzImportOptions &options,
                   NpzImportReport *report = nullptr, std::string *error = nullptr);
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
    double getSpacingX() const;
    double getSpacingY() const;
    double getSpacingZ() const;

    float getGlobalMin() const;
    float getGlobalMax() const;

    bool isMask() const { return m_isMask; }

private:
    // Loads a DICOM volume from a directory of slices or a single DICOM file.
    bool loadDicomSeries(const std::string &path);
    // Shared post-read processing (min/max, mask classification, logging).
    void finalizeLoad(const std::string &path);

    ImageType::Pointer m_image;
    ImageType::RegionType m_region;
    float m_min = 0.0f;
    float m_max = 1.0f;
    double m_spacingX = 1.0;
    double m_spacingY = 1.0;
    double m_spacingZ = 1.0;
    bool m_isMask = false;
    itk::ImageIOBase::IOComponentType m_component = itk::ImageIOBase::UNKNOWNCOMPONENTTYPE;
};
