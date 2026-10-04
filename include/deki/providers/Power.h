#pragma once

#include <deki/providers/IPower.h>

// Holds the active power driver.
//
// The platform integration package (e.g. deki-esp32-integration) registers
// its IPower driver with SetCurrent() at init. Engine and app code get it
// with GetCurrent(), which returns nullptr when no driver is registered.
// Callers must null-check; the engine main loop treats nullptr as "always
// awake".

namespace Deki
{

class Power
{
public:
    static void SetCurrent(IPower* power);
    static IPower* GetCurrent();

private:
    static IPower* s_Current;
};

}  // namespace Deki
