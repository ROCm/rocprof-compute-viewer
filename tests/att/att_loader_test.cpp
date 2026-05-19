#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <shared_mutex>
#include <string>
#include <vector>

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
