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
    void SetUp() override
    {
        auto settings = TestConfig::openSettings();
        settings.clear();
        settings.sync();
    }

    void TearDown() override
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
        settings.setValue("InstructionColumnWidths/Instruction", invalid);
        EXPECT_EQ(config.getColumnWidth("Instruction"), -1) << invalid.toString().toStdString();
    }
    settings.remove("InstructionColumnWidths/Instruction");
}

TEST_F(AppConfigTest, NamedColumnSettingsRoundTripIndependently)
{
    auto& config = AppConfig::getInstance();
    auto settings = TestConfig::openSettings();
    const QStringList columns = {
        "View",
        "LineNumber",
        "Instruction",
        "Hitcount",
        "Latency",
        "Idle",
        "Samples",
        "Stalls",
        "Issued",
        "CodeObject",
        "Address",
        "SourceLink"
    };
    for (int i = 0; i < columns.size(); ++i)
    {
        const auto& column = columns[i];
        EXPECT_TRUE(config.getColumnVisible(column));
        EXPECT_FALSE(config.getColumnVisible(column, false));
        EXPECT_EQ(config.getColumnWidth(column), -1);
        config.setColumnVisible(column, i % 2 == 0);
        config.setColumnWidth(column, 100 + i);
    }
    for (int i = 0; i < columns.size(); ++i)
    {
        const auto& column = columns[i];
        SCOPED_TRACE(column.toStdString());
        EXPECT_EQ(config.getColumnVisible(column), i % 2 == 0);
        EXPECT_EQ(config.getColumnWidth(column), 100 + i);
        EXPECT_EQ(settings.value("InstructionColumns/" + column).toBool(), i % 2 == 0);
        EXPECT_EQ(settings.value("InstructionColumnWidths/" + column).toInt(), 100 + i);
        for (int boundary : {48, 4096})
        {
            config.setColumnWidth(column, boundary);
            EXPECT_EQ(config.getColumnWidth(column), boundary);
        }
        config.setColumnWidth(column, -1);
        EXPECT_EQ(config.getColumnWidth(column), -1);
        EXPECT_FALSE(settings.contains("InstructionColumnWidths/" + column));
    }
    for (const auto& key : settings.allKeys()) EXPECT_FALSE(key.contains("/Element"));
}

TEST_F(AppConfigTest, LegacyNumericColumnSettingsAreIgnored)
{
    auto& config = AppConfig::getInstance();
    auto settings = TestConfig::openSettings();
    // The order here deliberately describes the old numeric settings format.
    const QStringList legacy_columns = {
        "View",
        "Instruction",
        "Hitcount",
        "Latency",
        "Idle",
        "Samples",
        "Stalls",
        "Issued",
        "CodeObject",
        "Address",
        "SourceLink",
        "LineNumber"
    };
    for (int i = 0; i < legacy_columns.size(); ++i)
    {
        const bool default_visible = i < 10;
        settings.setValue(QString("InstructionColumns/Element%1").arg(i - 1), !default_visible);
        settings.setValue(QString("InstructionColumnWidths/Element%1").arg(i - 1), 200 + i);
    }
    settings.setValue("DisplayOptions/FontSize", 13);
    settings.setValue("DisplayOptions/DarkTheme", false);
    settings.setValue("SourceOptions/DisplayLineNumber", false);
    for (int i = 0; i < legacy_columns.size(); ++i)
    {
        SCOPED_TRACE(legacy_columns[i].toStdString());
        EXPECT_EQ(config.getColumnVisible(legacy_columns[i], i < 10), i < 10);
        EXPECT_EQ(config.getColumnWidth(legacy_columns[i]), -1);
        config.setColumnVisible(legacy_columns[i], i < 10);
        config.setColumnWidth(legacy_columns[i], 300 + i);
        EXPECT_EQ(settings.value(QString("InstructionColumns/Element%1").arg(i - 1)).toBool(), i >= 10);
        EXPECT_EQ(settings.value(QString("InstructionColumnWidths/Element%1").arg(i - 1)).toInt(), 200 + i);
    }
    EXPECT_EQ(config.getFontSize(), 13);
    EXPECT_FALSE(config.getDarkTheme());
    EXPECT_FALSE(config.getDisplayLineNumber());
}

TEST_F(AppConfigTest, NamedColumnsAndOtherSettingsPersistAcrossProcesses)
{
    AppConfig::getInstance().setFontSize(13);
    AppConfig::getInstance().setColumnWidth("View", 240);
    AppConfig::getInstance().setColumnVisible("LineNumber", false);
    AppConfig::getInstance().setColumnWidth("LineNumber", 120);
    AppConfig::getInstance().setColumnWidth("Instruction", 275);
    auto settings = TestConfig::openSettings();
    settings.sync();
    ASSERT_EQ(settings.status(), QSettings::NoError);
    QDir storage_dir = QFileInfo(settings.fileName()).absoluteDir();
    ASSERT_TRUE(storage_dir.cdUp()); // QSettings appends the organization directory.
    // A second QSettings in this process shares a cache, so only a fresh process
    // proves the saved value is on disk and restored by AppConfig at startup.
    QProcess reader;
    reader.start(QCoreApplication::applicationFilePath(), {"--read-settings", storage_dir.absolutePath()});
    ASSERT_TRUE(reader.waitForFinished(10000));
    ASSERT_EQ(reader.exitStatus(), QProcess::NormalExit);
    ASSERT_EQ(reader.exitCode(), 0) << reader.readAllStandardError().toStdString();
    EXPECT_EQ(reader.readAllStandardOutput().trimmed(), QByteArray("13 240 0 120 275"));
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--read-settings")
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        const auto path = QString::fromLocal8Bit(argv[2]);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, path);
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, path);
        auto& config = AppConfig::getInstance();
        QTextStream(stdout) << config.getFontSize() << ' ' << config.getColumnWidth("View") << ' '
                            << config.getColumnVisible("LineNumber") << ' ' << config.getColumnWidth("LineNumber")
                            << ' ' << config.getColumnWidth("Instruction");
        return 0;
    }
    TestConfig::IsolatedSettings settings;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
