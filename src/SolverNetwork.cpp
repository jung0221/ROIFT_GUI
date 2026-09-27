#include "SolverNetwork.h"

#include "MaskLayers.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

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
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (error)
            *error = QString("cannot read %1: %2").arg(path, file.errorString());
        return false;
    }
    QTextStream stream(&file);
    if (!parseSolverNetwork(stream.readAll(), out, error))
        return false;
    out.path = QFileInfo(path).absoluteFilePath();
    return true;
}

QString solverSegmentMapPath(const QString &yamlPath)
{
    const QFileInfo info(yamlPath);
    QString name = info.fileName();
    for (const QString &suffix : {QStringLiteral(".yaml"), QStringLiteral(".yml")})
    {
        if (name.endsWith(suffix, Qt::CaseInsensitive))
        {
            name.chop(suffix.size());
            break;
        }
    }
    return info.absoluteDir().filePath(name + "_segments.nii.gz");
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
