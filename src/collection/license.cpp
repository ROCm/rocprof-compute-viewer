#include "license.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QPushButton>

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
