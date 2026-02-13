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

#pragma once

#include <QBoxLayout>
#include <QWidget>
#include <array>
#include <map>
#include <set>
#include <vector>
#include "data/shaderdata.h"
#include "data/wavemanager.h"
#include "measure.h"
#include "signal.h"
#include "token.h"
#include "wave/othersimd.h"

//! A timeline view of a waveslot[0,9] within a SIMD.
class QWaveView : public QWidget
{
    Q_OBJECT
    set_tracked();
    using Super = QWidget;

public:
    explicit QWaveView(class QCustomScroll* parent);
    virtual ~QWaveView()
    {
        if (tool) tool->update_list.erase(this);
    }

    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void leaveEvent(QEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    void onScroll(int value) { update(); }

    void Reset();
    std::shared_ptr<class ScrollValue> view;
    std::map<int64_t, std::shared_ptr<TokenGroup>> waves;

private:
    void setLineHover(int line_index, bool hover);
    void clearLineHover();

    std::shared_ptr<MeasureTool> tool;
    int hovering_line_index = -1;
public slots:
    void onupdatebar() { update(); }
signals:
};

//! A collection of QWaveView (s). Used by SIMD/CU View.
class QWaveSlots : public QWidget
{
    Q_OBJECT
    set_tracked();
    using Super = QWidget;

public:
    QWaveSlots(class QCustomScroll* parent);
    virtual ~QWaveSlots()
    {
        if (tool) tool->update_list.erase(this);
    }

    class QLabel* AddSlot(QWidget* view, const std::string& name, int fixedsize, bool useMonospace = false);
    virtual void Clear() { Reset(); };

    virtual void wheelEvent(QWheelEvent* event) override;
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;

    class QVBox* names_layout = nullptr;
    class QVBox* content_layout = nullptr;
    class QWidget* cuwaves_content = nullptr;

    std::shared_ptr<class ScrollValue> view;
    std::shared_ptr<MeasureTool> tool;
public slots:
    void onupdatebar() { update(); }

protected:
    void Reset();
signals:
};

class QUtilView : public QWaveView
{
    Q_OBJECT
    set_tracked();

public:
    virtual void paintEvent(QPaintEvent* event) override;
    QUtilView(class QCustomScroll* parent);
    void Add(Token token);
    void AddTokens(const std::vector<Token>& tokens);
    void Compile(bool bAlwaysVisible);

    std::shared_ptr<TokenGroup> wave0{nullptr};
    std::mutex mut{};
    class QLabel* label{nullptr};
};

//! A collection of QWaveView (s). Used by SIMD/CU View.
class QUtilization : public QWaveSlots
{
    Q_OBJECT
    set_tracked();

public:
    QUtilization(QCustomScroll* parent);
    void AddTokens(int simd, const TokenMap& tokens);
    virtual void Clear() override;
    virtual void Compile();
    void SetOtherSimdSources(OtherSimdFiles files);
    void PopulateOtherSimdTokens(int se, int simd, int64_t clock_start, int64_t clock_end);
    const std::vector<Token>& GetOtherSimdTokens() const { return other_simd.Tokens(); }
    int GetOtherSimdId() const { return other_simd_id; }

    static bool bSeparateLDSPipe;

    std::array<std::array<QUtilView*, 4>, 16> short_token_defs{};
    std::array<std::array<QUtilView*, 4>, 16> expanded_token_defs{};

    std::vector<QUtilView*> all_views{};

    std::array<QUtilView*, 4> VALU{};
    std::array<QUtilView*, 4> VMEM{};
    std::array<QUtilView*, 4> SCAL{};
    QUtilView* IMMED{};
    QUtilView* MSG{};

    // Expanded defs
    std::array<QUtilView*, 4> LDS{};
    std::array<QUtilView*, 4> RT{};
    std::array<QUtilView*, 4> WMMA{};
    QUtilView* JUMP{};

private:
    OtherSimdData other_simd;
    int other_simd_id = 0;

    void ClearOtherSimd();
signals:
};

//! A track that displays shaderdata records as markers.
class QShaderDataView : public QWidget
{
    Q_OBJECT
    set_tracked();
    using Super = QWidget;

public:
    QShaderDataView(class QCustomScroll* parent, ShaderDataRecordVec records);

    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;

private:
    std::shared_ptr<class ScrollValue> view;
    ShaderDataRecordVec shaderdata_records;

public slots:
    void onupdatebar() { update(); }
signals:
};
