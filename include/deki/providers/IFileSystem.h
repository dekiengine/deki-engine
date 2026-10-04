#pragma once

#include <cstddef>
#include <cstdint>

namespace Deki
{

/// What a filesystem package implements: file operations, path conversion
/// and USB storage mode.
class IFileSystem
{
public:
    /// Opaque; each implementation decides what it points to.
    typedef void* FileHandle;

    enum class OpenMode
    {
        ReadBinary,
        WriteBinary,
        ReadText,
        WriteText
    };

    enum class SeekOrigin
    {
        Begin,
        Current,
        End
    };

    virtual ~IFileSystem() = default;

    /// Returns false on failure.
    virtual bool Initialize() = 0;

    virtual void Shutdown() = 0;

    /// Opens `path` (in this filesystem's format). Returns nullptr on failure.
    virtual FileHandle OpenFile(const char* path, OpenMode mode) = 0;

    virtual void CloseFile(FileHandle handle) = 0;

    /// Reads up to `size` bytes. Returns the number read.
    virtual size_t ReadFile(FileHandle handle, void* buffer, size_t size) = 0;

    /// Writes `size` bytes. Returns the number written.
    virtual size_t WriteFile(FileHandle handle, const void* buffer, size_t size) = 0;

    /// Returns the new position, or -1 on error.
    virtual long SeekFile(FileHandle handle, long offset, SeekOrigin origin) = 0;

    /// Current position, or -1 on error.
    virtual long TellFile(FileHandle handle) = 0;

    /// Size in bytes, or -1 on error.
    virtual long GetFileSize(FileHandle handle) = 0;

    virtual bool FileExists(const char* path) = 0;

    /// Turns a virtual path (such as "S:/assets/texture.bin") into this
    /// platform's path. Returns false when `outBuffer` is too small.
    virtual bool ConvertPath(const char* virtualPath, char* outBuffer, size_t bufferSize) = 0;

    /// True when the storage can be exposed as a USB drive (mass storage).
    virtual bool SupportsStorageMode() const { return false; }

    /// Enters or leaves USB mass storage mode, used to deploy assets. While it
    /// is on, the storage is a USB drive and file operations fail. Returns
    /// false when the change failed.
    virtual bool SetStorageMode(bool enabled) { return false; }

    /// True while USB mass storage is active.
    virtual bool IsStorageMode() const { return false; }
};

}  // namespace Deki
