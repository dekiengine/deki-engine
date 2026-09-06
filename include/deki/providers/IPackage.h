#pragma once

#include <cstddef>
#include <cstdint>

// Forward declaration

namespace Deki
{

struct PackageConfig;

/**
 * @brief Package lifecycle states
 */
enum class PackageState
{
    Uninitialized,  // Package created but not yet initialized
    Initialized,    // Package initialized and ready
    Running,        // Package actively running (for continuous packages)
    Error,          // Package encountered an error
    Disabled        // Package explicitly disabled
};

/**
 * @brief Abstract interface for hardware peripheral packages
 *
 * This interface defines the contract that hardware packages (SD card, GPS, WiFi, etc.)
 * must implement to work with the Deki engine. Packages are discovered via plugin exports
 * and configured through the editor's platform configuration.
 *
 * Lifecycle:
 * 1. Package created via factory
 * 2. Configure() called with pin/setting configuration
 * 3. Initialize() called to set up hardware
 * 4. Update() called each frame (for polling packages)
 * 5. Shutdown() called on engine shutdown
 */
class IPackage
{
public:
    virtual ~IPackage() = default;

    /**
     * @brief Get the unique package identifier
     * @return Package ID string (e.g., "sd_card", "gps", "wifi")
     */
    virtual const char* GetPackageId() const = 0;

    /**
     * @brief Get the human-readable package name
     * @return Display name (e.g., "SD Card", "GPS Package")
     */
    virtual const char* GetPackageName() const = 0;

    /**
     * @brief Get the package category for UI grouping
     * @return Category string (e.g., "storage", "sensors", "communication")
     */
    virtual const char* GetPackageCategory() const = 0;

    /**
     * @brief Configure the package with pin mappings and settings
     *
     * Called before Initialize() to provide configuration from the platform.
     * The package should store these settings for use during initialization.
     *
     * @param config Configuration containing pins and settings
     */
    virtual void Configure(const PackageConfig& config) = 0;

    /**
     * @brief Initialize the package hardware
     *
     * Called after Configure(). Should set up hardware, allocate resources,
     * and prepare the package for operation.
     *
     * @return true if initialization successful, false otherwise
     */
    virtual bool Initialize() = 0;

    /**
     * @brief Shutdown the package and release resources
     *
     * Called during engine shutdown or when package is disabled.
     * Should safely release all hardware resources.
     */
    virtual void Shutdown() = 0;

    /**
     * @brief Update the package (called each frame)
     *
     * For polling-based packages (e.g., GPS), this is called each frame
     * to read data and update state. For event-based packages, this may
     * be a no-op.
     *
     * @param deltaTime Time elapsed since last frame in seconds
     */
    virtual void Update(float deltaTime) = 0;

    /**
     * @brief Get the current package state
     * @return Current lifecycle state
     */
    virtual PackageState GetState() const = 0;

    /**
     * @brief Get the last error message
     * @return Error string, or empty string if no error
     */
    virtual const char* GetLastError() const = 0;
};

}  // namespace Deki
