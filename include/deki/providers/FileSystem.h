#pragma once

#include <memory>
#include <vector>
#include <string>
#include <deki/providers/IFileSystem.h>

/**
 * @brief File System Provider for handling different platform filesystem implementations
 *
 * Holds the process-wide filesystem. Platform integration packages install a
 * concrete implementation via SetFileSystem(); there is no build-time platform
 * enum - which one you get is decided by which package is loaded.
 */

namespace Deki
{

class FileSystem
{
private:
    static std::unique_ptr<IFileSystem> s_CurrentFilesystem;
    static bool initialized;
    // Two or three entries ("F:/", "S:/"), matched by prefix on every file
    // open. A std::map keyed by std::string built a std::string from the
    // path and one substr per entry for each lookup.
    struct PrefixEntry
    {
        std::string prefix;
        IFileSystem* fs;
    };
    static std::vector<PrefixEntry> s_RegisteredFilesystems;
    static IFileSystem* s_DefaultFilesystem;

public:
    /**
     * @brief Initialize the filesystem manager
     * @return true if initialization successful, false otherwise
     */
    static bool Initialize();

    /**
     * @brief Shutdown the current filesystem
     */
    static void Shutdown();

    /**
     * @brief Get the current filesystem implementation
     * @return Pointer to current filesystem, or nullptr if not initialized
     */
    static IFileSystem* GetCurrentFileSystem();

    /**
     * @brief Check if the filesystem manager is initialized
     * @return true if initialized, false otherwise
     */
    static bool IsInitialized() { return initialized; }

    /**
     * @brief Set a custom filesystem implementation
     * @param fs Pointer to filesystem (takes ownership)
     * @return true if successful
     */
    static bool SetFileSystem(IFileSystem* fs);

    /**
     * @brief Check if storage mode (USB MSC) is supported
     * @return true if the current filesystem supports storage mode
     */
    static bool SupportsStorageMode();

    /**
     * @brief Enter or exit storage mode (USB Mass Storage)
     * @param enabled true to enter storage mode, false to exit
     * @return true if mode change successful
     */
    static bool SetStorageMode(bool enabled);

    /**
     * @brief Check if currently in storage mode
     * @return true if in storage mode (USB MSC active)
     */
    static bool IsStorageMode();

    // =========================================================================
    // Multi-Filesystem Support (F:/, S:/ prefix routing)
    // =========================================================================

    /**
     * @brief Register a filesystem for a specific path prefix
     * @param prefix Path prefix (e.g., "F:/", "S:/")
     * @param fs Filesystem to handle paths with this prefix
     */
    static void RegisterFileSystem(const std::string& prefix, IFileSystem* fs);

    /**
     * @brief Unregister a filesystem for a path prefix
     * @param prefix Path prefix to unregister
     */
    static void UnregisterFileSystem(const std::string& prefix);

    /**
     * @brief Get the filesystem that handles a specific path
     * @param path Path to look up (e.g., "F:/boot.scene", "S:/main.scene", or "scenes/Demo")
     * @return Filesystem for this path, or default filesystem if no prefix match
     */
    static IFileSystem* GetFileSystemForPath(const char* path);

    /**
     * @brief Set the default filesystem for non-prefixed paths
     * @param fs Filesystem to use for paths without a registered prefix (e.g., "scenes/Demo")
     *
     * This allows code to use simple relative paths like "scenes/Demo" instead of
     * prefixed paths like "S:/scenes/Demo". The default filesystem will handle
     * path resolution appropriately for the platform.
     */
    static void SetDefaultFileSystem(IFileSystem* fs);
};

}  // namespace Deki
