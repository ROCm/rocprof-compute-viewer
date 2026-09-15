// MIT License
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.

#pragma once

#include <QSettings>
#include <QTemporaryDir>
#include "config/appconfig.h"

namespace TestConfig
{
inline QSettings openSettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, "AMD", "Rocprof-Compute-Viewer");
}

// Configure before constructing AppConfig or any widget that uses it. Never
// clear or write the real user preferences, even if isolation regresses.
class IsolatedSettings
{
public:
    IsolatedSettings()
    {
        if (!directory.isValid()) qFatal("Cannot create temporary test settings directory");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path() + "/user");
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, directory.path() + "/system");
        auto settings = openSettings();
        // Two values prevent a coincidentally matching real preference from
        // passing the check. Only the explicitly isolated store is written.
        for (int size : {11, 9})
        {
            settings.setValue("DisplayOptions/FontSize", size);
            settings.sync();
            if (settings.status() != QSettings::NoError || AppConfig::getInstance().getFontSize() != size)
                qFatal("AppConfig test isolation failed; refusing to modify user settings");
        }
        settings.remove("DisplayOptions/FontSize");
    }

    ~IsolatedSettings()
    {
        // Flush AppConfig's shared settings cache before removing its directory.
        auto settings = openSettings();
        settings.sync();
    }

private:
    QTemporaryDir directory;
};
} // namespace TestConfig
