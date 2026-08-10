#include "MaskLayers.h"

#include "ColorUtils.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <exception>
#include <set>

#include <itkImage.h>
#include <itkImageFileReader.h>
#include <itkImageRegionConstIterator.h>

namespace
{
// One colour per mask, for the masks that carry a single label. Chosen for
// distance from each other and from mid-grey CT, and to stay legible at the
// 50% opacity the overlay blends at.
const QColor kMaskSlotPalette[] = {
    QColor(74, 170, 255),  // blue
    QColor(255, 90, 70),   // red-orange
    QColor(80, 220, 120),  // green
    QColor(255, 194, 74),  // amber
    QColor(199, 125, 255), // violet
    QColor(55, 224, 208),  // teal
    QColor(255, 122, 200), // pink
    QColor(167, 224, 74),  // lime
    QColor(255, 155, 74),  // orange
    QColor(122, 155, 255), // indigo
    QColor(224, 224, 74),  // yellow
    QColor(74, 224, 160),  // mint
};
constexpr int kMaskSlotPaletteSize = static_cast<int>(sizeof(kMaskSlotPalette) / sizeof(kMaskSlotPalette[0]));
} // namespace

std::size_t MaskVolume::voxelCount() const
{
    return static_cast<std::size_t>(dimX) * static_cast<std::size_t>(dimY) * static_cast<std::size_t>(dimZ);
}

bool MaskVolume::isValid() const
{
    return dimX > 0 && dimY > 0 && dimZ > 0 && !data.empty() && data.size() == voxelCount();
}

std::vector<int> MaskVolume::distinctLabels() const
{
    return distinctMaskLabels(data);
}

std::vector<int> distinctMaskLabels(const std::vector<int> &data)
{
    std::set<int> present;
    for (int value : data)
    {
        if (value != 0)
            present.insert(value);
    }
    return std::vector<int>(present.begin(), present.end());
}

bool readMaskVolume(const std::string &path,
                    const NpzImportOptions &numpyOptions,
                    MaskVolume &out,
                    QString *error)
{
    if (error)
        error->clear();
    out = MaskVolume();

    try
    {
        if (NiftiImage::isNumpyPath(path))
        {
            NiftiImage volume;
            std::string importError;
            if (!volume.loadNumpy(path, numpyOptions, nullptr, &importError))
            {
                if (error)
                    *error = QString::fromStdString(importError);
                return false;
            }
            out.dimX = volume.getSizeX();
            out.dimY = volume.getSizeY();
            out.dimZ = volume.getSizeZ();
            out.spacingX = volume.getSpacingX();
            out.spacingY = volume.getSpacingY();
            out.spacingZ = volume.getSpacingZ();
            out.data.resize(out.voxelCount());
            std::size_t writeIdx = 0;
            for (unsigned int z = 0; z < out.dimZ; ++z)
                for (unsigned int y = 0; y < out.dimY; ++y)
                    for (unsigned int x = 0; x < out.dimX; ++x)
                        out.data[writeIdx++] = static_cast<int>(std::lround(volume.getVoxelValue(x, y, z)));
        }
        else
        {
            using MaskImageType = itk::Image<int32_t, 3>;
            using ReaderType = itk::ImageFileReader<MaskImageType>;
            ReaderType::Pointer reader = ReaderType::New();
            reader->SetFileName(path);
            reader->Update();
            MaskImageType::Pointer img = reader->GetOutput();
            const MaskImageType::RegionType region = img->GetLargestPossibleRegion();
            const MaskImageType::SizeType size = region.GetSize();
            out.dimX = static_cast<unsigned int>(size[0]);
            out.dimY = static_cast<unsigned int>(size[1]);
            out.dimZ = static_cast<unsigned int>(size[2]);
            const auto spacing = img->GetSpacing();
            out.spacingX = std::abs(static_cast<double>(spacing[0]));
            out.spacingY = std::abs(static_cast<double>(spacing[1]));
            out.spacingZ = std::abs(static_cast<double>(spacing[2]));
            out.data.resize(out.voxelCount());
            itk::ImageRegionConstIterator<MaskImageType> it(img, region);
            std::size_t writeIdx = 0;
            for (it.GoToBegin(); !it.IsAtEnd(); ++it, ++writeIdx)
                out.data[writeIdx] = static_cast<int>(it.Get());
        }
    }
    catch (const std::exception &e)
    {
        out = MaskVolume();
        if (error)
            *error = QString::fromLatin1(e.what());
        return false;
    }

    // Spacing of 0 or NaN would divide by zero downstream; fall back to isotropic.
    for (double *s : {&out.spacingX, &out.spacingY, &out.spacingZ})
    {
        if (!std::isfinite(*s) || *s <= 0.0)
            *s = 1.0;
    }

    if (!out.isValid())
    {
        if (error && error->isEmpty())
            *error = QStringLiteral("Mask has no voxels: %1").arg(QFileInfo(QString::fromStdString(path)).fileName());
        out = MaskVolume();
        return false;
    }
    return true;
}

bool MaskLayer::usesLabelPalette() const
{
    switch (colorMode)
    {
    case MaskColorMode::PerLabel:
        return true;
    case MaskColorMode::PerMask:
        return false;
    case MaskColorMode::Auto:
    default:
        return labels.size() > 1;
    }
}

QColor MaskLayer::colorForLabelValue(int label) const
{
    if (usesLabelPalette())
        return colorForLabel(std::max(0, std::min(255, label)));
    return color.isValid() ? color : maskSlotColor(colorSlot);
}

std::vector<QColor> MaskLayer::swatchColors(int maxColors) const
{
    std::vector<QColor> colors;
    if (!usesLabelPalette())
    {
        colors.push_back(color.isValid() ? color : maskSlotColor(colorSlot));
        return colors;
    }
    const int wanted = std::max(1, maxColors);
    for (int label : labels)
    {
        if (static_cast<int>(colors.size()) >= wanted)
            break;
        colors.push_back(colorForLabel(std::max(0, std::min(255, label))));
    }
    if (colors.empty())
        colors.push_back(color.isValid() ? color : maskSlotColor(colorSlot));
    return colors;
}

QColor maskSlotColor(int slot)
{
    const int index = ((slot % kMaskSlotPaletteSize) + kMaskSlotPaletteSize) % kMaskSlotPaletteSize;
    return kMaskSlotPalette[index];
}

int maskSlotCount()
{
    return kMaskSlotPaletteSize;
}
