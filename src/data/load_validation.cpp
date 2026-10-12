// MIT License

#include "data/load_validation.h"
#include <algorithm>

LoadValidationResult validateLoadedData(const InputInfo& input_info, const DataStore* store, bool trace_decoder_enabled)
{
    LoadValidationResult result;

    if (!trace_decoder_enabled && (input_info.type == InputType::ATT_FILES || input_info.type == InputType::ROCPD))
    {
        result.status = LoadValidationStatus::UnsupportedInput;
        result.message = "This build does not include trace-decoder support.";
        return result;
    }

    if (!store)
    {
        result.status = LoadValidationStatus::LoadFailed;
        result.message = "No datastore was produced.";
        return result;
    }

    const bool has_wave_hierarchy = !store->wave_hierarchy.empty();
    const bool has_occupancy = !store->occupancy_by_se.empty();
    const bool has_code = !store->code.empty();
    const bool has_spm = !store->spm.empty();
    const auto has_records = [](const auto& records_by_se)
    {
        return std::any_of(
            records_by_se.begin(), records_by_se.end(), [](const auto& entry) { return !entry.second.empty(); }
        );
    };
    const bool has_events = has_records(store->trace_events_by_se) || has_records(store->dispatch_records_by_se);

    if (!has_wave_hierarchy && !has_occupancy && !has_code && !has_spm && !has_events)
    {
        result.status = LoadValidationStatus::LoadFailed;
        result.message = "Input did not produce usable viewer data.";
    }

    return result;
}
