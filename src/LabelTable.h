#pragma once

#include <QColor>
#include <QString>

#include <vector>

// Names (and, from ITK-SNAP files, colours) of the label values of a mask, free of the window.
// Read from an nnU-Net dataset.json ("labels": {"name": value}) or an ITK-SNAP label
// description file (IDX R G B A VIS MSH "LABEL").
class LabelTable
{
public:
    struct Entry
    {
        int value = 0;
        QString name;
        QColor color; // invalid when the file gives none (dataset.json)
    };

    // By extension: .json as nnU-Net, anything else as ITK-SNAP. False with *error on failure;
    // the table is then left as it was.
    bool load(const QString &path, QString *error);
    bool loadNnUnetDatasetJson(const QString &path, QString *error);
    bool loadItkSnapLabels(const QString &path, QString *error);
    void clear();

    bool empty() const { return m_entries.empty(); }
    const QString &sourcePath() const { return m_sourcePath; }
    // Ascending by value; background (0) and nnU-Net region entries (lists) are left out.
    const std::vector<Entry> &entries() const { return m_entries; }
    const Entry *find(int value) const;
    QString nameFor(int value) const;
    // The nnU-Net "ignore" label, or -1.
    int ignoreValue() const { return m_ignoreValue; }
    // A query matches a value it equals, or a name containing it (case-insensitive).
    static bool matches(int value, const QString &name, const QString &query);

private:
    std::vector<Entry> m_entries;
    QString m_sourcePath;
    int m_ignoreValue = -1;
};
