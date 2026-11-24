#include <deki/BootSplash.h>

namespace Deki::BootSplash
{

namespace
{
MakeScene s_Make = nullptr;
}

void SetMaker(MakeScene make)
{
    s_Make = make;
}

MakeScene GetMaker()
{
    return s_Make;
}

}  // namespace Deki::BootSplash
