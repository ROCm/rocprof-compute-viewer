#pragma once
#include <QList>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPair>
#include <QString>
#include <QTableWidget>
#include <QWidget>

class PieChartWidget : public QWidget
{
    Q_OBJECT

public:
    PieChartWidget(QWidget* parent = nullptr);

    void setData(const QList<QPair<QString, float>>& newData)
    {
        this->data = newData;
        update(); // Trigger a repaint after setting new data
    }

    void setTitle(const QString& title)
    {
        chart_title = title;
        update(); // Trigger a repaint after setting new title
    }

    std::vector<QColor>& getCharColors() { return chart_colors; }

    void setChartColors(const std::vector<QColor>& colors)
    {
        chart_colors = colors;
        update(); // Trigger a repaint after setting new colors
    }

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    QList<QPair<QString, float>> data;
    QList<QPair<int, int>> slice_regions; // Store the regions of each slice
    int hovered_index = -1;
    QRectF pie_rect;
    std::vector<QColor> chart_colors;
    QString chart_title = "";
    bool draw_value_on_newline = false; // Flag to control value drawing on new line
};

class BarChartWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BarChartWidget(QWidget* parent = nullptr) : QWidget(parent) {}

    void setData(const QList<std::tuple<QString, float, QColor>>& data)
    {
        this->data = data;
        update(); // Trigger a repaint
    }

    void setTitle(const QString& title)
    {
        chart_title = title;
        update(); // Trigger a repaint after setting new title
    }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QList<std::tuple<QString, float, QColor>> data;
    float max_data_value = 100.0f; // Maximum value for scaling
    bool scale_to_data_max = false;
    bool draw_axis = true; // Flag to control axis drawing
    QString chart_title = "";
};

class SummaryView : public QWidget
{
    Q_OBJECT
public:
    explicit SummaryView(QWidget* parent = nullptr);

    void setTableHeaders(const QStringList& headers);
    void setTableRowHeaders(const QStringList& headers);

    void addTableRow(const QList<QString>& rowData);
    void clearTableData();
    void clearBarChartData();
    void clearPieChartData();

    void setPieChartData(const QList<QPair<QString, float>>& data, const QString& title = "");
    void setPieChartColors(const std::vector<QColor>& colors) { pieChartWidget->setChartColors(colors); }

    void setBarChartData(const QList<std::tuple<QString, float, QColor>>& data, const QString& title = "");
signals:

protected:
    void paintEvent(QPaintEvent* event) override;
    void setupPieChartLegendTable();

private:
    PieChartWidget* pieChartWidget;
    BarChartWidget* barChartWidget;
    QTableWidget* legendTable;
    QTableWidget* tableWidget;
    QWidget* tableContainer;
    QWidget* pieChartContainer;
};
