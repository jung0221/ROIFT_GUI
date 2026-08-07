#include "NiftiImage.h"
#include <itkImageFileReader.h>
#include <itkImageSeriesReader.h>
#include <itkMinimumMaximumImageCalculator.h>
#include <itkImageFileWriter.h>
#include <itkNiftiImageIO.h>
#include <itkGDCMImageIO.h>
#include <itkGDCMSeriesFileNames.h>
#include <itkImageRegionIterator.h>
#include <itkImageDuplicator.h>
#include <algorithm>
#include <filesystem>
#include <unordered_set>
#include <cmath>
#include <zlib.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <itkImageIOFactory.h>
#include <itkImageIOBase.h>

NiftiImage::NiftiImage() {}
NiftiImage::~NiftiImage() {}

bool NiftiImage::load(const std::string &path)
{
    m_spacingX = 1.0;
    m_spacingY = 1.0;
    m_spacingZ = 1.0;

    auto has_suffix_ci = [](const std::string &p, const std::string &suf)
    {
        if (p.size() < suf.size())
            return false;
        return std::equal(suf.rbegin(), suf.rend(), p.rbegin(), p.rend(), [](char a, char b)
                          { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    };

    // Numpy containers carry no header, so they go through the importer that
    // recovers geometry. Defaults here; callers wanting control use loadNumpy().
    if (isNumpyPath(path))
        return loadNumpy(path, NpzImportOptions{});

    // Route DICOM input (a directory of slices, or a single .dcm/.dicom/.ima file)
    // through the GDCM series reader; everything else is treated as NIfTI.
    {
        std::error_code ec;
        const bool isDir = std::filesystem::is_directory(path, ec);
        std::string ext = std::filesystem::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool dicomExt = (ext == ".dcm" || ext == ".dicom" || ext == ".ima");
        if (isDir || dicomExt)
        {
            if (!loadDicomSeries(path))
                return false;
            if (m_region.GetSize()[0] == 0 || m_region.GetSize()[1] == 0 || m_region.GetSize()[2] == 0)
            {
                std::cerr << "NiftiImage::load: DICOM image has zero size for '" << path << "'" << std::endl;
                return false;
            }
            finalizeLoad(path);
            return true;
        }
    }

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
        const auto spacing = m_image->GetSpacing();
        m_spacingX = std::abs(static_cast<double>(spacing[0]));
        m_spacingY = std::abs(static_cast<double>(spacing[1]));
        m_spacingZ = std::abs(static_cast<double>(spacing[2]));
        if (!std::isfinite(m_spacingX) || m_spacingX <= 0.0)
            m_spacingX = 1.0;
        if (!std::isfinite(m_spacingY) || m_spacingY <= 0.0)
            m_spacingY = 1.0;
        if (!std::isfinite(m_spacingZ) || m_spacingZ <= 0.0)
            m_spacingZ = 1.0;
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

    finalizeLoad(path);
    cleanupTemp();
    return true;
}

// Load a DICOM volume from either a directory of slices or a single DICOM file.
// When given a single file, the whole series it belongs to is reconstructed.
bool NiftiImage::loadDicomSeries(const std::string &path)
{
    try
    {
        std::error_code ec;
        const bool isDir = std::filesystem::is_directory(path, ec);
        std::string dir = isDir ? path : std::filesystem::path(path).parent_path().string();
        if (dir.empty())
            dir = ".";

        itk::GDCMSeriesFileNames::Pointer names = itk::GDCMSeriesFileNames::New();
        names->SetUseSeriesDetails(true);
        names->SetDirectory(dir);

        const std::vector<std::string> &seriesUIDs = names->GetSeriesUIDs();
        if (seriesUIDs.empty())
        {
            std::cerr << "NiftiImage::loadDicomSeries: no DICOM series found in '" << dir << "'\n";
            return false;
        }

        // If a single file was selected, prefer the series that contains it;
        // otherwise fall back to the series with the most slices.
        std::string targetFile;
        if (!isDir)
            targetFile = std::filesystem::absolute(path, ec).string();

        std::string chosenUID;
        std::vector<std::string> fileNames;
        size_t bestCount = 0;
        for (const std::string &uid : seriesUIDs)
        {
            const std::vector<std::string> f = names->GetFileNames(uid);
            if (!targetFile.empty())
            {
                bool match = false;
                for (const std::string &fn : f)
                {
                    std::error_code ec2;
                    if (std::filesystem::equivalent(fn, targetFile, ec2))
                    {
                        match = true;
                        break;
                    }
                }
                if (match)
                {
                    chosenUID = uid;
                    fileNames = f;
                    break;
                }
            }
            if (f.size() > bestCount)
            {
                bestCount = f.size();
                chosenUID = uid;
                fileNames = f;
            }
        }

        if (fileNames.empty())
        {
            std::cerr << "NiftiImage::loadDicomSeries: empty file list for series in '" << dir << "'\n";
            return false;
        }

        itk::GDCMImageIO::Pointer dicomIO = itk::GDCMImageIO::New();
        using SeriesReaderType = itk::ImageSeriesReader<ImageType>;
        SeriesReaderType::Pointer reader = SeriesReaderType::New();
        reader->SetImageIO(dicomIO);
        reader->SetFileNames(fileNames);
        reader->Update();

        m_image = reader->GetOutput();
        if (!m_image)
        {
            std::cerr << "NiftiImage::loadDicomSeries: reader produced null output for '" << path << "'\n";
            return false;
        }
        m_image->DisconnectPipeline();
        m_region = m_image->GetLargestPossibleRegion();
        m_component = dicomIO->GetComponentType();

        const auto spacing = m_image->GetSpacing();
        m_spacingX = std::abs(static_cast<double>(spacing[0]));
        m_spacingY = std::abs(static_cast<double>(spacing[1]));
        m_spacingZ = std::abs(static_cast<double>(spacing[2]));
        if (!std::isfinite(m_spacingX) || m_spacingX <= 0.0)
            m_spacingX = 1.0;
        if (!std::isfinite(m_spacingY) || m_spacingY <= 0.0)
            m_spacingY = 1.0;
        if (!std::isfinite(m_spacingZ) || m_spacingZ <= 0.0)
            m_spacingZ = 1.0;

        std::cerr << "NiftiImage::loadDicomSeries: loaded series '" << chosenUID << "' from '" << dir
                  << "' (" << fileNames.size() << " file(s), " << seriesUIDs.size() << " series in directory)\n";
        return true;
    }
    catch (itk::ExceptionObject &e)
    {
        std::cerr << "NiftiImage::loadDicomSeries: ITK exception while reading '" << path << "': " << e << std::endl;
        return false;
    }
    catch (const std::exception &e)
    {
        std::cerr << "NiftiImage::loadDicomSeries: std::exception while reading '" << path << "': " << e.what() << std::endl;
        return false;
    }
    catch (...)
    {
        std::cerr << "NiftiImage::loadDicomSeries: unknown exception while reading '" << path << "'\n";
        return false;
    }
}

// ============================================================================
// NUMPY (.npz / .npy) IMPORT
//
// A numpy container stores samples and nothing else: no spacing, no origin, no
// orientation, and no agreed axis convention. Everything below exists to pin
// those down before the array is treated as a medical volume, because guessing
// wrong silently mirrors axes or reports distances and volumes in the wrong
// units — errors that look like plausible anatomy on screen.
// ============================================================================

const char *npzAxisOrderName(NpzImportOptions::AxisOrder order)
{
    switch (order)
    {
    case NpzImportOptions::AxisOrder::XYZ: return "XYZ";
    case NpzImportOptions::AxisOrder::XZY: return "XZY";
    case NpzImportOptions::AxisOrder::YXZ: return "YXZ";
    case NpzImportOptions::AxisOrder::YZX: return "YZX";
    case NpzImportOptions::AxisOrder::ZXY: return "ZXY";
    case NpzImportOptions::AxisOrder::ZYX: return "ZYX";
    default: return "auto";
    }
}

namespace
{

// Geometry borrowed from a real medical volume sitting next to the array.
struct ReferenceGeometry
{
    bool valid = false;
    size_t size[3] = {0, 0, 0};
    ImageType::SpacingType spacing;
    ImageType::PointType origin;
    ImageType::DirectionType direction;
    std::string source;
};

// Read a volume's header only; the pixels are never needed here.
bool readReferenceGeometry(const std::string &path, ReferenceGeometry &geom)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return false;
    try
    {
        itk::ImageIOBase::Pointer io =
            itk::ImageIOFactory::CreateImageIO(path.c_str(), itk::ImageIOFactory::ReadMode);
        if (io.IsNull())
            return false;
        io->SetFileName(path);
        io->ReadImageInformation();
        if (io->GetNumberOfDimensions() < 3)
            return false;

        geom.direction.SetIdentity();
        for (unsigned int i = 0; i < 3; ++i)
        {
            geom.size[i] = io->GetDimensions(i);
            const double s = io->GetSpacing(i);
            geom.spacing[i] = (std::isfinite(s) && s > 0.0) ? s : 1.0;
            geom.origin[i] = io->GetOrigin(i);
            const std::vector<double> column = io->GetDirection(i);
            for (unsigned int j = 0; j < 3 && j < column.size(); ++j)
                geom.direction[j][i] = column[j];
        }
        geom.valid = true;
        geom.source = path;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string stripNumpySuffix(const std::string &path)
{
    if (path.size() > 4)
    {
        const std::string tail = path.substr(path.size() - 4);
        if (tail == ".npz" || tail == ".npy" || tail == ".NPZ" || tail == ".NPY")
            return path.substr(0, path.size() - 4);
    }
    return path;
}

// Volumes an array may have been derived from, most specific first. The
// nnUNet layout is the motivating case: <case>.npz next to <case>.nii.gz.
std::vector<std::string> siblingVolumeCandidates(const std::string &stem)
{
    return {stem + ".nii.gz", stem + ".nii", stem + "_0000.nii.gz", stem + "_0000.nii"};
}

bool readJsonNumberArray(const std::string &text, const std::string &key, std::vector<double> &out)
{
    const std::string quoted = "\"" + key + "\"";
    const size_t k = text.find(quoted);
    if (k == std::string::npos)
        return false;
    const size_t open = text.find('[', k);
    const size_t close = (open == std::string::npos) ? std::string::npos : text.find(']', open);
    if (close == std::string::npos)
        return false;
    out.clear();
    std::istringstream fields(text.substr(open + 1, close - open - 1));
    std::string token;
    while (std::getline(fields, token, ','))
    {
        try
        {
            out.push_back(std::stod(token));
        }
        catch (const std::exception &)
        {
            return false;
        }
    }
    return !out.empty();
}

bool readJsonString(const std::string &text, const std::string &key, std::string &out)
{
    const std::string quoted = "\"" + key + "\"";
    const size_t k = text.find(quoted);
    if (k == std::string::npos)
        return false;
    const size_t colon = text.find(':', k + quoted.size());
    const size_t open = (colon == std::string::npos) ? std::string::npos : text.find('"', colon);
    const size_t close = (open == std::string::npos) ? std::string::npos : text.find('"', open + 1);
    if (close == std::string::npos)
        return false;
    out = text.substr(open + 1, close - open - 1);
    return true;
}

// The three spatial axes of the array, in array order. For a 4D array one axis
// holds channels: the outermost or innermost, whichever is shorter, which is
// how both (C, Z, Y, X) and (X, Y, Z, C) softmax volumes are laid out.
bool splitChannelAxis(const std::vector<size_t> &shape, NpzAxisMapping &out, size_t spatialAxes[3])
{
    const size_t nd = shape.size();
    if (nd != 3 && nd != 4)
        return false;

    out.hasChannel = (nd == 4);
    out.channelAxis = 0;
    out.channelCount = 1;
    if (out.hasChannel)
    {
        out.channelAxis = (shape.front() <= shape.back()) ? 0 : nd - 1;
        out.channelCount = shape[out.channelAxis];
    }

    int written = 0;
    for (size_t axis = 0; axis < nd; ++axis)
        if (!out.hasChannel || axis != out.channelAxis)
            spatialAxes[written++] = axis;
    return written == 3;
}

} // namespace

bool npzBuildAxisMapping(const std::vector<size_t> &shape, NpzImportOptions::AxisOrder order,
                         NpzAxisMapping &out)
{
    if (order == NpzImportOptions::AxisOrder::Auto)
        return false; // callers resolve Auto before asking for a layout

    size_t spatialAxes[3] = {0, 1, 2};
    if (!splitChannelAxis(shape, out, spatialAxes))
        return false;

    // The order's letters name the spatial axes in array order.
    const char *name = npzAxisOrderName(order);
    for (int i = 0; i < 3; ++i)
    {
        switch (name[i])
        {
        case 'X': out.axisForX = spatialAxes[i]; break;
        case 'Y': out.axisForY = spatialAxes[i]; break;
        case 'Z': out.axisForZ = spatialAxes[i]; break;
        default: return false;
        }
    }

    out.sizeX = shape[out.axisForX];
    out.sizeY = shape[out.axisForY];
    out.sizeZ = shape[out.axisForZ];
    return out.sizeX > 0 && out.sizeY > 0 && out.sizeZ > 0;
}

namespace
{

// Prefer the order whose resulting size matches the reference volume. Producers
// that pair a .npz with a NIfTI are usually SimpleITK (ZYX) or nibabel (XYZ),
// so those are tried first and the rest only break genuine ties.
NpzImportOptions::AxisOrder chooseAxisOrder(const std::vector<size_t> &shape, const ReferenceGeometry &ref,
                                            bool &ambiguous)
{
    ambiguous = true; // only a unique dimension match settles it
    static const NpzImportOptions::AxisOrder candidates[] = {
        NpzImportOptions::AxisOrder::ZYX, NpzImportOptions::AxisOrder::XYZ,
        NpzImportOptions::AxisOrder::YXZ, NpzImportOptions::AxisOrder::XZY,
        NpzImportOptions::AxisOrder::YZX, NpzImportOptions::AxisOrder::ZXY};

    if (ref.valid)
    {
        // A square-in-plane volume matches several permutations equally well;
        // say so rather than let a coin toss decide the anatomy.
        NpzImportOptions::AxisOrder best = NpzImportOptions::AxisOrder::ZYX;
        int matches = 0;
        for (NpzImportOptions::AxisOrder candidate : candidates)
        {
            NpzAxisMapping layout;
            if (npzBuildAxisMapping(shape, candidate, layout) &&
                layout.sizeX == ref.size[0] && layout.sizeY == ref.size[1] && layout.sizeZ == ref.size[2])
            {
                if (matches == 0)
                    best = candidate;
                ++matches;
            }
        }
        ambiguous = (matches != 1);
        return best;
    }

    // With nothing to compare against the layout is unknowable, so guess from
    // the shape: scans are square in-plane, which makes the odd-length axis the
    // slice axis. Anything else is a coin toss the user has to settle.
    NpzAxisMapping probe;
    size_t spatialAxes[3] = {0, 1, 2};
    if (splitChannelAxis(shape, probe, spatialAxes))
    {
        const size_t a = shape[spatialAxes[0]];
        const size_t b = shape[spatialAxes[1]];
        const size_t c = shape[spatialAxes[2]];
        if (a == b && c != a)
            return NpzImportOptions::AxisOrder::XYZ; // (in, in, slices)
        if (b == c && a != b)
            return NpzImportOptions::AxisOrder::ZYX; // (slices, in, in)
        if (a == c && b != a)
            return NpzImportOptions::AxisOrder::XZY; // (in, slices, in)
    }
    return NpzImportOptions::AxisOrder::ZYX;
}

// Keys that conventionally hold a volume, most likely first.
const npz::ArrayInfo *chooseArray(const std::vector<npz::ArrayInfo> &arrays, const std::string &wanted)
{
    auto usable = [](const npz::ArrayInfo &a)
    { return (a.shape.size() == 3 || a.shape.size() == 4) && a.elementCount() > 0; };

    if (!wanted.empty())
    {
        for (const npz::ArrayInfo &a : arrays)
            if (a.name == wanted)
                return &a;
        return nullptr;
    }

    static const char *const preferred[] = {"probabilities", "softmax", "data", "arr_0",
                                            "image", "volume", "ct", "seg", "label"};
    for (const char *name : preferred)
        for (const npz::ArrayInfo &a : arrays)
            if (a.name == name && usable(a))
                return &a;
    for (const npz::ArrayInfo &a : arrays)
        if (a.shape.size() == 3 && usable(a))
            return &a;
    for (const npz::ArrayInfo &a : arrays)
        if (usable(a))
            return &a;
    return nullptr;
}

itk::ImageIOBase::IOComponentType componentForDType(npz::DType t)
{
    switch (t)
    {
    case npz::DType::Bool:
    case npz::DType::UInt8: return itk::ImageIOBase::UCHAR;
    case npz::DType::Int8: return itk::ImageIOBase::CHAR;
    case npz::DType::UInt16: return itk::ImageIOBase::USHORT;
    case npz::DType::Int16: return itk::ImageIOBase::SHORT;
    case npz::DType::UInt32: return itk::ImageIOBase::UINT;
    case npz::DType::Int32: return itk::ImageIOBase::INT;
    case npz::DType::UInt64: return itk::ImageIOBase::ULONG;
    case npz::DType::Int64: return itk::ImageIOBase::LONG;
    case npz::DType::Float64: return itk::ImageIOBase::DOUBLE;
    default: return itk::ImageIOBase::FLOAT;
    }
}

} // namespace

bool NiftiImage::isNumpyPath(const std::string &path)
{
    return npz::isNpzPath(path) || npz::isNpyPath(path);
}

bool NiftiImage::inspectNumpy(const std::string &path, std::vector<npz::ArrayInfo> &arrays, std::string *error)
{
    return npz::inspect(path, arrays, error);
}

namespace
{

// Everything the importer decides before reading a single voxel: which array,
// which channel, how the numpy axes map onto image axes, and where the geometry
// comes from. Shared by previewNumpy() and loadNumpy() so what the dialog offers
// cannot drift from what the import actually does.
struct ResolvedImport
{
    npz::ArrayInfo info;
    NpzAxisMapping layout;
    NpzImportOptions::AxisOrder order = NpzImportOptions::AxisOrder::ZYX;
    bool axisOrderAmbiguous = false;
    int channel = 0;
    bool flip[3] = {false, false, false};
    std::vector<size_t> stride; // C-order strides of the numpy array
    size_t channelBase = 0;
    ImageType::SpacingType spacing;
    ImageType::PointType origin;
    ImageType::DirectionType direction;
    std::string geometrySource;
    bool geometryResolved = false;
};

bool resolveImport(const std::string &path, const NpzImportOptions &options,
                   ResolvedImport &resolved, std::string *error)
{
    auto fail = [&](const std::string &message) -> bool
    {
        if (error)
            *error = message;
        std::cerr << "NiftiImage: numpy import: " << message << " ('" << path << "')\n";
        return false;
    };

    std::vector<npz::ArrayInfo> arrays;
    std::string npzError;
    if (!npz::inspect(path, arrays, &npzError))
        return fail(npzError);

    const npz::ArrayInfo *selected = chooseArray(arrays, options.arrayName);
    if (!selected)
    {
        if (!options.arrayName.empty())
            return fail("array '" + options.arrayName + "' is not in the archive");
        return fail("no 3D or 4D array found; a volume needs 3 spatial axes (plus an optional channel axis)");
    }
    const npz::ArrayInfo info = *selected;

    // Geometry, in order of authority: explicit spacing, an explicit reference
    // volume, a sibling volume, a sidecar JSON, and finally an isotropic guess.
    ReferenceGeometry ref;
    const std::string stem = stripNumpySuffix(path);
    if (!options.referencePath.empty() && !readReferenceGeometry(options.referencePath, ref))
        std::cerr << "NiftiImage::loadNumpy: could not read geometry from reference '"
                  << options.referencePath << "'\n";
    if (!ref.valid)
    {
        for (const std::string &candidate : siblingVolumeCandidates(stem))
            if (readReferenceGeometry(candidate, ref))
                break;
    }

    NpzImportOptions::AxisOrder order = options.axisOrder;
    // A sidecar may pin the convention explicitly; that beats shape matching.
    std::vector<double> sidecarSpacing;
    const std::string sidecarPath = stem + ".json";
    {
        std::ifstream sidecar(sidecarPath);
        if (sidecar)
        {
            const std::string text((std::istreambuf_iterator<char>(sidecar)), std::istreambuf_iterator<char>());
            std::string declaredOrder;
            if (order == NpzImportOptions::AxisOrder::Auto && readJsonString(text, "axis_order", declaredOrder))
            {
                std::transform(declaredOrder.begin(), declaredOrder.end(), declaredOrder.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (declaredOrder == "zyx")
                    order = NpzImportOptions::AxisOrder::ZYX;
                else if (declaredOrder == "xyz")
                    order = NpzImportOptions::AxisOrder::XYZ;
            }
            std::vector<double> values;
            if (readJsonNumberArray(text, "spacing", values) && values.size() >= 3)
                sidecarSpacing = values;
        }
    }
    bool axisOrderAmbiguous = false;
    if (order == NpzImportOptions::AxisOrder::Auto)
        order = chooseAxisOrder(info.shape, ref, axisOrderAmbiguous);

    NpzAxisMapping layout;
    if (!npzBuildAxisMapping(info.shape, order, layout))
        return fail("array '" + info.name + "' has shape " + info.shapeString() +
                    "; a volume needs 3 spatial axes (plus an optional channel axis)");

    int channel = options.channel;
    if (channel < 0)
        channel = 0;
    if (static_cast<size_t>(channel) >= layout.channelCount)
        return fail("channel " + std::to_string(channel) + " is out of range; the array has " +
                    std::to_string(layout.channelCount) + " channel(s)");

    // Reject a reference whose size disagrees: copying its spacing onto a
    // differently sized array would place every voxel at the wrong millimetre.
    if (ref.valid && (ref.size[0] != layout.sizeX || ref.size[1] != layout.sizeY || ref.size[2] != layout.sizeZ))
    {
        std::cerr << "NiftiImage::loadNumpy: ignoring reference '" << ref.source << "' with size ("
                  << ref.size[0] << "," << ref.size[1] << "," << ref.size[2] << "); array is ("
                  << layout.sizeX << "," << layout.sizeY << "," << layout.sizeZ << ")\n";
        ref.valid = false;
    }

    // Strides of the numpy array, which readAsFloat/readAllAsFloat give in C order.
    const size_t nd = info.shape.size();
    std::vector<size_t> stride(nd, 1);
    for (size_t a = nd - 1; a-- > 0;)
        stride[a] = stride[a + 1] * info.shape[a + 1];

    ImageType::SpacingType spacing;
    ImageType::PointType origin;
    ImageType::DirectionType direction;
    direction.SetIdentity();
    origin.Fill(0.0);
    spacing.Fill(1.0);

    std::string geometrySource;
    bool geometryResolved = false;
    const bool explicitSpacing = options.spacing[0] > 0.0 && options.spacing[1] > 0.0 && options.spacing[2] > 0.0;
    if (explicitSpacing)
    {
        for (unsigned int i = 0; i < 3; ++i)
            spacing[i] = options.spacing[i];
        if (ref.valid)
        {
            origin = ref.origin;
            direction = ref.direction;
        }
        geometrySource = "caller-supplied spacing";
        geometryResolved = true;
    }
    else if (ref.valid)
    {
        spacing = ref.spacing;
        origin = ref.origin;
        direction = ref.direction;
        geometrySource = std::filesystem::path(ref.source).filename().string();
        geometryResolved = true;
    }
    else if (sidecarSpacing.size() >= 3)
    {
        for (unsigned int i = 0; i < 3; ++i)
            spacing[i] = (sidecarSpacing[i] > 0.0) ? sidecarSpacing[i] : 1.0;
        geometrySource = std::filesystem::path(sidecarPath).filename().string();
        geometryResolved = true;
    }
    else
    {
        geometrySource = "1 mm isotropic fallback";
        geometryResolved = false;
    }

    resolved.info = info;
    resolved.layout = layout;
    resolved.order = order;
    resolved.axisOrderAmbiguous = axisOrderAmbiguous;
    resolved.channel = channel;
    for (int i = 0; i < 3; ++i)
        resolved.flip[i] = options.flip[i];
    resolved.stride = std::move(stride);
    resolved.channelBase =
        layout.hasChannel ? static_cast<size_t>(channel) * resolved.stride[layout.channelAxis] : 0;
    resolved.spacing = spacing;
    resolved.origin = origin;
    resolved.direction = direction;
    resolved.geometrySource = geometrySource;
    resolved.geometryResolved = geometryResolved;
    return true;
}

void fillReport(const ResolvedImport &resolved, NpzImportReport &report)
{
    report.arrayName = resolved.info.name;
    report.channel = resolved.channel;
    report.channelCount = static_cast<int>(resolved.layout.channelCount);
    report.size[0] = static_cast<unsigned int>(resolved.layout.sizeX);
    report.size[1] = static_cast<unsigned int>(resolved.layout.sizeY);
    report.size[2] = static_cast<unsigned int>(resolved.layout.sizeZ);
    for (unsigned int i = 0; i < 3; ++i)
        report.spacing[i] = resolved.spacing[i];
    report.axisOrder = resolved.order;
    report.axisOrderAmbiguous = resolved.axisOrderAmbiguous;
    for (int i = 0; i < 3; ++i)
        report.flip[i] = resolved.flip[i];
    report.geometrySource = resolved.geometrySource;
    report.geometryResolved = resolved.geometryResolved;
}

} // namespace

bool NiftiImage::previewNumpy(const std::string &path, const NpzImportOptions &options,
                              NpzImportReport &report, std::string *error)
{
    ResolvedImport resolved;
    if (!resolveImport(path, options, resolved, error))
        return false;
    fillReport(resolved, report);
    return true;
}

bool NiftiImage::loadNumpy(const std::string &path, const NpzImportOptions &options,
                           NpzImportReport *report, std::string *error)
{
    auto fail = [&](const std::string &message) -> bool
    {
        if (error)
            *error = message;
        std::cerr << "NiftiImage::loadNumpy: " << message << " ('" << path << "')\n";
        return false;
    };

    ResolvedImport resolved;
    std::string resolveError;
    if (!resolveImport(path, options, resolved, &resolveError))
        return fail(resolveError);

    const npz::ArrayInfo &info = resolved.info;
    const NpzAxisMapping &layout = resolved.layout;
    const std::vector<size_t> &stride = resolved.stride;
    const size_t channelBase = resolved.channelBase;
    const size_t voxelCount = layout.sizeX * layout.sizeY * layout.sizeZ;
    std::string npzError;

    // A channel-first array keeps each channel contiguous, so only the selected
    // one is read. Anything else has to be materialised whole and gathered.
    const bool contiguousChannel = !info.fortranOrder && (!layout.hasChannel || layout.channelAxis == 0);
    std::vector<float> values;
    size_t bufferBase = 0;
    if (contiguousChannel)
    {
        if (!npz::readAsFloat(path, info.name, channelBase, voxelCount, values, &npzError))
            return fail(npzError);
        bufferBase = channelBase;
    }
    else
    {
        npz::ArrayInfo readInfo;
        if (!npz::readAllAsFloat(path, info.name, &readInfo, values, &npzError))
            return fail(npzError);
    }

    ImageType::SizeType size;
    size[0] = layout.sizeX;
    size[1] = layout.sizeY;
    size[2] = layout.sizeZ;
    ImageType::IndexType start;
    start.Fill(0);
    ImageType::RegionType region(start, size);

    ImageType::Pointer image = ImageType::New();
    image->SetRegions(region);
    try
    {
        image->Allocate();
    }
    catch (const std::exception &e)
    {
        return fail(std::string("could not allocate the volume: ") + e.what());
    }

    image->SetSpacing(resolved.spacing);
    image->SetOrigin(resolved.origin);
    image->SetDirection(resolved.direction);

    // Scatter the numpy samples into the ITK buffer, which runs X fastest.
    // A flipped image axis simply walks its source axis backwards.
    float *buffer = image->GetBufferPointer();
    const size_t strideX = stride[layout.axisForX];
    const size_t strideY = stride[layout.axisForY];
    const size_t strideZ = stride[layout.axisForZ];
    auto sourceIndex = [](size_t i, size_t extent, bool flipped)
    { return flipped ? (extent - 1 - i) : i; };

    for (size_t z = 0; z < layout.sizeZ; ++z)
    {
        const size_t sourceZ = channelBase + sourceIndex(z, layout.sizeZ, resolved.flip[2]) * strideZ;
        const size_t targetZ = z * layout.sizeY * layout.sizeX;
        for (size_t y = 0; y < layout.sizeY; ++y)
        {
            const size_t sourceY = sourceZ + sourceIndex(y, layout.sizeY, resolved.flip[1]) * strideY;
            const size_t targetY = targetZ + y * layout.sizeX;
            for (size_t x = 0; x < layout.sizeX; ++x)
                buffer[targetY + x] =
                    values[sourceY + sourceIndex(x, layout.sizeX, resolved.flip[0]) * strideX - bufferBase];
        }
    }

    m_image = image;
    m_region = m_image->GetLargestPossibleRegion();
    m_component = componentForDType(info.dtype);
    m_spacingX = std::abs(static_cast<double>(resolved.spacing[0]));
    m_spacingY = std::abs(static_cast<double>(resolved.spacing[1]));
    m_spacingZ = std::abs(static_cast<double>(resolved.spacing[2]));

    if (report)
        fillReport(resolved, *report);

    std::cerr << "NiftiImage::loadNumpy: array='" << info.name << "' shape=" << info.shapeString()
              << " dtype=" << npz::dtypeName(info.dtype) << " order=" << npzAxisOrderName(resolved.order)
              << " flip=" << (resolved.flip[0] ? "X" : "-") << (resolved.flip[1] ? "Y" : "-")
              << (resolved.flip[2] ? "Z" : "-")
              << " channel=" << resolved.channel << "/" << layout.channelCount
              << " geometry=" << resolved.geometrySource
              << (resolved.geometryResolved ? "" : " (UNVERIFIED)") << "\n";
    if (!resolved.geometryResolved)
        std::cerr << "NiftiImage::loadNumpy: WARNING no spacing found for '" << path
                  << "'; using 1 mm isotropic. Distances, volumes and 3D proportions will be wrong "
                     "unless the real spacing is supplied.\n";

    finalizeLoad(path);
    return true;
}

// Shared post-read processing: compute global min/max, classify mask vs. image,
// and log the result. Used by both the NIfTI and DICOM loading paths.
void NiftiImage::finalizeLoad(const std::string &path)
{
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

    // Only treat genuinely binary-ish volumes (e.g. {0,1}) as display masks.
    // Multi-label volumes opened as the primary image (e.g. {0,1,2,3}) must stay
    // windowable: classifying them as masks would collapse the window range to
    // [0,1] and force binary rendering, breaking the Window/Level controls.
    m_isMask = false;
    if (isInteger)
    {
        const bool binaryRange = (m_max - m_min) <= 1.5f;
        if (binaryRange)
            m_isMask = true;
    }

    // For masks, normalize min/max to [0,1] to avoid windowing artifacts.
    if (m_isMask)
    {
        m_min = 0.0f;
        m_max = 1.0f;
    }

    // Log loaded image properties for debugging
    std::cerr << "NiftiImage::finalizeLoad: '" << path << "' size=(" << m_region.GetSize()[0] << "," << m_region.GetSize()[1] << "," << m_region.GetSize()[2] << ") spacing=(" << m_spacingX << "," << m_spacingY << "," << m_spacingZ << ") min=" << m_min << " max=" << m_max << " comp=" << m_component << " isMask=" << (m_isMask ? "yes" : "no") << " uniq=" << uniques.size() << " sampled=" << sampled << "\n";
}

unsigned int NiftiImage::getSizeX() const { return m_region.GetSize()[0]; }
unsigned int NiftiImage::getSizeY() const { return m_region.GetSize()[1]; }
unsigned int NiftiImage::getSizeZ() const { return m_region.GetSize()[2]; }
double NiftiImage::getSpacingX() const { return m_spacingX; }
double NiftiImage::getSpacingY() const { return m_spacingY; }
double NiftiImage::getSpacingZ() const { return m_spacingZ; }

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
    out.m_spacingX = m_spacingX;
    out.m_spacingY = m_spacingY;
    out.m_spacingZ = m_spacingZ;
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
