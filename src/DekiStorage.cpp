#include <deki/Storage.h>

namespace Deki
{


namespace Storage
{

const char* GetPrefix(Location location)
{
    switch (location)
    {
        case Flash: return "F:/";
        case SDCard: return "S:/";
        case Default: return "";
    }
    return "";
}

std::string BuildPath(Location location, const char* filename)
{
    return std::string(GetPrefix(location)) + (filename ? filename : "");
}

}  // namespace Storage

}  // namespace Deki
