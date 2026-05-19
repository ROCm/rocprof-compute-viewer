// MIT License
#pragma once

#include <string>

#include "data/datastore.h"
#include "data/input_detector.h"

enum class LoadValidationStatus
{
    Success,
    UnsupportedInput,
    LoadFailed
};

struct LoadValidationResult
{
    LoadValidationStatus status = LoadValidationStatus::Success;
    std::string message;
};

LoadValidationResult validateLoadedData(
    const InputInfo& input_info, const DataStore* store, bool trace_decoder_enabled
);
