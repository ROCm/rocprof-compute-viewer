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

#include "appconfig.h"
#include <QDir>
#include <QStandardPaths>

AppConfig& AppConfig::getInstance()
{
    static AppConfig instance;
    return instance;
}

AppConfig::AppConfig() : settings("AMD", "Rocprof-Compute-Viewer") {}

// Graph Options
bool AppConfig::getLevelOfDetail() const { return settings.value("GraphOptions/LevelOfDetail", true).toBool(); }

void AppConfig::setLevelOfDetail(bool enabled) { settings.setValue("GraphOptions/LevelOfDetail", enabled); }

// Source Options
bool AppConfig::getDisplayLineNumber() const
{
    return settings.value("SourceOptions/DisplayLineNumber", true).toBool();
}

void AppConfig::setDisplayLineNumber(bool enabled) { settings.setValue("SourceOptions/DisplayLineNumber", enabled); }

int AppConfig::getSourceHotspotSize() const { return settings.value("SourceOptions/HotspotSize", 100).toInt(); }

void AppConfig::setSourceHotspotSize(int size) { settings.setValue("SourceOptions/HotspotSize", size); }

// Display Options
int AppConfig::getFontSize() const { return settings.value("DisplayOptions/FontSize", 9).toInt(); }

void AppConfig::setFontSize(int size) { settings.setValue("DisplayOptions/FontSize", size); }

bool AppConfig::getDarkTheme() const { return settings.value("DisplayOptions/DarkTheme", true).toBool(); }

void AppConfig::setDarkTheme(bool enabled) { settings.setValue("DisplayOptions/DarkTheme", enabled); }

bool AppConfig::getDisplayScaling() const { return settings.value("DisplayOptions/DisplayScaling", true).toBool(); }

void AppConfig::setDisplayScaling(bool enabled) { settings.setValue("DisplayOptions/DisplayScaling", enabled); }

bool AppConfig::getSeparateLDSPipe() const { return settings.value("DisplayOptions/SeparateLDSPipe", false).toBool(); }

void AppConfig::setSeparateLDSPipe(bool enabled) { settings.setValue("DisplayOptions/SeparateLDSPipe", enabled); }

// Instruction Column Visibility
bool AppConfig::getColumnVisible(int element, bool bDefault) const
{
    return settings.value(QString("InstructionColumns/Element%1").arg(element), bDefault).toBool();
}

void AppConfig::setColumnVisible(int element, bool enabled)
{
    settings.setValue(QString("InstructionColumns/Element%1").arg(element), enabled);
}
