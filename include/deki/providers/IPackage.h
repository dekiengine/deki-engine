#pragma once

#include <cstddef>
#include <cstdint>

namespace Deki
{

struct PackageConfig;

enum class PackageState
{
    Uninitialized,  // created, not yet initialized
    Initialized,    // ready
    Running,        // running continuously
    Error,
    Disabled  // turned off on purpose
};

/// What a hardware peripheral package (SD card, GPS, WiFi, ...) implements.
/// Packages are found through plugin exports and configured in the editor's
/// platform configuration.
///
/// Lifecycle:
/// 1. created by a factory
/// 2. Configure() with the pins and settings
/// 3. Initialize() sets up the hardware
/// 4. Update() every frame
/// 5. Shutdown() on engine shutdown
class IPackage
{
public:
    virtual ~IPackage() = default;

    /// Unique id, such as "sd_card", "gps" or "wifi".
    virtual const char* GetPackageId() const = 0;

    /// Display name, such as "SD Card".
    virtual const char* GetPackageName() const = 0;

    /// Category for grouping in the UI, such as "storage" or "sensors".
    virtual const char* GetPackageCategory() const = 0;

    /// Receives the platform's pins and settings before Initialize(); keep
    /// them for it.
    virtual void Configure(const PackageConfig& config) = 0;

    /// Sets up the hardware and resources, after Configure(). Returns false on
    /// failure.
    virtual bool Initialize() = 0;

    /// Releases all hardware resources, on engine shutdown or when the package
    /// is disabled.
    virtual void Shutdown() = 0;

    /// Called every frame with the seconds since the last one. Polling
    /// packages (such as GPS) read their data here; event-driven ones may do
    /// nothing.
    virtual void Update(float deltaTime) = 0;

    virtual PackageState GetState() const = 0;

    /// The last error message, or "" when there was none.
    virtual const char* GetLastError() const = 0;
};

}  // namespace Deki
