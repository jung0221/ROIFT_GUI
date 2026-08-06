/**
 * ExternalProcessRunner.cpp — External Python script runners.
 *
 * Contains ManualSeedSelector member function implementations for:
 *   - runLunasSeedGeneration()
 *   - runRibsSeedGeneration()
 *   - runSuperResolution()
 *   - runMaskPostProcessing()
 *
 * These were extracted from ManualSeedSelector.cpp to reduce file size.
 * They remain member functions of ManualSeedSelector.
 */

#include "ManualSeedSelector.h"
#include "UiUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QListWidget>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QTemporaryDir>

#include <algorithm>
#include <filesystem>
#include <iostream>

using namespace UiUtils;

// ============================================================================
// LUNAS seed generation
// ============================================================================

void ManualSeedSelector::runLunasSeedGeneration()
{
    if (!hasImage() || m_path.empty())
    {
        QMessageBox::warning(this, "Generate Seeds", "Please load an image before running LUNAS.");
        return;
    }

    const QString inputPath = QString::fromStdString(m_path);
    if (!QFileInfo::exists(inputPath))
    {
        QMessageBox::warning(this, "Generate Seeds", "The current image path does not exist on disk.");
        return;
    }

    const QString scriptPath = resolveProjectScriptPath(QStringLiteral("src/lunas.py"));
    if (scriptPath.isEmpty())
    {
        QMessageBox::critical(this, "Generate Seeds", "Could not locate src/lunas.py.");
        return;
    }

    QString pythonProgram;
    QStringList pythonPrefixArgs;
    if (!resolvePythonCommand(&pythonProgram, &pythonPrefixArgs))
    {
        QMessageBox::critical(this, "Generate Seeds", "Could not find a Python interpreter. Set ROIFT_PYTHON if needed.");
        return;
    }

    const QFileInfo inputInfo(inputPath);
    const QString patientName = stripNiftiSuffix(inputInfo.fileName());
    QDir outputRootDir = inputInfo.absoluteDir();
    if (QString::compare(outputRootDir.dirName(), patientName, Qt::CaseInsensitive) == 0)
    {
        QDir parentDir = outputRootDir;
        if (parentDir.cdUp())
            outputRootDir = parentDir;
    }
    const QString outDir = outputRootDir.absolutePath();

    const QFileInfo scriptInfo(scriptPath);
    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.setWorkingDirectory(scriptInfo.absolutePath());

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString scriptDir = scriptInfo.absolutePath();
    const QString pythonPath = env.value("PYTHONPATH");
    if (pythonPath.isEmpty())
        env.insert("PYTHONPATH", scriptDir);
    else
        env.insert("PYTHONPATH", scriptDir + QDir::listSeparator() + pythonPath);
    proc.setProcessEnvironment(env);

    QStringList args = pythonPrefixArgs;
    args << scriptPath
         << "--patient"
         << inputPath
         << "--output"
         << outDir
         << "--only-seeds";

    QProgressDialog progress("Running LUNAS seed generation...", "Cancel", 0, 0, this);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.show();

    proc.start(pythonProgram, args);
    if (!proc.waitForStarted(10000))
    {
        progress.close();
        QMessageBox::critical(this, "Generate Seeds", QString("Failed to start LUNAS process.\n%1").arg(proc.errorString()));
        return;
    }

    while (!proc.waitForFinished(200))
    {
        QCoreApplication::processEvents();
        if (progress.wasCanceled())
        {
            proc.kill();
            proc.waitForFinished(3000);
            progress.close();
            QMessageBox::warning(this, "Generate Seeds", "LUNAS seed generation was canceled.");
            return;
        }
    }
    progress.close();

    const QString procStdout = proc.readAllStandardOutput();
    const QString procStderr = proc.readAllStandardError();
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        QMessageBox::critical(
            this,
            "Generate Seeds",
            QString("LUNAS failed (exit code %1).\n\nSTDERR:\n%2").arg(proc.exitCode()).arg(procStderr));
        if (!procStdout.isEmpty())
            std::cerr << "LUNAS STDOUT:\n"
                      << procStdout.toStdString() << "\n";
        if (!procStderr.isEmpty())
            std::cerr << "LUNAS STDERR:\n"
                      << procStderr.toStdString() << "\n";
        return;
    }

    if (m_currentImageIndex >= 0 && m_currentImageIndex < static_cast<int>(m_images.size()))
    {
        const QString patientOutDir = QDir(outDir).filePath(patientName);
        const QStringList generatedSeeds = {
            QDir(patientOutDir).filePath(QString("left_lung_%1.txt").arg(patientName)),
            QDir(patientOutDir).filePath(QString("right_lung_%1.txt").arg(patientName)),
            QDir(patientOutDir).filePath(QString("trachea_%1.txt").arg(patientName)),
        };

        for (const QString &seedPath : generatedSeeds)
        {
            if (!QFileInfo::exists(seedPath))
                continue;
            const std::string asStd = seedPath.toStdString();
            auto &seedPaths = m_images[m_currentImageIndex].seedPaths;
            if (std::find(seedPaths.begin(), seedPaths.end(), asStd) == seedPaths.end())
                seedPaths.push_back(asStd);
        }
        updateMaskSeedLists();
    }

    if (m_statusLabel)
        m_statusLabel->setText(QString("LUNAS seeds generated at: %1").arg(outDir));
    QMessageBox::information(this, "Generate Seeds", "LUNAS seed generation finished successfully.");
}

// ============================================================================
// Ribs seed generation
// ============================================================================

void ManualSeedSelector::runRibsSeedGeneration()
{
    if (!hasImage() || m_path.empty())
    {
        QMessageBox::warning(this, "Generate Seeds", "Please load an image before running segment_ribs.");
        return;
    }

    const QString inputPath = QString::fromStdString(m_path);
    if (!QFileInfo::exists(inputPath))
    {
        QMessageBox::warning(this, "Generate Seeds", "The current image path does not exist on disk.");
        return;
    }

    const QString scriptPath = resolveProjectScriptPath(QStringLiteral("src/segment_ribs.py"));
    if (scriptPath.isEmpty())
    {
        QMessageBox::critical(this, "Generate Seeds", "Could not locate src/segment_ribs.py.");
        return;
    }

    QString pythonProgram;
    QStringList pythonPrefixArgs;
    if (!resolvePythonCommand(&pythonProgram, &pythonPrefixArgs))
    {
        QMessageBox::critical(this, "Generate Seeds", "Could not find a Python interpreter. Set ROIFT_PYTHON if needed.");
        return;
    }

    const QFileInfo inputInfo(inputPath);
    const QString patientName = stripNiftiSuffix(inputInfo.fileName());
    const QString segmentedDir = inputInfo.absolutePath();

    const QDir segDir(segmentedDir);
    const QStringList leftCandidates = segDir.entryList(
        QStringList() << QString("left_lung_%1.nii*").arg(patientName) << "left_lung_*.nii*",
        QDir::Files,
        QDir::Name);
    const QStringList rightCandidates = segDir.entryList(
        QStringList() << QString("right_lung_%1.nii*").arg(patientName) << "right_lung_*.nii*",
        QDir::Files,
        QDir::Name);

    if (leftCandidates.isEmpty() || rightCandidates.isEmpty())
    {
        QString missing;
        if (leftCandidates.isEmpty())
            missing += "left_lung_* ";
        if (rightCandidates.isEmpty())
            missing += "right_lung_*";
        QMessageBox::warning(
            this,
            "Generate Seeds",
            QString("Segmented lungs not found in the image folder.\nMissing pattern(s): %1\nFolder:\n%2")
                .arg(missing.trimmed())
                .arg(segmentedDir));
        return;
    }

    const QString outDir = segmentedDir;

    const QFileInfo scriptInfo(scriptPath);
    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.setWorkingDirectory(scriptInfo.absolutePath());

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString scriptDir = scriptInfo.absolutePath();
    const QString pythonPath = env.value("PYTHONPATH");
    if (pythonPath.isEmpty())
        env.insert("PYTHONPATH", scriptDir);
    else
        env.insert("PYTHONPATH", scriptDir + QDir::listSeparator() + pythonPath);
    env.insert("PYTHONUNBUFFERED", "1");
    proc.setProcessEnvironment(env);

    QStringList args = pythonPrefixArgs;
    args << "-u"
         << scriptPath
         << "--patient-path"
         << inputPath
         << "--segmented-lung-path"
         << segmentedDir
         << "--output"
         << outDir
         << "--only-seeds";

    QString procStdout;
    QString procStderr;
    auto flushProcessOutput = [&proc, &procStdout, &procStderr]()
    {
        const QByteArray outChunk = proc.readAllStandardOutput();
        if (!outChunk.isEmpty())
        {
            procStdout += QString::fromLocal8Bit(outChunk);
            std::cout.write(outChunk.constData(), outChunk.size());
            std::cout.flush();
        }

        const QByteArray errChunk = proc.readAllStandardError();
        if (!errChunk.isEmpty())
        {
            procStderr += QString::fromLocal8Bit(errChunk);
            std::cerr.write(errChunk.constData(), errChunk.size());
            std::cerr.flush();
        }
    };

    QProgressDialog progress("Running ribs seed generation...", "Cancel", 0, 0, this);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.show();

    proc.start(pythonProgram, args);
    if (!proc.waitForStarted(10000))
    {
        progress.close();
        QMessageBox::critical(this, "Generate Seeds", QString("Failed to start segment_ribs process.\n%1").arg(proc.errorString()));
        return;
    }

    while (!proc.waitForFinished(200))
    {
        flushProcessOutput();
        QCoreApplication::processEvents();
        if (progress.wasCanceled())
        {
            proc.kill();
            proc.waitForFinished(3000);
            progress.close();
            QMessageBox::warning(this, "Generate Seeds", "Ribs seed generation was canceled.");
            return;
        }
    }
    flushProcessOutput();
    progress.close();
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        QMessageBox::critical(
            this,
            "Generate Seeds",
            QString("segment_ribs failed (exit code %1).\n\nSTDERR:\n%2").arg(proc.exitCode()).arg(procStderr));
        if (!procStdout.isEmpty())
            std::cerr << "segment_ribs STDOUT:\n"
                      << procStdout.toStdString() << "\n";
        if (!procStderr.isEmpty())
            std::cerr << "segment_ribs STDERR:\n"
                      << procStderr.toStdString() << "\n";
        return;
    }

    if (m_currentImageIndex >= 0 && m_currentImageIndex < static_cast<int>(m_images.size()))
    {
        const QStringList generatedSeeds = {
            QDir(outDir).filePath(QString("ribs_%1.txt").arg(patientName)),
            QDir(outDir).filePath(QString("outlier_ribs_%1.txt").arg(patientName)),
        };

        for (const QString &seedPath : generatedSeeds)
        {
            if (!QFileInfo::exists(seedPath))
                continue;
            const std::string asStd = seedPath.toStdString();
            auto &seedPaths = m_images[m_currentImageIndex].seedPaths;
            if (std::find(seedPaths.begin(), seedPaths.end(), asStd) == seedPaths.end())
                seedPaths.push_back(asStd);
        }
        updateMaskSeedLists();
    }

    if (m_statusLabel)
        m_statusLabel->setText(QString("Ribs seeds generated at: %1").arg(outDir));
    QMessageBox::information(this, "Generate Seeds", "Ribs seed generation finished successfully.");
}

// ============================================================================
// Super resolution
// ============================================================================

void ManualSeedSelector::runSuperResolution()
{
    if (!hasImage() || m_path.empty())
    {
        QMessageBox::warning(this, "Super Resolution", "Please load an image before running super resolution.");
        return;
    }

    const QString inputPath = QString::fromStdString(m_path);
    if (!QFileInfo::exists(inputPath))
    {
        QMessageBox::warning(this, "Super Resolution", "The current image path does not exist on disk.");
        return;
    }

    QFileInfo inputInfo(inputPath);
    QString baseName = inputInfo.fileName();
    if (baseName.endsWith(".nii.gz", Qt::CaseInsensitive))
        baseName.chop(7);
    else if (baseName.endsWith(".nii", Qt::CaseInsensitive))
        baseName.chop(4);
    else
        baseName = inputInfo.completeBaseName();

    QString defaultOutput = QDir(inputInfo.absolutePath()).filePath(baseName + "_sr.nii.gz");
    QString outQ = QFileDialog::getSaveFileName(
        this,
        "Save Super Resolution Output",
        defaultOutput,
        "NIfTI files (*.nii *.nii.gz);;All files (*)");

    QCoreApplication::processEvents();
    if (outQ.isEmpty())
        return;
    if (!(outQ.endsWith(".nii", Qt::CaseInsensitive) || outQ.endsWith(".nii.gz", Qt::CaseInsensitive)))
        outQ += ".nii.gz";

    const QString scriptPath = resolveSuperResolutionScriptPath();
    if (scriptPath.isEmpty())
    {
        QMessageBox::critical(
            this,
            "Super Resolution",
            "Could not locate src/super_resolution/super_resolve_nifti.py.\n"
            "Set ROIFT_SR_SCRIPT to the script path if needed.");
        return;
    }

    const QString modelPath = resolveSuperResolutionModelPath(scriptPath);
    if (modelPath.isEmpty())
    {
        QMessageBox::critical(
            this,
            "Super Resolution",
            "Could not locate RealESRGAN_x2plus.pth.\n"
            "Set ROIFT_SR_MODEL to the checkpoint path if needed.");
        return;
    }

    QString pythonProgram;
    QStringList pythonPrefixArgs;
    if (!resolvePythonCommand(&pythonProgram, &pythonPrefixArgs))
    {
        QMessageBox::critical(
            this,
            "Super Resolution",
            "Could not find a Python interpreter.\n"
            "Install python/python3 or set ROIFT_PYTHON.");
        return;
    }

    QTemporaryDir tmpOutDir;
    if (!tmpOutDir.isValid())
    {
        QMessageBox::critical(this, "Super Resolution", "Failed to create a temporary output directory.");
        return;
    }

    const QFileInfo scriptInfo(scriptPath);
    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.setWorkingDirectory(scriptInfo.absolutePath());

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString pythonPath = env.value("PYTHONPATH");
    const QString scriptDir = scriptInfo.absolutePath();
    if (pythonPath.isEmpty())
        env.insert("PYTHONPATH", scriptDir);
    else
        env.insert("PYTHONPATH", scriptDir + QDir::listSeparator() + pythonPath);
    proc.setProcessEnvironment(env);

    QStringList args = pythonPrefixArgs;
    args << scriptPath
         << "--model"
         << modelPath
         << "--input"
         << inputPath
         << "--output"
         << tmpOutDir.path();

    QProgressDialog progress("Running super resolution. This may take a few minutes...", "Cancel", 0, 0, this);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.show();

    proc.start(pythonProgram, args);
    if (!proc.waitForStarted(10000))
    {
        progress.close();
        QMessageBox::critical(
            this,
            "Super Resolution",
            QString("Failed to start Python process.\n%1").arg(proc.errorString()));
        return;
    }

    while (!proc.waitForFinished(200))
    {
        QCoreApplication::processEvents();
        if (progress.wasCanceled())
        {
            proc.kill();
            proc.waitForFinished(3000);
            progress.close();
            QMessageBox::warning(this, "Super Resolution", "Super resolution execution was canceled.");
            return;
        }
    }
    progress.close();

    const QString procStdout = proc.readAllStandardOutput();
    const QString procStderr = proc.readAllStandardError();
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        QMessageBox::critical(
            this,
            "Super Resolution",
            QString("Super resolution failed (exit code %1).\n\nSTDERR:\n%2")
                .arg(proc.exitCode())
                .arg(procStderr));
        if (!procStdout.isEmpty())
            std::cerr << "Super-resolution STDOUT:\n"
                      << procStdout.toStdString() << "\n";
        if (!procStderr.isEmpty())
            std::cerr << "Super-resolution STDERR:\n"
                      << procStderr.toStdString() << "\n";
        return;
    }

    const QString expectedTmpOutput = QDir(tmpOutDir.path()).filePath("output.nii.gz");
    QString generatedOutput = expectedTmpOutput;
    if (!QFileInfo::exists(generatedOutput))
    {
        QDir tmpDir(tmpOutDir.path());
        const QStringList generated = tmpDir.entryList(
            QStringList() << "*.nii" << "*.nii.gz",
            QDir::Files,
            QDir::Name);
        if (!generated.isEmpty())
            generatedOutput = tmpDir.filePath(generated.front());
    }

    if (!QFileInfo::exists(generatedOutput))
    {
        QMessageBox::critical(
            this,
            "Super Resolution",
            "Super resolution finished but no output NIfTI file was generated.");
        return;
    }

    if (QFile::exists(outQ) && !QFile::remove(outQ))
    {
        QMessageBox::critical(
            this,
            "Super Resolution",
            QString("Could not overwrite existing file:\n%1").arg(outQ));
        return;
    }

    if (!QFile::copy(generatedOutput, outQ))
    {
        QMessageBox::critical(
            this,
            "Super Resolution",
            QString("Failed to save output file:\n%1").arg(outQ));
        return;
    }

    if (m_statusLabel)
        m_statusLabel->setText(QString("Super resolution saved: %1").arg(outQ));

    QMessageBox::information(
        this,
        "Super Resolution",
        QString("Super resolution finished successfully.\nSaved to:\n%1").arg(outQ));
}

// ============================================================================
// Mask post-processing
// ============================================================================

void ManualSeedSelector::runMaskPostProcessing()
{
    if (m_currentImageIndex < 0 || m_currentImageIndex >= static_cast<int>(m_images.size()))
    {
        QMessageBox::warning(this, "Postprocess Mask", "Please select an image first.");
        return;
    }

    if (!m_maskList || m_maskList->count() == 0)
    {
        QMessageBox::warning(this, "Postprocess Mask", "No masks are available for the selected image.");
        return;
    }

    const int selectedRow = m_maskList->currentRow();
    if (selectedRow < 0)
    {
        QMessageBox::warning(this, "Postprocess Mask", "Select a mask in the mask list before running post-processing.");
        return;
    }

    if (selectedRow >= static_cast<int>(m_images[m_currentImageIndex].maskPaths.size()))
    {
        QMessageBox::warning(this, "Postprocess Mask", "Selected mask index is out of range.");
        return;
    }

    const QString inputMaskPath = QFileInfo(QString::fromStdString(
                                                m_images[m_currentImageIndex].maskPaths[static_cast<size_t>(selectedRow)]))
                                      .absoluteFilePath();
    if (!QFileInfo::exists(inputMaskPath))
    {
        QMessageBox::warning(this, "Postprocess Mask", "The selected mask path does not exist on disk.");
        return;
    }

    QString baseName = stripNiftiSuffix(QFileInfo(inputMaskPath).fileName());
    QString defaultOutput = QDir(QFileInfo(inputMaskPath).absolutePath()).filePath(baseName + "_post.nii.gz");
    QString outQ = QFileDialog::getSaveFileName(
        this,
        "Save Postprocessed Mask",
        defaultOutput,
        "NIfTI files (*.nii *.nii.gz);;All files (*)");

    QCoreApplication::processEvents();
    if (outQ.isEmpty())
        return;
    if (!(outQ.endsWith(".nii", Qt::CaseInsensitive) || outQ.endsWith(".nii.gz", Qt::CaseInsensitive)))
        outQ += ".nii.gz";

    const QString scriptPath = resolveMaskPostprocessScriptPath();
    if (scriptPath.isEmpty())
    {
        QMessageBox::critical(
            this,
            "Postprocess Mask",
            "Could not locate src/seed_gen/ribs_segmentation/post_processing/postprocess_mask.py.\n"
            "Set ROIFT_MASK_PP_SCRIPT if needed.");
        return;
    }

    QString pythonProgram;
    QStringList pythonPrefixArgs;
    if (!resolvePythonCommand(&pythonProgram, &pythonPrefixArgs))
    {
        QMessageBox::critical(
            this,
            "Postprocess Mask",
            "Could not find a Python interpreter.\n"
            "Install python/python3 or set ROIFT_PYTHON.");
        return;
    }

    const QFileInfo scriptInfo(scriptPath);
    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.setWorkingDirectory(scriptInfo.absolutePath());

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString pythonPath = env.value("PYTHONPATH");
    const QString scriptDir = scriptInfo.absolutePath();
    if (pythonPath.isEmpty())
        env.insert("PYTHONPATH", scriptDir);
    else
        env.insert("PYTHONPATH", scriptDir + QDir::listSeparator() + pythonPath);
    proc.setProcessEnvironment(env);

    QStringList args = pythonPrefixArgs;
    args << scriptPath
         << "--input"
         << inputMaskPath
         << "--output"
         << outQ;

    QProgressDialog progress("Running mask post-processing...", "Cancel", 0, 0, this);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.show();

    proc.start(pythonProgram, args);
    if (!proc.waitForStarted(10000))
    {
        progress.close();
        QMessageBox::critical(
            this,
            "Postprocess Mask",
            QString("Failed to start Python process.\n%1").arg(proc.errorString()));
        return;
    }

    while (!proc.waitForFinished(200))
    {
        QCoreApplication::processEvents();
        if (progress.wasCanceled())
        {
            proc.kill();
            proc.waitForFinished(3000);
            progress.close();
            QMessageBox::warning(this, "Postprocess Mask", "Mask post-processing was canceled.");
            return;
        }
    }
    progress.close();

    const QString procStdout = proc.readAllStandardOutput();
    const QString procStderr = proc.readAllStandardError();
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        QMessageBox::critical(
            this,
            "Postprocess Mask",
            QString("Mask post-processing failed (exit code %1).\n\nSTDERR:\n%2")
                .arg(proc.exitCode())
                .arg(procStderr));
        if (!procStdout.isEmpty())
            std::cerr << "Mask postprocess STDOUT:\n"
                      << procStdout.toStdString() << "\n";
        if (!procStderr.isEmpty())
            std::cerr << "Mask postprocess STDERR:\n"
                      << procStderr.toStdString() << "\n";
        return;
    }

    const QString outAbs = QFileInfo(outQ).absoluteFilePath();
    if (!QFileInfo::exists(outAbs))
    {
        QMessageBox::critical(
            this,
            "Postprocess Mask",
            QString("Post-processing finished but output file was not generated:\n%1").arg(outAbs));
        return;
    }

    const std::string outStd = outAbs.toStdString();
    auto &maskPaths = m_images[m_currentImageIndex].maskPaths;
    if (std::find(maskPaths.begin(), maskPaths.end(), outStd) == maskPaths.end())
        maskPaths.push_back(outStd);

    updateMaskSeedLists();
    loadMaskFromFile(outStd);
    updateViews();

    const int outIndex = static_cast<int>(
        std::find(maskPaths.begin(), maskPaths.end(), outStd) - maskPaths.begin());
    if (m_maskList && outIndex >= 0 && outIndex < m_maskList->count())
        m_maskList->setCurrentRow(outIndex);

    if (m_statusLabel)
        m_statusLabel->setText(QString("Postprocessed mask saved: %1").arg(outAbs));

    QMessageBox::information(
        this,
        "Postprocess Mask",
        QString("Mask post-processing finished successfully.\nSaved to:\n%1").arg(outAbs));
}

// ============================================================================
// Vessel graph (Morse centreline of the current mask, rooted at a seed)
// ============================================================================

namespace
{
    // Last placed object seed, else the last seed of any kind.
    const Seed *pickRootSeed(const std::vector<Seed> &seeds)
    {
        for (int i = static_cast<int>(seeds.size()) - 1; i >= 0; --i)
        {
            if (seeds[static_cast<size_t>(i)].internal != 0)
                return &seeds[static_cast<size_t>(i)];
        }
        return seeds.empty() ? nullptr : &seeds.back();
    }
}

void ManualSeedSelector::runVesselGraph()
{
    if (!hasImage() || m_path.empty())
    {
        QMessageBox::warning(this, "Vessel Graph", "Please load an image first.");
        return;
    }

    const unsigned int sx = m_image.getSizeX();
    const unsigned int sy = m_image.getSizeY();
    const unsigned int sz = m_image.getSizeZ();
    const bool segmentFromCt = (m_vgraphDomainCombo &&
                                m_vgraphDomainCombo->currentData().toString() == "ct");

    const bool maskEmpty = (m_maskData.empty() ||
                            std::none_of(m_maskData.begin(), m_maskData.end(), [](int v)
                                         { return v > 0; }));
    if (!segmentFromCt)
    {
        if (maskEmpty)
        {
            QMessageBox::warning(this, "Vessel Graph",
                                 "The vessel graph runs on the current mask.\n"
                                 "Load or draw a mask, or switch Domain to \"Segment from CT\".");
            return;
        }
        if (m_maskDimX != sx || m_maskDimY != sy || m_maskDimZ != sz)
        {
            QMessageBox::warning(this, "Vessel Graph",
                                 QString("The mask grid (%1 x %2 x %3) does not match the image "
                                         "(%4 x %5 x %6). Resample the mask before extracting the graph.")
                                     .arg(m_maskDimX)
                                     .arg(m_maskDimY)
                                     .arg(m_maskDimZ)
                                     .arg(sx)
                                     .arg(sy)
                                     .arg(sz));
            return;
        }
    }

    const Seed *root = pickRootSeed(m_seeds);
    if (!root)
    {
        QMessageBox::warning(this, "Vessel Graph",
                             "Place a seed at the root of the tree (trunk / hilum) first.");
        return;
    }
    if (root->x < 0 || root->y < 0 || root->z < 0 ||
        static_cast<unsigned int>(root->x) >= sx ||
        static_cast<unsigned int>(root->y) >= sy ||
        static_cast<unsigned int>(root->z) >= sz)
    {
        QMessageBox::warning(this, "Vessel Graph", "The root seed lies outside the image.");
        return;
    }

    // Segment the label the user clicked on; 0 means "any non-zero mask voxel".
    const size_t rootIndex = static_cast<size_t>(root->x) +
                             static_cast<size_t>(root->y) * sx +
                             static_cast<size_t>(root->z) * static_cast<size_t>(sx) * sy;
    const int domainLabel = (!segmentFromCt && rootIndex < m_maskData.size() &&
                             m_maskData[rootIndex] > 0)
                                ? m_maskData[rootIndex]
                                : 0;

    const QString scriptPath = resolveProjectScriptPath(QStringLiteral("src/vessels/cli/vessel_graph.py"));
    if (scriptPath.isEmpty())
    {
        QMessageBox::critical(this, "Vessel Graph", "Could not locate src/vessels/cli/vessel_graph.py.");
        return;
    }

    QString pythonProgram;
    QStringList pythonPrefixArgs;
    if (!resolvePythonCommand(&pythonProgram, &pythonPrefixArgs))
    {
        QMessageBox::critical(this, "Vessel Graph",
                              "Could not find a Python interpreter.\n"
                              "Install python/python3 or set ROIFT_PYTHON.");
        return;
    }

    QTemporaryDir tmpDir;
    if (!tmpDir.isValid())
    {
        QMessageBox::critical(this, "Vessel Graph", "Could not create a temporary directory.");
        return;
    }
    QString tmpMask;
    if (!segmentFromCt)
    {
        tmpMask = QDir(tmpDir.path()).filePath("vessel_graph_domain.nii.gz");
        if (!saveMaskToFile(tmpMask.toStdString()))
            return;
    }

    const QFileInfo imageInfo(QString::fromStdString(m_path));
    const QString baseName = stripNiftiSuffix(imageInfo.fileName());
    const QDir outDir = imageInfo.absoluteDir();
    const QString outPath = outDir.filePath(baseName + "_vessel_graph.nii.gz");
    const QString centerlinePath = outDir.filePath(baseName + "_vessel_centerline.nii.gz");
    const bool wantCenterline = m_vgraphCenterlineBox && m_vgraphCenterlineBox->isChecked();

    const QFileInfo scriptInfo(scriptPath);
    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.setWorkingDirectory(scriptInfo.absolutePath());

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString scriptDir = scriptInfo.absolutePath();
    const QString pythonPath = env.value("PYTHONPATH");
    if (pythonPath.isEmpty())
        env.insert("PYTHONPATH", scriptDir);
    else
        env.insert("PYTHONPATH", scriptDir + QDir::listSeparator() + pythonPath);
    proc.setProcessEnvironment(env);

    const QString segmentationPath = outDir.filePath(baseName + "_vessel_roift.nii.gz");
    QStringList args = pythonPrefixArgs;
    args << scriptPath;
    if (segmentFromCt)
        args << "--ct" << imageInfo.absoluteFilePath()
             << "--save-segmentation" << segmentationPath;
    else
        args << "--mask" << tmpMask
             << "--image" << imageInfo.absoluteFilePath()
             << "--label" << QString::number(domainLabel);
    args << "--root" << QString::number(root->x) << QString::number(root->y) << QString::number(root->z)
         << "--out" << outPath
         << "--delta" << QString::number(m_vgraphDeltaSpin ? m_vgraphDeltaSpin->value() : 10.0, 'f', 2)
         << "--p" << QString::number(m_vgraphCenteringSpin ? m_vgraphCenteringSpin->value() : 3.0, 'f', 2)
         << "--label-mode" << (m_vgraphLabelModeCombo
                                   ? m_vgraphLabelModeCombo->currentData().toString()
                                   : QStringLiteral("branch"));
    if (wantCenterline)
        args << "--centerline" << centerlinePath;

    appendSegmentationLog(QString("Vessel graph: root (%1, %2, %3), domain %4")
                              .arg(root->x)
                              .arg(root->y)
                              .arg(root->z)
                              .arg(segmentFromCt
                                       ? QStringLiteral("segmented from the CT")
                                       : QString("current mask, label %1")
                                             .arg(domainLabel == 0 ? QStringLiteral("any")
                                                                   : QString::number(domainLabel))));

    QProgressDialog progress(segmentFromCt
                                 ? "Segmenting the vessels from the CT, then extracting the graph "
                                   "(this takes minutes)..."
                                 : "Extracting the vessel graph...",
                             "Cancel", 0, 0, this);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.show();

    proc.start(pythonProgram, args);
    if (!proc.waitForStarted(10000))
    {
        progress.close();
        QMessageBox::critical(this, "Vessel Graph",
                              QString("Failed to start Python process.\n%1").arg(proc.errorString()));
        return;
    }

    while (!proc.waitForFinished(200))
    {
        QCoreApplication::processEvents();
        if (progress.wasCanceled())
        {
            proc.kill();
            proc.waitForFinished(3000);
            progress.close();
            QMessageBox::warning(this, "Vessel Graph", "Vessel graph extraction was canceled.");
            return;
        }
    }
    progress.close();

    const QString procStdout = proc.readAllStandardOutput();
    const QString procStderr = proc.readAllStandardError();
    for (const QString &line : procStdout.split('\n', Qt::SkipEmptyParts))
        appendSegmentationLog(line);

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        QMessageBox::critical(this, "Vessel Graph",
                              QString("Vessel graph extraction failed (exit code %1).\n\n%2")
                                  .arg(proc.exitCode())
                                  .arg(procStderr.trimmed()));
        if (!procStderr.isEmpty())
            std::cerr << "Vessel graph STDERR:\n"
                      << procStderr.toStdString() << "\n";
        return;
    }

    const QString outAbs = QFileInfo(outPath).absoluteFilePath();
    if (!QFileInfo::exists(outAbs))
    {
        QMessageBox::critical(this, "Vessel Graph",
                              QString("Extraction finished but no mask was written:\n%1").arg(outAbs));
        return;
    }

    if (m_currentImageIndex >= 0 && m_currentImageIndex < static_cast<int>(m_images.size()))
    {
        auto &maskPaths = m_images[static_cast<size_t>(m_currentImageIndex)].maskPaths;
        auto registerMask = [&maskPaths](const QString &path)
        {
            const std::string p = QFileInfo(path).absoluteFilePath().toStdString();
            if (std::find(maskPaths.begin(), maskPaths.end(), p) == maskPaths.end())
                maskPaths.push_back(p);
        };
        registerMask(outAbs);
        if (wantCenterline && QFileInfo::exists(centerlinePath))
            registerMask(centerlinePath);
        if (segmentFromCt && QFileInfo::exists(segmentationPath))
            registerMask(segmentationPath);
        updateMaskSeedLists();

        const std::string outStd = QFileInfo(outAbs).absoluteFilePath().toStdString();
        const int outIndex = static_cast<int>(
            std::find(maskPaths.begin(), maskPaths.end(), outStd) - maskPaths.begin());
        if (m_maskList && outIndex >= 0 && outIndex < m_maskList->count())
            m_maskList->setCurrentRow(outIndex);
    }

    loadMaskFromFile(outAbs.toStdString());
    updateViews();

    QString summary = outAbs;
    for (const QString &line : procStdout.split('\n', Qt::SkipEmptyParts))
    {
        if (line.startsWith("OK "))
            summary = line.mid(3).trimmed();
    }
    if (m_statusLabel)
        m_statusLabel->setText(QString("Vessel graph: %1").arg(summary));
}
