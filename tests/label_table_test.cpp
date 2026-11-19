// Pins LabelTable: label names from an nnU-Net dataset.json and from an ITK-SNAP label file,
// what is left out (background, region lists), the ignore label, and the search rule.
#include "LabelTable.h"

#include <QFile>
#include <QTemporaryDir>

#include <cstdio>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-62s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

bool writeText(const QString &path, const QByteArray &text)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(text) == text.size();
}

} // namespace

int main()
{
    QTemporaryDir dir;
    if (!dir.isValid())
        return 1;

    const QString json = dir.filePath("dataset.json");
    writeText(json, R"json({
 "channel_names": {"0": "CBCT"},
 "labels": {
  "background": 0,
  "Mandible": 1,
  "Maxilla": 2,
  "Upper Left First Molar (26)": 18,
  "whole_jaw": [1, 2],
  "Pulp": 37,
  "ignore": 38
 },
 "file_ending": ".nii.gz"
})json");
    LabelTable table;
    QString error;
    check(table.load(json, &error), "an nnU-Net dataset.json loads");
    check(table.entries().size() == 5, "background and region lists are left out");
    check(table.entries().front().value == 1 && table.entries().back().value == 38, "entries ascend by value");
    check(table.nameFor(18) == "Upper Left First Molar (26)" && table.nameFor(5).isEmpty(), "names by value");
    check(table.ignoreValue() == 38, "the ignore label is found");
    check(!table.find(2)->color.isValid(), "dataset.json gives no colours");

    check(LabelTable::matches(18, "Upper Left First Molar (26)", "26"), "a number matches a name containing it");
    check(LabelTable::matches(26, "Lower Left Central Incisor (31)", "26"), "and the value it equals");
    check(LabelTable::matches(2, "Maxilla", "maxi"), "names match case-insensitively");
    check(!LabelTable::matches(1, "Mandible", "maxi"), "and only names containing the query");
    check(LabelTable::matches(1, "Mandible", "  "), "an empty query matches everything");

    const QString snap = dir.filePath("labels.txt");
    writeText(snap, "################################################\n"
                    "# ITK-SnAP Label Description File\n"
                    "################################################\n"
                    "    0     0    0    0        0  0  0    \"Clear Label\"\n"
                    "    1   255    0    0        1  1  1    \"Mandible\"\n"
                    "    2     0  255    0        1  1  1    \"Maxilla\"\n");
    check(table.load(snap, &error), "an ITK-SNAP label file loads");
    check(table.entries().size() == 2 && table.nameFor(2) == "Maxilla", "its names, without the clear label");
    check(table.find(1)->color == QColor(255, 0, 0), "and its colours");
    check(table.ignoreValue() == -1, "it has no ignore label");

    const QString broken = dir.filePath("broken.json");
    writeText(broken, R"({"labels": "none"})");
    check(!table.load(broken, &error) && table.nameFor(2) == "Maxilla", "a failed load leaves the table as it was");
    check(!table.load(dir.filePath("missing.txt"), &error) && !error.isEmpty(), "a missing file says why");

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
