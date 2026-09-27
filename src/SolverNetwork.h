#pragma once

/**
 * SolverNetwork.h — a 1D haemodynamic network (openBF-style solver YAML) and
 * where its segments lie in the image.
 *
 * The YAML carries topology and sizes only: segments `sn -> tn` with length,
 * radius and wall constants, and a Windkessel on each outlet. Placing them on
 * the image takes the segment map written beside it by
 * `vessels.cli.analyze_vessels --solver-yaml`: a label volume whose value k is
 * the k-th segment of the YAML, 1-based. No Qt widgets here, so it is testable.
 */

#include <QString>

#include <cstddef>
#include <utility>
#include <vector>

struct MaskVolume;

struct SolverSegment
{
    int index = 0; ///< 1-based position in the YAML: the value in the segment map
    QString label;
    int sn = 0;
    int tn = 0;
    double lengthM = 0.0;
    double radiusM = 0.0;
    double youngPa = 0.0;
    bool outlet = false; ///< carries an outlet boundary condition (R1)
    /// Every key of the entry as written, in file order, for display.
    std::vector<std::pair<QString, QString>> fields;
};

struct SolverNetwork
{
    QString path;
    /// Top-level and nested scalars as `key` or `block.key`, in file order.
    std::vector<std::pair<QString, QString>> header;
    std::vector<SolverSegment> segments;

    bool empty() const { return segments.empty(); }
    QString headerValue(const QString &key) const;
    /// 0-based position of the segment ending where @p i starts; -1 for the inlet.
    int parentOf(int i) const;
    /// 0-based positions of the segments starting where @p i ends.
    std::vector<int> daughtersOf(int i) const;
    /// Multi-line description of segment @p i (0-based), in mm and Pa.
    QString describe(int i) const;
};

/// Parse the YAML subset these files use: scalars, one level of nested blocks,
/// and the `network:` list of flat maps. False with @p error when a segment
/// lacks `label`, `sn`, `tn`, `L` or `R0`, or when there is no network.
bool parseSolverNetwork(const QString &text, SolverNetwork &out, QString *error = nullptr);
bool readSolverNetwork(const QString &path, SolverNetwork &out, QString *error = nullptr);

/// `<stem>_segments.nii.gz` beside `<stem>.yaml`, the name analyze_vessels writes.
QString solverSegmentMapPath(const QString &yamlPath);

/// Where to show a segment: the segment voxel nearest its centroid, so the
/// point is on the vessel even when the segment curves.
struct SegmentAnchor
{
    bool valid = false;
    int x = 0;
    int y = 0;
    int z = 0;
    std::size_t voxels = 0;
};

/// Voxel -> segment lookup that stores only the vessel's voxels.
class SegmentVoxelIndex
{
public:
    void clear();
    /// Index @p volume's non-zero voxels; anchors for labels 1..@p segmentCount.
    void build(const MaskVolume &volume, int segmentCount);
    /// 1-based segment at the voxel, or 0.
    int segmentAt(int x, int y, int z) const;
    /// Nearest segment within @p radius voxels (Chebyshev), or 0.
    int segmentNear(int x, int y, int z, int radius) const;
    const std::vector<SegmentAnchor> &anchors() const { return m_anchors; }
    bool empty() const { return m_entries.empty(); }
    unsigned int dimX() const { return m_dimX; }
    unsigned int dimY() const { return m_dimY; }
    unsigned int dimZ() const { return m_dimZ; }

private:
    std::vector<std::pair<std::size_t, int>> m_entries; ///< linear index, label; sorted
    std::vector<SegmentAnchor> m_anchors;               ///< [k-1] for segment k
    unsigned int m_dimX = 0;
    unsigned int m_dimY = 0;
    unsigned int m_dimZ = 0;
};
