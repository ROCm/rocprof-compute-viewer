#pragma once
#include <QDialog>

class LICENSE: public QDialog
{
    Q_OBJECT

public:
    LICENSE();
    virtual ~LICENSE() = default;

    static const char* license;
};
