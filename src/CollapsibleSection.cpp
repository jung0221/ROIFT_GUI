#include "CollapsibleSection.h"

#include "Theme.h"

#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{

/// Chevron drawn in code rather than shipped as an icon: two strokes, so it can
/// take the header's ink colour and rotate with the disclosure state.
QIcon makeChevronIcon(bool pointingDown)
{
    const int size = 10;
    QPixmap pm(size * 2, size * 2); // 2x so it stays crisp on hidpi
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(2.0, 2.0);

    QPen pen(QColor(Theme::kInk3));
    pen.setWidthF(1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);

    QPainterPath path;
    if (pointingDown)
    {
        path.moveTo(2.5, 4.0);
        path.lineTo(5.0, 6.5);
        path.lineTo(7.5, 4.0);
    }
    else
    {
        path.moveTo(4.0, 2.5);
        path.lineTo(6.5, 5.0);
        path.lineTo(4.0, 7.5);
    }
    p.drawPath(path);
    p.end();

    return QIcon(pm);
}

} // namespace

CollapsibleSection::CollapsibleSection(const QString &title, const QString &sectionName, QWidget *parent)
    : QWidget(parent), m_sectionName(sectionName)
{
    // Two object names: the stable per-section one callers may target, and the
    // shared "sectionCard" one the theme paints the panel material through.
    setObjectName(sectionName + "Section");
    setProperty("class", "sectionCard");

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // The card is the single box around a section. Nothing inside it draws a
    // second frame; groups inside are labels over content, not boxes.
    m_card = new QWidget(this);
    m_card->setObjectName("sectionCard");
    QVBoxLayout *cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(Theme::kCardPadding, Theme::kCardPadding,
                                   Theme::kCardPadding, Theme::kCardPadding);
    cardLayout->setSpacing(0);
    outer->addWidget(m_card);

    m_headerButton = new QToolButton(m_card);
    m_headerButton->setObjectName("sectionHeader");
    m_headerButton->setText(title);
    m_headerButton->setCheckable(true);
    m_headerButton->setChecked(true);
    m_headerButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_headerButton->setIcon(makeChevronIcon(true));
    m_headerButton->setIconSize(QSize(10, 10));
    m_headerButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_headerButton->setCursor(Qt::PointingHandCursor);
    cardLayout->addWidget(m_headerButton);

    m_contentWidget = new QWidget(m_card);
    m_contentWidget->setObjectName("sectionBody");
    cardLayout->addWidget(m_contentWidget);

    connect(m_headerButton, &QToolButton::toggled, this, [this](bool checked)
            { setExpanded(checked); });
}

void CollapsibleSection::setContentLayout(QLayout *layout)
{
    if (!m_contentWidget)
        return;
    // Replace any previously installed layout (Qt deletes the old one when a new
    // layout is assigned through delete-on-reparent semantics).
    if (QLayout *old = m_contentWidget->layout())
        delete old;
    // Content sits inset from the header text by the card padding, so a group
    // label lines up with the section title above it.
    layout->setContentsMargins(3, Theme::kRowGap, 3, 3);
    m_contentWidget->setLayout(layout);
}

void CollapsibleSection::setExpanded(bool expanded)
{
    if (m_expanded == expanded && m_headerButton->isChecked() == expanded)
        return;
    m_expanded = expanded;
    if (m_headerButton->isChecked() != expanded)
    {
        QSignalBlocker blocker(m_headerButton);
        m_headerButton->setChecked(expanded);
    }
    applyExpandedState();
    emit toggledExpanded(expanded);
}

bool CollapsibleSection::isExpanded() const
{
    return m_expanded;
}

void CollapsibleSection::applyExpandedState()
{
    if (m_contentWidget)
        m_contentWidget->setVisible(m_expanded);
    if (m_headerButton)
        m_headerButton->setIcon(makeChevronIcon(m_expanded));
}
