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

#include <QDialog>
#include <utility>
#include <vector>
#include "data/input_detector.h"

class QButtonGroup;
class QCheckBox;
class QRadioButton;

/// Modal selector for the ATT_FILES input path. Lets the user pick exactly one
/// capture — keyed by (agent, dispatch) since different agents (GPUs) may
/// reuse the same dispatch_id from independent processes. Files for the chosen
/// (agent, dispatch) on every captured SE are loaded together so REALTIME
/// alignment can place them on a common shader-clock axis.
/// .out files are independently checkable (default all on).
///
/// Usage:
///   AttSelectorDialog dlg(parent, info);
///   if (dlg.exec() == QDialog::Accepted) info = dlg.selectedInfo();
class AttSelectorDialog : public QDialog
{
    Q_OBJECT

public:
    AttSelectorDialog(QWidget* parent, const InputInfo& info);

    /// InputInfo with att_files / att_file_info / out_files filtered to the
    /// user's selection. Only valid after exec() returned Accepted.
    InputInfo selectedInfo() const;

private:
    InputInfo m_info;
    QButtonGroup* m_dispatch_group = nullptr;
    std::vector<std::pair<int, int>> m_dispatch_keys; ///< (agent, dispatch); parallel to radio buttons
    std::vector<QCheckBox*> m_out_checks;             ///< parallel to m_info.out_files
};
