#include "summaryview.h"
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QRectF>
#include <QVBoxLayout>
#include <QtMath> // For qDegreesToRadians, qCos, qSin, qAtan2, M_PI
#include "config/config.hpp"
#include "util/custom_layouts.h"
#include "util/memtracker.h"

PieChartWidget::PieChartWidget(QWidget* parent /*= nullptr*/) : QWidget(parent)
{
    setMouseTracking(true);
    chart_colors = {QColor("#FF7200"), QColor("#9C0000"), QColor("#FE0000")};
}

QSize PieChartWidget::minimumSizeHint() const
{
    return {std::max(200, QWidget::minimumSizeHint().width()), std::max(200, QWidget::minimumSizeHint().height())};
}

void PieChartWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (data.isEmpty()) return;

    float total = 0;
    for (const auto& item : data) { total += item.second; }
    if (total == 0) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor titleColor = palette().color(QPalette::WindowText);
    QColor lineColor = WindowColors::UtilizationBarColorBg();
    QColor textColor = palette().color(QPalette::WindowText);

    // measure title size
    QFontMetrics fm = painter.fontMetrics();
    QRect titleRect = fm.boundingRect(chart_title);
    painter.setPen(titleColor);
    QRect titlePosRect = QRect(
        (width() - titleRect.width()) / 2.0, 0, titleRect.width(), titleRect.height()
    ); // Title position rectangle
    painter.drawText(titlePosRect, Qt::AlignHCenter | Qt::AlignTop, chart_title);
    QRect canvasRect = QRect(0, 0, width(), height() - titleRect.height()); // Adjusted for title height

    // Maintain circular aspect ratio
    int side = std::max(qMin(canvasRect.width(), canvasRect.height()), 100) - 20; // -20 for padding
    pie_rect = QRectF(
        (canvasRect.width() - side) / 2.0, (canvasRect.height() - side) / 2.0 + titleRect.height(), side, side
    ); // Store for mouseMoveEvent

    int currentAngle16 = 0; // Start angle in 1/16th of a degree
    slice_regions.clear();

    for (int i = 0; i < data.size(); ++i)
    {
        const auto& item = data[i];
        int spanAngle16 = static_cast<int>((item.second / total) * 360.0 * 16.0);

        painter.setBrush(chart_colors[i % chart_colors.size()]); // Use color from char_colors vector
        painter.setPen(Qt::NoPen);                               // Set no pen for pie slices
        painter.drawPie(pie_rect, currentAngle16, spanAngle16);
        slice_regions.append({currentAngle16, spanAngle16});
        currentAngle16 += spanAngle16;
    }

    // Highlight hovered slice
    if (hovered_index >= 0 && hovered_index < slice_regions.size())
    {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(lineColor, 2, Qt::DashLine));
        // Ensure sliceRegions[hoveredIndex] is valid
        if (hovered_index < slice_regions.size())
        {
            painter.drawPie(pie_rect, slice_regions[hovered_index].first, slice_regions[hovered_index].second);
        }

        const auto& item = data[hovered_index];
        int angle16 = slice_regions[hovered_index].first;
        int spanAngle16 = slice_regions[hovered_index].second;
        // Calculate the middle angle of the current slice for label placement
        qreal midAngleDeg = (angle16 + spanAngle16 * 0.5) / 16.0;
        qreal radians = qDegreesToRadians(midAngleDeg);
        // Position the label.
        QPointF labelPos = pie_rect.center() + QPointF(qCos(radians), -qSin(radians)) * pie_rect.width() * 0.3;

        // Set pen for text
        painter.setPen(textColor);
        QString itemLabel = item.first;
        QString numericValueStr = QString::number(item.second, 'f', 0);
        if (!draw_value_on_newline)
        {
            itemLabel += " (" + numericValueStr + ")"; // Combine text and numeric value for display
        }
        QRect textRect = fm.boundingRect(itemLabel);
        // Center text
        QPointF textDrawPos = labelPos - QPointF(textRect.width() / 2.0, -textRect.height() / 2.0);
        painter.drawText(textDrawPos, itemLabel);

        if (draw_value_on_newline)
        {
            QRect numericValueRect = fm.boundingRect(numericValueStr);
            qreal spacing = fm.lineSpacing() * 0.5;
            QPointF numericValueDrawPos =
                QPointF(labelPos.x() - numericValueRect.width() / 2.0, labelPos.y() + textRect.height() + spacing);
            painter.drawText(numericValueDrawPos, numericValueStr);
        }
    }
}

void PieChartWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (pie_rect.isEmpty() || data.isEmpty() || slice_regions.isEmpty())
    { // Guard against uninitialized rect or no data
        if (hovered_index != -1)
        {
            hovered_index = -1;
            update();
        }
        return;
    }

    QPointF center = pie_rect.center();
    QPointF delta = event->pos() - center;

    // Check if mouse is outside the pie radius (optional, but good for accuracy)
    qreal distSq = QPointF::dotProduct(delta, delta);
    if (distSq > (pie_rect.width() / 2.0) * (pie_rect.width() / 2.0))
    {
        if (hovered_index != -1)
        {
            hovered_index = -1;
            update();
        }
        return;
    }

    qreal angleDeg = qRadiansToDegrees(qAtan2(-delta.y(), delta.x())); // Note: -delta.y() because Qt's Y is inverted
    if (angleDeg < 0) angleDeg += 360.0;

    int angle16 = static_cast<int>(angleDeg * 16.0);
    int newHoveredIndex = -1;

    for (int i = 0; i < slice_regions.size(); ++i)
    {
        int sliceStartAngle16 = slice_regions[i].first;
        int sliceSpanAngle16 = slice_regions[i].second;

        int sliceEndAngle16 = sliceStartAngle16 + sliceSpanAngle16;

        // Simple check (works if no slice crosses the 0-degree line in a weird way or if angles are always < 360*16
        // after sum)
        if (sliceSpanAngle16 > 0)
        { // Only check valid slices
            if (sliceEndAngle16 > 360 * 16)
            { // Slice crosses the 0-degree boundary
                if ((angle16 >= sliceStartAngle16 && angle16 < 360 * 16) ||
                    (angle16 >= 0 && angle16 < (sliceEndAngle16 % (360 * 16))))
                {
                    newHoveredIndex = i;
                    break;
                }
            }
            else
            { // Normal case
                if (angle16 >= sliceStartAngle16 && angle16 < sliceEndAngle16)
                {
                    newHoveredIndex = i;
                    break;
                }
            }
        }
    }

    if (newHoveredIndex != hovered_index)
    {
        hovered_index = newHoveredIndex;
        update();
    }
}

constexpr int margin = 50;
constexpr int barWidth = 40;
constexpr int spacing = 30;

int BarChartWidget::getsizex() const { return (barWidth + spacing) * data.size() + spacing; }

QSize BarChartWidget::sizeHint() const { return QSize(getsizex(), QWidget::sizeHint().height()); }

QSize BarChartWidget::minimumSizeHint() const { return QSize(getsizex(), QWidget::minimumSizeHint().height()); }

void BarChartWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor titleColor = palette().color(QPalette::WindowText);
    QColor textColor = palette().color(QPalette::WindowText);
    QColor outlineColor = palette().color(QPalette::Dark);
    QColor backgroundColor = WindowColors::UtilizationBarColorBg();

    QFontMetrics fm(painter.font());
    // measure title size
    QRect titleRect = fm.boundingRect(chart_title);
    painter.setPen(titleColor);
    QRect titlePosRect = QRect(
        (width() - titleRect.width()) / 2.0, 0, titleRect.width(), titleRect.height()
    ); // Title position rectangle
    painter.drawText(titlePosRect, Qt::AlignHCenter | Qt::AlignTop, chart_title);

    int maxHeight = height() - 2 * margin;
    int totalWidth = data.size() * (barWidth + spacing) + spacing; // Total width of the chart

    for (int i = 0; i < data.size(); ++i)
    {
        const QString& label = std::get<0>(data[i]);
        double value = std::get<1>(data[i]);
        QColor barColor = std::get<2>(data[i]);

        int barHeight = static_cast<int>(std::min(value / 100.0, 1.0) * maxHeight + 0.5);
        int x = i * barWidth + spacing * (i + 2);
        int y = height() - margin - barHeight;

        // Draw background bar
        QRect backgroundRect(x, height() - margin - maxHeight, barWidth, maxHeight);
        painter.setPen(Qt::NoPen);
        painter.setBrush(backgroundColor);
        painter.drawRect(backgroundRect);

        // Draw bar
        painter.setPen(outlineColor);
        QRect barRect(x, y, barWidth, barHeight);
        painter.setBrush(barColor);
        painter.drawRect(barRect);

        // Draw value above the bar
        QString valueText = QString::number(value, 'f', 1) + "%";
        int textWidth = fm.horizontalAdvance(valueText);
        int textX = x + (barWidth - textWidth) / 2;
        int textY = y - fm.height() - 5; // 5 pixels above the bar
        painter.setPen(textColor);
        painter.drawText(textX, textY, valueText);

        // Draw label centered under the bar
        textWidth = fm.horizontalAdvance(label);
        textX = x + (barWidth - textWidth) / 2;
        textY = height() - margin + fm.height() + 5;
        painter.setPen(textColor);
        painter.drawText(textX, textY, label);
    }

    if (draw_axis)
    {
        // Draw axes
        painter.drawLine(spacing, height() - margin, spacing + totalWidth, height() - margin); // X-axis
        painter.drawLine(spacing, margin, spacing, height() - margin);                         // Y-axis
    }
}

SummaryView::SummaryView(QWidget* parent) :
QWidget{parent},
tableWidget(nullptr),
pieChartWidget(nullptr),
barChartWidget(nullptr),
pieChartContainer(nullptr),
legendTable(nullptr),
tableContainer(nullptr)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Create a label to display the summary
    QLabel* summaryLabel = new QLabel("Summary View", this);
    summaryLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(summaryLabel);

    // Create a container for the pie chart and bar chart
    QWidget* chartContainer = new QWidget(this);
    QHBoxLayout* chartLayout = new QHBoxLayout(chartContainer);
    chartLayout->setContentsMargins(0, 0, 0, 0);
    chartLayout->setSpacing(1);
    layout->addWidget(chartContainer);

    barChartWidget = new BarChartWidget(this);
    barChartWidget->setMinimumHeight(180); // Set a minimum height for the bar chart
    barChartWidget->setMaximumHeight(300);
    chartLayout->addWidget(barChartWidget);

    // Create a container for the pie chart and legend table
    pieChartContainer = new QWidget(this);
    QHBoxLayout* pieChartLayout = new QHBoxLayout(pieChartContainer);
    pieChartLayout->setContentsMargins(0, 0, 0, 0);
    pieChartLayout->setSpacing(2);
    pieChartContainer->setLayout(pieChartLayout);
    pieChartContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Create legend table
    QWidget* legendContainer = new QWidget(this);
    QVBoxLayout* legendLayout = new QVBoxLayout(legendContainer);
    legendLayout->setContentsMargins(0, 0, 0, 0);
    legendLayout->setSpacing(0);
    legendContainer->setLayout(legendLayout);
    legendTable = new QTableWidget(this);
    setupPieChartLegendTable();
    legendLayout->addStretch(1);
    legendLayout->addWidget(legendTable);
    legendLayout->addStretch(1);

    // Create pie chart widget
    pieChartWidget = new PieChartWidget(this);
    pieChartWidget->setMinimumHeight(140); // Set a minimum height for the pie chart
    pieChartWidget->setMaximumHeight(240);

    pieChartLayout->addWidget(pieChartWidget, 1);
    pieChartLayout->addWidget(legendContainer, 0);

    chartLayout->addWidget(pieChartContainer);
    chartLayout->setStretchFactor(barChartWidget, 1);
    chartLayout->setStretchFactor(pieChartContainer, 1);
    chartContainer->setLayout(chartLayout);

    tableContainer = new QWidget(this);
    auto* tableLayout = new QVBox(tableContainer);
    tableWidget = new QTableWidget(0, 0, this);
    tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableWidget->setSelectionMode(QAbstractItemView::NoSelection);
    tableWidget->setMinimumHeight(120);
    tableWidget->setToolTip(
        "Load balancing: Utilization for counter X is computed as X/BUSY_CU_CYCLES.\nPeak rates indicate maximum "
        "across any given cycle.\nPer-CU rates are averaged over the period in which any wave was present in the CU."
    );
    tableLayout->addWidget(tableWidget);
    layout->addWidget(tableContainer);

    // Hide the table widget and charts initially
    tableContainer->setVisible(false);
    barChartWidget->setVisible(false);
    pieChartContainer->setVisible(false);

    // Set the layout for this widget
    setLayout(layout);
    // Set the size policy to expand in both directions
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void SummaryView::setTableHeaders(const QStringList& headers)
{
    QWARNING(tableWidget, "No table widget!", return );

    tableWidget->setColumnCount(headers.size());
    tableWidget->setHorizontalHeaderLabels(headers);
}

void SummaryView::setTableRowHeaders(const QStringList& headers)
{
    QWARNING(tableWidget, "No table widget!", return );

    tableWidget->setVerticalHeaderLabels(headers);
    tableWidget->resizeColumnsToContents();
}

void SummaryView::addTableRow(const QList<QString>& rowData)
{
    QWARNING(tableWidget, "Tablewidget is null!", return );

    int row = tableWidget->rowCount();
    tableWidget->insertRow(row);
    for (int col = 0; col < rowData.size(); ++col)
    {
        const auto& item = rowData[col];
        QTableWidgetItem* tableItem = new QTableWidgetItem(item);
        tableItem->setTextAlignment(Qt::AlignCenter);
        tableWidget->setItem(row, col, tableItem);
    }
    if (tableContainer && rowData.size() > 0 && !tableContainer->isVisible()) { tableContainer->setVisible(true); }
}

void SummaryView::clearTableData()
{
    if (tableWidget)
    {
        tableWidget->clearContents();
        tableWidget->setRowCount(0);
        tableWidget->setVerticalHeaderLabels(QStringList());
        if (tableContainer) { tableContainer->setVisible(false); }
        tableContainer->setVisible(false);
    }
}

void SummaryView::setBarChartData(const QList<std::tuple<QString, float, QColor>>& data, const QString& title)
{
    if (barChartWidget)
    {
        barChartWidget->setData(data);
        barChartWidget->setTitle(title);
        if (data.size() > 0) { barChartWidget->setVisible(true); }
        else { barChartWidget->setVisible(false); }
    }
}

void SummaryView::setPieChartData(const QList<QPair<QString, float>>& data, const QString& title)
{
    if (pieChartWidget && pieChartContainer)
    {
        pieChartWidget->setData(data);
        pieChartWidget->setTitle(title);
        if (data.size() > 0)
        {
            pieChartContainer->setVisible(true);
            legendTable->setRowCount(data.size());
            legendTable->setColumnCount(3); // 3 columns: Color, Description, Value
            legendTable->setHorizontalHeaderLabels(
                QStringList() << "Color"
                              << "Description"
                              << "Value"
            );
            for (int i = 0; i < data.size(); ++i)
            {
                const auto& item = data[i];
                QTableWidgetItem* colorItem = new QTableWidgetItem();
                auto chart_colors = pieChartWidget->getCharColors();
                colorItem->setBackground(QBrush(chart_colors[i % chart_colors.size()])); // Set the background color
                legendTable->setItem(i, 0, colorItem);                                   // Color column
                QTableWidgetItem* descriptionItem = new QTableWidgetItem(item.first);
                legendTable->setItem(i, 1, descriptionItem); // Description column
                QTableWidgetItem* valueItem = new QTableWidgetItem(QString::number(item.second, 'f', 1) + '%');
                legendTable->setItem(i, 2, valueItem); // Value column
            }
            legendTable->setRowCount(data.size());

            // Set the table to be a fixed height
            // Add heights of all rows
            int totalHeight = 0;
            for (int i = 0; i < legendTable->rowCount(); ++i) { totalHeight += legendTable->rowHeight(i); }
            totalHeight += legendTable->frameWidth() * 2;
            legendTable->setMinimumHeight(totalHeight);
            legendTable->setMaximumHeight(totalHeight);
        }
        else
        {
            pieChartContainer->setVisible(false);
            legendTable->clearContents();
            legendTable->setRowCount(0);
            legendTable->setVerticalHeaderLabels(QStringList());
        }
    }
}

void SummaryView::clearBarChartData()
{
    if (barChartWidget)
    {
        barChartWidget->setData(QList<std::tuple<QString, float, QColor>>());
        barChartWidget->setVisible(false);
    }
}

void SummaryView::clearPieChartData()
{
    if (pieChartWidget) { pieChartWidget->setData(QList<QPair<QString, float>>()); }
    if (pieChartContainer) { pieChartContainer->setVisible(false); }
}

void SummaryView::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setPen(Qt::NoPen);
    painter.setBrush(WindowColors::Background());
    painter.drawRect(QRect(0, 0, width(), height()));
    QWidget::paintEvent(event);
}

void SummaryView::setupPieChartLegendTable()
{
    if (!legendTable) { return; }

    legendTable->setColumnCount(3);

    // Hide headers
    legendTable->verticalHeader()->setVisible(false);
    legendTable->horizontalHeader()->setVisible(false);

    // Disable Selection and Editing
    legendTable->setSelectionMode(QAbstractItemView::NoSelection);
    legendTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Appearance and Behavior
    legendTable->setFocusPolicy(Qt::NoFocus);
    legendTable->setShowGrid(false);
    legendTable->setFrameShape(QFrame::NoFrame);
    legendTable->viewport()->setAutoFillBackground(false);

    // Column Resizing
    legendTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed); // Color column fixed size
    legendTable->setColumnWidth(0, 20);
    legendTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents); // Description stretches
    legendTable->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents
    ); // Value column fits content

    legendTable->setSizePolicy(legendTable->sizePolicy().horizontalPolicy(), QSizePolicy::Maximum);
}
