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

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "data/hwid.h"
#include "data/marker_walker.h"
#include "data/records.h"
#include "json/include/nlohmann/json.hpp"

#ifdef RCV_HAS_TRACE_DECODER
namespace rocprof_trace_decoder
{
namespace codeobj
{
class CodeobjAddressTranslate;
}
} // namespace rocprof_trace_decoder
#endif

/// A single shaderdata record: ["time","value","cu","simd","wave_id","flags"]
struct ShaderDataRecord
{
    int64_t time{0};
    uint32_t value{0};
    int cu{0};
    int simd{0};
    int wave_id{0};
    int flags{0};
    int se{0}; // Shader Engine, derived from filename

    /// Build a tooltip string for this record.
    std::string ToolTip() const;
};

/// Shared, sorted vector of shaderdata records (avoids copying across views).
using ShaderDataRecordVec = std::shared_ptr<const std::vector<ShaderDataRecord>>;

/// Find a shaderdata record near a clock position within a sorted record vector.
/// @param records  Shared pointer to sorted records (may be null/empty).
/// @param clock_pos  The clock position to search around.
/// @param hit_width  The hit-test width in clock units.
/// @return Index of the matching record, or -1 if none found.
int FindShaderDataRecord(const ShaderDataRecordVec& records, int64_t clock_pos, int64_t hit_width);

/// Manages loading and querying shaderdata records from JSON files.
class ShaderDataManager
{
public:
    ShaderDataManager() = default;

    /// Parse the "shaderdata_filenames" section of filenames.json and load all records.
    /// Files are loaded in parallel using multiple threads.
    /// @param shaderdata_filenames The JSON object: { "SE_num": [["file", begin, end], ...], ... }
    /// @param base_dir The directory containing the shaderdata files.
    void Load(const nlohmann::json& shaderdata_filenames, const std::string& base_dir);

    /// Get shared pointer to records for a given SE, CU, SIMD, and slot (sorted by time).
    /// The shaderdata wave_id field corresponds to the occupancy slot.
    ShaderDataRecordVec GetRecords(HWID hwid) const;

    /// Add a single record from the decoder path (thread-safe with external lock).
    void AddRecord(int se, const shaderdata_record_t& rec);

    /// Finalize after all AddRecord calls: sort buckets and wrap in shared_ptr.
    void Finalize();

    /// Add a per-SE clock offset to records for that SE.
    void ApplyTimeOffsets(const std::map<int, int64_t>& offsets);

    /// Check if any shaderdata was loaded.
    bool HasData() const { return m_has_data; }

    /// Shader engines that have pending or finalized shaderdata records.
    std::set<int> SEs() const;

    // ─── Markers (SQTT instrumentation) ─────────────────────────────────────

    /// Callback type used by ResolveMarkers to map a raw marker id at a
    /// concrete shaderdata location/time to a funcmap entry. Marker ids are
    /// code-object scoped; callers must use their own active-code-object lookup
    /// before resolving the id.
    using MarkerResolveAtFn = std::function<ResolvedMarker(HWID hwid, uint32_t id, int64_t time)>;

    /// Clear decoded marker state and diagnostics while leaving raw shaderdata
    /// records intact.
    void ClearMarkers();

    /// After all records are loaded, decode every record's `value` field into
    /// MarkerSpans using a caller-supplied scoped resolver.
    void ResolveMarkers(const MarkerResolveAtFn& resolver);

#ifdef RCV_HAS_TRACE_DECODER
    /// Callback type used by ResolveMarkers to find the active code object at
    /// a given (se,cu,simd,slot,time). The caller (TraceDecoderEmitter) owns
    /// the lookup machinery; ShaderDataManager just asks for an ID.
    /// Return 0 when no active code object can be identified; the marker id
    /// will remain unresolved.
    using ActiveCodeobjFn = std::function<uint64_t(HWID hwid, int64_t time)>;

    /// After all records are loaded (Load + Finalize done), decode every
    /// record's `value` field into MarkerSpans using the funcmap from the
    /// supplied CodeobjAddressTranslate. Idempotent — safe to call once at
    /// end of load. Skipped for buckets with no shaderdata records.
    void ResolveMarkers(
        rocprof_trace_decoder::codeobj::CodeobjAddressTranslate& codeobj_map, const ActiveCodeobjFn& active_codeobj_fn
    );
#endif

    /// Per-bucket marker spans (null if no markers were resolved for the bucket).
    MarkerSpanVec GetMarkers(HWID hwid) const;

    /// Iterate every non-empty marker bucket. Functor signature:
    ///   void(HWID hwid, const MarkerSpanVec& spans)
    template <class F> void ForEachMarkerBucket(F&& f) const
    {
        for (const auto& [hwid, spans] : m_markers_by_location) f(hwid, spans);
    }

    /// True if any bucket produced spans.
    bool HasMarkers() const { return m_has_markers; }

    /// Diagnostics emitted during marker resolution. Empty until ResolveMarkers
    /// is called; non-empty when the marker stream has unknown IDs or stack anomalies.
    const std::vector<MarkerDiagnostic>& GetMarkerDiagnostics() const { return m_marker_diags; }

private:
    /// Pending records accumulated via AddRecord, before Finalize.
    std::map<HWID, std::vector<ShaderDataRecord>> m_pending;
    /// Load a single shaderdata JSON file and return its records.
    static std::vector<ShaderDataRecord> LoadFile(const std::string& filepath, int se);

    /// Key: (se, cu, simd, slot/wave_id) -> shared sorted records
    std::map<HWID, ShaderDataRecordVec> m_records_by_location;

    /// Key: (se, cu, simd, slot) -> shared marker spans (sorted by enter_time).
    std::map<HWID, MarkerSpanVec> m_markers_by_location;

    std::vector<MarkerDiagnostic> m_marker_diags;
    bool m_has_data = false;
    bool m_has_markers = false;
};
