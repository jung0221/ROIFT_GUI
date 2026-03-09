#include "SegmentationRunner.h"
#include "ManualSeedSelector.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QMetaObject>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QDebug>
#include <QThread>
#include <QFile>
#include <QTime>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <functional>
#include <QListWidget>
#include <set>
#include <unordered_set>
#include <fstream>
#include <iostream>

#include <itkImage.h>
#include <itkImageFileReader.h>
#include <itkImageFileWriter.h>
#include <itkImageRegionIterator.h>
#include <itkIntensityWindowingImageFilter.h>
#include <QTemporaryFile>

using namespace SegmentationRunner;

namespace
{
    struct RoiftExecutable
    {
        QString path;
        bool gpuBinary = false;
    };

    QStringList roiftExecutableNames(bool preferGpu)
    {
#if defined(Q_OS_WIN)
        const QString suffix = ".exe";
#else
        const QString suffix;
#endif
        const QString gpuName = "oiftrelax_gpu" + suffix;
        const QString cpuName = "oiftrelax" + suffix;
        const QString parallelName = "oiftrelax_parallel" + suffix;

        if (preferGpu)
            return {gpuName, cpuName, parallelName};
        return {cpuName, parallelName, gpuName};
    }

    bool isGpuExecutablePath(const QString &path)
    {
        return QFileInfo(path).fileName().startsWith("oiftrelax_gpu", Qt::CaseInsensitive);
    }

    RoiftExecutable resolveRoiftExecutable(bool preferGpu)
    {
        RoiftExecutable resolved;

        const QString envExecutable = qEnvironmentVariable("ROIFT_EXECUTABLE").trimmed();
        if (!envExecutable.isEmpty())
        {
            const QString envPath = QFileInfo(envExecutable).absoluteFilePath();
            if (QFileInfo::exists(envPath))
            {
                resolved.path = envPath;
                resolved.gpuBinary = isGpuExecutablePath(envPath);
                return resolved;
            }
        }

        const QStringList names = roiftExecutableNames(preferGpu);
        for (const QString &name : names)
        {
            const QString inPath = QStandardPaths::findExecutable(name);
            if (!inPath.isEmpty())
            {
                resolved.path = inPath;
                resolved.gpuBinary = isGpuExecutablePath(inPath);
                return resolved;
            }
        }

        // Try likely paths for both in-source and out-of-source builds.
        const QStringList relativeSearchDirs = {
            "",
            "roift",
            "roift/gft_delta",
            "../roift",
            "../roift/gft_delta",
            "../roift/Release",
            "../roift/Debug",
            "../roift/gft_delta/Release",
            "../roift/gft_delta/Debug",
            "../../roift",
            "../../roift/gft_delta",
            "../../roift/Release",
            "../../roift/Debug",
            "../../roift/gft_delta/Release",
            "../../roift/gft_delta/Debug",
            "build/roift",
            "build/roift/gft_delta",
            "build/roift/Release",
            "build/roift/Debug",
            "build/roift/gft_delta/Release",
            "build/roift/gft_delta/Debug",
            "build/bin/Release",
            "build/bin/Debug",
            "build/src/ROIFT_GUI/roift",
            "build/src/ROIFT_GUI/roift/gft_delta",
            "build/src/ROIFT_GUI/roift/Release",
            "build/src/ROIFT_GUI/roift/Debug",
            "build/src/ROIFT_GUI/roift/gft_delta/Release",
            "build/src/ROIFT_GUI/roift/gft_delta/Debug",
            "build/src/ROIFT_GUI/Release",
            "build/src/ROIFT_GUI/Debug",
            "build/src",
            "build/src/Release",
            "build/src/Debug",
        };

        const QStringList roots = {
            QCoreApplication::applicationDirPath(),
            QDir::currentPath(),
        };

        for (const QString &root : roots)
        {
            QDir dir(root);
            if (!dir.exists())
                continue;

            for (int depth = 0; depth <= 12; ++depth)
            {
                for (const QString &subDir : relativeSearchDirs)
                {
                    for (const QString &name : names)
                    {
                        const QString relPath = subDir.isEmpty() ? name : QString("%1/%2").arg(subDir, name);
                        const QString candidate = QDir::cleanPath(dir.filePath(relPath));
                        if (QFileInfo::exists(candidate))
                        {
                            resolved.path = QFileInfo(candidate).absoluteFilePath();
                            resolved.gpuBinary = isGpuExecutablePath(resolved.path);
                            return resolved;
                        }
                    }
                }

                if (!dir.cdUp())
                    break;
            }
        }

        return resolved;
    }

    QString roiftNotFoundMessage(bool preferGpu)
    {
        const QString mode = preferGpu ? "GPU" : "CPU";
        return QString("Could not find external ROIFT executable (%1 mode).\n"
                       "Searched PATH and build folders relative to current/app directories.\n"
                       "If needed, set ROIFT_EXECUTABLE to the full executable path.")
            .arg(mode);
    }

    // Helper function to create a windowed image
    QString createWindowedImage(const std::string &originalPath, double windowLevel, double windowWidth)
    {
        using ImageType = itk::Image<short, 3>;
        using ReaderType = itk::ImageFileReader<ImageType>;
        using WriterType = itk::ImageFileWriter<ImageType>;
        using WindowFilterType = itk::IntensityWindowingImageFilter<ImageType, ImageType>;

        // Read original image
        ReaderType::Pointer reader = ReaderType::New();
        reader->SetFileName(originalPath);
        try
        {
            reader->Update();
        }
        catch (const itk::ExceptionObject &e)
        {
            qWarning() << "Error reading image for windowing:" << e.what();
            return QString::fromStdString(originalPath);
        }

        // Apply windowing: clamp values outside the window and remap to maintain relative intensities
        WindowFilterType::Pointer windowFilter = WindowFilterType::New();
        windowFilter->SetInput(reader->GetOutput());

        double windowMin = windowLevel - windowWidth / 2.0;
        double windowMax = windowLevel + windowWidth / 2.0;

        windowFilter->SetWindowMinimum(windowMin);
        windowFilter->SetWindowMaximum(windowMax);

        // Map the windowed range to itself (preserving actual intensity values)
        // Values below windowMin become windowMin, above windowMax become windowMax
        windowFilter->SetOutputMinimum(static_cast<short>(windowMin));
        windowFilter->SetOutputMaximum(static_cast<short>(windowMax));

        // Create temporary file
        QTemporaryFile *tempFile = new QTemporaryFile(QDir::temp().filePath("roift_windowed_XXXXXX.nii.gz"));
        tempFile->setAutoRemove(false); // We'll remove it manually
        if (!tempFile->open())
        {
            qWarning() << "Failed to create temporary windowed image file";
            delete tempFile;
            return QString::fromStdString(originalPath);
        }
        QString tempPath = tempFile->fileName();
        tempFile->close();
        delete tempFile;

        // Write windowed image
        WriterType::Pointer writer = WriterType::New();
        writer->SetFileName(tempPath.toStdString());
        writer->SetInput(windowFilter->GetOutput());
        try
        {
            writer->Update();
        }
        catch (const itk::ExceptionObject &e)
        {
            qWarning() << "Error writing windowed image:" << e.what();
            return QString::fromStdString(originalPath);
        }

        return tempPath;
    }
}

void SegmentationRunner::showSegmentationDialog(ManualSeedSelector *parent)
{
    if (!parent)
        return;

    // Gather seeds and build deduped list before showing the dialog so we can present
    // the available labels for optional skipping in batch mode.
    const auto &seeds = parent->getSeeds();
    std::set<int> uniq;
    for (const auto &s : seeds)
        uniq.insert(s.label);
    if (uniq.empty())
    {
        std::cerr << "No seeds available for segmentation\n";
        return;
    }

    // dedupe keeping newest per x:y:z
    std::vector<Seed> filtered;
    filtered.reserve(seeds.size());
    std::unordered_set<std::string> seen;
    for (int i = int(seeds.size()) - 1; i >= 0; --i)
    {
        const Seed &s = seeds[size_t(i)];
        std::string key = std::to_string(s.x) + ":" + std::to_string(s.y) + ":" + std::to_string(s.z);
        if (seen.find(key) != seen.end())
            continue;
        seen.insert(key);
        filtered.push_back(s);
    }
    std::reverse(filtered.begin(), filtered.end());

    QStringList choices;
    for (int v : uniq)
        choices << QString::number(v);

    // Build the same dialog that used to be inline in ManualSeedSelector
    QDialog dlg(parent);
    dlg.setWindowTitle("ROIFT parameters");
    QVBoxLayout *dlgLayout = new QVBoxLayout(&dlg);

    dlgLayout->addWidget(new QLabel("Polarity (pol):"));
    QHBoxLayout *polRow = new QHBoxLayout();
    QSlider *polSlider = new QSlider(Qt::Horizontal);
    polSlider->setRange(-100, 100);
    polSlider->setValue(100);
    QLabel *polValue = new QLabel("1.00");
    polRow->addWidget(polSlider);
    polRow->addWidget(polValue);
    dlgLayout->addLayout(polRow);
    QObject::connect(polSlider, &QSlider::valueChanged, polValue, [polValue](int v)
                     { polValue->setText(QString::number(v / 100.0, 'f', 2)); });

    dlgLayout->addWidget(new QLabel("Relax iters (niter):"));
    QSpinBox *niterSpin = new QSpinBox();
    niterSpin->setRange(1, 10000);
    niterSpin->setValue(1);
    dlgLayout->addWidget(niterSpin);

    dlgLayout->addWidget(new QLabel("Percentile:"));
    QHBoxLayout *percRow = new QHBoxLayout();
    QSlider *percSlider = new QSlider(Qt::Horizontal);
    percSlider->setRange(0, 100);
    percSlider->setValue(0);
    QLabel *percValue = new QLabel("0");
    percRow->addWidget(percSlider);
    percRow->addWidget(percValue);
    dlgLayout->addLayout(percRow);
    QObject::connect(percSlider, &QSlider::valueChanged, percValue, [percValue](int v)
                     { percValue->setText(QString::number(v)); });

    QCheckBox *segmentAllBox = new QCheckBox("Segment all labeled seeds (one run per label)");
    dlgLayout->addWidget(segmentAllBox);

    QCheckBox *polSweepBox = new QCheckBox("Segment all polarities (-1.0 to 1.0, step 0.1)");
    dlgLayout->addWidget(polSweepBox);

    // Avoid exploding combinations: disable polarity sweep when batch-by-label is enabled
    QObject::connect(segmentAllBox, &QCheckBox::toggled, polSweepBox, [polSweepBox](bool on)
                     { polSweepBox->setChecked(false); polSweepBox->setEnabled(!on); });

    // Provide a checklist of labels so the user can choose labels to SKIP in batch mode.
    dlgLayout->addWidget(new QLabel("Check labels to SKIP when 'Segment all' is used:"));
    QListWidget *skipList = new QListWidget();
    for (int v : uniq)
    {
        QListWidgetItem *it = new QListWidgetItem(QString::number(v));
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Unchecked);
        skipList->addItem(it);
    }
    dlgLayout->addWidget(skipList);

    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    dlgLayout->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return;

    double pol = polSlider->value() / 100.0;
    int niter = niterSpin->value();
    int percentile = percSlider->value();

    bool doAll = segmentAllBox->isChecked();
    bool polSweep = polSweepBox->isChecked() && !doAll;
    if (!doAll)
    {
        bool ok = false;
        QString sel = QInputDialog::getItem(parent, "Select Internal Label", "Choose label to be INTERNAL:", choices, 0, false, &ok);
        if (!ok)
            return;
        int internal_label = sel.toInt();

        QString baseDir = QFileInfo(QString::fromStdString(parent->getImagePath())).absolutePath();

        QString seedFile = QFileDialog::getSaveFileName(parent, "Save seed file", baseDir, "Text files (*.txt);;All files (*)");
        QCoreApplication::processEvents();
        if (seedFile.isEmpty())
            return;
        if (!seedFile.toLower().endsWith(".txt"))
            seedFile += ".txt";

        std::ofstream ofs(seedFile.toStdString());
        ofs << filtered.size() << "\n";
        for (const auto &s : filtered)
        {
            int internal_flag = (s.label == internal_label) ? 1 : 0;
            ofs << s.x << " " << s.y << " " << s.z << " " << s.label << " " << internal_flag << "\n";
        }
        ofs.close();

        QString outp;
        if (!polSweep)
        {
            QString outQ = QFileDialog::getSaveFileName(parent, "Save segmentation output", baseDir, "NIfTI files (*.nii *.nii.gz);;All files (*)");
            QCoreApplication::processEvents();
            if (outQ.isEmpty())
                return;
            outp = outQ;
            if (!(outp.endsWith(".nii", Qt::CaseInsensitive) || outp.endsWith(".nii.gz", Qt::CaseInsensitive)))
                outp += ".nii.gz";
        }

        QString seedPath = seedFile;
        const RoiftExecutable roiftExec = resolveRoiftExecutable(parent->getUseGPU());
        const QString exePath = roiftExec.path;
        if (exePath.isEmpty())
        {
            QMessageBox::critical(parent, "ROIFT not found", roiftNotFoundMessage(parent->getUseGPU()));
            return;
        }
        const bool useGpuExecution = parent->getUseGPU() && roiftExec.gpuBinary;
        if (parent->getUseGPU() && !roiftExec.gpuBinary)
            qWarning() << "Use GPU is enabled, but oiftrelax_gpu was not found. Falling back to:" << exePath;

        if (polSweep)
        {
            QString outDir = QFileDialog::getExistingDirectory(parent, "Select directory to save per-polarity segmentations", baseDir);
            QCoreApplication::processEvents();
            if (outDir.isEmpty())
                return;

            std::vector<double> polValues;
            polValues.reserve(21);
            for (int i = 0; i <= 20; ++i)
            {
                double v = -1.0 + 0.1 * i;
                // keep one decimal place, but avoid -0.0
                double rounded = std::round(v * 10.0) / 10.0;
                if (std::abs(rounded) < 1e-4)
                    rounded = 0.0;
                polValues.push_back(rounded);
            }

            // GPU can only handle one segmentation at a time
            bool useGPU = useGpuExecution;
            int maxParallel = useGPU ? 1 : std::min(5, std::max(1, QThread::idealThreadCount()));
            struct PolProc
            {
                double polValue;
                QString outPath;
                QProcess *proc;
            };
            std::vector<PolProc> running;
            std::vector<std::pair<double, QString>> successes;

            auto safeTag = [](double v)
            {
                QString t = QString::number(v, 'f', 1);
                t.replace("-", "neg");
                t.replace(".", "_");
                return t;
            };

            size_t nextIdx = 0;
            auto startPol = [&](double polVal) -> bool
            {
                QString tag = safeTag(polVal);
                QString outPol = QDir(outDir).filePath(QString("segmentation_pol_%1.nii.gz").arg(tag));
                QProcess *p = new QProcess();
                QStringList argsPol;
                argsPol << QString::fromStdString(parent->getImagePath()) << seedPath << QString::number(polVal, 'f', 1) << QString::number(niter) << QString::number(percentile) << outPol;
                QStringList quotedArgs;
                for (const QString &a : argsPol)
                    quotedArgs << '"' + a + '"';
                qDebug().noquote() << "Running:" << exePath << quotedArgs.join(' ');
                p->setProcessChannelMode(QProcess::SeparateChannels);
                p->start(exePath, argsPol);
                bool startedPol = p->waitForStarted(60000);
                if (!startedPol)
                {
                    std::cerr << "[ERROR] Failed to start ROIFT for pol=" << polVal << "\n";
                    delete p;
                    return false;
                }
                running.push_back({polVal, outPol, p});
                return true;
            };

            while (nextIdx < polValues.size() && (int)running.size() < maxParallel)
            {
                startPol(polValues[nextIdx]);
                ++nextIdx;
            }

            while (nextIdx < polValues.size() || !running.empty())
            {
                QCoreApplication::processEvents();
                for (auto it = running.begin(); it != running.end();)
                {
                    QProcess *p = it->proc;
                    if (p->state() == QProcess::NotRunning)
                    {
                        QString outStd = p->readAllStandardOutput();
                        QString outErr = p->readAllStandardError();
                        int code = p->exitCode();
                        double polVal = it->polValue;
                        if (code == 0)
                        {
                            std::cerr << "[INFO] ROIFT finished for pol=" << polVal << " output=" << it->outPath.toStdString() << "\n";
                            if (!outStd.isEmpty())
                                std::cerr << outStd.toStdString() << "\n";
                            if (!outErr.isEmpty())
                                std::cerr << outErr.toStdString() << "\n";
                            successes.emplace_back(polVal, it->outPath);
                        }
                        else
                        {
                            std::cerr << "[ERROR] ROIFT failed for pol=" << polVal << " code=" << code << "\n";
                            if (!outStd.isEmpty())
                                std::cerr << outStd.toStdString() << "\n";
                            if (!outErr.isEmpty())
                                std::cerr << outErr.toStdString() << "\n";
                        }
                        if (p->state() != QProcess::NotRunning)
                        {
                            p->kill();
                            p->waitForFinished(1000);
                        }
                        delete p;
                        it = running.erase(it);
                        if (nextIdx < polValues.size())
                        {
                            startPol(polValues[nextIdx]);
                            ++nextIdx;
                        }
                    }
                    else
                    {
                        ++it;
                    }
                }
                QThread::msleep(50);
            }

            if (successes.empty())
            {
                QMessageBox::warning(parent, "ROIFT", "No successful outputs were generated for the polarity sweep.");
                return;
            }

            QString summary = QString("Polarity sweep finished (%1 outputs). Masks saved in: %2")
                                  .arg(successes.size())
                                  .arg(outDir);
            QMessageBox::information(parent, "ROIFT", summary);
        }
        else
        {
            QProcess proc;
            QStringList args;
            args << QString::fromStdString(parent->getImagePath()) << seedPath << QString::number(pol) << QString::number(niter) << QString::number(percentile) << outp;
            if (useGpuExecution)
                args << "--delta";
            QStringList quotedArgs;
            for (const QString &a : args)
                quotedArgs << '"' + a + '"';
            qDebug().noquote() << "Running:" << exePath << quotedArgs.join(' ');

            proc.setProcessChannelMode(QProcess::SeparateChannels);
            proc.start(exePath, args);
            bool started = proc.waitForStarted(60000);
            if (!started)
            {
                QMessageBox::critical(parent, "ROIFT start failed", "Failed to start ROIFT executable.");
                return;
            }
            bool finished = proc.waitForFinished(-1);
            QString procStdout = proc.readAllStandardOutput();
            QString procStderr = proc.readAllStandardError();
            if (!finished)
            {
                std::cerr << "ROIFT did not finish (timed out or was killed).\n";
                if (!procStdout.isEmpty())
                    std::cerr << "ROIFT STDOUT:\n"
                              << procStdout.toStdString() << "\n";
                if (!procStderr.isEmpty())
                    std::cerr << "ROIFT STDERR:\n"
                              << procStderr.toStdString() << "\n";
                QMessageBox::critical(parent, "ROIFT failed", "ROIFT did not finish successfully. See console for details.");
                return;
            }
            int exitCode = proc.exitCode();
            if (exitCode == 0)
            {
                std::cerr << "ROIFT finished successfully, output=" << outp.toStdString() << "\n";
                if (!procStdout.isEmpty())
                    std::cerr << "ROIFT STDOUT:\n"
                              << procStdout.toStdString() << "\n";
                if (!procStderr.isEmpty())
                    std::cerr << "ROIFT STDERR:\n"
                              << procStderr.toStdString() << "\n";
                bool loaded = parent->applyMaskFromPath(outp.toStdString());
                if (!loaded)
                {
                    QMessageBox::warning(parent, "Load Mask", QString("ROIFT finished but failed to load output mask: %1").arg(outp));
                }
                else
                {
                    QMessageBox::information(parent, "ROIFT", "Segmentation finished and mask loaded successfully.");
                }
            }
            else
            {
                std::cerr << "ROIFT failed with code=" << exitCode << "\n";
                if (!procStdout.isEmpty())
                    std::cerr << "ROIFT STDOUT:\n"
                              << procStdout.toStdString() << "\n";
                if (!procStderr.isEmpty())
                    std::cerr << "ROIFT STDERR:\n"
                              << procStderr.toStdString() << "\n";
                QMessageBox::critical(parent, "ROIFT failed", QString("ROIFT returned exit code %1.\nSee console for details.\nSTDERR:\n%2").arg(exitCode).arg(procStderr));
            }
        }

        return;
    }

    // Batch per-label
    QString outDir = QFileDialog::getExistingDirectory(parent, "Select directory to save per-label seed files and segmentations", QString::fromStdString(parent->getImagePath()));
    QCoreApplication::processEvents();
    if (outDir.isEmpty())
        return;
    const RoiftExecutable roiftExec = resolveRoiftExecutable(parent->getUseGPU());
    const QString exePath = roiftExec.path;
    if (exePath.isEmpty())
    {
        QMessageBox::critical(parent, "ROIFT not found", roiftNotFoundMessage(parent->getUseGPU()));
        return;
    }
    const bool useGpuExecution = parent->getUseGPU() && roiftExec.gpuBinary;
    if (parent->getUseGPU() && !roiftExec.gpuBinary)
        qWarning() << "Use GPU is enabled, but oiftrelax_gpu was not found. Falling back to:" << exePath;

    // Parallel execution: schedule up to N concurrent oiftrelax invocations
    // Cap concurrency to at most 10 processes at once to avoid overloading the system
    // GPU can only handle one segmentation at a time
    bool useGPU = useGpuExecution;
    int maxParallel = useGPU ? 1 : std::min(5, std::max(1, QThread::idealThreadCount()));
    int idx = 0;
    // Build labels vector excluding any skipped labels from skipList (checked = skip)
    std::unordered_set<int> skipSet;
    for (int i = 0; i < skipList->count(); ++i)
    {
        QListWidgetItem *it = skipList->item(i);
        if (it && it->checkState() == Qt::Checked)
        {
            skipSet.insert(it->text().toInt());
        }
    }
    std::vector<int> labels;
    labels.reserve(uniq.size());
    for (int v : uniq)
    {
        if (skipSet.find(v) == skipSet.end())
            labels.push_back(v);
    }
    if (labels.empty())
    {
        QMessageBox::information(parent, "ROIFT", "No labels selected for segmentation after applying skip list.");
        return;
    }
    int total = int(labels.size());

    struct ProcInfo
    {
        int label;
        QString perSeed;
        QString outp;
        QProcess *proc;
    };
    std::vector<ProcInfo> running;
    std::vector<std::pair<int, QString>> successfulOutputs;

    size_t nextIndex = 0;
    // Helper to start a process for a given label
    auto startLabelProc = [&](int label) -> bool
    {
        QString perSeed = QDir(outDir).filePath(QString("seeds_label%1.txt").arg(label));
        std::ofstream ofs2(perSeed.toStdString());
        if (!ofs2)
        {
            std::cerr << "[ERROR] Failed to open seed file for label " << label << "\n";
            return false;
        }
        ofs2 << filtered.size() << "\n";
        for (const auto &s : filtered)
        {
            int internal_flag = (s.label == label) ? 1 : 0;
            ofs2 << s.x << " " << s.y << " " << s.z << " " << s.label << " " << internal_flag << "\n";
        }
        ofs2.close();
        QString outp2 = QDir(outDir).filePath(QString("segmentation_label%1.nii.gz").arg(label));

        // allocate QProcess without parent; we will manage its lifetime manually
        QProcess *proc2 = new QProcess();
        QStringList args2;
        args2 << QString::fromStdString(parent->getImagePath()) << perSeed << QString::number(pol) << QString::number(niter) << QString::number(percentile) << outp2;
        QStringList quoted2;
        for (const QString &a : args2)
            quoted2 << '"' + a + '"';
        qDebug().noquote() << "Running:" << exePath << quoted2.join(' ');
        proc2->setProcessChannelMode(QProcess::SeparateChannels);
        proc2->start(exePath, args2);
        bool started2 = proc2->waitForStarted(60000);
        if (!started2)
        {
            std::cerr << "[ERROR] Failed to start ROIFT for label " << label << "\n";
            delete proc2;
            return false;
        }
        running.push_back({label, perSeed, outp2, proc2});
        return true;
    };

    // Kick off initial batch
    while (nextIndex < labels.size() && (int)running.size() < maxParallel)
    {
        startLabelProc(labels[nextIndex]);
        ++nextIndex;
    }

    // Poll loop: start new processes as others finish
    while (nextIndex < labels.size() || !running.empty())
    {
        QCoreApplication::processEvents();
        // check running procs for completion
        for (auto it = running.begin(); it != running.end();)
        {
            QProcess *p = it->proc;
            if (p->state() == QProcess::NotRunning)
            {
                QString outStd = p->readAllStandardOutput();
                QString outErr = p->readAllStandardError();
                int code2 = p->exitCode();
                int label = it->label;
                if (code2 == 0)
                {
                    std::cerr << "[INFO] ROIFT finished for label " << label << ", output=" << it->outp.toStdString() << "\n";
                    if (!outStd.isEmpty())
                        std::cerr << outStd.toStdString() << "\n";
                    if (!outErr.isEmpty())
                        std::cerr << outErr.toStdString() << "\n";
                    successfulOutputs.emplace_back(label, it->outp);
                }
                else
                {
                    std::cerr << "[ERROR] ROIFT failed for label " << label << " with code=" << code2 << "\n";
                    if (!outStd.isEmpty())
                        std::cerr << outStd.toStdString() << "\n";
                    if (!outErr.isEmpty())
                        std::cerr << outErr.toStdString() << "\n";
                }
                // ensure process is stopped and deleted
                if (p->state() != QProcess::NotRunning)
                {
                    p->kill();
                    p->waitForFinished(1000);
                }
                delete p;
                it = running.erase(it);
                // Start next pending proc if any
                if (nextIndex < labels.size())
                {
                    startLabelProc(labels[nextIndex]);
                    ++nextIndex;
                }
            }
            else
            {
                ++it;
            }
        }
        // sleep a little to avoid busy loop
        QThread::msleep(50);
    }

    // If we have multiple per-label outputs, attempt to merge them into a single multilabel file
    if (!successfulOutputs.empty())
    {
        using PixelType = int32_t;
        using ImageType = itk::Image<PixelType, 3>;
        try
        {
            // Read first image to get size/spacing/origin
            itk::ImageFileReader<ImageType>::Pointer reader0 = itk::ImageFileReader<ImageType>::New();
            reader0->SetFileName(successfulOutputs.front().second.toStdString());
            reader0->Update();
            ImageType::Pointer base = reader0->GetOutput();
            ImageType::RegionType region = base->GetLargestPossibleRegion();
            ImageType::Pointer outImg = ImageType::New();
            outImg->SetRegions(region);
            outImg->SetSpacing(base->GetSpacing());
            outImg->SetOrigin(base->GetOrigin());
            outImg->SetDirection(base->GetDirection());
            outImg->Allocate();
            outImg->FillBuffer(0);

            // Iterate over successful outputs in the same order we processed (uniq ascending).
            for (const auto &pr : successfulOutputs)
            {
                int lbl = pr.first;
                const std::string srcPath = pr.second.toStdString();
                itk::ImageFileReader<ImageType>::Pointer r = itk::ImageFileReader<ImageType>::New();
                r->SetFileName(srcPath);
                r->Update();
                ImageType::Pointer img = r->GetOutput();
                itk::ImageRegionIterator<ImageType> itSrc(img, img->GetLargestPossibleRegion());
                itk::ImageRegionIterator<ImageType> itDst(outImg, outImg->GetLargestPossibleRegion());
                for (itSrc.GoToBegin(), itDst.GoToBegin(); !itSrc.IsAtEnd(); ++itSrc, ++itDst)
                {
                    PixelType v = itSrc.Get();
                    if (v != 0)
                    {
                        // later labels override earlier ones because uniq is ascending and we iterate in that order
                        itDst.Set(static_cast<PixelType>(lbl));
                    }
                }
            }

            QString merged = QDir(outDir).filePath("segmentation_multilabel.nii.gz");
            using WriterType = itk::ImageFileWriter<ImageType>;
            WriterType::Pointer writer = WriterType::New();
            writer->SetFileName(merged.toStdString());
            writer->SetInput(outImg);
            writer->Update();

            // Try to load the merged mask into the application
            bool loaded = parent->applyMaskFromPath(merged.toStdString());
            if (!loaded)
            {
                QMessageBox::information(parent, "ROIFT", QString("Batch segmentation finished, merged mask saved to %1 but failed to load into the GUI.").arg(merged));
            }
            else
            {
                QMessageBox::information(parent, "ROIFT", QString("Batch segmentation finished. Merged multilabel mask loaded: %1").arg(merged));
            }
        }
        catch (const std::exception &e)
        {
            QMessageBox::warning(parent, "ROIFT", QString("Batch segmentation finished but failed to merge outputs: %1").arg(e.what()));
        }
    }
    else
    {
        QMessageBox::information(parent, "ROIFT", "Batch segmentation finished. No successful per-label outputs were produced.");
    }
}

namespace
{
    enum class SegmentationRequestKind
    {
        Single,
        PolaritySweep,
        BatchPerLabel
    };

    struct SegmentationRequest
    {
        SegmentationRequestKind kind = SegmentationRequestKind::Single;
        QString sourceImagePath;
        QString outputPath;
        QString outputDir;
        QString executablePath;
        QString initialMessage;
        QString progressLabel;
        QStringList initialLogs;
        std::vector<Seed> filteredSeeds;
        std::vector<int> labels;
        std::vector<double> polarityValues;
        bool useGpuExecution = false;
        bool legacyBinaryMode = false;
        bool needsWindowing = false;
        double pol = 1.0;
        int niter = 1;
        int percentile = 0;
        int selectedInternalLabel = -1;
        double windowLevel = 0.0;
        double windowWidth = 1.0;
        int progressTotal = 0;
    };

    struct SegmentationExecutionResult
    {
        bool success = false;
        QString summary;
        QStringList generatedMaskPaths;
    };

    struct SegmentationExecutionCallbacks
    {
        std::function<void(const QString &)> log;
        std::function<void(const QString &, int, int)> progress;
    };

    struct ScopedFileCleanup
    {
        QString path;
        ~ScopedFileCleanup()
        {
            if (!path.isEmpty())
                QFile::remove(path);
        }
    };

    std::vector<Seed> dedupeSeedsKeepingLatest(const std::vector<Seed> &seeds)
    {
        std::vector<Seed> filtered;
        filtered.reserve(seeds.size());
        std::unordered_set<std::string> seen;
        for (int i = static_cast<int>(seeds.size()) - 1; i >= 0; --i)
        {
            const Seed &s = seeds[static_cast<size_t>(i)];
            const std::string key = std::to_string(s.x) + ":" + std::to_string(s.y) + ":" + std::to_string(s.z);
            if (seen.find(key) != seen.end())
                continue;
            seen.insert(key);
            filtered.push_back(s);
        }
        std::reverse(filtered.begin(), filtered.end());
        return filtered;
    }

    QStringList buildLabelChoices(const std::set<int> &labels)
    {
        QStringList choices;
        for (int label : labels)
            choices << QString::number(label);
        return choices;
    }

    bool writeMultilabelSeedFile(const QString &seedFilePath, const std::vector<Seed> &filteredSeeds)
    {
        std::ofstream ofs(seedFilePath.toStdString());
        if (!ofs)
            return false;

        ofs << filteredSeeds.size() << "\n";
        for (const auto &seed : filteredSeeds)
        {
            const int labelId = std::max(0, seed.label);
            ofs << seed.x << " " << seed.y << " " << seed.z << " " << labelId << " " << labelId << "\n";
        }
        return true;
    }

    bool writeLegacySeedFile(const QString &seedFilePath, const std::vector<Seed> &filteredSeeds, int internalLabel)
    {
        std::ofstream ofs(seedFilePath.toStdString());
        if (!ofs)
            return false;

        ofs << filteredSeeds.size() << "\n";
        for (const auto &seed : filteredSeeds)
        {
            const int internalFlag = (seed.label == internalLabel) ? 1 : 0;
            ofs << seed.x << " " << seed.y << " " << seed.z << " " << seed.label << " " << internalFlag << "\n";
        }
        return true;
    }

    QString quoteCommand(const QString &exePath, const QStringList &args)
    {
        QStringList quotedArgs;
        for (const QString &arg : args)
            quotedArgs << '"' + arg + '"';
        return QString("%1 %2").arg(exePath, quotedArgs.join(' '));
    }

    void logTextBlock(const QString &prefix,
                      const QString &text,
                      const std::function<void(const QString &)> &log)
    {
        if (!log || text.isEmpty())
            return;

        QString normalized = text;
        normalized.replace("\r\n", "\n");
        normalized.replace('\r', '\n');
        const QStringList lines = normalized.split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines)
        {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty())
                continue;
            log(prefix.isEmpty() ? trimmed : QString("%1 %2").arg(prefix, trimmed));
        }
    }

    void drainProcessOutput(QProcess *proc,
                            QString *capturedStdout,
                            QString *capturedStderr,
                            const QString &prefix,
                            const std::function<void(const QString &)> &log)
    {
        if (!proc)
            return;

        const QString stdOutChunk = QString::fromLocal8Bit(proc->readAllStandardOutput());
        if (!stdOutChunk.isEmpty())
        {
            if (capturedStdout)
                *capturedStdout += stdOutChunk;
            logTextBlock(prefix + " [stdout]", stdOutChunk, log);
        }

        const QString stdErrChunk = QString::fromLocal8Bit(proc->readAllStandardError());
        if (!stdErrChunk.isEmpty())
        {
            if (capturedStderr)
                *capturedStderr += stdErrChunk;
            logTextBlock(prefix + " [stderr]", stdErrChunk, log);
        }
    }

    bool promptInternalLabel(ManualSeedSelector *parent,
                             const QStringList &choices,
                             int *selectedLabel)
    {
        if (!selectedLabel)
            return false;

        bool ok = false;
        const QString selected = QInputDialog::getItem(parent,
                                                       "Select Internal Label",
                                                       "Choose label to be INTERNAL:",
                                                       choices,
                                                       0,
                                                       false,
                                                       &ok);
        if (!ok)
            return false;

        *selectedLabel = selected.toInt();
        return true;
    }

    std::vector<double> buildPolaritySweepValues()
    {
        std::vector<double> values;
        values.reserve(21);
        for (int i = 0; i <= 20; ++i)
        {
            double value = -1.0 + 0.1 * i;
            double rounded = std::round(value * 10.0) / 10.0;
            if (std::abs(rounded) < 1e-4)
                rounded = 0.0;
            values.push_back(rounded);
        }
        return values;
    }

    QString safePolarityTag(double value)
    {
        QString tag = QString::number(value, 'f', 1);
        tag.replace("-", "neg");
        tag.replace(".", "_");
        return tag;
    }

    bool prepareSegmentationRequest(ManualSeedSelector *parent, SegmentationRequest *request)
    {
        if (!parent || !request)
            return false;

        const QString sourceImagePath = QFileInfo(QString::fromStdString(parent->getImagePath())).absoluteFilePath();
        if (sourceImagePath.isEmpty())
        {
            QMessageBox::warning(parent, "ROIFT", "Load an image before running segmentation.");
            return false;
        }

        const auto &seeds = parent->getSeeds();
        std::set<int> uniqueLabels;
        for (const auto &seed : seeds)
            uniqueLabels.insert(seed.label);

        if (uniqueLabels.empty())
        {
            QMessageBox::warning(parent, "ROIFT", "No seeds available for segmentation.");
            return false;
        }

        request->sourceImagePath = sourceImagePath;
        request->filteredSeeds = dedupeSeedsKeepingLatest(seeds);
        request->pol = parent->getPolarity();
        request->niter = parent->getNiter();
        request->percentile = parent->getPercentile();
        request->legacyBinaryMode = parent->useLegacyBinaryMode();
        request->windowLevel = parent->getWindowLevel();
        request->windowWidth = parent->getWindowWidth();

        const double imageMin = parent->getImageMin();
        const double imageMax = parent->getImageMax();
        const double defaultWL = (imageMax + imageMin) / 2.0;
        const double defaultWW = imageMax - imageMin;
        request->needsWindowing =
            (std::abs(request->windowLevel - defaultWL) > 1.0 ||
             std::abs(request->windowWidth - defaultWW) > 1.0);

        const bool doAll = parent->getSegmentAll();
        const bool polSweep = parent->getPolaritySweep();
        const QString baseDir = QFileInfo(sourceImagePath).absolutePath();
        const QStringList labelChoices = buildLabelChoices(uniqueLabels);

        const RoiftExecutable roiftExec = resolveRoiftExecutable(parent->getUseGPU());
        request->executablePath = roiftExec.path;
        if (request->executablePath.isEmpty())
        {
            QMessageBox::critical(parent, "ROIFT not found", roiftNotFoundMessage(parent->getUseGPU()));
            return false;
        }

        request->useGpuExecution = parent->getUseGPU() && roiftExec.gpuBinary;
        request->initialLogs << QString("Executable: %1").arg(request->executablePath);
        if (parent->getUseGPU() && !roiftExec.gpuBinary)
            request->initialLogs << QString("GPU requested, but oiftrelax_gpu was not found. Falling back to: %1").arg(request->executablePath);
        if (request->needsWindowing)
            request->initialLogs << QString("Applying current window/level before segmentation (WL=%1, WW=%2).")
                                        .arg(request->windowLevel, 0, 'f', 1)
                                        .arg(request->windowWidth, 0, 'f', 1);

        if (request->legacyBinaryMode && !doAll)
        {
            if (!promptInternalLabel(parent, labelChoices, &request->selectedInternalLabel))
                return false;
            request->initialLogs << QString("Internal label: %1").arg(request->selectedInternalLabel);
        }

        if (!doAll && !polSweep)
        {
            request->kind = SegmentationRequestKind::Single;
            request->initialMessage = request->legacyBinaryMode
                                          ? "Binary segmentation started in background."
                                          : "Multi-label segmentation started in background.";
            request->progressLabel = "Segmentation";

            QString outputPath = QFileDialog::getSaveFileName(parent,
                                                              "Save segmentation output",
                                                              baseDir,
                                                              "NIfTI files (*.nii *.nii.gz);;All files (*)");
            QCoreApplication::processEvents();
            if (outputPath.isEmpty())
                return false;
            if (!(outputPath.endsWith(".nii", Qt::CaseInsensitive) || outputPath.endsWith(".nii.gz", Qt::CaseInsensitive)))
                outputPath += ".nii.gz";

            request->outputPath = outputPath;
            request->initialLogs << QString("Output mask: %1").arg(outputPath);
            return true;
        }

        if (polSweep)
        {
            request->kind = SegmentationRequestKind::PolaritySweep;
            request->initialMessage = request->legacyBinaryMode
                                          ? "Binary polarity sweep started in background."
                                          : "Multi-label polarity sweep started in background.";
            request->progressLabel = "Polarity sweep";
            request->polarityValues = buildPolaritySweepValues();
            request->progressTotal = static_cast<int>(request->polarityValues.size());

            const QString outputDir = QFileDialog::getExistingDirectory(parent,
                                                                        "Select directory to save per-polarity segmentations",
                                                                        baseDir);
            QCoreApplication::processEvents();
            if (outputDir.isEmpty())
                return false;

            request->outputDir = outputDir;
            request->initialLogs << QString("Output directory: %1").arg(outputDir);
            return true;
        }

        request->kind = SegmentationRequestKind::BatchPerLabel;
        request->initialMessage = "Batch per-label segmentation started in background.";
        request->progressLabel = "Batch segmentation";

        const QString outputDir = QFileDialog::getExistingDirectory(parent,
                                                                    "Select directory to save per-label segmentations",
                                                                    baseDir);
        QCoreApplication::processEvents();
        if (outputDir.isEmpty())
            return false;

        QDialog skipDialog(parent);
        skipDialog.setWindowTitle("Select labels to skip");
        QVBoxLayout *skipLayout = new QVBoxLayout(&skipDialog);
        skipLayout->addWidget(new QLabel("Check labels to SKIP in batch segmentation:"));

        QListWidget *skipList = new QListWidget();
        for (int label : uniqueLabels)
        {
            QListWidgetItem *item = new QListWidgetItem(QString::number(label));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
            skipList->addItem(item);
        }
        skipLayout->addWidget(skipList);

        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        skipLayout->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &skipDialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &skipDialog, &QDialog::reject);

        if (skipDialog.exec() != QDialog::Accepted)
            return false;

        std::unordered_set<int> skipped;
        for (int i = 0; i < skipList->count(); ++i)
        {
            QListWidgetItem *item = skipList->item(i);
            if (item && item->checkState() == Qt::Checked)
                skipped.insert(item->text().toInt());
        }

        request->labels.clear();
        request->labels.reserve(uniqueLabels.size());
        for (int label : uniqueLabels)
        {
            if (skipped.find(label) == skipped.end())
                request->labels.push_back(label);
        }

        if (request->labels.empty())
        {
            QMessageBox::information(parent, "ROIFT", "No labels selected for segmentation after applying the skip list.");
            return false;
        }

        request->outputDir = outputDir;
        request->progressTotal = static_cast<int>(request->labels.size());
        request->initialLogs << QString("Output directory: %1").arg(outputDir);
        request->initialLogs << QString("Labels queued: %1").arg(request->progressTotal);
        return true;
    }

    QString prepareExecutionImagePath(const SegmentationRequest &request,
                                      ScopedFileCleanup *cleanup,
                                      const SegmentationExecutionCallbacks &callbacks)
    {
        QString imagePath = request.sourceImagePath;
        if (!request.needsWindowing)
            return imagePath;

        if (callbacks.log)
            callbacks.log("Creating temporary windowed image...");

        const QString windowedPath = createWindowedImage(request.sourceImagePath.toStdString(),
                                                         request.windowLevel,
                                                         request.windowWidth);
        if (windowedPath != request.sourceImagePath)
        {
            if (cleanup)
                cleanup->path = windowedPath;
            imagePath = windowedPath;
            if (callbacks.log)
                callbacks.log(QString("Windowed image ready: %1").arg(windowedPath));
        }
        else if (callbacks.log)
        {
            callbacks.log("Failed to create a temporary windowed image. Using the original image.");
        }

        return imagePath;
    }

    SegmentationExecutionResult executeSingleSegmentation(const SegmentationRequest &request,
                                                          const QString &imagePath,
                                                          const SegmentationExecutionCallbacks &callbacks)
    {
        SegmentationExecutionResult result;
        const QString seedFile = QDir::temp().filePath(request.legacyBinaryMode
                                                           ? "roift_seeds_binary_temp.txt"
                                                           : "roift_seeds_multilabel_temp.txt");
        ScopedFileCleanup seedCleanup{seedFile};

        const bool wroteSeedFile = request.legacyBinaryMode
                                       ? writeLegacySeedFile(seedFile, request.filteredSeeds, request.selectedInternalLabel)
                                       : writeMultilabelSeedFile(seedFile, request.filteredSeeds);
        if (!wroteSeedFile)
        {
            result.summary = "Failed to create the temporary seed file for segmentation.";
            return result;
        }

        if (callbacks.progress)
            callbacks.progress(request.progressLabel, -1, -1);

        QStringList args;
        args << imagePath
             << seedFile
             << QString::number(request.pol)
             << QString::number(request.niter)
             << QString::number(request.percentile)
             << request.outputPath;
        if (request.useGpuExecution)
            args << "--delta";

        if (callbacks.log)
            callbacks.log(QString("Running: %1").arg(quoteCommand(request.executablePath, args)));

        QProcess proc;
        proc.setProcessChannelMode(QProcess::SeparateChannels);
        proc.start(request.executablePath, args);
        if (!proc.waitForStarted(60000))
        {
            result.summary = "Failed to start the ROIFT executable.";
            return result;
        }

        QString capturedStdout;
        QString capturedStderr;
        while (proc.state() != QProcess::NotRunning)
        {
            proc.waitForFinished(200);
            drainProcessOutput(&proc, &capturedStdout, &capturedStderr, "ROIFT", callbacks.log);
        }
        drainProcessOutput(&proc, &capturedStdout, &capturedStderr, "ROIFT", callbacks.log);

        if (proc.exitCode() != 0)
        {
            result.summary = QString("Segmentation failed with exit code %1.").arg(proc.exitCode());
            return result;
        }

        if (!QFileInfo::exists(request.outputPath))
        {
            result.summary = QString("ROIFT finished, but the output mask was not created: %1").arg(request.outputPath);
            return result;
        }

        result.success = true;
        result.generatedMaskPaths << request.outputPath;
        result.summary = request.legacyBinaryMode
                             ? QString("Binary segmentation finished. Mask saved to %1").arg(request.outputPath)
                             : QString("Multi-label segmentation finished. Mask saved to %1").arg(request.outputPath);
        return result;
    }

    SegmentationExecutionResult executePolaritySweep(const SegmentationRequest &request,
                                                     const QString &imagePath,
                                                     const SegmentationExecutionCallbacks &callbacks)
    {
        SegmentationExecutionResult result;
        const QString seedFile = QDir::temp().filePath(request.legacyBinaryMode
                                                           ? "roift_seeds_polsweep_binary_temp.txt"
                                                           : "roift_seeds_polsweep_multilabel_temp.txt");
        ScopedFileCleanup seedCleanup{seedFile};

        const bool wroteSeedFile = request.legacyBinaryMode
                                       ? writeLegacySeedFile(seedFile, request.filteredSeeds, request.selectedInternalLabel)
                                       : writeMultilabelSeedFile(seedFile, request.filteredSeeds);
        if (!wroteSeedFile)
        {
            result.summary = "Failed to create the temporary seed file for the polarity sweep.";
            return result;
        }

        struct RunningPolarityProcess
        {
            double polValue = 0.0;
            QString outputPath;
            QString stdOut;
            QString stdErr;
            QProcess *proc = nullptr;
        };

        const int total = static_cast<int>(request.polarityValues.size());
        const int maxParallel = request.useGpuExecution
                                    ? 1
                                    : std::min(5, std::max(1, QThread::idealThreadCount()));

        std::vector<RunningPolarityProcess> running;
        std::vector<std::pair<double, QString>> successes;
        running.reserve(static_cast<size_t>(maxParallel));
        successes.reserve(request.polarityValues.size());

        int completed = 0;
        size_t nextIndex = 0;
        auto updateProgress = [&]()
        {
            if (callbacks.progress)
                callbacks.progress(request.progressLabel, completed, total);
        };

        auto startPolarityProcess = [&](double polValue) -> bool
        {
            const QString outputPath = QDir(request.outputDir).filePath(
                QString("segmentation_pol_%1.nii.gz").arg(safePolarityTag(polValue)));

            QStringList args;
            args << imagePath
                 << seedFile
                 << QString::number(polValue, 'f', 1)
                 << QString::number(request.niter)
                 << QString::number(request.percentile)
                 << outputPath;
            if (request.useGpuExecution)
                args << "--delta";

            QProcess *proc = new QProcess();
            proc->setProcessChannelMode(QProcess::SeparateChannels);
            if (callbacks.log)
                callbacks.log(QString("Running: %1").arg(quoteCommand(request.executablePath, args)));
            proc->start(request.executablePath, args);
            if (!proc->waitForStarted(60000))
            {
                if (callbacks.log)
                    callbacks.log(QString("Failed to start ROIFT for polarity %1.").arg(polValue, 0, 'f', 1));
                delete proc;
                ++completed;
                updateProgress();
                return false;
            }

            if (callbacks.log)
                callbacks.log(QString("Started polarity %1 -> %2").arg(polValue, 0, 'f', 1).arg(outputPath));

            running.push_back({polValue, outputPath, QString(), QString(), proc});
            return true;
        };

        updateProgress();
        while (nextIndex < request.polarityValues.size() && static_cast<int>(running.size()) < maxParallel)
        {
            startPolarityProcess(request.polarityValues[nextIndex]);
            ++nextIndex;
        }

        while (nextIndex < request.polarityValues.size() || !running.empty())
        {
            for (auto it = running.begin(); it != running.end();)
            {
                if (it->proc->state() != QProcess::NotRunning)
                    it->proc->waitForFinished(10);
                drainProcessOutput(it->proc, &it->stdOut, &it->stdErr,
                                   QString("pol=%1").arg(it->polValue, 0, 'f', 1),
                                   callbacks.log);

                if (it->proc->state() != QProcess::NotRunning)
                {
                    ++it;
                    continue;
                }

                ++completed;
                updateProgress();

                const int exitCode = it->proc->exitCode();
                if (exitCode == 0 && QFileInfo::exists(it->outputPath))
                {
                    if (callbacks.log)
                        callbacks.log(QString("Finished polarity %1 -> %2").arg(it->polValue, 0, 'f', 1).arg(it->outputPath));
                    successes.emplace_back(it->polValue, it->outputPath);
                }
                else
                {
                    if (callbacks.log)
                        callbacks.log(QString("Polarity %1 failed with exit code %2.")
                                          .arg(it->polValue, 0, 'f', 1)
                                          .arg(exitCode));
                }

                delete it->proc;
                it = running.erase(it);

                while (nextIndex < request.polarityValues.size() && static_cast<int>(running.size()) < maxParallel)
                {
                    startPolarityProcess(request.polarityValues[nextIndex]);
                    ++nextIndex;
                }
            }

            QThread::msleep(25);
        }

        for (const auto &success : successes)
            result.generatedMaskPaths << success.second;

        if (successes.empty())
        {
            result.summary = "Polarity sweep finished, but no output masks were generated successfully.";
            return result;
        }

        result.success = true;
        result.summary = QString("%1 polarity sweep finished with %2 successful output(s). Masks saved in %3")
                             .arg(request.legacyBinaryMode ? "Binary" : "Multi-label")
                             .arg(successes.size())
                             .arg(request.outputDir);
        return result;
    }

    SegmentationExecutionResult executeBatchSegmentation(const SegmentationRequest &request,
                                                         const QString &imagePath,
                                                         const SegmentationExecutionCallbacks &callbacks)
    {
        SegmentationExecutionResult result;

        struct RunningLabelProcess
        {
            int label = 0;
            QString seedPath;
            QString outputPath;
            QString stdOut;
            QString stdErr;
            QProcess *proc = nullptr;
        };

        const int total = static_cast<int>(request.labels.size());
        const int maxParallel = request.useGpuExecution
                                    ? 1
                                    : std::min(5, std::max(1, QThread::idealThreadCount()));

        std::vector<RunningLabelProcess> running;
        std::vector<std::pair<int, QString>> successfulOutputs;
        running.reserve(static_cast<size_t>(maxParallel));
        successfulOutputs.reserve(request.labels.size());

        int completed = 0;
        size_t nextIndex = 0;
        auto updateProgress = [&]()
        {
            if (callbacks.progress)
                callbacks.progress(request.progressLabel, completed, total);
        };

        auto startLabelProcess = [&](int label) -> bool
        {
            const QString seedPath = QDir::temp().filePath(QString("roift_seeds_label%1_temp.txt").arg(label));
            if (!writeLegacySeedFile(seedPath, request.filteredSeeds, label))
            {
                if (callbacks.log)
                    callbacks.log(QString("Failed to create the temporary seed file for label %1.").arg(label));
                ++completed;
                updateProgress();
                return false;
            }

            const QString outputPath = QDir(request.outputDir).filePath(QString("segmentation_label%1.nii.gz").arg(label));
            QStringList args;
            args << imagePath
                 << seedPath
                 << QString::number(request.pol)
                 << QString::number(request.niter)
                 << QString::number(request.percentile)
                 << outputPath;
            if (request.useGpuExecution)
                args << "--delta";

            QProcess *proc = new QProcess();
            proc->setProcessChannelMode(QProcess::SeparateChannels);
            if (callbacks.log)
                callbacks.log(QString("Running: %1").arg(quoteCommand(request.executablePath, args)));
            proc->start(request.executablePath, args);
            if (!proc->waitForStarted(60000))
            {
                if (callbacks.log)
                    callbacks.log(QString("Failed to start ROIFT for label %1.").arg(label));
                QFile::remove(seedPath);
                delete proc;
                ++completed;
                updateProgress();
                return false;
            }

            if (callbacks.log)
                callbacks.log(QString("Started label %1 -> %2").arg(label).arg(outputPath));

            running.push_back({label, seedPath, outputPath, QString(), QString(), proc});
            return true;
        };

        updateProgress();
        while (nextIndex < request.labels.size() && static_cast<int>(running.size()) < maxParallel)
        {
            startLabelProcess(request.labels[nextIndex]);
            ++nextIndex;
        }

        while (nextIndex < request.labels.size() || !running.empty())
        {
            for (auto it = running.begin(); it != running.end();)
            {
                if (it->proc->state() != QProcess::NotRunning)
                    it->proc->waitForFinished(10);
                drainProcessOutput(it->proc, &it->stdOut, &it->stdErr,
                                   QString("label=%1").arg(it->label),
                                   callbacks.log);

                if (it->proc->state() != QProcess::NotRunning)
                {
                    ++it;
                    continue;
                }

                ++completed;
                updateProgress();

                const int exitCode = it->proc->exitCode();
                if (exitCode == 0 && QFileInfo::exists(it->outputPath))
                {
                    if (callbacks.log)
                        callbacks.log(QString("Finished label %1 -> %2").arg(it->label).arg(it->outputPath));
                    successfulOutputs.emplace_back(it->label, it->outputPath);
                    result.generatedMaskPaths << it->outputPath;
                }
                else
                {
                    if (callbacks.log)
                        callbacks.log(QString("Label %1 failed with exit code %2.").arg(it->label).arg(exitCode));
                }

                QFile::remove(it->seedPath);
                delete it->proc;
                it = running.erase(it);

                while (nextIndex < request.labels.size() && static_cast<int>(running.size()) < maxParallel)
                {
                    startLabelProcess(request.labels[nextIndex]);
                    ++nextIndex;
                }
            }

            QThread::msleep(25);
        }

        if (successfulOutputs.empty())
        {
            result.summary = "Batch segmentation finished, but no per-label outputs were generated successfully.";
            return result;
        }

        try
        {
            using PixelType = int32_t;
            using ImageType = itk::Image<PixelType, 3>;

            if (callbacks.log)
                callbacks.log("Merging successful per-label outputs into a multilabel mask...");

            itk::ImageFileReader<ImageType>::Pointer reader0 = itk::ImageFileReader<ImageType>::New();
            reader0->SetFileName(successfulOutputs.front().second.toStdString());
            reader0->Update();
            ImageType::Pointer base = reader0->GetOutput();
            ImageType::RegionType region = base->GetLargestPossibleRegion();

            ImageType::Pointer outputImage = ImageType::New();
            outputImage->SetRegions(region);
            outputImage->SetSpacing(base->GetSpacing());
            outputImage->SetOrigin(base->GetOrigin());
            outputImage->SetDirection(base->GetDirection());
            outputImage->Allocate();
            outputImage->FillBuffer(0);

            for (const auto &success : successfulOutputs)
            {
                itk::ImageFileReader<ImageType>::Pointer reader = itk::ImageFileReader<ImageType>::New();
                reader->SetFileName(success.second.toStdString());
                reader->Update();
                ImageType::Pointer image = reader->GetOutput();

                itk::ImageRegionIterator<ImageType> srcIt(image, image->GetLargestPossibleRegion());
                itk::ImageRegionIterator<ImageType> dstIt(outputImage, outputImage->GetLargestPossibleRegion());
                for (srcIt.GoToBegin(), dstIt.GoToBegin(); !srcIt.IsAtEnd(); ++srcIt, ++dstIt)
                {
                    const PixelType value = srcIt.Get();
                    if (value != 0)
                        dstIt.Set(static_cast<PixelType>(success.first));
                }
            }

            const QString mergedPath = QDir(request.outputDir).filePath("segmentation_multilabel.nii.gz");
            using WriterType = itk::ImageFileWriter<ImageType>;
            WriterType::Pointer writer = WriterType::New();
            writer->SetFileName(mergedPath.toStdString());
            writer->SetInput(outputImage);
            writer->Update();

            if (QFileInfo::exists(mergedPath))
            {
                result.generatedMaskPaths << mergedPath;
                result.success = true;
                result.summary = QString("Batch segmentation finished with %1 successful label(s). Merged mask saved to %2")
                                     .arg(successfulOutputs.size())
                                     .arg(mergedPath);
            }
            else
            {
                result.success = true;
                result.summary = QString("Batch segmentation finished with %1 successful label(s), but the merged mask was not written.")
                                     .arg(successfulOutputs.size());
            }
        }
        catch (const std::exception &e)
        {
            result.success = true;
            result.summary = QString("Batch segmentation finished with %1 successful label(s), but the merge failed: %2")
                                 .arg(successfulOutputs.size())
                                 .arg(e.what());
        }

        return result;
    }

    SegmentationExecutionResult executeSegmentationRequest(const SegmentationRequest &request,
                                                          const SegmentationExecutionCallbacks &callbacks)
    {
        ScopedFileCleanup windowedImageCleanup;
        const QString imagePath = prepareExecutionImagePath(request, &windowedImageCleanup, callbacks);

        switch (request.kind)
        {
        case SegmentationRequestKind::Single:
            return executeSingleSegmentation(request, imagePath, callbacks);
        case SegmentationRequestKind::PolaritySweep:
            return executePolaritySweep(request, imagePath, callbacks);
        case SegmentationRequestKind::BatchPerLabel:
            return executeBatchSegmentation(request, imagePath, callbacks);
        }

        SegmentationExecutionResult result;
        result.summary = "Invalid segmentation request.";
        return result;
    }

    void runPreparedSegmentation(ManualSeedSelector *parent, const SegmentationRequest &request)
    {
        if (!parent)
            return;

        const SegmentationExecutionCallbacks callbacks{
            [parent](const QString &message)
            {
                QMetaObject::invokeMethod(parent,
                                          [parent, message]()
                                          { parent->appendSegmentationLog(message); },
                                          Qt::QueuedConnection);
            },
            [parent](const QString &message, int done, int total)
            {
                QMetaObject::invokeMethod(parent,
                                          [parent, message, done, total]()
                                          { parent->setSegmentationTaskProgress(message, done, total); },
                                          Qt::QueuedConnection);
            }};

        SegmentationExecutionResult result;
        try
        {
            result = executeSegmentationRequest(request, callbacks);
        }
        catch (const std::exception &e)
        {
            result.success = false;
            result.summary = QString("Segmentation failed with an unexpected error: %1").arg(e.what());
        }
        catch (...)
        {
            result.success = false;
            result.summary = "Segmentation failed with an unknown error.";
        }

        QMetaObject::invokeMethod(parent,
                                  [parent, result, request]()
                                  {
                                      parent->completeSegmentationTask(result.success,
                                                                       result.summary,
                                                                       request.sourceImagePath,
                                                                       result.generatedMaskPaths);
                                  },
                                  Qt::QueuedConnection);
    }
}

void SegmentationRunner::runSegmentation(ManualSeedSelector *parent)
{
    if (!parent)
        return;

    SegmentationRequest request;
    if (!prepareSegmentationRequest(parent, &request))
        return;

    if (!parent->startSegmentationTask([parent, request]()
                                       { runPreparedSegmentation(parent, request); },
                                       request.initialMessage,
                                       request.initialLogs,
                                       request.progressLabel,
                                       request.progressTotal))
    {
        return;
    }
}
