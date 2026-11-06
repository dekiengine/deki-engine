#pragma once

#include <deki/providers/IFileSystem.h>
#include <string>

/// The file system of a game that runs as a program on an operating system
/// (the desktop build, a Linux handheld), on the C standard library. Maps
/// the device's virtual mounts to folders beside the program, on Windows,
/// Linux and macOS alike. The hosted entry (src/hosted/HostedMain.cpp) sets it
/// before the engine starts.
///
/// F:/ is the board's flash, as on a device: the game's data (the build's
/// export, in ./flash/) is read and never written, and the files the game
/// writes go to ./userdata/, read from there when the game's data has no file
/// of that name.

namespace Deki
{

class HostFileSystem : public IFileSystem
{
private:
    std::string m_BasePath;      // "S:/" (SD card) -> exported assets
    std::string m_FlashPath;     // "F:/" (flash)   -> the game's data: boot scene, settings, assets
    std::string m_UserDataPath;  // "F:/" files the game writes
    bool m_Initialized;

    /// Turns a virtual path ("S:/assets/texture.bin", "F:/boot.scene") into a
    /// real filesystem path. An F:/ path is the game's data when that has the
    /// file, else the user data.
    std::string ConvertPathInternal(const char* virtualPath);

    /// True when `virtualPath` names a file of the game's data (F:/ in
    /// ./flash/), which is never written.
    bool IsGameData(const char* virtualPath) const;

public:
    HostFileSystem();
    virtual ~HostFileSystem();

    // IFileSystem
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
