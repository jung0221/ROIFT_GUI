// Checks that the viewer can draw more than one mask at a time.
//
// The window edits one mask and draws any number: the eye in the mask list pins
// a mask on screen, and a pinned mask has to survive another mask being loaded
// for editing. Both halves are easy to break from either side — the pin
// bookkeeping in ManualSeedSelector, or the blend that walks the layers — so
// this drives the real window and reads the composed axial slice back.
//
// The two test masks occupy opposite quadrants, so a coloured pixel in one
// quadrant can only have come from one of them.
#include "ManualSeedSelector.h"
#include "MaskLayers.h"
#include "MaskListDelegate.h"
#include "OrthogonalView.h"
#include "UiUtils.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QListWidget>
#include <QMouseEvent>
#include <QTemporaryDir>

#include <cstdio>
#include <exception>

#include <itkImage.h>
#include <itkImageFileWriter.h>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-58s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

constexpr unsigned int kDimX = 24;
constexpr unsigned int kDimY = 24;
constexpr unsigned int kDimZ = 6;

using VolumeType = itk::Image<int32_t, 3>;

// Write a volume whose voxels come from valueAt(x, y, z).
template <typename ValueAt>
bool writeVolume(const QString &path, ValueAt valueAt)
{
    VolumeType::Pointer image = VolumeType::New();
    VolumeType::SizeType size;
    size[0] = kDimX;
    size[1] = kDimY;
    size[2] = kDimZ;
    VolumeType::IndexType start;
    start.Fill(0);
    VolumeType::RegionType region;
    region.SetSize(size);
    region.SetIndex(start);
    image->SetRegions(region);
    image->Allocate();
    image->FillBuffer(0);

    for (unsigned int z = 0; z < kDimZ; ++z)
        for (unsigned int y = 0; y < kDimY; ++y)
            for (unsigned int x = 0; x < kDimX; ++x)
            {
                VolumeType::IndexType index;
                index[0] = static_cast<itk::IndexValueType>(x);
                index[1] = static_cast<itk::IndexValueType>(y);
                index[2] = static_cast<itk::IndexValueType>(z);
                image->SetPixel(index, valueAt(x, y, z));
            }

    using WriterType = itk::ImageFileWriter<VolumeType>;
    WriterType::Pointer writer = WriterType::New();
    writer->SetFileName(path.toStdString());
    writer->SetInput(image);
    try
    {
        writer->Update();
    }
    catch (const std::exception &e)
    {
        std::printf("failed to write %s: %s\n", qPrintable(path), e.what());
        return false;
    }
    return true;
}

/// The mask list row holding @p fileName, or -1. Rows are renumbered on every
/// refresh, so it is looked up again after anything that rebuilds the list.
int rowForFile(QListWidget *list, const QString &fileName)
{
    for (int row = 0; row < list->count(); ++row)
    {
        const QString path = list->item(row)->data(UiUtils::kPathRole).toString();
        if (QFileInfo(path).fileName() == fileName)
            return row;
    }
    return -1;
}

/// Click the eye of a row, exactly where the viewport filter looks for it.
void clickEye(QListWidget *list, int row)
{
    const QRect itemRect = list->visualItemRect(list->item(row));
    const QPointF pos = QRectF(MaskListDelegate::eyeRect(itemRect)).center();
    const QPointF global = list->viewport()->mapToGlobal(pos);
    QMouseEvent press(QEvent::MouseButtonPress, pos, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, pos, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &press);
    QApplication::sendEvent(list->viewport(), &release);
}

/// The CT under the overlay is greyscale, so any pixel that is not grey has a
/// mask blended into it.
bool isGrey(const QColor &pixel)
{
    return pixel.red() == pixel.green() && pixel.green() == pixel.blue();
}

QColor leftQuadrantPixel(const QImage &slice)
{
    return slice.pixelColor(slice.width() / 4, slice.height() / 4);
}

QColor rightQuadrantPixel(const QImage &slice)
{
    return slice.pixelColor(3 * slice.width() / 4, 3 * slice.height() / 4);
}

/// The eye column of one row as painted, so the two eye states can be compared.
QImage paintedEye(QListWidget *list, int row)
{
    const QRect itemRect = list->visualItemRect(list->item(row));
    return list->viewport()->grab(MaskListDelegate::eyeRect(itemRect)).toImage();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QTemporaryDir workDir;
    if (!workDir.isValid())
    {
        std::printf("could not create a temporary directory\n");
        return 1;
    }
    const QDir dir(workDir.path());

    const QString ctPath = dir.filePath("case_0000.nii.gz");
    const QString leftPath = dir.filePath("case_left.nii.gz");
    const QString rightPath = dir.filePath("case_right.nii.gz");
    const bool wrote =
        writeVolume(ctPath, [](unsigned int x, unsigned int, unsigned int)
                    { return static_cast<int32_t>(x * 10); }) &&
        writeVolume(leftPath, [](unsigned int x, unsigned int y, unsigned int)
                    { return (x < kDimX / 2 && y < kDimY / 2) ? 1 : 0; }) &&
        writeVolume(rightPath, [](unsigned int x, unsigned int y, unsigned int)
                    { return (x >= kDimX / 2 && y >= kDimY / 2) ? 1 : 0; });
    check(wrote, "test data written");
    if (!wrote)
        return 1;

    ManualSeedSelector window("");
    window.addImagesFromPaths({ctPath});
    check(window.hasImage(), "image loaded");
    window.refreshAssociatedFilesForCurrentImage(true);

    QListWidget *maskList = window.findChild<QListWidget *>("maskList");
    OrthogonalView *axial = window.findChild<OrthogonalView *>("axialView");
    check(maskList != nullptr, "mask list found");
    check(axial != nullptr, "axial view found");
    if (!maskList || !axial)
        return 1;

    const QString leftName = QFileInfo(leftPath).fileName();
    const QString rightName = QFileInfo(rightPath).fileName();
    check(rowForFile(maskList, leftName) >= 0 && rowForFile(maskList, rightName) >= 0,
          "both masks listed for the image");
    if (rowForFile(maskList, leftName) < 0 || rowForFile(maskList, rightName) < 0)
        return 1;

    const auto visibilityOf = [maskList](const QString &fileName)
    {
        const int row = rowForFile(maskList, fileName);
        if (row < 0)
            return MaskVisibility::Hidden;
        return static_cast<MaskVisibility>(maskList->item(row)->data(UiUtils::kMaskVisibilityRole).toInt());
    };

    check(visibilityOf(leftName) == MaskVisibility::Hidden &&
              visibilityOf(rightName) == MaskVisibility::Hidden,
          "eyes start closed");
    check(isGrey(leftQuadrantPixel(axial->image())), "nothing is drawn while both eyes are closed");

    maskList->resize(220, 120); // give the rows a width worth painting
    const QImage closedEye = paintedEye(maskList, rowForFile(maskList, leftName));

    // Pin the left mask: it is drawn without ever being loaded for editing.
    clickEye(maskList, rowForFile(maskList, leftName));
    check(visibilityOf(leftName) == MaskVisibility::Pinned, "eye click pins the mask");
    check(!isGrey(leftQuadrantPixel(axial->image())), "pinned mask is drawn in the axial slice");
    check(isGrey(rightQuadrantPixel(axial->image())), "the other mask is still not drawn");

    const QImage openEye = paintedEye(maskList, rowForFile(maskList, leftName));
    check(!closedEye.isNull() && !openEye.isNull() && closedEye != openEye,
          "the eye is painted differently once the mask is pinned");

    // Load the other mask for editing: the pinned one has to stay on screen.
    check(window.applyMaskFromPath(rightPath.toStdString()), "second mask loaded for editing");
    check(visibilityOf(leftName) == MaskVisibility::Pinned, "pinned mask stays pinned");
    check(visibilityOf(rightName) == MaskVisibility::Active, "edited mask reads as active");

    const QImage bothSlice = axial->image();
    check(!isGrey(leftQuadrantPixel(bothSlice)) && !isGrey(rightQuadrantPixel(bothSlice)),
          "both masks are drawn in the same slice");
    check(leftQuadrantPixel(bothSlice) != rightQuadrantPixel(bothSlice),
          "the two masks are drawn in different colours");

    // Closing the eye takes the mask off screen again.
    clickEye(maskList, rowForFile(maskList, leftName));
    check(visibilityOf(leftName) == MaskVisibility::Hidden, "second eye click hides the mask");
    check(isGrey(leftQuadrantPixel(axial->image())), "hidden mask leaves the slice grey again");
    check(!isGrey(rightQuadrantPixel(axial->image())), "the edited mask is still drawn");

    // Colour policy: one colour per mask while the mask is binary, the shared
    // label palette once it carries more than one label.
    MaskLayer layer;
    layer.color = QColor(10, 20, 30);
    layer.labels = {1};
    check(!layer.usesLabelPalette(), "auto: single-label mask uses the mask colour");
    check(layer.colorForLabelValue(1) == QColor(10, 20, 30), "auto: that colour is the one set");
    layer.labels = {1, 2, 7};
    check(layer.usesLabelPalette(), "auto: multi-label mask uses the label palette");
    layer.colorMode = MaskColorMode::PerMask;
    check(!layer.usesLabelPalette(), "per-mask: overrides the label count");
    layer.colorMode = MaskColorMode::PerLabel;
    layer.labels = {1};
    check(layer.usesLabelPalette(), "per-label: overrides the label count too");

    std::printf("%s\n", failures == 0 ? "all checks passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
