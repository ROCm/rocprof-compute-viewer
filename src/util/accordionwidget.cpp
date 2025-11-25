#include "accordionwidget.h"
#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include "config/config.hpp"
#include "graphics/plot.h"
#include "memtracker.h"

CollapsibleSection::CollapsibleSection(
    const QString& title, QWidget* contentWidget, bool expanded /*= false*/, QWidget* parent /*= nullptr*/
) :
QWidget(parent),
m_title(title),
m_isExpanded(expanded),
m_contentWidget(contentWidget),
m_lastExpandedHeight(0),
m_minimumHeight(30),
m_initalHeight(120)
{
    m_internalLayout = new QVBoxLayout(this);
    m_internalLayout->setContentsMargins(0, 0, 0, 0);
    m_internalLayout->setSpacing(0);

    // If no content widget provided, create a placeholder
    if (!m_contentWidget)
    {
        m_contentWidget = new QWidget(this);
        m_contentWidget->setMinimumHeight(50);
        QLabel* label = new QLabel("No content", m_contentWidget);
        label->setAlignment(Qt::AlignCenter);
        QVBoxLayout* tempLayout = new QVBoxLayout(m_contentWidget);
        tempLayout->addWidget(label);
    }

    // Add content directly to layout
    m_internalLayout->addWidget(m_contentWidget);

    // Set size policies
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_contentWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_contentWidget->setMinimumHeight(0);

    setContentVisible(expanded);
}

void CollapsibleSection::expand(bool doExpand)
{
    if (m_isExpanded == doExpand) return;
    m_isExpanded = doExpand;
    setContentVisible(doExpand);
    emit expansionChanged(doExpand);
}

QWidget* CollapsibleSection::replaceContentWidget(QWidget* newContentWidget)
{
    // Remove old widget from layout
    QWidget* oldWidget = m_contentWidget;

    if (oldWidget) { m_internalLayout->removeWidget(oldWidget); }

    m_contentWidget = newContentWidget;

    if (m_contentWidget)
    {
        m_contentWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        m_contentWidget->setMinimumHeight(0);
        m_internalLayout->addWidget(m_contentWidget);
    }

    updateGeometry();
    return oldWidget;
}

void CollapsibleSection::setContentVisible(bool visible)
{
    if (m_contentWidget) { m_contentWidget->setVisible(visible); }

    // When collapsed, hide the entire section to save space
    if (!visible)
    {
        setMaximumHeight(0);
        setMinimumHeight(0);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }
    else
    {
        setMaximumHeight(QWIDGETSIZE_MAX);
        setMinimumHeight(m_minimumHeight);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    }

    updateGeometry();
}

void CollapsibleSection::onHandleDragged(int deltaY) { Q_UNUSED(deltaY); }

//-----------------------------------------------------------------------------

AccordionWidget::AccordionWidget(QWidget* parent /* = nullptr */) : QWidget(parent)
{
    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0); // Space between panel and sections

    // Button Panel Setup
    m_buttonPanel = new QWidget(this);
    m_buttonPanel->setObjectName("AccordionButtonPanel");
    m_buttonPanel->setStyleSheet(
        "#AccordionButtonPanel { background-color: palette(Window); padding: 0px; margin: 0px; }"
    );
    m_buttonPanelLayout = new QHBoxLayout(m_buttonPanel);
    m_buttonPanelLayout->setContentsMargins(0, 0, 0, 0); // No padding
    m_buttonPanelLayout->setSpacing(0);
    m_buttonPanelLayout->addStretch(); // Push buttons to the left

    // Widget containing the sections layout
    m_sectionsContainerWidget = new QWidget(this);
    m_sectionsContainerWidget->setObjectName("AccordionContent");
    m_mainLayout = new QVBoxLayout(m_sectionsContainerWidget);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(1);
    m_sectionsContainerWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    // Add panel and sections container to the outer layout
    outerLayout->addWidget(m_buttonPanel);
    outerLayout->addWidget(m_sectionsContainerWidget, 1); // Give it stretch factor

    // Ensure the accordion itself is visible
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    setMinimumHeight(100);
}

void AccordionWidget::addSection(
    const QString& title, QWidget* contentWidget, bool expanded /*= false*/, bool hideHeaderWhenCollapsed /*= true*/
)
{
    CollapsibleSection* section = new CollapsibleSection(title, contentWidget, expanded, this);
    m_mainLayout->addWidget(section);

    // Add to button panel
    QPushButton* panelButton = new QPushButton(title, m_buttonPanel);
    panelButton->setCheckable(true);
    panelButton->setChecked(section->isExpanded()); // Sync initial state

    // Create border color slightly lighter than background
    QColor borderColor = WindowColors::HotspotBkg().lighter(140);
    QColor checkedBgColor = WindowColors::HotspotBkg().lighter(120);
    QString styleSheet = QString("QPushButton { "
                                 "  border: 1px solid rgb(%1,%2,%3); "
                                 "  border-bottom: 2px solid transparent; "
                                 "  border-top-left-radius: 2px; "
                                 "  border-top-right-radius: 2px; "
                                 "  border-bottom-left-radius: 0px; "
                                 "  border-bottom-right-radius: 0px; "
                                 "  padding: 4px 10px; "
                                 "  margin: 0px; "
                                 "} "
                                 "QPushButton:checked { "
                                 "  border-bottom: 2px solid palette(Highlight); "
                                 "  background-color: rgb(%4,%5,%6); "
                                 "}")
                             .arg(borderColor.red())
                             .arg(borderColor.green())
                             .arg(borderColor.blue())
                             .arg(checkedBgColor.red())
                             .arg(checkedBgColor.green())
                             .arg(checkedBgColor.blue());
    panelButton->setStyleSheet(styleSheet);

    // Add shadow effect using QGraphicsDropShadowEffect
    QColor shadowColor = WindowColors::HotspotBkg().darker(200);
    shadowColor.setAlpha(100);
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(panelButton);
    shadow->setBlurRadius(4);
    shadow->setColor(shadowColor);
    shadow->setOffset(0, 1);
    panelButton->setGraphicsEffect(shadow);

    panelButton->installEventFilter(this);
    m_buttonPanelLayout->insertWidget(m_buttonPanelLayout->count() - 1, panelButton);

    m_sectionButtonMap.insert(section, panelButton); // Add to look up map

    connect(panelButton, &QPushButton::clicked, this, &AccordionWidget::onButtonClicked);
    connect(section, &CollapsibleSection::expansionChanged, panelButton, &QPushButton::setChecked);
    connect(section, &CollapsibleSection::expansionChanged, this, &AccordionWidget::onSectionExpansionChanged);

    updateLayoutStretches();
}

CollapsibleSection* AccordionWidget::findSectionByContent(QWidget* contentWidget) const
{
    if (!contentWidget) return nullptr;
    // Iterate through layout items
    for (int i = 0; i < m_mainLayout->count(); ++i)
    {
        CollapsibleSection* section = qobject_cast<CollapsibleSection*>(m_mainLayout->itemAt(i)->widget());
        if (section && section->contentWidget() == contentWidget) { return section; }
    }
    return nullptr;
}

CollapsibleSection* AccordionWidget::findSectionByTitle(const QString& title) const
{
    for (int i = 0; i < m_mainLayout->count(); ++i)
    {
        CollapsibleSection* section = qobject_cast<CollapsibleSection*>(m_mainLayout->itemAt(i)->widget());
        if (section && section->title() == title) { return section; }
    }
    return nullptr;
}

bool AccordionWidget::removeSectionByContent(QWidget* contentWidget)
{
    CollapsibleSection* section = findSectionByContent(contentWidget);
    bool result = removeSectionInternal(section);
    QWARNING(result, "Could not find section containing widget:" << contentWidget, );
    return result;
}

bool AccordionWidget::removeSectionByTitle(const QString& title)
{
    CollapsibleSection* section = findSectionByTitle(title);
    bool result = removeSectionInternal(section);
    QWARNING(result, "Could not find section titled:" << title.toStdString(), );
    return false;
}

bool AccordionWidget::removeSectionInternal(CollapsibleSection* section)
{
    if (section)
    {
        QPushButton* panelButton = m_sectionButtonMap.value(section, nullptr);
        m_sectionButtonMap.remove(section);

        // Remove panel button from layout and delete
        if (panelButton)
        {
            m_buttonPanelLayout->removeWidget(panelButton);
            panelButton->deleteLater();
        }

        // QVBoxLayout::removeWidget just removes from layout management
        m_mainLayout->removeWidget(section);
        section->deleteLater();
        updateGeometry(); // Update size hint after removal
        return true;
    }
    return false;
}

void AccordionWidget::replaceContent(QWidget* oldContentWidget, QWidget* newContentWidget)
{
    CollapsibleSection* section = findSectionByContent(oldContentWidget);
    QWARNING(section, "Could not find section containing widget:" << oldContentWidget, return );

    section->replaceContentWidget(newContentWidget);
    updateButtonState(section->title(), newContentWidget != nullptr);
    updateGeometry(); // Update size hint after replacement
}

void AccordionWidget::replaceContentByTitle(const QString& title, QWidget* newContentWidget)
{
    CollapsibleSection* section = findSectionByTitle(title);
    QWARNING(section, "Could not find section titled:" << title.toStdString(), return );

    section->replaceContentWidget(newContentWidget);
    updateButtonState(title, newContentWidget != nullptr);
    updateGeometry(); // Update size hint after replacement
}

void AccordionWidget::onSectionExpansionChanged(bool isExpanded)
{
    Q_UNUSED(isExpanded);
    updateLayoutStretches();
}

void AccordionWidget::onButtonClicked()
{
    QPushButton* clickedButton = qobject_cast<QPushButton*>(sender());
    if (!clickedButton) return;

    CollapsibleSection* clickedSection = m_sectionButtonMap.key(clickedButton, nullptr);
    if (!clickedSection) return;

    bool isCtrlPressed = QApplication::keyboardModifiers() & Qt::ControlModifier;

    // Check if clicking on the only open section
    int openSectionCount = 0;
    for (auto it = m_sectionButtonMap.constBegin(); it != m_sectionButtonMap.constEnd(); ++it)
    {
        if (it.key()->isExpanded()) { openSectionCount++; }
    }

    // Prevent closing if this is the only open section and user is trying to close it
    if (openSectionCount == 1 && clickedSection->isExpanded() && !isCtrlPressed) { return; }

    if (!isCtrlPressed)
    {
        // Regular tab behavior: close all other sections
        for (auto it = m_sectionButtonMap.constBegin(); it != m_sectionButtonMap.constEnd(); ++it)
        {
            CollapsibleSection* section = it.key();
            if (section != clickedSection && section->isExpanded()) { section->expand(false); }
        }
    }

    // Toggle the clicked section
    clickedSection->expand(!clickedSection->isExpanded());
}

void AccordionWidget::updateLayoutStretches()
{
    for (int i = 0; i < m_mainLayout->count(); ++i)
    {
        QLayoutItem* item = m_mainLayout->itemAt(i);
        if (item->widget())
        {
            CollapsibleSection* section = qobject_cast<CollapsibleSection*>(item->widget());
            if (section) { m_mainLayout->setStretchFactor(section, section->isExpanded() ? 1 : 0); }
        }
    }
}

void AccordionWidget::updateButtonState(const QString& title, bool enabled)
{
    CollapsibleSection* section = findSectionByTitle(title);
    if (!section) return;

    QPushButton* button = m_sectionButtonMap.value(section, nullptr);
    if (button)
    {
        button->setEnabled(enabled);
        // If disabling and section is expanded, collapse it
        if (!enabled && section->isExpanded())
        {
            // Find another section to expand
            for (auto it = m_sectionButtonMap.constBegin(); it != m_sectionButtonMap.constEnd(); ++it)
            {
                if (it.key() != section && it.value()->isEnabled())
                {
                    it.key()->expand(true);
                    break;
                }
            }
            section->expand(false);
        }
    }
}

void AccordionWidget::notifyPlotsUpdate()
{
    for (int i = 0; i < m_mainLayout->count(); ++i)
    {
        CollapsibleSection* section = qobject_cast<CollapsibleSection*>(m_mainLayout->itemAt(i)->widget());
        if (section && section->isExpanded() && section->contentWidget())
        {
            // Check if content is a PlotGraph
            if (auto* plot = qobject_cast<class PlotGraph*>(section->contentWidget())) { plot->update(); }
        }
    }
}

QSize AccordionWidget::sizeHint() const
{
    // Find the maximum size hint among all expanded sections
    QSize maxSize(200, 200);
    bool foundExpanded = false;

    for (int i = 0; i < m_mainLayout->count(); ++i)
    {
        CollapsibleSection* section = qobject_cast<CollapsibleSection*>(m_mainLayout->itemAt(i)->widget());
        if (section && section->isExpanded() && section->contentWidget())
        {
            QSize contentSize = section->contentWidget()->sizeHint();
            maxSize = maxSize.expandedTo(contentSize);
            foundExpanded = true;
        }
    }

    // Add button panel height
    int buttonPanelHeight = m_buttonPanel ? m_buttonPanel->sizeHint().height() : 0;
    return QSize(maxSize.width(), maxSize.height() + buttonPanelHeight);
}
