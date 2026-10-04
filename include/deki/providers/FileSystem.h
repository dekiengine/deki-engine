#pragma once

#include <memory>
#include <vector>
#include <string>
#include <deki/providers/IFileSystem.h>

namespace Deki
{

/// Holds the process-wide filesystems. Platform packages install their
/// implementation with SetFileSystem(), so the loaded package decides which
/// one is used.
class FileSystem
{
private:
    static std::unique_ptr<IFileSystem> s_CurrentFilesystem;
    static bool s_Initialized;
    // Two or three entries ("F:/", "S:/"), matched by prefix on every file
    // open. A plain vector, so a lookup builds no strings.
    struct PrefixEntry
    {
        std::string prefix;
        IFileSystem* fs;
    };
    static std::vector<PrefixEntry> s_RegisteredFilesystems;
    static IFileSystem* s_DefaultFilesystem;

public:
    /// Returns false on failure.
    static bool Initialize();

    static void Shutdown();

    /// The installed filesystem, or nullptr before one is set.
    static IFileSystem* GetCurrentFileSystem();

    static bool IsInitialized() { return s_Initialized; }

    /// Installs `fs` and takes ownership of it.
    static bool SetFileSystem(IFileSystem* fs);

    /// True when the filesystem can be exposed as USB mass storage.
    static bool SupportsStorageMode();

    /// Enters or leaves USB mass storage mode. Returns false on failure.
    static bool SetStorageMode(bool enabled);

    /// True while USB mass storage is active.
    static bool IsStorageMode();

    // =========================================================================
    // Several filesystems, chosen by path prefix (F:/, S:/)
    // =========================================================================

    /// Routes paths starting with `prefix` (such as "F:/" or "S:/") to `fs`.
    static void RegisterFileSystem(const std::string& prefix, IFileSystem* fs);

    static void UnregisterFileSystem(const std::string& prefix);

    /// The filesystem for `path` (such as "F:/boot.scene" or "scenes/Demo"):
    /// the one registered for its prefix, else the default.
    static IFileSystem* GetFileSystemForPath(const char* path);

    /// The filesystem for paths with no registered prefix, so code can use
    /// "scenes/Demo" instead of "S:/scenes/Demo"; it resolves them as suits
    /// the platform.
    static void SetDefaultFileSystem(IFileSystem* fs);
};

}  // namespace Deki
