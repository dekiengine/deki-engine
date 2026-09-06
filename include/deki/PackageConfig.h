#pragma once

#include <string>
#include <map>
#include <cstdint>
#include <cstdlib>

/**
 * @brief Configuration for a hardware package instance
 *
 * Contains the runtime configuration for a hardware package, including:
 * - Pin assignments (GPIO numbers)
 * - Package-specific settings (strings)
 *
 * This is the runtime representation loaded from the platform JSON.
 * The editor uses PackageConfigEntry (in PlatformConfig.h) which is
 * serialized to/from JSON.
 *
 * Note: this struct is unrelated to the installable-package system (the
 * deki-* DLLs listed in deki-packages.json). It exists at the engine src/ root
 * rather than under src/packages/ to make that distinction clear.
 */

namespace Deki
{

struct PackageConfig
{
    std::string packageId;                        // "sd_card", "gps", etc.
    bool enabled = false;                         // Whether package is enabled
    std::map<std::string, int> pins;              // Pin assignments: "MOSI" -> 11
    std::map<std::string, std::string> settings;  // Settings: "mode" -> "SPI"

    /**
     * @brief Get a pin GPIO number by name
     * @param name Pin name (e.g., "MOSI", "CS")
     * @param defaultVal Value to return if pin not found
     * @return GPIO number or default value
     */
    int GetPin(const std::string& name, int defaultVal = -1) const
    {
        auto it = pins.find(name);
        return (it != pins.end()) ? it->second : defaultVal;
    }

    /**
     * @brief Get a string setting by name
     * @param name Setting name (e.g., "mode", "mount_point")
     * @param defaultVal Value to return if setting not found
     * @return Setting value or default value
     */
    std::string GetString(const std::string& name, const std::string& defaultVal = "") const
    {
        auto it = settings.find(name);
        return (it != settings.end()) ? it->second : defaultVal;
    }

    /**
     * @brief Get an integer setting by name (parsed from string)
     * @param name Setting name (e.g., "baud_rate", "address")
     * @param defaultVal Value to return if setting not found or invalid
     * @return Integer value or default value
     */
    int GetInt(const std::string& name, int defaultVal = 0) const
    {
        auto it = settings.find(name);
        if (it == settings.end()) return defaultVal;
        // Use simple parsing without exceptions for embedded platforms
        const char* str = it->second.c_str();
        char* endPtr = nullptr;
        long val = std::strtol(str, &endPtr, 10);
        if (endPtr == str || *endPtr != '\0') return defaultVal;
        return static_cast<int>(val);
    }

    /**
     * @brief Get a boolean setting by name
     * @param name Setting name (e.g., "auto_mount", "enabled")
     * @param defaultVal Value to return if setting not found
     * @return Boolean value or default value
     */
    bool GetBool(const std::string& name, bool defaultVal = false) const
    {
        auto it = settings.find(name);
        if (it == settings.end()) return defaultVal;
        return (it->second == "true" || it->second == "1" || it->second == "yes");
    }
};

}  // namespace Deki
