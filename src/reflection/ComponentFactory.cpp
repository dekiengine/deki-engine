#include <deki/reflection/ComponentFactory.h>

namespace Deki
{

ComponentFactory& ComponentFactory::Instance()
{
    static ComponentFactory s_Instance;
    return s_Instance;
}

}  // namespace Deki
