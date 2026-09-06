#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief Abstract interface for filesystem package operations
 *
 * This interface defines the contract that filesystem packages must implement
 * to work with the Deki engine. It abstracts file operations,
 * path handling, and storage management.
 */

namespace Deki
{

class IFileSystem
{
public:
    /**
     * @brief File handle type - platform-specific implementation
     */
    typedef void* FileHandle;

    /**
     * @brief File open modes
     */
    enum class OpenMode
    {
        READ_BINARY,
        WRITE_BINARY,
        READ_TEXT,
        WRITE_TEXT
    };

    /**
     * @brief File seek origins
     */
    enum class SeekOrigin
    {
        BEGIN,
        CURRENT,
        END
    };

    virtual ~IFileSystem() = default;

    /**
     * @brief Initialize the file system
     * @return true if successful, false otherwise
     */
    virtual bool Initialize() = 0;

    /**
     * @brief Shutdown the file system and cleanup resources
     */
    virtual void Shutdown() = 0;

    /**
     * @brief Open a file
     * @param path Path to the file (platform-specific format)
     * @param mode Open mode
     * @return File handle or nullptr if failed
     */
    virtual FileHandle OpenFile(const char* path, OpenMode mode) = 0;

    /**
     * @brief Close a file
     * @param handle File handle
     */
    virtual void CloseFile(FileHandle handle) = 0;

    /**
     * @brief Read data from a file
     * @param handle File handle
     * @param buffer Buffer to read into
     * @param size Number of bytes to read
     * @return Number of bytes actually read
     */
    virtual size_t ReadFile(FileHandle handle, void* buffer, size_t size) = 0;

    /**
     * @brief Write data to a file
     * @param handle File handle
     * @param buffer Buffer to write from
     * @param size Number of bytes to write
     * @return Number of bytes actually written
     */
    virtual size_t WriteFile(FileHandle handle, const void* buffer, size_t size) = 0;

    /**
     * @brief Seek to a position in the file
     * @param handle File handle
     * @param offset Offset in bytes
     * @param origin Seek origin
     * @return New position, or -1 on error
     */
    virtual long SeekFile(FileHandle handle, long offset, SeekOrigin origin) = 0;

    /**
     * @brief Get current position in file
     * @param handle File handle
     * @return Current position, or -1 on error
     */
    virtual long TellFile(FileHandle handle) = 0;

    /**
     * @brief Get file size
     * @param handle File handle
     * @return File size in bytes, or -1 on error
     */
    virtual long GetFileSize(FileHandle handle) = 0;

    /**
     * @brief Check if file exists
     * @param path Path to the file
     * @return true if file exists, false otherwise
     */
    virtual bool FileExists(const char* path) = 0;

    /**
     * @brief Convert virtual path to platform-specific path
     * @param virtualPath Virtual path (e.g., "S:/assets/texture.bin")
     * @param outBuffer Buffer to store the converted path
     * @param bufferSize Size of the output buffer
     * @return true if conversion successful, false if buffer too small
     */
    virtual bool ConvertPath(const char* virtualPath, char* outBuffer, size_t bufferSize) = 0;

    /**
     * @brief Check if storage mode (USB MSC) is supported
     * @return true if the filesystem supports exposing storage via USB
     */
    virtual bool SupportsStorageMode() const { return false; }

    /**
     * @brief Enter or exit storage mode (USB Mass Storage)
     * @param enabled true to enter storage mode, false to exit
     * @return true if mode change successful
     *
     * When storage mode is enabled, the storage is exposed as a USB drive
     * and normal file operations will fail. Used for asset deployment.
     */
    virtual bool SetStorageMode(bool enabled) { return false; }

    /**
     * @brief Check if currently in storage mode
     * @return true if in storage mode (USB MSC active)
     */
    virtual bool IsStorageMode() const { return false; }
};

}  // namespace Deki
