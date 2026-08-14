#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "data/json_emitter.h"
#include "data/record_handlers.h"
#include "data/shaderdata.h"
#include "json/include/nlohmann/json.hpp"
#include "util/memtracker.h"

int MemTracker::count = 0;
std::unordered_map<std::string, int> MemTracker::classes;

std::shared_ptr<WaveInstance> DataStore::getWave(const WaveEntry&) { return nullptr; }

namespace fs = std::filesystem;

namespace
{
template <typename Record> int clusterId(const Record& rec)
{
    if constexpr (requires { rec.cluster_id; })
        return static_cast<int>(rec.cluster_id);
    else
        return 0;
}

fs::path freshTempDir(const std::string& name)
{
    fs::path dir = fs::temp_directory_path() / ("rcv_json_emitter_" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void writeJson(const fs::path& path, const nlohmann::json& data)
{
    std::ofstream out(path);
    out << data.dump();
}

void writeWaveHierarchy(const fs::path& dir, int shader_engine_count)
{
    nlohmann::json filenames;
    for (int se = 0; se < shader_engine_count; se++)
        filenames["wave_filenames"][std::to_string(se)]["0"]["0"]["0"] = {
            "se" + std::to_string(se) + "_wave.json", 0, 1};
    writeJson(dir / "filenames.json", filenames);
}

void loadOccupancyOnly(const fs::path& dir, DataStore& store)
{
    RecordDispatcher dispatcher;
    OccupancyHandler occupancy_handler(store);
    dispatcher.addHandler(&occupancy_handler);

    JsonRecordEmitter emitter(dir.string() + "/", dispatcher, store);
    emitter.runOccupancyOnlyForTests();
    dispatcher.signalComplete();
}

void loadJsonDirectory(const fs::path& dir, DataStore& store, bool load_wave_states = true)
{
    RecordDispatcher dispatcher;
    JsonRecordEmitter emitter(dir.string() + "/", dispatcher, store, load_wave_states);
    emitter.run();
}
} // namespace

TEST(JsonRecordEmitterWaveStates, LoadsPrecomputedWaveStateSeries)
{
    fs::path dir = freshTempDir("wave_states");
    writeJson(dir / "code.json", {{"header", nlohmann::json::array()}, {"code", nlohmann::json::array()}});
    writeWaveHierarchy(dir, 1);
    writeJson(dir / "wstates2.json", {{"time", {10, 20, 30}}, {"state", {1, 2, 3}}});
    writeJson(dir / "wstates3.json", {{"time", {21795, 21796}}, {"state", {1, 0}}});
    writeJson(dir / "wstates4.json", {{"time", {10, 20, 30}}, {"state", {6, 7, 8}}});

    DataStore store;
    loadJsonDirectory(dir, store);

    ASSERT_EQ(store.wave_state_series.size(), 3u);
    ASSERT_EQ(store.wave_state_series.at(2).size(), 3u);
    EXPECT_FLOAT_EQ(store.wave_state_series.at(2).at(0).time, 10.0f);
    EXPECT_FLOAT_EQ(store.wave_state_series.at(2).at(0).value, 1.0f);
    EXPECT_FLOAT_EQ(store.wave_state_series.at(3).at(1).value, 0.0f);
    EXPECT_FLOAT_EQ(store.wave_state_series.at(4).at(2).time, 30.0f);
    EXPECT_FLOAT_EQ(store.wave_state_series.at(4).at(2).value, 8.0f);
}

TEST(JsonRecordEmitterWaveStates, SkipsWaveStateFilesForMultipleShaderEngines)
{
    fs::path dir = freshTempDir("wave_states_multi_se");
    writeJson(dir / "code.json", {{"header", nlohmann::json::array()}, {"code", nlohmann::json::array()}});
    writeWaveHierarchy(dir, 2);
    {
        std::ofstream malformed(dir / "wstates2.json");
        malformed << "not valid JSON";
    }

    DataStore store;
    testing::internal::CaptureStderr();
    loadJsonDirectory(dir, store);
    const std::string diagnostics = testing::internal::GetCapturedStderr();

    ASSERT_EQ(store.wave_hierarchy.size(), 2u);
    EXPECT_TRUE(store.wave_state_series.empty());
    EXPECT_EQ(diagnostics.find("wstates2.json"), std::string::npos);
}

TEST(JsonRecordEmitterWaveStates, SkipsWaveStateFilesWhenDisabled)
{
    fs::path dir = freshTempDir("wave_states_disabled");
    writeJson(dir / "code.json", {{"header", nlohmann::json::array()}, {"code", nlohmann::json::array()}});
    writeWaveHierarchy(dir, 1);
    {
        std::ofstream malformed(dir / "wstates2.json");
        malformed << "not valid JSON";
    }

    DataStore store;
    testing::internal::CaptureStderr();
    loadJsonDirectory(dir, store, false);
    const std::string diagnostics = testing::internal::GetCapturedStderr();

    ASSERT_EQ(store.wave_hierarchy.size(), 1u);
    EXPECT_TRUE(store.wave_state_series.empty());
    EXPECT_EQ(diagnostics.find("wstates2.json"), std::string::npos);
}

TEST(JsonRecordEmitterOccupancy, ReadsLegacySixColumnOccupancyJson)
{
    fs::path dir = freshTempDir("legacy");

    nlohmann::json occupancy;
    occupancy["version"] = "3.0.0";
    occupancy["dispatches"] = {
        {"5", "oldKernel"}
    };
    occupancy["0"] =
        nlohmann::json::array({nlohmann::json::array({10, 2, 1, 3, 1, 5}), nlohmann::json::array({20, 2, 1, 3, 0, 5})});
    writeJson(dir / "occupancy.json", occupancy);

    DataStore store;
    loadOccupancyOnly(dir, store);

    ASSERT_EQ(store.occupancy_by_se.size(), 1u);
    ASSERT_EQ(store.occupancy_by_se.at(0).size(), 2u);
    EXPECT_FALSE(store.occupancy_has_dispatcher_info);
    EXPECT_TRUE(store.trace_events_by_se.empty());
    EXPECT_TRUE(store.dispatch_records_by_se.empty());

    const auto& start = store.occupancy_by_se.at(0).front();
    EXPECT_EQ(start.time, 10);
    EXPECT_EQ(start.cu, 2);
    EXPECT_EQ(start.simd, 1);
    EXPECT_EQ(start.wave_id, 3);
    EXPECT_EQ(start.start, 1u);
    EXPECT_EQ(start.me_id, 0u);
    EXPECT_EQ(start.pipe_id, 0u);
    EXPECT_EQ(start.is_ext, 0u);
    EXPECT_EQ(start.workgroup_id, 0u);
    EXPECT_EQ(clusterId(start), 0);
    EXPECT_EQ(store.dispatch_resolver.Resolve(start.pc), 5);
    EXPECT_EQ(store.dispatch_resolver.Name(5), "oldKernel");
}

TEST(JsonRecordEmitterOccupancy, ReadsSchema31OccupancyEventsAndDispatches)
{
    fs::path dir = freshTempDir("schema31");

    nlohmann::json occupancy;
    occupancy["version"] = "3.1.0";
    occupancy["occupancy_fields"] = {
        "time",
        "cu",
        "simd",
        "wave_id",
        "start",
        "kernel_id",
        "me_id",
        "pipe_id",
        "is_ext",
        "workgroup_id",
        "cluster_id"};
    occupancy["dispatches"] = {
        {"7", "matrixTranspose"}
    };
    occupancy["0"] = nlohmann::json::array(
        {nlohmann::json::array({100, 4, 2, 1, 1, 7, 3, 5, 1, 42, 9}),
         nlohmann::json::array({150, 4, 2, 1, 0, 7, 3, 5, 0, 42, 0})}
    );
    nlohmann::json dispatch_event = {
        {"kind",              "dispatch"                                },
        {"time",              80                                        },
        {"me_id",             3                                         },
        {"pipe_id",           5                                         },
        {"kernel_id",         7                                         },
        {"kernel_name",       "matrixTranspose"                         },
        {"entry_point",       {{"address", 4096}, {"code_object_id", 2}}},
        {"user_sgprs",        8                                         },
        {"vgprs",             32                                        },
        {"sgprs",             64                                        },
        {"lds_size",          256                                       },
        {"thread_dim_x",      32                                        },
        {"thread_dim_y",      16                                        },
        {"thread_dim_z",      1                                         },
        {"dispatch_pkt_addr", 8192                                      },
        {"byte_offset",       24                                        },
        {"flags",             17                                        }
    };
    nlohmann::json trace_event = {
        {"kind",        "event"},
        {"time",        90     },
        {"type",        4      },
        {"me_id",       3      },
        {"pipe_id",     5      },
        {"flags",       2      },
        {"payload",     2748   },
        {"byte_offset", 16     }
    };
    occupancy["events"]["0"] = nlohmann::json::array({dispatch_event, trace_event});
    writeJson(dir / "occupancy.json", occupancy);

    DataStore store;
    loadOccupancyOnly(dir, store);

    ASSERT_EQ(store.occupancy_by_se.size(), 1u);
    ASSERT_EQ(store.occupancy_by_se.at(0).size(), 2u);
    EXPECT_TRUE(store.occupancy_has_dispatcher_info);
    EXPECT_EQ(store.dispatch_resolver.Name(7), "matrixTranspose");

    const auto& start = store.occupancy_by_se.at(0).front();
    EXPECT_EQ(start.time, 100);
    EXPECT_EQ(start.cu, 4);
    EXPECT_EQ(start.simd, 2);
    EXPECT_EQ(start.wave_id, 1);
    EXPECT_EQ(start.start, 1u);
    EXPECT_EQ(start.me_id, 3u);
    EXPECT_EQ(start.pipe_id, 5u);
    EXPECT_EQ(start.is_ext, 1u);
    EXPECT_EQ(start.workgroup_id, 42u);
    EXPECT_EQ(clusterId(start), 9);
    EXPECT_EQ(store.dispatch_resolver.Resolve(start.pc), 7);

    ASSERT_EQ(store.dispatch_records_by_se.at(0).size(), 1u);
    const auto& dispatch = store.dispatch_records_by_se.at(0).front();
    EXPECT_EQ(dispatch.time, 80);
    EXPECT_EQ(dispatch.me_id, 3u);
    EXPECT_EQ(dispatch.pipe_id, 5u);
    EXPECT_EQ(dispatch.user_sgprs, 8u);
    EXPECT_EQ(dispatch.vgprs, 32u);
    EXPECT_EQ(dispatch.sgprs, 64u);
    EXPECT_EQ(dispatch.lds_size, 256u);
    EXPECT_EQ(dispatch.thread_dim_x, 32u);
    EXPECT_EQ(dispatch.thread_dim_y, 16u);
    EXPECT_EQ(dispatch.thread_dim_z, 1u);
    EXPECT_EQ(dispatch.dispatch_pkt_addr, 8192u);
    EXPECT_EQ(dispatch.byte_offset, 24u);
    EXPECT_EQ(dispatch.flags, 17);
    EXPECT_EQ(dispatch.entry_point.address, 4096u);
    EXPECT_EQ(dispatch.entry_point.code_object_id, 2u);

    ASSERT_EQ(store.trace_events_by_se.at(0).size(), 1u);
    const auto& event = store.trace_events_by_se.at(0).front();
    EXPECT_EQ(event.time, 90);
    EXPECT_EQ(static_cast<int>(event.type), 4);
    EXPECT_EQ(event.me_id, 3u);
    EXPECT_EQ(event.pipe_id, 5u);
    EXPECT_EQ(event.flags, 2u);
    EXPECT_EQ(event.payload.raw, 2748u);
    EXPECT_EQ(event.byte_offset, 16u);
}
