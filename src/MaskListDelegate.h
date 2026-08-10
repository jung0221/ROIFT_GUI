#pragma once

/**
 * MaskListDelegate.h — the mask list row: [eye] [colour swatch] name.
 *
 * The eye is painted rather than iconised because it has three states and each
 * one is a different ink (see MaskVisibility), which a fixed-colour SVG cannot
 * express. Rows stay plain QListWidgetItems — no per-row widgets — so renaming,
 * selection and the path context menu keep working untouched.
 */

#include <QRect>
#include <QStyledItemDelegate>

class MaskListDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    /// Width of the eye hit target at the left of a row.
    static constexpr int kEyeColumnWidth = 20;
    /// Width of the colour swatch that follows it.
    static constexpr int kSwatchWidth = 10;
    /// Gap between the swatch and the file name.
    static constexpr int kSwatchTextGap = 6;
    /// Left offset of the row text, reserved whether or not a swatch is drawn,
    /// so names do not shift when a mask is shown or hidden.
    static constexpr int kTextOffset = kEyeColumnWidth + kSwatchWidth + kSwatchTextGap;

    explicit MaskListDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    /// The eye's hit target inside a row rect, in the same coordinates.
    static QRect eyeRect(const QRect &itemRect);
};
