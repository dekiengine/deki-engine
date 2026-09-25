#include <deki/Storage.h>

namespace Deki
{


namespace Storage
{

const char* GetPrefix(Location location)
{
    switch (location)
    {
        case Internal: return "F:/";
        case External: return "S:/";
        case Default: return "";
    }
    return "";
}

const char* AssetRoot(Location location)
{
    switch (location)
    {
        case Internal: return "F:/assets/";
        case External: return "S:/";
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
