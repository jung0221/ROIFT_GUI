#include "ManualSeedSelector.h"
#include "SegmentationRunner.h"
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QApplication>
#include <QCursor>
#include <QPushButton>
#include <QFileDialog>
#include <QLabel>
#include <QImage>
#include <QBuffer>
#include <QByteArray>
#include <QPainter>
#include <QInputDialog>
#include <QMessageBox>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <algorithm>
#if HAVE_ITK
#include <itkImage.h>
#include <itkImageFileReader.h>
#include <itkImageRegionConstIterator.h>
#include <itkImageRegionIterator.h>
#include <itkImageFileWriter.h>
#include <itkNiftiImageIO.h>
#endif
#include <zlib.h>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QSlider>
#include <QKeyEvent>
#include <fstream>
#include <iostream>
#include <set>
#include <unordered_set>
#include <filesystem>
#include <QCoreApplication>
#include <QDir>
#include <QProcess>

// deterministic color map for integer labels that avoids accidental equal RGB
static QColor colorForLabel(int lbl) {
    // clamp to a sensible range
    int v = std::max(1, std::min(254, lbl));
    // use three different multipliers and a prime modulus to reduce collisions
    const int MOD = 251; // prime < 256
    int r = (v * 67) % MOD;
    int g = (v * 131) % MOD;
    int b = (v * 199) % MOD;
    // scale to 0-255 range
    int sr = (r * 255) / MOD;
    int sg = (g * 255) / MOD;
    int sb = (b * 255) / MOD;
    return QColor(sr, sg, sb);
}

ManualSeedSelector::ManualSeedSelector(const std::string &niftiPath, QWidget *parent) : QMainWindow(parent), m_path(niftiPath)
{
    setupUi();
    if (!niftiPath.empty())
    {
        if (!m_image.load(niftiPath))
        {
            std::cerr << "Failed to load " << niftiPath << std::endl;
        }
        else {
            // Clear any previously loaded mask data when a new image is loaded
            // to avoid index out-of-range accesses if the new image has
            // different dimensions than the previous one.
            if (!m_maskData.empty()) {
                std::cerr << "ManualSeedSelector: clearing existing mask buffer due to new image load" << std::endl;
                m_maskData.clear();
            }
            // initialize sliders and ranges for the loaded image
            initializeImageWidgets();
        }
    }
    updateViews();
}

void ManualSeedSelector::initializeImageWidgets()
{
    // Prevent emitting valueChanged while we initialize ranges/values to avoid
    // re-entrant calls into updateViews() while image buffers may not be ready.
    m_axialSlider->blockSignals(true);
    m_sagittalSlider->blockSignals(true);
    m_coronalSlider->blockSignals(true);

    m_axialSlider->setMinimum(0);
    m_axialSlider->setMaximum(int(m_image.getSizeZ()) - 1);
    m_axialSlider->setValue(int(m_image.getSizeZ()) / 2);
    m_sagittalSlider->setMinimum(0);
    m_sagittalSlider->setMaximum(int(m_image.getSizeX()) - 1);
    m_sagittalSlider->setValue(int(m_image.getSizeX()) / 2);
    m_coronalSlider->setMinimum(0);
    m_coronalSlider->setMaximum(int(m_image.getSizeY()) - 1);
    m_coronalSlider->setValue(int(m_image.getSizeY()) / 2);

    m_axialSlider->blockSignals(false);
    m_sagittalSlider->blockSignals(false);
    m_coronalSlider->blockSignals(false);
}

ManualSeedSelector::~ManualSeedSelector()
{
    // Clear large buffers and release image resources explicitly when the
    // window is destroyed to avoid holding memory if the app keeps running
    // or when reloading images.
    m_maskData.clear();
    m_seeds.clear();
    // Reset NiftiImage to release any ITK pointers it holds.
    m_image = NiftiImage();
}

void ManualSeedSelector::setupUi()
{
    QWidget *central = new QWidget();
    setCentralWidget(central);
    QVBoxLayout *main = new QVBoxLayout(central);

    QHBoxLayout *imgRow = new QHBoxLayout();
    m_axialView = new OrthogonalView();
    m_sagittalView = new OrthogonalView();
    m_coronalView = new OrthogonalView();
    // Ensure views can receive focus and key events; install event filter
    m_axialView->setFocusPolicy(Qt::StrongFocus);
    m_sagittalView->setFocusPolicy(Qt::StrongFocus);
    m_coronalView->setFocusPolicy(Qt::StrongFocus);
    m_axialView->installEventFilter(this);
    m_sagittalView->installEventFilter(this);
    m_coronalView->installEventFilter(this);
    // prefer larger default sizes
    m_axialView->setMinimumSize(512, 512);
    m_sagittalView->setMinimumSize(300, 512);
    m_coronalView->setMinimumSize(300, 512);
    imgRow->addWidget(m_axialView);
    imgRow->addWidget(m_sagittalView);
    imgRow->addWidget(m_coronalView);
    main->addLayout(imgRow);

    QHBoxLayout *btnRow = new QHBoxLayout();
    QPushButton *btnNiftiOptions = new QPushButton("NIfTI Options");
    m_btnUndoThreshold = new QPushButton("Undo Threshold");
    m_btnUndoThreshold->setEnabled(false);
    QPushButton *btnSegment = new QPushButton("Segment (ROIFT)");
    QPushButton *btnSeedOptions = new QPushButton("Seed Options");
    btnRow->addWidget(btnNiftiOptions);
    btnRow->addWidget(m_btnUndoThreshold);
    btnRow->addWidget(btnSegment);
    btnRow->addWidget(btnSeedOptions);
    QPushButton *btnMaskOptions = new QPushButton("Mask Options");
    btnRow->addWidget(btnMaskOptions);
    btnRow->addWidget(new QLabel("Label:"));
    m_labelSelector = new QSpinBox();
    m_labelSelector->setRange(1, 255);
    btnRow->addWidget(m_labelSelector);
    // color indicator square for the currently selected label
    m_labelColorIndicator = new QLabel();
    m_labelColorIndicator->setFixedSize(20, 20);
    m_labelColorIndicator->setFrameStyle(QFrame::Box | QFrame::Plain);
    btnRow->addWidget(m_labelColorIndicator);
    main->addLayout(btnRow);

    m_axialSlider = new QSlider(Qt::Horizontal);
    m_sagittalSlider = new QSlider(Qt::Horizontal);
    m_coronalSlider = new QSlider(Qt::Horizontal);
    // Reset zoom button (replaces zoom slider)
    QPushButton *btnResetZoom = new QPushButton("Reset Zoom");
    connect(btnResetZoom, &QPushButton::clicked, [this]()
            { m_axialView->resetView(); m_sagittalView->resetView(); m_coronalView->resetView(); });
    main->addWidget(btnResetZoom);
    main->addWidget(new QLabel("Axial"));
    main->addWidget(m_axialSlider);
    main->addWidget(new QLabel("Sagittal"));
    main->addWidget(m_sagittalSlider);
    main->addWidget(new QLabel("Coronal"));
    main->addWidget(m_coronalSlider);

    // status label showing current mouse x,y,z and intensity
    m_statusLabel = new QLabel("x: - y: - z: - val: -");
    main->addWidget(m_statusLabel);

    // NIfTI Options dialog: Open / Save / Threshold
    connect(btnNiftiOptions, &QPushButton::clicked, this, [this]() {
        QDialog dlg(this);
        dlg.setWindowTitle("NIfTI Options");
        QVBoxLayout *v = new QVBoxLayout(&dlg);
        QHBoxLayout *h1 = new QHBoxLayout();
        QPushButton *openBtn = new QPushButton("Open");
        QPushButton *saveBtn = new QPushButton("Save");
        h1->addWidget(openBtn);
        h1->addWidget(saveBtn);
        v->addLayout(h1);

        // Threshold controls
        QHBoxLayout *thl = new QHBoxLayout();
        thl->addWidget(new QLabel("Threshold >"));
        QDoubleSpinBox *thrSpin = new QDoubleSpinBox();
        thrSpin->setRange(-1e6, 1e6);
        thrSpin->setValue(200.0);
        thrSpin->setDecimals(2);
        thl->addWidget(thrSpin);
        thl->addWidget(new QLabel("Set to"));
        QDoubleSpinBox *setSpin = new QDoubleSpinBox();
        setSpin->setRange(-1e6, 1e6);
        setSpin->setValue(500.0);
        setSpin->setDecimals(2);
        thl->addWidget(setSpin);
        QPushButton *applyThr = new QPushButton("Apply Threshold");
        v->addLayout(thl);
        v->addWidget(applyThr);

        connect(openBtn, &QPushButton::clicked, this, &ManualSeedSelector::openImage);
        connect(saveBtn, &QPushButton::clicked, this, [this]() {
            QString f = QFileDialog::getSaveFileName(this, "Save NIfTI", "", "NIfTI files (*.nii *.nii.gz)");
            if (!f.isEmpty()) {
                if (!saveImageToFile(f.toStdString()))
                    QMessageBox::warning(this, "Save NIfTI", "Failed to save image.");
            }
        });

        connect(applyThr, &QPushButton::clicked, this, [this, thrSpin, setSpin, &dlg]() {
            double thr = thrSpin->value();
            double tgt = setSpin->value();
            if (m_image.getSizeX() == 0) {
                QMessageBox::warning(&dlg, "Threshold", "No image loaded.");
                return;
            }
            // create a backup copy so the threshold operation can be undone
            m_imageBackup = m_image.deepCopy();
            m_hasImageBackup = true;
            if (m_btnUndoThreshold) m_btnUndoThreshold->setEnabled(true);
            m_image.applyThreshold(static_cast<float>(thr), static_cast<float>(tgt));
            updateViews();
            QMessageBox::information(&dlg, "Threshold", "Threshold applied.");
        });

        // allow undo from within the dialog as well
        connect(m_btnUndoThreshold, &QPushButton::clicked, this, [this]() {
            if (!m_hasImageBackup) return;
            m_image = m_imageBackup.deepCopy();
            m_hasImageBackup = false;
            if (m_btnUndoThreshold) m_btnUndoThreshold->setEnabled(false);
            updateViews();
        });

        dlg.exec();
    });
    // instantiate dialogs and hook signals
    m_seedDialog = new SeedOptionsDialog(this);
    m_maskDialog = new MaskOptionsDialog(this);
    connect(btnSeedOptions, &QPushButton::clicked, m_seedDialog, &QDialog::exec);
    connect(btnMaskOptions, &QPushButton::clicked, m_maskDialog, &QDialog::exec);

    connect(m_seedDialog, &SeedOptionsDialog::modeChanged, this, [this](int m)
            { m_seedMode = m; });
    connect(m_seedDialog, &SeedOptionsDialog::cleared, this, [this]()
            { m_seeds.clear(); updateViews(); });
    connect(m_seedDialog, &SeedOptionsDialog::saveRequested, this, &ManualSeedSelector::saveSeeds);
    connect(m_seedDialog, &SeedOptionsDialog::loadRequested, this, &ManualSeedSelector::loadSeeds);
    connect(m_seedDialog, &SeedOptionsDialog::brushRadiusChanged, this, [this](int r)
            { m_seedBrushRadius = r; });

    connect(m_maskDialog, &MaskOptionsDialog::modeChanged, this, &ManualSeedSelector::setMaskMode);
    connect(m_maskDialog, &MaskOptionsDialog::loadMaskRequested, this, [this]()
            { QString f = QFileDialog::getOpenFileName(this, "Open Mask", "", "NIfTI files (*.nii *.nii.gz)"); if(!f.isEmpty()){ loadMaskFromFile(f.toStdString()); updateViews(); } });
    connect(m_maskDialog, &MaskOptionsDialog::saveMaskRequested, this, [this]()
            { QString f = QFileDialog::getSaveFileName(this, "Save Mask", "", "NIfTI files (*.nii *.nii.gz)"); if(!f.isEmpty()) saveMaskToFile(f.toStdString()); });
    connect(m_maskDialog, &MaskOptionsDialog::cleanRequested, this, [this]()
            { cleanMask(); updateViews(); });
    connect(m_maskDialog, &MaskOptionsDialog::brushRadiusChanged, this, [this](int r)
            { m_maskBrushRadius = r; });
    connect(m_maskDialog, &MaskOptionsDialog::maskOpacityChanged, this, [this](int p)
        { m_maskOpacity = float(p) / 100.0f; updateViews(); });

    // Route mouse events through lambdas so we can disable seed drawing while in mask mode.
    connect(m_axialView, &OrthogonalView::mousePressed, this, [this](int x, int y, Qt::MouseButton b)
            { if(m_maskMode!=0 && b==Qt::LeftButton) paintAxialMask(x,y); else onAxialClicked(x,y,b); });
    connect(m_axialView, &OrthogonalView::mouseMoved, this, [this](int x, int y, Qt::MouseButtons buttons)
            { if((buttons & Qt::LeftButton) && m_maskMode!=0) { paintAxialMask(x,y); } else if((buttons & Qt::LeftButton)) onAxialClicked(x,y,Qt::LeftButton); });
    connect(m_axialView, &OrthogonalView::mouseMoved, this, [this](int x, int y, Qt::MouseButtons){
        // axial view: x,y are image coords, z from slider
        int z = m_axialSlider->value();
        QString txt = "x:" + QString::number(x) + " y:" + QString::number(y) + " z:" + QString::number(z);
        if (m_image.getSizeX() > 0 && x >=0 && y >=0 && x < (int)m_image.getSizeX() && y < (int)m_image.getSizeY()) {
            float v = m_image.getVoxelValue((unsigned int)x, (unsigned int)y, (unsigned int)z);
            txt += " val:" + QString::number(v);
        } else {
            txt += " val: -";
        }
        m_statusLabel->setText(txt);
    });
    connect(m_axialView, &OrthogonalView::mouseReleased, this, [this](int x, int y, Qt::MouseButton b) {});

    connect(m_sagittalView, &OrthogonalView::mousePressed, this, [this](int x, int y, Qt::MouseButton b)
            { if(m_maskMode!=0 && b==Qt::LeftButton) paintSagittalMask(x,y); else onSagittalClicked(x,y,b); });
    connect(m_sagittalView, &OrthogonalView::mouseMoved, this, [this](int x, int y, Qt::MouseButtons buttons)
            { if((buttons & Qt::LeftButton) && m_maskMode!=0) { paintSagittalMask(x,y); } else if((buttons & Qt::LeftButton)) onSagittalClicked(x,y,Qt::LeftButton); });
    connect(m_sagittalView, &OrthogonalView::mouseMoved, this, [this](int x, int y, Qt::MouseButtons){
        // sagittal view: fixed x = slider, incoming x->y, y->z
        int xs = m_sagittalSlider->value();
        int yy = x;
        int zz = y;
        QString txt = "x:" + QString::number(xs) + " y:" + QString::number(yy) + " z:" + QString::number(zz);
        if (m_image.getSizeX() > 0 && xs >=0 && yy >=0 && zz >=0 && xs < (int)m_image.getSizeX() && yy < (int)m_image.getSizeY() && zz < (int)m_image.getSizeZ()) {
            float v = m_image.getVoxelValue((unsigned int)xs, (unsigned int)yy, (unsigned int)zz);
            txt += " val:" + QString::number(v);
        } else {
            txt += " val: -";
        }
        m_statusLabel->setText(txt);
    });

    connect(m_coronalView, &OrthogonalView::mousePressed, this, [this](int x, int y, Qt::MouseButton b)
            { if(m_maskMode!=0 && b==Qt::LeftButton) paintCoronalMask(x,y); else onCoronalClicked(x,y,b); });
    connect(m_coronalView, &OrthogonalView::mouseMoved, this, [this](int x, int y, Qt::MouseButtons buttons)
            { if((buttons & Qt::LeftButton) && m_maskMode!=0) { paintCoronalMask(x,y); } else if((buttons & Qt::LeftButton)) onCoronalClicked(x,y,Qt::LeftButton); });
    connect(m_coronalView, &OrthogonalView::mouseMoved, this, [this](int x, int y, Qt::MouseButtons){
        // coronal view: fixed y = slider, incoming x->x, y->z
        int yc = m_coronalSlider->value();
        int xx = x;
        int zz = y;
        QString txt = "x:" + QString::number(xx) + " y:" + QString::number(yc) + " z:" + QString::number(zz);
        if (m_image.getSizeX() > 0 && xx >=0 && yc >=0 && zz >=0 && xx < (int)m_image.getSizeX() && yc < (int)m_image.getSizeY() && zz < (int)m_image.getSizeZ()) {
            float v = m_image.getVoxelValue((unsigned int)xx, (unsigned int)yc, (unsigned int)zz);
            txt += " val:" + QString::number(v);
        } else {
            txt += " val: -";
        }
        m_statusLabel->setText(txt);
    });

    connect(m_axialSlider, &QSlider::valueChanged, this, &ManualSeedSelector::updateViews);
    connect(m_sagittalSlider, &QSlider::valueChanged, this, &ManualSeedSelector::updateViews);
    connect(m_coronalSlider, &QSlider::valueChanged, this, &ManualSeedSelector::updateViews);
    connect(btnSegment, &QPushButton::clicked, [this]() { SegmentationRunner::showSegmentationDialog(this); });

    // update color indicator when label changes
    connect(m_labelSelector, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this](int v){ updateLabelColor(v); });
    // initialize indicator to current label
    updateLabelColor(m_labelSelector->value());
}

void ManualSeedSelector::openImage() {
        QString fname = QFileDialog::getOpenFileName(this, "Open NIfTI", "", "NIfTI files (*.nii *.nii.gz)");
        if (fname.isEmpty())
            return;
        m_path = fname.toStdString();
        if (!m_image.load(m_path))
        {
            std::cerr << "Failed to load " << m_path << std::endl;
            return;
        }
        // Clear any previously loaded mask/seeds when opening a new image to
        // avoid mismatched buffer sizes and potential out-of-bounds access in
        // updateViews(). Seeds are image-specific and should be reset here.
        if (!m_maskData.empty()) {
            std::cerr << "ManualSeedSelector: clearing mask buffer on openImage()" << std::endl;
            m_maskData.clear();
        }
        if (!m_seeds.empty()) {
            std::cerr << "ManualSeedSelector: clearing seeds on openImage()" << std::endl;
            m_seeds.clear();
        }
        // Prevent emitting valueChanged while we initialize ranges/values to avoid
        // re-entrant calls into updateViews() while image buffers may not be ready.
        m_axialSlider->blockSignals(true);
        m_sagittalSlider->blockSignals(true);
        m_coronalSlider->blockSignals(true);

        m_axialSlider->setMinimum(0);
        m_axialSlider->setMaximum(int(m_image.getSizeZ()) - 1);
        m_axialSlider->setValue(int(m_image.getSizeZ()) / 2);
        m_sagittalSlider->setMinimum(0);
        m_sagittalSlider->setMaximum(int(m_image.getSizeX()) - 1);
        m_sagittalSlider->setValue(int(m_image.getSizeX()) / 2);
        m_coronalSlider->setMinimum(0);
        m_coronalSlider->setMaximum(int(m_image.getSizeY()) - 1);
        m_coronalSlider->setValue(int(m_image.getSizeY()) / 2);

        m_axialSlider->blockSignals(false);
        m_sagittalSlider->blockSignals(false);
        m_coronalSlider->blockSignals(false);

        updateViews();
}

void ManualSeedSelector::saveSeeds() {
        QString fname = QFileDialog::getSaveFileName(this, "Save seeds", "", "Text files (*.txt)");
        if (fname.isEmpty())
            return;
        // write current seeds to the chosen file
        std::ofstream ofs(fname.toStdString());
        if (!ofs)
        {
            QMessageBox::critical(this, "Save seeds", "Failed to open file for writing");
            return;
        }
        // write count then lines: x y z label internal
        ofs << m_seeds.size() << "\n";
        for (const auto &s : m_seeds)
        {
            ofs << s.x << " " << s.y << " " << s.z << " " << s.label << " " << s.internal << "\n";
        }
        ofs.close();
}

bool ManualSeedSelector::saveImageToFile(const std::string &path) {
    if (m_image.getSizeX() == 0) {
        QMessageBox::warning(this, "Save Image", "No image loaded.");
        return false;
    }
    bool ok = m_image.save(path);
    return ok;
}

void ManualSeedSelector::loadSeeds() {
        QString fname = QFileDialog::getOpenFileName(this, "Load seeds", "", "Text files (*.txt);;All files (*)");
        if (fname.isEmpty())
            return;
        bool ok = loadSeedsFromFile(fname.toStdString());
        if (!ok)
            QMessageBox::warning(this, "Load seeds", "Failed to load seeds from file.");
}

bool ManualSeedSelector::loadSeedsFromFile(const std::string &path) {
        std::ifstream ifs(path);
        if (!ifs)
            return false;
        size_t n = 0;
        ifs >> n;
        m_seeds.clear();
        int maxX = 0, maxY = 0, maxZ = 0;
        int sx = int(m_image.getSizeX()), sy = int(m_image.getSizeY()), sz = int(m_image.getSizeZ());
        std::vector<Seed> tmp;
        for (size_t i = 0; i < n; i++)
        {
            Seed s;
            ifs >> s.x >> s.y >> s.z >> s.label >> s.internal;
            tmp.push_back(s);
            if (s.x > maxX)
                maxX = s.x;
            if (s.y > maxY)
                maxY = s.y;
            if (s.z > maxZ)
                maxZ = s.z;
        }
        // Heuristic: if any coordinate equals image size (1-based indexing), convert all seeds to 0-based
        bool converted = false;
        if ((sx > 0 && maxX == sx) || (sy > 0 && maxY == sy) || (sz > 0 && maxZ == sz))
        {
            for (auto &s : tmp)
            {
                s.x -= 1;
                s.y -= 1;
                s.z -= 1;
            }
            converted = true;
            std::cerr << "[INFO] Converted loaded seeds from 1-based to 0-based indexing\n";
        }
        // clamp and store
        for (auto &s : tmp)
        {
            if (s.x < 0)
                s.x = 0;
            if (s.y < 0)
                s.y = 0;
            if (s.z < 0)
                s.z = 0;
            if (s.x >= sx)
                s.x = sx - 1;
            if (s.y >= sy)
                s.y = sy - 1;
            if (s.z >= sz)
                s.z = sz - 1;
            m_seeds.push_back(s);
        }
        // Reset view transforms to defaults to avoid misalignment caused by prior pan/zoom
        m_axialView->resetView();
        m_sagittalView->resetView();
        m_coronalView->resetView();
        updateViews();
        return true;
}

bool ManualSeedSelector::hasImage() const {
        return m_image.getSizeX() > 0 && m_image.getSizeY() > 0 && m_image.getSizeZ() > 0;
}
void ManualSeedSelector::onAxialClicked(int x, int y, Qt::MouseButton b) {
        int z = m_axialSlider->value();
        if (b == Qt::LeftButton)
        {
            if (m_seedMode == 1)
                addSeed(x, y, z);
            else if (m_seedMode == 2)
                eraseNear(x, y, z, m_seedBrushRadius);
        }
        else if (b == Qt::RightButton)
        {
            // always allow right-click erase
            eraseNear(x, y, z, m_seedBrushRadius);
        }
}

void ManualSeedSelector::onSagittalClicked(int x, int y, Qt::MouseButton b) {
        int x_loc = m_sagittalSlider->value();
        int y_loc = x;
        int z_loc = y;
        if (b == Qt::LeftButton)
        {
            if (m_seedMode == 1)
                addSeed(x_loc, y_loc, z_loc);
            else if (m_seedMode == 2)
                eraseNear(x_loc, y_loc, z_loc, m_seedBrushRadius);
        }
        else if (b == Qt::RightButton)
        {
            eraseNear(x_loc, y_loc, z_loc, m_seedBrushRadius);
        }
}

void ManualSeedSelector::onCoronalClicked(int x, int y, Qt::MouseButton b) {
        int y_loc = m_coronalSlider->value();
        int x_loc = x;
        int z_loc = y;
        if (b == Qt::LeftButton)
        {
            if (m_seedMode == 1)
                addSeed(x_loc, y_loc, z_loc);
            else if (m_seedMode == 2)
                eraseNear(x_loc, y_loc, z_loc, m_seedBrushRadius);
        }
        else if (b == Qt::RightButton)
        {
            eraseNear(x_loc, y_loc, z_loc, m_seedBrushRadius);
        }
}

// SeedOptionsDialog and MaskOptionsDialog are used instead of inline dialogs.

void ManualSeedSelector::addSeed(int x, int y, int z) {
        Seed s;
        s.x = x;
        s.y = y;
        s.z = z;
        s.label = m_labelSelector->value();
        s.internal = 1;
        m_seeds.push_back(s);
        updateViews();
}

void ManualSeedSelector::eraseNear(int x, int y, int z, int r) {
        std::vector<Seed> keep;
        for (auto &s : m_seeds)
        {
            int dx = s.x - x;
            int dy = s.y - y;
            int dz = s.z - z;
            if (dx * dx + dy * dy + dz * dz > r * r)
                keep.push_back(s);
        }
        m_seeds.swap(keep);
        updateViews();
}

void ManualSeedSelector::updateLabelColor(int label)
{
    // Ensure label in visible range
    int l = std::max(1, std::min(254, label));
    QColor c = colorForLabel(l);
    QPixmap pm(m_labelColorIndicator->width(), m_labelColorIndicator->height());
    pm.fill(c);
    m_labelColorIndicator->setPixmap(pm);
    // add a thin border via stylesheet for better visibility
    m_labelColorIndicator->setStyleSheet("border:1px solid black;");
}

static QImage makeQImageFromRGB(const std::vector<unsigned char> &rgb, int w, int h) {
        // defensive checks: ensure rgb buffer has expected size
        size_t expected = size_t(w) * size_t(h) * 3;
        if (rgb.size() < expected)
        {
            std::cerr << "[ERROR] makeQImageFromRGB: rgb buffer too small: got=" << rgb.size() << " expected=" << expected << "\n";
            // return an empty image to avoid crashes
            return QImage();
        }
        QImage img(w, h, QImage::Format_RGB888);
        int bpl = img.bytesPerLine();
        const unsigned char *src = rgb.data();
        unsigned char *dst = img.bits();
        for (int y = 0; y < h; ++y)
        {
            std::memcpy(dst + size_t(y) * bpl, src + size_t(y) * w * 3, size_t(w * 3));
        }
        return img;
}

void ManualSeedSelector::updateViews() {
        // basic guards: ensure image sizes are valid
        unsigned int sizeX = m_image.getSizeX();
        unsigned int sizeY = m_image.getSizeY();
        unsigned int sizeZ = m_image.getSizeZ();
        if (sizeX == 0 || sizeY == 0 || sizeZ == 0)
        {
            std::cerr << "[WARN] updateViews: image dimensions invalid (" << sizeX << "," << sizeY << "," << sizeZ << ")\n";
            return;
        }
        int z = m_axialSlider->value();
        float lo = m_image.getGlobalMin();
        float hi = m_image.getGlobalMax();
        auto axial_rgb = m_image.getAxialSliceAsRGB(z, lo, hi);
        // blend mask if present
        if (!m_maskData.empty())
        {
            // Defensive: ensure mask buffer size matches current image dimensions
            size_t expected = size_t(sizeX) * size_t(sizeY) * size_t(sizeZ);
            if (m_maskData.size() != expected)
            {
                std::cerr << "ManualSeedSelector::updateViews(): mask buffer size (" << m_maskData.size() << ") does not match expected (" << expected << "). Clearing mask to avoid OOB access." << std::endl;
                m_maskData.clear();
            }
        }
        if (!m_maskData.empty())
        {
            // diagnostic: count non-zero mask voxels in this axial slice
            size_t cnt = 0;
            for (unsigned int yy = 0; yy < sizeY; ++yy)
                for (unsigned int xx = 0; xx < sizeX; ++xx)
                    if (m_maskData[size_t(xx) + size_t(yy) * sizeX + size_t(z) * sizeX * sizeY] != 0)
                        ++cnt;
            for (unsigned int yy = 0; yy < sizeY; ++yy)
            {
                for (unsigned int xx = 0; xx < sizeX; ++xx)
                {
                    size_t idx3 = size_t(xx) + size_t(yy) * sizeX + size_t(z) * sizeX * sizeY;
                    int lbl = m_maskData[idx3];
                    if (lbl != 0)
                    {
                        // compute overlay color for label (use deterministic color map)
                            int dl = std::max(1, std::min(254, lbl));
                            QColor col = colorForLabel(dl);
                            unsigned char r = static_cast<unsigned char>(col.red());
                            unsigned char g = static_cast<unsigned char>(col.green());
                            unsigned char b = static_cast<unsigned char>(col.blue());
                            size_t pix = (yy * sizeX + xx) * 3;
                            axial_rgb[pix + 0] = static_cast<unsigned char>(m_maskOpacity * r + (1.0f - m_maskOpacity) * axial_rgb[pix + 0]);
                            axial_rgb[pix + 1] = static_cast<unsigned char>(m_maskOpacity * g + (1.0f - m_maskOpacity) * axial_rgb[pix + 1]);
                            axial_rgb[pix + 2] = static_cast<unsigned char>(m_maskOpacity * b + (1.0f - m_maskOpacity) * axial_rgb[pix + 2]);
                    }
                }
            }
        }
        QImage axial = makeQImageFromRGB(axial_rgb, int(sizeX), int(sizeY));
        m_axialView->setImage(axial);
        int sagX = m_sagittalSlider->value();
        auto sagittal_rgb = m_image.getSagittalSliceAsRGB(sagX, lo, hi);
        if (!m_maskData.empty())
        {
            unsigned int sxw = sizeX;
            unsigned int syh = sizeY;
            unsigned int szz = sizeZ;
            // sagittal: x fixed = sagX, iterate y (width) and z (height)
            size_t cnt2 = 0;
            for (unsigned int zz = 0; zz < szz; ++zz)
            {
                for (unsigned int yy = 0; yy < syh; ++yy)
                {
                    size_t idx3 = size_t(sagX) + size_t(yy) * sxw + size_t(zz) * sxw * syh;
                    int lbl = m_maskData[idx3];
                    if (lbl != 0)
                        ++cnt2;
                    if (lbl != 0)
                    {
                        int dl = std::max(1, std::min(254, lbl));
                        QColor col = colorForLabel(dl);
                        unsigned char r = static_cast<unsigned char>(col.red());
                        unsigned char g = static_cast<unsigned char>(col.green());
                        unsigned char b = static_cast<unsigned char>(col.blue());
                        size_t pix = (zz * syh + yy) * 3;
                        sagittal_rgb[pix + 0] = static_cast<unsigned char>(m_maskOpacity * r + (1.0f - m_maskOpacity) * sagittal_rgb[pix + 0]);
                        sagittal_rgb[pix + 1] = static_cast<unsigned char>(m_maskOpacity * g + (1.0f - m_maskOpacity) * sagittal_rgb[pix + 1]);
                        sagittal_rgb[pix + 2] = static_cast<unsigned char>(m_maskOpacity * b + (1.0f - m_maskOpacity) * sagittal_rgb[pix + 2]);
                    }
                }
            }
        }
        QImage sagittal = makeQImageFromRGB(sagittal_rgb, int(sizeY), int(sizeZ));
        m_sagittalView->setImage(sagittal);
        int corY = m_coronalSlider->value();
        auto coronal_rgb = m_image.getCoronalSliceAsRGB(corY, lo, hi);
        if (!m_maskData.empty())
        {
            unsigned int sxw = sizeX;
            unsigned int syh = sizeY;
            unsigned int szz = sizeZ;
            size_t cnt3 = 0;
            for (unsigned int zz = 0; zz < szz; ++zz)
            {
                for (unsigned int xx = 0; xx < sxw; ++xx)
                {
                    size_t idx3 = size_t(xx) + size_t(corY) * sxw + size_t(zz) * sxw * syh;
                    int lbl = m_maskData[idx3];
                    if (lbl != 0)
                        ++cnt3;
                    if (lbl != 0)
                    {
                        int dl = std::max(1, std::min(254, lbl));
                        QColor col = colorForLabel(dl);
                        unsigned char r = static_cast<unsigned char>(col.red());
                        unsigned char g = static_cast<unsigned char>(col.green());
                        unsigned char b = static_cast<unsigned char>(col.blue());
                        size_t pix = (zz * sxw + xx) * 3;
                        coronal_rgb[pix + 0] = static_cast<unsigned char>(m_maskOpacity * r + (1.0f - m_maskOpacity) * coronal_rgb[pix + 0]);
                        coronal_rgb[pix + 1] = static_cast<unsigned char>(m_maskOpacity * g + (1.0f - m_maskOpacity) * coronal_rgb[pix + 1]);
                        coronal_rgb[pix + 2] = static_cast<unsigned char>(m_maskOpacity * b + (1.0f - m_maskOpacity) * coronal_rgb[pix + 2]);
                    }
                }
            }
        }
        QImage coronal = makeQImageFromRGB(coronal_rgb, int(sizeX), int(sizeZ));
        m_coronalView->setImage(coronal);

        // set overlays to draw seeds
        m_axialView->setOverlayDraw([this, z](QPainter &p, float scale)
                                    {
        for (auto &s: m_seeds) {
            if (s.z != z) continue;
            // color by label
            int lbl = std::max(1, std::min(42, s.label));
            // deterministic color for label
            QColor qc = colorForLabel(lbl);
            p.setPen(QPen(qc));
            p.setBrush(qc);
            p.drawEllipse(QPoint(int(s.x*scale), int(s.y*scale)), 2,2);
        } });

        m_sagittalView->setOverlayDraw([this, sagX](QPainter &p, float scale)
                                       {
        for (auto &s: m_seeds) {
            if (s.x != sagX) continue;
            int lbl = std::max(1, std::min(254, s.label));
            QColor qc = colorForLabel(lbl);
            p.setPen(QPen(qc));
            p.setBrush(qc);
            p.drawEllipse(QPoint(int(s.y*scale), int(s.z*scale)), 2,2);
        } });

        m_coronalView->setOverlayDraw([this, corY](QPainter &p, float scale)
                                      {
        for (auto &s: m_seeds) {
            if (s.y != corY) continue;
            int lbl = std::max(1, std::min(254, s.label));
            QColor qc = colorForLabel(lbl);
            p.setPen(QPen(qc));
            p.setBrush(qc);
            p.drawEllipse(QPoint(int(s.x*scale), int(s.z*scale)), 2,2);
        } });

        // If mask buffer exists, set simple overlay to blend a translucent color per-label.
        if (!m_maskData.empty())
        {
            auto drawMaskOverlay = [this](QPainter &p, float scale)
            {
                // we will iterate pixels coarsely: draw small rects for non-zero mask points
                unsigned int sx = m_image.getSizeX();
                unsigned int sy = m_image.getSizeY();
                unsigned int sz = m_image.getSizeZ();
                // choose view based on which widget called us: we can't detect here, so do nothing
            };
            // Note: For now keep seed overlays as-is; blending is handled when creating the QImage in NiftiImage layer in a future step.
            (void)drawMaskOverlay;
        }
}

// Mask options dialog moved to MaskOptionsDialog class.

void ManualSeedSelector::setMaskMode(int mode) {
        m_maskMode = mode;
}

void ManualSeedSelector::cleanMask() {
        m_maskData.clear();
}

bool ManualSeedSelector::saveMaskToFile(const std::string &path) {
#if HAVE_ITK
        // Save mask buffer via ITK as a 3D integer image.
        try
        {
            // Use int16 image for masks to match requested format
            using PixelType = int16_t;
            using ImageType = itk::Image<PixelType, 3>;
            using WriterType = itk::ImageFileWriter<ImageType>;
            ImageType::Pointer out = ImageType::New();
            ImageType::RegionType region;
            ImageType::IndexType start;
            start.Fill(0);
            ImageType::SizeType size;
            unsigned int sx = m_image.getSizeX();
            unsigned int sy = m_image.getSizeY();
            unsigned int sz = m_image.getSizeZ();
            if (sx == 0 || sy == 0 || sz == 0) {
                QMessageBox::warning(this, "Save Mask", "No image loaded or invalid image size.");
                return false;
            }
            size[0] = static_cast<ImageType::SizeValueType>(sx);
            size[1] = static_cast<ImageType::SizeValueType>(sy);
            size[2] = static_cast<ImageType::SizeValueType>(sz);
            region.SetIndex(start);
            region.SetSize(size);
            out->SetRegions(region);
            out->Allocate();

            // If we don't have mask data, create an empty image
            if (m_maskData.empty())
                m_maskData.assign(size_t(sx) * size_t(sy) * size_t(sz), 0);

            itk::ImageRegionIterator<ImageType> it(out, region);
            size_t idx = 0;
            for (it.GoToBegin(); !it.IsAtEnd(); ++it, ++idx)
            {
                // clamp into int16 range
                int v = m_maskData[idx];
                if (v < std::numeric_limits<PixelType>::min()) v = std::numeric_limits<PixelType>::min();
                if (v > std::numeric_limits<PixelType>::max()) v = std::numeric_limits<PixelType>::max();
                it.Set(static_cast<PixelType>(v));
            }

            // Ensure filename has a NIfTI extension
            std::string outpath = path;
            auto has_suffix = [](const std::string &p, const std::string &suf) {
                if (p.size() < suf.size()) return false;
                return p.compare(p.size() - suf.size(), suf.size(), suf) == 0;
            };
            if (!has_suffix(outpath, ".nii") && !has_suffix(outpath, ".nii.gz")) {
                outpath += ".nii.gz";
            }

            WriterType::Pointer writer = WriterType::New();
            // Force NIfTI IO to ensure correct format
            itk::NiftiImageIO::Pointer nio = itk::NiftiImageIO::New();
            writer->SetImageIO(nio);
            writer->SetFileName(outpath);
            writer->SetInput(out);
            writer->Update();
            return true;
        }
        catch (const std::exception &e)
        {
            QMessageBox::critical(this, "Save Mask", QString("Failed to save mask via ITK: %1").arg(e.what()));
            return false;
        }
#else
        (void)path;
        QMessageBox::warning(this, "Save Mask", "Saving masks without ITK is not supported in this build. Rebuild with ITK enabled.");
        return false;
#endif
}

bool ManualSeedSelector::loadMaskFromFile(const std::string &path) {
#if HAVE_ITK
        // Use ITK to read the mask as a 3D image of integers
        try
        {
            std::cout << "You have ITK" << std::endl;
            using ImageType = itk::Image<int32_t, 3>;
            using ReaderType = itk::ImageFileReader<ImageType>;
            ReaderType::Pointer reader = ReaderType::New();
            reader->SetFileName(path);
            reader->Update();
            ImageType::Pointer img = reader->GetOutput();
            ImageType::RegionType region = img->GetLargestPossibleRegion();
            ImageType::SizeType size = region.GetSize();
            unsigned int sx = static_cast<unsigned int>(size[0]);
            unsigned int sy = static_cast<unsigned int>(size[1]);
            unsigned int sz = static_cast<unsigned int>(size[2]);
            size_t tot = size_t(sx) * size_t(sy) * size_t(sz);
            m_maskData.clear();
            m_maskData.resize(tot);
            itk::ImageRegionConstIterator<ImageType> it(img, region);
            size_t idx = 0;
            for (it.GoToBegin(); !it.IsAtEnd(); ++it, ++idx)
            {
                m_maskData[idx] = static_cast<int>(it.Get());
            }
            return true;
        }
        catch (const std::exception &e)
        {
            QMessageBox::critical(this, "Load Mask", QString("Failed to load mask via ITK: %1").arg(e.what()));
            return false;
        }
#else
        // Minimal pure-C++ NIfTI-1 (.nii uncompressed) reader fallback.
        // NOTE: this does not support .nii.gz compressed files. If you need .nii.gz support,
        // rebuild with ITK enabled (CMake option to provide ITK) or supply an uncompressed .nii file.
        bool gz = false;
        if (path.size() >= 7 && path.substr(path.size() - 7) == ".nii.gz")
            gz = true;
        std::vector<char> hdr(348);
        std::vector<char> bodybuf; // if gz we'll store full file here
        if (!gz)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f)
            {
                QMessageBox::critical(this, "Load Mask", QString("Failed to open file: %1").arg(QString::fromStdString(path)));
                return false;
            }
            // read 348-byte header
            f.read(hdr.data(), 348);
            if (!f)
            {
                QMessageBox::critical(this, "Load Mask", "Failed to read NIfTI header (not a valid .nii?).");
                return false;
            }
            // we'll use f for body reading later via normal ifstream
        }
        else
        {
            // read via gzopen
            gzFile gzfp = gzopen(path.c_str(), "rb");
            if (!gzfp)
            {
                QMessageBox::critical(this, "Load Mask", QString("Failed to open compressed file: %1").arg(QString::fromStdString(path)));
                return false;
            }
            int r = gzread(gzfp, hdr.data(), 348);
            if (r != 348)
            {
                gzclose(gzfp);
                QMessageBox::critical(this, "Load Mask", "Failed to read NIfTI header from compressed file.");
                return false;
            }
            // Keep gzfp open and we'll read body from it later
            // To simplify, we will buffer the rest of the file into memory
            std::vector<char> rest;
            const int chunk = 1 << 20;
            char buf[chunk];
            int got = 0;
            while ((got = gzread(gzfp, buf, chunk)) > 0)
            {
                rest.insert(rest.end(), buf, buf + got);
            }
            gzclose(gzfp);
            // attach rest after header for simpler parsing
            rest.insert(rest.begin(), hdr.begin(), hdr.end());
            // move rest into bodybuf so it remains in scope
            bodybuf.swap(rest);
            // copy header from bodybuf beginning to hdr (already same but ensure consistency)
            if (bodybuf.size() >= 348)
                std::copy(bodybuf.begin(), bodybuf.begin() + 348, hdr.begin());
        }
        auto read_int32 = [&](size_t off) -> int32_t
        {
            int32_t v;
            std::memcpy(&v, hdr.data() + off, sizeof(int32_t));
            return v;
        };
        auto read_int16 = [&](size_t off) -> int16_t
        {
            int16_t v;
            std::memcpy(&v, hdr.data() + off, sizeof(int16_t));
            return v;
        };
        auto read_float = [&](size_t off) -> float
        {
            float v;
            std::memcpy(&v, hdr.data() + off, sizeof(float));
            return v;
        };
        int32_t sizeof_hdr = read_int32(0);
        if (sizeof_hdr != 348)
        {
            QMessageBox::critical(this, "Load Mask", "Unexpected NIfTI header size (not 348). Unsupported or byte-swapped file.");
            return false;
        }
        // dims: short dim[8] at offset 40
        int16_t dim0 = read_int16(40);
        (void)dim0;
        int sx = static_cast<int>(read_int16(42));
        int sy = static_cast<int>(read_int16(44));
        int sz = static_cast<int>(read_int16(46));
        if (sx <= 0 || sy <= 0 || sz <= 0)
        {
            QMessageBox::critical(this, "Load Mask", "Invalid image dimensions in NIfTI header.");
            return false;
        }
        int16_t datatype = read_int16(70);
        int16_t bitpix = read_int16(72);
        float vox_offset = read_float(108);
        if (vox_offset < 0.0f)
            vox_offset = 0.0f;
        int bytesPerVoxel = std::max(1, int(bitpix / 8));
        size_t tot = size_t(sx) * size_t(sy) * size_t(sz);
        // read raw buffer
        std::vector<char> rawbuf(size_t(tot) * size_t(bytesPerVoxel));
        if (!gz)
        {
            std::ifstream f2(path, std::ios::binary);
            if (!f2)
            {
                QMessageBox::critical(this, "Load Mask", "Failed to re-open file for reading voxel data.");
                return false;
            }
            f2.seekg(static_cast<std::streamoff>(std::lround(vox_offset)), std::ios::beg);
            if (!f2)
            {
                QMessageBox::critical(this, "Load Mask", "Failed to seek to voxel data.");
                return false;
            }
            f2.read(rawbuf.data(), rawbuf.size());
            if (!f2 && !f2.eof())
            {
                QMessageBox::critical(this, "Load Mask", "Failed to read voxel data completely.");
                return false;
            }
        }
        else
        {
            // read from the buffered body we prepared earlier
            if (bodybuf.empty())
            {
                QMessageBox::critical(this, "Load Mask", "Internal error reading compressed buffer.");
                return false;
            }
            size_t offset = static_cast<size_t>(std::lround(vox_offset));
            size_t needed = offset + rawbuf.size();
            if (needed > bodybuf.size())
            {
                QMessageBox::critical(this, "Load Mask", "Compressed file appears truncated.");
                return false;
            }
            std::memcpy(rawbuf.data(), bodybuf.data() + offset, rawbuf.size());
        }
        m_maskData.clear();
        m_maskData.resize(tot);
        // interpret common datatypes
        if (datatype == 2 || bitpix == 8)
        {
            // unsigned char
            for (size_t i = 0; i < tot; ++i)
                m_maskData[i] = static_cast<unsigned char>(rawbuf[i]);
        }
        else if (datatype == 4 || bitpix == 16)
        {
            // signed short (int16)
            const int16_t *p = reinterpret_cast<const int16_t *>(rawbuf.data());
            for (size_t i = 0; i < tot; ++i)
                m_maskData[i] = static_cast<int>(p[i]);
        }
        else if (datatype == 8 || bitpix == 32)
        {
            // int32 or float32 - try int32 first
            if (bitpix == 32 && datatype == 8)
            {
                const int32_t *p = reinterpret_cast<const int32_t *>(rawbuf.data());
                for (size_t i = 0; i < tot; ++i)
                    m_maskData[i] = static_cast<int>(p[i]);
            }
            else if (datatype == 16 || bitpix == 32)
            {
                // float32
                const float *p = reinterpret_cast<const float *>(rawbuf.data());
                for (size_t i = 0; i < tot; ++i)
                    m_maskData[i] = static_cast<int>(std::lround(p[i]));
            }
            else
            {
                QMessageBox::warning(this, "Load Mask", "Unsupported 32-bit datatype in NIfTI mask.");
                return false;
            }
        }
        else
        {
            QMessageBox::warning(this, "Load Mask", "Unsupported NIfTI datatype for mask loading.");
            return false;
        }
        return true;
#endif
}

void ManualSeedSelector::paintAxialMask(int x, int y) {
        if (m_image.getSizeX() == 0)
            return;
        int z = m_axialSlider->value();
        bool erase = (m_maskMode == 2);
        std::cerr << "paintAxialMask at (" << x << "," << y << "," << z << ") erase=" << erase << " label=" << m_labelSelector->value() << " radius=" << m_maskBrushRadius << "\n";
        applyBrushToMask({x, y, z}, {0, 1}, m_maskBrushRadius, m_labelSelector->value(), erase);
        // Do NOT automatically add seeds here. Seed drawing is controlled by seed mode in the Seed Options dialog.
        updateViews();
}

void ManualSeedSelector::paintSagittalMask(int x, int y) {
        int sx = m_sagittalSlider->value();
        bool erase = (m_maskMode == 2);
        std::cerr << "paintSagittalMask at (" << sx << "," << x << "," << y << ") erase=" << erase << " label=" << m_labelSelector->value() << " radius=" << m_maskBrushRadius << "\n";
        applyBrushToMask({sx, x, y}, {1, 2}, m_maskBrushRadius, m_labelSelector->value(), erase);
        // Do not auto-add seeds
        updateViews();
}

void ManualSeedSelector::paintCoronalMask(int x, int y) {
        int cy = m_coronalSlider->value();
        bool erase = (m_maskMode == 2);
        std::cerr << "paintCoronalMask at (" << x << "," << cy << "," << y << ") erase=" << erase << " label=" << m_labelSelector->value() << " radius=" << m_maskBrushRadius << "\n";
        applyBrushToMask({x, cy, y}, {0, 2}, m_maskBrushRadius, m_labelSelector->value(), erase);
        // Do not auto-add seeds
        updateViews();
}

void ManualSeedSelector::applyBrushToMask(const std::array<int,3> &center, const std::pair<int,int> &axes, int radius, int labelValue, bool erase) {
        unsigned int sx = m_image.getSizeX();
        unsigned int sy = m_image.getSizeY();
        unsigned int sz = m_image.getSizeZ();
        if (sx == 0)
            return;
        if (m_maskData.empty())
            m_maskData.assign(sx * sy * sz, 0);
        int a0 = axes.first;
        int a1 = axes.second;
        int min0 = std::max(0, center[a0] - radius);
        int max0 = std::min(int((a0 == 0 ? sx : (a0 == 1 ? sy : sz))) - 1, center[a0] + radius);
        int min1 = std::max(0, center[a1] - radius);
        int max1 = std::min(int((a1 == 0 ? sx : (a1 == 1 ? sy : sz))) - 1, center[a1] + radius);
        for (int i = min0; i <= max0; ++i)
        {
            for (int j = min1; j <= max1; ++j)
            {
                int di = i - center[a0];
                int dj = j - center[a1];
                if (di * di + dj * dj <= radius * radius)
                {
                    int xi = (a0 == 0) ? i : ((a1 == 0) ? j : center[0]);
                    int yi = (a0 == 1) ? i : ((a1 == 1) ? j : center[1]);
                    int zi = (a0 == 2) ? i : ((a1 == 2) ? j : center[2]);
                    if (xi < 0 || yi < 0 || zi < 0 || xi >= int(sx) || yi >= int(sy) || zi >= int(sz))
                        continue;
                    size_t idx = size_t(xi) + size_t(yi) * sx + size_t(zi) * sx * sy;
                    if (erase) {
                        // Only erase if the existing voxel matches the requested label value
                        if (m_maskData[idx] == labelValue)
                            m_maskData[idx] = 0;
                    } else {
                        m_maskData[idx] = labelValue;
                    }
                }
            }
        }
        
}

void ManualSeedSelector::keyPressEvent(QKeyEvent *event)
{
    // Only handle slice navigation when focus is on one of the image views
    // (or no widget has focus). If the user is editing a spinbox, slider or
    // other UI control, defer to the default handler so keys modify those
    // controls as expected.
    QWidget *fw = QApplication::focusWidget();
    // Consider the focused widget allowed if it is the main window OR
    // if it is the image view itself or any child of an image view.
    auto isDescendant = [](QWidget *child, QWidget *ancestor) {
        while (child) {
            if (child == ancestor) return true;
            child = child->parentWidget();
        }
        return false;
    };
    if (fw && fw != this
        && !isDescendant(fw, m_axialView)
        && !isDescendant(fw, m_sagittalView)
        && !isDescendant(fw, m_coronalView)) {
        // If focus isn't inside a view, still allow handling when the mouse
        // cursor is currently over one of the views. This covers cases where
        // the user clicked the view but focus went to an internal widget or
        // the window manager didn't move focus.
        QPoint gpos = QCursor::pos();
        bool overView = false;
        for (QWidget *v : { m_axialView, m_sagittalView, m_coronalView }) {
            if (!v) continue;
            QPoint local = v->mapFromGlobal(gpos);
            if (v->rect().contains(local)) { overView = true; break; }
        }
        if (!overView) {
            QMainWindow::keyPressEvent(event);
            return;
        }
    }

    int axial = m_axialSlider->value();
    int sag = m_sagittalSlider->value();
    int cor = m_coronalSlider->value();
    bool handled = true;
    // Use WASD for axial/sagittal navigation and Q/E for coronal +/-
    // W: axial +, S: axial -, D: sagittal +, A: sagittal -,
    // E: coronal +, Q: coronal -
    switch (event->key()) {
        case Qt::Key_W:
            axial = std::min<int>(axial + 1, int(m_image.getSizeZ()) - 1);
            m_axialSlider->setValue(axial);
            break;
        case Qt::Key_S:
            axial = std::max<int>(axial - 1, 0);
            m_axialSlider->setValue(axial);
            break;
        case Qt::Key_D:
            sag = std::min<int>(sag + 1, int(m_image.getSizeX()) - 1);
            m_sagittalSlider->setValue(sag);
            break;
        case Qt::Key_A:
            sag = std::max<int>(sag - 1, 0);
            m_sagittalSlider->setValue(sag);
            break;
        case Qt::Key_E:
            cor = std::min<int>(cor + 1, int(m_image.getSizeY()) - 1);
            m_coronalSlider->setValue(cor);
            break;
        case Qt::Key_Q:
            cor = std::max<int>(cor - 1, 0);
            m_coronalSlider->setValue(cor);
            break;
        default:
            handled = false;
    }
    if (!handled) QMainWindow::keyPressEvent(event);
}

bool ManualSeedSelector::handleSliceKey(QKeyEvent *event)
{
    if (!hasImage()) return false;
    int axial = m_axialSlider->value();
    int sag = m_sagittalSlider->value();
    int cor = m_coronalSlider->value();
    bool handled = true;
    switch (event->key()) {
        case Qt::Key_W:
            axial = std::min<int>(axial + 1, int(m_image.getSizeZ()) - 1);
            m_axialSlider->setValue(axial);
            break;
        case Qt::Key_S:
            axial = std::max<int>(axial - 1, 0);
            m_axialSlider->setValue(axial);
            break;
        case Qt::Key_D:
            sag = std::min<int>(sag + 1, int(m_image.getSizeX()) - 1);
            m_sagittalSlider->setValue(sag);
            break;
        case Qt::Key_A:
            sag = std::max<int>(sag - 1, 0);
            m_sagittalSlider->setValue(sag);
            break;
        case Qt::Key_E:
            cor = std::min<int>(cor + 1, int(m_image.getSizeY()) - 1);
            m_coronalSlider->setValue(cor);
            break;
        case Qt::Key_Q:
            cor = std::max<int>(cor - 1, 0);
            m_coronalSlider->setValue(cor);
            break;
        case Qt::Key_BracketLeft:
            m_axialSlider->setValue(std::max(0, axial - 1));
            m_sagittalSlider->setValue(std::max(0, sag - 1));
            m_coronalSlider->setValue(std::max(0, cor - 1));
            break;
        case Qt::Key_BracketRight:
            m_axialSlider->setValue(std::min<int>(axial + 1, int(m_image.getSizeZ()) - 1));
            m_sagittalSlider->setValue(std::min<int>(sag + 1, int(m_image.getSizeX()) - 1));
            m_coronalSlider->setValue(std::min<int>(cor + 1, int(m_image.getSizeY()) - 1));
            break;
        default:
            handled = false;
    }
    return handled;
}

bool ManualSeedSelector::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        if (obj == m_axialView || obj == m_sagittalView || obj == m_coronalView) {
            QKeyEvent *ke = static_cast<QKeyEvent*>(event);
            if (handleSliceKey(ke)) return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}