// MIT License
//
// Copyright (c) 2024-2025 Advanced Micro Devices, Inc. All rights reserved.
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

#include "token.h"
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <cmath>
#include <sstream>
#include "code/qcodelist.h"
#include "data/wavemanager.h"
#include "mainwindow.h"

bool Token::bIsNaviWave = true;
int Token::mipmap_level = 0;

std::vector<std::string> WaveState::STATE_NAMES = {"EMPTY", "IDLE", "EXEC", "WAIT", "STALL"};
std::vector<QColor> WaveState::STATE_COLORS = {
    QColor(254, 254, 254), QColor(127, 127, 127), QColor(0, 254, 0), QColor(254, 254, 0), QColor(254, 0, 0)};

int64_t Token::PosToClock(int64_t value)
{
    return (int64_t(value * 2 / MainWindow::getScaling() / (bIsNaviWave ? 12 : 3)) << mipmap_level);
}

static float tonemap(float x)
{
    x /= 255.0f;
    return 255.0f * float(cbrt(double(x * x)));
}

static QColor whiter(const QColor& a) { return QColor(a.red() / 2 + 127, a.green() / 2 + 127, a.blue() / 2 + 127); }

const QColor& Token::GetToneColor(int i)
{
    static std::vector<QColor> toned = []()
    {
        std::vector<QColor> tones;
        for (auto& c : Config::TokenColors())
            tones.push_back(QColor(tonemap(c.qcolor.red()), tonemap(c.qcolor.green()), tonemap(c.qcolor.blue())));
        return tones;
    }();
    return toned.at(i % toned.size());
}

void Token::DrawToken(QPainter& painter, int64_t viewstart, int64_t viewend, float penwidth) const
{
    int pos = GetTokenSize(this->clock - viewstart);
    int width = GetTokenSize(this->cycles);
    if (viewstart > this->clock)
    {
        pos = 0;
        width = GetTokenSize(this->clock + this->cycles - viewstart);
    }

    const float scaling = MainWindow::getScaling();
    int height = (TOKEN_HEIGHT() - SLOT_OFFSET() * std::max(0, 2 * slot - 1)) / scaling;
    int posy = (TOKEN_POSY() - SLOT_OFFSET() * std::min<int>(1, slot)) / scaling;

    QBrush brush;
    if (WindowColors::isDark())
    {
        const QColor& qcolor = GetQColor();
        QColor color(tonemap(qcolor.red()), tonemap(qcolor.green()), tonemap(qcolor.blue()));
        brush = QBrush(color);
    }
    else
    {
        const QColor& color = GetToneColor();
        QLinearGradient grad(0, posy, 0, height);
        grad.setColorAt(IF_WINDOWS_ELSE(0.8, 0.6), color);
        grad.setColorAt(0, whiter(color));
        brush = QBrush(grad);
    }

    QRectF rect(pos, posy, width, height);
    painter.setPen(QPen(Qt::black, penwidth));

    painter.fillRect(rect, brush);
    painter.drawRect(rect);
}

std::string Token::ToolTip() const
{
    int64_t _clock = clock;
    int _cycles = cycles;

    if (hiddenStall())
    {
        _clock -= stall;
        _cycles += stall;
    }

    return std::string(GetName()) + ", cycles: " + std::to_string(_cycles) + ", clk: " + std::to_string(_clock);
};

TokenMap::const_iterator TokenMap::get_token_in_clock(int64_t clock) const
{
    auto static_it = std::upper_bound(begin(), end(), Token{clock, 0, 0, 0, 0, 0, 0});
    if (static_it == begin()) return end();

    auto it = static_it;
    while (it != begin())
    {
        it = std::prev(it);
        if (it->inClock(clock)) return it;

        if (it->clock + it->cycles < clock && !it->overlapped()) break;
    }

    return std::prev(static_it);
}

void WaveState::DrawState(QPainter& painter, int64_t viewstart, int64_t viewend)
{
    const float scaling = MainWindow::getScaling();
    const float wstate_posy = WSTATE_POSY() / scaling;
    const float wstate_height = WSTATE_HEIGHT() / scaling;

    int pos = Token::GetTokenSize(this->clock - viewstart);
    int width = Token::GetTokenSize(this->duration);
    if (viewstart > this->clock)
    {
        pos = 0;
        width = Token::GetTokenSize(this->clock + this->duration - viewstart);
    }

    QColor& color = STATE_COLORS[this->state % STATE_COLORS.size()];
    QPainterPath path;
    path.addRect(QRectF(pos, wstate_posy, width, wstate_height));

    QLinearGradient grad(0, wstate_posy, 0, wstate_posy + wstate_height);
    grad.setColorAt(0.5, color);
    grad.setColorAt(0, whiter(color));
    QBrush brush(grad);
    painter.fillPath(path, brush);

    painter.drawPath(path);
}
