// The read-only file image: format in FileImage.h.

#include <deki/FileImage.h>

#include <cstring>

#ifdef DEKI_EDITOR
#include <algorithm>
#endif

namespace Deki
{

namespace
{

// Little-endian, from any alignment.
uint32_t ReadU32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

struct EntryView
{
    uint32_t nameOffset;
    uint32_t nameLength;
    uint32_t dataOffset;
    uint32_t dataSize;
};

EntryView ReadEntry(const uint8_t* image, uint32_t index)
{
    const uint8_t* e = image + FileImage::kHeaderSize + static_cast<size_t>(index) * FileImage::kEntrySize;
    return { ReadU32(e), ReadU32(e + 4), ReadU32(e + 8), ReadU32(e + 12) };
}

// Byte order of names, the order the entries are sorted in.
int CompareNames(const uint8_t* a, size_t aLength, const uint8_t* b, size_t bLength)
{
    const int c = std::memcmp(a, b, aLength < bLength ? aLength : bLength);
    if (c != 0)
    {
        return c;
    }
    return aLength < bLength ? -1 : (aLength > bLength ? 1 : 0);
}

}  // namespace

uint32_t FileImage::SizeFromHeader(const uint8_t* header, size_t bytes)
{
    if (!header || bytes < kHeaderSize || std::memcmp(header, kMagic, sizeof(kMagic)) != 0 ||
        ReadU32(header + 4) != kVersion)
    {
        return 0;
    }
    return ReadU32(header + 12);
}

bool FileImage::Open(const uint8_t* data, size_t size)
{
    m_Data = nullptr;
    m_Count = 0;
    const uint32_t imageSize = SizeFromHeader(data, size);
    if (imageSize == 0 || imageSize > size)
    {
        return false;
    }
    const uint32_t count = ReadU32(data + 8);
    if (count > (imageSize - kHeaderSize) / kEntrySize)
    {
        return false;
    }
    // Every name and every file inside the image, and the names in order,
    // so Find can trust what it reads.
    for (uint32_t i = 0; i < count; i++)
    {
        const EntryView e = ReadEntry(data, i);
        if (e.nameLength == 0 || e.nameOffset > imageSize || e.nameLength > imageSize - e.nameOffset ||
            e.dataOffset > imageSize || e.dataSize > imageSize - e.dataOffset)
        {
            return false;
        }
        if (i > 0)
        {
            const EntryView prev = ReadEntry(data, i - 1);
            if (CompareNames(data + prev.nameOffset, prev.nameLength, data + e.nameOffset, e.nameLength) >= 0)
            {
                return false;
            }
        }
    }
    m_Data = data;
    m_Count = count;
    return true;
}

bool FileImage::Find(const char* name, size_t nameLength, const uint8_t*& data, size_t& size) const
{
    if (!m_Data || !name)
    {
        return false;
    }
    const uint8_t* key = reinterpret_cast<const uint8_t*>(name);
    uint32_t low = 0;
    uint32_t high = m_Count;
    while (low < high)
    {
        const uint32_t mid = low + (high - low) / 2;
        const EntryView e = ReadEntry(m_Data, mid);
        const int c = CompareNames(m_Data + e.nameOffset, e.nameLength, key, nameLength);
        if (c == 0)
        {
            data = m_Data + e.dataOffset;
            size = e.dataSize;
            return true;
        }
        if (c < 0)
        {
            low = mid + 1;
        }
        else
        {
            high = mid;
        }
    }
    return false;
}

#ifdef DEKI_EDITOR

namespace
{

void PutU32(std::vector<uint8_t>& out, size_t at, uint64_t value)
{
    for (int i = 0; i < 4; i++)
    {
        out[at + i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

}  // namespace

bool FileImage::Write(std::vector<File> files, std::vector<uint8_t>& out, std::string& error)
{
    std::sort(files.begin(), files.end(), [](const File& a, const File& b) { return a.name < b.name; });
    for (size_t i = 0; i < files.size(); i++)
    {
        if (files[i].name.empty())
        {
            error = "a file has no name";
            return false;
        }
        if (i > 0 && files[i].name == files[i - 1].name)
        {
            error = "two files are named " + files[i].name;
            return false;
        }
    }

    // Header, entries, names, then the data.
    uint64_t at = kHeaderSize + files.size() * kEntrySize;
    std::vector<uint64_t> nameOffsets(files.size());
    for (size_t i = 0; i < files.size(); i++)
    {
        nameOffsets[i] = at;
        at += files[i].name.size();
    }
    std::vector<uint64_t> dataOffsets(files.size());
    for (size_t i = 0; i < files.size(); i++)
    {
        at = (at + kDataAlignment - 1) / kDataAlignment * kDataAlignment;
        dataOffsets[i] = at;
        at += files[i].bytes.size();
    }
    if (at > UINT32_MAX)
    {
        error = "the files come to more than 4 GB";
        return false;
    }

    out.assign(static_cast<size_t>(at), 0);
    std::memcpy(out.data(), kMagic, sizeof(kMagic));
    PutU32(out, 4, kVersion);
    PutU32(out, 8, files.size());
    PutU32(out, 12, at);
    for (size_t i = 0; i < files.size(); i++)
    {
        const size_t e = kHeaderSize + i * kEntrySize;
        PutU32(out, e, nameOffsets[i]);
        PutU32(out, e + 4, files[i].name.size());
        PutU32(out, e + 8, dataOffsets[i]);
        PutU32(out, e + 12, files[i].bytes.size());
        std::memcpy(out.data() + nameOffsets[i], files[i].name.data(), files[i].name.size());
        if (!files[i].bytes.empty())
        {
            std::memcpy(out.data() + dataOffsets[i], files[i].bytes.data(), files[i].bytes.size());
        }
    }
    return true;
}

#endif

}  // namespace Deki
