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

#include "datastore.h"

#include <shared_mutex>

#include "data/wavemanager.h"

void DataStore::forEachWave(const WaveVisitor& visitor) const
{
    for (const auto& [se, simd_map] : wave_hierarchy)
        for (const auto& [simd, slot_map] : simd_map)
            for (const auto& [slot, instance_map] : slot_map)
                for (const auto& [instance, entry] : instance_map)
                    visitor(
                        {
                            {se, -1, simd, slot},
                            instance
                    },
                        entry
                    );
}

std::shared_ptr<WaveInstance> DataStore::getWave(const WaveEntry& entry)
{
    {
        std::shared_lock<std::shared_mutex> lock(wave_records_mutex);
        auto it = wave_records.find(entry.id);
        if (it != wave_records.end()) return WaveInstance::GetFromRecord(entry.id, it->second, code);
    }

    return WaveInstance::Get(ui_dir + entry.id, entry.time_offset);
}
