// MIT License
//
// Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
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

#include "license.h"
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

LICENSE::LICENSE()
{
    setWindowTitle("License information");
    setMinimumSize(640, 360);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);

    std::string repo = "https://github.com/ROCm/rocprof-compute-viewer/blob/amd-mainline/NOTICES";
    std::string notices = "For third party notices, see <a href=\"" + repo + "\">HERE</a>";

    QLabel* noticelabel = new QLabel(notices.c_str(), this);
    QLabel* licenseLabel = new QLabel(LICENSE::license, this);

    QFont font = noticelabel->font();
    font.setPointSize(12);
    noticelabel->setFont(font);
    noticelabel->setWordWrap(true);
    noticelabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    noticelabel->setMargin(10);

    noticelabel->setTextFormat(Qt::RichText);
    noticelabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    noticelabel->setOpenExternalLinks(true);

    licenseLabel->setWordWrap(true);
    licenseLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    licenseLabel->setMargin(10);

    scrollArea->setWidget(licenseLabel);

    QPushButton* closeButton = new QPushButton("Close", this);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    mainLayout->addWidget(noticelabel);
    mainLayout->addWidget(scrollArea);
    mainLayout->addWidget(closeButton);

    setLayout(mainLayout);
}
