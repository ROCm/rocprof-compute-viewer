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

#include "waveview.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolTip>
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <utility>
#include "code/qcodelist.h"
#include "config/config.hpp"
#include "data/marker_colors.h"
#include "data/wavemanager.h"
#include "mainwindow.h"
#include "scroll.h"
#include "util/jsonrequest.hpp"
#include "wave/overlay_utils.h"

namespace
{
constexpr int64_t SQTT_POINT_MARKER_MIN_CYCLES = 8;

int markerRowForDepth(int depth, int rows) { return rows - 1 - std::clamp(depth, 0, rows - 1); }

int markerDepthForRow(int row, int rows) { return rows - 1 - std::clamp(row, 0, rows - 1); }

bool horizontalScrollDominates(const QWheelEvent* event)
{
    const QPoint& pixel = event->pixelDelta();
    int dx;
    int dy;
    if (!pixel.isNull())
    {
        dx = pixel.x();
        dy = pixel.y();
    }
    else
    {
        dx = event->angleDelta().x();
        dy = event->angleDelta().y();
    }
    return dx != 0 && std::abs(dx) > std::abs(dy);
}
} // namespace

QWaveView::QWaveView(QCustomScroll* parent) : view(parent->view), tool(parent->tool)
{
    assert(parent);
    setMouseTracking(true);
    setAttribute(Qt::WA_AlwaysShowToolTips, true);
    Reset();
    connect(parent, &QCustomScroll::valueupdated, this, &QWaveView::onupdatebar);

    if (tool) tool->update_list.insert(this);
}

void QWaveView::Reset()
{
    waves.clear();
    trace_events = nullptr;
    dispatch_records = nullptr;
    decoder_event_se = -1;
}

void QWaveView::SetDecoderEvents(
    const std::vector<trace_event_record_t>* events, const std::vector<dispatch_record_t>* dispatches, int se
)
{
    trace_events = events;
    dispatch_records = dispatches;
    decoder_event_se = se;
    update();
}

void QWaveView::setLineHover(int line_index, bool hover)
{
    auto line_it = ASMCodeline::line_map.find(line_index);
    if (line_it == ASMCodeline::line_map.end() || line_it->second == nullptr) return;

    try
    {
        if (auto asm_line = line_it->second->elements.at(ASMCodeline::Element::EASM).get())
        {
            asm_line->setMouseHover(hover);
            if (QCodelist::singleton) QCodelist::singleton->update();
        }
    }
    catch (...)
    {
        RCV_LOG();
    }
}

void QWaveView::clearLineHover()
{
    if (hovering_line_index >= 0)
    {
        setLineHover(hovering_line_index, false);
        hovering_line_index = -1;
    }
}

void QWaveView::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    MainWindow::getScaling(painter);
    painter.setRenderHint(QPainter::Antialiasing);

    int64_t cutoff_start = QCustomScroll::clock_cutoff_start + view->start;
    int64_t cutoff_end = std::min(cutoff_start + Token::PosToClock(width()), QCustomScroll::clock_cutoff_end);

    for (auto& [_, wave] : waves) wave->Draw(painter, cutoff_start, cutoff_end);
    DrawDecoderEvents(painter, cutoff_start, cutoff_end, height() / MainWindow::getScaling());
}

void QWaveView::DrawDecoderEvents(QPainter& painter, int64_t clock_start, int64_t clock_end, int height)
{
    WaveOverlay::drawDecoderEvents(
        painter,
        trace_events,
        dispatch_records,
        WaveOverlay::DecoderEventSurface::ComputeUnit,
        clock_start,
        clock_end,
        height,
        [clock_start](int64_t time) { return Token::GetTokenSize(time - clock_start); }
    );
}

bool QWaveView::ShowDecoderEventTooltip(const QPoint& global_pos, int64_t clock)
{
    const int64_t tolerance = std::max<int64_t>(
        Token::PosToClock(std::max(WaveOverlay::DecoderDispatchWidth, WaveOverlay::DecoderEventWidth) + 2), 4
    );

    if (const auto* dispatch = WaveOverlay::findRecordAt(dispatch_records, clock, tolerance, true))
    {
        clearLineHover();
        QToolTip::showText(
            global_pos, QString::fromStdString(WaveOverlay::formatDispatchTooltip(*dispatch, decoder_event_se))
        );
        return true;
    }

    if (const auto* trace_event = WaveOverlay::findRecordAt(trace_events, clock, tolerance, true))
    {
        if (!WaveOverlay::showTraceEvent(*trace_event, WaveOverlay::DecoderEventSurface::ComputeUnit)) return false;

        clearLineHover();
        QToolTip::showText(
            global_pos, QString::fromStdString(WaveOverlay::formatTraceEventTooltip(*trace_event, decoder_event_se))
        );
        return true;
    }

    return false;
}

void QWaveView::mouseMoveEvent(QMouseEvent* event)
{
    this->Super::mouseMoveEvent(event);
    setFocus();

    IMPLEMENT_FPS_LIMITER();

    if (tool && tool->bClicking)
    {
        tool->mouseMoveEvent(event->pos().x(), event->pos().y());
        return;
    }

    const int64_t clock = Token::PosToClock(event->pos().x()) + QCustomScroll::clock_cutoff_start + view->start;
    int posy = event->pos().y();
    if (ShowDecoderEventTooltip(event->globalPos(), clock)) return;

    auto wave_it = waves.upper_bound(clock);
    if (wave_it == waves.begin())
    {
        clearLineHover();
        return;
    }

    auto& container_wave = *(std::prev(wave_it)->second);

    std::string tooltip;
    if (posy < WSTATE_POSY())
    {
        auto it = container_wave.tokens.get_token_in_clock(clock);
        if (it == container_wave.tokens.end())
        {
            clearLineHover();
            return;
        }

        auto& token = *it;

        // Update hover highlight on corresponding assembly line
        int new_hover_index = -1;
        if (token.end_time() > clock)
        {
            tooltip = token.ToolTip();
            new_hover_index = token.code_line;
        }
        else
        {
            Token blankToken{};
            blankToken.clock = token.end_time();
            if (std::next(it) != container_wave.tokens.end())
                blankToken.cycles = std::next(it)->clock - blankToken.clock;
            else
                blankToken.cycles = container_wave.wave_end - blankToken.clock;
            tooltip = blankToken.ToolTip();
        }

        // Update hover state
        if (new_hover_index != hovering_line_index)
        {
            clearLineHover();
            if (new_hover_index >= 0)
            {
                hovering_line_index = new_hover_index;
                setLineHover(hovering_line_index, true);
            }
        }
    }
    else
    {
        clearLineHover();
        auto it = container_wave.timeline.upper_bound(clock);
        if (it == container_wave.timeline.begin()) return;

        tooltip = std::prev(it)->second.GetName() + " clk: " + std::to_string(clock);
    }
    QToolTip::showText(event->globalPos(), tooltip.c_str());
}

void QWaveView::mouseReleaseEvent(QMouseEvent* event)
{
    if (tool && event->button() & Qt::RightButton) tool->mousePressEvent(false, event->pos().x(), event->pos().y());
}

void QWaveView::leaveEvent(QEvent* event)
{
    Super::leaveEvent(event);
    clearLineHover();
}

void QWaveView::mousePressEvent(QMouseEvent* event)
{
    QWARNING(QCodelist::singleton && MainWindow::window, "Invalid codelist", return );

    if (tool && event->button() & Qt::RightButton)
    {
        tool->mousePressEvent(true, event->pos().x(), event->pos().y());
        return;
    }

    const int64_t clock = Token::PosToClock(event->pos().x()) + QCustomScroll::clock_cutoff_start + view->start;
    int posy = event->pos().y();

    MainWindow::window->setPlotBarPos(clock);

    if (posy >= WSTATE_POSY()) return;

    auto wave_it = waves.upper_bound(clock);
    if (wave_it == waves.begin()) return;

    auto wave_ptr = std::prev(wave_it)->second;

    auto it = wave_ptr->tokens.get_token_in_clock(clock);
    if (it == wave_ptr->tokens.end() || !it->inClock(clock)) return;

    auto& token = *it;

    try
    {
        auto line = ASMCodeline::line_map.at(token.code_line);
        auto asm_line = line->elements.at(ASMCodeline::Element::EASM).get();

        int line_index = line->line_index;

        std::string str = asm_line ? asm_line->getStdText() : "unknown";
        MainWindow::window->AddHistoryEntry(token.clock, token.GetName(), str);

        QCodelist::singleton->Highlight(line_index, line_index, true);
        MainWindow::window->SetIteration(line_index, token.iteration());
    }
    catch (...)
    {
        RCV_LOG();
    }
}

QWaveSlots::QWaveSlots(QCustomScroll* parent)
{
    connect(parent, &QCustomScroll::valueupdated, this, &QWaveSlots::onupdatebar);
    Reset();
    setMouseTracking(true);
    view = parent->view;

    tool = std::make_shared<MeasureTool>();
    parent->tool = tool;
    tool->update_list.insert(this);

    this->setAutoFillBackground(true);
}

void QWaveSlots::Reset()
{
    if (layout()) delete layout();

    auto* hlayout = new QHBox();
    this->setLayout(hlayout);

    QWidget* cuwaves_names = new QWidget(this);
    names_layout = new QVBox();
    names_layout->insertStretch(-1);
    cuwaves_names->setLayout(names_layout);
    hlayout->addWidget(cuwaves_names);

    cuwaves_content = new QWidget(this);
    content_layout = new QVBox();
    content_layout->insertStretch(-1);
    cuwaves_content->setLayout(content_layout);
    hlayout->addWidget(cuwaves_content);

    content_layout->insertSpacing(0, 20);
    names_layout->insertSpacing(0, 20);

    QPalette pal = QPalette();
    pal.setColor(QPalette::Window, WindowColors::TraceBackground());
    this->setPalette(pal);
}

static int64_t getdelta(QKeyEvent* event)
{
    static const int64_t mult = 5 * WaveInstance::BaseClock();

    if (event->key() == Qt::Key_A)
        return -mipShiftLeft(mult, Token::mipmap_level);
    else if (event->key() == Qt::Key_D)
        return mipShiftLeft(mult, Token::mipmap_level);
    else
        return 0;
}

void QWaveView::keyPressEvent(QKeyEvent* event)
{
    Super::keyPressEvent(event);

    auto delta = getdelta(event);
    if (delta != 0)
    {
        view->start += delta;
        view->notify();
        update();
    }
}

void QWaveSlots::wheelEvent(QWheelEvent* event)
{
    if (QApplication::keyboardModifiers() & Qt::ControlModifier)
    {
        if (!cuwaves_content) return;
        IMPLEMENT_FPS_LIMITER();

        float mouse_x_in_content = event->position().x() - cuwaves_content->pos().x();
        int64_t clock_at_mouse = Token::PosToClock(mouse_x_in_content) + view->start;

        // Calculate ratio based on the clock position relative to the visible range
        float ratio = float(clock_at_mouse - view->start) / float(view->range);

        if (mouse_x_in_content > 0) MainWindow::incrementWaveViewMipmap(event->angleDelta().y() > 0 ? 1 : -1, ratio);
        return;
    }

    // Horizontal scroll: forward to a single scrollbar. It doesn't matter which
    // one — ScrollValue::notify() syncs all parents to the same value, so the
    // others follow. Forwarding to more than one would apply the delta twice.
    const bool shift = QApplication::keyboardModifiers() & Qt::ShiftModifier;
    if (horizontalScrollDominates(event) || shift)
    {
        // Shift + wheel scrolls forward (right) at reduced speed; touchpad gestures pass through.
        const qreal scale = shift ? -0.25 : 1.0;
        if (!view->parents.empty())
        {
            QWheelEvent scaled(
                event->position(),
                event->globalPosition(),
                event->pixelDelta() * scale,
                event->angleDelta() * scale,
                event->buttons(),
                event->modifiers(),
                event->phase(),
                event->inverted(),
                event->source()
            );
            QApplication::sendEvent(view->parents.front()->scrollbar, &scaled);
        }
        event->accept();
        return;
    }

    Super::wheelEvent(event);
}

void QWaveSlots::keyPressEvent(QKeyEvent* event)
{
    Super::keyPressEvent(event);

    auto delta = getdelta(event);
    if (delta != 0)
    {
        view->start += delta;
        view->notify();
        update();
    }
}

QLabel* QWaveSlots::AddSlot(QWidget* view, const std::string& name, int fixedsize, bool useMonospace)
{
    view->setFixedHeight(fixedsize);
    view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QLabel* label = new QLabel(name.c_str());
    if (useMonospace)
    {
        QFont monoFont("monospace");
        monoFont.setStyleHint(QFont::Monospace);
        label->setFont(monoFont);
    }
    label->setFixedHeight(fixedsize);

    if (names_layout) names_layout->addWidget(label);
    if (content_layout) content_layout->addWidget(view);

    return label;
}

void QWaveSlots::mousePressEvent(QMouseEvent* event)
{
    this->Super::mousePressEvent(event);
    if (!cuwaves_content) return;

    int posx = event->pos().x() - cuwaves_content->pos().x();
    if (posx >= 0 && tool && event->button() & Qt::RightButton) tool->mousePressEvent(true, posx, event->pos().y());
}

void QWaveSlots::mouseReleaseEvent(QMouseEvent* event)
{
    this->Super::mouseReleaseEvent(event);
    if (!cuwaves_content) return;

    int posx = event->pos().x() - cuwaves_content->pos().x();
    if (posx >= 0 && tool && event->button() & Qt::RightButton) tool->mousePressEvent(false, posx, event->pos().y());
}

void QWaveSlots::mouseMoveEvent(QMouseEvent* event)
{
    Super::mouseMoveEvent(event);
    setFocus();

    if (!tool || !tool->bClicking || !cuwaves_content) return;

    int posx = event->pos().x() - cuwaves_content->pos().x();
    if (posx < 0) return;

    IMPLEMENT_FPS_LIMITER();

    tool->mouseMoveEvent(posx, event->pos().y());

    std::stringstream ss;
    ss << "Start: " << Token::PosToClock(tool->measure_start_x) + QCustomScroll::clock_cutoff_start + view->start
       << "\n Cycles: " << Token::PosToClock(tool->measure_size_x);

    QToolTip::showText(event->globalPos(), ss.str().c_str());
}

void QWaveSlots::paintEvent(QPaintEvent* event)
{
    this->Super::paintEvent(event);

    if (!cuwaves_content) return;
    QPainter painter(this);
    MainWindow::getScaling(painter);

    int fmheight = 0;
    {
        QFont font = painter.font();
        font.setPointSize(MainWindow::font());
        painter.setFont(font);
        fmheight = QFontMetrics(font).overlinePos();
    }

    // Tick marks
    const int64_t clock_spacing = mipShiftLeft(25 * WaveInstance::BaseClock(), Token::mipmap_level);
    const int64_t pixel_spacing = Token::GetTokenSize(clock_spacing);
    const int64_t clock_start = QCustomScroll::clock_cutoff_start + view->start;

    int64_t clock_iter = clock_start + clock_spacing - (clock_start % clock_spacing);
    int barpos = cuwaves_content->pos().x() + Token::GetTokenSize(clock_iter - clock_start);

    QPen pen = painter.pen();
    pen.setWidth(1);
    pen.setStyle(Qt::DashLine);
    pen.setColor(WindowColors::textColor());
    painter.setPen(pen);

    const float INVSCALE = 1.0f / MainWindow::getScaling();
    const int scaled_wid = width() * INVSCALE;
    const int scaled_hei = height() * INVSCALE;

    while (barpos < scaled_wid)
    {
        if (clock_iter + clock_spacing > QCustomScroll::clock_cutoff_end)
        {
            pen.setColor(QColor(255, 0, 0));
            pen.setWidth(2);
            painter.setPen(pen);
        }
        std::string cycle = std::to_string(clock_iter);
        painter.drawLine(barpos, 0, barpos, scaled_hei);
        painter.drawText(barpos + 5, fmheight, cycle.c_str());
        clock_iter += clock_spacing;
        barpos += pixel_spacing;
    }

    // MeasureTool
    if (!tool || !tool->bClicking) return;

    QPainterPath path;
    path.addRect(QRect(
        (tool->measure_start_x + cuwaves_content->pos().x()) * INVSCALE, 0, tool->measure_size_x * INVSCALE, scaled_hei
    ));
    painter.fillPath(path, WindowColors::MeasureTool());
}

QUtilView::QUtilView(class QCustomScroll* parent) : QWaveView(parent)
{
    wave0 = std::make_shared<TokenGroup>();
    waves[0] = wave0;
    wave0->wave_begin = 0;
    wave0->wave_end = int64_t(1) << 40;
}

void QUtilView::Add(Token token)
{
    std::unique_lock<std::mutex> lk(mut);

    token.stall = std::max<int>(0, std::min<int>(token.cycles - WaveInstance::BaseClock(), token.stall));
    token.setHideStall(true);
    token.cycles -= token.stall;
    token.clock += token.stall;
    token.slot = 0;

    wave0->tokens.emplace_back(std::move(token));
}

void QUtilView::AddTokens(const std::vector<Token>& tokens)
{
    if (tokens.empty()) return;
    std::unique_lock<std::mutex> lk(mut);

    for (auto token : tokens)
    {
        token.stall = std::max<int>(0, std::min<int>(token.cycles - WaveInstance::BaseClock(), token.stall));
        token.setHideStall(true);
        token.cycles -= token.stall;
        token.clock += token.stall;
        token.slot = 0;

        wave0->tokens.emplace_back(std::move(token));
    }
}

void QUtilView::paintEvent(QPaintEvent* event)
{
    wave0->wave_begin = QCustomScroll::clock_cutoff_start;
    wave0->wave_end = QCustomScroll::clock_cutoff_end;
    QWaveView::paintEvent(event);
}

void QUtilView::Compile(bool bVisible)
{
    bVisible |= !wave0->tokens.empty();

    setVisible(bVisible);
    if (label) label->setVisible(bVisible);

    wave0->bInitialized = false;
    wave0->tokens.Compile();

    std::array<int64_t, 4> slot_start{};
    std::array<int64_t, 4> slot_end{};

    int64_t maxtime = 0;

    for (auto& token : wave0->tokens)
    {
        token.setOverlapped(token.clock < maxtime);
        maxtime = std::max(maxtime, token.clock + token.cycles);

        while (slot_start.at(token.slot) == token.clock && slot_end.at(token.slot) > token.clock && token.slot < 3)
            token.slot++;

        slot_start.at(token.slot) = token.clock;
        slot_end.at(token.slot) = std::max(slot_end.at(token.slot), token.clock + token.cycles);
    }
}

bool QUtilization::bSeparateLDSPipe = false;

QUtilization::QUtilization(QCustomScroll* parent) : QWaveSlots(parent)
{
    auto createView = [&](const std::string& name)
    {
        auto* view = all_views.emplace_back(new QUtilView(parent));
        view->label = AddSlot(view, name, WSTATE_HEIGHT() + WSTATE_POSY() + 1, true);
        return view;
    };

    int vertical_size = WSTATE_HEIGHT() + WSTATE_POSY() + 1;

    for (int i = 0; i < 4; i++) WMMA.at(i) = createView("WMMA" + std::to_string(i));
    for (int i = 0; i < 4; i++) VALU.at(i) = createView("VALU" + std::to_string(i) + ' ');

    for (int i = 0; i < 4; i++) LDS.at(i) = createView("LDS" + std::to_string(i));
    for (int i = 0; i < 4; i++) VMEM.at(i) = createView("VMEM" + std::to_string(i) + ' ');
    for (int i = 0; i < 4; i++) RT.at(i) = createView("RAY" + std::to_string(i));

    for (int i = 0; i < 4; i++) SCAL.at(i) = createView("SCAL" + std::to_string(i) + ' ');

    JUMP = createView("JUMP");
    MSG = createView("MSG");
    IMMED = createView("IMMED");

    for (auto& token : short_token_defs)
        for (int simd = 0; simd < 4; simd++) token.at(simd) = SCAL.at(simd);

    auto upper = [](std::string str)
    {
        std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::toupper(c); });
        return str;
    };

    for (int i = 0; i < Config::TokenColors().size(); i++)
    {
        auto name = upper(Config::TokenColors().at(i).name);

        auto sfind = [&name](std::string_view match) { return name.find(match) != std::string::npos; };

        if (sfind("IMMED") || sfind("TRAP"))
        {
            for (size_t simd = 0; simd < 4; simd++) short_token_defs.at(i).at(simd) = IMMED;
        }
        else if (sfind("MSG"))
        {
            for (size_t simd = 0; simd < 4; simd++) short_token_defs.at(i).at(simd) = MSG;
        }
        else if (sfind("VALU") || sfind("MFMA") || sfind("MATRIX") || sfind("WMMA"))
        {
            for (size_t simd = 0; simd < 4; simd++) short_token_defs.at(i).at(simd) = VALU.at(simd);
        }
        else if (QUtilization::bSeparateLDSPipe && sfind("LDS"))
        {
            for (size_t simd = 0; simd < 4; simd++) short_token_defs.at(i).at(simd) = LDS.at(simd);
        }
        else if (sfind("FLAT") || sfind("VMEM") || sfind("BVH") || sfind("RAY") || sfind("LDS"))
        {
            for (size_t simd = 0; simd < 4; simd++) short_token_defs.at(i).at(simd) = VMEM.at(simd);
        }
    }

    expanded_token_defs = short_token_defs;
    for (int i = 0; i < Config::TokenColors().size(); i++)
    {
        auto name = upper(Config::TokenColors().at(i).name);

        auto sfind = [&name](std::string_view match) { return name.find(match) != std::string::npos; };

        if (sfind("WMMA") || sfind("MFMA") || sfind("MATRIX"))
        {
            for (size_t simd = 0; simd < 4; simd++) expanded_token_defs.at(i).at(simd) = WMMA.at(simd);
        }

        if (sfind("LDS"))
        {
            for (size_t simd = 0; simd < 4; simd++) expanded_token_defs.at(i).at(simd) = LDS.at(simd);
        }

        if (sfind("BVH") || sfind("RAY"))
        {
            for (size_t simd = 0; simd < 4; simd++) expanded_token_defs.at(i).at(simd) = RT.at(simd);
        }

        if (sfind("BRANCH") || sfind("JUMP") || sfind("NEXT"))
        {
            for (size_t simd = 0; simd < 4; simd++) expanded_token_defs.at(i).at(simd) = JUMP;
        }
    }
}

void QUtilization::Clear()
{
    for (auto* view : all_views)
    {
        if (!view) continue;
        if (view->wave0) view->wave0->tokens.clear();
        view->SetDecoderEvents(nullptr, nullptr);
    }
    ClearOtherSimd();

    QPalette pal = QPalette();
    pal.setColor(QPalette::Window, WindowColors::TraceBackground());
    this->setPalette(pal);
}

void QUtilization::Compile()
{
    for (auto* view : all_views)
        if (view) view->Compile(false);
}

void QUtilization::SetDecoderEvents(
    const std::vector<trace_event_record_t>* events, const std::vector<dispatch_record_t>* dispatches, int se
)
{
    for (auto* view : all_views)
        if (view) view->SetDecoderEvents(events, dispatches, se);
}

// Cache the list of available other-SIMD files (per SE) and mark availability.
void QUtilization::SetOtherSimdSources(OtherSimdFiles files)
{
    other_simd.SetFiles(std::move(files));
    ClearOtherSimd();
}

void QUtilization::SetOtherSimdRecords(std::map<int, std::vector<OtherSimdInstruction>> records)
{
    other_simd.SetRecords(std::move(records));
    ClearOtherSimd();
}

// Load other-SIMD records for the given SE/SIMD and current clock window into the paired VMEM track.
void QUtilization::PopulateOtherSimdTokens(int se, int simd, int64_t clock_start, int64_t clock_end)
{
    if (!other_simd.HasFiles()) return;

    ClearOtherSimd();

    other_simd_id = simd ^ 1;
    if (other_simd_id < 0 || other_simd_id >= int(VMEM.size()) || VMEM.at(other_simd_id) == nullptr) return;
    auto* target_view = VMEM.at(other_simd_id);
    const auto& token_colors = Config::TokenColors();
    const auto& tokens = other_simd.LoadTokens(se, clock_start, clock_end, static_cast<int>(token_colors.size()));
    target_view->AddTokens(tokens);
}

void QUtilization::ClearOtherSimd()
{
    other_simd.Clear();
    other_simd_id = 0;
}

void QUtilization::AddTokens(int simd, const TokenMap& tokens)
{
    QWARNING(simd < 4, "Invalid simd " << simd, return );

    // Use base here as some decoder versions don't consider stalls for MSG
    // We dont want to show full cycles to avoid clutter in the timeline display
    int base = WaveInstance::BaseClock();
    auto& token_defs = Token::bIsNaviWave ? expanded_token_defs : short_token_defs;

    try
    {
        // Exclude immed, trap and msg
        for (auto& token : tokens)
        {
            if (auto* def = token_defs.at(token.type).at(simd))
            {
                Token _token = token;
                if (def == IMMED || def == MSG) _token.stall = std::max(0, _token.cycles - base);

                def->Add(_token);
            }
        }
    }
    catch (std::out_of_range&)
    {
        QWARNING(false, "Invalid token found!", );
    }
}

// --- QShaderDataView ---

QShaderDataView::QShaderDataView(QCustomScroll* parent, ShaderDataRecordVec records) :
view(parent->view), shaderdata_records(std::move(records))
{
    assert(parent);
    setMouseTracking(true);
    setAttribute(Qt::WA_AlwaysShowToolTips, true);
    connect(parent, &QCustomScroll::valueupdated, this, &QShaderDataView::onupdatebar);
}

void QShaderDataView::SetMarkers(MarkerSpanVec spans)
{
    markers.Reset(std::move(spans));
    update();
}

int QShaderDataView::suggestedHeight(int legacy_height) const
{
    if (markers.empty()) return legacy_height;
    // legacy_height is half-row by convention; give one (slightly thinner) row per stack depth, capped.
    const int row_h = std::max((legacy_height * 7) / 8, 7);
    const int rows = std::min(markers.max_depth + 1, 8);
    return row_h * rows;
}

void QShaderDataView::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    MainWindow::getScaling(painter);

    const int h = height();
    const int w = width();

    // Draw track guide lines
    {
        QPen guidePen;
        guidePen.setWidth(1);
        guidePen.setStyle(Qt::DotLine);
        guidePen.setColor(QColor(80, 80, 80));
        painter.setPen(guidePen);
        painter.drawLine(0, 0, w, 0);
        painter.drawLine(0, h - 1, w, h - 1);
    }

    int64_t cutoff_start = QCustomScroll::clock_cutoff_start + view->start;
    int64_t cutoff_end = std::min(cutoff_start + Token::PosToClock(w), QCustomScroll::clock_cutoff_end);

    // Marker mode: render typed colored spans + ticks.
    if (!markers.empty())
    {
        painter.setRenderHint(QPainter::Antialiasing, false);

        const auto& spans = *markers.spans;
        const int rows = std::max(markers.max_depth + 1, 1);
        const int row_h = std::max((h - 2) / rows, 2);

        const QPen edge(Qt::black, 0.5);

        auto draw_span = [&](const MarkerSpan& s, size_t idx)
        {
            const int64_t end_clock = s.is_open ? cutoff_end : s.exit_time;
            if (end_clock < cutoff_start) return;
            if (s.enter_time > cutoff_end) return;

            const int x0 = s.enter_time <= cutoff_start ? 0 : Token::GetTokenSize(s.enter_time - cutoff_start);
            const int x1_visible = s.is_open ? w : Token::GetTokenSize(end_clock - cutoff_start);
            const int x1 = std::min(x1_visible, w);

            const int row_y = 1 + markerRowForDepth(s.depth, rows) * row_h;
            const QColor& color = markers.colors[idx];

            if (s.is_point)
            {
                const int tick_w = std::max(Token::GetTokenSize(SQTT_POINT_MARKER_MIN_CYCLES), int64_t(2));
                QRect rect(x0, row_y, tick_w, row_h - 1);
                painter.fillRect(rect, color);
            }
            else
            {
                int width_px = std::max(x1 - x0, 1);
                QRect rect(x0, row_y, width_px, row_h - 1);
                painter.fillRect(rect, color);
                if (width_px >= 3)
                {
                    painter.setPen(edge);
                    painter.drawRect(rect);
                }
            }
        };

        // First-candidate cursor: closed spans whose enter_time precedes the
        // viewport are caught via max_closed_dur backstep; open spans before
        // that cutoff are tracked separately and always considered.
        const int64_t search_from = cutoff_start - markers.max_closed_dur;
        for (auto it = markers.FirstCandidate(cutoff_start); it != spans.end(); ++it)
        {
            if (it->enter_time > cutoff_end) break;
            draw_span(*it, static_cast<size_t>(it - spans.begin()));
        }
        for (int idx : markers.open_indices)
        {
            if (spans[idx].enter_time >= search_from) break;
            draw_span(spans[idx], static_cast<size_t>(idx));
        }

        painter.setRenderHint(QPainter::Antialiasing, true);
        return;
    }

    // Legacy raw-record path.
    if (!shaderdata_records || shaderdata_records->empty()) return;

    painter.setRenderHint(QPainter::Antialiasing, false);

    const auto& recs = *shaderdata_records;
    const int markerWidth = std::max(Token::GetTokenSize(8), int64_t(2));

    auto it_begin = std::lower_bound(
        recs.begin(), recs.end(), cutoff_start, [](const ShaderDataRecord& r, int64_t c) { return r.time < c; }
    );

    const QBrush fill(WindowColors::ShaderDataColor());
    const QPen edge(Qt::black, 0.5);

    int last_pixel = INT_MIN;

    for (auto it = it_begin; it != recs.end(); ++it)
    {
        if (it->time > cutoff_end) break;

        int pos = Token::GetTokenSize(it->time - cutoff_start);

        if (pos == last_pixel) continue;
        last_pixel = pos;

        QRect rect(pos, 1, markerWidth, h - 2);
        painter.setPen(edge);
        painter.fillRect(rect, fill);
        painter.drawRect(rect);
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
}

void QShaderDataView::mouseMoveEvent(QMouseEvent* event)
{
    this->Super::mouseMoveEvent(event);

    IMPLEMENT_FPS_LIMITER();

    int64_t clock = Token::PosToClock(event->pos().x()) + QCustomScroll::clock_cutoff_start + view->start;

    // Marker hit-test: find the innermost span that covers `clock` and whose
    // depth row matches the cursor's pixel y-position.
    if (!markers.empty())
    {
        const auto& spans = *markers.spans;
        const int h = height();
        const int rows = std::max(markers.max_depth + 1, 1);
        const int row_h = std::max((h - 2) / rows, 2);
        const int hover_row = std::clamp((event->pos().y() - 1) / row_h, 0, rows - 1);
        const int hover_depth = markerDepthForRow(hover_row, rows);

        const int64_t search_from = clock - markers.max_closed_dur;
        const int64_t point_tol = std::max<int64_t>(Token::PosToClock(3), SQTT_POINT_MARKER_MIN_CYCLES);
        const MarkerSpan* best = nullptr;
        ptrdiff_t best_idx = -1;
        auto consider = [&](const MarkerSpan& s, ptrdiff_t idx)
        {
            if (s.enter_time > clock) return;
            int64_t end = s.is_open ? INT64_MAX : s.exit_time;
            if (s.is_point)
            {
                if (std::llabs(clock - s.enter_time) > point_tol) return;
            }
            else
            {
                if (clock > end) return;
            }
            if (std::clamp(s.depth, 0, rows - 1) != hover_depth) return;
            if (!best || s.depth > best->depth)
            {
                best = &s;
                best_idx = idx;
            }
        };
        for (auto it = markers.FirstCandidate(clock); it != spans.end(); ++it)
        {
            if (it->enter_time > clock) break;
            consider(*it, it - spans.begin());
        }
        for (int idx : markers.open_indices)
        {
            if (spans[idx].enter_time >= search_from) break;
            consider(spans[idx], idx);
        }

        if (best)
        {
            (void) best_idx;
            QToolTip::showText(event->globalPos(), QString::fromStdString(FormatMarkerTooltip(*best, std::nullopt)));
            return;
        }
        // Fall through to raw lookup if no marker hit (lets unresolved tokens still show)
    }

    const int markerWidth = std::max(Token::GetTokenSize(8), int64_t(2));
    int idx = FindShaderDataRecord(shaderdata_records, clock, Token::PosToClock(markerWidth));
    if (idx < 0) return;

    QToolTip::showText(event->globalPos(), (*shaderdata_records)[idx].ToolTip().c_str());
}
