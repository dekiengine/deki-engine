#pragma once

#include <cstdint>
#include <functional>

namespace Deki
{

/// Device power management (sleep and wake). The engine uses it through the
/// Power facade; the platform package loaded at runtime registers its driver
/// with Power::SetCurrent at init.
///
/// Sleep modes are two platform-neutral tiers, Light and Deep, which each
/// implementation maps to native modes:
///   ESP32:  Light -> esp_light_sleep, Deep -> esp_deep_sleep
///   nRF52:  Light -> System ON idle,  Deep -> System OFF
///
/// The main loop calls Tick() once per iteration. Tick() either blocks (real
/// light sleep, returning on wake) or, without hardware support, sets the
/// state to Sleeping; the loop checks GetState() after Tick() and skips Update
/// and Render while sleeping.
///
/// Boot counts as an OnScreenOn event, so one callback covers every wake
/// including the cold boot.
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
        const char* platformModeName;  // for diagnostics, such as "esp_light_sleep"
    };

    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;

    virtual State GetState() const = 0;
    virtual SleepInfo GetCurrentSleepInfo() const = 0;
    virtual bool SupportsMode(SleepMode mode) const = 0;

    // Resets the idle timer. Call it from input handlers, BLE events and the like.
    virtual void NotifyActivity() = 0;

    // Sleep after this many seconds without activity. 0 disables idle sleep.
    virtual void SetIdleTimeoutSec(int32_t seconds) = 0;
    // The mode idle sleep enters. Default: Light.
    virtual void SetIdleSleepMode(SleepMode mode) = 0;

    // Sleeps now, without waiting for the idle timer.
    virtual void RequestSleep(SleepMode mode) = 0;

    // GPIO that wakes the device (touch / button). level: 0 wake on low, 1 wake on high.
    virtual void SetWakeGpio(int gpioNum, int level) = 0;

    using SleepCallback = std::function<void(SleepInfo)>;
    virtual void RegisterOnScreenOn(SleepCallback cb) = 0;     // fired after wake
    virtual void RegisterOnBeforeSleep(SleepCallback cb) = 0;  // fired before sleep

    // Called once per main loop iteration.
    virtual void Tick() = 0;
};

}  // namespace Deki
