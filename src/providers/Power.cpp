#include <deki/providers/Power.h>

namespace Deki
{

IPower* Power::s_Current = nullptr;

void Power::SetCurrent(IPower* power)
{
    s_Current = power;
}

IPower* Power::GetCurrent()
{
    return s_Current;
}

}  // namespace Deki
