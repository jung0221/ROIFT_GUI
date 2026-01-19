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
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QDebug>
#include <QThread>
#include <QFile>
#include <QTime>
#include <algorithm>
#include <cmath>
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
        QDir appDir(QCoreApplication::applicationDirPath());
        QStringList candidates;
        candidates << appDir.filePath("../roift/oiftrelax_gpu.exe");
        candidates << QDir::current().filePath("build/roift/Release/oiftrelax_gpu.exe");
        candidates << QDir::current().filePath("build/bin/Release/oiftrelax_gpu.exe");
        candidates << QDir::current().filePath("build/roift/oiftrelax_gpu");
        candidates << appDir.filePath("roift/oiftrelax_gpu");
        candidates << appDir.filePath("../roift/oiftrelax_gpu");
        candidates << QDir::current().filePath("build/roift/Release/oiftrelax_gpu");
        candidates << QDir::current().filePath("build/bin/Release/oiftrelax_gpu");
        QString exePath;
        for (auto &c : candidates)
        {
            if (QFile::exists(c))
            {
                exePath = c;
                break;
            }
        }
        if (exePath.isEmpty())
        {
            QMessageBox::critical(parent, "ROIFT not found", "Could not find external ROIFT executable.");
            return;
        }

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
            bool useGPU = parent->getUseGPU();
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
            if (parent->getUseGPU())
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
    QDir appDir(QCoreApplication::applicationDirPath());
    QStringList candidates;
    candidates << appDir.filePath("../roift/oiftrelax_gpu.exe");
    candidates << QDir::current().filePath("build/roift/Release/oiftrelax_gpu.exe");
    candidates << QDir::current().filePath("build/bin/Release/oiftrelax_gpu.exe");
    candidates << QDir::current().filePath("build/roift/oiftrelax_gpu");
    candidates << appDir.filePath("roift/oiftrelax_gpu");
    candidates << appDir.filePath("../roift/oiftrelax_gpu");
    candidates << QDir::current().filePath("build/roift/Release/oiftrelax_gpu");
    candidates << QDir::current().filePath("build/bin/Release/oiftrelax_gpu");
    QString exePath;
    for (auto &c : candidates)
    {
        if (QFile::exists(c))
        {
            exePath = c;
            break;
        }
    }
    if (exePath.isEmpty())
    {
        QMessageBox::critical(parent, "ROIFT not found", "Could not find external ROIFT executable.");
        return;
    }

    // Parallel execution: schedule up to N concurrent oiftrelax invocations
    // Cap concurrency to at most 10 processes at once to avoid overloading the system
    // GPU can only handle one segmentation at a time
    bool useGPU = parent->getUseGPU();
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

void SegmentationRunner::runSegmentation(ManualSeedSelector *parent)
{
    qDebug() << "[TIMER] runSegmentation START" << QTime::currentTime().toString("hh:mm:ss.zzz");

    if (!parent)
        return;

    // Gather seeds and build deduped list
    qDebug() << "[TIMER] After initial checks" << QTime::currentTime().toString("hh:mm:ss.zzz");
    const auto &seeds = parent->getSeeds();
    std::set<int> uniq;
    for (const auto &s : seeds)
        uniq.insert(s.label);
    if (uniq.empty())
    {
        QMessageBox::warning(parent, "ROIFT", "No seeds available for segmentation");
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

    // Get parameters from UI
    double pol = parent->getPolarity();
    int niter = parent->getNiter();
    int percentile = parent->getPercentile();
    bool doAll = parent->getSegmentAll();
    bool polSweep = parent->getPolaritySweep() && !doAll;
    bool useGPU = parent->getUseGPU();
    double windowLevel = parent->getWindowLevel();
    double windowWidth = parent->getWindowWidth();

    qDebug() << "[TIMER] After getting UI parameters" << QTime::currentTime().toString("hh:mm:ss.zzz");

    // Get image default values to check if windowing is needed
    double imageMin = parent->getImageMin();
    double imageMax = parent->getImageMax();
    double defaultWL = (imageMax + imageMin) / 2.0;
    double defaultWW = imageMax - imageMin;

    // Create windowed image only if window parameters differ from image defaults
    QString imagePath;
    QString windowedImagePath;
    bool needsWindowing = (std::abs(windowLevel - defaultWL) > 1.0 || std::abs(windowWidth - defaultWW) > 1.0);

    if (needsWindowing)
    {
        qDebug() << "[TIMER] Before creating windowed image" << QTime::currentTime().toString("hh:mm:ss.zzz");
        windowedImagePath = createWindowedImage(parent->getImagePath(), windowLevel, windowWidth);
        imagePath = windowedImagePath;
        qDebug() << "[TIMER] After creating windowed image" << QTime::currentTime().toString("hh:mm:ss.zzz");
    }
    else
    {
        // Use original image directly
        imagePath = QString::fromStdString(parent->getImagePath());
        qDebug() << "[TIMER] Using original image (no windowing needed)" << QTime::currentTime().toString("hh:mm:ss.zzz");
    }

    QString baseDir = QFileInfo(QString::fromStdString(parent->getImagePath())).absolutePath();

    if (!doAll && !polSweep)
    {
        // Single segmentation run
        bool ok = false;
        QString sel = QInputDialog::getItem(parent, "Select Internal Label", "Choose label to be INTERNAL:", choices, 0, false, &ok);
        if (!ok)
            return;
        int internal_label = sel.toInt();

        // Create temporary seed file
        QString seedFile = QDir::temp().filePath("roift_seeds_temp.txt");
        std::ofstream ofs(seedFile.toStdString());
        ofs << filtered.size() << "\n";
        for (const auto &s : filtered)
        {
            int internal_flag = (s.label == internal_label) ? 1 : 0;
            ofs << s.x << " " << s.y << " " << s.z << " " << s.label << " " << internal_flag << "\n";
        }
        ofs.close();

        QString outQ = QFileDialog::getSaveFileName(parent, "Save segmentation output", baseDir, "NIfTI files (*.nii *.nii.gz);;All files (*)");
        QCoreApplication::processEvents();
        if (outQ.isEmpty())
            return;
        QString outp = outQ;
        if (!(outp.endsWith(".nii", Qt::CaseInsensitive) || outp.endsWith(".nii.gz", Qt::CaseInsensitive)))
            outp += ".nii.gz";

        QDir appDir(QCoreApplication::applicationDirPath());
        QStringList candidates;
        candidates << appDir.filePath("../roift/oiftrelax_gpu.exe");
        candidates << QDir::current().filePath("build/roift/Release/oiftrelax_gpu.exe");
        candidates << QDir::current().filePath("build/bin/Release/oiftrelax_gpu.exe");
        candidates << QDir::current().filePath("build/roift/oiftrelax_gpu");
        candidates << appDir.filePath("roift/oiftrelax_gpu");
        candidates << appDir.filePath("../roift/oiftrelax_gpu");
        candidates << QDir::current().filePath("build/roift/Release/oiftrelax_gpu");
        candidates << QDir::current().filePath("build/bin/Release/oiftrelax_gpu");
        QString exePath;
        for (auto &c : candidates)
        {
            if (QFile::exists(c))
            {
                exePath = c;
                break;
            }
        }
        if (exePath.isEmpty())
        {
            QMessageBox::critical(parent, "ROIFT not found", "Could not find external ROIFT executable.");
            return;
        }

        QProcess proc;
        QStringList args;
        args << imagePath << seedFile << QString::number(pol) << QString::number(niter) << QString::number(percentile) << outp;
        if (parent->getUseGPU())
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
            QFile::remove(seedFile);
            return;
        }
        bool finished = proc.waitForFinished(-1);
        QString procStdout = proc.readAllStandardOutput();
        QString procStderr = proc.readAllStandardError();

        // Clean up temp file
        QFile::remove(seedFile);

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
    else if (polSweep)
    {
        // Polarity sweep - ask for internal label
        bool ok = false;
        QString sel = QInputDialog::getItem(parent, "Select Internal Label", "Choose label to be INTERNAL:", choices, 0, false, &ok);
        if (!ok)
            return;
        int internal_label = sel.toInt();

        // Create temporary seed file
        QString seedFile = QDir::temp().filePath("roift_seeds_polsweep_temp.txt");
        std::ofstream ofs(seedFile.toStdString());
        ofs << filtered.size() << "\n";
        for (const auto &s : filtered)
        {
            int internal_flag = (s.label == internal_label) ? 1 : 0;
            ofs << s.x << " " << s.y << " " << s.z << " " << s.label << " " << internal_flag << "\n";
        }
        ofs.close();

        QString outDir = QFileDialog::getExistingDirectory(parent, "Select directory to save per-polarity segmentations", baseDir);
        QCoreApplication::processEvents();
        if (outDir.isEmpty())
        {
            QFile::remove(seedFile);
            return;
        }

        std::vector<double> polValues;
        polValues.reserve(21);
        for (int i = 0; i <= 20; ++i)
        {
            double v = -1.0 + 0.1 * i;
            double rounded = std::round(v * 10.0) / 10.0;
            if (std::abs(rounded) < 1e-4)
                rounded = 0.0;
            polValues.push_back(rounded);
        }

        QDir appDir(QCoreApplication::applicationDirPath());
        QStringList candidates;
        candidates << appDir.filePath("../roift/oiftrelax_gpu.exe");
        candidates << QDir::current().filePath("build/roift/Release/oiftrelax_gpu.exe");
        candidates << QDir::current().filePath("build/bin/Release/oiftrelax_gpu.exe");
        candidates << QDir::current().filePath("build/roift/oiftrelax_gpu");
        candidates << appDir.filePath("roift/oiftrelax_gpu");
        candidates << appDir.filePath("../roift/oiftrelax_gpu");
        candidates << QDir::current().filePath("build/roift/Release/oiftrelax_gpu");
        candidates << QDir::current().filePath("build/bin/Release/oiftrelax_gpu");
        QString exePath;
        for (auto &c : candidates)
        {
            if (QFile::exists(c))
            {
                exePath = c;
                break;
            }
        }
        if (exePath.isEmpty())
        {
            QMessageBox::critical(parent, "ROIFT not found", "Could not find external ROIFT executable.");
            QFile::remove(seedFile);
            return;
        }

        // GPU can only handle one segmentation at a time
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
            argsPol << imagePath << seedFile << QString::number(polVal, 'f', 1) << QString::number(niter) << QString::number(percentile) << outPol;
            if (parent->getUseGPU())
                argsPol << "--delta";
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

        // Clean up temp file
        QFile::remove(seedFile);

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
        // Batch per-label segmentation
        QString outDir = QFileDialog::getExistingDirectory(parent, "Select directory to save per-label segmentations", baseDir);
        QCoreApplication::processEvents();
        if (outDir.isEmpty())
            return;

        QDir appDir(QCoreApplication::applicationDirPath());
        QStringList candidates;
        candidates << appDir.filePath("../roift/oiftrelax_gpu.exe");
        candidates << QDir::current().filePath("build/roift/Release/oiftrelax_gpu.exe");
        candidates << QDir::current().filePath("build/bin/Release/oiftrelax_gpu.exe");
        candidates << QDir::current().filePath("build/roift/oiftrelax_gpu");
        candidates << appDir.filePath("roift/oiftrelax_gpu");
        candidates << appDir.filePath("../roift/oiftrelax_gpu");
        candidates << QDir::current().filePath("build/roift/Release/oiftrelax_gpu");
        candidates << QDir::current().filePath("build/bin/Release/oiftrelax_gpu");
        QString exePath;
        for (auto &c : candidates)
        {
            if (QFile::exists(c))
            {
                exePath = c;
                break;
            }
        }
        if (exePath.isEmpty())
        {
            QMessageBox::critical(parent, "ROIFT not found", "Could not find external ROIFT executable.");
            return;
        }

        // GPU can only handle one segmentation at a time
        int maxParallel = useGPU ? 1 : std::min(5, std::max(1, QThread::idealThreadCount()));

        qDebug() << "[DEBUG] Showing skip labels dialog...";
        
        // Show dialog to select labels to skip
        QDialog skipDialog(parent);
        skipDialog.setWindowTitle("Select labels to skip");
        QVBoxLayout *skipLayout = new QVBoxLayout(&skipDialog);
        skipLayout->addWidget(new QLabel("Check labels to SKIP in batch segmentation:"));

        QListWidget *skipList = new QListWidget();
        for (int v : uniq)
        {
            QListWidgetItem *it = new QListWidgetItem(QString::number(v));
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(Qt::Unchecked);
            skipList->addItem(it);
        }
        skipLayout->addWidget(skipList);

        QDialogButtonBox *skipBB = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        skipLayout->addWidget(skipBB);
        QObject::connect(skipBB, &QDialogButtonBox::accepted, &skipDialog, &QDialog::accept);
        QObject::connect(skipBB, &QDialogButtonBox::rejected, &skipDialog, &QDialog::reject);

        qDebug() << "[DEBUG] About to exec skip dialog...";
        if (skipDialog.exec() != QDialog::Accepted)
        {
            qDebug() << "[DEBUG] Skip dialog cancelled";
            return;
        }
        qDebug() << "[DEBUG] Skip dialog accepted";
        
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
        auto startLabelProc = [&](int label) -> bool
        {
            QString perSeed = QDir::temp().filePath(QString("roift_seeds_label%1_temp.txt").arg(label));
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

            QProcess *proc2 = new QProcess();
            QStringList args2;
            args2 << imagePath << perSeed << QString::number(pol) << QString::number(niter) << QString::number(percentile) << outp2;
            if (parent->getUseGPU())
                args2 << "--delta";
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
                QFile::remove(perSeed);
                delete proc2;
                return false;
            }
            running.push_back({label, perSeed, outp2, proc2});
            return true;
        };

        while (nextIndex < labels.size() && (int)running.size() < maxParallel)
        {
            startLabelProc(labels[nextIndex]);
            ++nextIndex;
        }

        while (nextIndex < labels.size() || !running.empty())
        {
            QCoreApplication::processEvents();
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
                    if (p->state() != QProcess::NotRunning)
                    {
                        p->kill();
                        p->waitForFinished(1000);
                    }
                    // Clean up temp seed file
                    QFile::remove(it->perSeed);
                    delete p;
                    it = running.erase(it);
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
            QThread::msleep(50);
        }

        if (!successfulOutputs.empty())
        {
            using PixelType = int32_t;
            using ImageType = itk::Image<PixelType, 3>;
            try
            {
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

    // Clean up temporary windowed image if it was created
    if (needsWindowing && !windowedImagePath.isEmpty())
    {
        QFile::remove(windowedImagePath);
    }
}
