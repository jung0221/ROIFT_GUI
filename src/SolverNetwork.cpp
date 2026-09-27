#include "SolverNetwork.h"

#include "MaskLayers.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
// A comment starts at '#' at the line start or after whitespace; values never hold one.
QString stripComment(const QString &line)
{
    for (int i = 0; i < line.size(); ++i)
    {
        if (line[i] == '#' && (i == 0 || line[i - 1].isSpace()))
            return line.left(i);
    }
    return line;
}

int indentOf(const QString &line)
{
    int n = 0;
    while (n < line.size() && line[n] == ' ')
        ++n;
    return n;
}

bool splitKeyValue(const QString &text, QString &key, QString &value)
{
    const int colon = text.indexOf(':');
    if (colon <= 0)
        return false;
    key = text.left(colon).trimmed();
    value = text.mid(colon + 1).trimmed();
    if (value.size() >= 2 && (value.startsWith('"') || value.startsWith('\'')) && value.endsWith(value[0]))
        value = value.mid(1, value.size() - 2);
    return !key.isEmpty();
}

bool toDouble(const QString &text, double &out)
{
    bool ok = false;
    out = text.toDouble(&ok);
    return ok && std::isfinite(out);
}

QString fieldValue(const SolverSegment &s, const QString &key)
{
    for (const auto &kv : s.fields)
        if (kv.first == key)
            return kv.second;
    return QString();
}

bool finishSegment(SolverSegment &s, int index, QString *error)
{
    s.index = index;
    s.label = fieldValue(s, "label");
    bool okSn = false;
    bool okTn = false;
    s.sn = fieldValue(s, "sn").toInt(&okSn);
    s.tn = fieldValue(s, "tn").toInt(&okTn);
    double youngPa = 0.0;
    const bool okE = toDouble(fieldValue(s, "E"), youngPa);
    s.youngPa = okE ? youngPa : 0.0;
    s.outlet = !fieldValue(s, "R1").isEmpty();
    if (s.label.isEmpty() || !okSn || !okTn || !toDouble(fieldValue(s, "L"), s.lengthM) ||
        !toDouble(fieldValue(s, "R0"), s.radiusM))
    {
        if (error)
            *error = QString("network entry %1 (%2) lacks one of label, sn, tn, L, R0")
                         .arg(index)
                         .arg(s.label.isEmpty() ? QStringLiteral("no label") : s.label);
        return false;
    }
    return true;
}

QString mm(double metres, int decimals = 1)
{
    return QString::number(metres * 1000.0, 'f', decimals) + " mm";
}
} // namespace

QString SolverNetwork::headerValue(const QString &key) const
{
    for (const auto &kv : header)
        if (kv.first == key)
            return kv.second;
    return QString();
}

int SolverNetwork::parentOf(int i) const
{
    if (i < 0 || i >= static_cast<int>(segments.size()))
        return -1;
    for (int j = 0; j < static_cast<int>(segments.size()); ++j)
        if (segments[j].tn == segments[i].sn)
            return j;
    return -1;
}

std::vector<int> SolverNetwork::daughtersOf(int i) const
{
    std::vector<int> out;
    if (i < 0 || i >= static_cast<int>(segments.size()))
        return out;
    for (int j = 0; j < static_cast<int>(segments.size()); ++j)
        if (segments[j].sn == segments[i].tn)
            out.push_back(j);
    return out;
}

QString SolverNetwork::describe(int i) const
{
    if (i < 0 || i >= static_cast<int>(segments.size()))
        return QString();
    const SolverSegment &s = segments[static_cast<size_t>(i)];
    QStringList lines;
    lines << QString("%1  (#%2, node %3 → %4)").arg(s.label).arg(s.index).arg(s.sn).arg(s.tn);
    lines << QString("L %1   R0 %2   Ø %3").arg(mm(s.lengthM), mm(s.radiusM, 2), mm(2.0 * s.radiusM, 2));
    if (s.youngPa > 0.0)
        lines << QString("E %1 kPa").arg(s.youngPa / 1000.0, 0, 'f', 0);
    const int parent = parentOf(i);
    lines << QString("from: %1").arg(parent < 0 ? QStringLiteral("inlet") : segments[static_cast<size_t>(parent)].label);
    QStringList daughters;
    for (int d : daughtersOf(i))
        daughters << segments[static_cast<size_t>(d)].label;
    lines << QString("to: %1").arg(daughters.isEmpty() ? QStringLiteral("outlet") : daughters.join(", "));
    static const QStringList shown = {"label", "sn", "tn", "L", "R0", "E"};
    QStringList rest;
    for (const auto &kv : s.fields)
        if (!shown.contains(kv.first))
            rest << QString("%1: %2").arg(kv.first, kv.second);
    if (!rest.isEmpty())
        lines << rest.join("\n");
    return lines.join("\n");
}

bool parseSolverNetwork(const QString &text, SolverNetwork &out, QString *error)
{
    out.header.clear();
    out.segments.clear();
    QString block;       // enclosing top-level key of an indented scalar
    bool inNetwork = false;
    int itemIndent = -1; // indent of the "- " of network entries
    SolverSegment current;
    bool haveCurrent = false;

    auto flush = [&]() -> bool
    {
        if (!haveCurrent)
            return true;
        const int index = static_cast<int>(out.segments.size()) + 1;
        if (!finishSegment(current, index, error))
            return false;
        out.segments.push_back(current);
        current = SolverSegment{};
        haveCurrent = false;
        return true;
    };

    const QStringList lines = text.split('\n');
    for (int n = 0; n < lines.size(); ++n)
    {
        QString raw = lines[n];
        raw.remove('\r');
        const QString line = stripComment(raw);
        if (line.trimmed().isEmpty())
            continue;
        const int indent = indentOf(line);
        QString body = line.mid(indent);

        if (indent == 0)
        {
            if (!flush())
                return false;
            QString key, value;
            if (!splitKeyValue(body, key, value))
            {
                if (error)
                    *error = QString("line %1: expected 'key: value'").arg(n + 1);
                return false;
            }
            inNetwork = (key == "network");
            block = value.isEmpty() ? key : QString();
            itemIndent = -1;
            if (!value.isEmpty())
                out.header.emplace_back(key, value);
            continue;
        }

        if (inNetwork)
        {
            if (body.startsWith("- ") || body == "-")
            {
                if (!flush())
                    return false;
                haveCurrent = true;
                itemIndent = indent;
                body = body.mid(1).trimmed();
                if (body.isEmpty())
                    continue;
            }
            else if (!haveCurrent || indent <= itemIndent)
            {
                if (error)
                    *error = QString("line %1: network entries must start with '- '").arg(n + 1);
                return false;
            }
            QString key, value;
            if (!splitKeyValue(body, key, value))
            {
                if (error)
                    *error = QString("line %1: expected 'key: value'").arg(n + 1);
                return false;
            }
            current.fields.emplace_back(key, value);
            continue;
        }

        QString key, value;
        if (!block.isEmpty() && splitKeyValue(body, key, value))
            out.header.emplace_back(block + "." + key, value);
    }
    if (!flush())
        return false;
    if (out.segments.empty())
    {
        if (error)
            *error = QStringLiteral("no 'network:' entries");
        return false;
    }
    return true;
}

bool readSolverNetwork(const QString &path, SolverNetwork &out, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = QString("cannot read %1: %2").arg(path, file.errorString());
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (!parseSolverNetwork(QString::fromUtf8(bytes), out, error))
        return false;
    out.rawText = bytes;
    out.path = QFileInfo(path).absoluteFilePath();
    return true;
}

std::vector<int> SolverNetwork::segmentsInto(int id) const
{
    std::vector<int> out;
    for (int j = 0; j < static_cast<int>(segments.size()); ++j)
        if (segments[static_cast<size_t>(j)].tn == id)
            out.push_back(j);
    return out;
}

std::vector<int> SolverNetwork::segmentsOutOf(int id) const
{
    std::vector<int> out;
    for (int j = 0; j < static_cast<int>(segments.size()); ++j)
        if (segments[static_cast<size_t>(j)].sn == id)
            out.push_back(j);
    return out;
}

std::vector<int> SolverNetwork::nodeIds() const
{
    std::vector<int> ids;
    for (const SolverSegment &s : segments)
    {
        ids.push_back(s.sn);
        ids.push_back(s.tn);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

QString SolverNetwork::textSha256() const
{
    QByteArray text = rawText;
    text.replace("\r\n", "\n"); // the exporter hashed the string, not the Windows file
    return QString::fromLatin1(QCryptographicHash::hash(text, QCryptographicHash::Sha256).toHex());
}

const SolverGeometry::Point *SolverGeometry::node(int id) const
{
    const auto it = nodes.find(id);
    return it == nodes.end() ? nullptr : &it->second;
}

namespace
{
constexpr const char *kGeometryFormat = "ctsegmentation.solver_geometry";
constexpr int kGeometryVersion = 1;

bool readPoint(const QJsonValue &value, SolverGeometry::Point &p)
{
    const QJsonArray a = value.toArray();
    if (a.size() != 3 || !a[0].isDouble() || !a[1].isDouble() || !a[2].isDouble())
        return false;
    p.x = a[0].toDouble();
    p.y = a[1].toDouble();
    p.z = a[2].toDouble();
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
} // namespace

bool parseSolverGeometry(const QByteArray &json, SolverGeometry &out, QString *error)
{
    out = SolverGeometry{};
    auto fail = [error](const QString &why)
    {
        if (error)
            *error = why;
        return false;
    };
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (doc.isNull() || !doc.isObject())
        return fail(QString("not JSON: %1").arg(parseError.errorString()));
    const QJsonObject root = doc.object();
    if (root.value("format").toString() != kGeometryFormat)
        return fail(QString("not a solver geometry (format is '%1')").arg(root.value("format").toString()));
    const int version = root.value("version").toInt(-1);
    if (version < 1 || version > kGeometryVersion)
        return fail(QString("geometry version %1; this viewer reads up to %2").arg(version).arg(kGeometryVersion));

    out.yamlSha256 = root.value("yaml_sha256").toString();
    const QJsonObject files = root.value("files").toObject();
    for (auto it = files.begin(); it != files.end(); ++it)
        out.files.insert(it.key(), it.value().toString());

    const QJsonObject grid = root.value("grid").toObject();
    const QJsonArray shape = grid.value("shape").toArray();
    const QJsonArray spacing = grid.value("spacing_mm").toArray();
    if (shape.size() != 3)
        return fail("grid.shape is not three numbers");
    for (int k = 0; k < 3; ++k)
    {
        const int n = shape[k].toInt(-1);
        if (n <= 0)
            return fail("grid.shape has a non-positive size");
        out.shape[k] = static_cast<unsigned int>(n);
        if (spacing.size() == 3 && spacing[k].toDouble() > 0.0)
            out.spacing[k] = spacing[k].toDouble();
    }

    for (const QJsonValue &v : root.value("nodes").toArray())
    {
        const QJsonObject n = v.toObject();
        SolverGeometry::Point p;
        if (!n.value("id").isDouble() || !readPoint(n.value("voxel"), p))
            return fail("a node lacks an id or a voxel position");
        out.nodes[n.value("id").toInt()] = p;
    }
    for (const QJsonValue &v : root.value("segments").toArray())
    {
        const QJsonObject o = v.toObject();
        SolverGeometry::Segment seg;
        seg.index = o.value("index").toInt();
        seg.label = o.value("label").toString();
        seg.sn = o.value("sn").toInt();
        seg.tn = o.value("tn").toInt();
        seg.lumenVoxels = static_cast<long long>(o.value("lumen_voxels").toDouble());
        seg.territoryVoxels = static_cast<long long>(o.value("territory_voxels").toDouble());
        for (const QJsonValue &pv : o.value("centerline").toArray())
        {
            SolverGeometry::Point p;
            if (!readPoint(pv, p))
                return fail(QString("segment %1 has a malformed centreline point").arg(seg.label));
            seg.centerline.push_back(p);
        }
        if (seg.index != static_cast<int>(out.segments.size()) + 1)
            return fail(QString("segment %1 is listed at position %2 with index %3")
                            .arg(seg.label).arg(out.segments.size() + 1).arg(seg.index));
        out.segments.push_back(seg);
    }
    if (out.segments.empty())
        return fail("no segments");
    out.valid = true;
    return true;
}

bool readSolverGeometry(const QString &path, SolverGeometry &out, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = QString("cannot read %1: %2").arg(path, file.errorString());
        return false;
    }
    return parseSolverGeometry(file.readAll(), out, error);
}

QStringList solverGeometryProblems(const SolverNetwork &network, const SolverGeometry &geometry,
                                   const unsigned int imageDims[3])
{
    QStringList problems;
    if (!geometry.valid)
        return {QStringLiteral("no geometry")};
    if (!geometry.yamlSha256.isEmpty() && geometry.yamlSha256 != network.textSha256())
        problems << "it was written for a different YAML text (edited or replaced since the export)";
    if (geometry.segments.size() != network.segments.size())
        problems << QString("it has %1 segments, the YAML %2")
                        .arg(geometry.segments.size())
                        .arg(network.segments.size());
    const size_t n = std::min(geometry.segments.size(), network.segments.size());
    int mismatched = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const SolverGeometry::Segment &g = geometry.segments[i];
        const SolverSegment &y = network.segments[i];
        if (g.label != y.label || g.sn != y.sn || g.tn != y.tn)
        {
            if (mismatched++ == 0)
                problems << QString("segment %1 is %2 (%3 -> %4) there and %5 (%6 -> %7) in the YAML")
                                .arg(i + 1).arg(g.label).arg(g.sn).arg(g.tn)
                                .arg(y.label).arg(y.sn).arg(y.tn);
        }
    }
    if (mismatched > 1)
        problems << QString("%1 segments disagree in all").arg(mismatched);
    for (int id : network.nodeIds())
        if (!geometry.node(id))
        {
            problems << QString("node %1 has no position").arg(id);
            break;
        }
    if (imageDims && imageDims[0] && imageDims[1] && imageDims[2] &&
        (geometry.shape[0] != imageDims[0] || geometry.shape[1] != imageDims[1] ||
         geometry.shape[2] != imageDims[2]))
        problems << QString("its grid is %1x%2x%3, the image %4x%5x%6")
                        .arg(geometry.shape[0]).arg(geometry.shape[1]).arg(geometry.shape[2])
                        .arg(imageDims[0]).arg(imageDims[1]).arg(imageDims[2]);
    return problems;
}

SolverNodeKind solverNodeKind(const SolverNetwork &network, int id)
{
    const bool in = !network.segmentsInto(id).empty();
    const bool out = !network.segmentsOutOf(id).empty();
    if (!in && !out)
        return SolverNodeKind::Unknown;
    if (!in)
        return SolverNodeKind::Inlet;
    return out ? SolverNodeKind::Junction : SolverNodeKind::Outlet;
}

QString solverNodeKindName(SolverNodeKind kind)
{
    switch (kind)
    {
    case SolverNodeKind::Inlet:
        return QStringLiteral("inlet");
    case SolverNodeKind::Junction:
        return QStringLiteral("junction");
    case SolverNodeKind::Outlet:
        return QStringLiteral("outlet");
    case SolverNodeKind::Unknown:
        break;
    }
    return QStringLiteral("not in the network");
}

QString solverSiblingPath(const QString &yamlPath, const QString &suffix)
{
    const QFileInfo info(yamlPath);
    QString name = info.fileName();
    for (const QString &ext : {QStringLiteral(".yaml"), QStringLiteral(".yml")})
    {
        if (name.endsWith(ext, Qt::CaseInsensitive))
        {
            name.chop(ext.size());
            break;
        }
    }
    return info.absoluteDir().filePath(name + suffix);
}

QString solverSegmentMapPath(const QString &yamlPath)
{
    return solverSiblingPath(yamlPath, QStringLiteral("_segments.nii.gz"));
}

void SegmentVoxelIndex::clear()
{
    m_entries.clear();
    m_anchors.clear();
    m_dimX = m_dimY = m_dimZ = 0;
}

void SegmentVoxelIndex::build(const MaskVolume &volume, int segmentCount)
{
    clear();
    // Checked here rather than through MaskVolume::isValid(), which would link ITK into the test.
    const std::size_t expected = std::size_t(volume.dimX) * volume.dimY * volume.dimZ;
    if (expected == 0 || volume.data.size() != expected || segmentCount <= 0)
        return;
    m_dimX = volume.dimX;
    m_dimY = volume.dimY;
    m_dimZ = volume.dimZ;
    const std::size_t plane = std::size_t(m_dimX) * m_dimY;
    std::vector<double> sum(static_cast<size_t>(segmentCount) * 3, 0.0);
    m_anchors.assign(static_cast<size_t>(segmentCount), SegmentAnchor{});
    for (std::size_t i = 0; i < volume.data.size(); ++i)
    {
        const int label = volume.data[i];
        if (label <= 0)
            continue;
        m_entries.emplace_back(i, label);
        if (label > segmentCount)
            continue;
        const std::size_t k = static_cast<size_t>(label - 1);
        sum[3 * k] += static_cast<double>(i % m_dimX);
        sum[3 * k + 1] += static_cast<double>((i / m_dimX) % m_dimY);
        sum[3 * k + 2] += static_cast<double>(i / plane);
        ++m_anchors[k].voxels;
    }
    std::vector<double> best(static_cast<size_t>(segmentCount), std::numeric_limits<double>::max());
    for (const auto &entry : m_entries)
    {
        if (entry.second > segmentCount)
            continue;
        const std::size_t k = static_cast<size_t>(entry.second - 1);
        const double n = static_cast<double>(m_anchors[k].voxels);
        const int x = static_cast<int>(entry.first % m_dimX);
        const int y = static_cast<int>((entry.first / m_dimX) % m_dimY);
        const int z = static_cast<int>(entry.first / plane);
        const double dx = x - sum[3 * k] / n;
        const double dy = y - sum[3 * k + 1] / n;
        const double dz = z - sum[3 * k + 2] / n;
        const double d = dx * dx + dy * dy + dz * dz;
        if (d < best[k])
        {
            best[k] = d;
            m_anchors[k].valid = true;
            m_anchors[k].x = x;
            m_anchors[k].y = y;
            m_anchors[k].z = z;
        }
    }
}

int SegmentVoxelIndex::segmentAt(int x, int y, int z) const
{
    if (x < 0 || y < 0 || z < 0 || x >= static_cast<int>(m_dimX) || y >= static_cast<int>(m_dimY) ||
        z >= static_cast<int>(m_dimZ))
        return 0;
    const std::size_t key = (std::size_t(z) * m_dimY + std::size_t(y)) * m_dimX + std::size_t(x);
    const auto it = std::lower_bound(m_entries.begin(), m_entries.end(), std::make_pair(key, 0));
    return (it != m_entries.end() && it->first == key) ? it->second : 0;
}

int SegmentVoxelIndex::segmentNear(int x, int y, int z, int radius) const
{
    int best = segmentAt(x, y, z);
    if (best != 0 || radius <= 0)
        return best;
    int bestDist = std::numeric_limits<int>::max();
    for (int dz = -radius; dz <= radius; ++dz)
        for (int dy = -radius; dy <= radius; ++dy)
            for (int dx = -radius; dx <= radius; ++dx)
            {
                const int label = segmentAt(x + dx, y + dy, z + dz);
                const int d = dx * dx + dy * dy + dz * dz;
                if (label != 0 && d < bestDist)
                {
                    bestDist = d;
                    best = label;
                }
            }
    return best;
}
