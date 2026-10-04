#pragma once

#include <string>
#include <map>
#include <cstdint>
#include <cstdlib>

namespace Deki
{

/// Runtime configuration of one hardware package on a board: pin assignments
/// (GPIO numbers) and string settings, loaded from the platform JSON. The
/// editor's JSON-backed form is PackageConfigEntry (PlatformConfig.h).
///
/// Unrelated to installable packages (the deki-* DLLs in deki-packages.json).
struct PackageConfig
{
    std::string packageId;  // "sd_card", "gps", etc.
    bool enabled = false;
    std::map<std::string, int> pins;              // "MOSI" -> 11
    std::map<std::string, std::string> settings;  // "mode" -> "SPI"

    /// GPIO number of the pin `name` (such as "MOSI" or "CS"), or `defaultVal`.
    int GetPin(const std::string& name, int defaultVal = -1) const
    {
        auto it = pins.find(name);
        return (it != pins.end()) ? it->second : defaultVal;
    }

    /// The setting `name` (such as "mode" or "mount_point"), or `defaultVal`.
    std::string GetString(const std::string& name, const std::string& defaultVal = "") const
    {
        auto it = settings.find(name);
        return (it != settings.end()) ? it->second : defaultVal;
    }

    /// The setting `name` parsed as a decimal integer, or `defaultVal` when it
    /// is missing or not a number.
    int GetInt(const std::string& name, int defaultVal = 0) const
    {
        auto it = settings.find(name);
        if (it == settings.end())
        {
            return defaultVal;
        }
        // strtol, not std::stoi: embedded builds have no exceptions.
        const char* str = it->second.c_str();
        char* endPtr = nullptr;
        long val = std::strtol(str, &endPtr, 10);
        if (endPtr == str || *endPtr != '\0')
        {
            return defaultVal;
        }
        return static_cast<int>(val);
    }

    /// The setting `name` as a bool ("true", "1" or "yes" are true), or
    /// `defaultVal` when it is missing.
    bool GetBool(const std::string& name, bool defaultVal = false) const
    {
        auto it = settings.find(name);
        if (it == settings.end())
        {
            return defaultVal;
        }
        return (it->second == "true" || it->second == "1" || it->second == "yes");
    }
};

}  // namespace Deki
