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
#include <QSettings>
#include <QStandardPaths>

class AppConfigTest : public ::testing::Test
{
protected:
    // AppConfig keeps one QSettings instance alive for the process. Clearing the
    // native settings store between tests invalidates that instance on Windows.
    static void SetUpTestSuite()
    {
        QSettings settings("AMD", "Rocprof-Compute-Viewer");
        settings.clear();
        settings.sync();
    }

    static void TearDownTestSuite()
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

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
