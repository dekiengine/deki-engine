#pragma once
#include <cstdint>
#include <cstddef>
#ifdef DEKI_EDITOR
#include <vector>  // editor-only: the runtime half of this header stays allocation-free
#endif

/**
 * @brief Runtime project settings loaded from dproject.bin
 *
 * This file is exported by the editor alongside scenes and contains
 * project-wide settings that affect engine behavior at runtime.
 */

namespace Deki
{


namespace ProjectSettings
{

// File format constants
constexpr const char* FILENAME = "F:/dproject.bin";      // Runtime virtual path (ESP32: F:/ -> /sdcard/)
constexpr const char* EXPORT_FILENAME = "dproject.bin";  // Plain filename for editor export
constexpr uint32_t MAGIC = 0x444B5052;                   // "DKPR" in little-endian
constexpr uint16_t VERSION = 5;

// Largest dproject.bin LoadFromFile will accept. The v5 packageSettings chunk
// grows with every installed package, so this is a real bound, not a formality
// - it must be the ONLY one. It was previously two numbers (a 16384 size check
// guarding an 8192 stack buffer), which meant any file in between overflowed
// the stack by up to 8 KB while passing the check.
constexpr size_t MAX_FILE_SIZE = 16384;

/**
 * @brief Binary file header for project settings
 */
struct FileHeader
{
    uint32_t magic;    // MAGIC constant
    uint16_t version;  // Format version
    uint16_t flags;    // Reserved for future use
};

/**
 * @brief Display/rendering settings
 */
struct DisplaySettings
{
    int32_t targetWidth;
    int32_t targetHeight;
    uint8_t colorFormat;  // 0=RGB565, 1=RGB888, 2=ARGB8888
    uint8_t reserved1;    // Was pixelPerfect, kept for binary compat
    uint8_t reserved2;    // Was dirtyRectMaxCount, kept for binary compat
    uint8_t reserved3;    // Was dirtyRectEnabled, kept for binary compat
    float reserved4;      // Was dirtyRectOverlapThreshold, kept for binary compat
};

/**
 * @brief Runtime settings (version 3+)
 */
struct RuntimeSettings
{
    int32_t targetFPS;      // Target FPS (0 = no limit)
    uint16_t scenePathLen;  // Length of startup scene path string
    // Followed by scenePathLen bytes of scene path (not null-terminated in file)
};

/**
 * @brief Render pipeline configuration (version 4+)
 *
 * Baked from the .rpipeline asset at export time: which renderer to activate
 * and which passes to run, read back through GetRenderPipeline(),
 * GetPassCount() and GetPassName(). Both the renderer name and each pass name
 * are stored as a length followed by bytes, so neither the count nor the
 * length of a name is capped by this format. The one ceiling is the pass
 * count's own width, one byte.
 */

/**
 * @brief Load project settings from binary file
 * @param data Pointer to file data
 * @param size Size of file data
 * @return true if loaded successfully
 */
bool Load(const uint8_t* data, size_t size);

/**
 * @brief Get the target FPS from loaded project settings
 * @return Target FPS (0 = no limit)
 */
int32_t GetTargetFPS();

/**
 * @brief Get the startup scene path from loaded project settings
 * @return Startup scene path (empty if not set)
 */
const char* GetStartupScene();

/**
 * @brief Get the render pipeline renderer name (version 4+)
 * @return Renderer name (e.g., "standard2d"), or "standard2d" if not set
 */
const char* GetRenderPipeline();

/**
 * @brief Get the number of render passes in the pipeline (version 4+)
 * @return Pass count (0 if not set)
 */
int GetPassCount();

/**
 * @brief Get the name of a render pass by index (version 4+)
 * @param index Pass index (0-based)
 * @return Pass name (e.g., "clip2d"), or empty string if out of range
 */
const char* GetPassName(int index);

/**
 * @brief Load project settings from filesystem
 * @param path Path to dproject.bin file (optional, uses FILENAME if nullptr)
 * @return true if loaded successfully
 */
bool LoadFromFile(const char* path = nullptr);

/**
 * @brief Runtime lookup of one package setting from the last Load().
 *
 * `section` is the DEKI_PROJECT_SETTINGS_SECTION name (e.g. "Rendering"),
 * `key` the reflected field name. Works without the settings registry, which
 * device builds do not have (the generic hydration in Load() needs the
 * registry's metadata); editor builds should read the registry instance.
 * Returns false when no file is loaded, the section or key is absent, or the
 * stored type is not the one asked for.
 */
bool ReadPackageSettingBool(const char* section, const char* key, bool& out);
bool ReadPackageSettingInt32(const char* section, const char* key, int32_t& out);
bool ReadPackageSettingFloat(const char* section, const char* key, float& out);

#ifdef DEKI_EDITOR
/**
 * @brief Serialize project settings to a dproject.bin image (editor only)
 *
 * Returns the bytes rather than filling a caller-supplied pointer. The v5
 * packageSettings chunk grows with the settings registry, so no call site can
 * know the size up front - the previous signature took a bare uint8_t* with no
 * capacity argument and a doc comment claiming 256 bytes would do, while the
 * three call sites each guessed a 4096-byte stack array and LoadFromFile
 * accepted files up to MAX_FILE_SIZE (16384) back.
 *
 * @param displayWidth Target display width
 * @param displayHeight Target display height
 * @param colorFormat Color format (0=RGB565, 1=RGB888, 2=ARGB8888)
 * @param targetFPS Target FPS (0 = no limit)
 * @param startupScene Startup scene path (can be nullptr)
 * @return The serialized image; empty only if nothing could be written
 */
std::vector<uint8_t> Write(int32_t displayWidth, int32_t displayHeight, uint8_t colorFormat,
                           int32_t targetFPS = 0, const char* startupScene = nullptr,
                           const char* rendererName = nullptr,
                           const char* const* passNames = nullptr, int passCount = 0);
#endif

}  // namespace ProjectSettings

}  // namespace Deki
