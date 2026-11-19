#include "LabelTable.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>

namespace
{

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

} // namespace

bool LabelTable::load(const QString &path, QString *error)
{
    if (QFileInfo(path).suffix().compare("json", Qt::CaseInsensitive) == 0)
        return loadNnUnetDatasetJson(path, error);
    return loadItkSnapLabels(path, error);
}

bool LabelTable::loadNnUnetDatasetJson(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QString("Could not open %1").arg(path));
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (doc.isNull())
        return fail(error, QString("%1 is not JSON: %2").arg(path, parseError.errorString()));
    const QJsonValue labels = doc.object().value("labels");
    if (!labels.isObject())
        return fail(error, QString("%1 has no \"labels\" object").arg(path));

    std::vector<Entry> entries;
    int ignoreValue = -1;
    const QJsonObject object = labels.toObject();
    for (auto it = object.begin(); it != object.end(); ++it)
    {
        // A list is a region of several labels (region-based training), not a voxel value.
        if (!it.value().isDouble())
            continue;
        const int value = it.value().toInt();
        if (it.key().compare("ignore", Qt::CaseInsensitive) == 0)
            ignoreValue = value;
        if (value <= 0)
            continue;
        entries.push_back({value, it.key(), QColor()});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b)
              { return a.value < b.value; });
    if (entries.empty())
        return fail(error, QString("%1 names no label above 0").arg(path));
    m_entries = std::move(entries);
    m_ignoreValue = ignoreValue;
    m_sourcePath = path;
    return true;
}

bool LabelTable::loadItkSnapLabels(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return fail(error, QString("Could not open %1").arg(path));
    // IDX  -R-  -G-  -B-  -A--  VIS MSH  LABEL
    static const QRegularExpression line(
        R"re(^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+([0-9.]+)\s+\d+\s+\d+\s+"(.*)"\s*$)re");
    std::vector<Entry> entries;
    QTextStream in(&file);
    while (!in.atEnd())
    {
        const QString text = in.readLine();
        if (text.trimmed().isEmpty() || text.trimmed().startsWith('#'))
            continue;
        const QRegularExpressionMatch m = line.match(text);
        if (!m.hasMatch())
            return fail(error, QString("%1: not an ITK-SNAP label line: %2").arg(path, text.trimmed()));
        const int value = m.captured(1).toInt();
        if (value <= 0)
            continue;
        entries.push_back({value, m.captured(6),
                           QColor(m.captured(2).toInt(), m.captured(3).toInt(), m.captured(4).toInt())});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b)
              { return a.value < b.value; });
    if (entries.empty())
        return fail(error, QString("%1 names no label above 0").arg(path));
    m_entries = std::move(entries);
    m_ignoreValue = -1;
    m_sourcePath = path;
    return true;
}

void LabelTable::clear()
{
    m_entries.clear();
    m_sourcePath.clear();
    m_ignoreValue = -1;
}

const LabelTable::Entry *LabelTable::find(int value) const
{
    auto it = std::lower_bound(m_entries.begin(), m_entries.end(), value, [](const Entry &e, int v)
                               { return e.value < v; });
    return (it != m_entries.end() && it->value == value) ? &*it : nullptr;
}

QString LabelTable::nameFor(int value) const
{
    const Entry *entry = find(value);
    return entry ? entry->name : QString();
}

bool LabelTable::matches(int value, const QString &name, const QString &query)
{
    const QString q = query.trimmed();
    if (q.isEmpty())
        return true;
    bool isNumber = false;
    const int number = q.toInt(&isNumber);
    if (isNumber && number == value)
        return true;
    return name.contains(q, Qt::CaseInsensitive);
}
