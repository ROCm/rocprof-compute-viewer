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
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include "config/appconfig.h"

class AppConfigTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        QSettings settings("AMD", "Rocprof-Compute-Viewer");
        settings.clear();
    }
};

TEST_F(AppConfigTest, UsesExistingFallbackWithoutSavedDirectory)
{
    QTemporaryDir fallback;
    ASSERT_TRUE(fallback.isValid());

    EXPECT_EQ(
        AppConfig::getInstance().getLastImportDirectory(fallback.path()),
        QDir::cleanPath(fallback.path())
    );
}

TEST_F(AppConfigTest, RemembersSelectedDirectory)
{
    QTemporaryDir selected;
    ASSERT_TRUE(selected.isValid());

    AppConfig::getInstance().setLastImportDirectory(selected.path());

    EXPECT_EQ(
        AppConfig::getInstance().getLastImportDirectory(),
        QDir::cleanPath(selected.path())
    );
}

TEST_F(AppConfigTest, RemembersParentDirectoryForSelectedFile)
{
    QTemporaryDir selected;
    ASSERT_TRUE(selected.isValid());
    const QString file_path = selected.filePath("trace.rocpd");
    QFile file(file_path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.close();

    AppConfig::getInstance().setLastImportDirectory(file_path);

    EXPECT_EQ(
        AppConfig::getInstance().getLastImportDirectory(),
        QDir::cleanPath(selected.path())
    );
}

TEST_F(AppConfigTest, IgnoresMissingSelection)
{
    QTemporaryDir selected;
    ASSERT_TRUE(selected.isValid());
    AppConfig::getInstance().setLastImportDirectory(selected.path());

    AppConfig::getInstance().setLastImportDirectory(selected.filePath("missing"));

    EXPECT_EQ(
        AppConfig::getInstance().getLastImportDirectory(),
        QDir::cleanPath(selected.path())
    );
}

int main(int argc, char** argv)
{
    QTemporaryDir settings_dir;
    if (!settings_dir.isValid()) return 1;

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
