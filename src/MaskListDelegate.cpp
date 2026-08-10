#include "MaskListDelegate.h"

#include "MaskLayers.h"
#include "Theme.h"
#include "UiUtils.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QVariant>

namespace
{
// Ink for each eye state. Pinned is the accent because pinning is a state the
// user put the row into; active is metadata ink because it is merely where the
// selection happens to be; hidden is dimmer still. A selected row is already
// accent-filled, so there the ink has to come off the fill instead.
QColor eyeInk(MaskVisibility visibility, bool selected)
{
    if (selected)
    {
        QColor ink(Theme::kOnAccent);
        ink.setAlpha(visibility == MaskVisibility::Pinned  ? 255
                     : visibility == MaskVisibility::Active ? 190
                                                            : 130);
        return ink;
    }

    switch (visibility)
    {
    case MaskVisibility::Pinned:
        return QColor(Theme::kAccent);
    case MaskVisibility::Active:
        return QColor(Theme::kInk3);
    case MaskVisibility::Hidden:
    default:
        return QColor(Theme::kInkDisabled);
    }
}

// The eye, drawn in a square box: almond outline plus pupil when open, and a
// slash across it when closed.
void drawEyeGlyph(QPainter *painter, const QRectF &box, bool open, const QColor &ink, const QColor &backing)
{
    const qreal s = std::min(box.width(), box.height());
    const QPointF o(box.center().x() - s / 2.0, box.center().y() - s / 2.0);
    const auto p = [&o, s](qreal x, qreal y) { return QPointF(o.x() + x * s / 16.0, o.y() + y * s / 16.0); };

    QPainterPath almond;
    almond.moveTo(p(2.0, 8.0));
    almond.quadTo(p(8.0, 2.6), p(14.0, 8.0));
    almond.quadTo(p(8.0, 13.4), p(2.0, 8.0));

    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(ink, std::max(1.0, s / 13.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->drawPath(almond);

    if (open)
    {
        painter->setBrush(ink);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(box.center(), s * 2.2 / 16.0, s * 2.2 / 16.0);
        return;
    }

    // Back the slash with the row material so it reads as crossing the eye
    // rather than as one more stroke inside it.
    painter->setPen(QPen(backing, std::max(2.0, s / 5.0), Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(p(3.4, 13.0), p(12.6, 3.0));
    painter->setPen(QPen(ink, std::max(1.0, s / 13.0), Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(p(3.4, 13.0), p(12.6, 3.0));
}

// The swatch: one block per colour, stacked left to right inside the chip, so a
// multi-label mask shows a sample of its palette instead of one arbitrary hue.
void drawSwatch(QPainter *painter, const QRectF &box, const QList<QColor> &colors)
{
    if (colors.isEmpty())
        return;

    QPainterPath clip;
    clip.addRoundedRect(box, 3.0, 3.0);
    painter->save();
    painter->setClipPath(clip);
    const qreal bandHeight = box.height() / colors.size();
    for (int i = 0; i < colors.size(); ++i)
    {
        const QRectF band(box.left(), box.top() + i * bandHeight, box.width(), bandHeight + 0.5);
        painter->fillRect(band, colors.at(i));
    }
    painter->restore();

    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(QColor(Theme::kHairline), 1.0));
    painter->drawPath(clip);
}
} // namespace

MaskListDelegate::MaskListDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QRect MaskListDelegate::eyeRect(const QRect &itemRect)
{
    return QRect(itemRect.left(), itemRect.top(), kEyeColumnWidth, itemRect.height());
}

QSize MaskListDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setHeight(std::max(size.height(), 22));
    size.setWidth(size.width() + kTextOffset);
    return size;
}

void MaskListDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);

    // Row material and selection band first, full width — then everything else
    // is drawn by hand so the eye keeps its own ink instead of the text's.
    const QString text = opt.text;
    opt.text.clear();
    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const bool selected = (opt.state & QStyle::State_Selected);
    const auto visibility = static_cast<MaskVisibility>(index.data(UiUtils::kMaskVisibilityRole).toInt());
    const QRect eyeBox = eyeRect(option.rect);
    drawEyeGlyph(painter,
                 QRectF(eyeBox).adjusted(2.5, 2.5, -2.5, -2.5),
                 visibility != MaskVisibility::Hidden,
                 eyeInk(visibility, selected),
                 QColor(selected ? Theme::kAccentFill : Theme::kWell));

    if (visibility != MaskVisibility::Hidden)
    {
        QList<QColor> colors;
        for (const QVariant &value : index.data(UiUtils::kMaskSwatchRole).toList())
        {
            const QColor color = value.value<QColor>();
            if (color.isValid())
                colors.append(color);
        }
        const QRectF swatchBox(option.rect.left() + kEyeColumnWidth,
                               option.rect.top() + (option.rect.height() - 12) / 2.0,
                               kSwatchWidth,
                               12);
        drawSwatch(painter, swatchBox, colors);
    }

    // Match the row rules in Theme.cpp by hand: the style sheet's colours land
    // inside QStyleSheetStyle, not in the option palette this delegate holds.
    QColor ink(Theme::kInk2);
    if (selected)
        ink = QColor(Theme::kOnAccent);
    else if (const QVariant foreground = index.data(Qt::ForegroundRole); foreground.isValid())
        ink = foreground.value<QBrush>().color();

    const QRect textRect = option.rect.adjusted(kTextOffset, 0, -4, 0);
    painter->setPen(ink);
    painter->drawText(textRect,
                      Qt::AlignVCenter | Qt::AlignLeft,
                      opt.fontMetrics.elidedText(text, Qt::ElideMiddle, textRect.width()));
    painter->restore();
}
