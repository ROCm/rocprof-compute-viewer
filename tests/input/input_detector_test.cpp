#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "data/input_detector.h"

namespace fs = std::filesystem;

namespace {

struct TempDir
{
    fs::path path;

    explicit TempDir(const std::string& name) : path(fs::temp_directory_path() / name)
    {
        fs::remove_all(path);
        fs::create_directories(path);
    }

    ~TempDir() { fs::remove_all(path); }
};

void touch(const fs::path& path)
{
    std::ofstream out(path);
    out << "x";
}

} // namespace

TEST(InputDetector, UnknownForNonexistentPath)
{
    auto info = detectInput("/tmp/rcv_does_not_exist_123456789");
    EXPECT_EQ(info.type, InputType::UNKNOWN);
}

TEST(InputDetector, JsonDirectoryDetectedByFilenamesJson)
{
    TempDir dir("rcv_input_json_dir");
    touch(dir.path / "filenames.json");

    auto info = detectInput(dir.path.string());
    EXPECT_EQ(info.type, InputType::JSON_DIR);
    EXPECT_EQ(info.base_path, dir.path.string());
}

TEST(InputDetector, AttDirectoryDetectedWithoutOutFiles)
{
    TempDir dir("rcv_input_att_only_dir");
    touch(dir.path / "123_456_shader_engine_0_1.att");

    auto info = detectInput(dir.path.string());
    EXPECT_EQ(info.type, InputType::ATT_FILES);
    ASSERT_EQ(info.att_files.size(), 1u);
    EXPECT_TRUE(info.out_files.empty());
    ASSERT_EQ(info.att_file_info.size(), 1u);
    EXPECT_EQ(info.att_file_info[0].pid, 123);
    EXPECT_EQ(info.att_file_info[0].agent, 456);
    EXPECT_EQ(info.att_file_info[0].se, 0);
    EXPECT_EQ(info.att_file_info[0].dispatch, 1);
}

TEST(InputDetector, OutOnlyDirectoryIsUnknown)
{
    TempDir dir("rcv_input_out_only_dir");
    touch(dir.path / "123_gfx942_code_object_id_1.out");

    auto info = detectInput(dir.path.string());
    EXPECT_EQ(info.type, InputType::UNKNOWN);
    EXPECT_TRUE(info.att_files.empty());
    ASSERT_EQ(info.out_files.size(), 1u);
}

TEST(InputDetector, AttFilesAreSortedAndMetadataIsParallel)
{
    TempDir dir("rcv_input_sorted_att_dir");
    touch(dir.path / "9_99_shader_engine_2_7.att");
    touch(dir.path / "1_11_shader_engine_0_3.att");
    touch(dir.path / "5_55_shader_engine_1_4.att");

    auto info = detectInput(dir.path.string());
    EXPECT_EQ(info.type, InputType::ATT_FILES);
    ASSERT_EQ(info.att_files.size(), 3u);
    ASSERT_EQ(info.att_file_info.size(), info.att_files.size());

    EXPECT_LT(info.att_files[0], info.att_files[1]);
    EXPECT_LT(info.att_files[1], info.att_files[2]);

    for (size_t i = 0; i < info.att_files.size(); ++i)
        EXPECT_EQ(info.att_files[i], info.att_file_info[i].path);
}

TEST(InputDetector, JsonDirectoryTakesPrecedenceOverAttFiles)
{
    TempDir dir("rcv_input_json_precedence_dir");
    touch(dir.path / "filenames.json");
    touch(dir.path / "123_456_shader_engine_0_1.att");

    auto info = detectInput(dir.path.string());
    EXPECT_EQ(info.type, InputType::JSON_DIR);
    EXPECT_TRUE(info.att_files.empty());
}

TEST(InputDetector, RocpdFileDetected)
{
    TempDir dir("rcv_input_rocpd_dir");
    fs::path file = dir.path / "trace.rocpd";
    touch(file);

    auto info = detectInput(file.string());
    EXPECT_EQ(info.type, InputType::ROCPD);
    EXPECT_EQ(info.rocpd_path, file.string());
}

TEST(InputDetector, AttFileDetectedDirectly)
{
    TempDir dir("rcv_input_att_file_dir");
    fs::path att = dir.path / "123_456_shader_engine_0_1.att";
    touch(att);

    auto info = detectInput(att.string());
    EXPECT_EQ(info.type, InputType::ATT_FILES);
    EXPECT_EQ(info.base_path, dir.path.string());
    ASSERT_EQ(info.att_files.size(), 1u);
    EXPECT_EQ(info.att_files[0], att.string());
    ASSERT_EQ(info.att_file_info.size(), 1u);
    EXPECT_EQ(info.att_file_info[0].path, att.string());
}

TEST(InputDetector, MalformedAttFilenameStillRecognizedAsAttInput)
{
    TempDir dir("rcv_input_malformed_att_dir");
    touch(dir.path / "weird_name.att");

    auto info = detectInput(dir.path.string());
    EXPECT_EQ(info.type, InputType::ATT_FILES);
    ASSERT_EQ(info.att_file_info.size(), 1u);
    EXPECT_EQ(info.att_file_info[0].pid, -1);
    EXPECT_EQ(info.att_file_info[0].agent, -1);
    EXPECT_EQ(info.att_file_info[0].se, -1);
    EXPECT_EQ(info.att_file_info[0].dispatch, -1);
}
