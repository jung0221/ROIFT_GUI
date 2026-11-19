#include "MaskAutosave.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <algorithm>
#include <limits>

#include <itkImage.h>
#include <itkImageFileReader.h>
#include <itkImageFileWriter.h>

namespace mask_autosave
{
namespace
{

QString cleanPath(const QString &path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString keyFor(const QString &imagePath, const QString &maskPath)
{
    const QByteArray id = (cleanPath(imagePath) + QLatin1Char('\n') + cleanPath(maskPath)).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(id, QCryptographicHash::Sha1).toHex().left(20));
}

QString volumeFor(const QString &key)
{
    return QDir(directory()).filePath(key + ".nii.gz");
}

QString sidecarFor(const QString &key)
{
    return QDir(directory()).filePath(key + ".json");
}

template <typename Pixel>
bool writeVolume(const std::vector<Pixel> &labels, const std::array<unsigned, 3> &dims, const QString &path,
                 QString *error)
{
    using ImageType = itk::Image<Pixel, 3>;
    try
    {
        typename ImageType::Pointer image = ImageType::New();
        typename ImageType::SizeType size;
        for (unsigned i = 0; i < 3; ++i)
            size[i] = dims[i];
        typename ImageType::IndexType start;
        start.Fill(0);
        image->SetRegions(typename ImageType::RegionType(start, size));
        image->Allocate();
        std::copy(labels.begin(), labels.end(), image->GetBufferPointer());
        auto writer = itk::ImageFileWriter<ImageType>::New();
        writer->SetFileName(path.toStdString());
        writer->SetInput(image);
        writer->UseCompressionOn();
        writer->Update();
    }
    catch (const std::exception &e)
    {
        if (error)
            *error = QString("Could not write %1: %2").arg(path, e.what());
        return false;
    }
    return true;
}

} // namespace

QString directory()
{
    const QByteArray overridden = qgetenv("ROIFT_AUTOSAVE_DIR");
    if (!overridden.isEmpty())
        return QString::fromLocal8Bit(overridden);
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("autosave");
}

Snapshot takeSnapshot(const QString &imagePath, const QString &maskPath, const std::vector<int> &labels,
                      const std::array<unsigned, 3> &dims)
{
    Snapshot snapshot;
    snapshot.imagePath = cleanPath(imagePath);
    snapshot.maskPath = cleanPath(maskPath);
    snapshot.dims = dims;
    const auto [lo, hi] = std::minmax_element(labels.begin(), labels.end());
    snapshot.wide = !labels.empty() && (*lo < 0 || *hi > 255);
    if (snapshot.wide)
    {
        snapshot.wideLabels.resize(labels.size());
        constexpr int kMin = std::numeric_limits<std::int16_t>::min();
        constexpr int kMax = std::numeric_limits<std::int16_t>::max();
        for (std::size_t i = 0; i < labels.size(); ++i)
            snapshot.wideLabels[i] = static_cast<std::int16_t>(std::clamp(labels[i], kMin, kMax));
    }
    else
    {
        snapshot.narrow.resize(labels.size());
        for (std::size_t i = 0; i < labels.size(); ++i)
            snapshot.narrow[i] = static_cast<std::uint8_t>(labels[i]);
    }
    return snapshot;
}

bool write(const Snapshot &snapshot, QString *error)
{
    if (!QDir().mkpath(directory()))
    {
        if (error)
            *error = QString("Could not create %1").arg(directory());
        return false;
    }
    const QString key = keyFor(snapshot.imagePath, snapshot.maskPath);
    const QString volume = volumeFor(key);
    const QString sidecar = sidecarFor(key);
    const QString partial = QDir(directory()).filePath(key + ".partial.nii.gz");

    const bool wrote = snapshot.wide ? writeVolume(snapshot.wideLabels, snapshot.dims, partial, error)
                                     : writeVolume(snapshot.narrow, snapshot.dims, partial, error);
    if (!wrote)
    {
        QFile::remove(partial);
        return false;
    }
    // The old sidecar goes first, so no copy is listed while its volume is being replaced.
    QFile::remove(sidecar);
    QFile::remove(volume);
    if (!QFile::rename(partial, volume))
    {
        if (error)
            *error = QString("Could not move %1 into place").arg(partial);
        return false;
    }
    QJsonObject json;
    json["image"] = snapshot.imagePath;
    json["mask"] = snapshot.maskPath;
    json["saved"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    json["dims"] = QJsonArray{int(snapshot.dims[0]), int(snapshot.dims[1]), int(snapshot.dims[2])};
    QFile file(sidecar);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        file.write(QJsonDocument(json).toJson()) < 0)
    {
        if (error)
            *error = QString("Could not write %1").arg(sidecar);
        return false;
    }
    return true;
}

std::vector<Entry> findForImage(const QString &imagePath)
{
    std::vector<Entry> entries;
    const QString wanted = cleanPath(imagePath);
    const QDir dir(directory());
    for (const QFileInfo &info : dir.entryInfoList({"*.json"}, QDir::Files))
    {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        if (json["image"].toString() != wanted)
            continue;
        Entry entry;
        entry.sidecarPath = info.absoluteFilePath();
        entry.volumePath = dir.filePath(info.completeBaseName() + ".nii.gz");
        entry.imagePath = wanted;
        entry.maskPath = json["mask"].toString();
        entry.savedAt = QDateTime::fromString(json["saved"].toString(), Qt::ISODate);
        const QJsonArray dims = json["dims"].toArray();
        for (int i = 0; i < 3 && i < dims.size(); ++i)
            entry.dims[size_t(i)] = unsigned(dims[i].toInt());
        if (QFileInfo::exists(entry.volumePath))
            entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b)
              { return a.savedAt > b.savedAt; });
    return entries;
}

bool read(const Entry &entry, std::vector<int> &labels, QString *error)
{
    using ImageType = itk::Image<int, 3>;
    try
    {
        auto reader = itk::ImageFileReader<ImageType>::New();
        reader->SetFileName(entry.volumePath.toStdString());
        reader->Update();
        const ImageType *image = reader->GetOutput();
        const auto size = image->GetLargestPossibleRegion().GetSize();
        if (size[0] != entry.dims[0] || size[1] != entry.dims[1] || size[2] != entry.dims[2])
        {
            if (error)
                *error = QString("%1 is not the size its sidecar records").arg(entry.volumePath);
            return false;
        }
        const std::size_t count = std::size_t(size[0]) * size[1] * size[2];
        labels.assign(image->GetBufferPointer(), image->GetBufferPointer() + count);
    }
    catch (const std::exception &e)
    {
        if (error)
            *error = QString("Could not read %1: %2").arg(entry.volumePath, e.what());
        return false;
    }
    return true;
}

void remove(const QString &imagePath, const QString &maskPath)
{
    const QString key = keyFor(imagePath, maskPath);
    QFile::remove(sidecarFor(key));
    QFile::remove(volumeFor(key));
}

bool exists(const QString &imagePath, const QString &maskPath)
{
    return QFileInfo::exists(sidecarFor(keyFor(imagePath, maskPath)));
}

} // namespace mask_autosave
