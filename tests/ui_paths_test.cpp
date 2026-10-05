// Checks on the helpers that decide what counts as an openable image.
// They gate the file dialogs, the CSV importer and the folder scan, so a
// regression here silently makes a supported format unopenable.
#include "UiUtils.h"

#include <QCoreApplication>
#include <cstdio>
#include <string>

using namespace UiUtils;

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-58s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    check(isSupportedImagePath("case.npz"), "supported: case.npz");
    check(isSupportedImagePath("case.npy"), "supported: case.npy");
    check(isSupportedImagePath("case.nii"), "supported: case.nii");
    check(isSupportedImagePath("case.nii.gz"), "supported: case.nii.gz");
    check(isSupportedImagePath("slice.dcm"), "supported: slice.dcm");
    check(isSupportedImagePath("CASE.NPZ"), "supported: extension match is case-insensitive");
    for (const char *name : {"a.png", "a.JPG", "a.jpeg", "a.bmp", "a.tif", "a.tiff"})
    {
        check(isSupportedImagePath(name), (std::string("supported: ") + name).c_str());
        check(isRasterImagePath(name), (std::string("raster: ") + name).c_str());
    }
    check(!isRasterImagePath("a.nii.gz"), "not raster: a.nii.gz");
    check(!isRasterImagePath("a.npz"), "not raster: a.npz");
    check(!isSupportedImagePath("notes.txt"), "not supported: notes.txt");
    check(!isSupportedImagePath(""), "not supported: empty path");

    check(isImagePathCell("  /data/case.npz  "), "csv cell: surrounding blanks tolerated");
    check(isImagePathCell("case.nii.gz"), "csv cell: nifti still accepted");
    check(!isImagePathCell("seeds.txt"), "csv cell: seed file rejected");

    check(isMaskFilenameCandidate("seg.npz"), "mask scan: seg.npz accepted");
    check(isMaskFilenameCandidate("seg.nii.gz"), "mask scan: seg.nii.gz accepted");
    check(!isMaskFilenameCandidate("a.png"), "mask scan: raster rejected (a folder of photographs)");
    check(!isMaskFilenameCandidate("slice.dcm"), "mask scan: dicom rejected (holds no labels)");

    // Base names drive derived output names and the mask/seed folder scan.
    check(stripImageSuffix("case.npz") == "case", "stripImageSuffix: case.npz -> case");
    check(stripImageSuffix("case.npy") == "case", "stripImageSuffix: case.npy -> case");
    check(stripImageSuffix("case.nii") == "case", "stripImageSuffix: case.nii -> case");
    check(stripImageSuffix("case.nii.gz") == "case", "stripImageSuffix: case.nii.gz -> case");
    check(stripImageSuffix("photo.png") == "photo", "stripImageSuffix: photo.png -> photo");
    check(stripImageSuffix("scan.tiff") == "scan", "stripImageSuffix: scan.tiff -> scan");
    check(stripImageSuffix("x.JPEG") == "x", "stripImageSuffix: x.JPEG -> x");
    check(stripImageSuffix("plain") == "plain", "stripImageSuffix: unknown extension untouched");

    check(imageOpenFileFilter().contains("*.npz"), "open filter offers *.npz");
    check(imageOpenFileFilter().contains("*.nii.gz"), "open filter still offers *.nii.gz");
    check(maskOpenFileFilter().contains("*.npz"), "mask filter offers *.npz");
    check(imageOpenFileFilter().contains("*.png") && imageOpenFileFilter().contains("*.tiff"),
          "open filter offers raster images");
    check(maskOpenFileFilter().contains("*.png") && maskOpenFileFilter().contains("*.tiff"),
          "mask filter offers raster images");
    check(!maskOpenFileFilter().contains("*.jpg") && !maskOpenFileFilter().contains("*.jpeg"),
          "mask filter omits JPEG (lossy, labels would blur)");

    std::printf("\n%s\n", failures ? "FAILURES" : "all path-helper checks passed");
    return failures ? 1 : 0;
}
