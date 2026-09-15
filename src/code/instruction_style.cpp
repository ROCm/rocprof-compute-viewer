// MIT License
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.

#include "instruction_style.h"
#include <algorithm>
#include <initializer_list>
#include "isa_rows.h"

namespace Isa
{
std::string_view instructionMnemonic(std::string_view instruction)
{
    const auto start = instruction.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos || isLabel(instruction)) return {};
    instruction.remove_prefix(start);
    if (instruction.starts_with(';') || instruction.starts_with("//")) return {};
    return instruction.substr(0, instruction.find_first_of(" \t\r\n"));
}

namespace
{
bool startsWithAny(std::string_view opcode, std::initializer_list<std::string_view> prefixes)
{
    return std::any_of(prefixes.begin(), prefixes.end(), [opcode](auto prefix) { return opcode.starts_with(prefix); });
}
} // namespace

InstructionKind classifyInstruction(std::string_view instruction)
{
    const auto opcode = instructionMnemonic(instruction);
    // Condensed from rocprof-trace-decoder/source/trie.cpp. Its Trie is private,
    // not an installed API, and absent in decoder-free builds. These rules are
    // for presentation, not architecture-specific scheduling categories.
    // Specific prefixes precede family fallbacks. Only inspect the mnemonic:
    // an operand or comment containing "branch" must not change the category.
    // SKIP instructions generate no thread-trace token: leave them unstyled,
    // ahead of broader wait, scalar and buffer rules.
    if (startsWithAny(opcode, {"s_wait_dep", "s_wait_alu", "s_set_vgpr", "s_delay", "buffer_nop"}))
        return InstructionKind::Other;
    if (startsWithAny(opcode, {"v_wmma_", "v_mfma_"})) return InstructionKind::Matrix;
    if (opcode.starts_with("v_")) return InstructionKind::Vector;
    if (opcode.starts_with("s_"))
    {
        if (startsWithAny(
                opcode, {"s_branch", "s_cbranch", "s_call", "s_setpc", "s_swappc", "s_set_pc", "s_swap_pc", "s_rfe"}
            ))
            return InstructionKind::Branch;
        if (startsWithAny(opcode, {"s_ttrace", "s_endpgm", "s_sendmsg", "s_trap", "s_sethalt", "s_setkill"}))
            return InstructionKind::Control;
        if (opcode.starts_with("s_setprio_inc")) return InstructionKind::ScalarAlu;
        if (startsWithAny(
                opcode,
                {"s_wait",
                 "s_barrie",
                 "s_nop",
                 "s_sleep",
                 "s_wakeup",
                 "s_clause",
                 "s_setprio",
                 "s_set_inst",
                 "s_inst_prefetch",
                 "s_version",
                 "s_icache_inv",
                 "s_dcache_inv",
                 "s_incperflvl",
                 "s_decperflvl",
                 "s_setvskip",
                 "s_monitor"}
            ))
            return InstructionKind::Wait;
        if (opcode.starts_with("s_scratch")) return InstructionKind::Scratch;
        if (startsWithAny(
                opcode,
                {"s_load", "s_buffer", "s_atomic", "s_atc", "s_store", "s_dcache", "s_memrealtime", "s_prefetch"}
            ))
            return InstructionKind::ScalarMemory;
        // Includes arithmetic, compares, logic, shifts, selects and PC reads.
        return InstructionKind::ScalarAlu;
    }
    if (opcode.starts_with("scratch_")) return InstructionKind::Scratch;
    if (startsWithAny(opcode, {"flat_",     "global_",  "tbuffer_", "cluster_", "tensor_",  "dds_",     "buffer_l",
                               "buffer_s",  "buffer_a", "buffer_g", "buffer_i", "buffer_w", "buffer_p", "buffer_d",
                               "image_bvh", "image_l",  "image_s",  "image_a",  "image_m",  "image_g"}))
        return InstructionKind::Memory;
    if (startsWithAny(opcode, {"ds_", "lds_"})) return InstructionKind::Lds;
    return InstructionKind::Other;
}

QColor instructionColor(InstructionKind kind, bool dark, const QColor& fallback)
{
    // Darker, muted ink on light stripes; bright, low-saturation pastels on
    // dark stripes. Control and scratch share a wine/red tint in both themes.
    switch (kind)
    {
        // SALU is the lightest green; VALU is stronger and matrix is dark green.
        case InstructionKind::Vector: return dark ? QColor(133, 204, 145) : QColor(34, 90, 47);
        case InstructionKind::Matrix: return dark ? QColor(72, 158, 92) : QColor(27, 69, 38);
        case InstructionKind::ScalarMemory: return dark ? QColor(248, 226, 134) : QColor(105, 90, 27);
        case InstructionKind::Wait: return dark ? QColor(228, 230, 234) : QColor(50, 54, 60);
        case InstructionKind::ScalarAlu: return dark ? QColor(188, 226, 193) : QColor(48, 100, 60);
        case InstructionKind::Memory: return dark ? QColor(184, 227, 244) : QColor(34, 98, 118);
        case InstructionKind::Branch: return dark ? QColor(230, 203, 255) : QColor(113, 72, 145);
        case InstructionKind::Lds: return dark ? QColor(255, 210, 153) : QColor(132, 77, 36);
        case InstructionKind::Control:
        case InstructionKind::Scratch: return dark ? QColor(238, 190, 206) : QColor(138, 53, 82);
        default: return fallback;
    }
}
} // namespace Isa
