#include <deki/reflection/ComponentFactory.h>

namespace Deki
{



ComponentFactory& ComponentFactory::Instance()
{
    static ComponentFactory instance;
    return instance;
}


}  // namespace Deki
