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

#include "attselector.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>
#include <map>
#include <set>

namespace
{
QString humanSize(uint64_t bytes)
{
    constexpr double KB = 1024.0;
    constexpr double MB = 1024.0 * 1024.0;
    constexpr double GB = 1024.0 * 1024.0 * 1024.0;
    if (bytes >= GB) return QString::number(bytes / GB, 'f', 1) + " GB";
    if (bytes >= MB) return QString::number(bytes / MB, 'f', 1) + " MB";
    if (bytes >= KB) return QString::number(bytes / KB, 'f', 1) + " KB";
    return QString::number(bytes) + " B";
}
} // namespace

AttSelectorDialog::AttSelectorDialog(QWidget* parent, const InputInfo& info) : QDialog(parent), m_info(info)
{
    setWindowTitle(tr("Select Trace Files"));
    setModal(true);
    resize(640, 480);

    auto* main_layout = new QVBoxLayout(this);

    // ---- Header explaining the constraint ----
    auto* header = new QLabel(
        tr("Pick exactly one dispatch to visualize. Each capture has its own shader-clock "
           "domain; files from different dispatches cannot be shown on a common timeline."),
        this
    );
    header->setWordWrap(true);
    main_layout->addWidget(header);

    // ---- Capture group: one radio button per unique (agent, dispatch) ----
    // Different agents (GPUs) may reuse the same dispatch_id from independent
    // processes, so the identity has to include the agent.
    struct DispatchSummary
    {
        std::set<int> ses;
        uint64_t total_bytes = 0;
        int file_count = 0;
        std::vector<int> file_indices; ///< indices into m_info.att_files
    };
    std::map<std::pair<int, int>, DispatchSummary> by_capture;
    for (size_t i = 0; i < m_info.att_file_info.size(); ++i)
    {
        const auto& f = m_info.att_file_info[i];
        auto& d = by_capture[{f.agent, f.dispatch}];
        if (f.se >= 0) d.ses.insert(f.se);
        d.total_bytes += f.file_size;
        d.file_count += 1;
        d.file_indices.push_back(static_cast<int>(i));
    }

    auto* dispatch_box = new QGroupBox(tr("Capture (Agent · Dispatch)"), this);
    auto* dispatch_layout = new QVBoxLayout(dispatch_box);
    m_dispatch_group = new QButtonGroup(this);
    m_dispatch_group->setExclusive(true);

    bool first = true;
    for (auto& [key, sum] : by_capture)
    {
        const int agent_id = key.first;
        const int disp_id = key.second;

        QString ses_str;
        if (sum.ses.empty())
            ses_str = tr("?");
        else
        {
            QStringList list;
            for (int se : sum.ses) list << QString::number(se);
            ses_str = list.join(",");
        }

        QString agent_str = (agent_id < 0) ? tr("?") : QString::number(agent_id);
        QString disp_str = (disp_id < 0) ? tr("(unparsed)") : QString::number(disp_id);
        QString label = tr("Agent %1 · Dispatch %2 — SEs: %3 — %4 — %5 file(s)")
                            .arg(agent_str)
                            .arg(disp_str)
                            .arg(ses_str)
                            .arg(humanSize(sum.total_bytes))
                            .arg(sum.file_count);

        auto* rb = new QRadioButton(label, dispatch_box);
        if (first)
        {
            rb->setChecked(true);
            first = false;
        }
        dispatch_layout->addWidget(rb);
        m_dispatch_group->addButton(rb, static_cast<int>(m_dispatch_keys.size()));
        m_dispatch_keys.push_back({agent_id, disp_id});
    }
    main_layout->addWidget(dispatch_box);

    // ---- .out files: independent checkboxes (default all on) ----
    auto* out_box = new QGroupBox(tr("Code Objects (.out)"), this);
    auto* out_outer = new QVBoxLayout(out_box);
    auto* out_scroll = new QScrollArea(out_box);
    out_scroll->setWidgetResizable(true);
    auto* out_inner = new QWidget(out_scroll);
    auto* out_layout = new QVBoxLayout(out_inner);
    out_layout->setContentsMargins(2, 2, 2, 2);

    if (m_info.out_files.empty())
    {
        out_layout->addWidget(new QLabel(tr("(no .out files in this directory)"), out_inner));
    }
    else
    {
        m_out_checks.reserve(m_info.out_files.size());
        for (const auto& path : m_info.out_files)
        {
            QFileInfo fi(QString::fromStdString(path));
            QString label = QString("%1   (%2)").arg(fi.fileName()).arg(humanSize(fi.size()));
            auto* cb = new QCheckBox(label, out_inner);
            cb->setChecked(true);
            out_layout->addWidget(cb);
            m_out_checks.push_back(cb);
        }
    }
    out_layout->addStretch(1);
    out_scroll->setWidget(out_inner);
    out_outer->addWidget(out_scroll);
    main_layout->addWidget(out_box, /*stretch=*/1);

    // ---- Buttons ----
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, this);
    if (auto* open_btn = buttons->button(QDialogButtonBox::Open))
    {
        open_btn->setDefault(true);
        open_btn->setAutoDefault(true);
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    main_layout->addWidget(buttons);

    // ---- Centering ----
    // Center on parent when available, otherwise on the active screen — Qt's
    // default placement otherwise drops the dialog wherever the WM picks.
    QRect anchor;
    if (parent)
        anchor = parent->frameGeometry();
    else if (QScreen* scr = QApplication::primaryScreen())
        anchor = scr->availableGeometry();
    if (!anchor.isNull())
    {
        QSize chosen = size();
        QPoint topLeft(anchor.center().x() - chosen.width() / 2, anchor.center().y() - chosen.height() / 2);
        move(topLeft);
    }
}

InputInfo AttSelectorDialog::selectedInfo() const
{
    InputInfo out = m_info;

    // Resolve which (agent, dispatch) is selected.
    int selected_id = m_dispatch_group->checkedId();
    if (selected_id < 0 || selected_id >= static_cast<int>(m_dispatch_keys.size()))
    {
        // Shouldn't happen with exclusive group + initial check, but be defensive.
        return out;
    }
    const auto chosen = m_dispatch_keys[static_cast<size_t>(selected_id)];

    // Filter att_files / att_file_info to the chosen capture.
    out.att_files.clear();
    out.att_file_info.clear();
    for (const auto& f : m_info.att_file_info)
    {
        if (f.agent == chosen.first && f.dispatch == chosen.second)
        {
            out.att_files.push_back(f.path);
            out.att_file_info.push_back(f);
        }
    }

    // Filter out_files by checkbox state.
    out.out_files.clear();
    for (size_t i = 0; i < m_out_checks.size() && i < m_info.out_files.size(); ++i)
    {
        if (m_out_checks[i]->isChecked()) out.out_files.push_back(m_info.out_files[i]);
    }
    return out;
}
