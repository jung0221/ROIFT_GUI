/**
 * UiUtils.cpp — Implementation of shared UI utility functions.
 *
 * Extracted from ManualSeedSelector.cpp anonymous namespace.
 */

#include "UiUtils.h"
#include "RangeSlider.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QUrl>

#if defined(ROIFT_HAS_QT_SVG)
#include <QSvgRenderer>
#endif

#include <algorithm>
#include <cmath>
#include <limits>

namespace UiUtils
{

// ============================================================================
// Icon rendering
// ============================================================================

QIcon makeFallbackButtonIcon(NiftiButtonIcon type, const QSize &size)
{
    if (size.width() <= 0 || size.height() <= 0)
        return QIcon();

    auto renderIcon = [type](int edge) -> QPixmap
    {
        QPixmap pixmap(edge, edge);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);

        const QColor strokeColor(216, 216, 216);
        const qreal strokeWidth = std::max(1.4, edge * 0.095);
        QPen pen(strokeColor, strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);

        const qreal w = static_cast<qreal>(edge);
        const qreal h = static_cast<qreal>(edge);

        switch (type)
        {
        case NiftiButtonIcon::Add:
            painter.drawLine(QPointF(0.22 * w, 0.50 * h), QPointF(0.78 * w, 0.50 * h));
            painter.drawLine(QPointF(0.50 * w, 0.22 * h), QPointF(0.50 * w, 0.78 * h));
            break;
        case NiftiButtonIcon::AddCsv:
        {
            QRectF pageRect(0.15 * w, 0.14 * h, 0.46 * w, 0.72 * h);
            painter.drawRoundedRect(pageRect, 1.1, 1.1);
            painter.drawLine(QPointF(0.46 * w, 0.14 * h), QPointF(0.61 * w, 0.29 * h));
            painter.drawLine(QPointF(0.66 * w, 0.58 * h), QPointF(0.86 * w, 0.58 * h));
            painter.drawLine(QPointF(0.76 * w, 0.48 * h), QPointF(0.76 * w, 0.68 * h));
            break;
        }
        case NiftiButtonIcon::ExportCsv:
        {
            QRectF pageRect(0.14 * w, 0.14 * h, 0.50 * w, 0.72 * h);
            painter.drawRoundedRect(pageRect, 1.1, 1.1);
            painter.drawLine(QPointF(0.46 * w, 0.14 * h), QPointF(0.64 * w, 0.32 * h));
            painter.drawLine(QPointF(0.77 * w, 0.40 * h), QPointF(0.77 * w, 0.72 * h));
            painter.drawLine(QPointF(0.67 * w, 0.62 * h), QPointF(0.77 * w, 0.72 * h));
            painter.drawLine(QPointF(0.87 * w, 0.62 * h), QPointF(0.77 * w, 0.72 * h));
            break;
        }
        case NiftiButtonIcon::Remove:
            painter.drawLine(QPointF(0.22 * w, 0.50 * h), QPointF(0.78 * w, 0.50 * h));
            break;
        case NiftiButtonIcon::RemoveAll:
            painter.drawEllipse(QRectF(0.18 * w, 0.18 * h, 0.64 * w, 0.64 * h));
            painter.drawLine(QPointF(0.34 * w, 0.34 * h), QPointF(0.66 * w, 0.66 * h));
            painter.drawLine(QPointF(0.66 * w, 0.34 * h), QPointF(0.34 * w, 0.66 * h));
            break;
        case NiftiButtonIcon::Load:
        {
            painter.drawRoundedRect(QRectF(0.14 * w, 0.26 * h, 0.72 * w, 0.50 * h), 1.2, 1.2);
            painter.drawLine(QPointF(0.50 * w, 0.18 * h), QPointF(0.50 * w, 0.56 * h));
            painter.drawLine(QPointF(0.38 * w, 0.44 * h), QPointF(0.50 * w, 0.56 * h));
            painter.drawLine(QPointF(0.62 * w, 0.44 * h), QPointF(0.50 * w, 0.56 * h));
            break;
        }
        case NiftiButtonIcon::Refresh:
        {
            painter.drawArc(QRectF(0.18 * w, 0.18 * h, 0.64 * w, 0.64 * h), 35 * 16, 285 * 16);
            painter.drawLine(QPointF(0.77 * w, 0.27 * h), QPointF(0.86 * w, 0.32 * h));
            painter.drawLine(QPointF(0.77 * w, 0.27 * h), QPointF(0.79 * w, 0.37 * h));
            break;
        }
        case NiftiButtonIcon::Ruler:
        {
            painter.drawRoundedRect(QRectF(0.18 * w, 0.22 * h, 0.64 * w, 0.32 * h), 1.2, 1.2);
            painter.drawLine(QPointF(0.27 * w, 0.23 * h), QPointF(0.27 * w, 0.46 * h));
            painter.drawLine(QPointF(0.38 * w, 0.23 * h), QPointF(0.38 * w, 0.40 * h));
            painter.drawLine(QPointF(0.49 * w, 0.23 * h), QPointF(0.49 * w, 0.46 * h));
            painter.drawLine(QPointF(0.60 * w, 0.23 * h), QPointF(0.60 * w, 0.40 * h));
            painter.drawLine(QPointF(0.71 * w, 0.23 * h), QPointF(0.71 * w, 0.46 * h));
            painter.drawLine(QPointF(0.27 * w, 0.63 * h), QPointF(0.74 * w, 0.63 * h));
            painter.drawLine(QPointF(0.27 * w, 0.63 * h), QPointF(0.36 * w, 0.55 * h));
            painter.drawLine(QPointF(0.27 * w, 0.63 * h), QPointF(0.36 * w, 0.71 * h));
            painter.drawLine(QPointF(0.74 * w, 0.63 * h), QPointF(0.65 * w, 0.55 * h));
            painter.drawLine(QPointF(0.74 * w, 0.63 * h), QPointF(0.65 * w, 0.71 * h));
            break;
        }
        }

        return pixmap;
    };

    const int edge = std::max(size.width(), size.height());
    const int medium = std::max(edge + 6, static_cast<int>(std::round(edge * 1.5)));
    const int large = std::max(edge + 12, edge * 2);

    QIcon icon;
    icon.addPixmap(renderIcon(edge));
    icon.addPixmap(renderIcon(medium));
    icon.addPixmap(renderIcon(large));
    return icon;
}

QIcon makeMonochromeIcon(const char *svgData, const QSize &size, const QIcon &fallback)
{
#if defined(ROIFT_HAS_QT_SVG)
    if (!svgData || size.isEmpty())
        return fallback;

    QSvgRenderer renderer{QByteArray(svgData)};
    if (!renderer.isValid())
        return fallback;

    auto renderSvg = [&renderer](int edge) -> QPixmap
    {
        QPixmap pixmap(edge, edge);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        renderer.render(&painter, QRectF(0, 0, edge, edge));
        return pixmap;
    };

    const int edge = std::max(size.width(), size.height());
    const int medium = std::max(edge + 6, static_cast<int>(std::round(edge * 1.5)));
    const int large = std::max(edge + 12, edge * 2);

    QIcon icon;
    icon.addPixmap(renderSvg(edge));
    icon.addPixmap(renderSvg(medium));
    icon.addPixmap(renderSvg(large));
    return icon;
#else
    Q_UNUSED(svgData);
    Q_UNUSED(size);
    return fallback;
#endif
}

// ============================================================================
// SVG data
// ============================================================================

const char *kAddIconSvg = R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none">
  <path d="M12 5.2V18.8M5.2 12H18.8" stroke="#d8d8d8" stroke-width="1.8" stroke-linecap="round"/>
</svg>
)svg";

const char *kAddCsvIconSvg = R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none">
  <path d="M7 3.8H13L17 7.8V20.2H7V3.8Z" stroke="#d8d8d8" stroke-width="1.4" stroke-linejoin="round"/>
  <path d="M13 3.8V7.8H17" stroke="#d8d8d8" stroke-width="1.4" stroke-linejoin="round"/>
  <path d="M12 11.5V16.8M9.3 14.15H14.7" stroke="#d8d8d8" stroke-width="1.6" stroke-linecap="round"/>
</svg>
)svg";

const char *kRemoveIconSvg = R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none">
  <circle cx="12" cy="12" r="8.1" stroke="#d8d8d8" stroke-width="1.4"/>
  <path d="M8.2 12H15.8" stroke="#d8d8d8" stroke-width="1.8" stroke-linecap="round"/>
</svg>
)svg";

const char *kExportCsvIconSvg = R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none">
  <path d="M5.4 3.9H12.4L16.6 8.1V19.8H5.4V3.9Z" stroke="#d8d8d8" stroke-width="1.35" stroke-linejoin="round"/>
  <path d="M12.4 3.9V8.1H16.6" stroke="#d8d8d8" stroke-width="1.35" stroke-linejoin="round"/>
  <path d="M18.2 10.6V16.7" stroke="#d8d8d8" stroke-width="1.6" stroke-linecap="round"/>
  <path d="M16.2 14.9L18.2 16.9L20.2 14.9" stroke="#d8d8d8" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/>
</svg>
)svg";

const char *kRemoveAllIconSvg = R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none">
  <path d="M8.6 8.2H15.4M10 8.2V6.7C10 6.31 10.31 6 10.7 6H13.3C13.69 6 14 6.31 14 6.7V8.2M9 10V16.5C9 16.89 9.31 17.2 9.7 17.2H14.3C14.69 17.2 15 16.89 15 16.5V10"
        stroke="#d8d8d8" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"/>
  <path d="M6.4 10H17.6" stroke="#d8d8d8" stroke-width="1.4" stroke-linecap="round"/>
  <path d="M6.7 18.6H17.3" stroke="#d8d8d8" stroke-width="1.4" stroke-linecap="round"/>
</svg>
)svg";

const char *kLoadIconSvg = R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none">
  <path d="M4.8 17.8V8.8C4.8 8.36 5.16 8 5.6 8H9.2L10.5 9.4H18.4C18.84 9.4 19.2 9.76 19.2 10.2V17.8C19.2 18.24 18.84 18.6 18.4 18.6H5.6C5.16 18.6 4.8 18.24 4.8 17.8Z"
        stroke="#d8d8d8" stroke-width="1.35" stroke-linejoin="round"/>
  <path d="M12 11.1V16.1M10 14.1L12 16.1L14 14.1" stroke="#d8d8d8" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/>
</svg>
)svg";

const char *kRefreshIconSvg = R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none">
  <path d="M18.8 8.8A7 7 0 1 0 19 14.4" stroke="#d8d8d8" stroke-width="1.5" stroke-linecap="round"/>
  <path d="M18.8 5.2V9.6H14.4" stroke="#d8d8d8" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>
</svg>
)svg";

const char *kRulerIconSvg = R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none">
  <path d="M5.2 7.3H18.8V12.5H5.2V7.3Z" stroke="#d8d8d8" stroke-width="1.35" stroke-linejoin="round"/>
  <path d="M7.2 7.5V11.6M9.4 7.5V10.7M11.6 7.5V11.6M13.8 7.5V10.7M16 7.5V11.6" stroke="#d8d8d8" stroke-width="1.25" stroke-linecap="round"/>
  <path d="M6.3 17H17.7" stroke="#d8d8d8" stroke-width="1.45" stroke-linecap="round"/>
  <path d="M6.3 17L8.2 15.2M6.3 17L8.2 18.8M17.7 17L15.8 15.2M17.7 17L15.8 18.8" stroke="#d8d8d8" stroke-width="1.45" stroke-linecap="round" stroke-linejoin="round"/>
</svg>
)svg";

// ============================================================================
// CSV helpers
// ============================================================================

QString normalizeCsvCell(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"'))
        value = value.mid(1, value.size() - 2);
    return value.trimmed();
}

QString csvEscapeCell(const QString &value)
{
    QString escaped = value;
    escaped.replace('"', "\"\"");
    if (escaped.contains(',') || escaped.contains('"') || escaped.contains('\n') || escaped.contains('\r'))
        escaped = "\"" + escaped + "\"";
    return escaped;
}

QStringList parseCsvRow(const QString &line)
{
    QStringList fields;
    QString current;
    bool inQuotes = false;

    for (int i = 0; i < line.size(); ++i)
    {
        const QChar ch = line[i];
        if (ch == '"')
        {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == '"')
            {
                current += '"';
                ++i;
            }
            else
            {
                inQuotes = !inQuotes;
            }
            continue;
        }

        if (ch == ',' && !inQuotes)
        {
            fields.push_back(normalizeCsvCell(current));
            current.clear();
            continue;
        }

        current += ch;
    }

    fields.push_back(normalizeCsvCell(current));
    return fields;
}

bool isNiftiPathCell(const QString &value)
{
    const QString lower = normalizeCsvCell(value).toLower();
    return lower.endsWith(".nii") || lower.endsWith(".nii.gz");
}

bool isNiftiMaskFilenameCandidate(const QString &fileName)
{
    const QString lower = fileName.trimmed().toLower();
    return lower.endsWith(".nii.gz") || lower.endsWith(".nii");
}

bool isSeedFilenameCandidate(const QString &fileName)
{
    return fileName.trimmed().toLower().endsWith(".txt");
}

int chooseNiftiColumn(const QStringList &headers, const std::vector<int> &niftiCounts)
{
    int selected = -1;
    int bestScore = -1;

    for (int i = 0; i < headers.size(); ++i)
    {
        if (i < 0 || i >= static_cast<int>(niftiCounts.size()) || niftiCounts[i] <= 0)
            continue;

        const QString header = headers[i].trimmed().toLower();
        int score = niftiCounts[i] * 10;

        if (header == "path" || header == "filepath" || header == "file_path" || header == "image_path" || header == "nifti_path")
            score += 1000;
        if (header.contains("path"))
            score += 400;
        if (header.contains("nifti"))
            score += 250;
        if (header.contains("image"))
            score += 150;
        if (header.contains("file"))
            score += 100;

        if (score > bestScore)
        {
            bestScore = score;
            selected = i;
        }
    }

    return selected;
}

// ============================================================================
// Path resolution
// ============================================================================

QString findPathByAscending(const QString &startDir, const QString &relativePath, int maxLevels)
{
    QDir dir(startDir);
    if (!dir.exists())
        return {};

    for (int level = 0; level <= maxLevels; ++level)
    {
        const QString candidate = QDir::cleanPath(dir.filePath(relativePath));
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).absoluteFilePath();
        if (!dir.cdUp())
            break;
    }

    return {};
}

QString resolveSuperResolutionScriptPath()
{
    const QString envPath = qEnvironmentVariable("ROIFT_SR_SCRIPT").trimmed();
    if (!envPath.isEmpty())
    {
        const QString absPath = QFileInfo(envPath).absoluteFilePath();
        if (QFileInfo::exists(absPath))
            return absPath;
    }

    const QString relScript = QStringLiteral("src/super_resolution/super_resolve_nifti.py");

    const QString fromCwd = findPathByAscending(QDir::currentPath(), relScript);
    if (!fromCwd.isEmpty())
        return fromCwd;

    const QString fromAppDir = findPathByAscending(QCoreApplication::applicationDirPath(), relScript);
    if (!fromAppDir.isEmpty())
        return fromAppDir;

    return {};
}

QString resolveSuperResolutionModelPath(const QString &scriptPath)
{
    const QString envPath = qEnvironmentVariable("ROIFT_SR_MODEL").trimmed();
    if (!envPath.isEmpty())
    {
        const QString absPath = QFileInfo(envPath).absoluteFilePath();
        if (QFileInfo::exists(absPath))
            return absPath;
    }

    if (!scriptPath.isEmpty())
    {
        const QFileInfo scriptInfo(scriptPath);
        const QString candidate = QDir(scriptInfo.absolutePath()).filePath("weights/RealESRGAN_x2plus.pth");
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).absoluteFilePath();
    }

    const QString relModel = QStringLiteral("src/super_resolution/weights/RealESRGAN_x2plus.pth");
    const QString fromCwd = findPathByAscending(QDir::currentPath(), relModel);
    if (!fromCwd.isEmpty())
        return fromCwd;

    const QString fromAppDir = findPathByAscending(QCoreApplication::applicationDirPath(), relModel);
    if (!fromAppDir.isEmpty())
        return fromAppDir;

    return {};
}

QString resolveMaskPostprocessScriptPath()
{
    const QString envPath = qEnvironmentVariable("ROIFT_MASK_PP_SCRIPT").trimmed();
    if (!envPath.isEmpty())
    {
        const QString absPath = QFileInfo(envPath).absoluteFilePath();
        if (QFileInfo::exists(absPath))
            return absPath;
    }

    const QString relScript = QStringLiteral("src/seed_gen/ribs_segmentation/post_processing/postprocess_mask.py");

    const QString fromCwd = findPathByAscending(QDir::currentPath(), relScript);
    if (!fromCwd.isEmpty())
        return fromCwd;

    const QString fromAppDir = findPathByAscending(QCoreApplication::applicationDirPath(), relScript);
    if (!fromAppDir.isEmpty())
        return fromAppDir;

    return {};
}

bool resolvePythonCommand(QString *program, QStringList *prefixArgs)
{
    if (!program || !prefixArgs)
        return false;

    prefixArgs->clear();

    const QString envPython = qEnvironmentVariable("ROIFT_PYTHON").trimmed();
    if (!envPython.isEmpty())
    {
        const QString envExec = QStandardPaths::findExecutable(envPython);
        if (!envExec.isEmpty())
        {
            *program = envExec;
            return true;
        }
        if (QFileInfo::exists(envPython))
        {
            *program = QFileInfo(envPython).absoluteFilePath();
            return true;
        }
    }

#if defined(Q_OS_WIN)
    const QStringList pythonNames = {"python", "python3"};
#else
    const QStringList pythonNames = {"python3", "python"};
#endif
    for (const QString &name : pythonNames)
    {
        const QString execPath = QStandardPaths::findExecutable(name);
        if (!execPath.isEmpty())
        {
            *program = execPath;
            return true;
        }
    }

#if defined(Q_OS_WIN)
    const QString pyLauncher = QStandardPaths::findExecutable("py");
    if (!pyLauncher.isEmpty())
    {
        *program = pyLauncher;
        prefixArgs->push_back("-3");
        return true;
    }
#endif

    return false;
}

QString resolveProjectScriptPath(const QString &relativePath)
{
    const QString fromCwd = findPathByAscending(QDir::currentPath(), relativePath);
    if (!fromCwd.isEmpty())
        return fromCwd;

    const QString fromAppDir = findPathByAscending(QCoreApplication::applicationDirPath(), relativePath);
    if (!fromAppDir.isEmpty())
        return fromAppDir;

    return {};
}

bool revealPathInFileManager(const QString &path, QString *openedPath, QString *errorMessage)
{
    if (openedPath)
        openedPath->clear();
    if (errorMessage)
        errorMessage->clear();

    const QFileInfo info(path);
    const QString absolutePath = QDir::cleanPath(info.absoluteFilePath());
    if (absolutePath.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("Invalid path.");
        return false;
    }

    const bool isDirectory = info.exists() && info.isDir();
    const QString directoryPath = isDirectory ? absolutePath : QDir::cleanPath(info.absolutePath());
    if (directoryPath.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("Could not determine the containing directory.");
        return false;
    }

#if defined(Q_OS_WIN)
    if (!isDirectory)
    {
        if (QProcess::startDetached(QStringLiteral("explorer.exe"),
                                    {QStringLiteral("/select,"), QDir::toNativeSeparators(absolutePath)}))
        {
            if (openedPath)
                *openedPath = absolutePath;
            return true;
        }
    }

    if (QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QDir::toNativeSeparators(directoryPath)}))
    {
        if (openedPath)
            *openedPath = directoryPath;
        return true;
    }
#elif defined(Q_OS_LINUX)
    if (!isDirectory)
    {
        const QString dbusSend = QStandardPaths::findExecutable(QStringLiteral("dbus-send"));
        if (!dbusSend.isEmpty())
        {
            const QString fileUri = QUrl::fromLocalFile(absolutePath).toString();
            if (QProcess::startDetached(
                    dbusSend,
                    {QStringLiteral("--session"),
                     QStringLiteral("--dest=org.freedesktop.FileManager1"),
                     QStringLiteral("--type=method_call"),
                     QStringLiteral("/org/freedesktop/FileManager1"),
                     QStringLiteral("org.freedesktop.FileManager1.ShowItems"),
                     QStringLiteral("array:string:%1").arg(fileUri),
                     QStringLiteral("string:")}))
            {
                if (openedPath)
                    *openedPath = absolutePath;
                return true;
            }
        }

        const QString nautilus = QStandardPaths::findExecutable(QStringLiteral("nautilus"));
        if (!nautilus.isEmpty() &&
            QProcess::startDetached(nautilus, {QStringLiteral("--select"), absolutePath}))
        {
            if (openedPath)
                *openedPath = absolutePath;
            return true;
        }

        const QString dolphin = QStandardPaths::findExecutable(QStringLiteral("dolphin"));
        if (!dolphin.isEmpty() &&
            QProcess::startDetached(dolphin, {QStringLiteral("--select"), absolutePath}))
        {
            if (openedPath)
                *openedPath = absolutePath;
            return true;
        }
    }

    const QString xdgOpen = QStandardPaths::findExecutable(QStringLiteral("xdg-open"));
    if (!xdgOpen.isEmpty() && QProcess::startDetached(xdgOpen, {directoryPath}))
    {
        if (openedPath)
            *openedPath = directoryPath;
        return true;
    }
#endif

    if (errorMessage)
        *errorMessage = QStringLiteral("Could not launch a file manager for this path.");
    return false;
}

QString stripNiftiSuffix(const QString &fileName)
{
    QString baseName = fileName;
    if (baseName.endsWith(".nii.gz", Qt::CaseInsensitive))
        baseName.chop(7);
    else if (baseName.endsWith(".nii", Qt::CaseInsensitive))
        baseName.chop(4);
    return baseName;
}

// ============================================================================
// Progress / display helpers
// ============================================================================

std::string makeTerminalProgressBar(int done, int total, int width)
{
    const int safeTotal = std::max(1, total);
    const int clampedDone = std::clamp(done, 0, safeTotal);
    const int filled = static_cast<int>(std::round((static_cast<double>(clampedDone) / static_cast<double>(safeTotal)) * width));

    std::string bar;
    bar.reserve(static_cast<size_t>(width + 2));
    bar.push_back('[');
    for (int i = 0; i < width; ++i)
        bar.push_back(i < filled ? '=' : ' ');
    bar.push_back(']');
    return bar;
}

} // namespace UiUtils
