// MIT License
//
// Copyright (c) 2024-2026 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "flamegraphwidget.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolTip>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <functional>
#include <sstream>

#include "code/asmcode.h"
#include "code/sourcefile.h"
#include "config/config.hpp"
#include "mainwindow.h"

namespace
{

/// Trim leading whitespace from a string
std::string trimLeadingWhitespace(const std::string& str)
{
    size_t start = str.find_first_not_of(" \t");
    return (start != std::string::npos) ? str.substr(start) : str;
}

/// Extract the filename from a path (everything after the last '/')
std::string basename(const std::string& path)
{
    auto pos = path.find_last_of('/');
    return (pos != std::string::npos) ? path.substr(pos + 1) : path;
}

constexpr const char* kUnassignedFile = "[Unassigned]";

constexpr std::array<QColor, 10> kFileColors = {
    {
     QColor(0x40, 0x60, 0xD0), // Muted blue
        QColor(0x90, 0x40, 0xC0), // Purple
        QColor(0xC0, 0x30, 0x60), // Rose/magenta
        QColor(0x30, 0x80, 0xB0), // Teal-blue
        QColor(0xA0, 0x30, 0xA0), // Violet
        QColor(0x20, 0x60, 0x90), // Steel blue
        QColor(0xB0, 0x40, 0x80), // Plum
        QColor(0x50, 0x50, 0xC0), // Indigo
        QColor(0x80, 0x30, 0x30), // Dark red/maroon
        QColor(0x30, 0x70, 0x70)  // Dark cyan
    }
};

} // anonymous namespace

QColor FlameGraphWidget::fileColor(int index) { return kFileColors[index % kFileColors.size()]; }

QColor FlameGraphWidget::sourceColor(double latencyRatio)
{
    // Cool blue-to-magenta gradient; avoids green/orange/yellow/gray
    int r = 100 + static_cast<int>(120 * latencyRatio);
    int g = 60 + static_cast<int>(20 * latencyRatio);
    int b = 160 + static_cast<int>(60 * latencyRatio);
    return QColor(std::min(r, 255), std::max(g, 0), std::min(b, 255));
}

QColor FlameGraphWidget::asmColorForType(int tokenType)
{
    auto& colors = Config::TokenColors();
    if (tokenType >= 0 && tokenType < static_cast<int>(colors.size())) return colors[tokenType].qcolor;
    // Fallback: gray for unknown
    return QColor(0x70, 0x70, 0x70);
}

FlameGraphWidget::FlameGraphWidget(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(200);

    QPalette pal = QPalette();
    pal.setColor(QPalette::Window, WindowColors::Background());
    setAutoFillBackground(true);
    setPalette(pal);

    // Create horizontal scrollbar at bottom
    m_hScrollBar = new QScrollBar(Qt::Horizontal, this);
    m_hScrollBar->setRange(0, 0); // Will be updated when zoomed
    connect(m_hScrollBar, &QScrollBar::valueChanged, this, &FlameGraphWidget::onScrollBarChanged);
}

void FlameGraphWidget::rebuild()
{
    m_frames.clear();
    m_numRows = 0;
    m_totalLatency = 0;
    m_hoveredFrame = nullptr;

    // Reset view to a sane default and refresh the scrollbar so that
    // stale zoom/pan state from a previous dataset does not persist.
    m_viewLeft = 0.0;
    m_viewWidth = 1.0;
    updateScrollBar();

    if (!MainWindow::window || !MainWindow::window->source_filetab) return;

    auto* tab = MainWindow::window->source_filetab;
    if (tab->files.empty()) return;

    // ---- Step 1+2: For each ASM instruction, build inline call-stack paths under files ----
    // File nodes are created on-demand and their latency is accumulated from rooted instructions.
    std::map<std::string, std::shared_ptr<StackNode>> fileNodes;

    for (auto& asmLine : ASMCodeline::line_vec)
    {
        if (!asmLine) continue;
        int64_t lat = asmLine->hotspot.combined();
        if (lat <= 0) continue;

        auto* asmElem = dynamic_cast<ASMLine*>(asmLine->elements.at(ASMCodeline::Element::EASM).get());
        if (!asmElem) continue;

        // Get the inline call stack (outermost/definition first -> innermost/call-site last)
        const auto& refs = asmElem->line_ref;

        // Determine the innermost file. If no SourceLine resolved, fall back to the
        // raw source-ref text already stored in ESOURCEREF: take everything before
        // the last ':' as the file (so "myfile.cpp:?" → file "myfile.cpp", line "?").
        // If that text is empty, group under "[Unassigned]".
        std::string innerFile;
        bool resolvedInner = false;
        if (!refs.empty())
            if (auto r = refs.back().lock(); r && r->parent && !r->parent->filename.empty())
            {
                innerFile = r->parent->filename;
                resolvedInner = true;
            }

        if (innerFile.empty())
        {
            // cppline is "outer.cpp:N -> ... -> inner.cpp:?". Look only at the
            // innermost segment (after the last " -> "); take everything before
            // its last ':' as the file name.
            std::string_view src = asmLine->elements.at(ASMCodeline::Element::ESOURCEREF)->getStdText();
            if (auto a = src.rfind(" -> "); a != std::string_view::npos) src.remove_prefix(a + 4);
            auto colon = src.rfind(':');
            if (colon != std::string_view::npos && colon > 0) innerFile = std::string(src.substr(0, colon));
            else innerFile = kUnassignedFile;
        }

        const bool unassigned = (innerFile == kUnassignedFile);

        auto& fileNode = fileNodes[innerFile];
        if (!fileNode)
        {
            fileNode = std::make_shared<StackNode>();
            fileNode->key = innerFile;
            fileNode->label = unassigned ? innerFile : basename(innerFile);
            fileNode->filename = unassigned ? std::string() : innerFile;
        }
        fileNode->latency += lat;

        StackNode* current = fileNode.get();

        if (resolvedInner)
        {
            // Walk the inline chain in reverse (call-site first, then inlined frames going up)
            for (size_t i = refs.size(); i-- > 0;)
            {
                auto locked = refs[i].lock();
                if (!locked) continue;

                std::string srcFile = locked->parent ? locked->parent->filename : "Unknown";
                int displayLine = locked->line_number + 1; // line_number is 0-based
                std::string key = srcFile + ":" + std::to_string(locked->line_number);
                std::string location = basename(srcFile) + ":" + std::to_string(displayLine);

                auto& child = current->children[key];
                if (!child)
                {
                    child = std::make_shared<StackNode>();
                    child->key = key;
                    child->label = location;
                    child->content = trimLeadingWhitespace(locked->getStdText());
                    child->fullLocation = srcFile + ":" + std::to_string(displayLine);
                    child->filename = srcFile;
                    child->lineNumber = locked->line_number;
                }
                child->latency += lat;
                current = child.get();
            }
        }
        else
        {
            // No resolved SourceLine: one synthetic "<file>:?" row above the file row.
            // ASM entries are merged-by-label below so we end up with one frame per
            // unique instruction text instead of one per ASM line.
            std::string key = innerFile + ":?";
            auto& child = current->children[key];
            if (!child)
            {
                child = std::make_shared<StackNode>();
                child->key = key;
                child->label = (unassigned ? innerFile : basename(innerFile)) + ":?";
                child->fullLocation = innerFile + ":?";
                child->filename = unassigned ? std::string() : innerFile;
                child->lineNumber = -1;
            }
            child->latency += lat;
            current = child.get();
        }

        // Build the AsmEntry for this instruction.
        StackNode::AsmEntry entry;
        entry.label = trimLeadingWhitespace(asmElem->getStdText());
        entry.latency = lat;
        entry.asmIndex = asmLine->line_index;

        // Determine dominant token type from hotspot typed_latency
        int bestType = 0;
        int64_t bestLat = 0;
        for (size_t t = 0; t < asmLine->hotspot.typed_latency.size(); t++)
        {
            if (asmLine->hotspot.typed_latency[t] > bestLat)
            {
                bestLat = asmLine->hotspot.typed_latency[t];
                bestType = static_cast<int>(t);
            }
        }
        entry.tokenType = bestType;

        if (resolvedInner)
        {
            // Resolved path: keep one entry per ASM line (existing behavior).
            current->asmEntries.push_back(std::move(entry));
        }
        else
        {
            // Fallback path: merge by label so identical instructions collapse.
            // The first asmIndex is kept as a representative so the tooltip can
            // still show codeobj/vaddr for one instance of this instruction.
            auto [it, inserted] = current->asmIndexByLabel.try_emplace(entry.label, current->asmEntries.size());
            if (inserted) current->asmEntries.push_back(std::move(entry));
            else current->asmEntries[it->second].latency += entry.latency;
        }
    }

    // ---- Step 3: Calculate total latency and determine max depth ----
    m_totalLatency = 0;
    for (auto& [name, node] : fileNodes) m_totalLatency += node->latency;
    if (m_totalLatency <= 0) return;

    // Determine maximum call-stack depth
    std::function<int(const StackNode&)> maxDepth = [&](const StackNode& node) -> int
    {
        int d = 0;
        for (auto& [k, child] : node.children) d = std::max(d, 1 + maxDepth(*child));
        if (!node.asmEntries.empty()) d = std::max(d, 1);
        return d;
    };

    int globalMaxDepth = 0;
    for (auto& [name, node] : fileNodes) globalMaxDepth = std::max(globalMaxDepth, maxDepth(*node));

    // Row 0 = files (bottom), row 1..N = source lines / inline depths, row N+1 = ASM
    // Total rows = 1 (file) + globalMaxDepth (source/inline + asm at top)
    m_numRows = 1 + globalMaxDepth;

    // ---- Step 4: Convert tree to flat frames ----
    // Sort file nodes by latency (descending) for nicer layout
    std::vector<std::pair<std::string, std::shared_ptr<StackNode>>> sortedFiles(fileNodes.begin(), fileNodes.end());
    std::sort(
        sortedFiles.begin(),
        sortedFiles.end(),
        [](const auto& a, const auto& b) { return a.second->latency > b.second->latency; }
    );

    double xPos = 0.0;
    int fileIdx = 0;
    for (auto& [name, node] : sortedFiles)
    {
        double w = static_cast<double>(node->latency) / m_totalLatency;

        // File frame (row 0, at the bottom)
        Frame f;
        f.label = node->label;
        f.location = name;
        f.latency = node->latency;
        f.x = xPos;
        f.w = w;
        f.row = 0;
        f.color = fileColor(fileIdx);
        f.filename = name;
        m_frames.push_back(f);

        // Recurse into children
        flattenNode(*node, 1, globalMaxDepth, xPos, w, node->latency);

        xPos += w;
        fileIdx++;
    }

    // Adjust size
    int totalHeight = m_marginTop + m_numRows * (m_rowHeight + m_padding) + m_marginBottom;
    setMinimumHeight(totalHeight);
    updateGeometry();
    update();
}

void FlameGraphWidget::flattenNode(
    const StackNode& node, int depth, int maxDepth, double parentX, double parentW, int64_t parentLatency
)
{
    if (parentLatency <= 0) return;

    // Sort children by latency (descending) for consistent layout
    std::vector<std::pair<std::string, std::shared_ptr<StackNode>>> sortedChildren(
        node.children.begin(), node.children.end()
    );
    std::sort(
        sortedChildren.begin(),
        sortedChildren.end(),
        [](const auto& a, const auto& b) { return a.second->latency > b.second->latency; }
    );

    double xPos = parentX;

    for (auto& [key, child] : sortedChildren)
    {
        double w = parentW * static_cast<double>(child->latency) / parentLatency;

        double ratio = static_cast<double>(child->latency) / m_totalLatency;

        Frame f;
        f.label = child->label;
        f.location = child->fullLocation;
        f.content = child->content;
        f.latency = child->latency;
        f.x = xPos;
        f.w = w;
        f.row = depth;
        f.color = sourceColor(std::min(ratio * 5.0, 1.0)); // Scale up ratio for visual contrast
        f.filename = child->filename;
        f.lineNumber = child->lineNumber;
        m_frames.push_back(f);

        // Recurse into deeper inline levels
        flattenNode(*child, depth + 1, maxDepth, xPos, w, child->latency);

        // Add ASM frames at the topmost level (depth = maxDepth for this branch)
        if (!child->asmEntries.empty())
        {
            double asmX = xPos;
            int64_t childLat = child->latency;
            for (auto& asm_e : child->asmEntries)
            {
                double asmW = w * static_cast<double>(asm_e.latency) / childLat;

                Frame af;
                af.label = asm_e.label;
                af.content = asm_e.label;
                af.latency = asm_e.latency;
                af.x = asmX;
                af.w = asmW;
                af.row = depth + 1;
                af.color = asmColorForType(asm_e.tokenType);
                af.asmIndex = asm_e.asmIndex;
                m_frames.push_back(af);

                // Ensure this row exists
                if (af.row + 1 > m_numRows) m_numRows = af.row + 1;

                asmX += asmW;
            }
        }

        xPos += w;
    }
}

QRect FlameGraphWidget::frameRect(const Frame& f) const
{
    int drawWidth = width() - m_marginLeft - m_marginRight;

    // Transform from [0,1] normalized coords into view coords, then pixel coords
    double normLeft = (f.x - m_viewLeft) / m_viewWidth;
    double normWidth = f.w / m_viewWidth;

    int px = m_marginLeft + static_cast<int>(normLeft * drawWidth);
    int pw = std::max(1, static_cast<int>(normWidth * drawWidth));

    // Account for scrollbar at bottom
    int scrollBarHeight = m_hScrollBar ? m_hScrollBar->sizeHint().height() : 0;
    int bottomMargin = m_marginBottom + scrollBarHeight;

    // Anchor row 0 at the bottom of the widget (above scrollbar), rows stack upward
    int py = height() - bottomMargin - (f.row + 1) * (m_rowHeight + m_padding) + m_padding;

    return QRect(px, py, pw, m_rowHeight);
}

const FlameGraphWidget::Frame* FlameGraphWidget::frameAt(const QPoint& pos) const
{
    const Frame* best = nullptr;
    int bestArea = INT_MAX;

    for (auto& f : m_frames)
    {
        QRect r = frameRect(f);
        if (r.contains(pos))
        {
            int area = r.width() * r.height();
            if (area < bestArea)
            {
                best = &f;
                bestArea = area;
            }
        }
    }
    return best;
}

QSize FlameGraphWidget::sizeHint() const
{
    int h = m_marginTop + std::max(m_numRows, 3) * (m_rowHeight + m_padding) + m_marginBottom;
    return QSize(800, h);
}

QSize FlameGraphWidget::minimumSizeHint() const { return QSize(200, 100); }

void FlameGraphWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    if (m_frames.empty())
    {
        QFont font = MainWindow::default_font.isEmpty() ? painter.font() : QFont(MainWindow::default_font);
        font.setPointSize(MainWindow::font() + 2);
        painter.setFont(font);
        painter.setPen(WindowColors::textColor());
        painter.drawText(rect(), Qt::AlignCenter, "No profiling data available for flamegraph");
        return;
    }

    QFont font = MainWindow::default_font.isEmpty() ? painter.font() : QFont(MainWindow::default_font);
    font.setPointSize(std::max(MainWindow::font() - 2, 7));
    painter.setFont(font);
    QFontMetrics fm(font);

    for (auto& f : m_frames)
    {
        QRect r = frameRect(f);

        // Skip if entirely outside visible area
        if (r.right() < 0 || r.left() > width()) continue;
        // Skip if too small
        if (r.width() < 1) continue;

        // Draw frame rectangle
        QColor fillColor = f.color;
        if (&f == m_hoveredFrame) fillColor = fillColor.lighter(130);

        painter.fillRect(r, fillColor);

        // Draw border
        painter.setPen(QPen(fillColor.darker(150), 1));
        painter.drawRect(r);

        // Draw text if there's room
        if (r.width() > 20)
        {
            painter.setPen(WindowColors::reverseTextColor());

            QString text = QString::fromStdString(f.label);
            // Source lines: show location + content separated by spacing
            if ((f.lineNumber >= 0 && f.asmIndex < 0) && !f.content.empty())
                text += "          " + QString::fromStdString(f.content);

            QString elided = fm.elidedText(text, Qt::ElideRight, r.width() - 4);
            painter.drawText(r.adjusted(2, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft, elided);
        }
    }

    // Draw row labels on the left margin area
    if (m_numRows > 0)
    {
        QFont labelFont = font;
        labelFont.setPointSize(std::max(MainWindow::font() - 3, 6));
        labelFont.setBold(true);
        painter.setFont(labelFont);
        painter.setPen(WindowColors::textColor());

        // Row 0 label (bottom)
        int y0 = m_marginTop + (m_numRows - 1) * (m_rowHeight + m_padding);
        painter.drawText(2, y0, m_marginLeft - 4, m_rowHeight, Qt::AlignVCenter | Qt::AlignLeft, "");
    }
}

void FlameGraphWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isPanning)
    {
        int dx = event->pos().x() - m_lastMousePos.x();
        int drawWidth = width() - m_marginLeft - m_marginRight;
        if (drawWidth > 0)
        {
            double shift = -static_cast<double>(dx) / drawWidth * m_viewWidth;
            m_viewLeft = std::clamp(m_viewLeft + shift, 0.0, 1.0 - m_viewWidth);
        }
        m_lastMousePos = event->pos();
        updateScrollBar();
        update();
        return;
    }

    const Frame* hovered = frameAt(event->pos());
    if (hovered != m_hoveredFrame)
    {
        m_hoveredFrame = hovered;
        update();

        if (hovered)
        {
            // Format latency
            QString latStr;
            if (hovered->latency > 1000000)
                latStr = QString("%1M cycles").arg(hovered->latency / 1000000.0, 0, 'f', 1);
            else if (hovered->latency > 1000)
                latStr = QString("%1K cycles").arg(hovered->latency / 1000.0, 0, 'f', 1);
            else
                latStr = QString("%1 cycles").arg(hovered->latency);

            double pct = m_totalLatency > 0 ? 100.0 * hovered->latency / m_totalLatency : 0;

            // For ASM frames, prefix with "<codeobj> / 0x<vaddr>  " before cycles.
            QString prefix;
            if (hovered->asmIndex >= 0)
            {
                auto it = ASMCodeline::line_map.find(hovered->asmIndex);
                if (it != ASMCodeline::line_map.end())
                    if (auto* a = dynamic_cast<ASMLine*>(it->second->elements.at(ASMCodeline::Element::EASM).get()))
                        prefix = QString("%1 / 0x%2 - ").arg(a->codeobj).arg(a->addr, 0, 16);
            }

            // Build tooltip: cycles on top, then location, then content
            QString tip = prefix + latStr + QString(" (%1%)").arg(pct, 0, 'f', 1);
            if (!hovered->location.empty()) tip += "\n" + QString::fromStdString(hovered->location);
            if (!hovered->content.empty()) tip += "\n" + QString::fromStdString(hovered->content);
            QToolTip::showText(event->globalPos(), tip, this);
        }
        else
            QToolTip::hideText();
    }
}

void FlameGraphWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton)
    {
        m_isPanning = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (event->button() == Qt::LeftButton)
    {
        const Frame* clicked = frameAt(event->pos());
        if (!clicked) return;

        // Navigate to the appropriate code
        if (clicked->asmIndex >= 0)
        {
            // Click on ASM frame -> scroll to that instruction
            auto it = ASMCodeline::line_map.find(clicked->asmIndex);
            if (it != ASMCodeline::line_map.end() && MainWindow::window && MainWindow::window->code_contents)
            {
                auto* asmElem = dynamic_cast<ASMLine*>(it->second->elements.at(ASMCodeline::Element::EASM).get());
                if (asmElem) asmElem->onMousePress();
            }
        }
        else if (clicked->lineNumber >= 0 && !clicked->filename.empty())
        {
            // Click on source line -> scroll to source
            if (MainWindow::window && MainWindow::window->source_filetab)
                MainWindow::window->source_filetab->scrollTo(clicked->filename, clicked->lineNumber);
        }
        else if (!clicked->filename.empty())
        {
            // Click on file -> switch to that file's tab and scroll to top
            if (MainWindow::window && MainWindow::window->source_filetab)
                MainWindow::window->source_filetab->scrollTo(clicked->filename, 0);
        }
    }
}

void FlameGraphWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // Position scrollbar at the bottom
    int scrollBarHeight = m_hScrollBar->sizeHint().height();
    m_hScrollBar->setGeometry(0, height() - scrollBarHeight, width(), scrollBarHeight);

    update();
}

void FlameGraphWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_isPanning && (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton))
    {
        m_isPanning = false;
        setCursor(Qt::ArrowCursor);
    }
}

void FlameGraphWidget::wheelEvent(QWheelEvent* event)
{
    double zoomFactor = 1.15;
    int drawWidth = width() - m_marginLeft - m_marginRight;
    if (drawWidth <= 0) return;

    // Mouse position in normalized [0,1] space
    double mouseNorm = m_viewLeft + m_viewWidth * (event->position().x() - m_marginLeft) / drawWidth;

    if (event->angleDelta().y() > 0)
    {
        // Zoom in
        double newWidth = m_viewWidth / zoomFactor;
        newWidth = std::max(newWidth, 0.001); // minimum zoom
        double newLeft = mouseNorm - (mouseNorm - m_viewLeft) / zoomFactor;
        m_viewLeft = std::max(newLeft, 0.0);
        m_viewWidth = std::min(newWidth, 1.0 - m_viewLeft);
    }
    else if (event->angleDelta().y() < 0)
    {
        // Zoom out
        double newWidth = m_viewWidth * zoomFactor;
        newWidth = std::min(newWidth, 1.0);
        double newLeft = mouseNorm - (mouseNorm - m_viewLeft) * zoomFactor;
        m_viewLeft = std::max(newLeft, 0.0);
        if (m_viewLeft + newWidth > 1.0) m_viewLeft = 1.0 - newWidth;
        m_viewWidth = newWidth;
    }

    updateScrollBar();
    update();
    event->accept();
}

void FlameGraphWidget::updateScrollBar()
{
    // Scrollbar range: 0 to (1 - m_viewWidth) * 10000 (using 10000 steps for precision)
    // When fully zoomed out, m_viewWidth = 1.0, so range = 0 (no scrolling)
    const int scrollSteps = 10000;
    int maxVal = static_cast<int>((1.0 - m_viewWidth) * scrollSteps);
    int pageStep = static_cast<int>(m_viewWidth * scrollSteps);
    int currentVal = static_cast<int>(m_viewLeft * scrollSteps);

    m_hScrollBar->blockSignals(true);
    m_hScrollBar->setRange(0, maxVal);
    m_hScrollBar->setPageStep(pageStep);
    m_hScrollBar->setValue(currentVal);
    m_hScrollBar->blockSignals(false);
}

void FlameGraphWidget::onScrollBarChanged(int value)
{
    const int scrollSteps = 10000;
    double newLeft = static_cast<double>(value) / scrollSteps;
    m_viewLeft = std::clamp(newLeft, 0.0, 1.0 - m_viewWidth);
    update();
}
