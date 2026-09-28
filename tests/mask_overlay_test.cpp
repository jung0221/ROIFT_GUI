// Checks that the viewer can draw more than one mask at a time, and that it
// draws exactly the masks whose eye is open.
//
// Editing and drawing are independent: selecting a mask loads it into the
// editable buffer without putting it on screen, while the eye shows a mask
// whether or not it is the one being edited. Either half is easy to break from
// either side — the layer bookkeeping in ManualSeedSelector, or the blend that
// walks the layers — so this drives the real window and reads the composed
// axial slice back.
//
// The two test masks occupy opposite quadrants, so a coloured pixel in one
// quadrant can only have come from one of them.
//
// It also loads a solver network with its segment map, the other feature that
// puts a mask on screen on the program's initiative.
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
#include <QFile>
#include <QCryptographicHash>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
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

/// Send a left click to a row at @p pos, in viewport coordinates.
void clickAt(QListWidget *list, const QPointF &pos)
{
    const QPointF global = list->viewport()->mapToGlobal(pos);
    QMouseEvent press(QEvent::MouseButtonPress, pos, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, pos, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &press);
    QApplication::sendEvent(list->viewport(), &release);
}

/// Click the eye of a row, exactly where the viewport filter looks for it.
void clickEye(QListWidget *list, int row)
{
    clickAt(list, QRectF(MaskListDelegate::eyeRect(list->visualItemRect(list->item(row)))).center());
}

/// Click a row's name, which selects the mask for editing and nothing else.
void clickName(QListWidget *list, int row)
{
    const QRect itemRect = list->visualItemRect(list->item(row));
    clickAt(list, QPointF(itemRect.left() + MaskListDelegate::kTextOffset + 2, itemRect.center().y()));
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

    // Selecting a mask is not a request to see it, and — since nothing is drawn
    // by it — not a reason to read the file either. That read is what made
    // clicking down a list of masks stall the window.
    clickName(maskList, rowForFile(maskList, rightName));
    check(window.activeMaskPath() == QFileInfo(rightPath).absoluteFilePath(),
          "clicking a name picks that mask for editing");
    check(window.activeMaskPending(), "selecting it does not read the file");
    check(visibilityOf(rightName) == MaskVisibility::Hidden, "its eye stays closed");
    check(isGrey(rightQuadrantPixel(axial->image())), "and the selected mask is not drawn");

    // The eye draws a mask that is not the one being edited.
    clickEye(maskList, rowForFile(maskList, leftName));
    check(visibilityOf(leftName) == MaskVisibility::Visible, "eye click shows the mask");
    check(!isGrey(leftQuadrantPixel(axial->image())), "shown mask is drawn in the axial slice");
    check(window.activeMaskPath() == QFileInfo(rightPath).absoluteFilePath(),
          "showing a mask does not change which one is edited");
    check(isGrey(rightQuadrantPixel(axial->image())), "the edited mask is still not drawn");

    const QImage openEye = paintedEye(maskList, rowForFile(maskList, leftName));
    check(!closedEye.isNull() && !openEye.isNull() && closedEye != openEye,
          "the eye is painted differently once the mask is shown");

    // Two masks on screen at once, and the shown one survives the other being
    // loaded for editing.
    clickEye(maskList, rowForFile(maskList, rightName));
    check(visibilityOf(leftName) == MaskVisibility::Visible &&
              visibilityOf(rightName) == MaskVisibility::Visible,
          "both eyes open");
    check(!window.activeMaskPending(), "showing the selected mask is what reads it");

    const QImage bothSlice = axial->image();
    check(!isGrey(leftQuadrantPixel(bothSlice)) && !isGrey(rightQuadrantPixel(bothSlice)),
          "both masks are drawn in the same slice");
    check(leftQuadrantPixel(bothSlice) != rightQuadrantPixel(bothSlice),
          "the two masks are drawn in different colours");

    clickName(maskList, rowForFile(maskList, leftName));
    check(window.activeMaskPath() == QFileInfo(leftPath).absoluteFilePath(), "editing switched masks");
    check(!window.activeMaskPending(),
          "selecting a mask already on screen takes its voxels rather than re-reading them");
    check(!isGrey(leftQuadrantPixel(axial->image())) && !isGrey(rightQuadrantPixel(axial->image())),
          "a shown mask survives another being loaded for editing");

    // Closing the eye takes the mask off screen again.
    clickEye(maskList, rowForFile(maskList, rightName));
    check(visibilityOf(rightName) == MaskVisibility::Hidden, "second eye click hides the mask");
    check(isGrey(rightQuadrantPixel(axial->image())), "hidden mask leaves the slice grey again");
    check(!isGrey(leftQuadrantPixel(axial->image())), "the other mask is still drawn");

    // A mask that arrives on the program's initiative is not silently invisible.
    clickEye(maskList, rowForFile(maskList, leftName));
    check(isGrey(leftQuadrantPixel(axial->image())), "both masks hidden again");
    check(window.applyMaskFromPath(rightPath.toStdString()), "mask applied programmatically");
    check(visibilityOf(rightName) == MaskVisibility::Visible, "a segmentation result opens its own eye");
    check(!isGrey(rightQuadrantPixel(axial->image())), "and lands on screen");

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

    // Solver network: segment 1 fills x < 12, segment 2 the rest, segment 3 has no voxel.
    // The geometry places node 1 at x=2, node 2 at x=12, nodes 3 and 4 at the far corners.
    const QString yamlPath = dir.filePath("case_artery_solver.yaml");
    const QString segmentsPath = dir.filePath("case_artery_solver_segments.nii.gz");
    const QString lumenPath = dir.filePath("case_artery_solver_lumen.nii.gz");
    const QString geometryPath = dir.filePath("case_artery_solver_geometry.json");
    const QByteArray yamlText = "project_name: case\nnetwork:\n"
                                "  - label: PulmonaryTrunk\n    sn: 1\n    tn: 2\n    L: 1.0e-02\n    R0: 7.0e-03\n"
                                "  - label: LPA_01\n    sn: 2\n    tn: 3\n    L: 2.0e-02\n    R0: 4.0e-03\n    R1: 1.5e+08\n"
                                "  - label: RPA_01\n    sn: 2\n    tn: 4\n    L: 2.0e-02\n    R0: 4.0e-03\n    R1: 1.5e+08\n";
    auto writeText = [](const QString &path, const QByteArray &text)
    {
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(text) == text.size();
    };
    auto geometryJson = [&](const QByteArray &sha)
    {
        return QByteArray(R"({"format": "ctsegmentation.solver_geometry", "version": 1, "yaml_sha256": ")") + sha +
               QByteArray(R"(", "files": {"segment_map": "case_artery_solver_segments.nii.gz",
                 "lumen_map": "case_artery_solver_lumen.nii.gz"},
                 "grid": {"shape": [24, 24, 6], "spacing_mm": [1, 1, 1]},
                 "nodes": [{"id": 1, "voxel": [2, 12, 3]}, {"id": 2, "voxel": [12, 12, 3]},
                           {"id": 3, "voxel": [2, 22, 3]}, {"id": 4, "voxel": [22, 22, 3]}],
                 "segments": [
                   {"index": 1, "label": "PulmonaryTrunk", "sn": 1, "tn": 2, "lumen_voxels": 10, "territory_voxels": 10,
                    "centerline": [[2, 12, 3], [12, 12, 3]]},
                   {"index": 2, "label": "LPA_01", "sn": 2, "tn": 3, "lumen_voxels": 5, "territory_voxels": 9,
                    "centerline": [[12, 12, 3], [2, 22, 3]]},
                   {"index": 3, "label": "RPA_01", "sn": 2, "tn": 4, "lumen_voxels": 0, "territory_voxels": 0,
                    "centerline": [[12, 12, 3], [22, 22, 3]]}]})");
    };
    const QByteArray sha = QCryptographicHash::hash(yamlText, QCryptographicHash::Sha256).toHex();
    check(writeText(yamlPath, yamlText) && writeText(geometryPath, geometryJson(sha)) &&
              writeVolume(segmentsPath, [](unsigned int x, unsigned int, unsigned int)
                          { return x < kDimX / 2 ? 1 : 2; }) &&
              writeVolume(lumenPath, [](unsigned int x, unsigned int y, unsigned int)
                          { return x < kDimX / 2 ? 1 : (y < 4 ? 2 : 0); }),
          "solver network written");

    QString networkError;
    check(window.loadSolverNetworkFromPath(yamlPath, &networkError), "solver network loads");
    check(window.solverNetwork().segments.size() == 3, "three segments read");
    check(window.solverNetworkPlaced(), "placed on the image through its geometry");
    check(visibilityOf(QFileInfo(segmentsPath).fileName()) == MaskVisibility::Visible &&
              visibilityOf(QFileInfo(lumenPath).fileName()) != MaskVisibility::Visible,
          "the territory map is drawn, the lumen map is not");
    QTreeWidget *tree = window.findChild<QTreeWidget *>();
    int items = 0;
    if (tree)
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            ++items;
    check(tree && items == 3, "one tree item per segment");
    check(tree && tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->childCount() == 2,
          "the trunk roots the tree, both main arteries under it");
    if (tree && tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->childCount() == 2)
        tree->setCurrentItem(tree->topLevelItem(0)->child(1));
    check(window.selectedSolverSegment() >= 1, "a tree click selects its segment");
    window.selectSolverNode(2, true);
    check(window.selectedSolverNode() == 2 && window.selectedSolverSegment() == -1,
          "selecting a node clears the segment selection");
    window.selectSolverSegment(1, true);
    check(window.selectedSolverSegment() == 1 && tree && tree->currentItem() &&
              tree->currentItem()->text(0) == "LPA_01",
          "selecting from code moves the tree too");

    // Unloading takes the network and both maps out of the viewer, and leaves the files alone.
    window.unloadSolverNetwork();
    check(!window.solverNetworkLoaded() && !window.solverNetworkPlaced(), "unload forgets the network");
    check(rowForFile(maskList, QFileInfo(segmentsPath).fileName()) < 0 &&
              rowForFile(maskList, QFileInfo(lumenPath).fileName()) < 0,
          "and both of its maps leave the mask list");
    check(QFileInfo::exists(segmentsPath) && QFileInfo::exists(yamlPath), "but not the disk");
    check(window.loadSolverNetworkFromPath(yamlPath, &networkError) && window.solverNetworkPlaced(),
          "and it loads again");

    // A geometry for other YAML text is refused whole: nothing is placed from it.
    check(writeText(geometryPath, geometryJson(QByteArray(64, '0'))) &&
              window.loadSolverNetworkFromPath(yamlPath, &networkError),
          "a stale geometry still lets the YAML load");
    check(!window.solverNetworkPlaced(), "but places nothing");

    std::printf("%s\n", failures == 0 ? "all checks passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
