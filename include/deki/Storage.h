#pragma once

#include <string>

namespace Deki
{


namespace Storage
{

enum Location
{
    Flash,   // Internal flash (F:/)
    SDCard,  // SD card (S:/)
    Default  // Use default filesystem (no prefix)
};

// Get the path prefix for a storage location
const char* GetPrefix(Location location);

// Build a full path: prefix + filename
std::string BuildPath(Location location, const char* filename);

}  // namespace Storage

}  // namespace Deki
