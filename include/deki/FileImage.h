#pragma once

// A read-only file image: a game's files packed into one block of bytes that
// is used where it lies, such as a flash partition mapped into memory. Finding
// a file is a binary search over its sorted names and reading it is a copy (or
// no copy at all: Find gives a pointer to the bytes), where a filesystem on
// flash walks directory blocks for every open; on an ESP32-S3 that was 6 to
// 13 ms a file, before a byte of it was read.
//
// Layout, little-endian:
//   header   magic "DKFI", version, file count, the image's size in bytes
//   entries  one per file, sorted by name: name offset, name length,
//            data offset, data size (offsets from the image's start)
//   names    the names, back to back, no terminators
//   data     each file's bytes, starting on a 16-byte boundary

#include <deki/Export.h>  // DEKI_ENGINE_API

#include <cstddef>
#include <cstdint>

#ifdef DEKI_EDITOR
#include <string>
#include <vector>
#endif

namespace Deki
{

class DEKI_ENGINE_API FileImage
{
public:
    static constexpr char kMagic[4] = { 'D', 'K', 'F', 'I' };
    static constexpr uint32_t kVersion = 1;
    static constexpr size_t kHeaderSize = 16;
    static constexpr size_t kEntrySize = 16;
    // Where each file's bytes start: enough for any type read straight out
    // of them, and for a DMA from them.
    static constexpr size_t kDataAlignment = 16;

    /// The whole image's size, from its first kHeaderSize bytes: how much of
    /// a partition to map before Open. 0 when they are not an image's header.
    static uint32_t SizeFromHeader(const uint8_t* header, size_t bytes);

    /// Uses the image in `data`, which must stay where it is while files are
    /// found in it. False, and nothing opened, when `data` does not hold a
    /// whole, well-formed image of this version.
    bool Open(const uint8_t* data, size_t size);

    bool IsOpen() const { return m_Data != nullptr; }

    uint32_t GetFileCount() const { return m_Count; }

    /// The file `name` (such as "assets/5a7800eb-..." or "boot.scene", no
    /// mount prefix): its bytes and size. False when the image has no such
    /// file.
    bool Find(const char* name, size_t nameLength, const uint8_t*& data, size_t& size) const;

#ifdef DEKI_EDITOR
    /// One file to pack: its name in the image and its bytes.
    struct File
    {
        std::string name;
        std::vector<uint8_t> bytes;
    };

    /// Packs `files` into an image. False, with the reason in `error`, when
    /// two share a name, a name is empty, or the image would pass 4 GB.
    static bool Write(std::vector<File> files, std::vector<uint8_t>& out, std::string& error);
#endif

private:
    const uint8_t* m_Data = nullptr;
    uint32_t m_Count = 0;
};

}  // namespace Deki
