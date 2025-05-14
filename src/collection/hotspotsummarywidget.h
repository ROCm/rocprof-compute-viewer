#pragma once

#include <QLabel>
#include <QWidget>
#include <vector>
#include "code/sourcefile.h"
#include "code/textelement.h"

class HotspotSummaryWidget : public QWidget
{
    Q_OBJECT;
    set_tracked();

public:
    explicit HotspotSummaryWidget(QWidget* parent = nullptr);
    virtual ~HotspotSummaryWidget() {}

    void setFile(const std::string& filePath, const std::string& displayName);
    void clear();

    virtual void paintEvent(QPaintEvent* event) override;

private:
    struct HotspotLine
    {
        int lineNumber;
        QString content;
        int64_t latency;
        std::array<int64_t, 16> latencyByType{};
    };

    std::vector<HotspotLine> hotspotLines;
    QString fileDisplayName;
    int maxLatency = 1; // Start at 1 to avoid division by zero
};
