// MIT License
//
// Copyright (c) 2024-2026 Advanced Micro Devices, Inc. All rights reserved.

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
