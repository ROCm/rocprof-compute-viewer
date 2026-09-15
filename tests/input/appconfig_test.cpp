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

#include "config/appconfig.h"
#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QTextStream>
#include "../config_test_settings.h"

class AppConfigTest : public ::testing::Test
{
protected:
    // This is the temporary INI store configured in main(), never user settings.
    static void SetUpTestSuite()
    {
        auto settings = TestConfig::openSettings();
        settings.clear();
        settings.sync();
    }

    static void TearDownTestSuite()
    {
        auto settings = TestConfig::openSettings();
        settings.clear();
        settings.sync();
    }
};

TEST_F(AppConfigTest, AppliesFamilyDefaultsAndKeepsSameFamilyOverrides)
{
    AppConfig& config = AppConfig::getInstance();

    EXPECT_TRUE(config.resolveLoadWaveStatesForTrace(9, "vega"));
    config.setLoadWaveStates(false);
    EXPECT_FALSE(config.resolveLoadWaveStatesForTrace(9, "vega"));

    EXPECT_FALSE(config.resolveLoadWaveStatesForTrace(12, "navi"));
    config.setLoadWaveStates(true);
    EXPECT_TRUE(config.resolveLoadWaveStatesForTrace(12, "navi"));

    EXPECT_TRUE(config.resolveLoadWaveStatesForTrace(9, "vega"));
}

TEST_F(AppConfigTest, InvalidStoredColumnWidthsFallBackToAutomaticSizing)
{
    auto& config = AppConfig::getInstance();
    auto settings = TestConfig::openSettings();
    for (const auto& invalid : {QVariant("invalid"), QVariant(-50), QVariant(0), QVariant(47), QVariant(4097)})
    {
        settings.setValue("InstructionColumnWidths/Element0", invalid);
        EXPECT_EQ(config.getColumnWidth(0), -1) << invalid.toString().toStdString();
    }
    settings.remove("InstructionColumnWidths/Element0");
}

TEST_F(AppConfigTest, FontSizePersistsAcrossProcesses)
{
    AppConfig::getInstance().setFontSize(13);
    auto settings = TestConfig::openSettings();
    settings.sync();
    ASSERT_EQ(settings.status(), QSettings::NoError);
    QDir storage_dir = QFileInfo(settings.fileName()).absoluteDir();
    ASSERT_TRUE(storage_dir.cdUp()); // QSettings appends the organization directory.
    // A second QSettings in this process shares a cache, so only a fresh process
    // proves the saved value is on disk and restored by AppConfig at startup.
    QProcess reader;
    reader.start(QCoreApplication::applicationFilePath(), {"--read-font-size", storage_dir.absolutePath()});
    ASSERT_TRUE(reader.waitForFinished(10000));
    ASSERT_EQ(reader.exitStatus(), QProcess::NormalExit);
    ASSERT_EQ(reader.exitCode(), 0) << reader.readAllStandardError().toStdString();
    EXPECT_EQ(reader.readAllStandardOutput().trimmed(), QByteArray("13"));
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--read-font-size")
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        const auto path = QString::fromLocal8Bit(argv[2]);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, path);
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, path);
        QTextStream(stdout) << AppConfig::getInstance().getFontSize();
        return 0;
    }
    TestConfig::IsolatedSettings settings;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
