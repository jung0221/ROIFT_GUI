#pragma once

#include <QGroupBox>

/**
 * SectionGroup — a labelled group that draws no box.
 *
 * The tool sidebar already encloses each section in one panel card. A framed
 * QGroupBox inside that card would be a second rectangle around the same
 * content, and four of them stacked is what made the old sidebar read as noise.
 * SectionGroup keeps QGroupBox's layout behaviour (so call sites are unchanged
 * beyond the type name) and replaces the frame with a single uppercase label in
 * metadata ink, per the interior rule that depth comes from material and never
 * from borders.
 *
 * The title is painted here rather than by QGroupBox because uppercase labels
 * need letter tracking, and Qt style sheets have no letter-spacing property.
 * Painting it directly also keeps the tracked QFont off the widget, so it is not
 * inherited by the fields inside the group.
 */
class SectionGroup : public QGroupBox
{
    Q_OBJECT
public:
    explicit SectionGroup(const QString &title, QWidget *parent = nullptr);

protected:
    bool event(QEvent *e) override;
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_marginsNormalised = false;
};
