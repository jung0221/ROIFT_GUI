#pragma once

#include <QDialog>
#include <QString>
#include <vector>

#include "NiftiImage.h"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QTableWidget;

// Asks how to read a .npz/.npy: which array, which channel, how the numpy axes
// map onto the image axes, and what voxel spacing to use. None of that is
// recorded in a numpy container, and all of it changes what the volume means.
//
// The axis order in particular cannot be inferred without a reference volume,
// so the dialog renders a slice under the current settings: seeing the anatomy
// is the only reliable way to confirm the mapping.
class NpzImportDialog : public QDialog
{
    Q_OBJECT
public:
    // Settle the import options for `path`. Prompts only when the file is
    // genuinely ambiguous (several arrays, several channels, or no geometry
    // to be found); otherwise it takes the automatic reading. Returns false if
    // the user cancelled or the file holds nothing importable.
    static bool chooseOptions(QWidget *parent, const QString &path, NpzImportOptions &options);

    NpzImportDialog(const QString &path, const std::vector<npz::ArrayInfo> &arrays,
                    const QString &autoSelectedArray, QWidget *parent = nullptr);

    NpzImportOptions options() const;

private:
    void onArraySelectionChanged();
    void refreshPreview();
    void loadPreviewData();
    void renderPreview(const NpzImportReport &report);
    const npz::ArrayInfo *selectedArray() const;

    QString m_path;
    std::vector<npz::ArrayInfo> m_arrays;

    QTableWidget *m_table = nullptr;
    QSpinBox *m_channel = nullptr;
    QComboBox *m_axisOrder = nullptr;
    QCheckBox *m_flip[3] = {nullptr, nullptr, nullptr};
    QCheckBox *m_overrideSpacing = nullptr;
    QDoubleSpinBox *m_spacing[3] = {nullptr, nullptr, nullptr};
    QLabel *m_preview = nullptr;
    QLabel *m_previewCaption = nullptr;
    QLabel *m_summary = nullptr;
    QLabel *m_warning = nullptr;
    QDialogButtonBox *m_buttons = nullptr;

    // Coarsely subsampled copy of the selected array, kept so that changing the
    // axis order re-renders instantly instead of re-reading the file.
    std::string m_previewArray;
    std::vector<size_t> m_previewShape; // subsampled extents, per array axis
    std::vector<float> m_previewData;
    float m_previewLow = 0.0f;
    float m_previewHigh = 1.0f;
    QString m_previewError;
    bool m_refreshing = false;
};
