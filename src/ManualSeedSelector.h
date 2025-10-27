#pragma once

#include <QMainWindow>
#include <QSlider>
#include <QSpinBox>
#include <QLabel>
#include <vector>
#include "NiftiImage.h"
#include "OrthogonalView.h"
#include "SeedOptionsDialog.h"
#include "MaskOptionsDialog.h"

struct Seed { int x,y,z,label,internal; };

class ManualSeedSelector : public QMainWindow {
    Q_OBJECT
public:
    ManualSeedSelector(const std::string &niftiPath, QWidget *parent = nullptr);
    ~ManualSeedSelector();
    // keyboard handling for slice navigation
    void keyPressEvent(QKeyEvent *event) override;
    // catch key events on child views
    bool eventFilter(QObject *obj, QEvent *event) override;
    // load seeds from a supplied path (used by CLI). Returns true on success.
    bool loadSeedsFromFile(const std::string &path);
    // return true if an image is currently loaded
    bool hasImage() const;
    // initialize sliders and views after an image has been loaded
    void initializeImageWidgets();
    // expose current seeds for external helpers (read-only)
    const std::vector<Seed>& getSeeds() const { return m_seeds; }
    // expose image path
    std::string getImagePath() const { return m_path; }
    // convenience wrapper to load a mask and update views (used by segmentation runner)
    bool applyMaskFromPath(const std::string &path) { bool ok = loadMaskFromFile(path); if (ok) updateViews(); return ok; }

private slots:
    void openImage();
    void saveSeeds();
    void loadSeeds();
    bool saveImageToFile(const std::string &path);
    void onAxialClicked(int x, int y, Qt::MouseButton b);
    void onSagittalClicked(int x, int y, Qt::MouseButton b);
    void onCoronalClicked(int x, int y, Qt::MouseButton b);
    void updateViews();

    // Mask features
    void setMaskMode(int mode); // 0=idle,1=draw,2=erase
    void cleanMask();
    bool saveMaskToFile(const std::string &path);
    bool loadMaskFromFile(const std::string &path);
    void paintAxialMask(int x, int y);
    void paintSagittalMask(int x, int y);
    void paintCoronalMask(int x, int y);
    void applyBrushToMask(const std::array<int,3> &center, const std::pair<int,int> &axes, int radius, int labelValue, bool erase=false);
    

private:
    void setupUi();
    bool handleSliceKey(QKeyEvent *event);
    void addSeed(int x, int y, int z);
    void eraseNear(int x, int y, int z, int r);

    void updateLabelColor(int label);

    NiftiImage m_image;
    std::string m_path;
    std::vector<Seed> m_seeds;

    OrthogonalView *m_axialView;
    OrthogonalView *m_sagittalView;
    OrthogonalView *m_coronalView;
    QSlider *m_axialSlider;
    QSlider *m_sagittalSlider;
    QSlider *m_coronalSlider;
    QSpinBox *m_labelSelector;
    QLabel *m_labelColorIndicator;
    QLabel *m_statusLabel;
    // backup copy used to undo destructive edits like threshold
    NiftiImage m_imageBackup;
    bool m_hasImageBackup = false;
    QPushButton *m_btnUndoThreshold = nullptr;
    bool m_mouseDown = false;
    int m_dragButton = 0;
    std::vector<std::array<int,3>> m_colorLUT;
    // mask buffer: linearized X * Y * Z, 0 means empty, positive integers are label values
    std::vector<int> m_maskData;
    int m_maskMode = 0;
    int m_maskBrushRadius = 6;
    float m_maskOpacity = 0.5f;
    // seed interaction mode: 0=idle,1=draw,2=erase
    int m_seedMode = 1;
    int m_seedBrushRadius = 5;
    SeedOptionsDialog *m_seedDialog = nullptr;
    MaskOptionsDialog *m_maskDialog = nullptr;
};
