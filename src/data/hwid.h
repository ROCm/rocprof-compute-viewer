// MIT License
//
// Copyright (c) 2024-2026 Advanced Micro Devices, Inc. All rights reserved.

#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <tuple>

struct HWID
{
    int se = 0;
    int cu = -1;
    int simd = 0;
    int slot = 0;

    int wave_id() const { return slot; }
    bool hasCu() const { return cu >= 0; }

    bool operator<(const HWID& other) const
    {
        return std::tie(se, cu, simd, slot) < std::tie(other.se, other.cu, other.simd, other.slot);
    }

    bool operator==(const HWID& other) const
    {
        return se == other.se && cu == other.cu && simd == other.simd && slot == other.slot;
    }

    uint64_t packedKey() const
    {
        return (static_cast<uint64_t>(static_cast<uint16_t>(se))) |
               (static_cast<uint64_t>(static_cast<uint16_t>(cu)) << 16) |
               (static_cast<uint64_t>(static_cast<uint16_t>(simd)) << 32) |
               (static_cast<uint64_t>(static_cast<uint16_t>(slot)) << 48);
    }

    std::string toString() const
    {
        std::ostringstream ss;
        ss << "se=" << se << ",cu=" << cu << ",simd=" << simd << ",slot=" << slot;
        return ss.str();
    }
};
