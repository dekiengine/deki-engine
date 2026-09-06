#pragma once

#include <deki/providers/IFileSystem.h>
#include <string>

/**
 * @brief Desktop file system implementation using standard C file operations
 *
 * This implementation maps virtual paths to the local filesystem and uses
 * standard C library functions for file operations. It's suitable for
 * desktop platforms (Windows, Linux, macOS) for both editor and simulator builds.
 */

namespace Deki
{

class DesktopFileSystem : public IFileSystem
{
private:
    std::string m_BasePath;   // "S:/" (SD card) -> exported assets
    std::string m_FlashPath;  // "F:/" (flash)   -> boot scene + project settings
    bool m_Initialized;

    /**
     * @brief Convert virtual path to real filesystem path
     * @param virtualPath Virtual path (e.g. "S:/assets/texture.bin", "F:/boot.scene")
     * @return Real filesystem path
     */
    std::string ConvertPathInternal(const char* virtualPath);

public:
    DesktopFileSystem();
    virtual ~DesktopFileSystem();

    // IPlatformFileSystem interface implementation
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
