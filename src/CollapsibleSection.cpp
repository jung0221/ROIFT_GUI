#include "CollapsibleSection.h"

#include <QToolButton>
#include <QVBoxLayout>

CollapsibleSection::CollapsibleSection(const QString &title, const QString &sectionName, QWidget *parent)
    : QWidget(parent), m_sectionName(sectionName)
{
    setObjectName(sectionName + "Section");

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_headerButton = new QToolButton(this);
    m_headerButton->setObjectName(sectionName + "Header");
    m_headerButton->setText(title);
    m_headerButton->setCheckable(true);
    m_headerButton->setChecked(true);
    m_headerButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_headerButton->setArrowType(Qt::DownArrow);
    m_headerButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_headerButton->setCursor(Qt::PointingHandCursor);
    // Self-contained header styling (the section owns its own look so callers
    // don't need scattered inline stylesheets).
    m_headerButton->setStyleSheet(R"(
        QToolButton {
            background-color: #333337;
            border: 1px solid #444444;
            border-radius: 3px;
            padding: 6px 8px;
            font-weight: bold;
            font-size: 11px;
            color: #e8e8e8;
            text-align: left;
        }
        QToolButton:hover {
            background-color: #3c3c44;
            border-color: #0078d4;
        }
        QToolButton:checked {
            background-color: #333337;
        }
    )");
    outer->addWidget(m_headerButton);

    m_contentWidget = new QWidget(this);
    m_contentWidget->setObjectName(sectionName + "Content");
    outer->addWidget(m_contentWidget);

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
        m_headerButton->setArrowType(m_expanded ? Qt::DownArrow : Qt::RightArrow);
}
