#include "hotspotsummarywidget.h"
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include "config/config.hpp"
#include "mainwindow.h"

constexpr size_t MAX_HOTSPOT_LINES = 10;

HotspotSummaryWidget::HotspotSummaryWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumWidth(400);
    setMinimumHeight(200);
    this->setAutoFillBackground(true);
    QPalette pal = QPalette();
    pal.setColor(QPalette::Window, WindowColors::Background());
    this->setPalette(pal);
}

void HotspotSummaryWidget::clear()
{
    hotspotLines.clear();
    fileDisplayName = "";
    update();
}

void HotspotSummaryWidget::setFile(const std::string& filePath, const std::string& displayName)
{
    // Store original display name
    fileDisplayName = QString::fromStdString(displayName);
    hotspotLines.clear();

    // Collection for hotspot data
    std::vector<std::pair<int, HorizontalHotspot>> lineLatencies;
    std::map<int, std::shared_ptr<SourceLine>> foundLines;

    for (const auto& [key, linePtr] : SourceLine::all_lines)
    {
        // Extract file path from key (remove line number if present)
        std::string keyPath = key;
        size_t colonPos = keyPath.find_last_of(':');
        if (colonPos != std::string::npos) { keyPath = keyPath.substr(0, colonPos); }

        // Compare exact paths
        if (keyPath == filePath && linePtr && linePtr->hotspot.combined() > 0)
        {
            lineLatencies.push_back({linePtr->line_number, linePtr->hotspot});
            foundLines[linePtr->line_number] = linePtr;
        }
    }

    // If no matches, log error
    if (lineLatencies.empty()) { qDebug() << "ERROR: No hotspots found for path:" << QString::fromStdString(filePath); }

    // Sort and take top hotspots
    std::sort(
        lineLatencies.begin(),
        lineLatencies.end(),
        [](const auto& a, const auto& b) { return a.second.combined() > b.second.combined(); }
    );

    maxLatency = lineLatencies.empty() ? 1 : lineLatencies[0].second.combined();

    // Create hotspot lines
    size_t numLines = std::min(lineLatencies.size(), MAX_HOTSPOT_LINES);
    for (size_t i = 0; i < numLines; i++)
    {
        int lineNumber = lineLatencies[i].first;
        HorizontalHotspot latency = lineLatencies[i].second;

        HotspotLine hotspotLine{};
        hotspotLine.lineNumber = lineNumber;
        hotspotLine.content = "[Line content not available]";

        // Use the already found SourceLine if available
        auto lineIt = foundLines.find(lineNumber);
        if (lineIt != foundLines.end() && lineIt->second)
        {
            auto& linePtr = lineIt->second;
            hotspotLine.content = linePtr->getText();
            latency = linePtr->hotspot;
        }

        hotspotLines.push_back({hotspotLine, latency});
    }

    update();
}

void HotspotSummaryWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);

    // Set fonts
    QFont titleFont = MainWindow::default_font.isEmpty() ? painter.font() : QFont(MainWindow::default_font);
    titleFont.setPointSize(MainWindow::font() + 2);
    titleFont.setBold(true);

    QFont contentFont = MainWindow::default_font.isEmpty() ? painter.font() : QFont(MainWindow::default_font);
    contentFont.setPointSize(MainWindow::font());

    QFont latencyFont = contentFont;
    latencyFont.setBold(true);

    // Get metrics
    painter.setFont(titleFont);
    QFontMetrics titleFm(titleFont);
    painter.setFont(contentFont);
    QFontMetrics contentFm(contentFont);
    painter.setFont(latencyFont);
    QFontMetrics latencyFm(latencyFont);

    int lineHeight = contentFm.height();
    int padding = 14;
    int hotspotBarWidth = 150;

    // Draw title
    painter.setFont(titleFont);
    painter.setPen(QPen(WindowColors::textColor(), 1));

    QString title = hotspotLines.empty()
                      ? "Click on a source file to view the Hotspots"
                      : "Top " + QString::number(hotspotLines.size()) + " Hotspots in " + fileDisplayName;

    painter.drawText(padding, padding + titleFm.ascent(), title);

    // Calculate column positions and widths
    int lineTextWidth = std::max(
                            contentFm.horizontalAdvance("Line 9999: "), // Handle large line numbers
                            contentFm.horizontalAdvance("Line")
                        ) +
                        padding;
    int cyclesTextWidth = latencyFm.horizontalAdvance("999.9M") + padding;
    int hotspotColumnX = padding;
    int cyclesColumnX = hotspotColumnX + hotspotBarWidth + padding;
    int lineColumnX = cyclesColumnX + cyclesTextWidth + padding;

    // Draw column headers
    int yPos = padding + titleFm.height() + padding * 1.5;
    painter.setFont(latencyFont);
    painter.setPen(QPen(WindowColors::textColor(), 1));

    painter.drawText(hotspotColumnX, yPos, "");
    painter.drawText(cyclesColumnX, yPos, "Cycles");
    painter.drawText(lineColumnX, yPos, "Line");

    // Add space after headers
    yPos += padding + lineHeight / 2;

    // Draw hotspot lines
    for (const auto& [line, latency] : hotspotLines)
    {
        // TODO: PC sampling
        float value_to_pixel_ratio = hotspotBarWidth / static_cast<float>(maxLatency);
        latency.paint(
            painter,
            hotspotColumnX,
            yPos - lineHeight + contentFm.descent() + 1,
            lineHeight - contentFm.descent(),
            value_to_pixel_ratio,
            value_to_pixel_ratio,
            HorizontalHotspot::DrawFormat::DRAWSTALL,
            false
        );

        // Draw cycles count
        QString latencyText;
        // TODO: not true
        int64_t lat = latency.combined();
        if (lat > 1000000) { latencyText = QString("%1M").arg(lat / 1000000.0, 0, 'f', 1); }
        else if (lat > 1000) { latencyText = QString("%1K").arg(lat / 1000.0, 0, 'f', 1); }
        else { latencyText = QString::number(lat); }

        painter.setFont(latencyFont);
        painter.setPen(QPen(WindowColors::LatencyTextColor(), 1));
        painter.drawText(cyclesColumnX, yPos, latencyText);

        // Draw line number and code
        QString lineNumText = QString::number(line.lineNumber + 1) + ":";
        painter.setFont(contentFont);
        painter.setPen(QPen(WindowColors::textColor(), 1));
        painter.drawText(lineColumnX, yPos, lineNumText);

        int lineNumWidth = contentFm.horizontalAdvance(lineNumText);
        int contentX = lineColumnX + lineNumWidth + padding / 2;
        int availableWidth = width() - contentX - padding;

        QString elidedText = contentFm.elidedText(line.content, Qt::ElideRight, availableWidth);
        painter.drawText(contentX, yPos, elidedText);

        yPos += lineHeight + padding * 0.75;
    }
}
