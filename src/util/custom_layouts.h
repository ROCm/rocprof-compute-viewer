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

#pragma once

#ifdef _WIN32
#    include <QtCore/QtCore>
#    include <QtCore>
#    include <QtWidgets/QBoxLayout>
#    include <QtWidgets/QRadioButton>
#    include <QtWidgets>
#    define IF_WINDOWS(x) x
#    define NOT_WINDOWS(x)                                                                                             \
        {}
#    define IF_WINDOWS_ELSE(x, y) x
#else
#    include <QBoxLayout>
#    include <QRadioButton>
#    include <QtCore>
#    define IF_WINDOWS(x)                                                                                              \
        {}
#    define NOT_WINDOWS(x)        x
#    define IF_WINDOWS_ELSE(x, y) y
#endif
#include "memtracker.h"

#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#    define IF_QT6_ELSE(x, y) x
#else
#    define IF_QT6_ELSE(x, y) y
#endif

//! A QVBoxLayout that force deletes it's widgets on destruct
class QVBox : public QVBoxLayout
{
    Q_OBJECT
    set_tracked();

public:
    QVBox() : QVBoxLayout()
    {
        setSpacing(0);
        setContentsMargins(0, 0, 0, 0);
    }
    QVBox(QWidget* w) : QVBoxLayout(w)
    {
        setSpacing(0);
        setContentsMargins(0, 0, 0, 0);
    }
    virtual ~QVBox();
};

//! A QHBoxLayout that force deletes it's widgets on destruct
class QHBox : public QHBoxLayout
{
    Q_OBJECT
    set_tracked();

public:
    QHBox() : QHBoxLayout()
    {
        setSpacing(0);
        setContentsMargins(0, 0, 0, 0);
    }
    QHBox(QWidget* w) : QHBoxLayout(w)
    {
        setSpacing(0);
        setContentsMargins(0, 0, 0, 0);
    }
    virtual ~QHBox();
};

//! A QGridLayout that force deletes it's widgets on destruct
class QBox : public QGridLayout
{
    Q_OBJECT
    set_tracked();

public:
    QBox() : QGridLayout()
    {
        setSpacing(0);
        setContentsMargins(0, 0, 0, 0);
    }
    QBox(QWidget* w) : QGridLayout(w)
    {
        setSpacing(0);
        setContentsMargins(0, 0, 0, 0);
    }
    virtual ~QBox();
};

class QSelButton : public QRadioButton
{
    Q_OBJECT
    set_tracked();

public:
    QSelButton(const QString& str, QWidget* parent) : QRadioButton(str, parent){};
};
