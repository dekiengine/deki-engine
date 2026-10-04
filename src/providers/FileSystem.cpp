#include <deki/providers/FileSystem.h>
#include <cstring>
#include <deki/LogSystem.h>

// Static member definitions

namespace Deki
{

std::unique_ptr<IFileSystem> FileSystem::s_CurrentFilesystem = nullptr;
bool FileSystem::s_Initialized = false;
std::vector<FileSystem::PrefixEntry> FileSystem::s_RegisteredFilesystems;
IFileSystem* FileSystem::s_DefaultFilesystem = nullptr;

bool FileSystem::Initialize()
{
    if (s_Initialized)
    {
        return true;
    }

    // Filesystem must be set via SetFileSystem() before Initialize()
    if (!s_CurrentFilesystem)
    {
        DEKI_LOG_ERROR("DekiFileSystem: No filesystem set (call SetFileSystem first)");
        return false;
    }

    RegisterFileSystem("F:/", s_CurrentFilesystem.get());
    SetDefaultFileSystem(s_CurrentFilesystem.get());
    s_Initialized = true;
    return true;
}

void FileSystem::Shutdown()
{
    if (s_Initialized && s_CurrentFilesystem)
    {
        s_CurrentFilesystem->Shutdown();
        s_CurrentFilesystem.reset();
    }
    s_Initialized = false;
}

bool FileSystem::SetFileSystem(IFileSystem* fs)
{
    if (!fs)
    {
        DEKI_LOG_ERROR("DekiFileSystem::SetFileSystem: null filesystem");
        return false;
    }

    // Shutdown existing filesystem if any
    if (s_CurrentFilesystem)
    {
        s_CurrentFilesystem->Shutdown();
        s_CurrentFilesystem.reset();
    }

    // Take ownership and initialize
    s_CurrentFilesystem.reset(fs);
    if (!s_CurrentFilesystem->Initialize())
    {
        DEKI_LOG_ERROR("DekiFileSystem::SetFileSystem: failed to initialize");
        s_CurrentFilesystem.reset();
        return false;
    }

    RegisterFileSystem("F:/", s_CurrentFilesystem.get());
    SetDefaultFileSystem(s_CurrentFilesystem.get());
    s_Initialized = true;
    DEKI_LOG_INTERNAL("DekiFileSystem::SetFileSystem: custom filesystem set");
    return true;
}

IFileSystem* FileSystem::GetCurrentFileSystem()
{
    if (!s_CurrentFilesystem)
    {
        DEKI_LOG_INTERNAL("DekiFileSystem: GetCurrentFileSystem called but no filesystem available");
    }
    return s_CurrentFilesystem.get();
}

bool FileSystem::SupportsStorageMode()
{
    if (!s_CurrentFilesystem)
    {
        return false;
    }
    return s_CurrentFilesystem->SupportsStorageMode();
}

bool FileSystem::SetStorageMode(bool enabled)
{
    if (!s_CurrentFilesystem)
    {
        DEKI_LOG_ERROR("DekiFileSystem::SetStorageMode: no filesystem available");
        return false;
    }
    return s_CurrentFilesystem->SetStorageMode(enabled);
}

bool FileSystem::IsStorageMode()
{
    if (!s_CurrentFilesystem)
    {
        return false;
    }
    return s_CurrentFilesystem->IsStorageMode();
}

// ============================================================================
// Multi-Filesystem Support
// ============================================================================

void FileSystem::RegisterFileSystem(const std::string& prefix, IFileSystem* fs)
{
    if (!fs || prefix.empty())
    {
        DEKI_LOG_WARNING("DekiFileSystem::RegisterFileSystem: invalid prefix or filesystem");
        return;
    }

    for (auto& entry : s_RegisteredFilesystems)
    {
        if (entry.prefix == prefix)
        {
            entry.fs = fs;
            return;
        }
    }
    s_RegisteredFilesystems.push_back({ prefix, fs });
    DEKI_LOG_INTERNAL("DekiFileSystem: Registered filesystem for prefix '%s'", prefix.c_str());
}

void FileSystem::UnregisterFileSystem(const std::string& prefix)
{
    for (auto it = s_RegisteredFilesystems.begin(); it != s_RegisteredFilesystems.end(); ++it)
    {
        if (it->prefix == prefix)
        {
            s_RegisteredFilesystems.erase(it);
            DEKI_LOG_INTERNAL("DekiFileSystem: Unregistered filesystem for prefix '%s'", prefix.c_str());
            return;
        }
    }
}

IFileSystem* FileSystem::GetFileSystemForPath(const char* path)
{
    if (!path)
    {
        return s_DefaultFilesystem;
    }

    // Check for registered prefix match (e.g., "F:/", "S:/") without building
    // any std::string: this runs on every file open.
    for (const auto& entry : s_RegisteredFilesystems)
    {
        if (strncmp(path, entry.prefix.c_str(), entry.prefix.size()) == 0)
        {
            return entry.fs;
        }
    }

    // Non-prefixed paths use default filesystem
    return s_DefaultFilesystem;
}

void FileSystem::SetDefaultFileSystem(IFileSystem* fs)
{
    s_DefaultFilesystem = fs;
    if (fs)
    {
        DEKI_LOG_INTERNAL("DekiFileSystem: Set default filesystem for non-prefixed paths");
    }
    else
    {
        DEKI_LOG_INTERNAL("DekiFileSystem: Cleared default filesystem");
    }
}

}  // namespace Deki
