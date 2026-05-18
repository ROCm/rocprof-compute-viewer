// MIT License

#include "data/load_validation.h"

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

    if (!has_wave_hierarchy && !has_occupancy && !has_code)
    {
        result.status = LoadValidationStatus::LoadFailed;
        result.message = "Input did not produce usable viewer data.";
    }

    return result;
}
