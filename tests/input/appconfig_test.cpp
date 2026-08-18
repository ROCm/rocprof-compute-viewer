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

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>
#include "config/appconfig.h"

class AppConfigTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        QSettings settings("AMD", "Rocprof-Compute-Viewer");
        settings.clear();
        settings.sync();
    }

    void TearDown() override
    {
        QSettings settings("AMD", "Rocprof-Compute-Viewer");
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

TEST_F(AppConfigTest, PersistsGraphDetailOptions)
{
    AppConfig& config = AppConfig::getInstance();

    EXPECT_EQ(config.getLevelOfDetailBias(), 0);
    EXPECT_EQ(config.getPlotAlignment(), PlotAlignment::None);

    config.setLevelOfDetailBias(-3);
    config.setPlotAlignment(PlotAlignment::Detail);
    EXPECT_EQ(config.getLevelOfDetailBias(), -3);
    EXPECT_EQ(config.getPlotAlignment(), PlotAlignment::Detail);

    config.setPlotAlignment(PlotAlignment::Global);
    EXPECT_EQ(config.getPlotAlignment(), PlotAlignment::Global);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
