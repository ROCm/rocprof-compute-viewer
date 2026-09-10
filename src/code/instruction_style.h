// MIT License
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.

#pragma once

#include <QColor>
#include <string_view>

namespace Isa
{
enum class InstructionKind
{
    Other,
    Vector,
    Matrix,
    ScalarMemory,
    Wait,
    ScalarAlu,
    Memory,
    Branch,
    Control,
    Lds,
    Scratch
};

// A view into the first token, excluding indentation, labels and comments.
std::string_view instructionMnemonic(std::string_view instruction);
InstructionKind classifyInstruction(std::string_view instruction);
QColor instructionColor(InstructionKind kind, bool dark, const QColor& fallback);
} // namespace Isa
