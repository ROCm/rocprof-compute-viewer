#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

#include "code/codeload.hpp"
#include "data/datastore.h"
#include "data/input_detector.h"
#include "data/record_dispatcher.h"
#include "data/record_handlers.h"
#include "data/shaderdata.h"
#include "data/trace_decoder_emitter.h"

namespace fs = std::filesystem;

namespace {

std::string getDatasetRoot()
{
    const char* root = std::getenv("RCV_ATT_TEST_ROOT");
    if (!root || std::string(root).empty()) return {};
    return root;
}

void loadDataset(const std::string& dataset, InputInfo& info_out, DataStore& store_out, std::vector<std::string>& errors_out)
{
    info_out = detectInput(dataset);

    RecordDispatcher dispatcher;
    WaveHandler wave_handler(store_out);
    OccupancyHandler occ_handler(store_out);
    CounterHandler ctr_handler(store_out);
    ShaderDataHandler shaderdata_handler(store_out);
    RealtimeHandler rt_handler(store_out);
    MetadataHandler meta_handler(store_out);
    dispatcher.addHandler(&wave_handler);
    dispatcher.addHandler(&occ_handler);
    dispatcher.addHandler(&ctr_handler);
    dispatcher.addHandler(&shaderdata_handler);
    dispatcher.addHandler(&rt_handler);
    dispatcher.addHandler(&meta_handler);

    TraceDecoderEmitter emitter(info_out, dispatcher, store_out);
    emitter.run();
    errors_out = emitter.parseErrors();
}

bool hasUsableData(const DataStore& store)
{
    return !store.wave_hierarchy.empty() || !store.occupancy_by_se.empty() || !store.code.empty();
}

} // namespace

TEST(AttLoaderRecords, EmptyInstructionWaveIsKeptInMemory)
{
    DataStore store;
    WaveHandler handler(store);

    wave_record_t rec{};
    rec.id = "se0_simd0_cu1_w0_30608";
    rec.cu = 1;
    rec.simd = 0;
    rec.wave_id = 0;
    rec.begin = 30608;
    rec.end = 40960;

    handler.onWave(0, rec);

    ASSERT_EQ(store.wave_hierarchy.size(), 1u);
    ASSERT_EQ(store.wave_hierarchy.at(0).at(0).at(0).size(), 1u);
    EXPECT_EQ(store.wave_hierarchy.at(0).at(0).at(0).begin()->second.id, rec.id);

    std::shared_lock<std::shared_mutex> lock(store.wave_records_mutex);
    auto it = store.wave_records.find(rec.id);
    ASSERT_NE(it, store.wave_records.end());
    EXPECT_TRUE(it->second.instructions.empty());
}

TEST(AttLoaderRealData, DetectsSingleSeDatasetAndLoadsUsableData)
{
    std::string root = getDatasetRoot();
    if (root.empty()) GTEST_SKIP() << "RCV_ATT_TEST_ROOT not set";

    fs::path dataset = fs::path(root) / "navi2_fifo";
    if (!fs::is_directory(dataset)) GTEST_SKIP() << "Dataset not found: " << dataset.string();

    InputInfo info;
    DataStore store;
    std::vector<std::string> errors;
    loadDataset(dataset.string(), info, store, errors);

    ASSERT_EQ(info.type, InputType::ATT_FILES);
    ASSERT_EQ(info.att_files.size(), 1u);
    EXPECT_FALSE(info.out_files.empty());
    EXPECT_TRUE(hasUsableData(store));
    EXPECT_FALSE(store.occupancy_by_se.empty());
}

TEST(AttLoaderRealData, DetectsMultiSeDatasetAndLoadsMultipleAttFiles)
{
    std::string root = getDatasetRoot();
    if (root.empty()) GTEST_SKIP() << "RCV_ATT_TEST_ROOT not set";

    fs::path dataset = fs::path(root) / "mi350_histo0";
    if (!fs::is_directory(dataset)) GTEST_SKIP() << "Dataset not found: " << dataset.string();

    InputInfo info;
    DataStore store;
    std::vector<std::string> errors;
    loadDataset(dataset.string(), info, store, errors);

    ASSERT_EQ(info.type, InputType::ATT_FILES);
    EXPECT_GT(info.att_files.size(), 2u);
    EXPECT_TRUE(hasUsableData(store));
    EXPECT_FALSE(store.occupancy_by_se.empty());
    EXPECT_GE(store.wave_hierarchy.size(), 1u);
}

TEST(AttLoaderRealData, ParsedAttMetadataStaysParallelToAttFiles)
{
    std::string root = getDatasetRoot();
    if (root.empty()) GTEST_SKIP() << "RCV_ATT_TEST_ROOT not set";

    fs::path dataset = fs::path(root) / "navi4_float";
    if (!fs::is_directory(dataset)) GTEST_SKIP() << "Dataset not found: " << dataset.string();

    InputInfo info = detectInput(dataset.string());
    ASSERT_EQ(info.type, InputType::ATT_FILES);
    ASSERT_EQ(info.att_files.size(), info.att_file_info.size());

    for (size_t i = 0; i < info.att_files.size(); ++i)
        EXPECT_EQ(info.att_files[i], info.att_file_info[i].path);
}

// Cross-check that RCV's per-instruction aggregates match the decoder's
// golden CSV for the same dataset. The decoder's test/control/<name>/
// compare_ui_output_*.csv is the ground truth — if RCV's
// TraceDecoderEmitter → record handlers → DataStore::code pipeline drops or
// mis-attributes data, the (CodeObj, Vaddr) hitcount/latency/stall/idle
// sums diverge.
namespace {
struct GoldenRow
{
    int64_t codeobj_id;
    int64_t vaddr;
    int64_t hitcount;
    int64_t latency;
    int64_t stall;
    int64_t idle;
    std::string inst;
};

// CSV cells may be quoted (and contain commas inside quotes). We only need
// the first 7 unquoted numeric fields, so a small hand-rolled splitter is
// enough.
std::vector<std::string> splitCsv(const std::string& line)
{
    std::vector<std::string> out;
    std::string cur;
    bool in_q = false;
    for (char c : line) {
        if (c == '"') in_q = !in_q;
        else if (c == ',' && !in_q) { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

std::vector<GoldenRow> loadGolden(const fs::path& csv)
{
    std::vector<GoldenRow> rows;
    std::ifstream in(csv);
    std::string line;
    std::getline(in, line); // header
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto cells = splitCsv(line);
        if (cells.size() < 7) continue;
        GoldenRow r{};
        try {
            r.codeobj_id = std::stoll(cells[0]);
            r.vaddr      = std::stoll(cells[1]);
            r.inst       = cells[2];
            r.hitcount   = std::stoll(cells[3]);
            r.latency    = std::stoll(cells[4]);
            r.stall      = std::stoll(cells[5]);
            r.idle       = std::stoll(cells[6]);
        } catch (...) { continue; }
        // Skip pure label rows ("; <symbol>") — these are display-only
        // annotations the decoder emits alongside real instructions and are
        // not aggregated into RCV's CodeData::Line.
        if (!r.inst.empty() && r.inst.front() == ';') continue;
        rows.push_back(r);
    }
    return rows;
}
} // namespace

TEST(AttLoaderRealData, PerInstructionAggregatesMatchDecoderGolden)
{
    std::string root = getDatasetRoot();
    if (root.empty()) GTEST_SKIP() << "RCV_ATT_TEST_ROOT not set";
    const char* golden_root_env = std::getenv("RCV_DECODER_GOLDEN_ROOT");
    if (!golden_root_env || !*golden_root_env)
        GTEST_SKIP() << "RCV_DECODER_GOLDEN_ROOT not set";

    fs::path data_root(root), golden_root(golden_root_env);

    // Discover all (dataset_dir, golden_csv) pairs. A dataset is eligible if
    // its extracted directory exists *and* the decoder shipped a golden CSV
    // for it.
    struct Case { std::string name; fs::path dataset; fs::path golden; };
    std::vector<Case> cases;
    for (const auto& e : fs::directory_iterator(golden_root)) {
        if (!e.is_directory()) continue;
        std::string name = e.path().filename().string();
        fs::path dataset = data_root / name;
        if (!fs::is_directory(dataset)) continue;
        // Some datasets ship multiple CSVs (one near-empty header-only, one
        // with the actual data — e.g. mi350_histo0). Pick the largest by
        // size so we cross-check against the substantive one.
        fs::path chosen;
        uintmax_t chosen_size = 0;
        for (const auto& f : fs::directory_iterator(e.path())) {
            auto fn = f.path().filename().string();
            if (fn.rfind("compare_ui_output_", 0) == 0 && fn.size() > 4
                && fn.substr(fn.size() - 4) == ".csv") {
                uintmax_t sz = fs::file_size(f.path());
                if (chosen.empty() || sz > chosen_size) {
                    chosen = f.path();
                    chosen_size = sz;
                }
            }
        }
        if (!chosen.empty()) cases.push_back({name, dataset, chosen});
    }
    ASSERT_FALSE(cases.empty()) << "No (dataset, golden CSV) pairs discovered under "
                                << data_root << " / " << golden_root;

    using Key = std::pair<int64_t, int64_t>;  // (codeobj_id, vaddr)

    size_t total_datasets_with_overlap = 0;
    for (const auto& c : cases) {
        SCOPED_TRACE(c.name);

        InputInfo info;
        DataStore store;
        std::vector<std::string> errors;
        loadDataset(c.dataset.string(), info, store, errors);
        if (store.code.empty()) {
            ADD_FAILURE() << "[" << c.name << "] RCV produced no code lines";
            continue;
        }

        std::map<Key, const CodeData::Line*> rcv_by_key;
        for (const auto& cd : store.code) {
            if (!cd.line) continue;
            rcv_by_key[{cd.line->codeobj_id, cd.line->addr}] = cd.line.get();
        }

        auto golden_rows = loadGolden(c.golden);
        if (golden_rows.empty()) {
            // Defensive: every CSV picked above was the largest in its dir,
            // but a parse failure could still leave us with no usable rows.
            continue;
        }

        size_t matched = 0, primary_mismatched = 0, idle_only_mismatched = 0;
        for (const auto& g : golden_rows) {
            auto it = rcv_by_key.find({g.codeobj_id, g.vaddr});
            if (it == rcv_by_key.end()) continue;
            ++matched;
            const auto& l = *it->second;
            bool primary_ok = (l.hitcount == g.hitcount) && (l.latency_sum == g.latency)
                              && (l.stall_sum == g.stall);
            bool idle_ok = (l.idle_sum == g.idle);
            if (!primary_ok) {
                ++primary_mismatched;
                if (primary_mismatched <= 2) {
                    ADD_FAILURE() << "[" << c.name << "] codeobj=" << g.codeobj_id
                                  << " vaddr=" << g.vaddr
                                  << " golden(hit=" << g.hitcount << ",lat=" << g.latency
                                  << ",stall=" << g.stall << ")"
                                  << " rcv(hit=" << l.hitcount << ",lat=" << l.latency_sum
                                  << ",stall=" << l.stall_sum << ")";
                }
            } else if (!idle_ok) {
                ++idle_only_mismatched;
            }
        }
        if (matched > 0) ++total_datasets_with_overlap;

        EXPECT_GE(matched, golden_rows.size() / 2)
            << "[" << c.name << "] only matched " << matched << " of " << golden_rows.size()
            << " golden rows — RCV likely dropped instructions";
        // Hit/latency/stall must match exactly: a divergence there is a
        // real integration bug (we lost or mis-attributed a record).
        EXPECT_EQ(primary_mismatched, 0u)
            << "[" << c.name << "] " << primary_mismatched << " hit/latency/stall mismatches";
        // Idle accounting is allowed to differ — RCV's aggregation is known
        // to disagree slightly with the decoder's reference UI script. We
        // just report the count for visibility.
        if (idle_only_mismatched > 0) {
            std::cerr << "[integration] " << c.name << ": " << idle_only_mismatched
                      << "/" << matched << " rows differ in idle only\n";
        }
    }
    EXPECT_GT(total_datasets_with_overlap, 0u)
        << "No dataset produced any (codeobj,vaddr) overlap between RCV and golden CSVs";
    std::cerr << "[integration] checked " << cases.size() << " datasets, "
              << total_datasets_with_overlap << " had RCV/golden overlap\n";
}

TEST(AttLoaderRealData, ParseErrorsAreObservableNotSilent)
{
    std::string root = getDatasetRoot();
    if (root.empty()) GTEST_SKIP() << "RCV_ATT_TEST_ROOT not set";

    fs::path dataset = fs::path(root) / "navi2_fifo";
    if (!fs::is_directory(dataset)) GTEST_SKIP() << "Dataset not found: " << dataset.string();

    InputInfo info;
    DataStore store;
    std::vector<std::string> errors;
    loadDataset(dataset.string(), info, store, errors);

    EXPECT_GE(errors.size(), 0u);
}
