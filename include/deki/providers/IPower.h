#pragma once

#include <cstdint>
#include <functional>

/**
 * @brief Abstract interface for device power management (sleep / wake).
 *
 * Engine-core consumes this through the Power facade. Whichever platform
 * integration package is loaded at runtime registers its concrete driver via
 * Power::SetCurrent at init.
 *
 * Sleep modes are exposed as a platform-agnostic tier (Light / Deep) so the
 * same API serves ESP32, nRF52 etc. Each implementation maps the tiers
 * to native modes:
 *   ESP32:  Light -> esp_light_sleep, Deep -> esp_deep_sleep
 *   nRF52:  Light -> System ON idle,  Deep -> System OFF
 *
 * The engine main loop calls Tick() once per iteration. Tick() may either
 * block (real light sleep: function returns when wake fires) or set state to
 * Sleeping for soft / simulated sleep on platforms without HW support; the
 * loop checks GetState() after Tick() and skips Update / Render when sleeping.
 *
 * Boot is treated as an OnScreenOn event so app code that needs to run on
 * every wake can hook a single callback and have it cover the cold-boot path.
 */

namespace Deki
{

class IPower
{
public:
    virtual ~IPower() = default;

    enum class State
    {
        Awake,
        Sleeping
    };
    enum class SleepMode
    {
        Light,
        Deep
    };

    struct SleepInfo
    {
        SleepMode mode;
        const char* platformModeName;  // diagnostic, e.g. "esp_light_sleep"
    };

    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;

    virtual State GetState() const = 0;
    virtual SleepInfo GetCurrentSleepInfo() const = 0;
    virtual bool SupportsMode(SleepMode mode) const = 0;

    // Reset the idle timer. Call from input handlers, BLE events, etc.
    virtual void NotifyActivity() = 0;

    // Sleep after this many seconds without activity. 0 disables idle sleep.
    virtual void SetIdleTimeoutSec(int32_t seconds) = 0;
    // Mode the idle timer enters. Default: Light.
    virtual void SetIdleSleepMode(SleepMode mode) = 0;

    // Manually request sleep (skips idle timer).
    virtual void RequestSleep(SleepMode mode) = 0;

    // GPIO that wakes the device (touch / button). level: 0 wake on low, 1 wake on high.
    virtual void SetWakeGpio(int gpioNum, int level) = 0;

    using SleepCallback = std::function<void(SleepInfo)>;
    virtual void RegisterOnScreenOn(SleepCallback cb) = 0;     // fired after wake
    virtual void RegisterOnBeforeSleep(SleepCallback cb) = 0;  // fired before sleep

    // Called once per engine main loop iteration.
    virtual void Tick() = 0;
};

}  // namespace Deki
