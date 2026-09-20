#include <deki/ProjectSettings.h>
#include <deki/Engine.h>
#include <deki/Component.h>
#include <deki/LogSystem.h>
#include <deki/reflection/SettingsRegistry.h>
#include <deki/reflection/Property.h>
#include <deki/settings/UnitProjectSettings.h>
#include <deki/providers/FileSystem.h>
#include <deki/providers/IFileSystem.h>
#include <deki/providers/Buffer.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace Deki
{


namespace ProjectSettings
{

// ============================================================================
// Package-settings binary chunk (version 5+)
// ============================================================================
//
// Format appended after the v4 pipeline-config block:
//
//   packageSettingsCount   : uint16_t
//   For each section:
//       sectionNameLen    : uint16_t
//       sectionName       : char[sectionNameLen]
//       payloadSize       : uint32_t      (skip-on-unknown allows forward compat)
//       propertyCount     : uint16_t
//       For each property:
//           keyHash       : uint32_t      (FNV-1a of property name)
//           typeTag       : uint8_t       (numeric PropertyType)
//           valueBytes    : type-dependent
//
// Type-dependent value layout:
//   Bool / Int8 / UInt8                 : 1 byte
//   Int16 / UInt16                      : 2 bytes
//   Int32 / UInt32 / Float              : 4 bytes
//   String                              : uint16_t length + bytes (no null terminator)
//   Enum                                : 1, 2, or 4 bytes per the type's enumSize
//
// Unsupported property types (Color, Vector2, Vector3, Asset, AssetRef,
// ObjectRef, Array) are silently skipped on Write; Load skips unknown
// type tags by walking the per-property bounds.

namespace
{

#ifndef DEKI_EDITOR
// Runtime-only. The generic packageSettings hydration in Load() matches
// properties against settings-registry metadata, but that metadata
// (GetComponentMeta()) is compiled #ifdef DEKI_EDITOR only, so on runtime
// builds it is absent and the loop skips every section. Scan a section payload
// [start,end) for a Float property with the given key hash. The v5 chunk
// stores no per-property length and writes enum values at their native width
// (sometimes 1 byte), so a meta-less sequential walk can desync; matching on
// the (keyHash, Float tag) pair is order- and enum-size-independent.
bool ReadFloatSettingByHash(const uint8_t* data, size_t start, size_t end,
                            uint32_t keyHash, float& out)
{
    for (size_t p = start; p + 9 <= end; ++p)
    {  // 4 (hash) + 1 (tag) + 4 (float)
        uint32_t h;
        memcpy(&h, data + p, sizeof(uint32_t));
        if (h != keyHash) continue;
        if (data[p + 4] != static_cast<uint8_t>(PropertyType::Float)) continue;
        memcpy(&out, data + p + 5, sizeof(float));
        return true;
    }
    return false;
}
#endif

// Number of payload bytes for a property's value (excluding the keyHash + typeTag header).
size_t SettingsValueSize(const PropertyInfo& prop)
{
    switch (prop.type)
    {
        case PropertyType::Bool:
        case PropertyType::Int8:
        case PropertyType::UInt8:
            return 1;
        case PropertyType::Int16:
        case PropertyType::UInt16:
            return 2;
        case PropertyType::Int32:
        case PropertyType::UInt32:
        case PropertyType::Float:
            return 4;
        case PropertyType::Enum:
            return (prop.enumSize == 1 || prop.enumSize == 2) ? prop.enumSize : 4;
        case PropertyType::String:
        {
            // Variable-length: caller handles string bytes.
            return 2;  // length prefix only; payload appended separately
        }
        default:
            return 0;
    }
}

#ifdef DEKI_EDITOR
// Append raw bytes to the output buffer. Write() used to take a caller-owned
// uint8_t* with no capacity argument while emitting a variable-length v5 chunk
// that grows with the settings registry - every call site guessed a stack array
// size (4096) smaller than what LoadFromFile would accept back (16384). Growing
// a vector removes that whole class of mismatch.
void Append(std::vector<uint8_t>& buf, const void* src, size_t n)
{
    const uint8_t* p = static_cast<const uint8_t*>(src);
    buf.insert(buf.end(), p, p + n);
}
#endif

bool IsSupportedSettingsType(PropertyType t)
{
    switch (t)
    {
        case PropertyType::Bool:
        case PropertyType::Int8:
        case PropertyType::Int16:
        case PropertyType::Int32:
        case PropertyType::UInt8:
        case PropertyType::UInt16:
        case PropertyType::UInt32:
        case PropertyType::Float:
        case PropertyType::Enum:
        case PropertyType::String:
            return true;
        default:
            return false;
    }
}

}  // namespace

static int32_t s_TargetFPS = 0;
static std::string s_StartupScene;

// The v5 packageSettings chunk of the last Load(), kept so a package can ask
// for its own settings on device (ReadPackageSetting*), where the settings
// registry that hydrates instances in the editor does not exist. One heap
// block, reused across loads.
//
// Through Deki::Memory like everything else that runs on a board: this was a
// bare realloc, which skipped the placement decision, the accounting and the
// double-free guard. External, because it is read only when a package asks
// for a setting and the internal heap is the scarce one.
static Deki::Buffer<uint8_t> s_PackageChunk;
static size_t s_PackageChunkSize = 0;

static void RetainPackageChunk(const uint8_t* data, size_t size)
{
    // Grow-only: Count() is the capacity, so a smaller settings chunk on the
    // next Load() reuses the block rather than reallocating it.
    if (size > s_PackageChunk.Count())
    {
        // Allocate releases the old block before taking the new one, which
        // is right here (the memcpy below overwrites it anyway) but means a
        // failed grow loses the previously retained chunk. Size 0 already
        // marked it unreadable in that case, so the effect is the same:
        // ReadPackageSetting* reports the setting as absent and the package
        // takes its default.
        if (!s_PackageChunk.Allocate(size, Deki::Memory::External))
        {
            s_PackageChunkSize = 0;
            return;
        }
    }
    memcpy(s_PackageChunk.Data(), data, size);
    s_PackageChunkSize = size;
}

// Find `key` in `section` of the retained chunk and copy its value when the
// stored type tag matches. The v5 layout: u16 sectionCount, then per section
// u16 nameLen, name, u32 payloadSize, payload (u16 propCount, then per
// property u32 keyHash, u8 typeTag, value at its native width). Like the
// pixelsPerMeter special case, the payload is scanned for the (hash, tag)
// pair rather than walked, so an enum stored at 1 byte cannot desync it.
static bool FindPackageSetting(const char* section, const char* key, uint8_t typeTag, size_t valueSize, void* out)
{
    if (!section || !key || !out || !s_PackageChunk || s_PackageChunkSize < sizeof(uint16_t))
        return false;
    const uint8_t* d = s_PackageChunk.Data();
    const size_t n = s_PackageChunkSize;
    const size_t sectionLen = strlen(section);
    const uint32_t keyHash = HashStringLen(key, static_cast<uint16_t>(strlen(key)));

    uint16_t count = 0;
    memcpy(&count, d, sizeof(uint16_t));
    size_t off = sizeof(uint16_t);
    for (uint16_t i = 0; i < count; ++i)
    {
        if (off + sizeof(uint16_t) > n) return false;
        uint16_t nameLen = 0;
        memcpy(&nameLen, d + off, sizeof(uint16_t));
        off += sizeof(uint16_t);
        if (off + nameLen + sizeof(uint32_t) > n) return false;
        const char* name = reinterpret_cast<const char*>(d + off);
        off += nameLen;
        uint32_t payload = 0;
        memcpy(&payload, d + off, sizeof(uint32_t));
        off += sizeof(uint32_t);
        if (off + payload > n) return false;

        if (nameLen == sectionLen && memcmp(name, section, sectionLen) == 0)
        {
            const size_t end = off + payload;
            for (size_t p = off; p + 5 + valueSize <= end; ++p)
            {
                uint32_t h = 0;
                memcpy(&h, d + p, sizeof(uint32_t));
                if (h != keyHash || d[p + 4] != typeTag) continue;
                memcpy(out, d + p + 5, valueSize);
                return true;
            }
            return false;
        }
        off += payload;
    }
    return false;
}

bool ReadPackageSettingBool(const char* section, const char* key, bool& out)
{
    uint8_t v = 0;
    if (!FindPackageSetting(section, key, static_cast<uint8_t>(PropertyType::Bool), 1, &v)) return false;
    out = (v != 0);
    return true;
}

bool ReadPackageSettingInt32(const char* section, const char* key, int32_t& out)
{
    return FindPackageSetting(section, key, static_cast<uint8_t>(PropertyType::Int32), sizeof(int32_t), &out);
}

bool ReadPackageSettingFloat(const char* section, const char* key, float& out)
{
    return FindPackageSetting(section, key, static_cast<uint8_t>(PropertyType::Float), sizeof(float), &out);
}
static std::string s_RendererName = "standard2d";
static int s_PassCount = 0;

// Sized to what a project actually declares. A fixed array here used to cost a
// single-pass 2D project seven unused strings and cap a richer target at
// eight, which is the low end and the high end paying for each other.
static std::vector<std::string> s_PassNames;

int32_t GetTargetFPS()
{
    return s_TargetFPS;
}
const char* GetStartupScene()
{
    return s_StartupScene.c_str();
}
const char* GetRenderPipeline()
{
    return s_RendererName.c_str();
}
int GetPassCount()
{
    return s_PassCount;
}
const char* GetPassName(int index)
{
    if (index < 0 || index >= static_cast<int>(s_PassNames.size()))
        return "";
    return s_PassNames[index].c_str();
}

bool Load(const uint8_t* data, size_t size)
{
    // Minimum size: header (8 bytes) + base display settings
    // Version 1: 12 bytes (without dirty rect fields)
    // Version 2: 16 bytes (with dirty rect fields - now deprecated)
    constexpr size_t V1_DISPLAY_SIZE = 12;
    // Each rejection names its reason: the caller reports every `false` as
    // "no project settings found", which hid a corrupt file behind the same
    // message as a missing one.
    if (!data || size < sizeof(FileHeader) + V1_DISPLAY_SIZE)
    {
        DEKI_LOG_ERROR("ProjectSettings::Load: file too small (%zu bytes, need at least %zu)",
                       size, sizeof(FileHeader) + V1_DISPLAY_SIZE);
        return false;
    }

    // Read header
    FileHeader header;
    memcpy(&header, data, sizeof(FileHeader));

    // Validate magic
    if (header.magic != MAGIC)
    {
        DEKI_LOG_ERROR("ProjectSettings::Load: bad magic 0x%08X (expected 0x%08X) - not a settings file",
                       (unsigned)header.magic, (unsigned)MAGIC);
        return false;
    }

    // Check version
    if (header.version > VERSION)
    {
        DEKI_LOG_ERROR("ProjectSettings::Load: file version %u is newer than this engine supports (%u)",
                       (unsigned)header.version, (unsigned)VERSION);
        return false;  // Future version, can't read
    }

    // Reset runtime settings
    s_TargetFPS = 0;
    s_StartupScene.clear();
    s_RendererName = "standard2d";
    s_PassCount = 0;
    s_PassNames.clear();
    s_PackageChunkSize = 0;

    // Version 3+: read runtime settings (targetFPS + startup scene)
    if (header.version >= 3)
    {
        size_t offset = sizeof(FileHeader) + sizeof(DisplaySettings);

        // Read targetFPS
        if (offset + sizeof(int32_t) <= size)
        {
            memcpy(&s_TargetFPS, data + offset, sizeof(int32_t));
            offset += sizeof(int32_t);
        }

        // Read scene path length + string
        if (offset + sizeof(uint16_t) <= size)
        {
            uint16_t sceneLen = 0;
            memcpy(&sceneLen, data + offset, sizeof(uint16_t));
            offset += sizeof(uint16_t);

            if (sceneLen > 0 && offset + sceneLen <= size)
            {
                s_StartupScene.assign(reinterpret_cast<const char*>(data + offset), sceneLen);
                offset += sceneLen;
            }
        }
    }

    // Version 4+: read render pipeline config
    if (header.version >= 4)
    {
        size_t offset = sizeof(FileHeader) + sizeof(DisplaySettings) + sizeof(int32_t);

        // Skip past scene path (already read above)
        if (offset + sizeof(uint16_t) <= size)
        {
            uint16_t sceneLen = 0;
            memcpy(&sceneLen, data + offset, sizeof(uint16_t));
            offset += sizeof(uint16_t) + sceneLen;
        }

        // Read renderer name length + string
        if (offset + sizeof(uint16_t) <= size)
        {
            uint16_t nameLen = 0;
            memcpy(&nameLen, data + offset, sizeof(uint16_t));
            offset += sizeof(uint16_t);

            if (nameLen > 0 && offset + nameLen <= size)
            {
                s_RendererName.assign(reinterpret_cast<const char*>(data + offset), nameLen);
                offset += nameLen;
            }
        }

        // Read pass count
        if (offset + sizeof(uint8_t) <= size)
        {
            uint8_t passCount = 0;
            memcpy(&passCount, data + offset, sizeof(uint8_t));
            offset += sizeof(uint8_t);

            s_PassCount = passCount;
            s_PassNames.assign(static_cast<size_t>(passCount), std::string());

            // Read each pass name (uint16_t length + string)
            for (int i = 0; i < s_PassCount; i++)
            {
                if (offset + sizeof(uint16_t) > size)
                {
                    // Truncated file: keep only what actually parsed, so a
                    // caller iterating GetPassCount() cannot read a name that
                    // was never there.
                    s_PassNames.resize(static_cast<size_t>(i));
                    s_PassCount = i;
                    break;
                }

                uint16_t passNameLen = 0;
                memcpy(&passNameLen, data + offset, sizeof(uint16_t));
                offset += sizeof(uint16_t);

                if (passNameLen > 0 && offset + passNameLen <= size)
                {
                    s_PassNames[i].assign(reinterpret_cast<const char*>(data + offset), passNameLen);
                    offset += passNameLen;
                }
            }

            // Version 5+: packageSettings chunk lives after the pass-name list.
            // Hydrates registered SettingsRegistry instances directly.
            if (header.version >= 5 && offset + sizeof(uint16_t) <= size)
            {
                RetainPackageChunk(data + offset, size - offset);

                uint16_t packageCount = 0;
                memcpy(&packageCount, data + offset, sizeof(uint16_t));
                offset += sizeof(uint16_t);

                for (uint16_t mi = 0; mi < packageCount; ++mi)
                {
                    // Truncation inside the package block: stop reading, but
                    // say which section was cut short rather than silently
                    // leaving the remaining sections at their defaults.
                    if (offset + sizeof(uint16_t) > size)
                    {
                        DEKI_LOG_WARNING("ProjectSettings::Load: truncated before package section %u of %u",
                                         (unsigned)mi, (unsigned)packageCount);
                        break;
                    }
                    uint16_t sectionNameLen = 0;
                    memcpy(&sectionNameLen, data + offset, sizeof(uint16_t));
                    offset += sizeof(uint16_t);

                    if (offset + sectionNameLen + sizeof(uint32_t) > size)
                    {
                        DEKI_LOG_WARNING("ProjectSettings::Load: truncated in the name of package section %u of %u",
                                         (unsigned)mi, (unsigned)packageCount);
                        break;
                    }
                    std::string sectionName(reinterpret_cast<const char*>(data + offset), sectionNameLen);
                    offset += sectionNameLen;

                    uint32_t payloadSize = 0;
                    memcpy(&payloadSize, data + offset, sizeof(uint32_t));
                    offset += sizeof(uint32_t);

                    size_t sectionStart = offset;
                    if (sectionStart + payloadSize > size)
                    {
                        DEKI_LOG_WARNING("ProjectSettings::Load: package section '%s' is truncated (%u bytes declared)",
                                         sectionName.c_str(), (unsigned)payloadSize);
                        break;  // truncated; bail
                    }
                    size_t sectionEnd = sectionStart + payloadSize;

#ifndef DEKI_EDITOR
                    // The editor pushes project unit settings into
                    // EngineSettings::Global() on project open
                    // (UnitProjectSettings::ApplyToGlobalCache). The runtime has
                    // no settings-registry metadata, so the generic hydration
                    // below is a no-op; apply the runtime-relevant unit settings
                    // (camera world scale) directly. Without this, cameras using
                    // the inherit sentinel (pixelsPerMeter == 0) fall back to the
                    // engine-default ppm and the scene renders at the wrong scale.
                    if (sectionName == "Units")  // UnitProjectSettings' DEKI_SETTINGS_SECTION
                    {
                        float ppm = 0.0f;
                        if (ReadFloatSettingByHash(data, sectionStart, sectionEnd,
                                                   HashStringLen("pixelsPerMeter", 14), ppm) &&
                            ppm > 0.0f)
                        {
                            EngineSettings::Global().pixelsPerMeter = ppm;
                            DEKI_LOG_INTERNAL("ProjectSettings: applied pixelsPerMeter=%.3f to global cache", (double)ppm);
                        }
                    }
#endif

                    Component* inst = SettingsRegistry::Instance().GetByName(sectionName.c_str());
                    if (!inst)
                    {
                        offset = sectionEnd;  // unknown section: skip whole payload
                        continue;
                    }

                    // Look up metadata directly via the registry's stored info
                    // (avoids ComponentRegistry round-trip and Just Works on
                    // embedded where ComponentRegistry has linear lookups).
                    const ComponentMeta* meta = nullptr;
                    for (const auto& info : SettingsRegistry::Instance().GetAll())
                    {
                        if (info.instance == inst)
                        {
                            meta = info.meta;
                            break;
                        }
                    }
                    if (!meta)
                    {
                        offset = sectionEnd;
                        continue;
                    }

                    if (offset + sizeof(uint16_t) > sectionEnd)
                    {
                        offset = sectionEnd;
                        continue;
                    }
                    uint16_t propCount = 0;
                    memcpy(&propCount, data + offset, sizeof(uint16_t));
                    offset += sizeof(uint16_t);

                    for (uint16_t pi = 0; pi < propCount && offset < sectionEnd; ++pi)
                    {
                        if (offset + sizeof(uint32_t) + 1 > sectionEnd) break;
                        uint32_t keyHash = 0;
                        memcpy(&keyHash, data + offset, sizeof(uint32_t));
                        offset += sizeof(uint32_t);
                        uint8_t typeTag = data[offset++];

                        // Find the property in the metadata by name hash.
                        const PropertyInfo* matched = nullptr;
                        for (int q = 0; q < meta->propertyCount; ++q)
                        {
                            const PropertyInfo& p = meta->properties[q];
                            if (p.name && HashStringLen(p.name, (uint16_t)strlen(p.name)) == keyHash)
                            {
                                matched = &p;
                                break;
                            }
                        }

                        // Read the value bytes regardless of whether we matched
                        // (so we keep parsing the rest of the payload).
                        PropertyType propType = (PropertyType)typeTag;
                        switch (propType)
                        {
                            case PropertyType::Bool:
                            {
                                if (offset + 1 > sectionEnd)
                                {
                                    offset = sectionEnd;
                                    break;
                                }
                                uint8_t v = data[offset++];
                                if (matched && matched->type == propType)
                                    SetPropertyValue<bool>(inst, *matched, v != 0);
                                break;
                            }
                            case PropertyType::Int8:
                            case PropertyType::UInt8:
                            {
                                if (offset + 1 > sectionEnd)
                                {
                                    offset = sectionEnd;
                                    break;
                                }
                                uint8_t v = data[offset++];
                                if (matched && matched->type == propType)
                                {
                                    if (propType == PropertyType::Int8)
                                        SetPropertyValue<int8_t>(inst, *matched, (int8_t)v);
                                    else
                                        SetPropertyValue<uint8_t>(inst, *matched, v);
                                }
                                break;
                            }
                            case PropertyType::Int16:
                            case PropertyType::UInt16:
                            {
                                if (offset + 2 > sectionEnd)
                                {
                                    offset = sectionEnd;
                                    break;
                                }
                                uint16_t v = 0;
                                memcpy(&v, data + offset, 2);
                                offset += 2;
                                if (matched && matched->type == propType)
                                {
                                    if (propType == PropertyType::Int16)
                                        SetPropertyValue<int16_t>(inst, *matched, (int16_t)v);
                                    else
                                        SetPropertyValue<uint16_t>(inst, *matched, v);
                                }
                                break;
                            }
                            case PropertyType::Int32:
                            case PropertyType::UInt32:
                            case PropertyType::Float:
                            {
                                if (offset + 4 > sectionEnd)
                                {
                                    offset = sectionEnd;
                                    break;
                                }
                                uint32_t raw = 0;
                                memcpy(&raw, data + offset, 4);
                                offset += 4;
                                if (matched && matched->type == propType)
                                {
                                    if (propType == PropertyType::Int32)
                                        SetPropertyValue<int32_t>(inst, *matched, (int32_t)raw);
                                    else if (propType == PropertyType::UInt32)
                                        SetPropertyValue<uint32_t>(inst, *matched, raw);
                                    else
                                    {
                                        float f;
                                        memcpy(&f, &raw, 4);
                                        SetPropertyValue<float>(inst, *matched, f);
                                    }
                                }
                                break;
                            }
                            case PropertyType::Enum:
                            {
                                // Read the value sized by the on-disk header - but
                                // we don't have an enumSize byte in v5; fall back
                                // to the matched property's enumSize, else 4.
                                size_t valSize = matched ? SettingsValueSize(*matched) : 4;
                                if (offset + valSize > sectionEnd)
                                {
                                    offset = sectionEnd;
                                    break;
                                }
                                uint32_t raw = 0;
                                memcpy(&raw, data + offset, valSize);
                                offset += valSize;
                                if (matched && matched->type == propType)
                                {
                                    switch (matched->enumSize)
                                    {
                                        case 1: SetPropertyValue<uint8_t>(inst, *matched, (uint8_t)raw); break;
                                        case 2: SetPropertyValue<uint16_t>(inst, *matched, (uint16_t)raw); break;
                                        default: SetPropertyValue<int32_t>(inst, *matched, (int32_t)raw); break;
                                    }
                                }
                                break;
                            }
                            case PropertyType::String:
                            {
                                if (offset + 2 > sectionEnd)
                                {
                                    offset = sectionEnd;
                                    break;
                                }
                                uint16_t slen = 0;
                                memcpy(&slen, data + offset, 2);
                                offset += 2;
                                if (offset + slen > sectionEnd)
                                {
                                    offset = sectionEnd;
                                    break;
                                }
                                if (matched && matched->type == propType)
                                {
                                    std::string s((const char*)(data + offset), slen);
                                    SetPropertyValue<std::string>(inst, *matched, s);
                                }
                                offset += slen;
                                break;
                            }
                            default:
                                // Unknown / unsupported tag - bail on this section
                                // since we can't compute the value size.
                                offset = sectionEnd;
                                break;
                        }
                    }
                    offset = sectionEnd;  // ensure we land on the section boundary
                }
            }
        }
    }

    return true;
}

bool LoadFromFile(const char* path)
{
    const char* filePath = path ? path : FILENAME;

    // Use FileSystem if available
    if (!FileSystem::IsInitialized())
        return false;

    IFileSystem* fs = FileSystem::GetCurrentFileSystem();
    if (!fs)
        return false;

    // Open file
    IFileSystem::FileHandle handle = fs->OpenFile(filePath, IFileSystem::OpenMode::READ_BINARY);
    if (!handle)
        return false;

    // Get file size. MAX_FILE_SIZE is the one bound; the buffer below is sized
    // from fileSize itself, so the check and the allocation cannot disagree.
    long fileSize = fs->GetFileSize(handle);
    if (fileSize <= 0 || static_cast<size_t>(fileSize) > MAX_FILE_SIZE)
    {
        fs->CloseFile(handle);
        return false;
    }

    // Heap, not stack: this runs on an ESP32-S3 task where a multi-kilobyte
    // frame is a meaningful fraction of the stack.
    std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
    size_t bytesRead = fs->ReadFile(handle, buffer.data(), buffer.size());
    fs->CloseFile(handle);

    if (bytesRead != buffer.size())
        return false;

    return Load(buffer.data(), bytesRead);
}

#ifdef DEKI_EDITOR
std::vector<uint8_t> Write(int32_t displayWidth, int32_t displayHeight, uint8_t colorFormat,
                           int32_t targetFPS, const char* startupScene,
                           const char* rendererName, const char* const* passNames, int passCount)
{
    std::vector<uint8_t> buf;

    // Write header
    FileHeader header;
    header.magic = MAGIC;
    header.version = VERSION;
    header.flags = 0;
    Append(buf, &header, sizeof(FileHeader));

    // Write display settings
    DisplaySettings display;
    display.targetWidth = displayWidth;
    display.targetHeight = displayHeight;
    display.colorFormat = colorFormat;
    display.reserved1 = 0;
    display.reserved2 = 0;
    display.reserved3 = 0;
    display.reserved4 = 0;
    Append(buf, &display, sizeof(DisplaySettings));

    // Write runtime settings (version 3)
    Append(buf, &targetFPS, sizeof(int32_t));

    uint16_t sceneLen = 0;
    if (startupScene)
        sceneLen = static_cast<uint16_t>(strlen(startupScene));
    Append(buf, &sceneLen, sizeof(uint16_t));

    if (sceneLen > 0)
    {
        Append(buf, startupScene, sceneLen);
    }

    // Version 4: write render pipeline config
    // Renderer name (uint16_t length + string)
    const char* rName = rendererName ? rendererName : "standard2d";
    uint16_t rNameLen = static_cast<uint16_t>(strlen(rName));
    Append(buf, &rNameLen, sizeof(uint16_t));
    if (rNameLen > 0)
    {
        Append(buf, rName, rNameLen);
    }

    // Pass count. The field is one byte, which is the only ceiling left, and
    // it is far beyond anything a real pipeline declares.
    if (passCount < 0)
        passCount = 0;
    if (passCount > 255)
    {
        // The caller writes whatever comes back without checking, so failing
        // here would produce an empty settings file. Say so loudly and keep
        // going instead.
        DEKI_LOG_ERROR("Render pipeline declares %d passes; the format stores at most 255. "
                       "Writing the first 255.", passCount);
        passCount = 255;
    }
    uint8_t pc = static_cast<uint8_t>(passCount);
    Append(buf, &pc, sizeof(uint8_t));

    // Each pass name (uint16_t length + string)
    for (int i = 0; i < pc; i++)
    {
        const char* pName = (passNames && passNames[i]) ? passNames[i] : "";
        uint16_t pNameLen = static_cast<uint16_t>(strlen(pName));
        Append(buf, &pNameLen, sizeof(uint16_t));
        if (pNameLen > 0)
        {
            Append(buf, pName, pNameLen);
        }
    }

    // Version 5: packageSettings chunk. Walk the registry; emit each section
    // with a payloadSize prefix so the runtime can skip unknown sections.
    const auto& sections = SettingsRegistry::Instance().GetAll();
    uint16_t packageCount = (uint16_t)sections.size();
    Append(buf, &packageCount, sizeof(uint16_t));

    for (const auto& info : sections)
    {
        if (!info.instance || !info.meta || !info.sectionName) continue;

        // sectionNameLen + sectionName
        uint16_t snameLen = (uint16_t)strlen(info.sectionName);
        Append(buf, &snameLen, sizeof(uint16_t));
        Append(buf, info.sectionName, snameLen);

        // payloadSize placeholder - we patch this once we know the size.
        const size_t payloadSizeOffset = buf.size();
        uint32_t placeholder = 0;
        Append(buf, &placeholder, sizeof(uint32_t));

        const size_t payloadStart = buf.size();

        // Count supported properties first so we can write propertyCount up front.
        uint16_t propCount = 0;
        for (int i = 0; i < info.meta->propertyCount; ++i)
            if (IsSupportedSettingsType(info.meta->properties[i].type))
                ++propCount;
        Append(buf, &propCount, sizeof(uint16_t));

        for (int i = 0; i < info.meta->propertyCount; ++i)
        {
            const PropertyInfo& prop = info.meta->properties[i];
            if (!IsSupportedSettingsType(prop.type)) continue;

            uint32_t keyHash = HashStringLen(prop.name, (uint16_t)strlen(prop.name));
            Append(buf, &keyHash, sizeof(uint32_t));
            buf.push_back((uint8_t)prop.type);

            switch (prop.type)
            {
                case PropertyType::Bool:
                {
                    buf.push_back(GetPropertyValue<bool>(info.instance, prop) ? 1 : 0);
                    break;
                }
                case PropertyType::Int8: buf.push_back((uint8_t)GetPropertyValue<int8_t>(info.instance, prop)); break;
                case PropertyType::UInt8: buf.push_back(GetPropertyValue<uint8_t>(info.instance, prop)); break;
                case PropertyType::Int16:
                {
                    int16_t v = GetPropertyValue<int16_t>(info.instance, prop);
                    Append(buf, &v, 2);
                    break;
                }
                case PropertyType::UInt16:
                {
                    uint16_t v = GetPropertyValue<uint16_t>(info.instance, prop);
                    Append(buf, &v, 2);
                    break;
                }
                case PropertyType::Int32:
                {
                    int32_t v = GetPropertyValue<int32_t>(info.instance, prop);
                    Append(buf, &v, 4);
                    break;
                }
                case PropertyType::UInt32:
                {
                    uint32_t v = GetPropertyValue<uint32_t>(info.instance, prop);
                    Append(buf, &v, 4);
                    break;
                }
                case PropertyType::Float:
                {
                    float v = GetPropertyValue<float>(info.instance, prop);
                    Append(buf, &v, 4);
                    break;
                }
                case PropertyType::Enum:
                {
                    switch (prop.enumSize)
                    {
                        case 1:
                        {
                            uint8_t v = GetPropertyValue<uint8_t>(info.instance, prop);
                            buf.push_back(v);
                            break;
                        }
                        case 2:
                        {
                            uint16_t v = GetPropertyValue<uint16_t>(info.instance, prop);
                            Append(buf, &v, 2);
                            break;
                        }
                        default:
                        {
                            int32_t v = GetPropertyValue<int32_t>(info.instance, prop);
                            Append(buf, &v, 4);
                            break;
                        }
                    }
                    break;
                }
                case PropertyType::String:
                {
                    const std::string& s = GetPropertyValue<std::string>(info.instance, prop);
                    uint16_t slen = (uint16_t)s.size();
                    Append(buf, &slen, 2);
                    if (slen > 0)
                    {
                        Append(buf, s.data(), slen);
                    }
                    break;
                }
                default:
                    break;  // shouldn't reach (filtered by IsSupportedSettingsType)
            }
        }

        // Patch in the payload size now that we know it.
        uint32_t payloadSize = (uint32_t)(buf.size() - payloadStart);
        memcpy(buf.data() + payloadSizeOffset, &payloadSize, sizeof(uint32_t));
    }

    return buf;
}
#endif

}  // namespace ProjectSettings

}  // namespace Deki
