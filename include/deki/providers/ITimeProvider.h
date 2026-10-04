#pragma once

#include <cstdint>

namespace Deki
{

/// The platform's millisecond clock, such as SDL_GetTicks() on SDL3 or
/// esp_timer_get_time() / 1000 on ESP32.
class ITimeProvider
{
public:
    virtual ~ITimeProvider() = default;

    /// Monotonic time in milliseconds, from a platform-chosen start.
    virtual uint32_t GetTicksMs() const = 0;

    /// Sleeps `ms` milliseconds (SDL_Delay on SDL3, vTaskDelay on ESP32), for
    /// frame rate limiting. The default does nothing.
    virtual void DelayMs(uint32_t ms) const {}
};

}  // namespace Deki
