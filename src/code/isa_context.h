// MIT License
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.

#pragma once

#include <QFont>

class ASMLine;
class QPainter;

namespace Isa
{
// App services used by the hand-painted ISA/source widgets. The host must
// outlive the viewer; individual instructions don't allocate or own a context.
class Context
{
public:
    virtual ~Context() = default;

    virtual QFont codeFont(const QFont& fallback) const = 0;
    virtual int currentIteration() const = 0;
    virtual bool hiddenLatencyAvailable() const = 0;
    virtual void scalePainter(QPainter& painter) const = 0;
    virtual double painterScale() const = 0;

    virtual void selectInstruction(const ASMLine& instruction) = 0;
    virtual void listingChanged() = 0;
};
} // namespace Isa
