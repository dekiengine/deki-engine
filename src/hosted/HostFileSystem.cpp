#include <deki/providers/HostFileSystem.h>
#include <deki/LogSystem.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <filesystem>

namespace Deki
{

HostFileSystem::HostFileSystem()
    : m_Initialized(false)
{
}

HostFileSystem::~HostFileSystem()
{
    Shutdown();
}

bool HostFileSystem::Initialize()
{
    if (m_Initialized)
    {
        return true;
    }

    // Paths are relative to the working directory. A hosted game mirrors the
    // device's two storage mounts:
    //   "S:/" (SD card) -> "./storage/"  (exported/packed assets)
    //   "F:/" (flash)   -> "./flash/"    (the game's data, from the build's export)
    //                      "./userdata/" (what the game writes, kept across builds)
    m_BasePath = "./storage/";
    m_FlashPath = "./flash/";
    m_UserDataPath = "./userdata/";
    m_Initialized = true;
    return true;
}

void HostFileSystem::Shutdown()
{
    m_Initialized = false;
}

std::string HostFileSystem::ConvertPathInternal(const char* virtualPath)
{
    if (!virtualPath)
    {
        return "";
    }

    std::string path(virtualPath);
    // "S:/..." -> "./storage/..." (SD card mount)
    if (path.length() >= 3 && path.substr(0, 3) == "S:/")
    {
        path = m_BasePath + path.substr(3);
    }
    // "F:/..." -> "./flash/..." (the game's data), else "./userdata/..."
    else if (path.length() >= 3 && path.substr(0, 3) == "F:/")
    {
        const std::string inFlash = m_FlashPath + path.substr(3);
        std::error_code ec;
        path = std::filesystem::is_regular_file(inFlash, ec) ? inFlash : m_UserDataPath + path.substr(3);
    }
    return path;
}

bool HostFileSystem::IsGameData(const char* virtualPath) const
{
    if (std::strncmp(virtualPath, "F:/", 3) != 0)
    {
        return false;
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(m_FlashPath + (virtualPath + 3), ec);
}

IFileSystem::FileHandle HostFileSystem::OpenFile(const char* path, OpenMode mode)
{
    if (!m_Initialized || !path)
    {
        return nullptr;
    }

    std::string realPath = ConvertPathInternal(path);
    const char* modeStr = "";
    bool isWrite = false;

    switch (mode)
    {
        case OpenMode::ReadBinary: modeStr = "rb"; break;
        case OpenMode::WriteBinary:
            modeStr = "wb";
            isWrite = true;
            break;
        case OpenMode::ReadText: modeStr = "r"; break;
        case OpenMode::WriteText:
            modeStr = "w";
            isWrite = true;
            break;
    }

    if (isWrite && IsGameData(path))
    {
        // As on a board: the game's data is what was flashed, the same every
        // run.
        DEKI_LOG_WARNING("%s is part of the game's data, which is never written; write the game's own files "
                         "under another name",
                         path);
        return nullptr;
    }

    if (isWrite)
    {
        std::error_code ec;
        std::filesystem::path parent = std::filesystem::path(realPath).parent_path();
        if (!parent.empty())
        {
            std::filesystem::create_directories(parent, ec);
        }
    }

    FILE* file = fopen(realPath.c_str(), modeStr);
    return static_cast<FileHandle>(file);
}

void HostFileSystem::CloseFile(FileHandle handle)
{
    if (handle)
    {
        fclose(static_cast<FILE*>(handle));
    }
}

size_t HostFileSystem::ReadFile(FileHandle handle, void* buffer, size_t size)
{
    if (!handle || !buffer)
    {
        return 0;
    }
    return fread(buffer, 1, size, static_cast<FILE*>(handle));
}

size_t HostFileSystem::WriteFile(FileHandle handle, const void* buffer, size_t size)
{
    if (!handle || !buffer)
    {
        return 0;
    }
    return fwrite(buffer, 1, size, static_cast<FILE*>(handle));
}

long HostFileSystem::SeekFile(FileHandle handle, long offset, SeekOrigin origin)
{
    if (!handle)
    {
        return -1;
    }

    int whence = SEEK_SET;
    switch (origin)
    {
        case SeekOrigin::Begin: whence = SEEK_SET; break;
        case SeekOrigin::Current: whence = SEEK_CUR; break;
        case SeekOrigin::End: whence = SEEK_END; break;
    }

    if (fseek(static_cast<FILE*>(handle), offset, whence) == 0)
    {
        return ftell(static_cast<FILE*>(handle));
    }
    return -1;
}

long HostFileSystem::TellFile(FileHandle handle)
{
    if (!handle)
    {
        return -1;
    }
    return ftell(static_cast<FILE*>(handle));
}

long HostFileSystem::GetFileSize(FileHandle handle)
{
    if (!handle)
    {
        return -1;
    }

    FILE* file = static_cast<FILE*>(handle);
    long currentPos = ftell(file);

    if (fseek(file, 0, SEEK_END) != 0)
    {
        return -1;
    }

    long size = ftell(file);

    fseek(file, currentPos, SEEK_SET);

    return size;
}

bool HostFileSystem::FileExists(const char* path)
{
    if (!m_Initialized || !path)
    {
        return false;
    }

    std::string realPath = ConvertPathInternal(path);
    FILE* file = fopen(realPath.c_str(), "rb");

    if (file)
    {
        fclose(file);
        return true;
    }

    return false;
}

bool HostFileSystem::ConvertPath(const char* virtualPath, char* outBuffer, size_t bufferSize)
{
    if (!virtualPath || !outBuffer || bufferSize == 0)
    {
        return false;
    }

    std::string converted = ConvertPathInternal(virtualPath);
    if (converted.length() >= bufferSize)
    {
        return false;
    }

    strcpy(outBuffer, converted.c_str());
    return true;
}

}  // namespace Deki
