#pragma once

#include <QDateTime>
#include <QString>

#include <array>
#include <cstdint>
#include <vector>

// Crash copies of an edited mask, free of the window. One copy per (image, mask) pair: a
// label volume and a JSON sidecar naming the image and the mask, in directory().
namespace mask_autosave
{

// ROIFT_AUTOSAVE_DIR when set, else <application data>/autosave. Never beside the data.
QString directory();

// The labels at one moment, narrowed so the copy costs a byte a voxel up to label 255.
struct Snapshot
{
    QString imagePath;
    QString maskPath; // empty for a mask painted without a file
    std::array<unsigned, 3> dims{0, 0, 0};
    bool wide = false; // int16 samples; else uint8
    std::vector<std::uint8_t> narrow;
    std::vector<std::int16_t> wideLabels;
};

// Labels outside int16 are clamped, as Save Mask does.
Snapshot takeSnapshot(const QString &imagePath, const QString &maskPath, const std::vector<int> &labels,
                      const std::array<unsigned, 3> &dims);
// Writes the volume, then the sidecar; a copy is complete only once its sidecar exists, so a
// crash during the write leaves the previous copy, or none.
bool write(const Snapshot &snapshot, QString *error);

struct Entry
{
    QString volumePath;
    QString sidecarPath;
    QString imagePath;
    QString maskPath;
    QDateTime savedAt;
    std::array<unsigned, 3> dims{0, 0, 0};
};

// The copies whose image is imagePath, newest first.
std::vector<Entry> findForImage(const QString &imagePath);
bool read(const Entry &entry, std::vector<int> &labels, QString *error);
// Removes the copy of (imagePath, maskPath), if there is one.
void remove(const QString &imagePath, const QString &maskPath);
bool exists(const QString &imagePath, const QString &maskPath);

} // namespace mask_autosave
