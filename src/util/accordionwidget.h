#pragma once

#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

class CollapsibleSection : public QWidget
{
    Q_OBJECT

public:
    CollapsibleSection(const QString& title, QWidget* contentWidget, bool expanded = false, QWidget* parent = nullptr);

    QString title() const { return m_title; }
    QWidget* contentWidget() const { return m_contentWidget; }
    bool isExpanded() const { return m_isExpanded; }
    void setTitle(const QString& title) { m_title = title; }

    QWidget* replaceContentWidget(QWidget* newContentWidget);

public slots:
    void expand(bool doExpand);
    void setContentVisible(bool visible);

private slots:
    void onHandleDragged(int deltaY);

signals:
    void expansionChanged(bool isExpanded); // Signal for external sync

private:
    QString m_title;
    bool m_isExpanded;

    QWidget* m_contentWidget;
    QVBoxLayout* m_internalLayout;

    int m_lastExpandedHeight;
    int m_initalHeight;
    int m_minimumHeight;
};

//--------------------------------------------------------------------------------

// The main container widget holding multiple CollapsibleSections using QSplitter
class AccordionWidget : public QWidget
{
    Q_OBJECT

public:
    AccordionWidget(QWidget* parent = nullptr);

    // Add a new section to the accordion
    void addSection(
        const QString& title, QWidget* contentWidget, bool expanded = false, bool hideHeaderWhenCollapsed = true
    );

    // Find the CollapsibleSection containing the given content widget
    CollapsibleSection* findSectionByContent(QWidget* contentWidget) const;

    // Find the CollapsibleSection by its title
    CollapsibleSection* findSectionByTitle(const QString& title) const;

    // Remove the section associated with the contentWidget
    bool removeSectionByContent(QWidget* contentWidget);

    // Remove section by title
    bool removeSectionByTitle(const QString& title);

    // Replace the content of a section identified by its current content widget.
    void replaceContent(QWidget* oldContentWidget, QWidget* newContentWidget);

    // Replace content identified by section title
    void replaceContentByTitle(const QString& title, QWidget* newContentWidget);

    // Update button enabled state based on content
    void updateButtonState(const QString& title, bool enabled);

    // Notify all active PlotGraph widgets to update
    void notifyPlotsUpdate();

    // Update button styles to reflect current theme
    void updateButtonStyles();

    QSize sizeHint() const override;

public slots:
    void onSectionExpansionChanged(bool isExpanded);
    void onButtonClicked();

private:
    void updateLayoutStretches();
    QString createButtonStyleSheet() const;

    bool removeSectionInternal(CollapsibleSection* section);

    QVBoxLayout* m_mainLayout;

    QWidget* m_sectionsContainerWidget; // Widget inside scroll area holding m_mainLayout

    QWidget* m_buttonPanel;           // Button panel widget
    QHBoxLayout* m_buttonPanelLayout; // Layout for top panel

    QMap<CollapsibleSection*, QPushButton*> m_sectionButtonMap; // Map section -> panel button
};
