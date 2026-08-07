#pragma once

#include <QWidget>

class QToolButton;
class QLayout;
class QScrollArea;

// A lightweight, multi-open accordion section: a clickable header that expands
// or collapses a content area below it. Unlike QToolBox, several sections can be
// open at once, which suits a vertical tool sidebar.
//
// Visually the section is one panel card — the only box drawn around its
// contents. Groups placed inside it are labels over content, never further
// frames, so the sidebar reads as a stack of cards instead of nested rectangles.
// The section is named "<sectionName>Section"; the card, header and body carry
// the shared object names "sectionCard", "sectionHeader" and "sectionBody" that
// Theme::styleSheet() paints.
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
    QWidget *m_card = nullptr;
    QToolButton *m_headerButton = nullptr;
    QWidget *m_contentWidget = nullptr;
    bool m_expanded = true;
};
