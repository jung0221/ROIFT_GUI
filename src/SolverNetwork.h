#pragma once

/**
 * SolverNetwork.h — a 1D haemodynamic network (openBF-style solver YAML) and
 * where its segments lie in the image.
 *
 * The YAML carries topology and sizes only: segments `sn -> tn` with length,
 * radius and wall constants, and a Windkessel on each outlet. Placing them on
 * the image takes what `vessels.cli.analyze_vessels --solver-yaml` writes beside
 * it: the geometry JSON (node positions, one ordered centreline per segment) and
 * two label volumes whose value k is the k-th segment of the YAML, 1-based. The
 * segment map covers the whole vessel (a cut subtree goes to the segment it
 * hangs from); the lumen map only what the network models. No Qt widgets here.
 */

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <map>
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
    QByteArray rawText; ///< the file as read, for the geometry's hash check
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
    /// 0-based positions of the segments ending at / starting from node @p id.
    std::vector<int> segmentsInto(int id) const;
    std::vector<int> segmentsOutOf(int id) const;
    /// Every node id the segments name, ascending.
    std::vector<int> nodeIds() const;
    /// SHA-256 of the text with CRLF read as LF, hex: what the exporter hashed.
    QString textSha256() const;
};

/// Where the network lies, read from `<stem>_geometry.json`. Coordinates are
/// voxel indices (x, y, z) of the maps' grid, possibly fractional.
struct SolverGeometry
{
    struct Point
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };
    struct Segment
    {
        int index = 0;
        QString label;
        int sn = 0;
        int tn = 0;
        long long lumenVoxels = 0;
        long long territoryVoxels = 0;
        std::vector<Point> centerline; ///< from node sn to node tn, in order
    };

    bool valid = false;
    QString yamlSha256;
    QMap<QString, QString> files; ///< yaml, segment_map, lumen_map, geometry: names beside it
    unsigned int shape[3] = {0, 0, 0};
    double spacing[3] = {1.0, 1.0, 1.0};
    std::map<int, Point> nodes;
    std::vector<Segment> segments; ///< [k-1] for segment k

    const Point *node(int id) const;
};

/// Read the geometry JSON. False with @p error when it is not one, or is of a
/// newer version than this reader.
bool readSolverGeometry(const QString &path, SolverGeometry &out, QString *error = nullptr);
bool parseSolverGeometry(const QByteArray &json, SolverGeometry &out, QString *error = nullptr);

/// Every reason @p geometry does not describe @p network on an image of
/// @p imageDims (pass zeros to skip the grid check); empty when it does.
QStringList solverGeometryProblems(const SolverNetwork &network, const SolverGeometry &geometry,
                                   const unsigned int imageDims[3]);

/// What kind of node @p id is: no segment in = inlet, none out = outlet.
enum class SolverNodeKind
{
    Inlet,
    Junction,
    Outlet,
    Unknown,
};
SolverNodeKind solverNodeKind(const SolverNetwork &network, int id);
QString solverNodeKindName(SolverNodeKind kind);

/// Parse the YAML subset these files use: scalars, one level of nested blocks,
/// and the `network:` list of flat maps. False with @p error when a segment
/// lacks `label`, `sn`, `tn`, `L` or `R0`, or when there is no network.
bool parseSolverNetwork(const QString &text, SolverNetwork &out, QString *error = nullptr);
bool readSolverNetwork(const QString &path, SolverNetwork &out, QString *error = nullptr);

/// The files analyze_vessels writes beside `<stem>.yaml`: `<stem>` + @p suffix.
QString solverSiblingPath(const QString &yamlPath, const QString &suffix);
/// `<stem>_segments.nii.gz`, the whole-vessel map.
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
