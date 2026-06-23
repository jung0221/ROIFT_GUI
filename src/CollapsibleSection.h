#pragma once

#include <QWidget>

class QToolButton;
class QLayout;
class QScrollArea;

// A lightweight, multi-open accordion section: a clickable header that expands
// or collapses a content area below it. Unlike QToolBox, several sections can be
// open at once, which suits a vertical tool sidebar. The header carries an
// objectName ("<sectionName>Header") so it can be themed from the central
// stylesheet, and the section itself is named "<sectionName>Section".
class CollapsibleSection : public QWidget
{
    Q_OBJECT
public:
    // sectionName is a stable identifier used for QSS object names and for
    // persisting the expanded state via QSettings.
    explicit CollapsibleSection(const QString &title, const QString &sectionName, QWidget *parent = nullptr);

    // Takes ownership of `layout` and installs it as the content layout.
    void setContentLayout(QLayout *layout);

    void setExpanded(bool expanded);
    bool isExpanded() const;

    QString sectionName() const { return m_sectionName; }

signals:
    void toggledExpanded(bool expanded);

private:
    void applyExpandedState();

    QString m_sectionName;
    QToolButton *m_headerButton = nullptr;
    QWidget *m_contentWidget = nullptr;
    bool m_expanded = true;
};
