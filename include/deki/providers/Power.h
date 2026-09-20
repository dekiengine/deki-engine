#pragma once

#include <deki/providers/IPower.h>

/**
 * @brief Active-driver registry and facade for power management.
 *
 * The platform integration package (e.g. deki-esp32-integration) registers its
 * IPower driver via SetCurrent() at init. Engine and app code reach the
 * active driver via GetCurrent(). Returns nullptr on platforms without a
 * registered driver - callers must null-check (the engine main loop treats
 * nullptr as "no power management, always awake").
 */

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
