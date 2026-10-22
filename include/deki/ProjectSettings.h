#pragma once
#include <cstdint>
#include <cstddef>
#include <deki/Storage.h>
#ifdef DEKI_EDITOR
#include <vector>  // editor-only: the runtime half of this header stays allocation-free
#endif

// Project-wide runtime settings, read from project_data.bin, which the editor
// exports next to the scenes.

namespace Deki
{

namespace ProjectSettings
{

constexpr const char* kFilename = "F:/project_data.bin";     // runtime path, on internal storage
constexpr const char* kExportFilename = "project_data.bin";  // file name the editor exports
constexpr uint32_t kMagic = 0x444B5052;                      // "DKPR" in little-endian
constexpr uint16_t kVersion = 7;                             // 7: PropertyType numbers moved

// FileHeader::flags
constexpr uint16_t kFlagAssetsExternal = 1u << 0;  // the assets are on external storage

// Largest project_data.bin LoadFromFile accepts. The packageSettings chunk
// grows with every installed package, so files do get large. This must be the
// only limit: a smaller buffer anywhere else overflows on a file that passes
// this check.
constexpr size_t kMaxFileSize = 16384;

struct FileHeader
{
    uint32_t magic;    // kMagic
    uint16_t version;  // format version
    uint16_t flags;    // kFlag*
};

// The reserved fields are unused and kept only so the file layout stays the
// same. The framebuffer takes the display's own size and format.
struct DisplaySettings
{
    int32_t reserved0;  // formerly targetWidth
    int32_t reserved5;  // formerly targetHeight
    uint8_t reserved6;  // formerly colorFormat
    uint8_t reserved1;  // formerly pixelPerfect
    uint8_t reserved2;  // formerly dirtyRectMaxCount
    uint8_t reserved3;  // formerly dirtyRectEnabled
    // The screen's diagonal in inches, from the board's platform file; 0 when
    // unknown. Files from before it was added wrote 0 here.
    float screenDiagonalInches;
};

// Follows DisplaySettings.
struct RuntimeSettings
{
    int32_t targetFPS;      // 0 = no limit
    uint16_t scenePathLen;  // length of the startup scene path
    // Followed by scenePathLen bytes of scene path, not null-terminated.
};

// Render pipeline: baked from the .rpipeline asset at export,
// the renderer to activate and the passes to run, read back through
// GetRenderPipeline(), GetPassCount() and GetPassName(). Each name is stored
// as a length then bytes, so names have no length cap; the pass count is one
// byte.

/// Reads project settings from the bytes of a project_data.bin. Returns false
/// on failure.
bool Load(const uint8_t* data, size_t size);

/// Target frame rate, 0 for no limit.
int32_t GetTargetFPS();

/// The screen's diagonal in inches, from the board's platform file; 0 when
/// unknown.
float GetScreenDiagonalInches();

/// Whether this build's assets are on internal or external storage.
Storage::Location GetAssetStorage();

/// Startup scene path, empty when not set.
const char* GetStartupScene();

/// Renderer name, such as "standard2d"; "standard2d" when not set.
const char* GetRenderPipeline();

/// Number of render passes, 0 when not set.
int GetPassCount();

/// Name of render pass `index` (such as "clip2d"), or "" when out of range.
const char* GetPassName(int index);

/// Loads project settings from `path`, or from kFilename when it is nullptr.
/// Returns false on failure.
bool LoadFromFile(const char* path = nullptr);

/// Reads one package setting from the last Load(). `section` is the
/// DEKI_PROJECT_SETTINGS_SECTION name (such as "Rendering"), `key` the
/// reflected field name. Works without the settings registry, which device
/// builds lack; editor builds should read the registry instance instead.
/// Returns false when no file is loaded, the section or key is missing, or
/// the stored type is a different one.
bool ReadPackageSettingBool(const char* section, const char* key, bool& out);
bool ReadPackageSettingInt32(const char* section, const char* key, int32_t& out);
bool ReadPackageSettingFloat(const char* section, const char* key, float& out);
/// An enum setting, stored at the enum's own width: pass sizeof the enum (1, 2
/// or 4). `out` is the enumerator's value.
bool ReadPackageSettingEnum(const char* section, const char* key, size_t enumSize, int32_t& out);

#ifdef DEKI_EDITOR
/// Builds a project_data.bin image (editor only). `targetFPS` 0 means no
/// limit; `startupScene` may be nullptr; `screenDiagonalInches` 0 means
/// unknown. Returns the bytes, rather than
/// filling a caller's buffer, because the packageSettings chunk grows with the
/// settings registry and no caller can know the size. Empty only when nothing
/// could be written.
std::vector<uint8_t> Write(Storage::Location assetStorage, int32_t targetFPS = 0, const char* startupScene = nullptr,
                           const char* rendererName = nullptr, const char* const* passNames = nullptr,
                           int passCount = 0, float screenDiagonalInches = 0.0f);
#endif

}  // namespace ProjectSettings

}  // namespace Deki
