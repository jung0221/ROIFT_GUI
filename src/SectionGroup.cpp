#include "SectionGroup.h"

#include "Theme.h"

#include <QEvent>
#include <QFont>
#include <QLayout>
#include <QPainter>

SectionGroup::SectionGroup(const QString &title, QWidget *parent)
    : QGroupBox(title.toUpper(), parent)
{
    // The style sheet reserves the label band above the contents by way of
    // QGroupBox's top margin; see the QGroupBox rule in Theme::styleSheet().
    setObjectName("sectionGroup");
    setFlat(true);
}

bool SectionGroup::event(QEvent *e)
{
    // Call sites inset their content to clear the old QGroupBox frame. There is
    // no frame any more, so that inset only pushes the controls out of line with
    // the label above them. Flatten it the first time the layout reports in.
    if (e->type() == QEvent::LayoutRequest && !m_marginsNormalised)
    {
        if (QLayout *l = layout())
        {
            m_marginsNormalised = true;
            l->setContentsMargins(0, 0, 0, 0);
        }
    }
    return QGroupBox::event(e);
}

void SectionGroup::paintEvent(QPaintEvent *)
{
    QPainter p(this);

    QFont f = font();
    f.setPixelSize(Theme::kLabelFontSize);
    f.setWeight(QFont::DemiBold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, Theme::kLabelTracking);
    p.setFont(f);
    p.setPen(QColor(Theme::kInk3));

    const QRect band(1, 0, width() - 2, Theme::kLabelBand);
    p.drawText(band, Qt::AlignLeft | Qt::AlignVCenter, title());
}
