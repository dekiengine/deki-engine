#pragma once

#include <deki/providers/IFileSystem.h>
#include <string>

/**
 * @brief ESP32 file system implementation using LittleFS via ESP-IDF VFS
 *
 * Uses esp_vfs_littlefs (ESP-IDF native) with POSIX file operations.
 * Virtual paths: F:/file.bin → /littlefs/file.bin
 */

namespace Deki
{

class ESP32FileSystem : public IFileSystem
{
private:
    bool m_Initialized;

    /**
     * @brief Convert virtual path to VFS path
     * @param virtualPath Virtual path (e.g., "F:/dproject.bin")
     * @return VFS path (e.g., "/littlefs/dproject.bin")
     */
    std::string ConvertPathInternal(const char* virtualPath);

public:
    ESP32FileSystem();
    virtual ~ESP32FileSystem();

    bool Initialize() override;
    void Shutdown() override;
    FileHandle OpenFile(const char* path, OpenMode mode) override;
    void CloseFile(FileHandle handle) override;
    size_t ReadFile(FileHandle handle, void* buffer, size_t size) override;
    size_t WriteFile(FileHandle handle, const void* buffer, size_t size) override;
    long SeekFile(FileHandle handle, long offset, SeekOrigin origin) override;
    long TellFile(FileHandle handle) override;
    long GetFileSize(FileHandle handle) override;
    bool FileExists(const char* path) override;
    bool ConvertPath(const char* virtualPath, char* outBuffer, size_t bufferSize) override;
};

}  // namespace Deki
